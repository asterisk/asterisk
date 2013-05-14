#
# Asterisk -- An open source telephony toolkit.
# 
# Makefile for resource modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

MODULE_PREFIX=res
MENUSELECT_CATEGORY=RES
MENUSELECT_DESCRIPTION=Resource Modules

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  # cygwin has some dependencies among res_ things.
  # We use order-only dependencies, and then add the libraries as required.
  res_agi.so: | res_speech.so
  res_agi.so_LIBS:= -lres_speech.so
endif

res_config_ldap.o: _ASTCFLAGS+=-DLDAP_DEPRECATED

ael/ael_lex.o: ael/ael_lex.c ../include/asterisk/ael_structs.h ael/ael.tab.h
ael/ael_lex.o: _ASTCFLAGS+=-I. -Iael -Wno-unused

ael/ael.tab.o: ael/ael.tab.c ael/ael.tab.h ../include/asterisk/ael_structs.h
ael/ael.tab.o: _ASTCFLAGS+=-I. -Iael -DYYENABLE_NLS=0

$(if $(filter res_ais,$(EMBEDDED_MODS)),modules.link,res_ais.so): ais/clm.o ais/evt.o
ais/clm.o ais/evt.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,res_ais)

$(if $(filter res_snmp,$(EMBEDDED_MODS)),modules.link,res_snmp.so): snmp/agent.o
snmp/agent.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,res_snmp)

$(if $(filter res_ael_share,$(EMBEDDED_MODS)),modules.link,res_ael_share.so): ael/ael_lex.o ael/ael.tab.o ael/pval.o
ael/ael_lex.o ael/ael.tab.o ael/pval.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,res_ael_share)

$(if $(filter res_sip,$(EMBEDDED_MODS)),modules.link,res_sip.so): $(subst .c,.o,$(wildcard res_sip/*.c))
$(subst .c,.o,$(wildcard res_sip/*.c)): _ASTCFLAGS+=$(call MOD_ASTCFLAGS,res_sip)

$(if $(filter res_stasis,$(EMBEDDED_MODS)),modules.link,res_stasis.so): $(subst .c,.o,$(wildcard stasis/*.c))
$(subst .c,.o,$(wildcard stasis/*.c)): _ASTCFLAGS+=$(call MOD_ASTCFLAGS,res_stasis)

ifneq ($(findstring REBUILD_PARSERS,$(MENUSELECT_CFLAGS)),)
ael/ael_lex.c: ael/ael.flex
else
ael/ael_lex.c:
endif
	$(ECHO_PREFIX) echo "   [FLEX] $< -> $@"
	$(CMD_PREFIX) (cd ael; $(FLEX) ael.flex)
	$(CMD_PREFIX) sed 's@#if __STDC_VERSION__ >= 199901L@#if !defined __STDC_VERSION__ || __STDC_VERSION__ >= 199901L@' $@ > $@.fix
	$(CMD_PREFIX) echo "#include \"asterisk.h\"" > $@
	$(CMD_PREFIX) echo >> $@
	$(CMD_PREFIX) cat $@.fix >> $@
	$(CMD_PREFIX) rm $@.fix

ifneq ($(findstring REBUILD_PARSERS,$(MENUSELECT_CFLAGS)),)
ael/ael.tab.c ael/ael.tab.h: ael/ael.y
else
ael/ael.tab.c ael/ael.tab.h:
endif
	$(ECHO_PREFIX) echo "   [BISON] $< -> $@"
	$(CMD_PREFIX) (cd ael; $(BISON) -v -d ael.y)

ael/pval.o: ael/pval.c

clean::
	rm -f snmp/*.[oi] ael/*.[oi] ais/*.[oi] stasis_http/*.[oi]
	rm -f res_sip/*.[oi] stasis/*.[oi]

# Dependencies for res_stasis_http_*.so are generated, so they're in this file
include stasis_http.make
