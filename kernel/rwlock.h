#ifndef _RWLOCK_H_
#define _RWLOCK_H_

#include "types.h"
#include "param.h"
#include "spinlock.h"

#define MAXRW 32

struct rwlock {
  struct spinlock lock;
  int readers;          // 正在读的数量
  int writer;           // 是否有写者持有锁 (0/1)
  int waiting_writers;  // 等待中的写者数量（写者优先策略）
};

void rw_table_init(void);
void rw_init_k(int id);
void rw_rlock_k(int id);
void rw_runlock_k(int id);
void rw_wlock_k(int id);
void rw_wunlock_k(int id);

#endif
