// user/bttest.c
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    sleep(1);  // 触发 sys_sleep，从而调用 backtrace()
    exit(0);
}