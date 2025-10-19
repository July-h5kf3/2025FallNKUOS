#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>
#include <pmm.h>

#define SLUB_MAX_SLABS 64

/* 单个 slab 结构 */
typedef struct {
    struct Page *page;              
    uint8_t used[128];              // 简单位图
    size_t free_cnt;                
} slab_t;

/* cache 结构：一类大小的对象 */
typedef struct {
    size_t obj_size;                
    slab_t slabs[SLUB_MAX_SLABS];   
    size_t slab_count;              
} slub_cache_t;

void *slub_alloc_size(size_t size);
void slub_free(void *obj);

extern const struct pmm_manager slub_pmm_manager;

#endif /* !__KERN_MM_SLUB_H__ */