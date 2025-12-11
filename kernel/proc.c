#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

static struct proc* alloc_idleproc(void);

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
// --- Global priority runqueue (multi-level), protected by a single spinlock ---

static struct {
  struct spinlock lock;
  struct prio_queue q[NPRIO];  // 每个优先级一个队列：0..31
  int highest_nonempty;        // 当前非空队列的最高优先级（最小数字）
} runq;

void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
  prio_init();
  extern void sem_table_init(void);   // 声明函数（避免编译器报隐式声明）
  sem_table_init();                   // 初始化全局信号量表
  extern void rw_table_init(void);
  rw_table_init();
}
void
prio_init(void)
{
  initlock(&runq.lock, "runq");
  for (int i = 0; i < NPRIO; i++) {
    runq.q[i].head = 0;
    runq.q[i].tail = 0;
  }
  runq.highest_nonempty = -1;
}
static void
rq_push_tail(int prio, struct proc *p)
{
  struct prio_queue *Q = &runq.q[prio];
  p->rq_next = 0;
  if (Q->tail) {
    Q->tail->rq_next = p;
    Q->tail = p;
  } else {
    Q->head = Q->tail = p;
  }
  if (runq.highest_nonempty < 0 || prio < runq.highest_nonempty)
    runq.highest_nonempty = prio;
}

static void
rq_remove(int prio, struct proc *p)
{
  struct prio_queue *Q = &runq.q[prio];
  struct proc *prev = 0, *cur = Q->head;
  while (cur) {
    if (cur == p) {
      if (prev) prev->rq_next = cur->rq_next;
      else Q->head = cur->rq_next;
      if (Q->tail == cur) Q->tail = prev;
      cur->rq_next = 0;
      break;
    }
    prev = cur; cur = cur->rq_next;
  }
  // 更新 highest_nonempty
  if (!Q->head) {
    if (runq.highest_nonempty == prio) {
      int h = -1;
      for (int i = 0; i < NPRIO; i++) {
        if (runq.q[i].head) { h = i; break; }
      }
      runq.highest_nonempty = h;
    }
  }
}

void 
prio_enqueue(struct proc *p)
{
  acquire(&runq.lock);
  p->wait_ticks = 0;
  rq_push_tail(p->prio, p);
  release(&runq.lock);

  struct proc *cur = myproc();
  if (cur && cur->state == RUNNING && p->prio < cur->prio) {
    mycpu()->preempt_pending = 1;   // ← 正确设置标记
  }
}



void
prio_dequeue(struct proc *p)
{
  acquire(&runq.lock);
  rq_remove(p->prio, p);
  release(&runq.lock);
}

struct proc*
prio_pick_next(void)
{
  acquire(&runq.lock);
  int h = runq.highest_nonempty;
  if (h < 0) {
    release(&runq.lock);
    return 0;
  }
  struct proc *p = runq.q[h].head;
  if (p) {
    // 从队列头取出一个，并从队列中移除（出队）
    rq_remove(h, p);
  }
  release(&runq.lock);
  return p;
}

int
prio_highest_nonempty(void)
{
  int h;
  acquire(&runq.lock);
  h = runq.highest_nonempty;
  release(&runq.lock);
  return h;
}
void
prio_on_tick(void)
{
  // 在时钟中断中被调用：对 RUNNABLE 的进程进行 aging。
  // 这里只处理在 runq 中的进程（RUNNABLE 状态且已入队）。
  acquire(&runq.lock);
  for (int pr = 0; pr < NPRIO; pr++) {
    struct proc *cur = runq.q[pr].head;
    while (cur) {
      cur->wait_ticks++;
      if (cur->wait_ticks >= AGING_TICKS && cur->prio > PRIO_MIN) {
        // 该进程“升优先级”：从当前队列摘下，插入到更高优先级队列
        struct proc *next = cur->rq_next;
        rq_remove(pr, cur);
        cur->prio -= 1;
        cur->wait_ticks = 0;
        rq_push_tail(cur->prio, cur);
        cur = next;
        // 注意：rq_remove + rq_push_tail 会改变链表结构，所以上面先保存 next
      } else {
        cur = cur->rq_next;
      }
    }
  }
  release(&runq.lock);
}

// 返回：是否存在“高于 cur_prio 的就绪进程”
int
prio_should_preempt(int cur_prio)
{
  int h = prio_highest_nonempty();
  if (h < 0) return 0;
  return (h <= cur_prio); // ← 改为 <=
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->base_prio  = PRIO_DEFAULT;
  p->prio       = PRIO_DEFAULT;
  p->wait_ticks = 0;
  p->rq_next    = 0;
  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  prio_enqueue(initproc);

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
    // 继承父进程优先级（也可保留默认 PRIO_DEFAULT）
  np->prio = p->prio;
  np->wait_ticks = 0;
  prio_enqueue(np);

  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;
  release(&wait_lock);

  release(&p->lock);

  schedule();           // 再也不会回来了
  panic("zombie exit"); // 理论上到不了这里
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;
  c->preempt_pending = 0;

  struct proc *idle = alloc_idleproc();
  c->idleproc = idle;

  // 不要获取 idle->lock，直接让 CPU 跳到 idle
  c->proc = idle;
  swtch(&c->context, &idle->context);

  panic("scheduler returned");
}


// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  p->wait_ticks = 0;
  prio_enqueue(p);
  release(&p->lock);

  schedule();   // 进入调度器时不持有任何 p->lock
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  //release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  release(lk);

  p->chan = chan;
  if(p->state == RUNNABLE)
    prio_dequeue(p);
  p->state = SLEEPING;

  release(&p->lock);

  schedule();   // 进入调度器时同样不持有任何 p->lock

  // 被唤醒后
  acquire(&p->lock);
  p->chan = 0;
  release(&p->lock);

  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        p->wait_ticks = 0;
        prio_enqueue(p);
      }

      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
        p->wait_ticks = 0;
        prio_enqueue(p);
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
void
backtrace(void)
{
    uint64 fp;
    asm volatile("mv %0, s0" : "=r" (fp));

    while (fp != 0 && fp >= KERNBASE) {
        uint64 ret_addr = *(uint64*)(fp + 8);
        printf("%p\n", (void *)ret_addr);  // ← 关键修正：加 (void *)

        uint64 *frame = (uint64 *)fp;
        fp = *frame;

        // 可选：增加安全边界，防止无效遍历
        if (fp <= (uint64)frame || (fp & 0x7) != 0) {
            break;
        }
    }
}

void
schedule(void)
{
  struct cpu *c = mycpu();
  struct proc *prev = myproc();
  struct proc *next;
  int intena;

  intena = intr_get();
  intr_off();

  for(;;){
    next = prio_pick_next();

    if(next == 0){
      // 就绪队列空了，跑 idle
      next = c->idleproc;
      break;
    }

    // idle 不会在 runq 里，一般不会走到这里
    if(next == c->idleproc){
      break;
    }

    // 短暂加锁，检查状态并设置为 RUNNING
    acquire(&next->lock);
    if(next->state == RUNNABLE){
      next->state = RUNNING;
      next->wait_ticks = 0;
      release(&next->lock);
      break;
    }
    // 状态不对，放锁重选
    release(&next->lock);
  }

  c->proc = next;

  if(prev != next){
    swtch(&prev->context, &next->context);
  }

  if(intena)
    intr_on();
}


static void
idle_main(void)
{
  for (;;) {
    // 开中断、让设备中断可以唤醒其它进程
    intr_on();
    // 直接调用 schedule() 看看有没有就绪进程可跑
    schedule();
    // 理论上 schedule 不会返回到 idle_main，
    // 只有当 schedule 再次选中 idleproc 时才会回来
  }
}

static struct proc*
alloc_idleproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == UNUSED){
      
      p->pid = 0;
      safestrcpy(p->name, "idle", sizeof(p->name));

      p->state = RUNNABLE;
      p->prio = PRIO_MAX;
      p->base_prio = PRIO_MAX;
      p->wait_ticks = 0;
      p->rq_next = 0;

      // 和普通进程一样分配 trapframe 和 pagetable（必须）
     if((p->trapframe = (struct trapframe*)kalloc()) == 0){
        release(&p->lock);
        panic("idle trapframe");
      }
      p->pagetable = proc_pagetable(p);
      if(p->pagetable == 0){
        release(&p->lock);
        panic("idle pagetable");
      }
      // context：ra=idle_main, sp=kernel stack top
      memset(&p->context, 0, sizeof(p->context));
      p->context.ra = (uint64)idle_main;
      p->context.sp = p->kstack + PGSIZE;

      release(&p->lock);
      return p;
    }
    release(&p->lock);
  }
  panic("alloc_idleproc: no free proc");
}


