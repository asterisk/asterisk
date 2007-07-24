#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for file format modules
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

FORMAT_LIBS=format_gsm.so format_wav.so \
	format_wav_gsm.so format_vox.so format_pcm.so format_g729.so \
	format_pcm_alaw.so format_h263.so format_g726.so format_ilbc.so \
	format_sln.so format_au.so
FORMAT_LIBS+=format_jpeg.so

#
# G723 simple frame is deprecated
#
FORMAT_LIBS+=format_g723.so

#
# OGG/Vorbis format
#
ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/vorbis/codec.h),)
  FORMAT_LIBS+=format_ogg_vorbis.so
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

GSMLIB=../codecs/gsm/lib/libgsm.a

ifeq (${OSARCH},CYGWIN)
CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
CYGSOLIB=-L.. -L. -lasterisk.dll
else
CFLAGS+=-fPIC
endif
all: depend $(FORMAT_LIBS)

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

ifneq ($(wildcard .depend),)
  include .depend
endif

format_mp3.so : format_mp3.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -lm

format_ogg_vorbis.so : format_ogg_vorbis.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -logg -lvorbis -lvorbisenc -lm

install: all
	for x in $(FORMAT_LIBS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
