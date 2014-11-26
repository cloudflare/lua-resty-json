/* ****************************************************************************
 *
 *   The mempool is used to speedup memory allocation. Mempool allocate pape-size
 * "chunks" by calling malloc(), and the subsequent memory allocation is done by
 * carving block from the these chunks. A block dose not have management overhead,
 * and mempool dose not try to reclaim individual block, but it can free all the
 * blocks in one stroke.
 *
 *  The interface functions are:
 *  =============================
 *  o. mp_create : create a memory pool instance.
 *  o. mp_destroy: destroy the memory pool instance.
 *  o. mp_alloc(mempool, size) : allocate a block having at least "size"-byte.
 *                               block is 8-byte aligned.
 *  o. mp_free_all() : free all blocks allocated so far.
 *
 * ****************************************************************************
 */
#ifndef MEM_POOL_H
#define MEM_POOL_H

/* A chunk is typically 4k-byte in size; the management structure resides at
 * the beginning of the chunk.
 */
typedef struct chunk_hdr chunk_hdr_t;
struct chunk_hdr {
    chunk_hdr_t* next;
    char* chunk_end;
    char* free;
};

struct mempool;
typedef struct mempool mempool_t;
struct mempool {
    chunk_hdr_t chunk_hdr;
    chunk_hdr_t* last;
};

#define DEFAULT_ALIGN 8

/* create a mempool */
mempool_t* mp_create();

/* destroy the mempool*/
void mp_destroy(mempool_t*);

/* Free all blocks allocated by the mempool */
void mp_free_all(mempool_t*);

/* Allocate a block of "size" bytes. Default alignment is 8-byte. */
static inline void*
mp_alloc(mempool_t* mp, int size) {
    size = (size + DEFAULT_ALIGN - 1) & ~(DEFAULT_ALIGN - 1);

    chunk_hdr_t* chunk = mp->last;
    char* free_addr = chunk->free;
    char* free_end = chunk->chunk_end;

    if (free_addr + size <= free_end) {
        char* t = free_addr;
        chunk->free += size;
        return (void*)t;
    }

    void* mp_alloc_slow(mempool_t* mp, int size);
    return mp_alloc_slow(mp, size);
}

/* To allocate a block of type "t" */
#define MEMPOOL_ALLOC_TYPE(mp, t) ((t*)mp_alloc((mp), sizeof(t)))

/* To allocate an array with "n" elements. Elements are of type "t".*/
#define MEMPOOL_ALLOC_TYPE_N(mp, t, n) ((t*)mp_alloc((mp), sizeof(t) * (n)))

#endif
