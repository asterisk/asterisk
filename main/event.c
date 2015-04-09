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


/*!
 * \brief A holder for an event
 *
 * \details This struct used to have more of a purpose than it does now.
 * It is used to hold events in the event cache.  It can be completely removed
 * if one of these two things is done:
 *  - ast_event gets changed such that it never has to be realloc()d
 *  - astobj2 is updated so that you can realloc() an astobj2 object
 */
struct ast_event_ref {
	struct ast_event *event;
	unsigned int cache;
};

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

static int ast_event_cmp(void *obj, void *arg, int flags);
static int ast_event_hash_mwi(const void *obj, const int flags);
static int ast_event_hash_devstate(const void *obj, const int flags);
static int ast_event_hash_devstate_change(const void *obj, const int flags);
static int ast_event_hash_presence_state_change(const void *obj, const int flags);

#ifdef LOW_MEMORY
#define NUM_CACHE_BUCKETS 17
#else
#define NUM_CACHE_BUCKETS 563
#endif

#define MAX_CACHE_ARGS 8

/*!
 * \brief Event types that are kept in the cache.
 */
static struct {
	/*!
	 * \brief Container of cached events
	 *
	 * \details This gets allocated in ast_event_init() when Asterisk starts
	 * for the event types declared as using the cache.
	 */
	struct ao2_container *container;
	/*! \brief Event type specific hash function */
	ao2_hash_fn *hash_fn;
	/*!
	 * \brief Information Elements used for caching
	 *
	 * \details This array is the set of information elements that will be unique
	 * among all events in the cache for this event type.  When a new event gets
	 * cached, a previous event with the same values for these information elements
	 * will be replaced.
	 */
	enum ast_event_ie_type cache_args[MAX_CACHE_ARGS];
} ast_event_cache[AST_EVENT_TOTAL] = {
	[AST_EVENT_MWI] = {
		.hash_fn = ast_event_hash_mwi,
		.cache_args = { AST_EVENT_IE_MAILBOX, AST_EVENT_IE_CONTEXT },
	},
	[AST_EVENT_DEVICE_STATE] = {
		.hash_fn = ast_event_hash_devstate,
		.cache_args = { AST_EVENT_IE_DEVICE, },
	},
	[AST_EVENT_DEVICE_STATE_CHANGE] = {
		.hash_fn = ast_event_hash_devstate_change,
		.cache_args = { AST_EVENT_IE_DEVICE, AST_EVENT_IE_EID, },
	},
	[AST_EVENT_PRESENCE_STATE] = {
		.hash_fn = ast_event_hash_presence_state_change,
		.cache_args = { AST_EVENT_IE_PRESENCE_STATE, },
	},

};

/*!
 * \brief Names of cached event types, for CLI tab completion
 *
 * \note These names must match what is in the event_names array.
 */
static const char * const cached_event_types[] = { "MWI", "DeviceState", "DeviceStateChange", NULL };

/*!
 * \brief Event Names
 */
static const char * const event_names[AST_EVENT_TOTAL] = {
	[AST_EVENT_ALL]                 = "All",
	[AST_EVENT_CUSTOM]              = "Custom",
	[AST_EVENT_MWI]                 = "MWI",
	[AST_EVENT_SUB]                 = "Subscription",
	[AST_EVENT_UNSUB]               = "Unsubscription",
	[AST_EVENT_DEVICE_STATE]        = "DeviceState",
	[AST_EVENT_DEVICE_STATE_CHANGE] = "DeviceStateChange",
	[AST_EVENT_CEL]                 = "CEL",
	[AST_EVENT_SECURITY]            = "Security",
	[AST_EVENT_NETWORK_CHANGE]      = "NetworkChange",
	[AST_EVENT_PRESENCE_STATE]      = "PresenceState",
	[AST_EVENT_ACL_CHANGE]          = "ACLChange",
	[AST_EVENT_PING]                = "Ping",
};

/*!
 * \brief IE payload types and names
 */
static const struct ie_map {
	enum ast_event_ie_pltype ie_pltype;
	const char *name;
} ie_maps[AST_EVENT_IE_TOTAL] = {
	[AST_EVENT_IE_NEWMSGS]             = { AST_EVENT_IE_PLTYPE_UINT, "NewMessages" },
	[AST_EVENT_IE_OLDMSGS]             = { AST_EVENT_IE_PLTYPE_UINT, "OldMessages" },
	[AST_EVENT_IE_MAILBOX]             = { AST_EVENT_IE_PLTYPE_STR,  "Mailbox" },
	[AST_EVENT_IE_UNIQUEID]            = { AST_EVENT_IE_PLTYPE_UINT, "UniqueID" },
	[AST_EVENT_IE_EVENTTYPE]           = { AST_EVENT_IE_PLTYPE_UINT, "EventType" },
	[AST_EVENT_IE_EXISTS]              = { AST_EVENT_IE_PLTYPE_UINT, "Exists" },
	[AST_EVENT_IE_DEVICE]              = { AST_EVENT_IE_PLTYPE_STR,  "Device" },
	[AST_EVENT_IE_STATE]               = { AST_EVENT_IE_PLTYPE_UINT, "State" },
	[AST_EVENT_IE_CONTEXT]             = { AST_EVENT_IE_PLTYPE_STR,  "Context" },
	[AST_EVENT_IE_EID]                 = { AST_EVENT_IE_PLTYPE_RAW,  "EntityID" },
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
	[AST_EVENT_IE_SECURITY_EVENT]      = { AST_EVENT_IE_PLTYPE_STR,  "SecurityEvent" },
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
	[AST_EVENT_IE_CACHABLE]            = { AST_EVENT_IE_PLTYPE_UINT, "Cachable" },
	[AST_EVENT_IE_PRESENCE_PROVIDER]   = { AST_EVENT_IE_PLTYPE_STR,  "PresenceProvider" },
	[AST_EVENT_IE_PRESENCE_STATE]      = { AST_EVENT_IE_PLTYPE_UINT, "PresenceState" },
	[AST_EVENT_IE_PRESENCE_SUBTYPE]    = { AST_EVENT_IE_PLTYPE_STR,  "PresenceSubtype" },
	[AST_EVENT_IE_PRESENCE_MESSAGE]    = { AST_EVENT_IE_PLTYPE_STR,  "PresenceMessage" },
};

const char *ast_event_get_type_name(const struct ast_event *event)
{
	enum ast_event_type type;

	type = ast_event_get_type(event);

	if (type >= ARRAY_LEN(event_names)) {
		ast_log(LOG_ERROR, "Invalid event type - '%u'\n", type);
		return "";
	}

	return event_names[type];
}

int ast_event_str_to_event_type(const char *str, enum ast_event_type *event_type)
{
	int i;

	for (i = 0; i < ARRAY_LEN(event_names); i++) {
		if (ast_strlen_zero(event_names[i]) || strcasecmp(event_names[i], str)) {
			continue;
		}

		*event_type = i;
		return 0;
	}

	return -1;
}

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

int ast_event_str_to_ie_type(const char *str, enum ast_event_ie_type *ie_type)
{
	int i;

	for (i = 0; i < ARRAY_LEN(ie_maps); i++) {
		if (strcasecmp(ie_maps[i].name, str)) {
			continue;
		}

		*ie_type = i;
		return 0;
	}

	return -1;
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
	case AST_EVENT_IE_PLTYPE_BITFLAGS:
	case AST_EVENT_IE_PLTYPE_EXISTS:
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
		if (sub_ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_EXISTS) {
			/* The subscription only cares that this ie exists. */
			return 1;
		}
		/* Payload types do not match. */
		return 0;
	}

	switch (sub_ie_val->ie_pltype) {
	case AST_EVENT_IE_PLTYPE_UINT:
		res = (sub_ie_val->payload.uint == event_ie_val->payload.uint);
		break;
	case AST_EVENT_IE_PLTYPE_BITFLAGS:
		/*
		 * If the subscriber has requested *any* of the bitflags we are providing,
		 * then it's a match.
		 */
		res = (sub_ie_val->payload.uint & event_ie_val->payload.uint);
		break;
	case AST_EVENT_IE_PLTYPE_STR:
	{
		const char *substr = sub_ie_val->payload.str;
		const char *estr = event_ie_val->payload.str;
		if (sub_ie_val->ie_type == AST_EVENT_IE_DEVICE) {
			substr = ast_tech_to_upper(ast_strdupa(substr));
			estr = ast_tech_to_upper(ast_strdupa(estr));
		}
		res = !strcmp(substr, estr);
		break;
	}
	case AST_EVENT_IE_PLTYPE_RAW:
		res = (sub_ie_val->raw_datalen == event_ie_val->raw_datalen
			&& !memcmp(sub_ie_val->payload.raw, event_ie_val->payload.raw,
				sub_ie_val->raw_datalen));
		break;
	case AST_EVENT_IE_PLTYPE_EXISTS:
		/* Should never get here since check_ie_vals cannot have this type. */
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
		case AST_EVENT_IE_PLTYPE_BITFLAGS:
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
		case AST_EVENT_IE_PLTYPE_EXISTS:
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

	case AST_EVENT_IE_PLTYPE_BITFLAGS:
	{
		uint32_t flags = event2 ? ast_event_get_ie_uint(event2, ie_val->ie_type) : ie_val->payload.uint;

		/*
		 * If the subscriber has requested *any* of the bitflags that this event provides,
		 * then it's a match.
		 */
		return (flags & ast_event_get_ie_bitflags(event, ie_val->ie_type)) ? 1 : 0;
	}

	case AST_EVENT_IE_PLTYPE_STR:
	{
		const char *str;
		uint32_t hash;

		hash = event2 ? ast_event_get_ie_str_hash(event2, ie_val->ie_type) : ie_val->payload.hash;
		if (hash != ast_event_get_ie_str_hash(event, ie_val->ie_type)) {
			return 0;
		}

		str = event2 ? ast_event_get_ie_str(event2, ie_val->ie_type) : ie_val->payload.str;
		if (str) {
			const char *e1str, *e2str;
			e1str = ast_event_get_ie_str(event, ie_val->ie_type);
			e2str = str;

			if (ie_val->ie_type == AST_EVENT_IE_DEVICE) {
				e1str = ast_tech_to_upper(ast_strdupa(e1str));
				e2str = ast_tech_to_upper(ast_strdupa(e2str));
			}

			if (!strcmp(e1str, e2str)) {
				return 1;
			}
		}

		return 0;
	}

	case AST_EVENT_IE_PLTYPE_RAW:
	{
		const void *buf = event2 ? ast_event_get_ie_raw(event2, ie_val->ie_type) : ie_val->payload.raw;
		uint16_t ie_payload_len = event2 ? ast_event_get_ie_raw_payload_len(event2, ie_val->ie_type) : ie_val->raw_datalen;

		return (buf
			&& ie_payload_len == ast_event_get_ie_raw_payload_len(event, ie_val->ie_type)
			&& !memcmp(buf, ast_event_get_ie_raw(event, ie_val->ie_type), ie_payload_len)) ? 1 : 0;
	}

	case AST_EVENT_IE_PLTYPE_EXISTS:
	{
		return ast_event_get_ie_raw(event, ie_val->ie_type) ? 1 : 0;
	}

	case AST_EVENT_IE_PLTYPE_UNKNOWN:
		return 0;
	}

	return 0;
}

static int dump_cache_cb(void *obj, void *arg, int flags)
{
	const struct ast_event_ref *event_ref = obj;
	const struct ast_event *event = event_ref->event;
	const struct ast_event_sub *event_sub = arg;
	struct ast_event_ie_val *ie_val = NULL;

	AST_LIST_TRAVERSE(&event_sub->ie_vals, ie_val, entry) {
		if (!match_ie_val(event, ie_val, NULL)) {
			break;
		}
	}

	if (!ie_val) {
		/* All parameters were matched on this cache entry, so dump it */
		event_sub->cb(event, event_sub->userdata);
	}

	return 0;
}

/*! \brief Dump the event cache for the subscribed event type */
void ast_event_dump_cache(const struct ast_event_sub *event_sub)
{
	if (!ast_event_cache[event_sub->type].container) {
		return;
	}

	ao2_callback(ast_event_cache[event_sub->type].container, OBJ_NODATA,
			dump_cache_cb, (void *) event_sub);
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
		case AST_EVENT_IE_PLTYPE_EXISTS:
			ast_event_append_ie_uint(&event, AST_EVENT_IE_EXISTS, ie_val->ie_type);
			break;
		case AST_EVENT_IE_PLTYPE_UINT:
			ast_event_append_ie_uint(&event, ie_val->ie_type, ie_val->payload.uint);
			break;
		case AST_EVENT_IE_PLTYPE_BITFLAGS:
			ast_event_append_ie_bitflags(&event, ie_val->ie_type, ie_val->payload.uint);
			break;
		case AST_EVENT_IE_PLTYPE_STR:
			ast_event_append_ie_str(&event, ie_val->ie_type, ie_val->payload.str);
			break;
		case AST_EVENT_IE_PLTYPE_RAW:
			ast_event_append_ie_raw(&event, ie_val->ie_type, ie_val->payload.raw, ie_val->raw_datalen);
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
	int found = 0;
	struct ast_event_ie_val *ie_val;

	if (event_sub->type != AST_EVENT_SUB)
		return;

	AST_LIST_TRAVERSE(&event_sub->ie_vals, ie_val, entry) {
		if (ie_val->ie_type == AST_EVENT_IE_EVENTTYPE) {
			event_type = ie_val->payload.uint;
			found = 1;
			break;
		}
	}

	if (!found) {
		return;
	}

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

	if (type >= AST_EVENT_TOTAL) {
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

int ast_event_sub_append_ie_bitflags(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type, uint32_t flags)
{
	struct ast_event_ie_val *ie_val;

	if (ie_type <= 0 || ie_type >= AST_EVENT_IE_TOTAL) {
		return -1;
	}

	if (!(ie_val = ast_calloc(1, sizeof(*ie_val)))) {
		return -1;
	}

	ie_val->ie_type = ie_type;
	ie_val->payload.uint = flags;
	ie_val->ie_pltype = AST_EVENT_IE_PLTYPE_BITFLAGS;

	AST_LIST_INSERT_TAIL(&sub->ie_vals, ie_val, entry);

	return 0;
}

int ast_event_sub_append_ie_exists(struct ast_event_sub *sub,
	enum ast_event_ie_type ie_type)
{
	struct ast_event_ie_val *ie_val;

	if (ie_type <= 0 || ie_type >= AST_EVENT_IE_TOTAL) {
		return -1;
	}

	if (!(ie_val = ast_calloc(1, sizeof(*ie_val)))) {
		return -1;
	}

	ie_val->ie_type = ie_type;
	ie_val->ie_pltype = AST_EVENT_IE_PLTYPE_EXISTS;

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

	if (ie_type == AST_EVENT_IE_DEVICE) {
		char *uppertech = ast_strdupa(str);
		ast_tech_to_upper(uppertech);
		ie_val->payload.hash = ast_str_hash(uppertech);
	} else {
		ie_val->payload.hash = ast_str_hash(str);
	}

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
		case AST_EVENT_IE_PLTYPE_BITFLAGS:
		{
			uint32_t unsigned_int = va_arg(ap, uint32_t);
			ast_event_sub_append_ie_bitflags(sub, ie_type, unsigned_int);
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
		case AST_EVENT_IE_PLTYPE_EXISTS:
			ast_event_sub_append_ie_exists(sub, ie_type);
			break;
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

const char *ast_event_subscriber_get_description(struct ast_event_sub *sub)
{
	return sub ? sub->description : NULL;
}

struct ast_event_sub *ast_event_unsubscribe(struct ast_event_sub *sub)
{
	struct ast_event *event;

	AST_RWDLLIST_WRLOCK(&ast_event_subs[sub->type]);
	AST_DLLIST_REMOVE(&ast_event_subs[sub->type], sub, entry);
	AST_RWDLLIST_UNLOCK(&ast_event_subs[sub->type]);

	if (ast_event_check_subscriber(AST_EVENT_UNSUB,
		AST_EVENT_IE_EVENTTYPE, AST_EVENT_IE_PLTYPE_UINT, sub->type,
		AST_EVENT_IE_END) != AST_EVENT_SUB_NONE) {

		event = ast_event_new(AST_EVENT_UNSUB,
			AST_EVENT_IE_UNIQUEID,    AST_EVENT_IE_PLTYPE_UINT, sub->uniqueid,
			AST_EVENT_IE_EVENTTYPE,   AST_EVENT_IE_PLTYPE_UINT, sub->type,
			AST_EVENT_IE_DESCRIPTION, AST_EVENT_IE_PLTYPE_STR, sub->description,
			AST_EVENT_IE_END);
		if (event && ast_event_queue(event)) {
			ast_event_destroy(event);
		}
	}

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

uint32_t ast_event_iterator_get_ie_bitflags(struct ast_event_iterator *iterator)
{
	return ntohl(get_unaligned_uint32(iterator->ie->ie_payload));
}

const char *ast_event_iterator_get_ie_str(struct ast_event_iterator *iterator)
{
	const struct ast_event_ie_str_payload *str_payload;

	str_payload = (struct ast_event_ie_str_payload *) iterator->ie->ie_payload;

	return str_payload ? str_payload->str : NULL;
}

void *ast_event_iterator_get_ie_raw(struct ast_event_iterator *iterator)
{
	return iterator->ie->ie_payload;
}

uint16_t ast_event_iterator_get_ie_raw_payload_len(struct ast_event_iterator *iterator)
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

	ie_val = ast_event_get_ie_raw(event, ie_type);

	return ie_val ? ntohl(get_unaligned_uint32(ie_val)) : 0;
}

uint32_t ast_event_get_ie_bitflags(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	const uint32_t *ie_val;

	ie_val = ast_event_get_ie_raw(event, ie_type);

	return ie_val ? ntohl(get_unaligned_uint32(ie_val)) : 0;
}

uint32_t ast_event_get_ie_str_hash(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	const struct ast_event_ie_str_payload *str_payload;

	str_payload = ast_event_get_ie_raw(event, ie_type);

	return str_payload ? str_payload->hash : 0;
}

const char *ast_event_get_ie_str(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	const struct ast_event_ie_str_payload *str_payload;

	str_payload = ast_event_get_ie_raw(event, ie_type);

	return str_payload ? str_payload->str : NULL;
}

const void *ast_event_get_ie_raw(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	struct ast_event_iterator iterator;
	int res;

	for (res = ast_event_iterator_init(&iterator, event); !res; res = ast_event_iterator_next(&iterator)) {
		if (ast_event_iterator_get_ie_type(&iterator) == ie_type) {
			return ast_event_iterator_get_ie_raw(&iterator);
		}
	}

	return NULL;
}

uint16_t ast_event_get_ie_raw_payload_len(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	struct ast_event_iterator iterator;
	int res;

	for (res = ast_event_iterator_init(&iterator, event); !res; res = ast_event_iterator_next(&iterator)) {
		if (ast_event_iterator_get_ie_type(&iterator) == ie_type) {
			return ast_event_iterator_get_ie_raw_payload_len(&iterator);
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
	if (ie_type == AST_EVENT_IE_DEVICE) {
		char *uppertech = ast_strdupa(str);
		ast_tech_to_upper(uppertech);
		str_payload->hash = ast_str_hash(uppertech);
	} else {
		str_payload->hash = ast_str_hash(str);
	}

	return ast_event_append_ie_raw(event, ie_type, str_payload, payload_len);
}

int ast_event_append_ie_uint(struct ast_event **event, enum ast_event_ie_type ie_type,
	uint32_t data)
{
	data = htonl(data);
	return ast_event_append_ie_raw(event, ie_type, &data, sizeof(data));
}

int ast_event_append_ie_bitflags(struct ast_event **event, enum ast_event_ie_type ie_type,
	uint32_t flags)
{
	flags = htonl(flags);
	return ast_event_append_ie_raw(event, ie_type, &flags, sizeof(flags));
}

int ast_event_append_ie_raw(struct ast_event **event, enum ast_event_ie_type ie_type,
	const void *data, size_t data_len)
{
	struct ast_event_ie *ie;
	struct ast_event *old_event;
	unsigned int extra_len;
	uint16_t event_len;

	event_len = ntohs((*event)->event_len);
	extra_len = sizeof(*ie) + data_len;

	old_event = *event;
	*event = ast_realloc(*event, event_len + extra_len);
	if (!*event) {
		ast_free(old_event);
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
			"type '%u'!\n", type);
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
		case AST_EVENT_IE_PLTYPE_BITFLAGS:
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
		case AST_EVENT_IE_PLTYPE_EXISTS:
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
		case AST_EVENT_IE_PLTYPE_BITFLAGS:
			ast_event_append_ie_bitflags(&event, ie_val->ie_type, ie_val->payload.uint);
			break;
		case AST_EVENT_IE_PLTYPE_RAW:
			ast_event_append_ie_raw(&event, ie_val->ie_type,
					ie_val->payload.raw, ie_val->raw_datalen);
			break;
		case AST_EVENT_IE_PLTYPE_EXISTS:
		case AST_EVENT_IE_PLTYPE_UNKNOWN:
			break;
		}

		/* realloc inside one of the append functions failed */
		if (!event) {
			return NULL;
		}
	}

	if (!ast_event_get_ie_raw(event, AST_EVENT_IE_EID)) {
		/* If the event is originating on this server, add the server's
		 * entity ID to the event. */
		ast_event_append_eid(&event);
	}

	return event;
}

int ast_event_append_eid(struct ast_event **event)
{
	return ast_event_append_ie_raw(event, AST_EVENT_IE_EID,
			&ast_eid_default, sizeof(ast_eid_default));
}

void ast_event_destroy(struct ast_event *event)
{
	ast_free(event);
}

static void ast_event_ref_destroy(void *obj)
{
	struct ast_event_ref *event_ref = obj;

	ast_event_destroy(event_ref->event);
}

static struct ast_event *ast_event_dup(const struct ast_event *event)
{
	struct ast_event *dup_event;
	uint16_t event_len;

	event_len = ast_event_get_size(event);

	if (!(dup_event = ast_calloc(1, event_len))) {
		return NULL;
	}

	memcpy(dup_event, event, event_len);

	return dup_event;
}

struct ast_event *ast_event_get_cached(enum ast_event_type type, ...)
{
	va_list ap;
	enum ast_event_ie_type ie_type;
	struct ast_event *dup_event = NULL;
	struct ast_event_ref *cached_event_ref;
	struct ast_event *cache_arg_event;
	struct ast_event_ref tmp_event_ref = {
		.event = NULL,
	};
	struct ao2_container *container = NULL;

	if (type >= AST_EVENT_TOTAL) {
		ast_log(LOG_ERROR, "%u is an invalid type!\n", type);
		return NULL;
	}

	if (!(container = ast_event_cache[type].container)) {
		ast_log(LOG_ERROR, "%u is not a cached event type\n", type);
		return NULL;
	}

	if (!(cache_arg_event = ast_event_new(type, AST_EVENT_IE_END))) {
		return NULL;
	}

	va_start(ap, type);
	for (ie_type = va_arg(ap, enum ast_event_ie_type);
		ie_type != AST_EVENT_IE_END;
		ie_type = va_arg(ap, enum ast_event_ie_type))
	{
		enum ast_event_ie_pltype ie_pltype;

		ie_pltype = va_arg(ap, enum ast_event_ie_pltype);

		switch (ie_pltype) {
		case AST_EVENT_IE_PLTYPE_UINT:
			ast_event_append_ie_uint(&cache_arg_event, ie_type, va_arg(ap, uint32_t));
			break;
		case AST_EVENT_IE_PLTYPE_BITFLAGS:
			ast_event_append_ie_bitflags(&cache_arg_event, ie_type, va_arg(ap, uint32_t));
			break;
		case AST_EVENT_IE_PLTYPE_STR:
			ast_event_append_ie_str(&cache_arg_event, ie_type, va_arg(ap, const char *));
			break;
		case AST_EVENT_IE_PLTYPE_RAW:
		{
			void *data = va_arg(ap, void *);
			size_t datalen = va_arg(ap, size_t);
			ast_event_append_ie_raw(&cache_arg_event, ie_type, data, datalen);
			break;
		}
		case AST_EVENT_IE_PLTYPE_EXISTS:
			ast_log(LOG_WARNING, "PLTYPE_EXISTS not supported by this function\n");
			break;
		case AST_EVENT_IE_PLTYPE_UNKNOWN:
			break;
		}
	}
	va_end(ap);

	tmp_event_ref.event = cache_arg_event;

	cached_event_ref = ao2_find(container, &tmp_event_ref, OBJ_POINTER);

	ast_event_destroy(cache_arg_event);
	cache_arg_event = NULL;

	if (cached_event_ref) {
		dup_event = ast_event_dup(cached_event_ref->event);
		ao2_ref(cached_event_ref, -1);
		cached_event_ref = NULL;
	}

	return dup_event;
}

static struct ast_event_ref *alloc_event_ref(void)
{
	return ao2_alloc(sizeof(struct ast_event_ref), ast_event_ref_destroy);
}

/*!
 * \internal
 * \brief Update the given event cache with the new event.
 * \since 1.8
 *
 * \param cache Event cache container to update.
 * \param event New event to put in the cache.
 *
 * \return Nothing
 */
static void event_update_cache(struct ao2_container *cache, struct ast_event *event)
{
	struct ast_event_ref tmp_event_ref = {
		.event = event,
	};
	struct ast_event *dup_event;
	struct ast_event_ref *event_ref;

	/* Hold the cache container lock while it is updated. */
	ao2_lock(cache);

	/* Remove matches from the cache. */
	ao2_callback(cache, OBJ_POINTER | OBJ_UNLINK | OBJ_MULTIPLE | OBJ_NODATA,
		ast_event_cmp, &tmp_event_ref);

	/* Save a copy of the event in the cache. */
	dup_event = ast_event_dup(event);
	if (dup_event) {
		event_ref = alloc_event_ref();
		if (event_ref) {
			event_ref->event = dup_event;
			ao2_link(cache, event_ref);
			ao2_ref(event_ref, -1);
		} else {
			ast_event_destroy(dup_event);
		}
	}

	ao2_unlock(cache);
}

static int handle_event(void *data)
{
	struct ast_event_ref *event_ref = data;
	struct ast_event_sub *sub;
	const enum ast_event_type event_types[] = {
		ntohs(event_ref->event->type),
		AST_EVENT_ALL
	};
	int i;

	if (event_ref->cache) {
		struct ao2_container *container;
		container = ast_event_cache[ast_event_get_type(event_ref->event)].container;
		if (!container) {
			ast_log(LOG_WARNING, "cache requested for non-cached event type\n");
		} else {
			event_update_cache(container, event_ref->event);
		}
	}

	for (i = 0; i < ARRAY_LEN(event_types); i++) {
		AST_RWDLLIST_RDLOCK(&ast_event_subs[event_types[i]]);
		AST_RWDLLIST_TRAVERSE(&ast_event_subs[event_types[i]], sub, entry) {
			struct ast_event_ie_val *ie_val;

			AST_LIST_TRAVERSE(&sub->ie_vals, ie_val, entry) {
				if (!match_ie_val(event_ref->event, ie_val, NULL)) {
					/* The current subscription ie did not match an event ie. */
					break;
				}
			}
			if (ie_val) {
				/* The event did not match this subscription. */
				continue;
			}
			sub->cb(event_ref->event, sub->userdata);
		}
		AST_RWDLLIST_UNLOCK(&ast_event_subs[event_types[i]]);
	}

	ao2_ref(event_ref, -1);

	return 0;
}

static int _ast_event_queue(struct ast_event *event, unsigned int cache)
{
	struct ast_event_ref *event_ref;
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

	if (!(event_ref = alloc_event_ref())) {
		return -1;
	}

	event_ref->event = event;
	event_ref->cache = cache;

	res = ast_taskprocessor_push(event_dispatcher, handle_event, event_ref);
	if (res) {
		event_ref->event = NULL;
		ao2_ref(event_ref, -1);
	}
	return res;
}

int ast_event_queue(struct ast_event *event)
{
	return _ast_event_queue(event, 0);
}

int ast_event_queue_and_cache(struct ast_event *event)
{
	return _ast_event_queue(event, 1);
}

static int ast_event_hash_mwi(const void *obj, const int flags)
{
	const struct ast_event *event = obj;
	const char *mailbox = ast_event_get_ie_str(event, AST_EVENT_IE_MAILBOX);
	const char *context = ast_event_get_ie_str(event, AST_EVENT_IE_CONTEXT);

	return ast_str_hash_add(context, ast_str_hash(mailbox));
}

/*!
 * \internal
 * \brief Hash function for AST_EVENT_DEVICE_STATE
 *
 * \param[in] obj an ast_event
 * \param[in] flags unused
 *
 * \return hash value
 */
static int ast_event_hash_devstate(const void *obj, const int flags)
{
	const struct ast_event *event = obj;

	return ast_str_hash(ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE));
}

/*!
 * \internal
 * \brief Hash function for AST_EVENT_DEVICE_STATE_CHANGE
 *
 * \param[in] obj an ast_event
 * \param[in] flags unused
 *
 * \return hash value
 */
static int ast_event_hash_devstate_change(const void *obj, const int flags)
{
	const struct ast_event *event = obj;

	return ast_str_hash(ast_event_get_ie_str(event, AST_EVENT_IE_DEVICE));
}

/*!
 * \internal
 * \brief Hash function for AST_EVENT_PRESENCE_STATE
 *
 * \param[in] obj an ast_event
 * \param[in] flags unused
 *
 * \return hash value
 */
static int ast_event_hash_presence_state_change(const void *obj, const int flags)
{
	const struct ast_event *event = obj;

	return ast_str_hash(ast_event_get_ie_str(event, AST_EVENT_IE_PRESENCE_PROVIDER));
}

static int ast_event_hash(const void *obj, const int flags)
{
	const struct ast_event_ref *event_ref;
	const struct ast_event *event;
	ao2_hash_fn *hash_fn;

	event_ref = obj;
	event = event_ref->event;

	if (!(hash_fn = ast_event_cache[ast_event_get_type(event)].hash_fn)) {
		return 0;
	}

	return hash_fn(event, flags);
}

/*!
 * \internal
 * \brief Compare two events
 *
 * \param[in] obj the first event, as an ast_event_ref
 * \param[in] arg the second event, as an ast_event_ref
 * \param[in] flags unused
 *
 * \pre Both events must be the same type.
 * \pre The event type must be declared as a cached event type in ast_event_cache
 *
 * \details This function takes two events, and determines if they are considered
 * equivalent.  The values of information elements specified in the cache arguments
 * for the event type are used to determine if the events are equivalent.
 *
 * \retval 0 No match
 * \retval CMP_MATCH The events are considered equivalent based on the cache arguments
 */
static int ast_event_cmp(void *obj, void *arg, int flags)
{
	struct ast_event_ref *event_ref, *event_ref2;
	struct ast_event *event, *event2;
	int res = CMP_MATCH;
	int i;
	enum ast_event_ie_type *cache_args;

	event_ref = obj;
	event = event_ref->event;

	event_ref2 = arg;
	event2 = event_ref2->event;

	cache_args = ast_event_cache[ast_event_get_type(event)].cache_args;

	for (i = 0; i < ARRAY_LEN(ast_event_cache[0].cache_args) && cache_args[i]; i++) {
		struct ast_event_ie_val ie_val = {
			.ie_pltype = ast_event_get_ie_pltype(cache_args[i]),
			.ie_type = cache_args[i],
		};

		if (!match_ie_val(event, &ie_val, event2)) {
			res = 0;
			break;
		}
	}

	return res;
}

static void dump_raw_ie(struct ast_event_iterator *i, struct ast_cli_args *a)
{
	char eid_buf[32];
	enum ast_event_ie_type ie_type;
	const char *ie_type_name;

	ie_type = ast_event_iterator_get_ie_type(i);
	ie_type_name = ast_event_get_ie_type_name(ie_type);

	switch (ie_type) {
	case AST_EVENT_IE_EID:
		ast_eid_to_str(eid_buf, sizeof(eid_buf), ast_event_iterator_get_ie_raw(i));
		ast_cli(a->fd, "%.30s: %s\n", ie_type_name, eid_buf);
		break;
	default:
		ast_cli(a->fd, "%s\n", ie_type_name);
		break;
	}
}

static int event_dump_cli(void *obj, void *arg, int flags)
{
	const struct ast_event_ref *event_ref = obj;
	const struct ast_event *event = event_ref->event;
	struct ast_cli_args *a = arg;
	struct ast_event_iterator i;

	if (ast_event_iterator_init(&i, event)) {
		ast_cli(a->fd, "Failed to initialize event iterator.  :-(\n");
		return 0;
	}

	ast_cli(a->fd, "Event: %s\n", ast_event_get_type_name(event));

	do {
		enum ast_event_ie_type ie_type;
		enum ast_event_ie_pltype ie_pltype;
		const char *ie_type_name;

		ie_type = ast_event_iterator_get_ie_type(&i);
		ie_type_name = ast_event_get_ie_type_name(ie_type);
		ie_pltype = ast_event_get_ie_pltype(ie_type);

		switch (ie_pltype) {
		case AST_EVENT_IE_PLTYPE_UNKNOWN:
		case AST_EVENT_IE_PLTYPE_EXISTS:
			ast_cli(a->fd, "%s\n", ie_type_name);
			break;
		case AST_EVENT_IE_PLTYPE_STR:
			ast_cli(a->fd, "%.30s: %s\n", ie_type_name,
					ast_event_iterator_get_ie_str(&i));
			break;
		case AST_EVENT_IE_PLTYPE_UINT:
			ast_cli(a->fd, "%.30s: %u\n", ie_type_name,
					ast_event_iterator_get_ie_uint(&i));
			break;
		case AST_EVENT_IE_PLTYPE_BITFLAGS:
			ast_cli(a->fd, "%.30s: %u\n", ie_type_name,
					ast_event_iterator_get_ie_bitflags(&i));
			break;
		case AST_EVENT_IE_PLTYPE_RAW:
			dump_raw_ie(&i, a);
			break;
		}
	} while (!ast_event_iterator_next(&i));

	ast_cli(a->fd, "\n");

	return 0;
}

static char *event_dump_cache(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	enum ast_event_type event_type;
	enum ast_event_ie_type *cache_args;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "event dump cache";
		e->usage =
			"Usage: event dump cache <event type>\n"
			"       Dump all of the cached events for the given event type.\n"
			"       This is primarily intended for debugging.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return ast_cli_complete(a->word, cached_event_types, a->n);
		}
		return NULL;
	case CLI_HANDLER:
		break;
	}

	if (a->argc != e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	if (ast_event_str_to_event_type(a->argv[e->args], &event_type)) {
		ast_cli(a->fd, "Invalid cached event type: '%s'\n", a->argv[e->args]);
		return CLI_SHOWUSAGE;
	}

	if (!ast_event_cache[event_type].container) {
		ast_cli(a->fd, "Event type '%s' has no cache.\n", a->argv[e->args]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "Event Type: %s\n", a->argv[e->args]);
	ast_cli(a->fd, "Cache Unique Keys:\n");
	cache_args = ast_event_cache[event_type].cache_args;
	for (i = 0; i < ARRAY_LEN(ast_event_cache[0].cache_args) && cache_args[i]; i++) {
		ast_cli(a->fd, "--> %s\n", ast_event_get_ie_type_name(cache_args[i]));
	}

	ast_cli(a->fd, "\n--- Begin Cache Dump ---\n\n");
	ao2_callback(ast_event_cache[event_type].container, OBJ_NODATA, event_dump_cli, a);
	ast_cli(a->fd, "--- End Cache Dump ---\n\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry event_cli[] = {
	AST_CLI_DEFINE(event_dump_cache, "Dump the internal event cache (for debugging)"),
};

/*! \internal \brief Clean up resources on Asterisk shutdown */
static void event_shutdown(void)
{
	struct ast_event_sub *sub;
	int i;

	ast_cli_unregister_multiple(event_cli, ARRAY_LEN(event_cli));

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

	for (i = 0; i < AST_EVENT_TOTAL; i++) {
		if (!ast_event_cache[i].hash_fn) {
			continue;
		}
		if (ast_event_cache[i].container) {
			ao2_ref(ast_event_cache[i].container, -1);
		}
	}
}

int ast_event_init(void)
{
	int i;

	for (i = 0; i < AST_EVENT_TOTAL; i++) {
		AST_RWDLLIST_HEAD_INIT(&ast_event_subs[i]);
	}

	for (i = 0; i < AST_EVENT_TOTAL; i++) {
		if (!ast_event_cache[i].hash_fn) {
			/* This event type is not cached. */
			continue;
		}

		if (!(ast_event_cache[i].container = ao2_container_alloc(NUM_CACHE_BUCKETS,
				ast_event_hash, ast_event_cmp))) {
			goto event_init_cleanup;
		}
	}

	if (!(event_dispatcher = ast_taskprocessor_get("core_event_dispatcher", 0))) {
		goto event_init_cleanup;
	}

	ast_cli_register_multiple(event_cli, ARRAY_LEN(event_cli));

	ast_register_cleanup(event_shutdown);

	return 0;

event_init_cleanup:
	event_shutdown();
	return -1;
}

size_t ast_event_minimum_length(void)
{
	return sizeof(struct ast_event);
}
