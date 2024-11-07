# Task 1 Sytem Calls

## Task 1.A

### Task Content
In this task we are expected to implement a new system call getreadcount(). The system call returns the value of a counter which is incremented every time any process calls the <b>read()</b> system call.

### Implementation

#### Adding the counter

- At first we added  readid variable at the struct proc in _proc.h_ for keeping account of read calls and initialized the readid at 0 in the function allocproc() where a procces is allocated at first (_proc.c_)
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
  int readid; // For keeping account of read calls.
};
```

```C
//proc.h
  p->readid = 0; // Initialized readid to 0
```

#### Constructing the System Call
We basically followed this tutorial to create a new system call https://www.geeksforgeeks.org/xv6-operating-system-adding-a-new-system-call/

```C
//syscall.h
    #define SYS_getreadcount 22

//syscall.c
    extern int sys_getreadcount(void); //my change
    .
    .
    .
    [SYS_getreadcount] sys_getreadcount,

//user.h
    int getreadcount(void);

//usys.S
    SYSCALL(getreadcount)
```

And lastly defined our system call in _sysproc.c_
```C
//sysproc.c
    // Takes no arguments and returns an int.
    int
    sys_getreadcount(void)
    {
        return myproc()->readid;
    }
```

#### Incrementing the read counter
To increment read counter we basically checked in each call if the call is a read() call or not and if it is a read() call incremented the counter by 1.

```C
syscall(void)
{
  int num;
  struct proc *curproc = myproc();

  num = curproc->tf->eax;
  if (num==SYS_read){ // Checking if the call is a read() call
    readcount++; // Increasing readid by one.
  }

  if (num==SYS_getreadcount){// also updating the readid in the current process
    curproc->readid = readcount; //my change
  }
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    curproc->tf->eax = syscalls[num]();
  } else {
    cprintf("%d %s: unknown sys call %d\n",
            curproc->pid, curproc->name, num);
    curproc->tf->eax = -1;
  }
}

```
### Testing
Somehow the test provided in the project pdf is not working in our system(At least we couldn't manage it.) We made our conceptual tests for this task by combining this task with _1.B_ and using getreadcall() in ls.c.

```C
//syscall.h
    int indicator = 0; //To keeping account of actual reads
    void 
    syscall(void)
    {
        .
        .
        .
        if(num===SYS_read){
            indicator++;
            cprintf("%s -> %d: call %d\n", syscallnames[num-1],// we will be sure that each printed call is a read call
            num,
            indicator
            );
        }
    }

//ls.c
    void
    ls(char *path)
    {
        printf(1,"Before read count: %d\n", getreadcount());
        char buf[512], *p;
        int fd;
        struct dirent de;
        
        .
        .
        .

        printf(1,"After read count: %d\n", getreadcount());
    }
```

This is the result we have for this task:

![test-result1a](https://imgur.com/xKbOmZZ.png)

## Task 1.B


### Implementation
To trace the system calls we created an array of system call names in the same order with the system call definition in syscall.h and in each system call we printed the call's name and return value.

```C
//syscall.c

    // Created an array of system call names in the same order with syscall.h.
    // So when we call it with eax - 1 it will give us the syscall name
    const char* syscallnames[] = {
        "fork",
        "exit",
        "wait",
        "pipe",
        "read",
        "kill",
        "exec",
        "fstat",
        "chdir",
        "dup",
        "getpid",
        "sbrk",
        "sleep",
        "uptime",
        "open",
        "write",
        "mknod",
        "unlink",
        "link",
        "mkdir",
        "close"
    };


    void
    syscall(void)
    {
    int num;
    struct proc *curproc = myproc();

    .
    .
    .
    
    // Task 1.B
    cprintf("%s -> %d\n", syscallnames[num-1], curproc->tf->eax);
    }
```
### Testing
This is the result we got

![test-result1a](https://imgur.com/1CiCTuj.png)
