
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  if (fork() > 0)
    pause(5);
  exit(0);
}
