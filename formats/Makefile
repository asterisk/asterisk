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

MODS:=$(filter-out $(MENUSELECT_FORMATS),$(patsubst %.c,%.so,$(wildcard format_*.c)))

GSMLIB=../codecs/gsm/lib/libgsm.a

all: depend $(MODS)

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
  include .depend
endif

format_ogg_vorbis.so : format_ogg_vorbis.o
	$(CC) $(SOLINK) -o $@ $< $(OGG_LIB) $(VORBIS_LIB)

format_ogg_vorbis.o: format_ogg_vorbis.c
	$(CC) -c -o $@ $(CFLAGS) $(OGG_INCLUDE) $(VORBIS_INCLUDE) $<

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
