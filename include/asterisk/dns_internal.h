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
 * \brief Internal DNS structure definitions
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*! \brief Generic DNS record information */
struct ast_dns_record {
	/*! \brief Resource record type */
	int rr_type;
	/*! \brief Resource record class */
	int rr_class;
	/*! \brief Time-to-live of the record */
	int ttl;
	/*! \brief The size of the raw DNS record */
	size_t data_len;
	/*! \brief Linked list information */
	AST_LIST_ENTRY(ast_dns_record) list;
	char *data_ptr;
	/*! \brief The raw DNS record */
	char data[0];
};

/*! \brief An SRV record */
struct ast_dns_srv_record {
	/*! \brief Generic DNS record information */
	struct ast_dns_record generic;
	/*! \brief The hostname in the SRV record */
	const char *host;
	/*! \brief The priority of the SRV record */
	unsigned short priority;
	/*! \brief The weight of the SRV record */
	unsigned short weight;
	/*! \brief The port in the SRV record */
	unsigned short port;
	/*! \brief The running weight sum */
	unsigned int weight_sum;
	/*! \brief Additional data */
	char data[0];
};

/*! \brief A NAPTR record */
struct ast_dns_naptr_record {
	/*! \brief Generic DNS record information */
	struct ast_dns_record generic;
	/*! \brief The flags from the NAPTR record */
	const char *flags;
	/*! \brief The service from the NAPTR record */
	const char *service;
	/*! \brief The regular expression from the NAPTR record */
	const char *regexp;
	/*! \brief The replacement from the NAPTR record */
	const char *replacement;
	/*! \brief The order for the NAPTR record */
	unsigned short order;
	/*! \brief The preference of the NAPTR record */
	unsigned short preference;
};

/*! \brief The result of a DNS query */
struct ast_dns_result {
	/*! \brief Whether the result is secure */
	unsigned int secure;
	/*! \brief Whether the result is bogus */
	unsigned int bogus;
	/*! \brief Optional rcode, set if an error occurred */
	unsigned int rcode;
	/*! \brief Records returned */
	AST_LIST_HEAD_NOLOCK(dns_records, ast_dns_record) records;
	/*! \brief The canonical name */
	const char *canonical;
	/*! \brief The raw DNS answer */
	const char *answer;
	/*! \brief The size of the raw DNS answer */
	size_t answer_size;
	/*! \brief Buffer for dynamic data */
	char buf[0];
};

/*! \brief A DNS query */
struct ast_dns_query {
	/*! \brief Callback to invoke upon completion */
	ast_dns_resolve_callback callback;
	/*! \brief User-specific data */
	void *user_data;
	/*! \brief The resolver in use for this query */
	struct ast_dns_resolver *resolver;
	/*! \brief Resolver-specific data */
	void *resolver_data;
	/*! \brief Result of the DNS query */
	struct ast_dns_result *result;
	/*! \brief Resource record type */
	int rr_type;
	/*! \brief Resource record class */
	int rr_class;
	/*! \brief The name of what is being resolved */
	char name[0];
};

/*! \brief A recurring DNS query */
struct ast_dns_query_recurring {
	/*! \brief Callback to invoke upon completion */
	ast_dns_resolve_callback callback;
	/*! \brief User-specific data */
	void *user_data;
	/*! \brief Current active query */
	struct ast_dns_query_active *active;
	/*! \brief The recurring query has been cancelled */
	unsigned int cancelled;
	/*! \brief Scheduled timer for next resolution */
	int timer;
	/*! \brief Resource record type */
	int rr_type;
	/*! \brief Resource record class */
	int rr_class;
	/*! \brief The name of what is being resolved */
	char name[0];
};

/*! \brief An active DNS query */
struct ast_dns_query_active {
	/*! \brief The underlying DNS query */
	struct ast_dns_query *query;
};

struct ast_sched_context;

/*!
 * \brief Retrieve the DNS scheduler context
 *
 * \return scheduler context
 */
struct ast_sched_context *ast_dns_get_sched(void);

/*!
 * \brief Allocate and parse a DNS SRV record
 *
 * \param query The DNS query
 * \param data This specific SRV record
 * \param size The size of the SRV record
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
struct ast_dns_record *ast_dns_srv_alloc(struct ast_dns_query *query, const char *data, const size_t size);

/*!
 * \brief Sort the SRV records on a result
 *
 * \param result The DNS result
 */
void ast_dns_srv_sort(struct ast_dns_result *result);


