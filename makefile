cxx = gcc
CFLAGS = 
Object = sds.o zmalloc.o adlist.o dict.o intset.o endianconv.o ziplist.o ae.o anet.o \
		 config.o server.o debug.o sdstest.o sha1.o util.o release.o setproctitle.o \
		 quicklist.o t_zset.o object.o t_hash.o t_list.o t_set.o networking.o cluster.o \
		 multi.o blocked.o db.o hiredis.o t_string.o notify.o pubsub.o slowlog.o
allTarget = redis-server


all:$(allTarget)

redis-server:$(Object) $(sdstestObject)
	$(cxx) -o redis-server $(Object) -lpthread


$(Object): %.o: %.c
	$(cxx) -c $(CFLAGS) $< -o $@

clean:
	rm $(allTarget) $(Object)