#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for Channel backends (dynamically loaded)
#
# Copyright (C) 1999, Mark Spencer
#
# Mark Spencer <markster@linux-support.net>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

CHANNEL_LIBS=chan_vofr.so chan_modem.so \
	     chan_modem_aopen.so chan_iax.so chan_oss.so \
             chan_modem_i4l.so

CHANNEL_LIBS+=$(shell [ -f /usr/include/linux/ixjuser.h ] && echo chan_phone.so)

CFLAGS+=-Wno-missing-prototypes -Wno-missing-declarations
CFLAGS+=$(shell [ ! -f /usr/include/linux/if_wanpipe.h ] && echo " -DOLD_SANGOMA_API")
CFLAGS+=$(shell [ -f /usr/lib/libpri.so.1 ] && echo " -DTORMENTA_PRI")
TORPRI=$(shell [ -f /usr/lib/libpri.so.1 ] && echo "-lpri")

CFLAGS+=#-DVOFRDUMPER

ZAPDIR=/usr/lib
CHANNEL_LIBS+=$(shell [ -f $(ZAPDIR)/libzap.a ] && echo "chan_tor.so")

CFLAGS+=$(shell [ -f $(ZAPDIR)/libzap.a ] && echo "-I$(ZAPDIR)")

all: $(CHANNEL_LIBS)

clean:
	rm -f *.so *.o
	rm -f busy.h ringtone.h gentone

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

gentone: gentone.c
	$(CC) -o gentone gentone.c -lm

busy.h: gentone
	./gentone busy 480 620

ringtone.h: gentone
	./gentone ringtone 440 480

chan_oss.o: chan_oss.c  busy.h ringtone.h

chan_tor.so: chan_tor.o
	$(CC) -shared -Xlinker -x -o $@ $<  $(TORPRI) -lzap -ltonezone

#chan_modem.so : chan_modem.o
#	$(CC) -rdynamic -shared -Xlinker -x -o $@ $<

install: all
	for x in $(CHANNEL_LIBS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done
