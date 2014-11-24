#ifndef SCANER_H
#define SCANER_H

#include <stdint.h>
#include "ljson_parser.h"
#include "mempool.h"

typedef enum {
    /* Integer that can fit in int64_t. Otherwise, it would be represented with
     * double-precision floating-point number.
     */
    TT_INT64 = OT_INT64,

    /* double-precision number */
    TT_FP = OT_FP,

    TT_STR = OT_STR,
    TT_BOOL = OT_BOOL,
    TT_NULL = OT_NULL,
    TT_LAST_PRIMITIVE = TT_NULL,

    /* If scanner fail to recognaize a primtive at current position, it just
     * returns the character at current position. Since the scanner skip
     * whitespaces as it moves forward, the character at "current position" is
     * guaranted to be a non-whitespace.
     */
    TT_CHAR,

    TT_ERR,

    /* Meet the end of input json */
    TT_END,
    TT_LAST = TT_END + 1
} token_ty_t;

typedef struct {
    union {
        int64_t int_val;
        char* str_val;
        char char_val;
        double db_val;
    };
    token_ty_t type;

    /* valid iff the token is a string */
    int32_t str_len;

    /* How many chars in the input json string represent this token. In case of
     * lexical problem, the span points to location (relative to the begnning
     * of the token) where the problem occurs.
     */
    int32_t span;
} token_t;

typedef struct {
    /* The last token we get. NOTE: token dose not live across get_token() */
    token_t token;

    /* The half-open interval "[json_begin, json_end)" is the input json
     * in memory.
     */
    const char* json_begin;
    const char* json_end;

    /* pointer moving forward from json_text toward json_end */
    const char* scan_ptr;
    mempool_t* mempool;

    /* The location of current pointer */
    int32_t line_num;
    int32_t col_num;

    const char* err_msg;
} scaner_t;

static inline int
tk_is_primitive(const token_t* tk) {
    return ((uint32_t)tk->type) <= TT_LAST_PRIMITIVE;
}

void sc_init_scaner(scaner_t*, mempool_t*, const char* json, uint32_t json_len);

token_t* sc_get_token(scaner_t*);

/* Rewind the pointer back to beginning of token just successfully scaned.
 * It's called by parser when it detects syntax error.
 */
void sc_rewind(scaner_t*);

#endif
