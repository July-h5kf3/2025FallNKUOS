<center>
<h1>OS-lab05实验报告</h1>
</center>

## 基础练习:
### 练习一
下面从ucore启动到执行第一个应用程序的过程展开分析。

如lab04中操作一样，当我们启动ucore后，会自动创建`idle`内核进程和`init_main`内核进程.而`init_main`线程会在进程内创建`user_main`内核进程。而后者会使用宏`KERNEL_EXECVE`把内核镜像的用户程序作为参数，通过软中断触发`SYS_exec`系统调用，实现内核主动执行用户程序。

这里定义了多个宏:
```cpp
#define __KERNEL_EXECVE(name, binary, size) ({                          \
            cprintf("kernel_execve: pid = %d, name = \"%s\".\n",        \
                    current->pid, name);                                \
            kernel_execve(name, binary, (size_t)(size));                \
        })

#define KERNEL_EXECVE(x) ({                                             \
            extern unsigned char _binary_obj___user_##x##_out_start[],  \
                _binary_obj___user_##x##_out_size[];                    \
            __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,     \
                            _binary_obj___user_##x##_out_size);         \
        })

#define __KERNEL_EXECVE2(x, xstart, xsize) ({                           \
            extern unsigned char xstart[], xsize[];                     \
            __KERNEL_EXECVE(#x, xstart, (size_t)xsize);                 \
        })

#define KERNEL_EXECVE2(x, xstart, xsize)        __KERNEL_EXECVE2(x, xstart, xsize)
```

其中`_binary_obj___user_##x##_out_start`和`_binary_obj___user_##x##_out_size`都是编译的时候自动生成的符号。这里`##x##`是按照 C 语言宏的语法，会直接把 x 的变量名代替进去。

所以这里(`user_main`进程)在做的事情就是调用并执行函数:`kernel_execve("exit", _binary_obj___user_exit_out_start,_binary_obj___user_exit_out_size)`.这里实际上就是加载了存储在这个位置的程序`exit`并在`user_main`进程中开始执行。这时`user_main`就从内核进程变成了用户进程。

而通过翻阅`exit.c`我们发现在该程序中对`fork()`,`wait()`等函数进行了测试。这些函数都是对系统调用的封装。ucore通过如下方法实现系统调用(即在用户态的程序中获取内核态服务的方法)。

简单来说，函数调用就是在用户态提供了一个调用的接口，真正的处理都在内核态进行。

具体而言，我们首先在头文件中定义一些系统调用的编号，如`SYS_exit`是1等。然后系统调用实际上是在用户态通过内联汇编进行`ecall`环境调用.这将产生一个`trap`，进入到`S`态下进行异常处理。

然后在异常处理时会首先通过中断帧里scause寄存器的数值，判断出当前是来自USER_ECALL的异常,然后将sepc设置为`ecall`的下一条指令，最后调用`syscall`进行系统调用处理。

在`syscall`中`a0`寄存器存储了函数调用的编号，其他寄存器则存储了函数的参数。接着`syscall`就会把这些信息进行转发从而执行具体的函数。

回到整个流程。我们通过`kernel_execve`来启动第一个用户进程，进入用户态。首先来看`do_execve`的实现。

`load_icode`首先会创建一个新的mm和页目录，然后遍历ELF_program header,根据`p_flags`计算`vm_flags/perm`,调用`mm_map`建立VMA；之后利用`pgdir_alloc_page`为 TEXT/DATA/BSS 分页拷贝或清零；接下来用 mm_map + pgdir_alloc_page 搭建 4 页用户栈；之后把 current->mm/pgdir 切换到新地址空间；最后清零旧的`trapframe`设置sp=USTACKTOP、epc=elf->e_entry、status 的 SPP/SPIE 位，使 sret 回到用户态入口执行。

这里重点讲一下我们实现的第6步。这一步需要我们建立相应的用户内存空间来放置应用程序的代码段、数据段等，且要设置好proc_struct结构中的成员变量trapframe中的内容，确保在执行此进程后，能够从应用程序设定的起始执行地址开始执行。需设置正确的trapframe内容。

具体而言，我们首先留存旧的`sstatus`然后再将整个trapframe清空。之后将通用寄存器中的栈指针设置到用户栈顶。这样首次`sret`回到用户态时，用户代码一开始就有干净的栈空间是用。之后将`sepc`设置为ELF入口地址，保证启动的就是用户程序。对于`sstatus`则先清理掉当前特权级位`SPP`和全局中断使能位`SIE`保证回到用户态。然后`|= SSTATUS_SPIE`意思是在 sret 之后自动打开用户态的中断.经过上述配置，trapframe 保存了“下一次从内核返回时应该是什么样”的完整上下文：sret 会切换到 U 态、跳转到 elf->e_entry，并以 USTACKTOP 作为栈指针，具备正常的中断语义。这确保新用户进程在第一次调度时就能像正常 ELF 程序那样运行。

那么我们该如何实现`kernel_execve`函数呢？

我们不能直接调用`do_execve`，这是因为`do_execve`,`load_icode`中只是构建了用户程序运行的上下文，但是并没有完成切换。上下文切换实际上要借助中断处理的返回来完成。我们采用`ebreak`产生断点中断进行处理，通过设置`a7`寄存器的值为10说明这不是一个普通的断点中断，而是要转发到`syscall`,这样我们实现了在内核态复用系统调用的接口。

接下来我们梳理一下用户态进程被ucore选择占用CPU执行（RUNNING态）到具体执行应用程序第一条指令的整个经过。

首先调度器在`schedule()`选中该进程后，`proc_run`保存当前进程上下文、切换`current`指针，并用`switch_to`切换寄存器，使新的`current`获得CPU，状态标记位running。进入新进程时，内核仍然在`S`态运行；`load_icode` 先为该进程构建 `mm`、页表、用户栈以及 `ELF` 各段，然后把 trapframe 设置为：`sp=USTACKTOP`、`epc=elf->e_entry`、`status` 切到用户模式并允许返回后开中断。随后`proc_run`末尾或系统调用返回路径调用`sret`：硬件依据 trapframe 恢复寄存器、将 `satp` 指向的页表作为地址空间，并因 `SPP` 被清零而切换到 U 态。`sret`完成后，CPU的PC等于`tf->epc`，也就是应用程序的入口。栈指针指向用户栈顶，用户代码便开始执行第一条指令。
### 练习二
`copy_range`的功能实现了创建子进程函数`do_fork`在执行中拷贝当前进程（即父进程）的用户内存地址空间中的合法内容到新进程中（子进程），完成内存资源的复制的功能。

具体而言，我们是这样实现这个过程的。首先逐页遍历，通过`get_pte`找到父进程PTE，若为空则跳到下一个PTSIZE。若命中，则用`get_pte(to,start,1)`为子进程准备对应的PTE。之后取出父页表中的权限`perm=*ptep&PTE_USER`并解析出物理页`pte2page(*ptep)`。
接着会`alloc_page`分配新的页面然后用`memcpy`拷贝4KB，最后`page_insert`建立子进程映；失败路径记得回收新页，保障 fork 成功率。

我们同样实现了COW机制。如果采用COW机制，那么我们不再将父进程的内容copy到子进程中，而是两个进程共享同一物理页，并且把父子 PTE 的写权限都去掉，第一次写入时触发缺页异常再“真正复制”。

具体来说，我们是这样设计的。
在 `fork`地址空间复制时，不立即为子进程复制所有用户页，而是让父子进程先共享同一物理页。当任意一方第一次对共享页执行写操作时，触发缺页异常，再进行按需复制，从而减少内存占用和复制开销。