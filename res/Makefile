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

MODS=res_adsi.so res_features.so res_crypto.so res_musiconhold.so res_indications.so res_monitor.so \
     res_agi.so
MODS+=$(shell if [ -f "/usr/include/odbcinst.h" ]; then echo "res_odbc.so res_config_odbc.so"; fi)
MODS+=$(shell if [ -f "/usr/local/include/odbcinst.h" ]; then echo "res_odbc.so res_config_odbc.so"; fi)
MODS+=$(shell if [ -f "/usr/include/osp/osp.h" ]; then echo "res_osp.so"; fi)

CRYPTO_LIBS=-lssl -lcrypto

CFLAGS+=
CFLAGS+=$(shell [ -f /usr/include/linux/zaptel.h ] && echo " -DZAPATA_MOH")
CFLAGS+=$(shell [ -f /usr/local/include/zaptel.h ] && echo " -DZAPATA_MOH")
#
# Work around buggy RedHat 9.0
#
CFLAGS+=-DOPENSSL_NO_KRB5 -fPIC
OSPLIB=/usr/lib/libosp.a

all: depend $(MODS)

install: all
	rm -f $(DESTDIR)$(ASTHEADERDIR)/parking.h
	rm -f $(DESTDIR)$(MODULES_DIR)/app_agi.so 
	rm -f $(DESTDIR)$(MODULES_DIR)/res_parking.so
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

res_crypto.so: res_crypto.o
	$(CC) $(SOLINK) -o $@ $< $(CRYPTO_LIBS)

clean:
	rm -f *.so *.o .depend

res_odbc.so: res_odbc.o
	$(CC) $(SOLINK) -o $@ $< -lodbc

res_osp.so: res_osp.o $(OSPLIB)
	$(CC) $(SOLINK) -o $@ $< $(OSPLIB)

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	@if ! which mpg123 &>/dev/null ; then \
		echo "*** You don't have mpg123 installed. You're going to need ***";\
		echo "***       it if you want MusicOnHold                      ***";\
	elif ! mpg123 --longhelp 2>&1 | grep .59r &>/dev/null ; then \
			echo "*************************************************************";\
			echo "*** You have the WRONG version of mpg123... you need .59r ***";\
			echo "*** Use 'make mpg123' to get the right verison            ***";\
			echo "*************************************************************";\
 	fi
	../mkdep $(CFLAGS) `ls *.c`	

env:
	env
