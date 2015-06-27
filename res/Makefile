#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for resource modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps $(ASTTOPDIR)/build_tools/menuselect-deps

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
	ifneq ($(AST_CLANG_BLOCKS),)
		_ASTCFLAGS+=-Wno-parentheses-equality
	endif

$(if $(filter res_ais,$(EMBEDDED_MODS)),modules.link,res_ais.so): ais/clm.o ais/evt.o
ais/clm.o ais/evt.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,res_ais)

$(if $(filter res_snmp,$(EMBEDDED_MODS)),modules.link,res_snmp.so): snmp/agent.o
snmp/agent.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,res_snmp)

$(if $(filter res_ael_share,$(EMBEDDED_MODS)),modules.link,res_ael_share.so): ael/ael_lex.o ael/ael.tab.o ael/pval.o
ael/ael_lex.o ael/ael.tab.o ael/pval.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,res_ael_share)

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
	@if [ -f pjproject/build.mak ]; then $(MAKE) -C pjproject realclean; fi
	rm -f snmp/*.o snmp/*.i ael/*.o ael/*.i ais/*.o ais/*.i snmp/*.gcda snmp/*.gcno ael/*.gcda ael/*.gcno

dist-clean:: distclean
distclean::
	rm -f pjproject/build.mak

pjproject/build.mak: pjproject/aconfigure
	(cd pjproject && CFLAGS="-fPIC" ./configure --build=$(BUILD_PLATFORM) --host=$(HOST_PLATFORM) --disable-floating-point --disable-sound --disable-oss --disable-speex-aec --disable-l16-codec --disable-gsm-codec --disable-g722-codec --disable-g7221-codec --disable-speex-codec --disable-ilbc-codec --disable-g711-codec)

ifneq ($(findstring $(MAKECMDGOALS),all),)
-include pjproject/build.mak
endif

.PHONY: FORCE
FORCE:

$(PJ_LIB_FILES): FORCE
	$(MAKE) -C $(patsubst %/lib/,%,$(dir $@))/build/ ../lib/$(notdir $@)

ifeq ($(subst 1:0,1,$(UUID)), 1)
res_rtp_asterisk.o: $(PJ_LIB_FILES)
res_rtp_asterisk.o: _ASTCFLAGS+=-DUSE_PJPROJECT
res_rtp_asterisk.o: _ASTCFLAGS+=$(PJ_CFLAGS)
res_rtp_asterisk.so: _ASTLDFLAGS+=$(PJ_LDFLAGS)
res_rtp_asterisk.so: LIBS+=$(PJ_LDLIBS)
endif

