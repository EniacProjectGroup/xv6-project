# Task 2 Lottery Scheduler

### Task Content
In this task we are expected implement exception handling for derefrences for null pointers and ability to change the protection levels of some pages in a process's address space.

### Implementation

#### Preventing null pointer dereference

First thing we have done testing null dereference testing. In order to do that we created a basic user level program which dereferences a null pointer and run it.
```C

//null.c

#include "user.h"

int  main(int argc, char *argv[])
{
  char *p =0 ;
  printf(1, "%x\n",*p);
  exit();
}
```

This is the result of _null.c_

![null-result](https://imgur.com/ppi6CZJ.png)


First thing we have done to prevent null pointer dereference is modifying exec() function to skip mapping the first page.

```C
//exec.c

    int exec(char* path, char** argv) {
    .
    .
    .    
    //sz = 0;
    sz = PGSIZE; // my change
    for(i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
        if(readi(ip, (char*) &ph, off, sizeof(ph)) != sizeof(ph))
        goto bad;
        if(ph.type != ELF_PROG_LOAD)
        continue;
        if(ph.memsz < ph.filesz)
        goto bad;
        if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
        goto bad;
        if(loaduvm(pgdir, (char*) ph.vaddr, ip, ph.off, ph.filesz) < 0)
        goto bad;
    }
    
    .
    .
    .

    bad:
        if(pgdir)
            freevm(pgdir);
        if(ip) {
            iunlockput(ip);
            end_op();
        }
    return -1;
    }

```
Also we need to modify the linker flags for user programs in MakeFile and set the entry point to _0x1000_

```C
//Makefile
    _%: %.o $(ULIB)
	// $(LD) $(LDFLAGS) -T $U/user.ld -o $@ $^
	$(LD) $(LDFLAGS) -T $U/user.ld -Ttext 0x1000 -o $@ $^

    $U/_forktest: $U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	// $(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
	$(LD) $(LDFLAGS) -N -e main -Ttext 0x1000 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
```

We also need to modify the start point of copyuvm() in vm.c
```C
//vm.c

pde_t* copyuvm(pde_t* pgdir, uint sz) {
  .
  .
  .

    //for(i = 0; i < sz; i += PGSIZE) {
    for(i = PGSIZE; i < sz; i += PGSIZE) {
        if((pte = walkpgdir(pgdir, (void*) i, 0)) == 0)
        panic("copyuvm: pte should exist");
        if(!(*pte & PTE_P))
        panic("copyuvm: page not present");
        pa = PTE_ADDR(*pte);
        flags = PTE_FLAGS(*pte);
        
    .
    .
    .

    bad:
    freevm(d);
    return 0;
}
```

To prevent unwanted bugs we alse changed the condition in argptr() in _syscall.c_

```C
//syscall.c

    int argptr(int n, char** pp, int size) {
    int i;

    if(argint(n, &i) < 0)
        return -1;
    // if((uint) i >= proc->sz || (uint) i + size > proc->sz)
    if((uint) i >= proc->sz || (uint) i + size > proc->sz || (uint) i == 0)
        return -1;
    *pp = (char*) i;
    return 0;
    }
```

#### Implmenting memory protection system calls
In this part we created two system calls _mprotect_ and _munprotect_. 

_mprotect_ makes a range of pages read-only by clearing the write bit (PTE_W) in their page table entries and validates the address range and ensures the pages are present before modifying the permissions.

_munportect_ reverts the changes made by mprotect, making pages writable again by setting the write bit (PTE_W).

! since we already showed how we created system calls in the previous tasks we will only focus the implementation of calls this time.

We created our system call handlers for _mprotect_ and _munprotect_ in _sysproc.c_. They parse user input, validate arguments, and invoke the respective kernel functions.

```C
//sysproc.c

    int
    sys_mprotect()
    {
    void *addr;
    int len;
    
    if(argptr(0,(void *)&addr,sizeof(*addr)) < 0) //Extracts the first argument (address) from the system call.
        return -1;
    
    if(argint(1,&len)<0  || len <= 0) //Extracts the second argument (len, the number of pages) and ensures it is valid. 
        return -1;

    return mprotect(addr, len); //Passes the parsed addr and len to the kernel function mprotect for actual implementation.
    }

    //sys_munprotect works identically to sys_mprotect, but calls munprotect instead.

    int
    sys_munprotect()
    {
    void *addr;
    int len;
    
    if(argptr(0,(void *)&addr,sizeof(*addr)) < 0)
        return -1;
    
    if(argint(1,&len)<0 || len <= 0)
        return -1;

    return munprotect(addr, len);
    }
```

We defined _mprotect_ and _munprotect_ in _vm.c_. These functions perform the core task of modifying memory page permissions at the page table level.

```C
//vm.c

    int mprotect(void *addr, int len)
    {
    struct proc *curproc = myproc();
    pte_t *pte ;
    pde_t *pde ;

    char *address =(char *)PGROUNDDOWN((uint)addr); // Aligns the starting address to the nearest page boundary (downward).


    // Iterates over the specified range of pages. address increments by one page (PGSIZE) in each iteration, and len decrements.
    for ( ; len > 0 ; address += PGSIZE , len--) {
        /*
        Pde: Points to the page directory for the address.
        PDX: Retrieves the Page Directory Index (PDI) for the current address.
        */
        pde = &curproc->pgdir[PDX(address)];
        if (!(*pde & PTE_P)) {
        // Page table not present, cannot change permissions
        return -1;
        }

        // Retrieves the Page Table Index (PTI) for the current address and converts the physical address of the page table to a virtual address.
        pte = &((pte_t *)P2V(PTE_ADDR(*pde)))[PTX(address)];
        
        if (!(*pte & PTE_P)) {
        // Page not present, cannot change permissions
        return -1;
        }

        *pte &= ~PTE_W; // Clears the write bit (PTE_W) in the page table entry, making the page read-only.
    }


    lcr3(V2P(curproc->pgdir)); //Reloads the page table base register (CR3). This ensures the hardware reflects the updated page table permissions.

    return 0; // success
    }


    // munprotect works identical with mprotect except it doesnt make the page read-only.
    // It reverts this change and make the page writable again.

    int munprotect(void *addr, int len)
    {
    struct proc *curproc = myproc();
    pte_t *pte ;
    pde_t *pde ;

    char *address =(char *)PGROUNDDOWN((uint)addr);
    
    for ( ; len > 0 ; address += PGSIZE , len--) {
        pde = &curproc->pgdir[PDX(address)];
        if (!(*pde & PTE_P)) {
        // Page table not present, cannot change permissions
        return -1;
        }
        pte = &((pte_t *)P2V(PTE_ADDR(*pde)))[PTX(address)];
        if (!(*pte & PTE_P)) {
        // Page not present, cannot change permissions
        return -1;
        }
        *pte |= PTE_W;
    }
    lcr3(V2P(curproc->pgdir));
    return 0;
    }
```

#### Tests

This test checks whether the mprotect system call successfully makes a memory page read-only and verifies that writing to the page triggers a page fault.
```C
//mprotect-test.c

    #include "user.h"

    int main(int argc, char *argv[]) {
    char *addr = malloc(4096); //Allocates a single page of memory (4 KB) using malloc

    if (addr == 0) { //Ensures the malloc call was successful.
        printf(1, "Error: malloc failed\n");
        exit();
    }
    printf(1, "Allocated page at address %p\n", addr);
    if (mprotect(addr, 1) < 0) { //Calls mprotect to mark the allocated memory page as read-only.
        printf(1, "Error: mprotect failed\n");
        exit();
    }
    printf(1, "if a trap err occurred ----> mprotect test passed\n");
    addr[0] = 'A'; // Attempts to write to the read-only page (addr[0] = 'A'). This should trigger a page fault.

    if (munprotect(addr, 1)<0) { //Calls munprotect to revert the page's permissions to writable.
        printf(1, "Error munprotect failed\n");
    }
    addr[0]='A'; //Attempts to write to the page again. This should succeed now.

    printf(1, "munprotect test passed\n");
    exit();
    }
```

This test directly validates that munprotect successfully re-enables write access to a page after it has been made read-only.
```C
//munprotect-test.c

    #include "user.h"

    int main(int argc, char *argv[]) {
    char *addr = malloc(4096); //Allocates a single page of memory
    if (addr == 0) {
        printf(1, "Error: malloc failed\n");
        exit();
    }
    printf(1, "Allocated page at address %p\n", addr);
    if (mprotect(addr, 1) < 0) { //Calls mprotect to make the page read-only. If it fails, prints an error and exits.
        printf(1, "Error: mprotect failed\n");
        exit();
    }
    if (munprotect(addr, 1)<0) { //Calls munprotect to restore write permissions to the page.
        printf(1, "Error munprotect failed\n");
    }
    addr[0]='A'; //Attempts to write to the page.
    printf(1, "munprotect test passed\n");
    exit();
    }
```

This screenshot is our outcome from the above tests.

![test-outcome](https://imgur.com/D9CObEp.png)

#### Conclusion

In this project, we enhanced xv6's virtual memory by implementing null-pointer dereference protection and dynamic memory protection with mprotect and munprotect. These changes improve system robustness and memory safety by preventing invalid access and allowing user programs to enforce read-only memory for critical regions.

The implementation required modifying page tables, handling edge cases, and ensuring updates were reflected in the hardware. By inheriting these protections during fork, we maintained consistency in multi-process environments. This project deepened our understanding of virtual memory management and system-level programming.