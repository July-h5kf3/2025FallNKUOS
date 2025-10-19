<center>
<h1>OS-lab02实验报告</h1>
</center>

## 一、实验要求

1.理解first-fit 连续物理内存分配算法

阅读实验手册的教程并结合`kern/mm/default_pmm.c`中的相关代码，认真分析default_init_memmap，default_init，default_alloc_pages， default_free_pages等相关函数，并描述程序在进行物理内存分配的过程以及各个函数的作用。

回答：你的first fit算法是否有进一步的改进空间？

2.实现 Best-Fit 连续物理内存分配算法（需要编程）

参考kern/mm/default_pmm.c对First Fit算法的实现，**编程实现Best Fit页面分配算法**，算法的时空复杂度不做要求，能通过测试即可。 

回答：你的 Best-Fit 算法是否有进一步的改进空间？

3.扩展练习Challenge：buddy system（伙伴系统）分配算法（需要编程）

在ucore中**实现buddy system分配算法**，要求有比较充分的测试用例说明实现的正确性，需要有设计文档。

4.扩展练习Challenge：任意大小的内存单元slub分配算法（需要编程）

在ucore中**实现slub分配算法**。要求有比较充分的测试用例说明实现的正确性，需要有设计文档。

5.扩展练习Challenge：硬件的可用物理内存范围的获取方法（思考题）

如果 OS 无法提前知道当前硬件的可用物理内存范围，请问你有何办法让 OS 获取可用物理内存范围？

## 二、实验内容(part 1 + part2)

练习一：

- 函数`default_init_memmap`主要用于初始化一段连续的内存区域，将其转化为一个空闲的内存块。

具体而言，该函数是first_fit算法的初始化阶段，为内存分配器建立了一个有序的空闲内存块链表。给定输入空闲块的起始页面指针Base以及空闲内存块的页面数量n，则该函数会将以Base开始的连续n个Page进行清空初始化，然后被串入空闲内存块链表free_list(双向循环链表，链表中的空闲块依据其起始位置地址大小从小到大排序)。

串入链表的逻辑如下:
```
if (list_empty(&free_list)) {
        list_add(&free_list, &(base->page_link));
    } else {
        list_entry_t* le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* page = le2page(le, page_link);
            if (base < page) {
                list_add_before(le, &(base->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(base->page_link));
            }
        }
    }
```
若此时`free_list`是一个空链表，那么则直接加入新块，即将表头直接与该页面的`Page_link`连接，通过函数`list_add`实现。

若此时并非空链表，那么就开始逐个遍历链表元素，将其插入到链表中起始位置刚好大于插入空闲块的起始位置(Base)地址的元素之前，通过`list_add_before`实现。若链表中所有元素的页面地址都小于Base，那么就插入链表末尾。

其中空闲块传入链表的地址为Page的一个成员值，要从成员值获取Page的Base，需要通过`le2page`宏实现。它的作用是，给定 `list_entry* le`，以及 `member = page_link`，利用 `to_struct` 宏，从 `le` 的地址向前偏移，得到 `Page` 结构体的首地址，最终返回 `Page*`。

- `default_init`函数则是对用于串接所有空闲块的双向循环链表`free_list`的初始化函数。

具体而言，该函数会获取空闲链表头节点的地址，并将链表头节点的`prev`,`next`指针都指向自身，形成空链表。并通过`nr_free = 0`标识没有任何空闲页面。

- `default_free_pages`函数则实现了`first_fit`算法的内存释放逻辑，给定要释放内存的起始页面指针`Base`以及要释放的页面数量，即可释放对应的连续内存块。

首先是将对应的内存块进行清空和初始化，然后与函数`default_init_memmap`逻辑一致，将清空和初始化后的空闲内存块串入空闲量表`free_list`.最后进行后向合并，若释放的内存与已经在空闲链表中的后一个空闲块在地址上是连续的`base + base->property == p`,那么我们可以将这两个空闲块进行合并。

- `default_alloc_pages`函数则是进行内存分配的函数，对应的算法是`first_fit`算法，给定需要分配的连续页面数量，则返回分配到的内存块第一个页面的指针(若成功)

具体而言，先遍历空闲链表`free_list`,若存在满足条件`p->property >= n`，即空闲块连续页面数量大于等于n的空闲块，则将第一个满足该条件的内存块(`page`)取出。然后在空闲链表中删除对应的内存块。若该空闲块的大小大于n，那么就需要进行内存块分裂，将`page + n`作为新的内存块的起始地址，其中剩余的块数为`pape->property - n`，插入到原来的位置。

采用的`first_fit`算法存在大量的优化空间，下面是一些在不改变其核心实现算法的优化:

- 我们可以维护多个`free_list`,以空闲块的大小分类，减少检索空间

- 我们可以增加跳表或其他数据结构，实现复杂度更低的查找

- 可以加入缓存机制

练习二:

要求实现`Best_fit`的内存分配算法，简单来说在`first_fit`中我们的策略是找到空闲链表`free_list`中第一个足够大的空闲块，但是这种方法一方面会产生大量的内存碎片，出现总内存足够但无法继续分配的问题，内存利用效率地下；另一方面`first_fit`往往倾向于选择较低地址的内存块，高地址的内存被忽略。

`Best_fit`为了解决这一问题，在分配内存时我们不选择第一个足够大的空闲块，而是选择最接近的满足要求的内存块。

具体来说，我们对`first_fit`的代码进行更改
```
while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            // page = p;
            if (p->property < min_size) {
                min_size = p->property;
                page = p;
            }
        }
    }
```

为了测试我们更改的正确性，我们将`pmm.c`中的`pmm_manager`更改为`best_fit_pmm_manager`然后进行执行`make grade`进行测试。

得到如下结果:
```
root@DESKTOP-6N21GHG:~/操作系统/lab02# make grade
>>>>>>>>>> here_make>>>>>>>>>>>
gmake[1]: Entering directory '/root/操作系统/lab02' + cc kern/init/entry.S + cc kern/init/init.c + cc kern/libs/stdio.c + cc kern/debug/panic.c + cc kern/driver/console.c + cc kern/driver/dtb.c + cc kern/mm/best_fit_pmm.c + cc kern/mm/default_pmm.c + cc kern/mm/pmm.c + cc libs/printfmt.c + cc libs/readline.c + cc libs/sbi.c + cc libs/string.c + ld bin/kernel riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img gmake[1]: Leaving directory '/root/操作系统/lab02'
>>>>>>>>>> here_make>>>>>>>>>>>
<<<<<<<<<<<<<<< here_run_qemu <<<<<<<<<<<<<<<<<<
try to run qemu
qemu pid=8715
<<<<<<<<<<<<<<< here_run_check <<<<<<<<<<<<<<<<<<
  -check physical_memory_map_information:    OK
  -check_best_fit:                           OK
Total Score: 25/25
```
测试通过，说明我们更改正确！

与`First_fit`算法类似，同样存在一些性能提升的方法:

不像`First_fit`算法一遇到满足大小要求的内存块就结束对空闲链表的遍历，`Best_fit`算法要遍历完整个空闲链表才能得到最终决定的内存块。因此它在时间上对于链表的长度是十分敏感的。这要求我们提高它的检索效率，可以同样采用多链表进行大小分类以及缓存机制。

此外buddy system算法以及slub分配算法可以进一步提高算法的性能以及内存利用率，我们将在challenge部分进行实现与测试。

## 三、实验内容(challenge1)

## 四、实验内容(challenge2)

### 1. 原理概述

​        SLUB（The *SLAB allocator*, later improved as *SLUB*）是一种内核级高效内存分配算法，该算法建立在页级内存分配器之上，主要用于管理内核中大量小对象的动态分配与释放，之前我们所实现的`first_fit`等算法都是以页为单位的，但是难免会有用户所需的内存大小其实不足一页，那么此时slub算法的作用就体现出来了，slub算法的核心思想如下：

- **分层管理：**在该算法中我们采用两层架构的方式，第一层使用我们基础的页分配器算法（`first_fit`）以页为单位分配内存，第二层则将每页按照固定的大小分为多个对象，以对象为单位进行分配和释放。
- **Cache机制：**前面提到我们要将每页按照固定的大小进行分割，那么这个“固定大小”也可能有很多，所以我们对于不同大小的对象建立不同的Cache，每一个Cache管理按照相同大小分割的若干slab（页），从而减少碎片并提高分配效率。
- **对象复用与局部性：**如果某个slab之前分配出去的对象都被释放，自身变为完全空闲，我们并不会立马归还给页分配器，而是仍保留该slab，以便下次快速重用，从而减少频繁的页分配。

### 2. 主要机制

​        明白了slub任意内存分配算法的原理后，我们也很清晰要实现该算法的关键所在了：

1. **多 Cache 管理机制：**我们要为不同大小的对象建立不同的缓存池，每个cache管理若干slab，然后再分配内存时根据请求大小选择最合适的Cache。
2. **Slab层对象管理：**我们所说的slab就是指物理内存中的一页，在slab内我们可以通过位图或标志数组记录每个对象的使用状态。
3. **两层结构协同工作：**第一层我们通过`alloc_page()`向页分配器申请物理页，然后在第二层中（即页内），完成小对象的分配，释放对象时我们只需要在slab层更新标志数组即可。
4. **分配与释放接口：**在之前的页分配算法中，分配与释放函数的参数都是以`Page`为单位的，而我们此次的slub与之前的不同，是以更小的对象为单位的，在分配函数时我们传入的`size_t`参数是对象大小`size`，而不是之前的`n`（page数）了，另外释放对象时传入的参数指针指向的也不是某一页的基址了，可能是其所属某页的中间的某个位置，即由`Page *base`变为了`void *obj`。
5. **测试与验证：**实现完基础的算法结构后，我们还要编写相应的自检函数`slub_check()`，测试不同大小的分配、释放、复用和异常情况。

### 3. 具体实现过程

​       底层的页级分配器我们就采用之前的`first_fit`算法，设计报告中就不再讲述这部分了，主要是上层的slub分配器如何实现。

#### 3.1 总体规划

​       我们先对要实现的`slub`分配器的程度进行规划，在此我打算实现一个包含`3`种`cache`的分配器，且大小分别为`32`、`64`、`128`字节，并且对于对象和slab的管理我们进行简化实现，不采用复杂的链表形式，而使用数组来管理，所以我们需要设置一些数值，设定如下：

```c
#define cache_num 3//总共有3种大小的缓存
#define MAX_SLABS 64//因为我们使用的不是链表，而是数组来管理每个cache的slab，所以需要设定一个最大值
static const size_t cache_sizes[cache_num]={32,64,128};//这3种cache的大小分别为32字节，64字节和128字节
```

#### 3.2 关键结构体创建

​       在进行主要的函数算法编写之前，我们要先实现好用于管理对象和slab的两个结构体：**Slab和Cache**。

​       首先是**Slab**结构体，我们需要知道当前`slab`所在的位置——即它属于哪一个`page`，所以需要一个`Page`指针，另外需要一个数组来记录`slab`中每个对象的空闲情况，由于静态数组在定义时要有固定的大小，我们就将其设置为可能得最大数值（当对象大小为32字节时，单个slab的对象个数为`4096/32=128`个）。

​       另外是**Cache**结构体，同样的采用数组进行管理其所有的`slab`，也需要一个固定大小，因此我们固定每种`cache`可以拥有的`slab`个数最多为`MAX_SLABS=64`个。

```c
//定义slab结构体
typedef struct{
    struct Page *page;//该slab属于哪一页              
    uint8_t used[128];//用于记录slab中的对象是否被使用的数组（因为我们设置的对象大小最小为32字节，所以每个slab中的对象个数最多为128个）
    size_t free_num;//当前slab的空闲对象个数        
}Slab;
//定义cache结构体
 typedef struct{
    size_t obj_size;
    Slab slabs[MAX_SLABS];
    size_t slab_count;//该缓存内已有的slab数量
}Cache;
static Cache caches[cache_num];//初始化cache全局数组
```

#### 3.3 初始化

​        首先我们初始化底层的页级分配器（`default_init`），然后再将全局的 `caches` 数组清零，并设置每个缓存的 `obj_size` 属性（即`3`个`cache`分别为`32`、`64`和`128`字节，但所包含的`slab`个数均为`0`，因为此时还没有分配任何 slab）。

```c
//初始化每个cache
static void slub_init(void){
    default_init();//先初始化第一层页级分配器
    memset(caches,0,sizeof(caches));//将全局的cache数组清零
    //将对应cache的对象大小属性进行设置
    for (int i=0;i<cache_num;i++) {
        caches[i].obj_size=cache_sizes[i];
    }
}
```

#### 3.4 对象的分配

​       我们的分配逻辑是：用户申请某大小的内存调用分配函数，传入的参数就是`size`，根据这个`size`我们从`3`个`cache`中找到最小的满足该`size`的缓存，然后遍历该`cache`中所有的`slab`，查找有空闲对象的`slab`，如果所有的`slab`都满了，或者当前的`cache`还没有`slab`，那么我们就调用底层的`default_alloc_pages`分配一页创建一个新的`slab`，并且按照一开始所找到的`cache`的`obj_size`属性进行初始化，初始化完毕后将这个新`slab`的第一个对象标记为`1`（`used`）：

```c
void *slub_alloc_size(size_t size){
    if(size==0){
        return NULL;
    }
    Cache *cache=select_cache(size);
    if (cache==NULL){
        return NULL; 
    }
    //遍历该cache下的slab，直到找到一个有空闲对象的slab
    for (size_t i=0;i<cache->slab_count;i++){
        Slab *slab=&cache->slabs[i];
        if (slab->free_num>0){
            for (size_t j=0;j<PGSIZE / cache->obj_size;j++){
                if (slab->used[j]==0){
                    slab->used[j]=1;
                    slab->free_num--;
                    //计算并返回对象的虚拟地址
                    void *obj = (void *)((uintptr_t)page2kva(slab->page) + j * cache->obj_size);
                    return obj;
                }
            }
        }
    }
    //如果所有现有 slab 都满了就创建一个新的 slab，初始化完后使用第一个对象
    Slab *newslab=create_slab(cache);
    if (newslab==NULL){
        return NULL;
    }
    newslab->used[0] = 1;
    newslab->free_num--;
    return (void *)page2kva(newslab->page);
}
```

​        在分配函数中我们会使用到的函数`create_slab`和`select_cache`的逻辑很简单，此处就不展示了，可以在`github`仓库中查看，其中`create_slab`要注意的是其申请新的页时调用的是底层分配器的函数，其他的就是很简单的初始化了。

​        如果传入的`size`为0、没有合适的`cache`（超出`slub`的处理范围了）或者所找到的`cache`下的`slab`都满了，并且没有空闲页了，这时候就会返回`NULL`。

#### 3.5 对象的释放

​         相应的释放函数逻辑并不难，当我们想释放某一个对象时，将该对象的指针`obj`传入释放函数，通过该指针指向的位置我们找到它所属的`slab`和`cache`，根据`obj`所指向的地址与该`slab`的`page`基址的差值我们可以得到该对象在`slab`中的索引位置，然后将该位置的标志位重置为`0`，空闲对象数量`+1`即可：

```c
void slub_free(void *obj) {
    if(obj==NULL){ return; }
    Slab *slab=NULL;
    Cache *cache=get_cache(obj,&slab);
    if (cache==NULL || slab==NULL) { return; }
    uintptr_t base=(uintptr_t)page2kva(slab->page);
    size_t idx=((uintptr_t)obj - base) / cache->obj_size;

    if (idx < PGSIZE / cache->obj_size && slab->used[idx]){
        slab->used[idx] = 0;// 标记为空闲
        slab->free_cnt++;// 增加空闲计数
    }
}
```

​        那么我们是怎样找到它所在的`slab`和`cache`的呢，这里我们就要用到另一个函数`get_cache`了，我们是通过遍历所有缓存的所有`slab`，检查给定的 `obj` 指针是否落在某个 `slab` 的虚拟地址空间内，如果找到，就返回对应的 `cache` 和 `slab`。（篇幅原因就不展示该函数了）

​        需要注意的是**当一个 slab 的所有对象都被释放后，该 slab 占用的物理页并不会立即归还给系统，而是留作后续分配时复用。**

### 4. 测试

​      另外还设计了多层次的测试用例以验证 SLUB 分配算法的正确性，测试用例主要覆盖以下几个层面：

1. 不同对象大小（32B、64B、128B）的分配与释放功能是否正常
2. SLUB 三层结构（cache–slab–object）的协同工作是否正确
3. 边界条件与异常输入的处理机制是否正确
4. 内存块的部分释放后是否能被正确复用
5. 多 cache 混合分配时是否能互不干扰
6. 对非法释放操作的安全防护是否有效

​       具体的测试代码见仓库中的代码文件。

## 实验内容(challenge3)

