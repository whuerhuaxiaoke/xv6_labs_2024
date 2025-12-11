#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define RWID 0      // 选一个 rwlock id
#define NREAD 3
#define NWRITE 2

void reader(int id){
  for(int i=0;i<3;i++){
    rw_rlock(RWID);
    printf("Reader %d reading...\n", id);
    // 模拟读操作
    sleep(20);
    rw_runlock(RWID);
    sleep(30);
  }
  exit(0);
}

void writer(int id){
  for(int i=0;i<3;i++){
    rw_wlock(RWID);
    printf("Writer %d writing...\n", id);
    // 模拟写操作
    sleep(40);
    rw_wunlock(RWID);
    sleep(40);
  }
  exit(0);
}

int
main(void)
{
  printf("=== RWLock (writer-pref) test ===\n");
  rw_init(RWID);

  for(int i=0;i<NREAD;i++){
    if(fork()==0) reader(i);
  }
  for(int i=0;i<NWRITE;i++){
    if(fork()==0) writer(i);
  }

  for(int i=0;i<NREAD+NWRITE;i++)
    wait(0);

  printf("=== Done ===\n");
  exit(0);
}
