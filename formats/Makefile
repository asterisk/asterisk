#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for file format modules
#
# Copyright (C) 1999, Mark Spencer
#
# Mark Spencer <markster@linux-support.net>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

FORMAT_LIBS=format_g723.so format_wav.so format_mp3.so format_wav_gsm.so format_gsm.so format_vox.so format_pcm.so format_g729.so
FORMAT_LIBS+=format_jpeg.so

GSMLIB=../codecs/gsm/lib/libgsm.a

CFLAGS+=

all: $(FORMAT_LIBS)

clean:
	rm -f *.so *.o

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

format_mp3.so : format_mp3.o
	$(CC) -shared -Xlinker -x -o $@ $< -lm

install: all
	for x in $(FORMAT_LIBS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done
