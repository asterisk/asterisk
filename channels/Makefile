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

OSARCH=$(shell uname -s)

USE_MYSQL_FRIENDS=0
USE_SIP_MYSQL_FRIENDS=0

CHANNEL_LIBS=chan_modem.so chan_sip.so \
	     chan_modem_aopen.so \
             chan_modem_bestdata.so chan_modem_i4l.so \
             chan_agent.so chan_mgcp.so chan_iax2.so \
	     chan_local.so chan_skinny.so

#
# If you really want IAX1 uncomment the following, but it is
# unmaintained
#
#CHANNEL_LIBS+=chan_iax.so

#
# If you really want VoFR you can have it :-P
#
#CHANNEL_LIBS+=chan_vofr

ifeq (${OSARCH},OpenBSD)
MYSQLLIB=-L/usr/local/lib/mysql -lmysqlclient
CFLAGS+=-I/usr/local/include
PTLIB=-lpt_OpenBSD_x86_r
H323LIB=-lh323_OpenBSD_x86_r
CHANH323LIB=-L/usr/local/lib
else
MYSQLLIB=-L/usr/lib/mysql -lmysqlclient
PTLIB=-lpt_linux_x86_r
H323LIB=-lh323_linux_x86_r
CHANH323LIB=-ldl
endif

ifneq (${OSARCH},Darwin)
CHANNEL_LIBS+=chan_oss.so
endif

CHANNEL_LIBS+=$(shell [ -f /usr/include/linux/ixjuser.h ] && echo chan_phone.so)
CHANNEL_LIBS+=$(shell [ -f h323/libchanh323.a ] && echo chan_h323.so)

CFLAGS+=-Wno-missing-prototypes -Wno-missing-declarations
CFLAGS+=$(shell [ ! -f /usr/include/linux/if_wanpipe.h ] && echo " -DOLD_SANGOMA_API")
CHANNEL_LIBS+=$(shell [ -f /usr/include/alsa/asoundlib.h ] && echo "chan_alsa.so")
CFLAGS+=$(shell [ -f /usr/lib/libpri.so.1 ] && echo " -DZAPATA_PRI")
CFLAGS+=$(shell [ -f /usr/lib/libmfcr2.so.1 ] && echo " -DZAPATA_R2")
CFLAGS+=$(shell [ -f alsa-monitor.h ] && echo " -DALSA_MONITOR")
ZAPPRI=$(shell [ -f /usr/lib/libpri.so.1 ] && echo "-lpri")
ZAPR2=$(shell [ -f /usr/lib/libmfcr2.so.1 ] && echo "-lmfcr2")
CFLAGS+=$(shell [ -f /usr/include/linux/zaptel.h ] && echo "-DIAX_TRUNKING")
CHANNEL_LIBS+=$(shell [ -f /usr/include/vpbapi.h ] && echo "chan_vpb.so" )
CFLAGS+=$(shell [ -f /usr/include/vpbapi.h ] && echo " -DLINUX")

ALSA_SRC=chan_alsa.c
ALSA_SRC+=$(shell [ -f alsa-monitor.h ] && echo "alsa-monitor.h")

CFLAGS+=-DCRYPTO
CFLAGS+=-fPIC

ifeq ($(USE_MYSQL_FRIENDS),1)
CFLAGS+=-DMYSQL_FRIENDS
USE_SIP_MYSQL_FRIENDS=1
endif
ifeq ($(USE_SIP_MYSQL_FRIENDS),1)
CFLAGS+=-DSIP_MYSQL_FRIENDS
endif

CFLAGS+=#-DVOFRDUMPER

ZAPDIR=/usr/lib

CHANNEL_LIBS+=$(shell [ -f /usr/include/linux/zaptel.h ] && echo "chan_zap.so")

CHANNEL_LIBS+=$(shell [ -f /usr/include/nbs.h ] && echo "chan_nbs.so" )

ifndef OPENH323DIR
OPENH323DIR=$(HOME)/openh323
endif

ifndef PWLIBDIR
PWLIBDIR=$(HOME)/pwlib
endif

#CFLAGS+=$(shell [ -f $(ZAPDIR)/libzap.a ] && echo "-I$(ZAPDIR)")

all: depend $(CHANNEL_LIBS) 

clean:
	rm -f *.so *.o .depend
	rm -f busy.h ringtone.h gentone gentone-ulaw

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif

gentone: gentone.c
	$(CC) -o gentone gentone.c -lm

gentone-ulaw: gentone-ulaw.c
	$(CC) -o gentone-ulaw gentone-ulaw.c -lm

busy.h: gentone
	./gentone busy 480 620

ringtone.h: gentone
	./gentone ringtone 440 480

chan_oss.o: chan_oss.c  busy.h ringtone.h

ifeq (${OSARCH},OpenBSD)
chan_oss.so: chan_oss.o
	$(CC) $(SOLINK) -o $@ chan_oss.o -lossaudio
endif

chan_iax2.so: chan_iax2.o iax2-parser.o
ifeq ($(USE_MYSQL_FRIENDS),1)
	$(CC) $(SOLINK) -o $@ chan_iax2.o iax2-parser.o $(MYSQLLIB) -lz
else
	$(CC) $(SOLINK) -o $@ chan_iax2.o iax2-parser.o
endif

chan_iax.so: chan_iax.o
ifeq ($(USE_MYSQL_FRIENDS),1)
	$(CC) $(SOLINK) -o $@ chan_iax.o $(MYSQLLIB) -lz
else
	$(CC) $(SOLINK) -o $@ chan_iax.o
endif

chan_zap.o: chan_zap.c
	$(CC) -c $(CFLAGS) -o chan_zap.o chan_zap.c

chan_zap.so: chan_zap.o
	$(CC) $(SOLINK) -o $@ $<  $(ZAPPRI) $(ZAPR2) -ltonezone

chan_sip.so: chan_sip.o
ifeq ($(USE_SIP_MYSQL_FRIENDS),1)
	$(CC) $(SOLINK) -o $@ chan_sip.o $(MYSQLLIB) -lz
else
	$(CC) $(SOLINK) -o $@ chan_sip.o
endif

chan_alsa.o: $(ALSA_SRC)


chan_alsa.so: chan_alsa.o
	$(CC) $(SOLINK) -o $@ $< -lasound -lm -ldl

chan_nbs.so: chan_nbs.o
	$(CC) $(SOLINK) -o $@ $< -lnbs

chan_vpb.o: chan_vpb.c
	$(CXX) -c $(CFLAGS) -o $@ chan_vpb.c

chan_vpb.so: chan_vpb.o
	 $(CXX) $(SOLINK) -o $@ $< -lvpb -lpthread -lm -ldl

chan_h323.so: chan_h323.o h323/libchanh323.a
	$(CC) $(SOLINK) -o $@ $< h323/libchanh323.a $(CHANH323LIB) -L$(PWLIBDIR)/lib $(PTLIB) -L$(OPENH323DIR)/lib $(H323LIB) -L/usr/lib -lpthread -lcrypto -lssl -lexpat


#chan_modem.so : chan_modem.o
#	$(CC) -rdynamic -shared -Xlinker -x -o $@ $<

install: all
	for x in $(CHANNEL_LIBS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	if ! [ -f chan_iax.so ]; then rm -f $(DESTDIR)$(MODULES_DIR)/chan_iax.so ; fi

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`

env:
	env

