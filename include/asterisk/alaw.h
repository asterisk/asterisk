/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * A-Law to Signed linear conversion
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_ALAW_H
#define _ASTERISK_ALAW_H

/*! Init the ulaw conversion stuff */
/*!
 * To init the ulaw to slinear conversion stuff, this needs to be run.
 */
extern void ast_alaw_init(void);

/*! converts signed linear to mulaw */
/*!
  */
extern unsigned char __ast_lin2a[8192];

/*! help */
extern short __ast_alaw[256];

#define AST_LIN2A(a) (__ast_lin2a[((unsigned short)(a)) >> 3])
#define AST_ALAW(a) (__ast_alaw[(int)(a)])

#endif
