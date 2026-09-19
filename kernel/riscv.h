#ifndef __ASSEMBLER__

static inline uint64
r_mhartid()
{
  uint64 x;
  asm volatile("csrr %0, mhartid" : "=r"(x));
  return x;
}


#define MSTATUS_MPP_MASK (3L << 11)
#define MSTATUS_MPP_M    (3L << 11)
#define MSTATUS_MPP_S    (1L << 11)
#define MSTATUS_MPP_U    (0L << 11)

static inline uint64
r_mstatus()
{
  uint64 x;
  asm volatile("csrr %0, mstatus" : "=r"(x));
  return x;
}

static inline void
w_mstatus(uint64 x)
{
  asm volatile("csrw mstatus, %0" : : "r"(x));
}

static inline void
w_mepc(uint64 x)
{
  asm volatile("csrw mepc, %0" : : "r"(x));
}


#define SSTATUS_SPP  (1L << 8)
#define SSTATUS_SPIE (1L << 5)
#define SSTATUS_UPIE (1L << 4)
#define SSTATUS_SIE  (1L << 1)
#define SSTATUS_UIE  (1L << 0)

static inline uint64
r_sstatus()
{
  uint64 x;
  asm volatile("csrr %0, sstatus" : "=r"(x));
  return x;
}

static inline void
w_sstatus(uint64 x)
{
  asm volatile("csrw sstatus, %0" : : "r"(x));
}

static inline void
s_sstatus(uint64 x)
{
  __asm__ __volatile__("csrs sstatus, %0" ::"rK"(x) : "memory");
}

static inline void
c_sstatus(uint64 x)
{
  __asm__ __volatile__("csrc sstatus, %0" ::"rK"(x) : "memory");
}

static inline uint64
rc_sstatus(uint64 x)
{
  __asm__ __volatile__("csrrc %0, sstatus, %1" : "=r"(x) : "rK"(x) : "memory");
  return x;
}

static inline uint64
r_sip()
{
  uint64 x;
  asm volatile("csrr %0, sip" : "=r"(x));
  return x;
}

static inline void
w_sip(uint64 x)
{
  asm volatile("csrw sip, %0" : : "r"(x));
}

#define SIE_SEIE (1L << 9)
#define SIE_STIE (1L << 5)
static inline uint64
r_sie()
{
  uint64 x;
  asm volatile("csrr %0, sie" : "=r"(x));
  return x;
}

static inline void
w_sie(uint64 x)
{
  asm volatile("csrw sie, %0" : : "r"(x));
}

#define MIE_STIE (1L << 5)
static inline uint64
r_mie()
{
  uint64 x;
  asm volatile("csrr %0, mie" : "=r"(x));
  return x;
}

static inline void
w_mie(uint64 x)
{
  asm volatile("csrw mie, %0" : : "r"(x));
}

static inline void
w_sepc(uint64 x)
{
  asm volatile("csrw sepc, %0" : : "r"(x));
}

static inline uint64
r_sepc()
{
  uint64 x;
  asm volatile("csrr %0, sepc" : "=r"(x));
  return x;
}

static inline uint64
r_medeleg()
{
  uint64 x;
  asm volatile("csrr %0, medeleg" : "=r"(x));
  return x;
}

static inline void
w_medeleg(uint64 x)
{
  asm volatile("csrw medeleg, %0" : : "r"(x));
}

static inline uint64
r_mideleg()
{
  uint64 x;
  asm volatile("csrr %0, mideleg" : "=r"(x));
  return x;
}

static inline void
w_mideleg(uint64 x)
{
  asm volatile("csrw mideleg, %0" : : "r"(x));
}

static inline void
w_stvec(uint64 x)
{
  asm volatile("csrw stvec, %0" : : "r"(x));
}

static inline uint64
r_stvec()
{
  uint64 x;
  asm volatile("csrr %0, stvec" : "=r"(x));
  return x;
}

static inline uint64
r_stimecmp()
{
  uint64 x;
  asm volatile("csrr %0, 0x14d" : "=r"(x));
  return x;
}

static inline void
w_stimecmp(uint64 x)
{
  asm volatile("csrw 0x14d, %0" : : "r"(x));
}


#define MENVCFG_STCE (1L << 63)
#define MENVCFG_ADUE (1L << 61)

static inline uint64
r_menvcfg()
{
  uint64 x;
  asm volatile("csrr %0, 0x30a" : "=r"(x));
  return x;
}

static inline void
w_menvcfg(uint64 x)
{
  asm volatile("csrw 0x30a, %0" : : "r"(x));
}

static inline void
w_pmpcfg0(uint64 x)
{
  asm volatile("csrw pmpcfg0, %0" : : "r"(x));
}

static inline void
w_pmpaddr0(uint64 x)
{
  asm volatile("csrw pmpaddr0, %0" : : "r"(x));
}

#define SATP_SV39 (8L << 60)

#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

static inline void
w_satp(uint64 x)
{
  asm volatile("csrw satp, %0" : : "r"(x));
}

static inline uint64
r_satp()
{
  uint64 x;
  asm volatile("csrr %0, satp" : "=r"(x));
  return x;
}

static inline uint64
r_scause()
{
  uint64 x;
  asm volatile("csrr %0, scause" : "=r"(x));
  return x;
}

static inline uint64
r_stval()
{
  uint64 x;
  asm volatile("csrr %0, stval" : "=r"(x));
  return x;
}

static inline void
w_mcounteren(uint64 x)
{
  asm volatile("csrw mcounteren, %0" : : "r"(x));
}

static inline uint64
r_mcounteren()
{
  uint64 x;
  asm volatile("csrr %0, mcounteren" : "=r"(x));
  return x;
}

static inline uint64
r_time()
{
  uint64 x;
  asm volatile("csrr %0, time" : "=r"(x));
  return x;
}

static inline void
intr_on()
{
  s_sstatus(SSTATUS_SIE);
}

static inline void
intr_off()
{
  c_sstatus(SSTATUS_SIE);
}

static inline int
intr_get()
{
  uint64 x = r_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

static inline uint64
r_sp()
{
  uint64 x;
  asm volatile("mv %0, sp" : "=r"(x));
  return x;
}

static inline uint64
r_tp()
{
  uint64 x;
  asm volatile("mv %0, tp" : "=r"(x));
  return x;
}

static inline void
w_tp(uint64 x)
{
  asm volatile("mv tp, %0" : : "r"(x));
}

static inline uint64
r_ra()
{
  uint64 x;
  asm volatile("mv %0, ra" : "=r"(x));
  return x;
}

static inline void
sfence_vma()
{
  asm volatile("sfence.vma zero, zero" ::: "memory");
}

static inline void
io_fence()
{
  asm volatile("fence iorw, iorw" ::: "memory");
}

static inline void
icache_fence()
{
  asm volatile("fence.i" ::: "memory");
}

typedef uint64 pte_t;
typedef uint64 *pagetable_t;

#endif

#define PGSIZE  4096
#define PGSHIFT 12

#define PGROUNDUP(sz)  (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE - 1))

#define PTE_V (1L << 0)
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4)

#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)

#define PTE2PA(pte) (((pte) >> 10) << 12)

#define PTE_FLAGS(pte) ((pte) & 0x3FF)

#define PXMASK         0x1FF
#define PXSHIFT(level) (PGSHIFT + (9 * (level)))
#define PX(level, va)  ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)

#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
