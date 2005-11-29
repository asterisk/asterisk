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

INSTALL_PREFIX=

MODULES_DIR=$(INSTALL_PREFIX)/usr/lib/asterisk/modules
AGI_DIR=$(INSTALL_PREFIX)/var/lib/asterisk/agi-bin

# Pentium Pro Optimize
#PROC=i686
# Pentium Optimize
PROC=i586

DEBUG=-g #-pg
INCLUDE=-Iinclude -I../include
CFLAGS=-pipe  -Wall -Wmissing-prototypes -Wmissing-declarations -O6 $(DEBUG) $(INCLUDE) -D_REENTRANT
#CFLAGS+=-Werror
CFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
ASTERISKVERSION=$(shell if [ -f .version ]; then cat .version; fi)
RPMVERSION=$(shell sed 's/[-\/:]/_/g' .version)
CFLAGS+=-DASTERISK_VERSION=\"$(ASTERISKVERSION)\"
# Optional debugging parameters
CFLAGS+= -DDO_CRASH -DDEBUG_THREADS
# Uncomment next one to enable ast_frame tracing (for debugging)
#CLFAGS+= -DTRACE_FRAMES
CFLAGS+=# -fomit-frame-pointer 
SUBDIRS=res channels pbx apps codecs formats agi cdr
LIBS=-ldl -lpthread -lreadline -lncurses -lm
OBJS=io.o sched.o logger.o frame.o loader.o config.o channel.o \
	translate.o file.o say.o pbx.o cli.o md5.o \
	ulaw.o alaw.o callerid.o fskmodem.o image.o app.o \
	cdr.o tdd.o asterisk.o 
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

_version: 
	if [ -d CVS ] && ! [ -f .version ]; then echo "CVS-`date +"%D-%T"`" > .version; fi 

.version:
	_version

build.h:
	./make_build_h

asterisk: .version build.h $(OBJS)
	gcc -o asterisk -rdynamic $(OBJS) $(LIBS)

subdirs: 
	for x in $(SUBDIRS); do $(MAKE) -C $$x || exit 1 ; done

clean:
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean || exit 1 ; done
	rm -f *.o *.so asterisk
	rm -f build.h

datafiles: all
	mkdir -p $(INSTALL_PREFIX)/var/lib/asterisk/sounds/digits
	for x in sounds/digits/*; do \
		install $$x $(INSTALL_PREFIX)/var/lib/asterisk/sounds/digits ; \
	done
	for x in sounds/vm-* sounds/transfer* sounds/pbx-* sounds/ss-* sounds/beep* sounds/dir-*; do \
		install $$x $(INSTALL_PREFIX)/var/lib/asterisk/sounds ; \
	done
	mkdir -p $(INSTALL_PREFIX)/var/lib/asterisk/images
	for x in images/*.jpg; do \
		install $$x $(INSTALL_PREFIX)/var/lib/asterisk/images ; \
	done
	mkdir -p $(AGI_DIR)

install: all datafiles
	mkdir -p $(MODULES_DIR)
	mkdir -p $(INSTALL_PREFIX)/usr/sbin
	install -m 755 asterisk $(INSTALL_PREFIX)/usr/sbin/
	install -m 755 astgenkey $(INSTALL_PREFIX)/usr/sbin/
	for x in $(SUBDIRS); do $(MAKE) -C $$x install || exit 1 ; done
	install -d $(INSTALL_PREFIX)/usr/include/asterisk
	install include/asterisk/*.h $(INSTALL_PREFIX)/usr/include/asterisk
	rm -f $(INSTALL_PREFIX)/var/lib/asterisk/sounds/vm
	mkdir -p $(INSTALL_PREFIX)/var/spool/asterisk/vm
	rm -f $(INSTALL_PREFIX)/usr/lib/asterisk/modules/chan_ixj.so
	rm -f $(INSTALL_PREFIX)/usr/lib/asterisk/modules/chan_tor.so
	mkdir -p $(INSTALL_PREFIX)/var/lib/asterisk/sounds
	mkdir -p $(INSTALL_PREFIX)/var/log/asterisk/cdr-csv
	mkdir -p $(INSTALL_PREFIX)/var/lib/asterisk/keys
	install -m 644 keys/iaxtel.pub $(INSTALL_PREFIX)/var/lib/asterisk/keys
	( cd $(INSTALL_PREFIX)/var/lib/asterisk/sounds  ; ln -s ../../../spool/asterisk/vm . )
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
samples: all datafiles
	mkdir -p $(INSTALL_PREFIX)/etc/asterisk
	for x in configs/*.sample; do \
		if [ -f $(INSTALL_PREFIX)/etc/asterisk/`basename $$x .sample` ]; then \
			mv -f $(INSTALL_PREFIX)/etc/asterisk/`basename $$x .sample` $(INSTALL_PREFIX)/etc/asterisk/`basename $$x .sample`.old ; \
		fi ; \
		install $$x $(INSTALL_PREFIX)/etc/asterisk/`basename $$x .sample` ;\
	done
	for x in sounds/demo-*; do \
		install $$x $(INSTALL_PREFIX)/var/lib/asterisk/sounds; \
	done
	mkdir -p $(INSTALL_PREFIX)/var/spool/asterisk/vm/1234/INBOX
	:> $(INSTALL_PREFIX)/var/lib/asterisk/sounds/vm/1234/unavail.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isunavail; do \
		cat $(INSTALL_PREFIX)/var/lib/asterisk/sounds/$$x.gsm >> $(INSTALL_PREFIX)/var/lib/asterisk/sounds/vm/1234/unavail.gsm ; \
	done
	:> $(INSTALL_PREFIX)/var/lib/asterisk/sounds/vm/1234/busy.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isonphone; do \
		cat $(INSTALL_PREFIX)/var/lib/asterisk/sounds/$$x.gsm >> $(INSTALL_PREFIX)/var/lib/asterisk/sounds/vm/1234/busy.gsm ; \
	done

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
