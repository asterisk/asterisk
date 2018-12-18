/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, CFWare, LLC
 *
 * Corey Farrell <git@cfware.com>
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

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "include/res_pjsip_private.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/module.h"

AST_RWLIST_HEAD_STATIC(session_supplements, ast_sip_session_supplement);

/* This structure is used to support module references without
 * breaking the ABI of the public struct ast_sip_session_supplement. */
struct private_sip_session_supplement {
	/*! \brief This will be added to the session_supplements list.
	 *
	 * This field must be at offset 0 to enable retrieval of this
	 * structure from session_supplements.
	 */
	struct ast_sip_session_supplement copy;
	/*! Users of this session supplement will hold a reference to the module. */
	struct ast_module *module;
	/*! Pointer to the static variable for unregister comparisons. */
	struct ast_sip_session_supplement *original;
};

void ast_sip_session_register_supplement_with_module(struct ast_module *module, struct ast_sip_session_supplement *supplement)
{
	struct ast_sip_session_supplement *iter;
	struct private_sip_session_supplement *priv;
	int inserted = 0;
	SCOPED_LOCK(lock, &session_supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	ast_assert(supplement != NULL);

	if (!supplement->response_priority) {
		supplement->response_priority = AST_SIP_SESSION_BEFORE_MEDIA;
	}

	priv = ast_calloc(1, sizeof(*priv));
	if (!priv) {
		/* Really can't do anything here, just assert and return. */
		ast_assert(priv != NULL);
		return;
	}

	priv->copy = *supplement;
	priv->module = module;
	priv->original = supplement;

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&session_supplements, iter, next) {
		if (iter->priority > supplement->priority) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(&priv->copy, next);
			inserted = 1;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (!inserted) {
		AST_RWLIST_INSERT_TAIL(&session_supplements, &priv->copy, next);
	}
}

void ast_sip_session_unregister_supplement(struct ast_sip_session_supplement *supplement)
{
	struct ast_sip_session_supplement *iter;
	SCOPED_LOCK(lock, &session_supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&session_supplements, iter, next) {
		struct private_sip_session_supplement *priv = (void*)iter;

		if (supplement == priv->original) {
			AST_RWLIST_REMOVE_CURRENT(next);
			ast_free(priv);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

static struct private_sip_session_supplement *supplement_dup(const struct private_sip_session_supplement *src)
{
	struct private_sip_session_supplement *dst = ast_calloc(1, sizeof(*dst));

	if (!dst) {
		return NULL;
	}
	/* Will need to revisit if shallow copy becomes an issue */
	*dst = *src;

	return dst;
}

int ast_sip_session_add_supplements(struct ast_sip_session *session)
{
	struct ast_sip_session_supplement *iter;
	SCOPED_LOCK(lock, &session_supplements, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE(&session_supplements, iter, next) {
		struct private_sip_session_supplement *dup = supplement_dup((void*)iter);

		if (!dup) {
			return -1;
		}

		/* referenced session created. increasing module reference. */
		ast_module_ref(dup->module);

		AST_LIST_INSERT_TAIL(&session->supplements, &dup->copy, next);
	}

	return 0;
}

void ast_sip_session_remove_supplements(struct ast_sip_session *session)
{
	struct ast_sip_session_supplement *iter;

	if (!session) {
		return;
	}

	/* free the supplements */
	while ((iter = AST_LIST_REMOVE_HEAD(&session->supplements, next))) {
		struct private_sip_session_supplement *priv = (void*)iter;
		if (priv->module) {
			/* referenced session closed. decreasing module reference. */
			ast_module_unref(priv->module);
		}

		ast_free(iter);
	}

	return;
}

/* This stub is for ABI compatibility. */
#undef ast_sip_session_register_supplement
void ast_sip_session_register_supplement(struct ast_sip_session_supplement *supplement);
void ast_sip_session_register_supplement(struct ast_sip_session_supplement *supplement)
{
	ast_sip_session_register_supplement_with_module(NULL, supplement);
}
