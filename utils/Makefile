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

# to get check_expr, add it to the ALL_UTILS list -- this is a somewhat old checking
#     program that wants an ael file for input, and will check each $[] expr for
#     possible (old) problems, like spacing around operators, which dates back to
#     the 1.2 days. The neat part is that it will actually evaluate the expressions.
#     Users could use this to quickly check expressions in their .ael file.
# to get check_expr2, add it to the ALL_UTILS list -- this is a program that will
#     read in a file containing expressions (as if they were in $[ ]), one per line. 
#     It will, of course signal any syntax errors. Devs (like murf) should use this whenever
#     changes are made to ast_expr2.y or ast_expr2.fl (or the corresponding .c files),
#     as a regression test. Others (mere mortals?) need not bother, but they are
#     more than welcome to play! The regression test itself is in expr2.testinput.
ALL_UTILS:=astman smsq stereorize streamplayer aelparse 
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
	rm -f .*.d
	rm -f md5.c strcompat.c ast_expr2.c ast_expr2f.c pbx_ael.c
	rm -f aelparse.c aelbison.c

md5.c: ../main/md5.c
	@cp $< $@

astman: astman.o md5.o
astman: LIBS+=$(NEWT_LIB)
astman.o: _ASTCFLAGS+=-DNO_MALLOC_DEBUG -DSTANDALONE

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

ast_expr2.o: _ASTCFLAGS+=-DSTANDALONE_AEL

ast_expr2f.c: ../main/ast_expr2f.c
	@cp $< $@

ast_expr2f.o: _ASTCFLAGS+=-DSTANDALONE_AEL -I../main -Wno-unused

check_expr: check_expr.o ast_expr2.o ast_expr2f.o

check_expr2: ast_expr2.c ast_expr2f.o
	gcc -DSTANDALONE ast_expr2.c ast_expr2f.o -o check_expr2
	check_expr2 expr2.testinput

aelbison.c: ../pbx/ael/ael.tab.c
	@cp $< $@
aelbison.o: aelbison.c ../pbx/ael/ael.tab.h ../include/asterisk/ael_structs.h
aelbison.o: _ASTCFLAGS+=-I../pbx -DSTANDALONE_AEL

pbx_ael.c: ../pbx/pbx_ael.c
	@cp $< $@
pbx_ael.o: _ASTCFLAGS+=-DSTANDALONE_AEL

ael_main.o: ael_main.c ../include/asterisk/ael_structs.h
ael_main.o: _ASTCFLAGS+=-DSTANDALONE_AEL

aelparse.c: ../pbx/ael/ael_lex.c
	@cp $< $@
aelparse.o: aelparse.c ../include/asterisk/ael_structs.h ../pbx/ael/ael.tab.h
aelparse.o: _ASTCFLAGS+=-I../pbx -DSTANDALONE_AEL -Wno-unused

aelparse: aelparse.o aelbison.o pbx_ael.o ael_main.o ast_expr2f.o ast_expr2.o strcompat.o

smsq: smsq.o strcompat.o
smsq: LIBS+=$(POPT_LIB)

streamplayer: streamplayer.o

muted: muted.o
muted: LIBS+=$(AUDIO_LIBS)

ifneq ($(wildcard .*.d),)
   include .*.d
endif
