#include <stddef.h>
#include "slab.h"
#include "pmm.h"
#include "serial.h"
#include "memory.h"

/* -----------------------------------------------
 * Slab Allocator Implementation
 *
 * Each cache manages a linked list of slab pages.
 * Each slab page contains N fixed-size objects
 * tracked in a bitmap.
 * ----------------------------------------------- */

/* Internal slab page structure */
typedef struct slab_page {
    uint32_t          magic;            /* Validation magic */
    struct slab_page* next;             /* Next slab in the cache chain */
    uint32_t          free_count;       /* Number of free objects on this slab */
    uint32_t          total_count;      /* Total objects on this slab */
    uint8_t           bitmap[32];       /* 256-bit bitmap (tracking up to 256 objects) */
    uint8_t           data[];           /* Objects start here */
} __attribute__((aligned(4))) slab_page_t;

/* Internal slab cache structure */
struct slab_cache {
    uint32_t        magic;              /* Validation magic (SLAB_MAGIC) */
    size_t          object_size;        /* Size of each object */
    size_t          alignment;          /* Required alignment */
    size_t          stride;             /* object_size rounded up to alignment */
    slab_page_t*    partial_list;       /* Slabs with some free objects */
    slab_page_t*    full_list;          /* Slabs with no free objects */
    uint32_t        objects_per_slab;   /* Objects per slab page */
    uint32_t        slab_size;          /* Total size of one slab (PAGE_SIZE) */
};

/* Pre-defined kernel caches */
static slab_cache_t* small_cache   = NULL;
static slab_cache_t* medium_cache  = NULL;
static slab_cache_t* large_cache   = NULL;

/* All registered caches */ 
static slab_cache_t* caches[MAX_SLAB_CACHES];
static int           cache_count = 0;

/* Initialize the slab subsystem */
void slab_init(void) {
    write_serial("[SLAB] Initializing slab allocator...\r\n");

    for (int i = 0; i < MAX_SLAB_CACHES; i++) {
        caches[i] = NULL;
    }
    cache_count = 0;

    /* Create the three standard kernel caches */
    small_cache  = slab_cache_create(64,    8);
    medium_cache = slab_cache_create(256,   8);
    large_cache  = slab_cache_create(2048,  8);

    if (small_cache && medium_cache && large_cache) {
        write_serial("[SLAB] Caches: small(64B), medium(256B), large(2KB) ready\r\n");
    } else {
        write_serial("[SLAB] WARNING: Some caches failed to create\r\n");
    }

    write_serial("[SLAB] Slab allocator initialized.\r\n");
}

/* Create a new slab cache */
slab_cache_t* slab_cache_create(size_t object_size, size_t alignment) {
    if (object_size > SLAB_MAX_OBJECT || object_size < 8) return NULL;
    if (cache_count >= MAX_SLAB_CACHES) return NULL;

    /* Allocate cache structure from PMM (1 page) */
    slab_cache_t* cache = (slab_cache_t*)pmm_alloc_frame();
    if ((uint32_t)cache == (uint32_t)-1) return NULL;

    cache->magic = SLAB_MAGIC;
    cache->object_size = object_size;
    cache->alignment = (alignment < 8) ? 8 : alignment;
    cache->stride = (object_size + cache->alignment - 1) & ~(cache->alignment - 1);
    cache->partial_list = NULL;
    cache->full_list = NULL;

    /* Calculate how many objects fit in one page minus overhead */
    size_t overhead = sizeof(slab_page_t);
    cache->objects_per_slab = (PAGE_SIZE - overhead) / cache->stride;
    if (cache->objects_per_slab > 256) cache->objects_per_slab = 256;
    if (cache->objects_per_slab == 0) cache->objects_per_slab = 1;
    cache->slab_size = PAGE_SIZE;

    caches[cache_count++] = cache;

    write_serial("[SLAB] Cache created: ");
    write_serial_hex(object_size);
    write_serial("B -> ");
    write_serial_hex(cache->objects_per_slab);
    write_serial(" objects/slab\r\n");

    return cache;
}

/* Allocate a new slab page and add it to the cache */
static slab_page_t* slab_grow(slab_cache_t* cache) {
    void* page = pmm_alloc_frame();
    if ((uint32_t)page == (uint32_t)-1) return NULL;

    slab_page_t* slab = (slab_page_t*)page;
    slab->next = NULL;
    slab->free_count = cache->objects_per_slab;
    slab->total_count = cache->objects_per_slab;

    /* Clear bitmap: all objects free */
    for (int i = 0; i < 32; i++) {
        slab->bitmap[i] = 0;
    }

    /* Add to partial list */
    slab->next = cache->partial_list;
    cache->partial_list = slab;

    return slab;
}

/* Allocate an object from a slab cache */
void* slab_cache_alloc(slab_cache_t* cache) {
    if (!cache || cache->magic != SLAB_MAGIC) return NULL;

    /* Find a slab with free objects */
    slab_page_t* slab = cache->partial_list;
    if (!slab) {
        /* No partial slab — grow a new one */
        slab = slab_grow(cache);
        if (!slab) return NULL;
    }

    /* Find the first free object in the bitmap */
    uint32_t idx = 0;
    for (uint32_t i = 0; i < slab->total_count; i++) {
        uint32_t byte = i / 8;
        uint32_t bit  = i % 8;
        if (!(slab->bitmap[byte] & (1 << bit))) {
            idx = i;
            slab->bitmap[byte] |= (1 << bit);
            break;
        }
    }

    slab->free_count--;

    /* If slab is now full, move to full list */
    if (slab->free_count == 0) {
        /* Remove from partial list */
        slab_page_t** prev = &cache->partial_list;
        while (*prev != slab) prev = &(*prev)->next;
        *prev = slab->next;

        /* Add to full list */
        slab->next = cache->full_list;
        cache->full_list = slab;
    }

    /* Return pointer to the object */
    return (void*)((uint8_t*)slab + offsetof(slab_page_t, data) + idx * cache->stride);
}

/* Free an object back to its slab cache */
void slab_cache_free(slab_cache_t* cache, void* obj) {
    if (!cache || !obj || cache->magic != SLAB_MAGIC) return;

    /* Find which slab page this object belongs to */
    uint32_t obj_addr = (uint32_t)obj;
    uint32_t page_addr = obj_addr & ~(PAGE_SIZE - 1);
    slab_page_t* slab = (slab_page_t*)page_addr;

    if (slab->magic != 0 && slab->magic != SLAB_MAGIC) {
        return; /* Not a valid slab page (might be from partial init) */
    }

    /* Calculate object index */
    uint8_t* data_start = (uint8_t*)slab + offsetof(slab_page_t, data);
    uint32_t offset = (uint8_t*)obj - data_start;
    uint32_t idx = offset / cache->stride;

    if (idx >= slab->total_count) return;

    /* Clear bitmap bit */
    uint32_t byte = idx / 8;
    uint32_t bit  = idx % 8;
    if (!(slab->bitmap[byte] & (1 << bit))) return; /* Double-free */

    slab->bitmap[byte] &= ~(1 << bit);
    slab->free_count++;

    /* If slab was full, move to partial list */
    if (slab->free_count == 1) {
        /* Remove from full list */
        slab_page_t** prev = &cache->full_list;
        while (*prev != slab) prev = &(*prev)->next;
        *prev = slab->next;

        /* Add to partial list */
        slab->next = cache->partial_list;
        cache->partial_list = slab;
    }
}

void slab_cache_destroy(slab_cache_t* cache) {
    if (!cache || cache->magic != SLAB_MAGIC) return;

    /* Free all slab pages */
    slab_page_t* slab = cache->partial_list;
    while (slab) {
        slab_page_t* next = slab->next;
        pmm_free_frame(slab);
        slab = next;
    }

    slab = cache->full_list;
    while (slab) {
        slab_page_t* next = slab->next;
        pmm_free_frame(slab);
        slab = next;
    }

    /* Free the cache structure itself */
    pmm_free_frame(cache);
}

slab_cache_t* slab_get_small_cache(void)   { return small_cache; }
slab_cache_t* slab_get_medium_cache(void)  { return medium_cache; }
slab_cache_t* slab_get_large_cache(void)   { return large_cache; }
