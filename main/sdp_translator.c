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

#include "asterisk.h"
#include "asterisk/sdp_options.h"
#include "asterisk/sdp_translator.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"

AST_RWLOCK_DEFINE_STATIC(registered_ops_lock);
static struct ast_sdp_translator_ops *registered_ops[AST_SDP_IMPL_END];

int ast_sdp_register_translator(struct ast_sdp_translator_ops *ops)
{
	SCOPED_WRLOCK(lock, &registered_ops_lock);

	if (ops->repr >= AST_SDP_IMPL_END) {
		ast_log(LOG_ERROR, "SDP translator has unrecognized representation\n");
		return -1;
	}

	if (registered_ops[ops->repr] != NULL) {
		ast_log(LOG_ERROR, "SDP_translator with this representation already registered\n");
		return -1;
	}

	registered_ops[ops->repr] = ops;
	ast_log(LOG_NOTICE, "Placed ops %p at slot %d\n", ops, ops->repr);
	return 0;
}

void ast_sdp_unregister_translator(struct ast_sdp_translator_ops *ops)
{
	SCOPED_WRLOCK(lock, &registered_ops_lock);

	if (ops->repr >= AST_SDP_IMPL_END) {
		return;
	}

	registered_ops[ops->repr] = NULL;
}

struct ast_sdp_translator *ast_sdp_translator_new(enum ast_sdp_options_impl repr)
{
	struct ast_sdp_translator *translator;
	SCOPED_RDLOCK(lock, &registered_ops_lock);

	if (registered_ops[repr] == NULL) {
		ast_log(LOG_NOTICE, "No registered SDP translator with representation %d\n", repr);
		return NULL;
	}

	translator = ast_calloc(1, sizeof(*translator));
	if (!translator) {
		return NULL;
	}

	translator->ops = registered_ops[repr];

	translator->translator_priv = translator->ops->translator_new();
	if (!translator->translator_priv) {
		ast_free(translator);
		return NULL;
	}

	return translator;
}

void ast_sdp_translator_free(struct ast_sdp_translator *translator)
{
	if (!translator) {
		return;
	}
	translator->ops->translator_free(translator->translator_priv);
	ast_free(translator);
}

struct ast_sdp *ast_sdp_translator_to_sdp(struct ast_sdp_translator *translator,
	const void *native_sdp)
{
	return translator->ops->to_sdp(native_sdp, translator->translator_priv);
}

const void *ast_sdp_translator_from_sdp(struct ast_sdp_translator *translator,
	const struct ast_sdp *ast_sdp)
{
	return translator->ops->from_sdp(ast_sdp, translator->translator_priv);
}
