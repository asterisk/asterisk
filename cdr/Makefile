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

MODS=cdr_csv.so

CFLAGS+=-fPIC

#
# MySQL stuff...  Autoconf anyone??
#
MODS+=$(shell if [ -d /usr/local/mysql/include ] || [ -d /usr/include/mysql ] || [ -d /usr/local/include/mysql ] || [ -d /opt/mysql/include ]; then echo "cdr_mysql.so"; fi)
CFLAGS+=$(shell if [ -d /usr/local/mysql/include ]; then echo "-I/usr/local/mysql/include"; fi)
CFLAGS+=$(shell if [ -d /usr/include/mysql ]; then echo "-I/usr/include/mysql"; fi)
CFLAGS+=$(shell if [ -d /usr/local/include/mysql ]; then echo "-I/usr/local/include/mysql"; fi)
CFLAGS+=$(shell if [ -d /opt/mysql/include/mysql ]; then echo "-I/opt/mysql/include/mysql"; fi)
MLFLAGS=
MLFLAGS+=$(shell if [ -d /usr/lib/mysql ]; then echo "-L/usr/lib/mysql"; fi)
MLFLAGS+=$(shell if [ -d /usr/local/mysql/lib ]; then echo "-L/usr/local/mysql/lib"; fi)
MLFLAGS+=$(shell if [ -d /usr/local/lib/mysql ]; then echo "-L/usr/local/lib/mysql"; fi)
MLFLAGS+=$(shell if [ -d /opt/mysql/lib/mysql ]; then echo "-L/opt/mysql/lib/mysql"; fi)

all: depend $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif

cdr_mysql.so: cdr_mysql.o
	$(CC) -shared -Xlinker -x -o $@ $< -lmysqlclient -lz $(MLFLAGS)

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`
