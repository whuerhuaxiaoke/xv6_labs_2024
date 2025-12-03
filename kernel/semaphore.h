#ifndef _SEMAPHORE_H_
#define _SEMAPHORE_H_

#include "param.h"  // 可选
#include "types.h"
#include "spinlock.h"  // 需要 spinlock 定义一次即可

#define MAXSEM 32

struct semaphore {
  struct spinlock lock;
  int value;
};

// 仅声明，不定义
void sem_table_init(void);
void sem_init_k(int id, int value);
void sem_wait_k(int id);
void sem_signal_k(int id);

#endif
