#
# Asterisk -- A telephony toolkit for Linux.
# 
# Top level Makefile
#
# Copyright (C) 1999, Mark Spencer
#
# Mark Spencer <markster@linux-support.net>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

.EXPORT_ALL_VARIABLES:

# Create OPTIONS variable
OPTIONS=

OSARCH=$(shell uname -s)

ifeq (${OSARCH},Linux)
PROC=$(shell uname -m)
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

endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
PROC=$(shell uname -m)
endif

# Pentium Pro Optimize
#PROC=i686

# Pentium & VIA processors optimize
#PROC=i586

#PROC=k6
#PROC=ppc

PWD=$(shell pwd)

######### More GSM codec optimization
######### Uncomment to enable MMXTM optimizations for x86 architecture CPU's
######### which support MMX instructions.  This should be newer pentiums,
######### ppro's, etc, as well as the AMD K6 and K7.  
#K6OPT  = -DK6OPT

#Tell gcc to optimize the asterisk's code
OPTIMIZE+=-O6

#Include debug symbols in the executables (-g) and profiling info (-pg)
DEBUG=-g #-pg

# If you are running a radio application, define RADIO_RELAX so that the DTMF
# will be received more reliably
#OPTIONS += -DRADIO_RELAX

# If you don't have a lot of memory (e.g. embedded Asterisk), uncomment the
# following to reduce the size of certain static buffers
#OPTIONS += -DLOW_MEMORY

# Optional debugging parameters
DEBUG_THREADS = #-DDEBUG_THREADS #-DDO_CRASH 

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

INCLUDE=-Iinclude -I../include
CFLAGS=-pipe  -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations $(DEBUG) $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE #-DMAKE_VALGRIND_HAPPY
CFLAGS+=$(OPTIMIZE)

ifneq ($(PROC),ultrasparc)
CFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
endif

CFLAGS+=$(shell if uname -m | grep -q ppc; then echo "-fsigned-char"; fi)
CFLAGS+=$(shell if [ -f /usr/include/osp/osp.h ]; then echo "-DOSP_SUPPORT -I/usr/include/osp" ; fi)

ifeq (${OSARCH},FreeBSD)
OSVERSION=$(shell make -V OSVERSION -f /usr/share/mk/bsd.port.subdir.mk)
CFLAGS+=$(shell if test ${OSVERSION} -lt 500016 ; then echo "-D_THREAD_SAFE"; fi)
LIBS+=$(shell if test  ${OSVERSION} -lt 502102 ; then echo "-lc_r"; else echo "-pthread"; fi)
INCLUDE+=-I/usr/local/include
CFLAGS+=$(shell if [ -d /usr/local/include/spandsp ]; then echo "-I/usr/local/include/spandsp"; fi)
endif # FreeBSD

ifeq (${OSARCH},NetBSD)
CFLAGS+=-pthread
INCLUDE+=-I/usr/local/include -I/usr/pkg/include
endif

ifeq (${OSARCH},OpenBSD)
CFLAGS+=-pthread
endif

#Uncomment this to use the older DSP routines
#CFLAGS+=-DOLD_DSP_ROUTINES

CFLAGS+=$(shell if [ -f /usr/include/linux/zaptel.h ]; then echo "-DZAPTEL_OPTIMIZATIONS"; fi)
CFLAGS+=$(shell if [ -f /usr/local/include/zaptel.h ]; then echo "-DZAPTEL_OPTIMIZATIONS"; fi)

LIBEDIT=editline/libedit.a

ASTERISKVERSION=$(shell if [ -f .version ]; then cat .version; else if [ -d CVS ]; then if [ -f CVS/Tag ] ; then echo "CVS-`sed 's/^T//g' CVS/Tag`-`date +"%D-%T"`"; else echo "CVS-HEAD-`date +"%D-%T"`"; fi; fi; fi)
ASTERISKVERSIONNUM=$(shell if [ -d CVS ]; then echo 999999 ; else if [ -f .version ] ; then awk -F. '{printf "%02d%02d%02d", $$1, $$2, $$3}' .version ; else echo 000000 ; fi ; fi)
HTTPDIR=$(shell if [ -d /var/www ]; then echo "/var/www"; else echo "/home/httpd"; fi)
RPMVERSION=$(shell if [ -f .version ]; then sed 's/[-\/:]/_/g' .version; else echo "unknown" ; fi)
CFLAGS+=-DASTERISK_VERSION=\"$(ASTERISKVERSION)\"
CFLAGS+=-DASTERISK_VERSION_NUM=$(ASTERISKVERSIONNUM)
CFLAGS+=-DINSTALL_PREFIX=\"$(INSTALL_PREFIX)\"
CFLAGS+=-DASTETCDIR=\"$(ASTETCDIR)\"
CFLAGS+=-DASTLIBDIR=\"$(ASTLIBDIR)\"
CFLAGS+=-DASTVARLIBDIR=\"$(ASTVARLIBDIR)\"
CFLAGS+=-DASTVARRUNDIR=\"$(ASTVARRUNDIR)\"
CFLAGS+=-DASTSPOOLDIR=\"$(ASTSPOOLDIR)\"
CFLAGS+=-DASTLOGDIR=\"$(ASTLOGDIR)\"
CFLAGS+=-DASTCONFPATH=\"$(ASTCONFPATH)\"
CFLAGS+=-DASTMODDIR=\"$(MODULES_DIR)\"
CFLAGS+=-DASTAGIDIR=\"$(AGI_DIR)\"

CFLAGS+= $(DEBUG_THREADS)
CFLAGS+= $(TRACE_FRAMES)
CFLAGS+= $(MALLOC_DEBUG)
CFLAGS+= $(BUSYDETECT)
CFLAGS+= $(OPTIONS)
CFLAGS+=# -fomit-frame-pointer 
SUBDIRS=res channels pbx apps codecs formats agi cdr astman stdtime
ifeq (${OSARCH},Linux)
LIBS=-ldl -lpthread
endif
LIBS+=-lncurses -lm
ifeq (${OSARCH},Linux)
LIBS+=-lresolv  #-lnjamd
endif
ifeq (${OSARCH},Darwin)
LIBS+=-lresolv
endif
ifeq (${OSARCH},FreeBSD)
LIBS+=-lcrypto
endif
ifeq (${OSARCH},NetBSD)
LIBS+=-lpthread -lcrypto -lm -L/usr/local/lib -L/usr/pkg/lib -lncurses
endif
ifeq (${OSARCH},OpenBSD)
LIBS=-lcrypto -lpthread -lm -lncurses
endif
LIBS+=-lssl
OBJS=io.o sched.o logger.o frame.o loader.o config.o channel.o \
	translate.o file.o say.o pbx.o cli.o md5.o term.o \
	ulaw.o alaw.o callerid.o fskmodem.o image.o app.o \
	cdr.o tdd.o acl.o rtp.o manager.o asterisk.o ast_expr.o \
	dsp.o chanvars.o indications.o autoservice.o db.o privacy.o \
	astmm.o enum.o srv.o dns.o aescrypt.o aestab.o aeskey.o \
	utils.o 
ifeq (${OSARCH},Darwin)
OBJS+=poll.o dlfcn.o
ASTLINK=-Wl,-dynamic
SOLINK=-dynamic -bundle -undefined suppress -force_flat_namespace
else
ASTLINK=-Wl,-E 
SOLINK=-shared -Xlinker -x
endif

CC=gcc
INSTALL=install

_all: all
	@echo " +--------- Asterisk Build Complete ---------+"  
	@echo " + Asterisk has successfully been built, but +"  
	@echo " + cannot be run before being installed by   +"  
	@echo " + running:                                  +"  
	@echo " +                                           +"
	@echo " +               $(MAKE) install                +"  
	@echo " +-------------------------------------------+"  

all: depend asterisk subdirs 

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

.PHONY: _version

_version: 
	if [ -d CVS ] && ! [ -f .version ]; then echo $(ASTERISKVERSION) > .version; fi 

.version: _version

.y.c:
	bison $< --name-prefix=ast_yy -o $@

ast_expr.o: ast_expr.c

cli.o: cli.c build.h

asterisk.o: asterisk.c build.h

manpage: asterisk.8.gz

asterisk.8.gz: asterisk.sgml
	rm -f asterisk.8
	docbook2man asterisk.sgml
	mv ./*.8 asterisk.8
	gzip asterisk.8

ifneq ($(strip $(ASTERISKVERSION)),)
build.h: .version
	./make_build_h
else
build.h:
	./make_build_h
endif

stdtime/libtime.a: FORCE
	@if [ -d stdtime ]; then \
		$(MAKE) -C stdtime libtime.a ; \
	else \
		echo "You need to do a cvs update -d not just cvs update"; \
		exit 1; \
	fi

asterisk: editline/libedit.a db1-ast/libdb1.a stdtime/libtime.a $(OBJS)
	$(CC) $(DEBUG) -o asterisk $(ASTLINK) $(OBJS) $(LIBEDIT) db1-ast/libdb1.a stdtime/libtime.a $(LIBS)

muted: muted.o
	$(CC) -o muted muted.o

subdirs: 
	for x in $(SUBDIRS); do $(MAKE) -C $$x || exit 1 ; done

clean:
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean || exit 1 ; done
	rm -f *.o *.so asterisk .depend
	rm -f build.h 
	rm -f ast_expr.c
	@if [ -e editline/Makefile ]; then $(MAKE) -C editline distclean ; fi
	@if [ -d mpg123-0.59r ]; then make -C mpg123-0.59r clean; fi
	$(MAKE) -C db1-ast clean
	$(MAKE) -C stdtime clean

datafiles: all
	sh mkpkgconfig
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds/digits
	for x in sounds/digits/*.gsm; do \
		if grep -q "^%`basename $$x`%" sounds.txt; then \
			install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/sounds/digits ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds/letters
	for x in sounds/letters/*.gsm; do \
		if grep -q "^%`basename $$x`%" sounds.txt; then \
			install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/sounds/letters ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds/phonetic
	for x in sounds/phonetic/*.gsm; do \
		if grep -q "^%`basename $$x`%" sounds.txt; then \
			install -m 644 $$x $(DESTDIR)$(ASTVARLIBDIR)/sounds/phonetic ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	for x in sounds/vm-* sounds/transfer* sounds/pbx-* sounds/ss-* sounds/beep* sounds/dir-* sounds/conf-* sounds/agent-* sounds/invalid* sounds/tt-* sounds/auth-* sounds/privacy-* sounds/queue-*; do \
		if grep -q "^%`basename $$x`%" sounds.txt; then \
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
		echo "Updating from CVS..." ; \
		cvs -q -z3 update -Pd; \
		rm -f .version; \
	else \
		echo "Not CVS";  \
	fi

bininstall: all
	mkdir -p $(DESTDIR)$(MODULES_DIR)
	mkdir -p $(DESTDIR)$(ASTSBINDIR)
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	mkdir -p $(DESTDIR)$(ASTBINDIR)
	mkdir -p $(DESTDIR)$(ASTSBINDIR)
	mkdir -p $(DESTDIR)$(ASTVARRUNDIR)
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/voicemail
	mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/tmp
	install -m 755 asterisk $(DESTDIR)$(ASTSBINDIR)/
	install -m 755 contrib/scripts/astgenkey $(DESTDIR)$(ASTSBINDIR)/
	if [ ! -f $(DESTDIR)$(ASTSBINDIR)/safe_asterisk ]; then \
		install -m 755 contrib/scripts/safe_asterisk $(DESTDIR)$(ASTSBINDIR)/ ;\
	fi
	for x in $(SUBDIRS); do $(MAKE) -C $$x install || exit 1 ; done
	install -d $(DESTDIR)$(ASTHEADERDIR)
	install -m 644 include/asterisk/*.h $(DESTDIR)$(ASTHEADERDIR)
	rm -f $(DESTDIR)$(ASTVARLIBDIR)/sounds/vm
	rm -f $(DESTDIR)$(ASTVARLIBDIR)/sounds/voicemail
	if [ ! -h $(DESTDIR)$(ASTSPOOLDIR)/vm ] && [ -d $(DESTDIR)$(ASTSPOOLDIR)/vm ]; then \
		mv $(DESTDIR)$(ASTSPOOLDIR)/vm $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default; \
	else \
		mkdir -p $(DESTDIR)$(ASTSPOOLDIR)/voicemail/default; \
		rm -f $(DESTDIR)$(ASTSPOOLDIR)/vm; \
	fi
	ln -s $(ASTSPOOLDIR)/voicemail/default $(DESTDIR)$(ASTSPOOLDIR)/vm
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/sounds
	mkdir -p $(DESTDIR)$(ASTLOGDIR)/cdr-csv
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/keys
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/firmware
	mkdir -p $(DESTDIR)$(ASTVARLIBDIR)/firmware/iax
	mkdir -p $(DESTDIR)$(ASTMANDIR)/man8
	install -m 644 keys/iaxtel.pub $(DESTDIR)$(ASTVARLIBDIR)/keys
	install -m 644 keys/freeworlddialup.pub $(DESTDIR)$(ASTVARLIBDIR)/keys
	install -m 644 asterisk.8.gz $(DESTDIR)$(ASTMANDIR)/man8
	if [ -d contrib/firmware/iax ]; then \
		install -m 644 contrib/firmware/iax/iaxy.bin $(DESTDIR)$(ASTVARLIBDIR)/firmware/iax/iaxy.bin; \
	else \
		echo "You need to do cvs update -d not just cvs update" ; \
	fi 
	( cd $(DESTDIR)$(ASTVARLIBDIR)/sounds  ; ln -s $(ASTSPOOLDIR)/vm . )
	( cd $(DESTDIR)$(ASTVARLIBDIR)/sounds  ; ln -s $(ASTSPOOLDIR)/voicemail . )
	if [ -f mpg123-0.59r/mpg123 ]; then make -C mpg123-0.59r install; fi
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

install: all datafiles bininstall

upgrade: all bininstall

adsi: all
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.adsi; do \
		if ! [ -f $(DESTDIR)$(ASTETCDIRX)/$$x ]; then \
			install -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`basename $$x` ; \
		fi ; \
	done

samples: all datafiles adsi
	mkdir -p $(DESTDIR)$(ASTETCDIR)
	for x in configs/*.sample; do \
		if [ -f $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` ]; then \
			mv -f $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample`.old ; \
		fi ; \
		install -m 644 $$x $(DESTDIR)$(ASTETCDIR)/`basename $$x .sample` ;\
	done
	echo "[directories]" > $(DESTDIR)$(ASTETCDIR)/asterisk.conf
	echo "astetcdir => $(ASTETCDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf
	echo "astmoddir => $(MODULES_DIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf
	echo "astvarlibdir => $(ASTVARLIBDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf
	echo "astagidir => $(AGI_DIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf
	echo "astspooldir => $(ASTSPOOLDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf
	echo "astrundir => $(ASTVARRUNDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf
	echo "astlogdir => $(ASTLOGDIR)" >> $(DESTDIR)$(ASTETCDIR)/asterisk.conf
	for x in sounds/demo-*; do \
		if grep -q "^%`basename $$x`%" sounds.txt; then \
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
	@[ -d $(DESTDIR)$(HTTPDIR) ] || ( echo "No HTTP directory" && exit 1 )
	@[ -d $(DESTDIR)$(HTTPDIR)/html ] || ( echo "No http directory" && exit 1 )
	@[ -d $(DESTDIR)$(HTTPDIR)/cgi-bin ] || ( echo "No cgi-bin directory" && exit 1 )
	install -m 4755 -o root -g root contrib/scripts/vmail.cgi $(DESTDIR)$(HTTPDIR)/cgi-bin/vmail.cgi
	mkdir -p $(DESTDIR)$(HTTPDIR)/html/_asterisk
	for x in images/*.gif; do \
		install -m 644 $$x $(DESTDIR)$(HTTPDIR)/html/_asterisk/; \
	done
	@echo " +--------- Asterisk Web Voicemail ----------+"  
	@echo " +                                           +"
	@echo " + Asterisk Web Voicemail is installed in    +"
	@echo " + your cgi-bin directory.  IT USES A SETUID +"
	@echo " + ROOT PERL SCRIPT, SO IF YOU DON'T LIKE    +"
	@echo " + THAT, UNINSTALL IT!                       +"
	@echo " +                                           +"
	@echo " +-------------------------------------------+"  

mailbox:
	./contrib/scripts/addmailbox 

rpm: __rpm

__rpm: _version
	rm -rf /tmp/asterisk ; \
	mkdir -p /tmp/asterisk/redhat/RPMS/i386 ; \
	$(MAKE) DESTDIR=/tmp/asterisk install ; \
	$(MAKE) DESTDIR=/tmp/asterisk samples ; \
	mkdir -p /tmp/asterisk/etc/rc.d/init.d ; \
	cp -f redhat/asterisk /tmp/asterisk/etc/rc.d/init.d/ ; \
	sed "s/^Version:.*/Version: $(RPMVERSION)/g" redhat/asterisk.spec > asterisk.spec ; \
	rpmbuild --rcfile /usr/lib/rpm/rpmrc:redhat/rpmrc -bb asterisk.spec

progdocs:
	doxygen contrib/asterisk-ng-doxygen

mpg123:
	@wget -V >/dev/null || (echo "You need wget" ; false )
	[ -f mpg123-0.59r.tar.gz ] || wget http://www.mpg123.de/mpg123/mpg123-0.59r.tar.gz
	[ -d mpg123-0.59r ] || tar xfz mpg123-0.59r.tar.gz
	make -C mpg123-0.59r linux

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

depend: .depend
	for x in $(SUBDIRS); do $(MAKE) -C $$x depend || exit 1 ; done

.depend:
	./mkdep ${CFLAGS} `ls *.c`

FORCE:

%_env:
	make -C $(shell echo $@ | sed "s/_env//g") env

env:
	env
