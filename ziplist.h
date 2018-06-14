#ifndef ZIPLIST_H
#define ZIPLIST_H
#include <stdlib.h>
#define ZIPLIST_HEAD 0
#define ZIPLIST_TAIL 1
#define ZIP_END 255
#define ZIP_BIGLEN 254

#define ZIPLIST_BYTES(zl)       (*((uint32_t*)(zl)))
#define ZIPLIST_TAIL_OFFSET(zl) (*((uint32_t*)((zl)+sizeof(uint32_t))))
#define ZIPLIST_LENGTH(zl)      (*((uint16_t*)((zl)+sizeof(uint32_t)*2)))
#define ZIPLIST_HEADER_SIZE     (sizeof(uint32_t)*2+sizeof(uint16_t))

unsigned char *ziplistNew(void);
unsigned int ziplistLen(unsigned char *zl);
#endif