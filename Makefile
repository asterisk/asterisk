#
# Asterisk -- A telephony toolkit for Linux.
# 
# Top level Makefile
#
# Copyright (C) 1999-2005, Mark Spencer
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

.EXPORT_ALL_VARIABLES:

# Create OPTIONS variable
OPTIONS=
# If cross compiling, define these to suit
# CROSS_COMPILE=/opt/montavista/pro/devkit/arm/xscale_be/bin/xscale_be-
# CROSS_COMPILE_BIN=/opt/montavista/pro/devkit/arm/xscale_be/bin/
# CROSS_COMPILE_TARGET=/opt/montavista/pro/devkit/arm/xscale_be/target
CC=$(CROSS_COMPILE)gcc
HOST_CC=gcc
# CROSS_ARCH=Linux
# CROSS_PROC=arm
# SUB_PROC=xscale # or maverick

######### More GSM codec optimization
######### Uncomment to enable MMXTM optimizations for x86 architecture CPU's
######### which support MMX instructions.  This should be newer pentiums,
######### ppro's, etc, as well as the AMD K6 and K7.  
#K6OPT  = -DK6OPT

#Overwite config files on "make samples"
OVERWRITE=y

#Tell gcc to optimize the asterisk's code
OPTIMIZE+=-O6

#Include debug symbols in the executables (-g) and profiling info (-pg)
DEBUG=-g #-pg

# If you are running a radio application, define RADIO_RELAX so that the DTMF
# will be received more reliably
#OPTIONS += -DRADIO_RELAX

# If you don't have a lot of memory (e.g. embedded Asterisk), define LOW_MEMORY
# to reduce the size of certain static buffers

#ifneq ($(CROSS_COMPILE),)
#OPTIONS += -DLOW_MEMORY
#endif

# Optional debugging parameters
DEBUG_THREADS = #-DDUMP_SCHEDULER #-DDEBUG_SCHEDULER #-DDEBUG_THREADS #-DDO_CRASH #-DDETECT_DEADLOCKS

# Uncomment next one to enable ast_frame tracing (for debugging)
TRACE_FRAMES = #-DTRACE_FRAMES

# Uncomment next one to enable malloc debugging
# You can view malloc debugging with:
#   *CLI> show memory allocations [filename]
#   *CLI> show memory summary [filename]
#
MALLOC_DEBUG = #-include $(PWD)/include/asterisk/astmm.h

# Where to install asterisk after compiling
# Default -> leave empty
INSTALL_PREFIX=

# Staging directory
# Files are copied here temporarily during the install process
# For example, make DESTDIR=/tmp/asterisk woud put things in
# /tmp/asterisk/etc/asterisk
DESTDIR=

# Original busydetect routine
BUSYDETECT = #-DBUSYDETECT

# Improved busydetect routine, comment the previous one if you use this one
BUSYDETECT+= -DBUSYDETECT_MARTIN 
# Detect the busy signal looking only at tone lengths
# For example if you have 3 beeps 100ms tone, 100ms silence separated by 500 ms of silence
BUSYDETECT+= #-DBUSYDETECT_TONEONLY
# Inforce the detection of busy singal (get rid of false hangups)
# Don't use together with -DBUSYDETECT_TONEONLY
BUSYDETECT+= #-DBUSYDETECT_COMPARE_TONE_AND_SILENCE

ASTLIBDIR=$(INSTALL_PREFIX)/usr/lib/asterisk
ASTVARLIBDIR=$(INSTALL_PREFIX)/var/lib/asterisk
ASTETCDIR=$(INSTALL_PREFIX)/etc/asterisk
ASTSPOOLDIR=$(INSTALL_PREFIX)/var/spool/asterisk
ASTLOGDIR=$(INSTALL_PREFIX)/var/log/asterisk
ASTHEADERDIR=$(INSTALL_PREFIX)/usr/include/asterisk
ASTCONFPATH=$(ASTETCDIR)/asterisk.conf
ASTBINDIR=$(INSTALL_PREFIX)/usr/bin
ASTSBINDIR=$(INSTALL_PREFIX)/usr/sbin
ASTVARRUNDIR=$(INSTALL_PREFIX)/var/run
ASTMANDIR=$(INSTALL_PREFIX)/usr/share/man

MODULES_DIR=$(ASTLIBDIR)/modules
AGI_DIR=$(ASTVARLIBDIR)/agi-bin

# Pentium Pro Optimize
#PROC=i686

# Pentium & VIA processors optimize
#PROC=i586

#PROC=k6
#PROC=ppc

#Uncomment this to use the older DSP routines
#CFLAGS+=-DOLD_DSP_ROUTINES

# Determine by a grep 'DocumentRoot' of your httpd.conf file
HTTP_DOCSDIR=/var/www/html
# Determine by a grep 'ScriptAlias' of your httpd.conf file
HTTP_CGIDIR=/var/www/cgi-bin

# If the file .asterisk.makeopts is present in your home directory, you can
# include all of your favorite Makefile options so that every time you download
# a new version of Asterisk, you don't have to edit the makefile to set them. 
# The file, /etc/asterisk.makeopts will also be included, but can be overridden
# by the file in your home directory.

ifneq ($(wildcard /etc/asterisk.makeopts),)
include /etc/asterisk.makeopts
endif

ifneq ($(wildcard ~/.asterisk.makeopts),)
include ~/.asterisk.makeopts
endif

ifeq ($(CROSS_COMPILE),)
OSARCH=$(shell uname -s)
else
OSARCH=$(CROSS_ARCH)
endif

ifeq (${OSARCH},Linux)
ifeq ($(CROSS_COMPILE),)
PROC?=$(shell uname -m)
else
PROC=$(CROSS_PROC)
endif
ifeq ($(PROC),x86_64)
# You must have GCC 3.4 to use k8, otherwise use athlon
PROC=k8
#PROC=athlon
OPTIONS+=-m64
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
OPTIONS+=-fsigned-char -msoft-float -mcpu=xscale
else
OPTIONS+=-fsigned-char -msoft-float 
endif
endif
endif
MPG123TARG=linux
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
PROC=$(shell uname -m)
endif

PWD=$(shell pwd)

GREP=grep
ifeq (${OSARCH},SunOS)
GREP=/usr/xpg4/bin/grep
M4=/usr/local/bin/m4
endif

INCLUDE=-Iinclude -I../include
CFLAGS+=-pipe  -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations $(DEBUG) $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE #-DMAKE_VALGRIND_HAPPY
CFLAGS+=$(OPTIMIZE)

ifneq ($(PROC),ultrasparc)
CFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
endif
ifeq ($(PROC),ppc)
CFLAGS+=-fsigned-char
endif

CFLAGS+=$(shell if [ -f $(CROSS_COMPILE_TARGET)/usr/include/osp/osp.h ]; then echo "-DOSP_SUPPORT -I$(CROSS_COMPILE_TARGET)/usr/include/osp" ; fi)

ifeq (${OSARCH},FreeBSD)
OSVERSION=$(shell make -V OSVERSION -f $(CROSS_COMPILE_TARGET)/usr/share/mk/bsd.port.subdir.mk)
CFLAGS+=$(shell if test ${OSVERSION} -lt 500016 ; then echo "-D_THREAD_SAFE"; fi)
LIBS+=$(shell if test  ${OSVERSION} -lt 502102 ; then echo "-lc_r"; else echo "-pthread"; fi) -L$(CROSS_COMPILE_TARGET)/usr/local/lib
INCLUDE+=-I$(CROSS_COMPILE_TARGET)/usr/local/include
CFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/usr/local/include/spandsp ]; then echo "-I$(CROSS_COMPILE_TARGET)/usr/local/include/spandsp"; fi)
MPG123TARG=freebsd
endif # FreeBSD

ifeq (${OSARCH},NetBSD)
CFLAGS+=-pthread
INCLUDE+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -I$(CROSS_COMPILE_TARGET)/usr/pkg/include
MPG123TARG=netbsd
endif

ifeq (${OSARCH},OpenBSD)
CFLAGS+=-pthread
endif
ifeq (${OSARCH},SunOS)
CFLAGS+=-Wcast-align -DSOLARIS
INCLUDE+=-Iinclude/solaris-compat -I$(CROSS_COMPILE_TARGET)/usr/local/ssl/include
endif

CFLAGS+=$(shell if [ -f $(CROSS_COMPILE_TARGET)/usr/include/linux/zaptel.h ]; then echo "-DZAPTEL_OPTIMIZATIONS"; fi)
CFLAGS+=$(shell if [ -f $(CROSS_COMPILE_TARGET)/usr/local/include/zaptel.h ]; then echo "-DZAPTEL_OPTIMIZATIONS"; fi)

LIBEDIT=editline/libedit.a

ASTERISKVERSION=$(shell if [ -f .version ]; then cat .version; else if [ -d CVS ]; then if [ -f CVS/Tag ] ; then echo "CVS-`sed 's/^T//g' CVS/Tag`-`date +"%D-%T"`"; else echo "CVS-HEAD"; fi; fi; fi)
ASTERISKVERSIONNUM=$(shell if [ -d CVS ]; then echo 999999 ; else if [ -f .version ] ; then awk -F. '{printf "%02d%02d%02d", $$1, $$2, $$3}' .version ; else echo 000000 ; fi ; fi)
# Set the following two variables to match your httpd installation.

RPMVERSION=$(shell if [ -f .version ]; then sed 's/[-\/:]/_/g' .version; else echo "unknown" ; fi)

CFLAGS+= $(DEBUG_THREADS)
CFLAGS+= $(TRACE_FRAMES)
CFLAGS+= $(MALLOC_DEBUG)
CFLAGS+= $(BUSYDETECT)
CFLAGS+= $(OPTIONS)
CFLAGS+= -fomit-frame-pointer 
SUBDIRS=res channels pbx apps codecs formats agi cdr funcs utils stdtime
ifeq (${OSARCH},Linux)
LIBS=-ldl -lpthread
endif
LIBS+=-lncurses -lm
ifeq (${OSARCH},Linux)
LIBS+=-lresolv  #-lnjamd
endif
ifeq (${OSARCH},Darwin)
LIBS+=-lresolv
CFLAGS+=-D__Darwin__
AUDIO_LIBS=-framework CoreAudio
endif
ifeq (${OSARCH},FreeBSD)
LIBS+=-lcrypto
endif
ifeq (${OSARCH},NetBSD)
LIBS+=-lpthread -lcrypto -lm -L$(CROSS_COMPILE_TARGET)/usr/local/lib -L$(CROSS_COMPILE_TARGET)/usr/pkg/lib -lncurses
endif
ifeq (${OSARCH},OpenBSD)
LIBS=-lcrypto -lpthread -lm -lncurses
endif
ifeq (${OSARCH},SunOS)
LIBS+=-lpthread -ldl -lnsl -lsocket -lresolv -L$(CROSS_COMPILE_TARGET)/usr/local/ssl/lib
endif
LIBS+=-lssl

OBJS=io.o sched.o logger.o frame.o loader.o config.o channel.o \
	translate.o file.o say.o pbx.o cli.o md5.o term.o \
	ulaw.o alaw.o callerid.o fskmodem.o image.o app.o \
	cdr.o tdd.o acl.o rtp.o manager.o asterisk.o \
	dsp.o chanvars.o indications.o autoservice.o db.o privacy.o \
	astmm.o enum.o srv.o dns.o aescrypt.o aestab.o aeskey.o \
	utils.o config_old.o plc.o jitterbuf.o dnsmgr.o devicestate.o
ifeq (${OSARCH},Darwin)
OBJS+=poll.o dlfcn.o
ASTLINK=-Wl,-dynamic
SOLINK=-dynamic -bundle -undefined suppress -force_flat_namespace
else
ASTLINK=-Wl,-E 
SOLINK=-shared -Xlinker -x
endif
ifeq (${OSARCH},SunOS)
OBJS+=strcompat.o
ASTLINK=
SOLINK=-shared -fpic -L$(CROSS_COMPILE_TARGET)/usr/local/ssl/lib
endif

INSTALL=install

_all: all
	@echo " +--------- Asterisk Build Complete ---------+"  
	@echo " + Asterisk has successfully been built, but +"  
	@echo " + cannot be run before being installed by   +"  
	@echo " + running:                                  +"  
	@echo " +                                           +"
	@echo " +               $(MAKE) install                +"  
	@echo " +-------------------------------------------+"  

all: cleantest depend asterisk subdirs 

#ifneq ($(wildcard tags),)
ctags: tags
#endif

ifneq ($(wildcard TAGS),)
all: TAGS
endif

noclean: depend asterisk subdirs

editline/config.h:
	cd editline && unset CFLAGS LIBS && ./configure ; \

editline/libedit.a: FORCE
	cd editline && unset CFLAGS LIBS && test -f config.h || ./configure
	$(MAKE) -C editline libedit.a

db1-ast/libdb1.a: FORCE
	@if [ -d db1-ast ]; then \
		$(MAKE) -C db1-ast libdb1.a ; \
	else \
		echo "You need to do a cvs update -d not just cvs update"; \
		exit 1; \
	fi

ifneq ($(wildcard .depend),)
include .depend
endif

ifneq ($(wildcard .tags-depend),)
include .tags-depend
endif

.PHONY: ast_expr

build_tools/vercomp: build_tools/vercomp.c
	$(HOST_CC) -o $@ $<

ast_expr: build_tools/vercomp
	$(MAKE) ast_expr.a

ifeq ($(MAKECMDGOALS),ast_expr.a)
FLEXVER_GT_2_5_31=$(shell build_tools/vercomp flex \>= 2.5.31)
BISONVER_GE_1_85=$(shell build_tools/vercomp bison \>= 1.85 )
endif

ifeq ($(FLEXVER_GT_2_5_31),true)
FLEXOBJS=ast_expr2.o ast_expr2f.o
else
FLEXOBJS=ast_expr.o
endif

ast_expr.o:: ast_expr.c
	@echo "================================================================================="
	@echo "NOTE: Using older version of expression parser. To use the newer version,"
	@echo "NOTE: upgrade to flex 2.5.31 or higher, which can be found at"
	@echo "NOTE: http://sourceforge.net/project/showfiles.php?group_id=72099"
	@echo "================================================================================="

ast_expr.o:: ast_expr.c

ifeq ($(BISONVER_GE_1_85),false)
.y.c:
	@echo "================================================================================="
	@echo "NOTE: You may have trouble if you do not have bison-1.85 or higher installed!"
	@echo "NOTE: You can pick up a copy at: http://ftp.gnu.org or its mirrors"
	@echo "NOTE: You have:"
	@bison --version
	@echo "================================================================================"
	bison -v -d --name-prefix=ast_yy $< -o $@
else
.y.c:
	bison -v -d --name-prefix=ast_yy $< -o $@
endif

ast_expr2f.c: ast_expr2.fl
	flex ast_expr2.fl

ast_expr.a: $(FLEXOBJS)
	@rm -f $@
	ar r $@ $(FLEXOBJS)
	ranlib $@

testexpr2 :
	flex ast_expr2.fl
	bison -v -d --name-prefix=ast_yy -o ast_expr2.c ast_expr2.y
	gcc -g -c -DSTANDALONE ast_expr2f.c
	gcc -g -c -DSTANDALONE ast_expr2.c
	gcc -g -o testexpr2 ast_expr2f.o ast_expr2.o
	rm ast_expr2.c ast_expr2.o ast_expr2f.o ast_expr2f.c

manpage: asterisk.8.gz

asterisk.8.gz: asterisk.sgml
	rm -f asterisk.8
	docbook2man asterisk.sgml
	mv ./*.8 asterisk.8
	gzip asterisk.8

asterisk.pdf: asterisk.sgml
	docbook2pdf asterisk.sgml

asterisk.ps: asterisk.sgml
	docbook2ps asterisk.sgml

asterisk.html: asterisk.sgml
	docbook2html asterisk.sgml
	mv r1.html asterisk.html

asterisk.txt: asterisk.sgml
	docbook2txt asterisk.sgml

defaults.h: FORCE
	build_tools/make_defaults_h > $@.tmp
	if cmp -s $@.tmp $@ ; then echo ; else \
		mv $@.tmp $@ ; \
	fi
	rm -f $@.tmp


include/asterisk/build.h:
	build_tools/make_build_h > $@.tmp
	if cmp -s $@.tmp $@ ; then echo ; else \
		mv $@.tmp $@ ; \
	fi
	rm -f $@.tmp

# only force 'build.h' to be made for a non-'install' run
ifeq ($(findstring install,$(MAKECMDGOALS)),)
include/asterisk/build.h: FORCE
endif

include/asterisk/version.h: FORCE
	build_tools/make_version_h > $@.tmp
	if cmp -s $@.tmp $@ ; then echo; else \
		mv $@.tmp $@ ; \
	fi
	rm -f $@.tmp

stdtime/libtime.a: FORCE
	@if [ -d stdtime ]; then \
		$(MAKE) -C stdtime libtime.a ; \
	else \
		echo "You need to do a cvs update -d not just cvs update"; \
		exit 1; \
	fi

asterisk: editline/libedit.a db1-ast/libdb1.a stdtime/libtime.a $(OBJS) ast_expr
	$(CC) $(DEBUG) -o asterisk $(ASTLINK) $(OBJS) ast_expr.a $(LIBEDIT) db1-ast/libdb1.a stdtime/libtime.a $(LIBS)

muted: muted.o
	$(CC) $(AUDIO_LIBS) -o muted muted.o

subdirs: 
	for x in $(SUBDIRS); do $(MAKE) -C $$x || exit 1 ; done

clean:
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean || exit 1 ; done
	rm -f *.o *.so asterisk .depend
	rm -f defaults.h
	rm -f include/asterisk/build.h
	rm -f include/asterisk/version.h
	rm -f ast_expr.c ast_expr.h ast_expr.output
	rm -f ast_expr2.c ast_expr2f.c ast_expr2.h ast_expr2.output
	rm -f ast_expr.a build_tools/vercomp
	rm -f .version
	rm -f .tags-depend .tags-sources tags TAGS
	@if [ -f editline/Makefile ]; then $(MAKE) -C editline distclean ; fi
	@if [ -d mpg123-0.59r ]; then $(MAKE) -C mpg123-0.59r clean; fi
	$(MAKE) -C db1-ast clean
	$(MAKE) -C stdtime clean

datafiles: all
	sh mkpkgconfig $(DESTDIR)/usr/lib/pkgconfig
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds/digits
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds/priv-callerintros
	for x in sounds/digits/*.gsm; do \
		if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
			install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/sounds/digits ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds/dictate
	for x in sounds/dictate/*.gsm; do \
		if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
			install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/sounds/dictate ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds/letters
	for x in sounds/letters/*.gsm; do \
		if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
			install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/sounds/letters ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds/phonetic
	for x in sounds/phonetic/*.gsm; do \
		if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
			install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/sounds/phonetic ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	for x in sounds/demo-* sounds/vm-* sounds/transfer* sounds/pbx-* sounds/ss-* sounds/beep* sounds/dir-* sounds/conf-* sounds/agent-* sounds/invalid* sounds/tt-* sounds/auth-* sounds/privacy-* sounds/queue-*; do \
		if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
			install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/sounds ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/mohmp3
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/images
	for x in images/*.jpg; do \
		install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/images ; \
	done
	mkdir -p $(DESTDIR)$(AGI_DIR)

update: 
	@if [ -d CVS ]; then \
		if [ -f patches/.applied ]; then \
			patches=`cat patches/.applied`; \
		fi; \
		if [ ! -z "$$patches" ]; then \
			for x in $$patches; do \
				echo "Unapplying $$x..."; \
				patch -R -p0 < patches/$$x; \
			done; \
			rm -f patches/.applied; \
		fi ; \
		echo "Updating from CVS..." ; \
		cvs -q -z3 update -Pd | tee update.out; \
		rm -f .version; \
		if [ `grep -c ^C update.out` -gt 0 ]; then \
			echo ; echo "The following files have conflicts:" ; \
			grep ^C update.out | cut -d' ' -f2- ; \
		fi ; \
		rm -f update.out; \
		if [ ! -z "$$patches" ]; then \
			for x in $$patches; do \
				if [ -f patches/$$x ]; then \
					echo "Applying patch $$x..."; \
					patch -p0 < patches/$$x; \
					echo $$x >> patches/.applied; \
				else \
					echo "Patch $$x no longer relevant"; \
				fi; \
			done; \
		fi; \
	else \
		echo "Not CVS";  \
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
	install -m 755 asterisk $(DESTDIR)$(ASTSBINDIR)/
	install -m 755 contrib/scripts/astgenkey $(DESTDIR)$(ASTSBINDIR)/
	install -m 755 contrib/scripts/autosupport $(DESTDIR)$(ASTSBINDIR)/	
	if [ ! -f $(DESTDIR)$(ASTSBINDIR)/safe_asterisk ]; then \
		install -m 755 contrib/scripts/safe_asterisk $(DESTDIR)$(ASTSBINDIR)/ ;\
	fi
	for x in $(SUBDIRS); do $(MAKE) -C $$x install || exit 1 ; done
	install -d $(DESTDIR)$(ASTHEADERDIR)
	install -m 644 include/asterisk/*.h $(DESTDIR)$(ASTHEADERDIR)
	if [ -n "$(OLDHEADERS)" ]; then \
		rm -f $(addprefix $(DESTDIR)$(ASTHEADERDIR)/,$(OLDHEADERS)) ;\
	fi
	rm -f $(DESTDIR)$(ASTVARLIBDIR)/sounds/voicemail
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds
	mkdir -p $(DESTDIR)$(ASTLOGDIR)/cdr-csv
	mkdir -p $(DESTDIR)$(ASTLOGDIR)/cdr-custom
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/keys
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/firmware
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/firmware/iax
	mkdir -p $(DESTDIR)$(ASTMANDIR)/man8
	install -m 644 keys/iaxtel.pub $(DESTDIR)$(ASTVARLIBDIR)/keys
	install -m 644 keys/freeworlddialup.pub $(DESTDIR)$(ASTVARLIBDIR)/keys
	install -m 644 asterisk.8.gz $(DESTDIR)$(ASTMANDIR)/man8
	install -m 644 contrib/scripts/astgenkey.8 $(DESTDIR)$(ASTMANDIR)/man8
	install -m 644 contrib/scripts/autosupport.8 $(DESTDIR)$(ASTMANDIR)/man8
	install -m 644 contrib/scripts/safe_asterisk.8 $(DESTDIR)$(ASTMANDIR)/man8
	if [ -d contrib/firmware/iax ]; then \
		install -m 644 contrib/firmware/iax/iaxy.bin $(DESTDIR)$(ASTVARLIBDIR)/firmware/iax/iaxy.bin; \
	else \
		echo "You need to do cvs update -d not just cvs update" ; \
	fi 
	( cd $(DESTDIR)$(ASTVARLIBDIR)/sounds  ; ln -s $(ASTSPOOLDIR)/voicemail . )
	if [ -f mpg123-0.59r/mpg123 ]; then $(MAKE) -C mpg123-0.59r install; fi
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

install: all datafiles bininstall

upgrade: all bininstall

adsi: all
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.adsi; do \
		if [ ! -f $(DESTDIR)$(ASTETCDIRX)/$$x ]; then \
			install -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`basename $$x` ; \
		fi ; \
	done

samples: all datafiles adsi
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.sample; do \
		if [ -f $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` ]; then \
			if [ "$(OVERWRITE)" = "y" ]; then \
				if cmp -s $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` $$x ; then \
					echo "Config file $$x is unchanged"; \
					continue; \
				fi ; \
				mv -f $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample`.old ; \
			else \
				echo "Skipping config file $$x"; \
				continue; \
			fi ;\
		fi ; \
		install -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` ;\
	done
	if [ "$(OVERWRITE)" = "y" ] || [ ! -f $(DESTDIR)$(ASTETCDIR)/asterisk.conf ]; then \
		echo "[directories]" > $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo "astetcdir => $(ASTETCDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo "astmoddir => $(MODULES_DIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo "astvarlibdir => $(ASTVARLIBDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo "astagidir => $(AGI_DIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo "astspooldir => $(ASTSPOOLDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo "astrundir => $(ASTVARRUNDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo "astlogdir => $(ASTLOGDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo "" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo "; Changing the following lines may compromise your security." >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo ";[files]" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo ";astctlpermissions = 0660" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo ";astctlowner = root" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo ";astctlgroup = apache" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
		echo ";astctl = asterisk.ctl" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf ; \
	else \
		echo "Skipping asterisk.conf creation"; \
	fi
	for x in sounds/demo-*; do \
		if $(GREP) -q "^%`basename $$x`%" sounds.txt; then \
			install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/sounds ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	for x in sounds/*.mp3; do \
		install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/mohmp3 ; \
	done
	rm -f $(DESTDIR)$(ASTVARLIBDIR)/mohmp3/sample-hold.mp3
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/INBOX
	:> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/unavail.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isunavail; do \
		cat $(DESTDIR)$(ASTVARLIBDIR)/sounds/$$x.gsm >> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/unavail.gsm ; \
	done
	:> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/busy.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isonphone; do \
		cat $(DESTDIR)$(ASTVARLIBDIR)/sounds/$$x.gsm >> $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default/1234/busy.gsm ; \
	done

webvmail:
	@[ -d $(DESTDIR)$(HTTP_DOCSDIR)/ ] || ( printf "http docs directory not found.\nUpdate assignment of variable HTTP_DOCSDIR in Makefile!\n" && exit 1 )
	@[ -d $(DESTDIR)$(HTTP_CGIDIR) ] || ( printf "cgi-bin directory not found.\nUpdate assignment of variable HTTP_CGIDIR in Makefile!\n" && exit 1 )
	install -m 4755 -o root -g root contrib/scripts/vmail.cgi $(DESTDIR)$(HTTP_CGIDIR)/vmail.cgi
	mkdir -p $(DESTDIR)$(HTTP_DOCSDIR)/_asterisk
	for x in images/*.gif; do \
		install -m 644 $$x $(DESTDIR)$(HTTP_DOCSDIR)/_asterisk/; \
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

mailbox:
	./contrib/scripts/addmailbox 

spec: 
	sed "s/^Version:.*/Version: $(RPMVERSION)/g" redhat/asterisk.spec > asterisk.spec ; \

rpm: __rpm

__rpm: include/asterisk/version.h spec
	rm -rf /tmp/asterisk ; \
	mkdir -p /tmp/asterisk/redhat/RPMS/i386 ; \
	$(MAKE) DESTDIR=/tmp/asterisk install ; \
	$(MAKE) DESTDIR=/tmp/asterisk samples ; \
	mkdir -p /tmp/asterisk/etc/rc.d/init.d ; \
	cp -f redhat/asterisk /tmp/asterisk/etc/rc.d/init.d/ ; \
	rpmbuild --rcfile /usr/lib/rpm/rpmrc:redhat/rpmrc -bb asterisk.spec

progdocs:
	doxygen contrib/asterisk-ng-doxygen

mpg123:
	@wget -V >/dev/null || (echo "You need wget" ; false )
	[ -f mpg123-0.59r.tar.gz ] || wget http://www.mpg123.de/mpg123/mpg123-0.59r.tar.gz
	[ -d mpg123-0.59r ] || tar xfz mpg123-0.59r.tar.gz
	$(MAKE) -C mpg123-0.59r $(MPG123TARG)

config:
	if [ -d /etc/rc.d/init.d ]; then \
		install -m 755 contrib/init.d/rc.redhat.asterisk /etc/rc.d/init.d/asterisk; \
		/sbin/chkconfig --add asterisk; \
	elif [ -d /etc/init.d ]; then \
		install -m 755 init.asterisk /etc/init.d/asterisk; \
	fi 

dont-optimize:
	$(MAKE) OPTIMIZE= K6OPT= install

valgrind: dont-optimize

depend: .depend defaults.h include/asterisk/build.h include/asterisk/version.h
	for x in $(SUBDIRS); do $(MAKE) -C $$x depend || exit 1 ; done

.depend: include/asterisk/version.h
	build_tools/mkdep ${CFLAGS} $(filter-out ast_expr.c,$(wildcard *.c))
	build_tools/mkdep -a -d ${CFLAGS} ast_expr.c

.tags-depend:
	@echo -n ".tags-depend: " > $@
	@find . -maxdepth 1 -name \*.c -printf "\t%p \\\\\n" >> $@
	@find . -maxdepth 1 -name \*.h -printf "\t%p \\\\\n" >> $@
	@find ${SUBDIRS} -name \*.c -printf "\t%p \\\\\n" >> $@
	@find ${SUBDIRS} -name \*.h -printf "\t%p \\\\\n" >> $@
	@find include -name \*.h -printf "\t%p \\\\\n" >> $@
	@echo >> $@

.tags-sources:
	@rm -f $@
	@find . -maxdepth 1 -name \*.c -print >> $@
	@find . -maxdepth 1 -name \*.h -print >> $@
	@find ${SUBDIRS} -name \*.c -print >> $@
	@find ${SUBDIRS} -name \*.h -print >> $@
	@find include -name \*.h -print >> $@

tags: .tags-depend .tags-sources
	ctags -L .tags-sources -o $@

ctags: tags

TAGS: .tags-depend .tags-sources
	etags -o $@ `cat .tags-sources`

etags: TAGS

FORCE:

%_env:
	$(MAKE) -C $(shell echo $@ | sed "s/_env//g") env

env:
	env

# If the cleancount has been changed, force a make clean.
# .cleancount is the global clean count, and .lastclean is the 
# last clean count we had
# We can avoid this by making noclean

cleantest:
	if cmp -s .cleancount .lastclean ; then echo ; else \
		$(MAKE) clean; cp -f .cleancount .lastclean;\
	fi

patchlist:
	@echo "Experimental Patches:"
	@for x in patches/*; do \
		patch=`basename $$x`; \
		if [ "$$patch" = "CVS" ]; then \
			continue; \
		fi; \
		if grep -q ^$$patch$$ patches/.applied; then \
			echo "$$patch (applied)"; \
		else \
			echo "$$patch (available)"; \
		fi; \
	done

apply: 
	@if [ -z "$(PATCH)" ]; then \
		echo "Usage: make PATCH=<patchname> apply"; \
	elif grep -q ^$(PATCH)$$ patches/.applied 2>/dev/null; then \
		echo "Patch $(PATCH) is already applied"; \
	elif [ -f "patches/$(PATCH)" ]; then \
		echo "Applying patch $(PATCH)"; \
		patch -p0 < patches/$(PATCH); \
		echo "$(PATCH)" >> patches/.applied; \
	else \
		echo "No such patch $(PATCH) in patches directory"; \
	fi

unapply: 
	@if [ -z "$(PATCH)" ]; then \
		echo "Usage: make PATCH=<patchname> unapply"; \
	elif grep -v -q ^$(PATCH)$$ patches/.applied 2>/dev/null; then \
		echo "Patch $(PATCH) is not applied"; \
	elif [ -f "patches/$(PATCH)" ]; then \
		echo "Un-applying patch $(PATCH)"; \
		patch -p0 -R < patches/$(PATCH); \
		rm -f patches/.tmpapplied || :; \
		mv patches/.applied patches/.tmpapplied; \
		cat patches/.tmpapplied | grep -v ^$(PATCH)$$ > patches/.applied; \
		rm -f patches/.tmpapplied; \
	else \
		echo "No such patch $(PATCH) in patches directory"; \
	fi
