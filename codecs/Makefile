#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for codec modules
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

ifeq (${OSARCH},CYGWIN)
CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
CYGSOLIB=-L.. -L. -lasterisk.dll
else
CFLAGS+=-fPIC
endif

ifneq ($(wildcard g723.1/coder.c),)
  MODG723=codec_g723_1.so
  LIBG723=g723.1/libg723.a
endif

ifneq ($(wildcard g723.1b/coder2.c),)
  MODG723+=codec_g723_1b.so
  LIBG723B=g723.1b/libg723b.a
endif

UI_SPEEX=$(wildcard $(CROSS_COMPILE_TARGET)/usr/include/speex.h)
UIS_SPEEX=$(wildcard $(CROSS_COMPILE_TARGET)/usr/include/speex/speex.h)
ULI_SPEEX=$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/speex.h)
ULIS_SPEEX=$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/speex/speex.h)
ifneq (${UI_SPEEX},)
  MODSPEEX=codec_speex.so
  LIBSPEEX+=-lspeex -lm
endif
ifneq (${UIS_SPEEX},)
  MODSPEEX=codec_speex.so
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/include/speex
  LIBSPEEX+=-lspeex -lm
endif
ifneq (${ULI_SPEEX},)
  MODSPEEX=codec_speex.so
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include
  LIBSPEEX=-L$(CROSS_COMPILE_TARGET)/usr/local/lib
  LIBSPEEX+=-lspeex -lm
endif
ifneq (${ULIS_SPEEX},)
  MODSPEEX=codec_speex.so
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include/speex
  LIBSPEEX=-L$(CROSS_COMPILE_TARGET)/usr/local/lib
  LIBSPEEX+=-lspeex -lm
endif

ifneq ($(wildcard ilbc/iLBC_decode.h),)
  MODILBC=codec_ilbc.so
  LIBILBC=ilbc/libilbc.a
endif


LIBGSM_PATH:=/usr/local/include /usr/include
LIBGSM_SYSTEM_HEADERS:=$(wildcard $(LIBGSM_PATH:%=$(CROSS_COMPILE_TARGET)%/gsm/gsm.h))
ifneq (,$(LIBGSM_SYSTEM_HEADERS))
LIBGSM=-lgsm
LIBGSMT=
CFLAGS+=-DUSE_EXTERNAL_GSM_LIB
else
LIBGSM=gsm/lib/libgsm.a
LIBGSMT=$(LIBGSM)
CFLAGS+=-I.
endif

LIBLPC10=lpc10/liblpc10.a

ifeq ($(findstring BSD,${OSARCH}),BSD)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

CODECS+=$(MODG723) $(MODSPEEX) $(MODILBC) codec_gsm.so codec_lpc10.so  \
        codec_adpcm.so codec_ulaw.so codec_alaw.so codec_a_mu.so \
	codec_g726.so

all: depend $(CODECS)

clean:
	rm -f *.so *.o .depend
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

$(MODILBC): codec_ilbc.o $(LIBILBC)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBILBC)

codec_g723_1.so : codec_g723_1.o $(LIBG723)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBG723)

codec_g723_1b.o : codec_g723_1.c
	$(CC) -c -o $@ $(CFLAGS) -DANNEX_B -Dsingle $<

codec_g723_1b.so : codec_g723_1b.o $(LIBG723B)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBG723B) -lm

codec_gsm.so: codec_gsm.o $(LIBGSMT) 
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBGSM)

$(MODSPEEX): codec_speex.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBSPEEX)

codec_lpc10.so: codec_lpc10.o $(LIBLPC10)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(LIBLPC10) -lm

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

ifneq ($(wildcard .depend),)
  include .depend
endif

install: all
	for x in $(CODECS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
