#	@(#)Makefile	8.9 (Berkeley) 7/14/94

LIBDB=	libdb1.a
ARCH=$(shell uname -m)
ifeq ($(ARCH),alpha)
SOVER=2.1
else
SOVER=2
endif

ifeq ($(OSARCH),Darwin)
  OSARCH_DEFINE+=-D__Darwin__
endif

LIBDBSO=libdb.so.$(SOVER)
PROG=	db_dump185
OBJ1=	hash/hash.o hash/hash_bigkey.o hash/hash_buf.o hash/hash_func.o hash/hash_log2.o hash/hash_page.o \
	hash/ndbm.o
OBJ2=	btree/bt_close.o btree/bt_conv.o btree/bt_debug.o btree/bt_delete.o btree/bt_get.o btree/bt_open.o \
	btree/bt_overflow.o btree/bt_page.o btree/bt_put.o btree/bt_search.o btree/bt_seq.o btree/bt_split.o \
	btree/bt_utils.o
OBJ3=	db/db.o
OBJ4=	mpool/mpool.o
OBJ5=	recno/rec_close.o recno/rec_delete.o recno/rec_get.o recno/rec_open.o recno/rec_put.o recno/rec_search.o \
	recno/rec_seq.o recno/rec_utils.o
MISC=
OBJS=	$(OBJ1) $(OBJ2) $(OBJ3) $(OBJ4) $(OBJ5) $(MISC)
SHOBJS=	$(patsubst %.o,%.os,$(OBJS))

include $(ASTTOPDIR)/Makefile.rules

all: $(LIBDB) #$(LIBDBSO) $(PROG)

$(LIBDB): $(OBJS)
	$(ECHO_PREFIX) echo "   [AR] $^ -> $@"
	$(CMD_PREFIX) $(AR) cr $@ $^
	$(CMD_PREFIX) $(RANLIB) $@

$(LIBDBSO): $(SHOBJS)
	$(CC) -Wl,-O1 -Wl,--version-script=libdb.map -Wl,-soname=$(LIBDBSO) -shared -o $@ $^
	ln -sf $@ libdb.so

$(PROG): db_dump185.o $(LIBDBSO)
	$(CC) -o $@ db_dump185.o -L. -ldb

clean-depend:

clean:
	rm -f $(LIBDB) $(LIBDBSO) $(OBJS) $(SHOBJS) */*.s */*.i

_ASTCFLAGS:=-Wall -D__DBINTERFACE_PRIVATE -I. -I.. -Iinclude -Ihash -Ibtree -Irecno $(_ASTCFLAGS)

OSTYPE=$(shell uname -s)
ifeq ($(OSTYPE),SunOS)
_ASTCFLAGS+=-I../../include -I../../include/solaris-compat -DSOLARIS
endif

db_dump185.o: db_dump185.c
	$(CL) -o $@ $<
x%.o: hash/%.c
	$(CL) -Ihash $(OSARCH_DEFINE) -o $@ $<
%.os: hash/%.c
	$(CL) -Ihash -fPIC -o $@ $<
x%.o: btree/%.c
	$(CL) -Ibtree -o $@ $<
%.os: btree/%.c
	$(CL) -Ibtree -fPIC -o $@ $<
x%.o: recno/%.c
	$(CL) -Irecno -o $@ $<
%.os: recno/%.c
	$(CL) -Irecno -fPIC -o $@ $<
