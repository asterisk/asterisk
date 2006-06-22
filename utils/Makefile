#
# Asterisk -- A telephony toolkit for Linux.
# 
# Various utilities
#
# Copyright (C) 1999-2006, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

.PHONY: clean clean-depend all depend

UTILS:=astman smsq stereorize streamplayer aelparse

ifeq (${OSARCH},SunOS)
  SOL=../strcompat.o
  SOLLIBS=-lsocket -lnsl
endif

ifeq ($(POPT_LIB),)
  UTILS:=$(filter-out smsq,$(UTILS))
endif

ifeq ($(NEWT_LIB),)
  UTILS:=$(filter-out astman,$(UTILS))
endif

ifneq ($(filter pbx_ael,$(MENUSELECT_PBX)),)
  UTILS:=$(filter-out aelparse,$(UTILS))
endif

all: $(UTILS)

install:
	for x in $(UTILS); do \
		if [ "$$x" != "none" ]; then \
			$(INSTALL) -m 755 $$x $(DESTDIR)$(ASTSBINDIR)/$$x; \
		fi; \
	done 

uninstall:
	for x in $(UTILS); do rm -f $$x $(DESTDIR)$(ASTSBINDIR)/$$x; done

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.o $(UTILS) check_expr
	rm -f ast_expr2.o ast_expr2f.o

astman.o: astman.c
	$(CC) $(CFLAGS) -c -o $@ $<

astman: astman.o ../md5.o
	$(CC) $(CFLAGS) -o $@ $< ../md5.o -lnewt

stereorize: stereorize.o frame.o
	$(CC) $(CFLAGS) -o stereorize stereorize.o frame.o -lm

../ast_expr2.c:
	bison -o $@ -d --name-prefix=ast_yy ../ast_expr2.y

../ast_expr2f.c:
	flex -o $@ --full ../ast_expr2.fl

ast_expr2.o: ../ast_expr2.c
	gcc  $(CFLAGS) -c -o $@ $<

ast_expr2f.o: ../ast_expr2f.c
	gcc  $(CFLAGS) -c -DSTANDALONE -o $@ $<

check_expr: check_expr.c ast_expr2.o ast_expr2f.o
	$(CC) $(CFLAGS) -o $@ $^

aelflex.o: ../pbx/ael/ael_lex.c ../include/asterisk/ael_structs.h ../pbx/ael/ael.tab.h
	$(CC) $(CFLAGS) -I../pbx -DSTANDALONE -c -o $@ $<

aelbison.o: ../pbx/ael/ael.tab.c ../pbx/ael/ael.tab.h ../include/asterisk/ael_structs.h
	$(CC) $(CFLAGS) -I../pbx -c -o $@ $<

pbx_ael.o: ../pbx/pbx_ael.c
	$(CC) $(CFLAGS) -c -o $@ $<

aelparse : aelflex.o aelbison.o pbx_ael.o ael_main.o ast_expr2f.o ast_expr2.o
	$(CC) $(CFLAGS) -g -o aelparse aelflex.o aelbison.o ael_main.o pbx_ael.o ast_expr2f.o ast_expr2.o

ael_main.o : ael_main.c ../include/asterisk/ael_structs.h
	$(CC) $(CFLAGS) -c -g -o ael_main.o ael_main.c

ael_main1.o : ael_main.c ../include/asterisk/ael_structs.h
	$(CC) $(CFLAGS) -c -g  -o ael_main1.o ael_main.c

testexpr2s: ../ast_expr2f.c ../ast_expr2.c ../ast_expr2.h
	$(CC) -g -c -I../include -DSTANDALONE ../ast_expr2f.c -o ast_expr2f.o
	$(CC) -g -c -I../include -DSTANDALONE ../ast_expr2.c -o ast_expr2.o
	$(CC) -g -o testexpr2s ast_expr2f.o ast_expr2.o
	rm ast_expr2.o ast_expr2f.o 
	./testexpr2s expr2.testinput

smsq.o: smsq.c
	$(CC) $(CFLAGS) -c -o $@ $<

smsq: smsq.o
	$(CC) $(CFLAGS) -o smsq ${SOL} smsq.o -lpopt

streamplayer: streamplayer.o
	$(CC) $(CFLAGS) -o streamplayer ${SOL} streamplayer.o ${SOLLIBS}

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

