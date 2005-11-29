/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief A-Law to Signed linear conversion
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

#endif /* _ASTERISK_ALAW_H */
