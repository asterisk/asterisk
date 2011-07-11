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

/*! \file
 * \brief Jingle definitions for chan_jingle
 *
 * \ref chan_jingle.c
 *
 * \author Matt O'Gorman <mogorman@digium.com>
 */


#ifndef _ASTERISK_JINGLE_H
#define _ASTERISK_JINGLE_H

#include <iksemel.h>
#include "asterisk/astobj.h"


/* Jingle Constants */

#define JINGLE_NODE "jingle"
#define GOOGLE_NODE "session"

#define JINGLE_NS "urn:xmpp:tmp:jingle"
#define JINGLE_AUDIO_RTP_NS "urn:xmpp:tmp:jingle:apps:audio-rtp"
#define JINGLE_VIDEO_RTP_NS "urn:xmpp:tmp:jingle:apps:video"
#define JINGLE_ICE_UDP_NS "urn:xmpp:tmp:jingle:transports:ice-udp"
#define JINGLE_DTMF_NS "urn:xmpp:tmp:jingle:dtmf"

#define GOOGLE_NS "http://www.google.com/session"
#define GOOGLE_JINGLE_NS "urn:xmpp:jingle:1"
#define GOOGLE_AUDIO_NS "http://www.google.com/session/phone"
#define GOOGLE_VIDEO_NS "http://www.google.com/session/video"
#define GOOGLE_TRANSPORT_NS "http://www.google.com/transport/p2p"

#define JINGLE_SID "sid"
#define GOOGLE_SID "id"

#define JINGLE_INITIATE "session-initiate"

#define JINGLE_ACCEPT "session-accept"
#define GOOGLE_ACCEPT "accept"

#define JINGLE_NEGOTIATE "transport-info"
#define GOOGLE_NEGOTIATE "candidates"

#define JINGLE_INFO "session-info"
#define JINGLE_TERMINATE "session-terminate"

#endif
