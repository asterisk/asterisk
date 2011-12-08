/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Funding provided by nic.at
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
 * \brief DNS SRV Record Lookup Support for Asterisk
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \arg See also \ref AstENUM
 *
 * \note Funding provided by nic.at
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <netinet/in.h>
#include <arpa/nameser.h>
#ifdef __APPLE__
#if __APPLE_CC__ >= 1495
#include <arpa/nameser_compat.h>
#endif
#endif
#include <resolv.h>

#include "asterisk/channel.h"
#include "asterisk/srv.h"
#include "asterisk/dns.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"

#ifdef __APPLE__
#undef T_SRV
#define T_SRV 33
#endif

struct srv_entry {
	unsigned short priority;
	unsigned short weight;
	unsigned short port;
	unsigned int weight_sum;
	AST_LIST_ENTRY(srv_entry) list;
	char host[1];
};

struct srv_context {
	unsigned int have_weights:1;
	struct srv_entry *prev;
	unsigned int num_records;
	AST_LIST_HEAD_NOLOCK(srv_entries, srv_entry) entries;
};

static int parse_srv(unsigned char *answer, int len, unsigned char *msg, struct srv_entry **result)
{
	struct srv {
		unsigned short priority;
		unsigned short weight;
		unsigned short port;
	} __attribute__((__packed__)) *srv = (struct srv *) answer;

	int res = 0;
	char repl[256] = "";
	struct srv_entry *entry;

	if (len < sizeof(*srv))
		return -1;

	answer += sizeof(*srv);
	len -= sizeof(*srv);

	if ((res = dn_expand(msg, answer + len, answer, repl, sizeof(repl) - 1)) <= 0) {
		ast_log(LOG_WARNING, "Failed to expand hostname\n");
		return -1;
	}

	/* the magic value "." for the target domain means that this service
	   is *NOT* available at the domain we searched */
	if (!strcmp(repl, "."))
		return -1;

	if (!(entry = ast_calloc(1, sizeof(*entry) + strlen(repl))))
		return -1;
	
	entry->priority = ntohs(srv->priority);
	entry->weight = ntohs(srv->weight);
	entry->port = ntohs(srv->port);
	strcpy(entry->host, repl);

	*result = entry;
	
	return 0;
}

static int srv_callback(void *context, unsigned char *answer, int len, unsigned char *fullanswer)
{
	struct srv_context *c = (struct srv_context *) context;
	struct srv_entry *entry = NULL;
	struct srv_entry *current;

	if (parse_srv(answer, len, fullanswer, &entry))
		return -1;

	if (entry->weight)
		c->have_weights = 1;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&c->entries, current, list) {
		/* insert this entry just before the first existing
		   entry with a higher priority */
		if (current->priority <= entry->priority)
			continue;

		AST_LIST_INSERT_BEFORE_CURRENT(entry, list);
		entry = NULL;
		break;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* if we didn't find a place to insert the entry before an existing
	   entry, then just add it to the end */
	if (entry)
		AST_LIST_INSERT_TAIL(&c->entries, entry, list);

	return 0;
}

/* Do the bizarre SRV record weight-handling algorithm
   involving sorting and random number generation...

   See RFC 2782 if you want know why this code does this
*/
static void process_weights(struct srv_context *context)
{
	struct srv_entry *current;
	struct srv_entries newlist = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

	while (AST_LIST_FIRST(&context->entries)) {
		unsigned int random_weight;
		unsigned int weight_sum;
		unsigned short cur_priority = AST_LIST_FIRST(&context->entries)->priority;
		struct srv_entries temp_list = AST_LIST_HEAD_NOLOCK_INIT_VALUE;
		weight_sum = 0;

		AST_LIST_TRAVERSE_SAFE_BEGIN(&context->entries, current, list) {
			if (current->priority != cur_priority)
				break;

			AST_LIST_MOVE_CURRENT(&temp_list, list);
		}
		AST_LIST_TRAVERSE_SAFE_END;

		while (AST_LIST_FIRST(&temp_list)) {
			weight_sum = 0;
			AST_LIST_TRAVERSE(&temp_list, current, list)
				current->weight_sum = weight_sum += current->weight;

			/* if all the remaining entries have weight == 0,
			   then just append them to the result list and quit */
			if (weight_sum == 0) {
				AST_LIST_APPEND_LIST(&newlist, &temp_list, list);
				break;
			}

			random_weight = 1 + (unsigned int) ((float) weight_sum * (ast_random() / ((float) RAND_MAX + 1.0)));

			AST_LIST_TRAVERSE_SAFE_BEGIN(&temp_list, current, list) {
				if (current->weight < random_weight)
					continue;

				AST_LIST_MOVE_CURRENT(&newlist, list);
				break;
			}
			AST_LIST_TRAVERSE_SAFE_END;
		}

	}

	/* now that the new list has been ordered,
	   put it in place */

	AST_LIST_APPEND_LIST(&context->entries, &newlist, list);
}

int ast_srv_lookup(struct srv_context **context, const char *service, const char **host, unsigned short *port)
{
	struct srv_entry *cur;

	if (*context == NULL) {
		if (!(*context = ast_calloc(1, sizeof(struct srv_context)))) {
			return -1;
		}
		AST_LIST_HEAD_INIT_NOLOCK(&(*context)->entries);

		if ((ast_search_dns(*context, service, C_IN, T_SRV, srv_callback)) < 0) {
			ast_free(*context);
			*context = NULL;
			return -1;
		}

		if ((*context)->have_weights) {
			process_weights(*context);
		}

		(*context)->prev = AST_LIST_FIRST(&(*context)->entries);
		*host = (*context)->prev->host;
		*port = (*context)->prev->port;
		AST_LIST_TRAVERSE(&(*context)->entries, cur, list) {
			++((*context)->num_records);
		}
		return 0;
	}

	if (((*context)->prev = AST_LIST_NEXT((*context)->prev, list))) {
		/* Retrieve next item in result */
		*host = (*context)->prev->host;
		*port = (*context)->prev->port;
		return 0;
	} else {
		/* No more results */
		while ((cur = AST_LIST_REMOVE_HEAD(&(*context)->entries, list))) {
			ast_free(cur);
		}
		ast_free(*context);
		*context = NULL;
		return 1;
	}
}

void ast_srv_cleanup(struct srv_context **context)
{
	const char *host;
	unsigned short port;

	if (*context) {
		/* We have a context to clean up. */
		while (!(ast_srv_lookup(context, NULL, &host, &port))) {
		}
	}
}

int ast_get_srv(struct ast_channel *chan, char *host, int hostlen, int *port, const char *service)
{
	struct srv_context context = { .entries = AST_LIST_HEAD_NOLOCK_INIT_VALUE };
	struct srv_entry *current;
	int ret;

	if (chan && ast_autoservice_start(chan) < 0) {
		return -1;
	}

	ret = ast_search_dns(&context, service, C_IN, T_SRV, srv_callback);

	if (context.have_weights) {
		process_weights(&context);
	}

	if (chan) {
		ret |= ast_autoservice_stop(chan);
	}

	/* TODO: there could be a "." entry in the returned list of
	   answers... if so, this requires special handling */

	/* the list of entries will be sorted in the proper selection order
	   already, so we just need the first one (if any) */

	if ((ret > 0) && (current = AST_LIST_REMOVE_HEAD(&context.entries, list))) {
		ast_copy_string(host, current->host, hostlen);
		*port = current->port;
		ast_free(current);
		ast_verb(4, "ast_get_srv: SRV lookup for '%s' mapped to host %s, port %d\n",
				    service, host, *port);
	} else {
		host[0] = '\0';
		*port = -1;
	}

	while ((current = AST_LIST_REMOVE_HEAD(&context.entries, list))) {
		ast_free(current);
	}

	return ret;
}

unsigned int ast_srv_get_record_count(struct srv_context *context)
{
	return context->num_records;
}

int ast_srv_get_nth_record(struct srv_context *context, int record_num, const char **host,
		unsigned short *port, unsigned short *priority, unsigned short *weight)
{
	int i = 1;
	int res = -1;
	struct srv_entry *entry;

	if (record_num < 1 || record_num > context->num_records) {
		return res;
	}

	AST_LIST_TRAVERSE(&context->entries, entry, list) {
		if (i == record_num) {
			*host = entry->host;
			*port = entry->port;
			*priority = entry->priority;
			*weight = entry->weight;
			res = 0;
			break;
		}
		++i;
	}

	return res;
}
