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

#APPS=app_dial.so app_playback.so app_directory.so app_intercom.so app_mp3.so 
APPS=app_dial.so app_playback.so app_voicemail.so app_directory.so app_intercom.so app_mp3.so \
     app_system.so app_echo.so app_record.so app_image.so app_url.so app_disa.so \
     app_agi.so app_qcall.so app_adsiprog.so app_getcpeid.so app_milliwatt.so \
     app_zapateller.so app_datetime.so app_setcallerid.so app_festival.so \
     app_queue.so app_senddtmf.so app_parkandannounce.so app_striplsd.so \
     app_setcidname.so app_lookupcidname.so app_substring.so app_macro.so \
     app_authenticate.so app_softhangup.so app_lookupblacklist.so \
     app_waitforring.so app_privacy.so app_db.so app_chanisavail.so

#APPS+=app_sql_postgres.so
#APPS+=app_sql_odbc.so

APPS+=$(shell if [ -f /usr/include/linux/zaptel.h ]; then echo "app_zapras.so app_meetme.so app_flash.so app_zapbarge.so" ; fi)
#APPS+=$(shell if [ -f /usr/include/zap.h ]; then echo "app_rpt.so" ; fi)

CFLAGS+=

all: $(APPS)

clean:
	rm -f *.so *.o look

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

app_rpt.so : app_rpt.o
	$(CC) -shared -Xlinker -x -o $@ $< -ltonezone

install: all
	for x in $(APPS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done

app_todd.o: app_todd.c
	gcc -pipe -O6 -g  -Iinclude -I../include -D_REENTRANT -march=i586 -DDO_CRASH -c -o  app_todd.o app_todd.c

app_todd.so: app_todd.o
	$(CC) -shared -Xlinker -x -o $@ $< -L/usr/local/ssl/lib -lssl -lcrypto

app_sql_postgres.o: app_sql_postgres.c
	$(CC) -pipe -I/usr/local/pgsql/include -Wall -Wmissing-prototypes -Wmissing-declarations -O6 -g  -Iinclude -I../include -D_REENTRANT -D_GNU_SOURCE -march=i686  -DASTERISK_VERSION=\"CVS-07/21/02-14:49:14\" -DDO_CRASH -DDEBUG_THREADS     -c -o app_sql_postgres.o app_sql_postgres.c

app_sql_postgres.so: app_sql_postgres.o
	$(CC) -shared -Xlinker -x -o $@ $< -L/usr/local/pgsql/lib -lpq

app_sql_odbc.so: app_sql_odbc.o
	$(CC) -shared -Xlinker -x -o $@ $< -lodbc

look:	look.c
	gcc -pipe -O6 -g look.c -o look -lncurses
