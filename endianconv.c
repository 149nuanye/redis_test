#include "endianconv.h"

uint32_t intrev32(uint32_t v) {
    memrev32(&v);
    return v;
}

/* Toggle the 32 bit unsigned integer pointed by *p from little endian to
 * big endian */
 void memrev32(void *p) {
    unsigned char *x = p, t;

    t = x[0];
    x[0] = x[3];
    x[3] = t;
    t = x[1];
    x[1] = x[2];
    x[2] = t;
}