<center>
    <h1>OS-lab01实验报告</h1>
</center>

#### 一. 实验要求

1.阅读kern/init/entry.S内容代码，结合操作系统内核启动流程，回答下列问题：

- 说明指令 `la sp, bootstacktop` 完成了什么操作，目的是什么?
- 说明指令 `tail kern_init` 完成了什么操作，目的是什么？

2.使用 GDB 跟踪 QEMU 模拟的 RISC-V 从加电开始，直到执行内核第一条指令(跳转到 0x80200000)的整个过程。通过调试，请思考并回答：RISC-V 硬件加电后最初执行的几条指令位于什么地址？它们主要完成了哪些功能？

#### 二. 实验内容(Part1)

最小可执行内核的完整启动流程如下：

```txt
加电复位 → CPU从0x1000进入MROM → 跳转到0x80000000(OpenSBI) → OpenSBI初始化并加载内核到0x80200000 → 跳转到entry.S → 调用kern_init() → 输出信息 → 结束
```

我们可以看到，一个最小化的RISC-V操作系统内核从加电到执行其第一条指令，其启动流程可以划分为三个核心阶段：

1. 首先，CPU 的程序计数器PC被硬件强制设置为一个预定义的复位地址，在QEMU模拟的这款riscv处理器下，其复位地址为`0x1000`，该地址指向一段存储在只读存储器中的初始引导代码（MROM）。这段代码将完成最基础的硬件配置，核心任务是将控制权移交给bootloader，通过bootloader将操作系统内核加载到内存中，实验中我们使用QEMU自带的bootloader------OpenSBI固件。它会被加载到物理内存的 `0x80000000` 地址处，CPU 随即跳转至该地址继续执行
2. 在Qemu启动时，OpenSBI作为bootloader会将操作系统的二进制可执行文件从硬盘加载到内存中，然后OpenSBI会把CPU的PC跳转到内存里的一个位置，开始执行内存中那个位置的指令，也就是操作系统的指令。具体来说，OpenSBI会将内核镜像 os.bin 加载到 Qemu 物理内存以地址`0x80200000`开头的区域上，并将CPU的控制权交给操作系统
3. 当OpenSBI完成其所有任务后，将会执行一条跳转指令，把处理器的执行流交给位于 `0x80200000` 的内核入口代码。首先执行的就是该部分任务所提到的由汇编语言编写的入口程序 `entry.S`。该程序的主要功能有三个：建立内核栈；设置栈顶指针(sp)；利用tail指令跳转到init.c

执行完这些后，系统的控制权从引导固件完全移交给了操作系统内核。`kern_init` 函数将开始执行一系列复杂的初始化工作，例如输出启动信息、初始化内存管理和进程管理等，最终将系统带入一个完整可用的状态。

根据上述操作系统内核的启动流程，我们知道entry.S程序的作用就是：建立内核栈；设置栈顶指针(sp)；利用tail指令跳转到init.c。那么我们可以回答这两个问题：

**(1)说明指令 `la sp, bootstacktop` 完成了什么操作，目的是什么?**

la指令的作用是将一个符号的地址加载到寄存器中，本条指令的作用就是把 `bootstacktop` 的地址装进 `sp`，也就是设置内核栈顶。在数据段(.data段)中，我们定义了一块内核专用的内存区域，作为内核栈，该指令就是设置这个栈的栈顶位置

**(2)说明指令 `tail kern_init` 完成了什么操作，目的是什么？**

`tail`是RISC-V的一种伪指令，它等价于`jalr zero, kern_init`，即无返回地跳转（类似 goto）到 kern_init 函数执行。该指令执行后，CPU程序计数器PC将跳转到 C 函数 `kern_init` 的地址。其目的在于将执行流程从汇编代码移交到 C 代码，启动内核初始化。

#### 三. 实验内容(Part2)

首先我们先测试`make qemu`语句,运行得到如下结果

<img src="fig/make_qemu.png" width = "300">

说明我们环境配置无误，可以开始我们的调试工作

依据实验指导书的内容，我们开启两个终端，一个执行make debug,另一个执行make gdb.

<img src="fig/make_gdb.png" width="300">

随后我们开始跟踪QEMU模拟的RISC-V从加电开始，直到执行内核第一条指令的整个过程。在上图中我们可以看到'0x0000000000001000 in ?? ()'说明程序此时暂停在`0x1000`处。这是CPU的复位向量地址，处理器将从此处开始执行复位代码。

接下来我们使用指令`x/10i $pc`显示即将执行的10条指令，得到

```
(gdb) x/10i $pc
=> 0x1000:      auipc   t0,0x0     #t0 = pc + 0=0x1000
   0x1004:      addi    a1,t0,32   #a1 = t0 + 32 = 0x1020
   0x1008:      csrr    a0,mhartid #a0 = mhartid = 0
   0x100c:      ld      t0,24(t0)  #t0 = [t0 + 24]
   0x1010:      jr      t0         #跳转到t0的位置
   0x1014:      unimp
   0x1016:      unimp
   0x1018:      unimp
   0x101a:      .insn   2, 0x8000
   0x101c:      unimp
```

我们使用指令`x/1xw 0x1018`,查看地址`0x1018`中的数据,得到`0x1018: 0x80000000`,可以知道在指令`0x1010`后，CPU跳转到地址`0x80000000`.而我们知道，QEMU在开始执行任何指令之前，首先要将bootloader的OpenSBI.bin加载到物理内存地址0x80000000开头的区域上，因此在跳转指令执行后，CPU将开始执行OpenSBI.bin程序.

跳转执行完毕后显示`0x0000000080000000 in ?? ()`,此时我们再执行指令`x/10i $pc`,得到

```
0x80000000:  csrr    a6,mhartid
   0x80000004:  bgtz    a6,0x80000108
   0x80000008:  auipc   t0,0x0
   0x8000000c:  addi    t0,t0,1032
   0x80000010:  auipc   t1,0x0
   0x80000014:  addi    t1,t1,-16
   0x80000018:  sd      t1,0(t0)
   0x8000001c:  auipc   t0,0x0
   0x80000020:  addi    t0,t0,1020
   0x80000024:  ld      t0,0(t0)
```

该处的指令主要是为了加载操作系统内核并启动操作系统的执行.

接下来我们在`0x80200000`处通过指令`b *0x80200000`设置断点.然后输入c，执行到断点处。

<img src="fig/make_0x8020.png" width="300">

可以看到此时内核已经启动

输入指令`x/10i $pc`，得到

```
 0x80200000 <kern_entry>:     auipc   sp,0x3
   0x80200004 <kern_entry+4>:   mv      sp,sp
   0x80200008 <kern_entry+8>:
    j   0x8020000a <kern_init>
   0x8020000a <kern_init>:      auipc   a0,0x3
   0x8020000e <kern_init+4>:    addi    a0,a0,-2
   0x80200012 <kern_init+8>:    auipc   a2,0x3
   0x80200016 <kern_init+12>:   addi    a2,a2,-10
   0x8020001a <kern_init+16>:   addi    sp,sp,-16
   0x8020001c <kern_init+18>:   li      a1,0
   0x8020001e <kern_init+20>:   sub     a2,a2,a0
```

发现与内核中的`kernel_entry`代码块的内容一致，说明`0x80200000`的确是内核的起始位置。

<div style="background-color:#f9f9f9; padding:8px; border-radius:6px;">
<b>问题一:</b> RISC-V 硬件加电后最初执行的几条指令位于什么地址？
</div>


RISC-V硬件加电后，将要执行的指令在复位地址`0x1000`到跳转指令`0x1010`处。

<div style="background-color:#f9f9f9; padding:8px; border-radius:6px;">
<b>问题二:</b> 它们主要完成了哪些功能？
</div>


具体的指令如下:

```
   0x1000:      auipc   t0,0x0     #t0 = pc + 0=0x1000
   0x1004:      addi    a1,t0,32   #a1 = t0 + 32 = 0x1020
   0x1008:      csrr    a0,mhartid #a0 = mhartid = 0
   0x100c:      ld      t0,24(t0)  #t0 = [t0 + 24]
   0x1010:      jr      t0         #跳转到t0的位置
```

具体而言

- `auipc   t0,0x0`将`t0`寄存器的值设置为0x1000
- `addi    a1,t0,32` `a1`寄存器的值设置为0x1020
- `csrr    a0,mhartid` 将`a0`寄存器设置为当前CPU核心ID
- `ld      t0,24(t0)` 从`t0+24`位置加载内存地址
- `jr      t0`跳转到`t0`地址，将控制权转移给下一个启动阶段(OpenSBI.bin)

