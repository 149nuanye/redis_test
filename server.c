#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <locale.h>
#include <sys/socket.h>

#include "config.h"
#include "sds.h"
#include "dict.h"
#include "zmalloc.h"
#include "server.h"
#include "cluster.h"
#include "slowlog.h"

/* Our shared "common" objects */
struct sharedObjectsStruct shared;

double R_Zero, R_PosInf, R_NegInf, R_Nan;

/* Global vars */
struct redisServer server; /* server global state */

unsigned int dictEncObjHash(const void *key) {
    robj *o = (robj*) key;

    if (sdsEncodedObject(o)) {
        return dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
    } else {
        if (o->encoding == OBJ_ENCODING_INT) {
            char buf[32];
            int len;

            len = ll2string(buf,32,(long)o->ptr);
            return dictGenHashFunction((unsigned char*)buf, len);
        } else {
            unsigned int hash;

            o = getDecodedObject(o);
            hash = dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
            decrRefCount(o);
            return hash;
        }
    }
}

int dictEncObjKeyCompare(void *privdata, const void *key1,
    const void *key2)
{
    robj *o1 = (robj*) key1, *o2 = (robj*) key2;
    int cmp;

    if (o1->encoding == OBJ_ENCODING_INT &&
        o2->encoding == OBJ_ENCODING_INT)
            return o1->ptr == o2->ptr;

    o1 = getDecodedObject(o1);
    o2 = getDecodedObject(o2);
    cmp = dictSdsKeyCompare(privdata,o1->ptr,o2->ptr);
    decrRefCount(o1);
    decrRefCount(o2);
    return cmp;
}

void dictObjectDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    if (val == NULL) return; /* Values of swapped out keys as set to NULL */
    decrRefCount(val);
}

/* Sets type hash table */
dictType setDictType = {
    dictEncObjHash,            /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictEncObjKeyCompare,      /* key compare */
    dictObjectDestructor, /* key destructor */
    NULL                       /* val destructor */
};

/* Our command table.
 *
 * Every entry is composed of the following fields:
 *
 * name: a string representing the command name.
 * function: pointer to the C function implementing the command.
 * arity: number of arguments, it is possible to use -N to say >= N
 * sflags: command flags as string. See below for a table of flags.
 * flags: flags as bitmask. Computed by Redis using the 'sflags' field.
 * get_keys_proc: an optional function to get key arguments from a command.
 *                This is only used when the following three fields are not
 *                enough to specify what arguments are keys.
 * first_key_index: first argument that is a key
 * last_key_index: last argument that is a key
 * key_step: step to get all the keys from first to last argument. For instance
 *           in MSET the step is two since arguments are key,val,key,val,...
 * microseconds: microseconds of total execution time for this command.
 * calls: total number of calls of this command.
 *
 * The flags, microseconds and calls fields are computed by Redis and should
 * always be set to zero.
 *
 * Command flags are expressed using strings where every character represents
 * a flag. Later the populateCommandTable() function will take care of
 * populating the real 'flags' field using this characters.
 *
 * This is the meaning of the flags:
 *
 * w: write command (may modify the key space).
 * r: read command  (will never modify the key space).
 * m: may increase memory usage once called. Don't allow if out of memory.
 * a: admin command, like SAVE or SHUTDOWN.
 * p: Pub/Sub related command.
 * f: force replication of this command, regardless of server.dirty.
 * s: command not allowed in scripts.
 * R: random command. Command is not deterministic, that is, the same command
 *    with the same arguments, with the same key space, may have different
 *    results. For instance SPOP and RANDOMKEY are two random commands.
 * S: Sort command output array if called from script, so that the output
 *    is deterministic.
 * l: Allow command while loading the database.
 * t: Allow command while a slave has stale data but is not allowed to
 *    server this data. Normally no command is accepted in this condition
 *    but just a few.
 * M: Do not automatically propagate the command on MONITOR.
 * k: Perform an implicit ASKING for this command, so the command will be
 *    accepted in cluster mode if the slot is marked as 'importing'.
 * F: Fast command: O(1) or O(log(N)) command that should never delay
 *    its execution as long as the kernel scheduler is giving us time.
 *    Note that commands that may trigger a DEL as a side effect (like SET)
 *    are not fast commands.
 */
struct redisCommand redisCommandTable[] = {
    {"get",getCommand,2,"rF",0,NULL,1,1,1,0,0},
    // {"set",setCommand,-3,"wm",0,NULL,1,1,1,0,0},
    // {"setnx",setnxCommand,3,"wmF",0,NULL,1,1,1,0,0},
    // {"setex",setexCommand,4,"wm",0,NULL,1,1,1,0,0},
    // {"psetex",psetexCommand,4,"wm",0,NULL,1,1,1,0,0},
    // {"append",appendCommand,3,"wm",0,NULL,1,1,1,0,0},
    // {"strlen",strlenCommand,2,"rF",0,NULL,1,1,1,0,0},
    {"del",delCommand,-2,"w",0,NULL,1,-1,1,0,0},
    // {"exists",existsCommand,-2,"rF",0,NULL,1,-1,1,0,0},
    // {"setbit",setbitCommand,4,"wm",0,NULL,1,1,1,0,0},
    // {"getbit",getbitCommand,3,"rF",0,NULL,1,1,1,0,0},
    // {"bitfield",bitfieldCommand,-2,"wm",0,NULL,1,1,1,0,0},
    // {"setrange",setrangeCommand,4,"wm",0,NULL,1,1,1,0,0},
    // {"getrange",getrangeCommand,4,"r",0,NULL,1,1,1,0,0},
    // {"substr",getrangeCommand,4,"r",0,NULL,1,1,1,0,0},
    // {"incr",incrCommand,2,"wmF",0,NULL,1,1,1,0,0},
    // {"decr",decrCommand,2,"wmF",0,NULL,1,1,1,0,0},
    // {"mget",mgetCommand,-2,"r",0,NULL,1,-1,1,0,0},
    // {"rpush",rpushCommand,-3,"wmF",0,NULL,1,1,1,0,0},
    // {"lpush",lpushCommand,-3,"wmF",0,NULL,1,1,1,0,0},
    // {"rpushx",rpushxCommand,3,"wmF",0,NULL,1,1,1,0,0},
    // {"lpushx",lpushxCommand,3,"wmF",0,NULL,1,1,1,0,0},
    // {"linsert",linsertCommand,5,"wm",0,NULL,1,1,1,0,0},
    // {"rpop",rpopCommand,2,"wF",0,NULL,1,1,1,0,0},
    // {"lpop",lpopCommand,2,"wF",0,NULL,1,1,1,0,0},
    // {"brpop",brpopCommand,-3,"ws",0,NULL,1,-2,1,0,0},
    // {"brpoplpush",brpoplpushCommand,4,"wms",0,NULL,1,2,1,0,0},
    // {"blpop",blpopCommand,-3,"ws",0,NULL,1,-2,1,0,0},
    // {"llen",llenCommand,2,"rF",0,NULL,1,1,1,0,0},
    // {"lindex",lindexCommand,3,"r",0,NULL,1,1,1,0,0},
    // {"lset",lsetCommand,4,"wm",0,NULL,1,1,1,0,0},
    // {"lrange",lrangeCommand,4,"r",0,NULL,1,1,1,0,0},
    // {"ltrim",ltrimCommand,4,"w",0,NULL,1,1,1,0,0},
    // {"lrem",lremCommand,4,"w",0,NULL,1,1,1,0,0},
    // {"rpoplpush",rpoplpushCommand,3,"wm",0,NULL,1,2,1,0,0},
    // {"sadd",saddCommand,-3,"wmF",0,NULL,1,1,1,0,0},
    // {"srem",sremCommand,-3,"wF",0,NULL,1,1,1,0,0},
    // {"smove",smoveCommand,4,"wF",0,NULL,1,2,1,0,0},
    // {"sismember",sismemberCommand,3,"rF",0,NULL,1,1,1,0,0},
    // {"scard",scardCommand,2,"rF",0,NULL,1,1,1,0,0},
    // {"spop",spopCommand,-2,"wRF",0,NULL,1,1,1,0,0},
    // {"srandmember",srandmemberCommand,-2,"rR",0,NULL,1,1,1,0,0},
    // {"sinter",sinterCommand,-2,"rS",0,NULL,1,-1,1,0,0},
    // {"sinterstore",sinterstoreCommand,-3,"wm",0,NULL,1,-1,1,0,0},
    // {"sunion",sunionCommand,-2,"rS",0,NULL,1,-1,1,0,0},
    // {"sunionstore",sunionstoreCommand,-3,"wm",0,NULL,1,-1,1,0,0},
    // {"sdiff",sdiffCommand,-2,"rS",0,NULL,1,-1,1,0,0},
    // {"sdiffstore",sdiffstoreCommand,-3,"wm",0,NULL,1,-1,1,0,0},
    // {"smembers",sinterCommand,2,"rS",0,NULL,1,1,1,0,0},
    // {"sscan",sscanCommand,-3,"rR",0,NULL,1,1,1,0,0},
    // {"zadd",zaddCommand,-4,"wmF",0,NULL,1,1,1,0,0},
    // {"zincrby",zincrbyCommand,4,"wmF",0,NULL,1,1,1,0,0},
    // {"zrem",zremCommand,-3,"wF",0,NULL,1,1,1,0,0},
    // {"zremrangebyscore",zremrangebyscoreCommand,4,"w",0,NULL,1,1,1,0,0},
    // {"zremrangebyrank",zremrangebyrankCommand,4,"w",0,NULL,1,1,1,0,0},
    // {"zremrangebylex",zremrangebylexCommand,4,"w",0,NULL,1,1,1,0,0},
    // {"zunionstore",zunionstoreCommand,-4,"wm",0,zunionInterGetKeys,0,0,0,0,0},
    // {"zinterstore",zinterstoreCommand,-4,"wm",0,zunionInterGetKeys,0,0,0,0,0},
    // {"zrange",zrangeCommand,-4,"r",0,NULL,1,1,1,0,0},
    // {"zrangebyscore",zrangebyscoreCommand,-4,"r",0,NULL,1,1,1,0,0},
    // {"zrevrangebyscore",zrevrangebyscoreCommand,-4,"r",0,NULL,1,1,1,0,0},
    // {"zrangebylex",zrangebylexCommand,-4,"r",0,NULL,1,1,1,0,0},
    // {"zrevrangebylex",zrevrangebylexCommand,-4,"r",0,NULL,1,1,1,0,0},
    // {"zcount",zcountCommand,4,"rF",0,NULL,1,1,1,0,0},
    // {"zlexcount",zlexcountCommand,4,"rF",0,NULL,1,1,1,0,0},
    // {"zrevrange",zrevrangeCommand,-4,"r",0,NULL,1,1,1,0,0},
    // {"zcard",zcardCommand,2,"rF",0,NULL,1,1,1,0,0},
    // {"zscore",zscoreCommand,3,"rF",0,NULL,1,1,1,0,0},
    // {"zrank",zrankCommand,3,"rF",0,NULL,1,1,1,0,0},
    // {"zrevrank",zrevrankCommand,3,"rF",0,NULL,1,1,1,0,0},
    // {"zscan",zscanCommand,-3,"rR",0,NULL,1,1,1,0,0},
    // {"hset",hsetCommand,4,"wmF",0,NULL,1,1,1,0,0},
    // {"hsetnx",hsetnxCommand,4,"wmF",0,NULL,1,1,1,0,0},
    // {"hget",hgetCommand,3,"rF",0,NULL,1,1,1,0,0},
    // {"hmset",hmsetCommand,-4,"wm",0,NULL,1,1,1,0,0},
    // {"hmget",hmgetCommand,-3,"r",0,NULL,1,1,1,0,0},
    // {"hincrby",hincrbyCommand,4,"wmF",0,NULL,1,1,1,0,0},
    // {"hincrbyfloat",hincrbyfloatCommand,4,"wmF",0,NULL,1,1,1,0,0},
    // {"hdel",hdelCommand,-3,"wF",0,NULL,1,1,1,0,0},
    // {"hlen",hlenCommand,2,"rF",0,NULL,1,1,1,0,0},
    // {"hstrlen",hstrlenCommand,3,"rF",0,NULL,1,1,1,0,0},
    // {"hkeys",hkeysCommand,2,"rS",0,NULL,1,1,1,0,0},
    // {"hvals",hvalsCommand,2,"rS",0,NULL,1,1,1,0,0},
    // {"hgetall",hgetallCommand,2,"r",0,NULL,1,1,1,0,0},
    // {"hexists",hexistsCommand,3,"rF",0,NULL,1,1,1,0,0},
    // {"hscan",hscanCommand,-3,"rR",0,NULL,1,1,1,0,0},
    // {"incrby",incrbyCommand,3,"wmF",0,NULL,1,1,1,0,0},
    // {"decrby",decrbyCommand,3,"wmF",0,NULL,1,1,1,0,0},
    // {"incrbyfloat",incrbyfloatCommand,3,"wmF",0,NULL,1,1,1,0,0},
    // {"getset",getsetCommand,3,"wm",0,NULL,1,1,1,0,0},
    // {"mset",msetCommand,-3,"wm",0,NULL,1,-1,2,0,0},
    // {"msetnx",msetnxCommand,-3,"wm",0,NULL,1,-1,2,0,0},
    // {"randomkey",randomkeyCommand,1,"rR",0,NULL,0,0,0,0,0},
    // {"select",selectCommand,2,"lF",0,NULL,0,0,0,0,0},
    // {"move",moveCommand,3,"wF",0,NULL,1,1,1,0,0},
    // {"rename",renameCommand,3,"w",0,NULL,1,2,1,0,0},
    // {"renamenx",renamenxCommand,3,"wF",0,NULL,1,2,1,0,0},
    // {"expire",expireCommand,3,"wF",0,NULL,1,1,1,0,0},
    // {"expireat",expireatCommand,3,"wF",0,NULL,1,1,1,0,0},
    // {"pexpire",pexpireCommand,3,"wF",0,NULL,1,1,1,0,0},
    // {"pexpireat",pexpireatCommand,3,"wF",0,NULL,1,1,1,0,0},
    // {"keys",keysCommand,2,"rS",0,NULL,0,0,0,0,0},
    // {"scan",scanCommand,-2,"rR",0,NULL,0,0,0,0,0},
    // {"dbsize",dbsizeCommand,1,"rF",0,NULL,0,0,0,0,0},
    // {"auth",authCommand,2,"sltF",0,NULL,0,0,0,0,0},
    // {"ping",pingCommand,-1,"tF",0,NULL,0,0,0,0,0},
    // {"echo",echoCommand,2,"F",0,NULL,0,0,0,0,0},
    // {"save",saveCommand,1,"as",0,NULL,0,0,0,0,0},
    // {"bgsave",bgsaveCommand,-1,"a",0,NULL,0,0,0,0,0},
    // {"bgrewriteaof",bgrewriteaofCommand,1,"a",0,NULL,0,0,0,0,0},
    // {"shutdown",shutdownCommand,-1,"alt",0,NULL,0,0,0,0,0},
    // {"lastsave",lastsaveCommand,1,"RF",0,NULL,0,0,0,0,0},
    // {"type",typeCommand,2,"rF",0,NULL,1,1,1,0,0},
    // {"multi",multiCommand,1,"sF",0,NULL,0,0,0,0,0},
    // {"exec",execCommand,1,"sM",0,NULL,0,0,0,0,0},
    // {"discard",discardCommand,1,"sF",0,NULL,0,0,0,0,0},
    // {"sync",syncCommand,1,"ars",0,NULL,0,0,0,0,0},
    // {"psync",syncCommand,3,"ars",0,NULL,0,0,0,0,0},
    // {"replconf",replconfCommand,-1,"aslt",0,NULL,0,0,0,0,0},
    // {"flushdb",flushdbCommand,1,"w",0,NULL,0,0,0,0,0},
    // {"flushall",flushallCommand,1,"w",0,NULL,0,0,0,0,0},
    // {"sort",sortCommand,-2,"wm",0,sortGetKeys,1,1,1,0,0},
    // {"info",infoCommand,-1,"lt",0,NULL,0,0,0,0,0},
    // {"monitor",monitorCommand,1,"as",0,NULL,0,0,0,0,0},
    // {"ttl",ttlCommand,2,"rF",0,NULL,1,1,1,0,0},
    // {"touch",touchCommand,-2,"rF",0,NULL,1,1,1,0,0},
    // {"pttl",pttlCommand,2,"rF",0,NULL,1,1,1,0,0},
    // {"persist",persistCommand,2,"wF",0,NULL,1,1,1,0,0},
    // {"slaveof",slaveofCommand,3,"ast",0,NULL,0,0,0,0,0},
    // {"role",roleCommand,1,"lst",0,NULL,0,0,0,0,0},
    // {"debug",debugCommand,-1,"as",0,NULL,0,0,0,0,0},
    // {"config",configCommand,-2,"lat",0,NULL,0,0,0,0,0},
    // {"subscribe",subscribeCommand,-2,"pslt",0,NULL,0,0,0,0,0},
    // {"unsubscribe",unsubscribeCommand,-1,"pslt",0,NULL,0,0,0,0,0},
    // {"psubscribe",psubscribeCommand,-2,"pslt",0,NULL,0,0,0,0,0},
    // {"punsubscribe",punsubscribeCommand,-1,"pslt",0,NULL,0,0,0,0,0},
    // {"publish",publishCommand,3,"pltF",0,NULL,0,0,0,0,0},
    // {"pubsub",pubsubCommand,-2,"pltR",0,NULL,0,0,0,0,0},
    // {"watch",watchCommand,-2,"sF",0,NULL,1,-1,1,0,0},
    // {"unwatch",unwatchCommand,1,"sF",0,NULL,0,0,0,0,0},
    // {"cluster",clusterCommand,-2,"a",0,NULL,0,0,0,0,0},
    // {"restore",restoreCommand,-4,"wm",0,NULL,1,1,1,0,0},
    // {"restore-asking",restoreCommand,-4,"wmk",0,NULL,1,1,1,0,0},
    // {"migrate",migrateCommand,-6,"w",0,migrateGetKeys,0,0,0,0,0},
    // {"asking",askingCommand,1,"F",0,NULL,0,0,0,0,0},
    // {"readonly",readonlyCommand,1,"F",0,NULL,0,0,0,0,0},
    // {"readwrite",readwriteCommand,1,"F",0,NULL,0,0,0,0,0},
    // {"dump",dumpCommand,2,"r",0,NULL,1,1,1,0,0},
    // {"object",objectCommand,3,"r",0,NULL,2,2,2,0,0},
    // {"client",clientCommand,-2,"as",0,NULL,0,0,0,0,0},
    // {"eval",evalCommand,-3,"s",0,evalGetKeys,0,0,0,0,0},
    // {"evalsha",evalShaCommand,-3,"s",0,evalGetKeys,0,0,0,0,0},
    // {"slowlog",slowlogCommand,-2,"a",0,NULL,0,0,0,0,0},
    // {"script",scriptCommand,-2,"s",0,NULL,0,0,0,0,0},
    // {"time",timeCommand,1,"RF",0,NULL,0,0,0,0,0},
    // {"bitop",bitopCommand,-4,"wm",0,NULL,2,-1,1,0,0},
    // {"bitcount",bitcountCommand,-2,"r",0,NULL,1,1,1,0,0},
    // {"bitpos",bitposCommand,-3,"r",0,NULL,1,1,1,0,0},
    // {"wait",waitCommand,3,"s",0,NULL,0,0,0,0,0},
    // {"command",commandCommand,0,"lt",0,NULL,0,0,0,0,0},
    // {"geoadd",geoaddCommand,-5,"wm",0,NULL,1,1,1,0,0},
    // {"georadius",georadiusCommand,-6,"w",0,georadiusGetKeys,1,1,1,0,0},
    // {"georadius_ro",georadiusroCommand,-6,"r",0,georadiusGetKeys,1,1,1,0,0},
    // {"georadiusbymember",georadiusbymemberCommand,-5,"w",0,georadiusGetKeys,1,1,1,0,0},
    // {"georadiusbymember_ro",georadiusbymemberroCommand,-5,"r",0,georadiusGetKeys,1,1,1,0,0},
    // {"geohash",geohashCommand,-2,"r",0,NULL,1,1,1,0,0},
    // {"geopos",geoposCommand,-2,"r",0,NULL,1,1,1,0,0},
    // {"geodist",geodistCommand,-4,"r",0,NULL,1,1,1,0,0},
    // {"pfselftest",pfselftestCommand,1,"a",0,NULL,0,0,0,0,0},
    // {"pfadd",pfaddCommand,-2,"wmF",0,NULL,1,1,1,0,0},
    // {"pfcount",pfcountCommand,-2,"r",0,NULL,1,-1,1,0,0},
    // {"pfmerge",pfmergeCommand,-2,"wm",0,NULL,1,-1,1,0,0},
    // {"pfdebug",pfdebugCommand,-3,"w",0,NULL,0,0,0,0,0},
    // {"post",securityWarningCommand,-1,"lt",0,NULL,0,0,0,0,0},
    // {"host:",securityWarningCommand,-1,"lt",0,NULL,0,0,0,0,0},
    // {"latency",latencyCommand,-2,"aslt",0,NULL,0,0,0,0,0}
};

unsigned int dictSdsHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

int dictSdsKeyCompare(void *privdata, const void *key1,
    const void *key2)
{
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void dictSdsDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);
    sdsfree(val);
}

unsigned int dictSdsCaseHash(const void *key) {
    return dictGenCaseHashFunction((unsigned char*)key, sdslen((char*)key));
}

/* Lookup the command in the current table, if not found also check in
 * the original table containing the original command names unaffected by
 * redis.conf rename-command statement.
 *
 * This is used by functions rewriting the argument vector such as
 * rewriteClientCommandVector() in order to set client->cmd pointer
 * correctly even if the command was renamed. */
struct redisCommand *lookupCommandOrOriginal(sds name) {
    struct redisCommand *cmd = dictFetchValue(server.commands, name);

    if (!cmd) cmd = dictFetchValue(server.orig_commands,name);
    return cmd;
}

/* A case insensitive version used for the command lookup table and other
 * places where case insensitive non binary-safe comparison is needed. */
int dictSdsKeyCaseCompare(void *privdata, const void *key1,
    const void *key2)
{
    DICT_NOTUSED(privdata);
    return strcasecmp(key1, key2) == 0;
}

/* Return the UNIX time in microseconds */
long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec)*1000000;
    ust += tv.tv_usec;
    return ust;
}

/* Return the UNIX time in milliseconds */
mstime_t mstime(void) {
    return ustime()/1000;
}

unsigned int getLRUClock(void) {
    return (mstime()/LRU_CLOCK_RESOLUTION) & LRU_CLOCK_MAX;
}

/* Generate the Redis "Run ID", a SHA1-sized random number that identifies a
 * given execution of Redis, so that if you are talking with an instance
 * having run_id == A, and you reconnect and it has run_id == B, you can be
 * sure that it is either a different instance or it was restarted. */
void getRandomHexChars(char *p, unsigned int len) {
    char *charset = "0123456789abcdef";
    unsigned int j;
    /* Global state. */
    static int seed_initialized = 0;
    static unsigned char seed[20]; /* The SHA1 seed, from /dev/urandom. */
    static uint64_t counter = 0; /* The counter we hash with the seed. */

    if (!seed_initialized) {
        /* Initialize a seed and use SHA1 in counter mode, where we hash
         * the same seed with a progressive counter. For the goals of this
         * function we just need non-colliding strings, there are no
         * cryptographic security needs. */
        FILE *fp = fopen("/dev/urandom","r");
        if (fp && fread(seed,sizeof(seed),1,fp) == 1)
            seed_initialized = 1;
        if (fp) fclose(fp);
    }
    if (seed_initialized) {
        while(len) {
            unsigned char digest[20];
            SHA1_CTX ctx;
            unsigned int copylen = len > 20 ? 20 : len;

            SHA1Init(&ctx);
            SHA1Update(&ctx, seed, sizeof(seed));
            SHA1Update(&ctx, (unsigned char*)&counter,sizeof(counter));
            SHA1Final(digest, &ctx);
            counter++;

            memcpy(p,digest,copylen);
            /* Convert to hex digits. */
            for (j = 0; j < copylen; j++) p[j] = charset[p[j] & 0x0F];
            len -= copylen;
            p += copylen;
        }
    } else {
        /* If we can't read from /dev/urandom, do some reasonable effort
         * in order to create some entropy, since this function is used to
         * generate run_id and cluster instance IDs */
        char *x = p;
        unsigned int l = len;
        struct timeval tv;
        pid_t pid = getpid();

        /* Use time and PID to fill the initial array. */
        gettimeofday(&tv,NULL);
        if (l >= sizeof(tv.tv_usec)) {
            memcpy(x,&tv.tv_usec,sizeof(tv.tv_usec));
            l -= sizeof(tv.tv_usec);
            x += sizeof(tv.tv_usec);
        }
        if (l >= sizeof(tv.tv_sec)) {
            memcpy(x,&tv.tv_sec,sizeof(tv.tv_sec));
            l -= sizeof(tv.tv_sec);
            x += sizeof(tv.tv_sec);
        }
        if (l >= sizeof(pid)) {
            memcpy(x,&pid,sizeof(pid));
            l -= sizeof(pid);
            x += sizeof(pid);
        }
        /* Finally xor it with rand() output, that was already seeded with
         * time() at startup, and convert to hex digits. */
        for (j = 0; j < len; j++) {
            p[j] ^= rand();
            p[j] = charset[p[j] & 0x0F];
        }
    }
}

/* Migrate cache dict type. */
dictType migrateCacheDictType = {
    dictSdsHash,                /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictSdsKeyCompare,          /* key compare */
    dictSdsDestructor,          /* key destructor */
    NULL                        /* val destructor */
};

/* Command table. sds string -> command struct pointer. */
dictType commandTableDictType = {
    dictSdsCaseHash,           /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictSdsKeyCaseCompare,     /* key compare */
    dictSdsDestructor,         /* key destructor */
    NULL                       /* val destructor */
};

struct redisCommand *lookupCommandByCString(char *s) {
    struct redisCommand *cmd;
    sds name = sdsnew(s);
    cmd = dictFetchValue(server.commands, name);
    sdsfree(name);
    return cmd;
}

struct redisCommand *lookupCommand(sds name) {
    return dictFetchValue(server.commands, name);
}
/* Populates the Redis Command Table starting from the hard coded list
 * we have on top of redis.c file. */
void populateCommandTable(void) {
    int j;
    int numcommands = sizeof(redisCommandTable)/sizeof(struct redisCommand);
    for (j = 0; j < numcommands; j++) {
        struct redisCommand *c = redisCommandTable+j;
        char *f = c->sflags;
        int retval1, retval2;

        while(*f != '\0') {
            switch(*f) {
            case 'w': c->flags |= CMD_WRITE; break;
            case 'r': c->flags |= CMD_READONLY; break;
            case 'm': c->flags |= CMD_DENYOOM; break;
            case 'a': c->flags |= CMD_ADMIN; break;
            case 'p': c->flags |= CMD_PUBSUB; break;
            case 's': c->flags |= CMD_NOSCRIPT; break;
            case 'R': c->flags |= CMD_RANDOM; break;
            case 'S': c->flags |= CMD_SORT_FOR_SCRIPT; break;
            case 'l': c->flags |= CMD_LOADING; break;
            case 't': c->flags |= CMD_STALE; break;
            case 'M': c->flags |= CMD_SKIP_MONITOR; break;
            case 'k': c->flags |= CMD_ASKING; break;
            case 'F': c->flags |= CMD_FAST; break;
            default: serverPanic("Unsupported command flag"); break;
            }
            f++;
        }
        retval1 = dictAdd(server.commands, sdsnew(c->name), c);
        /* Populate an additional dictionary that will be unaffected
         * by rename-command statements in redis.conf. */
        retval2 = dictAdd(server.orig_commands, sdsnew(c->name), c);
        serverAssert(retval1 == DICT_OK && retval2 == DICT_OK);
    }
}

void initServerConfig(void) {
    int j;
    getRandomHexChars(server.runid,CONFIG_RUN_ID_SIZE);
    server.configfile = NULL;
    server.executable = NULL;
    server.hz = CONFIG_DEFAULT_HZ;
    server.runid[CONFIG_RUN_ID_SIZE] = '\0';
    server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
    server.port = CONFIG_DEFAULT_SERVER_PORT;
    server.tcp_backlog = CONFIG_DEFAULT_TCP_BACKLOG;
    server.bindaddr_count = 0;
    server.unixsocket = NULL;
    server.unixsocketperm = CONFIG_DEFAULT_UNIX_SOCKET_PERM;
    server.ipfd_count = 0;
    server.sofd = -1;
    server.protected_mode = CONFIG_DEFAULT_PROTECTED_MODE;
    server.dbnum = CONFIG_DEFAULT_DBNUM;
    server.verbosity = CONFIG_DEFAULT_VERBOSITY;
    server.maxidletime = CONFIG_DEFAULT_CLIENT_TIMEOUT;
    server.tcpkeepalive = CONFIG_DEFAULT_TCP_KEEPALIVE;
    server.active_expire_enabled = 1;
    server.client_max_querybuf_len = PROTO_MAX_QUERYBUF_LEN;
    server.saveparams = NULL;
    server.loading = 0;
    server.logfile = zstrdup(CONFIG_DEFAULT_LOGFILE);
    server.syslog_enabled = CONFIG_DEFAULT_SYSLOG_ENABLED;
    server.syslog_ident = zstrdup(CONFIG_DEFAULT_SYSLOG_IDENT);
    server.syslog_facility = LOG_LOCAL0;
    server.daemonize = CONFIG_DEFAULT_DAEMONIZE;
    server.supervised = 0;
    server.supervised_mode = SUPERVISED_NONE;
    server.aof_state = AOF_OFF;
    server.aof_fsync = CONFIG_DEFAULT_AOF_FSYNC;
    server.aof_no_fsync_on_rewrite = CONFIG_DEFAULT_AOF_NO_FSYNC_ON_REWRITE;
    server.aof_rewrite_perc = AOF_REWRITE_PERC;
    server.aof_rewrite_min_size = AOF_REWRITE_MIN_SIZE;
    server.aof_rewrite_base_size = 0;
    server.aof_rewrite_scheduled = 0;
    server.aof_last_fsync = time(NULL);
    server.aof_rewrite_time_last = -1;
    server.aof_rewrite_time_start = -1;
    server.aof_lastbgrewrite_status = C_OK;
    server.aof_delayed_fsync = 0;
    server.aof_fd = -1;
    server.aof_selected_db = -1; /* Make sure the first time will not match */
    server.aof_flush_postponed_start = 0;
    server.aof_rewrite_incremental_fsync = CONFIG_DEFAULT_AOF_REWRITE_INCREMENTAL_FSYNC;
    server.aof_load_truncated = CONFIG_DEFAULT_AOF_LOAD_TRUNCATED;
    server.pidfile = NULL;
    server.rdb_filename = zstrdup(CONFIG_DEFAULT_RDB_FILENAME);
    server.aof_filename = zstrdup(CONFIG_DEFAULT_AOF_FILENAME);
    server.requirepass = NULL;
    server.rdb_compression = CONFIG_DEFAULT_RDB_COMPRESSION;
    server.rdb_checksum = CONFIG_DEFAULT_RDB_CHECKSUM;
    server.stop_writes_on_bgsave_err = CONFIG_DEFAULT_STOP_WRITES_ON_BGSAVE_ERROR;
    server.activerehashing = CONFIG_DEFAULT_ACTIVE_REHASHING;
    server.notify_keyspace_events = 0;
    server.maxclients = CONFIG_DEFAULT_MAX_CLIENTS;
    server.bpop_blocked_clients = 0;
    server.maxmemory = CONFIG_DEFAULT_MAXMEMORY;
    server.maxmemory_policy = CONFIG_DEFAULT_MAXMEMORY_POLICY;
    server.maxmemory_samples = CONFIG_DEFAULT_MAXMEMORY_SAMPLES;
    server.hash_max_ziplist_entries = OBJ_HASH_MAX_ZIPLIST_ENTRIES;
    server.hash_max_ziplist_value = OBJ_HASH_MAX_ZIPLIST_VALUE;
    server.list_max_ziplist_size = OBJ_LIST_MAX_ZIPLIST_SIZE;
    server.list_compress_depth = OBJ_LIST_COMPRESS_DEPTH;
    server.set_max_intset_entries = OBJ_SET_MAX_INTSET_ENTRIES;
    server.zset_max_ziplist_entries = OBJ_ZSET_MAX_ZIPLIST_ENTRIES;
    server.zset_max_ziplist_value = OBJ_ZSET_MAX_ZIPLIST_VALUE;
    server.hll_sparse_max_bytes = CONFIG_DEFAULT_HLL_SPARSE_MAX_BYTES;
    server.shutdown_asap = 0;
    server.repl_ping_slave_period = CONFIG_DEFAULT_REPL_PING_SLAVE_PERIOD;
    server.repl_timeout = CONFIG_DEFAULT_REPL_TIMEOUT;
    server.repl_min_slaves_to_write = CONFIG_DEFAULT_MIN_SLAVES_TO_WRITE;
    server.repl_min_slaves_max_lag = CONFIG_DEFAULT_MIN_SLAVES_MAX_LAG;
    server.cluster_enabled = 0;
    server.cluster_node_timeout = CLUSTER_DEFAULT_NODE_TIMEOUT;
    server.cluster_migration_barrier = CLUSTER_DEFAULT_MIGRATION_BARRIER;
    server.cluster_slave_validity_factor = CLUSTER_DEFAULT_SLAVE_VALIDITY;
    server.cluster_require_full_coverage = CLUSTER_DEFAULT_REQUIRE_FULL_COVERAGE;
    server.cluster_configfile = zstrdup(CONFIG_DEFAULT_CLUSTER_CONFIG_FILE);
    server.migrate_cached_sockets = dictCreate(&migrateCacheDictType,NULL);
    server.next_client_id = 1; /* Client IDs, start from 1 .*/
    server.loading_process_events_interval_bytes = (1024*1024*2);
    server.lua_time_limit = LUA_SCRIPT_TIME_LIMIT;

    server.lruclock = getLRUClock();
    resetServerSaveParams();

    appendServerSaveParams(60*60,1);  /* save after 1 hour and 1 change */
    appendServerSaveParams(300,100);  /* save after 5 minutes and 100 changes */
    appendServerSaveParams(60,10000); /* save after 1 minute and 10000 changes */
    /* Replication related */
    server.masterauth = NULL;
    server.masterhost = NULL;
    server.masterport = 6379;
    server.master = NULL;
    server.cached_master = NULL;
    server.repl_master_initial_offset = -1;
    server.repl_state = REPL_STATE_NONE;
    server.repl_syncio_timeout = CONFIG_REPL_SYNCIO_TIMEOUT;
    server.repl_serve_stale_data = CONFIG_DEFAULT_SLAVE_SERVE_STALE_DATA;
    server.repl_slave_ro = CONFIG_DEFAULT_SLAVE_READ_ONLY;
    server.repl_down_since = 0; /* Never connected, repl is down since EVER. */
    server.repl_disable_tcp_nodelay = CONFIG_DEFAULT_REPL_DISABLE_TCP_NODELAY;
    server.repl_diskless_sync = CONFIG_DEFAULT_REPL_DISKLESS_SYNC;
    server.repl_diskless_sync_delay = CONFIG_DEFAULT_REPL_DISKLESS_SYNC_DELAY;
    server.slave_priority = CONFIG_DEFAULT_SLAVE_PRIORITY;
    server.slave_announce_ip = CONFIG_DEFAULT_SLAVE_ANNOUNCE_IP;
    server.slave_announce_port = CONFIG_DEFAULT_SLAVE_ANNOUNCE_PORT;
    server.master_repl_offset = 0;

    /* Replication partial resync backlog */
    server.repl_backlog = NULL;
    server.repl_backlog_size = CONFIG_DEFAULT_REPL_BACKLOG_SIZE;
    server.repl_backlog_histlen = 0;
    server.repl_backlog_idx = 0;
    server.repl_backlog_off = 0;
    server.repl_backlog_time_limit = CONFIG_DEFAULT_REPL_BACKLOG_TIME_LIMIT;
    server.repl_no_slaves_since = time(NULL);

    /* Client output buffer limits */
    for (j = 0; j < CLIENT_TYPE_OBUF_COUNT; j++)
        server.client_obuf_limits[j] = clientBufferLimitsDefaults[j];

    /* Double constants initialization */
    R_Zero = 0.0;
    R_PosInf = 1.0/R_Zero;
    R_NegInf = -1.0/R_Zero;
    R_Nan = R_Zero/R_Zero;

    /* Command table -- we initiialize it here as it is part of the
     * initial configuration, since command names may be changed via
     * redis.conf using the rename-command directive. */
    server.commands = dictCreate(&commandTableDictType,NULL);
    server.orig_commands = dictCreate(&commandTableDictType,NULL);
    populateCommandTable();
    server.delCommand = lookupCommandByCString("del");
    server.multiCommand = lookupCommandByCString("multi");
    server.lpushCommand = lookupCommandByCString("lpush");
    server.lpopCommand = lookupCommandByCString("lpop");
    server.rpopCommand = lookupCommandByCString("rpop");
    server.sremCommand = lookupCommandByCString("srem");
    server.execCommand = lookupCommandByCString("exec");
    server.expireCommand = lookupCommandByCString("expire");
    server.pexpireCommand = lookupCommandByCString("pexpire");

    /* Slow log */
    server.slowlog_log_slower_than = CONFIG_DEFAULT_SLOWLOG_LOG_SLOWER_THAN;
    server.slowlog_max_len = CONFIG_DEFAULT_SLOWLOG_MAX_LEN;

    /* Latency monitor */
    server.latency_monitor_threshold = CONFIG_DEFAULT_LATENCY_MONITOR_THRESHOLD;

    /* Debugging */
    server.assert_failed = "<no assertion failed>";
    server.assert_file = "<no file>";
    server.assert_line = 0;
    server.bug_report_start = 0;
    server.watchdog_period = 0;
}

/* Low level logging. To use only for very big messages, otherwise
 * serverLog() is to prefer. */
void serverLogRaw(int level, const char *msg) {
    const int syslogLevelMap[] = { LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING };
    const char *c = ".-*#";
    FILE *fp;
    char buf[64];
    int rawmode = (level & LL_RAW);
    int log_to_stdout = server.logfile[0] == '\0';

    level &= 0xff; /* clear flags */
    if (level < server.verbosity) return;

    fp = log_to_stdout ? stdout : fopen(server.logfile,"a");
    if (!fp) return;

    if (rawmode) {
        fprintf(fp,"%s",msg);
    } else {
        int off;
        struct timeval tv;
        int role_char;
        pid_t pid = getpid();

        gettimeofday(&tv,NULL);
        off = strftime(buf,sizeof(buf),"%d %b %H:%M:%S.",localtime(&tv.tv_sec));
        snprintf(buf+off,sizeof(buf)-off,"%03d",(int)tv.tv_usec/1000);
        if (server.sentinel_mode) {
            role_char = 'X'; /* Sentinel. */
        } else if (pid != server.pid) {
            role_char = 'C'; /* RDB / AOF writing child. */
        } else {
            role_char = (server.masterhost ? 'S':'M'); /* Slave or Master. */
        }
        fprintf(fp,"%d:%c %s %c %s\n",
            (int)getpid(),role_char, buf,c[level],msg);
    }
    fflush(fp);

    if (!log_to_stdout) fclose(fp);
    if (server.syslog_enabled) syslog(syslogLevelMap[level], "%s", msg);
}

void daemonize(void) {
    int fd;

    if (fork() != 0) exit(0); /* parent exits */
    setsid(); /* create a new session */

    /* Every output goes to /dev/null. If Redis is daemonized but
     * the 'logfile' is set to 'stdout' in the configuration file
     * it will not log at all. */
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}

int redisSupervisedSystemd(void) {
    const char *notify_socket = getenv("NOTIFY_SOCKET");
    int fd = 1;
    struct sockaddr_un su;
    struct iovec iov;
    struct msghdr hdr;
    int sendto_flags = 0;

    if (!notify_socket) {
        serverLog(LL_WARNING,
                "systemd supervision requested, but NOTIFY_SOCKET not found");
        return 0;
    }

    if ((strchr("@/", notify_socket[0])) == NULL || strlen(notify_socket) < 2) {
        return 0;
    }

    serverLog(LL_NOTICE, "supervised by systemd, will signal readiness");
    if ((fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
        serverLog(LL_WARNING,
                "Can't connect to systemd socket %s", notify_socket);
        return 0;
    }

    memset(&su, 0, sizeof(su));
    su.sun_family = AF_UNIX;
    strncpy (su.sun_path, notify_socket, sizeof(su.sun_path) -1);
    su.sun_path[sizeof(su.sun_path) - 1] = '\0';

    if (notify_socket[0] == '@')
        su.sun_path[0] = '\0';

    memset(&iov, 0, sizeof(iov));
    iov.iov_base = "READY=1";
    iov.iov_len = strlen("READY=1");

    memset(&hdr, 0, sizeof(hdr));
    hdr.msg_name = &su;
    hdr.msg_namelen = offsetof(struct sockaddr_un, sun_path) +
        strlen(notify_socket);
    hdr.msg_iov = &iov;
    hdr.msg_iovlen = 1;

    unsetenv("NOTIFY_SOCKET");
#ifdef HAVE_MSG_NOSIGNAL
    sendto_flags |= MSG_NOSIGNAL;
#endif
    if (sendmsg(fd, &hdr, sendto_flags) < 0) {
        serverLog(LL_WARNING, "Can't send notification to systemd");
        close(fd);
        return 0;
    }
    close(fd);
    return 1;
}


/*
 * Check whether systemd or upstart have been used to start redis.
 */

int redisSupervisedUpstart(void) {
    const char *upstart_job = getenv("UPSTART_JOB");

    if (!upstart_job) {
        serverLog(LL_WARNING,
                "upstart supervision requested, but UPSTART_JOB not found");
        return 0;
    }

    serverLog(LL_NOTICE, "supervised by upstart, will stop to signal readiness");
    raise(SIGSTOP);
    unsetenv("UPSTART_JOB");
    return 1;
}

int redisIsSupervised(int mode) {
    if (mode == SUPERVISED_AUTODETECT) {
        const char *upstart_job = getenv("UPSTART_JOB");
        const char *notify_socket = getenv("NOTIFY_SOCKET");

        if (upstart_job) {
            redisSupervisedUpstart();
        } else if (notify_socket) {
            redisSupervisedSystemd();
        }
    } else if (mode == SUPERVISED_UPSTART) {
        return redisSupervisedUpstart();
    } else if (mode == SUPERVISED_SYSTEMD) {
        return redisSupervisedSystemd();
    }

    return 0;
}

/* Like serverLogRaw() but with printf-alike support. This is the function that
 * is used across the code. The raw version is only used in order to dump
 * the INFO output on crash. */
void serverLog(int level, const char *fmt, ...) {
    va_list ap;
    char msg[LOG_MAX_LEN];

    if ((level&0xff) < server.verbosity) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    serverLogRaw(level,msg);
}

/* Helper function for the activeExpireCycle() function.
 * This function will try to expire the key that is stored in the hash table
 * entry 'de' of the 'expires' hash table of a Redis database.
 *
 * If the key is found to be expired, it is removed from the database and
 * 1 is returned. Otherwise no operation is performed and 0 is returned.
 *
 * When a key is expired, server.stat_expiredkeys is incremented.
 *
 * The parameter 'now' is the current time in milliseconds as is passed
 * to the function to avoid too many gettimeofday() syscalls. */
int activeExpireCycleTryExpire(redisDb *db, dictEntry *de, long long now) {
    // long long t = dictGetSignedIntegerVal(de);
    // if (now > t) {
    //     sds key = dictGetKey(de);
    //     robj *keyobj = createStringObject(key,sdslen(key));

    //     propagateExpire(db,keyobj);
    //     dbDelete(db,keyobj);
    //     notifyKeyspaceEvent(NOTIFY_EXPIRED,
    //         "expired",keyobj,db->id);
    //     decrRefCount(keyobj);
    //     server.stat_expiredkeys++;
    //     return 1;
    // } else {
    //     return 0;
    // }
}

void activeExpireCycle(int type) {
    /* This function has some global state in order to continue the work
     * incrementally across calls. */
    // static unsigned int current_db = 0; /* Last DB tested. */
    // static int timelimit_exit = 0;      /* Time limit hit in previous call? */
    // static long long last_fast_cycle = 0; /* When last fast cycle ran. */

    // int j, iteration = 0;
    // int dbs_per_call = CRON_DBS_PER_CALL;
    // long long start = ustime(), timelimit;

    // /* When clients are paused the dataset should be static not just from the
    //  * POV of clients not being able to write, but also from the POV of
    //  * expires and evictions of keys not being performed. */
    //  if (clientsArePaused()) return;

    // if (type == ACTIVE_EXPIRE_CYCLE_FAST) {
    //     /* Don't start a fast cycle if the previous cycle did not exited
    //      * for time limt. Also don't repeat a fast cycle for the same period
    //      * as the fast cycle total duration itself. */
    //     if (!timelimit_exit) return;
    //     if (start < last_fast_cycle + ACTIVE_EXPIRE_CYCLE_FAST_DURATION*2) return;
    //     last_fast_cycle = start;
    // }

    // /* We usually should test CRON_DBS_PER_CALL per iteration, with
    //  * two exceptions:
    //  *
    //  * 1) Don't test more DBs than we have.
    //  * 2) If last time we hit the time limit, we want to scan all DBs
    //  * in this iteration, as there is work to do in some DB and we don't want
    //  * expired keys to use memory for too much time. */
    // if (dbs_per_call > server.dbnum || timelimit_exit)
    //     dbs_per_call = server.dbnum;

    // /* We can use at max ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC percentage of CPU time
    //  * per iteration. Since this function gets called with a frequency of
    //  * server.hz times per second, the following is the max amount of
    //  * microseconds we can spend in this function. */
    // timelimit = 1000000*ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC/server.hz/100;
    // timelimit_exit = 0;
    // if (timelimit <= 0) timelimit = 1;

    // if (type == ACTIVE_EXPIRE_CYCLE_FAST)
    //     timelimit = ACTIVE_EXPIRE_CYCLE_FAST_DURATION; /* in microseconds. */

    // for (j = 0; j < dbs_per_call; j++) {
    //     int expired;
    //     redisDb *db = server.db+(current_db % server.dbnum);

    //     /* Increment the DB now so we are sure if we run out of time
    //      * in the current DB we'll restart from the next. This allows to
    //      * distribute the time evenly across DBs. */
    //     current_db++;

    //     /* Continue to expire if at the end of the cycle more than 25%
    //      * of the keys were expired. */
    //     do {
    //         unsigned long num, slots;
    //         long long now, ttl_sum;
    //         int ttl_samples;

    //         /* If there is nothing to expire try next DB ASAP. */
    //         if ((num = dictSize(db->expires)) == 0) {
    //             db->avg_ttl = 0;
    //             break;
    //         }
    //         slots = dictSlots(db->expires);
    //         now = mstime();

    //         /* When there are less than 1% filled slots getting random
    //          * keys is expensive, so stop here waiting for better times...
    //          * The dictionary will be resized asap. */
    //         if (num && slots > DICT_HT_INITIAL_SIZE &&
    //             (num*100/slots < 1)) break;

    //         /* The main collection cycle. Sample random keys among keys
    //          * with an expire set, checking for expired ones. */
    //         expired = 0;
    //         ttl_sum = 0;
    //         ttl_samples = 0;

    //         if (num > ACTIVE_EXPIRE_CYCLE_LOOKUPS_PER_LOOP)
    //             num = ACTIVE_EXPIRE_CYCLE_LOOKUPS_PER_LOOP;

    //         while (num--) {
    //             dictEntry *de;
    //             long long ttl;

    //             if ((de = dictGetRandomKey(db->expires)) == NULL) break;
    //             ttl = dictGetSignedIntegerVal(de)-now;
    //             if (activeExpireCycleTryExpire(db,de,now)) expired++;
    //             if (ttl > 0) {
    //                 /* We want the average TTL of keys yet not expired. */
    //                 ttl_sum += ttl;
    //                 ttl_samples++;
    //             }
    //         }

    //         /* Update the average TTL stats for this database. */
    //         if (ttl_samples) {
    //             long long avg_ttl = ttl_sum/ttl_samples;

    //             /* Do a simple running average with a few samples.
    //              * We just use the current estimate with a weight of 2%
    //              * and the previous estimate with a weight of 98%. */
    //             if (db->avg_ttl == 0) db->avg_ttl = avg_ttl;
    //             db->avg_ttl = (db->avg_ttl/50)*49 + (avg_ttl/50);
    //         }

    //         /* We can't block forever here even if there are many keys to
    //          * expire. So after a given amount of milliseconds return to the
    //          * caller waiting for the other active expire cycle. */
    //         iteration++;
    //         if ((iteration & 0xf) == 0) { /* check once every 16 iterations. */
    //             long long elapsed = ustime()-start;

    //             latencyAddSampleIfNeeded("expire-cycle",elapsed/1000);
    //             if (elapsed > timelimit) timelimit_exit = 1;
    //         }
    //         if (timelimit_exit) return;
    //         /* We don't repeat the cycle if there are less than 25% of keys
    //          * found expired in the current DB. */
    //     } while (expired > ACTIVE_EXPIRE_CYCLE_LOOKUPS_PER_LOOP/4);
    // }
}

/* This function gets called every time Redis is entering the
 * main loop of the event driven library, that is, before to sleep
 * for ready file descriptors. */
void beforeSleep(struct aeEventLoop *eventLoop) {
    UNUSED(eventLoop);
    
    /* Call the Redis Cluster before sleep function. Note that this function
    * may change the state of Redis Cluster (from ok to fail or vice versa),
         * so it's a good idea to call it before serving the unblocked clients
      * later in this function. */
    if (server.cluster_enabled) clusterBeforeSleep();

    /* Run a fast expire cycle (the called function will return
     * ASAP if a fast cycle is not needed). */
    if (server.active_expire_enabled && server.masterhost == NULL)
        activeExpireCycle(ACTIVE_EXPIRE_CYCLE_FAST);

    /* Send all the slaves an ACK request if at least one client blocked
     * during the previous event loop iteration. */
    // if (server.get_ack_from_slaves) {
    //     robj *argv[3];

    //     argv[0] = createStringObject("REPLCONF",8);
    //     argv[1] = createStringObject("GETACK",6);
    //     argv[2] = createStringObject("*",1); /* Not used argument. */
    //     replicationFeedSlaves(server.slaves, server.slaveseldb, argv, 3);
    //     decrRefCount(argv[0]);
    //     decrRefCount(argv[1]);
    //     decrRefCount(argv[2]);
    //     server.get_ack_from_slaves = 0;
    // }

    /* Unblock all the clients blocked for synchronous replication
     * in WAIT. */
    // if (listLength(server.clients_waiting_acks))
    //     processClientsWaitingReplicas();

    /* Try to process pending commands for clients that were just unblocked. */
    // if (listLength(server.unblocked_clients))
    //     processUnblockedClients();

    /* Write the AOF buffer on disk */
    // flushAppendOnlyFile(0);

    /* Handle writes with pending output buffers. */
    handleClientsWithPendingWrites();
}

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    return 0;
}

unsigned int dictObjHash(const void *key) {
    const robj *o = key;
    return dictGenHashFunction(o->ptr, sdslen((sds)o->ptr));
}

int dictObjKeyCompare(void *privdata, const void *key1,
    const void *key2)
{
    const robj *o1 = key1, *o2 = key2;
    return dictSdsKeyCompare(privdata,o1->ptr,o2->ptr);
}

void dictListDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);
    listRelease((list*)val);
}

/* Db->expires */
dictType keyptrDictType = {
    dictSdsHash,               /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictSdsKeyCompare,         /* key compare */
    NULL,                      /* key destructor */
    NULL                       /* val destructor */
};

/* Db->dict, keys are sds strings, vals are Redis objects. */
dictType dbDictType = {
    dictSdsHash,                /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictSdsKeyCompare,          /* key compare */
    dictSdsDestructor,          /* key destructor */
    dictObjectDestructor   /* val destructor */
};
/* Keylist hash table type has unencoded redis objects as keys and
 * lists as values. It's used for blocking operations (BLPOP) and to
 * map swapped keys to a list of clients waiting for this keys to be loaded. */
dictType keylistDictType = {
    dictObjHash,                /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictObjKeyCompare,          /* key compare */
    dictObjectDestructor,  /* key destructor */
    dictListDestructor          /* val destructor */
};

/* Create a new eviction pool. */
struct evictionPoolEntry *evictionPoolAlloc(void) {
    struct evictionPoolEntry *ep;
    int j;

    ep = zmalloc(sizeof(*ep)*MAXMEMORY_EVICTION_POOL_SIZE);
    for (j = 0; j < MAXMEMORY_EVICTION_POOL_SIZE; j++) {
        ep[j].idle = 0;
        ep[j].key = NULL;
    }
    return ep;
}

/* Initialize a set of file descriptors to listen to the specified 'port'
 * binding the addresses specified in the Redis server configuration.
 *
 * The listening file descriptors are stored in the integer array 'fds'
 * and their number is set in '*count'.
 *
 * The addresses to bind are specified in the global server.bindaddr array
 * and their number is server.bindaddr_count. If the server configuration
 * contains no specific addresses to bind, this function will try to
 * bind * (all addresses) for both the IPv4 and IPv6 protocols.
 *
 * On success the function returns C_OK.
 *
 * On error the function returns C_ERR. For the function to be on
 * error, at least one of the server.bindaddr addresses was
 * impossible to bind, or no bind addresses were specified in the server
 * configuration but the function is not able to bind * for at least
 * one of the IPv4 or IPv6 protocols. */
int listenToPort(int port, int *fds, int *count) {
    int j;

    /* Force binding of 0.0.0.0 if no bind address is specified, always
     * entering the loop if j == 0. */
    if (server.bindaddr_count == 0) server.bindaddr[0] = NULL;
    for (j = 0; j < server.bindaddr_count || j == 0; j++) {
        if (server.bindaddr[j] == NULL) {
            int unsupported = 0;
            /* Bind * for both IPv6 and IPv4, we enter here only if
             * server.bindaddr_count == 0. */
            fds[*count] = anetTcp6Server(server.neterr,port,NULL,
                server.tcp_backlog);
            if (fds[*count] != ANET_ERR) {
                anetNonBlock(NULL,fds[*count]);
                (*count)++;
            } else if (errno == EAFNOSUPPORT) {
                unsupported++;
                serverLog(LL_WARNING,"Not listening to IPv6: unsupproted");
            }

            if (*count == 1 || unsupported) {
                /* Bind the IPv4 address as well. */
                fds[*count] = anetTcpServer(server.neterr,port,NULL,
                    server.tcp_backlog);
                if (fds[*count] != ANET_ERR) {
                    anetNonBlock(NULL,fds[*count]);
                    (*count)++;
                } else if (errno == EAFNOSUPPORT) {
                    unsupported++;
                    serverLog(LL_WARNING,"Not listening to IPv4: unsupproted");
                }
            }
            /* Exit the loop if we were able to bind * on IPv4 and IPv6,
             * otherwise fds[*count] will be ANET_ERR and we'll print an
             * error and return to the caller with an error. */
            if (*count + unsupported == 2) break;
        } else if (strchr(server.bindaddr[j],':')) {
            /* Bind IPv6 address. */
            fds[*count] = anetTcp6Server(server.neterr,port,server.bindaddr[j],
                server.tcp_backlog);
        } else {
            /* Bind IPv4 address. */
            printf("anetTcpServer add:\n");
            fds[*count] = anetTcpServer(server.neterr,port,server.bindaddr[j],
                server.tcp_backlog);
        }
        if (fds[*count] == ANET_ERR) {
            serverLog(LL_WARNING,
                "Creating Server TCP listening socket %s:%d: %s",
                server.bindaddr[j] ? server.bindaddr[j] : "*",
                port, server.neterr);
            return C_ERR;
        }
        anetNonBlock(NULL,fds[*count]);
        (*count)++;
    }
    return C_OK;
}

void setupSignalHandlers(void) {
//     struct sigaction act;

//     /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction is used.
//      * Otherwise, sa_handler is used. */
//     sigemptyset(&act.sa_mask);
//     act.sa_flags = 0;
//     // act.sa_handler = sigShutdownHandler;
//     sigaction(SIGTERM, &act, NULL);
//     sigaction(SIGINT, &act, NULL);

// #ifdef HAVE_BACKTRACE
//     sigemptyset(&act.sa_mask);
//     act.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
//     act.sa_sigaction = sigsegvHandler;
//     sigaction(SIGSEGV, &act, NULL);
//     sigaction(SIGBUS, &act, NULL);
//     sigaction(SIGFPE, &act, NULL);
//     sigaction(SIGILL, &act, NULL);
// #endif
//     return;
}

void createSharedObjects(void) {
    int j;

    shared.crlf = createObject(OBJ_STRING,sdsnew("\r\n"));
    shared.ok = createObject(OBJ_STRING,sdsnew("+OK\r\n"));
    shared.err = createObject(OBJ_STRING,sdsnew("-ERR\r\n"));
    shared.emptybulk = createObject(OBJ_STRING,sdsnew("$0\r\n\r\n"));
    shared.czero = createObject(OBJ_STRING,sdsnew(":0\r\n"));
    shared.cone = createObject(OBJ_STRING,sdsnew(":1\r\n"));
    shared.cnegone = createObject(OBJ_STRING,sdsnew(":-1\r\n"));
    shared.nullbulk = createObject(OBJ_STRING,sdsnew("$-1\r\n"));
    shared.nullmultibulk = createObject(OBJ_STRING,sdsnew("*-1\r\n"));
    shared.emptymultibulk = createObject(OBJ_STRING,sdsnew("*0\r\n"));
    shared.pong = createObject(OBJ_STRING,sdsnew("+PONG\r\n"));
    shared.queued = createObject(OBJ_STRING,sdsnew("+QUEUED\r\n"));
    shared.emptyscan = createObject(OBJ_STRING,sdsnew("*2\r\n$1\r\n0\r\n*0\r\n"));
    shared.wrongtypeerr = createObject(OBJ_STRING,sdsnew(
        "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"));
    shared.nokeyerr = createObject(OBJ_STRING,sdsnew(
        "-ERR no such key\r\n"));
    shared.syntaxerr = createObject(OBJ_STRING,sdsnew(
        "-ERR syntax error\r\n"));
    shared.sameobjecterr = createObject(OBJ_STRING,sdsnew(
        "-ERR source and destination objects are the same\r\n"));
    shared.outofrangeerr = createObject(OBJ_STRING,sdsnew(
        "-ERR index out of range\r\n"));
    shared.noscripterr = createObject(OBJ_STRING,sdsnew(
        "-NOSCRIPT No matching script. Please use EVAL.\r\n"));
    shared.loadingerr = createObject(OBJ_STRING,sdsnew(
        "-LOADING Redis is loading the dataset in memory\r\n"));
    shared.slowscripterr = createObject(OBJ_STRING,sdsnew(
        "-BUSY Redis is busy running a script. You can only call SCRIPT KILL or SHUTDOWN NOSAVE.\r\n"));
    shared.masterdownerr = createObject(OBJ_STRING,sdsnew(
        "-MASTERDOWN Link with MASTER is down and slave-serve-stale-data is set to 'no'.\r\n"));
    shared.bgsaveerr = createObject(OBJ_STRING,sdsnew(
        "-MISCONF Redis is configured to save RDB snapshots, but is currently not able to persist on disk. Commands that may modify the data set are disabled. Please check Redis logs for details about the error.\r\n"));
    shared.roslaveerr = createObject(OBJ_STRING,sdsnew(
        "-READONLY You can't write against a read only slave.\r\n"));
    shared.noautherr = createObject(OBJ_STRING,sdsnew(
        "-NOAUTH Authentication required.\r\n"));
    shared.oomerr = createObject(OBJ_STRING,sdsnew(
        "-OOM command not allowed when used memory > 'maxmemory'.\r\n"));
    shared.execaborterr = createObject(OBJ_STRING,sdsnew(
        "-EXECABORT Transaction discarded because of previous errors.\r\n"));
    shared.noreplicaserr = createObject(OBJ_STRING,sdsnew(
        "-NOREPLICAS Not enough good slaves to write.\r\n"));
    shared.busykeyerr = createObject(OBJ_STRING,sdsnew(
        "-BUSYKEY Target key name already exists.\r\n"));
    shared.space = createObject(OBJ_STRING,sdsnew(" "));
    shared.colon = createObject(OBJ_STRING,sdsnew(":"));
    shared.plus = createObject(OBJ_STRING,sdsnew("+"));

    for (j = 0; j < PROTO_SHARED_SELECT_CMDS; j++) {
        char dictid_str[64];
        int dictid_len;

        dictid_len = ll2string(dictid_str,sizeof(dictid_str),j);
        shared.select[j] = createObject(OBJ_STRING,
            sdscatprintf(sdsempty(),
                "*2\r\n$6\r\nSELECT\r\n$%d\r\n%s\r\n",
                dictid_len, dictid_str));
    }
    shared.messagebulk = createStringObject("$7\r\nmessage\r\n",13);
    shared.pmessagebulk = createStringObject("$8\r\npmessage\r\n",14);
    shared.subscribebulk = createStringObject("$9\r\nsubscribe\r\n",15);
    shared.unsubscribebulk = createStringObject("$11\r\nunsubscribe\r\n",18);
    shared.psubscribebulk = createStringObject("$10\r\npsubscribe\r\n",17);
    shared.punsubscribebulk = createStringObject("$12\r\npunsubscribe\r\n",19);
    shared.del = createStringObject("DEL",3);
    shared.rpop = createStringObject("RPOP",4);
    shared.lpop = createStringObject("LPOP",4);
    shared.lpush = createStringObject("LPUSH",5);
    for (j = 0; j < OBJ_SHARED_INTEGERS; j++) {
        shared.integers[j] = createObject(OBJ_STRING,(void*)(long)j);
        shared.integers[j]->encoding = OBJ_ENCODING_INT;
    }
    for (j = 0; j < OBJ_SHARED_BULKHDR_LEN; j++) {
        shared.mbulkhdr[j] = createObject(OBJ_STRING,
            sdscatprintf(sdsempty(),"*%d\r\n",j));
        shared.bulkhdr[j] = createObject(OBJ_STRING,
            sdscatprintf(sdsempty(),"$%d\r\n",j));
    }
    /* The following two shared objects, minstring and maxstrings, are not
     * actually used for their value but as a special object meaning
     * respectively the minimum possible string and the maximum possible
     * string in string comparisons for the ZRANGEBYLEX command. */
    shared.minstring = createStringObject("minstring",9);
    shared.maxstring = createStringObject("maxstring",9);
}

void initServer(void) {
    int j;
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    setupSignalHandlers();

    if (server.syslog_enabled) {
        openlog(server.syslog_ident, LOG_PID | LOG_NDELAY | LOG_NOWAIT,
            server.syslog_facility);
    }

    server.pid = getpid();
    server.current_client = NULL;
    server.clients = listCreate();
    server.clients_to_close = listCreate();
    server.slaves = listCreate();
    server.monitors = listCreate();
    server.clients_pending_write = listCreate();
    server.slaveseldb = -1; /* Force to emit the first SELECT command. */
    server.unblocked_clients = listCreate();
    server.ready_keys = listCreate();
    server.clients_waiting_acks = listCreate();
    server.get_ack_from_slaves = 0;
    server.clients_paused = 0;
    server.system_memory_size = zmalloc_get_memory_size();

    createSharedObjects();
    // adjustOpenFilesLimit();
    server.el = aeCreateEventLoop(server.maxclients+CONFIG_FDSET_INCR);
    server.db = zmalloc(sizeof(redisDb)*server.dbnum);

    /* Open the TCP listening socket for the user commands. */
    if (server.port != 0 &&
        listenToPort(server.port,server.ipfd,&server.ipfd_count) == C_ERR)
        exit(1);

    /* Open the listening Unix domain socket. */
    if (server.unixsocket != NULL) {
        unlink(server.unixsocket); /* don't care if this fails */
        server.sofd = anetUnixServer(server.neterr,server.unixsocket,
            server.unixsocketperm, server.tcp_backlog);
        if (server.sofd == ANET_ERR) {
            serverLog(LL_WARNING, "Opening Unix socket: %s", server.neterr);
            exit(1);
        }
        anetNonBlock(NULL,server.sofd);
    }

    /* Abort if there are no listening sockets at all. */
    if (server.ipfd_count == 0 && server.sofd < 0) {
        serverLog(LL_WARNING, "Configured to not listen anywhere, exiting.");
        exit(1);
    }

    /* Create the Redis databases, and initialize other internal state. */
    for (j = 0; j < server.dbnum; j++) {
        server.db[j].dict = dictCreate(&dbDictType,NULL);
        server.db[j].expires = dictCreate(&keyptrDictType,NULL);
        server.db[j].blocking_keys = dictCreate(&keylistDictType,NULL);
        server.db[j].ready_keys = dictCreate(&setDictType,NULL);
        server.db[j].watched_keys = dictCreate(&keylistDictType,NULL);
        server.db[j].eviction_pool = evictionPoolAlloc();
        server.db[j].id = j;
        server.db[j].avg_ttl = 0;
    }
    server.pubsub_channels = dictCreate(&keylistDictType,NULL);
    server.pubsub_patterns = listCreate();
    // listSetFreeMethod(server.pubsub_patterns,freePubsubPattern);
    // listSetMatchMethod(server.pubsub_patterns,listMatchPubsubPattern);
    server.cronloops = 0;
    server.rdb_child_pid = -1;
    server.aof_child_pid = -1;
    server.rdb_child_type = RDB_CHILD_TYPE_NONE;
    server.rdb_bgsave_scheduled = 0;
    // aofRewriteBufferReset();
    server.aof_buf = sdsempty();
    server.lastsave = time(NULL); /* At startup we consider the DB saved. */
    server.lastbgsave_try = 0;    /* At startup we never tried to BGSAVE. */
    server.rdb_save_time_last = -1;
    server.rdb_save_time_start = -1;
    server.dirty = 0;
    // resetServerStats();
    /* A few stats we don't want to reset: server startup time, and peak mem. */
    server.stat_starttime = time(NULL);
    server.stat_peak_memory = 0;
    server.resident_set_size = 0;
    server.lastbgsave_status = C_OK;
    server.aof_last_write_status = C_OK;
    server.aof_last_write_errno = 0;
    server.repl_good_slaves_count = 0;
    // updateCachedTime();

    /* Create the serverCron() time event, that's our main way to process
     * background operations. */
    if(aeCreateTimeEvent(server.el, 1, serverCron, NULL, NULL) == AE_ERR) {
        serverPanic("Can't create the serverCron time event.");
        exit(1);
    }

    /* Create an event handler for accepting new connections in TCP and Unix
     * domain sockets. */
    for (j = 0; j < server.ipfd_count; j++) {
        if (aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE,
            acceptTcpHandler,NULL) == AE_ERR)
            {
                serverPanic(
                    "Unrecoverable error creating server.ipfd file event.");
            }
    }
    if (server.sofd > 0 && aeCreateFileEvent(server.el,server.sofd,AE_READABLE,
        acceptUnixHandler,NULL) == AE_ERR) serverPanic("Unrecoverable error creating server.sofd file event.");

    /* Open the AOF file if needed. */
    if (server.aof_state == AOF_ON) {
        server.aof_fd = open(server.aof_filename,
                               O_WRONLY|O_APPEND|O_CREAT,0644);
        if (server.aof_fd == -1) {
            serverLog(LL_WARNING, "Can't open the append-only file: %s",
                strerror(errno));
            exit(1);
        }
    }

    /* 32 bit instances are limited to 4GB of address space, so if there is
     * no explicit limit in the user provided configuration we set a limit
     * at 3 GB using maxmemory with 'noeviction' policy'. This avoids
     * useless crashes of the Redis instance for out of memory. */
    if (server.arch_bits == 32 && server.maxmemory == 0) {
        serverLog(LL_WARNING,"Warning: 32 bit instance detected but no memory limit set. Setting 3 GB maxmemory limit with 'noeviction' policy now.");
        server.maxmemory = 3072LL*(1024*1024); /* 3 GB */
        server.maxmemory_policy = MAXMEMORY_NO_EVICTION;
    }

    // if (server.cluster_enabled) clusterInit();
    // replicationScriptCacheInit();
    // scriptingInit(1);
    // slowlogInit();
    // latencyMonitorInit();
    // bioInit();
}

void redisOutOfMemoryHandler(size_t allocation_size) {
    serverLog(LL_WARNING,"Out Of Memory allocating %zu bytes!",
        allocation_size);
    serverPanic("Redis aborting for OUT OF MEMORY");
}

void createPidFile(void) {
    /* If pidfile requested, but no pidfile defined, use
     * default pidfile path */
    if (!server.pidfile) server.pidfile = zstrdup(CONFIG_DEFAULT_PID_FILE);

    /* Try to write the pid file in a best-effort way. */
    FILE *fp = fopen(server.pidfile,"w");
    if (fp) {
        fprintf(fp,"%d\n",(int)getpid());
        fclose(fp);
    }
}

/* Return zero if strings are the same, non-zero if they are not.
 * The comparison is performed in a way that prevents an attacker to obtain
 * information about the nature of the strings just monitoring the execution
 * time of the function.
 *
 * Note that limiting the comparison length to strings up to 512 bytes we
 * can avoid leaking any information about the password length and any
 * possible branch misprediction related leak.
 */
int time_independent_strcmp(char *a, char *b) {
    char bufa[CONFIG_AUTHPASS_MAX_LEN], bufb[CONFIG_AUTHPASS_MAX_LEN];
    /* The above two strlen perform len(a) + len(b) operations where either
     * a or b are fixed (our password) length, and the difference is only
     * relative to the length of the user provided string, so no information
     * leak is possible in the following two lines of code. */
    unsigned int alen = strlen(a);
    unsigned int blen = strlen(b);
    unsigned int j;
    int diff = 0;

    /* We can't compare strings longer than our static buffers.
     * Note that this will never pass the first test in practical circumstances
     * so there is no info leak. */
    if (alen > sizeof(bufa) || blen > sizeof(bufb)) return 1;

    memset(bufa,0,sizeof(bufa));        /* Constant time. */
    memset(bufb,0,sizeof(bufb));        /* Constant time. */
    /* Again the time of the following two copies is proportional to
     * len(a) + len(b) so no info is leaked. */
    memcpy(bufa,a,alen);
    memcpy(bufb,b,blen);

    /* Always compare all the chars in the two buffers without
     * conditional expressions. */
    for (j = 0; j < sizeof(bufa); j++) {
        diff |= (bufa[j] ^ bufb[j]);
    }
    /* Length must be equal as well. */
    diff |= alen ^ blen;
    return diff; /* If zero strings are the same. */
}

void authCommand(client *c) {
    if (!server.requirepass) {
        addReplyError(c,"Client sent AUTH, but no password is set");
    } else if (!time_independent_strcmp(c->argv[1]->ptr, server.requirepass)) {
      c->authenticated = 1;
      addReply(c,shared.ok);
    } else {
      c->authenticated = 0;
      addReplyError(c,"invalid password");
    }
}

/* The PING command. It works in a different way if the client is in
 * in Pub/Sub mode. */
 void pingCommand(client *c) {
    /* The command takes zero or one arguments. */
    if (c->argc > 2) {
        addReplyErrorFormat(c,"wrong number of arguments for '%s' command",
            c->cmd->name);
        return;
    }

    if (c->flags & CLIENT_PUBSUB) {
        addReply(c,shared.mbulkhdr[2]);
        addReplyBulkCBuffer(c,"pong",4);
        if (c->argc == 1)
            addReplyBulkCBuffer(c,"",0);
        else
            addReplyBulk(c,c->argv[1]);
    } else {
        if (c->argc == 1)
            addReply(c,shared.pong);
        else
            addReplyBulk(c,c->argv[1]);
    }
}

void redisOpArrayInit(redisOpArray *oa) {
    oa->ops = NULL;
    oa->numops = 0;
}

void redisOpArrayFree(redisOpArray *oa) {
    while(oa->numops) {
        int j;
        redisOp *op;

        oa->numops--;
        op = oa->ops+oa->numops;
        for (j = 0; j < op->argc; j++)
            decrRefCount(op->argv[j]);
        zfree(op->argv);
    }
    zfree(oa->ops);
}

/* Propagate the specified command (in the context of the specified database id)
 * to AOF and Slaves.
 *
 * flags are an xor between:
 * + PROPAGATE_NONE (no propagation of command at all)
 * + PROPAGATE_AOF (propagate into the AOF file if is enabled)
 * + PROPAGATE_REPL (propagate into the replication link)
 *
 * This should not be used inside commands implementation. Use instead
 * alsoPropagate(), preventCommandPropagation(), forceCommandPropagation().
 */
void propagate(struct redisCommand *cmd, int dbid, robj **argv, int argc,
    int flags)
{
    // if (server.aof_state != AOF_OFF && flags & PROPAGATE_AOF)
    //     feedAppendOnlyFile(cmd,dbid,argv,argc);
    // if (flags & PROPAGATE_REPL)
    //     replicationFeedSlaves(server.slaves,dbid,argv,argc);
}


/* Call() is the core of Redis execution of a command.
 *
 * The following flags can be passed:
 * CMD_CALL_NONE        No flags.
 * CMD_CALL_SLOWLOG     Check command speed and log in the slow log if needed.
 * CMD_CALL_STATS       Populate command stats.
 * CMD_CALL_PROPAGATE_AOF   Append command to AOF if it modified the dataset
 *                          or if the client flags are forcing propagation.
 * CMD_CALL_PROPAGATE_REPL  Send command to salves if it modified the dataset
 *                          or if the client flags are forcing propagation.
 * CMD_CALL_PROPAGATE   Alias for PROPAGATE_AOF|PROPAGATE_REPL.
 * CMD_CALL_FULL        Alias for SLOWLOG|STATS|PROPAGATE.
 *
 * The exact propagation behavior depends on the client flags.
 * Specifically:
 *
 * 1. If the client flags CLIENT_FORCE_AOF or CLIENT_FORCE_REPL are set
 *    and assuming the corresponding CMD_CALL_PROPAGATE_AOF/REPL is set
 *    in the call flags, then the command is propagated even if the
 *    dataset was not affected by the command.
 * 2. If the client flags CLIENT_PREVENT_REPL_PROP or CLIENT_PREVENT_AOF_PROP
 *    are set, the propagation into AOF or to slaves is not performed even
 *    if the command modified the dataset.
 *
 * Note that regardless of the client flags, if CMD_CALL_PROPAGATE_AOF
 * or CMD_CALL_PROPAGATE_REPL are not set, then respectively AOF or
 * slaves propagation will never occur.
 *
 * Client flags are modified by the implementation of a given command
 * using the following API:
 *
 * forceCommandPropagation(client *c, int flags);
 * preventCommandPropagation(client *c);
 * preventCommandAOF(client *c);
 * preventCommandReplication(client *c);
 *
 */
void call(client *c, int flags) {
    long long dirty, start, duration;
    int client_old_flags = c->flags;

    /* Sent the command to clients in MONITOR mode, only if the commands are
     * not generated from reading an AOF. */
    // if (listLength(server.monitors) &&
    //     !server.loading &&
    //     !(c->cmd->flags & (CMD_SKIP_MONITOR|CMD_ADMIN)))
    // {
    //     replicationFeedMonitors(c,server.monitors,c->db->id,c->argv,c->argc);
    // }

    /* Initialization: clear the flags that must be set by the command on
     * demand, and initialize the array for additional commands propagation. */
    c->flags &= ~(CLIENT_FORCE_AOF|CLIENT_FORCE_REPL|CLIENT_PREVENT_PROP);
    redisOpArrayInit(&server.also_propagate);

    /* Call the command. */
    dirty = server.dirty;
    start = ustime();
    c->cmd->proc(c);
    duration = ustime()-start;
    dirty = server.dirty-dirty;
    if (dirty < 0) dirty = 0;

    /* When EVAL is called loading the AOF we don't want commands called
     * from Lua to go into the slowlog or to populate statistics. */
    if (server.loading && c->flags & CLIENT_LUA)
        flags &= ~(CMD_CALL_SLOWLOG | CMD_CALL_STATS);

    /* If the caller is Lua, we want to force the EVAL caller to propagate
     * the script if the command flag or client flag are forcing the
     * propagation. */
    if (c->flags & CLIENT_LUA && server.lua_caller) {
        if (c->flags & CLIENT_FORCE_REPL)
            server.lua_caller->flags |= CLIENT_FORCE_REPL;
        if (c->flags & CLIENT_FORCE_AOF)
            server.lua_caller->flags |= CLIENT_FORCE_AOF;
    }

    /* Log the command into the Slow log if needed, and populate the
     * per-command statistics that we show in INFO commandstats. */
    if (flags & CMD_CALL_SLOWLOG && c->cmd->proc != execCommand) {
        char *latency_event = (c->cmd->flags & CMD_FAST) ?
                              "fast-command" : "command";
        // latencyAddSampleIfNeeded(latency_event,duration/1000);
        slowlogPushEntryIfNeeded(c->argv,c->argc,duration);
    }
    if (flags & CMD_CALL_STATS) {
        c->lastcmd->microseconds += duration;
        c->lastcmd->calls++;
    }

    /* Propagate the command into the AOF and replication link */
    if (flags & CMD_CALL_PROPAGATE &&
        (c->flags & CLIENT_PREVENT_PROP) != CLIENT_PREVENT_PROP)
    {
        int propagate_flags = PROPAGATE_NONE;

        /* Check if the command operated changes in the data set. If so
         * set for replication / AOF propagation. */
        if (dirty) propagate_flags |= (PROPAGATE_AOF|PROPAGATE_REPL);

        /* If the client forced AOF / replication of the command, set
         * the flags regardless of the command effects on the data set. */
        if (c->flags & CLIENT_FORCE_REPL) propagate_flags |= PROPAGATE_REPL;
        if (c->flags & CLIENT_FORCE_AOF) propagate_flags |= PROPAGATE_AOF;

        /* However prevent AOF / replication propagation if the command
         * implementatino called preventCommandPropagation() or similar,
         * or if we don't have the call() flags to do so. */
        if (c->flags & CLIENT_PREVENT_REPL_PROP ||
            !(flags & CMD_CALL_PROPAGATE_REPL))
                propagate_flags &= ~PROPAGATE_REPL;
        if (c->flags & CLIENT_PREVENT_AOF_PROP ||
            !(flags & CMD_CALL_PROPAGATE_AOF))
                propagate_flags &= ~PROPAGATE_AOF;

        /* Call propagate() only if at least one of AOF / replication
         * propagation is needed. */
        if (propagate_flags != PROPAGATE_NONE)
            propagate(c->cmd,c->db->id,c->argv,c->argc,propagate_flags);
    }

    /* Restore the old replication flags, since call() can be executed
     * recursively. */
    c->flags &= ~(CLIENT_FORCE_AOF|CLIENT_FORCE_REPL|CLIENT_PREVENT_PROP);
    c->flags |= client_old_flags &
        (CLIENT_FORCE_AOF|CLIENT_FORCE_REPL|CLIENT_PREVENT_PROP);

    /* Handle the alsoPropagate() API to handle commands that want to propagate
     * multiple separated commands. Note that alsoPropagate() is not affected
     * by CLIENT_PREVENT_PROP flag. */
    if (server.also_propagate.numops) {
        int j;
        redisOp *rop;

        if (flags & CMD_CALL_PROPAGATE) {
            for (j = 0; j < server.also_propagate.numops; j++) {
                rop = &server.also_propagate.ops[j];
                int target = rop->target;
                /* Whatever the command wish is, we honor the call() flags. */
                if (!(flags&CMD_CALL_PROPAGATE_AOF)) target &= ~PROPAGATE_AOF;
                if (!(flags&CMD_CALL_PROPAGATE_REPL)) target &= ~PROPAGATE_REPL;
                if (target)
                    propagate(rop->cmd,rop->dbid,rop->argv,rop->argc,target);
            }
        }
        redisOpArrayFree(&server.also_propagate);
    }
    server.stat_numcommands++;
}

/* If this function gets called we already read a whole
 * command, arguments are in the client argv/argc fields.
 * processCommand() execute the command or prepare the
 * server for a bulk read from the client.
 *
 * If C_OK is returned the client is still alive and valid and
 * other operations can be performed by the caller. Otherwise
 * if C_ERR is returned the client was destroyed (i.e. after QUIT). */
int processCommand(client *c) {
    /* The QUIT command is handled separately. Normal command procs will
     * go through checking for replication and QUIT will cause trouble
     * when FORCE_REPLICATION is enabled and would be implemented in
     * a regular command proc. */
    printf("processCommand \n");
    if (!strcasecmp(c->argv[0]->ptr,"quit")) {
        printf("processCommand quit\n");
        addReply(c,shared.ok);
        printf("processCommand quit end\n");
        c->flags |= CLIENT_CLOSE_AFTER_REPLY;
        return C_ERR;
    }

    /* Now lookup the command and check ASAP about trivial error conditions
     * such as wrong arity, bad command name and so forth. */
    c->cmd = c->lastcmd = lookupCommand(c->argv[0]->ptr);
    if (!c->cmd) {
        flagTransaction(c);
        addReplyErrorFormat(c,"unknown command '%s'",
            (char*)c->argv[0]->ptr);
        return C_OK;
    } else if ((c->cmd->arity > 0 && c->cmd->arity != c->argc) ||
               (c->argc < -c->cmd->arity)) {
        flagTransaction(c);
        addReplyErrorFormat(c,"wrong number of arguments for '%s' command",
            c->cmd->name);
        return C_OK;
    }

    /* Check if the user is authenticated */
    if (server.requirepass && !c->authenticated && c->cmd->proc != authCommand)
    {
        flagTransaction(c);
        addReply(c,shared.noautherr);
        return C_OK;
    }

    /* If cluster is enabled perform the cluster redirection here.
     * However we don't perform the redirection if:
     * 1) The sender of this command is our master.
     * 2) The command has no key arguments. */
    // if (server.cluster_enabled &&
    //     !(c->flags & CLIENT_MASTER) &&
    //     !(c->flags & CLIENT_LUA &&
    //       server.lua_caller->flags & CLIENT_MASTER) &&
    //     !(c->cmd->getkeys_proc == NULL && c->cmd->firstkey == 0 &&
    //       c->cmd->proc != execCommand))
    // {
    //     int hashslot;
    //     int error_code;
    //     clusterNode *n = getNodeByQuery(c,c->cmd,c->argv,c->argc,
    //                                     &hashslot,&error_code);
    //     if (n == NULL || n != server.cluster->myself) {
    //         if (c->cmd->proc == execCommand) {
    //             discardTransaction(c);
    //         } else {
    //             flagTransaction(c);
    //         }
    //         clusterRedirectClient(c,n,hashslot,error_code);
    //         return C_OK;
    //     }
    // }

    // /* Handle the maxmemory directive.
    //  *
    //  * First we try to free some memory if possible (if there are volatile
    //  * keys in the dataset). If there are not the only thing we can do
    //  * is returning an error. */
    // if (server.maxmemory) {
    //     int retval = freeMemoryIfNeeded();
    //     /* freeMemoryIfNeeded may flush slave output buffers. This may result
    //      * into a slave, that may be the active client, to be freed. */
    //     if (server.current_client == NULL) return C_ERR;

    //     /* It was impossible to free enough memory, and the command the client
    //      * is trying to execute is denied during OOM conditions? Error. */
    //     if ((c->cmd->flags & CMD_DENYOOM) && retval == C_ERR) {
    //         flagTransaction(c);
    //         addReply(c, shared.oomerr);
    //         return C_OK;
    //     }
    // }

    /* Don't accept write commands if there are problems persisting on disk
     * and if this is a master instance. */
    if (((server.stop_writes_on_bgsave_err &&
          server.saveparamslen > 0 &&
          server.lastbgsave_status == C_ERR) ||
          server.aof_last_write_status == C_ERR) &&
        server.masterhost == NULL &&
        (c->cmd->flags & CMD_WRITE ||
         c->cmd->proc == pingCommand))
    {
        flagTransaction(c);
        if (server.aof_last_write_status == C_OK)
            addReply(c, shared.bgsaveerr);
        else
            addReplySds(c,
                sdscatprintf(sdsempty(),
                "-MISCONF Errors writing to the AOF file: %s\r\n",
                strerror(server.aof_last_write_errno)));
        return C_OK;
    }

    // /* Don't accept write commands if there are not enough good slaves and
    //  * user configured the min-slaves-to-write option. */
    // if (server.masterhost == NULL &&
    //     server.repl_min_slaves_to_write &&
    //     server.repl_min_slaves_max_lag &&
    //     c->cmd->flags & CMD_WRITE &&
    //     server.repl_good_slaves_count < server.repl_min_slaves_to_write)
    // {
    //     flagTransaction(c);
    //     addReply(c, shared.noreplicaserr);
    //     return C_OK;
    // }

    // /* Don't accept write commands if this is a read only slave. But
    //  * accept write commands if this is our master. */
    // if (server.masterhost && server.repl_slave_ro &&
    //     !(c->flags & CLIENT_MASTER) &&
    //     c->cmd->flags & CMD_WRITE)
    // {
    //     addReply(c, shared.roslaveerr);
    //     return C_OK;
    // }

    // /* Only allow SUBSCRIBE and UNSUBSCRIBE in the context of Pub/Sub */
    // if (c->flags & CLIENT_PUBSUB &&
    //     c->cmd->proc != pingCommand &&
    //     c->cmd->proc != subscribeCommand &&
    //     c->cmd->proc != unsubscribeCommand &&
    //     c->cmd->proc != psubscribeCommand &&
    //     c->cmd->proc != punsubscribeCommand) {
    //     addReplyError(c,"only (P)SUBSCRIBE / (P)UNSUBSCRIBE / PING / QUIT allowed in this context");
    //     return C_OK;
    // }

    // /* Only allow INFO and SLAVEOF when slave-serve-stale-data is no and
    //  * we are a slave with a broken link with master. */
    // if (server.masterhost && server.repl_state != REPL_STATE_CONNECTED &&
    //     server.repl_serve_stale_data == 0 &&
    //     !(c->cmd->flags & CMD_STALE))
    // {
    //     flagTransaction(c);
    //     addReply(c, shared.masterdownerr);
    //     return C_OK;
    // }

    /* Loading DB? Return an error if the command has not the
     * CMD_LOADING flag. */
    if (server.loading && !(c->cmd->flags & CMD_LOADING)) {
        addReply(c, shared.loadingerr);
        return C_OK;
    }

    // /* Lua script too slow? Only allow a limited number of commands. */
    // if (server.lua_timedout &&
    //       c->cmd->proc != authCommand &&
    //       c->cmd->proc != replconfCommand &&
    //     !(c->cmd->proc == shutdownCommand &&
    //       c->argc == 2 &&
    //       tolower(((char*)c->argv[1]->ptr)[0]) == 'n') &&
    //     !(c->cmd->proc == scriptCommand &&
    //       c->argc == 2 &&
    //       tolower(((char*)c->argv[1]->ptr)[0]) == 'k'))
    // {
    //     flagTransaction(c);
    //     addReply(c, shared.slowscripterr);
    //     return C_OK;
    // }

    /* Exec the command */
    if (c->flags & CLIENT_MULTI &&
        c->cmd->proc != execCommand && c->cmd->proc != discardCommand &&
        c->cmd->proc != multiCommand && c->cmd->proc != watchCommand)
    {
        queueMultiCommand(c);
        addReply(c,shared.queued);
    } else {
        call(c,CMD_CALL_FULL);
        c->woff = server.master_repl_offset;
        // if (listLength(server.ready_keys))
        //     handleClientsBlockedOnLists();
    }
    return C_OK;
}

void redisAsciiArt(void) {
    #include "asciilogo.h"
        char *buf = zmalloc(1024*16);
        char *mode;
    
        if (server.cluster_enabled) mode = "cluster";
        else if (server.sentinel_mode) mode = "sentinel";
        else mode = "standalone";
    
        if (server.syslog_enabled) {
            serverLog(LL_NOTICE,
                "Redis %s (%s/%d) %s bit, %s mode, port %d, pid %ld ready to start.",
                REDIS_VERSION,
                redisGitSHA1(),
                strtol(redisGitDirty(),NULL,10) > 0,
                (sizeof(long) == 8) ? "64" : "32",
                mode, server.port,
                (long) getpid()
            );
        } else {
            snprintf(buf,1024*16,ascii_logo,
                REDIS_VERSION,
                redisGitSHA1(),
                strtol(redisGitDirty(),NULL,10) > 0,
                (sizeof(long) == 8) ? "64" : "32",
                mode, server.port,
                (long) getpid()
            );
            serverLogRaw(LL_NOTICE|LL_RAW,buf);
        }
        zfree(buf);
}

/* Check that server.tcp_backlog can be actually enforced in Linux according
 * to the value of /proc/sys/net/core/somaxconn, or warn about it. */
 void checkTcpBacklogSettings(void) {
#ifdef HAVE_PROC_SOMAXCONN
    FILE *fp = fopen("/proc/sys/net/core/somaxconn","r");
    char buf[1024];
    if (!fp) return;
    if (fgets(buf,sizeof(buf),fp) != NULL) {
        int somaxconn = atoi(buf);
        if (somaxconn > 0 && somaxconn < server.tcp_backlog) {
            serverLog(LL_WARNING,"WARNING: The TCP backlog setting of %d cannot be enforced because /proc/sys/net/core/somaxconn is set to the lower value of %d.", server.tcp_backlog, somaxconn);
        }
    }
    fclose(fp);
#endif
}

void redisSetProcTitle(char *title) {
#ifdef USE_SETPROCTITLE
    char *server_mode = "";
    if (server.cluster_enabled) server_mode = " [cluster]";
    else if (server.sentinel_mode) server_mode = " [sentinel]";
    setproctitle("%s %s:%d%s",
        title,
        server.bindaddr_count ? server.bindaddr[0] : "*",
        server.port,
        server_mode);
#else
    UNUSED(title);
#endif
}

struct redisServer server;

int main(int argc, char **argv) {
    struct timeval tv;
    int j;
    setlocale(LC_COLLATE,"");
    zmalloc_enable_thread_safeness();
    zmalloc_set_oom_handler(redisOutOfMemoryHandler);
    srand(time(NULL)^getpid());
    gettimeofday(&tv,NULL);
    dictSetHashFunctionSeed(tv.tv_sec^tv.tv_usec^getpid());
    // server.sentinel_mode = checkForSentinelMode(argc,argv);
    initServerConfig();
     /* Store the executable path and arguments in a safe place in order
     * to be able to restart the server later. */
    server.executable = getAbsolutePath(argv[0]);
    server.exec_argv = zmalloc(sizeof(char*)*(argc+1));
    server.exec_argv[argc] = NULL;
    for (j = 0; j < argc; j++) server.exec_argv[j] = zstrdup(argv[j]);

    server.supervised = redisIsSupervised(server.supervised_mode);
    int background = server.daemonize && !server.supervised;
    if (background) daemonize();    
    initServer();
    if (background || server.pidfile) createPidFile();
    redisSetProcTitle(argv[0]);
    // redisAsciiArt();
    checkTcpBacklogSettings();    
    // if (!server.sentinel_mode) {
    //     /* Things not needed when running in Sentinel mode. */
    //     serverLog(LL_WARNING,"Server started, Redis version " REDIS_VERSION);
    // #ifdef __linux__
    //     linuxMemoryWarnings();
    // #endif
    //     loadDataFromDisk();
    //     if (server.cluster_enabled) {
    //         if (verifyClusterConfigWithData() == C_ERR) {
    //             serverLog(LL_WARNING,
    //                 "You can't have keys in a DB different than DB 0 when in "
    //                 "Cluster mode. Exiting.");
    //             exit(1);
    //         }
    //     }
    //     if (server.ipfd_count > 0)
    //         serverLog(LL_NOTICE,"The server is now ready to accept connections on port %d", server.port);
    //     if (server.sofd > 0)
    //         serverLog(LL_NOTICE,"The server is now ready to accept connections at %s", server.unixsocket);
    // } else {
    //     sentinelIsRunning();
    // }

     /* Warning the user about suspicious maxmemory setting. */
     if (server.maxmemory > 0 && server.maxmemory < 1024*1024) {
        serverLog(LL_WARNING,"WARNING: You specified a maxmemory value that is less than 1MB (current value is %llu bytes). Are you sure this is what you really want?", server.maxmemory);
    }
    aeSetBeforeSleepProc(server.el,beforeSleep);
    aeMain(server.el);
    aeDeleteEventLoop(server.el);
    return 0;
}