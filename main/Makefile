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

OBJS=	io.o sched.o logger.o frame.o loader.o config.o channel.o \
	translate.o file.o pbx.o cli.o md5.o term.o \
	ulaw.o alaw.o callerid.o fskmodem.o image.o app.o \
	cdr.o tdd.o acl.o rtp.o udptl.o manager.o asterisk.o \
	dsp.o chanvars.o indications.o autoservice.o db.o privacy.o \
	astmm.o enum.o srv.o dns.o aescrypt.o aestab.o aeskey.o \
	utils.o plc.o jitterbuf.o dnsmgr.o devicestate.o \
	netsock.o slinfactory.o ast_expr2.o ast_expr2f.o \
	cryptostub.o sha1.o http.o fixedjitterbuf.o abstract_jb.o

# we need to link in the objects statically, not as a library, because
# otherwise modules will not have them available if none of the static
# objects use it.
OBJS+=stdtime/localtime.o

# At the moment say.o is an optional component which can be overridden
# by a module.
OBJS+=say.o

ifeq ($(wildcard /usr/include/sys/poll.h),)
  OBJS+=poll.o
endif

ifeq ($(wildcard /usr/include/dlfcn.h),)
  OBJS+=dlfcn.o
endif

ifneq ($(findstring $(OSARCH), linux-gnu uclinux ),)
  ifneq ($(findstring LOADABLE_MODULES,$(MENUSELECT_CFLAGS)),)
  AST_LIBS+=-ldl
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
  # Mac on Intel CoreDuo does not need poll compatibility layer
  ifneq ($(PROC),i386)
    OBJS+=poll.o
    ASTCFLAGS+=-DPOLLCOMPAT
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
  AST_LIBS+=-lcrypto
endif

ifeq ($(OSARCH),NetBSD)
  AST_LIBS+=-lpthread -lcrypto -lm -L/usr/pkg/lib $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),OpenBSD)
  AST_LIBS+=-lcrypto -lpthread -lm $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),SunOS)
  AST_LIBS+=-lpthread -ldl -lnsl -lsocket -lresolv -L/opt/ssl/lib -L/usr/local/ssl/lib
  OBJS+=strcompat.o
  ASTLINK=
endif

editline/libedit.a:
	cd editline && unset CFLAGS AST_LIBS && test -f config.h || CFLAGS="$(OPTIMIZE)" ./configure --build=$(BUILD_PLATFORM) --host=$(HOST_PLATFORM)
	$(MAKE) -C editline libedit.a

db1-ast/libdb1.a:
	CFLAGS="$(ASTCFLAGS)" $(MAKE) -C db1-ast libdb1.a

ast_expr2.c ast_expr2.h:
	bison -o $@ -d --name-prefix=ast_yy ast_expr2.y

ast_expr2f.c:
	flex -o $@ --full ast_expr2.fl

testexpr2: ast_expr2f.c ast_expr2.c ast_expr2.h
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2f.c
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2.c
	$(CC) -g -o testexpr2 ast_expr2f.o ast_expr2.o
	rm ast_expr2.o ast_expr2f.o 

channel.o: CFLAGS+=$(ZAPTEL_INCLUDE)

AST_EMBED_LDSCRIPTS:=$(sort $(EMBED_LDSCRIPTS))
AST_EMBED_LDFLAGS:=$(foreach dep,$(EMBED_LDFLAGS),$(value $(dep)))
AST_EMBED_LIBS:=$(foreach dep,$(EMBED_LIBS),$(value $(dep)))
OBJS:=$(sort $(OBJS))

asterisk: $(OBJS) editline/libedit.a db1-ast/libdb1.a $(AST_EMBED_LDSCRIPTS)
	@$(ASTTOPDIR)/build_tools/make_build_h > $(ASTTOPDIR)/include/asterisk/build.h.tmp
	@if cmp -s $(ASTTOPDIR)/include/asterisk/build.h.tmp $(ASTTOPDIR)/include/asterisk/build.h ; then echo ; else \
		mv $(ASTTOPDIR)/include/asterisk/build.h.tmp $(ASTTOPDIR)/include/asterisk/build.h ; \
	fi
	@rm -f $(ASTTOPDIR)/include/asterisk/build.h.tmp
	@$(CC) -c -o buildinfo.o $(CFLAGS) buildinfo.c
	$(ECHO_PREFIX) echo "   [LD] $^ -> $@"
	$(CMD_PREFIX) $(CXX) $(STATIC_BUILD) -o asterisk $(ASTLINK) $(AST_EMBED_LDFLAGS) $(LDFLAGS) $^ buildinfo.o $(AST_LIBS) $(AST_EMBED_LIBS)

clean::
	rm -f asterisk
	rm -f .depend
	@if [ -f editline/Makefile ]; then $(MAKE) -C editline distclean ; fi
	@$(MAKE) -C db1-ast clean
	@$(MAKE) -C stdtime clean
