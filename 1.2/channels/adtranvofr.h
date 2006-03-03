/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Voice over Frame Relay, Adtran Style
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ADTRANVOFR_H
#define _ADTRANVOFR_H

#define VOFR_CONTROL_ADTRAN		0x0
#define VOFR_CONTROL_VOICE   		0x1
#define VOFR_CONTROL_RFC1490		0x3

#define VOFR_TYPE_SIGNAL		0x0
#define VOFR_TYPE_VOICE			0x1
#define VOFR_TYPE_ANSWER		0x2
#define VOFR_TYPE_FAX			0x3
#define VOFR_TYPE_DTMF			0x4

#define VOFR_CARD_TYPE_UNSPEC		0x0
#define VOFR_CARD_TYPE_FXS		0x1
#define VOFR_CARD_TYPE_FXO		0x2
#define VOFR_CARD_TYPE_ENM		0x3
#define VOFR_CARD_TYPE_VCOM		0x4
#define VOFR_CARD_TYPE_ASTERISK		0xf

#define VOFR_MODULATION_SINGLE		0x0
#define VOFR_MODULATION_V21		0x1
#define VOFR_MODULATION_V27ter_2	0x2
#define VOFR_MODULATION_V27ter_4	0x3
#define VOFR_MODULATION_V29_7		0x4
#define VOFR_MODULATION_V29_9		0x5
#define VOFR_MODULATION_V33_12		0x6
#define VOFR_MODULATION_V33_14		0x7

#define VOFR_ROUTE_NONE			0x0
#define VOFR_ROUTE_LOCAL		0x1
#define VOFR_ROUTE_VOICE		0x2
#define VOFR_ROUTE_DTE1			0x4
#define VOFR_ROUTE_DTE2			0x8
#define VOFR_ROUTE_DTE			0xC

#define VOFR_MASK_EI			0x80
#define VOFR_MASK_LI			0x40
#define VOFR_MASK_CONTROL		0x3F

#define VOFR_SIGNAL_ON_HOOK		0x00
#define VOFR_SIGNAL_OFF_HOOK		0x01
#define VOFR_SIGNAL_RING		0x40
#define VOFR_SIGNAL_SWITCHED_DIAL	0x08
#define VOFR_SIGNAL_BUSY		0x02
#define VOFR_SIGNAL_TRUNK_BUSY		0x04
#define VOFR_SIGNAL_UNKNOWN		0x10
#define VOFR_SIGNAL_OFFHOOK		0x81

#define VOFR_TRACE_SIGNAL		1 << 0
#define VOFR_TRACE_VOICE		1 << 1

#define VOFR_MAX_PKT_SIZE		1500

/*
 * Wire level protocol 
 */

struct vofr_hdr {
	u_int8_t control;		/* Also contains unused EI and LI bits */
#if __BYTE_ORDER == __LITTLE_ENDIAN
	u_int8_t dtype:4;		/* Data type */
	u_int8_t ctag:4;		/* Connect tag */
	u_int8_t dlcih:4;		/* Hi 2 bits of DLCI x-ref */
	u_int8_t vflags:4;		/* Voice Routing Flags */
	u_int8_t dlcil;			/* Lo 8 bits of DLCI x-ref */
	u_int8_t cid;			/* Channel ID */
	u_int8_t mod:4;			/* Modulation */
	u_int8_t remid:4;		/* Remote ID */
#elif __BYTE_ORDER == __BIG_ENDIAN
	u_int8_t ctag:4;		/* Connect tag */
	u_int8_t dtype:4;		/* Data type */
	u_int8_t vflags:4;		/* Voice Routing Flags */
	u_int8_t dlcih:4;		/* Hi 2 bits of DLCI x-ref */
	u_int8_t dlcil;			/* Lo 8 bits of DLCI x-ref */
	u_int8_t cid;			/* Channel ID */
	u_int8_t remid:4;		/* Remote ID or Relay CMD*/
	u_int8_t mod:4;			/* Modulation */
#else
#error	"Please fix <bytesex.h>"
#endif
#ifdef __GNUC__
	u_int8_t data[0];		/* Data */
#endif
};

#define VOFR_HDR_SIZE 6

/* Number of milliseconds to fudge -- experimentally derived */
#define VOFR_FUDGE 2

#endif
