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

CHANNEL_LIBS=chan_modem.so chan_iax.so chan_sip.so \
	     chan_modem_aopen.so chan_oss.so \
             chan_modem_bestdata.so chan_modem_i4l.so \
             chan_agent.so chan_mgcp.so

#
# If you really want VoFR you can have it :-P
#
#CHANNEL_LIBS+=chan_vofr

CHANNEL_LIBS+=$(shell [ -f /usr/include/linux/ixjuser.h ] && echo chan_phone.so)

CFLAGS+=-Wno-missing-prototypes -Wno-missing-declarations
CFLAGS+=$(shell [ ! -f /usr/include/linux/if_wanpipe.h ] && echo " -DOLD_SANGOMA_API")
CHANNEL_LIBS+=$(shell [ -f /usr/include/alsa/asoundlib.h ] && echo "chan_alsa.so")
CFLAGS+=$(shell [ -f /usr/lib/libpri.so.1 ] && echo " -DZAPATA_PRI")
CFLAGS+=$(shell [ -f /usr/lib/libmfcr2.so.1 ] && echo " -DZAPATA_R2")
CFLAGS+=$(shell [ -f alsa-monitor.h ] && echo " -DALSA_MONITOR")
ZAPPRI=$(shell [ -f /usr/lib/libpri.so.1 ] && echo "-lpri")
ZAPR2=$(shell [ -f /usr/lib/libmfcr2.so.1 ] && echo "-lmfcr2")
CHANZAP=$(shell if [ -f .oldzap ]; then echo "chan_zap_old.c"; else echo "chan_zap.c"; fi)
ZAPLIB=$(shell if ! [ -f .newzap ]; then echo "-lzap"; fi)

ALSA_SRC=chan_alsa.c
ALSA_SRC+=$(shell [ -f alsa-monitor.h ] && echo "alsa-monitor.h")

CFLAGS+=-DCRYPTO

CFLAGS+=#-DVOFRDUMPER

ZAPDIR=/usr/lib

CHANNEL_LIBS+=$(shell [ -f /usr/include/linux/zaptel.h ] && echo "chan_zap.so")

CHANNEL_LIBS+=$(shell [ -f /usr/include/nbs.h ] && echo "chan_nbs.so" )

#CFLAGS+=$(shell [ -f $(ZAPDIR)/libzap.a ] && echo "-I$(ZAPDIR)")

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

chan_zap.o: $(CHANZAP)
	$(CC) -c $(CFLAGS) -o chan_zap.o $(CHANZAP)

chan_zap.so: chan_zap.o
	$(CC) -shared -Xlinker -x -o $@ $<  $(ZAPPRI) $(ZAPR2) -ltonezone $(ZAPLIB)

chan_alsa.o: $(ALSA_SRC)

chan_alsa.so: chan_alsa.o
	$(CC) -shared -Xlinker -x -o $@ $< -lasound -lm -ldl

chan_nbs.so: chan_nbs.o
	$(CC) -shared -Xlinker -x -o $@ $< -lnbs

#chan_modem.so : chan_modem.o
#	$(CC) -rdynamic -shared -Xlinker -x -o $@ $<

install: all
	for x in $(CHANNEL_LIBS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done
