#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "mempool.h"
#include "util.h"

#define MAX_ALIGN 32

static int
default_chunk_sz() {
    static int page_sz;
    if (!page_sz) {
        page_sz = sysconf(_SC_PAGESIZE);
        /* Adjust by malloc overhead in an attemp to fit the chunk in a page*/
        page_sz -= 64;
    }
    return page_sz;
}

static void
align_free_pointer(chunk_hdr_t* chunk_hdr, int align) {
    char* p = chunk_hdr->free;
    p = (char*)(((intptr_t)p) & ~(align - 1));
    chunk_hdr->free = p;
}

/* Allocate a chunk which can accommodate an object of given size. If size
 * is not specified (i.e. size = 0), default size is used.
 */
static chunk_hdr_t*
alloc_chunk(int size) {
    int s = default_chunk_sz();
    if (!size)
        size = s;
    else {
        size += sizeof(mempool_t) + MAX_ALIGN;
        if (size < s)
            size = s;
    }

    char* blk = (char*) malloc(size);
    chunk_hdr_t* chunk_hdr = (chunk_hdr_t*)blk;
    chunk_hdr->next = NULL;
    chunk_hdr->chunk_end = blk + size;
    chunk_hdr->free = blk + sizeof(chunk_hdr_t);
    align_free_pointer(chunk_hdr, DEFAULT_ALIGN);

    return chunk_hdr;
}

/* Allocate a new chunk and add it to the mempool, return 1 on success,
 * 0 otherwise.
 */
static int
add_a_chunk(mempool_t* mp, int size) {
    chunk_hdr_t* new_chunk = alloc_chunk(0);
    if (!new_chunk)
        return 0;

    if (!mp->chunk_hdr.next) {
        ASSERT(mp->last == &mp->chunk_hdr);
        mp->chunk_hdr.next = new_chunk;
    }

    mp->last->next = new_chunk;
    mp->last = new_chunk;
    ASSERT(new_chunk->chunk_end - new_chunk->free >= size);

    return 1;
}

mempool_t*
mp_create() {
    chunk_hdr_t* chunk_hdr = alloc_chunk(0);
    if (!chunk_hdr)
        return NULL;

    chunk_hdr->free = sizeof(struct mempool) + (char*)(void*)chunk_hdr;
    align_free_pointer(chunk_hdr, DEFAULT_ALIGN);

    mempool_t* mp = (mempool_t*)(void*)chunk_hdr;
    mp->last = chunk_hdr;

    return mp;
}

/* the slow-path of mp_alloc() */
void*
mp_alloc_slow(mempool_t* mp, int size) {
    if (unlikely(add_a_chunk(mp, size) == 0))
        return NULL;
    return mp_alloc(mp, size);
}

void
mp_destroy(mempool_t* mp) {
    chunk_hdr_t* iter = mp->chunk_hdr.next;
    while (iter) {
        chunk_hdr_t* next = iter->next;
        free((void*)iter);
        iter = next;
    };

    free((void*)mp);
}

/* Free all blocks allocated so far */
void
mp_free_all(mempool_t* mp) {
    chunk_hdr_t* iter;

    for (iter = mp->chunk_hdr.next; iter != 0;) {
        chunk_hdr_t* next = iter->next;
        free((void*)iter);
        iter = next;
    }

    chunk_hdr_t* chunk = &mp->chunk_hdr;
    chunk->next = 0;
    mp->last = chunk;

    chunk->free = sizeof(mempool_t) + (char*)(void*)chunk;
    align_free_pointer(chunk, DEFAULT_ALIGN);
}
