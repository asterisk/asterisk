OBJS=localtime.o

all: libtime.a

libtime.a: $(OBJS)
	ar rv $@ $(OBJS)
	ranlib $@

install:

clean:
	rm -f libtime.a *.o test .depend

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) *.c

test: test.c
	${CC} ${CFLAGS} -o test test.c

ifneq ($(wildcard .depend),)
include .depend
endif
