// round2_input – Round2 "is anyone playing?" watcher (GoldHEN plugin, loads inside each game/app).
// Watch-only: hooks scePadRead / scePadReadState the same proven way as GoldHEN's gamepad_helper (jocover),
// calls the real function first, NEVER changes the data the game gets, and only counts "activity".
// A background thread turns that count into a time and writes ONE small file for the Round2 helper:
//   /data/round2/input/last   "r2in 1 <last_input_epoch> <now_epoch> <titleid> <pid> <reads> <active_reads>\n"
// ASLEEP BY DEFAULT (owner, 18 Nov 6:01pm: "avoid an always-on detector, only detect once end session is reached"):
//   it only watches while /data/round2/input/arm exists and holds a future time ("<until_epoch>"). The Round2 helper
//   writes it when the end-of-session countdown starts and deletes it when the countdown ends. Asleep = the hook does one
//   flag check and returns; nothing is written. A forgotten arm file runs out on its own (until_epoch, max 2 h ahead).
// Off switch: the file /data/round2/input/off  -> stops counting and writing within 2 s (hooks stay, do nothing).
//
// Safety rules (owner, 18 Nov 2026: "account for overflows etc, we need this to work perfectly"):
//  - the hook body: no allocation, no syscalls, no locks, no loops beyond the count the API returned (capped at 64);
//  - every buffer has a fixed size and every string write is bounded (snprintf / explicit NUL);
//  - file writes go to a temp file then rename (the helper never reads a half-written file);
//  - if anything in setup fails, the plugin stays out of the way (returns 0, hooks nothing).

#include "plugin_common.h"
#include "Common.h"
#include <Patcher.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include "pad.h"

attr_public const char *g_pluginName = "round2_input";
attr_public const char *g_pluginDesc = "Round2: notes when the controller was last used";
attr_public const char *g_pluginAuth = "Round2";
attr_public u32 g_pluginVersion = 0x00000100; // 1.00

#define R2_DIR      "/data/round2/input"
#define R2_LAST     R2_DIR "/last"
#define R2_TMP      R2_DIR "/last.tmp"
#define R2_OFF      R2_DIR "/off"
#define R2_ARM      R2_DIR "/arm"
#define ARM_MAX     7200    // an arm file further ahead than 2 h is ignored
#define STICK_DEAD  40      // |axis-128| > 40 counts as "stick held" (a resting/drifting stick is ~0-20)
#define TRIG_DEAD   40      // analog L2/R2 above this counts
#define BTN_MASK    0x001FFFFEu  // all real buttons (L3..SQUARE, TOUCH_PAD); not bit 31 = INTERCEPTED (system has the pad)
#define MAX_ENTRIES 64      // never look at more than this many entries from one read, whatever the API returns

HOOK_INIT(scePadRead);
HOOK_INIT(scePadReadState);

static Patcher *g_patchRead = 0;
static Patcher *g_patchState = 0;

// Counters touched by the hook: plain 32-bit increments (wrap-around is harmless – the thread only compares
// "changed or not"; both are read with a single load).
static volatile u32 g_reads = 0;        // pad reads seen (proves the hook is alive)
static volatile u32 g_active = 0;       // reads that showed a pressed button / held stick / trigger
static volatile s32 g_off = 0;          // set by the thread when the off file exists
static volatile s32 g_armed = 0;        // 1 only while the helper's end-of-session countdown runs
static volatile s32 g_run = 0;          // thread keeps running while 1
static volatile s32 g_hooked = 0;

static char g_titleid[16];
static s32 g_pid = 0;

static inline int axis_out(u8 v, int dead) { int d = (int)v - 128; if (d < 0) d = -d; return d > dead; }

static inline int entry_active(const ScePadData *d) {
    if (!d->connected) return 0;
    if (d->buttons & 0x80000000u) return 0;               // system overlay owns the pad (PS menu etc.)
    if (d->buttons & BTN_MASK) return 1;
    if (axis_out(d->leftStick.x, STICK_DEAD) || axis_out(d->leftStick.y, STICK_DEAD)) return 1;
    if (axis_out(d->rightStick.x, STICK_DEAD) || axis_out(d->rightStick.y, STICK_DEAD)) return 1;
    if (d->analogButtons.l2 > TRIG_DEAD || d->analogButtons.r2 > TRIG_DEAD) return 1;
    if (d->touchData.touchNum > 0 && d->touchData.touchNum <= 2) return 1;
    return 0;
}

static inline void observe(const ScePadData *d, int n) {
    if (!g_armed || g_off || !d || n <= 0) return;
    if (n > MAX_ENTRIES) n = MAX_ENTRIES;
    g_reads++;
    for (int i = 0; i < n; i++) {
        if (entry_active(&d[i])) { g_active++; break; }
    }
}

int32_t scePadRead_hook(int32_t handle, ScePadData *pData, int32_t num) {
    int32_t ret = scePadReadExt(handle, pData, num);     // real read first, exactly like gamepad_helper
    if (ret > 0 && ret <= num) observe(pData, ret);
    return ret;                                          // data untouched
}

int32_t scePadReadState_hook(int32_t handle, ScePadData *pData) {
    int32_t ret = scePadReadStateExt(handle, pData);
    if (ret == 0) observe(pData, 1);
    return ret;
}

// ---------- background thread: time + file ----------
static int write_status(long long last_in, long long now) {
    char line[160];
    int n = snprintf(line, sizeof line, "r2in 1 %lld %lld %s %d %u %u\n",
                     last_in, now, g_titleid[0] ? g_titleid : "-", (int)g_pid, (unsigned)g_reads, (unsigned)g_active);
    if (n <= 0 || n >= (int)sizeof line) return -1;
    int fd = open(R2_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1;
    int w = (int)write(fd, line, (size_t)n);
    close(fd);
    if (w != n) { unlink(R2_TMP); return -1; }
    return rename(R2_TMP, R2_LAST);
}

static int off_file(void) { struct stat st; return stat(R2_OFF, &st) == 0; }

static int arm_file(long long now) {             // 1 = armed: file exists and holds a time from now to now+2h
    int fd = open(R2_ARM, O_RDONLY);
    if (fd < 0) return 0;
    char b[32];
    int n = (int)read(fd, b, sizeof b - 1);
    close(fd);
    if (n <= 0) return 0;
    b[n] = 0;
    long long until = 0;
    for (int i = 0; i < n && b[i] >= '0' && b[i] <= '9' && i < 12; i++) until = until * 10 + (b[i] - '0');
    return until > now && until <= now + ARM_MAX;
}

static void *r2_thread(void *arg) {
    (void)arg;
    u32 seen = g_active;
    long long last_in = 0, last_write = 0, last_written_in = -1;
    int tick = 0;
    while (g_run) {
        sleep(1);
        tick++;
        if ((tick % 2) == 0) {
            int o = off_file();
            if (o && !g_off) { g_off = 1; write_status(0, (long long)time(NULL)); }   // last word: "0 = nothing known"
            if (!o && g_off) g_off = 0;
        }
        if (g_off) continue;
        long long now = (long long)time(NULL);
        if ((tick % 2) == 0) {
            int arm = arm_file(now);
            if (arm && !g_armed) { seen = g_active; last_in = 0; last_written_in = -1; last_write = 0; g_armed = 1; }
            if (!arm && g_armed) { g_armed = 0; write_status(0, now); }   // last word when going back to sleep
        }
        if (!g_armed) continue;                  // asleep: no file writes at all
        u32 a = g_active;
        if (a != seen) { seen = a; last_in = now; }
        // write on new input at most once a second, otherwise a heartbeat every 5 s
        if (last_in != last_written_in || now - last_write >= 5) {
            if (write_status(last_in, now) == 0) { last_write = now; last_written_in = last_in; }
        }
    }
    scePthreadExit(NULL);
    return NULL;
}

s32 attr_public plugin_load(s32 argc, const char *argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    memset(g_titleid, 0, sizeof g_titleid);
    struct proc_info pi;
    memset(&pi, 0, sizeof pi);
    if (sys_sdk_proc_info(&pi) == 0) {
        memcpy(g_titleid, pi.titleid, sizeof g_titleid - 1);   // bounded, always NUL-terminated
        g_titleid[sizeof g_titleid - 1] = 0;
        for (int i = 0; g_titleid[i]; i++) {                    // keep the file one clean line
            char c = g_titleid[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { g_titleid[i] = 0; break; }
        }
        g_pid = pi.pid;
    }
    mkdir("/data/round2", 0777);
    mkdir(R2_DIR, 0777);
    if (off_file()) { final_printf("round2_input: off file present – not hooking\n"); return 0; }

    char module[256];
    int ml = snprintf(module, sizeof module, "/%s/common/lib/%s", sceKernelGetFsSandboxRandomWord(), "libScePad.sprx");
    if (ml <= 0 || ml >= (int)sizeof module) return 0;
    int h = 0;
    sys_dynlib_load_prx(module, &h);

    g_patchRead = (Patcher *)malloc(sizeof(Patcher));
    g_patchState = (Patcher *)malloc(sizeof(Patcher));
    if (!g_patchRead || !g_patchState) { final_printf("round2_input: no memory – not hooking\n"); return 0; }
    Patcher_Construct(g_patchRead);
    Patcher_Construct(g_patchState);
    uint8_t xor_ecx_ecx[5] = {0x31, 0xC9, 0x90, 0x90, 0x90};
    Patcher_Install_Patch(g_patchRead, (uint64_t)scePadReadExt, xor_ecx_ecx, sizeof(xor_ecx_ecx));
    uint8_t xor_edx_edx[5] = {0x31, 0xD2, 0x90, 0x90, 0x90};
    Patcher_Install_Patch(g_patchState, (uint64_t)scePadReadStateExt, xor_edx_edx, sizeof(xor_edx_edx));
    HOOK32(scePadRead);
    HOOK32(scePadReadState);
    g_hooked = 1;

    g_run = 1;
    OrbisPthread th;
    if (scePthreadCreate(&th, NULL, r2_thread, NULL, "round2_input") != 0) {
        g_run = 0;
        final_printf("round2_input: thread failed – hooks stay idle\n");
    }
    return 0;
}

s32 attr_public plugin_unload(s32 argc, const char *argv[]) {
    final_printf("[GoldHEN] <%s\\Ver.0x%08x> %s\n", g_pluginName, g_pluginVersion, __func__);
    g_run = 0;
    if (g_hooked) {
        UNHOOK(scePadRead);
        UNHOOK(scePadReadState);
        g_hooked = 0;
    }
    return 0;
}

s32 attr_module_hidden module_start(s64 argc, const void *args) { return 0; }
s32 attr_module_hidden module_stop(s64 argc, const void *args) { return 0; }
