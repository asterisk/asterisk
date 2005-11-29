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

extern void ast_ulaw_init(void);
extern unsigned char ast_lin2mu[65536];
extern short ast_mulaw[256];



#endif
