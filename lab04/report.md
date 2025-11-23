<center>
<h1>OS-lab04实验报告</h1>
</center>

## 练习1：分配并初始化一个进程控制块（需要编码）

实验内容为：alloc_proc函数负责分配并返回一个新的struct proc_struct结构，用于存储新建立的内核线程的管理信息。ucore需要对这个结构进行最基本的初始化，你需要完成这个初始化过程。请回答如下问题：请说明proc_struct中`struct context context`和`struct trapframe *tf`成员变量含义和在本实验中的作用是啥？

首先，由于该练习的任务为初始化一个proc_struct结构体用于存储新建立的内核线程的管理信息，我们需要先了解一下这个结构体的各个属性，结构体的定义为：

```c
struct proc_struct{
    enum proc_state state;        // Process state
    int pid;                      // Process ID
    int runs;                     // the running times of Proces
    uintptr_t kstack;             // Process kernel stack
    volatile bool need_resched;   // bool value: need to be rescheduled to release CPU?
    struct proc_struct *parent;   // the parent process
    struct mm_struct *mm;         // Process's memory management field
    struct context context;       // Switch here to run process
    struct trapframe *tf;         // Trap frame for current interrupt
    uintptr_t pgdir;              // the base addr of Page Directroy Table(PDT)
    uint32_t flags;               // Process flag
    char name[PROC_NAME_LEN + 1]; // Process name
    list_entry_t list_link;       // Process link list
    list_entry_t hash_link;       // Process hash list
};
```

我们解释一下这个结构体中部分属性的作用：

- `enum proc_state state`：该属性定义了进程的状态，表示当前进程的生命周期阶段。在proc.h文件中，我们定义了`proc_state`，其中`PROC_UNINIT`表示进程刚被函数`alloc_proc`分配，还未初始化完毕；`PROC_SLEEPING`表示进程因为 sleep/wait/block被挂起(处于阻塞态，等待某事件完成)；`PROC_RUNNABLE`表示进程可运行，或者正在被运行；`PROC_ZOMBIE`说明进程已退出，但父进程还未回收。

  ```c
  enum proc_state{
      PROC_UNINIT = 0, // uninitialized
      PROC_SLEEPING,   // sleeping
      //从PROC_SLEEPING到PROC_RUNNABLE，需要经过wakeup函数
      PROC_RUNNABLE,   // runnable(maybe running)
      PROC_ZOMBIE,     // almost dead, and wait parent proc to reclaim his resource
  };
  ```

  该属性的作用在于：调度器会根据`state`属性判断是否将这个进程加入运行队列。

- `int pid`：该属性定义了进程的ID，是每个进程的唯一标识，通过函数`get_pid()`分配。

- `int runs`：该属性定义了进程的运行次数，每次被调度器选中运行runs就会加1。

- `uintptr_t kstack`：该属性定义了进程内核栈的地址。每个进程都会有一块独立的内核栈，当出现中断异常或者系统调用进入内核时会使用该属性。内核栈指针会通过函数`setup_kstack()`进行分配，该函数会为每个进程单独分配一块连续的KSTACKPAGE页大小的内存，作为这个进程的内核栈：

  ```c
  struct Page *page = alloc_pages(KSTACKPAGE);
  proc->kstack = (uintptr_t)page2kva(page);
  ```

- `volatile bool need_resched`：该属性定义了进程是否需要重新调度。

- `struct proc_struct *parent`：该属性定义了进程的父进程指针。

- `struct mm_struct *mm`：该属性定义了进程的内存管理结构，`mm_struct`结构体描述了进程的虚拟内存空间。但由于该属性在用户态时才会起到作用，本次实验没有用户态程序，所有恒为NULL。

- `uintptr_t pgdir`：该属性定义了进程页表的物理地址。对于用户态进程而言，每个进程都会有其独立的页表。但在本次实验中，由于只考虑了内核态进程，所有的页表指针都指向了共用内核页表

  ```c
  pgdir = boot_pgdir_pa
  ```

- `char name[PROC_NAME_LEN + 1]`：该属性定义了进程的进程名，在函数`set_proc_name`中进行设置。

- `list_entry_t list_link`：该属性用于将所有进程串连为一个链表。

随后，我们进行proc_struct的初始化，代码为：

```c
    if (proc != NULL)
    {
        proc->state = PROC_UNINIT;
        proc->pid = -1; //将pid先设置为1，实际上在do_fork函数中分配pid
        proc->runs = 0;  
        proc->kstack = 0;  //内核栈指针由于尚未分配设置为0
        proc->need_resched = 0;
        proc->parent = NULL;
        proc->mm = NULL;
        memset(&(proc->context), 0, sizeof(struct context));
        proc->tf = NULL;
        proc->pgdir = boot_pgdir_pa;
        proc->flags = 0;
        memset(proc->name, 0, sizeof(proc->name));
        proc->list_link.prev = proc->list_link.next = NULL;
        proc->hash_link.prev = proc->hash_link.next = NULL;       
    }
```

最后我们回答一下任务中涉及的两个问题：

(1)`struct context context`成员变量含义和在本实验中的作用是什么？

该成员变量定义了进程在进行上下文切换时CPU的寄存器内容，我们先看一下struct context的代码定义：

```c
struct context{
    uintptr_t ra;
    uintptr_t sp;
    uintptr_t s0;
    ....
    uintptr_t s11;
};
```

可以看到，该结构体中保存了返回地址`ra`，栈指针`sp`以及若干通用寄存器。当进行上下文切换时，我们会执行这条代码`switch_to(&(prev->context), &(proc->context));`，会将当前进程的寄存器保存于`prev->context`中，当下一次进程调度到该进程时，再从该位置恢复寄存器。

简单来说，这个结构体的作用就是为了在进程切换时保存当前进程的部分寄存器，便于后续进程切换回来时可以恢复进程状态并继续执行。在线程第一次运行时，通过设置 `context.ra = forkret` 让线程从`forkret()`开始执行。

(2)`struct trapframe *tf`成员变量含义和在本实验中的作用是什么？

trapfram结构体在之前的实验中已经解释过了，在此再简单的说一下，首先给出该结构体的定义：

```c
struct trapframe{
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};
```

该结构保存的信息有：

- 原始sp(发生陷入之前的栈顶指针)

- 通用寄存器数据(从寄存器x0到寄存器x31的全部寄存器数据，注意x2(sp)的数据额外处理)
- 状态寄存器sstatus(保存CPU当前的运行控制位，例如SIE决定是否允许中断，SPIE保存陷入前的SIE状态)
- 当前CPU的PC值(即触发trap的那条指令的地址，也是中断返回地址)
- stval(用于保存异常相关的“出错虚拟地址”，例如缺页异常时保存引起异常的虚拟地址)
- scause(说明此次trap的原因，是中断还是异常？具体是哪种类型？)

也就是说，trapframe结构体保存了CPU在进入内核态时的所有寄存器。当用户态的进程出现trap时，会从用户态陷入到内核态，此时我们需要保存进程的完整信息，假设此时的trap是由系统调用造成的，结束系统调用后进程需要回到用户态，并恢复到进程之前的状态，就会用到trapframe中保存的信息。

简单来说，trapframe用来保存与恢复进程的状态，确保进程能继续运行或从内核返回用户态。

## 练习2:为新创建的内核线程分配资源（需要编码）

## 练习3:编写proc_run 函数（需要编码）

我们设计的代码如下:

kern/process/proc.c
```cpp
void proc_run(struct proc_struct *proc)
{
    if (proc != current)
    {
        bool intr_flag;
        struct proc_struct *prev = current;
        local_intr_save(intr_flag);
        {
            current = proc;
            lsatp(proc->pgdir);
            switch_to(&(prev->context), &(proc->context));
        }
        local_intr_restore(intr_flag);
    }
}
```
具体而言，完成中断状态的保存，这部分通过调用`/kern/sync/sync.h`中定义好的宏`local_intr_save(x)`来将保存的中断状态存储在`intr_flag`变量中。并保存当前正在运行的进程结构体指针到`prev`变量中。

接下来进行上下文切换:首先将`current`指针切换更新为要运行的进程`proc`，这表示从现在开始，系统将认为`proc`是当前正在运行的进程。

然后进行地址空间的切换，通过`lsatp`指令实现：`lstap`把`stap`寄存器指向目标进程的页表根(`proc->pgdir`)，让接下来CPU所看到的虚拟地址映射，权限等都按新进程的地址空间生效。

最后调用`switch_to`函数，传入当前进程的上下文结构体指针和要运行的进程的上下文指针。

在完成进程切换后，通过调用`local_intr_restore`函数，并传入之前保留的中断状态`intr_flag`,来恢复系统的中断状态。

问题:在本实验的执行过程中，创建且运行了几个内核线程？

在本次实验中，一共创建并运行了两个内核线程:`idleproc`(pid 0)在`proc init`中由`alloc_proc`创建并设为当前进程，随后通过`kernel_thread(init_main,...)`创建第二个内核线程`initproc`(pid1).

## 扩展练习Challenge1：

##  扩展练习Challenge 2：

get_pte()函数（位于`kern/mm/pmm.c`）用于在页表中查找或创建页表项，从而实现对指定线性地址对应的物理页的访问和映射操作。这在操作系统中的分页机制下，是实现虚拟内存与物理内存之间映射关系非常重要的内容。我们需要回答如下两个问题：

1.get_pte()函数中有两段形式类似的代码， 结合sv32，sv39，sv48的异同，解释这两段代码为什么如此相像

函数中两段形式类似的代码如下：

```c
//若一级页表不存在则分配一级页表
if (!(*pdep1 & PTE_V)) {     
    page = alloc_page();
    memset(...);
    *pdep1 = pte_create(...);
}

//若二级页表不存在则分配二级页表
if (!(*pdep0 & PTE_V)) {     
    page = alloc_page();
    memset(...);
    *pdep0 = pte_create(...);
}
```

sv32，sv39，sv48的核心差异在于页表层数的不同，分别为2，3，4层。每一层页表的逻辑都是相同的，当我们通过某个虚拟地址访问页表时，都是先找到对应的页表项检查是否有对应的下一级页表或者物理页框，若不存在则返回缺页异常，为其分配对应的下一级页表或者物理页框。

而由于每一级页表的逻辑都是相同的，所以代码自然也是相似的，在sv39中构建了三级页表的机制，对于前两级页表而言，其创建下一级页表的逻辑自然非常相似，都是：先判断是否存在n级页表(例如通过`*pdep0 & PTE_V`判断该条页表项指向的二级页表项的valid位是否为1，若页表项无效则说明不存在)，若不存在则分配一页作为下一级页表并且设置其ref等于1(说明这一页用作页表)，获取新分配的页的物理起始地址并清空这一页的旧数据，然后将其写入页表项，并修改页表项的valid位。

2.目前get_pte()函数将页表项的查找和页表项的分配合并在一个函数里，你认为这种写法好吗？有没有必要把两个功能拆开？

我觉得这种写法挺好的，查找了发现页表项无效就分配，非常直接。