/*****************************************************************************
 *
 *  This file tries to parse floating point literal quickly. There are two
 * implementations in this files:
 *
 *    1). The one very relaxed floating point mode (FP_RELAX >= 2), and
 *    2). The one with *almost* restrict mode. (FP_RELAX == 1).
 *
 * In variant 1), we evaluate a floating point number, say 12.345E12 as following:
 *   a) let d1 = 12
 *   b) let d2 = 345 * (10**-2 * 10 **-1) (NOTE: reciprocal is very imprecise)
 *   c) let d3 = d1 + d2
 *   d) let result = d3 * (10**3 + 10**2)
 *
 *  The reason and the only reason to keep the toy-grade variant-1 is to set
 * a bar (in terms of parsing speed) for the future work).
 *
 * Variant-2) is *almost* restrict. It can efficiently parse a floating point
 * literal if it's in the form of nnnn.mmm, and the integer part contains no
 * more than 20 digits, fraction part contains than 16 digits (it the liternal
 * does not satisfy this restrct, it would resort to expensive strtod() libc
 * function call). Variant 2) evaluate a liternal, say 123.456 this way:
 *   a) let d1 = 123
 *   b) let d2 = 456/10**3
 *   c) let result = d1 + d2
 *
 *  FIXME: {
 *     case 1: If interger part is 0 (d1 == 0), the "result" is precise
 *            unless the rounding mode we are using in the '/' operator
 *            is not what json expect (But does Json spec define which
 *            rounding mode should we go).
 *
 *     case 2: If the interger-part > (1<<53), "result == d1" should hold.
 *     case 3: If the integer-part < (1<<53), the rounding in step b) could
 *             ripple to step c) and hence incur 1/(2**53) relative error.
 *  }
 *
 * TODO: Implement the algorithm depicted in
 *  http://www.exploringbinary.com/correct-decimal-to-floating-point-using-big-integers/,
 *  Make sure the common cases can be parsed as fast as the variant-1
 *  and variant-2.
 */
#include <stdint.h>
#include <stdlib.h> /* for str2od() */
#include "util.h"
#include "scan_fp.h"

#if FP_RELAX >= 2

/* HINT: max double = 1.797693E+308, min-double = 2.225074E-308 */
#define MAX_EXP_ABS 308

static double pos_pow10[22] = { 1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
                                1e10, 1e11, 1e12, 1e13, 1e14, 1e15, 1e16, 1e17,
                                1e18, 1e19, 1e20, 1e21};

static double neg_pow10[22] = { 0.1, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7,
                                1e-8, 1e-9, 1e-10, 1e-11, 1e-12, 1e-13, 1e-14,
                                1e-15, 1e-16, 1e-17, 1e-18, 1e-19, 1e-20,
                                1e-21};

static double
mypow10(int exp, int negative) {
    ASSERT(exp >= 0 && exp <= MAX_EXP_ABS);
    if (exp < sizeof(pos_pow10)/sizeof(pos_pow10[0])) {
        return negative ? neg_pow10[exp] : pos_pow10[exp];
    }

    static const double exp_fact1[9] = {
        1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256
    };

    static const double exp_fact2[9] = {
        1e-1, 1e-2, 1e-4, 1e-8, 1e-16, 1e-32, 1e-64, 1e-128, 1e-256
    };

    double val = 1;
    const double* dbl_fact = negative ? exp_fact2 : exp_fact1;

    int idx = 0;
    while (exp) {
        if (exp & 1) {
            val *= dbl_fact[idx];
        }
        exp = exp >> 1;
        idx++;
    }

    return val;
}

int
scan_fp(const char** scan_ptr, const char* str_end, int_db_union_t* result) {
    const char* str, *p;
    str = p = *scan_ptr;
    int negative = 0;

    if (*p == '-') {
        negative = 1;
        p++;
    }

    int int_len = 0;
    int64_t int_val = 0;

    /* step 1: Calculate the integer part */
    while (p < str_end) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            int_val = c - '0' + int_val * 10;
            p++;
        } else {
            break;
        }
    }

    int_len = p - str;
    if (unlikely(p >= str_end) || unlikely(int_len >= 20)) {
        /*The "len < 20" condition is to guaranteed the value fit in int64_t.*/
        goto too_nasty;
    }

    char c = *p;
    if (c != '.' && ((c | 0x20) != 'e')) {
        result->int_val = negative ? - int_val : int_val;
        *scan_ptr = p;
        return 1;
    }

    /* step 2: Calculate the fraction part */
    double frac = 0.0;
    int frac_len = 0;
    if (c == '.') {
        const char* frac_start = ++p;
        while (p < str_end) {
            char c = *p;
            if (c >= '0' && c <= '9') {
                frac = c - '0' + frac * 10;
                p++;
            } else {
                break;
            }
        }

        frac_len = p - frac_start;
        if (frac_len > 20) {
            goto too_nasty;
        }
        frac = frac * mypow10(frac_len, 1);
    }

    if (unlikely(p >= str_end)) {
        /* The floating-point literal per se is nothing wrong. However, this
         * condition implies that the literal is the last token of the json
         * being processed, which is not correct.
         */
        return 0;
    }

    /* step 3: Calculate the exponent part */
    double dbl_result = (double)int_val + frac;
    if (negative)
        dbl_result = - dbl_result;

    c = *p;
    int exp = 0;
    if ((c | 0x20) == 'e') {
        if (int_len != 1)
            goto too_nasty;

        p++;
        int neg_exp = 0;
        if (p < str_end && *p == '-') {
            neg_exp = 1;
            p++;
        }

        while (p < str_end) {
            char c = *p;
            if (c >= '0' && c <= '9') {
                exp = c - '0' + exp * 10;
                /* HINT: max double = 1.797693E+308,
                 * min-double = 2.225074E-308
                 */
                if (exp >= 308)
                    goto too_nasty;
                p++;
            } else {
                break;
            }
        }

        dbl_result *= mypow10(exp, neg_exp);
    }
    result->db_val = dbl_result;
    *scan_ptr = p;
    return 2;

too_nasty:
    {
    fprintf(stderr, "too nasty %s!\n", str);
    char* fp_end;
    double d = strtod(str, &fp_end);
    if (fp_end != str) {
        result->db_val = d;
        *scan_ptr = fp_end;
        return 2;
    }
    }
    return 0;
}
#endif

#if FP_RELAX == 1

/* If the fraction part can fit in 53-bit, it can be represented by a
 * "double"-typed value exactly.  The "((long long)1 << 53) - 1" evaluates to
 * 9007199254740991 which has 16 digits. If the faction part has more than 16
 * digit, we simply give up.
 */
#define MAX_FRAC_LEN 16

static double
mypow10(int exp) {
    ASSERT(exp <= MAX_FRAC_LEN);
    static double pos_pow10[16] = { 1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
                                    1e10, 1e11, 1e12, 1e13, 1e14, 1e15 };

    return pos_pow10[exp];
}

int
scan_fp(const char** scan_ptr, const char* str_end, int_db_union_t* result) {
    const char* str, *p;
    str = p = *scan_ptr;
    int negative = 0;

    if (*p == '-') {
        negative = 1;
        p++;
    }

    int int_len = 0;
    int64_t int_val = 0;

    /* step 1: Calculate the integer part */
    while (p < str_end) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            int_val = c - '0' + int_val * 10;
            p++;
        } else {
            break;
        }
    }

    int_len = p - str;
    if (unlikely(p >= str_end) || unlikely(int_len >= 20)) {
        /*The "len < 20" condition is to guaranteed the value fit in int64_t.*/
        goto too_nasty;
    }

    char c = *p;
    if (c != '.' && ((c | 0x20) != 'e')) {
        result->int_val = negative ? - int_val : int_val;
        *scan_ptr = p;
        return 1;
    }

    /* step 2: Calculate the fraction part */
    int64_t frac_int = 0;
    int frac_len = 0;
    if (c == '.') {
        const char* frac_start = ++p;
        while (p < str_end) {
            char c = *p;
            if (c >= '0' && c <= '9') {
                frac_int = c - '0' + frac_int * 10;
                p++;
            } else {
                break;
            }
        }

        frac_len = p - frac_start;
        if (frac_len >= (((int64_t)1) << 53) - 1) {
            /* make sure frac_len can fit in 53 bit, such that it can be
             * represented exactly by a double.
             */
            goto too_nasty;
        }
    }

    if (unlikely(p >= str_end)) {
        /* The floating-point literal per se is nothing wrong. However, this
         * condition implies that the literal is the last token of the json
         * being processed, which is not correct.
         */
        return 0;
    }

    /* step 3: give up if it's in scientific notation */
    if ((*p | 0x20) == 'e') {
        goto too_nasty;
    }

    result->db_val = int_val + (double)frac_int / mypow10(frac_len);
    *scan_ptr = p;
    return 2;

too_nasty:
    {
    char* fp_end;
    double d = strtod(str, &fp_end);
    if (fp_end != str) {
        result->db_val = d;
        *scan_ptr = fp_end;
        return 2;
    }
    }
    return 0;
}
#endif
