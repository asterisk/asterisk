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
 * \brief DNS SRV Record Support
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include "asterisk/dns_core.h"
#include "asterisk/dns_srv.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dns_internal.h"
#include "asterisk/utils.h"

struct ast_dns_record *dns_srv_alloc(struct ast_dns_query *query, const char *data, const size_t size)
{
	uint16_t priority;
	uint16_t weight;
	uint16_t port;
	const char *ptr;
	const char *end_of_record;
	struct ast_dns_srv_record *srv;
	int host_size;
	char host[NI_MAXHOST] = "";

	ptr = dns_find_record(data, size, query->result->answer, query->result->answer_size);
	ast_assert(ptr != NULL);

	end_of_record = ptr + size;

	/* PRIORITY */
	ptr += dns_parse_short((unsigned char *) ptr, &priority);
	if (ptr >= end_of_record) {
		return NULL;
	}

	/* WEIGHT */
	ptr += dns_parse_short((unsigned char *) ptr, &weight);
	if (ptr >= end_of_record) {
		return NULL;
	}

	/* PORT */
	ptr += dns_parse_short((unsigned char *) ptr, &port);
	if (ptr >= end_of_record) {
		return NULL;
	}

	host_size = dn_expand((unsigned char *)query->result->answer, (unsigned char *) end_of_record, (unsigned char *) ptr, host, sizeof(host) - 1);
	if (host_size < 0) {
		ast_log(LOG_ERROR, "Failed to expand domain name: %s\n", strerror(errno));
		return NULL;
	}

	if (!strcmp(host, ".")) {
		return NULL;
	}

	srv = ast_calloc(1, sizeof(*srv) + size + host_size + 1);
	if (!srv) {
		return NULL;
	}

	srv->priority = priority;
	srv->weight = weight;
	srv->port = port;

	srv->host = srv->data + size;
	strcpy((char *)srv->host, host); /* SAFE */
	((char *)srv->host)[host_size] = '\0';

	srv->generic.data_ptr = srv->data;

	return (struct ast_dns_record *)srv;
}

/* This implementation was taken from the existing srv.c which, after reading the RFC, implements it
 * as it should.
 */
void dns_srv_sort(struct ast_dns_result *result)
{
	struct ast_dns_record *current;
	struct dns_records newlist = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

	while (AST_LIST_FIRST(&result->records)) {
		unsigned short cur_priority = 0;
		struct dns_records temp_list = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

		/* Find the lowest current priority to work on */
		AST_LIST_TRAVERSE(&result->records, current, list) {
			if (!cur_priority || ((struct ast_dns_srv_record *)current)->priority < cur_priority) {
				cur_priority = ((struct ast_dns_srv_record *)current)->priority;
			}
		}

		/* Find all records which match this priority */
		AST_LIST_TRAVERSE_SAFE_BEGIN(&result->records, current, list) {
			if (((struct ast_dns_srv_record *)current)->priority != cur_priority) {
				continue;
			}

			AST_LIST_REMOVE_CURRENT(list);

			/* Records with a weight of zero must always be at the head */
			if (((struct ast_dns_srv_record *)current)->weight == 0) {
				AST_LIST_INSERT_HEAD(&temp_list, current, list);
			} else {
				AST_LIST_INSERT_TAIL(&temp_list, current, list);
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;

		/* Apply weighting - as each record is passed the sum of all previous weights (plus its own) is stored away, and then a random weight
		 * is calculated. The first record with a weight sum greater than the random weight is put in the new list and the whole thing starts
		 * once again.
		 */
		while (AST_LIST_FIRST(&temp_list)) {
			unsigned int weight_sum = 0;
			unsigned int random_weight;

			AST_LIST_TRAVERSE(&temp_list, current, list) {
				((struct ast_dns_srv_record *)current)->weight_sum = weight_sum += ((struct ast_dns_srv_record *)current)->weight;
			}

			/* if all the remaining entries have weight == 0,
			   then just append them to the result list and quit */
			if (weight_sum == 0) {
				AST_LIST_APPEND_LIST(&newlist, &temp_list, list);
				break;
			}

			random_weight = 1 + (unsigned int) ((float) weight_sum * (ast_random() / ((float) RAND_MAX + 1.0)));

			AST_LIST_TRAVERSE_SAFE_BEGIN(&temp_list, current, list) {
				if (((struct ast_dns_srv_record *)current)->weight_sum < random_weight) {
					continue;
				}

				AST_LIST_MOVE_CURRENT(&newlist, list);
				break;
			}
			AST_LIST_TRAVERSE_SAFE_END;
		}

	}

	/* now that the new list has been ordered,
	   put it in place */

	AST_LIST_APPEND_LIST(&result->records, &newlist, list);
}

const char *ast_dns_srv_get_host(const struct ast_dns_record *record)
{
	struct ast_dns_srv_record *srv = (struct ast_dns_srv_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == ns_t_srv);
	return srv->host;
}

unsigned short ast_dns_srv_get_priority(const struct ast_dns_record *record)
{
	struct ast_dns_srv_record *srv = (struct ast_dns_srv_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == ns_t_srv);
	return srv->priority;
}

unsigned short ast_dns_srv_get_weight(const struct ast_dns_record *record)
{
	struct ast_dns_srv_record *srv = (struct ast_dns_srv_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == ns_t_srv);
	return srv->weight;
}

unsigned short ast_dns_srv_get_port(const struct ast_dns_record *record)
{
	struct ast_dns_srv_record *srv = (struct ast_dns_srv_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == ns_t_srv);
	return srv->port;
}
