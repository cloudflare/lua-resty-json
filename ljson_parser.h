#ifndef LUA_JSON_PASER_H
#define LUA_JSON_PASER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

typedef enum {
    OT_INT64,
    OT_FP,
    OT_STR,
    OT_BOOL,
    OT_NULL,
    OT_LAST_PRIMITIVE = OT_NULL,
    OT_HASHTAB,
    OT_ARRAY,
    OT_ROOT /* type of dummy object introduced during parsing process */
} obj_ty_t;

struct obj_tag;
typedef struct obj_tag obj_t;

struct obj_tag {
    obj_t* next;
    int32_t obj_ty;
    union {
        int32_t str_len;
        int32_t elmt_num; /* # of element of array/hashtab */
    };
};

/* primitive object */
typedef struct {
    obj_t common;
    union {
        char* str_val;
        int64_t int_val;
        double db_val;
    };
} obj_primitive_t;

/* composite object */
struct obj_composite_tag;
typedef struct obj_composite_tag obj_composite_t;
struct obj_composite_tag {
    obj_t common;
    obj_t* subobjs;
    obj_composite_t* reverse_nesting_order;
    uint32_t id;
};

struct json_parser;

#ifdef BUILDING_SO
#define LJP_EXPORT __attribute__ ((visibility ("protected")))
#else
#define LJP_EXPORT
#endif

/* Export functions */
struct json_parser* jp_create(void) LJP_EXPORT;
obj_t* jp_parse(struct json_parser*, const char* json, uint32_t len) LJP_EXPORT;
const char* jp_get_err(struct json_parser*) LJP_EXPORT;
void jp_destroy(struct json_parser*) LJP_EXPORT;

void dump_obj(FILE*, obj_t*) LJP_EXPORT;

#ifdef __cplusplus
}
#endif

#endif
