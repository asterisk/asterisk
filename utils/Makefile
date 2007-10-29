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

-include ../menuselect.makeopts

.PHONY: clean all uninstall

# to get check_expr, add it to the ALL_UTILS list
ALL_UTILS:=astman smsq stereorize streamplayer aelparse muted
UTILS:=$(ALL_UTILS)

include $(ASTTOPDIR)/Makefile.rules

ifeq ($(OSARCH),SunOS)
  LIBS+=-lsocket -lnsl
  UTILS:=$(filter-out muted,$(UTILS))
endif

ifeq ($(OSARCH),OpenBSD)
  UTILS:=$(filter-out muted,$(UTILS))
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
	for x in $(ALL_UTILS); do rm -f $$x $(DESTDIR)$(ASTSBINDIR)/$$x; done

clean:
	rm -f *.o $(ALL_UTILS) check_expr *.s *.i
	rm -f .*.o.d .*.oo.d
	rm -f md5.c strcompat.c ast_expr2.c ast_expr2f.c pbx_ael.c
	rm -f aelparse.c aelbison.c

md5.c: ../main/md5.c
	@cp $< $@

astman: astman.o md5.o
astman: LIBS+=$(NEWT_LIB)

stereorize: stereorize.o frame.o
stereorize: LIBS+=-lm

strcompat.c: ../main/strcompat.c
	@cp $< $@

../main/ast_expr2.c:
	@echo "   [BISON] ../main/ast_expr2.y -> $@"
	@bison -o $@ -d --name-prefix=ast_yy ../main/ast_expr2.y

../main/ast_expr2f.c:
	@echo "   [FLEX] ../main/ast_expr2.fl -> $@"
	@flex -o $@ --full ../main/ast_expr2.fl

ast_expr2.c: ../main/ast_expr2.c
	@cp $< $@

ast_expr2f.c: ../main/ast_expr2f.c
	@cp $< $@

ast_expr2f.o: ASTCFLAGS+=-DSTANDALONE_AEL -I../main

check_expr: check_expr.o ast_expr2.o ast_expr2f.o

aelbison.c: ../pbx/ael/ael.tab.c
	@cp $< $@
aelbison.o: aelbison.c ../pbx/ael/ael.tab.h ../include/asterisk/ael_structs.h
aelbison.o: ASTCFLAGS+=-I../pbx

pbx_ael.c: ../pbx/pbx_ael.c
	@cp $< $@
pbx_ael.o: ASTCFLAGS+=-DSTANDALONE_AEL

ael_main.o: ael_main.c ../include/asterisk/ael_structs.h

aelparse.c: ../pbx/ael/ael_lex.c
	@cp $< $@
aelparse.o: aelparse.c ../include/asterisk/ael_structs.h ../pbx/ael/ael.tab.h
aelparse.o: ASTCFLAGS+=-I../pbx -DSTANDALONE_AEL

aelparse: aelparse.o aelbison.o pbx_ael.o ael_main.o ast_expr2f.o ast_expr2.o strcompat.o

testexpr2s: ../main/ast_expr2f.c ../main/ast_expr2.c ../main/ast_expr2.h
	$(CC) -g -c -I../include -DSTANDALONE_AEL ../main/ast_expr2f.c -o ast_expr2f.o
	$(CC) -g -c -I../include -DSTANDALONE_AEL ../main/ast_expr2.c -o ast_expr2.o
	$(CC) -g -o testexpr2s ast_expr2f.o ast_expr2.o
	rm ast_expr2.o ast_expr2f.o 
	./testexpr2s expr2.testinput

smsq: smsq.o strcompat.o
smsq: LIBS+=$(POPT_LIB)

streamplayer: streamplayer.o

muted: muted.o
muted: LIBS+=$(AUDIO_LIBS)

ifneq ($(wildcard .*.d),)
   include .*.d
endif
