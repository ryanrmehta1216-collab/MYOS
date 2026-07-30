#ifndef SLAB_H
#define SLAB_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------
 * Slab Allocator — Fixed-size kernel object cache
 *
 * Supplements the existing kmalloc heap allocator
 * for fixed-size kernel objects (task structs, file
 * descriptors, network buffers, etc.).
 *
 * Each slab cache manages a list of fixed-size objects.
 * Objects are allocated from pre-allocated pages
 * to avoid external fragmentation.
 * ----------------------------------------------- */

/* Magic number for slab validation */
#define SLAB_MAGIC  0x5AB1E0B0

/* Maximum number of slab caches */
#define MAX_SLAB_CACHES 32

/* Maximum object size for slab allocation */
#define SLAB_MAX_OBJECT  2048

/* Opaque slab cache type */
typedef struct slab_cache slab_cache_t;

/* Create a new slab cache for objects of the given size.
 * object_size: size of each object in bytes (aligned to 8)
 * alignment: required alignment (0 = use natural alignment)
 * Returns: pointer to cache, or NULL on failure */
slab_cache_t* slab_cache_create(size_t object_size, size_t alignment);

/* Allocate an object from the slab cache */
void* slab_cache_alloc(slab_cache_t* cache);

/* Free an object back to the slab cache */
void slab_cache_free(slab_cache_t* cache, void* obj);

/* Destroy a slab cache (frees all pages) */
void slab_cache_destroy(slab_cache_t* cache);

/* Initialize the slab allocator subsystem */
void slab_init(void);

/* ---- Pre-defined kernel caches ---- */

/* Get the general-purpose small-object cache (< 64 bytes) */
slab_cache_t* slab_get_small_cache(void);

/* Get the medium-object cache (64-256 bytes) */
slab_cache_t* slab_get_medium_cache(void);

/* Get the large-object cache (256-2048 bytes) */
slab_cache_t* slab_get_large_cache(void);

#endif /* SLAB_H */
