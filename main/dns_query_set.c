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
 * \brief DNS Query Set API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/vector.h"
#include "asterisk/astobj2.h"
#include "asterisk/dns_core.h"
#include "asterisk/dns_query_set.h"

/*! \brief A set of DNS queries */
struct ast_dns_query_set {
	/*! \brief DNS queries */
	AST_VECTOR(, struct ast_dns_query *) queries;
	/*! \brief The total number of completed queries */
	unsigned int queries_completed;
	/*! \brief Callback to invoke upon completion */
	ast_dns_query_set_callback callback;
	/*! \brief User-specific data */
	void *user_data;
};

struct ast_dns_query_set *ast_dns_query_set_create(void)
{
	return NULL;
}

int ast_dns_query_set_add(struct ast_dns_query_set *query_set, const char *name, int rr_type, int rr_class)
{
	return -1;
}

size_t ast_dns_query_set_num_queries(const struct ast_dns_query_set *query_set)
{
	return 0;
}

struct ast_dns_query *ast_dns_query_set_get(const struct ast_dns_query_set *query_set, unsigned int index)
{
	return NULL;
}

void *ast_dns_query_set_get_data(const struct ast_dns_query_set *query_set)
{
	return query_set->user_data;
}

void ast_dns_query_set_resolve_async(struct ast_dns_query_set *query_set, ast_dns_query_set_callback callback, void *data)
{
	query_set->callback = callback;
	query_set->user_data = ao2_bump(data);
}

void ast_query_set_resolve(struct ast_dns_query_set *query_set)
{
}

int ast_dns_query_set_resolve_cancel(struct ast_dns_query_set *query_set)
{
	return -1;
}

void ast_dns_query_set_free(struct ast_dns_query_set *query_set)
{
}
