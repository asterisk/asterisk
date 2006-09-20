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

.PHONY: dist-clean all uninstall

-include $(ASTTOPDIR)/menuselect.makeopts

PWD:=$(shell pwd)
SOUNDS_DIR:=$(DESTDIR)$(ASTDATADIR)/sounds
MOH_DIR:=$(DESTDIR)$(ASTDATADIR)/moh
CORE_SOUNDS_VERSION:=1.4.2
EXTRA_SOUNDS_VERSION:=1.4.1
SOUNDS_URL:=http://ftp.digium.com/pub/telephony/sounds/releases
MENUSELECT_CORE_SOUNDS:=$(subst -EN-,-en-,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -FR-,-fr-,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -ES-,-es-,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -WAV,-wav,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -ULAW,-ulaw,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -ALAW,-alaw,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -GSM,-gsm,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -G729,-g729,$(MENUSELECT_CORE_SOUNDS))
CORE_SOUNDS:=$(MENUSELECT_CORE_SOUNDS:CORE-SOUNDS-%=asterisk-core-sounds-%-$(CORE_SOUNDS_VERSION).tar.gz)
CORE_SOUND_TAGS:=$(MENUSELECT_CORE_SOUNDS:CORE-SOUNDS-%=$(SOUNDS_DIR)/.asterisk-core-sounds-%-$(CORE_SOUNDS_VERSION))
MENUSELECT_EXTRA_SOUNDS:=$(subst -EN-,-en-,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -FR-,-fr-,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -ES-,-es-,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -WAV,-wav,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -ULAW,-ulaw,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -ALAW,-alaw,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -GSM,-gsm,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -G729,-g729,$(MENUSELECT_EXTRA_SOUNDS))
EXTRA_SOUNDS:=$(MENUSELECT_EXTRA_SOUNDS:EXTRA-SOUNDS-%=asterisk-extra-sounds-%-$(EXTRA_SOUNDS_VERSION).tar.gz)
EXTRA_SOUND_TAGS:=$(MENUSELECT_EXTRA_SOUNDS:EXTRA-SOUNDS-%=$(SOUNDS_DIR)/.asterisk-extra-sounds-%-$(EXTRA_SOUNDS_VERSION))
MENUSELECT_MOH:=$(subst -FREEPLAY-,-freeplay-,$(MENUSELECT_MOH))
MENUSELECT_MOH:=$(subst -WAV,-wav,$(MENUSELECT_MOH))
MENUSELECT_MOH:=$(subst -ULAW,-ulaw,$(MENUSELECT_MOH))
MENUSELECT_MOH:=$(subst -ALAW,-alaw,$(MENUSELECT_MOH))
MENUSELECT_MOH:=$(subst -GSM,-gsm,$(MENUSELECT_MOH))
MENUSELECT_MOH:=$(subst -G729,-g729,$(MENUSELECT_MOH))
MOH:=$(MENUSELECT_MOH:MOH-%=asterisk-moh-%.tar.gz)
MOH_TAGS:=$(MENUSELECT_MOH:MOH-%=$(MOH_DIR)/.asterisk-moh-%)
WGET_ARGS:=--continue

all: $(CORE_SOUNDS) $(EXTRA_SOUNDS) $(MOH)

$(SOUNDS_DIR)/.asterisk-core-sounds-en-%: $(SOUNDS_DIR)
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(CORE_SOUNDS_VERSION),,$@)-* && \
	cat $${PACKAGE} | gzip -d | tar xCf $(SOUNDS_DIR) -
	touch $@

$(SOUNDS_DIR)/.asterisk-core-sounds-es-%: $(SOUNDS_DIR)/es
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(CORE_SOUNDS_VERSION),,$@)-* && \
	cat $${PACKAGE} | gzip -d | tar xCf $(SOUNDS_DIR)/es -
	touch $@

$(SOUNDS_DIR)/.asterisk-core-sounds-fr-%: $(SOUNDS_DIR)/fr
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(CORE_SOUNDS_VERSION),,$@)-* && \
	cat $${PACKAGE} | gzip -d | tar xCf $(SOUNDS_DIR)/fr -
	touch $@

$(SOUNDS_DIR)/.asterisk-extra-sounds-en-%: $(SOUNDS_DIR)
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(EXTRA_SOUNDS_VERSION),,$@)-* && \
	cat $${PACKAGE} | gzip -d | tar xCf $(SOUNDS_DIR) -
	touch $@

$(SOUNDS_DIR)/.asterisk-extra-sounds-es-%: $(SOUNDS_DIR)/es
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(EXTRA_SOUNDS_VERSION),,$@)-* && \
	cat /$${PACKAGE} | gzip -d | tar xCf $(SOUNDS_DIR)/es -
	touch $@

$(SOUNDS_DIR)/.asterisk-extra-sounds-fr-%: $(SOUNDS_DIR)/fr
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(EXTRA_SOUNDS_VERSION),,$@)-* && \
	cat $${PACKAGE} | gzip -d | tar xCf $(SOUNDS_DIR)/fr -
	touch $@

$(MOH_DIR)/.asterisk-moh-%: $(MOH_DIR)
	@PACKAGE=$(subst $(MOH_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	cat $${PACKAGE} | gzip -d | tar xCf $(MOH_DIR) -
	touch $@

asterisk-core-%.tar.gz:
	@wget $(WGET_ARGS) $(SOUNDS_URL)/$@

asterisk-extra-%.tar.gz:
	@wget $(WGET_ARGS) $(SOUNDS_URL)/$@

asterisk-moh-%.tar.gz:
	@wget $(WGET_ARGS) $(SOUNDS_URL)/$@

dist-clean:
	rm -f *.tar.gz

.PHONY: $(SOUNDS_DIR) $(SOUNDS_DIR)/fr $(SOUNDS_DIR)/es $(MOH_DIR)

$(SOUNDS_DIR) $(MOH_DIR) $(SOUNDS_DIR)/es $(SOUNDS_DIR)/fr:
	mkdir -p $@

install: $(CORE_SOUND_TAGS) $(EXTRA_SOUND_TAGS) $(MOH_TAGS)

uninstall:
	rm -rf $(SOUNDS_DIR)
	rm -rf $(MOH_DIR)
