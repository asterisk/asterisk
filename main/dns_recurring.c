/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 *
 * \brief DNS Recurring Query Support
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/linkedlists.h"
#include "asterisk/sched.h"
#include "asterisk/strings.h"
#include "asterisk/dns_core.h"
#include "asterisk/dns_recurring.h"
#include "asterisk/dns_internal.h"

#include <arpa/nameser.h>

/*! \brief Destructor for a DNS query */
static void dns_query_recurring_destroy(void *data)
{
	struct ast_dns_query_recurring *recurring = data;

	ao2_cleanup(recurring->user_data);
}

static void dns_query_recurring_resolution_callback(const struct ast_dns_query *query);

/*! \brief Scheduled recurring query callback */
static int dns_query_recurring_scheduled_callback(const void *data)
{
	struct ast_dns_query_recurring *recurring = (struct ast_dns_query_recurring *)data;

	ao2_lock(recurring);
	recurring->timer = -1;
	if (!recurring->cancelled) {
		recurring->active = ast_dns_resolve_async(recurring->name, recurring->rr_type, recurring->rr_class, dns_query_recurring_resolution_callback,
			recurring);
	}
	ao2_unlock(recurring);

	ao2_ref(recurring, -1);

	return 0;
}

/*! \brief Query resolution callback */
static void dns_query_recurring_resolution_callback(const struct ast_dns_query *query)
{
	struct ast_dns_query_recurring *recurring = ast_dns_query_get_data(query);
	struct ast_dns_query *callback_query;

	/* Create a separate query to invoke the user specific callback on as the
	 * recurring query user data may get used externally (by the unit test)
	 * and thus changing it is problematic
	 */
	callback_query = dns_query_alloc(query->name, query->rr_type, query->rr_class,
		recurring->callback, recurring->user_data);
	if (callback_query) {
		/* The result is immutable at this point and can be safely provided */
		callback_query->result = query->result;
		callback_query->callback(callback_query);
		callback_query->result = NULL;
		ao2_ref(callback_query, -1);
	}

	ao2_lock(recurring);
	/* So.. if something has not externally cancelled this we can reschedule based on the TTL */
	if (!recurring->cancelled) {
		const struct ast_dns_result *result = ast_dns_query_get_result(query);
		int ttl = MIN(ast_dns_result_get_lowest_ttl(result), INT_MAX / 1000);

		if (ttl) {
			recurring->timer = ast_sched_add(ast_dns_get_sched(), ttl * 1000, dns_query_recurring_scheduled_callback, ao2_bump(recurring));
			if (recurring->timer < 0) {
				/* It is impossible for this to be the last reference as the query has a reference to it */
				ao2_ref(recurring, -1);
			}
		}
	}

	ao2_replace(recurring->active, NULL);
	ao2_unlock(recurring);
}

struct ast_dns_query_recurring *ast_dns_resolve_recurring(const char *name, int rr_type, int rr_class, ast_dns_resolve_callback callback, void *data)
{
	struct ast_dns_query_recurring *recurring;

	if (ast_strlen_zero(name) || !callback || !ast_dns_get_sched()) {
		return NULL;
	}

	recurring = ao2_alloc(sizeof(*recurring) + strlen(name) + 1, dns_query_recurring_destroy);
	if (!recurring) {
		return NULL;
	}

	recurring->callback = callback;
	recurring->user_data = ao2_bump(data);
	recurring->timer = -1;
	recurring->rr_type = rr_type;
	recurring->rr_class = rr_class;
	strcpy(recurring->name, name); /* SAFE */

	recurring->active = ast_dns_resolve_async(name, rr_type, rr_class, dns_query_recurring_resolution_callback, recurring);
	if (!recurring->active) {
		ao2_ref(recurring, -1);
		return NULL;
	}

	return recurring;
}

int ast_dns_resolve_recurring_cancel(struct ast_dns_query_recurring *recurring)
{
	int res = 0;

	ao2_lock(recurring);

	recurring->cancelled = 1;
	AST_SCHED_DEL_UNREF(ast_dns_get_sched(), recurring->timer, ao2_ref(recurring, -1));

	if (recurring->active) {
		res = ast_dns_resolve_cancel(recurring->active);
		ao2_replace(recurring->active, NULL);
	}

	ao2_unlock(recurring);

	return res;
}
