/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * General Asterisk channel definitions.
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Matthew Fredrickson <creslin@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_TRANSCAP_H
#define _ASTERISK_TRANSCAP_H

/* These definitions are taken directly out of libpri.h and used here.
 * DO NOT change them as it will cause unexpected behavior in channels
 * that utilize these fields.
 */

#define AST_TRANS_CAP_SPEECH				0x0
#define AST_TRANS_CAP_DIGITAL				0x08
#define AST_TRANS_CAP_RESTRICTED_DIGITAL		0x09
#define AST_TRANS_CAP_3_1K_AUDIO			0x10
#define AST_TRANS_CAP_7K_AUDIO				0x11	/* Depriciated ITU Q.931 (05/1998)*/
#define AST_TRANS_CAP_DIGITAL_W_TONES			0x11
#define AST_TRANS_CAP_VIDEO				0x18

#define IS_DIGITAL(cap)\
	(cap) & AST_TRANS_CAP_DIGITAL ? 1 : 0

#endif /* _ASTERISK_TRANSCAP_H */
