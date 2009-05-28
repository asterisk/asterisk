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

SOUNDS_DIR:=$(DESTDIR)$(ASTDATADIR)/sounds
MOH_DIR:=$(DESTDIR)$(ASTDATADIR)/moh
CORE_SOUNDS_VERSION:=1.4.15
EXTRA_SOUNDS_VERSION:=1.4.9
SOUNDS_URL:=http://downloads.asterisk.org/pub/telephony/sounds/releases
MCS:=$(subst -EN-,-en-,$(MENUSELECT_CORE_SOUNDS))
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
MM:=$(subst -FREEPLAY-,-freeplay-,$(MENUSELECT_MOH))
MM:=$(subst -WAV,-wav,$(MM))
MM:=$(subst -ULAW,-ulaw,$(MM))
MM:=$(subst -ALAW,-alaw,$(MM))
MM:=$(subst -GSM,-gsm,$(MM))
MM:=$(subst -G729,-g729,$(MM))
MM:=$(subst -G722,-g722,$(MM))
MM:=$(subst -SLN16,-sln16,$(MM))
MM:=$(subst -SIREN7,-siren7,$(MM))
MM:=$(subst -SIREN14,-siren14,$(MM))
MOH:=$(MM:MOH-%=asterisk-moh-%.tar.gz)
MOH_TAGS:=$(MM:MOH-%=$(MOH_DIR)/.asterisk-moh-%)
# If "fetch" is used, --continue is not a valid option.
ifeq ($(WGET),wget)
WGET_ARGS:=--continue
endif

all: $(CORE_SOUNDS) $(EXTRA_SOUNDS) $(MOH)

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
		echo "*** menuselecct before installing Asterisk.    ***"; \
		echo "***                                            ***"; \
		echo "**************************************************"; \
		exit 1; \
	fi

$(SOUNDS_DIR)/.asterisk-core-sounds-en-%: have_download
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(CORE_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/en; cat $(CURDIR)/$${PACKAGE} | gzip -d | tar xof -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-core-sounds-es-%: have_download
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(CORE_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/es; cat $(CURDIR)/$${PACKAGE} | gzip -d | tar xof -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-core-sounds-fr-%: have_download
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(CORE_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/fr; cat $(CURDIR)/$${PACKAGE} | gzip -d | tar xof -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-extra-sounds-en-%: have_download
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(EXTRA_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/en; cat $(CURDIR)/$${PACKAGE} | gzip -d | tar xof -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-extra-sounds-es-%: have_download
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(EXTRA_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/es; cat $(CURDIR)/$${PACKAGE} | gzip -d | tar xof -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-extra-sounds-fr-%: have_download
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(EXTRA_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/fr; cat $(CURDIR)/$${PACKAGE} | gzip -d | tar xof -) && \
	touch $@

$(MOH_DIR)/.asterisk-moh-%: have_download
	@PACKAGE=$(subst $(MOH_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	(cd $(MOH_DIR); cat $(CURDIR)/$${PACKAGE} | gzip -d | tar xof -) && \
	touch $@

asterisk-core-%.tar.gz: have_download
	@if test ! -f $@ && test ! -f $(SOUNDS_DIR)/.$(subst .tar.gz,,$@) ; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$@;fi

asterisk-extra-%.tar.gz: have_download
	@if test ! -f $@ && test ! -f $(SOUNDS_DIR)/.$(subst .tar.gz,,$@) ; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$@;fi

asterisk-moh-%.tar.gz: have_download
	@if test ! -f $@ && test ! -f $(MOH_DIR)/.$(subst .tar.gz,,$@) ; then $(DOWNLOAD) $(WGET_ARGS) $(SOUNDS_URL)/$@;fi

dist-clean:
	rm -f *.tar.gz

$(SOUNDS_DIR)/en $(MOH_DIR) $(SOUNDS_DIR)/es $(SOUNDS_DIR)/fr:
	mkdir -p $@

install: $(SOUNDS_DIR)/en $(SOUNDS_DIR)/es $(SOUNDS_DIR)/fr $(MOH_DIR) $(CORE_SOUND_TAGS) $(EXTRA_SOUND_TAGS) $(MOH_TAGS)

uninstall:
	rm -rf $(SOUNDS_DIR)
	rm -rf $(MOH_DIR)

core_sounds_version:
	@echo $(CORE_SOUNDS_VERSION)

extra_sounds_version:
	@echo $(EXTRA_SOUNDS_VERSION)
