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
             chan_modem_bestdata.so chan_modem_i4l.so

CHANNEL_LIBS+=$(shell [ -f /usr/include/linux/ixjuser.h ] && echo chan_phone.so)

CFLAGS+=-Wno-missing-prototypes -Wno-missing-declarations
CFLAGS+=$(shell [ ! -f /usr/include/linux/if_wanpipe.h ] && echo " -DOLD_SANGOMA_API")
CFLAGS+=$(shell [ -f /usr/lib/libpri.so.1 ] && echo " -DZAPATA_PRI")
ZAPPRI=$(shell [ -f /usr/lib/libpri.so.1 ] && echo "-lpri")

CFLAGS+=-DCRYPTO

CFLAGS+=#-DVOFRDUMPER

ZAPDIR=/usr/lib

CHANNEL_LIBS+=$(shell [ -f $(ZAPDIR)/libzap.a ] && echo "chan_zap.so")

CFLAGS+=$(shell [ -f $(ZAPDIR)/libzap.a ] && echo "-I$(ZAPDIR)")

all: $(CHANNEL_LIBS) 

clean:
	rm -f *.so *.o
	rm -f busy.h ringtone.h gentone gentone-ulaw

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

gentone: gentone.c
	$(CC) -o gentone gentone.c -lm

gentone-ulaw: gentone-ulaw.c
	$(CC) -o gentone-ulaw gentone-ulaw.c -lm

busy.h: gentone
	./gentone busy 480 620

ringtone.h: gentone
	./gentone ringtone 440 480

chan_oss.o: chan_oss.c  busy.h ringtone.h

chan_zap.so: chan_zap.o
	$(CC) -shared -Xlinker -x -o $@ $<  $(ZAPPRI) -lzap -ltonezone

#chan_modem.so : chan_modem.o
#	$(CC) -rdynamic -shared -Xlinker -x -o $@ $<

install: all
	for x in $(CHANNEL_LIBS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done
