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

USE_MYSQL_VM_INTERFACE=0
USE_POSTGRES_VM_INTERFACE=0

#APPS=app_dial.so app_playback.so app_directory.so app_intercom.so app_mp3.so 
APPS=app_dial.so app_playback.so app_voicemail.so app_directory.so app_mp3.so\
     app_system.so app_echo.so app_record.so app_image.so app_url.so app_disa.so \
     app_agi.so app_qcall.so app_adsiprog.so app_getcpeid.so app_milliwatt.so \
     app_zapateller.so app_setcallerid.so app_festival.so \
     app_queue.so app_senddtmf.so app_parkandannounce.so app_striplsd.so \
     app_setcidname.so app_lookupcidname.so app_substring.so app_macro.so \
     app_authenticate.so app_softhangup.so app_lookupblacklist.so \
     app_waitforring.so app_privacy.so app_db.so app_chanisavail.so \
     app_enumlookup.so app_transfer.so app_setcidnum.so app_cdr.so \
     app_hasnewvoicemail.so app_sayunixtime.so app_cut.so app_read.so \
     app_setcdruserfield.so app_random.so app_ices.so app_eval.so \
     app_nbscat.so app_sendtext.so app_exec.so app_sms.so \
     app_groupcount.so app_txtcidname.so app_controlplayback.so \
	 app_talkdetect.so

ifneq (${OSARCH},Darwin)
APPS+=app_intercom.so
endif

#APPS+=app_sql_postgres.so
#APPS+=app_sql_odbc.so
#APPS+=app_rpt.so

APPS+=$(shell if [ -f /usr/include/linux/zaptel.h ]; then echo "app_zapras.so app_meetme.so app_flash.so app_zapbarge.so app_zapscan.so" ; fi)
APPS+=$(shell if [ -f /usr/local/include/zaptel.h ]; then echo "app_zapras.so app_meetme.so app_flash.so app_zapbarge.so app_zapscan.so" ; fi)

CFLAGS+=-fPIC

ifeq ($(USE_POSTGRES_VM_INTERFACE),1)
CFLAGS+=-DUSEPOSTGRESVM
endif

ifeq ($(USE_MYSQL_VM_INTERFACE),1)
CFLAGS+=-DUSEMYSQLVM
endif

all: $(APPS)

clean:
	rm -f *.so *.o look .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

app_rpt.so : app_rpt.o
	$(CC) $(SOLINK) -o $@ $< -ltonezone

install: all
	for x in $(APPS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	rm -f $(DESTDIR)$(MODULES_DIR)/app_datetime.so

app_voicemail.so : app_voicemail.o
ifeq ($(USE_MYSQL_VM_INTERFACE),1)
	$(CC) $(SOLINK) -o $@ $(MLFLAGS) $< -L/usr/lib/mysql -lmysqlclient -lz
else
ifeq ($(USE_POSTGRES_VM_INTERFACE),1)
	$(CC) $(SOLINK) -o $@ $(MLFLAGS) $< -lpq
else
	$(CC) $(SOLINK) -o $@ $(MLFLAGS) $< 
endif
endif

app_sql_postgres.o: app_sql_postgres.c
	$(CC) -pipe -I/usr/local/pgsql/include $(CFLAGS) -c -o app_sql_postgres.o app_sql_postgres.c

app_sql_postgres.so: app_sql_postgres.o
	$(CC) $(SOLINK) -o $@ $< -L/usr/local/pgsql/lib -lpq

app_sql_odbc.so: app_sql_odbc.o
	$(CC) $(SOLINK) -o $@ $< -lodbc

look:	look.c
	gcc -pipe -O6 -g look.c -o look -lncurses

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`

env:
	env
