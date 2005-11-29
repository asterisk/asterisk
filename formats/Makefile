#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for file format modules
#
# Copyright (C) 1999, Adtran Inc. and Linux Support Services, LLC
#
# Mark Spencer <markster@linux-support.net>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

FORMAT_LIBS=format_g723.so format_wav.so format_mp3.so # format_wav_gsm.so

GSMLIB=../codecs/gsm/lib/libgsm.a

CFLAGS+=

all: $(FORMAT_LIBS)

clean:
	rm -f *.so *.o

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

format_wav.so : format_wav.o
	$(CC) -shared -Xlinker -x -o $@ $< -laudiofile

format_wav_gsm.so : format_wav_gsm.o
	$(CC) -shared -Xlinker -x -o $@ $< $(LIBGSM)

install: all
	for x in $(FORMAT_LIBS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done
