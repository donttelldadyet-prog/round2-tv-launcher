/* Round2 "own program" test payload (16 Nov 2026): proves a payload can run as its OWN PS4 program. Harmless: writes
   /data/round2/own-test.txt – who it is, then a line every 5 s for 3 minutes – and ends by itself. GPL-3.0. */
#include <stdio.h>
#include <time.h>
#include <unistd.h>
int main(void) {
  FILE *f = fopen("/data/round2/own-test.txt", "w");
  if (!f) return 1;
  fprintf(f, "own program: pid %d, parent %d, uid %d, started %lld\n", (int)getpid(), (int)getppid(), (int)getuid(), (long long)time(0));
  fflush(f);
  for (int i = 1; i <= 36; i++) {
    sleep(5);
    fprintf(f, "alive %d s (pid %d)\n", i * 5, (int)getpid());
    fflush(f);
  }
  fprintf(f, "done - ending by itself\n");
  fclose(f);
  return 0;
}
