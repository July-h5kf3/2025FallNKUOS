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

//计算阶数，即log2(n)，一般输入的n都是2的幂次
static int get_order(size_t n) 
{
    int order = 0;
    while (n > 1) 
    {
        n >>= 1;
        order++;
    }
    return order;
}

//计算2的幂次，即2^order
static size_t power_of_two(int order) 
{
    return 1UL << order;
}

//我们用free_list数组来管理空闲页面，free_list[i]表示阶数为i的空闲页面链表
//比如说：free_list[0]表示阶数为0的空闲页面链表，也就是最小内存块的空闲页面链表，其实就是单页的空闲页面链表
struct BuddySystem
{
    //总页面数，也就是内存块的大小
    size_t total_pages;
    struct Page* base;

    //我们知道内存总页数为32768，阶数为15，所以free_list数组大小为16就可以满足需求
    list_entry_t free_list[16]; 
    int max_order; 
    size_t nr_free; //空闲页面数
};

static void buddy_init_system(struct BuddySystem* buddy, size_t n, struct Page* base)
{
    //这个函数的作用是初始化伙伴分配器，输入参数为管理的内存空间的总页面数n和内存空间的首地址base

    //确保页面数是2的幂次
    if (n < 1 || !is_order_of_two(n))
    {
        cprintf("Error: size must be a power of two.\n");
        buddy->total_pages = 0;
        return;
    }
    
    buddy->total_pages = n;
    buddy->base = base;

    //获取n的阶数，这个阶数可以用来确定free_list数组的大小
    buddy->max_order = get_order(n);
    buddy->nr_free = n;
    
    //初始化所有free_list链表
    for (int i = 0; i <= buddy->max_order; i++) {
        list_init(&buddy->free_list[i]);
    }
    
    //初始化所有页面状态，将所有页面状态设置为未分配，引用计数为0
    for (size_t i = 0; i < n; i++) {
        struct Page* page = base + i;
        page->flags = 0;
        page->property = 0;
        set_page_ref(page, 0);
        list_init(&page->page_link);  //初始化链表节点
    }
    
    //将整个内存块作为一个大的空闲块添加到最高阶的free_list中
    struct Page* first_page = base;
    first_page->property = n;  //记录这个块的大小
    list_add(&first_page->page_link, &buddy->free_list[buddy->max_order]);
}

//向指定阶数的free_list中插入内存块
static void buddy_insert_block(struct BuddySystem* buddy, struct Page* block, int order)
{
    if (!block || order < 0 || order > buddy->max_order) return;
    
    //设置块的大小
    block->property = power_of_two(order);
    
    //如果链表为空，直接插入
    if (list_empty(&buddy->free_list[order])) {
        list_add(&buddy->free_list[order], &block->page_link);
        return;
    }
    
    //否则按地址顺序插入（从低到高），找到第一个大于block的页面，插入到它前面
    list_entry_t* le = &buddy->free_list[order];
    while ((le = list_next(le)) != &buddy->free_list[order]) {
        struct Page* page = le2page(le, page_link);
        if (block < page) {
            list_add_before(le, &block->page_link);
            return;
        }
    }
    
    //如果没找到合适位置，插入到链表末尾
    list_add(&buddy->free_list[order], &block->page_link);
}

//从伙伴分配器中分配内存块，所需的内存块大小为n，返回值为分配的内存块的首地址
static struct Page* buddy_alloc_block(struct BuddySystem* buddy, size_t n)
{
    if (n <= 0 || n > buddy->nr_free) return NULL;
    
    //对n进行调整，使其变为2的幂次，便于后续的分配
    //随后获取调整后的n的阶数，即log2(n)，这样就知道要在free_list[required_order]中寻找空闲页面
    int required_order = get_order(fix_size(n));
    
    //从所需阶数开始向上查找可用的块
    int current_order = required_order;
    while (current_order <= buddy->max_order) 
    {
        if (!list_empty(&buddy->free_list[current_order])) 
        {
            //如果在free_list[current_order]中存在空闲页面，那么就从链表中移除一个页面，并返回这个页面
            struct Page* block = le2page(buddy->free_list[current_order].next, page_link);
            list_del(&block->page_link);
            
            //如果块比需要的大，需要分割
            //例如在总内存块大小为16的内存空间中，需要分配8个页面，但是此时free_list[3]中不存在空闲页面
            //那么就继续向上查找，此时free_list[4]中存在空闲页面，那么就从free_list[4]中移除一个页面，并对其进行分割
            while (current_order > required_order) 
            {
                current_order--;
                size_t half_size = power_of_two(current_order);
                
                //创建伙伴块，其实就是将当前块分割成两个大小为half_size的块，然后将其添加到对应阶数的链表中
                struct Page* buddy_block = block + half_size;
                
                //将伙伴块添加到对应阶数的链表中
                buddy_insert_block(buddy, buddy_block, current_order);
            }
            
            // 标记块为已分配
            block->property = 0;
            buddy->nr_free -= power_of_two(required_order);  // 减少实际分配的块大小
            
            return block;
        }
        current_order++;
    }
    
    return NULL;  // 没有找到合适的块
}

//释放内存块到伙伴分配器，输入参数为要释放的内存块的首地址block和要释放的内存块的大小n
static void buddy_free_block(struct BuddySystem* buddy, struct Page* block, size_t n)
{
    if (!block || n <= 0) return;
    
    //计算块的阶数（按2的幂次处理）
    //因为分配的时候是对n进行调整，使其变为2的幂次，然后再分配n个页面
    //所以这里看似是释放了n个页面，但实际上是释放了2^order个页面
    int order = get_order(fix_size(n));
    size_t block_size = power_of_two(order);
    
    //重置页面状态（只重置实际使用的页面）
    for (size_t i = 0; i < n; i++) {
        struct Page* page = block + i;
        page->flags = 0;
        set_page_ref(page, 0);
    }
    
    //设置块的大小，不急着先把block代表的内存块加入到free_list中，先尝试与伙伴块合并
    block->property = block_size;
    
    //尝试与伙伴块合并
    while (order < buddy->max_order) 
    {
        //计算伙伴块的地址
        size_t buddy_offset = (block - buddy->base) ^ block_size;
        struct Page* buddy_block = buddy->base + buddy_offset;
        
        //检查伙伴块是否在同一阶数的free_list中
        list_entry_t* le = &buddy->free_list[order];
        int found_buddy = 0;
        while ((le = list_next(le)) != &buddy->free_list[order]) {
            struct Page* page = le2page(le, page_link);
            if (page == buddy_block) {
                found_buddy = 1;
                break;
            }
        }
        
        if (found_buddy) {
            // 找到伙伴块，进行合并
            list_del(&buddy_block->page_link);
            
            // 选择地址较小的块作为合并后的块
            if (buddy_block < block) {
                block = buddy_block;
            }
            
            // 更新块大小和阶数
            block_size *= 2;
            order++;
            block->property = block_size;
        } else {
            // 没有找到伙伴块，停止合并
            break;
        }
    }
    
    // 将块添加到对应阶数的free_list中
    buddy_insert_block(buddy, block, order);
    buddy->nr_free += block_size;
}

// 显示伙伴分配器状态
static void buddy_show_status(struct BuddySystem* buddy) 
{
    cprintf("=== Buddy System Status ===\n");
    cprintf("Total pages: %d, Free pages: %d, Max order: %d\n", 
            buddy->total_pages, buddy->nr_free, buddy->max_order);
    
    for (int i = 0; i <= buddy->max_order; i++) {
        int count = 0;
        list_entry_t* le = &buddy->free_list[i];
        while ((le = list_next(le)) != &buddy->free_list[i]) {
            count++;
        }
        if (count > 0) {
            cprintf("Order %d (size %d): %d blocks\n", i, power_of_two(i), count);
        }
    }
    cprintf("===========================\n");
}

// 全局伙伴分配器实例
static struct BuddySystem buddy_system;

static void buddy_init(void) 
{
    buddy_system.total_pages = 0;
    buddy_system.nr_free = 0;
    buddy_system.max_order = 0;
    buddy_system.base = NULL;
}

static void buddy_init_memmap(struct Page *base, size_t n) 
{
    //初始化内存块，块大小为n个页面，块中第一个页的地址为 base
    assert(n > 0);
    if (!is_order_of_two(n)) n = fix_size(n);
    buddy_init_system(&buddy_system, n, base);
}

static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > buddy_system.nr_free) return NULL;   
    return buddy_alloc_block(&buddy_system, n);
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    assert(base >= buddy_system.base && base < buddy_system.base + buddy_system.total_pages);    
    buddy_free_block(&buddy_system, base, n);
}

static size_t buddy_nr_free_pages(void) {
    return buddy_system.nr_free;
}

static void show_buddy_array(int start, int max_order) {
    buddy_show_status(&buddy_system);
}

static void buddy_check(void) 
{
    cprintf("=== Buddy System Check ===\n");
    cprintf("Total free pages: %d\n", buddy_system.nr_free);
    cprintf("Buddy system size: %d\n", buddy_system.total_pages);
    
    if (buddy_system.total_pages > 0) {
        cprintf("Initial Buddy system status:\n");
        buddy_show_status(&buddy_system);
        
        // 测试分配和释放
        cprintf("\n=== Testing Allocation and Deallocation ===\n");
        
        // 测试1: 分配1页
        cprintf("Test 1: Allocating 1 page\n");
        struct Page *page1 = buddy_alloc_pages(32678);
        if (page1) {
            cprintf("Allocated 1 page at offset %d\n", page1 - buddy_system.base);
            cprintf("Free pages after allocation: %d\n", buddy_system.nr_free);
            buddy_show_status(&buddy_system);
        }
        
        // 测试2: 分配2页
        cprintf("\nTest 2: Allocating 2 pages\n");
        struct Page *page2 = buddy_alloc_pages(2);
        if (page2) {
            cprintf("Allocated 2 pages at offset %d\n", page2 - buddy_system.base);
            cprintf("Free pages after allocation: %d\n", buddy_system.nr_free);
            buddy_show_status(&buddy_system);
        }
        
        // 测试3: 分配4页
        cprintf("\nTest 3: Allocating 4 pages\n");
        struct Page *page4 = buddy_alloc_pages(4);
        if (page4) {
            cprintf("Allocated 4 pages at offset %d\n", page4 - buddy_system.base);
            cprintf("Free pages after allocation: %d\n", buddy_system.nr_free);
            buddy_show_status(&buddy_system);
        }
        
        // 测试4: 释放2页
        cprintf("\nTest 4: Freeing 2 pages\n");
        if (page2) {
            buddy_free_pages(page2, 2);
            cprintf("Freed 2 pages at offset %d\n", page2 - buddy_system.base);
            cprintf("Free pages after freeing: %d\n", buddy_system.nr_free);
            buddy_show_status(&buddy_system);
        }
        
        // 测试5: 释放1页
        cprintf("\nTest 5: Freeing 1 page\n");
        if (page1) {
            buddy_free_pages(page1, 32678);
            cprintf("Freed 1 page at offset %d\n", page1 - buddy_system.base);
            cprintf("Free pages after freeing: %d\n", buddy_system.nr_free);
            buddy_show_status(&buddy_system);
        }
        
        // 测试6: 释放4页
        cprintf("\nTest 6: Freeing 4 pages\n");
        if (page4) {
            buddy_free_pages(page4, 4);
            cprintf("Freed 4 pages at offset %d\n", page4 - buddy_system.base);
            cprintf("Free pages after freeing: %d\n", buddy_system.nr_free);
            buddy_show_status(&buddy_system);
        }
        
        cprintf("\n=== Final State ===\n");
        cprintf("Final free pages: %d\n", buddy_system.nr_free);
        buddy_show_status(&buddy_system);
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