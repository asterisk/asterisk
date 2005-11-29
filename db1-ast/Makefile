#	@(#)Makefile	8.9 (Berkeley) 7/14/94

LIBDB=	libdb1.a
ARCH=$(shell uname -m)
ifeq ($(ARCH),alpha)
SOVER=2.1
else
SOVER=2
endif
#Added support for UltraSparc - Belgarath
ifeq ($(ARCH),sparc64)
PROC=ultrasparc
CFLAGS += -mtune=$(PROC) -O3 -pipe -fomit-frame-pointer -mcpu=v8
endif

LIBDBSO=libdb.so.$(SOVER)
PROG=	db_dump185
OBJ1=	hash.o hash_bigkey.o hash_buf.o hash_func.o hash_log2.o hash_page.o \
	ndbm.o
OBJ2=	bt_close.o bt_conv.o bt_debug.o bt_delete.o bt_get.o bt_open.o \
	bt_overflow.o bt_page.o bt_put.o bt_search.o bt_seq.o bt_split.o \
	bt_utils.o
OBJ3=	db.o
OBJ4=	mpool.o
OBJ5=	rec_close.o rec_delete.o rec_get.o rec_open.o rec_put.o rec_search.o \
	rec_seq.o rec_utils.o
MISC=
OBJS=	$(OBJ1) $(OBJ2) $(OBJ3) $(OBJ4) $(OBJ5) $(MISC)
SHOBJS=	$(patsubst %.o,%.os,$(OBJS))

all: $(LIBDB) #$(LIBDBSO) $(PROG)

$(LIBDB): $(OBJS)
	rm -f $@
	ar cq $@ $(OBJS)
	ranlib $@

$(LIBDBSO): $(SHOBJS)
	$(CC) -Wl,-O1 -Wl,--version-script=libdb.map -Wl,-soname=$(LIBDBSO) -shared -o $@ $^
	ln -sf $@ libdb.so

$(PROG): db_dump185.o $(LIBDBSO)
	$(CC) -o $@ db_dump185.o -L. -ldb

clean:
	rm -f $(LIBDB) $(LIBDBSO) $(OBJS) $(SHOBJS)

OORG=	-O2
CL=	$(CC) -Wall -c -D__DBINTERFACE_PRIVATE $(OORG) -I. -Iinclude

db_dump185.o: db_dump185.c
	$(CL) -o $@ $<
%.o: hash/%.c
	$(CL) -Ihash -o $@ $<
%.os: hash/%.c
	$(CL) -Ihash -fPIC -o $@ $<
%.o: btree/%.c
	$(CL) -Ibtree -o $@ $<
%.os: btree/%.c
	$(CL) -Ibtree -fPIC -o $@ $<
db.o: db/db.c
	$(CL) -o $@ $<
db.os: db/db.c
	$(CL) -fPIC -o $@ $<
mpool.o: mpool/mpool.c
	$(CL) -o $@ $<
mpool.os: mpool/mpool.c
	$(CL) -fPIC -o $@ $<
%.o: recno/%.c
	$(CL) -Irecno -o $@ $<
%.os: recno/%.c
	$(CL) -Irecno -fPIC -o $@ $<
