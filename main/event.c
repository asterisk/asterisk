/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/event.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/unaligned.h"

/* Only use one thread for now to ensure ordered delivery */
#define NUM_EVENT_THREADS 1

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
} __attribute__ ((packed));

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
} __attribute__ ((packed));

struct ast_event_ref {
	struct ast_event *event;
	AST_LIST_ENTRY(ast_event_ref) entry;
};

struct ast_event_iterator {
	uint16_t event_len;
	const struct ast_event *event;
	struct ast_event_ie *ie;
};

/*! \brief data shared between event dispatching threads */
static struct {
	ast_cond_t cond;
	ast_mutex_t lock;
	AST_LIST_HEAD_NOLOCK(, ast_event_ref) event_q;
} event_thread = {
	.lock = AST_MUTEX_INIT_VALUE,
};

struct ast_event_ie_val {
	AST_LIST_ENTRY(ast_event_ie_val) entry;
	enum ast_event_ie_type ie_type;
	enum ast_event_ie_pltype ie_pltype;
	union {
		uint32_t uint;
		const char *str;
	} payload;
};

/*! \brief Event subscription */
struct ast_event_sub {
	enum ast_event_type type;
	ast_event_cb_t cb;
	void *userdata;
	uint32_t uniqueid;
	AST_LIST_HEAD_NOLOCK(, ast_event_ie_val) ie_vals;
	AST_RWLIST_ENTRY(ast_event_sub) entry;
};

static uint32_t sub_uniqueid;

/*! \brief Event subscriptions
 * The event subscribers are indexed by which event they are subscribed to */
static AST_RWLIST_HEAD(ast_event_sub_list, ast_event_sub) ast_event_subs[AST_EVENT_TOTAL];

/*! \brief Cached events
 * The event cache is indexed on the event type.  The purpose of this is 
 * for events that express some sort of state.  So, when someone first
 * needs to know this state, it can get the last known state from the cache. */
static AST_RWLIST_HEAD(ast_event_ref_list, ast_event_ref) ast_event_cache[AST_EVENT_TOTAL];

static void ast_event_ie_val_destroy(struct ast_event_ie_val *ie_val)
{
	if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_STR)
		ast_free((void *) ie_val->payload.str);

	ast_free(ie_val);
}

enum ast_event_subscriber_res ast_event_check_subscriber(enum ast_event_type type, ...)
{
	va_list ap;
	enum ast_event_ie_type ie_type;
	enum ast_event_subscriber_res res = AST_EVENT_SUB_NONE;
	struct ast_event_ie_val *ie_val, *sub_ie_val;
	struct ast_event_sub *sub;
	AST_LIST_HEAD_NOLOCK_STATIC(ie_vals, ast_event_ie_val);

	if (type >= AST_EVENT_TOTAL) {
		ast_log(LOG_ERROR, "%u is an invalid type!\n", type);
		return res;
	}

	va_start(ap, type);
	for (ie_type = va_arg(ap, enum ast_event_type);
		ie_type != AST_EVENT_IE_END;
		ie_type = va_arg(ap, enum ast_event_type))
	{
		struct ast_event_ie_val *ie_val = alloca(sizeof(*ie_val));
		memset(ie_val, 0, sizeof(*ie_val));
		ie_val->ie_type = ie_type;
		ie_val->ie_pltype = va_arg(ap, enum ast_event_ie_pltype);
		if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_UINT)
			ie_val->payload.uint = va_arg(ap, uint32_t);
		else if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_STR)
			ie_val->payload.str = ast_strdupa(va_arg(ap, const char *));
		AST_LIST_INSERT_TAIL(&ie_vals, ie_val, entry);
	}
	va_end(ap);

	AST_RWLIST_RDLOCK(&ast_event_subs[type]);
	AST_RWLIST_TRAVERSE(&ast_event_subs[type], sub, entry) {
		AST_LIST_TRAVERSE(&ie_vals, ie_val, entry) {
			AST_LIST_TRAVERSE(&sub->ie_vals, sub_ie_val, entry) {
				if (sub_ie_val->ie_type == ie_val->ie_type)
					break;
			}
			if (!sub_ie_val) {
				if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_EXISTS)
					break;
				continue;
			}
			/* The subscriber doesn't actually care what the value is */
			if (sub_ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_EXISTS)
				continue;
			if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_UINT &&
				ie_val->payload.uint != sub_ie_val->payload.uint)
				break;
			if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_STR &&
				strcmp(ie_val->payload.str, sub_ie_val->payload.str))
				break;
		}
		if (!ie_val)
			break;
	}
	AST_RWLIST_UNLOCK(&ast_event_subs[type]);

	if (sub) /* All parameters were matched */
		return AST_EVENT_SUB_EXISTS;

	AST_RWLIST_RDLOCK(&ast_event_subs[AST_EVENT_ALL]);
	if (!AST_LIST_EMPTY(&ast_event_subs[AST_EVENT_ALL]))
		res = AST_EVENT_SUB_EXISTS;
	AST_RWLIST_UNLOCK(&ast_event_subs[AST_EVENT_ALL]);

	return res;
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

	AST_RWLIST_RDLOCK(&ast_event_subs[event_type]);
	AST_RWLIST_TRAVERSE(&ast_event_subs[event_type], sub, entry) {
		if (event_sub == sub)
			continue;

		event = ast_event_new(AST_EVENT_SUB,
			AST_EVENT_IE_UNIQUEID,  AST_EVENT_IE_PLTYPE_UINT, sub->uniqueid,
			AST_EVENT_IE_EVENTTYPE, AST_EVENT_IE_PLTYPE_UINT, sub->type,
			AST_EVENT_IE_END);

		AST_LIST_TRAVERSE(&sub->ie_vals, ie_val, entry) {
			switch (ie_val->ie_pltype) {
			case AST_EVENT_IE_PLTYPE_EXISTS:
				ast_event_append_ie_uint(&event, AST_EVENT_IE_EXISTS, ie_val->ie_type);
				break;
			case AST_EVENT_IE_PLTYPE_UINT:
				ast_event_append_ie_uint(&event, ie_val->ie_type, ie_val->payload.uint);
				break;
			case AST_EVENT_IE_PLTYPE_STR:
				ast_event_append_ie_str(&event, ie_val->ie_type, ie_val->payload.str);
				break;
			}
			if (!event)
				break;
		}

		if (!event)
			continue;

		event_sub->cb(event, event_sub->userdata);

		ast_event_destroy(event);
	}
	AST_RWLIST_UNLOCK(&ast_event_subs[event_type]);
}

struct ast_event_sub *ast_event_subscribe(enum ast_event_type type, ast_event_cb_t cb, 
	void *userdata, ...)
{
	va_list ap;
	enum ast_event_ie_type ie_type;
	struct ast_event_sub *sub;
	struct ast_event *event;

	if (type >= AST_EVENT_TOTAL) {
		ast_log(LOG_ERROR, "%u is an invalid type!\n", type);
		return NULL;
	}

	if (!(sub = ast_calloc(1, sizeof(*sub))))
		return NULL;

	va_start(ap, userdata);
	for (ie_type = va_arg(ap, enum ast_event_type);
		ie_type != AST_EVENT_IE_END;
		ie_type = va_arg(ap, enum ast_event_type))
	{
		struct ast_event_ie_val *ie_val;
		if (!(ie_val = ast_calloc(1, sizeof(*ie_val))))
			continue;
		ie_val->ie_type = ie_type;
		ie_val->ie_pltype = va_arg(ap, enum ast_event_ie_pltype);
		if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_UINT)
			ie_val->payload.uint = va_arg(ap, uint32_t);
		else if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_STR) {
			if (!(ie_val->payload.str = ast_strdup(va_arg(ap, const char *)))) {
				ast_free(ie_val);
				continue;
			}
		}
		AST_LIST_INSERT_TAIL(&sub->ie_vals, ie_val, entry);
	}
	va_end(ap);

	sub->type = type;
	sub->cb = cb;
	sub->userdata = userdata;
	sub->uniqueid = ast_atomic_fetchadd_int((int *) &sub_uniqueid, 1);

	if (ast_event_check_subscriber(AST_EVENT_SUB,
		AST_EVENT_IE_EVENTTYPE, AST_EVENT_IE_PLTYPE_UINT, type,
		AST_EVENT_IE_END) != AST_EVENT_SUB_NONE) {
		struct ast_event_ie_val *ie_val;

		event = ast_event_new(AST_EVENT_SUB,
			AST_EVENT_IE_UNIQUEID,  AST_EVENT_IE_PLTYPE_UINT, sub->uniqueid,
			AST_EVENT_IE_EVENTTYPE, AST_EVENT_IE_PLTYPE_UINT, sub->type,
			AST_EVENT_IE_END);

		AST_LIST_TRAVERSE(&sub->ie_vals, ie_val, entry) {
			switch (ie_val->ie_pltype) {
			case AST_EVENT_IE_PLTYPE_EXISTS:
				ast_event_append_ie_uint(&event, AST_EVENT_IE_EXISTS, ie_val->ie_type);
				break;
			case AST_EVENT_IE_PLTYPE_UINT:
				ast_event_append_ie_uint(&event, ie_val->ie_type, ie_val->payload.uint);
				break;
			case AST_EVENT_IE_PLTYPE_STR:
				ast_event_append_ie_str(&event, ie_val->ie_type, ie_val->payload.str);
				break;
			}
			if (!event)
				break;
		}

		if (event)
			ast_event_queue(event);
	}

	AST_RWLIST_WRLOCK(&ast_event_subs[type]);
	AST_RWLIST_INSERT_TAIL(&ast_event_subs[type], sub, entry);
	AST_RWLIST_UNLOCK(&ast_event_subs[type]);

	return sub;
}

static void ast_event_sub_destroy(struct ast_event_sub *sub)
{
	struct ast_event_ie_val *ie_val;

	while ((ie_val = AST_LIST_REMOVE_HEAD(&sub->ie_vals, entry)))
		ast_event_ie_val_destroy(ie_val);

	ast_free(sub);
}

void ast_event_unsubscribe(struct ast_event_sub *sub)
{
	struct ast_event *event;

	AST_RWLIST_WRLOCK(&ast_event_subs[sub->type]);
	AST_LIST_REMOVE(&ast_event_subs[sub->type], sub, entry);
	AST_RWLIST_UNLOCK(&ast_event_subs[sub->type]);

	if (ast_event_check_subscriber(AST_EVENT_UNSUB,
		AST_EVENT_IE_EVENTTYPE, AST_EVENT_IE_PLTYPE_UINT, sub->type,
		AST_EVENT_IE_END) != AST_EVENT_SUB_NONE) {
		
		event = ast_event_new(AST_EVENT_UNSUB,
			AST_EVENT_IE_UNIQUEID,  AST_EVENT_IE_PLTYPE_UINT, sub->uniqueid,
			AST_EVENT_IE_EVENTTYPE, AST_EVENT_IE_PLTYPE_UINT, sub->type,
			AST_EVENT_IE_END);

		if (event)
			ast_event_queue(event);
	}

	ast_event_sub_destroy(sub);
}

void ast_event_iterator_init(struct ast_event_iterator *iterator, const struct ast_event *event)
{
	iterator->event_len = ntohs(event->event_len);
	iterator->event = event;
	iterator->ie = (struct ast_event_ie *) ( ((char *) event) + sizeof(*event) );
	return;
}

int ast_event_iterator_next(struct ast_event_iterator *iterator)
{
	iterator->ie = (struct ast_event_ie *) ( ((char *) iterator->ie) + sizeof(*iterator->ie) + ntohs(iterator->ie->ie_payload_len));
	return ((iterator->event_len < (((char *) iterator->ie) - ((char *) iterator->event))) ? -1 : 0);
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
	return (const char*)iterator->ie->ie_payload;
}

void *ast_event_iterator_get_ie_raw(struct ast_event_iterator *iterator)
{
	return iterator->ie->ie_payload;
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

const char *ast_event_get_ie_str(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	return ast_event_get_ie_raw(event, ie_type);
}

const void *ast_event_get_ie_raw(const struct ast_event *event, enum ast_event_ie_type ie_type)
{
	struct ast_event_iterator iterator;
	int res = 0;

	for (ast_event_iterator_init(&iterator, event); !res; res = ast_event_iterator_next(&iterator)) {
		if (ast_event_iterator_get_ie_type(&iterator) == ie_type)
			return ast_event_iterator_get_ie_raw(&iterator);
	}

	return NULL;
}

int ast_event_append_ie_str(struct ast_event **event, enum ast_event_ie_type ie_type,
	const char *str)
{
	return ast_event_append_ie_raw(event, ie_type, str, strlen(str) + 1);
}

int ast_event_append_ie_uint(struct ast_event **event, enum ast_event_ie_type ie_type,
	uint32_t data)
{
	data = htonl(data);
	return ast_event_append_ie_raw(event, ie_type, &data, sizeof(data));
}

int ast_event_append_ie_raw(struct ast_event **event, enum ast_event_ie_type ie_type,
	const void *data, size_t data_len)
{
	struct ast_event_ie *ie;
	unsigned int extra_len;
	uint16_t event_len;

	event_len = ntohs((*event)->event_len);
	extra_len = sizeof(*ie) + data_len;

	if (!(*event = ast_realloc(*event, event_len + extra_len)))
		return -1;

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
	enum ast_event_type ie_type;
	struct ast_event_ie_val *ie_val;
	AST_LIST_HEAD_NOLOCK_STATIC(ie_vals, ast_event_ie_val);

	/* Invalid type */
	if (type >= AST_EVENT_TOTAL) {
		ast_log(LOG_WARNING, "Someone tried to create an event of invalid "
			"type '%d'!\n", type);
		return NULL;
	}

	va_start(ap, type);
	for (ie_type = va_arg(ap, enum ast_event_type);
		ie_type != AST_EVENT_IE_END;
		ie_type = va_arg(ap, enum ast_event_type))
	{
		struct ast_event_ie_val *ie_val = alloca(sizeof(*ie_val));
		memset(ie_val, 0, sizeof(*ie_val));
		ie_val->ie_type = ie_type;
		ie_val->ie_pltype = va_arg(ap, enum ast_event_ie_pltype);
		if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_UINT)
			ie_val->payload.uint = va_arg(ap, uint32_t);
		else if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_STR)
			ie_val->payload.str = ast_strdupa(va_arg(ap, const char *));
		AST_LIST_INSERT_TAIL(&ie_vals, ie_val, entry);
	}
	va_end(ap);

	if (!(event = ast_calloc(1, sizeof(*event))))
		return NULL;

	event->type = htons(type);
	event->event_len = htons(sizeof(*event));

	AST_LIST_TRAVERSE(&ie_vals, ie_val, entry) {
		if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_STR)
			ast_event_append_ie_str(&event, ie_val->ie_type, ie_val->payload.str);
		else if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_UINT)
			ast_event_append_ie_uint(&event, ie_val->ie_type, ie_val->payload.uint);

		if (!event)
			break;
	}

	return event;
}

void ast_event_destroy(struct ast_event *event)
{
	ast_free(event);
}

static void ast_event_ref_destroy(struct ast_event_ref *event_ref)
{
	ast_event_destroy(event_ref->event);
	ast_free(event_ref);
}

static struct ast_event *ast_event_dup(const struct ast_event *event)
{
	struct ast_event *dup_event;
	uint16_t event_len;

	event_len = ntohs(event->event_len);

	if (!(dup_event = ast_calloc(1, event_len)))
		return NULL;
	
	memcpy(dup_event, event, event_len);

	return dup_event;
}

struct ast_event *ast_event_get_cached(enum ast_event_type type, ...)
{
	va_list ap;
	enum ast_event_ie_type ie_type;
	struct ast_event *dup_event = NULL;
	struct ast_event_ref *event_ref;
	struct cache_arg {
		AST_LIST_ENTRY(cache_arg) entry;
		enum ast_event_ie_type ie_type;
		enum ast_event_ie_pltype ie_pltype;
		union {
			uint32_t uint;
			const char *str;
		} payload;
	} *cache_arg;
	AST_LIST_HEAD_NOLOCK_STATIC(cache_args, cache_arg);

	if (type >= AST_EVENT_TOTAL) {
		ast_log(LOG_ERROR, "%u is an invalid type!\n", type);
		return NULL;
	}

	va_start(ap, type);
	for (ie_type = va_arg(ap, enum ast_event_type);
		ie_type != AST_EVENT_IE_END;
		ie_type = va_arg(ap, enum ast_event_type))
	{
		cache_arg = alloca(sizeof(*cache_arg));
		memset(cache_arg, 0, sizeof(*cache_arg));
		cache_arg->ie_type = ie_type;
		cache_arg->ie_pltype = va_arg(ap, enum ast_event_ie_pltype);
		if (cache_arg->ie_pltype == AST_EVENT_IE_PLTYPE_UINT)
			cache_arg->payload.uint = va_arg(ap, uint32_t);
		else if (cache_arg->ie_pltype == AST_EVENT_IE_PLTYPE_STR)
			cache_arg->payload.str = ast_strdupa(va_arg(ap, const char *));
		AST_LIST_INSERT_TAIL(&cache_args, cache_arg, entry);
	}
	va_end(ap);

	if (AST_LIST_EMPTY(&cache_args)) {
		ast_log(LOG_ERROR, "Events can not be retrieved from the cache without "
			"specifying at least one IE type!\n");
		return NULL;
	}

	AST_RWLIST_RDLOCK(&ast_event_cache[type]);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&ast_event_cache[type], event_ref, entry) {
		AST_LIST_TRAVERSE(&cache_args, cache_arg, entry) {
			if ( ! ( (cache_arg->ie_pltype == AST_EVENT_IE_PLTYPE_UINT &&
			   (cache_arg->payload.uint ==
			    ast_event_get_ie_uint(event_ref->event, cache_arg->ie_type))) ||

			   (cache_arg->ie_pltype == AST_EVENT_IE_PLTYPE_STR &&
			   (!strcmp(cache_arg->payload.str,
			     ast_event_get_ie_str(event_ref->event, cache_arg->ie_type)))) ||

			   (cache_arg->ie_pltype == AST_EVENT_IE_PLTYPE_EXISTS &&
			    ast_event_get_ie_raw(event_ref->event, cache_arg->ie_type)) ) ) 
			{
				break;	
			}
		}
		if (!cache_arg) {
			/* All parameters were matched on this cache entry, so return it */
			dup_event = ast_event_dup(event_ref->event);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END
	AST_RWLIST_UNLOCK(&ast_event_cache[type]);

	return dup_event;
}

/*! \brief Duplicate an event and add it to the cache
 * \note This assumes this index in to the cache is locked */
static int ast_event_dup_and_cache(const struct ast_event *event)
{
	struct ast_event *dup_event;
	struct ast_event_ref *event_ref;

	if (!(dup_event = ast_event_dup(event)))
		return -1;
	if (!(event_ref = ast_calloc(1, sizeof(*event_ref))))
		return -1;
	
	event_ref->event = dup_event;

	AST_LIST_INSERT_TAIL(&ast_event_cache[ntohs(event->type)], event_ref, entry);

	return 0;
}

int ast_event_queue_and_cache(struct ast_event *event, ...)
{
	va_list ap;
	enum ast_event_type ie_type;
	uint16_t host_event_type;
	struct ast_event_ref *event_ref;
	int res;
	struct cache_arg {
		AST_LIST_ENTRY(cache_arg) entry;
		enum ast_event_ie_type ie_type;
		enum ast_event_ie_pltype ie_pltype;
	} *cache_arg;
	AST_LIST_HEAD_NOLOCK_STATIC(cache_args, cache_arg);

	host_event_type = ntohs(event->type);

	/* Invalid type */
	if (host_event_type >= AST_EVENT_TOTAL) {
		ast_log(LOG_WARNING, "Someone tried to queue an event of invalid "
			"type '%d'!\n", host_event_type);
		return -1;
	}

	va_start(ap, event);
	for (ie_type = va_arg(ap, enum ast_event_type);
		ie_type != AST_EVENT_IE_END;
		ie_type = va_arg(ap, enum ast_event_type))
	{
		cache_arg = alloca(sizeof(*cache_arg));
		memset(cache_arg, 0, sizeof(*cache_arg));
		cache_arg->ie_type = ie_type;
		cache_arg->ie_pltype = va_arg(ap, enum ast_event_ie_pltype);
		AST_LIST_INSERT_TAIL(&cache_args, cache_arg, entry);
	}
	va_end(ap);

	if (AST_LIST_EMPTY(&cache_args)) {
		ast_log(LOG_ERROR, "Events can not be cached without specifying at "
			"least one IE type!\n");
		return ast_event_queue(event);
	}
 
	AST_RWLIST_WRLOCK(&ast_event_cache[host_event_type]);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&ast_event_cache[host_event_type], event_ref, entry) {
		AST_LIST_TRAVERSE(&cache_args, cache_arg, entry) {
			if ( ! ( (cache_arg->ie_pltype == AST_EVENT_IE_PLTYPE_UINT &&
			   (ast_event_get_ie_uint(event, cache_arg->ie_type) ==
			    ast_event_get_ie_uint(event_ref->event, cache_arg->ie_type))) ||

			   (cache_arg->ie_pltype == AST_EVENT_IE_PLTYPE_STR &&
			   (!strcmp(ast_event_get_ie_str(event, cache_arg->ie_type),
			     ast_event_get_ie_str(event_ref->event, cache_arg->ie_type)))) ||

			   (cache_arg->ie_pltype == AST_EVENT_IE_PLTYPE_EXISTS &&
			    ast_event_get_ie_raw(event_ref->event, cache_arg->ie_type)) ) )
			{
				break;	
			}
		}
		if (!cache_arg) {
			/* All parameters were matched on this cache entry, so remove it */
			AST_LIST_REMOVE_CURRENT(entry);
			ast_event_ref_destroy(event_ref);
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	res = ast_event_dup_and_cache(event);
	AST_RWLIST_UNLOCK(&ast_event_cache[host_event_type]);

	return (ast_event_queue(event) || res) ? -1 : 0;
}

int ast_event_queue(struct ast_event *event)
{
	struct ast_event_ref *event_ref;
	uint16_t host_event_type;

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

	if (!(event_ref = ast_calloc(1, sizeof(*event_ref))))
		return -1;

	event_ref->event = event;

	ast_mutex_lock(&event_thread.lock);
	AST_LIST_INSERT_TAIL(&event_thread.event_q, event_ref, entry);
	ast_cond_signal(&event_thread.cond);
	ast_mutex_unlock(&event_thread.lock);

	return 0;
}

static void *ast_event_dispatcher(void *unused)
{
	for (;;) {
		struct ast_event_ref *event_ref;
		struct ast_event_sub *sub;
		uint16_t host_event_type;

		ast_mutex_lock(&event_thread.lock);
		while (!(event_ref = AST_LIST_REMOVE_HEAD(&event_thread.event_q, entry)))
			ast_cond_wait(&event_thread.cond, &event_thread.lock);
		ast_mutex_unlock(&event_thread.lock);

		host_event_type = ntohs(event_ref->event->type);

		/* Subscribers to this specific event first */
		AST_RWLIST_RDLOCK(&ast_event_subs[host_event_type]);
		AST_RWLIST_TRAVERSE(&ast_event_subs[host_event_type], sub, entry) {
			struct ast_event_ie_val *ie_val;
			AST_LIST_TRAVERSE(&sub->ie_vals, ie_val, entry) {
				if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_EXISTS &&
					ast_event_get_ie_raw(event_ref->event, ie_val->ie_type)) {
					continue;
				} else if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_UINT &&
					ast_event_get_ie_uint(event_ref->event, ie_val->ie_type) 
					== ie_val->payload.uint) {
					continue;
				} else if (ie_val->ie_pltype == AST_EVENT_IE_PLTYPE_STR &&
					!strcmp(ast_event_get_ie_str(event_ref->event, ie_val->ie_type),
						ie_val->payload.str)) {
					continue;
				}
				break;
			}
			if (ie_val)
				continue;
			sub->cb(event_ref->event, sub->userdata);
		}
		AST_RWLIST_UNLOCK(&ast_event_subs[host_event_type]);

		/* Now to subscribers to all event types */
		AST_RWLIST_RDLOCK(&ast_event_subs[AST_EVENT_ALL]);
		AST_RWLIST_TRAVERSE(&ast_event_subs[AST_EVENT_ALL], sub, entry)
			sub->cb(event_ref->event, sub->userdata);
		AST_RWLIST_UNLOCK(&ast_event_subs[AST_EVENT_ALL]);

		ast_event_ref_destroy(event_ref);
	}

	return NULL;
}

void ast_event_init(void)
{
	int i;

	for (i = 0; i < AST_EVENT_TOTAL; i++)
		AST_RWLIST_HEAD_INIT(&ast_event_subs[i]);

	for (i = 0; i < AST_EVENT_TOTAL; i++)
		AST_RWLIST_HEAD_INIT(&ast_event_cache[i]);

	ast_cond_init(&event_thread.cond, NULL);

	for (i = 0; i < NUM_EVENT_THREADS; i++) {
		pthread_t dont_care;
		ast_pthread_create_background(&dont_care, NULL, ast_event_dispatcher, NULL);
	}
}
