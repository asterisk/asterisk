/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2008, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \brief Internal generic event system
 *
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

#include "asterisk/event.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dlinkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/unaligned.h"
#include "asterisk/utils.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"

static struct ast_taskprocessor *event_dispatcher;
static int event_append_ie_raw(struct ast_event **event, enum ast_event_ie_type ie_type,
	const void *data, size_t data_len);
static const void *event_get_ie_raw(const struct ast_event *event, enum ast_event_ie_type ie_type);
static uint16_t event_get_ie_raw_payload_len(const struct ast_event *event, enum ast_event_ie_type ie_type);
static uint32_t event_get_ie_str_hash(const struct ast_event *event, enum ast_event_ie_type ie_type);

/*!
 * \brief An event information element
 *
 * \note The format of this structure is important.  Since these events may
 *       be sent directly over a network, changing this structure will break
 *       compatibility with older versions.  However, at this point, this code
 *       has not made it into a release, so it is still fair game for change.
 */
struct ast_event_ie {
	enum ast_event_ie_type ie_type:16;
	/*! Total length of the IE payload */
	uint16_t ie_payload_len;
	unsigned char ie_payload[0];
} __attribute__((packed));

/*!
 * \brief The payload for a string information element
 */
struct ast_event_ie_str_payload {
	/*! \brief A hash calculated with ast_str_hash(), to speed up comparisons */
	uint32_t hash;
	/*! \brief The actual string, null terminated */
	char str[1];
} __attribute__((packed));

/*!
 * \brief An event
 *
 * An ast_event consists of an event header (this structure), and zero or
 * more information elements defined by ast_event_ie.
 *
 * \note The format of this structure is important.  Since these events may
 *       be sent directly over a network, changing this structure will break
 *       compatibility with older versions.  However, at this point, this code
 *       has not made it into a release, so it is still fair game for change.
 */
struct ast_event {
	/*! Event type */
	enum ast_event_type type:16;
	/*! Total length of the event */
	uint16_t event_len:16;
	/*! The data payload of the event, made up of information elements */
	unsigned char payload[0];
} __attribute__((packed));


struct ast_event_ie_val {
	AST_LIST_ENTRY(ast_event_ie_val) entry;
	enum ast_event_ie_type ie_type;
	enum ast_event_ie_pltype ie_pltype;
	union {
		uint32_t uint;
		struct {
			uint32_t hash;
			const char *str;
		};
		void *raw;
	} payload;
	size_t raw_datalen;
};

/*! \brief Event subscription */
struct ast_event_sub {
	enum ast_event_type type;
	ast_event_cb_t cb;
	char description[64];
	void *userdata;
	uint32_t uniqueid;
	AST_LIST_HEAD_NOLOCK(, ast_event_ie_val) ie_vals;
	AST_RWDLLIST_ENTRY(ast_event_sub) entry;
};

static uint32_t sub_uniqueid;

/*! \brief Event subscriptions
 * The event subscribers are indexed by which event they are subscribed to */
static AST_RWDLLIST_HEAD(ast_event_sub_list, ast_event_sub) ast_event_subs[AST_EVENT_TOTAL];

struct ie_map {
	enum ast_event_ie_pltype ie_pltype;
	const char *name;
};

/*!
 * \brief IE payload types and names
 */
static const struct ie_map ie_maps[AST_EVENT_IE_TOTAL] = {
	[AST_EVENT_IE_UNIQUEID]            = { AST_EVENT_IE_PLTYPE_UINT, "UniqueID" },
	[AST_EVENT_IE_EVENTTYPE]           = { AST_EVENT_IE_PLTYPE_UINT, "EventType" },
	[AST_EVENT_IE_EXISTS]              = { AST_EVENT_IE_PLTYPE_UINT, "Exists" },
	[AST_EVENT_IE_CONTEXT]             = { AST_EVENT_IE_PLTYPE_STR,  "Context" },
	[AST_EVENT_IE_CEL_EVENT_TYPE]      = { AST_EVENT_IE_PLTYPE_UINT, "CELEventType" },
	[AST_EVENT_IE_CEL_EVENT_TIME]      = { AST_EVENT_IE_PLTYPE_UINT, "CELEventTime" },
	[AST_EVENT_IE_CEL_EVENT_TIME_USEC] = { AST_EVENT_IE_PLTYPE_UINT, "CELEventTimeUSec" },
	[AST_EVENT_IE_CEL_USEREVENT_NAME]  = { AST_EVENT_IE_PLTYPE_UINT, "CELUserEventName" },
	[AST_EVENT_IE_CEL_CIDNAME]         = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDName" },
	[AST_EVENT_IE_CEL_CIDNUM]          = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDNum" },
	[AST_EVENT_IE_CEL_EXTEN]           = { AST_EVENT_IE_PLTYPE_STR,  "CELExten" },
	[AST_EVENT_IE_CEL_CONTEXT]         = { AST_EVENT_IE_PLTYPE_STR,  "CELContext" },
	[AST_EVENT_IE_CEL_CHANNAME]        = { AST_EVENT_IE_PLTYPE_STR,  "CELChanName" },
	[AST_EVENT_IE_CEL_APPNAME]         = { AST_EVENT_IE_PLTYPE_STR,  "CELAppName" },
	[AST_EVENT_IE_CEL_APPDATA]         = { AST_EVENT_IE_PLTYPE_STR,  "CELAppData" },
	[AST_EVENT_IE_CEL_AMAFLAGS]        = { AST_EVENT_IE_PLTYPE_STR,  "CELAMAFlags" },
	[AST_EVENT_IE_CEL_ACCTCODE]        = { AST_EVENT_IE_PLTYPE_UINT, "CELAcctCode" },
	[AST_EVENT_IE_CEL_UNIQUEID]        = { AST_EVENT_IE_PLTYPE_STR,  "CELUniqueID" },
	[AST_EVENT_IE_CEL_USERFIELD]       = { AST_EVENT_IE_PLTYPE_STR,  "CELUserField" },
	[AST_EVENT_IE_CEL_CIDANI]          = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDani" },
	[AST_EVENT_IE_CEL_CIDRDNIS]        = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDrdnis" },
	[AST_EVENT_IE_CEL_CIDDNID]         = { AST_EVENT_IE_PLTYPE_STR,  "CELCIDdnid" },
	[AST_EVENT_IE_CEL_PEER]            = { AST_EVENT_IE_PLTYPE_STR,  "CELPeer" },
	[AST_EVENT_IE_CEL_LINKEDID]        = { AST_EVENT_IE_PLTYPE_STR,  "CELLinkedID" },
	[AST_EVENT_IE_CEL_PEERACCT]        = { AST_EVENT_IE_PLTYPE_STR,  "CELPeerAcct" },
	[AST_EVENT_IE_CEL_EXTRA]           = { AST_EVENT_IE_PLTYPE_STR,  "CELExtra" },
	[AST_EVENT_IE_EVENT_VERSION]       = { AST_EVENT_IE_PLTYPE_UINT, "EventVersion" },
	[AST_EVENT_IE_SERVICE]             = { AST_EVENT_IE_PLTYPE_STR,  "Service" },
	[AST_EVENT_IE_MODULE]              = { AST_EVENT_IE_PLTYPE_STR,  "Module" },
	[AST_EVENT_IE_ACCOUNT_ID]          = { AST_EVENT_IE_PLTYPE_STR,  "AccountID" },
	[AST_EVENT_IE_SESSION_ID]          = { AST_EVENT_IE_PLTYPE_STR,  "SessionID" },
	[AST_EVENT_IE_SESSION_TV]          = { AST_EVENT_IE_PLTYPE_STR,  "SessionTV" },
	[AST_EVENT_IE_ACL_NAME]            = { AST_EVENT_IE_PLTYPE_STR,  "ACLName" },
	[AST_EVENT_IE_LOCAL_ADDR]          = { AST_EVENT_IE_PLTYPE_STR,  "LocalAddress" },
	[AST_EVENT_IE_REMOTE_ADDR]         = { AST_EVENT_IE_PLTYPE_STR,  "RemoteAddress" },
	[AST_EVENT_IE_EVENT_TV]            = { AST_EVENT_IE_PLTYPE_STR,  "EventTV" },
	[AST_EVENT_IE_REQUEST_TYPE]        = { AST_EVENT_IE_PLTYPE_STR,  "RequestType" },
	[AST_EVENT_IE_REQUEST_PARAMS]      = { AST_EVENT_IE_PLTYPE_STR,  "RequestParams" },
	[AST_EVENT_IE_AUTH_METHOD]         = { AST_EVENT_IE_PLTYPE_STR,  "AuthMethod" },
	[AST_EVENT_IE_SEVERITY]            = { AST_EVENT_IE_PLTYPE_STR,  "Severity" },
	[AST_EVENT_IE_EXPECTED_ADDR]       = { AST_EVENT_IE_PLTYPE_STR,  "ExpectedAddress" },
	[AST_EVENT_IE_CHALLENGE]           = { AST_EVENT_IE_PLTYPE_STR,  "Challenge" },
	[AST_EVENT_IE_RESPONSE]            = { AST_EVENT_IE_PLTYPE_STR,  "Response" },
	[AST_EVENT_IE_EXPECTED_RESPONSE]   = { AST_EVENT_IE_PLTYPE_STR,  "ExpectedResponse" },
	[AST_EVENT_IE_RECEIVED_CHALLENGE]  = { AST_EVENT_IE_PLTYPE_STR,  "ReceivedChallenge" },
	[AST_EVENT_IE_RECEIVED_HASH]       = { AST_EVENT_IE_PLTYPE_STR,  "ReceivedHash" },
	[AST_EVENT_IE_USING_PASSWORD]      = { AST_EVENT_IE_PLTYPE_UINT, "UsingPassword" },
	[AST_EVENT_IE_ATTEMPTED_TRANSPORT] = { AST_EVENT_IE_PLTYPE_STR,  "AttemptedTransport" },
};

const char *ast_event_get_ie_type_name(enum ast_event_ie_type ie_type)
{
	if (ie_type <= 0 || ie_type >= ARRAY_LEN(ie_maps)) {
		ast_log(LOG_ERROR, "Invalid IE type - '%d'\n", ie_type);
		return "";
	}

	return ie_maps[ie_type].name;
}

enum ast_event_ie_pltype ast_event_get_ie_pltype(enum ast_event_ie_type ie_type)
{
	if (ie_type <= 0 || ie_type >= ARRAY_LEN(ie_maps)) {
		ast_log(LOG_ERROR, "Invalid IE type - '%d'\n", ie_type);
		return AST_EVENT_IE_PLTYPE_UNKNOWN;
	}

	return ie_maps[ie_type].ie_pltype;
}

size_t ast_event_get_size(const struct ast_event *event)
{
	size_t res;

	res = ntohs(event->event_len);

	return res;
}

static void ast_event_ie_val_destroy(struct ast_event_ie_val *ie_val)
{
	switch (ie_val->ie_pltype) {
	case AST_EVENT_IE_PLTYPE_STR:
		ast_free((char *) ie_val->payload.str);
		break;
	case AST_EVENT_IE_PLTYPE_RAW:
		ast_free(ie_val->payload.raw);
		break;
	case AST_EVENT_IE_PLTYPE_UINT:
	case AST_EVENT_IE_PLTYPE_UNKNOWN:
		break;
	}

	ast_free(ie_val);
}

/*! \brief Subscription event check list. */
struct ast_ev_check_list {
	AST_LIST_HEAD_NOLOCK(, ast_event_ie_val) ie_vals;
};

/*!
 * \internal
 * \brief Check if a subscription ie_val matches an event.
 *
 * \param sub_ie_val Subscripton IE value to check
 * \param check_ie_vals event list to check against
 *
 * \retval 0 not matched
 * \retval non-zero matched
 */
static int match_sub_ie_val_to_event(const struct ast_event_ie_val *sub_ie_val, const struct ast_ev_check_list *check_ie_vals)
{
	const struct ast_event_ie_val *event_ie_val;
	int res = 0;

	AST_LIST_TRAVERSE(&check_ie_vals->ie_vals, event_ie_val, entry) {
		if (sub_ie_val->ie_type == event_ie_val->ie_type) {
			break;
		}
	}
	if (!event_ie_val) {
		/* We did not find the event ie the subscriber cares about. */
		return 0;
	}

	if (sub_ie_val->ie_pltype != event_ie_val->ie_pltype) {
		/* Payload types do not match. */
		return 0;
	}

	switch (sub_ie_val->ie_pltype) {
	case AST_EVENT_IE_PLTYPE_UINT:
		res = (sub_ie_val->payload.uint == event_ie_val->payload.uint);
		break;
	case AST_EVENT_IE_PLTYPE_STR:
	{
		const char *substr = sub_ie_val->payload.str;
		const char *estr = event_ie_val->payload.str;
		res = !strcmp(substr, estr);
		break;
	}
	case AST_EVENT_IE_PLTYPE_RAW:
		res = (sub_ie_val->raw_datalen == event_ie_val->raw_datalen
			&& !memcmp(sub_ie_val->payload.raw, event_ie_val->payload.raw,
				sub_ie_val->raw_datalen));
		break;
	case AST_EVENT_IE_PLTYPE_UNKNOWN:
		/*
		 * Should never be in a subscription event ie val list and
		 * check_ie_vals cannot have this type either.
		 */
		break;
	}

	return res;
}

enum ast_event_subscriber_res ast_event_check_subscriber(enum ast_event_type type, ...)
{
	va_list ap;
	enum ast_event_ie_type ie_type;
	enum ast_event_subscriber_res res = AST_EVENT_SUB_NONE;
	struct ast_event_ie_val *ie_val;
	struct ast_event_sub *sub;
	struct ast_ev_check_list check_ie_vals = {
		.ie_vals = AST_LIST_HEAD_NOLOCK_INIT_VALUE
	};
	const enum ast_event_type event_types[] = { type, AST_EVENT_ALL };
	int i;
	int want_specific_event;/* TRUE if looking for subscribers wanting specific parameters. */

	if (type >= AST_EVENT_TOTAL) {
		ast_log(LOG_ERROR, "%u is an invalid type!\n", type);
		return res;
	}

	want_specific_event = 0;
	va_start(ap, type);
	for (ie_type = va_arg(ap, enum ast_event_ie_type);
		ie_type != AST_EVENT_IE_END;
		ie_type = va_arg(ap, enum ast_event_ie_type))
	{
		struct ast_event_ie_val *ie_value = ast_alloca(sizeof(*ie_value));
		int insert = 0;

		memset(ie_value, 0, sizeof(*ie_value));
		ie_value->ie_type = ie_type;
		ie_value->ie_pltype = va_arg(ap, enum ast_event_ie_pltype);
		switch (ie_value->ie_pltype) {
		case AST_EVENT_IE_PLTYPE_UINT:
			ie_value->payload.uint = va_arg(ap, uint32_t);
			insert = 1;
			break;
		case AST_EVENT_IE_PLTYPE_STR:
			ie_value->payload.str = va_arg(ap, const char *);
			insert = 1;
			break;
		case AST_EVENT_IE_PLTYPE_RAW:
		{
			void *data = va_arg(ap, void *);
			size_t datalen = va_arg(ap, size_t);

			ie_value->payload.raw = ast_alloca(datalen);
			memcpy(ie_value->payload.raw, data, datalen);
			ie_value->raw_datalen = datalen;
			insert = 1;
			break;
		}
		case AST_EVENT_IE_PLTYPE_UNKNOWN:
			/* Unsupported payload type. */
			break;
		}

		if (insert) {
			want_specific_event = 1;
			AST_LIST_INSERT_TAIL(&check_ie_vals.ie_vals, ie_value, entry);
		} else {
			ast_log(LOG_WARNING, "Unsupported PLTYPE(%d)\n", ie_value->ie_pltype);
		}
	}
	va_end(ap);

	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		AST_RWDLLIST_RDLOCK(&ast_event_subs[event_types[i]]);
		if (want_specific_event) {
			AST_RWDLLIST_TRAVERSE(&ast_event_subs[event_types[i]], sub, entry) {
				AST_LIST_TRAVERSE(&sub->ie_vals, ie_val, entry) {
					if (!match_sub_ie_val_to_event(ie_val, &check_ie_vals)) {
						/* The current subscription ie did not match an event ie. */
						break;
					}
				}
				if (!ie_val) {
					/* Everything matched.  A subscriber is looking for this event. */
					break;
				}
			}
		} else {
			/* Just looking to see if there are ANY subscribers to the event type. */
			sub = AST_RWLIST_FIRST(&ast_event_subs[event_types[i]]);
		}
		AST_RWDLLIST_UNLOCK(&ast_event_subs[event_types[i]]);
		if (sub) {
			break;
		}
	}

	return sub ? AST_EVENT_SUB_EXISTS : AST_EVENT_SUB_NONE;
}

/*!
 * \internal
 * \brief Check if an ie_val matches an event
 *
 * \param event event to check against
 * \param ie_val IE value to check
 * \param event2 optional event, if specified, the value to compare against will be pulled
 *        from this event instead of from the ie_val structure.  In this case, only the IE
 *        type and payload type will be pulled from ie_val.
 *
 * \retval 0 not matched
 * \retval non-zero matched
 */
static int match_ie_val(const struct ast_event *event,
		const struct ast_event_ie_val *ie_val, const struct ast_event *event2)
{
	switch (ie_val->ie_pltype) {
	case AST_EVENT_IE_PLTYPE_UINT:
	{
		uint32_t val = event2 ? ast_event_get_ie_uint(event2, ie_val->ie_type) : ie_val->payload.uint;

		return (val == ast_event_get_ie_uint(event, ie_val->ie_type)) ? 1 : 0;
	}

	case AST_EVENT_IE_PLTYPE_STR:
	{
		const char *str;
		uint32_t hash;

		hash = event2 ? event_get_ie_str_hash(event2, ie_val->ie_type) : ie_val->payload.hash;
		if (hash != event_get_ie_str_hash(event, ie_val->ie_type)) {
			return 0;
		}

		str = event2 ? ast_event_get_ie_str(event2, ie_val->ie_type) : ie_val->payload.str;
		if (str) {
			const char *e1str, *e2str;
			e1str = ast_event_get_ie_str(event, ie_val->ie_type);
			e2str = str;

			if (!strcmp(e1str, e2str)) {
				return 1;
			}
		}

		return 0;
	}

	case AST_EVENT_IE_PLTYPE_RAW:
	{
		const void *buf = event2 ? event_get_ie_raw(event2, ie_val->ie_type) : ie_val->payload.raw;
		uint16_t ie_payload_len = event2 ? event_get_ie_raw_payload_len(event2, ie_val->ie_type) : ie_val->raw_datalen;

		return (buf
			&& ie_payload_len == event_get_ie_raw_payload_len(event, ie_val->ie_type)
			&& !memcmp(buf, event_get_ie_raw(event, ie_val->ie_type), ie_payload_len)) ? 1 : 0;
	}

	case AST_EVENT_IE_PLTYPE_UNKNOWN:
		return 0;
	}

	return 0;
}

static struct ast_event *gen_sub_event(struct ast_event_sub *sub)
{
	struct ast_event_ie_val *ie_val;
	struct ast_event *event;

	event = ast_event_new(AST_EVENT_SUB,
		AST_EVENT_IE_UNIQUEID,    AST_EVENT_IE_PLTYPE_UINT, sub->uniqueid,
		AST_EVENT_IE_EVENTTYPE,   AST_EVENT_IE_PLTYPE_UINT, sub->type,
		AST_EVENT_IE_DESCRIPTION, AST_EVENT_IE_PLTYPE_STR, sub->description,
		AST_EVENT_IE_END);
	if (!event)
		return NULL;

	AST_LIST_TRAVERSE(&sub->ie_vals, ie_val, entry) {
		switch (ie_val->ie_pltype) {
		case AST_EVENT_IE_PLTYPE_UNKNOWN:
			break;
		case AST_EVENT_IE_PLTYPE_UINT:
			ast_event_append_ie_uint(&event, ie_val->ie_type, ie_val->payload.uint);
			break;
		case AST_EVENT_IE_PLTYPE_STR:
			ast_event_append_ie_str(&event, ie_val->ie_type, ie_val->payload.str);
			break;
		case AST_EVENT_IE_PLTYPE_RAW:
			event_append_ie_raw(&event, ie_val->ie_type, ie_val->payload.raw, ie_val->raw_datalen);
			break;
		}
		if (!event)
			break;
	}

	return event;
}

/*! \brief Send AST_EVENT_SUB events to this subscriber of ... subscriber events */
void ast_event_report_subs(const struct ast_event_sub *event_sub)
{
	struct ast_event *event;
	struct ast_event_sub *sub;
	enum ast_event_type event_type = -1;
	struct ast_event_ie_val *ie_val;

	if (event_sub->type != AST_EVENT_SUB)
		return;

	AST_LIST_TRAVERSE(&event_sub->ie_vals, ie_val, entry) {
		if (ie_val->ie_type == AST_EVENT_IE_EVENTTYPE) {
			event_type = ie_val->payload.uint;
			break;
		}
	}

	if (event_type == -1)
		return;

	AST_RWDLLIST_RDLOCK(&ast_event_subs[event_type]);
	AST_RWDLLIST_TRAVERSE(&ast_event_subs[event_type], sub, entry) {
		if (event_sub == sub) {
			continue;
		}

		event = gen_sub_event(sub);
		if (!event) {
			continue;
		}

		event_sub->cb(event, event_sub->userdata);

		ast_event_destroy(event);
	}
	AST_RWDLLIST_UNLOCK(&ast_event_subs[event_type]);
}

struct ast_event_sub *ast_event_subscribe_new(enum ast_event_type type,
	ast_event_cb_t cb, void *userdata)
{
	struct ast_event_sub *sub;

	if (type < 0 || type >= AST_EVENT_TOTAL) {
		ast_log(LOG_ERROR, "%u is an invalid type!\n", type);
		return NULL;
	}

	if (!(sub = ast_calloc(1, sizeof(*sub)))) {
		return NULL;
	}

	sub->type = type;
	sub->cb = cb;
	sub->userdata = userdata;
	sub->uniqueid = ast_atomic_fetchadd_int((int *) &sub_uniqueid, 1);

	return sub;
}

int ast_event_sub_append_ie_uint(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type, uint32_t unsigned_int)
{
	struct ast_event_ie_val *ie_val;

	if (ie_type <= 0 || ie_type >= AST_EVENT_IE_TOTAL) {
		return -1;
	}

	if (!(ie_val = ast_calloc(1, sizeof(*ie_val)))) {
		return -1;
	}

	ie_val->ie_type = ie_type;
	ie_val->payload.uint = unsigned_int;
	ie_val->ie_pltype = AST_EVENT_IE_PLTYPE_UINT;

	AST_LIST_INSERT_TAIL(&sub->ie_vals, ie_val, entry);

	return 0;
}

int ast_event_sub_append_ie_str(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type, const char *str)
{
	struct ast_event_ie_val *ie_val;

	if (ie_type <= 0 || ie_type >= AST_EVENT_IE_TOTAL) {
		return -1;
	}

	if (!(ie_val = ast_calloc(1, sizeof(*ie_val)))) {
		return -1;
	}

	ie_val->ie_type = ie_type;
	ie_val->ie_pltype = AST_EVENT_IE_PLTYPE_STR;

	if (!(ie_val->payload.str = ast_strdup(str))) {
		ast_free(ie_val);
		return -1;
	}

	ie_val->payload.hash = ast_str_hash(str);

	AST_LIST_INSERT_TAIL(&sub->ie_vals, ie_val, entry);

	return 0;
}

int ast_event_sub_append_ie_raw(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type, void *data, size_t raw_datalen)
{
	struct ast_event_ie_val *ie_val;

	if (ie_type <= 0 || ie_type >= AST_EVENT_IE_TOTAL) {
		return -1;
	}

	if (!(ie_val = ast_calloc(1, sizeof(*ie_val)))) {
		return -1;
	}

	ie_val->ie_type = ie_type;
	ie_val->ie_pltype = AST_EVENT_IE_PLTYPE_RAW;
	ie_val->raw_datalen = raw_datalen;

	if (!(ie_val->payload.raw = ast_malloc(raw_datalen))) {
		ast_free(ie_val);
		return -1;
	}

	memcpy(ie_val->payload.raw, data, raw_datalen);

	AST_LIST_INSERT_TAIL(&sub->ie_vals, ie_val, entry);

	return 0;
}

int ast_event_sub_activate(struct ast_event_sub *sub)
{
	if (ast_event_check_subscriber(AST_EVENT_SUB,
		AST_EVENT_IE_EVENTTYPE, AST_EVENT_IE_PLTYPE_UINT, sub->type,
		AST_EVENT_IE_END) != AST_EVENT_SUB_NONE) {
		struct ast_event *event;

		event = gen_sub_event(sub);
		if (event && ast_event_queue(event)) {
			ast_event_destroy(event);
		}
	}

	AST_RWDLLIST_WRLOCK(&ast_event_subs[sub->type]);
	AST_RWDLLIST_INSERT_TAIL(&ast_event_subs[sub->type], sub, entry);
	AST_RWDLLIST_UNLOCK(&ast_event_subs[sub->type]);

	return 0;
}

struct ast_event_sub *ast_event_subscribe(enum ast_event_type type, ast_event_cb_t cb,
	const char *description, void *userdata, ...)
{
	va_list ap;
	enum ast_event_ie_type ie_type;
	struct ast_event_sub *sub;

	if (!(sub = ast_event_subscribe_new(type, cb, userdata))) {
		return NULL;
	}

	ast_copy_string(sub->description, description, sizeof(sub->description));

	va_start(ap, userdata);
	for (ie_type = va_arg(ap, enum ast_event_ie_type);
		ie_type != AST_EVENT_IE_END;
		ie_type = va_arg(ap, enum ast_event_ie_type))
	{
		enum ast_event_ie_pltype ie_pltype;

		ie_pltype = va_arg(ap, enum ast_event_ie_pltype);

		switch (ie_pltype) {
		case AST_EVENT_IE_PLTYPE_UNKNOWN:
			break;
		case AST_EVENT_IE_PLTYPE_UINT:
		{
			uint32_t unsigned_int = va_arg(ap, uint32_t);
			ast_event_sub_append_ie_uint(sub, ie_type, unsigned_int);
			break;
		}
		case AST_EVENT_IE_PLTYPE_STR:
		{
			const char *str = va_arg(ap, const char *);
			ast_event_sub_append_ie_str(sub, ie_type, str);
			break;
		}
		case AST_EVENT_IE_PLTYPE_RAW:
		{
			void *data = va_arg(ap, void *);
			size_t data_len = va_arg(ap, size_t);
			ast_event_sub_append_ie_raw(sub, ie_type, data, data_len);
			break;
		}
		}
	}
	va_end(ap);

	ast_event_sub_activate(sub);

	return sub;
}

void ast_event_sub_destroy(struct ast_event_sub *sub)
{
	struct ast_event_ie_val *ie_val;

	while ((ie_val = AST_LIST_REMOVE_HEAD(&sub->ie_vals, entry))) {
		ast_event_ie_val_destroy(ie_val);
	}

	ast_free(sub);
}

struct ast_event_sub *ast_event_unsubscribe(struct ast_event_sub *sub)
{

	AST_RWDLLIST_WRLOCK(&ast_event_subs[sub->type]);
	AST_DLLIST_REMOVE(&ast_event_subs[sub->type], sub, entry);
	AST_RWDLLIST_UNLOCK(&ast_event_subs[sub->type]);

	ast_event_sub_destroy(sub);

	return NULL;
}

int ast_event_iterator_init(struct ast_event_iterator *iterator, const struct ast_event *event)
{
	int res = 0;

	iterator->event_len = ast_event_get_size(event);
	iterator->event = event;
	if (iterator->event_len >= sizeof(*event) + sizeof(struct ast_event_ie)) {
		iterator->ie = (struct ast_event_ie *) ( ((char *) event) + sizeof(*event) );
	} else {
		iterator->ie = NULL;
		res = -1;
	}

	return res;
}

int ast_event_iterator_next(struct ast_event_iterator *iterator)
{
	iterator->ie = (struct ast_event_ie *) ( ((char *) iterator->ie) + sizeof(*iterator->ie) + ntohs(iterator->ie->ie_payload_len));
	return ((iterator->event_len <= (((char *) iterator->ie) - ((char *) iterator->event))) ? -1 : 0);
}

enum ast_event_ie_type ast_event_iterator_get_ie_type(struct ast_event_iterator *iterator)
{
	return ntohs(iterator->ie->ie_type);
}

uint32_t ast_event_iterator_get_ie_uint(struct ast_event_iterator *iterator)
{
	return ntohl(get_unaligned_uint32(iterator->ie->ie_payload));
}

const char *ast_event_iterator_get_ie_str(struct ast_event_iterator *iterator)
{
	const struct ast_event_ie_str_payload *str_payload;

	str_payload = (struct ast_event_ie_str_payload *) iterator->ie->ie_payload;

	return str_payload ? str_payload->str : NULL;
}

static void *event_iterator_get_ie_raw(struct ast_event_iterator *iterator)
{
	return iterator->ie->ie_payload;
}

static uint16_t event_iterator_get_ie_raw_payload_len(struct ast_event_iterator *iterator)
{
	return ntohs(iterator->ie->ie_payload_len);
}

enum ast_event_type ast_event_get_type(const struct ast_event *event)
{
	return ntohs(event->type);
}

uint32_t ast_event_get_ie_uint(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	const uint32_t *ie_val;

	ie_val = event_get_ie_raw(event, ie_type);

	return ie_val ? ntohl(get_unaligned_uint32(ie_val)) : 0;
}

static uint32_t event_get_ie_str_hash(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	const struct ast_event_ie_str_payload *str_payload;

	str_payload = event_get_ie_raw(event, ie_type);

	return str_payload ? str_payload->hash : 0;
}

const char *ast_event_get_ie_str(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	const struct ast_event_ie_str_payload *str_payload;

	str_payload = event_get_ie_raw(event, ie_type);

	return str_payload ? str_payload->str : NULL;
}

static const void *event_get_ie_raw(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	struct ast_event_iterator iterator;
	int res;

	for (res = ast_event_iterator_init(&iterator, event); !res; res = ast_event_iterator_next(&iterator)) {
		if (ast_event_iterator_get_ie_type(&iterator) == ie_type) {
			return event_iterator_get_ie_raw(&iterator);
		}
	}

	return NULL;
}

static uint16_t event_get_ie_raw_payload_len(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	struct ast_event_iterator iterator;
	int res;

	for (res = ast_event_iterator_init(&iterator, event); !res; res = ast_event_iterator_next(&iterator)) {
		if (ast_event_iterator_get_ie_type(&iterator) == ie_type) {
			return event_iterator_get_ie_raw_payload_len(&iterator);
		}
	}

	return 0;
}

int ast_event_append_ie_str(struct ast_event **event, enum ast_event_ie_type ie_type,
	const char *str)
{
	struct ast_event_ie_str_payload *str_payload;
	size_t payload_len;

	payload_len = sizeof(*str_payload) + strlen(str);
	str_payload = ast_alloca(payload_len);

	strcpy(str_payload->str, str);
	str_payload->hash = ast_str_hash(str);

	return event_append_ie_raw(event, ie_type, str_payload, payload_len);
}

int ast_event_append_ie_uint(struct ast_event **event, enum ast_event_ie_type ie_type,
	uint32_t data)
{
	data = htonl(data);
	return event_append_ie_raw(event, ie_type, &data, sizeof(data));
}

static int event_append_ie_raw(struct ast_event **event, enum ast_event_ie_type ie_type,
	const void *data, size_t data_len)
{
	struct ast_event_ie *ie;
	unsigned int extra_len;
	uint16_t event_len;

	event_len = ntohs((*event)->event_len);
	extra_len = sizeof(*ie) + data_len;

	if (!(*event = ast_realloc(*event, event_len + extra_len))) {
		return -1;
	}

	ie = (struct ast_event_ie *) ( ((char *) *event) + event_len );
	ie->ie_type = htons(ie_type);
	ie->ie_payload_len = htons(data_len);
	memcpy(ie->ie_payload, data, data_len);

	(*event)->event_len = htons(event_len + extra_len);

	return 0;
}

struct ast_event *ast_event_new(enum ast_event_type type, ...)
{
	va_list ap;
	struct ast_event *event;
	enum ast_event_ie_type ie_type;
	struct ast_event_ie_val *ie_val;
	AST_LIST_HEAD_NOLOCK_STATIC(ie_vals, ast_event_ie_val);

	/* Invalid type */
	if (type >= AST_EVENT_TOTAL) {
		ast_log(LOG_WARNING, "Someone tried to create an event of invalid "
			"type '%d'!\n", type);
		return NULL;
	}

	va_start(ap, type);
	for (ie_type = va_arg(ap, enum ast_event_ie_type);
		ie_type != AST_EVENT_IE_END;
		ie_type = va_arg(ap, enum ast_event_ie_type))
	{
		struct ast_event_ie_val *ie_value = ast_alloca(sizeof(*ie_value));
		int insert = 0;

		memset(ie_value, 0, sizeof(*ie_value));
		ie_value->ie_type = ie_type;
		ie_value->ie_pltype = va_arg(ap, enum ast_event_ie_pltype);
		switch (ie_value->ie_pltype) {
		case AST_EVENT_IE_PLTYPE_UINT:
			ie_value->payload.uint = va_arg(ap, uint32_t);
			insert = 1;
			break;
		case AST_EVENT_IE_PLTYPE_STR:
			ie_value->payload.str = va_arg(ap, const char *);
			insert = 1;
			break;
		case AST_EVENT_IE_PLTYPE_RAW:
		{
			void *data = va_arg(ap, void *);
			size_t datalen = va_arg(ap, size_t);
			ie_value->payload.raw = ast_alloca(datalen);
			memcpy(ie_value->payload.raw, data, datalen);
			ie_value->raw_datalen = datalen;
			insert = 1;
			break;
		}
		case AST_EVENT_IE_PLTYPE_UNKNOWN:
			break;
		}

		if (insert) {
			AST_LIST_INSERT_TAIL(&ie_vals, ie_value, entry);
		} else {
			ast_log(LOG_WARNING, "Unsupported PLTYPE(%d)\n", ie_value->ie_pltype);
		}
	}
	va_end(ap);

	if (!(event = ast_calloc(1, sizeof(*event)))) {
		return NULL;
	}

	event->type = htons(type);
	event->event_len = htons(sizeof(*event));

	AST_LIST_TRAVERSE(&ie_vals, ie_val, entry) {
		switch (ie_val->ie_pltype) {
		case AST_EVENT_IE_PLTYPE_STR:
			ast_event_append_ie_str(&event, ie_val->ie_type, ie_val->payload.str);
			break;
		case AST_EVENT_IE_PLTYPE_UINT:
			ast_event_append_ie_uint(&event, ie_val->ie_type, ie_val->payload.uint);
			break;
		case AST_EVENT_IE_PLTYPE_RAW:
			event_append_ie_raw(&event, ie_val->ie_type,
					ie_val->payload.raw, ie_val->raw_datalen);
			break;
		case AST_EVENT_IE_PLTYPE_UNKNOWN:
			break;
		}

		/* realloc inside one of the append functions failed */
		if (!event) {
			return NULL;
		}
	}

	return event;
}

void ast_event_destroy(struct ast_event *event)
{
	ast_free(event);
}

static int handle_event(void *data)
{
	struct ast_event *event = data;
	struct ast_event_sub *sub;
	const enum ast_event_type event_types[] = {
		ntohs(event->type),
		AST_EVENT_ALL
	};
	int i;

	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		AST_RWDLLIST_RDLOCK(&ast_event_subs[event_types[i]]);
		AST_RWDLLIST_TRAVERSE(&ast_event_subs[event_types[i]], sub, entry) {
			struct ast_event_ie_val *ie_val;

			AST_LIST_TRAVERSE(&sub->ie_vals, ie_val, entry) {
				if (!match_ie_val(event, ie_val, NULL)) {
					/* The current subscription ie did not match an event ie. */
					break;
				}
			}
			if (ie_val) {
				/* The event did not match this subscription. */
				continue;
			}
			sub->cb(event, sub->userdata);
		}
		AST_RWDLLIST_UNLOCK(&ast_event_subs[event_types[i]]);
	}

	ast_event_destroy(event);

	return 0;
}

int ast_event_queue(struct ast_event *event)
{
	uint16_t host_event_type;
	int res;

	host_event_type = ntohs(event->type);

	/* Invalid type */
	if (host_event_type >= AST_EVENT_TOTAL) {
		ast_log(LOG_WARNING, "Someone tried to queue an event of invalid "
			"type '%d'!\n", host_event_type);
		return -1;
	}

	/* If nobody has subscribed to this event type, throw it away now */
	if (ast_event_check_subscriber(host_event_type, AST_EVENT_IE_END)
			== AST_EVENT_SUB_NONE) {
		ast_event_destroy(event);
		return 0;
	}

	res = ast_taskprocessor_push(event_dispatcher, handle_event, event);
	if (res) {
		ast_event_destroy(event);
	}
	return res;
}

/*! \internal \brief Clean up resources on Asterisk shutdown */
static void event_shutdown(void)
{
	struct ast_event_sub *sub;
	int i;

	if (event_dispatcher) {
		event_dispatcher = ast_taskprocessor_unreference(event_dispatcher);
	}

	/* Remove any remaining subscriptions.  Note that we can't just call
	 * unsubscribe, as it will attempt to lock the subscription list
	 * as well */
	for (i = 0; i < AST_EVENT_TOTAL; i++) {
		AST_RWDLLIST_WRLOCK(&ast_event_subs[i]);
		while ((sub = AST_RWDLLIST_REMOVE_HEAD(&ast_event_subs[i], entry))) {
			ast_event_sub_destroy(sub);
		}
		AST_RWDLLIST_UNLOCK(&ast_event_subs[i]);
		AST_RWDLLIST_HEAD_DESTROY(&ast_event_subs[i]);
	}
}

int ast_event_init(void)
{
	int i;

	for (i = 0; i < AST_EVENT_TOTAL; i++) {
		AST_RWDLLIST_HEAD_INIT(&ast_event_subs[i]);
	}

	if (!(event_dispatcher = ast_taskprocessor_get("core_event_dispatcher", 0))) {
		goto event_init_cleanup;
	}

	ast_register_atexit(event_shutdown);

	return 0;

event_init_cleanup:
	event_shutdown();
	return -1;
}

