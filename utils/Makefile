#
# Asterisk -- An open source telephony toolkit.
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

ASTTOPDIR?=..
-include $(ASTTOPDIR)/menuselect.makeopts

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
ALL_UTILS:=$(MENUSELECT_UTILS)
UTILS:=$(ALL_UTILS)

LIBS += $(BKTR_LIB) # astobj2 with devmode uses backtrace

_ASTCFLAGS+=-DSTANDALONE

include $(ASTTOPDIR)/Makefile.rules

ifeq ($(OSARCH),SunOS)
  LIBS+=-lsocket -lnsl
  UTILS:=$(filter-out muted,$(UTILS))
endif

ifeq ($(OSARCH),OpenBSD)
  UTILS:=$(filter-out muted,$(UTILS))
endif

ifeq ($(OSARCH),cygwin)
  UTILS:=$(filter-out muted,$(UTILS))
endif

ifeq ($(OSARCH),mingw32)
  UTILS:=
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
			$(INSTALL) -m 755 $$x "$(DESTDIR)$(ASTSBINDIR)/$$x"; \
		fi; \
	done 

uninstall:
	for x in $(ALL_UTILS); do rm -f "$(DESTDIR)$(ASTSBINDIR)/$$x"; done

clean:
	rm -f *.o $(ALL_UTILS) check_expr
	rm -f .*.d
	rm -f *.s *.i
	rm -f md5.c strcompat.c ast_expr2.c ast_expr2.h ast_expr2f.c pbx_ael.c pval.c hashtab.c lock.c
	rm -f aelparse.c aelbison.c conf2ael
	rm -f threadstorage.c
	rm -f utils.c strings.c poll.c version.c sha1.c astobj2.c refcounter
	rm -f db1-ast/.*.d
	@$(MAKE) -C db1-ast clean

md5.c: $(ASTTOPDIR)/main/md5.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"

astman: astman.o md5.o
astman: LIBS+=$(NEWT_LIB)
astman.o: _ASTCFLAGS+=-DNO_MALLOC_DEBUG

stereorize: stereorize.o frame.o
stereorize: LIBS+=-lm

hashtab.c: $(ASTTOPDIR)/main/hashtab.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"

lock.c: $(ASTTOPDIR)/main/lock.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"

strcompat.c: $(ASTTOPDIR)/main/strcompat.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"

pval.c: $(ASTTOPDIR)/res/ael/pval.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"

ast_expr2.c: $(ASTTOPDIR)/main/ast_expr2.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"

ast_expr2.h: $(ASTTOPDIR)/main/ast_expr2.h
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"

ast_expr2f.c: $(ASTTOPDIR)/main/ast_expr2f.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"
ast_expr2f.o: _ASTCFLAGS+=-I$(ASTTOPDIR)/main -Wno-unused

check_expr: check_expr.o ast_expr2.o ast_expr2f.o strcompat.o threadstorage.o clicompat.o

aelbison.c: $(ASTTOPDIR)/res/ael/ael.tab.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"
aelbison.o: _ASTCFLAGS+=-I$(ASTTOPDIR)/res/ael -DYYENABLE_NLS=0
	ifneq ($(AST_CLANG_BLOCKS),)
		_ASTCFLAGS+=-Wno-parentheses-equality
	endif

pbx_ael.c: $(ASTTOPDIR)/pbx/pbx_ael.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"
	$(ECHO_PREFIX) echo "   [SED] $@"
	$(CMD_PREFIX) sed 's/ast_debug([[:digit:]][[:digit:]]*/ast_log(LOG_DEBUG/' "$@" > "$@.new"
	$(CMD_PREFIX) mv "$@.new" "$@"

aelparse.c: $(ASTTOPDIR)/res/ael/ael_lex.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"
	$(ECHO_PREFIX) echo "   [SED] $@"
	$(CMD_PREFIX) sed 's/ast_debug([[:digit:]][[:digit:]]*/ast_log(LOG_DEBUG/' "$@" > "$@.new"
	$(CMD_PREFIX) mv "$@.new" "$@"

aelparse.o: _ASTCFLAGS+=-I$(ASTTOPDIR)/res -Wno-unused
aelparse: LIBS+=-lm
aelparse: aelparse.o aelbison.o pbx_ael.o hashtab.o lock.o ael_main.o ast_expr2f.o ast_expr2.o strcompat.o pval.o extconf.o

threadstorage.c: $(ASTTOPDIR)/main/threadstorage.c
	$(ECHO_PREFIX) echo "   [CP] $(subst $(ASTTOPDIR)/,,$<) -> $@"
	$(CMD_PREFIX) cp "$<" "$@"


extconf.o: extconf.c

conf2ael: conf2ael.o ast_expr2f.o ast_expr2.o hashtab.o lock.o aelbison.o aelparse.o pbx_ael.o pval.o extconf.o strcompat.o

check_expr2: $(ASTTOPDIR)/main/ast_expr2f.c $(ASTTOPDIR)/main/ast_expr2.c $(ASTTOPDIR)/main/ast_expr2.h
	$(ECHO_PREFIX) echo "   [CC] ast_expr2f.c -> ast_expr2fz.o"
	$(CC) -g -c -I$(ASTTOPDIR)/include -DSTANDALONE $(ASTTOPDIR)/main/ast_expr2f.c -o ast_expr2fz.o
	$(ECHO_PREFIX) echo "   [CC] ast_expr2.c -> ast_expr2z.o"
	$(CC) -g -c -I$(ASTTOPDIR)/include -DSTANDALONE2 $(ASTTOPDIR)/main/ast_expr2.c -o ast_expr2z.o
	$(ECHO_PREFIX) echo "   [LD] ast_expr2fz.o ast_expr2z.o  -> check_expr2"
	$(CC) -g -o check_expr2 ast_expr2fz.o ast_expr2z.o -lm
	$(ECHO_PREFIX) echo "   [RM] ast_expr2fz.o ast_expr2z.o"
	rm ast_expr2z.o ast_expr2fz.o 
	./check_expr2 expr2.testinput

smsq: smsq.o strcompat.o
smsq: LIBS+=$(POPT_LIB)

streamplayer: streamplayer.o

muted: muted.o
muted: LIBS+=$(AUDIO_LIBS)
muted: _ASTCFLAGS:=$(filter-out -Werror,$(_ASTCFLAGS))

CHECK_SUBDIR:	# do nothing, just make sure that we recurse in the subdir/
db1-ast/libdb1.a: CHECK_SUBDIR
	_ASTCFLAGS="$(_ASTCFLAGS) -Wno-strict-aliasing" ASTCFLAGS="$(ASTCFLAGS)" $(MAKE) -C db1-ast libdb1.a

astdb2sqlite3: LIBS+=$(SQLITE3_LIB)
astdb2sqlite3: _ASTCFLAGS+=$(SQLITE3_INCLUDE)
astdb2sqlite3: db1-ast/libdb1.a

astdb2bdb: LIBS+=$(SQLITE3_LIB)
astdb2bdb: _ASTCFLAGS+=$(SQLITE3_INCLUDE)
astdb2bdb: db1-ast/libdb1.a

ifneq ($(wildcard .*.d),)
   include .*.d
endif
