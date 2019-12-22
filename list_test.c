#include "redis_test.h"
#include "quicklist.h"
#include "ziplist.h"
#include "intset.h"

robj *createQuicklistObject(void) {
    quicklist *l = quicklistCreate();
    robj *o = createObject(OBJ_LIST,l);
    o->encoding = OBJ_ENCODING_QUICKLIST;
    return o;
}

robj *createZiplistObject(void) {
    unsigned char *zl = ziplistNew();
    robj *o = createObject(OBJ_LIST,zl);
    o->encoding = OBJ_ENCODING_ZIPLIST;
    return o;
}

void list_object(){
    intset *ints1 = intsetNew();
    intsetAdd(ints1, 1, NULL);
    printf("ints1 encoding:%d,length:%d\n",
        ints1->encoding,ints1->length);
    intsetAdd(ints1, 1000000, NULL);
    printf("ints1 encoding:%d,length:%d\n",
        ints1->encoding,ints1->length);
    // robj *rob1 = createZiplistObject();
    return;
}