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

# Pentium Pro Optimize
#PROC=i686
# Pentium Optimize
#PROC=i586
#PROC=k6
#PROC=ppc
PROC=$(shell uname -m)

######### More GSM codec optimization
######### Uncomment to enable MMXTM optimizations for x86 architecture CPU's
######### which support MMX instructions.  This should be newer pentiums,
######### ppro's, etc, as well as the AMD K6 and K7.  
K6OPT  = #-DK6OPT

#Tell gcc to optimize the asterisk's code
OPTIMIZE=-O6

#Include debug symbols in the executables (-g) and profiling info (-pg)
DEBUG=-g #-pg

# Optional debugging parameters
DEBUG_THREADS = #-DDO_CRASH -DDEBUG_THREADS

# Uncomment next one to enable ast_frame tracing (for debugging)
TRACE_FRAMES = #-DTRACE_FRAMES

# Where to install asterisk after compiling
# Default -> leave empty
INSTALL_PREFIX=

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


MODULES_DIR=$(ASTLIBDIR)/modules
AGI_DIR=$(ASTVARLIBDIR)/agi-bin

INCLUDE=-Iinclude -I../include
CFLAGS=-pipe  -Wall -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations $(DEBUG) $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE #-DMAKE_VALGRIND_HAPPY
CFLAGS+=$(OPTIMIZE)
CFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
CFLAGS+=$(shell if uname -m | grep -q ppc; then echo "-fsigned-char"; fi)

LIBEDIT=editline/libedit.a

ASTERISKVERSION=$(shell if [ -f .version ]; then cat .version; fi)
HTTPDIR=$(shell if [ -d /var/www ]; then echo "/var/www"; else echo "/home/httpd"; fi)
RPMVERSION=$(shell if [ -f .version ]; then sed 's/[-\/:]/_/g' .version; else echo "unknown" ; fi)
CFLAGS+=-DASTERISK_VERSION=\"$(ASTERISKVERSION)\"
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
CFLAGS+=# -fomit-frame-pointer 
SUBDIRS=res channels pbx apps codecs formats agi cdr astman
LIBS=-ldl -lpthread -lncurses -lm  #-lnjamd
OBJS=io.o sched.o logger.o frame.o loader.o config.o channel.o \
	translate.o file.o say.o pbx.o cli.o md5.o term.o \
	ulaw.o alaw.o callerid.o fskmodem.o image.o app.o \
	cdr.o tdd.o acl.o rtp.o manager.o asterisk.o ast_expr.o \
	dsp.o chanvars.o indications.o autoservice.o db.o privacy.o
CC=gcc
INSTALL=install

_all: all
	@echo " +--------- Asterisk Build Complete ---------+"  
	@echo " + Asterisk has successfully been built, but +"  
	@echo " + cannot be run before being installed by   +"  
	@echo " + running:                                  +"  
	@echo " +                                           +"
	@echo " +               make install                +"  
	@echo " +-------------------------------------------+"  

all: asterisk subdirs

editline/config.h:
	@if [ -d editline ]; then \
		cd editline && unset CFLAGS LIBS && ./configure ; \
	else \
		echo "You need to do a cvs update -d not just cvs update"; \
		exit 1; \
	fi

editline/libedit.a: editline/config.h
	make -C editline libedit.a

db1-ast/libdb1.a: 
	@if [ -d db1-ast ]; then \
		make -C db1-ast libdb1.a ; \
	else \
		echo "You need to do a cvs update -d not just cvs update"; \
		exit 1; \
	fi

_version: 
	if [ -d CVS ] && ! [ -f .version ]; then echo "CVS-`date +"%D-%T"`" > .version; fi 

.version: _version

.y.c:
	bison $< --name-prefix=ast_yy -o $@

ast_expr.o: ast_expr.c

build.h:
	./make_build_h

asterisk: .version build.h editline/libedit.a db1-ast/libdb1.a $(OBJS)
	gcc -o asterisk -rdynamic $(OBJS) $(LIBS) $(LIBEDIT) db1-ast/libdb1.a

subdirs: 
	for x in $(SUBDIRS); do $(MAKE) -C $$x || exit 1 ; done

clean:
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean || exit 1 ; done
	rm -f *.o *.so asterisk
	rm -f build.h 
	rm -f ast_expr.c
	@if [ -e editline/Makefile ]; then make -C editline clean ; fi
	make -C db1-ast clean

datafiles: all
	mkdir -p $(ASTVARLIBDIR)/sounds/digits
	for x in sounds/digits/*.gsm; do \
		if grep -q "^%`basename $$x`%" sounds.txt; then \
			install $$x $(ASTVARLIBDIR)/sounds/digits ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	for x in sounds/vm-* sounds/transfer* sounds/pbx-* sounds/ss-* sounds/beep* sounds/dir-* sounds/conf-* sounds/agent-* sounds/invalid* sounds/tt-* sounds/auth-* sounds/privacy-*; do \
		if grep -q "^%`basename $$x`%" sounds.txt; then \
			install $$x $(ASTVARLIBDIR)/sounds ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	mkdir -p $(ASTVARLIBDIR)/mohmp3
	mkdir -p $(ASTVARLIBDIR)/images
	for x in images/*.jpg; do \
		install $$x $(ASTVARLIBDIR)/images ; \
	done
	mkdir -p $(AGI_DIR)

update: 
	@if [ -d CVS ]; then \
		echo "Updating from CVS..." ; \
		cvs update -d; \
		rm -f .version; \
	else \
		echo "Not CVS";  \
	fi

bininstall: all
	mkdir -p $(MODULES_DIR)
	mkdir -p $(ASTSBINDIR)
	mkdir -p $(ASTETCDIR)
	mkdir -p $(ASTBINDIR)
	mkdir -p $(ASTSBINDIR)
	mkdir -p $(ASTVARRUNDIR)
	install -m 755 asterisk $(ASTSBINDIR)/
	install -m 755 astgenkey $(ASTSBINDIR)/
	install -m 755 safe_asterisk $(ASTSBINDIR)/
	for x in $(SUBDIRS); do $(MAKE) -C $$x install || exit 1 ; done
	install -d $(ASTHEADERDIR)
	install include/asterisk/*.h $(ASTHEADERDIR)
	rm -f $(ASTVARLIBDIR)/sounds/vm
	mkdir -p $(ASTSPOOLDIR)/vm
	rm -f $(ASTMODULESDIR)/chan_ixj.so
	rm -f $(ASTMODULESDIR)/chan_tor.so
	mkdir -p $(ASTVARLIBDIR)/sounds
	mkdir -p $(ASTLOGDIR)/cdr-csv
	mkdir -p $(ASTVARLIBDIR)/keys
	install -m 644 keys/iaxtel.pub $(ASTVARLIBDIR)/keys
	( cd $(ASTVARLIBDIR)/sounds  ; ln -s $(ASTSPOOLDIR)/vm . )
	@echo " +---- Asterisk Installation Complete -------+"  
	@echo " +                                           +"
	@echo " +    YOU MUST READ THE SECURITY DOCUMENT    +"
	@echo " +                                           +"
	@echo " + Asterisk has successfully been installed. +"  
	@echo " + If you would like to install the sample   +"  
	@echo " + configuration files (overwriting any      +"
	@echo " + existing config files), run:              +"  
	@echo " +                                           +"
	@echo " +               make samples                +"
	@echo " +                                           +"
	@echo " +-----------------  or ---------------------+"
	@echo " +                                           +"
	@echo " + You can go ahead and install the asterisk +"
	@echo " + program documentation now or later run:   +"
	@echo " +                                           +"
	@echo " +              make progdocs                +"
	@echo " +                                           +"
	@echo " + **Note** This requires that you have      +"
	@echo " + doxygen installed on your local system    +"
	@echo " +-------------------------------------------+"

install: all datafiles bininstall

upgrade: all bininstall

adsi: all
	mkdir -p $(ASTETCDIR)
	for x in configs/*.adsi; do \
		if ! [ -f $(ASTETCDIRX)/$$x ]; then \
			install -m 644 $$x $(ASTETCDIR)/`basename $$x` ; \
		fi ; \
	done

samples: all datafiles adsi
	mkdir -p $(ASTETCDIR)
	for x in configs/*.sample; do \
		if [ -f $(ASTETCDIR)/`basename $$x .sample` ]; then \
			mv -f $(ASTETCDIR)/`basename $$x .sample` $(ASTETCDIR)/`basename $$x .sample`.old ; \
		fi ; \
		install $$x $(ASTETCDIR)/`basename $$x .sample` ;\
	done
	echo "[directories]" > $(ASTETCDIR)/asterisk.conf
	echo "astetcdir => $(ASTETCDIR)" >> $(ASTETCDIR)/asterisk.conf
	echo "astmoddir => $(MODULES_DIR)" >> $(ASTETCDIR)/asterisk.conf
	echo "astvarlibdir => $(ASTVARLIBDIR)" >> $(ASTETCDIR)/asterisk.conf
	echo "astagidir => $(AGI_DIR)" >> $(ASTETCDIR)/asterisk.conf
	echo "astspooldir => $(ASTSPOOLDIR)" >> $(ASTETCDIR)/asterisk.conf
	echo "astrundir => $(ASTVARRUNDIR)" >> $(ASTETCDIR)/asterisk.conf
	echo "astlogdir => $(ASTLOGDIR)" >> $(ASTETCDIR)/asterisk.conf
	for x in sounds/demo-*; do \
		if grep -q "^%`basename $$x`%" sounds.txt; then \
			install $$x $(ASTVARLIBDIR)/sounds ; \
		else \
			echo "No description for $$x"; \
			exit 1; \
		fi; \
	done
	for x in sounds/*.mp3; do \
		install $$x $(ASTVARLIBDIR)/mohmp3 ; \
	done
	mkdir -p $(ASTSPOOLDIR)/vm/1234/INBOX
	:> $(ASTVARLIBDIR)/sounds/vm/1234/unavail.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isunavail; do \
		cat $(ASTVARLIBDIR)/sounds/$$x.gsm >> $(ASTVARLIBDIR)/sounds/vm/1234/unavail.gsm ; \
	done
	:> $(ASTVARLIBDIR)/sounds/vm/1234/busy.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isonphone; do \
		cat $(ASTVARLIBDIR)/sounds/$$x.gsm >> $(ASTVARLIBDIR)/sounds/vm/1234/busy.gsm ; \
	done

webvmail:
	@[ -d $(HTTPDIR) ] || ( echo "No HTTP directory" && exit 1 )
	@[ -d $(HTTPDIR)/html ] || ( echo "No http directory" && exit 1 )
	@[ -d $(HTTPDIR)/cgi-bin ] || ( echo "No cgi-bin directory" && exit 1 )
	install -m 4755 -o root -g root vmail.cgi $(HTTPDIR)/cgi-bin/vmail.cgi
	mkdir -p $(HTTPDIR)/html/_asterisk
	for x in images/*.gif; do \
		install -m 644 $$x $(HTTPDIR)/html/_asterisk/; \
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
	./addmailbox 
	

rpm: __rpm

__rpm: _version
	rm -rf /tmp/asterisk ; \
	mkdir -p /tmp/asterisk/redhat/RPMS/i386 ; \
	make INSTALL_PREFIX=/tmp/asterisk install ; \
	make INSTALL_PREFIX=/tmp/asterisk samples ; \
	mkdir -p /tmp/asterisk/etc/rc.d/init.d ; \
	cp -f redhat/asterisk /tmp/asterisk/etc/rc.d/init.d/ ; \
	cp -f redhat/rpmrc /tmp/asterisk/ ; \
	cp -f redhat/rpmmacros /tmp/asterisk/ ; \
	sed "s/Version:/Version: $(RPMVERSION)/g" redhat/asterisk.spec > /tmp/asterisk/asterisk.spec ; \
	rpm --rcfile /usr/lib/rpm/rpmrc:/tmp/asterisk/rpmrc -bb /tmp/asterisk/asterisk.spec ; \
	mv /tmp/asterisk/redhat/RPMS/i386/asterisk* ./ ; \
	rm -rf /tmp/asterisk

progdocs:
	doxygen asterisk-ng-doxygen

config:
	if [ -d /etc/rc.d/init.d ]; then \
		install -m 755 init.asterisk /etc/rc.d/init.d/asterisk; \
		/sbin/chkconfig --add asterisk; \
	elif [ -d /etc/init.d ]; then \
		install -m 755 init.asterisk /etc/init.d/asterisk; \
	fi 

	
dont-optimize:
	make OPTIMIZE= K6OPT= install

valgrind: dont-optimize
