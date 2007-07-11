#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for channel drivers
#
# Copyright (C) 1999-2005, Mark Spencer
#
# Mark Spencer <markster@digium.com>
#
# Edited By Belgarath <> Aug 28 2004
# Added bare bones ultrasparc-linux support.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

CHANNEL_LIBS=chan_sip.so chan_agent.so chan_mgcp.so chan_iax2.so chan_local.so chan_skinny.so chan_features.so

SUBDIRS=misdn h323

ifneq (${OSARCH},CYGWIN)
# if you really, really want to use these drivers, uncomment the line below
#CHANNEL_LIBS+=chan_modem.so chan_modem_aopen.so chan_modem_bestdata.so
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
 CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

ifeq (${OSARCH},OpenBSD)
  PTLIB=-lpt_OpenBSD_x86_r
  H323LIB=-lh323_OpenBSD_x86_r
endif

ifeq (${OSARCH},Linux)
  PTLIB=-lpt_linux_x86_r
  H323LIB=-lh323_linux_x86_r
  CHANH323LIB=-ldl
endif

ifeq (${OSARCH},CYGWIN)
CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
CYGSOLIB=-L.. -L. -L../res -lasterisk.dll -lres_features.so
CYG_CHAN_AGENT=-lres_monitor.so
endif

ifeq ($(PROC),sparc64)
  PROC=ultrasparc
  CFLAGS += -mtune=$(PROC) -pipe -fomit-frame-pointer -mcpu=v8
endif

ifeq (${OSARCH},FreeBSD)
  PTLIB=-lpt_FreeBSD_x86_r
  H323LIB=-lh323_FreeBSD_x86_r
  CHANH323LIB=-pthread
endif

ifeq (${OSARCH},NetBSD)
  PTLIB=-lpt_NetBSD_x86_r
  H323LIB=-lh323_NetBSD_x86_r
endif

ifneq (${OSARCH},Darwin)
  ifneq (${OSARCH},SunOS)
    ifneq (${OSARCH},CYGWIN)
# the ISDN4Linux channel driver is probably broken on all modern kernels,
# but if you wish to try to use it, uncomment the line below
#      CHANNEL_LIBS+=chan_modem_i4l.so
       CHANNEL_LIBS+=chan_oss.so
    endif
  endif
endif

ifeq (${OSARCH},SunOS)
  SOLINK+=-lrt
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/ixjuser.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/ixjuser.h),)
  CHANNEL_LIBS+=chan_phone.so
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/compiler.h),)
  CFLAGS+=-DHAVE_LINUX_COMPILER_H
endif

ifneq ($(wildcard h323/libchanh323.a),)
  CHANNEL_LIBS+=chan_h323.so
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/mISDNuser/mISDNlib.h),)
  CHANNEL_LIBS+=chan_misdn.so
  CFLAGS+=-Imisdn  
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/mISDNdsp.h),)
  CFLAGS+=-DMISDN_1_2
endif

CFLAGS+=-Wno-missing-prototypes -Wno-missing-declarations

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/alsa/asoundlib.h),)
  CHANNEL_LIBS+=chan_alsa.so
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/lib/libpri.so.1)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/lib/libpri.so.1),)
  CFLAGS+=-DZAPATA_PRI
  ZAPPRI=-lpri
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/lib/libmfcr2.so.1)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/lib/libmfcr2.so.1),)
  CFLAGS+=-DZAPATA_R2
  ZAPR2=-lmfcr2
endif

ALSA_SRC=chan_alsa.c

ifneq ($(wildcard alsa-monitor.h),)
  CFLAGS+=-DALSA_MONITOR
  ALSA_SRC+=alsa-monitor.h
endif

ifndef WITHOUT_ZAPTEL
ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/zaptel.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/zaptel.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/pkg/include/zaptel.h),)
  ifeq (${OSARCH},NetBSD)
    SOLINK+=-L$(CROSS_COMPILE_TARGET)/usr/pkg/lib
  endif
  ifeq (${OSARCH},FreeBSD)
    SOLINK+=-L$(CROSS_COMPILE_TARGET)/usr/local/lib
  endif
  CFLAGS+=-DIAX_TRUNKING
  CHANNEL_LIBS+=chan_zap.so
endif
endif # WITHOUT_ZAPTEL

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/vpbapi.h),)
  CHANNEL_LIBS+=chan_vpb.so
  CFLAGS+=-DLINUX
endif

CFLAGS+=-DCRYPTO

ifneq ($(OSARCH),CYGWIN)
CFLAGS+=-fPIC
endif

CFLAGS+=#-DVOFRDUMPER

ZAPDIR=/usr/lib

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/nbs.h),)
  CHANNEL_LIBS+=chan_nbs.so
endif

ifndef OPENH323DIR
  OPENH323DIR=$(HOME)/openh323
endif

ifndef PWLIBDIR
  PWLIBDIR=$(HOME)/pwlib
endif

#CFLAGS+=$(shell [ -f $(ZAPDIR)/libzap.a ] && echo "-I$(ZAPDIR)")

all: depend $(CHANNEL_LIBS) 

clean:
	for x in $(SUBDIRS); do $(MAKE) -C $$x clean || exit 1 ; done
	rm -f *.so *.o .depend
	rm -f busy.h ringtone.h gentone gentone-ulaw

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} ${LIBS}

ifneq ($(wildcard .depend),)
  include .depend
endif

ifneq ($(wildcard h323/Makefile.ast),)
  include h323/Makefile.ast
endif

ifneq ($(wildcard misdn/Makefile.ast),)
  include misdn/Makefile.ast
endif

gentone: gentone.c
	$(HOST_CC) -o gentone gentone.c -lm

gentone-ulaw: gentone-ulaw.c
	$(HOST_CC) -o gentone-ulaw gentone-ulaw.c -lm

busy.h: gentone
	./gentone busy 480 620

ringtone.h: gentone
	./gentone ringtone 440 480

chan_oss.o: chan_oss.c busy.h ringtone.h

chan_alsa.o: chan_alsa.c busy.h ringtone.h

ifeq (${OSARCH},OpenBSD)
chan_oss.so: chan_oss.o
	$(CC) $(SOLINK) -o $@ chan_oss.o -lossaudio
endif
ifeq (${OSARCH},NetBSD)
chan_oss.so: chan_oss.o
	$(CC) $(SOLINK) -o $@ chan_oss.o -lossaudio
endif

chan_iax2.so: chan_iax2.o iax2-parser.o iax2-provision.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} chan_iax2.o iax2-parser.o iax2-provision.o ${CYGSOLIB}

chan_zap.o: chan_zap.c
	$(CC) -c $(CFLAGS) -o chan_zap.o chan_zap.c

chan_zap.so: chan_zap.o
	$(CC) $(SOLINK) -o $@ $<  $(ZAPPRI) $(ZAPR2) -ltonezone

chan_sip.so: chan_sip.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} chan_sip.o ${CYGSOLIB}

chan_agent.so: chan_agent.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} chan_agent.o ${CYGSOLIB} ${CYG_CHAN_AGENT}

chan_alsa.o: $(ALSA_SRC)

chan_alsa.so: chan_alsa.o
	$(CC) $(SOLINK) -o $@ $< -lasound -lm -ldl

chan_nbs.so: chan_nbs.o
	$(CC) $(SOLINK) -o $@ $< -lnbs

chan_vpb.o: chan_vpb.c
	$(CXX) -c $(CFLAGS) -o $@ chan_vpb.c

chan_vpb.so: chan_vpb.o
	 $(CXX) $(SOLINK) -o $@ $< -lvpb -lpthread -lm -ldl

ifeq (${OSARCH},Linux)
chan_h323.so: chan_h323.o h323/libchanh323.a h323/Makefile.ast
	$(CC) $(SOLINK) $(H323LDFLAGS) -o $@ $< h323/libchanh323.a $(H323LDLIBS) -lstdc++
else
chan_h323.so: chan_h323.o h323/libchanh323.a
	$(CC) $(SOLINK) -o $@ $< h323/libchanh323.a $(CHANH323LIB) -L$(PWLIBDIR)/lib $(PTLIB) -L$(OPENH323DIR)/lib $(H323LIB) -L/usr/lib -lcrypto -lssl -lexpat
endif

misdn/chan_misdn_lib.a:
	make CROSS_COMPILE_TARGET=$(CROSS_COMPILE_TARGET) -C misdn

chan_misdn.so: chan_misdn.o misdn_config.o misdn/chan_misdn_lib.a 
	$(CC) -shared -Xlinker -x -L/usr/lib -o $@ $^ -lisdnnet -lmISDN 

chan_misdn.o: chan_misdn.c 
	$(CC) $(CFLAGS) -DCHAN_MISDN_VERSION=\"0.3.0\" -c $< -o $@

misdn_config.o: misdn_config.c misdn/chan_misdn_config.h
	$(CC) $(CFLAGS) -DCHAN_MISDN_VERSION=\"0.3.0\" -c $< -o $@

#chan_modem.so : chan_modem.o
#	$(CC) -rdynamic -shared -Xlinker -x -o $@ $<

install: all
	for x in $(CHANNEL_LIBS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	if ! [ -f chan_iax.so ]; then rm -f $(DESTDIR)$(MODULES_DIR)/chan_iax.so ; fi

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

env:
	env

