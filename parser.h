#ifndef PARSER_H
#define PARSER_H

//#include "adt.h"
#include "mempool.h"
#include "ljson_parser.h"
#include "scaner.h"

/****************************************************************************
 *
 *              Data structures.
 *
 ****************************************************************************
 */

/* state of parsing composite object */
typedef struct composite_state_tag composite_state_t;
struct composite_state_tag {
    obj_composite_t obj;
    int parse_state;
    composite_state_t* prev;
    composite_state_t* next;
};

typedef struct {
    composite_state_t parse_stack;
    scaner_t scaner;
    const char* err_msg;
    mempool_t* mempool;
    /* link the composite objects in a reverse nesting order. e.g
     *  Suppose Json is: [1, {"key":val}], the result is the linked list with
     *  1st element being the hashtab, and the second one being its enclosing
     *  array.
     */
    obj_t* result;
    int next_cobj_id; /* next composite object id */
} parser_t;

/****************************************************************************
 *
 *              Implementation of pstack_t
 *
 ****************************************************************************
 */
static inline composite_state_t*
pstack_top(parser_t* parser) {
    composite_state_t* ps = &parser->parse_stack;
    return ps->prev;
}

int pstack_push(parser_t*, obj_ty_t, int init_state);
composite_state_t* pstack_pop(parser_t*);

/****************************************************************************
 *
 *              Utilities
 *
 ****************************************************************************
 */
int emit_primitive_tk(mempool_t* mp, token_t* tk, obj_composite_t* nesting_obj);

void insert_subobj(obj_composite_t* nesting, obj_t* nested);

void __attribute__((format(printf, 2, 3), cold))
set_parser_err_fmt(parser_t* parser, const char* fmt, ...);

void __attribute__((cold)) set_parser_err(parser_t*, const char* str);

int start_parsing_array(parser_t*);
int start_parsing_hashtab(parser_t*);

int parse_hashtab(parser_t* parser);
int parse_array(parser_t* parser);

#endif /* PARSER_H */
