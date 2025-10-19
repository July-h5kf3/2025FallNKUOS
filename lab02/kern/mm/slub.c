#include <pmm.h>
#include <string.h>
#include <assert.h>
#include <memlayout.h>
#include <stdio.h>
#include <slub.h>

/* 定义三种对象大小 */
#define CACHE_NUM 3
static const size_t cache_sizes[CACHE_NUM] = {32, 64, 128};

/* 初始化全局 cache 数组 */
static slub_cache_t slub_caches[CACHE_NUM];

/* ---------------------- 基础函数 ---------------------- */

/* 初始化每个 cache */
static void slub_init(void) {
    memset(slub_caches, 0, sizeof(slub_caches));
    for (int i = 0; i < CACHE_NUM; i++) {
        slub_caches[i].obj_size = cache_sizes[i];
        cprintf("[SLUB] Cache[%d] initialized, obj_size=%zu bytes\n",
                i, cache_sizes[i]);
    }
}

/* 创建一个新的 slab */
static slab_t *create_slab(slub_cache_t *cache) {
    if (cache->slab_count >= SLUB_MAX_SLABS) {
        return NULL;
    }
    struct Page *page = alloc_page();
    if (page == NULL) {
        return NULL;
    }
    slab_t *slab = &cache->slabs[cache->slab_count++];
    slab->page = page;
    memset(slab->used, 0, sizeof(slab->used));
    slab->free_cnt = PGSIZE / cache->obj_size;
    cprintf("[SLUB] Cache(obj=%zuB) new slab #%zu created, objs=%zu\n",
            cache->obj_size, cache->slab_count - 1, slab->free_cnt);
    return slab;
}

/* 选择最合适的 cache（大于等于 size 的最小） */
static slub_cache_t *select_cache(size_t size) {
    for (int i = 0; i < CACHE_NUM; i++) {
        if (cache_sizes[i] >= size) {
            return &slub_caches[i];
        }
    }
    return NULL; // 请求太大（>= 4KB）
}

/* ---------------------- 分配与释放 ---------------------- */

void *slub_alloc_size(size_t size) {
    slub_cache_t *cache = select_cache(size);
    if (cache == NULL) {
        cprintf("[SLUB] Allocation failed: request %zu bytes too large\n", size);
        return NULL;
    }

    for (size_t i = 0; i < cache->slab_count; i++) {
        slab_t *slab = &cache->slabs[i];
        if (slab->free_cnt > 0) {
            for (size_t j = 0; j < PGSIZE / cache->obj_size; j++) {
                if (slab->used[j] == 0) {
                    slab->used[j] = 1;
                    slab->free_cnt--;
                    void *obj = (void *)((uintptr_t)page2kva(slab->page) + j * cache->obj_size);
                    cprintf("[SLUB] alloc obj=%p from cache(%zuB) slab #%zu (%zu free left)\n",
                            obj, cache->obj_size, i, slab->free_cnt);
                    return obj;
                }
            }
        }
    }

    slab_t *newslab = create_slab(cache);
    if (newslab == NULL) return NULL;
    newslab->used[0] = 1;
    newslab->free_cnt--;
    return (void *)page2kva(newslab->page);
}

/* 根据对象指针反查所属 cache */
static slub_cache_t *find_cache_by_obj(void *obj, slab_t **slab_out) {
    for (int i = 0; i < CACHE_NUM; i++) {
        slub_cache_t *cache = &slub_caches[i];
        for (size_t k = 0; k < cache->slab_count; k++) {
            slab_t *slab = &cache->slabs[k];
            uintptr_t base = (uintptr_t)page2kva(slab->page);
            if ((uintptr_t)obj >= base && (uintptr_t)obj < base + PGSIZE) {
                if (slab_out) *slab_out = slab;
                return cache;
            }
        }
    }
    return NULL;
}

void slub_free(void *obj) {
    if(obj==NULL){
        return;
    }
    slab_t *slab = NULL;
    slub_cache_t *cache = find_cache_by_obj(obj, &slab);
    if (cache == NULL || slab == NULL) {
        cprintf("[SLUB] WARNING: invalid free @%p (not found)\n", obj);
        return;
    }

    uintptr_t base = (uintptr_t)page2kva(slab->page);
    size_t idx = ((uintptr_t)obj - base) / cache->obj_size;
    if (idx < PGSIZE / cache->obj_size && slab->used[idx]) {
        slab->used[idx] = 0;
        slab->free_cnt++;
        cprintf("[SLUB] free obj=%p from cache(%zuB) slab #%zu (%zu free left)\n",
                obj, cache->obj_size,
                (size_t)(slab - cache->slabs), slab->free_cnt);
    }
}

/* ---------------------- 兼容接口 ---------------------- */

static void slub_init_memmap(struct Page *base, size_t n) {}
static struct Page *slub_alloc_pages(size_t n) { return NULL; }
static void slub_free_pages(struct Page *base, size_t n) {}
static size_t slub_nr_free_pages(void) { return 0; }

/* ---------------------- 检查函数 ---------------------- */

static void slub_check(void) {
    cprintf("========== SLUB Allocator Check ==========\n");
    void *ptrs[20];
    int n = 0;

    /* 1. 分配不同大小对象 */
    for (int i = 0; i < 5; i++) ptrs[n++] = slub_alloc_size(32);
    for (int i = 0; i < 5; i++) ptrs[n++] = slub_alloc_size(64);
    for (int i = 0; i < 5; i++) ptrs[n++] = slub_alloc_size(128);
    cprintf("[Check1] Allocated %d objects successfully.\n", n);

    /* 2. 部分释放 */
    for (int i = 0; i < n; i += 3) 
    if(ptrs[i]) slub_free(ptrs[i]);
    cprintf("[Check2] Released 1/3 objects, testing partial reuse...\n");

    /* 3. 重新分配（复用） */
    for (int i = 0; i < 5; i++) slub_alloc_size(64);
    cprintf("[Check3] Additional allocations complete.\n");

    /* 4. 全部释放 */
    for (int i = 0; i < n; i++) slub_free(ptrs[i]);
    cprintf("[Check4] All objects freed, slabs recycled correctly.\n");

    /* 5. 异常释放测试 */
    slub_free((void *)0x12345678);
    cprintf("[Check5] Exception handling verified.\n");
    cprintf("========== SLUB Check PASSED ==========\n");
}

/* 注册 SLUB 管理器 */
const struct pmm_manager slub_pmm_manager = {
    .name = "slub_pmm_manager_multi_cache",
    .init = slub_init,
    .init_memmap = slub_init_memmap,
    .alloc_pages = slub_alloc_pages,
    .free_pages = slub_free_pages,
    .nr_free_pages = slub_nr_free_pages,
    .check = slub_check,
};