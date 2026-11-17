/* computer test driver for ss/r2ss.c (see ss/r2ss_test.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int r2ss_run(const char *ip, const char *acct_hex, const char *pin, const char *ops, int wake_first, char *log, size_t logn);
void r2ss_credential(const char *acct_hex, char out[65]);
extern const char *r2ss_pubkey_override; extern int r2ss_ddp_port, r2ss_tcp_port;
int main(int argc, char **argv) {
  static char pub[4096]; FILE *f = fopen(argv[1], "r"); size_t n = fread(pub, 1, sizeof pub - 1, f); pub[n] = 0; fclose(f);
  r2ss_pubkey_override = pub; r2ss_ddp_port = 9870; r2ss_tcp_port = 9970;
  char log[1024]; int r = r2ss_run("127.0.0.1", argv[2], argv[3], argv[4], atoi(argv[5]), log, sizeof log);
  char c[65]; r2ss_credential(argv[2], c);
  printf("RET %d\nLOG %s\nCRED %s\n", r, log, c); return 0;
}
