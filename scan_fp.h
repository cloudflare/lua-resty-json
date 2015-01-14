#ifndef SCAN_FP_H
#define SCAN_FP_H

typedef union {
    int64_t int_val;
    double db_val;
} int_db_union_t;

/* return 0 on error, 1 if the result contains integer value, and 2 if the
 * result contains floating point value.
 */
int scan_fp(const char** scan_str, const char* str_e, int_db_union_t* result);

#endif
