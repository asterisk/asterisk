#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for channel drivers
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS:=$(filter-out $(MENUSELECT_CHANNELS),$(patsubst %.c,%.so,$(wildcard chan_*.c)))

ifeq ($(OSARCH),OpenBSD)
  PTLIB=-lpt_OpenBSD_x86_r
  H323LIB=-lh323_OpenBSD_x86_r
endif

ifeq ($(OSARCH),Linux)
  PTLIB=-lpt_linux_x86_r
  H323LIB=-lh323_linux_x86_r
  CHANH323LIB=-ldl
endif

ifeq ($(PROC),sparc64)
  PROC=ultrasparc
  CFLAGS += -mtune=$(PROC) -pipe -fomit-frame-pointer -mcpu=v8
endif

ifeq ($(OSARCH),FreeBSD)
  PTLIB=-lpt_FreeBSD_x86_r
  H323LIB=-lh323_FreeBSD_x86_r
  CHANH323LIB=-pthread
endif

ifeq ($(OSARCH),NetBSD)
  PTLIB=-lpt_NetBSD_x86_r
  H323LIB=-lh323_NetBSD_x86_r
endif

ifeq ($(OSARCH),SunOS)
  SOLINK+=-lrt
endif

ifeq ($(WITH_SMDI),1)
  CFLAGS+=-DWITH_SMDI
endif

ifeq ($(wildcard h323/libchanh323.a),)
  MODS:=$(filter-out chan_h323.so,$(MODS))
endif

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/mISDNuser/mISDNlib.h),)
  MODS:=$(filter-out chan_misdn.so,$(MODS))
else
  CFLAGS+=-Imisdn 
endif

ifndef OPENH323DIR
  OPENH323DIR=$(HOME)/openh323
endif

ifndef PWLIBDIR
  PWLIBDIR=$(HOME)/pwlib
endif

all: depend $(MODS) 

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o
	rm -f busy.h ringtone.h gentone gentone-ulaw

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

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
	$(CC) -c -o $@ $(CFLAGS) $(OSSAUDIO_INCLUDE) $<

chan_oss.so: chan_oss.o
	$(CC) $(SOLINK) -o $@ chan_oss.o $(OSSAUDIO_LIB)

chan_iax2.so: chan_iax2.o iax2-parser.o iax2-provision.o
	$(CC) $(SOLINK) -o $@ $< iax2-parser.o iax2-provision.o

chan_zap.so: chan_zap.o
	$(CC) $(SOLINK) -o $@ $< $(PRI_LIB) $(ZAPTEL_LIB)

chan_zap.o: chan_zap.c
	$(CC) -c -o $@ $(CFLAGS) $(ZAPTEL_INCLUDE) $<

chan_alsa.so: chan_alsa.o
	$(CC) $(SOLINK) -o $@ $< $(ASOUND_LIB)

chan_alsa.o: chan_alsa.c busy.h ringtone.h
	$(CC) -c -o $@ $(CFLAGS) $(ASOUND_INCLUDE) $<

chan_nbs.so: chan_nbs.o
	$(CC) $(SOLINK) -o $@ $< $(NBS_LIB)

chan_nbs.o: chan_nbs.c
	$(CC) -c -o $@ $(CFLAGS) $(NBS_INCLUDE) $<

chan_vpb.o: chan_vpb.c
	$(CXX) -c $(CFLAGS:-Werror=) -o $@ chan_vpb.c

chan_vpb.so: chan_vpb.o
	 $(CXX) $(SOLINK) -o $@ $< -lvpb -lpthread -lm -ldl

ifeq ($(OSARCH),Linux)
chan_h323.so: chan_h323.o h323/libchanh323.a h323/Makefile.ast
	$(CC) $(SOLINK) $(H323LDFLAGS) -o $@ $< h323/libchanh323.a $(H323LDLIBS) -lstdc++
else
chan_h323.so: chan_h323.o h323/libchanh323.a
	$(CC) $(SOLINK) -o $@ $< h323/libchanh323.a $(CHANH323LIB) -L$(PWLIBDIR)/lib $(PTLIB) -L$(OPENH323DIR)/lib $(H323LIB) -L/usr/lib -lcrypto -lssl -lexpat
endif

misdn/chan_misdn_lib.a:
	make -C misdn

chan_misdn.so: chan_misdn.o misdn_config.o misdn/chan_misdn_lib.a
	$(CC) -shared -Xlinker -x -L/usr/lib -o $@ $^ -lisdnnet -lmISDN

chan_misdn.o: chan_misdn.c
	$(CC) $(CFLAGS) -DCHAN_MISDN_VERSION=\"0.3.0\" -c $< -o $@

misdn_config.o: misdn_config.c misdn/chan_misdn_config.h
	$(CC) $(CFLAGS) -DCHAN_MISDN_VERSION=\"0.3.0\" -c $< -o $@

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

env:
	env
