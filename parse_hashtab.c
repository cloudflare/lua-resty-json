#include "parser.h"

static int
emit_hashtab(parser_t* parser) {
    pstack_t* ps = &parser->parse_stack;
    composite_state_t* top = pstack_top(ps);

    int elmt_num = top->sub_objs.size;
    ASSERT((elmt_num & 1) == 0);

    composite_obj_t* ht = MEMPOOL_ALLOC_TYPE(parser->mempool, composite_obj_t);
    if (unlikely(!ht)) {
        return 0;
    }

    obj_t** elmt_vect = MEMPOOL_ALLOC_TYPE_N(parser->mempool, obj_t*, elmt_num);
    slist_elmt_t* slist_elmt = top->sub_objs.first;

    int idx = elmt_num - 1;
    for (; idx >= 0; idx--) {
        obj_t* elmt = (obj_t*)slist_elmt->ptr_val;
        elmt_vect[idx] = elmt;
        slist_elmt = slist_elmt->next;
    }

    ht->obj.obj_ty = OT_HASHTAB;
    ht->obj.elmt_vect = elmt_vect;
    ht->obj.elmt_num = elmt_num;
    ht->id = parser->next_cobj_id++;

    pstack_pop(ps);

    /* Add this hashtab to the enclosing composite object */
    insert_subobj(parser, ht);

    return 1;
}

typedef enum {
    PKVP_DONE,
    PKVP_COMPOSITE,
    PKVP_CLOSE,
    PKVP_ERR,
} PKVP_STATE;

int
parse_keyval_pair(parser_t* parser, scaner_t* scaner) {
    token_t* tk = sc_get_token(scaner);
    pstack_t* ps = &parser->parse_stack;
    composite_state_t* top = pstack_top(ps);
    slist_t* subobj_list = &top->sub_objs;

    /* step 1: Parse the key string */
    if (tk->type == TT_STR) {
        if (unlikely(!emit_primitive_tk(parser->mempool, tk, subobj_list))) {
            return PKVP_ERR;
        }
    } else if (tk->type == TT_CHAR && tk->char_val == '}') {
        return PKVP_CLOSE;
    } else {
        if (tk->type != TT_ERR) {
            sc_rewind(scaner);
            set_parser_err(parser, "Key must be a string");
        }

        return PKVP_ERR;
    }

    /* step 2: Expect ':' delimiter */
    tk = sc_get_token(scaner);
    if (tk->type != TT_CHAR || tk->char_val != ':') {
        set_parser_err(parser, "expect ':'");
        return PKVP_ERR;
    }

    /* step 3: parse the 'value' part */
    tk = sc_get_token(scaner);
    if (tk_is_primitive(tk)) {
        if (unlikely(!emit_primitive_tk(parser->mempool, tk, subobj_list)))
            return PKVP_ERR;
        return PKVP_DONE;
    }

    if (tk->type == TT_CHAR) {
        char c = tk->char_val;
        if (c == '{') {
            start_parsing_hashtab(parser);
            return PKVP_COMPOSITE;
        }

        if (c == '[') {
            start_parsing_array(parser);
            return PKVP_COMPOSITE;
        }
    }

    set_parser_err(parser, "value object syntax error");
    return PKVP_ERR;
}

typedef enum {
    PHT_JUST_BEGUN,
    PHT_PARSING_MORE_ELMT,
    PHT_PARSING_1st_ELMT,
} PHT_STATE;

int
parse_hashtab(parser_t* parser) {
    scaner_t* scaner = &parser->scaner;
    pstack_t* ps = &parser->parse_stack;
    composite_state_t* state = pstack_top(ps);
    PHT_STATE parse_state = state->parse_state;

    while (1) {
        /* case 1: So far we have successfully parsed at least one element,
         *    and now move on parsing remaining elements.
         *
         *   At this moment we are expecting to see:
         *     o. "',' KEY_VAL_PAIR ... ", or
         *     o. the closing delimiter of a hashtab, i.e. the '}'.
         */
        if (parse_state == PHT_PARSING_MORE_ELMT) {
            token_t* tk = sc_get_token(scaner);
            if (unlikely(!tk)) {
                parser->err_msg = scaner->err_msg;
                return 0;
            }

            if (tk->type == TT_CHAR) {
                char c = tk->char_val;
                if (c == ',') {
                    PKVP_STATE ret = parse_keyval_pair(parser, scaner);
                    if (ret == PKVP_DONE)
                        continue;
                    else if (ret == PKVP_COMPOSITE) {
                        state->parse_state = PHT_PARSING_MORE_ELMT;
                        return 1;
                    }

                    goto err_out;
                }

                if (c == '}') {
                    return emit_hashtab(parser);
                }
            }

            goto err_out;
        }

        if (parse_state == PHT_JUST_BEGUN) {
            state->parse_state = PHT_PARSING_1st_ELMT;
            PKVP_STATE ret = parse_keyval_pair(parser, scaner);
            switch (ret) {
            case PKVP_DONE:
                parse_state = PHT_PARSING_MORE_ELMT;
                continue;

            case PKVP_COMPOSITE:
                return 1;

            case PKVP_CLOSE:
                return emit_hashtab(parser);

            default:
                goto err_out;
            }
        }

        ASSERT(parse_state == PHT_PARSING_1st_ELMT);
        parse_state = PHT_PARSING_MORE_ELMT;
    }

err_out:
    set_parser_err(parser, "hashtab syntax error");
    return 0;
}

int
start_parsing_hashtab(parser_t* parser) {
    if (!pstack_push(&parser->parse_stack, OT_HASHTAB, PHT_JUST_BEGUN))
        return 0;

    return parse_hashtab(parser);
}
