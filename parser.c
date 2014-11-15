#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>

#include "mempool.h"
#include "adt.h"
#include "scaner.h"

#include "parser.h"

/* Return 1 on success, or 0 otherwise (due to OOM) */
static int pstack_add_subobj(pstack_t* s, composite_state_t* level, obj_t* subojb);

static inline int
pstack_add_subobj_to_top(pstack_t* s, obj_t* subobj) {
    return pstack_add_subobj(s, pstack_top(s), subobj);
}

static inline list_elmt_t*
alloc_composite_state(pstack_t *s) {
    slist_t* free_comp_state = &s->free_comp_state;
    if (!slist_empty(free_comp_state)) {
        slist_elmt_t* e = slist_delete_first(free_comp_state);
        return (list_elmt_t*)(void*)e;
    }

    uint32_t elmt_sz = list_elmt_size(sizeof(composite_state_t));
    list_elmt_t* e = (list_elmt_t*)mp_alloc(s->mempool, elmt_sz);
    return e;
}

int
pstack_push(pstack_t* s, obj_ty_t obj_ty, int init_state) {
    list_elmt_t* e = alloc_composite_state(s);
    if (e) {
        composite_state_t* cs = LIST_ELMT_PAYLOAD(e, composite_state_t);
        cs->obj_ty = obj_ty;
        cs->parse_state = init_state;
        slist_init(&cs->sub_objs);

        list_t* l = &s->stack;
        list_insert_after(l, &l->sentinel, e);
        return 1;
    }

    return 0;
}

void
pstack_pop(pstack_t* s) {
    list_t* l = &s->stack;
    ASSERT(l->size);

    list_elmt_t* top = l->sentinel.next;
    list_delete(l, top);

    /* recycle elements of sub-object list */
    composite_state_t* cs = LIST_ELMT_PAYLOAD(top, composite_state_t);
    slist_splice(&s->free_sub_objs, &cs->sub_objs);

    /* recycle the free stack element */
    slist_prepend(&s->free_comp_state, (slist_elmt_t*)(void*)top);
}

int
pstack_add_subobj(pstack_t* s, composite_state_t* level, obj_t* subojb) {
    slist_t* free_list = &s->free_sub_objs;

    slist_elmt_t* subobj_elmt = slist_delete_first(free_list);
    if (!subobj_elmt) {
        subobj_elmt = MEMPOOL_ALLOC_TYPE(s->mempool, slist_elmt_t);
        if (unlikely(!subobj_elmt))
            return 0;
    }

    subobj_elmt->ptr_val = subojb;
    slist_prepend(&level->sub_objs, subobj_elmt);

    return 1;
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
    obj_t* obj = MEMPOOL_ALLOC_TYPE(mp, obj_t);
    if (unlikely(!obj))
        return 0;

    static const char tk_ty_map[] = {
        [TT_INT64] = OT_INT64,
        [TT_FP] = OT_FP,
        [TT_STR] = OT_STR,
        [TT_BOOL] = OT_BOOL,
        [TT_NULL] = OT_NULL
    };

    obj->obj_ty = tk_ty_map[tk->type];
    obj->int_val = tk->int_val;
    obj->str_len = tk->str_len;

    return obj;
}

/* Add sub-objects (aka elements) to the composite object being processed.
 * We use a singly-linked list (slist) to link the elements; first element is
 * at the end of the slist, while the last element is at the head of the list.
 * The reason for reverse order is that it's slist, and we prepend element
 * to the slist as we go.
 *
 *   To be pedantic, hashtab's element is key-value pair. For efficiency
 * reasons, we don't introduce a data-structure for key-value-pair. Instead,
 * view hashtab as a sequence with alternativing keys and values. So given,
 * hashtab ht = {k1:v1, ... kn:vn} in json, once it is successfully parsed,
 * its representation is vn, kn, ... v1, k1.
 */
static inline int
insert_primitive_subobj(parser_t* parser, obj_t* subobj) {
    pstack_t* s = parser->parse_stack;
    composite_state_t* top = pstack_top(s);
    if (unlikely(!pstack_add_subobj(s, top, subobj)))
        return 0;
    return 1;
}

int
insert_subobj(parser_t* parser, obj_t* subobj) {
    if (!insert_primitive_subobj(parser, subobj))
        return 0;

    if (subobj->obj_ty <= OT_LAST_PRIMITIVE)
        return 1;

    /* Link the composite object in a reverse nesting order */
    composite_obj_t* cobj = (composite_obj_t*)(void*)subobj;
    composite_obj_t* last = parser->last_emitted_cobj;
    cobj->next = 0;

    if (last) {
        last->next = cobj;
    } else {
        parser->result = subobj;
    }

    parser->last_emitted_cobj = cobj;
    return 1;
}

/* Emit primitive toekn, which include two steps:
 *   - Convert the token to objects, and
 *   - Add the object to its immediately enclosing composite object.
 */
int
emit_primitive_tk(parser_t* parser, token_t* tk) {
    obj_t* obj = cvt_primitive_tk(parser->mempool, tk);
    if (obj) {
        return insert_primitive_subobj(parser, obj);
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
    pstack_t* pstack =  parser->parse_stack;
    ASSERT(pstack_empty(pstack));

    scaner_t* scanner = parser->scaner;
    pstack_push(pstack, OT_ROOT, 0 /* don't care */);

    token_t* tk = sc_get_token(scanner);
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
            composite_state_t* top = pstack_top(pstack);
            if (top->obj_ty == OT_HASHTAB) {
                succ = parse_hashtab(parser);
            } else if (top->obj_ty == OT_ARRAY) {
                succ = parse_array(parser);
            } else {
                ASSERT(top->obj_ty == OT_ROOT);
                break;
            }
        }

        if (unlikely(!succ))
            return 0;

        token_t* end_tk = sc_get_token(scanner);
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
        if (sc_get_token(scanner)->type == TT_END) {
            return parser->result;
        }
    }

trailing_junk:
    parser->result = 0;
    set_parser_err(parser, "Extraneous stuff");
    return 0;
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

    p->parse_stack = 0;
    p->scaner = 0;
    p->err_msg = "Out of Memory"; /* default error message :-)*/
    p->mempool = mp;
    return (struct json_parser*)(void*)p;
}

obj_t*
jp_parse(struct json_parser* jp, const char* json, uint32_t len) {
    parser_t* parser = (parser_t*)(void*)jp;

    parser->result = 0;
    parser->last_emitted_cobj = 0;

    mempool_t* mp = parser->mempool;

    mp_free_all(mp);

    scaner_t* s = sc_create(mp, json, len);
    if (unlikely(!s))
        return 0;
    parser->scaner = s;

    pstack_t* ps = pstack_create(mp);
    if (unlikely(!ps))
        return 0;
    parser->parse_stack = ps;

    parser->err_msg = 0;
    parser->next_cobj_id = 1;

    obj_t* obj = parse(parser, json,  len);
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

    scaner_t* scaner = parser->scaner;
    if (scaner) {
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
    }

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
dump_primitive_obj (FILE* f, obj_t* obj) {
    switch (obj->obj_ty) {
    case OT_INT64:
        fprintf(f, "%" PRIi64, obj->int_val);
        break;

    case OT_FP:
        fprintf(f, "%.16f", obj->db_val);
        break;

    case OT_STR:
        {
            int idx = 0;
            int len = obj->str_len;
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
dump_obj(FILE* f, obj_t* obj) {
    if (!obj) {
        fprintf(f, "null\n");
        return;
    }

    obj_ty_t type = obj->obj_ty;
    if (type <= OT_LAST_PRIMITIVE) {
        dump_primitive_obj(f, obj);
        fputc('\n', f);
    }

    composite_obj_t* cobj = (composite_obj_t*)(void*)obj;
    obj_t** elmts = obj->elmt_vect;
    int elmt_num = obj->elmt_num;

    if (type == OT_ARRAY) {
        fprintf (f, "[ (id:%d) ", cobj->id);
        int i;
        for(i = 0; i < elmt_num; i++) {
            obj_t* elmt = elmts[i];
            if (elmt->obj_ty <= OT_LAST_PRIMITIVE) {
                dump_primitive_obj(f, elmt);
            } else {
                int id = ((composite_obj_t*)(void*)elmt)->id;
                fprintf(f, "obj-%d", id);
            }

            if (i != elmt_num - 1)
                fputs(", ", f);
        }
        fputs("]\n", f);
        return;
    }

    ASSERT(type == OT_HASHTAB);
    ASSERT((elmt_num & 1) == 0);

    fprintf(f, "{ (id:%d) ", cobj->id);
    int i;
    for(i = 0; i < elmt_num; i+=2) {
        obj_t* key = elmts[i];
        obj_t* val = elmts[i+1];
        dump_primitive_obj(f, key);

        fputc(':', f);

        if (val->obj_ty <= OT_LAST_PRIMITIVE) {
            dump_primitive_obj(f, val);
        } else {
            int id = ((composite_obj_t*)(void*)val)->id;
            fprintf(f, "obj-%d", id);
        }

        if (i != elmt_num - 2)
            fputs(", ", f);
    }
    fputs("}\n", f);

    if (cobj->next) {
        dump_obj(f, (obj_t*)(void*)cobj->next);
    }
}

const char* __attribute__((cold))
jp_get_err(struct json_parser* p) {
    parser_t* parser = (parser_t*)(void*)p;
    return parser->err_msg;
}
