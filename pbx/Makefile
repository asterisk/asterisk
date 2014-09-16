#
# Asterisk -- An open source telephony toolkit.
# 
# Makefile for PBX modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps $(ASTTOPDIR)/makeopts

MODULE_PREFIX=pbx
MENUSELECT_CATEGORY=PBX
MENUSELECT_DESCRIPTION=PBX Modules

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  LIBS+= -lres_ael_share.so -lres_monitor.so
endif

LIBS+=-L/usr/lib/python2.7/config -lpthread -ldl -lutil -lm -lpython2.7 -Xlinker -export-dynamic -Wl,-O1 -Wl,-Bsymbolic-functions

clean::
	rm -f ael/*.o ael/*.i
	rm -f python/*.o python/*.i

dundi-parser.o: dundi-parser.h
dundi-parser.o: _ASTCFLAGS+=-I.

$(if $(filter pbx_python,$(EMBEDDED_MODS)),modules.link,pbx_python.so): $(subst .c,.o,$(wildcard python/*.c))
$(subst .c,.o,$(wildcard python/*.c)): _ASTCFLAGS+=$(call MOD_ASTCFLAGS,pbx_python)

pbx_python.o: _ASTCFLAGS+=-I/usr/include/python2.7 -I/usr/include/python2.7 -fno-strict-aliasing -DNDEBUG -g -fwrapv -O2 -Wall -Wstrict-prototypes -g -fstack-protector --param=ssp-buffer-size=4 -Wformat -Wformat-security -Werror=format-security

pbx_gtkconsole.o: _ASTCFLAGS+=-Wno-strict-prototypes

$(if $(filter pbx_dundi,$(EMBEDDED_MODS)),modules.link,pbx_dundi.so): dundi-parser.o
dundi-parser.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,pbx_dundi)
