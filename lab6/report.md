### 练习1: 理解调度器框架的实现

请仔细阅读和分析调度器框架的相关代码，特别是以下两个关键部分的实现：

在完成练习0后，请仔细阅读并分析以下调度器框架的实现：

- 调度类结构体 sched_class 的分析：请详细解释 sched_class 结构体中每个函数指针的作用和调用时机，分析为什么需要将这些函数定义为函数指针，而不是直接实现函数。
- 运行队列结构体 run_queue 的分析：比较lab5和lab6中 run_queue 结构体的差异，解释为什么lab6的 run_queue 需要支持两种数据结构（链表和斜堆）。
- 调度器框架函数分析：分析 sched_init()、wakeup_proc() 和 schedule() 函数在lab6中的实现变化，理解这些函数如何与具体的调度算法解耦。

对于调度器框架的使用流程，请在实验报告中完成以下分析：

- 调度类的初始化流程：描述从内核启动到调度器初始化完成的完整流程，分析 default_sched_class 如何与调度器框架关联。
- 进程调度流程：绘制一个完整的进程调度流程图，包括：时钟中断触发、proc_tick 被调用、schedule() 函数执行、调度类各个函数的调用顺序。并解释 need_resched 标志位在调度过程中的作用
- 调度算法的切换机制：分析如果要添加一个新的调度算法（如stride），需要修改哪些代码？并解释为什么当前的设计使得切换调度算法变得容易。

#### 调度类结构体 sched_class 的分析

`sched_class` 将具体算法的关键动作抽象为函数指针，框架只关心这些动作的语义：

- `init(struct run_queue *rq)`：初始化运行队列，设置队列结构及计数器。调用时机：`sched_init()` 中在设置 `rq->max_time_slice` 后调用。
- `enqueue(struct run_queue *rq, struct proc_struct *proc)`：将可运行进程加入运行队列，并初始化时间片等字段。调用时机：`wakeup_proc()` 里唤醒非当前进程时入队；`schedule()` 里当前进程仍可运行时再入队。
- `dequeue(struct run_queue *rq, struct proc_struct *proc)`：将进程从运行队列移除。调用时机：`schedule()` 选中下一个进程后出队。
- `pick_next(struct run_queue *rq)`：选择下一个要运行的进程（算法核心）。调用时机：`schedule()` 中。
- `proc_tick(struct run_queue *rq, struct proc_struct *proc)`：处理时钟中断中的时间片或调度相关逻辑。调用时机：时钟中断中通过 `sched_class_proc_tick()` 调用。

为什么用函数指针而不是直接实现：框架需要在不改动核心调度逻辑（`sched.c`、`trap.c`）的前提下切换算法；函数指针让不同调度算法以“模块”的形式插拔，核心仅依赖统一接口，从而实现解耦、复用和可扩展。

#### 运行队列结构体 run_queue 的分析

lab5 中没有显式 `run_queue`，调度器直接遍历全局 `proc_list` 选择 `PROC_RUNNABLE` 进程（`lab05/kern/schedule/sched.c`），逻辑固定且无法复用其他数据结构。

lab6 引入 `run_queue`（`lab6/kern/schedule/sched.h`），包含：

- `run_list`：链表队列，适合 RR 等顺序队列调度；
- `lab6_run_pool`：斜堆（skew heap）优先队列，适合 stride 等按“最小步长”选取；
- `proc_num`、`max_time_slice`：通用元数据。

因此 lab6 需要支持“链表 + 斜堆”两种结构：同一套框架要同时兼容 RR（链表）和 stride（优先队列）等不同策略，避免在核心调度代码中写死数据结构。

#### 调度器框架函数分析（sched_init / wakeup_proc / schedule）

- `sched_init()`（`lab6/kern/schedule/sched.c`）：
  - 选择调度类：`sched_class = &default_sched_class`；
  - 初始化运行队列：`rq->max_time_slice = MAX_TIME_SLICE; sched_class->init(rq);`；
  - 打印调度器名称。  
  与 lab5 的固定逻辑相比，lab6 通过 `sched_class` 将初始化细节交给具体算法，框架只做“选择+调用”。

- `wakeup_proc()`：
  - lab5 仅改变状态为 `RUNNABLE`；
  - lab6 在唤醒非当前进程时调用 `sched_class_enqueue(proc)` 入队。  
  这样不同算法由各自 `enqueue` 决定入队结构与策略。

- `schedule()`：
  - lab5 遍历 `proc_list` 找下一个 `RUNNABLE`；
  - lab6 使用 `enqueue/pick_next/dequeue` 组合完成入队、选取、出队。  
  这一层彻底抽象了“怎么挑进程”的细节，实现与具体算法解耦。

#### 调度类的初始化流程

1. `kern_init()` 中依次初始化内核子系统（`lab6/kern/init/init.c`）。
2. 在 `vmm_init()` 之后调用 `sched_init()`。
3. `sched_init()` 设置 `sched_class = &default_sched_class`，配置 `rq->max_time_slice`，并调用 `sched_class->init(rq)`。
4. `default_sched_class` 在 `lab6/kern/schedule/default_sched.c` 定义，将 RR 相关函数挂接到框架。  
这样 `default_sched_class` 成为调度框架的“实现体”，框架通过函数指针调用具体算法。

#### 进程调度流程（含 need_resched 作用）

```text
时钟中断
  -> trap_dispatch()
     -> 时钟中断处理:
        clock_set_next_event()
        ticks++ ...
        sched_class_proc_tick(current)
           -> sched_class->proc_tick(rq, current)
           -> (idleproc 时直接 need_resched=1)
  -> trap() 返回用户态前检查
     if (current->need_resched)
        schedule()
           -> current->need_resched = 0
           -> if current RUNNABLE: sched_class->enqueue(rq, current)
           -> next = sched_class->pick_next(rq)
           -> if next != NULL: sched_class->dequeue(rq, next)
           -> if next == NULL: next = idleproc
           -> proc_run(next)
```

`need_resched` 是“延迟调度”的标志：时钟中断或 `do_yield()` 将其置位，返回用户态前统一触发 `schedule()`，避免在中断或内核态随意切换导致状态不一致。

#### 调度算法的切换机制

要新增 stride 调度（或其他算法），通常需要：

1. 新增一个实现文件，定义 `struct sched_class`，实现 `init/enqueue/dequeue/pick_next/proc_tick`（如 `default_sched_stride.c`）。
2. 在 `sched_init()` 中将 `sched_class` 指向新算法（或通过宏/配置选择）。
3. 确保构建系统编译链接新的调度器文件。

由于核心调度逻辑仅依赖 `sched_class` 接口，切换算法无需修改 `schedule()`、`wakeup_proc()`、`trap.c` 等代码，耦合度低，扩展成本小。




### 练习2:实现 Round Robin 调度算法（需要编码）

完成练习0后，建议大家比较一下（可用kdiff3等文件比较软件）个人完成的lab5和练习0完成后的刚修改的lab6之间的区别，分析了解lab6采用RR调度算法后的执行过程。理解调度器框架的工作原理后，请在此框架下实现时间片轮转（Round Robin）调度算法。

注意有“LAB6”的注释，你需要完成 kern/schedule/default_sched.c 文件中的 RR_init、RR_enqueue、RR_dequeue、RR_pick_next 和 RR_proc_tick 函数的实现，使系统能够正确地进行进程调度。代码中所有需要完成的地方都有“LAB6”和“YOUR CODE”的注释，请在提交时特别注意保持注释，将“YOUR CODE”替换为自己的学号，并且将所有标有对应注释的部分填上正确的代码。

提示，请在实现时注意以下细节：

- 链表操作：list_add_before、list_add_after等。
- 宏的使用：le2proc(le, member) 宏等。
- 边界条件处理：空队列的处理、进程时间片耗尽后的处理、空闲进程的处理等。

请在实验报告中完成：

- 比较一个在lab5和lab6都有, 但是实现不同的函数, 说说为什么要做这个改动, 不做这个改动会出什么问题
    - 提示: 如kern/schedule/sched.c里的函数。你也可以找个其他地方做了改动的函数。
- 描述你实现每个函数的具体思路和方法，解释为什么选择特定的链表操作方法。对每个实现函数的关键代码进行解释说明，并解释如何处理边界情况。
- 展示 make grade 的输出结果，并描述在 QEMU 中观察到的调度现象。
- 分析 Round Robin 调度算法的优缺点，讨论如何调整时间片大小来优化系统性能，并解释为什么需要在 RR_proc_tick 中设置 need_resched 标志。
拓展思考：如果要实现优先级 RR 调度，你的代码需要如何修改？当前的实现是否支持多核调度？如果不支持，需要如何改进？

#### 我的实现与对比

- sched.c 差异：lab6 的 `schedule()` 通过调度类接口抽象（enqueue/pick/dequeue/proc_tick），而 lab5 写死了链表操作。否则无法在同一核心框架下切换 RR/stride 等策略，且易造成重复代码。
- 函数实现要点（lab6/kern/schedule/default_sched.c）：
  - `RR_init`：初始化 `run_list`（空循环链表）、`proc_num=0`，并清空 `lab6_run_pool`。
  - `RR_enqueue`：若 `run_link` 已在队列会断言；时间片未设置或溢出则重置为 `rq->max_time_slice`，插入队尾（`list_add_before(run_list, run_link)`），更新 `rq` 指针与计数。
  - `RR_dequeue`：断言节点有效，`list_del_init` 解绑，并递减 `proc_num`。
  - `RR_pick_next`：若队列空返回 NULL，否则取队头 `list_next(&run_list)` 并用 `le2proc` 还原 proc。
  - `RR_proc_tick`：时间片递减，耗尽时置 `need_resched=1` 触发延迟调度。

#### 设计与边界处理

- 链表选择：RR 需 FIFO 语义，直接用 `run_list` 双向循环链表；入队尾（before head）保证轮转顺序，出队/取队头用 `list_next`。
- 时间片：入队时统一规范为 `max_time_slice`，避免无穷时间片或旧值过大；tick 中只针对非 idle 的进程递减（idle 在 sched_class_proc_tick 中直接置 resched），耗尽后标记换出，不在中断内切换保证原子性。
- 空队列兜底：`pick_next` 返回 NULL 时调度器回退 idleproc；`dequeue` 断言确保不会删除未入队节点。

#### 运行结果与现象

- `make grade`：通过，得分 50/50。关键输出包含 `all user-mode processes have quit.` 与 `init check memory pass.`。
- QEMU 观察：priority 测例中 5 个子进程轮转递增计数，最终 `sched result` 近似均衡（1 1 1 1 1），符合 RR 时间片均分。

#### RR 优缺点与时间片

- 优点：公平、实现简单、响应时间可控；缺点：无优先级，频繁切换带来开销，CPU 绑定任务可能导致 cache 抖动。
- 时间片太大→响应差、可能饿死短任务；太小→切换开销占比高。通常取“上下文切换开销/典型计算量”的折中（本实验默认 5 tick）。
- `RR_proc_tick` 置 `need_resched` 是为了在中断返回用户态前统一触发 `schedule()`，保持调度点单一、避免在中断上下文直接切换造成状态复杂。

#### 拓展思考

- 优先级 RR：可在队列中维护多级队列或在入队时按优先级插入/多队列轮询；同时动态调整时间片（如优先级高→更大时间片或更高队列频率）。
- 多核支持：当前 run_queue 为单核全局，且无锁保护；需为每核维护 rq，加锁或使用 per-CPU 屏蔽中断保护，增加负载均衡/窃取逻辑，调度类接口扩展 load_balance/get_proc 等。
