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

MODS=res_adsi.so res_parking.so res_crypto.so res_musiconhold.so res_indications.so res_monitor.so

CRYPTO_LIBS=-lssl -lcrypto

CFLAGS+=
CFLAGS+=$(shell [ -f /usr/include/linux/zaptel.h ] && echo " -DZAPATA_MOH")
CFLAGS+=$(shell [ -f /usr/local/include/zaptel.h ] && echo " -DZAPATA_MOH")
#
# Work around buggy RedHat 9.0
#
CFLAGS+=-DOPENSSL_NO_KRB5 -fPIC

all: depend $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

res_crypto.so: res_crypto.o
	$(CC) $(SOLINK) -o $@ $< $(CRYPTO_LIBS)

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`

env:
	env
