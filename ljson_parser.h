/* **************************************************************************
 *
 *   The file delcare json parser interface. All the export functions are
 * self-descriptive except the jp_parse() which is bit involved in its
 * return value. The return value of the jp_parse() is a singly-linked list
 * of composite objects chained together in a reverse-nesting order.
 *
 *   Use the same runing example we use in parser.c. Suppose the input json
 * is: [1, 2, {"key": 3.4}]. Let object Obj2 be {"key": 3.4}, and Obj1 be
 * [1, 2, Obj2]. The return-value would be:
 *
 *     Obj2 -> Obj1. (linked via obj_composite_t::reverse_nesting_order)
 *
 *   Note that the out-most composite object, in this case, the array Obj1,
 * is at the end of the resulting list. The rationale for reverse-nesting
 * order is that re-consturcting the nesting relationship can be done
 * simply by iterating the list only once.
 *
 *    The elements of a composite object is pointed by obj_composite_t::subobjs.
 * Again, the order of the elements is *reversed* as well. In general, if
 * the original JSON object is an array {e1, e2, ..., en}; the representation
 * of the array is list: en->...,->e2->e1; if the original JSON object is a
 * hash-table { k1:v1, k2:v2, ..., kn:vn}, the representation would be
 * vn->kn->....->v2->k2->v1->k1
 *
 * **************************************************************************
 **/
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

/* Data structure shared both by composite and primitive objects.
 * In our context, jason array (object in the form of [elmt1, ... ]) and
 * hash-tab (in the form of {"key":value, ... } are called compostive object,
 * while number/string/null/boolean are called primitive objects.
 */
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
    obj_t common; /* Must be the 1st field */
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
    obj_t common; /* Must be the 1st field */
    obj_t* subobjs;
    obj_composite_t* reverse_nesting_order;
    uint32_t id;
};

struct json_parser;

#ifdef BUILDING_SO
    #ifndef __APPLE__
        #define LJP_EXPORT __attribute__ ((visibility ("protected")))
    #else
        /* OSX does not support protect-visibility */
        #define LJP_EXPORT __attribute__ ((visibility ("default")))
    #endif
#else
    #define LJP_EXPORT
#endif

/* **************************************************************************
 *
 *              Export Functions
 *
 * **************************************************************************
 */
struct json_parser* jp_create(void) LJP_EXPORT;
void jp_destroy(struct json_parser*) LJP_EXPORT;

/* Parse the given json, and return the resulting object corresponding to the
 * input json is returned. In the event of error, NULL is returned. See the
 * above comment for details.
 */
obj_t* jp_parse(struct json_parser*, const char* json, uint32_t len) LJP_EXPORT;

/* Get the error message. Do not call this function if jp_parser() return
 * non-NULL pointer.
 */
const char* jp_get_err(struct json_parser*) LJP_EXPORT;

/* Dump the result returned from jp_parse() */
void dump_obj(FILE*, obj_t*) LJP_EXPORT;

#ifdef __cplusplus
}
#endif

#endif
