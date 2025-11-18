<center>
<h1>OS-lab04实验报告</h1>
</center>

## 练习1：分配并初始化一个进程控制块（需要编码）

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

## Challenge1：说明语句local_intr_save(intr_flag);....local_intr_restore(intr_flag);是如何实现开关中断的？

##  扩展练习Challenge 2：深入理解不同分页模式的工作原理（思考题）