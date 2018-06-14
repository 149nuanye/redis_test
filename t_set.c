#include "server.h"

unsigned long setTypeSize(robj *subject) {
    if (subject->encoding == OBJ_ENCODING_HT) {
        return dictSize((dict*)subject->ptr);
    } else if (subject->encoding == OBJ_ENCODING_INTSET) {
        return intsetLen((intset*)subject->ptr);
    } else {
        serverPanic("Unknown set encoding");
    }
}