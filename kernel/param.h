#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       2000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define USERSTACK    1     // user stack pages

// --- Priority scheduling parameters ---
#define NPRIO         32     // # of priority levels, 0 (highest) .. 31 (lowest)
#define PRIO_MIN       0
#define PRIO_MAX      (NPRIO-1)
#define PRIO_DEFAULT  20     // default base priority for new procs
#define AGING_TICKS   20     // how many waiting ticks to raise priority by 1
