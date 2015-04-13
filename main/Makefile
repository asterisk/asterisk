#
# Asterisk -- An open source telephony toolkit.
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
SRC:=$(wildcard *.c) ast_expr2.c ast_expr2f.c
ifeq ($(AST_ASTERISKSSL),yes)
SRC:=$(filter-out libasteriskssl.c,$(SRC))
endif
OBJSFILTER=fskmodem_int.o fskmodem_float.o cygload.o buildinfo.o
OBJS=$(filter-out $(OBJSFILTER),$(SRC:.c=.o))

# we need to link in the objects statically, not as a library, because
# otherwise modules will not have them available if none of the static
# objects use it.
OBJS+=stdtime/localtime.o

ASTSSL_LIBS:=$(OPENSSL_LIB)
AST_LIBS+=$(BKTR_LIB)
AST_LIBS+=$(LIBXML2_LIB)
AST_LIBS+=$(LIBXSLT_LIB)
AST_LIBS+=$(SQLITE3_LIB)
AST_LIBS+=$(ASTSSL_LIBS)
AST_LIBS+=$(JANSSON_LIB)
AST_LIBS+=$(URIPARSER_LIB)
AST_LIBS+=$(UUID_LIB)
AST_LIBS+=$(CRYPT_LIB)
AST_LIBS+=$(AST_CLANG_BLOCKS_LIBS)

ifneq ($(findstring $(OSARCH), linux-gnu uclinux linux-uclibc kfreebsd-gnu),)
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
  ASTLINK=-mmacosx-version-min=10.6 -Wl,-undefined,dynamic_lookup -force_flat_namespace
  ASTLINK+=/usr/lib/bundle1.o
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
  ASTLINK+=-shared -Wl,--out-implib,libasterisk.a
endif
ifeq ($(OSARCH),NetBSD)
  AST_LIBS+=-lpthread -lcrypto -lm -L/usr/pkg/lib $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),OpenBSD)
  AST_LIBS+=-lcrypto -lpthread -lm $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),SunOS)
  AST_LIBS+=-lpthread -ldl -lrt -lnsl -lsocket -lresolv
  ASTSSL_LIBS+=-L/opt/ssl/lib -L/usr/local/ssl/lib
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

ifneq ($(LIBEDIT_INTERNAL),no)
LIBEDIT_OBJ=editline/libedit.a
LIBEDIT_INCLUDE=-I. -Ieditline
endif

db.o: _ASTCFLAGS+=$(SQLITE3_INCLUDE)
asterisk.o: _ASTCFLAGS+=$(LIBEDIT_INCLUDE)
cli.o: _ASTCFLAGS+=$(LIBEDIT_INCLUDE)
json.o: _ASTCFLAGS+=$(JANSSON_INCLUDE)
bucket.o: _ASTCFLAGS+=$(URIPARSER_INCLUDE)
crypt.o: _ASTCFLAGS+=$(CRYPT_INCLUDE)
uuid.o: _ASTCFLAGS+=$(UUID_INCLUDE)

ifneq ($(findstring ENABLE_UPLOADS,$(MENUSELECT_CFLAGS)),)
http.o: _ASTCFLAGS+=$(GMIME_INCLUDE)
endif

stdtime/localtime.o: _ASTCFLAGS+=$(AST_NO_STRICT_OVERFLOW) -Wno-format-nonliteral

AST_EMBED_LDSCRIPTS:=$(sort $(EMBED_LDSCRIPTS))
AST_EMBED_LDFLAGS:=$(foreach dep,$(EMBED_LDFLAGS),$(value $(dep)))
AST_EMBED_LIBS:=$(foreach dep,$(EMBED_LIBS),$(value $(dep)))
OBJS:=$(sort $(OBJS))

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

$(OBJS): _ASTCFLAGS+=-DAST_MODULE=\"core\" -DAST_IN_CORE

libasteriskssl.o: _ASTCFLAGS+=$(OPENSSL_INCLUDE)

ifeq ($(AST_ASTERISKSSL),yes)
# The ABI *version* of the asteriskssl library; don't change this unless there truly is a
# non-backwards-compatible ABI change in the library
ASTSSL_SO_VERSION=1

ASTSSL_LDLIBS=-L. -lasteriskssl

ifeq ($(findstring darwin,$(OSARCH)),) # not Darwin
ASTSSL_LIB:=libasteriskssl.so

$(ASTSSL_LIB).$(ASTSSL_SO_VERSION): _ASTLDFLAGS+=-Wl,-soname=$(ASTSSL_LIB).$(ASTSSL_SO_VERSION)
$(ASTSSL_LIB).$(ASTSSL_SO_VERSION): _ASTCFLAGS+=-fPIC -DAST_MODULE=\"asteriskssl\"
$(ASTSSL_LIB).$(ASTSSL_SO_VERSION): LIBS+=$(ASTSSL_LIBS)
ifeq ($(GNU_LD),1)
    $(ASTSSL_LIB).$(ASTSSL_SO_VERSION): SO_SUPPRESS_SYMBOLS=-Wl,--version-script,libasteriskssl.exports,--warn-common
endif
$(ASTSSL_LIB).$(ASTSSL_SO_VERSION): SOLINK=$(DYLINK)

# These rules are duplicated from $(ASTTOPDIR)/Makefile.rules because the library name
# being built does not match the "%.so" pattern; there are also additional steps
# required to build a proper shared library (as opposed to the 'loadable module'
# type that are built by the standard rules)
$(ASTSSL_LIB).$(ASTSSL_SO_VERSION): libasteriskssl.o
ifeq ($(GNU_LD),1)
	$(CMD_PREFIX) $(ASTTOPDIR)/build_tools/make_linker_version_script libasteriskssl "$(LINKER_SYMBOL_PREFIX)" "$(ASTTOPDIR)"
endif
	$(ECHO_PREFIX) echo "   [LD] $^ -> $@"
	$(CMD_PREFIX) $(CC) $(STATIC_BUILD) -o $@ $(CC_LDFLAGS_SO) $^ $(CC_LIBS)
ifneq ($(LDCONFIG),)
	$(LDCONFIG) $(LDCONFIG_FLAGS) .
endif

$(ASTSSL_LIB): $(ASTSSL_LIB).$(ASTSSL_SO_VERSION)
	$(LN) -sf $< $@

else # Darwin
ASTSSL_LIB:=libasteriskssl.dylib

# -install_name allows library to be found if installed somewhere other than
# /lib or /usr/lib
$(ASTSSL_LIB): _ASTLDFLAGS+=-dynamiclib -install_name $(ASTLIBDIR)/$(ASTSSL_LIB)
$(ASTSSL_LIB): _ASTCFLAGS+=-fPIC -DAST_MODULE=\"asteriskssl\"
$(ASTSSL_LIB): LIBS+=$(ASTSSL_LIBS)
$(ASTSSL_LIB): SOLINK=$(DYLINK)

# Special rules for building a shared library (not a dynamically loadable module)
$(ASTSSL_LIB): libasteriskssl.o
	$(ECHO_PREFIX) echo "   [LD] $^ -> $@"
	$(CMD_PREFIX) $(CC) $(STATIC_BUILD) -o $@ $(CC_LDFLAGS_SO) $^ $(CC_LIBS)
endif

endif

tcptls.o: _ASTCFLAGS+=$(OPENSSL_INCLUDE)

$(MAIN_TGT): $(OBJS) $(ASTSSL_LIB) $(LIBEDIT_OBJ) $(AST_EMBED_LDSCRIPTS)
	@$(CC) -c -o buildinfo.o $(_ASTCFLAGS) buildinfo.c $(ASTCFLAGS)
	$(ECHO_PREFIX) echo "   [LD] $(OBJS) $(LIBEDIT_OBJ) $(AST_EMBED_LDSCRIPTS) -> $@"
	$(CMD_PREFIX) $(CXX) $(STATIC_BUILD) -o $@ $(ASTLINK) $(AST_EMBED_LDFLAGS) $(_ASTLDFLAGS) $(ASTLDFLAGS) $(OBJS) $(ASTSSL_LDLIBS) $(LIBEDIT_OBJ) $(AST_EMBED_LDSCRIPTS) buildinfo.o $(AST_LIBS) $(AST_EMBED_LIBS) $(GMIMELDFLAGS) $(LIBEDIT_LIB)

ifeq ($(GNU_LD),1)
$(MAIN_TGT): asterisk.exports
asterisk.exports: asterisk.exports.in
	$(CMD_PREFIX) $(ASTTOPDIR)/build_tools/make_linker_version_script asterisk $(LINKER_SYMBOL_PREFIX)
endif

bininstall:
	$(INSTALL) -m 755 $(MAIN_TGT) "$(DESTDIR)$(ASTSBINDIR)/"
ifeq ($(AST_ASTERISKSSL),yes)
ifeq ($(findstring darwin,$(OSARCH)),) # not Darwin
	$(INSTALL) -m 755 $(ASTSSL_LIB).$(ASTSSL_SO_VERSION) "$(DESTDIR)$(ASTLIBDIR)/"
	$(LN) -sf $(ASTSSL_LIB).$(ASTSSL_SO_VERSION) "$(DESTDIR)$(ASTLIBDIR)/$(ASTSSL_LIB)"
else # Darwin
	$(INSTALL) -m 755 $(ASTSSL_LIB) "$(DESTDIR)$(ASTLIBDIR)/"
endif
ifneq ($(LDCONFIG),)
	$(LDCONFIG) $(LDCONFIG_FLAGS) "$(DESTDIR)$(ASTLIBDIR)/"
endif
endif
	$(LN) -sf asterisk "$(DESTDIR)$(ASTSBINDIR)/rasterisk"

binuninstall:
	rm -f "$(DESTDIR)$(ASTSBINDIR)/$(MAIN_TGT)"
	rm -f "$(DESTDIR)$(ASTSBINDIR)/rasterisk"
	rm -f "$(DESTDIR)$(ASTLIBDIR)/$(ASTSSL_LIB).$(ASTSSL_SO_VERSION)"
ifneq ($(LDCONFIG),)
	$(LDCONFIG) $(LDCONFIG_FLAGS) "$(DESTDIR)$(ASTLIBDIR)/"
endif

clean::
	rm -f asterisk libasteriskssl.o
ifeq ($(AST_ASTERISKSSL),yes)
	rm -f $(ASTSSL_LIB) $(ASTSSL_LIB).*
endif
	rm -f asterisk.exports libasteriskssl.exports
	@if [ -f editline/Makefile ]; then $(MAKE) -C editline distclean ; fi
	@$(MAKE) -C stdtime clean
	rm -f libresample/src/*.o
