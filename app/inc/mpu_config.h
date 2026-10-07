/**
 * @file  mpu_config.h
 * @brief MPU setup and cache-maintenance helpers.
 */
#ifndef MPU_CONFIG_H
#define MPU_CONFIG_H

#include <stddef.h>
#include <stdint.h>

void mpu_config(void);

/*
 * Cache coherency for DMA buffers. The Cortex-M4 in the F407 has no data cache (the ART
 * accelerator only caches flash reads), so on this target these are no-ops. They are
 * called at every DMA hand-over point anyway so the code is correct when ported to a
 * Cortex-M7 part (F7/H7) where the D-cache would otherwise return stale DMA data.
 */
static inline void cache_clean(const void *addr, size_t len)       /* CPU wrote -> DMA reads */
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_CleanDCache_by_Addr((uint32_t *)((uintptr_t)addr & ~31U), (int32_t)(len + 32U));
#else
    (void)addr; (void)len;
#endif
}

static inline void cache_invalidate(const void *addr, size_t len)  /* DMA wrote -> CPU reads */
{
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    SCB_InvalidateDCache_by_Addr((uint32_t *)((uintptr_t)addr & ~31U), (int32_t)(len + 32U));
#else
    (void)addr; (void)len;
#endif
}

#endif /* MPU_CONFIG_H */
