#ifndef PARSER_H
#define PARSER_H

#include "adt.h"
#include "mempool.h"
#include "ljson_parser.h"
#include "scaner.h"

/****************************************************************************
 *
 *              Data structures.
 *
 ****************************************************************************
 */

/* when come across a nesting composite object, and pop the stack when this
 * composite objects are successfully parsed and emitted.
 */
typedef struct {
    mempool_t* mempool;

    list_t stack;

    /* to recycle stack element (composite_state_t) */
    slist_t free_comp_state;

    /* to recycle the elements of composite_state_t::sub_objs.*/
    slist_t free_sub_objs;
} pstack_t;

/* state of parsing composite object */
typedef struct {
    int16_t obj_ty;      /* array/hashtab/root(dummy) */
    int16_t parse_state;
    int16_t begin_line;
    int16_t begin_colomn;
    slist_t sub_objs;    /* list of elements of this composiste objects */
} composite_state_t;

typedef struct {
    pstack_t parse_stack;
    scaner_t scaner;
    const char* err_msg;
    mempool_t* mempool;
    /* link the composite objects in a reverse nesting order. e.g
     *  Suppose Json is: [1, {"key":val}], the result is the linked list with
     *  1st element being the hashtab, and the second one being its enclosing
     *  array.
     */
    obj_t* result;

    /* last_emitted_obj points the last element of "result" (which is the singly
     * linked list).
     */
    composite_obj_t* last_emitted_cobj;

    int next_cobj_id; /* next composite object id */
} parser_t;

/****************************************************************************
 *
 *              Implementation of pstack_t
 *
 ****************************************************************************
 */
static inline void
pstack_init(pstack_t* ps, mempool_t* mp) {
    list_init(&ps->stack);
    ps->mempool = mp;

    slist_init(&ps->free_comp_state);
    slist_init(&ps->free_sub_objs);
}

static inline composite_state_t*
pstack_top(pstack_t* s) {
    list_t* l = &s->stack;
    if (l->size) {
        list_elmt_t* top = l->sentinel.next;
        composite_state_t* cs = LIST_ELMT_PAYLOAD(top, composite_state_t);
        return cs;
    }
    return 0;
}

int pstack_push(pstack_t* s, obj_ty_t, int init_state);
void pstack_pop(pstack_t* s);
static inline int pstack_empty(pstack_t* s) { return list_empty(&s->stack); }

/****************************************************************************
 *
 *              Utilities
 *
 ****************************************************************************
 */
int emit_primitive_tk(mempool_t* mp, token_t* tk, slist_t* sub_obj_list);

static inline int
insert_primitive_subobj(mempool_t* mp, slist_t* subobj_list, obj_t* subobj) {
    slist_elmt_t* subobj_elmt = MEMPOOL_ALLOC_TYPE(mp, slist_elmt_t);
    if (unlikely(!subobj_elmt))
        return 0;

    subobj_elmt->ptr_val = subobj;
    slist_prepend(subobj_list, subobj_elmt);
    return 1;
}

int insert_subobj(parser_t* parser, composite_obj_t* subobj);

void __attribute__((format(printf, 2, 3), cold))
    set_parser_err_fmt(parser_t* parser, const char* fmt, ...);

void __attribute__((cold)) set_parser_err(parser_t*, const char* str);

int start_parsing_array(parser_t*);
int start_parsing_hashtab(parser_t*);

int parse_hashtab(parser_t* parser);
int parse_array(parser_t* parser);

#endif /* PARSER_H */
