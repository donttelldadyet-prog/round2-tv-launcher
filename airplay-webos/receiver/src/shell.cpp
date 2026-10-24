#include "shell.h"
#include "identity.h"
#include "usage_stats.h"
#include <SDL_webOS.h>
#include <SDL_ttf.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <algorithm>
#include <cmath>
static std::string log_path;
static volatile sig_atomic_t interrupted = 0;
static void signal_stop(int) { interrupted = 1; }
int64_t now_ns() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return int64_t(t.tv_sec)*1000000000 + t.tv_nsec; }
std::string app_directory() {
    char p[4096]; ssize_t n = readlink("/proc/self/exe", p, sizeof(p)-1);
    if (n <= 0) return ".";
    p[n] = 0; std::string s(p); return s.substr(0, s.rfind('/'));
}
// Round2/webOS 5: a Homebrew-installed native app may not be allowed to write into its own
// install folder. Use the first folder that accepts a test file: the app folder (persistent),
// then $HOME, then /tmp (pairing is then forgotten on restart).
static bool writable(const std::string& dir) {
    mkdir(dir.c_str(),0700);
    std::string probe=dir+"/.write-test";
    FILE *f=fopen(probe.c_str(),"w");
    if(!f) return false;
    bool ok=fputs("1",f)>=0; fclose(f); unlink(probe.c_str());
    return ok;
}
static std::string g_airplay_name=UXAIR_DISPLAY_NAME;
const std::string& airplay_name() { return g_airplay_name; }
void set_airplay_name(const std::string& name) { if(!name.empty()) g_airplay_name=name; }
std::string writable_directory() {
    static std::string chosen;
    if(!chosen.empty()) return chosen;
    std::string candidates[3]={app_directory(),"",std::string("/tmp/")+UXAIR_APP_ID};
    const char *home=getenv("HOME");
    if(home && *home) candidates[1]=std::string(home)+"/"+UXAIR_APP_ID;
    for(const std::string& c:candidates) if(!c.empty() && writable(c)) {chosen=c;return chosen;}
    chosen="/tmp";
    return chosen;
}
void start_log(const char *name) {
    std::string p = writable_directory() + "/" + name;
    log_path=p;
    rename(p.c_str(), (p+".previous").c_str());
    freopen(p.c_str(), "w", stderr);
    setvbuf(stderr, nullptr, _IOLBF, 0);
    // Proprietary drivers emit high-volume frame diagnostics to stdout.
    // Keep application/errors on stderr, without filling app storage with them.
    int nullfd=open("/dev/null",O_WRONLY);
    if(nullfd>=0){dup2(nullfd,STDOUT_FILENO);close(nullfd);}
}
void rotate_log() {
    struct stat s;
    if(!log_path.empty() && fstat(fileno(stderr),&s)==0 && s.st_size>1024*1024) {
        rename(log_path.c_str(),(log_path+".previous").c_str());
        freopen(log_path.c_str(),"w",stderr);setvbuf(stderr,nullptr,_IOLBF,0);
    }
}
bool Shell::open() {
    signal(SIGTERM, signal_stop); signal(SIGINT, signal_stop);
    input_ = InputMapper(static_cast<SDL_Scancode>(SDL_WEBOS_SCANCODE_BACK),
                         static_cast<SDL_Scancode>(SDL_WEBOS_SCANCODE_EXIT));
    setenv("EGL_PLATFORM", "wayland", 0); setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 0);
    SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, "true");
    SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_EXIT, "true");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "[SDL] %s\n",SDL_GetError()); return false; }
    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0,&mode) != 0) return false;
    window = SDL_CreateWindow(airplay_name().c_str(),0,0,mode.w,mode.h,SDL_WINDOW_FULLSCREEN);
    if (window) renderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
    if (!renderer) { fprintf(stderr,"[SDL] %s\n",SDL_GetError()); return false; }
    fprintf(stderr,"[SDL] graphics-window=%dx%d (logical app coordinates)\n",mode.w,mode.h);
    if(TTF_Init()!=0) return false;
    if(!load_fonts(app_directory()+"/fonts")) {fprintf(stderr,"[font] %s\n",TTF_GetError());return false;}
    return true;
}
void Shell::transparent() {
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer,0,0,0,0); SDL_RenderClear(renderer); SDL_RenderPresent(renderer);
}
Action Shell::action() {
    SDL_Event e;
    static Uint32 last_motion_log_ms = 0;
    pointer_select_ = false;
    auto update_pointer=[&](int x,int y) {if(!pointer_valid_||x!=pointer_x_||y!=pointer_y_)pointer_moved_=true;pointer_x_=x;pointer_y_=y;pointer_valid_=true;};
    while (SDL_PollEvent(&e)) {
        const Uint32 now = SDL_GetTicks();
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            if (!e.key.repeat)
                fprintf(stderr,"[input] key %s keycode=%ld scancode=%d repeat=%u\n",
                        e.type == SDL_KEYDOWN ? "down" : "up",
                        static_cast<long>(e.key.keysym.sym),
                        static_cast<int>(e.key.keysym.scancode), e.key.repeat);
        } else if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            update_pointer(e.button.x,e.button.y);
            fprintf(stderr,"[input] mouse %s button=%u clicks=%u x=%d y=%d\n",
                    e.type == SDL_MOUSEBUTTONDOWN ? "down" : "up",
                    e.button.button, e.button.clicks, e.button.x, e.button.y);
        } else if (e.type == SDL_MOUSEMOTION &&
                   (last_motion_log_ms == 0 || Uint32(now - last_motion_log_ms) >= 5000)) {
            update_pointer(e.motion.x,e.motion.y);
            fprintf(stderr,"[input] pointer motion x=%d y=%d buttons=%u\n",
                    e.motion.x, e.motion.y, e.motion.state);
            last_motion_log_ms = now;
        } else if(e.type==SDL_MOUSEMOTION) update_pointer(e.motion.x,e.motion.y);
        const Action mapped = input_.map(e, now);
        if (mapped != Action::None) {
            if (mapped==Action::Select && e.type==SDL_MOUSEBUTTONDOWN) {
                pointer_select_=true;update_pointer(e.button.x,e.button.y);
            }
            return mapped;
        }
    }
    return interrupted?Action::Exit:Action::None;
}
bool Shell::pointer_select(int& x, int& y) const {
    if (!pointer_select_) return false;
    x=pointer_x_;y=pointer_y_;return true;
}
bool Shell::take_pointer_motion() {bool moved=pointer_moved_;pointer_moved_=false;return moved;}
bool Shell::pump() {Action a=action();return a!=Action::Back&&a!=Action::Exit;}
Shell::~Shell() { close_fonts(); TTF_Quit(); if(renderer) SDL_DestroyRenderer(renderer); if(window) SDL_DestroyWindow(window); SDL_Quit(); }
