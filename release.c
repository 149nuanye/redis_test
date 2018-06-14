#include <string.h>
#include "release.h"

char *redisGitDirty(void) {
    return REDIS_GIT_DIRTY;
}

char *redisGitSHA1(void) {
    return REDIS_GIT_SHA1;
}