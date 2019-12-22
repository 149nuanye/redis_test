#ifndef REDIS_TEST_H
#define REDIS_TEST_H 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "dict.h"
#include "sds.h"
#include "zmalloc.h"

#define LRU_BITS 24
#define LRU_CLOCK_MAX ((1<<LRU_BITS)-1) /* Max value of obj->lru */
/* Object types */
#define OBJ_STRING 0
#define OBJ_LIST 1
#define OBJ_SET 2
#define OBJ_ZSET 3



#define OBJ_ENCODING_RAW 0
#define OBJ_ENCODING_INT 1
#define OBJ_ENCODING_HT 2
#define OBJ_ENCODING_ZIPLIST 5
#define OBJ_ENCODING_INTSET 6 
#define OBJ_ENCODING_SKIPLIST 7  /* Encoded as skiplist */
#define OBJ_ENCODING_EMBSTR 8
#define OBJ_ENCODING_QUICKLIST 9
#define LRU_CLOCK_RESOLUTION 1000

#define LRU_CLOCK() ((1000/10 <= LRU_CLOCK_RESOLUTION) ? 100 : getLRUClock())

typedef long long mstime_t; /* millisecond time type. */

typedef struct redisObject {
    unsigned type:4;
    unsigned encoding:4;
    unsigned lru:LRU_BITS; /* lru time (relative to server.lruclock) */
    int refcount;
    void *ptr;
} robj;

#define REDIS_COMPARE_BINARY (1<<0)
#define REDIS_COMPARE_COLL (1<<1)

void string_object();
void list_object();
void set_object();
long long ustime(void);
unsigned int getLRUClock(void);
mstime_t mstime(void);
robj *createObject(int type, void *ptr);
#endif