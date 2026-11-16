/* Round2 (16 Nov 2026, owner: "willing to commit to this feature" – remote wake): register this helper with a PS4 as a
   Remote Play device, the way chiaki does (chiaki-ng lib/src/regist.c, upstream 43728af; AGPL-3.0-only with OpenSSL exception –
   this file follows that code's steps and is under the same licence; rpcrypt.c beside it is chiaki's file unchanged).
   No Sony server is involved: the PS4 shows an 8-digit number (Settings › Remote Play Connection Settings › Add Device), we send
   it (as the key that encrypts the request) with a profile's account ID to the PS4's port 9295, and the PS4 answers with a
   "regist key". That key later wakes the PS4 from rest mode (UDP 987, WAKEUP auth-type R).
   PS4 firmware 7.00+ speaks Remote Play protocol "10.0" (chiaki target PS4_10) – our rooms are 9.00 and 11.00. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <chiaki/rpcrypt.h>

int sceRandomGetRandomNumber(uint8_t *buf, size_t size);
void r2_slog(const char *fmt, ...); /* step log, synced line by line: /data/round2/own-spawn.log (1.1.22: 1.1.21 died somewhere in here) */
#define RP_PORT 9295
static const char *client_type = "dabfa2ec873de5839bee8d3f4c0239c4282c07c25c6077a2931afcf0adc0d34f";

static void b64(const uint8_t *in, size_t n, char *out) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = T[v >> 18 & 63]; out[o++] = T[v >> 12 & 63];
    out[o++] = i + 1 < n ? T[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? T[v & 63] : '=';
  }
  out[o] = 0;
}
static int wait_fd(int fd, int wr, int ms) {
  fd_set s; FD_ZERO(&s); FD_SET(fd, &s);
  struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
  return select(fd + 1, wr ? 0 : &s, wr ? &s : 0, 0, &tv) > 0;
}
static const char *hdr_val(const char *text, const char *key, char *out, size_t on) { /* "Key: value\r\n" */
  size_t kl = strlen(key);
  out[0] = 0;
  for (const char *p = text; p && *p;) {
    if (!strncasecmp(p, key, kl) && p[kl] == ':') {
      p += kl + 1; while (*p == ' ') p++;
      size_t n = strcspn(p, "\r\n"); if (n > on - 1) n = on - 1;
      memcpy(out, p, n); out[n] = 0; return out;
    }
    p = strchr(p, '\n'); if (p) p++;
  }
  return 0;
}
/* 0 = registered: regkey = the wake key (8 hex characters, text), info = "nickname|mac|rp-key". Else why says what failed. */
int r2_rp_regist(const char *ip, const uint8_t acct[8], uint32_t pin, char *regkey, size_t rn, char *info, size_t in, char *why, size_t wn) {
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(RP_PORT);
  regkey[0] = 0; info[0] = 0;
  if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) { snprintf(why, wn, "bad address"); return -1; }
  r2_slog("rp regist: start, %s", ip);
  /* 1) search: "SRC2" -> "RES2" (the PS4 only takes a registration right after this) */
  int u = socket(AF_INET, SOCK_DGRAM, 0);
  if (u < 0) { snprintf(why, wn, "no socket"); return -1; }
  char r[64]; int got = 0;
  for (int t = 0; t < 3 && !got; t++) {
    sendto(u, "SRC2", 5, 0, (struct sockaddr *)&a, sizeof a);
    if (wait_fd(u, 0, 1500)) { ssize_t n = recv(u, r, sizeof r - 1, 0); if (n >= 4 && !memcmp(r, "RES2", 4)) got = 1; }
  }
  close(u);
  if (!got) { snprintf(why, wn, "no answer to the Remote Play search (Remote Play enabled? Add Device screen open?)"); return -2; }
  r2_slog("rp regist: search answered");
  usleep(100000);
  /* 2) the request: 0x1e0 filler bytes carrying the scrambled session key, then the encrypted inner header */
  uint8_t buf[0x400], amb[CHIAKI_RPCRYPT_KEY_SIZE], aero[0x10];
  ChiakiRPCrypt crypt;
  if (sceRandomGetRandomNumber(amb, sizeof amb)) { snprintf(why, wn, "no random numbers"); return -1; }
  r2_slog("rp regist: random ok");
  memset(buf, 'A', 0x1e0);
  size_t key_0_off = buf[0x18D] & 0x1F, key_1_off = buf[0] >> 3;
  if (chiaki_rpcrypt_init_regist(&crypt, CHIAKI_TARGET_PS4_10, amb, key_0_off, pin) != CHIAKI_ERR_SUCCESS ||
      chiaki_rpcrypt_aeropause(CHIAKI_TARGET_PS4_10, key_1_off, aero, crypt.ambassador) != CHIAKI_ERR_SUCCESS) { snprintf(why, wn, "key set-up failed"); return -1; }
  r2_slog("rp regist: keys ok");
  memcpy(buf + 0xc7, aero + 8, 8);
  memcpy(buf + 0x191, aero, 8);
  char id64[24]; b64(acct, 8, id64);
  int inner = snprintf((char *)buf + 0x1e0, sizeof buf - 0x1e0, "Client-Type: %s\r\nNp-AccountId: %s\r\n", client_type, id64);
  if (chiaki_rpcrypt_encrypt(&crypt, 0, buf + 0x1e0, buf + 0x1e0, inner) != CHIAKI_ERR_SUCCESS) { snprintf(why, wn, "encrypt failed"); return -1; }
  r2_slog("rp regist: encrypted");
  size_t psz = 0x1e0 + inner;
  char head[300];
  int hl = snprintf(head, sizeof head, "POST /sie/ps4/rp/sess/rgst HTTP/1.1\r\n HTTP/1.1\r\nHOST: 10.0.2.15\r\nUser-Agent: remoteplay Windows\r\n"
                    "Connection: close\r\nContent-Length: %llu\r\nRP-Version: 10.0\r\n\r\n", (unsigned long long)psz);
  /* 3) send it (TCP 9295), 5 s limits */
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) { snprintf(why, wn, "no socket"); return -1; }
  int fl = fcntl(s, F_GETFL, 0); fcntl(s, F_SETFL, fl | O_NONBLOCK);
  int ok = connect(s, (struct sockaddr *)&a, sizeof a) == 0;
  if (!ok && errno == EINPROGRESS && wait_fd(s, 1, 5000)) { int e = 0; socklen_t el = sizeof e; ok = !getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &el) && !e; }
  if (!ok) { close(s); snprintf(why, wn, "can't connect to the PS4's Remote Play port"); return -2; }
  r2_slog("rp regist: connected");
  fcntl(s, F_SETFL, fl & ~O_NONBLOCK);
  struct timeval tv = { 5, 0 }; setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv); setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  if (send(s, head, hl, 0) != hl || send(s, buf, psz, 0) != (ssize_t)psz) { close(s); snprintf(why, wn, "sending failed"); return -2; }
  r2_slog("rp regist: sent %d + %u bytes", hl, (unsigned)psz);
  /* 4) the answer */
  static char resp[0x900]; size_t rl = 0; char *body = 0; size_t clen = 0;
  for (;;) {
    ssize_t n = recv(s, resp + rl, sizeof resp - 1 - rl, 0);
    if (n <= 0) break;
    rl += n; resp[rl] = 0;
    if (!body) { char *e = strstr(resp, "\r\n\r\n"); if (e) { body = e + 4; char v[24]; char save = *body; *body = 0; if (hdr_val(resp, "Content-Length", v, sizeof v)) clen = strtoul(v, 0, 10); *body = save; } }
    if (body && rl >= (size_t)(body - resp) + clen) break;
    if (rl >= sizeof resp - 1) break;
  }
  close(s);
  r2_slog("rp regist: received %u bytes: %.40s", (unsigned)rl, rl ? resp : "");
  if (rl < 12 || strncmp(resp, "HTTP/1.", 7)) { snprintf(why, wn, "no answer to the registration (%u bytes)", (unsigned)rl); return -3; }
  int code = atoi(resp + 9);
  if (code != 200) {
    char rs[24] = "";
    if (body) body[-2] = 0;
    hdr_val(resp, "RP-Application-Reason", rs, sizeof rs);
    snprintf(why, wn, "PS4 refused: HTTP %d reason %s (wrong/expired number, or this profile's ID isn't accepted)", code, rs[0] ? rs : "-");
    return -4;
  }
  if (!body || !clen || rl < (size_t)(body - resp) + clen) { snprintf(why, wn, "answer cut short"); return -3; }
  chiaki_rpcrypt_decrypt(&crypt, 0, (uint8_t *)body, (uint8_t *)body, clen);
  body[clen] = 0;
  r2_slog("rp regist: decrypted %u bytes", (unsigned)clen);
  char hex[80], nick[64] = "", mac[32] = "", rpkey[80] = "";
  if (!hdr_val(body, "PS4-RegistKey", hex, sizeof hex)) { snprintf(why, wn, "answer had no regist key (decrypt wrong?)"); return -5; }
  size_t o = 0;
  for (size_t i = 0; hex[i] && hex[i + 1] && o < rn - 1; i += 2) { char b[3] = { hex[i], hex[i + 1], 0 }; char c = (char)strtoul(b, 0, 16); if (!c) break; regkey[o++] = c; }
  regkey[o] = 0;
  hdr_val(body, "PS4-Nickname", nick, sizeof nick); hdr_val(body, "PS4-Mac", mac, sizeof mac); hdr_val(body, "RP-Key", rpkey, sizeof rpkey);
  snprintf(info, in, "%s|%s|%s", nick, mac, rpkey);
  snprintf(why, wn, "registered");
  return 0;
}
/* wake a PS4 in rest mode with a Remote Play regist key (text, hex). Returns bytes sent or -1. */
int r2_rp_wake(const char *ip, const char *regkey) {
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(987);
  if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) return -1;
  unsigned long long cred = strtoull(regkey, 0, 16);
  char m[300];
  int n = snprintf(m, sizeof m, "WAKEUP * HTTP/1.1\nclient-type:vr\nauth-type:R\nmodel:w\napp-type:r\nuser-credential:%llu\ndevice-discovery-protocol-version:00020020\n", cred);
  int u = socket(AF_INET, SOCK_DGRAM, 0);
  if (u < 0) return -1;
  int one = 1; setsockopt(u, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
  int r = -1;
  for (int i = 0; i < 3; i++) { r = (int)sendto(u, m, n, 0, (struct sockaddr *)&a, sizeof a); usleep(300000); }
  close(u);
  return r;
}
