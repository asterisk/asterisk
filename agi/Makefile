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

AGIS=agi-test.agi

CFLAGS+=

all: $(AGIS)

install: all
	for x in $(AGIS); do $(INSTALL) -m 755 $$x $(AGI_DIR) ; done

clean:
	rm -f *.so *.o look

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<
