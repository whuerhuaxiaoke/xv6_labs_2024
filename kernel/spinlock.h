// Mutual exclusion lock.
#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
};

#endif