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

MODS:=$(filter-out $(MENUSELECT_CODECS),$(patsubst %.c,%.so,$(wildcard codec_*.c)))

ifeq ($(wildcard g723.1/coder.c),)
  MODS:=$(filter-out codec_g723_1.so,$(MODS))
else
  LIBG723=g723.1/libg723.a
endif

ifneq ($(wildcard g723.1b/coder2.c),)
  MODS+=codec_g723_1b.so
  LIBG723B=g723.1b/libg723b.a
endif

ifeq ($(wildcard ilbc/iLBC_decode.h),)
  MODS:=$(filter-out codec_ilbc.so,$(MODS))
else
  LIBILBC=ilbc/libilbc.a
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
	CFLAGS="$(ASTCFLAGS)" $(MAKE) -C g723.1 all

gsm/lib/libgsm.a:
	CFLAGS="$(ASTCFLAGS) -I." $(MAKE) -C gsm lib/libgsm.a

$(LIBG723B):
	CFLAGS="$(ASTCFLAGS)" $(MAKE) -C g723.1b all

$(LIBLPC10):
	CFLAGS="$(ASTCFLAGS)" $(MAKE) -C lpc10 all

$(LIBILBC):
	CFLAGS="$(ASTCFLAGS)" $(MAKE) -C ilbc all

codec_ilbc.so: codec_ilbc.o $(LIBILBC)
	$(CC) $(SOLINK) -o $@ $< $(LIBILBC)

codec_g723_1.so : codec_g723_1.o $(LIBG723)
	$(CC) $(SOLINK) -o $@ $< $(LIBG723)

codec_g723_1b.o : codec_g723_1.c
	$(CC) -c -o $@ $(CFLAGS) -DANNEX_B -Dsingle $<

codec_g723_1b.so : codec_g723_1b.o $(LIBG723B)
	$(CC) $(SOLINK) -o $@ $< $(LIBG723B) -lm

ifeq ($(GSM_LIB),internal)
codec_gsm.o: codec_gsm.c
	$(CC) -c -o $@ $(CFLAGS) -Igsm/inc $<

codec_gsm.so: codec_gsm.o gsm/lib/libgsm.a
	$(CC) $(SOLINK) -o $@ $< gsm/lib/libgsm.a
else
codec_gsm.o: codec_gsm.c
	$(CC) -c -o $@ $(CFLAGS) $(GSM_INCLUDE) $<

codec_gsm.so: codec_gsm.o
	$(CC) $(SOLINK) -o $@ $< $(GSM_LIB)
endif

codec_speex.o: codec_speex.c
	$(CC) -c -o $@ $(CFLAGS) $(SPEEX_INCLUDE) $<

codec_speex.so: codec_speex.o
	$(CC) $(SOLINK) -o $@ $< $(SPEEX_LIB)

codec_lpc10.so: codec_lpc10.o $(LIBLPC10)
	$(CC) $(SOLINK) -o $@ $< $(LIBLPC10) -lm

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
  include .depend
endif

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
