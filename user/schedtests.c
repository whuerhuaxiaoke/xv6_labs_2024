#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "kernel/schedstat.h"
#include "user/user.h"

struct start_rec {
  char tag;
  int tick;
};

static void
print_result(const char *name, int ok)
{
  printf("[%s] %s\n", name, ok ? "PASS" : "FAIL");
}

static void
worker(char tag, int start_fd, int log_fd, int prio, int spins)
{
  char dummy;
  read(start_fd, &dummy, 1);
  setpriority(getpid(), prio);

  struct start_rec rec;
  rec.tag = tag;
  rec.tick = uptime();
  if (log_fd >= 0)
    write(log_fd, &rec, sizeof(rec));

  for (int i = 0; i < spins; i++) {
    for (volatile int j = 0; j < 200000; j++) {
    }
    if ((i & 3) == 0)
      yield();
  }
  exit(0);
}

static void
idle_gap_test(void)
{
  struct sched_stats before, after;
  if (schedstat(&before) < 0) {
    print_result("idle-gap", 0);
    return;
  }
  sleep(30);
  if (schedstat(&after) < 0) {
    print_result("idle-gap", 0);
    return;
  }
  print_result("idle-gap", after.idle_runs > before.idle_runs);
}

static void
priority_preempt_test(void)
{
  int startL[2], startH[2], logp[2];
  pipe(startL);
  pipe(startH);
  pipe(logp);

  int low = fork();
  if (low == 0) {
    close(startL[1]);
    close(startH[0]); close(startH[1]);
    close(logp[0]);
    worker('L', startL[0], logp[1], PRIO_DEFAULT + 6, 40);
  }

  int high = fork();
  if (high == 0) {
    close(startH[1]);
    close(startL[0]); close(startL[1]);
    close(logp[0]);
    worker('H', startH[0], logp[1], PRIO_MIN, 40);
  }

  close(startL[0]);
  close(startH[0]);
  close(logp[1]);

  setpriority(low, PRIO_DEFAULT + 6);
  setpriority(high, PRIO_MIN);

  char go = 'g';
  write(startL[1], &go, 1);
  write(startH[1], &go, 1);
  close(startL[1]);
  close(startH[1]);

  struct start_rec recs[2];
  int nread = 0;
  while (nread < 2) {
    int m = read(logp[0], (char *)&recs[nread], sizeof(struct start_rec));
    if (m == sizeof(struct start_rec))
      nread++;
  }
  close(logp[0]);

  int first_is_high = (recs[0].tag == 'H');
  wait(0);
  wait(0);
  print_result("priority-preempt", first_is_high);
}

static void
switch_counter_test(void)
{
  int starter[2];
  pipe(starter);
  struct sched_stats before, after;

  int p1 = fork();
  if (p1 == 0) {
    close(starter[1]);
    worker('A', starter[0], -1, PRIO_DEFAULT, 30);
  }

  int p2 = fork();
  if (p2 == 0) {
    close(starter[1]);
    worker('B', starter[0], -1, PRIO_DEFAULT, 30);
  }

  close(starter[0]);
  schedstat(&before);
  char go = 'g';
  write(starter[1], &go, 1);
  write(starter[1], &go, 1);
  close(starter[1]);

  wait(0);
  wait(0);
  schedstat(&after);

  uint64 delta = after.ctx_switches - before.ctx_switches;
  print_result("switch-counter", delta >= 20);
}

int
main(void)
{
  printf("=== scheduler validation tests ===\n");
  idle_gap_test();
  priority_preempt_test();
  switch_counter_test();
  printf("=== scheduler validation tests done ===\n");
  exit(0);
}
