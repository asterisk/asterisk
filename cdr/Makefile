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

#ADD cdr_pgsql.so to MODS= to include PostgreSQL support: REQUIRES PostgreSQL libs
MODS=cdr_csv.so cdr_manager.so


CFLAGS+=-fPIC

OSARCH=$(shell uname -s)
ifeq (${OSARCH},FreeBSD)
SOLINK+=-L/usr/local/lib
endif

#
# unixODBC stuff...
#
MODS+=$(shell if [ -f "/usr/include/odbcinst.h" ]; then echo "cdr_odbc.so"; fi)
MODS+=$(shell if [ -f "/usr/local/include/odbcinst.h" ]; then echo "cdr_odbc.so"; fi) 

#
# FreeTDS stuff...
#
MODS+=$(shell if [ -f "/usr/include/tds.h" ]; then echo "cdr_tds.so"; fi)
MODS+=$(shell if [ -f "/usr/local/include/tds.h" ]; then echo "cdr_tds.so"; fi)

#
# PGSQL stuff...  Autoconf anyone??
#
MODS+=$(shell if [ -d /usr/local/pgsql/include ] || [ -d /usr/include/pgsql ] || [ -d /usr/local/include/pgsql ] || [ -d /opt/pgsql/include ] || [ -f /usr/include/libpq-fe.h ] ; then echo "cdr_pgsql.so"; fi)
CFLAGS+=$(shell if [ -d /usr/local/pgsql/include ]; then echo "-I/usr/local/pgsql/include"; fi)
CFLAGS+=$(shell if [ -d /usr/include/pgsql ]; then echo "-I/usr/include/pgsql"; fi)
CFLAGS+=$(shell if [ -d /usr/include/postgresql ]; then echo "-I/usr/include/postgresql"; fi)
CFLAGS+=$(shell if [ -d /usr/local/include/pgsql ]; then echo "-I/usr/local/include/pgsql"; fi)
CFLAGS+=$(shell if [ -d /opt/pgsql/include ]; then echo "-I/opt/pgsql/include"; fi)
#CFLAGS+=$(shell if [ -f /usr/include/libpq-fe.h ]; then echo "-I/usr/include"; fi)
MLFLAGS=
MLFLAGS+=$(shell if [ -d /usr/lib/pgsql ]; then echo "-L/usr/lib/pgsql"; fi)
MLFLAGS+=$(shell if [ -d /usr/local/pgsql/lib ]; then echo "-L/usr/local/pgsql/lib"; fi)
MLFLAGS+=$(shell if [ -d /usr/local/lib/pgsql ]; then echo "-L/usr/local/lib/pgsql"; fi)
MLFLAGS+=$(shell if [ -d /opt/pgsql/lib ]; then echo "-L/opt/pgsql/lib"; fi)
MLFLAGS+=$(shell if [ -f /usr/lib/libpq.so ]; then echo "-L/usr/lib"; fi)

#
# SQLIte stuff...
#
MODS+=$(shell if [ -f "/usr/include/sqlite.h" ]; then echo "cdr_sqlite.so"; fi)

all: depend $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif

cdr_odbc.so: cdr_odbc.o
	$(CC) $(SOLINK) -o $@ $< -lodbc $(MLFLAGS)

cdr_tds.so: cdr_tds.o
	$(CC) $(SOLINK) -o $@ $< -ltds $(MLFLAGS)

cdr_pgsql.so: cdr_pgsql.o
	$(CC) $(SOLINK) -o $@ $< -lpq -lz $(MLFLAGS)

cdr_sqlite.so: cdr_sqlite.o
	$(CC) $(SOLINK) -o $@ $< -lsqlite $(MLFLAGS)

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`
