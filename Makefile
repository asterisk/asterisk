#
# Asterisk -- A telephony toolkit for Linux.
# 
# Top level Makefile
#
# Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
#
# Mark Spencer <markster@linux-support.net>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#


.EXPORT_ALL_VARIABLES:

MODULES_DIR=/usr/lib/asterisk/modules

DEBUG=-g #-pg
INCLUDE=-Iinclude -I../include
CFLAGS=-Wall -Werror -O6 $(DEBUG) $(INCLUDE) -D_REENTRANT
CFLAGS+=$(shell if $(CC) -march=i686 -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=i686"; fi)
SUBDIRS=channels pbx apps codecs formats
LIBS=-ldl -lpthread #-lefence
OBJS=io.o sched.o logger.o frame.o loader.o config.o channel.o translate.o file.o say.o pbx.o asterisk.o
CC=gcc
INSTALL=install

all: asterisk subdirs

asterisk: $(OBJS)
	gcc -o asterisk -rdynamic $(OBJS) $(LIBS)

subdirs: 
	for x in $(SUBDIRS); do $(MAKE) -C $$x || exit 1 ; done

clean:
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean || exit 1 ; done
	rm -f *.o *.so asterisk

install: all
	mkdir -p $(MODULES_DIR)
	for x in $(SUBDIRS); do $(MAKE) -C $$x install || exit 1 ; done
	install -d /usr/include/asterisk
	install include/asterisk/*.h /usr/include/asterisk
