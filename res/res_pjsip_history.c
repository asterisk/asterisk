/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2015, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief PJSIP History
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <regex.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/netsock2.h"
#include "asterisk/vector.h"
#include "asterisk/lock.h"
#include "asterisk/res_pjproject.h"

#define HISTORY_INITIAL_SIZE 256

/*! \brief Pool factory used by pjlib to allocate memory. */
static pj_caching_pool cachingpool;

/*! \brief Whether or not we are storing history */
static int enabled;

/*! \brief Packet count */
static int packet_number;

/*! \brief An item in the history */
struct pjsip_history_entry {
	/*! \brief Packet number */
	int number;
	/*! \brief Whether or not we transmitted the packet */
	int transmitted;
	/*! \brief Time the packet was transmitted/received */
	struct timeval timestamp;
	/*! \brief Source address */
	pj_sockaddr_in src;
	/*! \brief Destination address */
	pj_sockaddr_in dst;
	/*! \brief Memory pool used to allocate \c msg */
	pj_pool_t *pool;
	/*! \brief The actual SIP message */
	pjsip_msg *msg;
};

/*! \brief Mutex that protects \ref vector_history */
AST_MUTEX_DEFINE_STATIC(history_lock);

/*! \brief The one and only history that we've captured */
static AST_VECTOR(vector_history_t, struct pjsip_history_entry *) vector_history;

struct expression_token;

/*! \brief An operator that we understand in an expression */
struct operator {
	/*! \brief Our operator's symbol */
	const char *symbol;
	/*! \brief Precedence of the symbol */
	int precedence;
	/*! \brief Non-zero if the operator is evaluated right-to-left */
	int right_to_left;
	/*! \brief Number of operands the operator takes */
	int operands;
	/*!
	 * \brief Evaluation function for unary operators
	 *
	 * \param op The operator being evaluated
	 * \param type The type of value contained in \c operand
	 * \param operand A pointer to the value to evaluate
	 *
	 * \retval -1 error
	 * \retval 0 evaluation is False
	 * \retval 1 evaluation is True
	 */
	int (* const evaluate_unary)(struct operator *op, enum aco_option_type type, void *operand);
	/*!
	 * \brief Evaluation function for binary operators
	 *
	 * \param op The operator being evaluated
	 * \param type The type of value contained in \c op_left
	 * \param op_left A pointer to the value to evaluate (a result or extracted from an entry)
	 * \param op_right The expression token containing the other value (a result or user-provided)
	 *
	 * \retval -1 error
	 * \retval 0 evaluation is False
	 * \retval 1 evaluation is True
	 */
	int (* const evaluate)(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right);
};

/*! \brief A field that we understand and can perform operations on */
struct allowed_field {
	/*! \brief The representation of the field */
	const char *symbol;
	/*! \brief The type /c get_field returns */
	enum aco_option_type return_type;
	/*!
	 * \brief Function that returns the field from a pjsip_history_entry
	 *
	 * Note that the function must return a pointer to the location in
	 * \c pjsip_history_entry - no memory should be allocated as the caller
	 * will not dispose of any
	 */
	void *(* const get_field)(struct pjsip_history_entry *entry);
};

/*! \brief The type of token that has been parsed out of an expression */
enum expression_token_type {
	/*! The \c expression_token contains a field */
	TOKEN_TYPE_FIELD,
	/*! The \c expression_token contains an operator */
	TOKEN_TYPE_OPERATOR,
	/*! The \c expression_token contains a previous result */
	TOKEN_TYPE_RESULT
};

/*! \brief A token in the expression or an evaluated part of the expression */
struct expression_token {
	/*! \brief The next expression token in the queue */
	struct expression_token *next;
	/*! \brief The type of value stored in the expression token */
	enum expression_token_type token_type;
	/*! \brief An operator that evaluates expressions */
	struct operator *op;
	/*! \brief The result of an evaluated expression */
	int result;
	/*! \brief The field in the expression */
	char field[];
};

/*! \brief Log level for history output */
static int log_level = -1;

/*!
 * \brief Operator callback for determining equality
 */
static int evaluate_equal(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right)
{
	switch (type) {
	case OPT_BOOL_T:
	case OPT_BOOLFLAG_T:
	case OPT_INT_T:
	case OPT_UINT_T:
	{
		int right;

		if (sscanf(op_right->field, "%30d", &right) != 1) {
			ast_log(LOG_WARNING, "Unable to extract field '%s': not an integer\n", op_right->field);
			return -1;
		}
		return (*(int *)op_left) == right;
	}
	case OPT_DOUBLE_T:
	{
		double right;

		if (sscanf(op_right->field, "%lf", &right) != 1) {
			ast_log(LOG_WARNING, "Unable to extract field '%s': not a double\n", op_right->field);
			return -1;
		}
		return (*(double *)op_left) == right;
	}
	case OPT_CHAR_ARRAY_T:
	case OPT_STRINGFIELD_T:
		/* In our case, we operate on pj_str_t */
		return pj_strcmp2(op_left, op_right->field) == 0;
	case OPT_NOOP_T:
	/* Used for timeval */
	{
		struct timeval right = { 0, };

		if (sscanf(op_right->field, "%ld", &right.tv_sec) != 1) {
			ast_log(LOG_WARNING, "Unable to extract field '%s': not a timestamp\n", op_right->field);
			return -1;
		}

		return ast_tvcmp(*(struct timeval *)op_left, right) == 0;
	}
	case OPT_SOCKADDR_T:
	/* In our case, we operate only on pj_sockaddr_t */
	{
		pj_sockaddr right;
		pj_str_t str_right;

		pj_cstr(&str_right, op_right->field);
		if (pj_sockaddr_parse(pj_AF_UNSPEC(), 0, &str_right, &right) != PJ_SUCCESS) {
			ast_log(LOG_WARNING, "Unable to convert field '%s': not an IPv4 or IPv6 address\n", op_right->field);
			return -1;
		}

		return pj_sockaddr_cmp(op_left, &right) == 0;
	}
	default:
		ast_log(LOG_WARNING, "Cannot evaluate field '%s': invalid type for operator '%s'\n",
			op_right->field, op->symbol);
	}

	return -1;
}

/*!
 * \brief Operator callback for determining inequality
 */
static int evaluate_not_equal(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right)
{
	return !evaluate_equal(op, type, op_left, op_right);
}

/*
 * \brief Operator callback for determining if one operand is less than another
 */
static int evaluate_less_than(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right)
{
	switch (type) {
	case OPT_BOOL_T:
	case OPT_BOOLFLAG_T:
	case OPT_INT_T:
	case OPT_UINT_T:
	{
		int right;

		if (sscanf(op_right->field, "%30d", &right) != 1) {
			ast_log(LOG_WARNING, "Unable to extract field '%s': not an integer\n", op_right->field);
			return -1;
		}
		return (*(int *)op_left) < right;
	}
	case OPT_DOUBLE_T:
	{
		double right;

		if (sscanf(op_right->field, "%lf", &right) != 1) {
			ast_log(LOG_WARNING, "Unable to extract field '%s': not a double\n", op_right->field);
			return -1;
		}
		return (*(double *)op_left) < right;
	}
	case OPT_NOOP_T:
	/* Used for timeval */
	{
		struct timeval right = { 0, };

		if (sscanf(op_right->field, "%ld", &right.tv_sec) != 1) {
			ast_log(LOG_WARNING, "Unable to extract field '%s': not a timestamp\n", op_right->field);
			return -1;
		}

		return ast_tvcmp(*(struct timeval *)op_left, right) == -1;
	}
	default:
		ast_log(LOG_WARNING, "Cannot evaluate field '%s': invalid type for operator '%s'\n",
			op_right->field, op->symbol);
	}

	return -1;
}

/*
 * \brief Operator callback for determining if one operand is greater than another
 */
static int evaluate_greater_than(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right)
{
	switch (type) {
	case OPT_BOOL_T:
	case OPT_BOOLFLAG_T:
	case OPT_INT_T:
	case OPT_UINT_T:
	{
		int right;

		if (sscanf(op_right->field, "%30d", &right) != 1) {
			ast_log(LOG_WARNING, "Unable to extract field '%s': not an integer\n", op_right->field);
			return -1;
		}
		return (*(int *)op_left) > right;
	}
	case OPT_DOUBLE_T:
	{
		double right;

		if (sscanf(op_right->field, "%lf", &right) != 1) {
			ast_log(LOG_WARNING, "Unable to extract field '%s': not a double\n", op_right->field);
			return -1;
		}
		return (*(double *)op_left) > right;
	}
	case OPT_NOOP_T:
	/* Used for timeval */
	{
		struct timeval right = { 0, };

		if (sscanf(op_right->field, "%ld", &right.tv_sec) != 1) {
			ast_log(LOG_WARNING, "Unable to extract field '%s': not a timestamp\n", op_right->field);
			return -1;
		}

		return ast_tvcmp(*(struct timeval *)op_left, right) == 1;
	}
	default:
		ast_log(LOG_WARNING, "Cannot evaluate field '%s': invalid type for operator '%s'\n",
			op_right->field, op->symbol);
	}

	return -1;
}

/*
 * \brief Operator callback for determining if one operand is less than or equal to another
 */
static int evaluate_less_than_or_equal(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right)
{
	return !evaluate_greater_than(op, type, op_left, op_right);
}

/*
 * \brief Operator callback for determining if one operand is greater than or equal to another
 */
static int evaluate_greater_than_or_equal(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right)
{
	return !evaluate_less_than(op, type, op_left, op_right);
}

/*
 * \brief Operator callback for determining logical NOT
 */
static int evaluate_not(struct operator *op, enum aco_option_type type, void *operand)
{
	switch (type) {
	case OPT_BOOL_T:
	case OPT_BOOLFLAG_T:
	case OPT_INT_T:
	case OPT_UINT_T:
		return !(*(int *)operand);
	default:
		ast_log(LOG_WARNING, "Cannot evaluate: invalid operand type for operator '%s'\n", op->symbol);
	}

	return -1;
}

/*
 * \brief Operator callback for determining logical AND
 */
static int evaluate_and(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right)
{
	switch (type) {
	case OPT_BOOL_T:
	case OPT_BOOLFLAG_T:
	case OPT_INT_T:
	case OPT_UINT_T:
		return (*(int *)op_left && op_right->result);
	default:
		ast_log(LOG_WARNING, "Cannot evaluate: invalid operand type for operator '%s'\n", op->symbol);
	}

	return -1;
}

/*
 * \brief Operator callback for determining logical OR
 */
static int evaluate_or(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right)
{
	switch (type) {
	case OPT_BOOL_T:
	case OPT_BOOLFLAG_T:
	case OPT_INT_T:
	case OPT_UINT_T:
		return (*(int *)op_left || op_right->result);
	default:
		ast_log(LOG_WARNING, "Cannot evaluate: invalid operand type for operator '%s'\n", op->symbol);
	}

	return -1;
}

/*
 * \brief Operator callback for regex 'like'
 */
static int evaluate_like(struct operator *op, enum aco_option_type type, void *op_left, struct expression_token *op_right)
{
	switch (type) {
	case OPT_CHAR_ARRAY_T:
	case OPT_STRINGFIELD_T:
	/* In our case, we operate on pj_str_t */
	{
		int result;
		regex_t regexbuf;
		char buf[pj_strlen(op_left) + 1];

		ast_copy_pj_str(buf, op_left, pj_strlen(op_left));
		if (regcomp(&regexbuf, op_right->field, REG_EXTENDED | REG_NOSUB)) {
			ast_log(LOG_WARNING, "Failed to compile '%s' into a regular expression\n", op_right->field);
			return -1;
		}

		result = (regexec(&regexbuf, buf, 0, NULL, 0) == 0);
		regfree(&regexbuf);

		return result;
	}
	default:
		ast_log(LOG_WARNING, "Cannot evaluate: invalid operand type for operator '%s'\n", op->symbol);
	}

	return -1;
}

/*!
 * \brief Operator token for a left parenthesis.
 *
 * While this is used by the shunting-yard algorithm implementation,
 * it should never appear in the resulting RPN queue of expression tokens
 */
static struct operator left_paren = {
	.symbol = "(",
	.precedence = 15
};

/*!
 * \brief Our allowed operations
 */
static struct operator allowed_operators[] = {
	{ .symbol = "=", .precedence = 7, .operands = 2, .evaluate = evaluate_equal, },
	{ .symbol = "==", .precedence = 7, .operands = 2, .evaluate = evaluate_equal, },
	{ .symbol = "!=", .precedence = 7, .operands = 2, .evaluate = evaluate_not_equal, },
	{ .symbol = "<", .precedence = 6, .operands = 2, .evaluate = evaluate_less_than, },
	{ .symbol = ">", .precedence = 6, .operands = 2, .evaluate = evaluate_greater_than, },
	{ .symbol = "<=", .precedence = 6, .operands = 2, .evaluate = evaluate_less_than_or_equal, },
	{ .symbol = ">=", .precedence = 6, .operands = 2, .evaluate = evaluate_greater_than_or_equal, },
	{ .symbol = "!", .precedence = 2, .operands = 1, .right_to_left = 1, .evaluate_unary = evaluate_not, },
	{ .symbol = "&&", .precedence = 11, .operands = 2, .evaluate = evaluate_and, },
	{ .symbol = "||", .precedence = 12, .operands = 2, .evaluate = evaluate_or, },
	{ .symbol = "like", .precedence = 7, .operands = 2, .evaluate = evaluate_like, },
	{ .symbol = "and", .precedence = 11, .operands = 2, .evaluate = evaluate_and, },
	{ .symbol = "or", .precedence = 11, .operands = 2, .evaluate = evaluate_or, },
	{ .symbol = "not", .precedence = 2, .operands = 1, .right_to_left = 1, .evaluate_unary = evaluate_not, },
};

/*! \brief Callback to retrieve the entry index number */
static void *entry_get_number(struct pjsip_history_entry *entry)
{
	return &entry->number;
}

/*! \brief Callback to retrieve the entry's timestamp */
static void *entry_get_timestamp(struct pjsip_history_entry *entry)
{
	return &entry->timestamp;
}

/*! \brief Callback to retrieve the entry's destination address */
static void *entry_get_addr(struct pjsip_history_entry *entry)
{
	if (entry->transmitted) {
		return &entry->dst;
	} else {
		return &entry->src;
	}
}

/*! \brief Callback to retrieve the entry's SIP request method type */
static void *entry_get_sip_msg_request_method(struct pjsip_history_entry *entry)
{
	if (entry->msg->type != PJSIP_REQUEST_MSG) {
		return NULL;
	}

	return &entry->msg->line.req.method.name;
}

/*! \brief Callback to retrieve the entry's SIP Call-ID header */
static void *entry_get_sip_msg_call_id(struct pjsip_history_entry *entry)
{
	pjsip_cid_hdr *cid_hdr;

	cid_hdr = PJSIP_MSG_CID_HDR(entry->msg);

	return &cid_hdr->id;
}

/*! \brief The fields we allow */
static struct allowed_field allowed_fields[] = {
	{ .symbol = "number", .return_type = OPT_INT_T, .get_field = entry_get_number, },
	/* We co-op the NOOP type here for timeval */
	{ .symbol = "timestamp", .return_type = OPT_NOOP_T, .get_field = entry_get_timestamp, },
	{ .symbol = "addr", .return_type = OPT_SOCKADDR_T, .get_field = entry_get_addr, },
	{ .symbol = "sip.msg.request.method", .return_type = OPT_CHAR_ARRAY_T, .get_field = entry_get_sip_msg_request_method, },
	{ .symbol = "sip.msg.call-id", .return_type = OPT_CHAR_ARRAY_T, .get_field = entry_get_sip_msg_call_id, },
};

/*! \brief Free an expression token and all others it references */
static struct expression_token *expression_token_free(struct expression_token *token)
{
	struct expression_token *it_token;

	it_token = token;
	while (it_token) {
		struct expression_token *prev = it_token;

		it_token = it_token->next;
		ast_free(prev);
	}

	return NULL;
}

/*!
 * \brief Allocate an expression token
 *
 * \param token_type The type of token in the expression
 * \param value The value/operator/result to pack into the token
 *
 * \retval NULL on failure
 * \retval \c expression_token on success
 */
static struct expression_token *expression_token_alloc(enum expression_token_type token_type, void *value)
{
	struct expression_token *token;

	switch (token_type) {
	case TOKEN_TYPE_RESULT:
	case TOKEN_TYPE_OPERATOR:
		token = ast_calloc(1, sizeof(*token));
		break;
	case TOKEN_TYPE_FIELD:
		token = ast_calloc(1, sizeof(*token) + strlen((const char *)value) + 1);
		break;
	default:
		ast_assert(0);
		return NULL;
	}

	if (!token) {
		return NULL;
	}
	token->token_type = token_type;

	switch (token_type) {
	case TOKEN_TYPE_RESULT:
		token->result = *(int *)value;
		break;
	case TOKEN_TYPE_OPERATOR:
		token->op = value;
		break;
	case TOKEN_TYPE_FIELD:
		strcpy(token->field, value); /* safe */
		break;
	default:
		ast_assert(0);
	}

	return token;
}

/*! \brief Determine if the expression token matches a field in \c allowed_fields */
static struct allowed_field *get_allowed_field(struct expression_token *token)
{
	int i;

	ast_assert(token->token_type == TOKEN_TYPE_FIELD);

	for (i = 0; i < ARRAY_LEN(allowed_fields); i++) {
		if (strcasecmp(allowed_fields[i].symbol, token->field)) {
			continue;
		}

		return &allowed_fields[i];
	}

	return NULL;
}

/*! \brief AO2 destructor for \c pjsip_history_entry */
static void pjsip_history_entry_dtor(void *obj)
{
	struct pjsip_history_entry *entry = obj;

	if (entry->pool) {
		/* This mimics the behavior of pj_pool_safe_release
		 * which was introduced in pjproject 2.6.
		 */
		pj_pool_t *temp_pool = entry->pool;

		entry->pool = NULL;
		pj_pool_release(temp_pool);
	}
}

/*!
 * \brief Create a \c pjsip_history_entry AO2 object
 *
 * \param msg The PJSIP message that this history entry wraps
 *
 * \retval An AO2 \c pjsip_history_entry object on success
 * \retval NULL on failure
 */
static struct pjsip_history_entry *pjsip_history_entry_alloc(pjsip_msg *msg)
{
	struct pjsip_history_entry *entry;

	entry = ao2_alloc_options(sizeof(*entry), pjsip_history_entry_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!entry) {
		return NULL;
	}
	entry->number = ast_atomic_fetchadd_int(&packet_number, 1);
	entry->timestamp = ast_tvnow();
	entry->timestamp.tv_usec = 0;

	entry->pool = pj_pool_create(&cachingpool.factory, NULL, PJSIP_POOL_RDATA_LEN,
	                             PJSIP_POOL_RDATA_INC, NULL);
	if (!entry->pool) {
		ao2_ref(entry, -1);
		return NULL;
	}

	entry->msg = pjsip_msg_clone(entry->pool, msg);
	if (!entry->msg) {
		ao2_ref(entry, -1);
		return NULL;
	}

	return entry;
}

/*! \brief Format single line history entry */
static void sprint_list_entry(struct pjsip_history_entry *entry, char *line, int len)
{
	char addr[64];

	if (entry->transmitted) {
		pj_sockaddr_print(&entry->dst, addr, sizeof(addr), 3);
	} else {
		pj_sockaddr_print(&entry->src, addr, sizeof(addr), 3);
	}

	if (entry->msg->type == PJSIP_REQUEST_MSG) {
		char uri[128];

		pjsip_uri_print(PJSIP_URI_IN_REQ_URI, entry->msg->line.req.uri, uri, sizeof(uri));
		snprintf(line, len, "%-5.5d %-10.10ld %-5.5s %-24.24s %.*s %s SIP/2.0",
			entry->number,
			entry->timestamp.tv_sec,
			entry->transmitted ? "* ==>" : "* <==",
			addr,
			(int)pj_strlen(&entry->msg->line.req.method.name),
			pj_strbuf(&entry->msg->line.req.method.name),
			uri);
	} else {
		snprintf(line, len, "%-5.5d %-10.10ld %-5.5s %-24.24s SIP/2.0 %u %.*s",
			entry->number,
			entry->timestamp.tv_sec,
			entry->transmitted ? "* ==>" : "* <==",
			addr,
			entry->msg->line.status.code,
			(int)pj_strlen(&entry->msg->line.status.reason),
			pj_strbuf(&entry->msg->line.status.reason));
	}
}

/*! \brief PJSIP callback when a SIP message is transmitted */
static pj_status_t history_on_tx_msg(pjsip_tx_data *tdata)
{
	struct pjsip_history_entry *entry;

	if (!enabled) {
		return PJ_SUCCESS;
	}

	entry = pjsip_history_entry_alloc(tdata->msg);
	if (!entry) {
		return PJ_SUCCESS;
	}
	entry->transmitted = 1;
	pj_sockaddr_cp(&entry->src, &tdata->tp_info.transport->local_addr);
	pj_sockaddr_cp(&entry->dst, &tdata->tp_info.dst_addr);

	ast_mutex_lock(&history_lock);
	if (AST_VECTOR_APPEND(&vector_history, entry)) {
		ao2_ref(entry, -1);
		entry = NULL;
	}
	ast_mutex_unlock(&history_lock);

	if (log_level != -1 && entry) {
		char line[256];

		sprint_list_entry(entry, line, sizeof(line));
		ast_log_dynamic_level(log_level, "%s\n", line);
	}

	return PJ_SUCCESS;
}

/*! \brief PJSIP callback when a SIP message is received */
static pj_bool_t history_on_rx_msg(pjsip_rx_data *rdata)
{
	struct pjsip_history_entry *entry;

	if (!enabled) {
		return PJ_FALSE;
	}

	if (!rdata->msg_info.msg) {
		return PJ_FALSE;
	}

	entry = pjsip_history_entry_alloc(rdata->msg_info.msg);
	if (!entry) {
		return PJ_FALSE;
	}

	if (rdata->tp_info.transport->addr_len) {
		pj_sockaddr_cp(&entry->dst, &rdata->tp_info.transport->local_addr);
	}

	if (rdata->pkt_info.src_addr_len) {
		pj_sockaddr_cp(&entry->src, &rdata->pkt_info.src_addr);
	}

	ast_mutex_lock(&history_lock);
	if (AST_VECTOR_APPEND(&vector_history, entry)) {
		ao2_ref(entry, -1);
		entry = NULL;
	}
	ast_mutex_unlock(&history_lock);

	if (log_level != -1 && entry) {
		char line[256];

		sprint_list_entry(entry, line, sizeof(line));
		ast_log_dynamic_level(log_level, "%s\n", line);
	}

	return PJ_FALSE;
}

/*! \brief Vector callback that releases the reference for the entry in a history vector */
static void clear_history_entry_cb(struct pjsip_history_entry *entry)
{
	ao2_ref(entry, -1);
}

/*!
 * \brief Remove all entries from \ref vector_history
 *
 * This must be called from a registered PJSIP thread
 */
static int clear_history_entries(void *obj)
{
	ast_mutex_lock(&history_lock);
	AST_VECTOR_RESET(&vector_history, clear_history_entry_cb);
	packet_number = 0;
	ast_mutex_unlock(&history_lock);

	return 0;
}

/*!
 * \brief Build a reverse polish notation expression queue
 *
 * This function is an implementation of the Shunting-Yard Algorithm. It takes
 * a user provided infix-notation expression and converts it into a reverse
 * polish notation expression, which is a queue of tokens that can be easily
 * parsed.
 *
 * \params a The CLI arguments provided by the User, containing the infix expression
 *
 * \retval NULL error
 * \retval expression_token A 'queue' of expression tokens in RPN
 */
static struct expression_token *build_expression_queue(struct ast_cli_args *a)
{
	AST_VECTOR(, struct operator *) operators; /* A stack of saved operators */
	struct expression_token *output = NULL;    /* The output queue */
	struct expression_token *head = NULL;      /* Pointer to the head of /c output */
	int i;

#define APPEND_TO_OUTPUT(output, token) do { \
	if ((output)) { \
		(output)->next = (token); \
		(output) = (token); \
	} else { \
		(output) = (token); \
		head = (output); \
	} \
} while (0)

	if (AST_VECTOR_INIT(&operators, 8)) {
		return NULL;
	}

	for (i = 4; i < a->argc; i++) {
		struct expression_token *out_token;
		char *token = ast_strdupa(a->argv[i]);
		int j;

		/* Strip off and append any left parentheses */
		if (token[0] == '(') {
			AST_VECTOR_APPEND(&operators, &left_paren);
			if (!token[1]) {
				continue;
			}
			token = &token[1];
		}

		/* Handle the case where the token is an operator */
		for (j = 0; j < ARRAY_LEN(allowed_operators); j++) {
			int k;

			if (strcasecmp(token, allowed_operators[j].symbol)) {
				continue;
			}

			for (k = AST_VECTOR_SIZE(&operators) - 1; k >= 0; k--) {
				struct operator *top = AST_VECTOR_GET(&operators, k);

				/* Remove and push queued up operators, if they are of
				 * less precedence than this operator
				 */
				if ((allowed_operators[j].right_to_left && allowed_operators[j].precedence >= top->precedence)
					|| (!allowed_operators[j].right_to_left && allowed_operators[j].precedence > top->precedence)) {

					if (!(out_token = expression_token_alloc(TOKEN_TYPE_OPERATOR, top))) {
						goto error;
					}
					APPEND_TO_OUTPUT(output, out_token);
					AST_VECTOR_REMOVE(&operators, k, 1);
				}
			}

			AST_VECTOR_APPEND(&operators, &allowed_operators[j]);
			token = NULL;
			break;
		}

		/* Token was an operator; continue to next token */
		if (!token) {
			continue;
		}

		/* Handle a right parentheses either by itself or as part of the token.
		 * If part of the token, push the token onto the output queue first
		 */
		if (token[0] == ')' || token[strlen(token) - 1] == ')') {

			if (token[strlen(token) - 1] == ')') {
				token[strlen(token) - 1] = '\0';

				if (!(out_token = expression_token_alloc(TOKEN_TYPE_FIELD, token))) {
					goto error;
				}
				APPEND_TO_OUTPUT(output, out_token);
				token = NULL;
			}

			for (j = AST_VECTOR_SIZE(&operators) - 1; j >= 0; j--) {
				struct operator *top = AST_VECTOR_GET(&operators, j);

				AST_VECTOR_REMOVE(&operators, j, 1);
				if (top == &left_paren) {
					break;
				}

				if (!(out_token = expression_token_alloc(TOKEN_TYPE_OPERATOR, top))) {
					goto error;
				}
				APPEND_TO_OUTPUT(output, out_token);
			}
		}

		/* Just a plain token, push to the output queue */
		if (token) {
			if (!(out_token = expression_token_alloc(TOKEN_TYPE_FIELD, token))) {
				goto error;
			}
			APPEND_TO_OUTPUT(output, out_token);
		}
	}

	/* Remove any non-applied operators that remain, applying them
	 * to the output queue
	 */
	for (i = AST_VECTOR_SIZE(&operators) - 1; i >= 0; i--) {
		struct operator *top = AST_VECTOR_GET(&operators, i);
		struct expression_token *out_token;

		AST_VECTOR_REMOVE(&operators, i, 1);
		if (top == &left_paren) {
			ast_log(LOG_WARNING, "Unbalanced '(' parentheses in expression!\n");
			continue;
		}

		if (!(out_token = expression_token_alloc(TOKEN_TYPE_OPERATOR, top))) {
			goto error;
		}
		APPEND_TO_OUTPUT(output, out_token);
	}

	AST_VECTOR_FREE(&operators);
	return head;

error:
	AST_VECTOR_FREE(&operators);
	expression_token_free(output);
	return NULL;
}

/*!
 * \brief Evaluate a single entry in this history using a RPN expression
 *
 * \param entry The entry in the history to evaluate
 * \param queue The RPN expression
 *
 * \retval 0 The expression evaluated FALSE on \c entry
 * \retval 1 The expression evaluated TRUE on \c entry
 * \retval -1 The expression errored
 */
static int evaluate_history_entry(struct pjsip_history_entry *entry, struct expression_token *queue)
{
	AST_VECTOR(, struct expression_token *) stack; /* Our stack of results and operands */
	struct expression_token *it_queue;
	struct expression_token *final;
	int result;
	int i;

	if (AST_VECTOR_INIT(&stack, 16)) {
		return -1;
	}

	for (it_queue = queue; it_queue; it_queue = it_queue->next) {
		struct expression_token *op_one;
		struct expression_token *op_two = NULL;
		struct expression_token *result;
		int res = 0;

		/* If this is not an operator, push it to the stack */
		if (!it_queue->op) {
			if (AST_VECTOR_APPEND(&stack, it_queue)) {
				goto error;
			}
			continue;
		}

		if (AST_VECTOR_SIZE(&stack) < it_queue->op->operands) {
			ast_log(LOG_WARNING, "Unable to evaluate expression operator '%s': not enough operands\n",
				it_queue->op->symbol);
			goto error;
		}

		if (it_queue->op->operands == 1) {
			/* Unary operators currently consist only of 'not', which can only act
			 * upon an evaluated condition result.
			 */
			ast_assert(it_queue->op->evaluate_unary != NULL);

			op_one = AST_VECTOR_REMOVE(&stack, AST_VECTOR_SIZE(&stack) - 1, 1);
			if (op_one->token_type != TOKEN_TYPE_RESULT) {
				ast_log(LOG_WARNING, "Unable to evaluate '%s': operand is not the result of an operation\n",
					it_queue->op->symbol);
				goto error;
			}

			res = it_queue->op->evaluate_unary(it_queue->op, OPT_INT_T, &op_one->result) == 0 ? 0 : 1;
		} else if (it_queue->op->operands == 2) {
			struct allowed_field *field;
			enum aco_option_type type;
			void *value;

			ast_assert(it_queue->op->evaluate != NULL);

			op_one = AST_VECTOR_REMOVE(&stack, AST_VECTOR_SIZE(&stack) - 1, 1);
			op_two = AST_VECTOR_REMOVE(&stack, AST_VECTOR_SIZE(&stack) - 1, 1);

			/* If operand two is a field, then it must be a field we recognize. */
			if (op_two->token_type == TOKEN_TYPE_FIELD) {
				field = get_allowed_field(op_two);
				if (!field) {
					ast_log(LOG_WARNING, "Unknown or unrecognized field: %s\n", op_two->field);
					goto error;
				}

				type = field->return_type;
				value = field->get_field(entry);
			} else if (op_two->token_type == TOKEN_TYPE_RESULT) {
				type = OPT_INT_T;
				value = &op_two->result;
			} else {
				ast_log(LOG_WARNING, "Attempting to evaluate an operator: %s\n", op_two->op->symbol);
				goto error;
			}

			if (value) {
				res = it_queue->op->evaluate(it_queue->op, type, value, op_one) == 0 ? 0 : 1;
			} else {
				res = 0;
			}
		} else {
			ast_log(LOG_WARNING, "Operator '%s' has an invalid number of operands\n", it_queue->op->symbol);
			ast_assert(0);
			goto error;
		}

		/* Results are temporary; clean used ones up */
		if (op_one && op_one->token_type == TOKEN_TYPE_RESULT) {
			ast_free(op_one);
		}
		if (op_two && op_two->token_type == TOKEN_TYPE_RESULT) {
			ast_free(op_two);
		}

		/* Push the result onto the stack */
		result = expression_token_alloc(TOKEN_TYPE_RESULT, &res);
		if (!result) {
			goto error;
		}
		if (AST_VECTOR_APPEND(&stack, result)) {
			expression_token_free(result);

			goto error;
		}
	}

	/*
	 * When the evaluation is complete, we must have:
	 *  - A single result remaining on the stack
	 *  - An actual result
	 */
	if (AST_VECTOR_SIZE(&stack) != 1) {
		ast_log(LOG_WARNING, "Expression was unbalanced: %zu results remained after evaluation\n",
			AST_VECTOR_SIZE(&stack));
		goto error;
	}

	final = AST_VECTOR_GET(&stack, 0);
	if (final->token_type != TOKEN_TYPE_RESULT) {
		ast_log(LOG_WARNING, "Expression did not create a usable result\n");
		goto error;
	}
	result = final->result;
	ast_free(final);
	AST_VECTOR_FREE(&stack);

	return result;

error:
	/* Clean out any remaining result expression tokens */
	for (i = 0; i < AST_VECTOR_SIZE(&stack); i++) {
		struct expression_token *failed_token = AST_VECTOR_GET(&stack, i);

		if (failed_token->token_type == TOKEN_TYPE_RESULT) {
			ast_free(failed_token);
		}
	}
	AST_VECTOR_FREE(&stack);
	return -1;
}

/*!
 * \brief Create a filtered history based on a user provided expression
 *
 * \param a The CLI arguments containing the expression
 *
 * \retval NULL on error
 * \retval A vector containing the filtered history on success
 */
static struct vector_history_t *filter_history(struct ast_cli_args *a)
{
	struct vector_history_t *output;
	struct expression_token *queue;
	int i;

	output = ast_malloc(sizeof(*output));
	if (!output) {
		return NULL;
	}

	if (AST_VECTOR_INIT(output, HISTORY_INITIAL_SIZE / 2)) {
		ast_free(output);
		return NULL;
	}

	queue = build_expression_queue(a);
	if (!queue) {
		AST_VECTOR_PTR_FREE(output);
		return NULL;
	}

	ast_mutex_lock(&history_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&vector_history); i++) {
		struct pjsip_history_entry *entry = AST_VECTOR_GET(&vector_history, i);
		int res;

		res = evaluate_history_entry(entry, queue);
		if (res == -1) {
			/* Error in expression evaluation; bail */
			ast_mutex_unlock(&history_lock);
			AST_VECTOR_RESET(output, clear_history_entry_cb);
			AST_VECTOR_FREE(output);
			ast_free(output);
			expression_token_free(queue);
			return NULL;
		} else if (!res) {
			continue;
		} else {
			ao2_bump(entry);
			if (AST_VECTOR_APPEND(output, entry)) {
				ao2_cleanup(entry);
			}
		}
	}
	ast_mutex_unlock(&history_lock);

	expression_token_free(queue);

	return output;
}

/*! \brief Print a detailed view of a single entry in the history to the CLI */
static void display_single_entry(struct ast_cli_args *a, struct pjsip_history_entry *entry)
{
	char addr[64];
	char *buf;

	buf = ast_calloc(1, PJSIP_MAX_PKT_LEN * sizeof(char));
	if (!buf) {
		return;
	}

	if (pjsip_msg_print(entry->msg, buf, PJSIP_MAX_PKT_LEN) == -1) {
		ast_log(LOG_WARNING, "Unable to print SIP message %d: packet too large!\n", entry->number);
		ast_free(buf);
		return;
	}

	if (entry->transmitted) {
		pj_sockaddr_print(&entry->dst, addr, sizeof(addr), 3);
	} else {
		pj_sockaddr_print(&entry->src, addr, sizeof(addr), 3);
	}

	ast_cli(a->fd, "<--- History Entry %d %s %s at %-10.10ld --->\n",
		entry->number,
		entry->transmitted ? "Sent to" : "Received from",
		addr,
		entry->timestamp.tv_sec);
	ast_cli(a->fd, "%s\n", buf);

	ast_free(buf);
}

/*! \brief Print a list of the entries to the CLI */
static void display_entry_list(struct ast_cli_args *a, struct vector_history_t *vec)
{
	int i;

	ast_cli(a->fd, "%-5.5s %-10.10s %-30.30s %-35.35s\n",
		"No.",
		"Timestamp",
		"(Dir) Address",
		"SIP Message");
	ast_cli(a->fd, "===== ========== ============================== ===================================\n");

	for (i = 0; i < AST_VECTOR_SIZE(vec); i++) {
		struct pjsip_history_entry *entry;
		char line[256];

		entry = AST_VECTOR_GET(vec, i);
		sprint_list_entry(entry, line, sizeof(line));

		ast_cli(a->fd, "%s\n", line);
	}
}

/*! \brief Cleanup routine for a history vector, serviced on a registered PJSIP thread */
static int safe_vector_cleanup(void *obj)
{
	struct vector_history_t *vec = obj;

	AST_VECTOR_RESET(vec, clear_history_entry_cb);
	AST_VECTOR_FREE(vec);
	ast_free(vec);

	return 0;
}

static char *pjsip_show_history(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct vector_history_t *vec = &vector_history;
	struct pjsip_history_entry *entry = NULL;

	if (cmd == CLI_INIT) {
		e->command = "pjsip show history";
		e->usage =
			"Usage: pjsip show history [entry <num>|where [...]]\n"
			"       Displays the currently collected history or an\n"
			"       entry within the history.\n\n"
			"       * Running the command with no options will display\n"
			"         the entire history.\n"
			"       * Providing 'entry <num>' will display the full\n"
			"         detail of a particular entry in this history.\n"
			"       * Providing 'where ...' will allow for filtering\n"
			"         the history. The history can be filtered using\n"
			"         any of the following fields:\n"
			"         - number: The history entry number\n"
			"         - timestamp: The time associated with the history entry\n"
			"         - addr: The source/destination address of the SIP message\n"
			"         - sip.msg.request.method: The request method type\n"
			"         - sip.msg.call-id: The Call-ID header of the SIP message\n"
			"\n"
			"         When filtering, standard Boolean operators can be used,\n"
			"         as well as 'like' for regexs.\n"
			"\n"
			"         Example:\n"
			"         'pjsip show history where number > 5 and (addr = \"192.168.0.3:5060\" or addr = \"192.168.0.5:5060\")'\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	if (a->argc > 3) {
		if (!strcasecmp(a->argv[3], "entry") && a->argc == 5) {
			int num;

			if (sscanf(a->argv[4], "%30d", &num) != 1) {
				ast_cli(a->fd, "'%s' is not a valid entry number\n", a->argv[4]);
				return CLI_FAILURE;
			}

			/* Get the entry at the provided position */
			ast_mutex_lock(&history_lock);
			if (num >= AST_VECTOR_SIZE(&vector_history) || num < 0) {
				ast_cli(a->fd, "Entry '%d' does not exist\n", num);
				ast_mutex_unlock(&history_lock);
				return CLI_FAILURE;
			}
			entry = ao2_bump(AST_VECTOR_GET(&vector_history, num));
			ast_mutex_unlock(&history_lock);
		} else if (!strcasecmp(a->argv[3], "where")) {
			vec = filter_history(a);
			if (!vec) {
				return CLI_FAILURE;
			}
		} else {
			return CLI_SHOWUSAGE;
		}
	}

	if (AST_VECTOR_SIZE(vec) == 1) {
		if (vec == &vector_history) {
			ast_mutex_lock(&history_lock);
		}
		entry = ao2_bump(AST_VECTOR_GET(vec, 0));
		if (vec == &vector_history) {
			ast_mutex_unlock(&history_lock);
		}
	}

	if (entry) {
		display_single_entry(a, entry);
	} else {
		if (vec == &vector_history) {
			ast_mutex_lock(&history_lock);
		}

		display_entry_list(a, vec);

		if (vec == &vector_history) {
			ast_mutex_unlock(&history_lock);
		}
	}

	if (vec != &vector_history) {
		ast_sip_push_task(NULL, safe_vector_cleanup, vec);
	}
	ao2_cleanup(entry);

	return CLI_SUCCESS;
}

static char *pjsip_set_history(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *what;

	if (cmd == CLI_INIT) {
		e->command = "pjsip set history {on|off|clear}";
		e->usage =
			"Usage: pjsip set history {on|off|clear}\n"
			"       Enables/disables/clears the PJSIP history.\n\n"
			"       Enabling the history will start recording transmitted/received\n"
			"       packets. Disabling the history will stop recording, but keep\n"
			"       the already received packets. Clearing the history will wipe\n"
			"       the received packets from memory.\n\n"
			"       As the PJSIP history is maintained in memory, and includes\n"
			"       all received/transmitted requests and responses, it should\n"
			"       only be enabled for debugging purposes, and cleared when done.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	what = a->argv[e->args - 1];	/* Guaranteed to exist */

	if (a->argc == e->args) {
		if (!strcasecmp(what, "on")) {
			enabled = 1;
			ast_cli(a->fd, "PJSIP History enabled\n");
			return CLI_SUCCESS;
		} else if (!strcasecmp(what, "off")) {
			enabled = 0;
			ast_cli(a->fd, "PJSIP History disabled\n");
			return CLI_SUCCESS;
		} else if (!strcasecmp(what, "clear")) {
			ast_sip_push_task(NULL, clear_history_entries, NULL);
			ast_cli(a->fd, "PJSIP History cleared\n");
			return CLI_SUCCESS;
		}
	}

	return CLI_SHOWUSAGE;
}

static pjsip_module logging_module = {
	.name = { "History Module", 14 },
	.priority = 0,
	.on_rx_request = history_on_rx_msg,
	.on_rx_response = history_on_rx_msg,
	.on_tx_request = history_on_tx_msg,
	.on_tx_response = history_on_tx_msg,
};

static struct ast_cli_entry cli_pjsip[] = {
	AST_CLI_DEFINE(pjsip_set_history, "Enable/Disable PJSIP History"),
	AST_CLI_DEFINE(pjsip_show_history, "Display PJSIP History"),
};

static int load_module(void)
{
	log_level = ast_logger_register_level("PJSIP_HISTORY");
	if (log_level < 0) {
		ast_log(LOG_WARNING, "Unable to register history log level\n");
	}

	ast_pjproject_caching_pool_init(&cachingpool, &pj_pool_factory_default_policy, 0);

	AST_VECTOR_INIT(&vector_history, HISTORY_INITIAL_SIZE);

	ast_sip_register_service(&logging_module);
	ast_cli_register_multiple(cli_pjsip, ARRAY_LEN(cli_pjsip));

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_pjsip, ARRAY_LEN(cli_pjsip));
	ast_sip_unregister_service(&logging_module);

	ast_sip_push_task_wait_servant(NULL, clear_history_entries, NULL);
	AST_VECTOR_FREE(&vector_history);

	ast_pjproject_caching_pool_destroy(&cachingpool);

	if (log_level != -1) {
		ast_logger_unregister_level("PJSIP_HISTORY");
	}

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP History",
		.support_level = AST_MODULE_SUPPORT_EXTENDED,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
		.requires = "res_pjsip",
	);
