#include <pmm.h>
#include <list.h>
#include <string.h>
#include <stdio.h>

static int is_order_of_two(size_t n) 
{
    return n > 0 && ((n & (n - 1)) == 0);
}

static size_t fix_size(size_t n) 
{
    size_t res = 1;
    while (res < n) res *= 2;
    return res;
}

struct Buddy2
{
    size_t size;//size是内存的总页面数，也是buddy2二叉树根节点的longest数值
    struct Page* base;
    //我们需要记录内存中第一个页面的地址，base+offset就可以获取目标页面
    struct Page* original_base;  //记录原始base指针，用于访问longest数组

    //考虑到无法使用malloc动态分配longest数组，我们将其直接存放在伙伴系统管理的内存中
    //所以我们只需要记录longest数组的大小即可
    size_t longest_size;
};

static void buddy2_init(struct Buddy2* buddy, size_t n, struct Page* base)
{
    //初始化函数，设计一颗完全二叉树，将内存空间分为n个页，页的数目为2的幂次
    if (n < 1 || !is_order_of_two(n))
    {
        //如果页的数目不为2的幂次，那么就无法后续进行折半生成完全二叉树，返回错误
        cprintf("Error: size must be a power of two.\n");
        buddy->size = 0;
        return;
    }
    buddy->size = n;
    buddy->original_base = base;  // 保存原始base指针
    buddy->longest_size = 2 * n - 1;

    //直接使用伙伴系统管理的内存开头作为longest数组，无需malloc
    //这里假设longest数组存放在伙伴系统管理的内存开始处
    //我们需要为longest数组预留空间，所以实际可用的内存会减少
    int* longest = (int*)base;  

    int node_size = 2 * n;
    for (size_t i = 0; i < buddy->longest_size; i++)
    {
        if (is_order_of_two(i + 1)) node_size /= 2;
        //假设i+1为2的幂次，那么说明进入了二叉树新的一层，那么其对应的内存单元数目折半
        longest[i] = node_size;
    }

    //调整base指针，跳过longest数组占用的空间
    //longest数组占用了 (2*n-1) * sizeof(int) 字节
    size_t metadata_size = buddy->longest_size * sizeof(int);

    //将数组占用的字节数转换为页面数，向上取整，因为直接除以sizeof(struct Page)是向下取整
    //向下取整显然不合理，所以加上page_size-1再除以page_size
    size_t pages_for_metadata = (metadata_size + sizeof(struct Page) - 1) / sizeof(struct Page);
    
    //base指针指向内存块的第一个页面，也就是longest数组占用的页面之后的第一个页面
    buddy->base = base + pages_for_metadata;
    size_t available_pages = n - pages_for_metadata;
    
    //如果内存大小大于4096，那么就向下取整到最近的2的幂次
    if (n >= 4096) {
        buddy->size = 1;
        while (buddy->size * 2 <= available_pages) 
        {
            buddy->size *= 2;
        }
    } else {
        // 小内存：直接使用输入的n，避免过度浪费
        buddy->size = n;
    }
    
    // 重新计算longest数组，使用确定后的实际页面数
    int actual_node_size = 2 * buddy->size;
    for (size_t i = 0; i < buddy->longest_size; i++)
    {
        if (is_order_of_two(i + 1)) actual_node_size /= 2;
        longest[i] = actual_node_size;
    }
}

static int buddy2_alloc(struct Buddy2* buddy, size_t n)
{
    //如果要分配n个内存单元，使用fix_size函数，将其调整为适合的内存块大小
    if (n <= 0) n = 1;
    else if (!is_order_of_two(n)) n = fix_size(n);

    // 使用original_base访问longest数组
    int* longest = (int*)buddy->original_base;

    if (longest[0] < (int)n) return -1;
    //倘若目前内存总空间都无法满足n，那么自然无法进行分配

    size_t index = 0;
    int node_size;

    //循环的目的在于找到合适的index，终止条件为node_size=n，也就是longest[index]=node_size=n时
    for (node_size = buddy->size; node_size != (int)n; node_size /= 2)
    {
        if (longest[2 * index + 1] >= (int)n)
        {
            //假设当前节点的左儿子可以满足n，那么进入左子树继续遍历
            index = 2 * index + 1;
        }
        else
        {
            index = 2 * index + 2;
        }
    }
    //找到合适的index后，将其内存块页面值变为0(这一内存块的首页面property为0)
    longest[index] = 0;

    int offset = (index + 1) * node_size - buddy->size;
    //offset的计算是这样的流程：
        //offset = node_size * pos，其中node_size = s，pos指当前index在该层的第几个
        //level = log_2{size / node_size}
        //first_index = 2 ^ level - 1
        //pos = index - first_index
        //offset = node_size * (index - 2 ^ level + 1) = (index + 1) * node_size - size

    while (index)
    {
        index = (index - 1) / 2;
        longest[index] = longest[2 * index + 1] > longest[2 * index + 2] ?
                                longest[2 * index + 1] : longest[2 * index + 2];
        //逐级向上遍历，修改父节点的longest值，取两儿子节点中数值较大的
    }
    return offset;
}

static void buddy2_free(struct Buddy2* buddy, int offset)
{
    if (offset < 0 || offset >= (int)buddy->size) return;

    // 使用original_base访问longest数组
    int* longest = (int*)buddy->original_base;

    int node_size = 1;
    size_t index = buddy->size - 1 + offset;
    //我们先假设释放的内存为最小内存块，所代表的可分配内存空间为1个单元
    //size-1为所有叶子节点的开头，比如size=8时，第一个叶子节点的index=7
    //加上offset后，就确定了我们要释放的是哪个最小内存单元

    //找到第一个标记为'完全被占用'的节点，获取其Index和该节点理论的内存块大小
    for (; longest[index]; index = (index - 1) / 2)
    {
        node_size *= 2;
        if (index == 0) return;
    }

    longest[index] = node_size;
    while (index)
    {
        index = (index - 1) / 2;
        node_size *= 2;
        int ll = longest[2 * index + 1];
        int rl = longest[2 * index + 2];

        //加入左右儿子数值之和与父节点理论值一致，那么恢复父节点的longest值
        if (ll + rl == node_size) 
        {
            longest[index] = node_size;
        } 
        else 
        {
            longest[index] = ll > rl ? ll : rl;
        }
    }
}

static void buddy2_destroy(struct Buddy2* buddy) 
{
    buddy->size = 0;
    buddy->base = NULL;
    buddy->original_base = NULL;
    buddy->longest_size = 0;
}

static void buddy2_show_array(struct Buddy2* buddy, int start, int max_order) 
{
    // 使用original_base访问longest数组
    int* longest = (int*)buddy->original_base;

    cprintf("Buddy array (longest) from index %d:\n", start);
    for (int i = start; i < (int)buddy->longest_size && i < (1 << (max_order + 1)) - 1; i++) 
    {
        cprintf("longest[%d] = %d\n", i, longest[i]);
    }
}

static struct {
    size_t nr_free; 
    struct Buddy2 buddy_storage;  
} buddy_area;

#define nr_free (buddy_area.nr_free)
#define buddy (buddy_area.buddy_storage)
#define MAX_BUDDY_ORDER 10  // 最大块阶数，例如 2^10 = 1024 页


static void buddy_init(void) 
{
    nr_free = 0;
}

static void buddy_init_memmap(struct Page *base, size_t n) 
{
    //初始化内存块，块大小为n个页面，块中第一个页的地址为 base
    assert(n > 0);
    if (!is_order_of_two(n)) n = fix_size(n);

    //初始化所有页面状态
    for(struct Page *p = base; p != base + n; p++) 
    {
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    //如果已经初始化过伙伴系统，先销毁
    if (buddy.size > 0) {
        buddy2_destroy(&buddy);
    }
    
    //初始化伙伴系统，使用输入的n作为实际管理的内存大小
    buddy2_init(&buddy, n, base);
    nr_free += buddy.size;
    
}

static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) return NULL;
    
    size_t alloc_size = fix_size(n);
    int offset = buddy2_alloc(&buddy, alloc_size);
    if (offset < 0) return NULL;
    
    //直接通过偏移量计算分配的页面地址
    struct Page *page = buddy.base + offset;
    
    //更新空闲页面计数
    nr_free -= n;
    
    return page;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    
    //重置页面状态
    for (struct Page *p = base; p != base + n; p++) {
        p->flags = 0;
        set_page_ref(p, 0);
    }
    
    //通过buddy系统释放内存
    int offset = base - buddy.base;
    buddy2_free(&buddy, offset);
    
    nr_free += n;
}

static size_t buddy_nr_free_pages(void) {
    return nr_free;
}

static void show_buddy_array(int start, int max_order) {
    if (buddy.size > 0) {
        buddy2_show_array(&buddy, start, max_order);
    }
}

static void buddy_check(void) 
{
    cprintf("=== Buddy System Check ===\n");
    cprintf("Total free pages: %d\n", nr_free);
    cprintf("Buddy system size: %d\n", buddy.size);
    
    if (buddy.size > 0) {
        cprintf("Initial Buddy array status:\n");
        buddy2_show_array(&buddy, 0, 4);
        
        // 测试分配和释放
        cprintf("\n=== Testing Allocation and Deallocation ===\n");
        
        // 测试1: 分配1页
        cprintf("Test 1: Allocating 1024 page\n");
        struct Page *page1 = buddy_alloc_pages(1024);
        if (page1) {
            cprintf("Allocated 1 page at offset %d\n", page1 - buddy.base);
            cprintf("Free pages after allocation: %d\n", nr_free);
            buddy2_show_array(&buddy, 0, 4);
        }
        
        // 测试2: 分配2页
        cprintf("\nTest 2: Allocating 2048 pages\n");
        struct Page *page2 = buddy_alloc_pages(2048);
        if (page2) {
            cprintf("Allocated 2 pages at offset %d\n", page2 - buddy.base);
            cprintf("Free pages after allocation: %d\n", nr_free);
            buddy2_show_array(&buddy, 0, 4);
        }
        
        // 测试3: 分配4页
        cprintf("\nTest 3: Allocating 4096 pages\n");
        struct Page *page4 = buddy_alloc_pages(4096);
        if (page4) {
            cprintf("Allocated 4 pages at offset %d\n", page4 - buddy.base);
            cprintf("Free pages after allocation: %d\n", nr_free);
            buddy2_show_array(&buddy, 0, 4);
        }
        
        // 测试4: 释放2页
        cprintf("\nTest 4: Freeing 2048 pages\n");
        if (page2) {
            buddy_free_pages(page2, 2048);
            cprintf("Freed 2 pages at offset %d\n", page2 - buddy.base);
            cprintf("Free pages after freeing: %d\n", nr_free);
            buddy2_show_array(&buddy, 0, 4);
        }
        
        // 测试5: 释放1页
        cprintf("\nTest 5: Freeing 1024 pages\n");
        if (page1) {
            buddy_free_pages(page1, 1024);
            cprintf("Freed 1 page at offset %d\n", page1 - buddy.base);
            cprintf("Free pages after freeing: %d\n", nr_free);
            buddy2_show_array(&buddy, 0, 4);
        }
        
        // 测试6: 释放4页
        cprintf("\nTest 6: Freeing 4096 pages\n");
        if (page4) {
            buddy_free_pages(page4, 4096);
            cprintf("Freed 4 pages at offset %d\n", page4 - buddy.base);
            cprintf("Free pages after freeing: %d\n", nr_free);
            buddy2_show_array(&buddy, 0, 4);
        }
        
        cprintf("\n=== Final State ===\n");
        cprintf("Final free pages: %d\n", nr_free);
        buddy2_show_array(&buddy, 0, 4);
    }
    
    cprintf("=== Check Complete ===\n");
}


const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};