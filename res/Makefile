#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for CDR backends (dynamically loaded)
#
# Copyright (C) 1999, Mark Spencer
#
# Mark Spencer <markster@linux-support.net>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS=res_adsi.so res_parking.so res_crypto.so res_musiconhold.so

CRYPTO_LIBS=-lssl -lcrypto

CFLAGS+=
CFLAGS+=$(shell [ -f /usr/include/zap.h ] && echo " -DZAPATA_MOH")

all: $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done

res_crypto.so: res_crypto.o
	$(CC) -shared -Xlinker -x -o $@ $< $(CRYPTO_LIBS)

clean:
	rm -f *.so *.o

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<
