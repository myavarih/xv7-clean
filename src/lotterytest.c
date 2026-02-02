#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"

#define PGSIZE 4096
#define TOTAL_PAGES 700
#define HOT_PERCENT 10
#define ITERATIONS 1000
#define ACCESSES_PER_ITER (TOTAL_PAGES * 2)

static uint randstate;

static uint
prand(void)
{
  randstate = uptime();
  if (randstate == 0)
    randstate = 1;
  randstate = randstate * 1664525 + 1013904223;
  return randstate;
}

int main(int argc, char *argv[])
{
  char *pages[TOTAL_PAGES];
  uint i;
  uint hot_pages;
  uint cold_pages;
  uint iter;
  uint j;
  uint idx;
  uint r;
  int sum = 0;
  int faults;
  int start_faults;
  int last_faults;

  printf(1, "lotterytest: %d pages\n", TOTAL_PAGES);

  for (i = 0; i < TOTAL_PAGES; i++)
  {
    pages[i] = malloc(PGSIZE);
    if (pages[i] == 0)
      goto failed;
    pages[i][0] = (char)i;
    pages[i][PGSIZE - 1] = (char)(i ^ 0x5a);
  }

  hot_pages = TOTAL_PAGES / HOT_PERCENT;
  if (hot_pages < 1)
    hot_pages = 1;
  cold_pages = TOTAL_PAGES - hot_pages;

  start_faults = get_faults();
  last_faults = start_faults;
  printf(1, "starting faults=%d\n", start_faults);

  for (iter = 1; iter <= ITERATIONS; iter++)
  {
    for (j = 0; j < ACCESSES_PER_ITER; j++)
    {
      r = prand();
      if (cold_pages == 0)
        idx = r % hot_pages;
      else if ((r % 10) != 0)
        idx = r % hot_pages;
      else
        idx = hot_pages + (r % cold_pages);

      pages[idx][0] ^= 1;
      sum += pages[idx][0];
    }

    if ((iter % 10) == 0)
    {
      faults = get_faults();
      printf(1, "iter %d: faults=%d (+%d, since start +%d)\n",
             iter, faults, faults - last_faults, faults - start_faults);
      last_faults = faults;
    }
  }

  faults = get_faults();
  printf(1, "done: sum=%d faults=%d (since start +%d)\n",
         sum, faults, faults - start_faults);
  exit();

failed:
  printf(1, "test failed!\n");
  exit();
}
