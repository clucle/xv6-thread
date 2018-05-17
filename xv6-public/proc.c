#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#define DEBUG 0
#define THREADDEBUG 0

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
  struct pqstride stride;
  struct mlfq mlfq;
} ptable;

int
comparenode(struct proc *low, struct proc *high)
{
  return (low->u1.passvalue < high->u1.passvalue);
}

void
swap(struct proc** p1, struct proc** p2)
{
  struct proc* tmp = *p1;
  *p1 = *p2;
  *p2 = tmp;
}

void
shiftup(int index)
{
  if (index <= 1) return ;
  int parent = index >> 1;
  if (comparenode(ptable.stride.p[index], ptable.stride.p[parent])) {
    swap(&ptable.stride.p[parent], &ptable.stride.p[index]);
    shiftup(parent);
  }
}

void
shiftdown(int index)
{
  int l = index * 2;
  int r = index * 2 + 1;
  int imin = index;

  if (l <= ptable.stride.cntproc
      && comparenode(ptable.stride.p[l], ptable.stride.p[imin])) {
    imin = l;
  }

  if (r <= ptable.stride.cntproc
      && comparenode(ptable.stride.p[r], ptable.stride.p[imin])) {
    imin = r;
  }

  if (imin == index) return ;
  swap(&ptable.stride.p[imin], &ptable.stride.p[index]);
  shiftdown(imin);
}

void
push(struct proc *p)
{
  int i = ++ptable.stride.cntproc;
  ptable.stride.p[i] = p;
  shiftup(i);
}

void
pop(void)
{
  int i = ptable.stride.cntproc;
  if (i == 0) return;
  swap(&ptable.stride.p[1], &ptable.stride.p[i]);
  ptable.stride.p[i] = 0;
  ptable.stride.cntproc--;
  shiftdown(1);
}

void
boost(void)
{
#if DEBUG
  cprintf("[do boosting]\n");
#endif
  ptable.mlfq.priority = 0;
  ptable.mlfq.index = 0;
  ptable.mlfq.tick = 0;

  struct proc* p;
  for (p = ptable.proc; p< &ptable.proc[NPROC]; p++) {
    if (p->type == 'm' && p->state == RUNNABLE) {
      p->u1.priority = 0;
      p->u2.tick = 0;
      p->u3.runticks = 0;
    }
  }
}

int
ticklimit(int priority)
{
  if (priority == 0) return 1;
  if (priority == 1) return 2;
  if (priority == 2) return 4;
  panic("ticklimit get wrong priority");
  return -1;
}

int
runlimit(int priority)
{
  if (priority == 0) return 5;
  if (priority == 1) return 10;
  panic("run limit get wrong priority");
  return -1;
}

void deallocthread(struct proc* p, int pid);
void exitproc(struct proc* p);

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid(void) {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->main_thread = p;
  p->maxtid = 0;
  int i;
  for (i = 0; i < 64; i++) {
    p->hasThread[i] = 0;
  }
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  p->type = 'm';
  p->u1.priority = 0;
  p->u2.tick = 0;
  p->u3.runticks = 0;
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  p->heap = PGSIZE;
  p->stack = KERNBASE - PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();
  struct proc *mthread = curproc->main_thread;
#if THREADDEBUG
  cprintf("[PID : %d] heap : %d\n", mthread->pid, mthread->heap);
#endif
  sz = mthread->heap;
  if(n > 0){
    if((sz = allocuvm(mthread->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(mthread->pgdir, sz, sz + n)) == 0)
      return -1;
  }

  mthread->heap = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc()->main_thread;

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  // Copy process state from proc.
#if THREADDEBUG
  cprintf("[FORK] %d %d %d\n", curproc->pid, curproc->heap, curproc->stack);
#endif
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->heap, curproc->stack)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
#if THREADEBUG
  cprintf("[FORKEND]\n");
#endif
  np->sz = curproc->sz;
  np->heap = curproc->heap;
  np->stack = curproc->stack;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

void
printstate(struct proc* p)
{
  switch(p->state) {
    case UNUSED:
      cprintf("UNUSED  ");
      break;
    case EMBRYO:
      cprintf("EMBRYO  ");
      break;
    case SLEEPING:
      cprintf("SLEEPING  ");
      break;
    case RUNNABLE:
      cprintf("RUNNABLE  ");
      break;
    case RUNNING:
      cprintf("RUNNING  ");
      break;
    case ZOMBIE:
      cprintf("ZOMBIE  ");
      break;
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if (curproc->type == 's') {
    ptable.stride.total_tickets -= curproc->u2.tickets;
    pop();
  }

  if(curproc == initproc)
    panic("init exiting");

  deallocthread(curproc, -1);
  /*
  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;
*/
  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);
  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}


void check_down_priority(struct proc* p) {
  if (p->u1.priority > 1) return ;
  if (p->u3.runticks >= runlimit(p->u1.priority)) {
#if DEBUG
    cprintf("PRIORITY DOWN\n");
#endif
    p->u1.priority++;
    p->u2.tick = 0;
    p->u3.runticks = 0;
  }
}
  
void
mlfq_run(struct cpu* c)
{
  struct proc* p;

  if (ptable.mlfq.tick >= 100) boost();

  int i;
  int icur = 0;
  int min = 2;

  for (i = icur; i < NPROC; i++) {
    p = &ptable.proc[i];
    if (p->type == 'm' && p->state == RUNNABLE) {
      if (min > p->u1.priority) {
        min = p->u1.priority;
      }
    }
  }

  if (ptable.mlfq.priority != min) {
    // Level Change
    ptable.mlfq.priority = min;
    ptable.mlfq.index = 0;
  }

  icur = ptable.mlfq.index;
  int find = 0;

  for (i = icur; i < NPROC; i++) {
    p = &ptable.proc[i];
    if (p->type == 'm' && p->state == RUNNABLE
        && p->u1.priority == ptable.mlfq.priority) {
      find = 1;
      ptable.mlfq.index = i + 1;
      break;
    }
  }
  i = 0;
  if (find) i = NPROC;
  for (; i < icur; i++) {
    p = &ptable.proc[i];
    if (p->type == 'm' && p->state == RUNNABLE
        && p->u1.priority == ptable.mlfq.priority) {
      find = 1;
      ptable.mlfq.index = i + 1;
      break;
    }
  }

  if (find) {
    c->proc = p;
    ptable.mlfq.tick++;
    p->u2.tick++;
    p->u3.runticks++;

#if DEBUG
    cprintf("schd call %d\n", myproc()->u1.priority);
#endif

    check_down_priority(p);

    switchuvm(p);
    p->state = RUNNING;
    swtch(&(c->scheduler), p->context);
    switchkvm();
    c->proc = 0;
  }
}

void
stride_run(struct cpu *c)
{
  struct proc* p;
  if (ptable.stride.cntproc > 0) {
    p = ptable.stride.p[1];
    if (p->state != RUNNABLE) {
      pop();
      push(p);
      p->u1.passvalue += p->u3.stride;
      return ;
    }
    c->proc = p;
    switchuvm(p);
    p->state = RUNNING;
    swtch(&(c->scheduler), p->context);
    switchkvm();
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;
  boost(); 
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    if (ptable.stride.cntproc > 0) {
      if (ptable.mlfq.passvalue <= ptable.stride.p[1]->u1.passvalue) {
        // MLFQ using STRIDE
        ptable.mlfq.passvalue += (1000 / (100 - ptable.stride.total_tickets));
        mlfq_run(c);
      } else {
        // STRIDE using STRIDE
        stride_run(c);
      }
    } else {
      // JUST MLFQ
      mlfq_run(c);
    }    
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();
  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  if (myproc()->type == 'm') myproc()->u2.tick = 0;
  else if (myproc()->type == 's') {
    struct proc* p = ptable.stride.p[1];
    pop();
    push(p);
    p->u1.passvalue += p->u3.stride;
  }
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

void
mlfq_yield(void)
{
  struct proc* p = myproc();
#if DEBUG
    cprintf("mlfq called\n");
#endif
  if (p->u2.tick < ticklimit(p->u1.priority)) {
    p->u2.tick++;
    p->u3.runticks++;
    ptable.mlfq.tick++;

#if DEBUG
    cprintf("mlfq call %d\n", myproc()->u1.priority);
#endif
    check_down_priority(p);
  } else {
    p->u2.tick = 0;
#if DEBUG
    cprintf("YIELD\n");
#endif
    yield();
  }
}

void
stride_yield(void)
{
  yield();
}

int
getlev(void)
{
  return myproc()->u1.priority;
}

int
set_cpu_share(int tickets)
{
  if (tickets == 0) return -1;
  struct proc* p = myproc();
  if (p->type == 'm') {
    if (ptable.stride.total_tickets + tickets > 80) return -1;
    
    acquire(&ptable.lock);
    ptable.stride.total_tickets += tickets;
    p->u2.tickets = tickets;
    p->u3.stride = 1000 / tickets;
    if (ptable.stride.cntproc == 0) {
      p->u1.passvalue = ptable.mlfq.passvalue;
    } else {
      p->u1.passvalue = ptable.stride.p[1]->u1.passvalue;
    }
    p->type = 's';
    push(p);
  } else {
    int future_tickets = ptable.stride.total_tickets + tickets - p->u2.tickets;
    if (future_tickets > 80) return -1;
    
    acquire(&ptable.lock);
    ptable.stride.total_tickets = future_tickets;
    p->u2.tick = tickets;
    p->u3.stride = 1000 / tickets;
  }
  release(&ptable.lock);
  return tickets;
}

int
thread_create(thread_t *thread, void *(*start_routine)(void *), void *arg)
{
  struct proc *np;
  struct proc *curproc = myproc();

  // ptable has full process
  if ((np = allocproc()) == 0)
  {
    panic("allocproc fail\n");
    return -1;
  }

  // Set Parent
  struct proc *mthread = curproc->main_thread;
  np->main_thread = mthread;
  np->parent = mthread->parent;
  *thread = np->pid;

  // TODO: Set All Process Stride Value Change
  if (mthread->type == 's')
  {
    // pass
  }

  // Copy File Descriptor
  int i;
  for (i = 0; i < NOFILE; i++)
  {
    if (curproc->ofile[i])
    {
      np->ofile[i] = filedup(curproc->ofile[i]);
    }
  }
  np->cwd = idup(mthread->cwd);
  safestrcpy(np->name, mthread->name, sizeof(mthread->name));

  // Alloc Thread at Main Thread
  int tid = 0;
  for (i = 1; i < NPROC; i++)
  {
    if (!mthread->hasThread[i])
    {
      tid = i;
      mthread->hasThread[i] = 1;
      break;
    }
  }

  if (tid == 0)
  {
    panic("can't find empty thread space");
    return -1;
  }

  np->tid = tid;
  
  // Alloc Stack;
 
  uint sz, ustack[2];
  pde_t *pgdir;
  pgdir = mthread->pgdir;



  sz = mthread->stack - ((3 + (tid - 1)) * PGSIZE);
  if (tid > mthread->maxtid) {
    mthread->maxtid = tid;
    // alloc
    sz -= PGSIZE;
    np->stack = sz;
    if ((sz = allocuvm(pgdir, sz, sz + PGSIZE)) == 0)
    {
      panic("alloc uvm at thread create fail");
      return -1;
    }
  }

  ustack[0] = 0xffffffff;
  ustack[1] = (uint)arg;
  sz -= 8;
  if (copyout(pgdir, sz, ustack, 8) < 0)
  {
    panic("copyout fail");
    return -1;
  }

  np->tf->eax = 0;
  *np->tf = *curproc->tf;
  np->pgdir = pgdir;
  np->sz = mthread->sz;
  np->heap = mthread->heap;
  np->tf->eip = (uint)start_routine;
  np->tf->esp = sz;

  // Change Process State
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);
  return 0;
}

int
thread_join(thread_t thread, void **retval)
{
  struct proc *p;
  struct proc *curproc = myproc();
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pid == thread)
    {
      while (1)
      {
        if (p->state == ZOMBIE)
        {
          *retval = (void*)p->maxtid;
          exitproc(p);
          kfree(p->kstack);
          release(&ptable.lock);
          return 0;
        } else {
          sleep(curproc, &ptable.lock);
        }
      }
    }
  }
  release(&ptable.lock);
  return -1;
}

void thread_exit(void *retval)
{
  struct proc *curproc = myproc();
  if (curproc->main_thread == curproc)
  {
    exit();
  }

  curproc->maxtid = (int)retval;
  acquire(&ptable.lock);
  curproc->state = ZOMBIE;
  wakeup1(curproc->main_thread);
  sched();
  panic("thread exit error with zombie");
}

void deallocthread(struct proc* mthread, int pid)
{
  struct proc* p;
  pde_t *pgdir = mthread->pgdir;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->pgdir == pgdir && p->pid != pid)
    {
      exitproc(p);
      begin_op();
      iput(p->cwd);
      end_op();
      p->cwd = 0;
    }
  }

  uint sz = KERNBASE - 3 * PGSIZE;
  uint min = sz - (mthread->maxtid * PGSIZE);
  mthread->maxtid = 0;
  if ((sz = deallocuvm(mthread->pgdir, min, sz))== 0)
  {
    panic("dealloc uvm err");
  }
}

void exitproc(struct proc *p)
{
  int fd;
  for (fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
      fileclose(p->ofile[fd]);
      p->ofile[fd] = 0;
    }
  }
  p->state = UNUSED;
  p->main_thread->hasThread[p->tid] = 0;
  
  // wakeup1(p->parent);
  
  /*for (p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if (p->parent == p) {
      p->parent = initproc;
      if (p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }*/
  //p->state = UNUSED;
}

