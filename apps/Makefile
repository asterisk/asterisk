#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX applications
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS:=$(patsubst %.c,%.so,$(wildcard app_*.c))

#
# Experimental things
#
MODS:=$(filter-out app_ivrdemo.so,$(MODS))
MODS:=$(filter-out app_skel.so,$(MODS))
MODS:=$(filter-out app_rpt.so,$(MODS))

ifndef WITHOUT_ZAPTEL
ZAPAVAIL:=$(wildcard $(CROSS_COMPILE_TARGET)/usr/include/linux/zaptel.h $(CROSS_COMPILE_TARGET)/usr/local/include/zaptel.h)
endif

ifeq (${ZAPAVAIL},)
  MODS:=$(filter-out app_zapras.so app_meetme.so app_flash.so app_zapbarge.so app_zapscan.so app_page.so,$(MODS))
endif

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/osp/osp.h $(CROSS_COMPILE_TARGET)/usr/include/osp/osp.h),)
  MODS:=$(filter-out app_osplookup.so,$(MODS))
endif

ifneq (${WITH_SMDI},)
  CFLAGS+=-DWITH_SMDI
endif

ifeq (${OSARCH},CYGWIN)
  CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
  CYGSOLIB=-L.. -L. -L../res -lasterisk.dll -lres_features.so -lres_adsi.so -lres_monitor.so
  MODS:=$(filter-out app_sms.so,$(MODS))
else
  CFLAGS+=-fPIC
endif

# If you have UnixODBC you can use ODBC voicemail
# storage
#
# Uncomment to use ODBC storage
#CFLAGS+=-DUSE_ODBC_STORAGE
# Uncomment for extended ODBC voicemail storage
#CFLAGS+=-DEXTENDED_ODBC_STORAGE
# See doc/README.odbcstorage for more information

all: $(MODS)

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o look

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

app_rpt.so : app_rpt.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -ltonezone

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	rm -f $(DESTDIR)$(MODULES_DIR)/app_cut.so
	rm -f $(DESTDIR)$(MODULES_DIR)/app_datetime.so
	rm -f $(DESTDIR)$(MODULES_DIR)/app_qcall.so

uninstall:

look:	look.c
	$(CC) -pipe -O6 -g look.c -o look -lncurses

ifeq (SunOS,$(shell uname))
app_chanspy.so: app_chanspy.o
	$(CC) $(SOLINK) -o $@ $< -lrt
endif


ifneq ($(wildcard .depend),)
 include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

env:
	env
