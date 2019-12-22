cxx = gcc
CFLAGS = 
Object = sds.o zmalloc.o adlist.o dict.o intset.o endianconv.o ziplist.o ae.o anet.o \
		config.o server.o debug.o sha1.o util.o release.o setproctitle.o \
		quicklist.o t_zset.o object.o t_hash.o t_list.o t_set.o networking.o cluster.o \
		multi.o blocked.o db.o hiredis.o t_string.o notify.o pubsub.o slowlog.o lzf_c.o \
		lzf_d.o


redisObject = redis_test.o string_test.o dict.o zmalloc.o sds.o list_test.o \
			quicklist.o ziplist.o util.o lzf_c.o lzf_d.o intset.o \
			zset_test.o t_zset.o


AllObject = $(Object) $(redisObject)

allTarget = redis-server


all:$(allTarget)

redis-server:$(Object)
	$(cxx) -o redis-server $(Object) -lpthread

redis-test:$(redisObject)
	$(cxx) -Wall -g -o redis-test $(redisObject)

$(AllObject): %.o: %.c
	$(cxx) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(allTarget) $(AllObject) 