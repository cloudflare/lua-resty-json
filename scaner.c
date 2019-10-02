#include <ctype.h>  /* for isdigit */
#include <string.h> /* for memchr() */
#include <stdio.h>
#include <stdlib.h> /* for strtod() */
#include <stdarg.h>
#include <math.h> /* for the time being */
#include "util.h"
#include "scaner.h"
#include "scan_fp.h"

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
        token_predict[(uint8_t)np[idx]] = TT_FP;
    }

    /* Boolean predictor */
    const char* bp = "tTfF";
    for (idx = 0, idx_e = strlen(bp); idx < idx_e; idx++) {
        token_predict[(uint8_t)bp[idx]] = TT_BOOL;
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

/* ***********************************************************************
 *
 *      Handle String
 *
 * ***********************************************************************
 */

/* The input "hex4" is a string with *four* leading hex-digits,
 * this function is to convert them into an positive integer.
 *
 * For instance, given input hex4 being "aBc9...", the return value would
 * be 0xabc9. Hexadecimal digits are case insensitive. If the leading
 * four character include non-hex-digit, -1 is returned.
 *
 * NOTE: It's up to the caller to ensure the length of "hex4" is no less
 *  than 4.
 */
static int32_t
hex4_to_int(const char* hex4) {
    unsigned char c = *hex4++;
    int hval = 0, value;

    if (c >= '0' && c <= '9')
        hval = c - '0';
    else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
        hval = (c | 0x20) - 'a' + 10;
    } else {
        return -1;
    }
    value = hval;

    c = *hex4++;
    if (c >= '0' && c <= '9')
        hval = c - '0';
    else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
        hval = (c | 0x20) - 'a' + 10;
    } else {
        return -1;
    }
    value = (value << 4) | hval;

    c = *hex4++;
    if (c >= '0' && c <= '9')
        hval = c - '0';
    else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
        hval = (c | 0x20) - 'a' + 10;
    } else {
        return -1;
    }
    value = (value << 4) | hval;

    c = *hex4++;
    if (c >= '0' && c <= '9')
        hval = c - '0';
    else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
        hval = (c | 0x20) - 'a' + 10;
    } else {
        return -1;
    }

    value = (value << 4) | hval;
    return value;
}

/* determine the number of bytes needed to encode the given codepoint */
static int
utf8_encode_len(int codepoint) {
    if (codepoint < 0x80)
        return 1;

    if (codepoint < 0x800)
        return 2;

    if (codepoint < 0x10000)
        return 3;

    return 4;
}

/* Encode the given codepoint in a sequence of UTF-8s */
static void
utf8_encode(char* buf, int codepoint, int len) {
    static unsigned char len_mark[] = {0, 0xc0, 0xe0, 0xf0 };
    switch (len) {
    case 4: *(buf + 3) = ((codepoint | 0x80) & 0xbf);
        codepoint >>= 6;
        /* fall through */

    case 3: *(buf + 2) = ((codepoint | 0x80) & 0xbf);
        codepoint >>= 6;
        /* fall through */

    case 2: *(buf + 1) = ((codepoint | 0x80) & 0xbf);
        codepoint >>= 6;
        /* fall through */

    default: break;
    }

    *buf = codepoint | len_mark[len - 1];
}

/* Process \u escape.
 *
 * The legal input string falls in one of the following two cases:
 *   1. "\uzzzz", where the zzzz in (0, 0xD800] (Note zzzz > 0)
 *   2. "\uxxxx\uyyyy", where the xxxx in [0xD800, 0xDBFF] yyyy in [DC00,DFFF].
 *      i.e. the input string is a UTF-16 surrogate pair.
 *
 *  This function is to convert the input string to up to four UTF-8s and
 * save them to "dest".
 *
 *   On success, return 1, and the "src_advance" and "dest_advance" is set to
 * the amount of byte the source and the destination string need to advance,
 * respectively. Otherwise, 0 is returned.
 */
static const char* illegal_u_esc = "Illegal \\u escape";
static int
process_u_esc(scaner_t* scaner, const char* src, const char* src_end,
              char* dest, int* src_advance, int* dest_advance) {
    int32_t codepoint;

    /* Step 1: get the codepoint */
    if (unlikely(src + 6 > src_end))
        return 0;

    codepoint = hex4_to_int(src + 2);
    if (unlikely(codepoint < 0)) {
        set_scan_err(scaner, src, illegal_u_esc);
        return 0;
    }

    *src_advance = 6; /* skip the \\uxxxx, hence 6 */

    /* Detect UTF-16 surrogate pair. The codepoint be in this form : 110110x...
     */
    if (codepoint >= 0xd800 && (codepoint < 0xe000 || codepoint > 0xffff)) {
        int32_t codepoint_low;
        const char* lower = src + 6;
        if (codepoint & 0x400) {
            set_scan_err(scaner, src, "Higher part of UTF-16 surrogate must "
                                      "be in the range of [0xd800, 0xdbff]");
            return 0;
        }

        if (unlikely(src + 12 > src_end) ||
            unlikely(*(src + 6) != '\\') || unlikely(*(src + 7) != 'u')) {
            set_scan_err(scaner, src + 6,
                         "Expect \\u escape for lower part "
                         "of UTF-16 surrogate");
            return 0;
        }

        codepoint_low = hex4_to_int(lower + 2);
        if (codepoint_low < 0) {
            set_scan_err(scaner, lower, illegal_u_esc);
            return 0;
        }

        if (unlikely(codepoint_low < 0xdc00) ||
            unlikely(codepoint_low > 0xdfff)) {
            set_scan_err(scaner, lower, "Lower part of UTF-16 surrogate must "
                                        "be in the range of [0xdc00, 0xdfff]");
            return 0;
        }

        /* Extract the lower 10-bit from surrogate pairs, and concatenate
         * them together.
         */
        codepoint = (codepoint_low & 0x3ff) | ((codepoint & 0x3ff) << 10);
        codepoint |= 0x10000;

        *src_advance = 12; /* skip the "\\uxxxx\\uyyyy", hence 12 */
    }

    /* Step 2: Encode the codepoint with UTF-8 sequence */
    utf8_encode(dest, codepoint, *dest_advance = utf8_encode_len(codepoint));
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

        if (likely(*(str_quote - 1) != '\\')) {
            break;
        } else {
            /* Consider the cases like "Junk\\" */
            const char* t = str_quote - 2;
            int cnt = 1;
            for (; *t == '\\'; t--, cnt++) {}
            if ((cnt & 1) == 0) {
                break;
            }
        }
    } while(1);

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
                int src_adv, dest_adv;
                if (process_u_esc(scaner, src, str_quote, dest,
                                  &src_adv, &dest_adv)) {
                    src += src_adv;
                    dest += dest_adv;
                    continue;
                }
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
        tt = (token_ty_t)token_predict[(uint32_t)(uint8_t)lookahead];
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
    token_ty_t tt = (token_ty_t)token_predict[(uint32_t)(uint8_t)lookahead];
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
    if (scaner->err_msg)
        return;

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
    if (scaner->err_msg)
        return;

    if (!str) { str = unrecog_token; }
    set_scan_err_fmt(scaner, loc, "%s", str);
}
