#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for file format modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS:=$(patsubst %.c,%.so,$(wildcard format_*.c))

MODS:=$(filter-out format_pcm_alaw.so,$(MODS))
MODS:=$(filter-out format_au.so,$(MODS))

# merged.	format_pcm_alaw.so
# merged.	format_au.so
#
# OGG/Vorbis format
#
ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/vorbis/codec.h),)
  MODS:=$(filter-out format_ogg_vorbis.so,$(MODS))
endif

GSMLIB=../codecs/gsm/lib/libgsm.a

ifeq (${OSARCH},CYGWIN)
  CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
  CYGSOLIB=-L.. -L. -lasterisk.dll
else
  CFLAGS+=-fPIC
endif

all: depend $(MODS)

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o

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
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
