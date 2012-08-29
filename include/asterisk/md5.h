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

/*!\file
 * \brief MD5 digest functions
 */

#ifndef _ASTERISK_MD5_H
#define _ASTERISK_MD5_H

struct MD5Context {
	uint32_t buf[4];
	uint32_t bits[2];
	/*! Align because we cast this buffer to uint32s */
	unsigned char in[64] __attribute__((aligned(__alignof__(uint32_t))));
};

void MD5Init(struct MD5Context *context);
void MD5Update(struct MD5Context *context, unsigned char const *buf,
	       unsigned len);
void MD5Final(unsigned char digest[16], struct MD5Context *context);
void MD5Transform(uint32_t buf[4], uint32_t const in[16]);

#endif /* _ASTERISK_MD5_H */
