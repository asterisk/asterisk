#
# Asterisk -- A telephony toolkit for Linux.
# 
# Various utilities
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

#
# Don't use ast mm routines
#
CFLAGS+=-DNO_AST_MM

ifeq ($(findstring BSD,${OSARCH}),BSD)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

TARGET=stereorize streamplayer

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/popt.h)$(wildcard -f $(CROSS_COMPILE_TARGET)/usr/local/include/popt.h),)
  TARGET+=smsq
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/newt.h)$(wildcard -f $(CROSS_COMPILE_TARGET)/usr/local/include/newt.h),)
  TARGET+=astman
endif

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
	rm -f ast_expr2.o ast_expr2f.o

astman: astman.o ../md5.o
	$(CC) $(CFLAGS) -o astman astman.o ../md5.o -lnewt

stereorize: stereorize.o frame.o
	$(CC) $(CFLAGS) -o stereorize stereorize.o frame.o -lm

ast_expr2.o: ../ast_expr2.c
	gcc -g -c -o $@ $<

ast_expr2f.o: ../ast_expr2f.c
	gcc -g -c -DSTANDALONE -o $@ $<

check_expr: check_expr.c ast_expr2.o ast_expr2f.o
	$(CC) $(CFLAGS) -o $@ $^

smsq: smsq.o
	$(CC) $(CFLAGS) -o smsq ${SOL} smsq.o -lpopt

streamplayer: streamplayer.o
	$(CC) $(CFLAGS) -o streamplayer ${SOL} streamplayer.o

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

