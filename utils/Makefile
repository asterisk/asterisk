#
# Don't use ast mm routines
#
CFLAGS+=-DNO_AST_MM

OSARCH=$(shell uname -s)
ifeq ($(findstring BSD,${OSARCH}),BSD)
CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

TARGET=stereorize

TARGET+=$(shell if [ -f $(CROSS_COMPILE_TARGET)/usr/include/popt.h ]; then echo "smsq"; else if [ -f $(CROSS_COMPILE_TARGET)/usr/local/include/popt.h ]; then echo "smsq"; fi ; fi)
TARGET+=$(shell if [ -f $(CROSS_COMPILE_TARGET)/usr/include/newt.h ]; then echo "astman"; else if [ -f $(CROSS_COMPILE_TARGET)/usr/local/include/newt.h ]; then echo "astman"; fi ; fi)
ifeq (${OSARCH},SunOS)
SOL=../strcompat.o
endif

all: depend $(TARGET)

install:
	for x in $(TARGET); do \
		if [ "$$x" != "none" ]; then \
			$(INSTALL) -m 755 $$x $(DESTDIR)$(ASTSBINDIR)/$$x; \
		fi; \
	done 

clean:
	rm -f *.o astman smsq stereorize check_expr .depend

astman: astman.o ../md5.o
	$(CC) $(CFLAGS) -o astman astman.o ../md5.o -lnewt

stereorize: stereorize.o frame.o
	$(CC) $(CFLAGS) -o stereorize stereorize.o frame.o -lm

smsq: smsq.o
	$(CC) $(CFLAGS) -o smsq ${SOL} smsq.o -lpopt

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`

