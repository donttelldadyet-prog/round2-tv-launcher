/* Round2 (16 Nov 2026, owner: "own program" test): start an ELF as its OWN PS4 program instead of a thread inside GoldHEN's
   payload host. The method is ps4-payload-dev/elfldr's (GPLv3+, John Törnblom): bootstrap-elf.c's preparation + elfldr_spawn.
   Preparation (all in memory, gone at the next restart): allow ptrace (QA flags + one kernel check, only if the pattern is
   found), give THIS program full rights for the moment of the spawn, then put its rights back exactly as they were.
   Every step is written to /data/round2/own-spawn.log first. GPL-3.0. */
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ps4/kernel.h>
#include "elfldr.h"

#define SLOG "/data/round2/own-spawn.log"
void r2_slog(const char *fmt, ...) {
  char b[400];
  time_t t = time(0) + 8 * 3600; struct tm tm; gmtime_r(&t, &tm);
  int n = snprintf(b, sizeof b, "%02d/%02d %02d:%02d:%02d ", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min, tm.tm_sec);
  va_list ap; va_start(ap, fmt); n += vsnprintf(b + n, sizeof b - n - 2, fmt, ap); va_end(ap);
  if (n > (int)sizeof b - 2) n = sizeof b - 2;
  while (n && (b[n - 1] == '\n' || b[n - 1] == '\r')) n--;
  b[n++] = '\n';
  int fd = open(SLOG, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd >= 0) { if (write(fd, b, n) < 0) {} fsync(fd); close(fd); }
}
pid_t r2_waitpid(pid_t pid, int *st, int opt) { /* at most 10 s */
  for (int i = 0; i < 5000; i++) {
    pid_t r = (waitpid)(pid, st, opt | WNOHANG);
    if (r) return r;
    usleep(2000);
  }
  r2_slog("waitpid(%d): gave up after 10 s", (int)pid);
  errno = ETIMEDOUT;
  return -1;
}
pid_t r2_spawn_own(const char *name, const uint8_t *elf, size_t n, char *why, size_t wn) {
  unsigned char privcaps[16], caps[16]; memset(privcaps, 0xff, sizeof privcaps);
  uint8_t qa[16];
  unsigned int fw = kernel_get_fw_version() & 0xffff0000;
  intptr_t pt_patch = 0;
  unsigned long prison, rootdir, jaildir;
  r2_slog("---- own-program start of %s (%u bytes), firmware 0x%x, from program %d", name, (unsigned)n, fw, (int)getpid());
  if (fw != 0x9000000 && fw != 0x11000000) { snprintf(why, wn, "firmware 0x%x not on Round2's list (9.00 / 11.00)", fw); r2_slog("%s", why); return -1; }
  /* same pattern elfldr uses for 7.00–14.00; not found = already allowed (e.g. by GoldHEN) -> nothing is written */
  pt_patch = kernel_find_pattern(KERNEL_ADDRESS_IMAGE_BASE, KERNEL_IMAGE_SIZE, "48b8361000007e020000????????????????????????0f84190200004c8b");
  r2_slog("ptrace check: %s", pt_patch ? "found - will be switched off (until restart)" : "pattern not found - left alone");
  if (kernel_get_qaflags(qa)) { snprintf(why, wn, "can't read QA flags"); r2_slog("%s", why); return -1; }
  r2_slog("QA flags [1] = 0x%02x", qa[1]);
  if ((qa[1] & 3) != 3) { qa[1] |= 3; if (kernel_set_qaflags(qa)) { snprintf(why, wn, "can't set QA flags"); r2_slog("%s", why); return -1; } r2_slog("QA flags [1] set to 0x%02x (until restart)", qa[1]); }
  if (pt_patch) { pt_patch += 22; kernel_patch(pt_patch, 0, "\x90\x90\x90\x90\x90\x90", 6); r2_slog("ptrace check switched off"); }
  if (kernel_get_ucred_caps(-1, caps)) { snprintf(why, wn, "can't read this program's rights"); r2_slog("%s", why); return -1; }
  if (!(prison = kernel_get_ucred_prison(-1)) || !(rootdir = kernel_get_proc_rootdir(-1)) || !(jaildir = kernel_get_proc_jaildir(-1))) { snprintf(why, wn, "can't read this program's jail/root"); r2_slog("%s", why); return -1; }
  r2_slog("rights saved; raising them for the spawn");
  pid_t pid = -1; int err = 0;
  if ((err = kernel_set_ucred_caps(-1, privcaps))) r2_slog("set caps failed");
  else if ((err = kernel_set_ucred_prison(-1, KERNEL_ADDRESS_PRISON0))) r2_slog("set prison failed");
  else if ((err = kernel_set_proc_rootdir(-1, KERNEL_ADDRESS_ROOTVNODE))) r2_slog("set rootdir failed");
  else if ((err = kernel_set_proc_jaildir(-1, KERNEL_ADDRESS_ROOTVNODE))) r2_slog("set jaildir failed");
  if (!err) {
    char *const argv[] = { (char *)name, 0 };
    signal(SIGCHLD, SIG_IGN);
    r2_slog("spawning");
    pid = elfldr_spawn(-1, argv, (uint8_t *)elf, n);
    r2_slog("spawn returned %d", (int)pid);
  }
  int bad = 0;
  if (kernel_set_ucred_caps(-1, caps)) bad = 1;
  if (kernel_set_ucred_prison(-1, prison)) bad = 1;
  if (kernel_set_proc_rootdir(-1, rootdir)) bad = 1;
  if (kernel_set_proc_jaildir(-1, jaildir)) bad = 1;
  r2_slog("rights put back%s", bad ? " - WITH AN ERROR" : "");
  if (pid <= 0) snprintf(why, wn, "%s", err ? "couldn't raise rights for the spawn" : "the new program didn't start (see own-spawn.log)");
  else snprintf(why, wn, "started as program %d", (int)pid);
  return pid;
}
