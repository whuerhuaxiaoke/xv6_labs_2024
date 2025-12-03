#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

//
// Robust priority preemption test (user-space only)
//  - Detects true preemption vs round-robin sharing
//  - Tolerates console buffering / pipe latency
//

struct logrec {
  char tag;
  int tick;
};

// busy loop with time sampling
void busy_work(const char tag, int fd, int rounds) {
  struct logrec rec;
  rec.tag = tag;
  for (int i = 0; i < rounds; i++) {
    if (i % 2000000 == 0) {
      rec.tick = uptime();
      write(fd, &rec, sizeof(rec));
    }
  }
}

int main(void)
{
  printf("=== prio_strict_test: start ===\n");

  int pipeL[2], pipeH[2];
  pipe(pipeL);
  pipe(pipeH);

  int low = fork();
  if (low == 0) {
    close(pipeL[0]);
    busy_work('L', pipeL[1], 30000000);
    close(pipeL[1]);
    exit(0);
  }

  // 给低优先级一些先运行时间
  sleep(20);

  int high = fork();
  if (high == 0) {
    close(pipeH[0]);
    busy_work('H', pipeH[1], 30000000);
    close(pipeH[1]);
    exit(0);
  }

  close(pipeL[1]);
  close(pipeH[1]);

  struct logrec rl, rh;
  int seenH = 0;
  int fail = 0;
  int last_H_tick = 0;
  //int last_L_tick = 0;

  // 超时时间防止死循环
  int start_tick = uptime();
  while (uptime() - start_tick < 500) {
    int nL = read(pipeL[0], &rl, sizeof(rl));
    int nH = read(pipeH[0], &rh, sizeof(rh));

    if (nL <= 0 && nH <= 0)
      break;

    // 更新记录
    //if (nL > 0 && rl.tag == 'L') last_L_tick = rl.tick;
    if (nH > 0 && rh.tag == 'H') {
      seenH = 1;
      last_H_tick = rh.tick;
    }

    // 若已进入H阶段，且之后L仍有输出 => FAIL
    if (seenH && nL > 0 && rl.tick > last_H_tick + 1) {
      fail = 1;
      break;
    }
  }

  wait(0);
  wait(0);

  // 判断结果
  if (!seenH) {
    printf("===> test FAIL: high-priority never ran\n");
  } else if (fail) {
    printf("===> test FAIL: low-priority ran after H started (round-robin only)\n");
  } else {
    printf("===> test OK: strict priority preemption observed\n");
  }

  printf("=== prio_strict_test: end ===\n");
  exit(0);
}
