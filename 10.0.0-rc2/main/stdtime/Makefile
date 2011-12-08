OBJS=localtime.o

all: libtime.a

libtime.a: $(OBJS)
	ar rv $@ $(OBJS)
	ranlib $@

install:

uninstall:

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f libtime.a *.o test *.i

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) *.c

test: test.c
	${CC} ${CFLAGS} -o test test.c

ifneq ($(wildcard .depend),)
include .depend
endif
