#
# Don't use ast mm routines
#
CFLAGS+=-DNO_AST_MM
TARGET=$(shell if [ -f /usr/include/newt.h ]; then echo "astman"; else echo "none" ; fi)
all: depend $(TARGET)

install:
	if [ "$(TARGET)" != "none" ]; then \
		for x in $(TARGET); do \
			install -m 755 $$x $(ASTSBINDIR)/astman; \
		done ; \
	fi

none:
	@echo Not building the Asterisk Manager "astman"

clean:
	rm -f *.o astman .depend

astman: astman.o ../md5.o
	$(CC) -o astman astman.o ../md5.o -lnewt

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`

