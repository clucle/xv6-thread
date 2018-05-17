// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  char type;                   // default 'm' mlfq / call cpu share 's' stride
  union {
    int priority;              // [mlfq]   cur process priority
    int passvalue;             // [stride] cur process passvalue
  } u1;
  union {
    int tick;                  // [mlfq]   cur process tick
    int tickets;               // [stride] cur process tickets
  } u2;
  union {
    int runticks;              // [mlfq]   cur process runticks
    int stride;                // [stride] cur process stride
  } u3;
  int tid;                     // [thread] thread id / init : -1
  int heap;                    // [thread] top of heap (main thread)
  int stack;                   // [thread] per process base_stack
  int hasThread[64];           // [thread] find empty stack space;
  struct proc *main_thread;    // [thread] main thread parent
  int maxtid;                  // [thread] stack max low;
  int alltickets;              // [thread] all thread's ticket saved a tmainthread
  int guard;                   // [thread] exit guard
  int cguard;                  // [thread] thread_create guard
};

struct mlfq {
  int priority;                // priority of mlfq queue
  int passvalue;               // cur location (compare stride passvalue)
  int index;                   // index using round robin
  int tick;                    // mlfq tick for boost
};

struct pqstride {
  struct proc* p[NPROC + 1];   // process priority queue
  int cntproc;                 // count stride nodes
  int total_tickets;           // stride total tickets <= 80
};

void deallocthread(struct proc* p, int pid);
// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
