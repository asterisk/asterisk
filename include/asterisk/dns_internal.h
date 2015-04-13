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

/*! \brief For AST_VECTOR */
#include "asterisk/vector.h"

/*! \brief For ast_dns_query_set_callback */
#include "asterisk/dns_query_set.h"

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
	/*! \brief pointer to record-specific data.
	 *
	 * For certain "subclasses" of DNS records, the
	 * location of the raw DNS data will differ from
	 * the generic case. This pointer will reliably
	 * be set to point to the raw DNS data, no matter
	 * where in the structure it may lie.
	 */
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
	/*! \brief Buffer for NAPTR-specific data
	 *
	 * This includes the raw NAPTR record, as well as
	 * the area where the flags, service, regexp, and
	 * replacement strings are stored.
	 */
	char data[0];
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

/*! \brief A DNS query set query, which includes its state */
struct dns_query_set_query {
	/*! \brief Whether the query started successfully or not */
	unsigned int started;
	/*! \brief THe query itself */
	struct ast_dns_query *query;
};

/*! \brief A set of DNS queries */
struct ast_dns_query_set {
	/*! \brief DNS queries */
	AST_VECTOR(, struct dns_query_set_query) queries;
	/* \brief Whether the query set is in progress or not */
	int in_progress;
	/*! \brief The total number of completed queries */
	int queries_completed;
	/*! \brief The total number of cancelled queries */
	int queries_cancelled;
	/*! \brief Callback to invoke upon completion */
	ast_dns_query_set_callback callback;
	/*! \brief User-specific data */
	void *user_data;
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
 * \brief Allocate and parse a DNS NAPTR record
 *
 * \param query The DNS query
 * \param data This specific NAPTR record
 * \param size The size of the NAPTR record
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
struct ast_dns_record *dns_naptr_alloc(struct ast_dns_query *query, const char *data, const size_t size);

/*!
 * \brief Sort the NAPTR records on a result
 *
 * \param result The DNS result
 */
void dns_naptr_sort(struct ast_dns_result *result);

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
struct ast_dns_record *dns_srv_alloc(struct ast_dns_query *query, const char *data, const size_t size);

/*!
 * \brief Sort the SRV records on a result
 *
 * \param result The DNS result
 */
void dns_srv_sort(struct ast_dns_result *result);

/*!
 * \brief Find the location of a DNS record within the entire DNS answer
 *
 * The DNS record that has been returned by the resolver may be a copy of the record that was
 * found in the complete DNS response. If so, then some DNS record types (specifically those that
 * parse domains) will need to locate the DNS record within the complete DNS response. This is so
 * that if the domain contains pointers to other sections of the DNS response, then the referenced
 * domains may be located.
 *
 * \param record The DNS record returned by a resolver implementation
 * \param record_size The size of the DNS record in bytes
 * \param response The complete DNS answer
 * \param response_size The size of the complete DNS response
 */
char *dns_find_record(const char *record, size_t record_size, const char *response, size_t response_size);

/*!
 * \brief Parse a 16-bit unsigned value from a DNS record
 *
 * \param cur Pointer to the location of the 16-bit value in the DNS record
 * \param[out] val The parsed 16-bit unsigned integer
 * \return The number of bytes consumed while parsing
 */
int dns_parse_short(unsigned char *cur, uint16_t *val);

/*!
 * \brief Parse a DNS string from a DNS record
 *
 * A DNS string consists of an 8-bit size, followed by the
 * string value (not NULL-terminated).
 *
 * \param cur Pointer to the location of the DNS string
 * \param[out] size The parsed size of the DNS string
 * \param[out] val The contained string (not NULL-terminated)
 * \return The number of bytes consumed while parsing
 */
int dns_parse_string(char *cur, uint8_t *size, char **val);

/*!
 * \brief Allocate a DNS query (but do not start resolution)
 *
 * \param name The name of what to resolve
 * \param rr_type Resource record type
 * \param rr_class Resource record class
 * \param callback The callback to invoke upon completion
 * \param data User data to make available on the query
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The result passed to the callback does not need to be freed
 *
 * \note The user data MUST be an ao2 object
 *
 * \note This function increments the reference count of the user data, it does NOT steal
 *
 * \note The query must be released upon completion or cancellation using ao2_ref
 */
struct ast_dns_query *dns_query_alloc(const char *name, int rr_type, int rr_class, ast_dns_resolve_callback callback, void *data);
