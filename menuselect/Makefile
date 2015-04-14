#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for Menuselect
#
# Copyright (C) 2005-2008, Digium, Inc.
#
# Russell Bryant <russell@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

# even though we could use '-include makeopts' here, use a wildcard
# lookup anyway, so that make won't try to build makeopts if it doesn't
# exist (other rules will force it to be built if needed)
ifneq ($(wildcard makeopts),)
  include makeopts
endif

.PHONY: clean dist-clean distclean test ntest ctest gtest

# Basic set of sources and flags/libraries/includes
OBJS:=menuselect.o strcompat.o
CFLAGS+=-g -D_GNU_SOURCE -Wall

ifeq ($(MENUSELECT_DEBUG),yes)
  CFLAGS += -DMENUSELECT_DEBUG
endif

ifdef NCURSES_LIB
  C_OBJS += menuselect_curses.o
  C_LIBS +=$(NCURSES_LIB)
  C_INCLUDE += $(NCURSES_INCLUDE)
  ALL_TGTS += cmenuselect
else
  ifdef CURSES_LIB
    C_OBJS += menuselect_curses.o
    C_LIBS +=$(CURSES_LIB)
    C_INCLUDE += $(CURSES_INCLUDE)
    ALL_TGTS += cmenuselect
  endif
endif

ifdef GTK2_LIB
  G_OBJS += menuselect_gtk.o
  G_LIBS += $(GTK2_LIB)
  G_INCLUDE += $(GTK2_INCLUDE)
  ALL_TGTS += gmenuselect
endif

ifdef NEWT_LIB
  N_OBJS += menuselect_newt.o
  N_LIBS += $(NEWT_LIB)
  N_INCLUDE += $(NEWT_INCLUDE)
  ALL_TGTS += nmenuselect
endif

M_OBJS += menuselect_stub.o
M_LIBS += $(LIBXML2_LIB)
ALL_TGTS += menuselect

all: $(ALL_TGTS)

$(OBJS) $(C_OBJS) $(N_OBJS) $(G_OBJS) $(M_OBJS): autoconfig.h menuselect.h

makeopts autoconfig.h: autoconfig.h.in makeopts.in
	@./configure $(CONFIGURE_SILENT)
	@echo "****"
	@echo "**** The configure script was just executed, so 'make' needs to be"
	@echo "**** restarted."
	@echo "****"
	@exit 1

ifdef C_OBJS
menuselect_curses.o: CFLAGS+=$(C_INCLUDE)
cmenuselect: $(OBJS) $(C_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(C_LIBS) $(LIBXML2_LIB)
else
cmenuselect:
endif

ifdef G_OBJS
menuselect_gtk.o: CFLAGS+=$(G_INCLUDE)
gmenuselect: $(OBJS) $(G_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(G_LIBS) $(LIBXML2_LIB)
else
gmenuselect:
endif

ifdef N_OBJS
menuselect_newt.o: CFLAGS+=$(N_INCLUDE)
nmenuselect: $(OBJS) $(N_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(N_LIBS) $(LIBXML2_LIB)
else
nmenuselect:
endif

menuselect.o: CFLAGS+=$(LIBXML2_INCLUDE)

menuselect: $(OBJS) $(M_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(M_LIBS)

test: menuselect
	(cd test; ../$< menuselect.makeopts)

ctest: cmenuselect
	(cd test; ../$< menuselect.makeopts)

gtest: gmenuselect
	(cd test; ../$< menuselect.makeopts)

ntest: nmenuselect
	(cd test; ../$< menuselect.makeopts)

clean:
	rm -f menuselect cmenuselect gmenuselect nmenuselect $(OBJS) $(M_OBJS) $(C_OBJS) $(G_OBJS) $(N_OBJS)

dist-clean: distclean

distclean: clean
	rm -f autoconfig.h config.status config.log makeopts
	rm -rf autom4te.cache
