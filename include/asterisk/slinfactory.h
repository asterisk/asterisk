/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II
 *
 * Anthony Minessale <anthmct@yahoo.com>
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
 * \brief A machine to gather up arbitrary frames and convert them
 * to raw slinear on demand.
 */

#ifndef _ASTERISK_SLINFACTORY_H
#define _ASTERISK_SLINFACTORY_H

#include "asterisk/format.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define AST_SLINFACTORY_MAX_HOLD 1280

struct ast_slinfactory {
	AST_LIST_HEAD_NOLOCK(, ast_frame) queue; /*!< A list of unaltered frames */
	struct ast_trans_pvt *trans;             /*!< Translation path that converts fed frames into signed linear */
	short hold[AST_SLINFACTORY_MAX_HOLD];    /*!< Hold for audio that no longer belongs to a frame (ie: if only some samples were taken from a frame) */
	short *offset;                           /*!< Offset into the hold where audio begins */
	size_t holdlen;                          /*!< Number of samples currently in the hold */
	unsigned int size;                       /*!< Number of samples currently in the factory */
	struct ast_format format;                /*!< Current format the translation path is converting from */
	struct ast_format output_format;         /*!< The output format desired */
};

/*!
 * \brief Initialize a slinfactory
 *
 * \param sf The slinfactory to initialize
 *
 * \return Nothing
 */
void ast_slinfactory_init(struct ast_slinfactory *sf);

/*!
 * \brief Initialize a slinfactory
 *
 * \param sf The slinfactory to initialize
 * \param slin_out the slinear output format desired.
 *
 * \return 0 on success, non-zero on failure
 */
int ast_slinfactory_init_with_format(struct ast_slinfactory *sf, const struct ast_format *slin_out);

/*!
 * \brief Destroy the contents of a slinfactory
 *
 * \param sf The slinfactory that is no longer needed
 *
 * This function will free any memory allocated for the contents of the
 * slinfactory.  It does not free the slinfactory itself.  If the sf is
 * malloc'd, then it must be explicitly free'd after calling this function.
 *
 * \return Nothing
 */
void ast_slinfactory_destroy(struct ast_slinfactory *sf);

/*!
 * \brief Feed audio into a slinfactory
 *
 * \param sf The slinfactory to feed into
 * \param f Frame containing audio to feed in
 *
 * \return Number of frames currently in factory
 */
int ast_slinfactory_feed(struct ast_slinfactory *sf, struct ast_frame *f);

/*!
 * \brief Read samples from a slinfactory
 *
 * \param sf The slinfactory to read from
 * \param buf Buffer to put samples into
 * \param samples Number of samples wanted
 *
 * \return Number of samples read
 */
int ast_slinfactory_read(struct ast_slinfactory *sf, short *buf, size_t samples);

/*!
 * \brief Retrieve number of samples currently in a slinfactory
 *
 * \param sf The slinfactory to peek into
 *
 * \return Number of samples in slinfactory
 */
unsigned int ast_slinfactory_available(const struct ast_slinfactory *sf);

/*!
 * \brief Flush the contents of a slinfactory
 *
 * \param sf The slinfactory to flush
 *
 * \return Nothing
 */
void ast_slinfactory_flush(struct ast_slinfactory *sf);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_SLINFACTORY_H */
