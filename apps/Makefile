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

APPS=app_dial.so app_playback.so app_voicemail.so app_directory.so app_intercom.so app_mp3.so

CFLAGS+=

all: $(APPS)

clean:
	rm -f *.so *.o

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

install: all
	for x in $(APPS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done
