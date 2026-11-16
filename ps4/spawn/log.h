/* Round2 replacement for elfldr's log.h (GPL-3.0): every message goes to /data/round2/own-spawn.log, written and synced
   line by line, so if the PS4 program dies mid-way the file shows the last step. Also bounds every waitpid(). */
#pragma once
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "pt.h"
void r2_slog(const char *fmt, ...);
pid_t r2_waitpid(pid_t pid, int *st, int opt);
#define waitpid(p, s, o) r2_waitpid(p, s, o)
#define LOG_PUTS(s) r2_slog("%s", s)
#define LOG_PRINTF(s, ...) r2_slog(s, __VA_ARGS__)
#define LOG_PERROR(s) r2_slog("%s:%d:%s: %s", __FILE__, __LINE__, s, strerror(errno))
#define LOG_PT_PERROR(pid, s) r2_slog("%s:%d:%s: error in the new program", __FILE__, __LINE__, s)
#define klog_puts(s) r2_slog("%s", s)
#define klog_perror(s) r2_slog("%s: %s", s, strerror(errno))
