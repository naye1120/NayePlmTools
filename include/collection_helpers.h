#ifndef COLLECTION_HELPERS_H
#define COLLECTION_HELPERS_H

#include <stdint.h>
#include <stdlib.h>

#define COLLECTION_MAX_ITEMS ((size_t)4096)
#define COLLECTION_MAX_BYTES ((size_t)64 * 1024 * 1024)

/* Bound each collected array, including unused geometric capacity. */
static inline size_t collection_limit(size_t item_size) {
    if (!item_size) return 0;
    size_t limit = COLLECTION_MAX_BYTES / item_size;
    return limit < COLLECTION_MAX_ITEMS ? limit : COLLECTION_MAX_ITEMS;
}

/* Neither pointer nor capacity changes on allocation/limit failure. */
static inline void *collection_reserve(void *items, size_t *capacity,
                                     size_t needed, size_t item_size) {
    size_t limit = collection_limit(item_size);
    if (needed > limit) return NULL;
    if (needed <= *capacity) return items;
    size_t next = *capacity ? *capacity : (limit < 32 ? limit : 32);
    while (next < needed) next = next > limit / 2 ? limit : next * 2;
    void *grown = realloc(items, next * item_size);
    if (!grown) return NULL;
    *capacity = next;
    return grown;
}

#endif
