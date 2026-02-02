#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "paging.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[]; // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void tvinit(void)
{
  int i;

  for (i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE << 3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE << 3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void idtinit(void)
{
  lidt(idt, sizeof(idt));
}

// ! LOTTERYVM:
#define TICKET_DEBUG 1        // * indicates whether to print debug page infos
#define TICKET_UPDATE_TICKS 1 // * indicates on which ticks to run the update_tickets() - now on all

static void
update_tickets(struct proc *p) // * update_tickets() function to go through the page table and update tickets
{
  pte_t *pte;
  pde_t *pgdir;
  uint pa;
  uint idx;
  int tickets;
  int accessed;
  int printed = 0;
  int cleared = 0;

  if (p == 0 || p->pgdir == 0 || p->sz == 0)
    return;

  pgdir = p->pgdir; // * access process's page table
  for (uint va = 0; va < p->sz; va += PGSIZE)
  {
    pte = uva2pte(pgdir, va); // * get page table entry from virtual address (get the nth page table entry)
    if (pte == 0)
      continue;
    if (!(*pte & PTE_P) || (*pte & PTE_SWAPPED)) // * skip if not present or swapped
      continue;

    pa = PTE_ADDR(*pte);
    if (pa >= PHYSTOP)
      continue;
    idx = pa / PGSIZE; // * get the frame address and calculate metadata index from it

    tickets = metadata[idx].tickets;
    accessed = ((*pte & PTE_A) != 0);
    if (accessed)
    {
      tickets += 10;
      if (tickets > 500)
        tickets = 500;
    }
    else
    {
      tickets -= 5;
      if (tickets < 10)
        tickets = 10;
    }
    metadata[idx].tickets = tickets; // * calculate and update tickets (if it's accessed plus 10 with the ceil of 500 and if not minus 5 with the floor of 10)
    if (TICKET_DEBUG && printed < 10)
    {
      cprintf("page 0x%x : tickets=%d, accessed=%d\n", va, tickets, accessed);
      printed++;
    } // * print debug info if it's among the first 10 pages that we are looping
    *pte &= ~PTE_A;
    cleared = 1;
  }

  if (cleared)
    lcr3(V2P(pgdir)); // * TLB flush if at least one PTE_A is cleared
}
// ! end LOTTERYVM

// PAGEBREAK: 41
void trap(struct trapframe *tf)
{
  if (tf->trapno == T_SYSCALL)
  {
    if (myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if (myproc()->killed)
      exit();
    return;
  }

  switch (tf->trapno)
  {
  case T_PGFLT:
    handle_pgfault();
    break;
  case T_IRQ0 + IRQ_TIMER:
    if (cpuid() == 0)
    {
      static uint last_update_ticks;
      uint t;

      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      t = ticks;
      release(&tickslock);

      // ! LOTTERYVM:
      if (myproc() && myproc()->pgdir && myproc()->sz > 0)
      {
        if (t - last_update_ticks >= TICKET_UPDATE_TICKS)
        {
          last_update_ticks = t;
          update_tickets(myproc()); // * run update_tickets every TICKET_UPDATE_TICKS ticks
        }
      }
      // ! end LOTTERYVM
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE + 1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  // PAGEBREAK: 13
  default:
    if (myproc() == 0 || (tf->cs & 3) == 0)
    {
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if (myproc() && myproc()->state == RUNNING &&
      tf->trapno == T_IRQ0 + IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();
}
