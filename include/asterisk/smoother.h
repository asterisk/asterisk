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
 * \brief Asterisk internal frame definitions.
 * \arg For an explanation of frames, see \ref Def_Frame
 * \arg Frames are send of Asterisk channels, see \ref Def_Channel
 */

#ifndef _ASTERISK_SMOOTHER_H
#define _ASTERISK_SMOOTHER_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/endian.h"

#define AST_SMOOTHER_FLAG_G729		(1 << 0)
#define AST_SMOOTHER_FLAG_BE		(1 << 1)
#define AST_SMOOTHER_FLAG_FORCED	(1 << 2)

/*! \name AST_Smoother
 *
 * @{
 */
/*! \page ast_smooth The AST Frame Smoother
The ast_smoother interface was designed specifically
to take frames of variant sizes and produce frames of a single expected
size, precisely what you want to do.

The basic interface is:

- Initialize with ast_smoother_new()
- Queue input frames with ast_smoother_feed()
- Get output frames with ast_smoother_read()
- when you're done, free the structure with ast_smoother_free()
- Also see ast_smoother_test_flag(), ast_smoother_set_flags(), ast_smoother_get_flags(), ast_smoother_reset()
*/
struct ast_smoother;

struct ast_frame;

struct ast_smoother *ast_smoother_new(int bytes);
void ast_smoother_set_flags(struct ast_smoother *smoother, int flags);
int ast_smoother_get_flags(struct ast_smoother *smoother);
int ast_smoother_test_flag(struct ast_smoother *s, int flag);
void ast_smoother_free(struct ast_smoother *s);
void ast_smoother_reset(struct ast_smoother *s, int bytes);

/*!
 * \brief Reconfigure an existing smoother to output a different number of bytes per frame
 * \param s the smoother to reconfigure
 * \param bytes the desired number of bytes per output frame
 */
void ast_smoother_reconfigure(struct ast_smoother *s, int bytes);

int __ast_smoother_feed(struct ast_smoother *s, struct ast_frame *f, int swap);
struct ast_frame *ast_smoother_read(struct ast_smoother *s);
#define ast_smoother_feed(s,f) __ast_smoother_feed(s, f, 0)
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ast_smoother_feed_be(s,f) __ast_smoother_feed(s, f, 1)
#define ast_smoother_feed_le(s,f) __ast_smoother_feed(s, f, 0)
#else
#define ast_smoother_feed_be(s,f) __ast_smoother_feed(s, f, 0)
#define ast_smoother_feed_le(s,f) __ast_smoother_feed(s, f, 1)
#endif

/*! @} */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_SMOOTHER_H */
