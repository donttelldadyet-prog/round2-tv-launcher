/* Round2 PS4: small HTTPS client on mbedTLS (TLS 1.2) with the Mozilla root certificates built in.
   GPL-3.0 / Apache-2.0 (mbedTLS). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

int r2tls_request(const char *host, const char *method, const char *path, const char *headers, const char *body,
                  size_t blen, char *out, size_t outlen, char *err, size_t errlen) {
  int ret, status = -1;
  mbedtls_net_context net;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_entropy_context ent;
  mbedtls_ctr_drbg_context drbg;
  if (err && errlen) err[0] = 0;
  if (out && outlen) out[0] = 0;

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
  if ((ret = mbedtls_net_connect(&net, host, "443", MBEDTLS_NET_PROTO_TCP))) goto fail;
  if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT))) goto fail;
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
  mbedtls_ssl_conf_read_timeout(&conf, 15000);
  if ((ret = mbedtls_ssl_setup(&ssl, &conf))) goto fail;
  if ((ret = mbedtls_ssl_set_hostname(&ssl, host))) goto fail;
  mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, NULL, mbedtls_net_recv_timeout);
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
    if ((ret = mbedtls_ssl_write(&ssl, (const unsigned char *)req, n)) < 0) goto fail;
    size_t sent = 0;
    while (body && sent < blen) {
      ret = mbedtls_ssl_write(&ssl, (const unsigned char *)body + sent, blen - sent);
      if (ret < 0) goto fail;
      sent += ret;
    }
  }

  {
    size_t got = 0;
    static char resp[65536];
    static unsigned char scratch[16384];
    r2tls_last_bytes = 0;
    for (;;) {
      int room = got < sizeof resp - 1;
      ret = room ? mbedtls_ssl_read(&ssl, (unsigned char *)resp + got, sizeof resp - 1 - got)
                 : mbedtls_ssl_read(&ssl, scratch, sizeof scratch); /* keep counting, don't keep */
      if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
      if (ret <= 0) break;
      if (room) got += ret;
      r2tls_last_bytes += ret;
    }
    resp[got] = 0;
    if (got > 12 && !strncmp(resp, "HTTP/1.", 7)) status = atoi_safe(resp + 9);
    char *b = strstr(resp, "\r\n\r\n");
    if (out && outlen) snprintf(out, outlen, "%s", b ? b + 4 : resp);
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

int atoi_safe(const char *s) {
  int v = 0;
  while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
  return v;
}
