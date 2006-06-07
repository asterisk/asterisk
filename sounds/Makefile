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

include ../menuselect.makeopts

CORE_SOUNDS_VERSION:=1.4.0
SOUNDS_URL:="http://cork.digium.internal/apache2-default"
MENUSELECT_CORE_SOUNDS:=$(subst -EN-,-en-,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -FR-,-fr-,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -ES-,-es-,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -WAV,-wav,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -ULAW,-ulaw,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -ALAW,-alaw,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -GSM,-gsm,$(MENUSELECT_CORE_SOUNDS))
MENUSELECT_CORE_SOUNDS:=$(subst -G729,-g729,$(MENUSELECT_CORE_SOUNDS))
CORE_SOUNDS:=$(MENUSELECT_CORE_SOUNDS:CORE-SOUNDS-%=asterisk-core-sounds-%-$(CORE_SOUNDS_VERSION).tar.gz)
MENUSELECT_EXTRA_SOUNDS:=$(subst -EN-,-en-,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -FR-,-fr-,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -ES-,-es-,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -WAV,-wav,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -ULAW,-ulaw,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -ALAW,-alaw,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -GSM,-gsm,$(MENUSELECT_EXTRA_SOUNDS))
MENUSELECT_EXTRA_SOUNDS:=$(subst -G729,-g729,$(MENUSELECT_EXTRA_SOUNDS))
EXTRA_SOUNDS:=$(MENUSELECT_EXTRA_SOUNDS:EXTRA-SOUNDS-%=asterisk-extra-sounds-%.tar.gz)
MENUSELECT_MOH:=$(subst -FREEPLAY-,-freeplay-,$(MENUSELECT_MOH))
MENUSELECT_MOH:=$(subst -MP3,-mp3,$(MENUSELECT_MOH))
MOH:=$(MENUSELECT_MOH:MOH-%=asterisk-moh-%.tar.gz)

%.gz:
	@wget --no-verbose --continue $(SOUNDS_URL)/$@

all:	$(CORE_SOUNDS) $(EXTRA_SOUNDS) $(MOH)

clean:

install: all
	mkdir -p $(DESTDIR)$(ASTDATADIR)/sounds
	mkdir -p $(DESTDIR)$(ASTDATADIR)/mohmp3

uninstall:
	rm -rf $(DESTDIR)$(ASTDATADIR)/sounds
	rm -rf $(DESTDIR)$(ASTDATADIR)/mohmp3
