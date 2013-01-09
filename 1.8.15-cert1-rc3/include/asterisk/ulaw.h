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
 * \brief u-Law to Signed linear conversion
 */

#ifndef _ASTERISK_ULAW_H
#define _ASTERISK_ULAW_H


/*!
 * To init the ulaw to slinear conversion stuff, this needs to be run.
 */
void ast_ulaw_init(void);

#define AST_ULAW_BIT_LOSS  3
#define AST_ULAW_STEP      (1 << AST_ULAW_BIT_LOSS)
#define AST_ULAW_TAB_SIZE  (32768 / AST_ULAW_STEP + 1)
#define AST_ULAW_SIGN_BIT  0x80

/*! \brief converts signed linear to mulaw */
#ifndef G711_NEW_ALGORITHM
extern unsigned char __ast_lin2mu[16384];
#else
extern unsigned char __ast_lin2mu[AST_ULAW_TAB_SIZE];
#endif

/*! help */
extern short __ast_mulaw[256];

#ifndef G711_NEW_ALGORITHM

#define AST_LIN2MU(a) (__ast_lin2mu[((unsigned short)(a)) >> 2])

#else

#define AST_LIN2MU_LOOKUP(mag)											\
	__ast_lin2mu[((mag) + AST_ULAW_STEP / 2) >> AST_ULAW_BIT_LOSS]


/*! \brief convert signed linear sample to sign-magnitude pair for u-Law */
static inline void ast_ulaw_get_sign_mag(short sample, unsigned *sign, unsigned *mag)
{
       /* It may look illogical to retrive the sign this way in both cases,
        * but this helps gcc eliminate the branch below and produces
        * faster code */
       *sign = ((unsigned short)sample >> 8) & AST_ULAW_SIGN_BIT;
#if defined(G711_REDUCED_BRANCHING)
       {
               unsigned dual_mag = (-sample << 16) | (unsigned short)sample;
               *mag = (dual_mag >> (*sign >> 3)) & 0xffffU;
       }
#else
       if (sample < 0)
               *mag = -sample;
       else
               *mag = sample;
#endif /* G711_REDUCED_BRANCHING */
}

static inline unsigned char AST_LIN2MU(short sample)
{
       unsigned mag, sign;
       ast_ulaw_get_sign_mag(sample, &sign, &mag);
       return ~(sign | AST_LIN2MU_LOOKUP(mag));
}
#endif

#define AST_MULAW(a) (__ast_mulaw[(a)])

#endif /* _ASTERISK_ULAW_H */
