#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile to build main Asterisk binary
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps $(ASTTOPDIR)/makeopts.embed_rules $(ASTTOPDIR)/makeopts

all: asterisk

include $(ASTTOPDIR)/Makefile.moddir_rules

# Must include the extra ast_expr2.c, ast_expr2f.c, in case they need to be regenerated (because to force regeneration, we delete them)
SRC=$(wildcard *.c) ast_expr2.c ast_expr2f.c
OBJSFILTER=fskmodem_int.o fskmodem_float.o cygload.o buildinfo.o
OBJS=$(filter-out $(OBJSFILTER),$(SRC:.c=.o))

# we need to link in the objects statically, not as a library, because
# otherwise modules will not have them available if none of the static
# objects use it.
OBJS+=stdtime/localtime.o
ifneq ($(firstword $(subst :, ,$(WEAKREF))),1)
OBJS+=../res/res_adsi.o
endif

AST_LIBS += $(OPENSSL_LIB)
AST_LIBS += $(BKTR_LIB)
AST_LIBS += $(LIBXML2_LIB) 
AST_LIBS += $(SQLITE3_LIB)

ifneq ($(findstring $(OSARCH), linux-gnu uclinux linux-uclibc linux-gnueabi kfreebsd-gnu linux-gnueabihf),)
  ifneq ($(findstring LOADABLE_MODULES,$(MENUSELECT_CFLAGS)),)
  AST_LIBS+=-ldl
  endif
  ifneq (x$(CAP_LIB),x)
    AST_LIBS+=$(CAP_LIB)
  endif
  AST_LIBS+=-lpthread $(EDITLINE_LIB) -lm -lresolv
else
  AST_LIBS+=$(EDITLINE_LIB) -lm
endif

ifneq ($(findstring BETTER_BACKTRACES,$(MENUSELECT_CFLAGS)),)
  AST_LIBS+=$(BFD_LIB)
endif

ifneq ($(findstring darwin,$(OSARCH)),)
  AST_LIBS+=-lresolv
  ASTLINK=-Xlinker -macosx_version_min -Xlinker 10.4 -Xlinker -undefined -Xlinker dynamic_lookup -force_flat_namespace
  ifeq ($(shell /usr/bin/sw_vers -productVersion | cut -c1-4),10.6)
    ASTLINK+=/usr/lib/bundle1.o
  endif
else
# These are used for all but Darwin
  ifneq ($(findstring LOADABLE_MODULES,$(MENUSELECT_CFLAGS)),)
    ASTLINK+=-Wl,--export-dynamic
  else
    ASTLINK+=${GC_LDFLAGS}
  endif
  ifneq ($(findstring BSD,$(OSARCH)),)
    LDFLAGS+=-L/usr/local/lib
  endif
endif

ifeq ($(OSARCH),FreeBSD)
  # -V is understood by BSD Make, not by GNU make.
  BSDVERSION=$(shell make -V OSVERSION -f /usr/share/mk/bsd.port.subdir.mk)
  AST_LIBS+=$(shell if test $(BSDVERSION) -lt 502102 ; then echo "-lc_r"; else echo "-pthread"; fi)
  AST_LIBS+=-lcrypto
endif

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  AST_LIBS+=-lminires -ldl
  ASTLINK+= -shared -Wl,--out-implib,libasterisk.a
endif
ifeq ($(OSARCH),NetBSD)
  AST_LIBS+=-lpthread -lcrypto -lm -L/usr/pkg/lib $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),OpenBSD)
  AST_LIBS+=-lcrypto -lpthread -lm $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),SunOS)
  AST_LIBS+=-lpthread -ldl -lrt -lnsl -lsocket -lresolv -L/opt/ssl/lib -L/usr/local/ssl/lib
  ASTLINK=
endif

ifneq ($(findstring USE_HOARD_ALLOCATOR,$(MENUSELECT_CFLAGS)),)
  ifneq ($(HOARD_LIB),)
    AST_LIBS+=$(HOARD_LIB)
  endif
endif

ifeq ($(GNU_LD),1)
ASTLINK+=-Wl,--version-script,asterisk.exports
  ifeq ($(HAVE_DYNAMIC_LIST),1)
  ASTLINK+=-Wl,--dynamic-list,asterisk.dynamics
  endif
endif

.PHONY: CHECK_SUBDIR
CHECK_SUBDIR:	# do nothing, just make sure that we recurse in the subdir/

editline/libedit.a: CHECK_SUBDIR
	cd editline && test -f config.h || CFLAGS="$(PTHREAD_CFLAGS) $(subst $(ASTTOPDIR),../../,$(_ASTCFLAGS:-Werror=) $(ASTCFLAGS))" LDFLAGS="$(_ASTLDFLAGS) $(ASTLDFLAGS)" ./configure --build=$(BUILD_PLATFORM) --host=$(HOST_PLATFORM) --with-ncurses=$(NCURSES_DIR) --with-curses=$(CURSES_DIR) --with-termcap=$(TERMCAP_DIR) --with-tinfo=$(TINFO_DIR)
	$(MAKE) -C editline libedit.a

ifneq ($(findstring REBUILD_PARSERS,$(MENUSELECT_CFLAGS)),)
ast_expr2.c ast_expr2.h: ast_expr2.y
else
ast_expr2.c ast_expr2.h:
endif
	$(ECHO_PREFIX) echo "   [BISON] $< -> $@"
	$(CMD_PREFIX) $(BISON) -o $@ -d --name-prefix=ast_yy ast_expr2.y

ifneq ($(findstring REBUILD_PARSERS,$(MENUSELECT_CFLAGS)),)
ast_expr2f.c: ast_expr2.fl
else
ast_expr2f.c:
endif
	$(ECHO_PREFIX) echo "   [FLEX] $< -> $@"
	$(CMD_PREFIX) $(FLEX) -o $@ ast_expr2.fl
	$(CMD_PREFIX) sed 's@#if __STDC_VERSION__ >= 199901L@#if !defined __STDC_VERSION__ || __STDC_VERSION__ >= 199901L@' $@ > $@.fix
	$(CMD_PREFIX) echo "#include \"asterisk.h\"" > $@
	$(CMD_PREFIX) echo >> $@
	$(CMD_PREFIX) cat $@.fix >> $@
	$(CMD_PREFIX) rm $@.fix

ast_expr2f.o: _ASTCFLAGS+=-Wno-unused

testexpr2: ast_expr2f.c ast_expr2.c ast_expr2.h
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2f.c
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2.c
	$(CC) -g -o testexpr2 ast_expr2f.o ast_expr2.o -lm
	rm ast_expr2.o ast_expr2f.o 

db.o: _ASTCFLAGS+=$(SQLITE3_INCLUDE)

ifneq ($(findstring ENABLE_UPLOADS,$(MENUSELECT_CFLAGS)),)
http.o: _ASTCFLAGS+=$(GMIME_INCLUDE)
endif

stdtime/localtime.o: _ASTCFLAGS+=$(AST_NO_STRICT_OVERFLOW) -Wno-format-nonliteral

AST_EMBED_LDSCRIPTS:=$(sort $(EMBED_LDSCRIPTS))
AST_EMBED_LDFLAGS:=$(foreach dep,$(EMBED_LDFLAGS),$(value $(dep)))
AST_EMBED_LIBS:=$(foreach dep,$(EMBED_LIBS),$(value $(dep)))
OBJS:=$(sort $(OBJS))

ifneq ($(wildcard ../channels/h323/Makefile.ast),)
  include ../channels/h323/Makefile.ast
else
  H323LDFLAGS=
  H323LDLIBS=
endif

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
MAIN_TGT:=asterisk.dll
asterisk: cygload
	mv cygload.exe asterisk.exe

cygload: asterisk.dll
else
MAIN_TGT:=asterisk
endif

ifneq ($(findstring ENABLE_UPLOADS,$(MENUSELECT_CFLAGS)),)
GMIMELDFLAGS+=$(GMIME_LIB)
endif

$(OBJS): _ASTCFLAGS+=-DAST_MODULE=\"core\"

$(MAIN_TGT): $(OBJS) editline/libedit.a $(AST_EMBED_LDSCRIPTS)
	@$(CC) -c -o buildinfo.o $(_ASTCFLAGS) buildinfo.c $(ASTCFLAGS)
	$(ECHO_PREFIX) echo "   [LD] $(OBJS) editline/libedit.a $(AST_EMBED_LDSCRIPTS) -> $@"
ifneq ($(findstring chan_h323,$(MENUSELECT_CHANNELS)),)
	$(CMD_PREFIX) $(CC) $(STATIC_BUILD) -o $@ $(ASTLINK) $(AST_EMBED_LDFLAGS) $(_ASTLDFLAGS) $(ASTLDFLAGS) $(OBJS) editline/libedit.a $(AST_EMBED_LDSCRIPTS) buildinfo.o $(AST_LIBS) $(AST_EMBED_LIBS) $(GMIMELDFLAGS)
else
	$(CMD_PREFIX) $(CXX) $(STATIC_BUILD) -o $@ $(ASTLINK) $(AST_EMBED_LDFLAGS) $(_ASTLDFLAGS) $(ASTLDFLAGS) $(H323LDFLAGS) $(OBJS) editline/libedit.a $(AST_EMBED_LDSCRIPTS) buildinfo.o $(AST_LIBS) $(AST_EMBED_LIBS) $(H323LDLIBS) $(GMIMELDFLAGS)
endif

ifeq ($(GNU_LD),1)
$(MAIN_TGT): asterisk.exports
asterisk.exports: asterisk.exports.in
	$(CMD_PREFIX) $(ASTTOPDIR)/build_tools/make_linker_version_script asterisk $(LINKER_SYMBOL_PREFIX)
endif

clean::
	rm -f asterisk
	rm -f asterisk.exports
	@if [ -f editline/Makefile ]; then $(MAKE) -C editline distclean ; fi
	@$(MAKE) -C stdtime clean
	rm -f libresample/src/*.o
