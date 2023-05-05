/* ************************************************************************
 *
 *  This file implements the json parser.
 *
 * Working example and terminology
 * ================================
 * We use following json to depict
 * how it works:
 *   [1, 2, {"key": 3.4}]
 *
 *   We call [...] as *array*, and {...} as *hashtab*. Array and hashtab are
 * *composite objects*, and number/string/boolean/null are *primitive objects*.
 *
 *  This json snippet has two composite objects:
 *   - O2: is a hash-table having only one element with key being "key", and
 *         value being 3.4.
 *   - O1: is a array containing three elements, i.e. 1, 2 and O2.
 *
 *  O2 is *nested* in O1, and O1 is O2's *immediate nesting* composite object.
 *
 * How it works
 * =============
 * The parser walks the input json from left to right, calling scaner to get a
 * token at a time. The scaner recognizes following tokens in order:
 *
 *   token type           value
 *   ---------------------------
 *     char               '['
 *     number              1
 *     char               ','
 *     number              2
 *     char               ','
 *     cahr               '{'
 *     string             "key"
 *     ....
 *
 *   At the heart of the parser is a *parsing-stack*, which push a level when
 * seeing the starting delimiter of a composite object (e.g. seeing '[' of
 * an array), and pop until the closing delimiter of the same composite object
 * is seen). So, the parse-stack is in essence mimicking the nesting
 * relationship. Actually in our implementation, the stack element contains
 * a data structure keeping track of the current composite object being
 * processed.
 *
 *  The result of the parser is organized in reverse-nesting order linked
 * in a singly-linked list. See the comment to jp_parse() in ljson_parser.h
 * for details.
 *
 * ************************************************************************
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>

#include "util.h"
#include "mempool.h"
#include "scaner.h"
#include "parser.h"

#ifdef DEBUG
static int verfiy_reverse_nesting_order(obj_t* parse_result);
#endif
/* **************************************************************************
 *
 *              About parse-stack.
 *
 * **************************************************************************
 */
static inline void
init_obj(obj_t* obj, obj_ty_t ty) {
    obj->next = 0;
    obj->obj_ty = ty;
    obj->elmt_num = 0;
}

static inline void
init_composite_obj(obj_composite_t* obj, obj_ty_t ty, uint32_t id) {
    init_obj(&obj->common, ty);
    obj->subobjs = 0;
    obj->id = id;
}

static inline composite_state_t*
alloc_composite_state(parser_t* parser) {
    composite_state_t* cs;
    cs = MEMPOOL_ALLOC_TYPE(parser->mempool, composite_state_t);
    return cs;
}

static void
pstack_init(parser_t* parser) {
    composite_state_t* cs = &parser->parse_stack;
    init_composite_obj(&cs->obj, OT_ROOT, 0);

    cs->next = 0;
    cs->prev = cs;  /* this is *top* */
}

int
pstack_push(parser_t* parser, obj_ty_t obj_ty, int init_state) {
    /* Step 1: Allocate an stack element */
    composite_state_t* cs = alloc_composite_state(parser);
    if (unlikely(!cs))
        return 0;

    /* Step 2: Initialize the corresponding composite object. */
    obj_composite_t* cobj = &cs->obj;
    init_composite_obj(cobj, obj_ty, parser->next_cobj_id++);

    /* link the composite objects in reverse-nesting order */
    cobj->reverse_nesting_order = (obj_composite_t*)(void*)parser->result;
    parser->result = &cobj->common;

    /* Step 3: Push one level */
    cs->parse_state = init_state;
    cs->next = 0;

    composite_state_t* root = &parser->parse_stack;
    composite_state_t* top = root->prev;
    cs->prev = top;
    root->prev = cs; /* update the "top" */

    return 1;
}

composite_state_t*
pstack_pop(parser_t* parser) {
    composite_state_t* ps = &parser->parse_stack;
    composite_state_t* top = ps->prev;

    composite_state_t* new_top = top->prev;
    new_top->next = 0;
    ps->prev = new_top;

    return new_top;
}

/***************************************************************************
 *
 *                  Emit Objects
 *
 ***************************************************************************
 */

/* Convert the primitive token to primitive object */
static inline obj_t*
cvt_primitive_tk(mempool_t* mp, token_t* tk) {
    ASSERT(tk_is_primitive(tk));
    obj_primitive_t* obj = MEMPOOL_ALLOC_TYPE(mp, obj_primitive_t);
    if (unlikely(!obj))
        return 0;

    ASSERT((((int)TT_INT64 == (int)OT_INT64) &&
            ((int)TT_FP == (int)OT_FP) &&
            ((int)TT_STR == (int)OT_STR) &&
            ((int)TT_BOOL == (int)OT_BOOL) &&
            ((int)TT_NULL == (int)OT_NULL)));

    obj->common.obj_ty = tk->type;
    obj->common.str_len = tk->str_len;
    obj->int_val = tk->int_val;

    return &obj->common;
}

void
insert_subobj(obj_composite_t* nesting, obj_t* nested) {
    nested->next = nesting->subobjs;
    nesting->subobjs = nested;
    nesting->common.elmt_num ++;
}

int
emit_primitive_tk(mempool_t* mp, token_t* tk,
                  obj_composite_t* nesting_cobj) {
    obj_t* obj = cvt_primitive_tk(mp, tk);
    if (obj) {
        insert_subobj(nesting_cobj, obj);
        return 1;
    }

    return 0;
}

/***************************************************************************
 *
 *                  Parser driver
 *
 ***************************************************************************
 */
obj_t*
parse(parser_t* parser, const char* json,  uint32_t json_len) {
    scaner_t* scaner = &parser->scaner;
    const char* json_end = scaner->json_end;
    pstack_init(parser);

    token_t* tk = sc_get_token(scaner, json_end);
    token_ty_t tk_ty = tk->type;

    /* case 1: The input json starts with delimiter of composite objects
     *    (i.e. array/hashtab).
     */
    if (tk_ty == TT_CHAR) {
        int succ = 0;
        char c = tk->char_val;
        if (c == '{') {
            succ = start_parsing_hashtab(parser);
        } else if (c == '[') {
            succ = start_parsing_array(parser);
        } else {
            set_parser_err_fmt(parser, "Unknow object starting with '%c'", c);
            return 0;
        }

        while (succ) {
            composite_state_t* top = pstack_top(parser);
            obj_ty_t ot = top->obj.common.obj_ty;
            if (ot == OT_HASHTAB) {
                succ = parse_hashtab(parser);
            } else if (ot == OT_ARRAY) {
                succ = parse_array(parser);
            } else {
                ASSERT(ot == OT_ROOT);
                break;
            }
        }

        if (unlikely(!succ))
            return 0;

        token_t* end_tk = sc_get_token(scaner, json_end);
        if (end_tk->type != TT_END) {
            goto trailing_junk;
        }

        return parser->result;
    }

    /* case 2: The input jason is empty */
    if (unlikely(tk_ty == TT_END)) {
        parser->err_msg = "Input json is empty";
        return 0;
    }

    /* case 3: The input starts with a primitive object. I don't know if it
     *   conforms to spec or not.
     */
    if (tk_is_primitive(tk)) {
        parser->result = cvt_primitive_tk(parser->mempool, tk);
        if (sc_get_token(scaner, json_end)->type == TT_END) {
            return parser->result;
        }
    }

trailing_junk:
    parser->result = 0;
    set_parser_err(parser, "Extraneous stuff");
    return 0;
}

static void
reset_parser(parser_t* parser, const char* json, uint32_t json_len) {
    mempool_t* mp = parser->mempool;
    mp_free_all(mp);

    pstack_init(parser);
    sc_init_scaner(&parser->scaner, mp, json, json_len);
    parser->result = 0;

    parser->err_msg = 0;
    parser->next_cobj_id = 1;
}

/****************************************************************************
 *
 *          Implementation of the exported functions
 *
 ***************************************************************************
 */
struct json_parser*
jp_create(void) {
    parser_t* p = (parser_t*)malloc(sizeof(parser_t));
    if (unlikely(!p))
        return 0;

    mempool_t* mp = mp_create();
    if (unlikely(!mp))
        return 0;

    p->mempool = mp;
    p->result = 0;
    p->err_msg = "Out of Memory"; /* default error message :-)*/

    pstack_init(p);
    return (struct json_parser*)(void*)p;
}

obj_t*
jp_parse(struct json_parser* jp, const char* json, uint32_t len) {
    parser_t* parser = (parser_t*)(void*)jp;
    reset_parser(parser, json, len);

    obj_t* obj = parse(parser, json,  len);
    ASSERT(verfiy_reverse_nesting_order(obj));
    return obj;
}

void
jp_destroy(struct json_parser* p) {
    parser_t* parser = (parser_t*)(void*)p;
    mp_destroy(parser->mempool);
    free((void*)p);
}

/* *****************************************************************************
 *
 *      Debugging, error handling and other cold code
 *
 * *****************************************************************************
 */
void __attribute__((format(printf, 2, 3), cold))
set_parser_err_fmt(parser_t* parser, const char* fmt, ...) {
    if (parser->err_msg)
        return;

    int buf_len = 250;
    char* buf = MEMPOOL_ALLOC_TYPE_N(parser->mempool, char, buf_len);
    if (!buf) {
        parser->err_msg = "OOM";
        return;
    }
    parser->err_msg = buf;

    scaner_t* scaner = &parser->scaner;
    /* In case error take place in scaner, we should go for scaner's
     * error message.
     */

    if (scaner->err_msg) {
        snprintf(buf, buf_len, "%s", scaner->err_msg);
        return;
    }

    int loc_info_len = snprintf(buf, buf_len, "(line:%d,col:%d) ",
                                    scaner->line_num, scaner->col_num);
    buf += loc_info_len;
    buf_len -= loc_info_len;

    va_list vl;
    va_start(vl, fmt);
    vsnprintf(buf, buf_len, fmt, vl);
    va_end(vl);
}

void __attribute__((cold))
set_parser_err(parser_t* parser, const char* str) {
    if (!parser->err_msg)
        set_parser_err_fmt(parser, "%s", str);
}

static void __attribute__((cold))
dump_primitive_obj (FILE* f, obj_t* the_obj) {
    obj_primitive_t* obj = (obj_primitive_t*)(void*)the_obj;

    switch (the_obj->obj_ty) {
    case OT_INT64:
        fprintf(f, "%" PRIi64, obj->int_val);
        break;

    case OT_FP:
        fprintf(f, "%.16f", obj->db_val);
        break;

    case OT_STR:
        {
            int idx = 0;
            int len = the_obj->str_len;
            fputc('"', f);
            for (; idx < len; idx++) {
                char c = obj->str_val[idx];
                if (isprint(c)) {
                    fputc(c, f);
                } else {
                    fprintf(f, "\\%#02x", c);
                }
            }
            fputc('"', f);
        }
        break;

    case OT_BOOL:
        fputs(obj->int_val ? "true" : "false", f);
        break;

    case OT_NULL:
        fputs("null", f);
        break;

    default:
        ASSERT(0 && "NOT Primitive");
        break;
    }
}

void __attribute__((cold))
dump_composite_obj(FILE* f, obj_composite_t* cobj) {
    obj_ty_t type = cobj->common.obj_ty;
    if (type != OT_ARRAY && type != OT_HASHTAB) {
        fprintf(f, "unknown composite type %d\n", (int)type);
        return;
    }

    obj_t* elmt_slist = cobj->subobjs;
    int elmt_num = cobj->common.elmt_num;

    obj_t** elmt_vect = (obj_t**)malloc(sizeof(obj_t*) * elmt_num);
    int i = elmt_num - 1;
    while (elmt_slist) {
        elmt_vect[i] = elmt_slist;
        elmt_slist = elmt_slist->next;
        i--;
    }

    if (i != -1) {
        free(elmt_vect);
        fprintf(f, "the numbers of elements disagree\n");
        return;
    }

    if (type == OT_ARRAY) {
        fprintf (f, "[ (id:%d) ", cobj->id);
        int i;
        for(i = 0; i < elmt_num; i++) {
            obj_t* elmt = elmt_vect[i];
            if (elmt->obj_ty <= OT_LAST_PRIMITIVE) {
                dump_primitive_obj(f, elmt);
            } else {
                int id = ((obj_composite_t*)(void*)elmt)->id;
                fprintf(f, "obj-%d", id);
            }

            if (i != elmt_num - 1)
                fputs(", ", f);
        }
        fputs("]\n", f);
    } else {
        ASSERT(type == OT_HASHTAB);
        ASSERT((elmt_num & 1) == 0);

        fprintf(f, "{ (id:%d) ", cobj->id);
        int i;
        for(i = 0; i < elmt_num; i+=2) {
            obj_t* key = elmt_vect[i];
            obj_t* val = elmt_vect[i+1];
            dump_primitive_obj(f, key);

            fputc(':', f);

            if (val->obj_ty <= OT_LAST_PRIMITIVE) {
                dump_primitive_obj(f, val);
            } else {
                int id = ((obj_composite_t*)(void*)val)->id;
                fprintf(f, "obj-%d", id);
            }

            if (i != elmt_num - 2)
                fputs(", ", f);
        }
        fputs("}\n", f);
    }

    free(elmt_vect);
}

void __attribute__((cold))
dump_obj(FILE* f, obj_t* obj) {
    if (!obj) {
        fprintf(f, "null\n");
        return;
    }

    obj_ty_t type = obj->obj_ty;
    if (type <= OT_LAST_PRIMITIVE) {
        dump_primitive_obj(f, obj);
        fputc('\n', f);
    } else {
        obj_composite_t* cobj = (obj_composite_t*)(void*)obj;
        for (; cobj; cobj = cobj->reverse_nesting_order) {
            dump_composite_obj(f, cobj);
        }
    }
}

const char* __attribute__((cold))
jp_get_err(struct json_parser* p) {
    parser_t* parser = (parser_t*)(void*)p;
    return parser->err_msg;
}

#ifdef DEBUG
static int
verfiy_reverse_nesting_order(obj_t* parse_result) {
    if (!parse_result)
        return 1;

    obj_ty_t type = parse_result->obj_ty;
    if (type <= OT_LAST_PRIMITIVE)
        return 0;

    obj_composite_t* cobj = (obj_composite_t*)(void*)parse_result;

    int obj_cnt = 1;

    int first_id, last_id;
    first_id = last_id = cobj->id;

    /* loop over all composite-object in the the reverse-nesting order */
    for (cobj = cobj->reverse_nesting_order;
         cobj != 0;
         cobj = cobj->reverse_nesting_order) {
        if (cobj->id != last_id - 1)
            return 0;

        last_id = cobj->id;
        obj_cnt++;
    }

    if (last_id != 1 || obj_cnt != first_id)
        return 0;

    return 1;
}
#endif
