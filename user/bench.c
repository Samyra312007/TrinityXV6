
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static int mul = 1;


static void
printuint(uint64 v)
{
  char buf[24];
  int i = 23;

  buf[i] = 0;
  do {
    buf[--i] = '0' + (v % 10);
    v /= 10;
  } while (v > 0);
  printf("%s", buf + i);
}

static void
printint(int v)
{
  if (v < 0) {
    printf("-");
    printuint((uint64)(-(long)v));
  } else {
    printuint((uint64)v);
  }
}

static void
report(const char *name, uint64 n, int ticks)
{
  uint64 ns_per;

  if (ticks <= 0)
    ticks = 1;
  ns_per = ((uint64)ticks * 10000000UL) / n;
  printf("%s n=", name);
  printuint(n);
  printf(" ticks=");
  printint(ticks);
  printf(" per_op=");
  printuint(ns_per / 1000);
  printf(".%c%c%c us\n",
         (int)('0' + (ns_per / 100) % 10),
         (int)('0' + (ns_per / 10) % 10),
         (int)('0' + ns_per % 10));
}

static void
report_kbps(const char *name, uint64 bytes, int ticks)
{
  uint64 tenth_kbps;

  if (ticks <= 0)
    ticks = 1;
  tenth_kbps = bytes / ((uint64)ticks * 10);
  printf("%s bytes=", name);
  printuint(bytes);
  printf(" ticks=");
  printint(ticks);
  printf(" ");
  printuint(tenth_kbps / 10);
  printf(".");
  printuint(tenth_kbps % 10);
  printf(" KB/s\n");
}

static int
t0(void)
{
  return uptime();
}

static void
bench_syscall(void)
{
  uint64 n = 200000 * mul;
  int t;

  t = t0();
  for (uint64 i = 0; i < n; i++)
    getpid();
  report("syscall", n, t0() - t);
}

static void
bench_fork(void)
{
  uint64 n = 1000 * mul;
  int t;

  t = t0();
  for (uint64 i = 0; i < n; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("fork failed at ");
      printuint(i);
      printf("\n");
      break;
    }
    if (pid == 0)
      exit(0);
    wait(0);
  }
  report("fork", n, t0() - t);
}

static void
bench_openclose(void)
{
  uint64 n = 5000 * mul;
  int t;

  t = t0();
  for (uint64 i = 0; i < n; i++) {
    int fd = open(".", O_RDONLY);
    if (fd < 0) {
      printf("open failed\n");
      break;
    }
    close(fd);
  }
  report("openclose", n, t0() - t);
}

static void
bench_create(void)
{
  uint64 n = 500 * mul;
  char name[16], tmp[12];
  int t;

  t = t0();
  for (uint64 i = 0; i < n; i++) {
    int j = 0, k = 0;
    uint64 v = i;

    name[j++] = 'b';
    do {
      tmp[k++] = '0' + (v % 10);
      v /= 10;
    } while (v > 0);
    while (k > 0)
      name[j++] = tmp[--k];
    name[j] = 0;

    int fd = open(name, O_CREATE | O_RDWR);
    if (fd < 0) {
      printf("create failed at ");
      printuint(i);
      printf("\n");
      break;
    }
    close(fd);
    if (unlink(name) < 0)
      printf("unlink failed\n");
  }
  report("create", n, t0() - t);
}

static void
bench_write(void)
{
  uint64 n = 200 * mul, done = 0;
  char buf[512];
  int t, fd;

  memset(buf, 'x', sizeof(buf));
  fd = open("benchfile", O_CREATE | O_WRONLY);
  if (fd < 0) {
    printf("benchfile open failed\n");
    return;
  }
  t = t0();
  for (uint64 i = 0; i < n; i++) {
    if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
      printf("write failed at ");
      printuint(i);
      printf("\n");
      break;
    }
    done++;
  }
  report_kbps("write512", done * sizeof(buf), t0() - t);
  close(fd);
}

static void
bench_read(void)
{
  uint64 n = 200 * mul, done = 0;
  char buf[512];
  int t, fd;

  fd = open("benchfile", O_RDONLY);
  if (fd < 0) {
    printf("benchfile missing for read\n");
    return;
  }
  t = t0();
  for (uint64 i = 0; i < n; i++) {
    int r = read(fd, buf, sizeof(buf));
    if (r <= 0)
      break;
    done += r / sizeof(buf);
  }
  report_kbps("read512", done * sizeof(buf), t0() - t);
  close(fd);
}

int
main(int argc, char *argv[])
{
  if (argc > 1)
    mul = atoi(argv[1]);
  if (mul < 1)
    mul = 1;

  printf("=== bench (mult=");
  printint(mul);
  printf(") ===\n");
  bench_syscall();
  bench_fork();
  bench_openclose();
  bench_create();
  bench_write();
  bench_read();
  unlink("benchfile");
  printf("=== bench done ===\n");
  exit(0);
}
