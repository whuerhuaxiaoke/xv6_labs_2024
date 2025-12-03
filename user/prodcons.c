#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define NBUF 5          // 缓冲区大小
#define NPROD 2         // 生产者数量
#define NCONS 2         // 消费者数量

// 信号量编号
#define SEM_EMPTY 0
#define SEM_FULL  1
#define SEM_MUTEX 2

int buffer[NBUF];
int in = 0, out = 0;

// 模拟向缓冲区放数据
void put(int x) {
  buffer[in] = x;
  in = (in + 1) % NBUF;
}

// 模拟从缓冲区取数据
int get(void) {
  int x = buffer[out];
  out = (out + 1) % NBUF;
  return x;
}

void producer(int id) {
  for (int i = 0; i < 10; i++) {
    sem_wait(SEM_EMPTY);  // 等待空位
    sem_wait(SEM_MUTEX);  // 进入临界区

    put(i + id * 100);
    printf("Producer %d -> item %d\n", id, i + id * 100);

    sem_signal(SEM_MUTEX);
    sem_signal(SEM_FULL); // 通知消费者
    sleep(20);
  }
  exit(0);
}

void consumer(int id) {
  for (int i = 0; i < 10; i++) {
    sem_wait(SEM_FULL);   // 等待有数据
    sem_wait(SEM_MUTEX);  // 进入临界区

    int x = get();
    printf("Consumer %d <- item %d\n", id, x);

    sem_signal(SEM_MUTEX);
    sem_signal(SEM_EMPTY); // 通知生产者
    sleep(40);
  }
  exit(0);
}

int
main(void)
{
  printf("=== Producer-Consumer test ===\n");

  sem_init(SEM_EMPTY, NBUF); // 初始空位=缓冲区大小
  sem_init(SEM_FULL,  0);    // 初始满位=0
  sem_init(SEM_MUTEX, 1);    // 互斥锁=1

  for (int i = 0; i < NPROD; i++) {
    if (fork() == 0)
      producer(i);
  }
  for (int i = 0; i < NCONS; i++) {
    if (fork() == 0)
      consumer(i);
  }

  // 父进程等待所有子进程结束
  for (int i = 0; i < NPROD + NCONS; i++)
    wait(0);

  printf("=== Done ===\n");
  exit(0);
}
