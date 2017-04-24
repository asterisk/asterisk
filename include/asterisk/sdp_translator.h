/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#ifndef _ASTERISK_SDP_TRANSLATOR_H
#define _ASTERISK_SDP_TRANSLATOR_H

#include "asterisk/sdp_options.h"

struct sdp;

/*!
 * \brief SDP translator operations
 */
struct ast_sdp_translator_ops {
	/*! The SDP representation on which this translator operates */
	enum ast_sdp_options_impl repr;
	/*! Allocate new translator private data for a translator */
	void *(*translator_new)(void);
	/*! Free translator private data */
	void (*translator_free)(void *translator_priv);
	/*! Convert the channel-native SDP into an internal Asterisk SDP */
	struct ast_sdp *(*to_sdp)(const void *repr_sdp, void *translator_priv);
	/*! Convert an internal Asterisk SDP into a channel-native SDP */
	const void *(*from_sdp)(const struct ast_sdp *sdp, void *translator_priv);
};

/*!
 * \brief An SDP translator
 *
 * An SDP translator is responsible for converting between Asterisk's internal
 * representation of an SDP and the representation that is native to the channel
 * driver. Translators are allocated per-use.
 */
struct ast_sdp_translator {
	/*! The operations this translator uses */
	struct ast_sdp_translator_ops *ops;
	/*! Private data this translator uses */
	void *translator_priv;
};

/*!
 * \brief Register an SDP translator
 * \param ops The SDP operations defined by this translator
 * \retval 0 Success
 * \retval -1 FAIL
 */
int ast_sdp_register_translator(struct ast_sdp_translator_ops *ops);

/*!
 * \brief Unregister an SDP translator
 */
void ast_sdp_unregister_translator(struct ast_sdp_translator_ops *ops);

/*!
 * \brief Allocate a new SDP translator
 * \param Representation corresponding to the translator_ops to use
 * \retval NULL FAIL
 * \retval non-NULL New SDP translator
 */
struct ast_sdp_translator *ast_sdp_translator_new(enum ast_sdp_options_impl repr);

/*!
 * \brief Free an SDP translator
 */
void ast_sdp_translator_free(struct ast_sdp_translator *translator);

/*!
 * \brief Translate a native SDP to internal Asterisk SDP
 *
 * \param translator The translator to use when translating
 * \param native_sdp The SDP from the channel driver
 * \retval NULL FAIL
 * \retval Non-NULL The translated SDP
 */
struct ast_sdp *ast_sdp_translator_to_sdp(struct ast_sdp_translator *translator, const void *native_sdp);

/*!
 * \brief Translate an internal Asterisk SDP to a native SDP
 *
 * \param translator The translator to use when translating
 * \param ast_sdp The Asterisk SDP to translate
 * \retval NULL FAIL
 * \retval non-NULL The translated SDP
 */
const void *ast_sdp_translator_from_sdp(struct ast_sdp_translator *translator,
	const struct ast_sdp *ast_sdp);

#endif /* _ASTERISK_SDP_TRANSLATOR_H */
