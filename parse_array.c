#include <stdint.h>
#include "util.h"
#include "parser.h"

static const char* syntax_err = "Array syntax error, expect ',' or ']'";

/* Emit this array by adding it to the nesting composite data structure. */
static void
emit_array(parser_t* parser) {
    composite_state_t* top = pstack_top(parser);
    ASSERT(top->obj.common.obj_ty == OT_ARRAY);

    obj_t* array_obj = &top->obj.common;
    composite_state_t* new_top = pstack_pop(parser);
    insert_subobj(&new_top->obj, array_obj);
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

static PAE_STATE
parse_array_elmt(parser_t* parser, obj_composite_t* array_obj, token_t* tk) {
    /* case 1: The token contains a primitive object */
    if (tk_is_primitive(tk)) {
        if (emit_primitive_tk(parser->mempool, tk, array_obj))
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
    scaner_t* scaner = &parser->scaner;
    const char* json_end = scaner->json_end;
    composite_state_t* state = pstack_top(parser);
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
            token_t* delimiter = sc_get_token(scaner, json_end);
            if (delimiter->type == TT_CHAR) {
                char c = delimiter->char_val;
                if (c == ',') {
                    token_t* tk = sc_get_token(scaner, json_end);
                    PAE_STATE ret = parse_array_elmt(parser, &state->obj, tk);
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
                    emit_array(parser);
                    return 1;
                }
            }
            goto err_out;
        }

        /* case 2: Just saw '[', and try to parse the first element. */
        if (parse_state == PA_JUST_BEGUN) {
            token_t* tk = sc_get_token(scaner, json_end);
            PAE_STATE ret = parse_array_elmt(parser, &state->obj, tk);
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
                emit_array(parser);
                return 1;

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
    if (!pstack_push(parser, OT_ARRAY, PA_JUST_BEGUN))
        return 0;

    return parse_array(parser);
}
