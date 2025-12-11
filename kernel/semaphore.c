#include "types.h"
#include "param.h"     
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "semaphore.h"
static struct semaphore sem_table[MAXSEM];

void
sem_table_init(void)
{
  for(int i = 0; i < MAXSEM; i++) {
    initlock(&sem_table[i].lock, "semaphore");
    sem_table[i].value = 0;
  }
}

void
sem_init_k(int id, int value)
{
  if(id < 0 || id >= MAXSEM)
    return;
  acquire(&sem_table[id].lock);
  sem_table[id].value = value;
  release(&sem_table[id].lock);
}

void
sem_wait_k(int id)
{
  if(id < 0 || id >= MAXSEM)
    return;
  struct semaphore *sem = &sem_table[id];
  acquire(&sem->lock);
  while(sem->value == 0){
    sleep(sem, &sem->lock);
  }
  sem->value--;
  release(&sem->lock);
}

void
sem_signal_k(int id)
{
  if(id < 0 || id >= MAXSEM)
    return;
  struct semaphore *sem = &sem_table[id];
  acquire(&sem->lock);
  sem->value++;
  wakeup(sem);
  release(&sem->lock);
}
