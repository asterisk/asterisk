#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for resource modules
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS=res_adsi.so res_features.so res_crypto.so res_musiconhold.so res_indications.so res_monitor.so \
     res_agi.so

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/odbcinst.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/odbcinst.h),)
  ifneq (${OSARCH},FreeBSD)
    MODS+=res_config_odbc.so
  else
    MODS+=$(shell if test ${BSDVERSION} -ge 500000 ; then echo "res_config_odbc.so"; fi)
  endif
  MODS+=res_odbc.so
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/osp/osp.h),)
  MODS+=res_osp.so
  OSPLIB=$(CROSS_COMPILE_TARGET)/usr/local/lib/libosptk.a
else
  ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/osp/osp.h),)
    MODS+=res_osp.so
    OSPLIB=$(CROSS_COMPILE_TARGET)/usr/lib/libosp.a
  endif
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

CRYPTO_LIBS=-lssl -lcrypto

CFLAGS+=

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/zaptel.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/zaptel.h),)
  CFLAGS+=-DZAPATA_MOH
endif

#
# Work around buggy RedHat 9.0
#
CFLAGS+=-DOPENSSL_NO_KRB5 -fPIC

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
	$(CC) $(SOLINK) -o $@ $^

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend:
	@if [ x`which mpg123 2>/dev/null | grep -v '^no'` != x ] ; then \
	  if mpg123 --longhelp 2>&1 | grep -q .59r 2>&1 >/dev/null ; then echo ; else \
			echo "*************************************************************";\
			echo "*** You have the WRONG version of mpg123... you need .59r ***";\
			echo "*** Use 'make mpg123' to get the right verison            ***";\
			echo "*************************************************************";\
	  fi ;\
	else \
		echo "*** You don't have mpg123 installed. You're going to need ***";\
		echo "***       it if you want MusicOnHold                      ***";\
	fi
	../build_tools/mkdep $(CFLAGS) `ls *.c`	

env:
	env
