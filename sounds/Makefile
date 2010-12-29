#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for sound files
#
# Copyright (C) 2006, Digium, Inc.
#
# Kevin P. Fleming <kpfleming@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

.PHONY: dist-clean all uninstall have_download install

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/makeopts

# Make doesn't tolerate spaces in pathnames well.  I hate to hardcode the OS X
# path, but Make doesn't allow spaces as arguments, either.
ifneq ($(findstring Application Support,$(ASTDATADIR)),)
TMPDATADIR=/tmp/astdatadir
PREFIXCMD=ln -sf "$(ASTDATADIR)/" /tmp/astdatadir
else
TMPDATADIR=$(ASTDATADIR)
endif

CMD_PREFIX?=@
SOUNDS_DIR:=$(DESTDIR)$(TMPDATADIR)/sounds
SOUNDS_CACHE_DIR?=
MOH_DIR:=$(DESTDIR)$(TMPDATADIR)/moh
CORE_SOUNDS_VERSION:=1.4.20
EXTRA_SOUNDS_VERSION:=1.4.11
MOH_VERSION:=2.03
SOUNDS_URL:=http://downloads.asterisk.org/pub/telephony/sounds/releases
MCS:=$(subst -EN-,-en-,$(MENUSELECT_CORE_SOUNDS))
MCS:=$(subst -EN_AU-,-en_AU-,$(MCS))
MCS:=$(subst -FR-,-fr-,$(MCS))
MCS:=$(subst -ES-,-es-,$(MCS))
MCS:=$(subst -WAV,-wav,$(MCS))
MCS:=$(subst -ULAW,-ulaw,$(MCS))
MCS:=$(subst -ALAW,-alaw,$(MCS))
MCS:=$(subst -GSM,-gsm,$(MCS))
MCS:=$(subst -G729,-g729,$(MCS))
MCS:=$(subst -G722,-g722,$(MCS))
MCS:=$(subst -SLN16,-sln16,$(MCS))
MCS:=$(subst -SIREN7,-siren7,$(MCS))
MCS:=$(subst -SIREN14,-siren14,$(MCS))
CORE_SOUNDS:=$(MCS:CORE-SOUNDS-%=asterisk-core-sounds-%-$(CORE_SOUNDS_VERSION).tar.gz)
CORE_SOUND_TAGS:=$(MCS:CORE-SOUNDS-%=$(SOUNDS_DIR)/.asterisk-core-sounds-%-$(CORE_SOUNDS_VERSION))
MES:=$(subst -EN-,-en-,$(MENUSELECT_EXTRA_SOUNDS))
MES:=$(subst -FR-,-fr-,$(MES))
MES:=$(subst -ES-,-es-,$(MES))
MES:=$(subst -WAV,-wav,$(MES))
MES:=$(subst -ULAW,-ulaw,$(MES))
MES:=$(subst -ALAW,-alaw,$(MES))
MES:=$(subst -GSM,-gsm,$(MES))
MES:=$(subst -G729,-g729,$(MES))
MES:=$(subst -G722,-g722,$(MES))
MES:=$(subst -SLN16,-sln16,$(MES))
MES:=$(subst -SIREN7,-siren7,$(MES))
MES:=$(subst -SIREN14,-siren14,$(MES))
EXTRA_SOUNDS:=$(MES:EXTRA-SOUNDS-%=asterisk-extra-sounds-%-$(EXTRA_SOUNDS_VERSION).tar.gz)
EXTRA_SOUND_TAGS:=$(MES:EXTRA-SOUNDS-%=$(SOUNDS_DIR)/.asterisk-extra-sounds-%-$(EXTRA_SOUNDS_VERSION))
MM:=$(subst -OPSOUND-,-opsound-,$(MENUSELECT_MOH))
MM:=$(subst -WAV,-wav,$(MM))
MM:=$(subst -ULAW,-ulaw,$(MM))
MM:=$(subst -ALAW,-alaw,$(MM))
MM:=$(subst -GSM,-gsm,$(MM))
MM:=$(subst -G729,-g729,$(MM))
MM:=$(subst -G722,-g722,$(MM))
MM:=$(subst -SLN16,-sln16,$(MM))
MM:=$(subst -SIREN7,-siren7,$(MM))
MM:=$(subst -SIREN14,-siren14,$(MM))
MOH:=$(MM:MOH-%=asterisk-moh-%-$(MOH_VERSION).tar.gz)
MOH_TAGS:=$(MM:MOH-%=$(MOH_DIR)/.asterisk-moh-%-$(MOH_VERSION))
# If "fetch" is used, --continue is not a valid option.
ifneq ($(findstring wget,$(DOWNLOAD)),)
DOWNLOAD+=--continue $(WGET_EXTRA_ARGS)
endif

ifneq ($(SOUNDS_CACHE_DIR),)

define sound_format_lang_rule
$(1)/.asterisk-$(2)$(if $(3),-$(3),)-%: have_download
	$(CMD_PREFIX)PACKAGE=$$(subst $(1)/.asterisk,asterisk,$$@).tar.gz; \
	if test ! -f $$(SOUNDS_CACHE_DIR)/$$$${PACKAGE}; then \
	  (cd $$(SOUNDS_CACHE_DIR); $$(DOWNLOAD) $$(SOUNDS_URL)/$$$${PACKAGE}); \
	fi; \
	if test ! -f $$(SOUNDS_CACHE_DIR)/$$$${PACKAGE}.sha1; then \
	  (cd $$(SOUNDS_CACHE_DIR); $$(DOWNLOAD) $$(SOUNDS_URL)/$$$${PACKAGE}.sha1); \
	fi; \
	$(LN) -sf $$(SOUNDS_CACHE_DIR)/$$$${PACKAGE} .; \
	$(LN) -sf $$(SOUNDS_CACHE_DIR)/$$$${PACKAGE}.sha1 .; \
	$$(SHA1SUM) -c --status $$$${PACKAGE}.sha1 || \
	  ( \
	    rm -f $$(SOUNDS_CACHE_DIR)/$$$${PACKAGE} $$(SOUNDS_CACHE_DIR)/$$$${PACKAGE}.sha1 $$$${PACKAGE} $$$${PACKAGE}.sha1; \
	    echo "Bad checksum: $$$${PACKAGE}" 1>&2; \
	    exit 1; \
	  ) || exit 1; \
	rm -f $$(subst -$(4),,$$@)-*; \
	(cd $(1)$(if $(3),/$(3),); cat $$(CURDIR)/$$$${PACKAGE} | gzip -d | tar xof -) && touch $$@
endef

define sound_download_rule
asterisk-$(1)-%.tar.gz: have_download
	$(CMD_PREFIX) \
	if test ! -f $$(SOUNDS_CACHE_DIR)/$$@; then \
	  (cd $$(SOUNDS_CACHE_DIR); $$(DOWNLOAD) $$(SOUNDS_URL)/$$@); \
	fi; \
	if test ! -f $$(SOUNDS_CACHE_DIR)/$$@.sha1; then \
	  (cd $$(SOUNDS_CACHE_DIR); $$(DOWNLOAD) $$(SOUNDS_URL)/$$@.sha1); \
	fi; \
	$(LN) -sf $$(SOUNDS_CACHE_DIR)/$$@ .; \
	$(LN) -sf $$(SOUNDS_CACHE_DIR)/$$@.sha1 .; \
	$$(SHA1SUM) -c --status $$@.sha1 || \
	  ( \
	    rm -f $$(SOUNDS_CACHE_DIR)/$$@ $$(SOUNDS_CACHE_DIR)/$$@.sha1 $$@ $$@.sha1; \
	    echo "Bad checksum: $$@" 1>&2; \
	    exit 1; \
	  ) || exit 1
endef

else

define sound_format_lang_rule
$(1)/.asterisk-$(2)$(if $(3),-$(3),)-%: have_download
	$(CMD_PREFIX)PACKAGE=$$(subst $(1)/.asterisk,asterisk,$$@).tar.gz; \
	if test ! -f $$$${PACKAGE}; then \
	  $$(DOWNLOAD) $$(SOUNDS_URL)/$$$${PACKAGE} || exit 1; \
	fi; \
	rm -f $$(subst -$(4),,$$@)-*; \
	(cd $(1)$(if $(3),/$(3),); cat $$(CURDIR)/$$$${PACKAGE} | gzip -d | tar xof -) && touch $$@
endef

define sound_download_rule
asterisk-$(1)-%.tar.gz: have_download
	$(CMD_PREFIX)if test ! -f $$@ && test ! -f $$(SOUNDS_DIR)/.$$(subst .tar.gz,,$$@); then \
	  $$(DOWNLOAD) $$(SOUNDS_URL)/$$@; \
	fi
endef

endif

all: prefixcmd $(SOUNDS_CACHE_DIR) $(CORE_SOUNDS) $(EXTRA_SOUNDS) $(MOH)

prefixcmd:
	if [ -d $(TMPDATADIR) ] ; then \
		rm -rf $(TMPDATADIR) ; \
	fi
	@$(PREFIXCMD)

have_download:
	@if test "$(DOWNLOAD)" = ":" ; then \
		echo "**************************************************"; \
		echo "***                                            ***"; \
		echo "*** You must have either wget or fetch to be   ***"; \
		echo "*** able to automatically download and install ***"; \
		echo "*** the requested sound packages.              ***"; \
		echo "***                                            ***"; \
		echo "*** Please install one of these, or remove any ***"; \
		echo "*** extra sound package selections in          ***"; \
		echo "*** menuselect before installing Asterisk.     ***"; \
		echo "***                                            ***"; \
		echo "**************************************************"; \
		exit 1; \
	fi

$(eval $(call sound_format_lang_rule,$(SOUNDS_DIR),core-sounds,en,$(CORE_SOUNDS_VERSION)))

$(eval $(call sound_format_lang_rule,$(SOUNDS_DIR),core-sounds,en_AU,$(CORE_SOUNDS_VERSION)))

$(eval $(call sound_format_lang_rule,$(SOUNDS_DIR),core-sounds,es,$(CORE_SOUNDS_VERSION)))

$(eval $(call sound_format_lang_rule,$(SOUNDS_DIR),core-sounds,fr,$(CORE_SOUNDS_VERSION)))

$(eval $(call sound_format_lang_rule,$(SOUNDS_DIR),extra-sounds,en,$(EXTRA_SOUNDS_VERSION)))

$(eval $(call sound_format_lang_rule,$(SOUNDS_DIR),extra-sounds,es,$(EXTRA_SOUNDS_VERSION)))

$(eval $(call sound_format_lang_rule,$(SOUNDS_DIR),extra-sounds,fr,$(EXTRA_SOUNDS_VERSION)))

$(eval $(call sound_format_lang_rule,$(MOH_DIR),moh,,$(MOH_VERSION)))

$(eval $(call sound_download_rule,core-sounds))

$(eval $(call sound_download_rule,extra-sounds))

$(eval $(call sound_download_rule,moh))

dist-clean:
	rm -f *.tar.gz

$(SOUNDS_DIR)/en $(MOH_DIR) $(SOUNDS_DIR)/en_AU $(SOUNDS_DIR)/es $(SOUNDS_DIR)/fr $(SOUNDS_CACHE_DIR):
	mkdir -p $@

install: prefixcmd $(SOUNDS_CACHE_DIR) $(SOUNDS_DIR)/en $(SOUNDS_DIR)/en_AU $(SOUNDS_DIR)/es $(SOUNDS_DIR)/fr $(MOH_DIR) $(CORE_SOUND_TAGS) $(EXTRA_SOUND_TAGS) $(MOH_TAGS)

uninstall:
	rm -rf $(SOUNDS_DIR)
	rm -rf $(MOH_DIR)

core_sounds_version:
	@echo $(CORE_SOUNDS_VERSION)

extra_sounds_version:
	@echo $(EXTRA_SOUNDS_VERSION)

moh_version:
	@echo $(MOH_VERSION)
