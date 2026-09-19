struct context {
  uint64 ra;
  uint64 sp;

  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

struct cpu {
  struct proc *proc;
  struct context context;
  int noff;
  int intena;
};

extern struct cpu cpus[NCPU];

struct trapframe {
   uint64 kernel_satp;
   uint64 kernel_sp;
   uint64 kernel_trap;
   uint64 epc;
   uint64 kernel_hartid;
   uint64 ra;
   uint64 sp;
   uint64 gp;
   uint64 tp;
   uint64 t0;
   uint64 t1;
   uint64 t2;
   uint64 s0;
   uint64 s1;
   uint64 a0;
   uint64 a1;
   uint64 a2;
   uint64 a3;
   uint64 a4;
   uint64 a5;
   uint64 a6;
   uint64 a7;
   uint64 s2;
   uint64 s3;
   uint64 s4;
   uint64 s5;
   uint64 s6;
   uint64 s7;
   uint64 s8;
   uint64 s9;
   uint64 s10;
   uint64 s11;
   uint64 t3;
   uint64 t4;
   uint64 t5;
   uint64 t6;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
  struct spinlock lock;

  enum procstate state;
  void *chan;
  int killed;
  int xstate;
  int pid;

  struct proc *parent;

  uint64 kstack;
  uint64 sz;
  pagetable_t pagetable;
  struct trapframe *trapframe;
  struct context context;
  struct file *ofile[NOFILE];
  struct inode *cwd;
  char name[16];
};
