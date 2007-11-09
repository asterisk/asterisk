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
ALL_UTILS:=astman smsq stereorize streamplayer aelparse muted check_expr conf2ael hashtest2 hashtest
UTILS:=$(ALL_UTILS)

include $(ASTTOPDIR)/Makefile.rules

ifeq ($(OSARCH),SunOS)
  LIBS+=-lsocket -lnsl
  UTILS:=$(filter-out muted,$(UTILS))
endif

ifeq ($(OSARCH),OpenBSD)
  UTILS:=$(filter-out muted,$(UTILS))
endif

ifneq ($(findstring darwin,$(OSARCH)),)
  AUDIO_LIBS=-framework CoreAudio
endif

ifeq ($(POPT_LIB),)
  UTILS:=$(filter-out smsq,$(UTILS))
endif

ifeq ($(NEWT_LIB),)
  UTILS:=$(filter-out astman,$(UTILS))
endif

ifneq ($(filter pbx_ael,$(MENUSELECT_PBX)),)
  UTILS:=$(filter-out aelparse,$(UTILS))
  UTILS:=$(filter-out conf2ael,$(UTILS))
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
	rm -f *.o $(ALL_UTILS) check_expr
	rm -f .*.o.d .*.oo.d
	rm -f *.s *.i
	rm -f md5.c strcompat.c ast_expr2.c ast_expr2f.c pbx_ael.c pval.c
	rm -f aelparse.c aelbison.c conf2ael
	rm -f utils.c threadstorage.c sha1.c astobj2.c hashtest2 hashtest

md5.c: ../main/md5.c
	@cp $< $@

astman: astman.o md5.o
astman: LIBS+=$(NEWT_LIB)

stereorize: stereorize.o frame.o
stereorize: LIBS+=-lm

hashtab.c: ../main/hashtab.c
	@cp $< $@

strcompat.c: ../main/strcompat.c
	@cp $< $@

../main/ast_expr2.c:
	@echo "   [BISON] ../main/ast_expr2.y -> $@"
	@bison -o $@ -d --name-prefix=ast_yy ../main/ast_expr2.y

../main/ast_expr2f.c:
	@echo "   [FLEX] ../main/ast_expr2.fl -> $@"
	@flex -o $@ --full ../main/ast_expr2.fl

pval.c: ../res/ael/pval.c
	@cp $< $@

ast_expr2.c: ../main/ast_expr2.c
	@cp $< $@

ast_expr2f.c: ../main/ast_expr2f.c
	@cp $< $@

ast_expr2f.o: ASTCFLAGS+=-DSTANDALONE_AEL -I../main

pval.o : ASTCFLAGS+=-DSTANDALONE

check_expr: check_expr.o ast_expr2.o ast_expr2f.o strcompat.o threadstorage.o

aelbison.c: ../res/ael/ael.tab.c
	@cp $< $@

aelbison.o: ASTCFLAGS+=-I../res/ael  -DYYENABLE_NLS=0

pbx_ael.c: ../pbx/pbx_ael.c
	@cp $< $@

pbx_ael.o: ASTCFLAGS+=-DSTANDALONE_AEL

aelparse.c: ../res/ael/ael_lex.c
	@cp $< $@

aelparse.o: ASTCFLAGS+=-I../res -DSTANDALONE_AEL

aelparse: aelparse.o aelbison.o pbx_ael.o ael_main.o ast_expr2f.o ast_expr2.o strcompat.o pval.o extconf.o

astobj2.c: ../main/astobj2.c
	@cp $< $@

utils.c: ../main/utils.c
	@cp $< $@

sha1.c: ../main/sha1.c
	@cp $< $@

threadstorage.c: ../main/threadstorage.c
	@cp $< $@

hashtest2.o: ASTCFLAGS+=-O0

hashtest2: hashtest2.o md5.o utils.o astobj2.o sha1.o strcompat.o threadstorage.o clicompat.o

hashtest: hashtest.o md5.o hashtab.o utils.o sha1.o strcompat.o threadstorage.o

hashtest.o : hashtest.c
	$(CC) -g -O0 -c hashtest.c -I/usr/include -I../include

extconf.o : extconf.c

conf2ael: conf2ael.o ast_expr2f.o ast_expr2.o aelbison.o aelparse.o pbx_ael.o pval.o extconf.o strcompat.o

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
