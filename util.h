#ifndef UTIL_H
#define UTIL_H

#ifdef DEBUG
#include <stdlib.h> /* for abort */
#endif

#include "ljson_parser.h"

#define likely(x)   __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

#define offsetof(t, m)  __builtin_offsetof(t, m)

#ifdef DEBUG
    #define ASSERT(c) if (!(c))\
        { fprintf(stderr, "%s:%d Assert: %s\n", __FILE__, __LINE__, #c); abort(); }
#else
    #define ASSERT(c) ((void)0)
#endif

#endif
