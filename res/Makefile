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
ael/ael_lex.o: _ASTCFLAGS+=-I. -Iael -Wno-unused $(AST_NO_FORMAT_TRUNCATION)

ael/ael.tab.o: ael/ael.tab.c ael/ael.tab.h ../include/asterisk/ael_structs.h
ael/ael.tab.o: _ASTCFLAGS+=-I. -Iael -DYYENABLE_NLS=0
	ifneq ($(AST_CLANG_BLOCKS),)
		_ASTCFLAGS+=-Wno-parentheses-equality
	endif

ifneq ($(findstring REBUILD_PARSERS,$(MENUSELECT_CFLAGS)),)
ael/ael_lex.c: ael/ael.flex
else
ael/ael_lex.c:
endif
	$(ECHO_PREFIX) echo "   [FLEX] $< -> $@"
	$(CMD_PREFIX) (cd ael; $(FLEX) ael.flex)
	$(CMD_PREFIX) sed 's@#if __STDC_VERSION__ >= 199901L@#if !defined __STDC_VERSION__ || __STDC_VERSION__ >= 199901L@' $@ > $@.fix
	$(CMD_PREFIX) echo "#define ASTMM_LIBC ASTMM_REDIRECT" > $@
	$(CMD_PREFIX) echo "#include \"asterisk.h\"" >> $@
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

$(call MOD_ADD_C,res_ael_share,ael/ael_lex.c ael/ael.tab.c ael/pval.c)
$(call MOD_ADD_C,res_stasis,$(wildcard stasis/*.c))
$(call MOD_ADD_C,res_snmp,snmp/agent.c)
$(call MOD_ADD_C,res_parking,$(wildcard parking/*.c))
$(call MOD_ADD_C,res_pjsip,$(wildcard res_pjsip/*.c))
$(call MOD_ADD_C,res_ari,ari/cli.c ari/config.c ari/ari_websockets.c)
$(call MOD_ADD_C,res_ari_model,ari/ari_model_validators.c)
$(call MOD_ADD_C,res_stasis_recording,stasis_recording/stored.c)

res_parking.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)

# Dependencies for res_ari_*.so are generated, so they're in this file
include ari.make
