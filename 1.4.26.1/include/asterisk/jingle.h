/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Matt O'Gorman <mogorman@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#ifndef _ASTERISK_JINGLE_H
#define _ASTERISK_JINGLE_H

#include <iksemel.h>
#include "asterisk/astobj.h"


/* Jingle Constants */

#define JINGLE_NODE "jingle"
#define GOOGLE_NODE "session"

#define JINGLE_NS "http://jabber.org/protocol/jingle"
#define GOOGLE_NS "http://www.google.com/session"

#define JINGLE_SID "sid"
#define GOOGLE_SID "id"

#define JINGLE_INITIATE "initiate"

#define JINGLE_ACCEPT "accept"
#define GOOGLE_ACCEPT "accept"

#define JINGLE_NEGOTIATE "negotiate"
#define GOOGLE_NEGOTIATE "candidates"

#endif
