#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX frontends (dynamically loaded)
#
# Copyright (C) 1999, Mark Spencer
#
# Mark Spencer <markster@linux-support.net>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

#
# Uncomment if you have g723.1 code (with the same API as the Annex-A code
# and have placed it in the g723.1 directory and/or the Annex-B code in 
# g723.1b)
#
MODG723=codec_g723_1.so codec_g723_1b.so
MODG723=$(shell [ -f g723.1/coder.c ] && echo "codec_g723_1.so")
MODG723+=$(shell [ -f g723.1b/coder2.c ] && echo "codec_g723_1b.so")
MODG729=$(shell [ -f g729cp/codld8cp.c ] && echo "codec_g729.so")
MODG729+=$(shell [ -f g729abc/cod_ld8a.c ] && echo "codec_g729ab.so")
MODG729+=$(shell [ -f g729bc/cod_ld8c.c ] && echo "codec_g729b.so")
MODSPEEX=$(shell [ -f /usr/include/speex.h ] || [ -f /usr/local/include/speex.h ] && echo "codec_speex.so")
MODILBC=$(shell [ -f ilbc/iLBC_decode.h ] && echo "codec_ilbc.so")
CFLAGS+=-fPIC
CFLAGS+=$(shell [ -f /usr/local/include/speex.h ] && echo "-I/usr/local/include")

LIBG723=g723.1/libg723.a
LIBG723B=g723.1b/libg723b.a
LIBG729=g729cp/libg729.a
LIBG729AB=g729abc/libg729ab.a
LIBG729B=g729bc/libg729b.a
LIBGSM=gsm/lib/libgsm.a
LIBGSMT=gsm/lib/libgsm.a
LIBLPC10=lpc10/liblpc10.a
LIBSPEEX=$(shell [ -f /usr/local/lib/libspeex.a ] && echo "-L/usr/local/lib")
LIBSPEEX+=-lspeex -lm
LIBILBC=ilbc/libilbc.a

CODECS+=$(MODG723) $(MODSPEEX) $(MODILBC) $(MODG729) codec_gsm.so codec_lpc10.so  \
        codec_adpcm.so codec_ulaw.so codec_alaw.so codec_a_mu.so \
	codec_g726.so

all: depend $(CODECS)

clean:
	rm -f *.so *.o .depend
	! [ -d g723.1 ] || $(MAKE) -C g723.1 clean
	! [ -d g723.1b ] || $(MAKE) -C g723.1b clean
	! [ -d g729abc ] || $(MAKE) -C g729abc clean
	! [ -d g729bc ] || $(MAKE) -C g729bc clean
	! [ -d g729cp ] || $(MAKE) -C g729cp clean
	$(MAKE) -C gsm clean
	$(MAKE) -C lpc10 clean
	$(MAKE) -C ilbc clean

$(LIBG723):
	$(MAKE) -C g723.1 all

$(LIBG729):
	$(MAKE) -C g729cp all

$(LIBG729B):
	$(MAKE) -C g729bc all

$(LIBG729AB):
	$(MAKE) -C g729abc all

gsm/lib/libgsm.a:
	$(MAKE) -C gsm lib/libgsm.a

$(LIBG723B):
	$(MAKE) -C g723.1b all

$(LIBLPC10):
	$(MAKE) -C lpc10 all

$(LIBILBC):
	$(MAKE) -C ilbc all

codec_ilbc.so: codec_ilbc.o $(LIBILBC)
	$(CC) $(SOLINK) -o $@ $< $(LIBILBC)

codec_g723_1.so : codec_g723_1.o $(LIBG723)
	$(CC) $(SOLINK) -o $@ $< $(LIBG723)

codec_g723_1b.o : codec_g723_1.c
	$(CC) -c -o $@ $(CFLAGS) -DANNEX_B -Dsingle $<

codec_g723_1b.so : codec_g723_1b.o $(LIBG723B)
	$(CC) $(SOLINK) -o $@ $< $(LIBG723B) -lm

codec_g729.so : codec_g729.o $(LIBG729)
	$(CC) $(SOLINK) -o $@ $< $(LIBG729)

codec_g729.o : codec_g729.c
	$(CC) -c -o $@ $(CFLAGS) -Dsingle -DANNEX_ALL $<

codec_g729ab.so : codec_g729ab.o $(LIBG729AB)
	$(CC) $(SOLINK) -o $@ $< $(LIBG729AB)

codec_g729ab.o : codec_g729.c
	$(CC) -c -o $@ $(CFLAGS) -Dsingle -DANNEX_A -DANNEX_B $<

codec_g729b.so : codec_g729b.o $(LIBG729B)
	$(CC) $(SOLINK) -o $@ $< $(LIBG729B)

codec_g729b.o : codec_g729.c
	$(CC) -c -o $@ $(CFLAGS) -Dsingle -DANNEX_B $<

codec_gsm.so: codec_gsm.o $(LIBGSMT) 
	$(CC) $(SOLINK) -o $@ $< $(LIBGSM)

codec_speex.so: codec_speex.o
	$(CC) $(SOLINK) -o $@ $< $(LIBSPEEX)

codec_lpc10.so: codec_lpc10.o $(LIBLPC10)
	$(CC) $(SOLINK) -o $@ $< $(LIBLPC10) -lm

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

include .depend

install: all
	for x in $(CODECS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`
