CC=gcc
#CFLAGS=-Wall
INCLUDE=-I../include

OBJS=localtime.o

all: libtime.a

libtime.a: $(OBJS)
	ar rv $@ $(OBJS)
	ranlib $@

clean:
	rm -f libtime.a *.o test

depend: .depend

.depend:
	../mkdep $(CFLAGS) *.c

test: test.c
	${CC} ${CFLAGS} -o test test.c

ifneq ($(wildcard .depend),)
include .depend
endif
