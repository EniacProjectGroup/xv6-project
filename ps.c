#include "types.h"
#include "stat.h"
#include "user.h"
#include "pstat.h"

int main(void) {
  struct pstat p;
  if (getpinfo(&p) < 0) {
    printf(1, "Error: getpinfo failed\n");
    exit();
  }

  printf(1, "PID\tTickets\tTicks\n");
  for (int i = 0; i < NPROC; i++) {
    if (p.inuse[i]) {
      printf(1, "%d\t%d\t%d\n", p.pid[i], p.tickets[i], p.ticks[i]);
    }
  }

  exit();
}
