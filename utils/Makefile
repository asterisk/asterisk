#
# Don't use ast mm routines
#
CFLAGS+=-DNO_AST_MM

OSARCH=$(shell uname -s)
ifeq ($(findstring BSD,${OSARCH}),BSD)
CFLAGS+=-I/usr/local/include -L/usr/local/lib
endif

TARGET+=$(shell if [ -f /usr/include/popt.h ]; then echo "smsq"; else if [ -f /usr/local/include/popt.h ]; then echo "smsq"; fi ; fi)
TARGET+=$(shell if [ -f /usr/include/newt.h ]; then echo "astman"; else if [ -f /usr/local/include/newt.h ]; then echo "astman"; fi ; fi)

all: depend $(TARGET)

install:
	if [ "$(TARGET)" != "none" ]; then \
		for x in $(TARGET); do \
			install -m 755 $$x $(DESTDIR)$(ASTSBINDIR)/astman; \
		done ; \
	fi

none:
	@echo Not building the Asterisk Manager "astman"

clean:
	rm -f *.o astman .depend

astman: astman.o ../md5.o
	$(CC) $(CFLAGS) -o astman astman.o ../md5.o -lnewt

smsq: smsq.o
	$(CC) $(CFLAGS) -o smsq smsq.o -lpopt

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`

