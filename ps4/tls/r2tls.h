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
/* 1.1.26: name lookup with a time limit (tls/r2dns.c) + notes on which step the helper is in */
int r2dns_lookup(const char *host, char *out, size_t outn);   /* 0 = found (dotted address in out); never waits more than ~10 s */
int r2dns_lookup_own(const char *host, char *out, size_t outn); /* only the PS4's own servers; -1 = blocked or no answer */
void r2tls_set_dns(const char *a, const char *b);             /* the PS4's own DNS servers, asked first */
void r2dns_set_port(int p);                                   /* test only */
void r2_where(const char *name, int save);                    /* "I'm in this step now"; a step over a minute gets a line in /data/round2/slow.log */
void r2_where_start(const char *version);
