<center>
<h1>OS-lab03实验报告</h1>
</center>

## 练习1：完善中断处理(需要编程)
练习一要求我们完善中断处理函数trap,对时钟中断进行处理的部分填写时钟中断的处理部分，使操作系统每遇到100次时钟中断后，调用`print_ticks`子程序，向屏幕上打印一行文字"100 ticks"。在打印完10行后调用`sbi.h`中的`shut_down()`函数关机。

在实现之前，我们先对定时器中断(时钟中断)以及其中断处理流程进行简单的说明。时钟中断实际上就是每隔若干个时钟周期执行一次的程序。这个每隔若干个时钟周期的定时我们通过RISC-V提供的硬件支持实现:
    
1. OpenSBI提供了一个`sbi_set_timer()`接口，该接口可以传入一个时刻,让它在那个时刻触发一次时钟中断

2. `rdtime`伪指令,读取一个叫做`time`的CSR数值，表示CPU启动之后经过的真实时间。

我们要实现的是每隔一段时间就发生一次时钟中断，但是OpenSBI提供的接口一次只能设置一个时钟中断事件。因此我们采用的方式是最开始时设置一个时钟中断事件，之后每次触发时钟中断事件的时候，设置下一次的时钟中断。

在代码层面，我们是这样实现的，在进行`kern_init`的时候要进行`clock_init`,在这个时候我们设置第一个时钟中断事件，并记录时钟中断事件触发的次数`ticks`

之后每触发一次时钟中断事件，我们就给这个变量+1。每触发100次时钟中断事件，我们就调用子程序`print_ticks()`,打印出"100 ticks".另外为了实现一旦打印了10次"100 ticks"就调用`sbi_shut_down()`我们仿照`ticks`变量设置了`prints`变量，每次打印我们都给它+1，一旦它等于10就关机。

![实验完成示范](fig/test1.gif)



## Challenge1：描述与理解中断流程

### 1.ucore中处理中断异常的流程

在`RISC-V`架构下，在程序执行过程中发生中断或者异常（统称为`Trap`）时，`CPU`会完成的操作流程包括：

1. 首先，将当前指令地址保存到`sepc`寄存器中，同时将发生`Trap`的原因写入`scause`，将`sstatus.SIE`设置为`0`表示在该中断处理过程中禁止其他中断的发生，设置完这些后，会根据`stevc`的值跳转到我们设置的中断入口（我们采用`Direct`模式，也就是`stvec`直接指向唯一的中断处理程序入口点，所有类型的中断和异常都会跳转到这里），即`_alltraps`；
2. `_alltraps`，我们先执行宏`SAVE_ALL`将中断发生时所有通用寄存器以及部分控制寄存器的状态都保存至栈中（即将中断时的现场结构体`trapframe`保存至栈里），接着通过 `mov a0, sp` 将 `trapframe` 的地址传入寄存器 `a0`，作为参数调用 C 语言函数 `trap()`，在关键函数`trap()`中，我们会根据`scause`的值选择性调用`interrupt_handler()` 或 `exception_handler()` 进行分发和处理。
3. `Trap`处理完成后，程序会进入`_trapret`标签继续执行（相当于中断的出口），在这里我们会先执行 `RESTORE_ALL` 恢复之前保存的寄存器状态，最后通过 `sret` 指令从内核态返回到被中断的用户程序，继续执行后续指令。

### 2.`mov a0, sp` 的目的

​       在我们进入中断入口`_alltraps`之后，会先进行`SAVE_ALL`，此时`sp`指向`trapframe`结构体的起始地址，指令 `mov a0, sp` 的作用是将该地址传递给寄存器 `a0`，作为调用函数 `trap()` 的参数。

​       因为在我们的`trapframe`结构体中包含有`4`个控制寄存器，通过将结构体基地址传入`trap()`函数，我们才可以访问结构体中的`cause`寄存器值，判断`Trap`的类型，进行相应的处理。

### 3.`SAVE_ALL` 中寄存器保存在栈中的位置确定方式

​       我们首先通过`addi sp, sp, -36*REGBYTES`将`sp` 向下移动 `36 * REGBYTES`，在栈中腾出 36 个寄存器的空间（包含 32 个通用寄存器 + 4 个 CSR），然后依次执行：

```assembly
STORE x0, 0*REGBYTES(sp)
STORE x1, 1*REGBYTES(sp)
...
STORE x31, 31*REGBYTES(sp)
```

将所有的通用寄存器按编号顺序写入栈中固定偏移位置，最后将 `sstatus`、`sepc` 等控制寄存器按固定顺序存放在其后的 4 个位置。

​       故**寄存器在栈中的存储位置是由 `SAVE_ALL` 中保存的先后顺序与偏移量共同确定的，这一布局与 `trapframe` 结构体中字段的排列顺序保持一致**，保证了汇编与 C 层访问的一致性。

### 4.对于任何`Trap`，__alltraps 中都需要保存所有寄存器？

​       在`RISC-V`架构下，硬件只会自动保存 `sepc`、`sstatus` 等控制寄存器，故所有的通用寄存器`x0~x31`必须通过软件层面的汇编进行手动保存，并且我们在进入`trap()`后会执行普通的C函数逻辑，编译器可能会使用任何寄存器，如果我们没有事先保存，`trap()`返回后寄存器的值可能发生更改。

​       另外我们前面了解过`Trap`的处理流程了：**只要有`Trap`发生，它在设置好一些状态后都会进入唯一中断入口`_alltraps`**，所以我们无法在触发时提前区分是哪种类型的中断或异常，即使某些中断（如时钟中断）可能只需少数寄存器，也都会执行`SAVE_ALL`完整保存所有寄存器，这样也保证了 `trapframe` 的结构统一。


## Challenge2：理解上下文切换机制

回答：在trapentry.S中，汇编代码`csrw sscratch, sp`；`csrrw s0, sscratch, x0`实现了什么操作，目的是什么？为什么宏SAVE_ALL里面保存了`stval`和`scause`这些csr，而在宏RESTORE_ALL里面却不还原它们？那这样store的意义何在呢？

我们知道，在操作系统中，中断指的是打断CPU当前正在执行的程序，转而去执行另一程序(即中断处理程序)的过程。当中断的处理结束后，CPU将会恢复到之前中断的位置继续执行。这意味着我们需要将中断发生时CPU的状态保存下来，这样在结束中断处理后才能正确的继续先前程序的执行。

首先，我们先简要说明一下发生中断时CPU需要保存的内容：

- 原始sp(发生陷入之前的栈顶指针)

- 通用寄存器数据(从寄存器x0到寄存器x31的全部寄存器数据，注意x2(sp)的数据额外处理)
- 状态寄存器sstatus(保存CPU当前的运行控制位，例如SIE决定是否允许中断，SPIE保存陷入前的SIE状态)
- 当前CPU的PC值(即触发trap的那条指令的地址，也是中断返回地址)
- stval(用于保存异常相关的“出错虚拟地址”，例如缺页异常时保存引起异常的虚拟地址)
- scause(说明此次trap的原因，是中断还是异常？具体是哪种类型？)

这总共36个寄存器也被称作CPU的上下文context，我们**通过两个宏来实现上下文切换的机制**：

| 宏            | 功能                                   |
| ------------- | -------------------------------------- |
| `SAVE_ALL`    | 保存所有寄存器和关键 CSR（上下文保存） |
| `RESTORE_ALL` | 恢复寄存器和关键 CSR（上下文恢复）     |

首先是第一个宏`SAVE_ALL`，用于保存所有寄存器和关键CSR，具体代码如下：

```assembly
.macro SAVE_ALL
    
    #保存原始sp，将原始sp的数据存入sscratch，然后栈顶向下开辟36个寄存器的空间
    csrw sscratch, sp
    addi sp, sp, -36 * REGBYTES
    
    #保存通用寄存器数据
    #以x0寄存器为例：此时栈顶指针sp指向的一个REGBYTES的空间储存着x0寄存器中保存的数据
    STORE x0, 0*REGBYTES(sp)
    STORE x1, 1*REGBYTES(sp)
    #接下来依次保存x3-x31的寄存器数值，x2寄存器sp的原值已经保存到了sscratch中

    #保存重要的CSR
    csrrw s0, sscratch, x0 #s0 = 原来的sscratch = 陷入前的sp；sscratch = 0
    csrr s1, sstatus 
    csrr s2, sepc          
    #csrr s3, sbadaddr
    csrr s3, stval         
    csrr s4, scause

    #将s0-s4寄存器数据放入到trapframe的尾部
    STORE s0, 2*REGBYTES(sp)
    STORE s1, 32*REGBYTES(sp)
    STORE s2, 33*REGBYTES(sp)
    STORE s3, 34*REGBYTES(sp)
    STORE s4, 35*REGBYTES(sp)
.endm
```

其次是第二个宏`RESTORE_ALL`，用于恢复寄存器和关键CSR，具体代码如下：

```assembly
.macro RESTORE_ALL
    #获取sstatus：
    #在地址32*REGBYTES+sp处保存着sstatus的数值，将其load至s1中，然后放回专用寄存器
    LOAD s1, 32*REGBYTES(sp)
    csrw sstatus, s1
    
    #获取sepc：获取中断发生时的指令地址
    LOAD s2, 33*REGBYTES(sp)
    csrw sepc, s2

    #恢复通用寄存器的数据
    #根据SAVE_ALL获取通用寄存器先前数据的保存地址，将其依次load回到通用寄存器中
    LOAD x1, 1*REGBYTES(sp)
    LOAD x3, 3*REGBYTES(sp)
    #load x4-x31

    #恢复中断发生前的栈顶地址
    LOAD x2, 2*REGBYTES(sp)
.endm
```

当发生中断或异常时，硬件会自动把当前PC保存至`sepc`，把当前状态保存至`sstatus`，然后跳转到 `stvec` 寄存器中保存的入口地址(此时认为所有的中断/异常处理程序相同)。在操作系统启动阶段，内核一般会设置`csrw stvec, __alltraps`，于是一旦trap发生，CPU将跳转到汇编标签`__alltraps`，执行下面的汇编代码中`__alltraps`部分，同样，在中断处理结束后，CPU跳转到汇编标签`__trapret`，调用宏`RESTORE_ALL`，恢复trap前的寄存器状态，然后执行`sret`指令返回到中断之前的CPU状态，继续执行。

```assembly
.globl __alltraps
.align(2)
__alltraps:
    SAVE_ALL   #调用宏SAVE_ALL保存CPU所有现场
    
    move  a0, sp
    jal trap
    # sp should be the same as before "jal trap"
    
.globl __trapret
__trapret:
    RESTORE_ALL #将trap发生前的寄存器数值重新load回寄存器中
    # return from supervisor call
    sret
```

其中`sret`的作用在于：从 `sstatus.SPP`中恢复陷入前的特权级；从`sstatus.SPIE`中恢复中断使能位；从 `sepc`中取回陷入前的指令地址；通过`sepc`恢复PC值，从陷入前的那条指令继续执行。

分析完代码后，我们来回答本任务的几个问题：

1.汇编代码`csrw sscratch, sp`；`csrrw s0, sscratch, x0`实现了什么操作，目的是什么？

其中，汇编代码`csrw sscratch, sp`实现的操作是将当前栈顶指针sp的值保存到控制寄存器sscratch中，因为在一旦中断发生，CPU将会自动切换到内核栈，而内核需要知道在发生trap前用户态的栈顶指针，后续才可以恢复上下文；

而`csrrw s0, sscratch, x0`实际上实现了两个操作：

- 一是将当前的控制寄存器 `sscratch` 中的值读到寄存器`s0`中，相当于寄存器`s0`保存了上下文切换前的栈顶指针；
- 二是将寄存器`sscratch`写为0，之所以要对控制寄存器进行清零操作，是因为如果再次发生嵌套trap(例如内核代码执行时发生系统调用)，那么trap的入口代码就会对寄存器`sscratch`的值进行识别，若此时该寄存器中值为零，那么就可以确定当前trap是来自内核态而非用户态，这样就会依照S模式触发中断的路由进行。

2.为什么宏SAVE_ALL里面保存了`stval`和`scause`这些csr，而在宏RESTORE_ALL里面却不还原它们？那这样store的意义何在呢？

这些CSR反映的是中断发生前的上下文信息，保存它们的目的在于可以让内核的trap函数访问这些值然后进行处理：例如scause说明了本次trap的原因；stval说明了出错的虚拟地址；sepc说明了发生trap时CPU的PC值，也同样是中断处理结束后CPU应该返回继续执行的位置。这些信息对于trap函数进行中断处理是必要的，我们将这些信息保存于trapframe中，trap函数就知道要在哪个位置找到这些信息，然后进行对应的处理，这就是为什么一定要store这些CSR。

而不用还原的原因在于：`stval`、`scause` 属于“只读诊断信息”，不需要也不应该进行修改，在下一次trap发生时，硬件会自动重新填入新的值；`sepc`在`RESTORE_ALL`之后的sret指令执行时还需要使用，此时对其进行还原会造成错误，在sret指令执行时，CPU会跳转到`sepc`指定的地址处继续执行，当下一次trap发生时，硬件也会自动为其填入新的值；`sstatus`同样会在sret指令执行时自动进行恢复，例如SIE恢复为SPIE的数值，并不需要在`RESTORE_ALL`中手动还原。








## Challenge3：完善异常中断(需要编程)

Challenge3要求我们完善异常中断，具体而言是两种异常的中断处理:"ILLEGAL INSTRUCTION"，即异常指令，如在非M态下使用指令`mret`."EBREAK",即断点。

对于处理而言就是输出异常类型以及异常触发的地址。

异常触发的地址由`trapframe`的epc寄存器记录，因此当异常触发的时候我们通过读取该寄存器里的值就可以获取被中断指令的虚拟地址。

而在异常处理后，我们需要更新epc，这里由于没有提供其他具体的处理函数，本来应该跳转到对应的处理函数的地址，这里直接跳转到下一条指令即可。

最后为了测试我们异常处理的正确性，我们在`clock.c`中用于获取的当前时间的`get_cycles`函数中插入了内联汇编代码`__asm__ __volatile__("mret");`从而触发这个异常，结果演示如下:

![实验完成示范](fig/challenge3.gif)