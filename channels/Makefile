#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for Channel backends (dynamically loaded)
#
# Copyright (C) 1999, Mark Spencer
#
# Mark Spencer <markster@linux-support.net>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

CHANNEL_LIBS=chan_vofr.so chan_modem.so \
	     chan_modem_aopen.so chan_iax.so chan_oss.so

CHANNEL_LIBS+=$(shell [ -f /usr/include/linux/ixjuser.h ] && echo chan_phone.so)

CFLAGS+=#-DVOFRDUMPER

all: $(CHANNEL_LIBS)

clean:
	rm -f *.so *.o

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

#chan_modem.so : chan_modem.o
#	$(CC) -rdynamic -shared -Xlinker -x -o $@ $<

install: all
	for x in $(CHANNEL_LIBS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done
