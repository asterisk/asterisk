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

APPS=app_dial.so app_playback.so app_voicemail.so app_directory.so app_intercom.so app_mp3.so \
     app_system.so app_echo.so app_record.so

CFLAGS+=

all: $(APPS)

clean:
	rm -f *.so *.o look

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

install: all
	for x in $(APPS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done

app_todd.o: app_todd.c
	gcc -pipe -O6 -g  -Iinclude -I../include -D_REENTRANT -march=i586 -DDO_CRASH -DDEBUG_THREADS -c -o  app_todd.o app_todd.c

app_todd.so: app_todd.o
	$(CC) -shared -Xlinker -x -o $@ $< -L/usr/local/ssl/lib -lssl -lcrypto


look:	look.c
	gcc -pipe -O6 -g look.c -o look -lncurses
