# Task 3 Kernel Thread Implementation

### Task Content
In this task, we are expected to implement kernel-level threading support in xv6. This includes creating a new system call, clone(), to spawn kernel threads that share the address space of their parent process, and a join() system call to wait for a thread's completion and clean up its resources. Additionally, we build a lightweight user-level thread library that provides abstractions for thread creation (thread_create), thread synchronization (lock_acquire and lock_release), and thread termination (thread_join).

### Implementation

#### Creating clone() and join() calls

! since we already showed how we created system calls in the previous tasks we will only focus the implementation of calls this time.

First thing we have done for creating clone and join system calls was adding a new property called _threadstack_ which address of thread to be freed to _proc.h_.
```C

//proc.h

    struct proc {
    uint sz;                     // Size of process memory (bytes)
    pde_t* pgdir;                // Page table
    char *kstack;                // Bottom of kernel stack for this process
    void *threadstack;          // my change
    enum procstate state;        // Process state
    .
    .
    .
    }
```

Afterwards we created the handler for clone and join in sysproc.c

```C
//sysproc.c

    int
    sys_clone(void)
    {
    /*
    //fcn: the address of the function the new thread should execute.
    //stack: the address of the new thread's stack.
    //arg1 & arg2: the arguments to be passed to the thread function.
    */
    int fcn, arg1, arg2, stack;
    
    //extracts arguments from user space by their position in the system call
    if(argint(0, &fcn)<0 || argint(1, &arg1)<0 || argint(2, &arg2)<0 || argint(3, &stack)<0)
        return -1; //any argument is invalid (e.g., out of range or inaccessible)

    //Calls the kernel's clone() implementation with passing the validated arguments
    return clone((void *)fcn, (void *)arg1, (void *)arg2, (void *)stack);
    }

    int
    sys_join(void)
    {
    void **stack; //stores the address of the user stack provided by the calling thread.
    int stackArg;
    stackArg = argint(0, &stackArg); // extracts the first argument (stack address) passed by the user program and stores it in stackArg
    stack = (void**) stackArg; //Converts the integer stack address to a pointer type
    return join(stack); //Calls the kernel’s join() implementation
    }

```
and kernel implementations of these system calls.

```C
//proc.c

    // creates a new thread that shares its parent's address space but has its own stack.
    int 
    clone(void(*fcn)(void*,void*), void *arg1, void *arg2, void* stack)
    {
    struct proc *np; // represents new thread
    struct proc *p = myproc(); // current process(the parent thread)
    
    // Allocate process for new process slot.
    if((np = allocproc()) == 0)
        return -1;

    // Copy process data to the new thread
    np->pgdir = p->pgdir; // page data
    np->sz = p->sz; // memory size
    np->parent = p; // sets the parent of new thread to currect process
    *np->tf = *p->tf; // trap frame
    
    void * sarg1, *sarg2, *sret;
    // Push fake return address to the stack of thread
    sret = stack + PGSIZE - 3 * sizeof(void *);
    *(uint*)sret = 0xFFFFFFF;
    // Push first argument to the stack of thread
    sarg1 = stack + PGSIZE - 2 * sizeof(void *);
    *(uint*)sarg1 = (uint)arg1;
    // Push second argument to the stack of thread
    sarg2 = stack + PGSIZE - 1 * sizeof(void *);
    *(uint*)sarg2 = (uint)arg2;
    // Put address of new stack in the stack pointer (ESP)
    np->tf->esp = (uint) stack;
    // Save address of stack
    np->threadstack = stack;
    // Initialize stack pointer to appropriate address
    np->tf->esp += PGSIZE - 3 * sizeof(void*);
    np->tf->ebp = np->tf->esp;
    // Set instruction pointer to given function
    np->tf->eip = (uint) fcn;
    // Clear %eax so that fork returns 0 in the child.
    np->tf->eax = 0;

    /*
    ofile: copies open file descriptors from the parent to the new thread.
    cwd: duplicates the current working directory.
    name: copies the parent's name to the thread for debugging.
    */
    int i;
    for(i = 0; i < NOFILE; i++)
        if(p->ofile[i])
        np->ofile[i] = filedup(p->ofile[i]);
    np->cwd = idup(p->cwd);
    safestrcpy(np->name, p->name, sizeof(p->name));


    
    acquire(&ptable.lock);
    np->state = RUNNABLE; // Sets the thread’s state to RUNNABLE
    release(&ptable.lock);
    return np->pid;
    }

    // waits for a child thread to finish and cleans up its resources.
    int
    join(void** stack)
    {
    struct proc *p;
    int havekids, pid;
    struct proc *cp = myproc();
    acquire(&ptable.lock); // Acquires the process table lock to ensure safe iteration.

    for(;;){  // Scan through table looking for zombie children.
        havekids = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        // Check if this is a child thread (parent or shared address space)
        if(p->parent != cp || p->pgdir != p->parent->pgdir)
            continue;
            
        havekids = 1;
        if(p->state == ZOMBIE){
            // Found one.
            pid = p->pid;
            // Remove thread from the kernel stack
            kfree(p->kstack);
            p->kstack = 0;
            // Reset thread in process table
            p->pid = 0;
            p->parent = 0;
            p->name[0] = 0;
            p->killed = 0;
            p->state = UNUSED;
            stack = p->threadstack;
            p->threadstack = 0;
            release(&ptable.lock);
            return pid;
        }
        }
        // No point waiting if we don't have any children.
        if(!havekids || cp->killed){
        release(&ptable.lock);
        return -1;
        }
        // Wait for children to exit.
        sleep(cp, &ptable.lock);  
    }
    }
```

We created a simple spinlock and thread library in ulib.c but first we defined the functions in user.h

```C
//user.h
    #ifndef __USER__
    .
    .
    .
    int thread_create(void (*start_routine)(void *,void*), void * arg1, void * arg2);
    int thread_join(); 
    int lock_init(lock_t *lk);
    void lock_acquire(lock_t *lk);
    void lock_release(lock_t *lk);
    #endif
```

```C
//ulib.c

    int thread_create(void (*start_routine)(void *, void *), void* arg1, void* arg2)
    {
        void* stack;
        stack = malloc(PGSIZE); // allocates one page of memory for the new thread’s stack
        return clone(start_routine, arg1, arg2, stack); 
    }

    int thread_join()
    {
        void * stackPtr; // declares a pointer to store the address of the thread’s stack and passes it to join()
        int x = join(&stackPtr);
        return x;
    }
    
    int lock_init(lock_t *lk)
    {
        lk->flag = 0; // initializes the lock’s flag to 0, indicating that the lock is unlocked.
        return 0;
    }
    
    void lock_acquire(lock_t *lk)
    {
        while(xchg(&lk->flag, 1) != 0); // sets flag back to 1
        // if the lock is already held (flag == 1), the thread waits in a spin loop until the lock is released.
    }

    void lock_release(lock_t *lk)
    {
        xchg(&lk->flag, 0); // sets the lock’s flag to 0 (unlocked) atomically.
        // ensures no other thread modifies the flag while it’s being released.
    }
```

#### Tests

We created a test program called _testthreads.c_ which validates the functionality of the thread library by creating threads that run specific functions (f1, f2, and f3). It tests both synchronized and unsynchronized thread execution to demonstrate proper thread creation, execution order, synchronization, and cleanup.

```C
//_testthreads.c

    #include "types.h"
    #include "stat.h"
    #include "user.h"
    #include "fcntl.h"
    #define SLEEP_TIME 100 //Specifies the sleep duration (100 ticks) for each thread.

    lock_t* lk; // A pointer to a lock used to enforce synchronization between threads in the first test scenario.
    
    void f1(void* arg1, void* arg2) {
        //Casts arg1 to an integer pointer and dereferences it to get its value.
        int num = *(int*)arg1;
        if (num) lock_acquire(lk);
        printf(1, "1. this should print %s\n", num ? "first" : "whenever");
        printf(1, "1. sleep for %d ticks\n", SLEEP_TIME);
        sleep(SLEEP_TIME);
        
        //If num is 1, the thread acquires the lock to synchronize its execution with other threads.
        if (num) lock_release(lk);
        exit();
    }

    // f2 and f3 follow the same structure, with adjusted log identifiers.

    void f2(void* arg1, void* arg2) {
        int num = *(int*)arg1;
        if (num) lock_acquire(lk);
        printf(1, "2. this should print %s\n", num ? "second" : "whenever");
        printf(1, "2. sleep for %d ticks\n", SLEEP_TIME);
        sleep(SLEEP_TIME);
        if (num) lock_release(lk);
        exit();
    }
    
    void f3(void* arg1, void* arg2) {
        int num = *(int*)arg1;
        if (num) lock_acquire(lk);
        printf(1, "3. this should print %s\n", num ? "third" : "whenever");
        printf(1, "3. sleep for %d ticks\n", SLEEP_TIME);
        sleep(SLEEP_TIME);
        if (num) lock_release(lk);
        exit();
    }
    
    int
    main(int argc, char *argv[])
    {
        lock_init(lk); // Initializes the global lock (lk) to synchronize thread execution.

        int arg1 = 1, arg2 = 1; // Sets arg1 to 1, ensuring the lock is acquired and threads execute sequentially.
        printf(1, "below should be sequential print statements:\n");
        
        // Creates three threads that execute f1, f2, and f3 with the same arguments.
        thread_create(&f1, (void *)&arg1, (void *)&arg2);
        thread_create(&f2, (void *)&arg1, (void *)&arg2);
        thread_create(&f3, (void *)&arg1, (void *)&arg2);
        
        //Waits for all three threads to finish execution in the order they were created.
        thread_join();
        thread_join();
        thread_join();
        
        arg1 = 0; // Disables the lock mechanism by setting num to 0 in all thread functions.
        printf(1, "below should be printed concurrently:\n");
        thread_create(&f1, (void *)&arg1, (void *)&arg2);
        thread_create(&f2, (void *)&arg1, (void *)&arg2);
        thread_create(&f3, (void *)&arg1, (void *)&arg2);
        thread_join();
        thread_join();
        thread_join();
        
        exit();
    }
```

![test-outcome](https://imgur.com/3OxbrBx.png)

#### Conclusion

In this project, we implemented threading support in xv6 with system calls (clone and join) and a user-level thread library. We enabled threads to share the same address space while using separate stacks and added a spinlock mechanism for synchronization. Testing verified correct thread creation, execution, and synchronization in both sequential and concurrent scenarios. This task enhanced our understanding of multi-threading and synchronization in operating systems.
