#include <pmm.h>
#include <string.h>
#include <assert.h>
#include <memlayout.h>
#include <stdio.h>
#include <slub_pmm.h>
//先实现第一级：页级分配器，我们采用最初的first_fit算法
static free_area_t free_area;

#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

static void
default_init(void) {
    list_init(&free_list);
    nr_free = 0;
}

static void
default_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
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
}

static struct Page *
default_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    struct Page *page = NULL;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list) {
        struct Page *p = le2page(le, page_link);
        if (p->property >= n) {
            page = p;
            break;
        }
    }
    if (page != NULL) {
        list_entry_t* prev = list_prev(&(page->page_link));
        list_del(&(page->page_link));
        if (page->property > n) {
            struct Page *p = page + n;
            p->property = page->property - n;
            SetPageProperty(p);
            list_add(prev, &(p->page_link));
        }
        nr_free -= n;
        ClearPageProperty(page);
    }
    return page;
}

static void
default_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
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

    list_entry_t* le = list_prev(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (p + p->property == base) {
            p->property += base->property;
            ClearPageProperty(base);
            list_del(&(base->page_link));
            base = p;
        }
    }

    le = list_next(&(base->page_link));
    if (le != &free_list) {
        p = le2page(le, page_link);
        if (base + base->property == p) {
            base->property += p->property;
            ClearPageProperty(p);
            list_del(&(p->page_link));
        }
    }
}
//第二层：slab内的对象管理分配器
#define CACHE_NUM 3//总共有3种大小的缓存
#define MAX_SLABS 64//因为我们使用的不是链表，而是数组来管理每个cache的slab，所以需要设定一个最大值
static const size_t cache_sizes[CACHE_NUM]={32,64,128};//这3种cache的大小分别为32字节，64字节和128字节
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
static Cache caches[CACHE_NUM];//初始化cache全局数组

//初始化每个cache
static void slub_init(void){
    default_init();//先初始化第一层页级分配器
    memset(caches,0,sizeof(caches));//将全局的cache数组清零
    //将对应cache的对象大小属性进行设置
    for (int i=0;i<CACHE_NUM;i++) {
        caches[i].obj_size=cache_sizes[i];
        cprintf("[SLUB] Cache[%d] initialized, obj_size=%d bytes\n",
                i, cache_sizes[i]);
    }
}

//创建新的slab
static Slab *create_slab(Cache *cache){
    if (cache->slab_count>=MAX_SLABS){
        return NULL;
    }
    //先检查该cache中的slab数是否已到上限，没有的话再通过一级页分配器得到新的slab
    struct Page *page=default_alloc_pages(1);
    if (page==NULL){
        return NULL;
    }
    //对新的slab进行初始化
    Slab *slab=&cache->slabs[cache->slab_count++];
    slab->page=page;
    memset(slab->used,0,sizeof(slab->used));//所有对象标记为空闲
    slab->free_num=PGSIZE / cache->obj_size; // 计算该页能容纳的对象总数
    cprintf("[SLUB] Cache(obj_size=%dB) new slab #%d created, objs=%d\n",
            cache->obj_size, cache->slab_count - 1, slab->free_num);
    return slab;
}

//用户请求内存，分配器根据内存选择合适的cache
static Cache *select_cache(size_t size){
    for (int i=0;i<CACHE_NUM;i++){
        if (cache_sizes[i]>=size){
            return &caches[i];
        }
    }
    return NULL;//请求的内存大于我们实现的最大缓存128字节
}

//核心分配函数
void *slub_alloc_size(size_t size){
    if(size==0){
        return NULL;
        cprintf("[SLUB] Allocation failed: request %d bytes is nothing\n", size);
    }
    //根据size选择合适的cache
    Cache *cache=select_cache(size);
    if (cache==NULL){
        cprintf("[SLUB] Allocation failed: request %d bytes too large\n", size);
        return NULL; 
    }
    //遍历该cache下的slab，直到找到一个有空闲对象的slab
    for (size_t i=0;i<cache->slab_count;i++){
        Slab *slab=&cache->slabs[i];
        if (slab->free_num>0){
            //找到空闲slab了，遍历这个slab的所有对象，直至找到一个标志位为0的对象，使用它，将标志位改为1，并且free_num减一
            for (size_t j=0;j<PGSIZE / cache->obj_size;j++){
                if (slab->used[j]==0){
                    slab->used[j]=1;
                    slab->free_num--;
                    //计算并返回对象的虚拟地址
                    void *obj = (void *)((uintptr_t)page2kva(slab->page) + j * cache->obj_size);
                    cprintf("[SLUB] alloc obj=%p from cache(%dB) slab #%d (%d free left)\n",
                            obj, cache->obj_size, i, slab->free_num);
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
    void *obj=(void *)page2kva(newslab->page);
    cprintf("[SLUB] alloc obj=%p from cache(%dB) slab #%d (%d free left)\n",
                            obj, cache->obj_size, cache->slab_count-1, newslab->free_num);
    return obj;
}

//通过要释放的对象指针找到他所属的cache和slab
static Cache *get_cache(void *obj,Slab **slab_out){
    for (int i=0;i<CACHE_NUM;i++){
        Cache *cache=&caches[i];
        for(size_t k=0;k<cache->slab_count;k++){
            Slab *slab=&cache->slabs[k];
            uintptr_t base=(uintptr_t)page2kva(slab->page);
            // 判断对象指针是否落在该 slab 的地址范围内
            if ((uintptr_t)obj>=base && (uintptr_t)obj< base + PGSIZE){
                if (slab_out) *slab_out = slab;
                return cache;
            }
        }
    }
    return NULL;
}

//释放函数
void slub_free(void *obj) {
    if(obj==NULL){ return; }
    Slab *slab=NULL;
    Cache *cache=get_cache(obj,&slab);
    if (cache==NULL || slab==NULL) { 
        cprintf("[SLUB] WARNING: invalid free @%p (not found)\n", obj);
        return; 
    }
    uintptr_t base=(uintptr_t)page2kva(slab->page);
    size_t idx=((uintptr_t)obj - base) / cache->obj_size;

    if (idx < PGSIZE / cache->obj_size && slab->used[idx]){//这里确保了不会二次释放
        slab->used[idx] = 0;// 标记为空闲
        slab->free_num++;// 增加空闲计数
        cprintf("[SLUB] free obj=%p from cache(%dB) slab #%d (%d free left)\n",
                obj, cache->obj_size,
                (size_t)(slab - cache->slabs), slab->free_num);
    }
}
//与pmm.c兼容的接口
static void
slub_init_memmap(struct Page *base, size_t n){
    default_init_memmap(base, n); 
}

static struct Page *
slub_alloc_pages(size_t n){
    return default_alloc_pages(n); 
}

static void 
slub_free_pages(struct Page *base, size_t n){
    default_free_pages(base, n); 
}
static size_t slub_nr_free_pages(void){
    return nr_free; 
}
static void slub_check(void) {
    cprintf("\n========== SLUB Allocator Basic Check ==========\n");

    void *a1, *a2, *a3, *b1, *b2, *b3;

    // 基础分配测试
    cprintf("[Test1] Basic allocation:\n");
    a1 = slub_alloc_size(32);
    a2 = slub_alloc_size(64);
    a3 = slub_alloc_size(128);
    assert(a1 && a2 && a3);
    cprintf("  Alloc OK: 32B=%p, 64B=%p, 128B=%p\n", a1, a2, a3);

    //基础释放测试
    cprintf("[Test2] Basic free:\n");
    slub_free(a1);
    slub_free(a2);
    slub_free(a3);
    cprintf("  Free OK (all three released)\n");

    //  边界测试
    cprintf("[Test3] Boundary cases:\n");
    void *null_alloc = slub_alloc_size(0);
    void *too_large  = slub_alloc_size(4096);
    assert(null_alloc == NULL && too_large == NULL);
    cprintf("  Boundary OK (size=0 and 4KB+ rejected)\n");

    // 部分释放 + 复用测试
    cprintf("[Test4] Partial reuse:\n");
    b1 = slub_alloc_size(64);
    b2 = slub_alloc_size(64);
    b3 = slub_alloc_size(64);
    cprintf("  Allocated 3 objects: %p, %p, %p\n", b1, b2, b3);
    slub_free(b2);
    cprintf("  Freed middle one (%p)\n", b2);
    void *b4 = slub_alloc_size(64);
    cprintf("  Reallocated: %p\n", b4);
    assert(b4 == b2);  // 复用同一位置
    cprintf("  Partial reuse OK\n");

    // 混合分配与释放测试
    cprintf("[Test5] Mixed scenario:\n");
    void *m1 = slub_alloc_size(32);
    void *m2 = slub_alloc_size(128);
    void *m3 = slub_alloc_size(64);
    cprintf("  Mixed alloc: %p, %p, %p\n", m1, m2, m3);
    slub_free(m2);
    slub_free(m1);
    void *m4 = slub_alloc_size(32);
    cprintf("  After reuse alloc(32B): %p\n", m4);
    cprintf("  Mixed alloc/free OK\n");

    //  异常释放测试
    cprintf("[Test6] Invalid free:\n");
    slub_free((void*)0xDEADBEEF);
    cprintf("  Invalid free handled safely.\n");

    cprintf("========== SLUB Basic Check PASSED ==========\n\n");
}

const struct pmm_manager slub_pmm_manager = {
    .name = "slub_pmm_manager_multi_cache",
    .init = slub_init,
    .init_memmap = slub_init_memmap,
    .alloc_pages = slub_alloc_pages,
    .free_pages = slub_free_pages,
    .nr_free_pages = slub_nr_free_pages,
    .check = slub_check,
};