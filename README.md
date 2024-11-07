# Task 2 Lottery Scheduler

### Task Content
In this task we are expected to implement lottery scheduler. Lottery Scheduling is a type of process scheduling, somewhat different from other Scheduling. Processes are scheduled in a random manner. Lottery scheduling can be preemptive or non-preemptive. It also solves the problem of starvation. Giving each process at least one lottery ticket guarantees that it has a non-zero probability of being selected at each scheduling operation.

### Implementation

#### Creating System Calls

First thing we have done is adding ticket(Accounting the number of ticket that process has) and ticks(how much time it is runed) attribute to struct proc in _proc.h_.
```C
//proc.h

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
  int readid; // For keeping account for the read calls.
  int tickets;
  int ticks;
};
```

In order to implement lottery scheduler we need settickets() system call and for observing the scheduler we need getpinfo() system call. We started with defining these system calls.

```C
//syscall.h

#define SYS_settickets 22
#define SYS_getpinfo 23
```
```C
//syscall.c

extern int sys_settickets(void);
extern int sys_getpinfo(void);

.
.
.

[SYS_settickets] sys_settickets,
[SYS_getpinfo] sys_getpinfo,
```
```C
//user.h

int settickets(int number);
int getpinfo(struct pstat *pinfo);
```

```C
//usys.S

    SYSCALL(settickets)
    SYSCALL(getpinfo)
```

! pstat.h (struct pstat) is directly used in the project without a change as stated in the task documentation.
```C
//pstat.h

    #ifndef _PSTAT_H_
    #define _PSTAT_H_
    #include "param.h"
    struct pstat {
    int inuse[NPROC];   // whether this slot of the process table is in use (1 or 0)
    int tickets[NPROC]; // the number of tickets this process has
    int pid[NPROC];     // the PID of each process 
    int ticks[NPROC];   // the number of ticks each process has accumulated 
    };
    #endif // _PSTAT_H_
```

Definition of system calls in _sysproc.c_
```C
//sysproc.c

int
sys_settickets(void)
{
  int number;
  if (argint(0, &number) < 0 || number < 1) {
    return -1;
  }
  // Update the tickets of the current process
  struct proc *curproc = myproc();
  curproc->tickets = number;
  return 0;
}

int
sys_getpinfo(void)
{
  struct pstat *p;
  
  // Get the argument and check if it's valid
  if (argptr(0, (void*)&p, sizeof(*p)) < 0)
    return -1;
  
  return getpinfo(p);
}
```
```C
//proc.c

    int
    getpinfo(struct pstat *p) 
    {
    struct proc *proc;
    int i = 0;
    acquire(&ptable.lock);  // Lock to ensure consistency
    for (proc = ptable.proc; proc < &ptable.proc[NPROC]; proc++) {
        if (proc->state != UNUSED) {
        p->inuse[i] = 1;
        p->pid[i] = proc->pid;
        p->tickets[i] = proc->tickets; 
        p->ticks[i] = proc->ticks;
        } else {
        p->inuse[i] = 0;
        }
        i++;
    }
    release(&ptable.lock);  // Release the lock
    return 0;
    }
```

Since we will be using getpinfo() and pstat.h in kernel level we should add them in _defs.h_

```C
//defs.h

#include "pstat.h" // added in this file to use in kernel level

.
.
.

int             getpinfo(struct pstat *p); // the getpinfo function created in proc.c
```


To observe what is happining with processes and using getpinfo() system call we created a user level function called ps which gets the information from processes using getpinfo() and prints them within a table

```C
//ps.c

    #include "types.h"
    #include "stat.h"
    #include "user.h"
    #include "pstat.h"
    int main(void) {
    struct pstat p;
    if (getpinfo(&p) < 0) {
        printf(1, "Error: getpinfo failed\n");
        exit();
    }
    printf(1, "PID\tTickets\tTicks\n");
    for (int i = 0; i < NPROC; i++) {
        if (p.inuse[i]) {
        printf(1, "%d\t%d\t%d\n", p.pid[i], p.tickets[i], p.ticks[i]);
        }
    }
    exit();
    }
```
To use ps in user level we should add it to MakeFile

```C
//MakeFile

    UPROGS=\
        _cat\

        .
        .
        .
        
        _ps\
```
And this is the final result we took from ps

img

#### Managing Tickets
All tickets have 1 tickets and 0 ticks as default as stated in the project documentation
```C
//proc.c
    allocproc(void)
    {
    struct proc *p;
    char *sp;

        .
        .
        .

    found:
    p->state = EMBRYO;
    p->pid = nextpid++;

    p->readid = 0; // Initialized readid to 0
    p->tickets = 1; // By default each process has 1 tickets
    p->ticks = 0; // By default each process has 0 ticks


        .
        .
        .

    return p;
}
```

In order to inherit tickets within child processes we configured the fork() system call.
```C
//proc.c

    int
    fork(void)
    {
    int i, pid;
    struct proc *np;
    struct proc *curproc = myproc();

        .
        .
        .

    np->tickets = curproc->tickets;  // Inherit the parent's tickets

        .
        .
        .
        
    return pid;
    }

```

#### Getting a Random Number
This part is more advanced and mostly copy-paste from internet. Explanations for theese random functions are given in the _random.c_ file.

```C
//random.h

    void sgenrand(unsigned long);
    long genrand(void);
    long random_at_most(long);
```

```C
//random.c

    #include "param.h"
    #include "types.h"
    #include "defs.h"

    #define N 624
    #define M 397
    #define MATRIX_A 0x9908b0df   /* constant vector a */
    #define UPPER_MASK 0x80000000 /* most significant w-r bits */
    #define LOWER_MASK 0x7fffffff /* least significant r bits */
    /* Tempering parameters */   
    #define TEMPERING_MASK_B 0x9d2c5680
    #define TEMPERING_MASK_C 0xefc60000
    #define TEMPERING_SHIFT_U(y)  (y >> 11)
    #define TEMPERING_SHIFT_S(y)  (y << 7)
    #define TEMPERING_SHIFT_T(y)  (y << 15)
    #define TEMPERING_SHIFT_L(y)  (y >> 18)
    #define RAND_MAX 0x7fffffff
    static unsigned long mt[N]; /* the array for the state vector  */
    static int mti=N+1; /* mti==N+1 means mt[N] is not initialized */
    /* initializing the array with a NONZERO seed */
    void
    sgenrand(unsigned long seed)
    {
        /* setting initial seeds to mt[N] using         */
        /* the generator Line 25 of Table 1 in          */
        /* [KNUTH 1981, The Art of Computer Programming */
        /*    Vol. 2 (2nd Ed.), pp102]                  */
        mt[0]= seed & 0xffffffff;
        for (mti=1; mti<N; mti++)
            mt[mti] = (69069 * mt[mti-1]) & 0xffffffff;
    }
    long /* for integer generation */
    genrand()
    {
        unsigned long y;
        static unsigned long mag01[2]={0x0, MATRIX_A};
        /* mag01[x] = x * MATRIX_A  for x=0,1 */
        if (mti >= N) { /* generate N words at one time */
            int kk;
            if (mti == N+1)   /* if sgenrand() has not been called, */
                sgenrand(4357); /* a default initial seed is used   */
            for (kk=0;kk<N-M;kk++) {
                y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
                mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1];
            }
            for (;kk<N-1;kk++) {
                y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
                mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1];
            }
            y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
            mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1];
            mti = 0;
        }

        y = mt[mti++];
        y ^= TEMPERING_SHIFT_U(y);
        y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
        y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
        y ^= TEMPERING_SHIFT_L(y);
        // Strip off uppermost bit because we want a long,
        // not an unsigned long
        return y & RAND_MAX;
    }
    // Assumes 0 <= max <= RAND_MAX
    // Returns in the half-open interval [0, max]
    long random_at_most(long max) {
    unsigned long
        // max <= RAND_MAX < ULONG_MAX, so this is okay.
        num_bins = (unsigned long) max + 1,
        num_rand = (unsigned long) RAND_MAX + 1,
        bin_size = num_rand / num_bins,
        defect   = num_rand % num_bins;
    long x;
    do {
    x = genrand();
    }
    // This is carefully written not to overflow
    while (num_rand - defect <= (unsigned long)x);
    // Truncated division is intentional
    return x/bin_size;
}
```

```C
//defs.h

long            random_at_most(long max);
```

```C
//MakeFile

    OBJS = \
        bio.o\

            .
            .
            .
        
        random.o\

```

#### Configuring The Scheduler
In the scheduler function we basically loop through all runnable processes and sum up the total ticket count to get a random ticket and _continue_ until we have the process. 

```C
//proc.c

    void
    scheduler(void)
    {
    struct proc *p;
    struct cpu *c = mycpu();
    c->proc = 0;
    
    for(;;){
        // Enable interrupts on this processor.
        sti();

        
        int tickets_passed = 0;
        int totalTickets = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
            continue;
        totalTickets = totalTickets + p->tickets;  
        }
        long winner = random_at_most(totalTickets);
        // Loop over process table looking for process to run.
        acquire(&ptable.lock);
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
            continue;
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        tickets_passed += p->tickets;
        if(tickets_passed<winner){
            continue;
        }
        
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;
        swtch(&(c->scheduler), p->context);
        swtch(&(c->scheduler), c->proc->context);
        switchkvm();
        
        p->ticks++;

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        break;
        }
        release(&ptable.lock);

    }
    }
```

#### Graph (_Conceptual_)
A conceptual graph is satisfied as requested in project documentation.

![graph](https://imgur.com/ycYuZ31.png)

#### Conclusion

The lottery scheduler provides a probabilistic fairness model. Processes with more tickets tend to get more CPU time, while those with fewer tickets get less but are not completely neglected. This setup allows for some flexibility in prioritizing processes without hard guarantees on exact slice counts per cycle.

However, randomness can sometimes cause slight irregularities in time slice distribution, particularly over short observation windows. This was evident in the graph where, occasionally, the process with fewer tickets still got CPU time due to random selection. Over time, though, the scheduler consistently reflects the ticket ratio distribution.

These are some results we got with ps. Randomness is mostly visible.

![res1](https://imgur.com/nJM3gga.png)

![res2](https://imgur.com/3ifYTg5.png)