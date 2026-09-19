#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

__attribute__((aligned(16))) char stack0[4096 * NCPU];

void
start()
{
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  w_mepc((uint64)main);

  w_satp(0);

  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);

  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  w_menvcfg(r_menvcfg() | MENVCFG_ADUE);

  timerinit();

  int id = r_mhartid();
  w_tp(id);

  asm volatile("mret");
}

void
timerinit()
{
  w_menvcfg(r_menvcfg() | MENVCFG_STCE);

  w_mcounteren(r_mcounteren() | 2);

  w_stimecmp(r_time() + 1000000);
}
