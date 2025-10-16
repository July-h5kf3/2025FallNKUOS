# Lab-02

### 实验内容：

必做部分：

#### 练习1：理解first-fit 连续物理内存分配算法

阅读实验手册的教程并结合`kern/mm/default_pmm.c`中的相关代码，认真分析default_init_memmap，default_init，default_alloc_pages， default_free_pages等相关函数，并描述程序在进行物理内存分配的过程以及各个函数的作用。

回答：你的first fit算法是否有进一步的改进空间？

#### 练习2：实现 Best-Fit 连续物理内存分配算法（需要编程）

参考kern/mm/default_pmm.c对First Fit算法的实现，**编程实现Best Fit页面分配算法**，算法的时空复杂度不做要求，能通过测试即可。 

回答：你的 Best-Fit 算法是否有进一步的改进空间？



选做部分：

#### 扩展练习Challenge：buddy system（伙伴系统）分配算法（需要编程）

 在ucore中**实现buddy system分配算法**，要求有比较充分的测试用例说明实现的正确性，需要有设计文档。

参考：[伙伴分配器的一个极简实现 | 酷 壳 - CoolShell](https://coolshell.cn/articles/10427.html)



#### 扩展练习Challenge：任意大小的内存单元slub分配算法（需要编程）

在ucore中**实现slub分配算法**。要求有比较充分的测试用例说明实现的正确性，需要有设计文档。

参考：[linux/mm/slub.c at master · torvalds/linux](https://github.com/torvalds/linux/blob/master/mm/slub.c)



#### 扩展练习Challenge：硬件的可用物理内存范围的获取方法（思考题）

如果 OS 无法提前知道当前硬件的可用物理内存范围，请问你有何办法让 OS 获取可用物理内存范围？




### 实验分工：

建议大家有时间的话，各个练习都做一遍，这样方便大家加深对OS的理解，也能够对咱们共同的报告进行纠错

考虑到：完成Challenge并回答了助教问题的小组可获得本次实验的加分，我们可以三个都做一下



- [ ] 冯：扩展练习Challenge：任意大小的内存单元slub分配算法，最后MarkDown文件以你的风格进行书写
- [ ] 袁：扩展练习Challenge：buddy system（伙伴系统）分配算法 + (思考题)
- [x] 刘：练习1 + 练习2


### DDL：	

实验DDL：2025.10.20

各自part的DDL：2025.10.19




### 要求：

大家的实验报告统一写在本目录下的`report.md`文件中，**切记在开始写之前进行Pull**
