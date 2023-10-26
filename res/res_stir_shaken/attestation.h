/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#ifndef ATTESTATION_H_
#define ATTESTATION_H_

#include "common_config.h"

struct ast_stir_shaken_as_ctx {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(tag);
		AST_STRING_FIELD(orig_tn);
		AST_STRING_FIELD(dest_tn);
	);
	struct ast_channel *chan;
	struct ast_vector_string fingerprints;
	struct tn_cfg *etn;
};

/*!
 * \brief Load the stir/shaken attestation service
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int as_load(void);

/*!
 * \brief Load the stir/shaken attestation service
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int as_reload(void);

/*!
 * \brief Load the stir/shaken attestation service
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int as_unload(void);

#endif /* ATTESTATION_H_ */
