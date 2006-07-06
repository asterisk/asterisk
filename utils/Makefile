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

.PHONY: clean clean-depend all depend uninstall

UTILS:=astman smsq stereorize streamplayer aelparse

ifeq (${OSARCH},SunOS)
  LDFLAGS+=../strcompat.o -lsocket -lnsl
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

include $(ASTTOPDIR)/Makefile.rules

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

astman: astman.o ../md5.o
astman: LDFLAGS+=-lnewt

stereorize: stereorize.o frame.o
stereorize: LDFLAGS+=-lm

../ast_expr2.c:
	@echo "   [BISON] ../ast_expr2.y -> $@"
	@bison -o $@ -d --name-prefix=ast_yy ../ast_expr2.y

../ast_expr2f.c:
	@echo "   [FLEX] ../ast_expr2.fl -> $@"
	@flex -o $@ --full ../ast_expr2.fl

$(eval $(call ast_make_o_c,ast_expr2.o,../ast_expr2.c))

$(eval $(call ast_make_o_c,ast_expr2f.o,../ast_expr2f.c))
ast_expr2f.o: CFLAGS+=-DSTANDALONE

$(eval $(call ast_make_final,check_expr,check_expr.c ast_expr2.o ast_expr2f.o))

$(eval $(call ast_make_o_c,aelflex.o,../pbx/ael/ael_lex.c ../include/asterisk/ael_structs.h ../pbx/ael/ael.tab.h))
aelflex.o: CFLAGS+=-I../pbx -DSTANDALONE

$(eval $(call ast_make_o_c,aelbison.o,../pbx/ael/ael.tab.c ../pbx/ael/ael.tab.h ../include/asterisk/ael_structs.h))
aelbison.o: CFLAGS+=-I../pbx

$(eval $(call ast_make_o_c,pbx_ael.o,../pbx/pbx_ael.c))

$(eval $(call ast_make_final,aelparse,aelflex.o aelbison.o pbx_ael.o ael_main.o ast_expr2f.o ast_expr2.o))

$(eval $(call ast_make_o_c,ael_main.o,ael_main.c ../include/asterisk/ael_structs.h))

testexpr2s: ../ast_expr2f.c ../ast_expr2.c ../ast_expr2.h
	$(CC) -g -c -I../include -DSTANDALONE ../ast_expr2f.c -o ast_expr2f.o
	$(CC) -g -c -I../include -DSTANDALONE ../ast_expr2.c -o ast_expr2.o
	$(CC) -g -o testexpr2s ast_expr2f.o ast_expr2.o
	rm ast_expr2.o ast_expr2f.o 
	./testexpr2s expr2.testinput

smsq: smsq.o
smsq: LDFLAGS+=-lpopt

streamplayer: streamplayer.o

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

