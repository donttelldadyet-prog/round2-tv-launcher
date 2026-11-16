/* Computer test for tls/r2dns.c:  cc -o /tmp/r2dns_test tls/r2dns_test.c tls/r2dns.c && python3 tls/r2dns_test.py */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "r2tls.h"
int main(int argc, char **argv) {
  r2dns_set_port(5353);
  r2tls_set_dns(argc > 2 ? argv[2] : "127.0.0.1", "");
  char ip[32] = "-"; struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
  int r = argc > 3 ? r2dns_lookup_own(argv[1], ip, sizeof ip) : r2dns_lookup(argv[1], ip, sizeof ip);
  clock_gettime(CLOCK_MONOTONIC, &b);
  printf("%d %s %.1f\n", r, ip, (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9);
  return 0;
}
