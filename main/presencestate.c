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

/*** DOCUMENTATION
	<managerEvent language="en_US" name="PresenceStateChange">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a presence state changes</synopsis>
			<syntax>
				<parameter name="Presentity">
					<para>The entity whose presence state has changed</para>
				</parameter>
				<parameter name="Status">
					<para>The new status of the presentity</para>
				</parameter>
				<parameter name="Subtype">
					<para>The new subtype of the presentity</para>
				</parameter>
				<parameter name="Message">
					<para>The new message of the presentity</para>
				</parameter>
			</syntax>
			<description>
				<para>This differs from the <literal>PresenceStatus</literal>
				event because this event is raised for all presence state changes,
				not only for changes that affect dialplan hints.</para>
			</description>
			<see-also>
				<ref type="managerEvent">PresenceStatus</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
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
#include "asterisk/test.h"

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

static struct ast_manager_event_blob *presence_state_to_ami(struct stasis_message *msg);

STASIS_MESSAGE_TYPE_DEFN(ast_presence_state_message_type,
	.to_ami = presence_state_to_ami,
);
struct stasis_topic *presence_state_topic_all;
struct stasis_cache *presence_state_cache;
struct stasis_caching_topic *presence_state_topic_cached;

/*! \brief  A presence state provider */
struct presence_state_provider {
	char label[40];
	ast_presence_state_prov_cb_type callback;
	AST_RWLIST_ENTRY(presence_state_provider) list;
};

/*! \brief A list of providers */
static AST_RWLIST_HEAD_STATIC(presence_state_providers, presence_state_provider);

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
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct ast_presence_state_message *presence_state;

	msg = stasis_cache_get(ast_presence_state_cache(), ast_presence_state_message_type(), presence_provider);

	if (!msg) {
		return res;
	}

	presence_state = stasis_message_data(msg);
	res = presence_state->state;

	*subtype = !ast_strlen_zero(presence_state->subtype) ? ast_strdup(presence_state->subtype) : NULL;
	*message = !ast_strlen_zero(presence_state->message) ? ast_strdup(presence_state->message) : NULL;

	return res;
}

static enum ast_presence_state ast_presence_state_helper(const char *presence_provider, char **subtype, char **message, int check_cache)
{
	char *labels = ast_strdupa(presence_provider);
	char *label;
	enum ast_presence_state state = AST_PRESENCE_INVALID;
	enum ast_presence_state state_order[] = {
		[AST_PRESENCE_INVALID]     = 0,
		[AST_PRESENCE_NOT_SET]     = 1,
		[AST_PRESENCE_AVAILABLE]   = 2,
		[AST_PRESENCE_UNAVAILABLE] = 3,
		[AST_PRESENCE_CHAT]        = 4,
		[AST_PRESENCE_AWAY]        = 5,
		[AST_PRESENCE_XA]          = 6,
		[AST_PRESENCE_DND]         = 7
	};

	while ((label = strsep(&labels, "&"))) {
		enum ast_presence_state next_state = AST_PRESENCE_INVALID;
		char *next_subtype = NULL;
		char *next_message = NULL;

		if (check_cache) {
			next_state = presence_state_cached(label, &next_subtype, &next_message);
		}

		if (next_state == AST_PRESENCE_INVALID) {
			struct presence_state_provider *provider;
			const struct ast_channel_tech *chan_tech;
			char *address;

			if ((address = strchr(label, '/'))) {
				*address++ = '\0';

				if ((chan_tech = ast_get_channel_tech(label)) && chan_tech->presencestate) {
					next_state = chan_tech->presencestate(address, &next_subtype, &next_message);
				}
			} else if ((address = strchr(label, ':'))) {
				*address++ = '\0';

				AST_RWLIST_RDLOCK(&presence_state_providers);
				AST_RWLIST_TRAVERSE(&presence_state_providers, provider, list) {
					ast_debug(5, "Checking provider %s with %s\n", provider->label, label);

					if (!strcasecmp(provider->label, label)) {
						next_state = provider->callback(address, &next_subtype, &next_message);
						break;
					}
				}
				AST_RWLIST_UNLOCK(&presence_state_providers);

				if (!provider) {
					ast_log(LOG_WARNING, "No provider found for label: %s\n", label);
				}
			} else {
				ast_log(LOG_WARNING, "No label found for presence state provider: %s\n", label);
			}
		}

		if (state_order[next_state] > state_order[state]) {
			state = next_state;

			ast_free(*subtype);
			ast_free(*message);

			*subtype = next_subtype;
			*message = next_message;
		}
	}

	return state;
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

static void presence_state_dtor(void *obj)
{
	struct ast_presence_state_message *presence_state = obj;
	ast_string_field_free_memory(presence_state);
}

static struct ast_presence_state_message *presence_state_alloc(const char *provider,
		enum ast_presence_state state,
		const char *subtype,
		const char *message)
{
	RAII_VAR(struct ast_presence_state_message *, presence_state, ao2_alloc(sizeof(*presence_state), presence_state_dtor), ao2_cleanup);

	if (!presence_state || ast_string_field_init(presence_state, 256)) {
		return NULL;
	}

	presence_state->state = state;
	ast_string_field_set(presence_state, provider, provider);
	ast_string_field_set(presence_state, subtype, S_OR(subtype, ""));
	ast_string_field_set(presence_state, message, S_OR(message, ""));

	ao2_ref(presence_state, +1);
	return presence_state;
}

static void presence_state_event(const char *provider,
		enum ast_presence_state state,
		const char *subtype,
		const char *message)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_presence_state_message *, presence_state, NULL, ao2_cleanup);

	if (!ast_presence_state_message_type()) {
		return;
	}

	presence_state = presence_state_alloc(provider, state, subtype, message);
	if (!presence_state) {
		return;
	}

	msg = stasis_message_create(ast_presence_state_message_type(), presence_state);
	if (!msg) {
		return;
	}

	stasis_publish(ast_presence_state_topic_all(), msg);
}

static void do_presence_state_change(const char *provider)
{
	char *subtype = NULL;
	char *message = NULL;
	enum ast_presence_state state;

	state = ast_presence_state_helper(provider, &subtype, &message, 0);

	if (state < 0) {
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
	if (state == AST_PRESENCE_NOT_SET) {
		do_presence_state_change(presence_provider);
	} else {
		presence_state_event(presence_provider, state, subtype, message);
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

struct stasis_topic *ast_presence_state_topic_all(void)
{
	return presence_state_topic_all;
}

struct stasis_cache *ast_presence_state_cache(void)
{
	return presence_state_cache;
}

struct stasis_topic *ast_presence_state_topic_cached(void)
{
	return stasis_caching_get_topic(presence_state_topic_cached);
}

static const char *presence_state_get_id(struct stasis_message *msg)
{
	struct ast_presence_state_message *presence_state = stasis_message_data(msg);

	if (stasis_message_type(msg) != ast_presence_state_message_type()) {
		return NULL;
	}

	return presence_state->provider;
}

#if defined(TEST_FRAMEWORK)

#define TEST_CATEGORY "/main/presence"

static int presence_test_alice_state = AST_PRESENCE_UNAVAILABLE;
static int presence_test_bob_state = AST_PRESENCE_UNAVAILABLE;

static int presence_test_presencestate(const char *label, char **subtype, char **message)
{
	if (!strcmp(label, "Alice")) {
		return presence_test_alice_state;
	} else if (!strcmp(label, "Bob")) {
		return presence_test_bob_state;
	} else {
		return AST_PRESENCE_UNAVAILABLE;
	}
}

static struct ast_channel_tech presence_test_tech = {
	.type = "PresenceTestChannel",
	.description = "Presence test technology",
	.presencestate = presence_test_presencestate,
};

AST_TEST_DEFINE(test_presence_chan)
{
	int res = AST_TEST_FAIL;
	char provider[80];
	enum ast_presence_state state;
	char *subtype = NULL, *message = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "channel_presence";
		info->category = TEST_CATEGORY;
		info->summary = "Channel presence state tests";
		info->description = "Creates test channel technology and then test the presence state callback";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_channel_register(&presence_test_tech)) {
		ast_log(LOG_WARNING, "Unable to register channel type '%s'\n", presence_test_tech.type);
		goto presence_test_cleanup;
	}

	/* Check Alice's state */
	snprintf(provider, sizeof(provider), "%s/Alice", presence_test_tech.type);

	presence_test_alice_state = AST_PRESENCE_AVAILABLE;
	state = ast_presence_state_nocache(provider, &subtype, &message);

	if (state != presence_test_alice_state) {
		ast_log(LOG_WARNING, "Presence state of '%s' returned '%s' instead of the expected value '%s'\n",
			provider, ast_presence_state2str(state), ast_presence_state2str(presence_test_alice_state));
		goto presence_test_cleanup;
	}

	/* Check Alice's and Bob's state, Alice's should win as DND > AVAILABLE */
	snprintf(provider, sizeof(provider), "%s/Alice&%s/Bob", presence_test_tech.type, presence_test_tech.type);

	presence_test_alice_state = AST_PRESENCE_DND;
	presence_test_bob_state = AST_PRESENCE_UNAVAILABLE;
	state = ast_presence_state_nocache(provider, &subtype, &message);

	if (state != presence_test_alice_state) {
		ast_log(LOG_WARNING, "Presence state of '%s' returned '%s' instead of the expected value '%s'\n",
			provider, ast_presence_state2str(state), ast_presence_state2str(presence_test_alice_state));
		goto presence_test_cleanup;
        }

	/* Check Alice's and Bob's state, Bob's should now win as AVAILABLE < UNAVAILABLE */
	presence_test_alice_state = AST_PRESENCE_AVAILABLE;
	state = ast_presence_state_nocache(provider, &subtype, &message);

	if (state != presence_test_bob_state) {
		ast_log(LOG_WARNING, "Presence state of '%s' returned '%s' instead of the expected value '%s'\n",
			provider, ast_presence_state2str(state), ast_presence_state2str(presence_test_bob_state));
		goto presence_test_cleanup;
	}

	res = AST_TEST_PASS;

presence_test_cleanup:
	ast_channel_unregister(&presence_test_tech);
	ast_free(subtype);
	ast_free(message);

	return res;
}
#endif

static void presence_state_engine_cleanup(void)
{
	ao2_cleanup(presence_state_topic_all);
	presence_state_topic_all = NULL;
	ao2_cleanup(presence_state_cache);
	presence_state_cache = NULL;
	presence_state_topic_cached = stasis_caching_unsubscribe_and_join(presence_state_topic_cached);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_presence_state_message_type);
	AST_TEST_UNREGISTER(test_presence_chan);
}

int ast_presence_state_engine_init(void)
{
	ast_register_cleanup(presence_state_engine_cleanup);

	if (STASIS_MESSAGE_TYPE_INIT(ast_presence_state_message_type) != 0) {
		return -1;
	}

	presence_state_topic_all = stasis_topic_create("ast_presence_state_topic_all");
	if (!presence_state_topic_all) {
		return -1;
	}

	presence_state_cache = stasis_cache_create(presence_state_get_id);
	if (!presence_state_cache) {
		return -1;
	}

	presence_state_topic_cached = stasis_caching_topic_create(presence_state_topic_all, presence_state_cache);
	if (!presence_state_topic_cached) {
		return -1;
	}

	AST_TEST_REGISTER(test_presence_chan);

	return 0;
}

static struct ast_manager_event_blob *presence_state_to_ami(struct stasis_message *msg)
{
	struct ast_presence_state_message *presence_state = stasis_message_data(msg);

	return ast_manager_event_blob_create(EVENT_FLAG_CALL, "PresenceStateChange",
		"Presentity: %s\r\n"
		"Status: %s\r\n"
		"Subtype: %s\r\n"
		"Message: %s\r\n",
		presence_state->provider,
		ast_presence_state2str(presence_state->state),
		presence_state->subtype,
		presence_state->message);
}
