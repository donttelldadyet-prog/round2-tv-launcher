/* Round2 PS4 (17 Nov 2026, owner: "let's try method b"): the PS4 "Second Screen" / PlayStation App protocol, so one PS4's
   helper can WAKE another PS4, SIGN its profile IN and PRESS ITS PS BUTTON - the hope being that this clears the
   "Welcome back to PlayStation" screen a Remote Play wake leaves behind.
   Steps follow dhleong/ps4-waker (lib/ps4socket.js, lib/packets.js; MIT) and BenjaminFaal/ps4-remote-control:
     UDP 987  "WAKEUP" + "LAUNCH" with user-credential  -> the PS4 opens TCP 997
     TCP 997  client hello -> server hello (16-byte seed) -> handshake (16-byte AES key, RSA-OAEP with the PS4 public key)
              -> login (AES-128-CBC from here on; first time with the 8-digit PIN from
                 Settings > Mobile App Connection Settings > Add Device) -> status -> remote keys / start a title -> bye
   The credential is SHA-256 of the profile's account ID written as a decimal number (BenjaminFaal UserCredentialUtil) -
   the published tools take it from Sony's phone app instead; whether the PS4 accepts ours is exactly what we test.
   Plain POSIX + mbedTLS: also built on a computer for the protocol test (ss/r2ss_test.py, -DR2SS_HOST). */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "mbedtls/aes.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"

#ifdef R2SS_HOST
static int rng(void *p, unsigned char *b, size_t n) { (void)p; FILE *f = fopen("/dev/urandom", "rb"); if (!f) return -1; size_t g = fread(b, 1, n, f); fclose(f); return g == n ? 0 : -1; }
const char *r2ss_pubkey_override = 0; /* the test uses its own key pair */
int r2ss_ddp_port = 987, r2ss_tcp_port = 997;
#else
int sceRandomGetRandomNumber(uint8_t *buf, size_t size);
static int rng(void *p, unsigned char *b, size_t n) { (void)p; while (n) { size_t k = n > 64 ? 64 : n; if (sceRandomGetRandomNumber(b, k)) return -1; b += k; n -= k; } return 0; }
static const char *r2ss_pubkey_override = 0;
static int r2ss_ddp_port = 987, r2ss_tcp_port = 997;
#endif

static const char PS4_PUB[] = "-----BEGIN PUBLIC KEY-----\n"
  "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAxfAO/MDk5ovZpp7xlG9J\n"
  "JKc4Sg4ztAz+BbOt6Gbhub02tF9bryklpTIyzM0v817pwQ3TCoigpxEcWdTykhDL\n"
  "cGhAbcp6E7Xh8aHEsqgtQ/c+wY1zIl3fU//uddlB1XuipXthDv6emXsyyU/tJWqc\n"
  "zy9HCJncLJeYo7MJvf2TE9nnlVm1x4flmD0k1zrvb3MONqoZbKb/TQVuVhBv7SM+\n"
  "U5PSi3diXIx1Nnj4vQ8clRNUJ5X1tT9XfVmKQS1J513XNZ0uYHYRDzQYujpLWucu\n"
  "ob7v50wCpUm3iKP1fYCixMP6xFm0jPYz1YQaMV35VkYwc40qgk3av0PDS+1G0dCm\n"
  "swIDAQAB\n"
  "-----END PUBLIC KEY-----\n";

/* account ID as the registry holds it (8 bytes, 16 hex) -> credential: SHA-256 hex of the little-endian number in decimal */
void r2ss_credential(const char *acct_hex, char out[65]) {
  unsigned long long v = 0;
  for (int i = 7; i >= 0; i--) { unsigned b = 0; sscanf(acct_hex + i * 2, "%2x", &b); v = (v << 8) | b; }
  char dec[24]; snprintf(dec, sizeof dec, "%llu", v);
  unsigned char h[32]; mbedtls_sha256((const unsigned char *)dec, strlen(dec), h, 0);
  for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", h[i]);
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void msleep(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&t, 0); }

typedef struct { int fd; mbedtls_aes_context enc, dec; uint8_t ivE[16], ivD[16]; int crypt; } ss_t;

static int wait_fd(int fd, int wr, int ms) {
  fd_set s; FD_ZERO(&s); FD_SET(fd, &s); struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
  return select(fd + 1, wr ? 0 : &s, wr ? &s : 0, 0, &tv);
}
static int send_all(ss_t *s, const uint8_t *b, size_t n) {
  while (n) { if (wait_fd(s->fd, 1, 5000) <= 0) return -1; ssize_t w = send(s->fd, b, n, 0); if (w <= 0) return -1; b += w; n -= w; }
  return 0;
}
static int recv_n(ss_t *s, uint8_t *b, size_t n, int ms) {
  while (n) { if (wait_fd(s->fd, 0, ms) <= 0) return -1; ssize_t r = recv(s->fd, b, n, 0); if (r <= 0) return -1; b += r; n -= r; }
  return 0;
}
/* send a packet: plain before the handshake, AES-128-CBC (padded to 16, one running chain) after it */
static int ss_send(ss_t *s, uint8_t *p, size_t n) {
  if (!s->crypt) return send_all(s, p, n);
  uint8_t buf[512]; size_t m = (n + 15) & ~15u; if (m > sizeof buf) return -1;
  memset(buf, 0, m); memcpy(buf, p, n);
  uint8_t out[512]; if (mbedtls_aes_crypt_cbc(&s->enc, MBEDTLS_AES_ENCRYPT, m, s->ivE, buf, out)) return -1;
  return send_all(s, out, m);
}
/* read one encrypted packet; returns its type, fills *len and the plain bytes */
static int ss_recv(ss_t *s, uint8_t *p, size_t cap, uint32_t *len, int ms) {
  uint8_t c[16]; if (recv_n(s, c, 16, ms)) return -1;
  if (mbedtls_aes_crypt_cbc(&s->dec, MBEDTLS_AES_DECRYPT, 16, s->ivD, c, p)) return -1;
  uint32_t L = get32(p); *len = L;
  size_t m = (L + 15) & ~15u; if (L < 8 || m > cap) return -2;
  for (size_t o = 16; o < m; o += 16) { if (recv_n(s, c, 16, ms)) return -1; if (mbedtls_aes_crypt_cbc(&s->dec, MBEDTLS_AES_DECRYPT, 16, s->ivD, c, p + o)) return -1; }
  return (int)get32(p + 4);
}

static int ddp_send(const char *ip, const char *type, const char *cred) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0); if (fd < 0) return -1;
  int one = 1; setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
  struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(r2ss_ddp_port);
  inet_pton(AF_INET, ip, &a.sin_addr);
  char m[300]; int n;
  if (!strcmp(type, "WAKEUP"))
    n = snprintf(m, sizeof m, "WAKEUP * HTTP/1.1\nclient-type:a\nauth-type:C\nuser-credential:%s\ndevice-discovery-protocol-version:00020020\n", cred);
  else n = snprintf(m, sizeof m, "%s * HTTP/1.1\nuser-credential:%s\ndevice-discovery-protocol-version:00020020\n", type, cred);
  int r = sendto(fd, m, n, 0, (struct sockaddr *)&a, sizeof a) == n ? 0 : -1;
  close(fd); return r;
}

#define LOG(...) do { if (log) { size_t _l = strlen(log); snprintf(log + _l, logn - _l, __VA_ARGS__); } } while (0)
static const char *login_err(int r) { return r == 20 ? "PIN needed" : r == 22 ? "passcode needed" : r == 24 ? "passcode wrong" : r == 30 ? "login manager busy" : r == 0 ? "ok" : "refused"; }

/* ops (comma list): "ps", "enter", "back", "up", "down", "boot-CUSA12345", "wait-<ms>"; "" = login only.
   pin "" = already registered.  Returns 0 on login ok, the PS4's login code (>0) when refused, <0 on a network/protocol fault.
   log gets one short line per step (shown in the look result). */
int r2ss_run(const char *ip, const char *acct_hex, const char *pin, const char *ops, int wake_first, char *log, size_t logn) {
  if (log && logn) log[0] = 0;
  char cred[65]; r2ss_credential(acct_hex, cred);
  LOG("cred %.8s.. ", cred);
  if (wake_first) { for (int i = 0; i < 3; i++) { ddp_send(ip, "WAKEUP", cred); msleep(300); } LOG("wakeup sent; "); msleep(wake_first > 1 ? wake_first : 1000); }
  ddp_send(ip, "LAUNCH", cred); msleep(300); ddp_send(ip, "LAUNCH", cred); msleep(700);

  ss_t S; memset(&S, 0, sizeof S); S.fd = -1;
  int ret = -1;
  mbedtls_pk_context pk; mbedtls_pk_init(&pk);
  mbedtls_aes_init(&S.enc); mbedtls_aes_init(&S.dec);
  S.fd = socket(AF_INET, SOCK_STREAM, 0); if (S.fd < 0) { LOG("no socket"); goto out; }
  { struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons(r2ss_tcp_port); inet_pton(AF_INET, ip, &a.sin_addr);
    int fl = fcntl(S.fd, F_GETFL, 0); fcntl(S.fd, F_SETFL, fl | O_NONBLOCK);
    int c = connect(S.fd, (struct sockaddr *)&a, sizeof a);
    if (c && errno != EINPROGRESS) { LOG("tcp 997 refused (%d)", errno); ret = -2; goto out; }
    if (c) { int e = 0; socklen_t el = sizeof e;
      if (wait_fd(S.fd, 1, 6000) <= 0 || getsockopt(S.fd, SOL_SOCKET, SO_ERROR, &e, &el) || e) { LOG("tcp 997 no answer (%d)", e); ret = -2; goto out; } }
    fcntl(S.fd, F_SETFL, fl & ~O_NONBLOCK); }
  LOG("tcp ok; ");
  uint8_t p[512];
  memset(p, 0, 28); put32(p, 28); put32(p + 4, 0x6f636370); put32(p + 8, 0x20000);
  if (ss_send(&S, p, 28)) { LOG("hello send failed"); goto out; }
  uint8_t sh[64]; if (recv_n(&S, sh, 36, 6000)) { LOG("no server hello"); ret = -3; goto out; }
  uint32_t shl = get32(sh); if (shl > 36 && shl <= sizeof sh) recv_n(&S, sh + 36, shl - 36, 2000);
  if (get32(sh + 4) != 0x6f636370) { LOG("odd hello type %08x", get32(sh + 4)); ret = -3; goto out; }
  if (get32(sh + 12)) { LOG("hello status %u", get32(sh + 12)); ret = -3; goto out; }
  uint8_t seed[16]; memcpy(seed, sh + 20, 16);
  LOG("hello ok; ");
  /* handshake: our AES key (all zero, as ps4-waker) wrapped with RSA-OAEP(SHA-1) under the PS4's public key */
  uint8_t key[16]; memset(key, 0, 16);
  const char *pub = r2ss_pubkey_override ? r2ss_pubkey_override : PS4_PUB;
  if (mbedtls_pk_parse_public_key(&pk, (const unsigned char *)pub, strlen(pub) + 1)) { LOG("key parse failed"); goto out; }
  mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
  mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1);
  uint8_t ek[256];
  if (mbedtls_rsa_rsaes_oaep_encrypt(rsa, rng, 0, 0, 0, 16, key, ek)) { LOG("rsa failed"); goto out; }
  memset(p, 0, 280); put32(p, 280); put32(p + 4, 32); memcpy(p + 8, ek, 256); memcpy(p + 264, seed, 16);
  if (ss_send(&S, p, 280)) { LOG("handshake send failed"); goto out; }
  mbedtls_aes_setkey_enc(&S.enc, key, 128); mbedtls_aes_setkey_dec(&S.dec, key, 128);
  memcpy(S.ivE, seed, 16); memcpy(S.ivD, seed, 16); S.crypt = 1;
  /* login */
  memset(p, 0, 384); put32(p, 384); put32(p + 4, 30);
  /* p+8: 4-byte passcode (none) */ put32(p + 12, 513);
  memcpy(p + 16, cred, 64);
  memcpy(p + 80, "PlayStation", 11);           /* app label, 256 */
  memcpy(p + 336, "4.4", 3);                   /* os version, 16 */
  memcpy(p + 352, "Round2", 6);                /* model shown on the PS4, 16 */
  if (pin && pin[0]) memcpy(p + 368, pin, strlen(pin) > 16 ? 16 : strlen(pin));
  if (ss_send(&S, p, 384)) { LOG("login send failed"); goto out; }
  int lr = -9; uint32_t L = 0;
  for (int i = 0; i < 6; i++) {
    int t = ss_recv(&S, p, sizeof p, &L, 8000);
    if (t < 0) { LOG("no login answer (%d)", t); ret = -4; goto out; }
    if (t == 7) { lr = (int)get32(p + 8); break; }
    LOG("pkt %d; ", t);
  }
  LOG("login %d %s; ", lr, login_err(lr));
  if (lr != 0) { ret = lr > 0 ? lr : -4; goto out; }
  ret = 0;
  memset(p, 0, 12); put32(p, 12); put32(p + 4, 20); ss_send(&S, p, 12); /* our status */
  /* operations */
  char opbuf[128]; snprintf(opbuf, sizeof opbuf, "%s", ops ? ops : "");
  int rc_open = 0;
  for (char *tok = strtok(opbuf, ","); tok; tok = strtok(0, ",")) {
    if (!strncmp(tok, "wait-", 5)) { msleep(atoi(tok + 5)); continue; }
    if (!strncmp(tok, "boot-", 5)) {
      memset(p, 0, 24); put32(p, 24); put32(p + 4, 10); memcpy(p + 8, tok + 5, strlen(tok + 5) > 16 ? 16 : strlen(tok + 5));
      ss_send(&S, p, 24);
      int t = ss_recv(&S, p, sizeof p, &L, 8000); LOG("boot %s: %d/%d; ", tok + 5, t, t == 11 ? (int)get32(p + 8) : -1);
      continue;
    }
    int k = !strcmp(tok, "ps") ? 128 : !strcmp(tok, "enter") ? 16 : !strcmp(tok, "back") ? 32 : !strcmp(tok, "up") ? 1 :
            !strcmp(tok, "down") ? 2 : !strcmp(tok, "left") ? 8 : !strcmp(tok, "right") ? 4 : !strcmp(tok, "option") ? 64 : 0;
    if (!k) { LOG("? %s; ", tok); continue; }
    if (!rc_open) { memset(p, 0, 16); put32(p, 16); put32(p + 4, 28); put32(p + 8, 1024); ss_send(&S, p, 16); rc_open = 1; msleep(400); }
    memset(p, 0, 16); put32(p, 16); put32(p + 4, 28); put32(p + 8, k); ss_send(&S, p, 16);
    msleep(k == 128 ? 200 : 100);
    if (k != 128) { memset(p, 0, 16); put32(p, 16); put32(p + 4, 28); put32(p + 8, 256); ss_send(&S, p, 16); }
    msleep(k == 128 ? 1000 : 300);
    LOG("key %s; ", tok);
  }
  if (rc_open) { memset(p, 0, 16); put32(p, 16); put32(p + 4, 28); put32(p + 8, 2048); ss_send(&S, p, 16); msleep(300); }
  memset(p, 0, 8); put32(p, 8); put32(p + 4, 4); ss_send(&S, p, 8); /* bye */
  msleep(200);
out:
  if (S.fd >= 0) close(S.fd);
  mbedtls_pk_free(&pk); mbedtls_aes_free(&S.enc); mbedtls_aes_free(&S.dec);
  return ret;
}

/* wake only (type C, the Second Screen kind) */
int r2ss_wake(const char *ip, const char *acct_hex) {
  char cred[65]; r2ss_credential(acct_hex, cred);
  int r = 0; for (int i = 0; i < 3; i++) { r |= ddp_send(ip, "WAKEUP", cred); msleep(300); }
  return r;
}
