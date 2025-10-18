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
    size_t size; 
    //size是内存的总页面数，也是buddy2二叉树根节点的longest数值       
    struct Page* base;  
    //我们需要记录内存中第一个页面的地址，base+offset就可以获取目标页面

    int* longest;        
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
    buddy->base = base;
    buddy->longest_size = 2 * n - 1;
    //因为不能使用malloc等函数，那么让longest数组紧跟在Buddy2结构体后面分配内存
    buddy->longest = (int*)(buddy + 1);

    int node_size = 2 * n;
    for (size_t i = 0; i < buddy->longest_size; i++) 
    {
        if (is_order_of_two(i + 1)) node_size /= 2;
        //假设i+1为2的幂次，那么说明进入了二叉树新的一层，那么其对应的内存单元数目折半
        buddy->longest[i] = node_size;
    }
}

static int buddy2_alloc(struct Buddy2* buddy, size_t n) 
{
    //如果要分配n个内存单元，使用fix_size函数，将其调整为适合的内存块大小
    if (n <= 0) n = 1;
    else if (!is_order_of_two(n)) n = fix_size(n);

    if (buddy->longest[0] < (int)n) return -1;
    //倘若目前内存总空间都无法满足n，那么自然无法进行分配

    size_t index = 0;
    int node_size;

    //循环的目的在于找到合适的index，终止条件为node_size=n，也就是longest[index]=node_size=n时
    for (node_size = buddy->size; node_size != (int)n; node_size /= 2) 
    {
        if (buddy->longest[2 * index + 1] >= (int)n) 
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
    buddy->longest[index] = 0;

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
        buddy->longest[index] = buddy->longest[2 * index + 1] > buddy->longest[2 * index + 2] ?
                                buddy->longest[2 * index + 1] : buddy->longest[2 * index + 2];
        //逐级向上遍历，修改父节点的longest值，取两儿子节点中数值较大的
    }
    return offset;
}

static void buddy2_free(struct Buddy2* buddy, int offset) 
{
    if (offset < 0 || offset >= (int)buddy->size) return;

    int node_size = 1;
    size_t index = buddy->size - 1 + offset;
    //我们先假设释放的内存为最小内存块，所代表的可分配内存空间为1个单元
    //size-1为所有叶子节点的开头，比如size=8时，第一个叶子节点的index=7
    //加上offset后，就确定了我们要释放的是哪个最小内存单元

    //找到第一个标记为‘完全被占用’的节点，获取其Index和该节点理论的内存块大小
    for (; buddy->longest[index]; index = (index - 1) / 2) 
    {
        node_size *= 2;
        if (index == 0) return;
    }

    buddy->longest[index] = node_size;
    while (index) 
    {
        index = (index - 1) / 2;
        node_size *= 2;
        int ll = buddy->longest[2 * index + 1];
        int rl = buddy->longest[2 * index + 2];

        //加入左右儿子数值之和与父节点理论值一致，那么恢复父节点的longest值
        if (ll + rl == node_size) {
            buddy->longest[index] = node_size;
        } else {
            buddy->longest[index] = ll > rl ? ll : rl;
        }
    }
}


static struct 
{
    list_entry_t free_list; 
    size_t nr_free; 
    struct Buddy2* buddy;
} buddy_area;

#define free_list (buddy_area.free_list)
#define nr_free (buddy_area.nr_free)
#define MAX_BUDDY_ORDER 10  // 最大块阶数，例如 2^10 = 1024 页

// pmm_manager 接口函数
static void buddy_init(void) 
{
    list_init(&free_list);
    nr_free = 0;
    buddy_area.buddy = NULL;
}

static void buddy_init_memmap(struct Page *base, size_t n) 
{
    //初始化内存块，块大小为n个页面，块中第一个页的地址为 base
    assert(n > 0);
    if (!is_order_of_two(n)) n = fix_size(n);

    size_t longest_size = 2 * n - 1;
    size_t bytes_needed = sizeof(struct Buddy2) + longest_size * sizeof(int);
    size_t meta_pages = (bytes_needed + PGSIZE - 1) / PGSIZE;


    for (struct Page *p = base; p != base + n; p++) 
    {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    base->property = n;
    SetPageProperty(base);
    nr_free += n;
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
    if (buddy_area.buddy) {
        buddy2_destroy(buddy_area.buddy);
        free(buddy_area.buddy);
    }
    buddy_area.buddy = (struct Buddy2*)malloc(sizeof(struct Buddy2));
    if (buddy_area.buddy) {
        buddy2_init(buddy_area.buddy, n, base);
    } else {
        cprintf("Error: failed to allocate Buddy2 struct.\n");
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) return NULL;
    size_t alloc_size = fix_size(n);
    int offset = buddy2_alloc(buddy_area.buddy, alloc_size);
    if (offset < 0) return NULL;
    struct Page *page = buddy_area.buddy->base + offset;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p == page) {
            list_del(&(p->page_link));
            break;
        }
    }
    if (alloc_size > n) {
        struct Page *p = page + n;
        p->property = alloc_size - n;
        SetPageProperty(p);
        le = &free_list;
        while ((le = list_next(le)) != &free_list) {
            struct Page* curr = le2page(le, page_link);
            if (p < curr) {
                list_add_before(le, &(p->page_link));
                break;
            } else if (list_next(le) == &free_list) {
                list_add(le, &(p->page_link));
            }
        }
    }
    nr_free -= n; // 只减去请求的页面数
    ClearPageProperty(page);
    return page;
}

static void buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    for (struct Page *p = base; p != base + n; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    size_t alloc_size = fix_size(n);
    int offset = base - buddy_area.buddy->base;
    buddy2_free(buddy_area.buddy, offset);
    base->property = alloc_size;
    SetPageProperty(base);
    nr_free += n;
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
    list_entry_t* le = list_prev(&(base->page_link));
    if (le != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p + p->property == base && is_order_of_two(p->property + base->property)) {
            p->property += base->property;
            ClearPageProperty(base);
            list_del(&(base->page_link));
            base = p;
        }
    }
    le = list_next(&(base->page_link));
    if (le != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (base + base->property == p && is_order_of_two(base->property + p->property)) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
    }
}

static size_t buddy_nr_free_pages(void) {
    return nr_free;
}

static void show_buddy_array(int start, int max_order) {
    if (buddy_area.buddy) {
        buddy2_show_array(buddy_area.buddy, start, max_order);
    }
}

static void buddy_system_check_easy_alloc_and_free_condition(void) {
    cprintf("CHECK OUR EASY ALLOC CONDITION:\n");
    cprintf("当前总的空闲块的数量为：%d\n", nr_free);
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;

    cprintf("首先,p0请求10页\n");
    p0 = alloc_pages(10);
    show_buddy_array(0, MAX_BUDDY_ORDER);

    cprintf("然后,p1请求10页\n");
    p1 = alloc_pages(10);
    show_buddy_array(0, MAX_BUDDY_ORDER);

    cprintf("最后,p2请求10页\n");
    p2 = alloc_pages(10);
    show_buddy_array(0, MAX_BUDDY_ORDER);

    cprintf("p0的虚拟地址为:0x%016lx.\n", (unsigned long)p0);
    cprintf("p1的虚拟地址为:0x%016lx.\n", (unsigned long)p1);
    cprintf("p2的虚拟地址为:0x%016lx.\n", (unsigned long)p2);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    cprintf("CHECK OUR EASY FREE CONDITION:\n");
    cprintf("释放p0...\n");
    free_pages(p0, 10);
    cprintf("释放p0后,总空闲块数目为:%d\n", nr_free);
    show_buddy_array(0, MAX_BUDDY_ORDER);

    cprintf("释放p1...\n");
    free_pages(p1, 10);
    cprintf("释放p1后,总空闲块数目为:%d\n", nr_free);
    show_buddy_array(0, MAX_BUDDY_ORDER);

    cprintf("释放p2...\n");
    free_pages(p2, 10);
    cprintf("释放p2后,总空闲块数目为:%d\n", nr_free);
    show_buddy_array(0, MAX_BUDDY_ORDER);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_system_check_easy_alloc_and_free_condition,
};