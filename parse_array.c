#include <stdint.h>
#include "parser.h"

static const char* syntax_err = "Array syntax error, expect ',' or ']'";
static int
emit_array(parser_t* parser) {
    composite_state_t* top = pstack_top(parser->parse_stack);
    ASSERT(top->obj_ty == OT_ARRAY);

    composite_obj_t* array_obj =
        MEMPOOL_ALLOC_TYPE(parser->mempool, composite_obj_t);
    if (unlikely(!array_obj))
        return 0;

    slist_t* sub_objs = &top->sub_objs;
    int elmt_num = sub_objs->size;

    obj_t** elmt_vect = 0;
    if (elmt_num != 0) {
        elmt_vect = MEMPOOL_ALLOC_TYPE_N(parser->mempool, obj_t*, elmt_num);
        if (unlikely(!elmt_vect))
            return 0;

        slist_elmt_t* iter = top->sub_objs.first;
        int idx = elmt_num - 1;
        for (; iter != 0; iter = iter->next, idx--) {
            elmt_vect[idx] = (obj_t*)iter->ptr_val;
        }
    }

    array_obj->obj.obj_ty = OT_ARRAY;
    array_obj->obj.elmt_num = elmt_num;
    array_obj->obj.elmt_vect = elmt_vect;
    array_obj->id = parser->next_cobj_id++;

    pstack_pop(parser->parse_stack);
    return insert_subobj(parser, (obj_t*)(void*)array_obj);
}


/* state returned from parse_array_elmt */
typedef enum {
    /* An element (must be primitive object) was successfully parsed */
    PAE_DONE,

    /* Parsing the nesting composite element */
    PAE_COMPOSITE,

    /* See  ']' */
    PAE_CLOSE,

    PAE_ERR
} PAE_STATE;

PAE_STATE
parse_array_elmt(parser_t* parser, scaner_t* scaner, token_t* tk) {
    if (unlikely(!tk)) {
        parser->err_msg = scaner->err_msg;
        return PAE_ERR;
    }

    /* case 1: The token contains an primitive object */
    if (tk_is_primitive(tk)) {
        if (emit_primitive_tk(parser, tk))
            return PAE_DONE;
        return PAE_ERR;
    }

    if (tk->type == TT_CHAR) {
        char c = tk->char_val;

        /* case 2: The token is the starting delimiter of composite objects. */
        if (c == '{') {
            if (!start_parsing_hashtab(parser))
                return PAE_ERR;
            return PAE_COMPOSITE;
        }

        if (c == '[') {
            if (!start_parsing_array(parser))
                return PAE_ERR;
            return PAE_COMPOSITE;
        }

        /* case 3: see the array closing delimiter */
        if (c == ']')
            return PAE_CLOSE;
    }

    set_parser_err(parser, syntax_err);
    return PAE_ERR;
}

typedef enum {
    /* The scaner just saw '[', and moving on the parsing the 1st element */
    PA_JUST_BEGUN,

    /* at least one element is parsed, and now parsing "{',' <element> }" */
    PA_PARSING_MORE_ELMT,

    /* Parsing the 1st *composite* element */
    PA_PARSING_1st_ELMT
} PA_STATE;

/* Parse an array object, return 0 if something wrong take places, or 0 implying
 * so-far-so-good.
 *
 * Array syntax : '[' [ ELMT {',' ELMT } * ] ']'
 */
int
parse_array(parser_t* parser) {
    scaner_t* scaner = parser->scaner;
    composite_state_t* state = pstack_top(parser->parse_stack);
    PA_STATE parse_state = state->parse_state;

    while (1) {
        /* case 1: So far we have successfully parsed at least one element,
         *   and now move on parsing remaining elements.
         *
         *   At this moment we are expecting to see:
         *      o. "',' ELEMENT ....", or
         *      o. closing delimiter of an array, i.e. the ']'.
         */
        if (parse_state == PA_PARSING_MORE_ELMT) {
            token_t* tk = sc_get_token(scaner);
            if (tk->type == TT_CHAR) {
                char c = tk->char_val;
                if (c == ',') {
                    tk = sc_get_token(scaner);
                    PAE_STATE ret = parse_array_elmt(parser, scaner, tk);
                    if (ret == PAE_DONE)
                        continue;
                    else if (ret == PAE_COMPOSITE) {
                        /* remember where we leave off */
                        state->parse_state = PA_PARSING_MORE_ELMT;
                        return 1;
                    } else {
                        goto err_out;
                    }
                }

                if (c == ']') {
                    return emit_array(parser);
                }
            }

            goto err_out;
        }

        /* case 2: Just saw '[', and try to parse the first element. */
        if (parse_state == PA_JUST_BEGUN) {
            token_t* tk = sc_get_token(scaner);
            PAE_STATE ret = parse_array_elmt(parser, scaner, tk);
            switch (ret) {
            case PAE_DONE:
                parse_state = PA_PARSING_MORE_ELMT;
                continue;

            case PAE_COMPOSITE:
                /* The 1st element is an composite object */
                state->parse_state = PA_PARSING_1st_ELMT;
                return 1;

            case PAE_CLOSE:
                /* This is an empty array */
                return emit_array(parser);

            default:
                goto err_out;
            }
        }

        /* case 3: The first element is a composite object, and it is just
         * successfully parsed.
         */
        ASSERT(parse_state == PA_PARSING_1st_ELMT);
        parse_state = PA_PARSING_MORE_ELMT;
    }

err_out:
    set_parser_err(parser, syntax_err);
    return 0;
}

int
start_parsing_array(parser_t* parser) {
    if (!pstack_push(parser->parse_stack, OT_ARRAY, PA_JUST_BEGUN))
        return 0;

    return parse_array(parser);
}
