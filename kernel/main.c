#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

void
main()
{
  if (cpuid() == 0) {
    consoleinit();
    printkinit();
    printk("\n");
    printk("xv6 kernel is booting\n");
    printk("\n");
    kinit();
    kvminit();
    kvminithart();
    procinit();
    trapinit();
    trapinithart();
    plicinit();
    plicinithart();
    binit();
    iinit();
    fileinit();
    virtio_disk_init();
    userinit();

    __atomic_store_n(&started, 1, __ATOMIC_RELEASE);
  } else {
    while (__atomic_load_n(&started, __ATOMIC_ACQUIRE) == 0)
      ;

    printk("hart %d starting\n", cpuid());
    kvminithart();
    trapinithart();
    plicinithart();
  }

  scheduler();
}
