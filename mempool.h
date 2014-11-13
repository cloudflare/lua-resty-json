#ifndef MEM_POOL_H
#define MEM_POOL_H

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

#define MEMPOOL_ALLOC_TYPE(mp, t) ((t*)mp_alloc((mp), sizeof(t)))
#define MEMPOOL_ALLOC_TYPE_N(mp, t, n) ((t*)mp_alloc((mp), sizeof(t) * (n)))

#endif
