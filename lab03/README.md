# Lab-03

### 实验内容：

必做部分：

#### 练习1：完善中断处理(需要编程)


请编程完善trap.c中的中断处理函数trap，在对时钟中断进行处理的部分填写kern/trap/trap.c函数中处理时钟中断的部分，使操作系统每遇到100次时钟中断后，调用print_ticks子程序，向屏幕上打印一行文字”100 ticks”，在打印完10行后调用sbi.h中的shut_down()函数关机。

要求完成问题1提出的相关函数实现，提交改进后的源代码包（可以编译执行），并在实验报告中简要说明实现过程和定时器中断中断处理的流程。实现要求的部分代码后，运行整个系统，大约每1秒会输出一次”100 ticks”，输出10行。



选做部分：

#### 扩展练习Challenge 1：描述与理解中断流程

回答：描述ucore中处理中断异常的流程（从异常的产生开始），其中mov a0，sp的目的是什么？SAVE_ALL中寄寄存器保存在栈中的位置是什么确定的？对于任何中断，__alltraps 中都需要保存所有寄存器吗？请说明理由。



#### 扩展练习Challenge 2：理解上下文切换机制

回答：在trapentry.S中汇编代码 csrw sscratch, sp；csrrw s0, sscratch, x0实现了什么操作，目的是什么？save all里面保存了stval scause这些csr，而在restore all里面却不还原它们？那这样store的意义何在呢？



#### 扩展练习Challenge 3：完善异常中断(需要编程)

编程完善在触发一条非法指令异常 mret和，在 kern/trap/trap.c的异常处理函数中捕获，并对其进行处理，简单输出异常类型和异常指令触发地址，即“Illegal instruction caught at 0x(地址)”，“ebreak caught at 0x（地址）”与“Exception type:Illegal instruction"，“Exception type: breakpoint”。（




### 实验分工：

建议大家有时间的话，各个练习都做一遍，这样方便大家加深对OS的理解，也能够对咱们共同的报告进行纠错

考虑到：完成Challenge并回答了助教问题的小组可获得本次实验的加分，我们可以三个都做一下



- [ ] 冯：扩展练习Challenge：描述与理解中断流程
- [ ] 袁：扩展练习Challenge：理解上下文切换机制 + 文稿以你的风格撰写
- [x] 刘：扩展练习Challenge: 完善异常中断 + 练习一


### DDL：	

实验DDL：2025.11.03

各自part的DDL：2025.11.02




### 要求：

大家的实验报告统一写在本目录下的`report.md`文件中，**切记在开始写之前进行Pull**
