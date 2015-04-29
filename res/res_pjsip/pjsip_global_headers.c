/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/linkedlists.h"
#include "include/res_pjsip_private.h"

static pj_status_t add_request_headers(pjsip_tx_data *tdata);
static pj_status_t add_response_headers(pjsip_tx_data *tdata);

/*!
 * \brief Indicator we've already handled a specific request/response
 *
 * PJSIP tends to reuse requests and responses. If we already have added
 * headers to a request or response, we mark the message with this value
 * so that we know not to re-add the headers again.
 */
static unsigned int handled_id = 0xCA115785;

static pjsip_module global_header_mod = {
	.name = {"Global headers", 13},
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_tx_request = add_request_headers,
	.on_tx_response = add_response_headers,
};

struct header {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(value);
	);
	AST_LIST_ENTRY(header) next;
};

static struct header *alloc_header(const char *name, const char *value)
{
	struct header *alloc;
	
	alloc = ast_calloc_with_stringfields(1, struct header, 32);

	if (!alloc) {
		return NULL;
	}

	ast_string_field_set(alloc, name, name);
	ast_string_field_set(alloc, value, value);

	return alloc;
}

static void destroy_header(struct header *to_destroy)
{
	ast_string_field_free_memory(to_destroy);
	ast_free(to_destroy);
}

AST_RWLIST_HEAD(header_list, header);

static struct header_list request_headers;
static struct header_list response_headers;

static void add_headers_to_message(struct header_list *headers, pjsip_tx_data *tdata)
{
	struct header *iter;
	SCOPED_LOCK(lock, headers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	if (tdata->mod_data[global_header_mod.id] == &handled_id) {
		return;
	}
	AST_LIST_TRAVERSE(headers, iter, next) {
		ast_sip_add_header(tdata, iter->name, iter->value);
	};
	tdata->mod_data[global_header_mod.id] = &handled_id;
}

static pj_status_t add_request_headers(pjsip_tx_data *tdata)
{
	add_headers_to_message(&request_headers, tdata);

	return PJ_SUCCESS;
}

static pj_status_t add_response_headers(pjsip_tx_data *tdata)
{
	add_headers_to_message(&response_headers, tdata);

	return PJ_SUCCESS;
}

static void remove_header(struct header_list *headers, const char *to_remove)
{
	struct header *iter;
	AST_LIST_TRAVERSE_SAFE_BEGIN(headers, iter, next) {
		if (!strcasecmp(iter->name, to_remove)) {
			AST_LIST_REMOVE_CURRENT(next);
			destroy_header(iter);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
}

static int add_header(struct header_list *headers, const char *name, const char *value, int replace)
{
	struct header *to_add = NULL;

	if (!ast_strlen_zero(value)) {
		to_add = alloc_header(name, value);
		if (!to_add) {
			return -1;
		}
	}

	AST_RWLIST_WRLOCK(headers);
	if (replace) { 
		remove_header(headers, name);
	}
	if (to_add) {
		AST_LIST_INSERT_TAIL(headers, to_add, next);
	}
	AST_RWLIST_UNLOCK(headers);

	return 0;
}

int ast_sip_add_global_request_header(const char *name, const char *value, int replace)
{
	return add_header(&request_headers, name, value, replace);
}

int ast_sip_add_global_response_header(const char *name, const char *value, int replace)
{
	return add_header(&response_headers, name, value, replace);
}

void ast_sip_initialize_global_headers(void)
{
	AST_RWLIST_HEAD_INIT(&request_headers);
	AST_RWLIST_HEAD_INIT(&response_headers);

	ast_sip_register_service(&global_header_mod);
}

static void destroy_headers(struct header_list *headers)
{
	struct header *iter;

	while ((iter = AST_RWLIST_REMOVE_HEAD(headers, next))) {
		destroy_header(iter);
	}
	AST_RWLIST_HEAD_DESTROY(headers);
}

void ast_sip_destroy_global_headers(void)
{
	destroy_headers(&request_headers);
	destroy_headers(&response_headers);
}
