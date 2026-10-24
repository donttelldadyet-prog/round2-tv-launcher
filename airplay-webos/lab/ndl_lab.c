/*
 * Round2 AirPlay lab, step 1: can this LG TV show a picture through NDL DirectMedia v2?
 *
 * webOS 5+ removed libAcbAPI, which UxAir / UxPlay webOS use for the video plane. NDL
 * DirectMedia v2 (libNDL_directmedia, webOS 5+) owns the video plane itself, takes H.264
 * plus PCM audio with microsecond timestamps, and is what the AirPlay receiver would feed.
 *
 * This program plays the bundled test.h264 (Annex-B, one access unit per AUD) and test.pcm
 * (S16LE stereo 48 kHz) once, in real time, and writes what happened to a log file named by
 * the launch parameter {"log": "/path"}. Round2 Home's "AirPlay lab" reads that log.
 *
 * Exit: Back / Exit on the remote, or the end of the clip.
 */
#define NDL_DIRECTMEDIA_API_VERSION 2

#include <NDL_directmedia.h>
#include <SDL.h>
#include <SDL_webOS.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* not exported by every firmware's SDL: weak, so a missing one doesn't stop the app loading */
extern SDL_bool SDL_webOSGetPanelResolution(int *width, int *height) __attribute__((weak));
extern SDL_bool SDL_webOSCursorVisibility(SDL_bool visible) __attribute__((weak));

#define FPS 30
#define RATE 48000
#define CHANNELS 2
#define AUDIO_CHUNK (RATE * CHANNELS * 2 / FPS) /* bytes of PCM per video frame */

static int64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static unsigned char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = n > 0 ? malloc((size_t) n) : NULL;
    if (buf && fread(buf, 1, (size_t) n, f) != (size_t) n) { free(buf); buf = NULL; }
    fclose(f);
    *len = buf ? (size_t) n : 0;
    return buf;
}

/* next start code (00 00 01 or 00 00 00 01) at or after pos; returns its offset or len */
static size_t next_start(const unsigned char *b, size_t len, size_t pos) {
    for (size_t i = pos; i + 3 <= len; i++) {
        if (b[i] == 0 && b[i + 1] == 0 && (b[i + 2] == 1 || (i + 4 <= len && b[i + 2] == 0 && b[i + 3] == 1)))
            return i;
    }
    return len;
}
static int nal_type_at(const unsigned char *b, size_t len, size_t sc) {
    size_t p = sc + (b[sc + 2] == 1 ? 3 : 4);
    return p < len ? (b[p] & 0x1f) : -1;
}
/* access units: the stream is encoded with an AUD (type 9) before every picture */
static size_t next_access_unit(const unsigned char *b, size_t len, size_t start) {
    size_t sc = next_start(b, len, start + 3);
    while (sc < len && nal_type_at(b, len, sc) != 9) sc = next_start(b, len, sc + 3);
    return sc;
}

static volatile int g_events[64];
static void on_media_event(int type, long long num, const char *str) {
    fprintf(stderr, "[ndl] event 0x%x %lld %s\n", type, num, str ? str : "");
    if (type >= 0 && type < 64) g_events[type]++;
}
static void on_released(const char *type) {
    fprintf(stderr, "[ndl] resource taken by the TV: %s\n", type ? type : "?");
}

static void param_log(const char *params) {
    /* {"log":"/tmp/..."} -> stderr and stdout go to that file */
    const char *k = params ? strstr(params, "\"log\"") : NULL;
    if (!k) return;
    k = strchr(k + 5, '"');
    if (!k) return;
    char path[256];
    size_t i = 0;
    for (k++; *k && *k != '"' && i + 1 < sizeof(path); k++) path[i++] = *k;
    path[i] = 0;
    if (strncmp(path, "/tmp/", 5) != 0) return;
    if (freopen(path, "w", stderr)) { setvbuf(stderr, NULL, _IOLBF, 0); dup2(fileno(stderr), fileno(stdout)); }
}

int main(int argc, char *argv[]) {
    param_log(argc > 1 && argv[1][0] == '{' ? argv[1] : NULL);
    const char *app_id = getenv("APPID");
    if (!app_id || !*app_id) app_id = "sg.round2.lab.ndl";
    fprintf(stderr, "[lab] start %s\n", app_id);

    char dir[512];
    ssize_t n = readlink("/proc/self/exe", dir, sizeof(dir) - 32);
    if (n <= 0) { strcpy(dir, "."); n = 1; }
    dir[n] = 0;
    char *slash = strrchr(dir, '/'); if (slash) *slash = 0;
    char vpath[600], apath[600];
    snprintf(vpath, sizeof(vpath), "%s/test.h264", dir);
    snprintf(apath, sizeof(apath), "%s/test.pcm", dir);
    size_t vlen = 0, alen = 0;
    unsigned char *video = slurp(vpath, &vlen), *audio = slurp(apath, &alen);
    if (!video) { fprintf(stderr, "[lab] RESULT FAIL no test video at %s\n", vpath); return 1; }
    fprintf(stderr, "[lab] video %zu bytes, audio %zu bytes\n", vlen, alen);

    setenv("EGL_PLATFORM", "wayland", 0);
    setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 0);
    SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, "true");
    SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_EXIT, "true");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "[lab] RESULT FAIL SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_DisplayMode mode;
    SDL_GetCurrentDisplayMode(0, &mode);
    SDL_Window *win = SDL_CreateWindow("Round2 AirPlay lab", 0, 0, mode.w, mode.h, SDL_WINDOW_FULLSCREEN);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (!ren) { fprintf(stderr, "[lab] RESULT FAIL window: %s\n", SDL_GetError()); return 1; }
    if (SDL_webOSCursorVisibility) SDL_webOSCursorVisibility(SDL_FALSE);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);   /* writes alpha 0 straight through */
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);        /* transparent: the video plane shows through */
    SDL_RenderClear(ren);
    SDL_RenderPresent(ren);
    int pw = mode.w, ph = mode.h;
    if (SDL_webOSGetPanelResolution) { int w = 0, h = 0; if (SDL_webOSGetPanelResolution(&w, &h) && w > 0 && h > 0) { pw = w; ph = h; } }
    fprintf(stderr, "[lab] window %dx%d panel %dx%d\n", mode.w, mode.h, pw, ph);

    if (NDL_DirectMediaInit(app_id, on_released) != 0) {
        fprintf(stderr, "[lab] RESULT FAIL NDL_DirectMediaInit: %s\n", NDL_DirectMediaGetError());
        return 1;
    }
    NDL_DirectMediaSetAppState(NDL_DIRECTMEDIA_APP_STATE_FOREGROUND);

    NDL_DIRECTMEDIA_DATA_INFO_T info;
    memset(&info, 0, sizeof(info));
    info.video.width = 1920; info.video.height = 1080; info.video.type = NDL_VIDEO_TYPE_H264;
    if (audio) {
        info.audio.pcm.type = NDL_AUDIO_TYPE_PCM;
        info.audio.pcm.format = NDL_DIRECTMEDIA_AUDIO_PCM_FORMAT_S16LE;
        info.audio.pcm.layout = "interleaved";
        info.audio.pcm.channelMode = NDL_DIRECTMEDIA_AUDIO_PCM_MODE_STEREO;
        info.audio.pcm.sampleRate = NDL_DIRECTMEDIA_AUDIO_PCM_SAMPLE_RATE_48KHZ;
    }

    /* Four tries, one after another, ~6 s each. The number of white squares in the top-left corner
     * says which try is playing, so the person watching can say which one showed the picture:
     *   1  timestamps in milliseconds, screen area left alone
     *   2  milliseconds, area set to 0,0,1920,1080
     *   3  microseconds, area 0,0,1920,1080
     *   4  milliseconds, area set to the panel size */
    int quit = 0;
    for (int phase = 1; phase <= 4 && !quit; phase++) {
        int pts_ms = phase != 3;
        memset((void *) g_events, 0, sizeof(g_events));
        if (NDL_DirectMediaLoad(&info, on_media_event) != 0) {
            fprintf(stderr, "[lab] try %d RESULT FAIL NDL_DirectMediaLoad: %s\n", phase, NDL_DirectMediaGetError());
            continue;
        }
        if (phase == 2 || phase == 3) NDL_DirectVideoSetArea(0, 0, 1920, 1080);
        if (phase == 4) NDL_DirectVideoSetArea(0, 0, pw, ph);
        fprintf(stderr, "[lab] try %d: pts in %s, area %s\n", phase, pts_ms ? "ms" : "us",
                phase == 1 ? "default" : phase == 4 ? "panel" : "1920x1080");

        long frames = 0, refused = 0, achunks = 0;
        size_t vpos = next_start(video, vlen, 0), apos = 0;
        int64_t t0 = 0, last_draw = 0;
        while (!quit && vpos < vlen && frames < FPS * 6) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                if (ev.type == SDL_QUIT) quit = 1;
                if (ev.type == SDL_KEYDOWN) {
                    int sc = (int) ev.key.keysym.scancode;
                    if (sc == SDL_WEBOS_SCANCODE_BACK || sc == SDL_WEBOS_SCANCODE_EXIT || sc == SDL_SCANCODE_ESCAPE) quit = 1;
                }
            }
            if (now_us() - last_draw > 100000) {   /* keep the window see-through, with the try number */
                last_draw = now_us();
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
                SDL_RenderClear(ren);
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
                for (int i = 0; i < phase; i++) { SDL_Rect r = { 60 + i * 110, 60, 80, 80 }; SDL_RenderFillRect(ren, &r); }
                SDL_RenderPresent(ren);
            }
            size_t end = next_access_unit(video, vlen, vpos);
            long long t_us = (long long) frames * 1000000 / FPS;
            long long pts = pts_ms ? t_us / 1000 : t_us;
            if (t0) {
                int64_t due = t0 + t_us, wait = due - now_us();
                if (wait > 1000) usleep((useconds_t) (wait > 20000 ? 20000 : wait));
                if (now_us() < due) continue;
            }
            if (NDL_DirectVideoPlay(video + vpos, (unsigned) (end - vpos), pts) != 0) {
                if (++refused % 50 == 1) fprintf(stderr, "[lab] try %d video refused (%ld): %s\n", phase, refused, NDL_DirectMediaGetError());
                usleep(10000);
                if (refused > 1500) break;
                continue;
            }
            if (!t0) t0 = now_us();
            if (audio && apos + AUDIO_CHUNK <= alen && NDL_DirectAudioPlay(audio + apos, AUDIO_CHUNK, pts) == 0) { apos += AUDIO_CHUNK; achunks++; }
            frames++;
            vpos = end;
        }
        int rbl = -1; NDL_DirectVideoGetRenderBufferLength(&rbl);
        fprintf(stderr, "[lab] try %d RESULT %s: %ld frames, %ld audio, refused %ld, render buffer %d, loaded %s, playing %s\n",
                phase, frames > FPS * 3 ? "OK" : "FAIL", frames, achunks, refused, rbl,
                g_events[0x16] ? "yes" : "no", g_events[0x1a] ? "yes" : "no");
        usleep(500000);
        NDL_DirectMediaUnload();
        usleep(500000);
    }
    NDL_DirectMediaQuit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    free(video);
    free(audio);
    return 0;
}
