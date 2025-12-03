#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "rwlock.h"

static struct rwlock rw_table[MAXRW];

void
rw_table_init(void)
{
  for(int i = 0; i < MAXRW; i++){
    initlock(&rw_table[i].lock, "rwlock");
    rw_table[i].readers = 0;
    rw_table[i].writer = 0;
    rw_table[i].waiting_writers = 0;
  }
}

void
rw_init_k(int id)
{
  if(id < 0 || id >= MAXRW) return;
  acquire(&rw_table[id].lock);
  rw_table[id].readers = 0;
  rw_table[id].writer = 0;
  rw_table[id].waiting_writers = 0;
  release(&rw_table[id].lock);
}

// 读锁：写者优先 —— 若有写者在写或有写者等待，则读者需等待
void
rw_rlock_k(int id)
{
  if(id < 0 || id >= MAXRW) return;
  struct rwlock *rw = &rw_table[id];
  acquire(&rw->lock);
  while(rw->writer || rw->waiting_writers > 0){
    sleep(rw, &rw->lock);
  }
  rw->readers++;
  release(&rw->lock);
}

void
rw_runlock_k(int id)
{
  if(id < 0 || id >= MAXRW) return;
  struct rwlock *rw = &rw_table[id];
  acquire(&rw->lock);
  rw->readers--;
  if(rw->readers == 0){
    // 唤醒可能等待的写者/读者
    wakeup(rw);
  }
  release(&rw->lock);
}

void
rw_wlock_k(int id)
{
  if(id < 0 || id >= MAXRW) return;
  struct rwlock *rw = &rw_table[id];
  acquire(&rw->lock);
  rw->waiting_writers++;
  while(rw->writer || rw->readers > 0){
    sleep(rw, &rw->lock);
  }
  rw->waiting_writers--;
  rw->writer = 1;
  release(&rw->lock);
}

void
rw_wunlock_k(int id)
{
  if(id < 0 || id >= MAXRW) return;
  struct rwlock *rw = &rw_table[id];
  acquire(&rw->lock);
  rw->writer = 0;
  // 唤醒所有在此通道上等待的读/写
  wakeup(rw);
  release(&rw->lock);
}
