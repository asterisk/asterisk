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

void ast_sip_session_register_supplement(struct ast_sip_session_supplement *supplement)
{
	struct ast_sip_session_supplement *iter;
	int inserted = 0;
	SCOPED_LOCK(lock, &session_supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	if (!supplement->response_priority) {
		supplement->response_priority = AST_SIP_SESSION_BEFORE_MEDIA;
	}

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&session_supplements, iter, next) {
		if (iter->priority > supplement->priority) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(supplement, next);
			inserted = 1;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (!inserted) {
		AST_RWLIST_INSERT_TAIL(&session_supplements, supplement, next);
	}
}

void ast_sip_session_unregister_supplement(struct ast_sip_session_supplement *supplement)
{
	struct ast_sip_session_supplement *iter;
	SCOPED_LOCK(lock, &session_supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&session_supplements, iter, next) {
		if (supplement == iter) {
			AST_RWLIST_REMOVE_CURRENT(next);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

static struct ast_sip_session_supplement *supplement_dup(const struct ast_sip_session_supplement *src)
{
	struct ast_sip_session_supplement *dst = ast_calloc(1, sizeof(*dst));

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
		struct ast_sip_session_supplement *copy = supplement_dup(iter);

		if (!copy) {
			return -1;
		}
		AST_LIST_INSERT_TAIL(&session->supplements, copy, next);
	}

	return 0;
}
