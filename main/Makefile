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

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps $(ASTTOPDIR)/makeopts.embed_rules

all: asterisk

include $(ASTTOPDIR)/Makefile.moddir_rules

SRC=$(wildcard *.c)
OBJSFILTER=fskmodem_int.o fskmodem_float.o cygload.o buildinfo.o
OBJS=$(filter-out $(OBJSFILTER),$(SRC:.c=.o))

# we need to link in the objects statically, not as a library, because
# otherwise modules will not have them available if none of the static
# objects use it.
OBJS+=stdtime/localtime.o

AST_LIBS += $(OPENSSL_LIB)
AST_LIBS += $(BKTR_LIB)
AST_LIBS += $(LIBXML2_LIB) 

ifneq ($(findstring $(OSARCH), linux-gnu uclinux linux-uclibc linux-gnueabi ),)
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

ifneq ($(findstring darwin,$(OSARCH)),)
  AST_LIBS+=-lresolv
  ifneq ($(findstring LOADABLE_MODULES,$(MENUSELECT_CFLAGS)),)
    ASTLINK=-Wl,-dynamic
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
  AST_LIBS+=-lpthread -ldl -lnsl -lsocket -lresolv -L/opt/ssl/lib -L/usr/local/ssl/lib
  ASTLINK=
endif

ifneq ($(findstring USE_HOARD_ALLOCATOR,$(MENUSELECT_CFLAGS)),)
  ifneq ($(HOARD_LIB),)
    AST_LIBS+=$(HOARD_LIB)
  endif
endif

ifeq ($(GNU_LD),1)
ASTLINK+=-Wl,--version-script,asterisk.exports
endif

CHECK_SUBDIR:	# do nothing, just make sure that we recurse in the subdir/

editline/libedit.a: CHECK_SUBDIR
	cd editline && test -f config.h || CFLAGS="$(PTHREAD_CFLAGS) $(subst $(ASTTOPDIR),../../,$(ASTCFLAGS:-Werror=))" LDFLAGS="$(ASTLDFLAGS)" ./configure --build=$(BUILD_PLATFORM) --host=$(HOST_PLATFORM) --with-ncurses=$(NCURSES_DIR) --with-curses=$(CURSES_DIR) --with-termcap=$(TERMCAP_DIR) --with-tinfo=$(TINFO_DIR)
	$(MAKE) -C editline libedit.a

db1-ast/libdb1.a: CHECK_SUBDIR
	CFLAGS="$(subst $(ASTTOPDIR),../../,$(ASTCFLAGS))" LDFLAGS="$(ASTLDFLAGS)" $(MAKE) -C db1-ast libdb1.a

ast_expr2.c ast_expr2.h:
	bison -o $@ -d --name-prefix=ast_yy ast_expr2.y

ast_expr2f.c:
	flex -o $@ ast_expr2.fl
	sed 's@#if __STDC_VERSION__ >= 199901L@#if !defined __STDC_VERSION__ || __STDC_VERSION__ >= 199901L@' $@ > $@.fix
	echo "#include \"asterisk.h\"" > $@
	echo >> $@
	cat $@.fix >> $@
	rm $@.fix

ast_expr2f.o: ASTCFLAGS+=-Wno-unused

testexpr2: ast_expr2f.c ast_expr2.c ast_expr2.h
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2f.c
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2.c
	$(CC) -g -o testexpr2 ast_expr2f.o ast_expr2.o -lm
	rm ast_expr2.o ast_expr2f.o 

ifneq ($(findstring ENABLE_UPLOADS,$(MENUSELECT_CFLAGS)),)
http.o: ASTCFLAGS+=$(GMIME_INCLUDE)
endif

stdtime/localtime.o: ASTCFLAGS+=$(AST_NO_STRICT_OVERFLOW) -Wno-format-nonliteral

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

$(MAIN_TGT): $(OBJS) editline/libedit.a db1-ast/libdb1.a $(AST_EMBED_LDSCRIPTS) asterisk.exports
	@$(CC) -c -o buildinfo.o $(ASTCFLAGS) buildinfo.c
	$(ECHO_PREFIX) echo "   [LD] $(OBJS) editline/libedit.a db1-ast/libdb1.a $(AST_EMBED_LDSCRIPTS) -> $@"
ifneq ($(findstring chan_h323,$(MENUSELECT_CHANNELS)),)
	$(CMD_PREFIX) $(CC) $(STATIC_BUILD) -o $@ $(ASTLINK) $(AST_EMBED_LDFLAGS) $(ASTLDFLAGS) $(OBJS) editline/libedit.a db1-ast/libdb1.a $(AST_EMBED_LDSCRIPTS) buildinfo.o $(AST_LIBS) $(AST_EMBED_LIBS) $(GMIMELDFLAGS)
else
	$(CMD_PREFIX) $(CXX) $(STATIC_BUILD) -o $@ $(ASTLINK) $(AST_EMBED_LDFLAGS) $(ASTLDFLAGS) $(H323LDFLAGS) $(OBJS) editline/libedit.a db1-ast/libdb1.a $(AST_EMBED_LDSCRIPTS) buildinfo.o $(AST_LIBS) $(AST_EMBED_LIBS) $(H323LDLIBS) $(GMIMELDFLAGS)
endif

clean::
	rm -f asterisk
	rm -f db1-ast/.*.d
	@if [ -f editline/Makefile ]; then $(MAKE) -C editline distclean ; fi
	@$(MAKE) -C db1-ast clean
	@$(MAKE) -C stdtime clean
	rm -f libresample/src/*.o
