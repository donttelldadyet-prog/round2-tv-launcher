/* Round2 PS4: small HTTPS client on mbedTLS (TLS 1.2) with the Mozilla root certificates built in.
   GPL-3.0 / Apache-2.0 (mbedTLS). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "r2tls.h"

extern const char r2_ca_pem[];
int sceRandomGetRandomNumber(uint8_t *buf, size_t size);

/* entropy for mbedTLS: the PS4's hardware random number generator (max 64 bytes per call) */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
  (void)data;
  size_t done = 0;
  while (done < len) {
    size_t n = len - done > 64 ? 64 : len - done;
    if (sceRandomGetRandomNumber(output + done, n)) break;
    done += n;
  }
  *olen = done;
  return done ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}

unsigned long long r2tls_last_bytes = 0;
static mbedtls_x509_crt cacert;
static int ca_ready = 0;

/* 1.1.15 (16 Nov): every network step has a time limit. Bella (16 Nov morning) and Max (2:12pm update) had a helper that
   was alive but stuck – most likely a connect or send that never came back on weak Wi-Fi. Reads already stop after 15 s
   (mbedtls read timeout); now the connect gives up after 10 s per address and sends after 20 s. */
#define R2_CONNECT_S 10
#define R2_SEND_S 20
static int r2_connect(mbedtls_net_context *net, const char *host, const char *port) {
  /* 1.1.16: remember the last good address of each host in /data/round2/addr-<host>.txt and use it when the name can't be
     looked up or comes back blocked (0.0.0.0) – so a bad blocker setting can't cut the helper off from the engine.
     1.1.26: the lookup itself is r2dns_lookup (our own question to the DNS servers, a few seconds at most) – getaddrinfo
     had no time limit and was the one step that could still stand still for good. */
  char cpath[200], cached[64] = "", found[64] = "";
  snprintf(cpath, sizeof cpath, "/data/round2/addr-%s.txt", host);
  for (char *c = cpath + 18; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '.' || *c == '-' || *c == '/')) *c = '_';
  { FILE *cf = fopen(cpath, "r"); if (cf) { if (!fgets(cached, sizeof cached, cf)) cached[0] = 0; fclose(cf); cached[strcspn(cached, "\r\n")] = 0; } }
  r2_where("lookup", 1);
  if (r2dns_lookup(host, found, sizeof found)) found[0] = 0;
  if (!found[0] && !cached[0]) return MBEDTLS_ERR_NET_UNKNOWN_HOST;
  int ret = MBEDTLS_ERR_NET_CONNECT_FAILED;
  r2_where("connect", 1);
  for (int k = 0; k < 2; k++) {
    const char *ip = k == 0 ? found : cached;
    if (!ip[0] || (k == 1 && !strcmp(found, cached))) continue;
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)atoi_safe(port));
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) continue;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { ret = MBEDTLS_ERR_NET_SOCKET_FAILED; continue; }
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int ok = connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0;
    if (!ok && errno == EINPROGRESS) {
      fd_set wf; FD_ZERO(&wf); FD_SET(fd, &wf);
      struct timeval tv = { R2_CONNECT_S, 0 };
      int e = 0; socklen_t el = sizeof e;
      ok = select(fd + 1, 0, &wf, 0, &tv) > 0 && !getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el) && !e;
    }
    if (!ok) { close(fd); continue; }
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    struct timeval st = { R2_SEND_S, 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &st, sizeof st);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &st, sizeof st);
    net->fd = fd;
    ret = 0;
    if (strcmp(ip, cached)) { FILE *cf = fopen(cpath, "w"); if (cf) { fputs(ip, cf); fclose(cf); } }
    break;
  }
  return ret;
}

static int tls_go(const char *host, const char *method, const char *path, const char *headers, const char *body,
                  size_t blen, char **rbuf, size_t *rlen, size_t max, char *err, size_t errlen) {
  int ret, status = -1;
  mbedtls_net_context net;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_entropy_context ent;
  mbedtls_ctr_drbg_context drbg;
  if (err && errlen) err[0] = 0;
  *rbuf = 0;
  *rlen = 0;

  if (!ca_ready) {
    mbedtls_x509_crt_init(&cacert);
    ret = mbedtls_x509_crt_parse(&cacert, (const unsigned char *)r2_ca_pem, strlen(r2_ca_pem) + 1);
    if (ret < 0) { snprintf(err, errlen, "certificates: -0x%04x", -ret); return -1; }
    ca_ready = 1;
  }
  mbedtls_net_init(&net);
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&conf);
  mbedtls_entropy_init(&ent);
  mbedtls_ctr_drbg_init(&drbg);

  if ((ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, (const unsigned char *)"round2", 6))) goto fail;
  if ((ret = r2_connect(&net, host, "443"))) goto fail;
  if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT))) goto fail;
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
  mbedtls_ssl_conf_read_timeout(&conf, 15000);
  if ((ret = mbedtls_ssl_setup(&ssl, &conf))) goto fail;
  if ((ret = mbedtls_ssl_set_hostname(&ssl, host))) goto fail;
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, NULL, mbedtls_net_recv_timeout);
  r2_where("handshake", 1);
  while ((ret = mbedtls_ssl_handshake(&ssl))) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
      if (flags && flags != (uint32_t)-1) {
        char vb[256];
        mbedtls_x509_crt_verify_info(vb, sizeof vb, "", flags);
        snprintf(err, errlen, "certificate: %s", vb);
        goto done;
      }
      goto fail;
    }
  }

  {
    char req[1024];
    int n = snprintf(req, sizeof req,
                     "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Round2PS4\r\nConnection: close\r\n%s"
                     "Content-Length: %zu\r\n\r\n",
                     method, path, host, headers ? headers : "", body ? blen : 0);
    r2_where("send", 1);
    if ((ret = mbedtls_ssl_write(&ssl, (const unsigned char *)req, n)) < 0) goto fail;
    size_t sent = 0;
    while (body && sent < blen) {
      ret = mbedtls_ssl_write(&ssl, (const unsigned char *)body + sent, blen - sent);
      if (ret < 0) goto fail;
      sent += ret;
    }
  }

  {
    size_t got = 0, cap = 65536;
    char *resp = malloc(cap + 1);
    static unsigned char scratch[16384];
    r2tls_last_bytes = 0;
    r2_where("read", 1);
    for (;;) {
      if (resp && got == cap && cap < max) { size_t nc = cap * 2 > max ? max : cap * 2; char *x = realloc(resp, nc + 1); if (x) { resp = x; cap = nc; } }
      int room = resp && got < cap;
      ret = room ? mbedtls_ssl_read(&ssl, (unsigned char *)resp + got, cap - got)
                 : mbedtls_ssl_read(&ssl, scratch, sizeof scratch); /* past max: keep counting, don't keep */
      if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
      if (ret <= 0) break;
      if (room) got += ret;
      r2tls_last_bytes += ret;
    }
    if (resp) resp[got] = 0;
    if (resp && got > 12 && !strncmp(resp, "HTTP/1.", 7)) status = atoi_safe(resp + 9);
    *rbuf = resp;
    *rlen = got;
  }
  goto done;

fail:
  if (err && errlen) {
    char eb[128];
    mbedtls_strerror(ret, eb, sizeof eb);
    snprintf(err, errlen, "-0x%04x %s", -ret, eb);
  }
done:
  mbedtls_ssl_close_notify(&ssl);
  mbedtls_net_free(&net);
  mbedtls_ssl_free(&ssl);
  mbedtls_ssl_config_free(&conf);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&ent);
  return status;
}


/* text replies (small): body copied into out as a string */
int r2tls_request(const char *host, const char *method, const char *path, const char *headers, const char *body,
                  size_t blen, char *out, size_t outlen, char *err, size_t errlen) {
  char *r = 0;
  size_t n = 0;
  if (out && outlen) out[0] = 0;
  int st = tls_go(host, method, path, headers, body, blen, &r, &n, 65536, err, errlen);
  r2_netnote(path, st, err);
  if (r) {
    char *b = strstr(r, "\r\n\r\n");
    if (out && outlen) snprintf(out, outlen, "%s", b ? b + 4 : r);
    free(r);
  }
  return st;
}

static int hdr_has(const char *h, size_t hl, const char *name, const char *val) { /* case-insensitive "Name: ...val..." */
  size_t nl = strlen(name);
  for (size_t i = 0; i + nl < hl; i++) {
    if ((i == 0 || h[i - 1] == '\n') && !strncasecmp(h + i, name, nl) && h[i + nl] == ':') {
      const char *e = memchr(h + i, '\n', hl - i);
      size_t ll = e ? (size_t)(e - (h + i)) : hl - i;
      for (size_t k = 0; k + strlen(val) <= ll; k++) if (!strncasecmp(h + i + k, val, strlen(val))) return 1;
      return 0;
    }
  }
  return 0;
}

/* binary download (e.g. a new helper): *data = malloc'd body (chunked replies decoded), *len its size. Caller frees. */
int r2tls_fetch(const char *host, const char *method, const char *path, const char *headers, const char *body, size_t blen,
                unsigned char **data, size_t *len, size_t max, char *err, size_t errlen) {
  char *r = 0;
  size_t n = 0;
  *data = 0;
  *len = 0;
  int st = tls_go(host, method, path, headers, body, blen, &r, &n, max + 65536, err, errlen);
  if (!r) return st;
  char *b = 0;
  for (size_t i = 0; i + 3 < n; i++) if (!memcmp(r + i, "\r\n\r\n", 4)) { b = r + i + 4; break; }
  if (!b) { free(r); if (err) snprintf(err, errlen, "no reply body"); return -1; }
  size_t hl = b - r, bl = n - hl;
  if (hdr_has(r, hl, "Transfer-Encoding", "chunked")) { /* decode in place */
    size_t in = 0, o = 0;
    while (in < bl) {
      size_t sz = 0;
      while (in < bl && b[in] != '\r') {
        char c = b[in++];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0) { while (in < bl && b[in] != '\r') in++; break; } /* chunk extensions */
        sz = sz * 16 + v;
      }
      in += 2;
      if (!sz || in + sz > bl) break;
      memmove(b + o, b + in, sz);
      o += sz;
      in += sz + 2;
    }
    bl = o;
  }
  if (bl > max) { free(r); if (err) snprintf(err, errlen, "too big"); return -1; }
  unsigned char *d = malloc(bl ? bl : 1);
  if (!d) { free(r); if (err) snprintf(err, errlen, "no memory"); return -1; }
  memcpy(d, b, bl);
  free(r);
  *data = d;
  *len = bl;
  return st;
}

int atoi_safe(const char *s) {
  int v = 0;
  while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
  return v;
}
