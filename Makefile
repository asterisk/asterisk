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

MODULES_DIR=/usr/lib/asterisk/modules

# Pentium Pro Optimize
#PROC=i686
# Pentium Optimize
PROC=i586

DEBUG=-g #-pg
INCLUDE=-Iinclude -I../include
CFLAGS=-pipe -Wall -Werror -Wmissing-prototypes -Wmissing-declarations -fomit-frame-pointer -O6 $(DEBUG) $(INCLUDE) -D_REENTRANT
CFLAGS+=$(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
CFLAGS += -DDO_CRASH
SUBDIRS=channels pbx apps codecs formats
LIBS=-ldl -lpthread -lreadline -lncurses -lm # -lefence
OBJS=io.o sched.o logger.o frame.o loader.o config.o channel.o \
	translate.o file.o say.o pbx.o cli.o md5.o \
	ulaw.o callerid.o fskmodem.o asterisk.o 
CC=gcc
INSTALL=install

_all: all
	@echo " +--------- Asterisk Build Complete ---------+"  
	@echo " + Asterisk has successfully been built, but +"  
	@echo " + cannot be run before being installed by   +"  
	@echo " + running:                                  +"  
	@echo " +                                           +"
	@echo " +               make install                +"  
	@echo " +                                           +"
	@echo " +-------------------------------------------+"  

all: asterisk subdirs

asterisk: $(OBJS)
	gcc -o asterisk -rdynamic $(OBJS) $(LIBS)

subdirs: 
	for x in $(SUBDIRS); do $(MAKE) -C $$x || exit 1 ; done

clean:
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean || exit 1 ; done
	rm -f *.o *.so asterisk

datafiles: all
	mkdir -p /var/lib/asterisk/sounds/digits
	for x in sounds/digits/*; do \
		install $$x /var/lib/asterisk/sounds/digits ; \
	done
	for x in sounds/vm-* sounds/transfer* sounds/pbx-* sounds/ss-*; do \
		install $$x /var/lib/asterisk/sounds ; \
	done
install: all datafiles
	mkdir -p $(MODULES_DIR)
	for x in $(SUBDIRS); do $(MAKE) -C $$x install || exit 1 ; done
	install -d /usr/include/asterisk
	install include/asterisk/*.h /usr/include/asterisk
	rm -f /var/lib/asterisk/sounds/vm
	mkdir -p /var/spool/asterisk/vm
	rm -f /usr/lib/asterisk/modules/chan_ixj.so
	mkdir -p /var/lib/asterisk/sounds
	( cd /var/lib/asterisk/sounds  ; ln -s ../../../spool/asterisk/vm . )
	@echo " +---- Asterisk Installation Complete -------+"  
	@echo " + Asterisk has successfully been installed. +"  
	@echo " + If you would like to install the sample   +"  
	@echo " + configuration files (overwriting any      +"
	@echo " + existing config files), run:              +"  
	@echo " +                                           +"
	@echo " +               make samples                +"
	@echo " +                                           +"
	@echo " +-------------------------------------------+"  

samples: all datafiles
	mkdir -p /etc/asterisk
	for x in configs/*.sample; do \
		if [ -f /etc/asterisk/`basename $$x .sample` ]; then \
			mv -f /etc/asterisk/`basename $$x .sample` /etc/asterisk/`basename $$x .sample`.old ; \
		fi ; \
		install $$x /etc/asterisk/`basename $$x .sample` ;\
	done
	for x in sounds/demo-*; do \
		install $$x /var/lib/asterisk/sounds; \
	done
	mkdir -p /var/spool/asterisk/vm/1234/INBOX
	:> /var/lib/asterisk/sounds/vm/1234/unavail.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isunavail; do \
		cat /var/lib/asterisk/sounds/$$x.gsm >> /var/lib/asterisk/sounds/vm/1234/unavail.gsm ; \
	done
	:> /var/lib/asterisk/sounds/vm/1234/busy.gsm
	for x in vm-theperson digits/1 digits/2 digits/3 digits/4 vm-isonphone; do \
		cat /var/lib/asterisk/sounds/$$x.gsm >> /var/lib/asterisk/sounds/vm/1234/busy.gsm ; \
	done
