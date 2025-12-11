#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

volatile int dummy = 0;

// 简单忙循环，制造 CPU 压力
static void
busy_work(int loops)
{
  for (int i = 0; i < loops; i++) {
    dummy++;
  }
}

/* ---------------- Test 1: 基本时间片/抢占测试 ---------------- */

static void
test1_child(int id)
{
  for (int i = 0; i < 5; i++) {
    busy_work(8000000);  // 调大一点看到明显切换
    printf("[Test1] child %d iteration %d\n", id, i);
  }
  printf("[Test1] child %d done\n", id);
  exit(0);
}

static void
run_test1(void)
{
  printf("=== Test1: basic CPU round-robin / preemption ===\n");

  int pid1 = fork();
  if (pid1 == 0) {
    test1_child(1);
  }

  int pid2 = fork();
  if (pid2 == 0) {
    test1_child(2);
  }

  // 父进程等待两个子进程结束
  int status;
  wait(&status);
  wait(&status);

  printf("=== Test1 finished ===\n\n");
}

/* ---------------- Test 2: sleep / wakeup 行为测试 ---------------- */

static void
run_test2(void)
{
  printf("=== Test2: sleep / wakeup behavior ===\n");

  int pid = fork();
  if (pid == 0) {
    // child
    for (int i = 0; i < 5; i++) {
      printf("[Test2] child: iteration %d (before sleep)\n", i);
      sleep(50);
    }
    printf("[Test2] child: done\n");
    exit(0);
  }

  // parent
  for (int i = 0; i < 10; i++) {
    printf("[Test2] parent: %d\n", i);
    sleep(20);
  }

  int status;
  wait(&status);

  printf("=== Test2 finished ===\n\n");
}

/* ---------------- Test 3: 混合压力测试 (CPU + IO) ---------------- */

static void
cpu_task(int id)
{
  for (int i = 0; i < 8; i++) {
    busy_work(6000000);
    if (i % 2 == 0) {
      printf("[Test3] CPU %d heartbeat %d\n", id, i / 2);
    }
  }
  printf("[Test3] CPU %d done\n", id);
  exit(0);
}

static void
io_task(int id)
{
  for (int i = 0; i < 6; i++) {
    printf("[Test3] IO %d sleeping (%d)\n", id, i);
    sleep(40);
  }
  printf("[Test3] IO %d done\n", id);
  exit(0);
}

static void
run_test3(void)
{
  printf("=== Test3: mixed CPU/IO stress test ===\n");

  // 启动几个 CPU bound 任务
  for (int i = 0; i < 3; i++) {
    int pid = fork();
    if (pid == 0) {
      cpu_task(i);
    }
  }

  // 再启动几个 IO bound 任务
  for (int i = 0; i < 3; i++) {
    int pid = fork();
    if (pid == 0) {
      io_task(100 + i);
    }
  }

  // 父进程等待所有子进程结束
  int status;
  for (int i = 0; i < 6; i++) {   // 3 CPU + 3 IO = 6 子进程
    wait(&status);
  }

  printf("=== Test3 finished ===\n\n");
}
/* ---------------- main: 依次执行所有测试 ---------------- */

int
main(void)
{
  printf("========== Scheduler tests start ==========\n\n");

  run_test1();
  run_test2();
  run_test3();

  printf("========== Scheduler tests all done ==========\n");
  exit(0);
}
