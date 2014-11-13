#ifndef ADT_H
#define ADT_H

#include <stdint.h>
#include "util.h"
/****************************************************************************
 *
 *              Singly linked list
 *
 ****************************************************************************
 */
struct slist_elmt_tag;
typedef struct slist_elmt_tag slist_elmt_t;

struct slist_elmt_tag {
    slist_elmt_t* next;
    union {
        int64_t int_val;
        void* ptr_val;
    };
};

#define SLIST_ELMT_PAYLOAD(e, t) \
    ({char* p = offsetof(slist_elmt_t, int_val) + (char*)(e); (t*)(void*)p;})

static inline uint32_t
calc_slist_elmt_size(uint32_t payload_sz) {
    uint32_t s = offsetof(slist_elmt_t, int_val) + payload_sz;
    uint32_t align = __alignof__(slist_elmt_t);

    s = (align - 1 + s) & ~(align - 1);
    if (s < sizeof(slist_elmt_t))
        s = sizeof(slist_elmt_t);

    return s;
}

typedef struct {
    slist_elmt_t* first;
    slist_elmt_t* last;
    int size; /* the number of element */
} slist_t;

static inline void
slist_init(slist_t *l) {
    l->first = l->last = 0;
    l->size = 0;
}

static inline void
slist_prepend(slist_t* l, slist_elmt_t* e) {
    e->next = l->first;
    l->first = e;

    if (!l->last) {
        l->last = e;
    }

    l->size ++;
}

static inline slist_elmt_t*
slist_delete_first(slist_t* l) {
    slist_elmt_t* first = l->first;
    if (first) {
        l->first = first->next;
        if (!l->first) {
            l->last = 0;
        }
    }
    return first;
}

static inline void
slist_splice(slist_t* dest, slist_t* src) {
    if (dest->last) {
        if (src->first) {
            dest->last->next = src->first;
            dest->last = src->last;
        }
    } else {
        dest->first = src->first;
        dest->last = src->last;
    }

    src->first = src->last = 0;
    dest->size += src->size;
}

static inline int
slist_empty(slist_t* l) {
    return l->first == 0;
}

/****************************************************************************
 *
 *              doubly-linked list
 *
 ****************************************************************************
 */
struct list_elmt_tag;
typedef struct list_elmt_tag list_elmt_t;

struct list_elmt_tag {
    list_elmt_t* prev;
    list_elmt_t* next;
    union {
        int64_t int_val;
        void* ptr_val;
    };
};

static inline uint32_t
list_elmt_size(uint32_t payload_sz) {
    uint32_t t = offsetof(list_elmt_t, int_val);
    t += payload_sz;

    uint32_t align = __alignof__(list_elmt_t);
    t += align - 1;
    t = t & ~(align - 1);

    return t;
}

#define LIST_ELMT_PAYLOAD(e, t) \
    ({char* p = offsetof(list_elmt_t, int_val) + (char*)(e); (t*)(void*)p;})

typedef struct {
    list_elmt_t sentinel;
    int size;
} list_t;

static inline void
list_init(list_t* l) {
    l->sentinel.prev = l->sentinel.next = &l->sentinel;
    l->size = 0;
}

/* Insert the given element right after the given position */
static inline void
list_insert_after(list_t* l, list_elmt_t* pos, list_elmt_t* elmt) {
    list_elmt_t* next = pos->next;

    elmt->next = next;
    elmt->prev = pos;

    pos->next = elmt;
    next->prev = elmt;

    l->size++;
}

static inline void
list_delete(list_t* l, list_elmt_t* elmt) {
    list_elmt_t* prev = elmt->prev;
    list_elmt_t* next = elmt->next;

    prev->next = next;
    next->prev = prev;
    l->size --;
}

static inline void
list_splice(list_t* d, list_t* s) {
    if (s->size != 0) {
        list_elmt_t* d_last = d->sentinel.prev;
        list_elmt_t* s_first = s->sentinel.next;
        list_elmt_t* s_last = s->sentinel.prev;

        d_last->next = s_first;
        s_first->prev = d_last;

        d->sentinel.prev = s_last;
        d->size += s->size;
    }
}

static inline int
list_empty(list_t* l) {
    return l->size == 0;
}

#endif
