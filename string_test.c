#include "redis_test.h"
#include "sds.h"

robj *createObject(int type, void *ptr) {
    robj *o = zmalloc(sizeof(*o));
    o->type = type;
    o->encoding = OBJ_ENCODING_RAW;
    o->ptr = ptr;
    o->refcount = 1;
    /* Set the LRU to the current lruclock (minutes resolution). */
    o->lru = LRU_CLOCK();
    return o;
}

robj *createEmbeddedStringObject(const char *ptr, size_t len) {
    robj *o = zmalloc(sizeof(robj)+sizeof(struct sdshdr8)+len+1);
    struct sdshdr8 *sh = (void*)(o+1);

    o->type = OBJ_STRING;
    o->encoding = OBJ_ENCODING_EMBSTR;
    o->ptr = sh+1;
    o->refcount = 1;
    o->lru = LRU_CLOCK();

    sh->len = len;
    sh->alloc = len;
    sh->flags = SDS_TYPE_8;
    if (ptr) {
        memcpy(sh->buf,ptr,len);
        sh->buf[len] = '\0';
    } else {
        memset(sh->buf,0,len+1);
    }
    return o;
}

robj *createRawStringObject(const char *ptr, size_t len) {
    return createObject(OBJ_STRING,sdsnewlen(ptr,len));
}

#define OBJ_ENCODING_EMBSTR_SIZE_LIMIT 44
robj *createStringObject(const char *ptr, size_t len) {
    if (len <= OBJ_ENCODING_EMBSTR_SIZE_LIMIT)
        return createEmbeddedStringObject(ptr,len);
    else
        return createRawStringObject(ptr,len);
}

robj *createStringObjectFromLongLong(long long value) {
    robj *o;  
    o = createObject(OBJ_STRING, NULL);
    o->encoding = OBJ_ENCODING_INT;
    o->ptr = (void*)((long)value);
    return o;
}
void string_object(){
    char *str1 = "hello word";
    sds s1 = sdsnew(str1); 
    robj *rob1 = createRawStringObject(str1,strlen(str1));
    printf("len str1:%ld, rob1->encoding:%d, rob1->refcount:%d, rob1->ptr:%s\n",
        strlen(str1),rob1->encoding,rob1->refcount,rob1->ptr);

    robj *rob2 = createStringObject(str1,strlen(str1));
    printf("len str1:%ld, rob2->encoding:%d, rob2->refcount:%d, rob2->ptr:%s\n",
        strlen(str1),rob2->encoding,rob2->refcount,rob2->ptr);
    
    robj *rob3 = createStringObjectFromLongLong(100);
    printf("len str1:%ld, rob3->encoding:%d, rob3->refcount:%d, rob3->ptr:%d\n",
        strlen(str1),rob3->encoding,rob3->refcount,(int)rob3->ptr);

    return;
}