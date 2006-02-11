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

MODS:=$(patsubst %.c,%.so,$(wildcard res_*.c))

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/odbcinst.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/odbcinst.h),)
  MODS:=$(filter-out res_odbc.so,$(MODS))
  MODS:=$(filter-out res_config_odbc.so,$(MODS))
else
  ifeq (${OSARCH},FreeBSD)
    MODS:=$(filter-out $(shell if test ${BSDVERSION} -lt 500000 ; then echo "res_config_odbc.so"; fi),$(MODS))
  endif
endif

ifneq ($(NOCRYPTO),yes)
  MODS:=$(filter-out res_crypto.so,$(MODS))
endif

OSPLIB=$(wildcard $(CROSS_COMPILE_TARGET)/usr/lib/libosptk.a)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/lib/libosptk.a)
ifeq (${OSPLIB},)
  MODS:=$(filter-out res_osp.so,$(MODS))
endif

ifeq (${WITH_SMDI},)
  MODS:=$(filter-out res_smdi.so,$(MODS))
endif

ifeq (${OSARCH},CYGWIN)
  CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
  CYGSOLIB=-L.. -L. -lasterisk.dll
  CYG_RES_CONFIG_ODBC_LIB=-lres_odbc.so
  CYG_RES_FEATURES_LIB=-lres_adsi.so -lres_monitor.so
  MODS:=$(filter-out res_musiconhold.so,$(MODS))
endif

CRYPTO_LIBS=-lssl -lcrypto

CFLAGS+=

ifndef WITHOUT_ZAPTEL
ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/zaptel.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/zaptel.h),)
  CFLAGS+=-DZAPATA_MOH
endif
endif # WITHOUT_ZAPTEL

#
# Work around buggy RedHat 9.0
#
ifeq (${OSARCH},CYGWIN)
CFLAGS+=-DOPENSSL_NO_KRB5
else
CFLAGS+=-DOPENSSL_NO_KRB5 -fPIC
endif

all: depend $(MODS)

install: all
	rm -f $(DESTDIR)$(ASTHEADERDIR)/parking.h
	rm -f $(DESTDIR)$(MODULES_DIR)/app_agi.so 
	rm -f $(DESTDIR)$(MODULES_DIR)/res_parking.so
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	@if [ x`which mpg123 2>/dev/null | grep -v '^no'` != x ] ; then \
	  if mpg123 --longhelp 2>&1 | grep -q .59r 2>&1 >/dev/null ; then echo ; else \
			echo "*************************************************************";\
			echo "*** You have the WRONG version of mpg123... you need .59r ***";\
			echo "*** Use 'make mpg123' to get the right verison            ***";\
			echo "*************************************************************";\
	  fi ;\
	else \
		echo "*** You don't have mpg123 installed. You may need ***";\
		echo "***       it if you want to use MusicOnHold       ***";\
	fi

uninstall:

res_crypto.so: res_crypto.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(CRYPTO_LIBS)

clean:
	rm -f *.so *.o .depend

res_odbc.so: res_odbc.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -lodbc

res_osp.so: res_osp.o $(OSPLIB)
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(OSPLIB)

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

res_features.so: res_features.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} ${CYG_RES_FEATURES_LIB}

res_config_odbc.so: res_config_odbc.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} ${CYG_RES_CONFIG_ODBC_LIB}

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`	

env:
	env
