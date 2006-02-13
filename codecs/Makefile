#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for codec modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS:=$(patsubst %.c,%.so,$(wildcard codec_*.c))

ifeq (${OSARCH},CYGWIN)
  CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
  CYGSOLIB=-L.. -L. -lasterisk.dll
else
  CFLAGS+=-fPIC
endif

ifeq ($(wildcard g723.1/coder.c),)
  MODS:=$(filter-out codec_g723_1.so,$(MODS))
else
  LIBG723=g723.1/libg723.a
endif

ifeq ($(wildcard g723.1b/coder2.c),)
  MODS:=$(filter-out codec_g723_1b.so,$(MODS))
else
  LIBG723B=g723.1b/libg723b.a
endif

SPEEX_PATH:=/usr/local/include /usr/include /usr/include/speex /usr/local/include/speex
SPEEX_SYSTEM_HEADERS:=$(firstword $(wildcard $(SPEEX_PATH:%=$(CROSS_COMPILE_TARGET)%/speex.h)))
ifeq (${SPEEX_SYSTEM_HEADERS},)
  MODS:=$(filter-out codec_speex.so,$(MODS))
else
  CFLAGS+=-I$(subst /speex.h,,${SPEEX_SYSTEM_HEADERS})
  LIBSPEEX=-lspeex -lm
endif

ifeq ($(wildcard ilbc/iLBC_decode.h),)
  MODS:=$(filter-out codec_ilbc.so,$(MODS))
else
  LIBILBC=ilbc/libilbc.a
endif

LIBGSM_PATH:=/usr/local/include /usr/include
LIBGSM_SYSTEM_HEADERS:=$(firstword $(wildcard $(LIBGSM_PATH:%=$(CROSS_COMPILE_TARGET)%/gsm/gsm.h)))
ifneq ($(LIBGSM_SYSTEM_HEADERS),)
  LIBGSM=-lgsm
  LIBGSMT=
  CFLAGS+=-DUSE_EXTERNAL_GSM_LIB
else
  LIBGSM=gsm/lib/libgsm.a
  LIBGSMT=$(LIBGSM)
  CFLAGS+=-I.
endif

LIBLPC10=lpc10/liblpc10.a

all: depend $(MODS)

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o
	[ ! -d g723.1 ] || $(MAKE) -C g723.1 clean
	[ ! -d g723.1b ] || $(MAKE) -C g723.1b clean
	$(MAKE) -C gsm clean
	$(MAKE) -C lpc10 clean
	$(MAKE) -C ilbc clean

$(LIBG723):
	$(MAKE) -C g723.1 all

$(LIBGSM):
	$(MAKE) -C gsm lib/libgsm.a

$(LIBG723B):
	$(MAKE) -C g723.1b all

$(LIBLPC10):
	$(MAKE) -C lpc10 all

$(LIBILBC):
	$(MAKE) -C ilbc all

codec_ilbc.so: codec_ilbc.o $(LIBILBC)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBILBC)

codec_g723_1.so : codec_g723_1.o $(LIBG723)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBG723)

codec_g723_1b.o : codec_g723_1.c
	$(CC) -c -o $@ $(CFLAGS) -DANNEX_B -Dsingle $<

codec_g723_1b.so : codec_g723_1b.o $(LIBG723B)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBG723B) -lm

codec_gsm.so: codec_gsm.o $(LIBGSMT) 
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBGSM)

codec_speex.so: codec_speex.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBSPEEX)

codec_lpc10.so: codec_lpc10.o $(LIBLPC10)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBLPC10) -lm

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

ifneq ($(wildcard .depend),)
  include .depend
endif

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
