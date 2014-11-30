#include "util.h"
#include "parser.h"

static void
emit_hashtab(parser_t* parser) {
    composite_state_t* top = pstack_top(parser);
    ASSERT(top->obj.common.obj_ty == OT_HASHTAB);

    obj_t* array_obj = &top->obj.common;
    composite_state_t* new_top = pstack_pop(parser);
    insert_subobj(&new_top->obj, array_obj);
}

typedef enum {
    PKVP_DONE,
    PKVP_COMPOSITE,
    PKVP_CLOSE,
    PKVP_ERR,
} PKVP_STATE;

static PKVP_STATE
parse_keyval_pair(parser_t* parser, obj_composite_t* htab_obj) {
    scaner_t* scaner = &parser->scaner;
    const char* json_end = scaner->json_end;

    token_t* tk = sc_get_token(scaner, json_end);

    /* step 1: Parse the key string */
    if (tk->type == TT_STR) {
        if (unlikely(!emit_primitive_tk(parser->mempool, tk, htab_obj))) {
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
    tk = sc_get_token(scaner, json_end);
    if (tk->type != TT_CHAR || tk->char_val != ':') {
        set_parser_err(parser, "expect ':'");
        return PKVP_ERR;
    }

    /* step 3: parse the 'value' part */
    tk = sc_get_token(scaner, json_end);
    if (tk_is_primitive(tk)) {
        if (unlikely(!emit_primitive_tk(parser->mempool, tk, htab_obj)))
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
    PHT_PARSING_ELMT,
} PHT_STATE;

int
parse_hashtab(parser_t* parser) {
    scaner_t* scaner = &parser->scaner;
    const char* json_end = scaner->json_end;

    composite_state_t* state = pstack_top(parser);
    PHT_STATE parse_state = state->parse_state;

    if (parse_state == PHT_JUST_BEGUN) {
        state->parse_state = PHT_PARSING_ELMT;
        PKVP_STATE ret = parse_keyval_pair(parser, &state->obj);
        switch (ret) {
        case PKVP_DONE:
            parse_state = PHT_PARSING_ELMT;
            break;

        case PKVP_COMPOSITE:
            return 1;

        case PKVP_CLOSE:
            emit_hashtab(parser);
            return 1;

        default:
            goto err_out;
        }
    }

    while (1) {
        token_t* tk = sc_get_token(scaner, json_end);
        if (tk->type == TT_CHAR) {
            char c = tk->char_val;
            if (c == ',') {
                PKVP_STATE ret = parse_keyval_pair(parser, &state->obj);
                if (ret == PKVP_DONE)
                    continue;
                else if (ret == PKVP_COMPOSITE) {
                    return 1;
                }

                goto err_out;
            }

            if (c == '}') {
                emit_hashtab(parser);
                return 1;
            }
        }

        goto err_out;
    }

err_out:
    set_parser_err(parser, "hashtab syntax error");
    return 0;
}

int
start_parsing_hashtab(parser_t* parser) {
    if (!pstack_push(parser, OT_HASHTAB, PHT_JUST_BEGUN))
        return 0;

    return parse_hashtab(parser);
}
