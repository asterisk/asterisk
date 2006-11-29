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
CORE_SOUNDS_VERSION:=1.4.3
EXTRA_SOUNDS_VERSION:=1.4.3
SOUNDS_URL:=http://ftp.digium.com/pub/telephony/sounds/releases
MCS:=$(subst -EN-,-en-,$(MENUSELECT_CORE_SOUNDS))
MCS:=$(subst -FR-,-fr-,$(MCS))
MCS:=$(subst -ES-,-es-,$(MCS))
MCS:=$(subst -WAV,-wav,$(MCS))
MCS:=$(subst -ULAW,-ulaw,$(MCS))
MCS:=$(subst -ALAW,-alaw,$(MCS))
MCS:=$(subst -GSM,-gsm,$(MCS))
MCS:=$(subst -G729,-g729,$(MCS))
MCS:=$(subst -G722,-g722,$(MCS))
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
EXTRA_SOUNDS:=$(MES:EXTRA-SOUNDS-%=asterisk-extra-sounds-%-$(EXTRA_SOUNDS_VERSION).tar.gz)
EXTRA_SOUND_TAGS:=$(MES:EXTRA-SOUNDS-%=$(SOUNDS_DIR)/.asterisk-extra-sounds-%-$(EXTRA_SOUNDS_VERSION))
MM:=$(subst -FREEPLAY-,-freeplay-,$(MENUSELECT_MOH))
MM:=$(subst -WAV,-wav,$(MM))
MM:=$(subst -ULAW,-ulaw,$(MM))
MM:=$(subst -ALAW,-alaw,$(MM))
MM:=$(subst -GSM,-gsm,$(MM))
MM:=$(subst -G729,-g729,$(MM))
MM:=$(subst -G722,-g722,$(MM))
MOH:=$(MM:MOH-%=asterisk-moh-%.tar.gz)
MOH_TAGS:=$(MM:MOH-%=$(MOH_DIR)/.asterisk-moh-%)
WGET_ARGS:=--continue

all: $(CORE_SOUNDS) $(EXTRA_SOUNDS) $(MOH)

$(SOUNDS_DIR)/.asterisk-core-sounds-en-%:
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(CORE_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR); cat $(PWD)/$${PACKAGE} | gzip -d | tar xf -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-core-sounds-es-%:
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(CORE_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/es; cat $(PWD)/$${PACKAGE} | gzip -d | tar xf -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-core-sounds-fr-%:
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(CORE_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/fr; cat $(PWD)/$${PACKAGE} | gzip -d | tar xf -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-extra-sounds-en-%:
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(EXTRA_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR); cat $(PWD)/$${PACKAGE} | gzip -d | tar xf -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-extra-sounds-es-%:
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(EXTRA_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/es; cat $(PWD)/$${PACKAGE} | gzip -d | tar xf -) && \
	touch $@

$(SOUNDS_DIR)/.asterisk-extra-sounds-fr-%:
	@PACKAGE=$(subst $(SOUNDS_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	rm -f $(subst -$(EXTRA_SOUNDS_VERSION),,$@)-* && \
	(cd $(SOUNDS_DIR)/fr; cat $(PWD)/$${PACKAGE} | gzip -d | tar xf -) && \
	touch $@

$(MOH_DIR)/.asterisk-moh-%:
	@PACKAGE=$(subst $(MOH_DIR)/.asterisk,asterisk,$@).tar.gz; \
	if test ! -f $${PACKAGE}; then wget $(WGET_ARGS) $(SOUNDS_URL)/$${PACKAGE}; fi; \
	if test ! -f $${PACKAGE}; then exit 1; fi; \
	(cd $(MOH_DIR); cat $(PWD)/$${PACKAGE} | gzip -d | tar xf -) && \
	touch $@

asterisk-core-%.tar.gz:
	@wget $(WGET_ARGS) $(SOUNDS_URL)/$@

asterisk-extra-%.tar.gz:
	@wget $(WGET_ARGS) $(SOUNDS_URL)/$@

asterisk-moh-%.tar.gz:
	@wget $(WGET_ARGS) $(SOUNDS_URL)/$@

dist-clean:
	rm -f *.tar.gz

$(SOUNDS_DIR) $(MOH_DIR) $(SOUNDS_DIR)/es $(SOUNDS_DIR)/fr:
	mkdir -p $@

install: $(SOUNDS_DIR) $(SOUNDS_DIR)/es $(SOUNDS_DIR)/fr $(MOH_DIR) $(CORE_SOUND_TAGS) $(EXTRA_SOUND_TAGS) $(MOH_TAGS)

uninstall:
	rm -rf $(SOUNDS_DIR)
	rm -rf $(MOH_DIR)
