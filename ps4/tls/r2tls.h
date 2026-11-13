#pragma once
#include <stddef.h>
/* HTTPS request over TLS 1.2 with certificate checking. Returns the HTTP status (e.g. 200) or -1 (see err).
   out gets the response body (cut to outlen). */
int r2tls_request(const char *host, const char *method, const char *path, const char *headers, const char *body,
                  size_t blen, char *out, size_t outlen, char *err, size_t errlen);
/* Binary download: *data = malloc'd body (chunked replies decoded), *len = its size, at most max bytes. Caller frees. */
int r2tls_fetch(const char *host, const char *method, const char *path, const char *headers, const char *body, size_t blen,
                unsigned char **data, size_t *len, size_t max, char *err, size_t errlen);
int atoi_safe(const char *s);
extern unsigned long long r2tls_last_bytes; /* bytes received by the last request */
