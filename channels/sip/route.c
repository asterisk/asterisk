/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

/*!
 * \file
 * \brief sip_route functions
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"

#include "include/route.h"
#include "include/reqresp_parser.h"

/*!
 * \brief Traverse route hops
 */
#define sip_route_traverse(route,elem) AST_LIST_TRAVERSE(&(route)->list, elem, list)
#define sip_route_first(route) AST_LIST_FIRST(&(route)->list)

/*!
 * \brief Structure to save a route hop
 */
struct sip_route_hop {
	AST_LIST_ENTRY(sip_route_hop) list;
	char uri[0];
};

const char *sip_route_add(struct sip_route *route, const char *uri, size_t len, int inserthead)
{
	struct sip_route_hop *hop;

	if (!uri || len < 1 || uri[0] == '\0') {
		return NULL;
	}

	/* Expand len to include null terminator */
	len++;

	/* ast_calloc is not needed because all fields are initialized in this block */
	hop = ast_malloc(sizeof(struct sip_route_hop) + len);
	if (!hop) {
		return NULL;
	}
	ast_copy_string(hop->uri, uri, len);

	if (inserthead) {
		AST_LIST_INSERT_HEAD(&route->list, hop, list);
		route->type = route_invalidated;
	} else {
		if (sip_route_empty(route)) {
			route->type = route_invalidated;
		}
		AST_LIST_INSERT_TAIL(&route->list, hop, list);
		hop->list.next = NULL;
	}

	return hop->uri;
}

void sip_route_process_header(struct sip_route *route, const char *header, int inserthead)
{
	const char *hop;
	int len = 0;
	const char *uri;

	if (!route) {
		ast_log(LOG_ERROR, "sip_route_process_header requires non-null route");
		ast_do_crash();
		return;
	}

	while (!get_in_brackets_const(header, &uri, &len)) {
		header = strchr(header, ',');
		if (header >= uri && header <= (uri + len)) {
			/* comma inside brackets */
			const char *next_br = strchr(header, '<');
			if (next_br && next_br <= (uri + len)) {
				header++;
				continue;
			}
			continue;
		}
		if ((hop = sip_route_add(route, uri, len, inserthead))) {
			ast_debug(2, "sip_route_process_header: <%s>\n", hop);
		}
		header = strchr(uri + len + 1, ',');
		if (header == NULL) {
			/* No more field-values, we're done with this header */
			break;
		}
		/* Advance past comma */
		header++;
	}
}

void sip_route_copy(struct sip_route *dst, const struct sip_route *src)
{
	struct sip_route_hop *hop;

	/* make sure dst is empty */
	sip_route_clear(dst);

	sip_route_traverse(src, hop) {
		const char *uri = sip_route_add(dst, hop->uri, strlen(hop->uri), 0);
		if (uri) {
			ast_debug(2, "sip_route_copy: copied hop: <%s>\n", uri);
		}
	}

	dst->type = src->type;
}

void sip_route_clear(struct sip_route *route)
{
	struct sip_route_hop *hop;

	while ((hop = AST_LIST_REMOVE_HEAD(&route->list, list))) {
		ast_free(hop);
	}

	route->type = route_loose;
}

void sip_route_dump(const struct sip_route *route)
{
	if (sip_route_empty(route)) {
		ast_verbose("sip_route_dump: no route/path\n");
	} else {
		struct sip_route_hop *hop;
		sip_route_traverse(route, hop) {
			ast_verbose("sip_route_dump: route/path hop: <%s>\n", hop->uri);
		}
	}
}

struct ast_str *sip_route_list(const struct sip_route *route, int formatcli, int skip)
{
	struct sip_route_hop *hop;
	const char *comma;
	struct ast_str *buf;
	int i = 0 - skip;

	buf = ast_str_create(64);
	if (!buf) {
		return NULL;
	}

	comma = formatcli ? ", " : ",";

	sip_route_traverse(route, hop) {
		if (i >= 0) {
			ast_str_append(&buf, 0, "%s<%s>", i ? comma : "", hop->uri);
		}
		i++;
	}

	if (formatcli && i <= 0) {
		ast_str_append(&buf, 0, "N/A");
	}

	return buf;
}

int sip_route_is_strict(struct sip_route *route)
{
	if (!route) {
		return 0;
	}

	if (route->type == route_invalidated) {
		struct sip_route_hop *hop = sip_route_first(route);
		int ret = hop && (strstr(hop->uri, ";lr") == NULL);
		route->type = ret ? route_strict : route_loose;
		return ret;
	}

	return (route->type == route_strict) ? 1 : 0;
}

const char *sip_route_first_uri(const struct sip_route *route)
{
	struct sip_route_hop *hop = sip_route_first(route);
	return hop ? hop->uri : NULL;
}
