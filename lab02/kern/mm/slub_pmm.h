#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>
#include <pmm.h>

void *slub_alloc_size(size_t size);
void slub_free(void *obj);

extern const struct pmm_manager slub_pmm_manager;

#endif /* !__KERN_MM_SLUB_H__ */