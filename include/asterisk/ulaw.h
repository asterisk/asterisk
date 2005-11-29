/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * u-Law to Signed linear conversion
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_ULAW_H
#define _ASTERISK_ULAW_H

/*! Init the ulaw conversion stuff */
/*!
 * To init the ulaw to slinear conversion stuff, this needs to be run.
 */
extern void ast_ulaw_init(void);

/*! converts signed linear to mulaw */
/*!
  */
extern unsigned char __ast_lin2mu[16384];

/*! help */
extern short __ast_mulaw[256];

#define AST_LIN2MU(a) (__ast_lin2mu[((unsigned short)(a)) >> 2])
#define AST_MULAW(a) (__ast_mulaw[(a)])

#endif
