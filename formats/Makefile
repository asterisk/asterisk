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

FORMAT_LIBS=format_gsm.so format_wav.so \
	format_wav_gsm.so format_vox.so format_pcm.so format_g729.so \
	format_pcm_alaw.so format_h263.so format_g726.so format_ilbc.so \
	format_sln.so
FORMAT_LIBS+=format_jpeg.so
#
# G723 simple frame is depricated
#
#FORMAT_LIBS+=format_g723.so

GSMLIB=../codecs/gsm/lib/libgsm.a

CFLAGS+=-fPIC

all: depend $(FORMAT_LIBS)

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif

format_mp3.so : format_mp3.o
	$(CC) $(SOLINK) -o $@ $< -lm

install: all
	for x in $(FORMAT_LIBS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`
