#ifndef XV6_SCHEDSTAT_H
#define XV6_SCHEDSTAT_H
#include "types.h"

struct sched_stats {
  uint64 resched_calls;   // total calls into schedule()
  uint64 ctx_switches;    // times schedule() chose a different process
  uint64 idle_runs;       // times the idle task became runnable selection
};

#endif // XV6_SCHEDSTAT_H
