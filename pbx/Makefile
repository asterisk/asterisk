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



PBX_LIBS=pbx_config.so pbx_wilcalu.so             # pbx_gtkconsole.so pbx_kdeconsole.so

# Add GTK console if appropriate
PBX_LIBS+=$(shell gtk-config --cflags >/dev/null 2>/dev/null && echo "pbx_gtkconsole.so")
# Add KDE Console if appropriate
#PBX_LIBS+=$(shell [ "$$QTDIR" != "" ] && echo "pbx_kdeconsole.so")


GTK_FLAGS=`gtk-config --cflags gthread`
GTK_LIBS=`gtk-config --libs gthread`
#CXX=egcs
MOC=$(QTDIR)/bin/moc
KDE_FLAGS=-I$(KDEDIR)/include -I$(KDEDIR)/include/kde -I$(QTDIR)/include
KDE_LIBS=-L$(KDEDIR)/lib -L$(QTDIR)/lib -lqt -lkdecore -lkdeui
CFLAGS+=

KDE_CONSOLE_OBJS=pbx_kdeconsole_main.o pbx_kdeconsole.o 

all: $(PBX_LIBS)

clean:
	rm -f *.so *.o

pbx_gtkconsole.o: pbx_gtkconsole.c
	$(CC) $(CFLAGS) $(GTK_FLAGS) -c -o $@ $<

pbx_gtkconsole.so: pbx_gtkconsole.o
	$(CC) -shared -Xlinker -x -o $@ $< $(GTK_LIBS)

pbx_kdeconsole.o: pbx_kdeconsole.cc pbx_kdeconsole.moc
	$(CXX) $(CFLAGS) $(KDE_FLAGS) -c -o $@ $<

pbx_kdeconsole_main.o: pbx_kdeconsole_main.cc pbx_kdeconsole.h
	$(CXX) $(CFLAGS) $(KDE_FLAGS) -c -o $@ $<

pbx_kdeconsole.so: $(KDE_CONSOLE_OBJS)
	$(CC) -shared -Xlinker -x -o $@ $(KDE_CONSOLE_OBJS) $(KDE_LIBS)

%.moc : %.h
	$(MOC) $< -o $@

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

install: all
	for x in $(PBX_LIBS); do $(INSTALL) -m 755 $$x $(MODULES_DIR) ; done
