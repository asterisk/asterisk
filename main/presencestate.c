/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011-2012, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief Presence state management
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/presencestate.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/event.h"

/*! \brief Device state strings for printing */
static const struct {
	const char *string;
	enum ast_presence_state state;

} state2string[] = {
	{ "not_set", AST_PRESENCE_NOT_SET},
	{ "unavailable", AST_PRESENCE_UNAVAILABLE },
	{ "available", AST_PRESENCE_AVAILABLE},
	{ "away", AST_PRESENCE_AWAY},
	{ "xa", AST_PRESENCE_XA},
	{ "chat", AST_PRESENCE_CHAT},
	{ "dnd", AST_PRESENCE_DND},
};

/*! \brief Flag for the queue */
static ast_cond_t change_pending;

struct state_change {
	AST_LIST_ENTRY(state_change) list;
	char provider[1];
};

/*! \brief  A presence state provider */
struct presence_state_provider {
	char label[40];
	ast_presence_state_prov_cb_type callback;
	AST_RWLIST_ENTRY(presence_state_provider) list;
};

/*! \brief A list of providers */
static AST_RWLIST_HEAD_STATIC(presence_state_providers, presence_state_provider);

/*! \brief The state change queue. State changes are queued
	for processing by a separate thread */
static AST_LIST_HEAD_STATIC(state_changes, state_change);

/*! \brief The presence state change notification thread */
static pthread_t change_thread = AST_PTHREADT_NULL;

const char *ast_presence_state2str(enum ast_presence_state state)
{
	int i;
	for (i = 0; i < ARRAY_LEN(state2string); i++) {
		if (state == state2string[i].state) {
			return state2string[i].string;
		}
	}
	return "";
}

enum ast_presence_state ast_presence_state_val(const char *val)
{
	int i;
	for (i = 0; i < ARRAY_LEN(state2string); i++) {
		if (!strcasecmp(val, state2string[i].string)) {
			return state2string[i].state;
		}
	}
	return AST_PRESENCE_INVALID;
}

static enum ast_presence_state presence_state_cached(const char *presence_provider, char **subtype, char **message)
{
	enum ast_presence_state res = AST_PRESENCE_INVALID;
	struct ast_event *event;
	const char *_subtype;
	const char *_message;

	event = ast_event_get_cached(AST_EVENT_PRESENCE_STATE,
		AST_EVENT_IE_PRESENCE_PROVIDER, AST_EVENT_IE_PLTYPE_STR, presence_provider,
		AST_EVENT_IE_END);

	if (!event) {
		return res;
	}

	res = ast_event_get_ie_uint(event, AST_EVENT_IE_PRESENCE_STATE);
	_subtype = ast_event_get_ie_str(event, AST_EVENT_IE_PRESENCE_SUBTYPE);
	_message = ast_event_get_ie_str(event, AST_EVENT_IE_PRESENCE_MESSAGE);

	*subtype = !ast_strlen_zero(_subtype) ? ast_strdup(_subtype) : NULL;
	*message = !ast_strlen_zero(_message) ? ast_strdup(_message) : NULL;
	ast_event_destroy(event);

	return res;
}

static enum ast_presence_state ast_presence_state_helper(const char *presence_provider, char **subtype, char **message, int check_cache)
{
	struct presence_state_provider *provider;
	char *address;
	char *label = ast_strdupa(presence_provider);
	int res = AST_PRESENCE_INVALID;

	if (check_cache) {
		res = presence_state_cached(presence_provider, subtype, message);
		if (res != AST_PRESENCE_INVALID) {
			return res;
		}
	}

	if ((address = strchr(label, ':'))) {
		*address = '\0';
		address++;
	} else {
		ast_log(LOG_WARNING, "No label found for presence state provider: %s\n", presence_provider);
		return res;
	}

	AST_RWLIST_RDLOCK(&presence_state_providers);
	AST_RWLIST_TRAVERSE(&presence_state_providers, provider, list) {
		ast_debug(5, "Checking provider %s with %s\n", provider->label, label);

		if (!strcasecmp(provider->label, label)) {
			res = provider->callback(address, subtype, message);
			break;
		}
	}
	AST_RWLIST_UNLOCK(&presence_state_providers);

	if (!provider) {
		ast_log(LOG_WARNING, "No provider found for label %s\n", label);
	}

	return res;
}

enum ast_presence_state ast_presence_state(const char *presence_provider, char **subtype, char **message)
{
	return ast_presence_state_helper(presence_provider, subtype, message, 1);
}

enum ast_presence_state ast_presence_state_nocache(const char *presence_provider, char **subtype, char **message)
{
	return ast_presence_state_helper(presence_provider, subtype, message, 0);
}

int ast_presence_state_prov_add(const char *label, ast_presence_state_prov_cb_type callback)
{
	struct presence_state_provider *provider;

	if (!callback || !(provider = ast_calloc(1, sizeof(*provider)))) {
		return -1;
	}

	provider->callback = callback;
	ast_copy_string(provider->label, label, sizeof(provider->label));

	AST_RWLIST_WRLOCK(&presence_state_providers);
	AST_RWLIST_INSERT_HEAD(&presence_state_providers, provider, list);
	AST_RWLIST_UNLOCK(&presence_state_providers);

	return 0;
}
int ast_presence_state_prov_del(const char *label)
{
	struct presence_state_provider *provider;
	int res = -1;

	AST_RWLIST_WRLOCK(&presence_state_providers);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&presence_state_providers, provider, list) {
		if (!strcasecmp(provider->label, label)) {
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_free(provider);
			res = 0;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&presence_state_providers);

	return res;
}

static void presence_state_event(const char *provider,
		enum ast_presence_state state,
		const char *subtype,
		const char *message)
{
	struct ast_event *event;

	if (!(event = ast_event_new(AST_EVENT_PRESENCE_STATE,
			AST_EVENT_IE_PRESENCE_PROVIDER, AST_EVENT_IE_PLTYPE_STR, provider,
			AST_EVENT_IE_PRESENCE_STATE, AST_EVENT_IE_PLTYPE_UINT, state,
			AST_EVENT_IE_PRESENCE_SUBTYPE, AST_EVENT_IE_PLTYPE_STR, S_OR(subtype, ""),
			AST_EVENT_IE_PRESENCE_MESSAGE, AST_EVENT_IE_PLTYPE_STR, S_OR(message, ""),
			AST_EVENT_IE_END))) {
		return;
	}

	ast_event_queue_and_cache(event);
}

static void do_presence_state_change(const char *provider)
{
	char *subtype = NULL;
	char *message = NULL;
	enum ast_presence_state state;

	state = ast_presence_state_helper(provider, &subtype, &message, 0);

	if (state == AST_PRESENCE_INVALID) {
		return;
	}

	presence_state_event(provider, state, subtype, message);
	ast_free(subtype);
	ast_free(message);
}

int ast_presence_state_changed_literal(enum ast_presence_state state,
		const char *subtype,
		const char *message,
		const char *presence_provider)
{
	struct state_change *change;

	if (state != AST_PRESENCE_NOT_SET) {
		presence_state_event(presence_provider, state, subtype, message);
	} else if ((change_thread == AST_PTHREADT_NULL) ||
		!(change = ast_calloc(1, sizeof(*change) + strlen(presence_provider)))) {
		do_presence_state_change(presence_provider);
	} else {
		strcpy(change->provider, presence_provider);
		AST_LIST_LOCK(&state_changes);
		AST_LIST_INSERT_TAIL(&state_changes, change, list);
		ast_cond_signal(&change_pending);
		AST_LIST_UNLOCK(&state_changes);
	}

	return 0;
}

int ast_presence_state_changed(enum ast_presence_state state,
		const char *subtype,
		const char *message,
		const char *fmt, ...)
{
	char buf[AST_MAX_EXTENSION];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return ast_presence_state_changed_literal(state, subtype, message, buf);
}

/*! \brief Go through the presence state change queue and update changes in the presence state thread */
static void *do_presence_changes(void *data)
{
	struct state_change *next, *current;

	for (;;) {
		/* This basically pops off any state change entries, resets the list back to NULL, unlocks, and processes each state change */
		AST_LIST_LOCK(&state_changes);
		if (AST_LIST_EMPTY(&state_changes))
			ast_cond_wait(&change_pending, &state_changes.lock);
		next = AST_LIST_FIRST(&state_changes);
		AST_LIST_HEAD_INIT_NOLOCK(&state_changes);
		AST_LIST_UNLOCK(&state_changes);

		/* Process each state change */
		while ((current = next)) {
			next = AST_LIST_NEXT(current, list);
			do_presence_state_change(current->provider);
			ast_free(current);
		}
	}

	return NULL;
}

int ast_presence_state_engine_init(void)
{
	ast_cond_init(&change_pending, NULL);
	if (ast_pthread_create_background(&change_thread, NULL, do_presence_changes, NULL) < 0) {
		ast_log(LOG_ERROR, "Unable to start presence state change thread.\n");
		return -1;
	}

	return 0;
}

