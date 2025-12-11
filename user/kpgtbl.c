// user/kpgtbl.c
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    kpgtbl();  // 调用系统调用
    exit(0);
}