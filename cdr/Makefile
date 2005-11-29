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

CFLAGS+=

all: $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done

clean:
	rm -f *.so *.o

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<
