#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for CDR backends (dynamically loaded)
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

#ADD cdr_pgsql.so to MODS= to include PostgreSQL support: REQUIRES PostgreSQL libs
MODS=cdr_csv.so cdr_manager.so cdr_custom.so


CFLAGS+=-fPIC

PROC=$(shell uname -m)
OSARCH=$(shell uname -s)

ifeq (${OSARCH},FreeBSD)
SOLINK+=-L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

#The problem with sparc is the best stuff is in newer versions of gcc (post 3.0) only.
#This works for even old (2.96) versions of gcc and provides a small boost either way.
#A ultrasparc cpu is really v9 but the stock debian stable 3.0 gcc doesn.t support it.
#So we go lowest common available by gcc and go a step down, still a step up from
#the default as we now have a better instruction set to work with. - Belgarath
ifeq ($(PROC),sparc64)
PROC=ultrasparc
CFLAGS += -mtune=$(PROC) -O3 -pipe -fomit-frame-pointer -mcpu=v8
endif

#
# unixODBC stuff...
#
MODS+=$(shell if [ -f "$(CROSS_COMPILE_TARGET)/usr/include/odbcinst.h" ]; then echo "cdr_odbc.so"; fi)
MODS+=$(shell if [ -f "$(CROSS_COMPILE_TARGET)/usr/local/include/odbcinst.h" ]; then echo "cdr_odbc.so"; fi) 

#
# FreeTDS stuff...
#
MODS+=$(shell if [ -f "$(CROSS_COMPILE_TARGET)/usr/include/tds.h" ]; then echo "cdr_tds.so"; fi)
MODS+=$(shell if [ -f "$(CROSS_COMPILE_TARGET)/usr/local/include/tds.h" ]; then echo "cdr_tds.so"; fi)

#
# PGSQL stuff...  Autoconf anyone??
#
MODS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/usr/local/pgsql/include ] || [ -d $(CROSS_COMPILE_TARGET)/usr/include/pgsql ] || [ -d $(CROSS_COMPILE_TARGET)/usr/local/include/pgsql ] || [ -d $(CROSS_COMPILE_TARGET)/opt/pgsql/include ] || [ -f $(CROSS_COMPILE_TARGET)/usr/include/libpq-fe.h ] ; then echo "cdr_pgsql.so"; fi)
CFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/usr/local/pgsql/include ]; then echo "-I$(CROSS_COMPILE_TARGET)/usr/local/pgsql/include"; fi)
CFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/usr/include/pgsql ]; then echo "-I$(CROSS_COMPILE_TARGET)/usr/include/pgsql"; fi)
CFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/usr/include/postgresql ]; then echo "-I$(CROSS_COMPILE_TARGET)/usr/include/postgresql"; fi)
CFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/usr/local/include/pgsql ]; then echo "-I$(CROSS_COMPILE_TARGET)/usr/local/include/pgsql"; fi)
CFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/opt/pgsql/include ]; then echo "-I$(CROSS_COMPILE_TARGET)/opt/pgsql/include"; fi)
#CFLAGS+=$(shell if [ -f $(CROSS_COMPILE_TARGET)/usr/include/libpq-fe.h ]; then echo "-I$(CROSS_COMPILE_TARGET)/usr/include"; fi)
MLFLAGS=
MLFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/usr/lib/pgsql ]; then echo "-L$(CROSS_COMPILE_TARGET)/usr/lib/pgsql"; fi)
MLFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/usr/local/pgsql/lib ]; then echo "-L$(CROSS_COMPILE_TARGET)/usr/local/pgsql/lib"; fi)
MLFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/usr/local/lib/pgsql ]; then echo "-L$(CROSS_COMPILE_TARGET)/usr/local/lib/pgsql"; fi)
MLFLAGS+=$(shell if [ -d $(CROSS_COMPILE_TARGET)/opt/pgsql/lib ]; then echo "-L$(CROSS_COMPILE_TARGET)/opt/pgsql/lib"; fi)
MLFLAGS+=$(shell if [ -f $(CROSS_COMPILE_TARGET)/usr/lib/libpq.so ]; then echo "-L$(CROSS_COMPILE_TARGET)/usr/lib"; fi)

#
# SQLIte stuff...
#
MODS+=$(shell if [ -f "$(CROSS_COMPILE_TARGET)/usr/include/sqlite.h" ]; then echo "cdr_sqlite.so"; fi)

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
	../build_tools/mkdep $(CFLAGS) `ls *.c`
