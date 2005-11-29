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
#MODG723=codec_g723_1.so codec_g723_1b.so
MODG723=$(shell [ -f g723.1/coder.c ] && echo "codec_g723_1.so")
MODG723+=$(shell [ -f g723.1b/coder2.c ] && echo "codec_g723_1b.so")
MODSPEEX=$(shell [ -f /usr/include/speex.h ] || [ -f /usr/local/include/speex.h ] && echo "codec_speex.so")
CFLAGS+=

LIBG723=g723.1/libg723.a
LIBG723B=g723.1b/libg723b.a
LIBGSM=gsm/lib/libgsm.a
LIBGSM=$(shell if uname -m | grep -q 86; then echo gsm/lib/libgsm.a; else echo "-lgsm" ; fi)
LIBGSMT=$(shell if uname -m | grep -q 86; then echo gsm/lib/libgsm.a; fi)
LIBMP3=mp3/libmp3.a
LIBLPC10=lpc10/liblpc10.a
LIBSPEEX=-lspeex -lm

CODECS+=$(MODG723) $(MODSPEEX) codec_gsm.so codec_mp3_d.so codec_lpc10.so  \
        codec_adpcm.so codec_ulaw.so codec_alaw.so codec_a_mu.so

all: $(CODECS)

clean:
	rm -f *.so *.o
	! [ -d g723.1 ] || make -C g723.1 clean
	! [ -d g723.1b ] || make -C g723.1b clean
	make -C gsm clean
	make -C mp3 clean
	make -C lpc10 clean

$(LIBG723):
	make -C g723.1 all

gsm/lib/libgsm.a:
	make -C gsm lib/libgsm.a

$(LIBG723B):
	make -C g723.1b all

$(LIBMP3):
	make -C mp3 all

$(LIBLPC10):
	make -C lpc10 all

codec_g723_1.so : codec_g723_1.o $(LIBG723)
	$(CC) -shared -Xlinker -x -o $@ $< $(LIBG723)

codec_g723_1b.o : codec_g723_1.c
	$(CC) -c -o $@ $(CFLAGS) -DANNEX_B -Dsingle $<

codec_g723_1b.so : codec_g723_1b.o $(LIBG723B)
	$(CC) -shared -Xlinker -x -o $@ $< $(LIBG723B) -lm

codec_gsm.so: codec_gsm.o $(LIBGSMT) 
	$(CC) -shared -Xlinker -x -o $@ $< $(LIBGSM)

codec_speex.so: codec_speex.o
	$(CC) -shared -Xlinker -x -o $@ $< $(LIBSPEEX)

codec_lpc10.so: codec_lpc10.o $(LIBLPC10)
	$(CC) -shared -Xlinker -x -o $@ $< $(LIBLPC10) -lm

codec_mp3_d.so: codec_mp3_d.o $(LIBMP3)
	$(CC) -lm -shared -Xlinker -x -o $@ $< $(LIBMP3)

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

install: all
	for x in $(CODECS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done
