#
# Asterisk -- A telephony toolkit for Linux.
# 
# Top level Makefile
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

# All Makefiles use the following variables:
#
# LDFLAGS - linker flags (not libraries), used for all links
# LIBS - additional libraries, at top-level for all links,
#      on a single object just for that object
# SOLINK - linker flags used only for creating shared objects (.so files),
#      used for all .so links
#

.EXPORT_ALL_VARIABLES:

#Uncomment this to see all build commands instead of 'quiet' output
#NOISY_BUILD=yes

# Create OPTIONS variable
OPTIONS=

# If cross compiling, define these to suit
#CROSS_COMPILE=/opt/montavista/pro/devkit/arm/xscale_be/bin/xscale_be-
#CROSS_COMPILE_BIN=/opt/montavista/pro/devkit/arm/xscale_be/bin/
#CROSS_COMPILE_TARGET=/opt/montavista/pro/devkit/arm/xscale_be/target
#CROSS_ARCH=Linux
#CROSS_PROC=arm
#SUB_PROC=xscale # or maverick

ifeq ($(CROSS_COMPILE),)
  OSARCH:=$(shell uname -s)
  PROC?:=$(shell uname -m)
else
  OSARCH=$(CROSS_ARCH)
  PROC=$(CROSS_PROC)
endif

ASTTOPDIR:=$(shell pwd)

# Remember the MAKELEVEL at the top
MAKETOPLEVEL?=$(MAKELEVEL)

# Overwite config files on "make samples"
OVERWRITE=y

# Include debug and macro symbols in the executables (-g) and profiling info (-pg)
DEBUG=-g3

# Staging directory
# Files are copied here temporarily during the install process
# For example, make DESTDIR=/tmp/asterisk woud put things in
# /tmp/asterisk/etc/asterisk
# !!! Watch out, put no spaces or comments after the value !!!
DESTDIR?=
#DESTDIR?=/tmp/asterisk

# Original busydetect routine
#BUSYDETECT = -DBUSYDETECT

# Improved busydetect routine, comment the previous one if you use this one
#BUSYDETECT+= -DBUSYDETECT_MARTIN 
# Detect the busy signal looking only at tone lengths
# For example if you have 3 beeps 100ms tone, 100ms silence separated by 500 ms of silence
#BUSYDETECT+= -DBUSYDETECT_TONEONLY
# Enforce the detection of busy signal (get rid of false hangups)
# Don't use together with -DBUSYDETECT_TONEONLY
#BUSYDETECT+= -DBUSYDETECT_COMPARE_TONE_AND_SILENCE

# Define standard directories for various platforms
# These apply if they are not redefined in asterisk.conf 
ifeq ($(OSARCH),SunOS)
  ASTETCDIR=/var/etc/asterisk
  ASTLIBDIR=/opt/asterisk/lib
  ASTVARLIBDIR=/var/opt/asterisk
  ASTSPOOLDIR=/var/spool/asterisk
  ASTLOGDIR=/var/log/asterisk
  ASTHEADERDIR=/opt/asterisk/include
  ASTBINDIR=/opt/asterisk/bin
  ASTSBINDIR=/opt/asterisk/sbin
  ASTVARRUNDIR=/var/run/asterisk
  ASTMANDIR=/opt/asterisk/man
else
  ASTETCDIR=$(sysconfdir)/asterisk
  ASTLIBDIR=$(libdir)/asterisk
  ASTHEADERDIR=$(includedir)/asterisk
  ASTBINDIR=$(bindir)
  ASTSBINDIR=$(sbindir)
  ASTSPOOLDIR=$(localstatedir)/spool/asterisk
  ASTLOGDIR=$(localstatedir)/log/asterisk
  ASTVARRUNDIR=$(localstatedir)/run
  ASTMANDIR=$(mandir)
ifeq ($(OSARCH),FreeBSD)
  ASTVARLIBDIR=$(prefix)/share/asterisk
else
  ASTVARLIBDIR=$(localstatedir)/lib/asterisk
endif
endif
ASTDATADIR?=$(ASTVARLIBDIR)

# Asterisk.conf is located in ASTETCDIR or by using the -C flag
# when starting Asterisk
ASTCONFPATH=$(ASTETCDIR)/asterisk.conf
MODULES_DIR=$(ASTLIBDIR)/modules
AGI_DIR=$(ASTDATADIR)/agi-bin

# If you use Apache, you may determine by a grep 'DocumentRoot' of your httpd.conf file
HTTP_DOCSDIR=/var/www/html
# Determine by a grep 'ScriptAlias' of your Apache httpd.conf file
HTTP_CGIDIR=/var/www/cgi-bin

ASTCFLAGS=

# Uncomment this to use the older DSP routines
#ASTCFLAGS+=-DOLD_DSP_ROUTINES

# If the file .asterisk.makeopts is present in your home directory, you can
# include all of your favorite menuselect options so that every time you download
# a new version of Asterisk, you don't have to run menuselect to set them. 
# The file /etc/asterisk.makeopts will also be included but can be overridden
# by the file in your home directory.

GLOBAL_MAKEOPTS=$(wildcard /etc/asterisk.makeopts)
USER_MAKEOPTS=$(wildcard ~/.asterisk.makeopts)

ifeq ($(strip $(foreach var,clean distclean dist-clean update,$(findstring $(var),$(MAKECMDGOALS)))),)
 ifneq ($(wildcard menuselect.makeopts),)
  include menuselect.makeopts
  include menuselect.makedeps
 endif
endif

ifeq ($(strip $(foreach var,clean distclean dist-clean update,$(findstring $(var),$(MAKECMDGOALS)))),)
 ifneq ($(wildcard makeopts),)
  include makeopts
 endif
endif

TOPDIR_CFLAGS=-Iinclude
MOD_SUBDIR_CFLAGS=-I../include -I..
OTHER_SUBDIR_CFLAGS=-I../include -I..

ifeq ($(origin MENUSELECT_CFLAGS),undefined)
  MENUSELECT_CFLAGS:=$(shell grep MENUSELECT_CFLAGS $(USER_MAKEOPTS) .)
  ifeq ($(MENUSELECT_CFLAGS),)
    MENUSELECT_CFLAGS:=$(shell grep MENUSELECT_CFLAGS $(GLOBAL_MAKEOPTS) .)
  endif
  ifneq ($(MENUSELECT_CFLAGS),)
    MENUSELECT_CFLAGS:=$(shell echo $(MENUSELECT_CFLAGS) | cut -f2 -d'=')
  endif
endif

ifeq ($(findstring dont-optimize,$(MAKECMDGOALS)),$(findstring DONT_OPTIMIZE,$(MENUSELECT_CFLAGS)))
# More GSM codec optimization
# Uncomment to enable MMXTM optimizations for x86 architecture CPU's
# which support MMX instructions.  This should be newer pentiums,
# ppro's, etc, as well as the AMD K6 and K7.  
#K6OPT  = -DK6OPT

# Tell gcc to optimize the code
OPTIMIZE+=-O6
else
  # Stack backtraces, while useful for debugging, are incompatible with optimizations
  ifeq ($(OSARCH),Linux)
    CFLAGS+=-DSTACK_BACKTRACES
  endif
endif

#   *CLI> show memory allocations [filename]
#   *CLI> show memory summary [filename]
ifneq ($(findstring MALLOC_DEBUG,$(MENUSELECT_CFLAGS)),)
  TOPDIR_CFLAGS+=-include include/asterisk/astmm.h
  MOD_SUBDIR_CFLAGS+=-include ../include/asterisk/astmm.h
endif

MOD_SUBDIR_CFLAGS+=-fPIC

ifeq ($(OSARCH),Linux)
  ifeq ($(PROC),x86_64)
    # You must have GCC 3.4 to use k8, otherwise use athlon
    PROC=k8
    #PROC=athlon
  endif

  ifeq ($(PROC),sparc64)
    #The problem with sparc is the best stuff is in newer versions of gcc (post 3.0) only.
    #This works for even old (2.96) versions of gcc and provides a small boost either way.
    #A ultrasparc cpu is really v9 but the stock debian stable 3.0 gcc doesn't support it.
    #So we go lowest common available by gcc and go a step down, still a step up from
    #the default as we now have a better instruction set to work with. - Belgarath
    PROC=ultrasparc
    OPTIONS+=$(shell if $(CC) -mtune=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-mtune=$(PROC)"; fi)
    OPTIONS+=$(shell if $(CC) -mcpu=v8 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-mcpu=v8"; fi)
    OPTIONS+=-fomit-frame-pointer
  endif

  ifeq ($(PROC),arm)
    # The Cirrus logic is the only heavily shipping arm processor with a real floating point unit
    ifeq ($(SUB_PROC),maverick)
      OPTIONS+=-fsigned-char -mcpu=ep9312
    else
      ifeq ($(SUB_PROC),xscale)
        OPTIONS+=-fsigned-char -mcpu=xscale
      else
        OPTIONS+=-fsigned-char 
      endif
    endif
  endif
endif

GREP=grep
ID=id

ifeq ($(OSARCH),SunOS)
  GREP=/usr/xpg4/bin/grep
  M4=/usr/local/bin/m4
  ID=/usr/xpg4/bin/id
endif

ASTCFLAGS+=-pipe -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations $(DEBUG)
ifneq ($(OPTIMIZE),)
ASTCFLAGS+=$(OPTIMIZE)
endif

ifeq ($(AST_DEVMODE),yes)
  ASTCFLAGS+=-Werror -Wunused
endif

ASTOBJ=-o asterisk

ifeq ($(findstring BSD,$(OSARCH)),BSD)
  ASTCFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

ifneq ($(PROC),ultrasparc)
  ASTCFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
endif

ifeq ($(PROC),ppc)
  ASTCFLAGS+=-fsigned-char
endif

ifeq ($(OSARCH),FreeBSD)
  BSDVERSION=$(shell make -V OSVERSION -f $(CROSS_COMPILE_TARGET)/usr/share/mk/bsd.port.subdir.mk)
  ASTCFLAGS+=$(shell if test $(BSDVERSION) -lt 500016 ; then echo "-D_THREAD_SAFE"; fi)
  AST_LIBS+=$(shell if test  $(BSDVERSION) -lt 502102 ; then echo "-lc_r"; else echo "-pthread"; fi)
endif # FreeBSD

ifeq ($(OSARCH),NetBSD)
  AST_CFLAGS+=-pthread -I$(CROSS_COMPILE_TARGET)/usr/pkg/include
endif

ifeq ($(OSARCH),OpenBSD)
  ASTCFLAGS+=-pthread
endif

ifeq ($(OSARCH),SunOS)
  ASTCFLAGS+=-Wcast-align -DSOLARIS -Iinclude/solaris-compat -I$(CROSS_COMPILE_TARGET)/opt/ssl/include -I$(CROSS_COMPILE_TARGET)/usr/local/ssl/include
endif

LIBEDIT=editline/libedit.a

ASTERISKVERSION:=$(shell build_tools/make_version .)

ifneq ($(wildcard .version),)
  ASTERISKVERSIONNUM:=$(shell awk -F. '{printf "%02d%02d%02d", $$1, $$2, $$3}' .version)
  RPMVERSION:=$(shell sed 's/[-\/:]/_/g' .version)
else
  RPMVERSION=unknown
endif

ifneq ($(wildcard .svn),)
  ASTERISKVERSIONNUM=999999
endif

ASTCFLAGS+=$(MALLOC_DEBUG)$(BUSYDETECT)$(OPTIONS)

MOD_SUBDIRS:=res channels pbx apps codecs formats cdr funcs
OTHER_SUBDIRS:=utils agi
SUBDIRS:=$(MOD_SUBDIRS) $(OTHER_SUBDIRS)
SUBDIRS_INSTALL:=$(SUBDIRS:%=%-install)
SUBDIRS_CLEAN:=$(SUBDIRS:%=%-clean)
SUBDIRS_CLEAN_DEPEND:=$(SUBDIRS:%=%-clean-depend)
MOD_SUBDIRS_DEPEND:=$(MOD_SUBDIRS:%=%-depend)
OTHER_SUBDIRS_DEPEND:=$(OTHER_SUBDIRS:%=%-depend)
SUBDIRS_DEPEND:=$(MOD_SUBDIRS_DEPEND) $(OTHER_SUBDIRS_DEPEND)
SUBDIRS_UNINSTALL:=$(SUBDIRS:%=%-uninstall)

OBJS=io.o sched.o logger.o frame.o loader.o config.o channel.o \
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

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/sys/poll.h),)
  OBJS+= poll.o
  ASTCFLAGS+=-DPOLLCOMPAT
endif

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/dlfcn.h),)
  OBJS+= dlfcn.o
  ASTCFLAGS+=-DDLFCNCOMPAT
endif

ifeq ($(OSARCH),Linux)
  AST_LIBS+=-ldl -lpthread $(EDITLINE_LIB) -lm -lresolv  #-lnjamd
else
  AST_LIBS+=$(EDITLINE_LIB) -lm
endif

ifeq ($(OSARCH),Darwin)
  AST_LIBS+=-lresolv
  ASTCFLAGS+=-D__Darwin__
  AUDIO_LIBS=-framework CoreAudio
  ASTLINK=-Wl,-dynamic
  SOLINK=-dynamic -bundle -undefined suppress -force_flat_namespace
  # Mac on Intel CoreDuo does not need poll compatibility layer
  ifneq ($(PROC),i386)
    OBJS+=poll.o
    ASTCFLAGS+=-DPOLLCOMPAT
  endif
else
# These are used for all but Darwin
  ASTLINK=-Wl,-E 
  SOLINK=-shared -Xlinker -x
  ifeq ($(findstring BSD,$(OSARCH)),BSD)
    LDFLAGS+=-L$(CROSS_COMPILE_TARGET)/usr/local/lib
  endif
endif

ifeq ($(OSARCH),FreeBSD)
  AST_LIBS+=-lcrypto
endif

ifeq ($(OSARCH),NetBSD)
  AST_LIBS+=-lpthread -lcrypto -lm -L$(CROSS_COMPILE_TARGET)/usr/pkg/lib $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),OpenBSD)
  AST_LIBS+=-lcrypto -lpthread -lm $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),SunOS)
  AST_LIBS+=-lpthread -ldl -lnsl -lsocket -lresolv -L$(CROSS_COMPILE_TARGET)/opt/ssl/lib -L$(CROSS_COMPILE_TARGET)/usr/local/ssl/lib
  OBJS+=strcompat.o
  ASTLINK=
  SOLINK=-shared -fpic -L$(CROSS_COMPILE_TARGET)/usr/local/ssl/lib
endif

ifeq ($(MAKETOPLEVEL),$(MAKELEVEL))
  CFLAGS+=$(TOPDIR_CFLAGS)$(ASTCFLAGS)
endif

# This is used when generating the doxygen documentation
ifneq ($(DOT),:)
  HAVEDOT=yes
else
  HAVEDOT=no
endif

include Makefile.rules

_all: all
	@echo " +--------- Asterisk Build Complete ---------+"  
	@echo " + Asterisk has successfully been built, but +"  
	@echo " + cannot be run before being installed by   +"  
	@echo " + running:                                  +"  
	@echo " +                                           +"
	@echo " +               make install                +"  
	@echo " +-------------------------------------------+"  

all: cleantest config.status menuselect.makeopts depend $(SUBDIRS) asterisk

$(MOD_SUBDIRS):
	@CFLAGS="$(MOD_SUBDIR_CFLAGS)$(ASTCFLAGS)" $(MAKE) -C $@

$(OTHER_SUBDIRS):
	@CFLAGS="$(OTHER_SUBDIR_CFLAGS)$(ASTCFLAGS)" $(MAKE) -C $@

config.status: configure
	@CFLAGS="" ./configure
	@echo "****"
	@echo "**** The configure script was just executed, so 'make' needs to be"
	@echo "**** restarted."
	@echo "****"
	@exit 1

makeopts: configure
	@CFLAGS="" ./configure
	@echo "****"
	@echo "**** The configure script was just executed, so 'make' needs to be"
	@echo "**** restarted."
	@echo "****"
	@exit 1

menuselect.makeopts menuselect.makedeps: menuselect/menuselect menuselect-tree
	menuselect/menuselect --check-deps $(GLOBAL_MAKEOPTS) $(USER_MAKEOPTS) menuselect.makeopts

#ifneq ($(wildcard tags),)
ctags: tags
#endif

ifneq ($(wildcard TAGS),)
all: TAGS
endif

editline/config.h:
	cd editline && unset CFLAGS AST_LIBS && CFLAGS="$(OPTIMIZE)" ./configure ; \

editline/libedit.a:
	cd editline && unset CFLAGS AST_LIBS && test -f config.h || CFLAGS="$(OPTIMIZE)" ./configure
	$(MAKE) -C editline libedit.a

db1-ast/libdb1.a:
	$(MAKE) -C db1-ast libdb1.a

ifeq ($(strip $(foreach var,clean distclean dist-clean update,$(findstring $(var),$(MAKECMDGOALS)))),)
 ifneq ($(wildcard .depend),)
  include .depend
 endif
endif

ifeq ($(strip $(foreach var,clean distclean dist-clean update,$(findstring $(var),$(MAKECMDGOALS)))),)
 ifneq ($(wildcard .tags-depend),)
  include .tags-depend
 endif
endif

ast_expr2.c ast_expr2.h:
	bison -o $@ -d --name-prefix=ast_yy ast_expr2.y

ast_expr2f.c:
	flex -o $@ --full ast_expr2.fl

testexpr2: config.status include/asterisk/buildopts.h ast_expr2f.c ast_expr2.c ast_expr2.h
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2f.c
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2.c
	$(CC) -g -o testexpr2 ast_expr2f.o ast_expr2.o
	rm ast_expr2.o ast_expr2f.o 

manpage: asterisk.8

asterisk.8: asterisk.sgml
	rm -f asterisk.8
	docbook2man asterisk.sgml
	mv ./*.8 asterisk.8

asterisk.pdf: asterisk.sgml
	docbook2pdf asterisk.sgml

asterisk.ps: asterisk.sgml
	docbook2ps asterisk.sgml

asterisk.html: asterisk.sgml
	docbook2html asterisk.sgml
	mv r1.html asterisk.html

asterisk.txt: asterisk.sgml
	docbook2txt asterisk.sgml

defaults.h: makeopts
	@build_tools/make_defaults_h > $@.tmp
	@if cmp -s $@.tmp $@ ; then : ; else \
		mv $@.tmp $@ ; \
	fi
	@rm -f $@.tmp

include/asterisk/version.h:
	@build_tools/make_version_h > $@.tmp
	@if cmp -s $@.tmp $@ ; then : ; else \
		mv $@.tmp $@ ; \
	fi
	@rm -f $@.tmp

include/asterisk/buildopts.h: menuselect.makeopts
	@build_tools/make_buildopts_h > $@.tmp
	@if cmp -s $@.tmp $@ ; then : ; else \
		mv $@.tmp $@ ; \
	fi
	@rm -f $@.tmp

channel.o: CFLAGS+=$(ZAPTEL_INCLUDE)

asterisk: include/asterisk/buildopts.h editline/libedit.a db1-ast/libdb1.a $(OBJS)
	@build_tools/make_build_h > include/asterisk/build.h.tmp
	@if cmp -s include/asterisk/build.h.tmp include/asterisk/build.h ; then echo ; else \
		mv include/asterisk/build.h.tmp include/asterisk/build.h ; \
	fi
	@rm -f include/asterisk/build.h.tmp
	@$(CC) -c -o buildinfo.o $(CFLAGS) buildinfo.c
	@echo "   [LD] $(OBJS) buildinfo.o $(LIBEDIT) db1-ast/libdb1.1 $(AST_LIBS) -> $@"
	@$(CC) $(DEBUG) $(ASTOBJ) $(ASTLINK) $(OBJS) buildinfo.o $(LIBEDIT) db1-ast/libdb1.a $(AST_LIBS)

muted: muted.o
muted: LIBS+=$(AUDIO_LIBS)

$(SUBDIRS_CLEAN_DEPEND):
	@$(MAKE) -C $(@:-clean-depend=) clean-depend

$(SUBDIRS_CLEAN):
	@$(MAKE) -C $(@:-clean=) clean

clean-depend: $(SUBDIRS_CLEAN_DEPEND)

clean: $(SUBDIRS_CLEAN) clean-depend
	rm -f *.o *.so asterisk
	rm -f defaults.h
	rm -f include/asterisk/build.h
	rm -f include/asterisk/version.h
	rm -f .tags-sources tags TAGS
	rm -f .depend .tags-depend
	@if [ -f editline/Makefile ]; then $(MAKE) -C editline distclean ; fi
	@$(MAKE) -C db1-ast clean
	@$(MAKE) -C stdtime clean
	@$(MAKE) -C menuselect clean

dist-clean: distclean

distclean: clean
	@$(MAKE) -C mxml clean
	@$(MAKE) -C menuselect dist-clean
	@$(MAKE) -C sounds dist-clean
	rm -f menuselect.makeopts makeopts menuselect-tree menuselect.makedeps
	rm -f config.log config.status
	rm -rf autom4te.cache
	rm -f include/asterisk/autoconfig.h
	rm -f include/asterisk/buildopts.h
	rm -rf doc/api
	rm -f build_tools/menuselect-deps

datafiles: all
	if [ x`$(ID) -un` = xroot ]; then sh build_tools/mkpkgconfig $(DESTDIR)/usr/lib/pkgconfig; fi
# Should static HTTP be installed during make samples or even with its own target ala
# webvoicemail?  There are portions here that *could* be customized but might also be
# improved a lot.  I'll put it here for now.
	mkdir -p $(DESTDIR)$(ASTDATADIR)/static-http
	for x in static-http/*; do \
		$(INSTALL) -m 644 $$x $(DESTDIR)$(ASTDATADIR)/static-http ; \
	done
	mkdir -p $(DESTDIR)$(ASTDATADIR)/images
	for x in images/*.jpg; do \
		$(INSTALL) -m 644 $$x $(DESTDIR)$(ASTDATADIR)/images ; \
	done
	mkdir -p $(DESTDIR)$(AGI_DIR)
	$(MAKE) -C sounds install

update: 
	@if [ -d .svn ]; then \
		echo "Updating from Subversion..." ; \
		svn update | tee update.out; \
		rm -f .version; \
		if [ `grep -c ^C update.out` -gt 0 ]; then \
			echo ; echo "The following files have conflicts:" ; \
			grep ^C update.out | cut -b4- ; \
		fi ; \
		rm -f update.out; \
		$(MAKE) clean-depend; \
	else \
		echo "Not under version control";  \
	fi

NEWHEADERS=$(notdir $(wildcard include/asterisk/*.h))
OLDHEADERS=$(filter-out $(NEWHEADERS),$(notdir $(wildcard $(DESTDIR)$(ASTHEADERDIR)/*.h)))

bininstall: all
	mkdir -p $(DESTDIR)$(MODULES_DIR)
	mkdir -p $(DESTDIR)$(ASTSBINDIR)
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	mkdir -p $(DESTDIR)$(ASTBINDIR)
	mkdir -p $(DESTDIR)$(ASTVARRUNDIR)
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/voicemail
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/dictate
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/system
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/tmp
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/meetme
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/monitor
	if [ -f asterisk ]; then $(INSTALL) -m 755 asterisk $(DESTDIR)$(ASTSBINDIR)/; fi
	if [ -f asterisk.dll ]; then $(INSTALL) -m 755 asterisk.dll $(DESTDIR)$(ASTSBINDIR)/; fi
	$(LN) -sf asterisk $(DESTDIR)$(ASTSBINDIR)/rasterisk
	$(INSTALL) -m 755 contrib/scripts/astgenkey $(DESTDIR)$(ASTSBINDIR)/
	$(INSTALL) -m 755 contrib/scripts/autosupport $(DESTDIR)$(ASTSBINDIR)/
	if [ ! -f $(DESTDIR)$(ASTSBINDIR)/safe_asterisk ]; then \
		cat contrib/scripts/safe_asterisk | sed 's|__ASTERISK_SBIN_DIR__|$(ASTSBINDIR)|;' > $(DESTDIR)$(ASTSBINDIR)/safe_asterisk ;\
		chmod 755 $(DESTDIR)$(ASTSBINDIR)/safe_asterisk;\
	fi
	$(INSTALL) -d $(DESTDIR)$(ASTHEADERDIR)
	$(INSTALL) -m 644 include/asterisk.h $(DESTDIR)$(includedir)
	$(INSTALL) -m 644 include/asterisk/*.h $(DESTDIR)$(ASTHEADERDIR)
	if [ -n "$(OLDHEADERS)" ]; then \
		rm -f $(addprefix $(DESTDIR)$(ASTHEADERDIR)/,$(OLDHEADERS)) ;\
	fi
	mkdir -p $(DESTDIR)$(ASTLOGDIR)/cdr-csv
	mkdir -p $(DESTDIR)$(ASTLOGDIR)/cdr-custom
	mkdir -p $(DESTDIR)$(ASTDATADIR)/keys
	mkdir -p $(DESTDIR)$(ASTDATADIR)/firmware
	mkdir -p $(DESTDIR)$(ASTDATADIR)/firmware/iax
	mkdir -p $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 keys/iaxtel.pub $(DESTDIR)$(ASTDATADIR)/keys
	$(INSTALL) -m 644 keys/freeworlddialup.pub $(DESTDIR)$(ASTDATADIR)/keys
	$(INSTALL) -m 644 asterisk.8 $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 contrib/scripts/astgenkey.8 $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 contrib/scripts/autosupport.8 $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 contrib/scripts/safe_asterisk.8 $(DESTDIR)$(ASTMANDIR)/man8
	$(INSTALL) -m 644 contrib/firmware/iax/iaxy.bin $(DESTDIR)$(ASTDATADIR)/firmware/iax/iaxy.bin; \

$(SUBDIRS_INSTALL):
	@$(MAKE) -C $(@:-install=) install

NEWMODS=$(notdir $(wildcard */*.so))
OLDMODS=$(filter-out $(NEWMODS),$(notdir $(wildcard $(DESTDIR)$(MODULES_DIR)/*.so)))

oldmodcheck:
	@if [ -n "$(OLDMODS)" ]; then \
		echo " WARNING WARNING WARNING" ;\
		echo "" ;\
		echo " Your Asterisk modules directory, located at" ;\
		echo " $(DESTDIR)$(MODULES_DIR)" ;\
		echo " contains modules that were not installed by this " ;\
		echo " version of Asterisk. Please ensure that these" ;\
		echo " modules are compatible with this version before" ;\
		echo " attempting to run Asterisk." ;\
		echo "" ;\
		for f in $(OLDMODS); do \
			echo "    $$f" ;\
		done ;\
		echo "" ;\
		echo " WARNING WARNING WARNING" ;\
	fi

install: all datafiles bininstall $(SUBDIRS_INSTALL)
	@if [ -x /usr/sbin/asterisk-post-install ]; then \
		/usr/sbin/asterisk-post-install $(DESTDIR) . ; \
	fi
	@echo " +---- Asterisk Installation Complete -------+"  
	@echo " +                                           +"
	@echo " +    YOU MUST READ THE SECURITY DOCUMENT    +"
	@echo " +                                           +"
	@echo " + Asterisk has successfully been installed. +"  
	@echo " + If you would like to install the sample   +"  
	@echo " + configuration files (overwriting any      +"
	@echo " + existing config files), run:              +"  
	@echo " +                                           +"
	@echo " +               $(MAKE) samples                +"
	@echo " +                                           +"
	@echo " +-----------------  or ---------------------+"
	@echo " +                                           +"
	@echo " + You can go ahead and install the asterisk +"
	@echo " + program documentation now or later run:   +"
	@echo " +                                           +"
	@echo " +              $(MAKE) progdocs                +"
	@echo " +                                           +"
	@echo " + **Note** This requires that you have      +"
	@echo " + doxygen installed on your local system    +"
	@echo " +-------------------------------------------+"
	@$(MAKE) -s oldmodcheck

upgrade: all bininstall

adsi:
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.adsi; do \
		if [ ! -f $(DESTDIR)$(ASTETCDIR)/$$x ]; then \
			$(INSTALL) -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x` ; \
		fi ; \
	done

samples: adsi
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.sample; do \
		if [ -f $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample` ]; then \
			if [ "$(OVERWRITE)" = "y" ]; then \
				if cmp -s $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample` $$x ; then \
					echo "Config file $$x is unchanged"; \
					continue; \
				fi ; \
				mv -f $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample` $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample`.old ; \
			else \
				echo "Skipping config file $$x"; \
				continue; \
			fi ;\
		fi ; \
		$(INSTALL) -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`$(BASENAME) $$x .sample` ;\
	done
	if [ "$(OVERWRITE)" = "y" ] || [ ! -f $(DESTDIR)$(ASTCONFPATH) ]; then \
		( \
		echo "[directories]" ; \
		echo "astetcdir => $(ASTETCDIR)" ; \
		echo "astmoddir => $(MODULES_DIR)" ; \
		echo "astvarlibdir => $(ASTVARLIBDIR)" ; \
		echo "astdatadir => $(ASTDATADIR)" ; \
		echo "astagidir => $(AGI_DIR)" ; \
		echo "astspooldir => $(ASTSPOOLDIR)" ; \
		echo "astrundir => $(ASTVARRUNDIR)" ; \
		echo "astlogdir => $(ASTLOGDIR)" ; \
		echo "" ; \
		echo "; Changing the following lines may compromise your security." ; \
		echo ";[files]" ; \
		echo ";astctlpermissions = 0660" ; \
		echo ";astctlowner = root" ; \
		echo ";astctlgroup = apache" ; \
		echo ";astctl = asterisk.ctl" ; \
		echo ";[options]" ; \
		echo ";internal_timing = yes" ; \
		) > $(DESTDIR)$(ASTCONFPATH) ; \
	else \
		echo "Skipping asterisk.conf creation"; \
	fi
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/INBOX
	:> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/unavail.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isunavail; do \
		cat $(DESTDIR)$(ASTDATADIR)/sounds/$$x.gsm >> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/unavail.gsm ; \
	done
	:> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/busy.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isonphone; do \
		cat $(DESTDIR)$(ASTDATADIR)/sounds/$$x.gsm >> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/busy.gsm ; \
	done

webvmail:
	@[ -d $(DESTDIR)$(HTTP_DOCSDIR)/ ] || ( printf "http docs directory not found.\nUpdate assignment of variable HTTP_DOCSDIR in Makefile!\n" && exit 1 )
	@[ -d $(DESTDIR)$(HTTP_CGIDIR) ] || ( printf "cgi-bin directory not found.\nUpdate assignment of variable HTTP_CGIDIR in Makefile!\n" && exit 1 )
	$(INSTALL) -m 4755 -o root -g root contrib/scripts/vmail.cgi $(DESTDIR)$(HTTP_CGIDIR)/vmail.cgi
	mkdir -p $(DESTDIR)$(HTTP_DOCSDIR)/_asterisk
	for x in images/*.gif; do \
		$(INSTALL) -m 644 $$x $(DESTDIR)$(HTTP_DOCSDIR)/_asterisk/; \
	done
	@echo " +--------- Asterisk Web Voicemail ----------+"  
	@echo " +                                           +"
	@echo " + Asterisk Web Voicemail is installed in    +"
	@echo " + your cgi-bin directory:                   +"
	@echo " + $(DESTDIR)$(HTTP_CGIDIR)"
	@echo " + IT USES A SETUID ROOT PERL SCRIPT, SO     +"
	@echo " + IF YOU DON'T LIKE THAT, UNINSTALL IT!     +"
	@echo " +                                           +"
	@echo " + Other static items have been stored in:   +"
	@echo " + $(DESTDIR)$(HTTP_DOCSDIR)"
	@echo " +                                           +"
	@echo " + If these paths do not match your httpd    +"
	@echo " + installation, correct the definitions     +"
	@echo " + in your Makefile of HTTP_CGIDIR and       +"
	@echo " + HTTP_DOCSDIR                              +"
	@echo " +                                           +"
	@echo " +-------------------------------------------+"  

spec: 
	sed "s/^Version:.*/Version: $(RPMVERSION)/g" redhat/asterisk.spec > asterisk.spec ; \

rpm: __rpm

__rpm: include/asterisk/version.h include/asterisk/buildopts.h spec
	rm -rf /tmp/asterisk ; \
	mkdir -p /tmp/asterisk/redhat/RPMS/i386 ; \
	$(MAKE) DESTDIR=/tmp/asterisk install ; \
	$(MAKE) DESTDIR=/tmp/asterisk samples ; \
	mkdir -p /tmp/asterisk/etc/rc.d/init.d ; \
	cp -f contrib/init.d/rc.redhat.asterisk /tmp/asterisk/etc/rc.d/init.d/asterisk ; \
	rpmbuild --rcfile /usr/lib/rpm/rpmrc:redhat/rpmrc -bb asterisk.spec

progdocs:
	(cat contrib/asterisk-ng-doxygen; echo "HAVE_DOT=$(HAVEDOT)"; \
	echo "PROJECT_NUMBER=$(ASTERISKVERSION)") | doxygen - 

config:
	@if [ "${OSARCH}" = "Linux" ]; then \
		if [ -f /etc/redhat-release -o -f /etc/fedora-release ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.redhat.asterisk /etc/rc.d/init.d/asterisk; \
			/sbin/chkconfig --add asterisk; \
		elif [ -f /etc/debian_version ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.debian.asterisk /etc/init.d/asterisk; \
			/usr/sbin/update-rc.d asterisk start 10 2 3 4 5 . stop 91 2 3 4 5 .; \
		elif [ -f /etc/gentoo-release ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.gentoo.asterisk /etc/init.d/asterisk; \
			/sbin/rc-update add asterisk default; \
		elif [ -f /etc/mandrake-release ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.mandrake.asterisk /etc/rc.d/init.d/asterisk; \
			/sbin/chkconfig --add asterisk; \
		elif [ -f /etc/SuSE-release -o -f /etc/novell-release ]; then \
			$(INSTALL) -m 755 contrib/init.d/rc.suse.asterisk /etc/init.d/asterisk; \
			/sbin/chkconfig --add asterisk; \
		elif [ -f /etc/slackware-version ]; then \
			echo "Slackware is not currently supported, although an init script does exist for it." \
		else \
			echo "We could not install init scripts for your distribution."; \
		fi \
	else \
		echo "We could not install init scripts for your operating system."; \
	fi

dont-optimize: _all

valgrind: dont-optimize

$(MOD_SUBDIRS_DEPEND):
	@CFLAGS="$(MOD_SUBDIR_CFLAGS)$(ASTCFLAGS)" $(MAKE) -C $(@:-depend=) depend

$(OTHER_SUBDIRS_DEPEND):
	@CFLAGS="$(OTHER_SUBDIR_CFLAGS)$(ASTCFLAGS)" $(MAKE) -C $(@:-depend=) depend

depend: include/asterisk/version.h include/asterisk/buildopts.h .depend defaults.h $(SUBDIRS_DEPEND)

.depend: include/asterisk/version.h include/asterisk/buildopts.h defaults.h
	build_tools/mkdep $(CFLAGS) $(wildcard *.c)

.tags-depend:
	@echo -n ".tags-depend: " > $@
	@$(FIND) . -maxdepth 1 -name \*.c -printf "\t%p \\\\\n" >> $@
	@$(FIND) . -maxdepth 1 -name \*.h -printf "\t%p \\\\\n" >> $@
	@$(FIND) $(SUBDIRS) -name \*.c -printf "\t%p \\\\\n" >> $@
	@$(FIND) $(SUBDIRS) -name \*.h -printf "\t%p \\\\\n" >> $@
	@$(FIND) include -name \*.h -printf "\t%p \\\\\n" >> $@
	@echo >> $@

.tags-sources:
	@rm -f $@
	@$(FIND) . -maxdepth 1 -name \*.c -print >> $@
	@$(FIND) . -maxdepth 1 -name \*.h -print >> $@
	@$(FIND) $(SUBDIRS) -name \*.c -print >> $@
	@$(FIND) $(SUBDIRS) -name \*.h -print >> $@
	@$(FIND) include -name \*.h -print >> $@

tags: .tags-depend .tags-sources
	ctags -L .tags-sources -o $@

ctags: tags

TAGS: .tags-depend .tags-sources
	etags -o $@ `cat .tags-sources`

etags: TAGS

%_env:
	$(MAKE) -C $(shell echo $@ | sed "s/_env//g") env

sounds:
	$(MAKE) -C sounds all

env:
	env

# If the cleancount has been changed, force a make clean.
# .cleancount is the global clean count, and .lastclean is the 
# last clean count we had

cleantest:
	@if cmp -s .cleancount .lastclean ; then echo ; else \
		$(MAKE) clean; cp -f .cleancount .lastclean;\
		$(MAKE) defaults.h;\
	fi

$(SUBDIRS_UNINSTALL):
	@$(MAKE) -C $(@:-uninstall=) uninstall

_uninstall: $(SUBDIRS_UNINSTALL)
	rm -f $(DESTDIR)$(MODULES_DIR)/*
	rm -f $(DESTDIR)$(ASTSBINDIR)/*asterisk*
	rm -f $(DESTDIR)$(ASTSBINDIR)/astgenkey
	rm -f $(DESTDIR)$(ASTSBINDIR)/autosupport
	rm -rf $(DESTDIR)$(ASTHEADERDIR)
	rm -rf $(DESTDIR)$(ASTDATADIR)/firmware
	rm -rf $(DESTDIR)$(ASTMANDIR)/man8
	$(MAKE) -C sounds uninstall

uninstall: _uninstall
	@echo " +--------- Asterisk Uninstall Complete -----+"  
	@echo " + Asterisk binaries, sounds, man pages,     +"  
	@echo " + headers, modules, and firmware builds,    +"  
	@echo " + have all been uninstalled.                +"  
	@echo " +                                           +"
	@echo " + To remove ALL traces of Asterisk,         +"
	@echo " + including configuration, spool            +"
	@echo " + directories, and logs, run the following  +"
	@echo " + command:                                  +"
	@echo " +                                           +"
	@echo " +            $(MAKE) uninstall-all             +"  
	@echo " +-------------------------------------------+"  

uninstall-all: _uninstall
	rm -rf $(DESTDIR)$(ASTLIBDIR)
	rm -rf $(DESTDIR)$(ASTVARLIBDIR)
	rm -rf $(DESTDIR)$(ASTDATADIR)
	rm -rf $(DESTDIR)$(ASTSPOOLDIR)
	rm -rf $(DESTDIR)$(ASTETCDIR)
	rm -rf $(DESTDIR)$(ASTLOGDIR)

menuselect: menuselect/menuselect menuselect-tree
	-@menuselect/menuselect $(GLOBAL_MAKEOPTS) $(USER_MAKEOPTS) menuselect.makeopts && echo "menuselect changes saved!" || echo "menuselect changes NOT saved!"

menuselect/menuselect: menuselect/menuselect.c menuselect/menuselect_curses.c menuselect/menuselect_stub.c menuselect/menuselect.h menuselect/linkedlists.h config.status mxml/libmxml.a
	@CFLAGS="-include $(ASTTOPDIR)/include/asterisk/autoconfig.h -I$(ASTTOPDIR)/include" PARENTSRC="$(ASTTOPDIR)" $(MAKE) -C menuselect menuselect

mxml/libmxml.a:
	@cd mxml && unset CFLAGS AST_LIBS && test -f config.h || ./configure
	$(MAKE) -C mxml libmxml.a

menuselect-tree: $(foreach dir,$(MOD_SUBDIRS),$(wildcard $(dir)/*.c) $(wildcard $(dir)/*.cc)) build_tools/cflags.xml sounds/sounds.xml
	@echo "Generating list of available modules ..."
	@build_tools/prep_moduledeps > $@

.PHONY: menuselect sounds clean clean-depend dist-clean distclean all _all depend cleantest uninstall _uninstall uninstall-all dont-optimize valgrind $(SUBDIRS_INSTALL) $(SUBDIRS_CLEAN) $(SUBDIRS_CLEAN_DEPEND) $(SUBDIRS_DEPEND) $(SUBDIRS_UNINSTALL) $(SUBDIRS)
