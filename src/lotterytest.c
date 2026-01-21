#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "memlayout.h"

#define PGSIZE 4096
#define TARGET_BYTES (PHYSTOP * 2)
#define TOTAL_PAGES (TARGET_BYTES / PGSIZE)
#define HOT_PERCENT 10
#define ITERATIONS 1000
#define ACCESSES_PER_ITER TOTAL_PAGES

static char *pages[TOTAL_PAGES];

int
main(int argc, char *argv[])
{
  char *base;
  uint i;
  uint hot_pages;
  uint cold_pages;
  uint iter;
  uint j;
  uint idx;
  int sum = 0;
  int faults;

  printf(1, "lotterytest: %d pages (%d bytes)\n", TOTAL_PAGES, TARGET_BYTES);

  base = sbrk(TARGET_BYTES);
  if (base == (char *)-1)
  {
    printf(1, "sbrk failed\n");
    exit();
  }

  for (i = 0; i < TOTAL_PAGES; i++)
  {
    pages[i] = base + (i * PGSIZE);
    pages[i][0] = (char)i;
  }

  hot_pages = TOTAL_PAGES / HOT_PERCENT;
  if (hot_pages < 1)
    hot_pages = 1;
  cold_pages = TOTAL_PAGES - hot_pages;

  faults = get_faults();
  printf(1, "starting faults=%d\n", faults);

  for (iter = 1; iter <= ITERATIONS; iter++)
  {
    for (j = 0; j < ACCESSES_PER_ITER; j++)
    {
      if (cold_pages == 0)
        idx = (j + iter) % hot_pages;
      else if ((j % 10) != 0)
        idx = (j + iter) % hot_pages;
      else
        idx = hot_pages + ((j + iter) % cold_pages);

      pages[idx][0] = pages[idx][0] + 1;
      sum += pages[idx][0];
    }

    if ((iter % 10) == 0)
    {
      faults = get_faults();
      printf(1, "iter %d: faults=%d\n", iter, faults);
    }
  }

  printf(1, "done: sum=%d faults=%d\n", sum, get_faults());
  exit();
}
