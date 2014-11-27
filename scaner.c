#include <ctype.h>  /* for isdigit */
#include <string.h> /* for memchr() */
#include <stdio.h>
#include <stdlib.h> /* for strtod() */
#include <stdarg.h>

#include "util.h"
#include "scaner.h"

static const char* unrecog_token = "Unrecognizable token";

/* Forward decl */
static void __attribute__((format(printf, 3, 4), cold))
set_scan_err_fmt(scaner_t* scaner, const char* loc, const char* fmt, ...);

static void __attribute__((cold))
set_scan_err(scaner_t* scaner, const char* loc, const char* str);

static char token_predict[256];
static char esc_char[256];

#define TT_IS_SPACE (TT_LAST + 1)

static void
init_token_predict() {
    memset(token_predict, TT_ERR, sizeof(token_predict));

    /* Seperator */
    {
        int i, e;
        const char* sep = "{}[],:";
        for (i = 0, e = (int)strlen(sep); i < e; i++) {
            uint8_t c = (uint8_t)sep[i];
            token_predict[c] = TT_CHAR;
        }
    }

    /* Null predictor */
    token_predict['n'] = TT_NULL;
    token_predict['N'] = TT_NULL;

    /* Number(int/fp) predictor. NOTE: unlike C, numbers
     * like +1.2 .5, -.4 are illegal.
     */
    const char* np = "-0123456789";
    int idx, idx_e;
    for (idx = 0, idx_e = strlen(np); idx < idx_e; idx++) {
        token_predict[(uint)np[idx]] = TT_FP;
    }

    /* Boolean predictor */
    const char* bp = "tTfF";
    for (idx = 0, idx_e = strlen(bp); idx < idx_e; idx++) {
        token_predict[(uint)bp[idx]] = TT_BOOL;
    }

    /* string predictor */
    token_predict['"'] = TT_STR;

    token_predict[' '] = TT_IS_SPACE;
    token_predict['\t'] = TT_IS_SPACE;
    token_predict['\r'] = TT_IS_SPACE;
    token_predict['\n'] = TT_IS_SPACE;
    token_predict['\f'] = TT_IS_SPACE;
    token_predict['\v'] = TT_IS_SPACE;
}

static void
init_esc_table() {
    memset(esc_char, 0, sizeof(esc_char));
    esc_char['"'] = '"';
    esc_char['/'] = '/';
    esc_char['\\'] = '\\';
    esc_char['b'] = '\b';
    esc_char['f'] = '\f';
    esc_char['n'] = '\n';
    esc_char['r'] = '\r';
    esc_char['t'] = '\t';
}

static void __attribute__((constructor))
init_const_table() {
    init_token_predict();
    init_esc_table();
}

/* On success, scaner advance the pointer right after the token just
 * recognized, and the token_t::span records span of the token in
 * input string.
 */
static inline void
update_ptr_on_succ(scaner_t* scaner, const char* scan_starts,
                   int32_t span) {
    scaner->scan_ptr = scan_starts + span;
    scaner->token.span = span;
    scaner->col_num += span;
}

/* On failure, scaner's pointer is not advanced, and token_t::span points
 * to the locations where lexical error takes place.
 */
static inline void
update_ptr_on_failure(scaner_t* scaner, const char* scan_starts,
                      int32_t span) {
    scaner->scan_ptr = scan_starts;
    scaner->token.span = span;
    scaner->token.type = TT_ERR;
}

static token_t*
char_handler(scaner_t* scaner, const char* str, const char* str_e) {
    update_ptr_on_succ(scaner, str, 1);

    token_t* tk = &scaner->token;
    tk->type = TT_CHAR;
    tk->char_val = *str;

    return tk;
}

static token_t*
null_handler(scaner_t* scaner, const char* str, const char* str_e) {
    token_t* tk = &scaner->token;

    if (str + 4 < str_e && !strncmp(str, "null", 4)) {
        update_ptr_on_succ(scaner, str, 4);
        tk->type = TT_NULL;
        return tk;
    }

    update_ptr_on_failure(scaner, str, 0);
    if (str + 4 < str_e && !strncasecmp(str, "null", 4)) {
        set_scan_err(scaner, str, "'null' must be in lower case");
    } else {
        set_scan_err(scaner, str, 0);
    }
    return tk;
}

typedef union {
    int64_t int_val;
    double db_val;
} int_db_union_t;

/* Helper function of fp_handler(). It returns: 0 on failure, 1 if the value
 * is of integer type, or 2 if the value of 'double' type. The value is
 * returned via "result".
 */
static int
scan_fp(const char** scan_str, const char* str_e, int_db_union_t* result) {
    const char* str_save = *scan_str;
    const char* str = *scan_str;

    int is_negative = (*str == '-') ? 1 : 0;
    str += is_negative;

    /* More often than not, the number is of interger type that can fit in
     * int64_t. So, we speculatively try to convert input string into
     * an int64_t as we go along. In case it turns out to be a floating
     * point number, or the interger is too big to fit in int64_t, we start
     * over converting the string to "double"-typed value.
     */
    int64_t int_val = 0;

    while (str < str_e) {
        char c = *str;
        if (isdigit(c)) {
            int_val = int_val * 10 + (c - '0');
            str++;
        } else {
            if (c != '.' && (c | 0x20) != 'e') {
                if (str - str_save < 20) {
                    /* It's guarantee to fit in int64_t */
                    if (!is_negative) {
                        result->int_val = int_val;
                    } else {
                        result->int_val = - int_val;
                    }
                    *scan_str = str;
                    return 1;
                }
            }

            double d = strtod(str_save, (char**)scan_str);
            if (*scan_str != str_save) {
                result->db_val = d;
                return 2;
            }
            return 0;
        }
    }
    return 0;
}

static token_t*
fp_handler(scaner_t* scaner, const char* str, const char* str_e) {
    const char* advance = str;
    int_db_union_t val;
    int res = scan_fp(&advance, str_e, &val);

    token_t* tk = &scaner->token;
    if (res == 1) {
        update_ptr_on_succ(scaner, str, advance - str);
        tk->type = TT_INT64,
        tk->int_val = val.int_val;
    } else if (res == 2) {
        update_ptr_on_succ(scaner, str, advance - str);
        tk->type = TT_FP,
        tk->db_val = val.db_val;
    } else {
        update_ptr_on_failure(scaner, str, advance - str);
    }

    return tk;
}

static token_t*
bool_handler(scaner_t* scaner, const char* str, const char* str_e) {
    int len = str_e - str;
    token_t* tk = &scaner->token;
    tk->type = TT_BOOL;
    if (len >= 5) {
        if (!strncmp(str, "true", 4)) {
            tk->int_val = 1;
            update_ptr_on_succ(scaner, str, 4);
            return tk;
        }

        if (!strncmp(str, "false", 5)) {
            tk->int_val = 0;
            update_ptr_on_succ(scaner, str, 5);
            return tk;
        }
    }

    update_ptr_on_failure(scaner, str, 0);

    /* Emit eror-message if true/false is not in lower case, or the token
     * starts with [tTfF], but is not boolean value at all.
     */
    if ((len >= 4 && strncasecmp(str, "true", 4)) ||
        (len >= 5 && strncasecmp(str, "false", 5))) {
        set_scan_err(scaner, str, "boolean value must be in lower case");
    } else {
        set_scan_err(scaner, str, 0);
    }

    return tk;
}

static token_t*
unknown_tk_handler(scaner_t* scaner, const char* str, const char* str_e) {
    token_t* tk = &scaner->token;
    update_ptr_on_failure(scaner, str, 0);
    set_scan_err(scaner, str, 0);
    return tk;
}

static int
utf8_get_val(const unsigned char* hex4) {
    unsigned char c = *hex4++;
    int hval = 0, value;

    if (c >= '0' && c <= '9')
        hval = c - '0';
    else if ((c | 0x20) <= 'f') {
        hval = (c | 0x20) - 'a' + 10;
    } else {
        return 0;
    }
    value = hval;

    c = *hex4++;
    if (c >= '0' && c <= '9')
        hval = c - '0';
    else if ((c | 0x20) <= 'f') {
        hval = (c | 0x20) - 'a' + 10;
    } else {
        return 0;
    }
    value = (value << 4) | hval;

    c = *hex4++;
    if (c >= '0' && c <= '9')
        hval = c - '0';
    else if ((c | 0x20) <= 'f') {
        hval = (c | 0x20) - 'a' + 10;
    } else {
        return 0;
    }
    value = (value << 4) | hval;

    c = *hex4++;
    if (c >= '0' && c <= '9')
        hval = c - '0';
    else if ((c | 0x20) <= 'f') {
        hval = (c | 0x20) - 'a' + 10;
    } else {
        return 0;
    }

    value = (value << 4) | hval;
    return value;
}

static int
utf8_len(int val) {
    if (val < 0x80)
        return 1;

    if (val < 0x800)
        return 2;

    if (val < 0x10000)
        return 3;

    return 4;
}

static void
utf8_encode(char* buf, int value, int len) {
    static unsigned char len_mark[] = {0, 0xc0, 0xe0, 0xf0 };
    switch (len) {
    case 4: *(buf + 3) = ((value | 0x80) & 0xbf); value >>= 6; /* fall through */
    case 3: *(buf + 2) = ((value | 0x80) & 0xbf); value >>= 6; /* fall through */
    case 2: *(buf + 1) = ((value | 0x80) & 0xbf); value >>= 6; /* fall through */
    default: break;
    }

    *buf = value | len_mark[len - 1];
}

static int
process_unicode_esc(const char* src, const char* src_end, char* dest, int* len) {
    if (unlikely(src + 4 > src_end))
        return 0;

    int val = utf8_get_val((unsigned char*)src);
    if (unlikely(val == 0))
        return 0;

    /* TODO: check UTF-16 surrogate */
    int l = *len = utf8_len(val);
    utf8_encode(dest, val, l);

    return 1;
}

static token_t*
str_handler(scaner_t* scaner, const char* str, const char* str_e) {
    /* step 1: determine the end of string */
    const char* str_quote = str;
    token_t* tk = &scaner->token;

    do {
        str_quote = memchr(str_quote + 1, '"', str_e - str_quote);
        if (unlikely(!str_quote)) {
            /* The string dose not end with quote*/
            set_scan_err(scaner, str, "String does not end with quote");
            return tk;
        }
    } while(*(str_quote - 1) == '\\');

    /* step 2: allocate space for the string. The new string has trailing
     * '\0' for easing purpose.
     */
    char* new_str =
        MEMPOOL_ALLOC_TYPE_N(scaner->mempool, char, str_quote - str);
    if (unlikely(!new_str)) {
        set_scan_err(scaner, str, "OOM");
        return tk;
    }

    /* step 3: copy the string */
    {
        char* dest = new_str;
        const char* src = str + 1;
        do {
            int len = str_quote - src;
            char* esc = (char*)memchr(src, '\\', len);
            if (!esc) {
                memcpy(dest, src, len);
                src += len;
                dest += len;
                *dest = '\0'; /* to ease debugging*/

                tk->str_val = new_str;
                tk->str_len = dest - new_str;
                tk->type = TT_STR;
                update_ptr_on_succ(scaner, str, str_quote - str + 1);
                return tk;
            }

            /* Handle escape */
            len = esc - src;
            memcpy(dest, src, len);
            src = esc;
            dest += len;

            char esc_key = esc[1];
            char esc_val = esc_char[(unsigned char)esc_key];

            /* successfully processed non-unicode (\u) escape */
            if (esc_val) {
                *dest++ = esc_val;
                src += sizeof("\\n") - 1;
                continue;
            }

            /* process unicode escape */
            if (esc_key == 'u') {
                int len;
                if (process_unicode_esc(src + 2, str_e, dest, &len)) {
                    src += sizeof("\\uffff") - 1;
                    dest += len;
                    continue;
                }
                set_scan_err(scaner, esc, "unicode escape");
                return tk;
            }

            /* illegal escape */
            set_scan_err_fmt(scaner, esc, "illegal escape \\%c", esc[1]);
            return tk;
        } while(1);
    }

    /* Should not reach here. The return-statement is just to make
     * stupid compilers happy.
     */
    ASSERT(0);
    return NULL;
}

static token_t* space_handler(scaner_t*, const char*, const char*);

typedef token_t* (*tk_hd_func)(scaner_t*, const char*, const char*);
tk_hd_func token_handler[] = {
    [TT_INT64] = 0,
    [TT_FP] = fp_handler,
    [TT_STR] = str_handler,
    [TT_BOOL] = bool_handler,
    [TT_NULL] = null_handler,
    [TT_CHAR] = char_handler,
    [TT_ERR] = unknown_tk_handler,
    [TT_IS_SPACE] = space_handler,
};

static token_t*
space_handler(scaner_t* scaner, const char* str_ptr, const char* str_end) {
    int32_t ln = 0;
    int32_t col = 0;

    char lookahead = *str_ptr;
    token_ty_t tt;
    do {
        col = (lookahead == '\n') ? 1 : col + 1;
        ln += ((lookahead == '\n') ? 1 : 0);

        if (unlikely(str_end <= ++str_ptr)) {
            scaner->token.type = TT_END;
            return &scaner->token;
        }

        lookahead = *str_ptr;
        tt = (token_ty_t)token_predict[(uint32_t)lookahead];
        if (tt != TT_IS_SPACE)
            break;
    } while (1);

    scaner->line_num += ln;
    scaner->col_num += col;
    /* It is not necessary to set scan_ptr as token-handler will update it.*/
    /*scaner->scan_ptr = str_ptr; */

    return token_handler[tt](scaner, str_ptr, str_end);
}

token_t*
sc_get_token(scaner_t* scaner, const char* str_end) {
    const char* str_ptr = scaner->scan_ptr;
    ASSERT(str_end == scaner->json_end);

    if (unlikely(str_ptr >= str_end)) {
        scaner->token.type = TT_END;
        return &scaner->token;
    }

    char lookahead = *str_ptr;
    token_ty_t tt = (token_ty_t)token_predict[(uint32_t)lookahead];
    return token_handler[tt](scaner, str_ptr, str_end);
}

void
sc_init_scaner(scaner_t* scaner, mempool_t* mp,
               const char* json, uint32_t json_len) {
    scaner->mempool = mp;
    scaner->json_begin = json;
    scaner->json_end = json + json_len;
    scaner->scan_ptr = json;
    scaner->line_num = 1;
    scaner->col_num = 1;
    scaner->err_msg = NULL;
}

void
sc_rewind (scaner_t* scaner) {
    int span = scaner->token.span;
    scaner->scan_ptr -= span;
    scaner->col_num -= span;
}

/****************************************************************
 *
 *   Error handling and other cold code cluster here
 *
 *****************************************************************
 */
static void __attribute__((format(printf, 3, 4)))
set_scan_err_fmt(scaner_t* scaner, const char* loc, const char* fmt, ...) {
    token_t* tk = &scaner->token;
    tk->type = TT_ERR;

    int buf_len = 250;
    char* buf = MEMPOOL_ALLOC_TYPE_N(scaner->mempool, char, buf_len);
    if (!buf) {
        scaner->err_msg = "OOM";
        return;
    }

    scaner->err_msg = buf;
    int span = loc - scaner->scan_ptr;
    int loc_info_len = snprintf(buf, buf_len, "(line:%d,col:%d) ",
                                scaner->line_num, scaner->col_num + span);

    buf += loc_info_len;
    buf_len -= loc_info_len;

    va_list vl;
    va_start(vl, fmt);
    vsnprintf(buf, buf_len, fmt, vl);
    va_end(vl);
}

static void __attribute__((cold))
set_scan_err(scaner_t* scaner, const char* loc, const char* str) {
    if (!str) { str = unrecog_token; }
    set_scan_err_fmt(scaner, loc, "%s", str);
}
