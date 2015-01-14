#include <stdint.h>
#include <stdlib.h> /* for str2od() */
#include "util.h"
#include "scan_fp.h"

#if FP_RELAX == 0
/* i.e strict floating point mode */

int
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
        if (c >= '0' && c <= '9') {
            int_val = int_val * 10 + (c - '0');
            str++;
        } else {
            if (c != '.' && (c | 0x20) != 'e') {
                if (str - str_save < 20) {
                    /* It's guaranteed to fit in int64_t */
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

#endif /*  FP_RELAX == 0 */
