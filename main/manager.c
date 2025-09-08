/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief The Asterisk Management Interface - AMI
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * OpenSSL http://www.openssl.org - for AMI/SSL
 *
 * At the moment this file contains a number of functions, namely:
 *
 * - data structures storing AMI state
 * - AMI-related API functions, used by internal asterisk components
 * - handlers for AMI-related CLI functions
 * - handlers for AMI functions (available through the AMI socket)
 * - the code for the main AMI listener thread and individual session threads
 * - the http handlers invoked for AMI-over-HTTP by the threads in main/http.c
 *
 * \ref amiconf
 */

/*! \li \ref manager.c uses the configuration file \ref manager.conf
 * \addtogroup configuration_file
 */

/*! \page manager.conf manager.conf
 * \verbinclude manager.conf.sample
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/paths.h"	/* use various ast_config_AST_* */
#include <ctype.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <regex.h>

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/manager.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/callerid.h"
#include "asterisk/core_local.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/mwi.h"
#include "asterisk/pbx.h"
#include "asterisk/md5.h"
#include "asterisk/acl.h"
#include "asterisk/utils.h"
#include "asterisk/tcptls.h"
#include "asterisk/http.h"
#include "asterisk/ast_version.h"
#include "asterisk/threadstorage.h"
#include "asterisk/linkedlists.h"
#include "asterisk/term.h"
#include "asterisk/astobj2.h"
#include "asterisk/features.h"
#include "asterisk/security_events.h"
#include "asterisk/aoc.h"
#include "asterisk/strings.h"
#include "asterisk/stringfields.h"
#include "asterisk/presencestate.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/test.h"
#include "asterisk/json.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_after.h"
#include "asterisk/features_config.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/format_cache.h"
#include "asterisk/translate.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/message.h"

/*! \addtogroup Group_AMI AMI functions
*/
/*! @{
 Doxygen group */

enum error_type {
	UNKNOWN_ACTION = 1,
	UNKNOWN_CATEGORY,
	UNSPECIFIED_CATEGORY,
	UNSPECIFIED_ARGUMENT,
	FAILURE_ALLOCATION,
	FAILURE_NEWCAT,
	FAILURE_DELCAT,
	FAILURE_EMPTYCAT,
	FAILURE_UPDATE,
	FAILURE_DELETE,
	FAILURE_APPEND,
	FAILURE_TEMPLATE
};

enum add_filter_result {
	FILTER_SUCCESS = 0,
	FILTER_ALLOC_FAILED,
	FILTER_COMPILE_FAIL,
	FILTER_FORMAT_ERROR,
};

/*!
 * Linked list of events.
 * Global events are appended to the list by append_event().
 * The usecount is the number of stored pointers to the element,
 * excluding the list pointers. So an element that is only in
 * the list has a usecount of 0, not 1.
 *
 * Clients have a pointer to the last event processed, and for each
 * of these clients we track the usecount of the elements.
 * If we have a pointer to an entry in the list, it is safe to navigate
 * it forward because elements will not be deleted, but only appended.
 * The worst that can happen is seeing the pointer still NULL.
 *
 * When the usecount of an element drops to 0, and the element is the
 * first in the list, we can remove it. Removal is done within the
 * main thread, which is woken up for the purpose.
 *
 * For simplicity of implementation, we make sure the list is never empty.
 */
struct eventqent {
	int usecount;		/*!< # of clients who still need the event */
	int category;
	unsigned int seq;	/*!< sequence number */
	struct timeval tv;  /*!< When event was allocated */
	int event_name_hash;
	AST_RWLIST_ENTRY(eventqent) eq_next;
	char eventdata[1];	/*!< really variable size, allocated by append_event() */
};

static AST_RWLIST_HEAD_STATIC(all_events, eventqent);

static int displayconnects = 1;
static int allowmultiplelogin = 1;
static int timestampevents;
static int httptimeout = 60;
static int broken_events_action = 0;
static int manager_enabled = 0;
static int subscribed = 0;
static int webmanager_enabled = 0;
static int manager_debug = 0;	/*!< enable some debugging code in the manager */
static int authtimeout;
static int authlimit;
static char *manager_channelvars;
static char *manager_disabledevents;

#define DEFAULT_REALM		"asterisk"
static char global_realm[MAXHOSTNAMELEN];	/*!< Default realm */

static int unauth_sessions = 0;
static struct stasis_subscription *acl_change_sub;

/*! \brief A \ref stasis_topic that all topics AMI cares about will be forwarded to */
static struct stasis_topic *manager_topic;

/*! \brief The \ref stasis_message_router for all \ref stasis messages */
static struct stasis_message_router *stasis_router;

/*! \brief The \ref stasis_subscription for forwarding the RTP topic to the AMI topic */
static struct stasis_forward *rtp_topic_forwarder;

/*! \brief The \ref stasis_subscription for forwarding the Security topic to the AMI topic */
static struct stasis_forward *security_topic_forwarder;

/*!
 * \brief Set to true (non-zero) to globally allow all dangerous AMI actions to run
 */
static int live_dangerously;

#ifdef TEST_FRAMEWORK
/*! \brief The \ref stasis_subscription for forwarding the Test topic to the AMI topic */
static struct stasis_forward *test_suite_forwarder;
#endif

#define MGR_SHOW_TERMINAL_WIDTH 80

#define MAX_VARS 128

/*! \brief Fake event class used to end sessions at shutdown */
#define EVENT_FLAG_SHUTDOWN -1

/*! \brief
 * Descriptor for a manager session, either on the AMI socket or over HTTP.
 *
 * \note
 * AMI session have managerid == 0; the entry is created upon a connect,
 * and destroyed with the socket.
 * HTTP sessions have managerid != 0, the value is used as a search key
 * to lookup sessions (using the mansession_id cookie, or nonce key from
 * Digest Authentication http header).
 */
#define MAX_BLACKLIST_CMD_LEN 2
static const struct {
	const char *words[AST_MAX_CMD_LEN];
} command_blacklist[] = {
	{{ "module", "load", NULL }},
	{{ "module", "unload", NULL }},
	{{ "restart", "gracefully", NULL }},
};

static void acl_change_stasis_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message);

static void acl_change_stasis_subscribe(void)
{
	if (!acl_change_sub) {
		acl_change_sub = stasis_subscribe(ast_security_topic(),
			acl_change_stasis_cb, NULL);
		stasis_subscription_accept_message_type(acl_change_sub, ast_named_acl_change_type());
		stasis_subscription_set_filter(acl_change_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);
	}
}

static void acl_change_stasis_unsubscribe(void)
{
	acl_change_sub = stasis_unsubscribe_and_join(acl_change_sub);
}

/* In order to understand what the heck is going on with the
 * mansession_session and mansession structs, we need to have a bit of a history
 * lesson.
 *
 * In the beginning, there was the mansession. The mansession contained data that was
 * intrinsic to a manager session, such as the time that it started, the name of the logged-in
 * user, etc. In addition to these parameters were the f and fd parameters. For typical manager
 * sessions, these were used to represent the TCP socket over which the AMI session was taking
 * place. It makes perfect sense for these fields to be a part of the session-specific data since
 * the session actually defines this information.
 *
 * Then came the HTTP AMI sessions. With these, the f and fd fields need to be opened and closed
 * for every single action that occurs. Thus the f and fd fields aren't really specific to the session
 * but rather to the action that is being executed. Because a single session may execute many commands
 * at once, some sort of safety needed to be added in order to be sure that we did not end up with fd
 * leaks from one action overwriting the f and fd fields used by a previous action before the previous action
 * has had a chance to properly close its handles.
 *
 * The initial idea to solve this was to use thread synchronization, but this prevented multiple actions
 * from being run at the same time in a single session. Some manager actions may block for a long time, thus
 * creating a large queue of actions to execute. In addition, this fix did not address the basic architectural
 * issue that for HTTP manager sessions, the f and fd variables are not really a part of the session, but are
 * part of the action instead.
 *
 * The new idea was to create a structure on the stack for each HTTP Manager action. This structure would
 * contain the action-specific information, such as which file to write to. In order to maintain expectations
 * of action handlers and not have to change the public API of the manager code, we would need to name this
 * new stacked structure 'mansession' and contain within it the old mansession struct that we used to use.
 * We renamed the old mansession struct 'mansession_session' to hopefully convey that what is in this structure
 * is session-specific data. The structure that it is wrapped in, called a 'mansession' really contains action-specific
 * data.
 */
struct mansession_session {
				/*! \todo XXX need to document which fields it is protecting */
	struct ast_sockaddr addr;	/*!< address we are connecting from */
	struct ast_iostream *stream;	/*!< AMI stream */
	int inuse;		/*!< number of HTTP sessions using this entry */
	int needdestroy;	/*!< Whether an HTTP session should be destroyed */
	pthread_t waiting_thread;	/*!< Sleeping thread using this descriptor */
	uint32_t managerid;	/*!< Unique manager identifier, 0 for AMI sessions */
	time_t sessionstart;    /*!< Session start time */
	struct timeval sessionstart_tv; /*!< Session start time */
	time_t sessiontimeout;	/*!< Session timeout if HTTP */
	char username[80];	/*!< Logged in username */
	char challenge[10];	/*!< Authentication challenge */
	int authenticated;	/*!< Authentication status */
	int readperm;		/*!< Authorization for reading */
	int writeperm;		/*!< Authorization for writing */
	char inbuf[1025];	/*!< Buffer -  we use the extra byte to add a '\\0' and simplify parsing */
	int inlen;		/*!< number of buffered bytes */
	struct ao2_container *includefilters;	/*!< Manager event filters - include list */
	struct ao2_container *excludefilters;	/*!< Manager event filters - exclude list */
	struct ast_variable *chanvars;  /*!< Channel variables to set for originate */
	int send_events;	/*!<  XXX what ? */
	struct eventqent *last_ev;	/*!< last event processed. */
	int writetimeout;	/*!< Timeout for ast_carefulwrite() */
	time_t authstart;
	int pending_event;         /*!< Pending events indicator in case when waiting_thread is NULL */
	time_t noncetime;	/*!< Timer for nonce value expiration */
	unsigned long oldnonce;	/*!< Stale nonce value */
	unsigned long nc;	/*!< incremental  nonce counter */
	unsigned int kicked:1;	/*!< Flag set if session is forcibly kicked */
	ast_mutex_t notify_lock; /*!< Lock for notifying this session of events */
	AST_LIST_HEAD_NOLOCK(mansession_datastores, ast_datastore) datastores; /*!< Data stores on the session */
	AST_LIST_ENTRY(mansession_session) list;
};

enum mansession_message_parsing {
	MESSAGE_OKAY,
	MESSAGE_LINE_TOO_LONG
};

/*! \brief In case you didn't read that giant block of text above the mansession_session struct, the
 * \ref mansession is named this solely to keep the API the same in Asterisk. This structure really
 * represents data that is different from Manager action to Manager action. The mansession_session pointer
 * contained within points to session-specific data.
 */
struct mansession {
	struct mansession_session *session;
	struct ast_iostream *stream;
	struct ast_tcptls_session_instance *tcptls_session;
	enum mansession_message_parsing parsing;
	unsigned int write_error:1;
	struct manager_custom_hook *hook;
	ast_mutex_t lock;
};

/*! Active manager connection sessions container. */
static AO2_GLOBAL_OBJ_STATIC(mgr_sessions);

/*! \brief user descriptor, as read from the config file.
 *
 * \note It is still missing some fields -- e.g. we can have multiple permit and deny
 * lines which are not supported here, and readperm/writeperm/writetimeout
 * are not stored.
 */
struct ast_manager_user {
	char username[80];
	char *secret;			/*!< Secret for logging in */
	int readperm;			/*!< Authorization for reading */
	int writeperm;			/*!< Authorization for writing */
	int writetimeout;		/*!< Per user Timeout for ast_carefulwrite() */
	int displayconnects;		/*!< XXX unused */
	int allowmultiplelogin; /*!< Per user option*/
	int keep;			/*!< mark entries created on a reload */
	struct ao2_container *includefilters; /*!< Manager event filters - include list */
	struct ao2_container *excludefilters; /*!< Manager event filters - exclude list */
	struct ast_acl_list *acl;       /*!< ACL setting */
	char *a1_hash;			/*!< precalculated A1 for Digest auth */
	struct ast_variable *chanvars;  /*!< Channel variables to set for originate */
	AST_RWLIST_ENTRY(ast_manager_user) list;
};

/*! \brief list of users found in the config file */
static AST_RWLIST_HEAD_STATIC(users, ast_manager_user);

/*! \brief list of actions registered */
static AST_RWLIST_HEAD_STATIC(actions, manager_action);

/*! \brief list of hooks registered */
static AST_RWLIST_HEAD_STATIC(manager_hooks, manager_custom_hook);

#ifdef AST_XML_DOCS
/*! \brief A container of event documentation nodes */
static AO2_GLOBAL_OBJ_STATIC(event_docs);
#endif

static int __attribute__((format(printf, 9, 0))) __manager_event_sessions(
	struct ao2_container *sessions,
	int category,
	const char *event,
	int chancount,
	struct ast_channel **chans,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	...);

enum event_filter_match_type {
	FILTER_MATCH_REGEX = 0,
	FILTER_MATCH_EXACT,
	FILTER_MATCH_STARTS_WITH,
	FILTER_MATCH_ENDS_WITH,
	FILTER_MATCH_CONTAINS,
	FILTER_MATCH_NONE,
};

static char *match_type_names[] = {
	[FILTER_MATCH_REGEX] = "regex",
	[FILTER_MATCH_EXACT] = "exact",
	[FILTER_MATCH_STARTS_WITH] = "starts_with",
	[FILTER_MATCH_ENDS_WITH] = "ends_with",
	[FILTER_MATCH_CONTAINS] = "contains",
	[FILTER_MATCH_NONE] = "none",
};

struct event_filter_entry {
	enum event_filter_match_type match_type;
	regex_t *regex_filter;
	char *string_filter;
	char *event_name;
	unsigned int event_name_hash;
	char *header_name;
	int is_excludefilter;
};

static enum add_filter_result manager_add_filter(const char *criteria,
	const char *filter_pattern, struct ao2_container *includefilters,
	struct ao2_container *excludefilters);

static int should_send_event(struct ao2_container *includefilters,
	struct ao2_container *excludefilters, struct eventqent *eqe);

/*!
 * @{ \brief Define AMI message types.
 */
STASIS_MESSAGE_TYPE_DEFN(ast_manager_get_generic_type);
/*! @} */

/*!
 * \internal
 * \brief Find a registered action object.
 *
 * \param name Name of AMI action to find.
 *
 * \return Reffed action found or NULL
 */
static struct manager_action *action_find(const char *name)
{
	struct manager_action *act;

	AST_RWLIST_RDLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, act, list) {
		if (!strcasecmp(name, act->action)) {
			ao2_t_ref(act, +1, "found action object");
			break;
		}
	}
	AST_RWLIST_UNLOCK(&actions);

	return act;
}

struct stasis_topic *ast_manager_get_topic(void)
{
	return manager_topic;
}

struct stasis_message_router *ast_manager_get_message_router(void)
{
	return stasis_router;
}

static void manager_json_value_str_append(struct ast_json *value, const char *key,
					  struct ast_str **res)
{
	switch (ast_json_typeof(value)) {
	case AST_JSON_STRING:
		ast_str_append(res, 0, "%s: %s\r\n", key, ast_json_string_get(value));
		break;
	case AST_JSON_INTEGER:
		ast_str_append(res, 0, "%s: %jd\r\n", key, ast_json_integer_get(value));
		break;
	case AST_JSON_TRUE:
		ast_str_append(res, 0, "%s: True\r\n", key);
		break;
	case AST_JSON_FALSE:
		ast_str_append(res, 0, "%s: False\r\n", key);
		break;
	default:
		ast_str_append(res, 0, "%s: \r\n", key);
		break;
	}
}

static void manager_json_to_ast_str(struct ast_json *obj, const char *key,
				    struct ast_str **res, key_exclusion_cb exclusion_cb);

static void manager_json_array_with_key(struct ast_json *obj, const char* key,
					size_t index, struct ast_str **res,
					key_exclusion_cb exclusion_cb)
{
	struct ast_str *key_str = ast_str_alloca(64);
	ast_str_set(&key_str, 0, "%s(%zu)", key, index);
	manager_json_to_ast_str(obj, ast_str_buffer(key_str),
				res, exclusion_cb);
}

static void manager_json_obj_with_key(struct ast_json *obj, const char* key,
				      const char *parent_key, struct ast_str **res,
				      key_exclusion_cb exclusion_cb)
{
	if (parent_key) {
		struct ast_str *key_str = ast_str_alloca(64);
		ast_str_set(&key_str, 0, "%s/%s", parent_key, key);
		manager_json_to_ast_str(obj, ast_str_buffer(key_str),
					res, exclusion_cb);
		return;
	}

	manager_json_to_ast_str(obj, key, res, exclusion_cb);
}

void manager_json_to_ast_str(struct ast_json *obj, const char *key,
			     struct ast_str **res, key_exclusion_cb exclusion_cb)
{
	struct ast_json_iter *i;

	/* If obj or res is not given, just return */
	if (!obj || !res) {
		return;
	}

	if (!*res && !(*res = ast_str_create(1024))) {
		return;
	}

	if (exclusion_cb && key && exclusion_cb(key)) {
		return;
	}

	if (ast_json_typeof(obj) != AST_JSON_OBJECT &&
	    ast_json_typeof(obj) != AST_JSON_ARRAY) {
		manager_json_value_str_append(obj, key, res);
		return;
	}

	if (ast_json_typeof(obj) == AST_JSON_ARRAY) {
		size_t j;
		for (j = 0; j < ast_json_array_size(obj); ++j) {
			manager_json_array_with_key(ast_json_array_get(obj, j),
						    key, j, res, exclusion_cb);
		}
		return;
	}

	for (i = ast_json_object_iter(obj); i;
	     i = ast_json_object_iter_next(obj, i)) {
		manager_json_obj_with_key(ast_json_object_iter_value(i),
					  ast_json_object_iter_key(i),
					  key, res, exclusion_cb);
	}
}

struct ast_str *ast_manager_str_from_json_object(struct ast_json *blob, key_exclusion_cb exclusion_cb)
{
	struct ast_str *res = ast_str_create(1024);

	if (!ast_json_is_null(blob)) {
	   manager_json_to_ast_str(blob, NULL, &res, exclusion_cb);
	}

	return res;
}

#define manager_event_sessions(sessions, category, event, contents , ...)	\
	__manager_event_sessions(sessions, category, event, 0, NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, contents , ## __VA_ARGS__)

#define any_manager_listeners(sessions)	\
	((sessions && ao2_container_count(sessions)) || !AST_RWLIST_EMPTY(&manager_hooks))

static void manager_default_msg_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	struct ao2_container *sessions;
	struct ast_manager_event_blob *ev;

	if (!stasis_message_can_be_ami(message)) {
		/* Not an AMI message; disregard */
		return;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!any_manager_listeners(sessions)) {
		/* Nobody is listening */
		ao2_cleanup(sessions);
		return;
	}

	ev = stasis_message_to_ami(message);
	if (!ev) {
		/* Conversion failure */
		ao2_cleanup(sessions);
		return;
	}

	manager_event_sessions(sessions, ev->event_flags, ev->manager_event,
		"%s", ev->extra_fields);
	ao2_ref(ev, -1);
	ao2_cleanup(sessions);
}

static void manager_generic_msg_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	struct ast_json_payload *payload;
	int class_type;
	const char *type;
	struct ast_json *event;
	struct ast_str *event_buffer;
	struct ao2_container *sessions;

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!any_manager_listeners(sessions)) {
		/* Nobody is listening */
		ao2_cleanup(sessions);
		return;
	}

	payload = stasis_message_data(message);
	class_type = ast_json_integer_get(ast_json_object_get(payload->json, "class_type"));
	type = ast_json_string_get(ast_json_object_get(payload->json, "type"));
	event = ast_json_object_get(payload->json, "event");

	event_buffer = ast_manager_str_from_json_object(event, NULL);
	if (!event_buffer) {
		ast_log(AST_LOG_WARNING, "Error while creating payload for event %s\n", type);
		ao2_cleanup(sessions);
		return;
	}

	manager_event_sessions(sessions, class_type, type,
		"%s", ast_str_buffer(event_buffer));
	ast_free(event_buffer);
	ao2_cleanup(sessions);
}

void ast_manager_publish_event(const char *type, int class_type, struct ast_json *obj)
{
	RAII_VAR(struct ast_json *, event_info, NULL, ast_json_unref);
	RAII_VAR(struct ast_json_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	if (!obj || !ast_manager_get_generic_type()) {
		return;
	}

	ast_json_ref(obj);
	event_info = ast_json_pack("{s: s, s: i, s: o}",
			"type", type,
			"class_type", class_type,
			"event", obj);
	if (!event_info) {
		return;
	}

	payload = ast_json_payload_create(event_info);
	if (!payload) {
		return;
	}
	message = stasis_message_create(ast_manager_get_generic_type(), payload);
	if (!message) {
		return;
	}
	stasis_publish(ast_manager_get_topic(), message);
}

/*! \brief Add a custom hook to be called when an event is fired */
void ast_manager_register_hook(struct manager_custom_hook *hook)
{
	AST_RWLIST_WRLOCK(&manager_hooks);
	AST_RWLIST_INSERT_TAIL(&manager_hooks, hook, list);
	AST_RWLIST_UNLOCK(&manager_hooks);
}

/*! \brief Delete a custom hook to be called when an event is fired */
void ast_manager_unregister_hook(struct manager_custom_hook *hook)
{
	AST_RWLIST_WRLOCK(&manager_hooks);
	AST_RWLIST_REMOVE(&manager_hooks, hook, list);
	AST_RWLIST_UNLOCK(&manager_hooks);
}

int ast_manager_check_enabled(void)
{
	return manager_enabled;
}

int ast_webmanager_check_enabled(void)
{
	return (webmanager_enabled && manager_enabled);
}

/*!
 * Grab a reference to the last event, update usecount as needed.
 * Can handle a NULL pointer.
 */
static struct eventqent *grab_last(void)
{
	struct eventqent *ret;

	AST_RWLIST_WRLOCK(&all_events);
	ret = AST_RWLIST_LAST(&all_events);
	/* the list is never empty now, but may become so when
	 * we optimize it in the future, so be prepared.
	 */
	if (ret) {
		ast_atomic_fetchadd_int(&ret->usecount, 1);
	}
	AST_RWLIST_UNLOCK(&all_events);
	return ret;
}

/*!
 * Purge unused events. Remove elements from the head
 * as long as their usecount is 0 and there is a next element.
 */
static void purge_events(void)
{
	struct eventqent *ev;
	struct timeval now = ast_tvnow();

	AST_RWLIST_WRLOCK(&all_events);
	while ( (ev = AST_RWLIST_FIRST(&all_events)) &&
	    ev->usecount == 0 && AST_RWLIST_NEXT(ev, eq_next)) {
		AST_RWLIST_REMOVE_HEAD(&all_events, eq_next);
		ast_free(ev);
	}

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&all_events, ev, eq_next) {
		/* Never release the last event */
		if (!AST_RWLIST_NEXT(ev, eq_next)) {
			break;
		}

		/* 2.5 times whatever the HTTP timeout is (maximum 2.5 hours) is the maximum time that we will definitely cache an event */
		if (ev->usecount == 0 && ast_tvdiff_sec(now, ev->tv) > (httptimeout > 3600 ? 3600 : httptimeout) * 2.5) {
			AST_RWLIST_REMOVE_CURRENT(eq_next);
			ast_free(ev);
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&all_events);
}

/*!
 * helper functions to convert back and forth between
 * string and numeric representation of set of flags
 */
static const struct permalias {
	int num;
	const char *label;
} perms[] = {
	{ EVENT_FLAG_SYSTEM, "system" },
	{ EVENT_FLAG_CALL, "call" },
	{ EVENT_FLAG_LOG, "log" },
	{ EVENT_FLAG_VERBOSE, "verbose" },
	{ EVENT_FLAG_COMMAND, "command" },
	{ EVENT_FLAG_AGENT, "agent" },
	{ EVENT_FLAG_USER, "user" },
	{ EVENT_FLAG_CONFIG, "config" },
	{ EVENT_FLAG_DTMF, "dtmf" },
	{ EVENT_FLAG_REPORTING, "reporting" },
	{ EVENT_FLAG_CDR, "cdr" },
	{ EVENT_FLAG_DIALPLAN, "dialplan" },
	{ EVENT_FLAG_ORIGINATE, "originate" },
	{ EVENT_FLAG_AGI, "agi" },
	{ EVENT_FLAG_CC, "cc" },
	{ EVENT_FLAG_AOC, "aoc" },
	{ EVENT_FLAG_TEST, "test" },
	{ EVENT_FLAG_SECURITY, "security" },
	{ EVENT_FLAG_MESSAGE, "message" },
	{ INT_MAX, "all" },
	{ 0, "none" },
};

/*! Maximum string length of the AMI authority permission string buildable from perms[]. */
#define MAX_AUTH_PERM_STRING	150

/*! \brief Checks to see if a string which can be used to evaluate functions should be rejected */
static int function_capable_string_allowed_with_auths(const char *evaluating, int writepermlist)
{
	if (!(writepermlist & EVENT_FLAG_SYSTEM)
		&& (
			strstr(evaluating, "SHELL") ||       /* NoOp(${SHELL(rm -rf /)})  */
			strstr(evaluating, "EVAL")           /* NoOp(${EVAL(${some_var_containing_SHELL})}) */
		)) {
		return 0;
	}
	return 1;
}

/*! \brief Convert authority code to a list of options for a user. This will only
 * display those authority codes that have an explicit match on authority */
static const char *user_authority_to_str(int authority, struct ast_str **res)
{
	int i;
	char *sep = "";

	ast_str_reset(*res);
	for (i = 0; i < ARRAY_LEN(perms) - 1; i++) {
		if ((authority & perms[i].num) == perms[i].num) {
			ast_str_append(res, 0, "%s%s", sep, perms[i].label);
			sep = ",";
		}
	}

	if (ast_str_strlen(*res) == 0) {
		/* replace empty string with something sensible */
		ast_str_append(res, 0, "<none>");
	}

	return ast_str_buffer(*res);
}


/*! \brief Convert authority code to a list of options. Note that the EVENT_FLAG_ALL
 * authority will always be returned. */
static const char *authority_to_str(int authority, struct ast_str **res)
{
	int i;
	char *sep = "";

	ast_str_reset(*res);
	if (authority != EVENT_FLAG_SHUTDOWN) {
		for (i = 0; i < ARRAY_LEN(perms) - 1; i++) {
			if (authority & perms[i].num) {
				ast_str_append(res, 0, "%s%s", sep, perms[i].label);
				sep = ",";
			}
		}
	}

	if (ast_str_strlen(*res) == 0) {
		/* replace empty string with something sensible */
		ast_str_append(res, 0, "<none>");
	}

	return ast_str_buffer(*res);
}

/*! Tells you if smallstr exists inside bigstr
   which is delim by delim and uses no buf or stringsep
   ast_instring("this|that|more","this",'|') == 1;

   feel free to move this to app.c -anthm */
static int ast_instring(const char *bigstr, const char *smallstr, const char delim)
{
	const char *val = bigstr, *next;

	do {
		if ((next = strchr(val, delim))) {
			if (!strncmp(val, smallstr, (next - val))) {
				return 1;
			} else {
				continue;
			}
		} else {
			return !strcmp(smallstr, val);
		}
	} while (*(val = (next + 1)));

	return 0;
}

static int get_perm(const char *instr)
{
	int x = 0, ret = 0;

	if (!instr) {
		return 0;
	}

	for (x = 0; x < ARRAY_LEN(perms); x++) {
		if (ast_instring(instr, perms[x].label, ',')) {
			ret |= perms[x].num;
		}
	}

	return ret;
}

/*!
 * A number returns itself, false returns 0, true returns all flags,
 * other strings return the flags that are set.
 */
static int strings_to_mask(const char *string)
{
	const char *p;

	if (ast_strlen_zero(string)) {
		return -1;
	}

	for (p = string; *p; p++) {
		if (*p < '0' || *p > '9') {
			break;
		}
	}
	if (!*p) { /* all digits */
		return atoi(string);
	}
	if (ast_false(string)) {
		return 0;
	}
	if (ast_true(string)) {	/* all permissions */
		int x, ret = 0;
		for (x = 0; x < ARRAY_LEN(perms); x++) {
			ret |= perms[x].num;
		}
		return ret;
	}
	return get_perm(string);
}

/*! \brief Unreference manager session object.
     If no more references, then go ahead and delete it */
static struct mansession_session *unref_mansession(struct mansession_session *s)
{
	int refcount = ao2_ref(s, -1);
	if (manager_debug) {
		ast_debug(1, "Mansession: %p refcount now %d\n", s, refcount - 1);
	}
	return NULL;
}

static void event_filter_destructor(void *obj)
{
	struct event_filter_entry *entry = obj;
	if (entry->regex_filter) {
		regfree(entry->regex_filter);
		ast_free(entry->regex_filter);
	}
	ast_free(entry->event_name);
	ast_free(entry->header_name);
	ast_free(entry->string_filter);
}

static void session_destructor(void *obj)
{
	struct mansession_session *session = obj;
	struct eventqent *eqe = session->last_ev;
	struct ast_datastore *datastore;

	/* Get rid of each of the data stores on the session */
	while ((datastore = AST_LIST_REMOVE_HEAD(&session->datastores, entry))) {
		/* Free the data store */
		ast_datastore_free(datastore);
	}

	if (eqe) {
		ast_atomic_fetchadd_int(&eqe->usecount, -1);
	}
	if (session->chanvars) {
		ast_variables_destroy(session->chanvars);
	}

	if (session->includefilters) {
		ao2_t_ref(session->includefilters, -1, "decrement ref for include container, should be last one");
	}

	if (session->excludefilters) {
		ao2_t_ref(session->excludefilters, -1, "decrement ref for exclude container, should be last one");
	}

	ast_mutex_destroy(&session->notify_lock);
}

/*! \brief Allocate manager session structure and add it to the list of sessions */
static struct mansession_session *build_mansession(const struct ast_sockaddr *addr)
{
	struct ao2_container *sessions;
	struct mansession_session *newsession;

	newsession = ao2_alloc(sizeof(*newsession), session_destructor);
	if (!newsession) {
		return NULL;
	}

	newsession->includefilters = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
	newsession->excludefilters = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
	if (!newsession->includefilters || !newsession->excludefilters) {
		ao2_ref(newsession, -1);
		return NULL;
	}

	newsession->waiting_thread = AST_PTHREADT_NULL;
	newsession->writetimeout = 100;
	newsession->send_events = -1;
	ast_sockaddr_copy(&newsession->addr, addr);

	ast_mutex_init(&newsession->notify_lock);

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (sessions) {
		ao2_link(sessions, newsession);
		ao2_ref(sessions, -1);
	}

	return newsession;
}

static int mansession_cmp_fn(void *obj, void *arg, int flags)
{
	struct mansession_session *s = obj;
	char *str = arg;
	return !strcasecmp(s->username, str) ? CMP_MATCH : 0;
}

static void session_destroy(struct mansession_session *s)
{
	struct ao2_container *sessions;

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (sessions) {
		ao2_unlink(sessions, s);
		ao2_ref(sessions, -1);
	}
	unref_mansession(s);
}


static int check_manager_session_inuse(const char *name)
{
	struct ao2_container *sessions;
	struct mansession_session *session;
	int inuse = 0;

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (sessions) {
		session = ao2_find(sessions, (char *) name, 0);
		ao2_ref(sessions, -1);
		if (session) {
			unref_mansession(session);
			inuse = 1;
		}
	}
	return inuse;
}


/*!
 * lookup an entry in the list of registered users.
 * must be called with the list lock held.
 */
static struct ast_manager_user *get_manager_by_name_locked(const char *name)
{
	struct ast_manager_user *user = NULL;

	AST_RWLIST_TRAVERSE(&users, user, list) {
		if (!strcasecmp(user->username, name)) {
			break;
		}
	}

	return user;
}

/*! \brief Get displayconnects config option.
 *  \param session manager session to get parameter from.
 *  \return displayconnects config option value.
 */
static int manager_displayconnects(struct mansession_session *session)
{
	struct ast_manager_user *user = NULL;
	int ret = 0;

	AST_RWLIST_RDLOCK(&users);
	if ((user = get_manager_by_name_locked(session->username))) {
		ret = user->displayconnects;
	}
	AST_RWLIST_UNLOCK(&users);

	return ret;
}

#ifdef AST_XML_DOCS
static void print_event_instance(struct ast_cli_args *a, struct ast_xml_doc_item *instance);
#endif

static char *handle_showmancmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct manager_action *cur;
	struct ast_str *authority;
	int num;
	int l;
	const char *auth_str;

	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show command";
		e->usage =
			"Usage: manager show command <actionname> [<actionname> [<actionname> [...]]]\n"
			"	Shows the detailed description for a specific Asterisk manager interface command.\n";
		return NULL;
	case CLI_GENERATE:
		l = strlen(a->word);
		AST_RWLIST_RDLOCK(&actions);
		AST_RWLIST_TRAVERSE(&actions, cur, list) {
			if (!strncasecmp(a->word, cur->action, l)) {
				if (ast_cli_completion_add(ast_strdup(cur->action))) {
					break;
				}
			}
		}
		AST_RWLIST_UNLOCK(&actions);
		return NULL;
	}
	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	authority = ast_str_alloca(MAX_AUTH_PERM_STRING);

	AST_RWLIST_RDLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		for (num = 3; num < a->argc; num++) {
			if (!strcasecmp(cur->action, a->argv[num])) {
				auth_str = authority_to_str(cur->authority, &authority);

#ifdef AST_XML_DOCS
				if (cur->docsrc == AST_XML_DOC) {
					char *synopsis = ast_xmldoc_printable(S_OR(cur->synopsis, "Not available"), 1);
					char *since = ast_xmldoc_printable(S_OR(cur->since, "Not available"), 1);
					char *description = ast_xmldoc_printable(S_OR(cur->description, "Not available"), 1);
					char *syntax = ast_xmldoc_printable(S_OR(cur->syntax, "Not available"), 1);
					char *arguments = ast_xmldoc_printable(S_OR(cur->arguments, "Not available"), 1);
					char *privilege = ast_xmldoc_printable(S_OR(auth_str, "Not available"), 1);
					char *seealso = ast_xmldoc_printable(S_OR(cur->seealso, "Not available"), 1);
					char *responses = ast_xmldoc_printable("None", 1);

					if (!synopsis || !since || !description || !syntax || !arguments
							|| !privilege || !seealso || !responses) {
						ast_free(synopsis);
						ast_free(since);
						ast_free(description);
						ast_free(syntax);
						ast_free(arguments);
						ast_free(privilege);
						ast_free(seealso);
						ast_free(responses);
						ast_cli(a->fd, "Allocation failure.\n");
						AST_RWLIST_UNLOCK(&actions);

						return CLI_FAILURE;
					}

					ast_cli(a->fd, "\n"
						"%s  -= Info about Manager Command '%s' =- %s\n\n"
						COLORIZE_FMT "\n"
						"%s\n\n"
						COLORIZE_FMT "\n"
						"%s\n\n"
						COLORIZE_FMT "\n"
						"%s\n\n"
						COLORIZE_FMT "\n"
						"%s\n\n"
						COLORIZE_FMT "\n"
						"%s\n\n"
						COLORIZE_FMT "\n"
						"%s\n\n"
						COLORIZE_FMT "\n"
						"%s\n\n"
						COLORIZE_FMT "\n",
						ast_term_color(COLOR_MAGENTA, 0), cur->action, ast_term_reset(),
						COLORIZE(COLOR_MAGENTA, 0, "[Synopsis]"), synopsis,
						COLORIZE(COLOR_MAGENTA, 0, "[Since]"), since,
						COLORIZE(COLOR_MAGENTA, 0, "[Description]"), description,
						COLORIZE(COLOR_MAGENTA, 0, "[Syntax]"), syntax,
						COLORIZE(COLOR_MAGENTA, 0, "[Arguments]"), arguments,
						COLORIZE(COLOR_MAGENTA, 0, "[Privilege]"), privilege,
						COLORIZE(COLOR_MAGENTA, 0, "[See Also]"), seealso,
						COLORIZE(COLOR_MAGENTA, 0, "[List Responses]")
						);

					if (!cur->list_responses) {
						ast_cli(a->fd, "%s\n\n", responses);
					} else {
						struct ast_xml_doc_item *temp;
						for (temp = cur->list_responses; temp; temp = AST_LIST_NEXT(temp, next)) {
							ast_cli(a->fd, "Event: %s\n", temp->name);
							print_event_instance(a, temp);
						}
					}
					ast_cli(a->fd,
						COLORIZE_FMT "\n",
						COLORIZE(COLOR_MAGENTA, 0, "[End List Responses]")
						);

					ast_cli(a->fd, "\n"
						COLORIZE_FMT "\n",
						COLORIZE(COLOR_MAGENTA, 0, "[Final Response]")
						);
					if (!cur->final_response) {
						ast_cli(a->fd, "%s\n\n", responses);
					} else {
						ast_cli(a->fd, "Event: %s\n", cur->final_response->name);
						print_event_instance(a, cur->final_response);
					}
					ast_cli(a->fd,
						COLORIZE_FMT "\n",
						COLORIZE(COLOR_MAGENTA, 0, "[End Final Response]")
						);

					ast_free(synopsis);
					ast_free(since);
					ast_free(description);
					ast_free(syntax);
					ast_free(arguments);
					ast_free(privilege);
					ast_free(seealso);
					ast_free(responses);
				} else
#endif
				{
					ast_cli(a->fd, "Action: %s\nSynopsis: %s\nPrivilege: %s\n%s\n",
						cur->action, cur->synopsis,
						auth_str,
						S_OR(cur->description, ""));
				}
			}
		}
	}
	AST_RWLIST_UNLOCK(&actions);

	return CLI_SUCCESS;
}

static char *handle_mandebug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager set debug [on|off]";
		e->usage = "Usage: manager set debug [on|off]\n	Show, enable, disable debugging of the manager code.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 3) {
		ast_cli(a->fd, "manager debug is %s\n", manager_debug? "on" : "off");
	} else if (a->argc == 4) {
		if (!strcasecmp(a->argv[3], "on")) {
			manager_debug = 1;
		} else if (!strcasecmp(a->argv[3], "off")) {
			manager_debug = 0;
		} else {
			return CLI_SHOWUSAGE;
		}
	}
	return CLI_SUCCESS;
}

static char *handle_showmanager(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_manager_user *user = NULL;
	int l;
	struct ast_str *rauthority = ast_str_alloca(MAX_AUTH_PERM_STRING);
	struct ast_str *wauthority = ast_str_alloca(MAX_AUTH_PERM_STRING);
	struct ast_variable *v;

	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show user";
		e->usage =
			" Usage: manager show user <user>\n"
			"        Display all information related to the manager user specified.\n";
		return NULL;
	case CLI_GENERATE:
		l = strlen(a->word);
		if (a->pos != 3) {
			return NULL;
		}
		AST_RWLIST_RDLOCK(&users);
		AST_RWLIST_TRAVERSE(&users, user, list) {
			if (!strncasecmp(a->word, user->username, l)) {
				if (ast_cli_completion_add(ast_strdup(user->username))) {
					break;
				}
			}
		}
		AST_RWLIST_UNLOCK(&users);
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&users);

	if (!(user = get_manager_by_name_locked(a->argv[3]))) {
		ast_cli(a->fd, "There is no manager called %s\n", a->argv[3]);
		AST_RWLIST_UNLOCK(&users);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "\n");
	ast_cli(a->fd,
		"          username: %s\n"
		"            secret: %s\n"
		"               ACL: %s\n"
		"         read perm: %s\n"
		"        write perm: %s\n"
		"   displayconnects: %s\n"
		"allowmultiplelogin: %s\n",
		S_OR(user->username, "(N/A)"),
		(user->secret ? "<Set>" : "(N/A)"),
		((user->acl && !ast_acl_list_is_empty(user->acl)) ? "yes" : "no"),
		user_authority_to_str(user->readperm, &rauthority),
		user_authority_to_str(user->writeperm, &wauthority),
		(user->displayconnects ? "yes" : "no"),
		(user->allowmultiplelogin ? "yes" : "no"));
	ast_cli(a->fd, "         Variables: \n");
		for (v = user->chanvars ; v ; v = v->next) {
			ast_cli(a->fd, "                 %s = %s\n", v->name, v->value);
		}
	if (!ast_acl_list_is_empty(user->acl)) {
		ast_acl_output(a->fd, user->acl, NULL);
	}

	AST_RWLIST_UNLOCK(&users);

	return CLI_SUCCESS;
}

static char *handle_showmanagers(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_manager_user *user = NULL;
	int count_amu = 0;
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show users";
		e->usage =
			"Usage: manager show users\n"
			"       Prints a listing of all managers that are currently configured on that\n"
			" system.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&users);

	/* If there are no users, print out something along those lines */
	if (AST_RWLIST_EMPTY(&users)) {
		ast_cli(a->fd, "There are no manager users.\n");
		AST_RWLIST_UNLOCK(&users);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "\nusername\n--------\n");

	AST_RWLIST_TRAVERSE(&users, user, list) {
		ast_cli(a->fd, "%s\n", user->username);
		count_amu++;
	}

	AST_RWLIST_UNLOCK(&users);

	ast_cli(a->fd,"-------------------\n"
		      "%d manager users configured.\n", count_amu);
	return CLI_SUCCESS;
}

/*! \brief  CLI command  manager list commands */
static char *handle_showmancmds(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct manager_action *cur;
	int name_len = 1;
	int space_remaining;
#define HSMC_FORMAT "  %-*.*s  %-.*s\n"
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show commands";
		e->usage =
			"Usage: manager show commands\n"
			"	Prints a listing of all the available Asterisk manager interface commands.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	AST_RWLIST_RDLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		int incoming_len = strlen(cur->action);
		if (incoming_len > name_len) {
			name_len = incoming_len;
		}
	}

	space_remaining = MGR_SHOW_TERMINAL_WIDTH - name_len - 4;
	if (space_remaining < 0) {
		space_remaining = 0;
	}

	ast_cli(a->fd, HSMC_FORMAT, name_len, name_len, "Action", space_remaining, "Synopsis");
	ast_cli(a->fd, HSMC_FORMAT, name_len, name_len, "------", space_remaining, "--------");

	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		ast_cli(a->fd, HSMC_FORMAT, name_len, name_len, cur->action, space_remaining, cur->synopsis);
	}
	AST_RWLIST_UNLOCK(&actions);

	return CLI_SUCCESS;
}

/*! \brief CLI command manager kick session */
static char *handle_kickmanconn(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *sessions;
	struct mansession_session *session;
	struct ao2_iterator i;
	int fd = -1;
	int found = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "manager kick session";
		e->usage =
			"Usage: manager kick session <file descriptor>\n"
			"	Kick an active Asterisk Manager Interface session\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	fd = atoi(a->argv[3]);
	if (fd <= 0) { /* STDOUT won't be a valid AMI fd either */
		ast_cli(a->fd, "Invalid AMI file descriptor: %s\n", a->argv[3]);
		return CLI_FAILURE;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (sessions) {
		i = ao2_iterator_init(sessions, 0);
		ao2_ref(sessions, -1);
		while ((session = ao2_iterator_next(&i))) {
			ao2_lock(session);
			if (session->stream) {
				if (ast_iostream_get_fd(session->stream) == fd) {
					if (session->kicked) {
						ast_cli(a->fd, "Manager session using file descriptor %d has already been kicked\n", fd);
						ao2_unlock(session);
						unref_mansession(session);
						break;
					}
					fd = ast_iostream_get_fd(session->stream);
					found = fd;
					ast_cli(a->fd, "Kicking manager session connected using file descriptor %d\n", fd);
					ast_mutex_lock(&session->notify_lock);
					session->kicked = 1;
					if (session->waiting_thread != AST_PTHREADT_NULL) {
						pthread_kill(session->waiting_thread, SIGURG);
					}
					ast_mutex_unlock(&session->notify_lock);
					ao2_unlock(session);
					unref_mansession(session);
					break;
				}
			}
			ao2_unlock(session);
			unref_mansession(session);
		}
		ao2_iterator_destroy(&i);
	}

	if (!found) {
		ast_cli(a->fd, "No manager session found using file descriptor %d\n", fd);
	}
	return CLI_SUCCESS;
}

/*! \brief CLI command manager list connected */
static char *handle_showmanconn(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *sessions;
	struct mansession_session *session;
	time_t now = time(NULL);
#define HSMCONN_FORMAT1 "  %-15.15s  %-55.55s  %-10.10s  %-10.10s  %-8.8s  %-8.8s  %-10.10s  %-10.10s\n"
#define HSMCONN_FORMAT2 "  %-15.15s  %-55.55s  %-10d  %-10d  %-8d  %-8d  %-10.10d  %-10.10d\n"
	int count = 0;
	struct ao2_iterator i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show connected";
		e->usage =
			"Usage: manager show connected\n"
			"	Prints a listing of the users that are currently connected to the\n"
			"Asterisk manager interface.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, HSMCONN_FORMAT1, "Username", "IP Address", "Start", "Elapsed", "FileDes", "HttpCnt", "ReadPerms", "WritePerms");

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (sessions) {
		i = ao2_iterator_init(sessions, 0);
		ao2_ref(sessions, -1);
		while ((session = ao2_iterator_next(&i))) {
			ao2_lock(session);
			ast_cli(a->fd, HSMCONN_FORMAT2, session->username,
				ast_sockaddr_stringify_addr(&session->addr),
				(int) (session->sessionstart),
				(int) (now - session->sessionstart),
				session->stream ? ast_iostream_get_fd(session->stream) : -1,
				session->inuse,
				session->readperm,
				session->writeperm);
			count++;
			ao2_unlock(session);
			unref_mansession(session);
		}
		ao2_iterator_destroy(&i);
	}
	ast_cli(a->fd, "%d users connected.\n", count);

	return CLI_SUCCESS;
}

/*! \brief CLI command manager list eventq */
/* Should change to "manager show connected" */
static char *handle_showmaneventq(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct eventqent *s;
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show eventq";
		e->usage =
			"Usage: manager show eventq\n"
			"	Prints a listing of all events pending in the Asterisk manger\n"
			"event queue.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	AST_RWLIST_RDLOCK(&all_events);
	AST_RWLIST_TRAVERSE(&all_events, s, eq_next) {
		ast_cli(a->fd, "Usecount: %d\n", s->usecount);
		ast_cli(a->fd, "Category: %d\n", s->category);
		ast_cli(a->fd, "Event:\n%s", s->eventdata);
	}
	AST_RWLIST_UNLOCK(&all_events);

	return CLI_SUCCESS;
}

static int reload_module(void);

/*! \brief CLI command manager reload */
static char *handle_manager_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager reload";
		e->usage =
			"Usage: manager reload\n"
			"       Reloads the manager configuration.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc > 2) {
		return CLI_SHOWUSAGE;
	}
	reload_module();
	return CLI_SUCCESS;
}

static struct eventqent *advance_event(struct eventqent *e)
{
	struct eventqent *next;

	AST_RWLIST_RDLOCK(&all_events);
	if ((next = AST_RWLIST_NEXT(e, eq_next))) {
		ast_atomic_fetchadd_int(&next->usecount, 1);
		ast_atomic_fetchadd_int(&e->usecount, -1);
	}
	AST_RWLIST_UNLOCK(&all_events);
	return next;
}

#define	GET_HEADER_FIRST_MATCH	0
#define	GET_HEADER_LAST_MATCH	1
#define	GET_HEADER_SKIP_EMPTY	2

/*!
 * \brief Return a matching header value.
 *
 * \details
 * Generic function to return either the first or the last
 * matching header from a list of variables, possibly skipping
 * empty strings.
 *
 * \note At the moment there is only one use of this function in
 * this file, so we make it static.
 *
 * \note Never returns NULL.
 */
static const char *__astman_get_header(const struct message *m, char *var, int mode)
{
	int x, l = strlen(var);
	const char *result = "";

	if (!m) {
		return result;
	}

	for (x = 0; x < m->hdrcount; x++) {
		const char *h = m->headers[x];
		if (!strncasecmp(var, h, l) && h[l] == ':') {
			const char *value = h + l + 1;
			value = ast_skip_blanks(value); /* ignore leading spaces in the value */
			/* found a potential candidate */
			if ((mode & GET_HEADER_SKIP_EMPTY) && ast_strlen_zero(value)) {
				continue;	/* not interesting */
			}
			if (mode & GET_HEADER_LAST_MATCH) {
				result = value;	/* record the last match so far */
			} else {
				return value;
			}
		}
	}

	return result;
}

/*!
 * \brief Return the first matching variable from an array.
 *
 * \note This is the legacy function and is implemented in
 * therms of __astman_get_header().
 *
 * \note Never returns NULL.
 */
const char *astman_get_header(const struct message *m, char *var)
{
	return __astman_get_header(m, var, GET_HEADER_FIRST_MATCH);
}

/*!
 * \brief Append additional headers into the message structure from params.
 *
 * \note You likely want to initialize m->hdrcount to 0 before calling this.
 */
static void astman_append_headers(struct message *m, const struct ast_variable *params)
{
	const struct ast_variable *v;

	for (v = params; v && m->hdrcount < ARRAY_LEN(m->headers); v = v->next) {
		if (ast_asprintf((char**)&m->headers[m->hdrcount], "%s: %s", v->name, v->value) > -1) {
			++m->hdrcount;
		}
	}
}

/*!
 * \brief Free headers inside message structure, but not the message structure itself.
 */
static void astman_free_headers(struct message *m)
{
	while (m->hdrcount) {
		--m->hdrcount;
		ast_free((void *) m->headers[m->hdrcount]);
		m->headers[m->hdrcount] = NULL;
	}
}

/*!
 * \internal
 * \brief Process one "Variable:" header value string.
 *
 * \param head Current list of AMI variables to get new values added.
 * \param hdr_val Header value string to process.
 *
 * \return New variable list head.
 */
static struct ast_variable *man_do_variable_value(struct ast_variable *head, const char *hdr_val)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vars)[64];
	);

	hdr_val = ast_skip_blanks(hdr_val); /* ignore leading spaces in the value */
	parse = ast_strdupa(hdr_val);

	/* Break the header value string into name=val pair items. */
	AST_STANDARD_APP_ARGS(args, parse);
	if (args.argc) {
		int y;

		/* Process each name=val pair item. */
		for (y = 0; y < args.argc; y++) {
			struct ast_variable *cur;
			char *var;
			char *val;

			if (!args.vars[y]) {
				continue;
			}
			var = val = args.vars[y];
			strsep(&val, "=");

			/* XXX We may wish to trim whitespace from the strings. */
			if (!val || ast_strlen_zero(var)) {
				continue;
			}

			/* Create new variable list node and prepend it to the list. */
			cur = ast_variable_new(var, val, "");
			if (cur) {
				cur->next = head;
				head = cur;
			}
		}
	}

	return head;
}

struct ast_variable *astman_get_variables(const struct message *m)
{
	return astman_get_variables_order(m, ORDER_REVERSE);
}

struct ast_variable *astman_get_variables_order(const struct message *m,
	enum variable_orders order)
{
	int varlen;
	int x;
	struct ast_variable *head = NULL;

	static const char var_hdr[] = "Variable:";

	/* Process all "Variable:" headers. */
	varlen = strlen(var_hdr);
	for (x = 0; x < m->hdrcount; x++) {
		if (strncasecmp(var_hdr, m->headers[x], varlen)) {
			continue;
		}
		head = man_do_variable_value(head, m->headers[x] + varlen);
	}

	if (order == ORDER_NATURAL) {
		head = ast_variables_reverse(head);
	}

	return head;
}

/*! \brief access for hooks to send action messages to ami */
int ast_hook_send_action(struct manager_custom_hook *hook, const char *msg)
{
	const char *action;
	int ret = 0;
	struct manager_action *act_found;
	struct mansession s = {.session = NULL, };
	struct message m = { 0 };
	char *dup_str;
	char *src;
	int x = 0;
	int curlen;

	if (hook == NULL) {
		return -1;
	}

	/* Create our own copy of the AMI action msg string. */
	src = dup_str = ast_strdup(msg);
	if (!dup_str) {
		return -1;
	}

	/* convert msg string to message struct */
	curlen = strlen(src);
	for (x = 0; x < curlen; x++) {
		int cr;	/* set if we have \r */
		if (src[x] == '\r' && x+1 < curlen && src[x+1] == '\n')
			cr = 2;	/* Found. Update length to include \r\n */
		else if (src[x] == '\n')
			cr = 1;	/* also accept \n only */
		else
			continue;
		/* don't keep empty lines */
		if (x && m.hdrcount < ARRAY_LEN(m.headers)) {
			/* ... but trim \r\n and terminate the header string */
			src[x] = '\0';
			m.headers[m.hdrcount++] = src;
		}
		x += cr;
		curlen -= x;		/* remaining size */
		src += x;		/* update pointer */
		x = -1;			/* reset loop */
	}

	action = astman_get_header(&m, "Action");

	do {
		if (!strcasecmp(action, "login")) {
			break;
		}

		act_found = action_find(action);
		if (!act_found) {
			break;
		}

		/*
		 * we have to simulate a session for this action request
		 * to be able to pass it down for processing
		 * This is necessary to meet the previous design of manager.c
		 */
		s.hook = hook;

		ret = -1;
		ao2_lock(act_found);
		if (act_found->registered && act_found->func) {
			struct ast_module *mod_ref = ast_module_running_ref(act_found->module);

			ao2_unlock(act_found);
			/* If the action is in a module it must be running. */
			if (!act_found->module || mod_ref) {
				ret = act_found->func(&s, &m);
				ast_module_unref(mod_ref);
			}
		} else {
			ao2_unlock(act_found);
		}
		ao2_t_ref(act_found, -1, "done with found action object");
	} while (0);

	ast_free(dup_str);
	return ret;
}

/*!
 * helper function to send a string to the socket.
 * Return -1 on error (e.g. buffer full).
 */
static int send_string(struct mansession *s, char *string)
{
	struct ast_iostream *stream;
	int len, res;

	/* It's a result from one of the hook's action invocation */
	if (s->hook) {
		/*
		 * to send responses, we're using the same function
		 * as for receiving events. We call the event "HookResponse"
		 */
		s->hook->helper(EVENT_FLAG_HOOKRESPONSE, "HookResponse", string);
		return 0;
	}

	stream = s->stream ? s->stream : s->session->stream;

	len = strlen(string);
	ast_iostream_set_timeout_inactivity(stream, s->session->writetimeout);
	res = ast_iostream_write(stream, string, len);
	ast_iostream_set_timeout_disable(stream);

	if (res < len) {
		s->write_error = 1;
	}

	return res;
}

/*!
 * \brief thread local buffer for astman_append
 *
 * \note This can not be defined within the astman_append() function
 *       because it declares a couple of functions that get used to
 *       initialize the thread local storage key.
 */
AST_THREADSTORAGE(astman_append_buf);

AST_THREADSTORAGE(userevent_buf);

/*! \brief initial allocated size for the astman_append_buf and astman_send_*_va */
#define ASTMAN_APPEND_BUF_INITSIZE   256

static void astman_flush(struct mansession *s, struct ast_str *buf)
{
	if (s->hook || (s->tcptls_session && s->tcptls_session->stream)) {
		send_string(s, ast_str_buffer(buf));
	} else {
		ast_verbose("No connection stream in astman_append, should not happen\n");
	}
}

/*!
 * utility functions for creating AMI replies
 */
void astman_append(struct mansession *s, const char *fmt, ...)
{
	int res;
	va_list ap;
	struct ast_str *buf;

	if (!(buf = ast_str_thread_get(&astman_append_buf, ASTMAN_APPEND_BUF_INITSIZE))) {
		return;
	}

	va_start(ap, fmt);
	res = ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);
	if (res == AST_DYNSTR_BUILD_FAILED) {
		return;
	}

	if (s->hook || (s->tcptls_session != NULL && s->tcptls_session->stream != NULL)) {
		send_string(s, ast_str_buffer(buf));
	} else {
		ast_verbose("No connection stream in astman_append, should not happen\n");
	}
}

/*! \note NOTE: XXX this comment is unclear and possibly wrong.
   Callers of astman_send_error(), astman_send_response() or astman_send_ack() must EITHER
   hold the session lock _or_ be running in an action callback (in which case s->session->busy will
   be non-zero). In either of these cases, there is no need to lock-protect the session's
   fd, since no other output will be sent (events will be queued), and no input will
   be read until either the current action finishes or get_input() obtains the session
   lock.
 */

/*! \todo XXX MSG_MOREDATA should go to a header file. */
#define MSG_MOREDATA	((char *)astman_send_response)

/*! \brief send a response with an optional message,
 * and terminate it with an empty line.
 * m is used only to grab the 'ActionID' field.
 *
 * Use the explicit constant MSG_MOREDATA to remove the empty line.
 * XXX MSG_MOREDATA should go to a header file.
 */
static void astman_send_response_full(struct mansession *s, const struct message *m, char *resp, char *msg, char *listflag)
{
	const char *id = astman_get_header(m, "ActionID");
	struct ast_str *buf;

	buf = ast_str_thread_get(&astman_append_buf, ASTMAN_APPEND_BUF_INITSIZE);
	if (!buf) {
		return;
	}

	ast_str_set(&buf, 0, "Response: %s\r\n", resp);

	if (!ast_strlen_zero(id)) {
		ast_str_append(&buf, 0, "ActionID: %s\r\n", id);
	}

	if (listflag) {
		/* Start, complete, cancelled */
		ast_str_append(&buf, 0, "EventList: %s\r\n", listflag);
	}

	if (msg != MSG_MOREDATA) {
		if (msg) {
			ast_str_append(&buf, 0, "Message: %s\r\n", msg);
		}
		ast_str_append(&buf, 0, "\r\n");
	}

	astman_flush(s, buf);
}

void astman_send_response(struct mansession *s, const struct message *m, char *resp, char *msg)
{
	astman_send_response_full(s, m, resp, msg, NULL);
}

void astman_send_error(struct mansession *s, const struct message *m, char *error)
{
	astman_send_response_full(s, m, "Error", error, NULL);
}

void astman_send_error_va(struct mansession *s, const struct message *m, const char *fmt, ...)
{
	int res;
	va_list ap;
	struct ast_str *buf;
	char *msg;

	if (!(buf = ast_str_thread_get(&astman_append_buf, ASTMAN_APPEND_BUF_INITSIZE))) {
		return;
	}

	va_start(ap, fmt);
	res = ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);
	if (res == AST_DYNSTR_BUILD_FAILED) {
		return;
	}

	/* astman_append will use the same underlying buffer, so copy the message out
	 * before sending the response */
	msg = ast_str_buffer(buf);
	if (msg) {
		msg = ast_strdupa(msg);
	}
	astman_send_response_full(s, m, "Error", msg, NULL);
}

void astman_send_ack(struct mansession *s, const struct message *m, char *msg)
{
	astman_send_response_full(s, m, "Success", msg, NULL);
}

static void astman_start_ack(struct mansession *s, const struct message *m)
{
	astman_send_response_full(s, m, "Success", MSG_MOREDATA, NULL);
}

void astman_send_listack(struct mansession *s, const struct message *m, char *msg, char *listflag)
{
	astman_send_response_full(s, m, "Success", msg, listflag);
}

static struct ast_str *astman_send_list_complete_start_common(struct mansession *s, const struct message *m, const char *event_name, int count)
{
	const char *id = astman_get_header(m, "ActionID");
	struct ast_str *buf;

	buf = ast_str_thread_get(&astman_append_buf, ASTMAN_APPEND_BUF_INITSIZE);
	if (!buf) {
		return NULL;
	}

	ast_str_set(&buf, 0, "Event: %s\r\n", event_name);
	if (!ast_strlen_zero(id)) {
		ast_str_append(&buf, 0, "ActionID: %s\r\n", id);
	}
	ast_str_append(&buf, 0,
		"EventList: Complete\r\n"
		"ListItems: %d\r\n",
		count);

	return buf;
}

static void astman_send_list_complete(struct mansession *s, const struct message *m, const char *event_name, int count)
{
	struct ast_str *buf = astman_send_list_complete_start_common(s, m, event_name, count);
	if (buf) {
		ast_str_append(&buf, 0, "\r\n");
		astman_flush(s, buf);
	}
}

void astman_send_list_complete_start(struct mansession *s, const struct message *m, const char *event_name, int count)
{
	struct ast_str *buf = astman_send_list_complete_start_common(s, m, event_name, count);
	if (buf) {
		astman_flush(s, buf);
	}
}

void astman_send_list_complete_end(struct mansession *s)
{
	astman_append(s, "\r\n");
}

/*! \brief Lock the 'mansession' structure. */
static void mansession_lock(struct mansession *s)
{
	ast_mutex_lock(&s->lock);
}

/*! \brief Unlock the 'mansession' structure. */
static void mansession_unlock(struct mansession *s)
{
	ast_mutex_unlock(&s->lock);
}

/*! \brief
   Rather than braindead on,off this now can also accept a specific int mask value
   or a ',' delim list of mask strings (the same as manager.conf) -anthm
*/
static int set_eventmask(struct mansession *s, const char *eventmask)
{
	int maskint = strings_to_mask(eventmask);

	ao2_lock(s->session);
	if (maskint >= 0) {
		s->session->send_events = maskint;
	}
	ao2_unlock(s->session);

	return maskint;
}

static enum ast_transport mansession_get_transport(const struct mansession *s)
{
	return s->tcptls_session->parent->tls_cfg ? AST_TRANSPORT_TLS :
			AST_TRANSPORT_TCP;
}

static void report_invalid_user(const struct mansession *s, const char *username)
{
	char session_id[32];
	struct ast_security_event_inval_acct_id inval_acct_id = {
		.common.event_type = AST_SECURITY_EVENT_INVAL_ACCT_ID,
		.common.version    = AST_SECURITY_EVENT_INVAL_ACCT_ID_VERSION,
		.common.service    = "AMI",
		.common.account_id = username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s);

	ast_security_event_report(AST_SEC_EVT(&inval_acct_id));
}

static void report_failed_acl(const struct mansession *s, const char *username)
{
	char session_id[32];
	struct ast_security_event_failed_acl failed_acl_event = {
		.common.event_type = AST_SECURITY_EVENT_FAILED_ACL,
		.common.version    = AST_SECURITY_EVENT_FAILED_ACL_VERSION,
		.common.service    = "AMI",
		.common.account_id = username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&failed_acl_event));
}

static void report_inval_password(const struct mansession *s, const char *username)
{
	char session_id[32];
	struct ast_security_event_inval_password inval_password = {
		.common.event_type = AST_SECURITY_EVENT_INVAL_PASSWORD,
		.common.version    = AST_SECURITY_EVENT_INVAL_PASSWORD_VERSION,
		.common.service    = "AMI",
		.common.account_id = username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&inval_password));
}

static void report_auth_success(const struct mansession *s)
{
	char session_id[32];
	struct ast_security_event_successful_auth successful_auth = {
		.common.event_type = AST_SECURITY_EVENT_SUCCESSFUL_AUTH,
		.common.version    = AST_SECURITY_EVENT_SUCCESSFUL_AUTH_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&successful_auth));
}

static void report_req_not_allowed(const struct mansession *s, const char *action)
{
	char session_id[32];
	char request_type[64];
	struct ast_security_event_req_not_allowed req_not_allowed = {
		.common.event_type = AST_SECURITY_EVENT_REQ_NOT_ALLOWED,
		.common.version    = AST_SECURITY_EVENT_REQ_NOT_ALLOWED_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,

		.request_type      = request_type,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);
	snprintf(request_type, sizeof(request_type), "Action: %s", action);

	ast_security_event_report(AST_SEC_EVT(&req_not_allowed));
}

static void report_req_bad_format(const struct mansession *s, const char *action)
{
	char session_id[32];
	char request_type[64];
	struct ast_security_event_req_bad_format req_bad_format = {
		.common.event_type = AST_SECURITY_EVENT_REQ_BAD_FORMAT,
		.common.version    = AST_SECURITY_EVENT_REQ_BAD_FORMAT_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,

		.request_type      = request_type,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);
	snprintf(request_type, sizeof(request_type), "Action: %s", action);

	ast_security_event_report(AST_SEC_EVT(&req_bad_format));
}

static void report_failed_challenge_response(const struct mansession *s,
		const char *response, const char *expected_response)
{
	char session_id[32];
	struct ast_security_event_chal_resp_failed chal_resp_failed = {
		.common.event_type = AST_SECURITY_EVENT_CHAL_RESP_FAILED,
		.common.version    = AST_SECURITY_EVENT_CHAL_RESP_FAILED_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,

		.challenge         = s->session->challenge,
		.response          = response,
		.expected_response = expected_response,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&chal_resp_failed));
}

static void report_session_limit(const struct mansession *s)
{
	char session_id[32];
	struct ast_security_event_session_limit session_limit = {
		.common.event_type = AST_SECURITY_EVENT_SESSION_LIMIT,
		.common.version    = AST_SECURITY_EVENT_SESSION_LIMIT_VERSION,
		.common.service    = "AMI",
		.common.account_id = s->session->username,
		.common.session_tv = &s->session->sessionstart_tv,
		.common.local_addr = {
			.addr      = &s->tcptls_session->parent->local_address,
			.transport = mansession_get_transport(s),
		},
		.common.remote_addr = {
			.addr      = &s->session->addr,
			.transport = mansession_get_transport(s),
		},
		.common.session_id = session_id,
	};

	snprintf(session_id, sizeof(session_id), "%p", s->session);

	ast_security_event_report(AST_SEC_EVT(&session_limit));
}

/*
 * Here we start with action_ handlers for AMI actions,
 * and the internal functions used by them.
 * Generally, the handlers are called action_foo()
 */

/* helper function for action_login() */
static int authenticate(struct mansession *s, const struct message *m)
{
	const char *username = astman_get_header(m, "Username");
	const char *password = astman_get_header(m, "Secret");
	int error = -1;
	struct ast_manager_user *user = NULL;
	regex_t *regex_filter;
	struct ao2_iterator filter_iter;

	if (ast_strlen_zero(username)) {	/* missing username */
		return -1;
	}

	/* locate user in locked state */
	AST_RWLIST_WRLOCK(&users);

	if (!(user = get_manager_by_name_locked(username))) {
		report_invalid_user(s, username);
		ast_log(LOG_NOTICE, "%s tried to authenticate with nonexistent user '%s'\n", ast_sockaddr_stringify_addr(&s->session->addr), username);
	} else if (user->acl && (ast_apply_acl(user->acl, &s->session->addr, "Manager User ACL: ") == AST_SENSE_DENY)) {
		report_failed_acl(s, username);
		ast_log(LOG_NOTICE, "%s failed to pass IP ACL as '%s'\n", ast_sockaddr_stringify_addr(&s->session->addr), username);
	} else if (!strcasecmp(astman_get_header(m, "AuthType"), "MD5")) {
		const char *key = astman_get_header(m, "Key");
		if (!ast_strlen_zero(key) && !ast_strlen_zero(s->session->challenge) && user->secret) {
			int x;
			int len = 0;
			char md5key[256] = "";
			struct MD5Context md5;
			unsigned char digest[16];

			MD5Init(&md5);
			MD5Update(&md5, (unsigned char *) s->session->challenge, strlen(s->session->challenge));
			MD5Update(&md5, (unsigned char *) user->secret, strlen(user->secret));
			MD5Final(digest, &md5);
			for (x = 0; x < 16; x++)
				len += sprintf(md5key + len, "%02hhx", digest[x]);
			if (!strcmp(md5key, key)) {
				error = 0;
			} else {
				report_failed_challenge_response(s, key, md5key);
			}
		} else {
			ast_debug(1, "MD5 authentication is not possible.  challenge: '%s'\n",
				S_OR(s->session->challenge, ""));
		}
	} else if (user->secret) {
		if (!strcmp(password, user->secret)) {
			error = 0;
		} else {
			report_inval_password(s, username);
		}
	}

	if (error) {
		ast_log(LOG_NOTICE, "%s failed to authenticate as '%s'\n", ast_sockaddr_stringify_addr(&s->session->addr), username);
		AST_RWLIST_UNLOCK(&users);
		return -1;
	}

	/* auth complete */

	/* All of the user parameters are copied to the session so that in the event
	* of a reload and a configuration change, the session parameters are not
	* changed. */
	ast_copy_string(s->session->username, username, sizeof(s->session->username));
	s->session->readperm = user->readperm;
	s->session->writeperm = user->writeperm;
	s->session->writetimeout = user->writetimeout;
	if (user->chanvars) {
		s->session->chanvars = ast_variables_dup(user->chanvars);
	}

	filter_iter = ao2_iterator_init(user->includefilters, 0);
	while ((regex_filter = ao2_iterator_next(&filter_iter))) {
		ao2_t_link(s->session->includefilters, regex_filter, "add include user filter to session");
		ao2_t_ref(regex_filter, -1, "remove iterator ref");
	}
	ao2_iterator_destroy(&filter_iter);

	filter_iter = ao2_iterator_init(user->excludefilters, 0);
	while ((regex_filter = ao2_iterator_next(&filter_iter))) {
		ao2_t_link(s->session->excludefilters, regex_filter, "add exclude user filter to session");
		ao2_t_ref(regex_filter, -1, "remove iterator ref");
	}
	ao2_iterator_destroy(&filter_iter);

	s->session->sessionstart = time(NULL);
	s->session->sessionstart_tv = ast_tvnow();
	set_eventmask(s, astman_get_header(m, "Events"));

	report_auth_success(s);

	AST_RWLIST_UNLOCK(&users);
	return 0;
}

static int action_ping(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	struct timeval now = ast_tvnow();

	astman_append(s, "Response: Success\r\n");
	if (!ast_strlen_zero(actionid)){
		astman_append(s, "ActionID: %s\r\n", actionid);
	}
	astman_append(
		s,
		"Ping: Pong\r\n"
		"Timestamp: %ld.%06lu\r\n"
		"\r\n",
		(long) now.tv_sec, (unsigned long) now.tv_usec);
	return 0;
}

void astman_live_dangerously(int new_live_dangerously)
{
	if (new_live_dangerously && !live_dangerously)
	{
		ast_log(LOG_WARNING, "Manager Configuration load protection disabled.\n");
	}

	if (!new_live_dangerously && live_dangerously)
	{
		ast_log(LOG_NOTICE, "Manager Configuration load protection enabled.\n");
	}
	live_dangerously = new_live_dangerously;
}

/**
 * \brief Check if a file is restricted or not
 *
 * \return 0 on success
 * \return 1 on restricted file
 * \return -1 on failure
 */
static int is_restricted_file(const char *filename)
{
	char *stripped_filename;
	RAII_VAR(char *, path, NULL, ast_free);
	RAII_VAR(char *, real_path, NULL, ast_std_free);

	if (live_dangerously) {
		return 0;
	}

	stripped_filename = ast_strip(ast_strdupa(filename));

	/* If the file path starts with '/', don't prepend ast_config_AST_CONFIG_DIR */
	if (stripped_filename[0] == '/') {
		real_path = realpath(stripped_filename, NULL);
	} else {
		if (ast_asprintf(&path, "%s/%s", ast_config_AST_CONFIG_DIR, stripped_filename) == -1) {
			return -1;
		}
		real_path = realpath(path, NULL);
	}

	if (!real_path) {
		return -1;
	}

	if (!ast_begins_with(real_path, ast_config_AST_CONFIG_DIR)) {
		return 1;
	}

	return 0;
}

static int action_getconfig(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *fn = astman_get_header(m, "Filename");
	const char *category = astman_get_header(m, "Category");
	const char *filter = astman_get_header(m, "Filter");
	const char *category_name;
	int catcount = 0;
	int lineno = 0;
	int ret = 0;
	struct ast_category *cur_category = NULL;
	struct ast_variable *v;
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}

	ret = is_restricted_file(fn);
	if (ret == 1) {
		astman_send_error(s, m, "File requires escalated privileges");
		return 0;
	} else if (ret == -1) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	}

	cfg = ast_config_load2(fn, "manager", config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		astman_send_error(s, m, "Config file has invalid format");
		return 0;
	}

	astman_start_ack(s, m);
	while ((cur_category = ast_category_browse_filtered(cfg, category, cur_category, filter))) {
		struct ast_str *templates;

		category_name = ast_category_get_name(cur_category);
		lineno = 0;
		astman_append(s, "Category-%06d: %s\r\n", catcount, category_name);

		if (ast_category_is_template(cur_category)) {
			astman_append(s, "IsTemplate-%06d: %d\r\n", catcount, 1);
		}

		if ((templates = ast_category_get_templates(cur_category))
			&& ast_str_strlen(templates) > 0) {
			astman_append(s, "Templates-%06d: %s\r\n", catcount, ast_str_buffer(templates));
			ast_free(templates);
		}

		for (v = ast_category_first(cur_category); v; v = v->next) {
			astman_append(s, "Line-%06d-%06d: %s=%s\r\n", catcount, lineno++, v->name, v->value);
		}

		catcount++;
	}

	if (!ast_strlen_zero(category) && catcount == 0) { /* TODO: actually, a config with no categories doesn't even get loaded */
		astman_append(s, "No categories found\r\n");
	}

	ast_config_destroy(cfg);
	astman_append(s, "\r\n");

	return 0;
}

static int action_listcategories(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *fn = astman_get_header(m, "Filename");
	const char *match = astman_get_header(m, "Match");
	struct ast_category *category = NULL;
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };
	int catcount = 0;
	int ret = 0;

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}

	ret = is_restricted_file(fn);
	if (ret == 1) {
		astman_send_error(s, m, "File requires escalated privileges");
		return 0;
	} else if (ret == -1) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	}

	if (!(cfg = ast_config_load2(fn, "manager", config_flags))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		astman_send_error(s, m, "Config file has invalid format");
		return 0;
	}

	astman_start_ack(s, m);
	while ((category = ast_category_browse_filtered(cfg, NULL, category, match))) {
		astman_append(s, "Category-%06d: %s\r\n", catcount, ast_category_get_name(category));
		catcount++;
	}

	if (catcount == 0) { /* TODO: actually, a config with no categories doesn't even get loaded */
		astman_append(s, "Error: no categories found\r\n");
	}

	ast_config_destroy(cfg);
	astman_append(s, "\r\n");

	return 0;
}

/*! The amount of space in out must be at least ( 2 * strlen(in) + 1 ) */
static void json_escape(char *out, const char *in)
{
	for (; *in; in++) {
		if (*in == '\\' || *in == '\"') {
			*out++ = '\\';
		}
		*out++ = *in;
	}
	*out = '\0';
}

/*!
 * \internal
 * \brief Append a JSON escaped string to the manager stream.
 *
 * \param s AMI stream to append a string.
 * \param str String to append to the stream after JSON escaping it.
 */
static void astman_append_json(struct mansession *s, const char *str)
{
	char *buf;

	buf = ast_alloca(2 * strlen(str) + 1);
	json_escape(buf, str);
	astman_append(s, "%s", buf);
}

static int action_getconfigjson(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *fn = astman_get_header(m, "Filename");
	const char *filter = astman_get_header(m, "Filter");
	const char *category = astman_get_header(m, "Category");
	struct ast_category *cur_category = NULL;
	const char *category_name;
	struct ast_variable *v;
	int comma1 = 0;
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}

	if (is_restricted_file(fn)) {
		astman_send_error(s, m, "File requires escalated privileges");
		return 0;
	}

	if (!(cfg = ast_config_load2(fn, "manager", config_flags))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		astman_send_error(s, m, "Config file has invalid format");
		return 0;
	}

	astman_start_ack(s, m);
	astman_append(s, "JSON: {");
	while ((cur_category = ast_category_browse_filtered(cfg, category, cur_category, filter))) {
		int comma2 = 0;
		struct ast_str *templates;

		category_name = ast_category_get_name(cur_category);
		astman_append(s, "%s\"", comma1 ? "," : "");
		astman_append_json(s, category_name);
		astman_append(s, "\":{");
		comma1 = 1;

		if (ast_category_is_template(cur_category)) {
			astman_append(s, "\"istemplate\":1");
			comma2 = 1;
		}

		if ((templates = ast_category_get_templates(cur_category))
			&& ast_str_strlen(templates) > 0) {
			astman_append(s, "%s", comma2 ? "," : "");
			astman_append(s, "\"templates\":\"%s\"", ast_str_buffer(templates));
			ast_free(templates);
			comma2 = 1;
		}

		for (v = ast_category_first(cur_category); v; v = v->next) {
			astman_append(s, "%s\"", comma2 ? "," : "");
			astman_append_json(s, v->name);
			astman_append(s, "\":\"");
			astman_append_json(s, v->value);
			astman_append(s, "\"");
			comma2 = 1;
		}

		astman_append(s, "}");
	}
	astman_append(s, "}\r\n\r\n");

	ast_config_destroy(cfg);

	return 0;
}

/*! \brief helper function for action_updateconfig */
static enum error_type handle_updates(struct mansession *s, const struct message *m, struct ast_config *cfg, const char *dfn)
{
	int x;
	char hdr[40];
	const char *action, *cat, *var, *value, *match, *line, *options;
	struct ast_variable *v;
	struct ast_str *str1 = ast_str_create(16), *str2 = ast_str_create(16);
	enum error_type result = 0;

	for (x = 0; x < 100000; x++) {	/* 100000 = the max number of allowed updates + 1 */
		unsigned int object = 0;
		char *dupoptions;
		int allowdups = 0;
		int istemplate = 0;
		int ignoreerror = 0;
		RAII_VAR(char *, inherit, NULL, ast_free);
		RAII_VAR(char *, catfilter, NULL, ast_free);
		char *token;
		int foundvar = 0;
		int foundcat = 0;
		struct ast_category *category = NULL;

		snprintf(hdr, sizeof(hdr), "Action-%06d", x);
		action = astman_get_header(m, hdr);
		if (ast_strlen_zero(action))		/* breaks the for loop if no action header */
			break;							/* this could cause problems if actions come in misnumbered */

		snprintf(hdr, sizeof(hdr), "Cat-%06d", x);
		cat = astman_get_header(m, hdr);
		if (ast_strlen_zero(cat)) {		/* every action needs a category */
			result =  UNSPECIFIED_CATEGORY;
			break;
		}

		snprintf(hdr, sizeof(hdr), "Var-%06d", x);
		var = astman_get_header(m, hdr);

		snprintf(hdr, sizeof(hdr), "Value-%06d", x);
		value = astman_get_header(m, hdr);

		if (!ast_strlen_zero(value) && *value == '>') {
			object = 1;
			value++;
		}

		snprintf(hdr, sizeof(hdr), "Match-%06d", x);
		match = astman_get_header(m, hdr);

		snprintf(hdr, sizeof(hdr), "Line-%06d", x);
		line = astman_get_header(m, hdr);

		snprintf(hdr, sizeof(hdr), "Options-%06d", x);
		options = astman_get_header(m, hdr);
		if (!ast_strlen_zero(options)) {
			char copy[strlen(options) + 1];
			strcpy(copy, options); /* safe */
			dupoptions = copy;
			while ((token = ast_strsep(&dupoptions, ',', AST_STRSEP_STRIP))) {
				if (!strcasecmp("allowdups", token)) {
					allowdups = 1;
					continue;
				}
				if (!strcasecmp("template", token)) {
					istemplate = 1;
					continue;
				}
				if (!strcasecmp("ignoreerror", token)) {
					ignoreerror = 1;
					continue;
				}
				if (ast_begins_with(token, "inherit")) {
					char *c = ast_strsep(&token, '=', AST_STRSEP_STRIP);
					c = ast_strsep(&token, '=', AST_STRSEP_STRIP);
					if (c) {
						inherit = ast_strdup(c);
					}
					continue;
				}
				if (ast_begins_with(token, "catfilter")) {
					char *c = ast_strsep(&token, '=', AST_STRSEP_STRIP);
					c = ast_strsep(&token, '=', AST_STRSEP_STRIP);
					if (c) {
						catfilter = ast_strdup(c);
					}
					continue;
				}
			}
		}

		if (!strcasecmp(action, "newcat")) {
			struct ast_category *template;
			char *tmpl_name = NULL;

			if (!allowdups) {
				if (ast_category_get(cfg, cat, "TEMPLATES=include")) {
					if (ignoreerror) {
						continue;
					} else {
						result = FAILURE_NEWCAT;	/* already exist */
						break;
					}
				}
			}

			if (istemplate) {
				category = ast_category_new_template(cat, dfn, -1);
			} else {
				category = ast_category_new(cat, dfn, -1);
			}

			if (!category) {
				result = FAILURE_ALLOCATION;
				break;
			}

			if (inherit) {
				while ((tmpl_name = ast_strsep(&inherit, ',', AST_STRSEP_STRIP))) {
					if ((template = ast_category_get(cfg, tmpl_name, "TEMPLATES=restrict"))) {
						if (ast_category_inherit(category, template)) {
							result = FAILURE_ALLOCATION;
							break;
						}
					} else {
						ast_category_destroy(category);
						category = NULL;
						result = FAILURE_TEMPLATE;	/* template not found */
						break;
					}
				}
			}

			if (category != NULL) {
				if (ast_strlen_zero(match)) {
					ast_category_append(cfg, category);
				} else {
					if (ast_category_insert(cfg, category, match)) {
						ast_category_destroy(category);
						result = FAILURE_NEWCAT;
						break;
					}
				}
			}
		} else if (!strcasecmp(action, "renamecat")) {
			if (ast_strlen_zero(value)) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				ast_category_rename(category, value);
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		} else if (!strcasecmp(action, "delcat")) {
			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				category = ast_category_delete(cfg, category);
				foundcat = 1;
			}

			if (!foundcat && !ignoreerror) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		} else if (!strcasecmp(action, "emptycat")) {
			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				ast_category_empty(category);
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		} else if (!strcasecmp(action, "update")) {
			if (ast_strlen_zero(var)) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			foundvar = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				if (!ast_variable_update(category, var, value, match, object)) {
					foundvar = 1;
				}
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}

			if (!foundvar) {
				result = FAILURE_UPDATE;
				break;
			}
		} else if (!strcasecmp(action, "delete")) {
			if ((ast_strlen_zero(var) && ast_strlen_zero(line))) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			foundvar = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				if (!ast_variable_delete(category, var, match, line)) {
					foundvar = 1;
				}
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}

			if (!foundvar && !ignoreerror) {
				result = FAILURE_UPDATE;
				break;
			}
		} else if (!strcasecmp(action, "append")) {
			if (ast_strlen_zero(var)) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				if (!(v = ast_variable_new(var, value, dfn))) {
					result = FAILURE_ALLOCATION;
					break;
				}
				if (object || (match && !strcasecmp(match, "object"))) {
					v->object = 1;
				}
				ast_variable_append(category, v);
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		} else if (!strcasecmp(action, "insert")) {
			if (ast_strlen_zero(var) || ast_strlen_zero(line)) {
				result = UNSPECIFIED_ARGUMENT;
				break;
			}

			foundcat = 0;
			while ((category = ast_category_browse_filtered(cfg, cat, category, catfilter))) {
				if (!(v = ast_variable_new(var, value, dfn))) {
					result = FAILURE_ALLOCATION;
					break;
				}
				ast_variable_insert(category, v, line);
				foundcat = 1;
			}

			if (!foundcat) {
				result = UNKNOWN_CATEGORY;
				break;
			}
		}
		else {
			ast_log(LOG_WARNING, "Action-%06d: %s not handled\n", x, action);
			result = UNKNOWN_ACTION;
			break;
		}
	}
	ast_free(str1);
	ast_free(str2);
	return result;
}

static int action_updateconfig(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *sfn = astman_get_header(m, "SrcFilename");
	const char *dfn = astman_get_header(m, "DstFilename");
	int res;
	const char *rld = astman_get_header(m, "Reload");
	int preserve_effective_context = CONFIG_SAVE_FLAG_PRESERVE_EFFECTIVE_CONTEXT;
	const char *preserve_effective_context_string = astman_get_header(m, "PreserveEffectiveContext");
	struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS | CONFIG_FLAG_NOCACHE };
	enum error_type result;

	if (ast_strlen_zero(sfn) || ast_strlen_zero(dfn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}
	if (is_restricted_file(sfn) || is_restricted_file(dfn)) {
		astman_send_error(s, m, "File requires escalated privileges");
		return 0;
	}
	if (!(cfg = ast_config_load2(sfn, "manager", config_flags))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		astman_send_error(s, m, "Config file has invalid format");
		return 0;
	}
	result = handle_updates(s, m, cfg, dfn);
	if (!result) {
		ast_include_rename(cfg, sfn, dfn); /* change the include references from dfn to sfn, so things match up */
		if (!ast_strlen_zero(preserve_effective_context_string) && !ast_true(preserve_effective_context_string)) {
			preserve_effective_context = CONFIG_SAVE_FLAG_NONE;
		}
		res = ast_config_text_file_save2(dfn, cfg, "Manager", preserve_effective_context);
		ast_config_destroy(cfg);
		if (res) {
			astman_send_error(s, m, "Save of config failed");
			return 0;
		}
		astman_send_ack(s, m, NULL);
		if (!ast_strlen_zero(rld)) {
			if (ast_true(rld)) {
				ast_module_reload(NULL); /* Reload everything */
			} else if (!ast_false(rld)) {
				ast_module_reload(rld); /* Reload the specific module */
			}
		}
	} else {
		ast_config_destroy(cfg);
		switch(result) {
		case UNKNOWN_ACTION:
			astman_send_error(s, m, "Unknown action command");
			break;
		case UNKNOWN_CATEGORY:
			astman_send_error(s, m, "Given category does not exist");
			break;
		case UNSPECIFIED_CATEGORY:
			astman_send_error(s, m, "Category not specified");
			break;
		case UNSPECIFIED_ARGUMENT:
			astman_send_error(s, m, "Problem with category, value, or line (if required)");
			break;
		case FAILURE_ALLOCATION:
			astman_send_error(s, m, "Memory allocation failure, this should not happen");
			break;
		case FAILURE_NEWCAT:
			astman_send_error(s, m, "Create category did not complete successfully");
			break;
		case FAILURE_DELCAT:
			astman_send_error(s, m, "Delete category did not complete successfully");
			break;
		case FAILURE_EMPTYCAT:
			astman_send_error(s, m, "Empty category did not complete successfully");
			break;
		case FAILURE_UPDATE:
			astman_send_error(s, m, "Update did not complete successfully");
			break;
		case FAILURE_DELETE:
			astman_send_error(s, m, "Delete did not complete successfully");
			break;
		case FAILURE_APPEND:
			astman_send_error(s, m, "Append did not complete successfully");
			break;
		case FAILURE_TEMPLATE:
			astman_send_error(s, m, "Template category not found");
			break;
		}
	}
	return 0;
}

static int action_createconfig(struct mansession *s, const struct message *m)
{
	int fd;
	const char *fn = astman_get_header(m, "Filename");
	char *stripped_filename;
	RAII_VAR(char *, filepath, NULL, ast_free);
	RAII_VAR(char *, real_dir, NULL, ast_std_free);
	RAII_VAR(char *, real_path, NULL, ast_free);
	char *filename;

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}

	stripped_filename = ast_strip(ast_strdupa(fn));

	/* If the file name is relative, prepend ast_config_AST_CONFIG_DIR */
	if (stripped_filename[0] != '/') {
		if (ast_asprintf(&filepath, "%s/%s", ast_config_AST_CONFIG_DIR, stripped_filename) == -1) {
			return -1;
		}
	} else {
		filepath = ast_strdup(stripped_filename);
	}

	/*
	 * We can't call is_restricted_file() here because it uses realpath() and...
	 *
	 * realpath() and other functions that canonicalize paths won't work with
	 * a filename that doesn't exist, so we need to separate the directory
	 * from the filename and canonicalize the directory first.  We have to do
	 * the separation manually because dirname() and basename() aren't all
	 * that friendly to multi-threaded programs and there are different
	 * versions of basename for glibc and POSIX.
	 */

	filename = strrchr(filepath, '/');
	if (!filename) {
		astman_send_error(s, m, "Filename is invalid");
		return 0;
	}
	*filename = '\0';
	filename++;

	/* filepath just has the directory now so canonicalize it. */
	real_dir = realpath(filepath, NULL);
	if (ast_strlen_zero(real_dir)) {
		astman_send_error(s, m, strerror(errno));
		return 0;
	}

	/* Check if the directory is restricted. */
	if (!live_dangerously && !ast_begins_with(real_dir, ast_config_AST_CONFIG_DIR)) {
		astman_send_error(s, m, "File requires escalated privileges");
		return 0;
	}

	/* Create the final file path. */
	if (ast_asprintf(&real_path, "%s/%s", real_dir, filename) == -1) {
		astman_send_error(s, m, strerror(errno));
		return -1;
	}

	if ((fd = open(real_path, O_CREAT | O_EXCL, AST_FILE_MODE)) != -1) {
		close(fd);
		astman_send_ack(s, m, "New configuration file created successfully");
	} else {
		astman_send_error(s, m, strerror(errno));
	}

	return 0;
}

static int action_waitevent(struct mansession *s, const struct message *m)
{
	const char *timeouts = astman_get_header(m, "Timeout");
	int timeout = -1;
	int x;
	int needexit = 0;
	const char *id = astman_get_header(m, "ActionID");
	char idText[256];

	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	} else {
		idText[0] = '\0';
	}

	if (!ast_strlen_zero(timeouts)) {
		sscanf(timeouts, "%30i", &timeout);
		if (timeout < -1) {
			timeout = -1;
		}
		/* XXX maybe put an upper bound, or prevent the use of 0 ? */
	}

	ast_mutex_lock(&s->session->notify_lock);
	if (s->session->waiting_thread != AST_PTHREADT_NULL) {
		pthread_kill(s->session->waiting_thread, SIGURG);
	}
	ast_mutex_unlock(&s->session->notify_lock);

	ao2_lock(s->session);

	if (s->session->managerid) { /* AMI-over-HTTP session */
		/*
		 * Make sure the timeout is within the expire time of the session,
		 * as the client will likely abort the request if it does not see
		 * data coming after some amount of time.
		 */
		time_t now = time(NULL);
		int max = s->session->sessiontimeout - now - 10;

		if (max < 0) {	/* We are already late. Strange but possible. */
			max = 0;
		}
		if (timeout < 0 || timeout > max) {
			timeout = max;
		}
		if (!s->session->send_events) {	/* make sure we record events */
			s->session->send_events = -1;
		}
	}
	ao2_unlock(s->session);

	ast_mutex_lock(&s->session->notify_lock);
	s->session->waiting_thread = pthread_self();	/* let new events wake up this thread */
	ast_mutex_unlock(&s->session->notify_lock);
	ast_debug(1, "Starting waiting for an event!\n");

	for (x = 0; x < timeout || timeout < 0; x++) {
		ao2_lock(s->session);
		if (AST_RWLIST_NEXT(s->session->last_ev, eq_next)) {
			needexit = 1;
		}
		if (s->session->needdestroy) {
			needexit = 1;
		}
		ao2_unlock(s->session);
		/* We can have multiple HTTP session point to the same mansession entry.
		 * The way we deal with it is not very nice: newcomers kick out the previous
		 * HTTP session. XXX this needs to be improved.
		 */
		ast_mutex_lock(&s->session->notify_lock);
		if (s->session->waiting_thread != pthread_self()) {
			needexit = 1;
		}
		ast_mutex_unlock(&s->session->notify_lock);
		if (needexit) {
			break;
		}
		if (s->session->managerid == 0) {	/* AMI session */
			if (ast_wait_for_input(ast_iostream_get_fd(s->session->stream), 1000)) {
				break;
			}
		} else {	/* HTTP session */
			sleep(1);
		}
	}
	ast_debug(1, "Finished waiting for an event!\n");

	ast_mutex_lock(&s->session->notify_lock);
	if (s->session->waiting_thread == pthread_self()) {
		struct eventqent *eqe = s->session->last_ev;

		s->session->waiting_thread = AST_PTHREADT_NULL;
		ast_mutex_unlock(&s->session->notify_lock);

		ao2_lock(s->session);
		astman_send_response(s, m, "Success", "Waiting for Event completed.");
		while ((eqe = advance_event(eqe))) {
			if (((s->session->readperm & eqe->category) == eqe->category)
				&& ((s->session->send_events & eqe->category) == eqe->category)
				&& should_send_event(s->session->includefilters, s->session->excludefilters, eqe)) {
				astman_append(s, "%s", eqe->eventdata);
			}
			s->session->last_ev = eqe;
		}
		astman_append(s,
			"Event: WaitEventComplete\r\n"
			"%s"
			"\r\n", idText);
		ao2_unlock(s->session);
	} else {
		ast_mutex_unlock(&s->session->notify_lock);
		ast_debug(1, "Abandoning event request!\n");
	}

	return 0;
}

static int action_listcommands(struct mansession *s, const struct message *m)
{
	struct manager_action *cur;
	struct ast_str *temp = ast_str_alloca(MAX_AUTH_PERM_STRING);

	astman_start_ack(s, m);
	AST_RWLIST_RDLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		if ((s->session->writeperm & cur->authority) || cur->authority == 0) {
			astman_append(s, "%s: %s (Priv: %s)\r\n",
				cur->action, cur->synopsis, authority_to_str(cur->authority, &temp));
		}
	}
	AST_RWLIST_UNLOCK(&actions);
	astman_append(s, "\r\n");

	return 0;
}

static int action_events(struct mansession *s, const struct message *m)
{
	const char *mask = astman_get_header(m, "EventMask");
	int res, x;
	const char *id = astman_get_header(m, "ActionID");
	char id_text[256];

	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	} else {
		id_text[0] = '\0';
	}

	res = set_eventmask(s, mask);
	if (broken_events_action) {
		/* if this option is set we should not return a response on
		 * error, or when all events are set */

		if (res > 0) {
			for (x = 0; x < ARRAY_LEN(perms); x++) {
				if (!strcasecmp(perms[x].label, "all") && res == perms[x].num) {
					return 0;
				}
			}
			astman_append(s, "Response: Success\r\n%s"
					 "Events: On\r\n\r\n", id_text);
		} else if (res == 0)
			astman_append(s, "Response: Success\r\n%s"
					 "Events: Off\r\n\r\n", id_text);
		return 0;
	}

	if (res > 0)
		astman_append(s, "Response: Success\r\n%s"
				 "Events: On\r\n\r\n", id_text);
	else if (res == 0)
		astman_append(s, "Response: Success\r\n%s"
				 "Events: Off\r\n\r\n", id_text);
	else
		astman_send_error(s, m, "Invalid event mask");

	return 0;
}

static int action_logoff(struct mansession *s, const struct message *m)
{
	astman_send_response(s, m, "Goodbye", "Thanks for all the fish.");
	return -1;
}

static int action_login(struct mansession *s, const struct message *m)
{

	/* still authenticated - don't process again */
	if (s->session->authenticated) {
		astman_send_ack(s, m, "Already authenticated");
		return 0;
	}

	if (authenticate(s, m)) {
		sleep(1);
		astman_send_error(s, m, "Authentication failed");
		return -1;
	}
	s->session->authenticated = 1;
	ast_atomic_fetchadd_int(&unauth_sessions, -1);
	if (manager_displayconnects(s->session)) {
		ast_verb(2, "%sManager '%s' logged on from %s\n", (s->session->managerid ? "HTTP " : ""), s->session->username, ast_sockaddr_stringify_addr(&s->session->addr));
	}
	astman_send_ack(s, m, "Authentication accepted");
	if ((s->session->send_events & EVENT_FLAG_SYSTEM)
		&& (s->session->readperm & EVENT_FLAG_SYSTEM)
		&& ast_fully_booted) {
		struct ast_str *auth = ast_str_alloca(MAX_AUTH_PERM_STRING);
		const char *cat_str = authority_to_str(EVENT_FLAG_SYSTEM, &auth);
		long uptime = 0;
		long lastreloaded = 0;
		struct timeval tmp;
		struct timeval curtime = ast_tvnow();

		if (ast_startuptime.tv_sec) {
			tmp = ast_tvsub(curtime, ast_startuptime);
			uptime = tmp.tv_sec;
		}

		if (ast_lastreloadtime.tv_sec) {
			tmp = ast_tvsub(curtime, ast_lastreloadtime);
			lastreloaded = tmp.tv_sec;
		}

		astman_append(s, "Event: FullyBooted\r\n"
			"Privilege: %s\r\n"
			"Uptime: %ld\r\n"
			"LastReload: %ld\r\n"
			"Status: Fully Booted\r\n\r\n", cat_str, uptime, lastreloaded);
	}
	return 0;
}

static int action_challenge(struct mansession *s, const struct message *m)
{
	const char *authtype = astman_get_header(m, "AuthType");

	if (!strcasecmp(authtype, "MD5")) {
		if (ast_strlen_zero(s->session->challenge)) {
			snprintf(s->session->challenge, sizeof(s->session->challenge), "%ld", ast_random());
		}
		mansession_lock(s);
		astman_start_ack(s, m);
		astman_append(s, "Challenge: %s\r\n\r\n", s->session->challenge);
		mansession_unlock(s);
	} else {
		astman_send_error(s, m, "Must specify AuthType");
	}
	return 0;
}

int ast_manager_hangup_helper(struct mansession *s,
	const struct message *m, manager_hangup_handler_t hangup_handler,
	manager_hangup_cause_validator_t cause_validator)
{
	struct ast_channel *c = NULL;
	int causecode = 0; /* all values <= 0 mean 'do not set hangupcause in channel' */
	const char *id = astman_get_header(m, "ActionID");
	const char *name_or_regex = astman_get_header(m, "Channel");
	const char *cause = astman_get_header(m, "Cause");
	char idText[256];
	regex_t regexbuf;
	struct ast_channel_iterator *iter = NULL;
	struct ast_str *regex_string;
	int channels_matched = 0;

	if (ast_strlen_zero(name_or_regex)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	} else {
		idText[0] = '\0';
	}

	if (cause_validator) {
		causecode = cause_validator(name_or_regex, cause);
	} else if (!ast_strlen_zero(cause)) {
		char *endptr;
		causecode = strtol(cause, &endptr, 10);
		if (causecode < 0 || causecode > 127 || *endptr != '\0') {
			ast_log(LOG_NOTICE, "Invalid 'Cause: %s' in manager action Hangup\n", cause);
			/* keep going, better to hangup without cause than to not hang up at all */
			causecode = 0; /* do not set channel's hangupcause */
		}
	}

	/************************************************/
	/* Regular explicit match channel byname hangup */

	if (name_or_regex[0] != '/') {
		if (!(c = ast_channel_get_by_name(name_or_regex))) {
			ast_log(LOG_NOTICE, "Request to hangup non-existent channel: %s\n",
				name_or_regex);
			astman_send_error(s, m, "No such channel");
			return 0;
		}

		ast_verb(3, "%sManager '%s' from %s, hanging up channel: %s\n",
			(s->session->managerid ? "HTTP " : ""),
			s->session->username,
			ast_sockaddr_stringify_addr(&s->session->addr),
			ast_channel_name(c));

		hangup_handler(c, causecode);
		c = ast_channel_unref(c);

		astman_send_ack(s, m, "Channel Hungup");

		return 0;
	}

	/***********************************************/
	/* find and hangup any channels matching regex */

	regex_string = ast_str_create(strlen(name_or_regex));
	if (!regex_string) {
		astman_send_error(s, m, "Memory Allocation Failure");
		return 0;
	}

	/* Make "/regex/" into "regex" */
	if (ast_regex_string_to_regex_pattern(name_or_regex, &regex_string) != 0) {
		astman_send_error(s, m, "Regex format invalid, Channel param should be /regex/");
		ast_free(regex_string);
		return 0;
	}

	/* if regex compilation fails, hangup fails */
	if (regcomp(&regexbuf, ast_str_buffer(regex_string), REG_EXTENDED | REG_NOSUB)) {
		astman_send_error_va(s, m, "Regex compile failed on: %s", name_or_regex);
		ast_free(regex_string);
		return 0;
	}

	astman_send_listack(s, m, "Channels hung up will follow", "start");

	iter = ast_channel_iterator_all_new();
	if (iter) {
		for (; (c = ast_channel_iterator_next(iter)); ast_channel_unref(c)) {
			if (regexec(&regexbuf, ast_channel_name(c), 0, NULL, 0)) {
				continue;
			}

			ast_verb(3, "%sManager '%s' from %s, hanging up channel: %s\n",
				(s->session->managerid ? "HTTP " : ""),
				s->session->username,
				ast_sockaddr_stringify_addr(&s->session->addr),
				ast_channel_name(c));

			hangup_handler(c, causecode);
			channels_matched++;

			astman_append(s,
				"Event: ChannelHungup\r\n"
				"Channel: %s\r\n"
				"%s"
				"\r\n", ast_channel_name(c), idText);
		}
		ast_channel_iterator_destroy(iter);
	}

	regfree(&regexbuf);
	ast_free(regex_string);

	astman_send_list_complete(s, m, "ChannelsHungupListComplete", channels_matched);

	return 0;
}

static int action_hangup(struct mansession *s, const struct message *m)
{
	return ast_manager_hangup_helper(s, m,
		ast_channel_softhangup_withcause_locked, NULL);
}

static int action_setvar(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *varname = astman_get_header(m, "Variable");
	const char *varval = astman_get_header(m, "Value");
	int res = 0;

	if (ast_strlen_zero(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}

	if (!ast_strlen_zero(name)) {
		if (!(c = ast_channel_get_by_name(name))) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}

	res = pbx_builtin_setvar_helper(c, varname, S_OR(varval, ""));

	if (c) {
		c = ast_channel_unref(c);
	}
	if (res == 0) {
		astman_send_ack(s, m, "Variable Set");
	} else {
		astman_send_error(s, m, "Variable not set");
	}
	return 0;
}

static int action_getvar(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *varname = astman_get_header(m, "Variable");
	char *varval;
	char workspace[1024];

	if (ast_strlen_zero(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}

	/* We don't want users with insufficient permissions using certain functions. */
	if (!(function_capable_string_allowed_with_auths(varname, s->session->writeperm))) {
		astman_send_error(s, m, "GetVar Access Forbidden: Variable");
		return 0;
	}

	if (!ast_strlen_zero(name)) {
		if (!(c = ast_channel_get_by_name(name))) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}

	workspace[0] = '\0';
	if (varname[strlen(varname) - 1] == ')') {
		if (!c) {
			c = ast_dummy_channel_alloc();
			if (c) {
				ast_func_read(c, (char *) varname, workspace, sizeof(workspace));
			} else
				ast_log(LOG_ERROR, "Unable to allocate bogus channel for variable substitution.  Function results may be blank.\n");
		} else {
			ast_func_read(c, (char *) varname, workspace, sizeof(workspace));
		}
		varval = workspace;
	} else {
		pbx_retrieve_variable(c, varname, &varval, workspace, sizeof(workspace), NULL);
	}

	if (c) {
		c = ast_channel_unref(c);
	}

	astman_start_ack(s, m);
	astman_append(s, "Variable: %s\r\nValue: %s\r\n\r\n", varname, S_OR(varval, ""));

	return 0;
}

static void generate_status(struct mansession *s, struct ast_channel *chan, char **vars, int varc, int all_variables, char *id_text, int *count)
{
	struct timeval now;
	long elapsed_seconds;
	struct ast_bridge *bridge;
	RAII_VAR(struct ast_str *, variable_str, NULL, ast_free);
	struct ast_str *write_transpath = ast_str_alloca(256);
	struct ast_str *read_transpath = ast_str_alloca(256);
	struct ast_str *codec_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
	struct ast_party_id effective_id;
	int i;
	RAII_VAR(struct ast_channel_snapshot *, snapshot,
		ast_channel_snapshot_get_latest(ast_channel_uniqueid(chan)),
		ao2_cleanup);
	RAII_VAR(struct ast_str *, snapshot_str, NULL, ast_free);

	if (!snapshot) {
		return;
	}

	snapshot_str = ast_manager_build_channel_state_string(snapshot);
	if (!snapshot_str) {
		return;
	}

	if (all_variables) {
		variable_str = ast_str_create(2048);
	} else {
		variable_str = ast_str_create(1024);
	}
	if (!variable_str) {
		return;
	}

	now = ast_tvnow();
	elapsed_seconds = ast_tvdiff_sec(now, ast_channel_creationtime(chan));

	/* Even if all_variables has been specified, explicitly requested variables
	 * may be global variables or dialplan functions */
	for (i = 0; i < varc; i++) {
		char valbuf[512], *ret = NULL;

		if (vars[i][strlen(vars[i]) - 1] == ')') {
			if (ast_func_read(chan, vars[i], valbuf, sizeof(valbuf)) < 0) {
				valbuf[0] = '\0';
			}
			ret = valbuf;
		} else {
			pbx_retrieve_variable(chan, vars[i], &ret, valbuf, sizeof(valbuf), NULL);
		}

		ast_str_append(&variable_str, 0, "Variable: %s=%s\r\n", vars[i], ret);
	}

	/* Walk all channel variables and add them */
	if (all_variables) {
		struct ast_var_t *variables;

		AST_LIST_TRAVERSE(ast_channel_varshead(chan), variables, entries) {
			ast_str_append(&variable_str, 0, "Variable: %s=%s\r\n",
				ast_var_name(variables), ast_var_value(variables));
		}
	}

	bridge = ast_channel_get_bridge(chan);
	effective_id = ast_channel_connected_effective_id(chan);

	astman_append(s,
		"Event: Status\r\n"
		"Privilege: Call\r\n"
		"%s"
		"Type: %s\r\n"
		"DNID: %s\r\n"
		"EffectiveConnectedLineNum: %s\r\n"
		"EffectiveConnectedLineName: %s\r\n"
		"TimeToHangup: %ld\r\n"
		"BridgeID: %s\r\n"
		"Application: %s\r\n"
		"Data: %s\r\n"
		"Nativeformats: %s\r\n"
		"Readformat: %s\r\n"
		"Readtrans: %s\r\n"
		"Writeformat: %s\r\n"
		"Writetrans: %s\r\n"
		"Callgroup: %llu\r\n"
		"Pickupgroup: %llu\r\n"
		"Seconds: %ld\r\n"
		"%s"
		"%s"
		"\r\n",
		ast_str_buffer(snapshot_str),
		ast_channel_tech(chan)->type,
		S_OR(ast_channel_dialed(chan)->number.str, ""),
		S_COR(effective_id.number.valid, effective_id.number.str, "<unknown>"),
		S_COR(effective_id.name.valid, effective_id.name.str, "<unknown>"),
		(long)ast_channel_whentohangup(chan)->tv_sec,
		bridge ? bridge->uniqueid : "",
		ast_channel_appl(chan),
		ast_channel_data(chan),
		ast_format_cap_get_names(ast_channel_nativeformats(chan), &codec_buf),
		ast_format_get_name(ast_channel_readformat(chan)),
		ast_translate_path_to_str(ast_channel_readtrans(chan), &read_transpath),
		ast_format_get_name(ast_channel_writeformat(chan)),
		ast_translate_path_to_str(ast_channel_writetrans(chan), &write_transpath),
		ast_channel_callgroup(chan),
		ast_channel_pickupgroup(chan),
		(long)elapsed_seconds,
		ast_str_buffer(variable_str),
		id_text);
	++*count;

	ao2_cleanup(bridge);
}

/*! \brief Manager "status" command to show channels */
static int action_status(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *chan_variables = astman_get_header(m, "Variables");
	const char *all_chan_variables = astman_get_header(m, "AllVariables");
	int all_variables = 0;
	const char *id = astman_get_header(m, "ActionID");
	char *variables = ast_strdupa(S_OR(chan_variables, ""));
	struct ast_channel *chan;
	int channels = 0;
	int all = ast_strlen_zero(name); /* set if we want all channels */
	char id_text[256];
	struct ast_channel_iterator *it_chans = NULL;
	AST_DECLARE_APP_ARGS(vars,
		AST_APP_ARG(name)[100];
	);

	if (!ast_strlen_zero(all_chan_variables)) {
		all_variables = ast_true(all_chan_variables);
	}

	if (!(function_capable_string_allowed_with_auths(variables, s->session->writeperm))) {
		astman_send_error(s, m, "Status Access Forbidden: Variables");
		return 0;
	}

	if (all) {
		if (!(it_chans = ast_channel_iterator_all_new())) {
			astman_send_error(s, m, "Memory Allocation Failure");
			return 1;
		}
		chan = ast_channel_iterator_next(it_chans);
	} else {
		chan = ast_channel_get_by_name(name);
		if (!chan) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}

	astman_send_listack(s, m, "Channel status will follow", "start");

	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	} else {
		id_text[0] = '\0';
	}

	if (!ast_strlen_zero(chan_variables)) {
		AST_STANDARD_APP_ARGS(vars, variables);
	}

	/* if we look by name, we break after the first iteration */
	for (; chan; all ? chan = ast_channel_iterator_next(it_chans) : 0) {
		ast_channel_lock(chan);

		generate_status(s, chan, vars.name, vars.argc, all_variables, id_text, &channels);

		ast_channel_unlock(chan);
		chan = ast_channel_unref(chan);
	}

	if (it_chans) {
		ast_channel_iterator_destroy(it_chans);
	}

	astman_send_list_complete_start(s, m, "StatusComplete", channels);
	astman_append(s, "Items: %d\r\n", channels);
	astman_send_list_complete_end(s);

	return 0;
}

/*!
 * \brief Queue a given read action containing a payload onto a channel
 *
 * This queues a READ_ACTION control frame that contains a given "payload", or
 * data to be triggered and handled on the channel's read side. This ensures
 * the "action" is handled by the channel's media reading thread.
 *
 * \param chan The channel to queue the action on
 * \param payload The read action's payload
 * \param payload_size The size of the given payload
 * \param action The type of read action to queue
 *
 * \retval -1 on error
 * \retval 0 on success
 */
static int queue_read_action_payload(struct ast_channel *chan, const unsigned char *payload,
	size_t payload_size, enum ast_frame_read_action action)
{
	struct ast_control_read_action_payload *obj;
	size_t obj_size;
	int res;

	obj_size = payload_size + sizeof(*obj);

	obj = ast_malloc(obj_size);
	if (!obj) {
		return -1;
	}

	obj->action = action;
	obj->payload_size = payload_size;
	memcpy(obj->payload, payload, payload_size);

	res = ast_queue_control_data(chan, AST_CONTROL_READ_ACTION, obj, obj_size);

	ast_free(obj);
	return res;
}

/*!
 * \brief Queue a read action to send a text message
 *
 * \param chan The channel to queue the action on
 * \param body The body of the message
 *
 * \retval -1 on error
 * \retval 0 on success
 */
static int queue_sendtext(struct ast_channel *chan, const char *body)
{
	return queue_read_action_payload(chan, (const unsigned char *)body,
		strlen(body) + 1, AST_FRAME_READ_ACTION_SEND_TEXT);
}

/*!
 * \brief Queue a read action to send a text data message
 *
 * \param chan The channel to queue the action on
 * \param body The body of the message
 * \param content_type The message's content type
 *
 * \retval -1 on error
 * \retval 0 on success
 */
static int queue_sendtext_data(struct ast_channel *chan, const char *body,
	const char *content_type)
{
	int res;
	struct ast_msg_data *obj;

	obj = ast_msg_data_alloc2(AST_MSG_DATA_SOURCE_TYPE_UNKNOWN,
							NULL, NULL, content_type, body);
	if (!obj) {
		return -1;
	}

	res = queue_read_action_payload(chan, (const unsigned char *)obj,
		ast_msg_data_get_length(obj), AST_FRAME_READ_ACTION_SEND_TEXT_DATA);

	ast_free(obj);
	return res;
}

static int action_sendtext(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	const char *textmsg = astman_get_header(m, "Message");
	const char *content_type = astman_get_header(m, "Content-Type");
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (ast_strlen_zero(textmsg)) {
		astman_send_error(s, m, "No Message specified");
		return 0;
	}

	c = ast_channel_get_by_name(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	/*
	 * If the "extra" data is not available, then send using "string" only.
	 * Doing such maintains backward compatibilities.
	 */
	res = ast_strlen_zero(content_type) ? queue_sendtext(c, textmsg) :
		queue_sendtext_data(c, textmsg, content_type);

	ast_channel_unref(c);

	if (res >= 0) {
		astman_send_ack(s, m, "Success");
	} else {
		astman_send_error(s, m, "Failure");
	}

	return 0;
}

static int async_goto_with_discard_bridge_after(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	ast_bridge_discard_after_goto(chan);
	return ast_async_goto(chan, context, exten, priority);
}

/*! \brief  action_redirect: The redirect manager command */
static int action_redirect(struct mansession *s, const struct message *m)
{
	char buf[256];
	const char *name = astman_get_header(m, "Channel");
	const char *name2 = astman_get_header(m, "ExtraChannel");
	const char *exten = astman_get_header(m, "Exten");
	const char *exten2 = astman_get_header(m, "ExtraExten");
	const char *context = astman_get_header(m, "Context");
	const char *context2 = astman_get_header(m, "ExtraContext");
	const char *priority = astman_get_header(m, "Priority");
	const char *priority2 = astman_get_header(m, "ExtraPriority");
	struct ast_channel *chan;
	struct ast_channel *chan2;
	int pi = 0;
	int pi2 = 0;
	int res;
	int chan1_wait = 0;
	int chan2_wait = 0;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}

	if (ast_strlen_zero(context)) {
		astman_send_error(s, m, "Context not specified");
		return 0;
	}
	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "Exten not specified");
		return 0;
	}
	if (ast_strlen_zero(priority)) {
		astman_send_error(s, m, "Priority not specified");
		return 0;
	}
	if (sscanf(priority, "%30d", &pi) != 1) {
		pi = ast_findlabel_extension(NULL, context, exten, priority, NULL);
	}
	if (pi < 1) {
		astman_send_error(s, m, "Priority is invalid");
		return 0;
	}

	if (!ast_strlen_zero(name2) && !ast_strlen_zero(context2)) {
		/* We have an ExtraChannel and an ExtraContext */
		if (ast_strlen_zero(exten2)) {
			astman_send_error(s, m, "ExtraExten not specified");
			return 0;
		}
		if (ast_strlen_zero(priority2)) {
			astman_send_error(s, m, "ExtraPriority not specified");
			return 0;
		}
		if (sscanf(priority2, "%30d", &pi2) != 1) {
			pi2 = ast_findlabel_extension(NULL, context2, exten2, priority2, NULL);
		}
		if (pi2 < 1) {
			astman_send_error(s, m, "ExtraPriority is invalid");
			return 0;
		}
	}

	chan = ast_channel_get_by_name(name);
	if (!chan) {
		snprintf(buf, sizeof(buf), "Channel does not exist: %s", name);
		astman_send_error(s, m, buf);
		return 0;
	}
	if (ast_check_hangup_locked(chan)) {
		astman_send_error(s, m, "Redirect failed, channel not up.");
		chan = ast_channel_unref(chan);
		return 0;
	}

	if (ast_strlen_zero(name2)) {
		/* Single channel redirect in progress. */
		res = async_goto_with_discard_bridge_after(chan, context, exten, pi);
		if (!res) {
			astman_send_ack(s, m, "Redirect successful");
		} else {
			astman_send_error(s, m, "Redirect failed");
		}
		chan = ast_channel_unref(chan);
		return 0;
	}

	chan2 = ast_channel_get_by_name(name2);
	if (!chan2) {
		snprintf(buf, sizeof(buf), "ExtraChannel does not exist: %s", name2);
		astman_send_error(s, m, buf);
		chan = ast_channel_unref(chan);
		return 0;
	}
	if (ast_check_hangup_locked(chan2)) {
		astman_send_error(s, m, "Redirect failed, extra channel not up.");
		chan2 = ast_channel_unref(chan2);
		chan = ast_channel_unref(chan);
		return 0;
	}

	/* Dual channel redirect in progress. */
	ast_channel_lock(chan);
	if (ast_channel_is_bridged(chan)) {
		ast_set_flag(ast_channel_flags(chan), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT);
		chan1_wait = 1;
	}
	ast_channel_unlock(chan);

	ast_channel_lock(chan2);
	if (ast_channel_is_bridged(chan2)) {
		ast_set_flag(ast_channel_flags(chan2), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT);
		chan2_wait = 1;
	}
	ast_channel_unlock(chan2);

	res = async_goto_with_discard_bridge_after(chan, context, exten, pi);
	if (!res) {
		if (!ast_strlen_zero(context2)) {
			res = async_goto_with_discard_bridge_after(chan2, context2, exten2, pi2);
		} else {
			res = async_goto_with_discard_bridge_after(chan2, context, exten, pi);
		}
		if (!res) {
			astman_send_ack(s, m, "Dual Redirect successful");
		} else {
			astman_send_error(s, m, "Secondary redirect failed");
		}
	} else {
		astman_send_error(s, m, "Redirect failed");
	}

	/* Release the bridge wait. */
	if (chan1_wait) {
		ast_channel_clear_flag(chan, AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT);
	}
	if (chan2_wait) {
		ast_channel_clear_flag(chan2, AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT);
	}

	chan2 = ast_channel_unref(chan2);
	chan = ast_channel_unref(chan);
	return 0;
}

static int action_blind_transfer(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	struct ast_channel *chan;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "No extension specified");
		return 0;
	}

	chan = ast_channel_get_by_name(name);
	if (!chan) {
		astman_send_error(s, m, "Channel specified does not exist");
		return 0;
	}

	if (ast_strlen_zero(context)) {
		context = ast_channel_context(chan);
	}

	switch (ast_bridge_transfer_blind(1, chan, exten, context, NULL, NULL)) {
	case AST_BRIDGE_TRANSFER_NOT_PERMITTED:
		astman_send_error(s, m, "Transfer not permitted");
		break;
	case AST_BRIDGE_TRANSFER_INVALID:
		astman_send_error(s, m, "Transfer invalid");
		break;
	case AST_BRIDGE_TRANSFER_FAIL:
		astman_send_error(s, m, "Transfer failed");
		break;
	case AST_BRIDGE_TRANSFER_SUCCESS:
		astman_send_ack(s, m, "Transfer succeeded");
		break;
	}

	ast_channel_unref(chan);
	return 0;
}

static int action_atxfer(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	struct ast_channel *chan = NULL;
	char feature_code[AST_FEATURE_MAX_LEN];
	const char *digit;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "No extension specified");
		return 0;
	}

	if (!(chan = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "Channel specified does not exist");
		return 0;
	}

	ast_channel_lock(chan);
	if (ast_get_builtin_feature(chan, "atxfer", feature_code, sizeof(feature_code)) ||
			ast_strlen_zero(feature_code)) {
		ast_channel_unlock(chan);
		astman_send_error(s, m, "No attended transfer feature code found");
		ast_channel_unref(chan);
		return 0;
	}
	ast_channel_unlock(chan);

	if (!ast_strlen_zero(context)) {
		pbx_builtin_setvar_helper(chan, "TRANSFER_CONTEXT", context);
	}

	for (digit = feature_code; *digit; ++digit) {
		struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = *digit };
		ast_queue_frame(chan, &f);
	}

	for (digit = exten; *digit; ++digit) {
		struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = *digit };
		ast_queue_frame(chan, &f);
	}

	chan = ast_channel_unref(chan);

	astman_send_ack(s, m, "Atxfer successfully queued");

	return 0;
}

static int action_cancel_atxfer(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	struct ast_channel *chan = NULL;
	char *feature_code;
	const char *digit;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (!(chan = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "Channel specified does not exist");
		return 0;
	}

	ast_channel_lock(chan);
	feature_code = ast_get_chan_features_atxferabort(chan);
	ast_channel_unlock(chan);

	if (!feature_code) {
		astman_send_error(s, m, "No disconnect feature code found");
		ast_channel_unref(chan);
		return 0;
	}

	for (digit = feature_code; *digit; ++digit) {
		struct ast_frame f = { AST_FRAME_DTMF, .subclass.integer = *digit };
		ast_queue_frame(chan, &f);
	}
	ast_free(feature_code);

	chan = ast_channel_unref(chan);

	astman_send_ack(s, m, "CancelAtxfer successfully queued");

	return 0;
}


static int check_blacklist(const char *cmd)
{
	char *cmd_copy, *cur_cmd;
	char *cmd_words[AST_MAX_CMD_LEN] = { NULL, };
	int i;

	cmd_copy = ast_strdupa(cmd);
	for (i = 0; i < MAX_BLACKLIST_CMD_LEN && (cur_cmd = strsep(&cmd_copy, " ")); i++) {
		cur_cmd = ast_strip(cur_cmd);
		if (ast_strlen_zero(cur_cmd)) {
			i--;
			continue;
		}

		cmd_words[i] = cur_cmd;
	}

	for (i = 0; i < ARRAY_LEN(command_blacklist); i++) {
		int j, match = 1;

		for (j = 0; command_blacklist[i].words[j]; j++) {
			if (ast_strlen_zero(cmd_words[j]) || strcasecmp(cmd_words[j], command_blacklist[i].words[j])) {
				match = 0;
				break;
			}
		}

		if (match) {
			return 1;
		}
	}

	return 0;
}

/*! \brief  Manager command "command" - execute CLI command */
static int action_command(struct mansession *s, const struct message *m)
{
	const char *cmd = astman_get_header(m, "Command");
	char *buf = NULL, *final_buf = NULL, *delim, *output;
	char template[] = "/tmp/ast-ami-XXXXXX";	/* template for temporary file */
	int fd, ret;
	off_t len;

	if (ast_strlen_zero(cmd)) {
		astman_send_error(s, m, "No command provided");
		return 0;
	}

	if (check_blacklist(cmd)) {
		astman_send_error(s, m, "Command blacklisted");
		return 0;
	}

	if ((fd = mkstemp(template)) < 0) {
		astman_send_error_va(s, m, "Failed to create temporary file: %s", strerror(errno));
		return 0;
	}

	ret = ast_cli_command(fd, cmd);
	astman_send_response_full(s, m, ret == RESULT_SUCCESS ? "Success" : "Error", MSG_MOREDATA, NULL);

	/* Determine number of characters available */
	if ((len = lseek(fd, 0, SEEK_END)) < 0) {
		astman_append(s, "Message: Failed to determine number of characters: %s\r\n", strerror(errno));
		goto action_command_cleanup;
	}

	/* This has a potential to overflow the stack.  Hence, use the heap. */
	buf = ast_malloc(len + 1);
	final_buf = ast_malloc(len + 1);

	if (!buf || !final_buf) {
		astman_append(s, "Message: Memory allocation failure\r\n");
		goto action_command_cleanup;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		astman_append(s, "Message: Failed to set position on temporary file: %s\r\n", strerror(errno));
		goto action_command_cleanup;
	}

	if (read(fd, buf, len) < 0) {
		astman_append(s, "Message: Failed to read from temporary file: %s\r\n", strerror(errno));
		goto action_command_cleanup;
	}

	buf[len] = '\0';
	term_strip(final_buf, buf, len);
	final_buf[len] = '\0';

	/* Trim trailing newline */
	if (len && final_buf[len - 1] == '\n') {
		final_buf[len - 1] = '\0';
	}

	astman_append(s, "Message: Command output follows\r\n");

	delim = final_buf;
	while ((output = strsep(&delim, "\n"))) {
		astman_append(s, "Output: %s\r\n", output);
	}

action_command_cleanup:
	astman_append(s, "\r\n");

	close(fd);
	unlink(template);

	ast_free(buf);
	ast_free(final_buf);

	return 0;
}

/*! \brief helper function for originate */
struct fast_originate_helper {
	int timeout;
	struct ast_format_cap *cap;				/*!< Codecs used for a call */
	int early_media;
	AST_DECLARE_STRING_FIELDS (
		AST_STRING_FIELD(tech);
		/*! data can contain a channel name, extension number, username, password, etc. */
		AST_STRING_FIELD(data);
		AST_STRING_FIELD(app);
		AST_STRING_FIELD(appdata);
		AST_STRING_FIELD(cid_name);
		AST_STRING_FIELD(cid_num);
		AST_STRING_FIELD(context);
		AST_STRING_FIELD(exten);
		AST_STRING_FIELD(idtext);
		AST_STRING_FIELD(account);
		AST_STRING_FIELD(channelid);
		AST_STRING_FIELD(otherchannelid);
	);
	int priority;
	struct ast_variable *vars;
};

/*!
 * \internal
 *
 * \param doomed Struct to destroy.
 */
static void destroy_fast_originate_helper(struct fast_originate_helper *doomed)
{
	ao2_cleanup(doomed->cap);
	ast_variables_destroy(doomed->vars);
	ast_string_field_free_memory(doomed);
	ast_free(doomed);
}

static void *fast_originate(void *data)
{
	struct fast_originate_helper *in = data;
	int res;
	int reason = 0;
	struct ast_channel *chan = NULL, *chans[1];
	char requested_channel[AST_CHANNEL_NAME];
	struct ast_assigned_ids assignedids = {
		.uniqueid = in->channelid,
		.uniqueid2 = in->otherchannelid
	};

	if (!ast_strlen_zero(in->app)) {
		res = ast_pbx_outgoing_app(in->tech, in->cap, in->data,
			in->timeout, in->app, in->appdata, &reason,
			AST_OUTGOING_WAIT,
			S_OR(in->cid_num, NULL),
			S_OR(in->cid_name, NULL),
			in->vars, in->account, &chan, &assignedids);
	} else {
		res = ast_pbx_outgoing_exten(in->tech, in->cap, in->data,
			in->timeout, in->context, in->exten, in->priority, &reason,
			AST_OUTGOING_WAIT,
			S_OR(in->cid_num, NULL),
			S_OR(in->cid_name, NULL),
			in->vars, in->account, &chan, in->early_media, &assignedids);
	}

	if (!chan) {
		snprintf(requested_channel, AST_CHANNEL_NAME, "%s/%s", in->tech, in->data);
	}
	/* Tell the manager what happened with the channel */
	chans[0] = chan;
	if (!ast_strlen_zero(in->app)) {
		ast_manager_event_multichan(EVENT_FLAG_CALL, "OriginateResponse", chan ? 1 : 0, chans,
			"%s"
			"Response: %s\r\n"
			"Channel: %s\r\n"
			"Application: %s\r\n"
			"Data: %s\r\n"
			"Reason: %d\r\n"
			"Uniqueid: %s\r\n"
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n",
			in->idtext, res ? "Failure" : "Success",
			chan ? ast_channel_name(chan) : requested_channel,
			in->app, in->appdata, reason,
			chan ? ast_channel_uniqueid(chan) : S_OR(in->channelid, "<unknown>"),
			S_OR(in->cid_num, "<unknown>"),
			S_OR(in->cid_name, "<unknown>")
			);
	} else {
		ast_manager_event_multichan(EVENT_FLAG_CALL, "OriginateResponse", chan ? 1 : 0, chans,
			"%s"
			"Response: %s\r\n"
			"Channel: %s\r\n"
			"Context: %s\r\n"
			"Exten: %s\r\n"
			"Reason: %d\r\n"
			"Uniqueid: %s\r\n"
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n",
			in->idtext, res ? "Failure" : "Success",
			chan ? ast_channel_name(chan) : requested_channel,
			in->context, in->exten, reason,
			chan ? ast_channel_uniqueid(chan) : S_OR(in->channelid, "<unknown>"),
			S_OR(in->cid_num, "<unknown>"),
			S_OR(in->cid_name, "<unknown>")
			);
	}

	/* Locked and ref'd by ast_pbx_outgoing_exten or ast_pbx_outgoing_app */
	if (chan) {
		ast_channel_unlock(chan);
		ast_channel_unref(chan);
	}
	destroy_fast_originate_helper(in);
	return NULL;
}

static int aocmessage_get_unit_entry(const struct message *m, struct ast_aoc_unit_entry *entry, unsigned int entry_num)
{
	const char *unitamount;
	const char *unittype;
	struct ast_str *str = ast_str_alloca(32);

	memset(entry, 0, sizeof(*entry));

	ast_str_set(&str, 0, "UnitAmount(%u)", entry_num);
	unitamount = astman_get_header(m, ast_str_buffer(str));

	ast_str_set(&str, 0, "UnitType(%u)", entry_num);
	unittype = astman_get_header(m, ast_str_buffer(str));

	if (!ast_strlen_zero(unitamount) && (sscanf(unitamount, "%30u", &entry->amount) == 1)) {
		entry->valid_amount = 1;
	}

	if (!ast_strlen_zero(unittype) && sscanf(unittype, "%30u", &entry->type) == 1) {
		entry->valid_type = 1;
	}

	return 0;
}

static struct ast_aoc_decoded *action_aoc_de_message(struct mansession *s, const struct message *m)
{
	const char *msgtype = astman_get_header(m, "MsgType");
	const char *chargetype = astman_get_header(m, "ChargeType");
	const char *currencyname = astman_get_header(m, "CurrencyName");
	const char *currencyamount = astman_get_header(m, "CurrencyAmount");
	const char *mult = astman_get_header(m, "CurrencyMultiplier");
	const char *totaltype = astman_get_header(m, "TotalType");
	const char *aocbillingid = astman_get_header(m, "AOCBillingId");
	const char *association_id= astman_get_header(m, "ChargingAssociationId");
	const char *association_num = astman_get_header(m, "ChargingAssociationNumber");
	const char *association_plan = astman_get_header(m, "ChargingAssociationPlan");

	enum ast_aoc_type _msgtype;
	enum ast_aoc_charge_type _chargetype;
	enum ast_aoc_currency_multiplier _mult = AST_AOC_MULT_ONE;
	enum ast_aoc_total_type _totaltype = AST_AOC_TOTAL;
	enum ast_aoc_billing_id _billingid = AST_AOC_BILLING_NA;
	unsigned int _currencyamount = 0;
	int _association_id = 0;
	unsigned int _association_plan = 0;

	struct ast_aoc_decoded *decoded = NULL;

	if (ast_strlen_zero(chargetype)) {
		astman_send_error(s, m, "ChargeType not specified");
		goto aocmessage_cleanup;
	}

	_msgtype = strcasecmp(msgtype, "d") ? AST_AOC_E : AST_AOC_D;

	if (!strcasecmp(chargetype, "NA")) {
		_chargetype = AST_AOC_CHARGE_NA;
	} else if (!strcasecmp(chargetype, "Free")) {
		_chargetype = AST_AOC_CHARGE_FREE;
	} else if (!strcasecmp(chargetype, "Currency")) {
		_chargetype = AST_AOC_CHARGE_CURRENCY;
	} else if (!strcasecmp(chargetype, "Unit")) {
		_chargetype = AST_AOC_CHARGE_UNIT;
	} else {
		astman_send_error(s, m, "Invalid ChargeType");
		goto aocmessage_cleanup;
	}

	if (_chargetype == AST_AOC_CHARGE_CURRENCY) {

		if (ast_strlen_zero(currencyamount) || (sscanf(currencyamount, "%30u", &_currencyamount) != 1)) {
			astman_send_error(s, m, "Invalid CurrencyAmount, CurrencyAmount is a required when ChargeType is Currency");
			goto aocmessage_cleanup;
		}

		if (ast_strlen_zero(mult)) {
			astman_send_error(s, m, "ChargeMultiplier unspecified, ChargeMultiplier is required when ChargeType is Currency.");
			goto aocmessage_cleanup;
		} else if (!strcasecmp(mult, "onethousandth")) {
			_mult = AST_AOC_MULT_ONETHOUSANDTH;
		} else if (!strcasecmp(mult, "onehundredth")) {
			_mult = AST_AOC_MULT_ONEHUNDREDTH;
		} else if (!strcasecmp(mult, "onetenth")) {
			_mult = AST_AOC_MULT_ONETENTH;
		} else if (!strcasecmp(mult, "one")) {
			_mult = AST_AOC_MULT_ONE;
		} else if (!strcasecmp(mult, "ten")) {
			_mult = AST_AOC_MULT_TEN;
		} else if (!strcasecmp(mult, "hundred")) {
			_mult = AST_AOC_MULT_HUNDRED;
		} else if (!strcasecmp(mult, "thousand")) {
			_mult = AST_AOC_MULT_THOUSAND;
		} else {
			astman_send_error(s, m, "Invalid ChargeMultiplier");
			goto aocmessage_cleanup;
		}
	}

	/* create decoded object and start setting values */
	if (!(decoded = ast_aoc_create(_msgtype, _chargetype, 0))) {
			astman_send_error(s, m, "Message Creation Failed");
			goto aocmessage_cleanup;
	}

	if (_msgtype == AST_AOC_D) {
		if (!ast_strlen_zero(totaltype) && !strcasecmp(totaltype, "subtotal")) {
			_totaltype = AST_AOC_SUBTOTAL;
		}

		if (ast_strlen_zero(aocbillingid)) {
			/* ignore this is optional */
		} else if (!strcasecmp(aocbillingid, "Normal")) {
			_billingid = AST_AOC_BILLING_NORMAL;
		} else if (!strcasecmp(aocbillingid, "ReverseCharge")) {
			_billingid = AST_AOC_BILLING_REVERSE_CHARGE;
		} else if (!strcasecmp(aocbillingid, "CreditCard")) {
			_billingid = AST_AOC_BILLING_CREDIT_CARD;
		} else {
			astman_send_error(s, m, "Invalid AOC-D AOCBillingId");
			goto aocmessage_cleanup;
		}
	} else {
		if (ast_strlen_zero(aocbillingid)) {
			/* ignore this is optional */
		} else if (!strcasecmp(aocbillingid, "Normal")) {
			_billingid = AST_AOC_BILLING_NORMAL;
		} else if (!strcasecmp(aocbillingid, "ReverseCharge")) {
			_billingid = AST_AOC_BILLING_REVERSE_CHARGE;
		} else if (!strcasecmp(aocbillingid, "CreditCard")) {
			_billingid = AST_AOC_BILLING_CREDIT_CARD;
		} else if (!strcasecmp(aocbillingid, "CallFwdUnconditional")) {
			_billingid = AST_AOC_BILLING_CALL_FWD_UNCONDITIONAL;
		} else if (!strcasecmp(aocbillingid, "CallFwdBusy")) {
			_billingid = AST_AOC_BILLING_CALL_FWD_BUSY;
		} else if (!strcasecmp(aocbillingid, "CallFwdNoReply")) {
			_billingid = AST_AOC_BILLING_CALL_FWD_NO_REPLY;
		} else if (!strcasecmp(aocbillingid, "CallDeflection")) {
			_billingid = AST_AOC_BILLING_CALL_DEFLECTION;
		} else if (!strcasecmp(aocbillingid, "CallTransfer")) {
			_billingid = AST_AOC_BILLING_CALL_TRANSFER;
		} else {
			astman_send_error(s, m, "Invalid AOC-E AOCBillingId");
			goto aocmessage_cleanup;
		}

		if (!ast_strlen_zero(association_id) && (sscanf(association_id, "%30d", &_association_id) != 1)) {
			astman_send_error(s, m, "Invalid ChargingAssociationId");
			goto aocmessage_cleanup;
		}
		if (!ast_strlen_zero(association_plan) && (sscanf(association_plan, "%30u", &_association_plan) != 1)) {
			astman_send_error(s, m, "Invalid ChargingAssociationPlan");
			goto aocmessage_cleanup;
		}

		if (_association_id) {
			ast_aoc_set_association_id(decoded, _association_id);
		} else if (!ast_strlen_zero(association_num)) {
			ast_aoc_set_association_number(decoded, association_num, _association_plan);
		}
	}

	if (_chargetype == AST_AOC_CHARGE_CURRENCY) {
		ast_aoc_set_currency_info(decoded, _currencyamount, _mult, ast_strlen_zero(currencyname) ? NULL : currencyname);
	} else if (_chargetype == AST_AOC_CHARGE_UNIT) {
		struct ast_aoc_unit_entry entry;
		int i;

		/* multiple unit entries are possible, lets get them all */
		for (i = 0; i < 32; i++) {
			if (aocmessage_get_unit_entry(m, &entry, i)) {
				break; /* that's the end then */
			}

			ast_aoc_add_unit_entry(decoded, entry.valid_amount, entry.amount, entry.valid_type, entry.type);
		}

		/* at least one unit entry is required */
		if (!i) {
			astman_send_error(s, m, "Invalid UnitAmount(0), At least one valid unit entry is required when ChargeType is set to Unit");
			goto aocmessage_cleanup;
		}

	}

	ast_aoc_set_billing_id(decoded, _billingid);
	ast_aoc_set_total_type(decoded, _totaltype);

	return decoded;

aocmessage_cleanup:

	ast_aoc_destroy_decoded(decoded);
	return NULL;
}

static int action_aoc_s_submessage(struct mansession *s, const struct message *m,
		struct ast_aoc_decoded *decoded)
{
	const char *chargeditem = __astman_get_header(m, "ChargedItem", GET_HEADER_LAST_MATCH);
	const char *ratetype = __astman_get_header(m, "RateType", GET_HEADER_LAST_MATCH);
	const char *currencyname = __astman_get_header(m, "CurrencyName", GET_HEADER_LAST_MATCH);
	const char *currencyamount = __astman_get_header(m, "CurrencyAmount", GET_HEADER_LAST_MATCH);
	const char *mult = __astman_get_header(m, "CurrencyMultiplier", GET_HEADER_LAST_MATCH);
	const char *time = __astman_get_header(m, "Time", GET_HEADER_LAST_MATCH);
	const char *timescale = __astman_get_header(m, "TimeScale", GET_HEADER_LAST_MATCH);
	const char *granularity = __astman_get_header(m, "Granularity", GET_HEADER_LAST_MATCH);
	const char *granularitytimescale = __astman_get_header(m, "GranularityTimeScale", GET_HEADER_LAST_MATCH);
	const char *chargingtype = __astman_get_header(m, "ChargingType", GET_HEADER_LAST_MATCH);
	const char *volumeunit = __astman_get_header(m, "VolumeUnit", GET_HEADER_LAST_MATCH);
	const char *code = __astman_get_header(m, "Code", GET_HEADER_LAST_MATCH);

	enum ast_aoc_s_charged_item _chargeditem;
	enum ast_aoc_s_rate_type _ratetype;
	enum ast_aoc_currency_multiplier _mult = AST_AOC_MULT_ONE;
	unsigned int _currencyamount = 0;
	unsigned int _code;
	unsigned int _time = 0;
	enum ast_aoc_time_scale _scale = 0;
	unsigned int _granularity = 0;
	enum ast_aoc_time_scale _granularity_time_scale = AST_AOC_TIME_SCALE_MINUTE;
	int _step = 0;
	enum ast_aoc_volume_unit _volumeunit = 0;

	if (ast_strlen_zero(chargeditem)) {
		astman_send_error(s, m, "ChargedItem not specified");
		goto aocmessage_cleanup;
	}

	if (ast_strlen_zero(ratetype)) {
		astman_send_error(s, m, "RateType not specified");
		goto aocmessage_cleanup;
	}

	if (!strcasecmp(chargeditem, "NA")) {
		_chargeditem = AST_AOC_CHARGED_ITEM_NA;
	} else if (!strcasecmp(chargeditem, "SpecialArrangement")) {
		_chargeditem = AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT;
	} else if (!strcasecmp(chargeditem, "BasicCommunication")) {
		_chargeditem = AST_AOC_CHARGED_ITEM_BASIC_COMMUNICATION;
	} else if (!strcasecmp(chargeditem, "CallAttempt")) {
		_chargeditem = AST_AOC_CHARGED_ITEM_CALL_ATTEMPT;
	} else if (!strcasecmp(chargeditem, "CallSetup")) {
		_chargeditem = AST_AOC_CHARGED_ITEM_CALL_SETUP;
	} else if (!strcasecmp(chargeditem, "UserUserInfo")) {
		_chargeditem = AST_AOC_CHARGED_ITEM_USER_USER_INFO;
	} else if (!strcasecmp(chargeditem, "SupplementaryService")) {
		_chargeditem = AST_AOC_CHARGED_ITEM_SUPPLEMENTARY_SERVICE;
	} else {
		astman_send_error(s, m, "Invalid ChargedItem");
		goto aocmessage_cleanup;
	}

	if (!strcasecmp(ratetype, "NA")) {
		_ratetype = AST_AOC_RATE_TYPE_NA;
	} else if (!strcasecmp(ratetype, "Free")) {
		_ratetype = AST_AOC_RATE_TYPE_FREE;
	} else if (!strcasecmp(ratetype, "FreeFromBeginning")) {
		_ratetype = AST_AOC_RATE_TYPE_FREE_FROM_BEGINNING;
	} else if (!strcasecmp(ratetype, "Duration")) {
		_ratetype = AST_AOC_RATE_TYPE_DURATION;
	} else if (!strcasecmp(ratetype, "Flat")) {
		_ratetype = AST_AOC_RATE_TYPE_FLAT;
	} else if (!strcasecmp(ratetype, "Volume")) {
		_ratetype = AST_AOC_RATE_TYPE_VOLUME;
	} else if (!strcasecmp(ratetype, "SpecialCode")) {
		_ratetype = AST_AOC_RATE_TYPE_SPECIAL_CODE;
	} else {
		astman_send_error(s, m, "Invalid RateType");
		goto aocmessage_cleanup;
	}

	if (_ratetype > AST_AOC_RATE_TYPE_FREE_FROM_BEGINNING) {
		if (ast_strlen_zero(currencyamount) || (sscanf(currencyamount, "%30u",
				&_currencyamount) != 1)) {
			astman_send_error(s, m, "Invalid CurrencyAmount, CurrencyAmount is a required when RateType is non-free");
			goto aocmessage_cleanup;
		}

		if (ast_strlen_zero(mult)) {
			astman_send_error(s, m, "ChargeMultiplier unspecified, ChargeMultiplier is required when ChargeType is Currency.");
			goto aocmessage_cleanup;
		} else if (!strcasecmp(mult, "onethousandth")) {
			_mult = AST_AOC_MULT_ONETHOUSANDTH;
		} else if (!strcasecmp(mult, "onehundredth")) {
			_mult = AST_AOC_MULT_ONEHUNDREDTH;
		} else if (!strcasecmp(mult, "onetenth")) {
			_mult = AST_AOC_MULT_ONETENTH;
		} else if (!strcasecmp(mult, "one")) {
			_mult = AST_AOC_MULT_ONE;
		} else if (!strcasecmp(mult, "ten")) {
			_mult = AST_AOC_MULT_TEN;
		} else if (!strcasecmp(mult, "hundred")) {
			_mult = AST_AOC_MULT_HUNDRED;
		} else if (!strcasecmp(mult, "thousand")) {
			_mult = AST_AOC_MULT_THOUSAND;
		} else {
			astman_send_error(s, m, "Invalid ChargeMultiplier");
			goto aocmessage_cleanup;
		}
	}

	if (_ratetype == AST_AOC_RATE_TYPE_DURATION) {
		if (ast_strlen_zero(timescale)) {
			astman_send_error(s, m, "TimeScale unspecified, TimeScale is required when RateType is Duration.");
			goto aocmessage_cleanup;
		} else if (!strcasecmp(timescale, "onehundredthsecond")) {
			_scale = AST_AOC_TIME_SCALE_HUNDREDTH_SECOND;
		} else if (!strcasecmp(timescale, "onetenthsecond")) {
			_scale = AST_AOC_TIME_SCALE_TENTH_SECOND;
		} else if (!strcasecmp(timescale, "second")) {
			_scale = AST_AOC_TIME_SCALE_SECOND;
		} else if (!strcasecmp(timescale, "tenseconds")) {
			_scale = AST_AOC_TIME_SCALE_TEN_SECOND;
		} else if (!strcasecmp(timescale, "minute")) {
			_scale = AST_AOC_TIME_SCALE_MINUTE;
		} else if (!strcasecmp(timescale, "hour")) {
			_scale = AST_AOC_TIME_SCALE_HOUR;
		} else if (!strcasecmp(timescale, "day")) {
			_scale = AST_AOC_TIME_SCALE_DAY;
		} else {
			astman_send_error(s, m, "Invalid TimeScale");
			goto aocmessage_cleanup;
		}

		if (ast_strlen_zero(time) || (sscanf(time, "%30u", &_time) != 1)) {
			astman_send_error(s, m, "Invalid Time, Time is a required when RateType is Duration");
			goto aocmessage_cleanup;
		}

		if (!ast_strlen_zero(granularity)) {
			if ((sscanf(time, "%30u", &_granularity) != 1)) {
				astman_send_error(s, m, "Invalid Granularity");
				goto aocmessage_cleanup;
			}

			if (ast_strlen_zero(granularitytimescale)) {
				astman_send_error(s, m, "Invalid GranularityTimeScale, GranularityTimeScale is a required when Granularity is specified");
			} else if (!strcasecmp(granularitytimescale, "onehundredthsecond")) {
				_granularity_time_scale = AST_AOC_TIME_SCALE_HUNDREDTH_SECOND;
			} else if (!strcasecmp(granularitytimescale, "onetenthsecond")) {
				_granularity_time_scale = AST_AOC_TIME_SCALE_TENTH_SECOND;
			} else if (!strcasecmp(granularitytimescale, "second")) {
				_granularity_time_scale = AST_AOC_TIME_SCALE_SECOND;
			} else if (!strcasecmp(granularitytimescale, "tenseconds")) {
				_granularity_time_scale = AST_AOC_TIME_SCALE_TEN_SECOND;
			} else if (!strcasecmp(granularitytimescale, "minute")) {
				_granularity_time_scale = AST_AOC_TIME_SCALE_MINUTE;
			} else if (!strcasecmp(granularitytimescale, "hour")) {
				_granularity_time_scale = AST_AOC_TIME_SCALE_HOUR;
			} else if (!strcasecmp(granularitytimescale, "day")) {
				_granularity_time_scale = AST_AOC_TIME_SCALE_DAY;
			} else {
				astman_send_error(s, m, "Invalid GranularityTimeScale");
				goto aocmessage_cleanup;
			}
		}

		if (ast_strlen_zero(chargingtype) || strcasecmp(chargingtype, "continuouscharging") == 0) {
			_step = 0;
		} else if (strcasecmp(chargingtype, "stepfunction") == 0 ) {
			_step = 1;
		} else {
			astman_send_error(s, m, "Invalid ChargingType");
			goto aocmessage_cleanup;
		}
	}

	if (_ratetype == AST_AOC_RATE_TYPE_VOLUME) {
		if (ast_strlen_zero(volumeunit)) {
			astman_send_error(s, m, "VolumeUnit unspecified, VolumeUnit is required when RateType is Volume.");
			goto aocmessage_cleanup;
		} else if (!strcasecmp(timescale, "octet")) {
			_volumeunit = AST_AOC_VOLUME_UNIT_OCTET;
		} else if (!strcasecmp(timescale, "segment")) {
			_volumeunit = AST_AOC_VOLUME_UNIT_SEGMENT;
		} else if (!strcasecmp(timescale, "message")) {
			_volumeunit = AST_AOC_VOLUME_UNIT_MESSAGE;
		}else {
			astman_send_error(s, m, "Invalid VolumeUnit");
			goto aocmessage_cleanup;
		}
	}

	if (_chargeditem == AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT
			|| _ratetype == AST_AOC_RATE_TYPE_SPECIAL_CODE) {
		if (ast_strlen_zero(code) || (sscanf(code, "%30u", &_code) != 1)) {
			astman_send_error(s, m, "Invalid Code, Code is a required when ChargedItem is SpecialArrangement and when RateType is SpecialCode");
			goto aocmessage_cleanup;
		}
	}

	if (_chargeditem == AST_AOC_CHARGED_ITEM_SPECIAL_ARRANGEMENT) {
		ast_aoc_s_add_special_arrangement(decoded, _code);
	} else if (_ratetype == AST_AOC_RATE_TYPE_DURATION) {
		ast_aoc_s_add_rate_duration(decoded, _chargeditem, _currencyamount, _mult,
			currencyname, _time, _scale, _granularity, _granularity_time_scale, _step);
	} else if (_ratetype == AST_AOC_RATE_TYPE_FLAT) {
		ast_aoc_s_add_rate_flat(decoded, _chargeditem, _currencyamount, _mult,
				currencyname);
	} else if (_ratetype == AST_AOC_RATE_TYPE_VOLUME) {
		ast_aoc_s_add_rate_volume(decoded, _chargeditem, _volumeunit, _currencyamount,
			_mult, currencyname);
	} else if (_ratetype == AST_AOC_RATE_TYPE_SPECIAL_CODE) {
		ast_aoc_s_add_rate_special_charge_code(decoded, _chargeditem, _code);
	} else if (_ratetype == AST_AOC_RATE_TYPE_FREE) {
		ast_aoc_s_add_rate_free(decoded, _chargeditem, 0);
	} else if (_ratetype == AST_AOC_RATE_TYPE_FREE_FROM_BEGINNING) {
		ast_aoc_s_add_rate_free(decoded, _chargeditem, 1);
	} else if (_ratetype == AST_AOC_RATE_TYPE_NA) {
		ast_aoc_s_add_rate_na(decoded, _chargeditem);
	}

	return 0;

aocmessage_cleanup:

	return -1;
}

static struct ast_aoc_decoded *action_aoc_s_message(struct mansession *s,
		const struct message *m)
{
	struct ast_aoc_decoded *decoded = NULL;
	int hdrlen;
	int x;
	static const char hdr[] = "ChargedItem:";
	struct message sm = { 0 };
	int rates = 0;

	if (!(decoded = ast_aoc_create(AST_AOC_S, 0, 0))) {
		astman_send_error(s, m, "Message Creation Failed");
		goto aocmessage_cleanup;
	}

	hdrlen = strlen(hdr);
	for (x = 0; x < m->hdrcount; x++) {
		if (strncasecmp(hdr, m->headers[x], hdrlen) == 0) {
			if (rates > ast_aoc_s_get_count(decoded)) {
				if (action_aoc_s_submessage(s, &sm, decoded) == -1) {
					goto aocmessage_cleanup;
				}
			}
			++rates;
		}

		sm.headers[sm.hdrcount] = m->headers[x];
		++sm.hdrcount;
	}
	if (rates > ast_aoc_s_get_count(decoded)) {
		if (action_aoc_s_submessage(s, &sm, decoded) == -1) {
			goto aocmessage_cleanup;
		}
	}

	return decoded;

aocmessage_cleanup:

	ast_aoc_destroy_decoded(decoded);
	return NULL;
}

static int action_aocmessage(struct mansession *s, const struct message *m)
{
	const char *msgtype = astman_get_header(m, "MsgType");
	const char *channel = astman_get_header(m, "Channel");
	const char *pchannel = astman_get_header(m, "ChannelPrefix");

	struct ast_channel *chan = NULL;

	struct ast_aoc_decoded *decoded = NULL;
	struct ast_aoc_encoded *encoded = NULL;
	size_t encoded_size = 0;

	if (ast_strlen_zero(channel) && ast_strlen_zero(pchannel)) {
		astman_send_error(s, m, "Channel and PartialChannel are not specified. Specify at least one of these.");
		goto aocmessage_cleanup;
	}

	if (!(chan = ast_channel_get_by_name(channel)) && !ast_strlen_zero(pchannel)) {
		chan = ast_channel_get_by_name_prefix(pchannel, strlen(pchannel));
	}

	if (!chan) {
		astman_send_error(s, m, "No such channel");
		goto aocmessage_cleanup;
	}

	if (strcasecmp(msgtype, "d") == 0 || strcasecmp(msgtype, "e") == 0) {
		decoded = action_aoc_de_message(s, m);
	}
	else if (strcasecmp(msgtype, "s") == 0) {
		decoded = action_aoc_s_message(s, m);
	}
	else {
		astman_send_error(s, m, "Invalid MsgType");
		goto aocmessage_cleanup;
	}

	if (!decoded) {
		goto aocmessage_cleanup;
	}

	if ((encoded = ast_aoc_encode(decoded, &encoded_size, chan))
			&& !ast_indicate_data(chan, AST_CONTROL_AOC, encoded, encoded_size)) {
		astman_send_ack(s, m, "AOC Message successfully queued on channel");
	} else {
		astman_send_error(s, m, "Error encoding AOC message, could not queue onto channel");
	}

aocmessage_cleanup:

	ast_aoc_destroy_decoded(decoded);
	ast_aoc_destroy_encoded(encoded);

	if (chan) {
		chan = ast_channel_unref(chan);
	}
	return 0;
}

struct originate_permissions_entry {
	const char *search;
	int permission;
	int (*searchfn)(const char *app, const char *data, const char *search);
};

/*!
 * \internal
 * \brief Check if the application is allowed for Originate
 *
 * \param app The "app" parameter
 * \param data The "appdata" parameter (ignored)
 * \param search The search string
 * \retval 1 Match
 * \retval 0 No match
 */
static int app_match(const char *app, const char *data, const char *search)
{
	/*
	 * We use strcasestr so we don't have to trim any blanks
	 * from the front or back of the string.
	 */
	return !!(strcasestr(app, search));
}

/*!
 * \internal
 * \brief Check if the appdata is allowed for Originate
 *
 * \param app The "app" parameter (ignored)
 * \param data The "appdata" parameter
 * \param search The search string
 * \retval 1 Match
 * \retval 0 No match
 */
static int appdata_match(const char *app, const char *data, const char *search)
{
	if (ast_strlen_zero(data)) {
		return 0;
	}
	return !!(strstr(data, search));
}

/*!
 * \internal
 * \brief Check if the Queue application is allowed for Originate
 *
 * It's only allowed if there's no AGI parameter set
 *
 * \param app The "app" parameter
 * \param data The "appdata" parameter
 * \param search The search string
 * \retval 1 Match
 * \retval 0 No match
 */
static int queue_match(const char *app, const char *data, const char *search)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(queuename);
		AST_APP_ARG(options);
		AST_APP_ARG(url);
		AST_APP_ARG(announceoverride);
		AST_APP_ARG(queuetimeoutstr);
		AST_APP_ARG(agi);
		AST_APP_ARG(gosub);
		AST_APP_ARG(rule);
		AST_APP_ARG(position);
	);

	if (!strcasestr(app, "queue") || ast_strlen_zero(data)) {
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	/*
	 * The Queue application is fine unless the AGI parameter is set.
	 * If it is, we need to check the user's permissions.
	 */
	return !ast_strlen_zero(args.agi);
}

/*
 * The Originate application and application data are passed
 * to each searchfn in the list.  If a searchfn returns true
 * and the user's permissions don't include the permissions specified
 * in the list entry, the Originate action will be denied.
 *
 * If no searchfn returns true, the Originate action is allowed.
 */
static struct originate_permissions_entry originate_app_permissions[] = {
	/*
	 * The app_match function checks if the search string is
	 * anywhere in the app parameter.  The check is case-insensitive.
	 */
	{ "agi", EVENT_FLAG_SYSTEM, app_match },
	{ "dbdeltree", EVENT_FLAG_SYSTEM, app_match },
	{ "exec", EVENT_FLAG_SYSTEM, app_match },
	{ "externalivr", EVENT_FLAG_SYSTEM, app_match },
	{ "mixmonitor", EVENT_FLAG_SYSTEM, app_match },
	{ "originate", EVENT_FLAG_SYSTEM, app_match },
	{ "reload", EVENT_FLAG_SYSTEM, app_match },
	{ "system", EVENT_FLAG_SYSTEM, app_match },
	/*
	 * Since the queue_match function specifically checks
	 * for the presence of the AGI parameter, we'll allow
	 * the call if the user has either the AGI or SYSTEM
	 * permission.
	 */
	{ "queue", EVENT_FLAG_AGI | EVENT_FLAG_SYSTEM, queue_match },
	/*
	 * The appdata_match function checks if the search string is
	 * anywhere in the appdata parameter.  Unlike app_match,
	 * the check is case-sensitive.  These are generally
	 * dialplan functions.
	 */
	{ "CURL", EVENT_FLAG_SYSTEM, appdata_match },
	{ "DB", EVENT_FLAG_SYSTEM, appdata_match },
	{ "EVAL", EVENT_FLAG_SYSTEM, appdata_match },
	{ "FILE", EVENT_FLAG_SYSTEM, appdata_match },
	{ "ODBC", EVENT_FLAG_SYSTEM, appdata_match },
	{ "REALTIME", EVENT_FLAG_SYSTEM, appdata_match },
	{ "SHELL", EVENT_FLAG_SYSTEM, appdata_match },
	{ NULL, 0 },
};

static int is_originate_app_permitted(const char *app, const char *data,
	int permission)
{
	int i;

	for (i = 0; originate_app_permissions[i].search; i++) {
		if (originate_app_permissions[i].searchfn(app, data, originate_app_permissions[i].search)) {
			return !!(permission & originate_app_permissions[i].permission);
		}
	}

	return 1;
}

#ifdef TEST_FRAMEWORK
#define ALL_PERMISSIONS (INT_MAX)
#define NO_PERMISSIONS (0)
AST_TEST_DEFINE(originate_permissions_test)
{
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "originate_permissions_test";
		info->category = "/main/manager/";
		info->summary = "Test permissions for originate action";
		info->description =
			"Make sure that dialplan apps/functions that need special "
			"permissions are prohibited if the user doesn't have the permission.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/*
	 * Check application matching. We don't need to check every one.
	 * The code is the same.
	 */

	ast_test_validate(test, is_originate_app_permitted("exec",
		NULL, EVENT_FLAG_SYSTEM), "exec permission check failed");
	ast_test_validate(test, is_originate_app_permitted("exec",
		NULL, EVENT_FLAG_SYSTEM | EVENT_FLAG_AGI), "exec check permission failed");
	ast_test_validate(test, is_originate_app_permitted("exec",
		NULL, ALL_PERMISSIONS), "exec check permission failed");
	ast_test_validate(test, !is_originate_app_permitted("exec",
		NULL, EVENT_FLAG_AGI), "exec permission check failed");
	ast_test_validate(test, !is_originate_app_permitted("exec",
		NULL, EVENT_FLAG_VERBOSE), "exec permission check failed");
	ast_test_validate(test, !is_originate_app_permitted("exec",
		NULL, NO_PERMISSIONS), "exec permission check failed");

	/*
	 * If queue is used with the AGI parameter but without the SYSTEM or AGI
	 * permission, it should be denied. Queue param order:
	 * queuename,options,url,announceoverride,queuetimeoutstr,AGI,gosub,rule,position
	 * The values of the options aren't checked. They just have to be present.
	 */

	/* AGI not specified should always be allowed */
	ast_test_validate(test, is_originate_app_permitted("queue",
		NULL, NO_PERMISSIONS), "Queue permission check failed");
	ast_test_validate(test, is_originate_app_permitted("queue",
		"somequeue,CcdHh,someURL,tt-monkeys,100,,gosub,rule,666",
		EVENT_FLAG_ORIGINATE | EVENT_FLAG_HOOKRESPONSE ), "Queue permission check failed");

	/* AGI specified with SYSTEM or AGI permission should be allowed */
	ast_test_validate(test, is_originate_app_permitted("queue",
		"somequeue,CcdHh,someURL,tt-monkeys,100,SomeAGIScript,gosub,rule,666",
		EVENT_FLAG_SYSTEM | EVENT_FLAG_HOOKRESPONSE ), "Queue permission check failed");
	ast_test_validate(test, is_originate_app_permitted("queue",
		"somequeue,CcdHh,someURL,tt-monkeys,100,SomeAGIScript,gosub,rule,666",
		EVENT_FLAG_AGI | EVENT_FLAG_HOOKRESPONSE ), "Queue permission check failed");
	ast_test_validate(test, is_originate_app_permitted("queue",
		"somequeue,CcdHh,someURL,tt-monkeys,100,SomeAGIScript,gosub,rule,666",
		ALL_PERMISSIONS), "Queue permission check failed");

	/* AGI specified without SYSTEM or AGI permission should be denied */
	ast_test_validate(test, !is_originate_app_permitted("queue",
		"somequeue,CcdHh,someURL,tt-monkeys,100,SomeAGIScript,gosub,rule,666",
		NO_PERMISSIONS), "Queue permission check failed");
	ast_test_validate(test, !is_originate_app_permitted("queue",
		"somequeue,CcdHh,someURL,tt-monkeys,100,SomeAGIScript,gosub,rule,666",
		EVENT_FLAG_ORIGINATE | EVENT_FLAG_HOOKRESPONSE ), "Queue permission check failed");

	/*
	 * Check appdata.  The function name can appear anywhere in appdata.
	 */
	ast_test_validate(test, is_originate_app_permitted("someapp",
		"aaaDBbbb", EVENT_FLAG_SYSTEM), "exec permission check failed");
	ast_test_validate(test, is_originate_app_permitted("someapp",
		"aaa DB bbb", ALL_PERMISSIONS), "exec permission check failed");
	ast_test_validate(test, !is_originate_app_permitted("someapp",
		"aaaDBbbb", NO_PERMISSIONS), "exec permission check failed");
	ast_test_validate(test, !is_originate_app_permitted("someapp",
		"aaa DB bbb", NO_PERMISSIONS), "exec permission check failed");
	/* The check is case-sensitive so although DB is a match, db isn't. */
	ast_test_validate(test, is_originate_app_permitted("someapp",
		"aaa db bbb", NO_PERMISSIONS), "exec permission check failed");

	return res;
}
#undef ALL_PERMISSIONS
#undef NO_PERMISSIONS
#endif

static int action_originate(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	const char *priority = astman_get_header(m, "Priority");
	const char *timeout = astman_get_header(m, "Timeout");
	const char *callerid = astman_get_header(m, "CallerID");
	const char *account = astman_get_header(m, "Account");
	const char *app = astman_get_header(m, "Application");
	const char *appdata = astman_get_header(m, "Data");
	const char *async = astman_get_header(m, "Async");
	const char *id = astman_get_header(m, "ActionID");
	const char *codecs = astman_get_header(m, "Codecs");
	const char *early_media = astman_get_header(m, "Earlymedia");
	struct ast_assigned_ids assignedids = {
		.uniqueid = astman_get_header(m, "ChannelId"),
		.uniqueid2 = astman_get_header(m, "OtherChannelId"),
	};
	const char *gosub = astman_get_header(m, "PreDialGoSub");

	struct ast_variable *vars = NULL;
	char *tech, *data;
	char *l = NULL, *n = NULL;
	int pi = 0;
	int res;
	int to = 30000;
	int reason = 0;
	char tmp[256];
	char tmp2[256];
	struct ast_format_cap *cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	pthread_t th;
	int bridge_early = 0;

	if (!cap) {
		astman_send_error(s, m, "Internal Error. Memory allocation failure.");
		return 0;
	}
	ast_format_cap_append(cap, ast_format_slin, 0);

	if ((assignedids.uniqueid && AST_MAX_PUBLIC_UNIQUEID < strlen(assignedids.uniqueid))
		|| (assignedids.uniqueid2 && AST_MAX_PUBLIC_UNIQUEID < strlen(assignedids.uniqueid2))) {
		astman_send_error_va(s, m, "Uniqueid length exceeds maximum of %d\n",
			AST_MAX_PUBLIC_UNIQUEID);
		res = 0;
		goto fast_orig_cleanup;
	}

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "Channel not specified");
		res = 0;
		goto fast_orig_cleanup;
	}
	if (!ast_strlen_zero(priority) && (sscanf(priority, "%30d", &pi) != 1)) {
		if ((pi = ast_findlabel_extension(NULL, context, exten, priority, NULL)) < 1) {
			astman_send_error(s, m, "Invalid priority");
			res = 0;
			goto fast_orig_cleanup;
		}
	}
	if (!ast_strlen_zero(timeout) && (sscanf(timeout, "%30d", &to) != 1)) {
		astman_send_error(s, m, "Invalid timeout");
		res = 0;
		goto fast_orig_cleanup;
	}
	ast_copy_string(tmp, name, sizeof(tmp));
	tech = tmp;
	data = strchr(tmp, '/');
	if (!data) {
		astman_send_error(s, m, "Invalid channel");
		res = 0;
		goto fast_orig_cleanup;
	}
	*data++ = '\0';
	ast_copy_string(tmp2, callerid, sizeof(tmp2));
	ast_callerid_parse(tmp2, &n, &l);
	if (n) {
		if (ast_strlen_zero(n)) {
			n = NULL;
		}
	}
	if (l) {
		ast_shrink_phone_number(l);
		if (ast_strlen_zero(l)) {
			l = NULL;
		}
	}
	if (!ast_strlen_zero(codecs)) {
		ast_format_cap_remove_by_type(cap, AST_MEDIA_TYPE_UNKNOWN);
		ast_format_cap_update_by_allow_disallow(cap, codecs, 1);
	}

	if (!ast_strlen_zero(app) && s->session) {
		if (!is_originate_app_permitted(app, appdata, s->session->writeperm)) {
			astman_send_error(s, m, "Originate Access Forbidden: app or data blacklisted");
			res = 0;
			goto fast_orig_cleanup;
		}
	}

	/* Check early if the extension exists. If not, we need to bail out here. */
	if (exten && context && pi) {
		if (! ast_exists_extension(NULL, context, exten, pi, l)) {
			/* The extension does not exist. */
			astman_send_error(s, m, "Extension does not exist.");
			res = 0;
			goto fast_orig_cleanup;
		}
	}

	/* Allocate requested channel variables */
	vars = astman_get_variables(m);
	if (s->session && s->session->chanvars) {
		struct ast_variable *v, *old;
		old = vars;
		vars = NULL;

		/* The variables in the AMI originate action are appended at the end of the list, to override any user variables that apply */

		vars = ast_variables_dup(s->session->chanvars);
		if (old) {
			for (v = vars; v->next; v = v->next );
			v->next = old;	/* Append originate variables at end of list */
		}
	}

	/* For originate async - we can bridge in early media stage */
	bridge_early = ast_true(early_media);

	if (ast_true(async)) {
		struct fast_originate_helper *fast;

		fast = ast_calloc(1, sizeof(*fast));
		if (!fast || ast_string_field_init(fast, 252)) {
			ast_free(fast);
			ast_variables_destroy(vars);
			res = -1;
		} else {
			if (!ast_strlen_zero(id)) {
				ast_string_field_build(fast, idtext, "ActionID: %s\r\n", id);
			}
			ast_string_field_set(fast, tech, tech);
			ast_string_field_set(fast, data, data);
			ast_string_field_set(fast, app, app);
			ast_string_field_set(fast, appdata, appdata);
			ast_string_field_set(fast, cid_num, l);
			ast_string_field_set(fast, cid_name, n);
			ast_string_field_set(fast, context, context);
			ast_string_field_set(fast, exten, exten);
			ast_string_field_set(fast, account, account);
			ast_string_field_set(fast, channelid, assignedids.uniqueid);
			ast_string_field_set(fast, otherchannelid, assignedids.uniqueid2);
			fast->vars = vars;
			fast->cap = cap;
			cap = NULL; /* transferred originate helper the capabilities structure.  It is now responsible for freeing it. */
			fast->timeout = to;
			fast->early_media = bridge_early;
			fast->priority = pi;
			if (ast_pthread_create_detached(&th, NULL, fast_originate, fast)) {
				destroy_fast_originate_helper(fast);
				res = -1;
			} else {
				res = 0;
			}
		}
	} else if (!ast_strlen_zero(app)) {
		res = ast_pbx_outgoing_app(tech, cap, data, to, app, appdata, &reason,
				AST_OUTGOING_WAIT, l, n, vars, account, NULL,
				assignedids.uniqueid ? &assignedids : NULL);
		ast_variables_destroy(vars);
	} else {
		if (exten && context && pi) {
			res = ast_pbx_outgoing_exten_predial(tech, cap, data, to,
					context, exten, pi, &reason, AST_OUTGOING_WAIT,
					l, n, vars, account, NULL, bridge_early,
					assignedids.uniqueid ? &assignedids : NULL , gosub);
			ast_variables_destroy(vars);
		} else {
			astman_send_error(s, m, "Originate with 'Exten' requires 'Context' and 'Priority'");
			ast_variables_destroy(vars);
			res = 0;
			goto fast_orig_cleanup;
		}
	}
	if (!res) {
		astman_send_ack(s, m, "Originate successfully queued");
	} else {
		astman_send_error(s, m, "Originate failed");
	}

fast_orig_cleanup:
	ao2_cleanup(cap);
	return 0;
}

static int action_mailboxstatus(struct mansession *s, const struct message *m)
{
	const char *mailbox = astman_get_header(m, "Mailbox");
	int ret;

	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	ret = ast_app_has_voicemail(mailbox, NULL);
	astman_start_ack(s, m);
	astman_append(s, "Message: Mailbox Status\r\n"
			 "Mailbox: %s\r\n"
			 "Waiting: %d\r\n\r\n", mailbox, ret);
	return 0;
}

static int action_mailboxcount(struct mansession *s, const struct message *m)
{
	const char *mailbox = astman_get_header(m, "Mailbox");
	int newmsgs = 0, oldmsgs = 0, urgentmsgs = 0;;

	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	ast_app_inboxcount2(mailbox, &urgentmsgs, &newmsgs, &oldmsgs);
	astman_start_ack(s, m);
	astman_append(s,   "Message: Mailbox Message Count\r\n"
			   "Mailbox: %s\r\n"
			   "UrgMessages: %d\r\n"
			   "NewMessages: %d\r\n"
			   "OldMessages: %d\r\n"
			   "\r\n",
			   mailbox, urgentmsgs, newmsgs, oldmsgs);
	return 0;
}

static int action_extensionstate(struct mansession *s, const struct message *m)
{
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	char hint[256];
	int status;

	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "Extension not specified");
		return 0;
	}
	if (ast_strlen_zero(context)) {
		context = "default";
	}
	status = ast_extension_state(NULL, context, exten);
	hint[0] = '\0';
	ast_get_hint(hint, sizeof(hint), NULL, 0, NULL, context, exten);
	astman_start_ack(s, m);
	astman_append(s, "Message: Extension Status\r\n"
		"Exten: %s\r\n"
		"Context: %s\r\n"
		"Hint: %s\r\n"
		"Status: %d\r\n"
		"StatusText: %s\r\n"
		"\r\n",
		exten, context, hint, status,
		ast_extension_state2str(status));
	return 0;
}

static int action_presencestate(struct mansession *s, const struct message *m)
{
	const char *provider = astman_get_header(m, "Provider");
	enum ast_presence_state state;
	char *subtype;
	char *message;

	if (ast_strlen_zero(provider)) {
		astman_send_error(s, m, "No provider specified");
		return 0;
	}

	state = ast_presence_state(provider, &subtype, &message);
	if (state == AST_PRESENCE_INVALID) {
		astman_send_error_va(s, m, "Invalid provider %s or provider in invalid state", provider);
		return 0;
	}

	astman_start_ack(s, m);
	astman_append(s, "Message: Presence State\r\n"
	                 "State: %s\r\n", ast_presence_state2str(state));

	if (!ast_strlen_zero(subtype)) {
		astman_append(s, "Subtype: %s\r\n", subtype);
	}

	if (!ast_strlen_zero(message)) {
		/* XXX The Message header here is deprecated as it
		 * duplicates the action response header 'Message'.
		 * Remove it in the next major revision of AMI.
		 */
		astman_append(s, "Message: %s\r\n"
		                 "PresenceMessage: %s\r\n",
		                 message, message);
	}
	astman_append(s, "\r\n");

	ast_free(subtype);
	ast_free(message);

	return 0;
}

static int action_timeout(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	double timeout = atof(astman_get_header(m, "Timeout"));
	struct timeval when = { timeout, 0 };

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (!timeout || timeout < 0) {
		astman_send_error(s, m, "No timeout specified");
		return 0;
	}

	if (!(c = ast_channel_get_by_name(name))) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	when.tv_usec = (timeout - when.tv_sec) * 1000000.0;

	ast_channel_lock(c);
	ast_channel_setwhentohangup_tv(c, when);
	ast_channel_unlock(c);
	c = ast_channel_unref(c);

	astman_send_ack(s, m, "Timeout Set");

	return 0;
}

/*!
 * \brief Test eventdata against a filter entry
 *
 * \param entry The event_filter entry to match with
 * \param eventdata  The data to match against
 * \retval 0 if no match
 * \retval 1 if match
 */
static int match_eventdata(struct event_filter_entry *entry, const char *eventdata)
{
	switch(entry->match_type) {
	case FILTER_MATCH_REGEX:
		return regexec(entry->regex_filter, eventdata, 0, NULL, 0) == 0;
	case FILTER_MATCH_STARTS_WITH:
		return ast_begins_with(eventdata, entry->string_filter);
	case FILTER_MATCH_ENDS_WITH:
		return ast_ends_with(eventdata, entry->string_filter);
	case FILTER_MATCH_CONTAINS:
		return strstr(eventdata, entry->string_filter) != NULL;
	case FILTER_MATCH_EXACT:
		return strcmp(eventdata, entry->string_filter) == 0;
	case FILTER_MATCH_NONE:
		return 1;
	}

	return 0;
}

static int filter_cmp_fn(void *obj, void *arg, void *data, int flags)
{
	struct eventqent *eqe = arg;
	struct event_filter_entry *filter_entry = obj;
	char *line_buffer_start = NULL;
	char *line_buffer = NULL;
	char *line = NULL;
	int match = 0;
	int *result = data;

	if (filter_entry->event_name_hash) {
		if (eqe->event_name_hash != filter_entry->event_name_hash) {
			goto done;
		}
	}

	/* We're looking at the entire event data */
	if (!filter_entry->header_name) {
		match = match_eventdata(filter_entry, eqe->eventdata);
		goto done;
	}

	/* We're looking at a specific header */
	line_buffer_start = ast_strdup(eqe->eventdata);
	line_buffer = line_buffer_start;
	if (!line_buffer_start) {
		goto done;
	}

	while ((line = ast_read_line_from_buffer(&line_buffer))) {
		if (ast_begins_with(line, filter_entry->header_name)) {
			line += strlen(filter_entry->header_name);
			line = ast_skip_blanks(line);
			if (ast_strlen_zero(line)) {
				continue;
			}
			match = match_eventdata(filter_entry, line);
			if (match) {
				ast_free(line_buffer_start);
				line_buffer_start = NULL;
				break;
			}
		}
	}

	ast_free(line_buffer_start);

done:

	*result = match;
	return match ? CMP_MATCH | CMP_STOP : 0;
}

static int should_send_event(struct ao2_container *includefilters,
	struct ao2_container *excludefilters, struct eventqent *eqe)
{
	int result = 0;

	if (manager_debug) {
		ast_verbose("<-- Examining AMI event (%u): -->\n%s\n", eqe->event_name_hash, eqe->eventdata);
	} else {
		ast_debug(4, "Examining AMI event (%u):\n%s\n", eqe->event_name_hash, eqe->eventdata);
	}
	if (!ao2_container_count(includefilters) && !ao2_container_count(excludefilters)) {
		return 1; /* no filtering means match all */
	} else if (ao2_container_count(includefilters) && !ao2_container_count(excludefilters)) {
		/* include filters only: implied exclude all filter processed first, then include filters */
		ao2_t_callback_data(includefilters, OBJ_NODATA, filter_cmp_fn, eqe, &result, "find filter in includefilters container");
		return result;
	} else if (!ao2_container_count(includefilters) && ao2_container_count(excludefilters)) {
		/* exclude filters only: implied include all filter processed first, then exclude filters */
		ao2_t_callback_data(excludefilters, OBJ_NODATA, filter_cmp_fn, eqe, &result, "find filter in excludefilters container");
		return !result;
	} else {
		/* include and exclude filters: implied exclude all filter processed first, then include filters, and lastly exclude filters */
		ao2_t_callback_data(includefilters, OBJ_NODATA, filter_cmp_fn, eqe, &result, "find filter in session filter container");
		if (result) {
			result = 0;
			ao2_t_callback_data(excludefilters, OBJ_NODATA, filter_cmp_fn, eqe, &result, "find filter in session filter container");
			return !result;
		}
	}

	return result;
}

/*!
 * \brief Manager command to add an event filter to a manager session
 * \see For more details look at manager_add_filter
 */
static int action_filter(struct mansession *s, const struct message *m)
{
	const char *match_criteria = astman_get_header(m, "MatchCriteria");
	const char *filter = astman_get_header(m, "Filter");
	const char *operation = astman_get_header(m, "Operation");
	int res;

	if (!strcasecmp(operation, "Add")) {
		char *criteria;
		int have_match = !ast_strlen_zero(match_criteria);

		/* Create an eventfilter expression.
		 * eventfilter[(match_criteria)]
		 */
		res = ast_asprintf(&criteria, "eventfilter%s%s%s",
			S_COR(have_match, "(", ""), S_OR(match_criteria, ""),
			S_COR(have_match, ")", ""));
		if (res <= 0) {
			astman_send_error(s, m, "Internal Error. Failed to allocate storage for filter type");
			return 0;
		}

		res = manager_add_filter(criteria, filter, s->session->includefilters, s->session->excludefilters);
		ast_std_free(criteria);
		if (res != FILTER_SUCCESS) {
			if (res == FILTER_ALLOC_FAILED) {
				astman_send_error(s, m, "Internal Error. Failed to allocate regex for filter");
				return 0;
			} else if (res == FILTER_COMPILE_FAIL) {
				astman_send_error(s, m,
					"Filter did not compile.  Check the syntax of the filter given.");
				return 0;
			} else if (res == FILTER_FORMAT_ERROR) {
				astman_send_error(s, m,
					"Filter was formatted incorrectly.  Check the syntax of the filter given.");
				return 0;
			} else {
				astman_send_error(s, m, "Internal Error. Failed adding filter.");
				return 0;
			}
		}

		astman_send_ack(s, m, "Success");
		return 0;
	}

	astman_send_error(s, m, "Unknown operation");
	return 0;
}

/*!
 * \brief Add an event filter to a manager session
 *
 * \param criteria See examples in manager.conf.sample
 * \param filter_pattern  Filter pattern
 * \param includefilters, excludefilters
 *
 * \return FILTER_ALLOC_FAILED   Memory allocation failure
 * \return FILTER_COMPILE_FAIL   If the filter did not compile
 * \return FILTER_FORMAT_ERROR   If the criteria weren't formatted correctly
 * \return FILTER_SUCCESS        Success
 *
 *
 * Examples:
 * See examples in manager.conf.sample
 *
 */
static enum add_filter_result manager_add_filter(
	const char *criteria, const char *filter_pattern,
	struct ao2_container *includefilters, struct ao2_container *excludefilters)
{
	RAII_VAR(struct event_filter_entry *, filter_entry,
		ao2_t_alloc(sizeof(*filter_entry), event_filter_destructor, "event_filter allocation"),
		ao2_cleanup);
	char *options_start = NULL;
	SCOPE_ENTER(3, "manager_add_filter(%s, %s, %p, %p)", criteria, filter_pattern, includefilters, excludefilters);

	if (!filter_entry) {
		SCOPE_EXIT_LOG_RTN_VALUE(FILTER_ALLOC_FAILED, LOG_WARNING, "Unable to allocate filter_entry");
	}

	/*
	 * At a minimum, criteria must be "eventfilter" but may contain additional
	 * constraints.
	 */
	if (ast_strlen_zero(criteria)) {
		SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING, "Missing criteria");
	}

	/*
	 * filter_pattern could be empty but it should never be NULL.
	 */
	if (!filter_pattern) {
		SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING, "Filter pattern was NULL");
	}

	/*
	 * For a legacy filter, if the first character of filter_pattern is
	 * '!' then it's an exclude filter.  It's also accepted as an alternative
	 * to specifying "action(exclude)" for an advanced filter.  If
	 * "action" is specified however, it will take precedence.
	 */
	if (filter_pattern[0] == '!') {
		filter_entry->is_excludefilter = 1;
		filter_pattern++;
	}

	/*
	 * This is the default
	 */
	filter_entry->match_type = FILTER_MATCH_REGEX;

	/*
	 * If the criteria has a '(' in it, then it's an advanced filter.
	 */
	options_start = strstr(criteria, "(");

	/*
	 * If it's a legacy filter, there MUST be a filter pattern.
	 */
	if (!options_start && ast_strlen_zero(filter_pattern)) {
		SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING,
			"'%s = %s': Legacy filter with no filter pattern specified\n",
			criteria, filter_pattern);
	}

	if (options_start) {
		/*
		 * This is an advanced filter
		 */
		char *temp = ast_strdupa(options_start + 1); /* skip over the leading '(' */
		char *saveptr = NULL;
		char *option = NULL;
		enum found_options {
			action_found = (1 << 0),
			name_found = (1 << 1),
			header_found = (1 << 2),
			method_found = (1 << 3),
		};
		enum found_options options_found = 0;

		filter_entry->match_type = FILTER_MATCH_NONE;

		ast_strip(temp);
		if (ast_strlen_zero(temp) || !ast_ends_with(temp, ")")) {
			SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING,
				"'%s = %s': Filter options not formatted correctly\n",
				criteria, filter_pattern);
		}

		/*
		 * These can actually be in any order...
		 * action(include|exclude),name(<event_name>),header(<header_name>),method(<match_method>)
		 * At least one of action, name, or header is required.
		 */
		while ((option = strtok_r(temp, " ,)", &saveptr))) {
			if (!strncmp(option, "action", 6)) {
				char *method = strstr(option, "(");
				if (ast_strlen_zero(method)) {
					SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING, "'%s = %s': 'action' parameter not formatted correctly\n",
						criteria, filter_pattern);
				}
				method++;
				ast_strip(method);
				if (!strcmp(method, "include")) {
					filter_entry->is_excludefilter = 0;
				} else if (!strcmp(method, "exclude")) {
					filter_entry->is_excludefilter = 1;
				} else {
					SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING, "'%s = %s': 'action' option '%s' is unknown\n",
						criteria, filter_pattern, method);
				}
				options_found |= action_found;
			} else if (!strncmp(option, "name", 4)) {
				char *event_name = strstr(option, "(");
				event_name++;
				ast_strip(event_name);
				if (ast_strlen_zero(event_name)) {
					SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING, "'%s = %s': 'name' parameter not formatted correctly\n",
						criteria, filter_pattern);
				}
				filter_entry->event_name = ast_strdup(event_name);
				filter_entry->event_name_hash = ast_str_hash(event_name);
				options_found |= name_found;
			} else if (!strncmp(option, "header", 6)) {
				char *header_name = strstr(option, "(");
				header_name++;
				ast_strip(header_name);
				if (ast_strlen_zero(header_name)) {
					SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING, "'%s = %s': 'header' parameter not formatted correctly\n",
						criteria, filter_pattern);
				}
				if (!ast_ends_with(header_name, ":")) {
					filter_entry->header_name = ast_malloc(strlen(header_name) + 2);
					if (!filter_entry->header_name) {
						SCOPE_EXIT_LOG_RTN_VALUE(FILTER_ALLOC_FAILED, LOG_ERROR, "Unable to allocate memory for header_name");
					}
					sprintf(filter_entry->header_name, "%s:", header_name); /* Safe */
				} else {
					filter_entry->header_name = ast_strdup(header_name);
				}
				options_found |= header_found;
			} else if (!strncmp(option, "method", 6)) {
				char *method = strstr(option, "(");
				method++;
				ast_strip(method);
				if (ast_strlen_zero(method)) {
					SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING, "'%s = %s': 'method' parameter not formatted correctly\n",
						criteria, filter_pattern);
				}
				if (!strcmp(method, "regex")) {
					filter_entry->match_type = FILTER_MATCH_REGEX;
				} else if (!strcmp(method, "exact")) {
					filter_entry->match_type = FILTER_MATCH_EXACT;
				} else if (!strcmp(method, "starts_with")) {
					filter_entry->match_type = FILTER_MATCH_STARTS_WITH;
				} else if (!strcmp(method, "ends_with")) {
					filter_entry->match_type = FILTER_MATCH_ENDS_WITH;
				} else if (!strcmp(method, "contains")) {
					filter_entry->match_type = FILTER_MATCH_CONTAINS;
				} else if (!strcmp(method, "none")) {
					filter_entry->match_type = FILTER_MATCH_NONE;
				} else {
					SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING, "'%s = %s': 'method' option '%s' is unknown\n",
						criteria, filter_pattern, method);
				}
				options_found |= method_found;
			} else {
				SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING, "'%s = %s': Filter option '%s' is unknown\n",
					criteria, filter_pattern, option);
			}
			temp = NULL;
		}
		if (!options_found) {
			SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING,
				"'%s = %s': No action, name, header, or method option found\n",
				criteria, filter_pattern);
		}
		if (ast_strlen_zero(filter_pattern) && filter_entry->match_type != FILTER_MATCH_NONE) {
			SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING,
				"'%s = %s': method can't be '%s' with no filter pattern\n",
				criteria, filter_pattern, match_type_names[filter_entry->match_type]);
		}
		if (!ast_strlen_zero(filter_pattern) && filter_entry->match_type == FILTER_MATCH_NONE) {
			SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING,
				"'%s = %s': method can't be 'none' with a filter pattern\n",
				criteria, filter_pattern);
		}
		if (!(options_found & name_found) && !(options_found & header_found) &&
			filter_entry->match_type == FILTER_MATCH_NONE) {
			SCOPE_EXIT_LOG_RTN_VALUE(FILTER_FORMAT_ERROR, LOG_WARNING,
				"'%s = %s': No name or header option found and no filter pattern\n",
				criteria, filter_pattern);
		}
	}

	if (!ast_strlen_zero(filter_pattern)) {
		if (filter_entry->match_type == FILTER_MATCH_REGEX) {
			filter_entry->regex_filter = ast_calloc(1, sizeof(regex_t));
			if (!filter_entry->regex_filter) {
				SCOPE_EXIT_LOG_RTN_VALUE(FILTER_ALLOC_FAILED, LOG_ERROR, "Unable to allocate memory for regex_filter");
			}
			if (regcomp(filter_entry->regex_filter, filter_pattern, REG_EXTENDED | REG_NOSUB)) {
				SCOPE_EXIT_LOG_RTN_VALUE(FILTER_COMPILE_FAIL, LOG_WARNING, "Unable to compile regex filter for '%s'", filter_pattern);
			}
		} else {
			filter_entry->string_filter = ast_strdup(filter_pattern);
		}
	}

	ast_debug(2, "Event filter:\n"
		"conf entry: %s = %s\n"
		"event_name: %s (hash: %d)\n"
		"test_header:  %s\n"
		"match_type: %s\n"
		"regex_filter: %p\n"
		"string filter: %s\n"
		"is excludefilter: %d\n",
		criteria, filter_pattern,
		S_OR(filter_entry->event_name, "<not used>"),
		filter_entry->event_name_hash,
		S_OR(filter_entry->header_name, "<not used>"),
		match_type_names[filter_entry->match_type],
		filter_entry->regex_filter,
		filter_entry->string_filter,
		filter_entry->is_excludefilter);

	if (filter_entry->is_excludefilter) {
		ao2_t_link(excludefilters, filter_entry, "link new filter into exclude user container");
	} else {
		ao2_t_link(includefilters, filter_entry, "link new filter into include user container");
	}

	SCOPE_EXIT_RTN_VALUE(FILTER_SUCCESS, "Filter added successfully");
}

#ifdef TEST_FRAMEWORK

struct test_filter_data {
	const char *criteria;
	const char *filter;
	enum add_filter_result expected_add_filter_result;
	struct event_filter_entry expected_filter_entry;
	const char *test_event_name;
	const char *test_event_payload;
	int expected_should_send_event;
};

static char *add_filter_result_enums[] = {
	[FILTER_SUCCESS] = "FILTER_SUCCESS",
	[FILTER_ALLOC_FAILED] = "FILTER_ALLOC_FAILED",
	[FILTER_COMPILE_FAIL] = "FILTER_COMPILE_FAIL",
	[FILTER_FORMAT_ERROR] = "FILTER_FORMAT_ERROR",
};

#define TEST_EVENT_NEWCHANNEL "Newchannel", "Event: Newchannel\r\nChannel: XXX\r\nSomeheader: YYY\r\n"
#define TEST_EVENT_VARSET "VarSet", "Event: VarSet\r\nChannel: ABC\r\nSomeheader: XXX\r\n"
#define TEST_EVENT_NONE "", ""

static struct test_filter_data parsing_filter_tests[] = {
	/* Valid filters */
	{ "eventfilter", "XXX", FILTER_SUCCESS,
		{ FILTER_MATCH_REGEX, NULL, NULL, NULL, 0, NULL, 0}, TEST_EVENT_NEWCHANNEL, 1},
	{ "eventfilter", "!XXX", FILTER_SUCCESS,
		{ FILTER_MATCH_REGEX, NULL, NULL, NULL, 0, NULL, 1},  TEST_EVENT_VARSET, 0},
	{ "eventfilter(name(VarSet),method(none))", "", FILTER_SUCCESS,
		{ FILTER_MATCH_NONE, NULL, NULL, "VarSet", 0, NULL, 0}, TEST_EVENT_VARSET, 1},
	{ "eventfilter(name(Newchannel),method(regex))", "X[XYZ]X", FILTER_SUCCESS,
		{ FILTER_MATCH_REGEX, NULL, NULL, "Newchannel", 0, NULL, 0}, TEST_EVENT_NEWCHANNEL, 1},
	{ "eventfilter(name(Newchannel),method(regex))", "X[abc]X", FILTER_SUCCESS,
		{ FILTER_MATCH_REGEX, NULL, NULL, "Newchannel", 0, NULL, 0}, TEST_EVENT_NEWCHANNEL, 0},
	{ "eventfilter(action(exclude),name(Newchannel),method(regex))", "X[XYZ]X", FILTER_SUCCESS,
		{ FILTER_MATCH_REGEX, NULL, NULL, "Newchannel", 0, NULL, 1}, TEST_EVENT_NEWCHANNEL, 0},
	{ "eventfilter(action(exclude),name(Newchannel),method(regex))", "X[abc]X", FILTER_SUCCESS,
		{ FILTER_MATCH_REGEX, NULL, NULL, "Newchannel", 0, NULL, 1}, TEST_EVENT_NEWCHANNEL, 1},
	{ "eventfilter(action(include),name(VarSet),header(Channel),method(starts_with))", "AB", FILTER_SUCCESS,
		{ FILTER_MATCH_STARTS_WITH, NULL, NULL, "VarSet", 0, "Channel:", 0}, TEST_EVENT_VARSET, 1},
	{ "eventfilter(action(include),name(VarSet),header(Channel),method(ends_with))", "BC", FILTER_SUCCESS,
		{ FILTER_MATCH_ENDS_WITH, NULL, NULL, "VarSet", 0, "Channel:", 0}, TEST_EVENT_VARSET, 1},
	{ "eventfilter(action(include),name(VarSet),header(Channel),method(exact))", "ABC", FILTER_SUCCESS,
		{ FILTER_MATCH_EXACT, NULL, NULL, "VarSet", 0, "Channel:", 0}, TEST_EVENT_VARSET, 1},
	{ "eventfilter(action(include),name(VarSet),header(Channel),method(exact))", "XXX", FILTER_SUCCESS,
		{ FILTER_MATCH_EXACT, NULL, NULL, "VarSet", 0, "Channel:", 0}, TEST_EVENT_VARSET, 0},
	{ "eventfilter(name(VarSet),header(Channel),method(exact))", "!ZZZ", FILTER_SUCCESS,
		{ FILTER_MATCH_EXACT, NULL, NULL, "VarSet", 0, "Channel:", 1}, TEST_EVENT_VARSET, 1},
	{ "eventfilter(action(exclude),name(VarSet),header(Channel),method(exact))", "ZZZ", FILTER_SUCCESS,
		{ FILTER_MATCH_EXACT, NULL, NULL, "VarSet", 0, "Channel:", 1}, TEST_EVENT_VARSET, 1},
	{ "eventfilter(action(include),name(VarSet),header(Someheader),method(exact))", "!XXX", FILTER_SUCCESS,
		{ FILTER_MATCH_EXACT, NULL, NULL, "VarSet", 0, "Someheader:", 0}, TEST_EVENT_VARSET, 1},

	/* Invalid filters */
	{ "eventfilter(action(include)", "", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(action(inlude)", "", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(nnnn(yyy)", "XXX", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(eader(VarSet)", "XXX", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(ethod(contains)", "XXX", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(nnnn(yyy),header(VarSet),method(contains)", "XXX", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(name(yyy),heder(VarSet),method(contains)", "XXX", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(name(yyy),header(VarSet),mehod(contains)", "XXX", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(name(yyy),header(VarSet),method(coains)", "XXX", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(method(yyy))", "XXX", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter", "", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter", "!", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter()", "XXX", FILTER_FORMAT_ERROR, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter", "XX[X", FILTER_COMPILE_FAIL, { 0, }, TEST_EVENT_NONE, 0},
	{ "eventfilter(method(regex))", "XX[X", FILTER_COMPILE_FAIL, { 0, }, TEST_EVENT_NONE, 0},
};

/*
 * This is a bit different than ast_strings_equal in that
 * it will return 1 if both strings are NULL.
 */
static int strings_equal(const char *str1, const char *str2)
{
	if ((!str1 && str2) || (str1 && !str2)) {
		return 0;
	}

	return str1 == str2 || !strcmp(str1, str2);
}

AST_TEST_DEFINE(eventfilter_test_creation)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	RAII_VAR(struct ao2_container *, includefilters, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, excludefilters, NULL, ao2_cleanup);
	int i = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "eventfilter_test_creation";
		info->category = "/main/manager/";
		info->summary = "Test eventfilter creation";
		info->description =
			"This creates various eventfilters and tests to make sure they were created successfully.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	includefilters = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
	excludefilters = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
	if (!includefilters || !excludefilters) {
		ast_test_status_update(test, "Failed to allocate filter containers.\n");
		return AST_TEST_FAIL;
	}

	for (i = 0; i < ARRAY_LEN(parsing_filter_tests); i++) {
		struct event_filter_entry *filter_entry;
		enum add_filter_result add_filter_res;
		int send_event = 0;
		struct eventqent *eqe = NULL;
		int include_container_count = 0;
		int exclude_container_count = 0;

		/* We need to clear the containers before each test */
		ao2_callback(includefilters, OBJ_UNLINK | OBJ_NODATA, NULL, NULL);
		ao2_callback(excludefilters, OBJ_UNLINK | OBJ_NODATA, NULL, NULL);

		add_filter_res = manager_add_filter(parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
			includefilters, excludefilters);

		/* If you're adding a new test, enable this to see the full results */
#if 0
		ast_test_debug(test, "Add filter result '%s = %s': Expected: %s  Actual: %s  %s\n",
			parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
			add_filter_result_enums[parsing_filter_tests[i].expected_add_filter_result],
			add_filter_result_enums[add_filter_res],
			add_filter_res != parsing_filter_tests[i].expected_add_filter_result ? "FAIL" : "PASS");
#endif

		if (add_filter_res != parsing_filter_tests[i].expected_add_filter_result) {
			ast_test_status_update(test,
				"Unexpected add filter result '%s = %s'. Expected result: %s Actual result: %s\n",
				parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
				add_filter_result_enums[parsing_filter_tests[i].expected_add_filter_result],
				add_filter_result_enums[add_filter_res]);
			res = AST_TEST_FAIL;
			continue;
		}

		if (parsing_filter_tests[i].expected_add_filter_result != FILTER_SUCCESS) {
			/*
			 * We don't need to test filters that we know aren't going
			 * to be parsed successfully.
			 */
			continue;
		}

		/* We need to set the event name hash on the test data */
		if (parsing_filter_tests[i].expected_filter_entry.event_name) {
			parsing_filter_tests[i].expected_filter_entry.event_name_hash =
				ast_str_hash(parsing_filter_tests[i].expected_filter_entry.event_name);
		}

		include_container_count = ao2_container_count(includefilters);
		exclude_container_count = ao2_container_count(excludefilters);

		if (parsing_filter_tests[i].expected_filter_entry.is_excludefilter) {
			if (exclude_container_count != 1 || include_container_count != 0) {
				ast_test_status_update(test,
					"Invalid container counts for exclude filter '%s = %s'. Exclude: %d Include: %d.  Should be 1 and 0\n",
					parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
					exclude_container_count, include_container_count);
				res = AST_TEST_FAIL;
				continue;
			}
			/* There can only be one entry in the container so ao2_find is fine */
			filter_entry = ao2_find(excludefilters, NULL, OBJ_SEARCH_OBJECT);
		} else {
			if (include_container_count != 1 || exclude_container_count != 0) {
				ast_test_status_update(test,
					"Invalid container counts for include filter '%s = %s'. Include: %d Exclude: %d.  Should be 1 and 0\n",
					parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
					include_container_count, exclude_container_count);
				res = AST_TEST_FAIL;
				continue;
			}
			/* There can only be one entry in the container so ao2_find is fine */
			filter_entry = ao2_find(includefilters, NULL, OBJ_SEARCH_OBJECT);
		}

		if (!filter_entry) {
			ast_test_status_update(test,
				"Failed to find filter entry for '%s = %s' in %s filter container\n",
				parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
				parsing_filter_tests[i].expected_filter_entry.is_excludefilter ? "exclude" : "include");
			res = AST_TEST_FAIL;
			goto loop_cleanup;
		}

		if (filter_entry->match_type != parsing_filter_tests[i].expected_filter_entry.match_type) {
			ast_test_status_update(test,
				"Failed to match filter type for '%s = %s'. Expected: %s Actual: %s\n",
				parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
				match_type_names[parsing_filter_tests[i].expected_filter_entry.match_type],
				match_type_names[filter_entry->match_type]);
			res = AST_TEST_FAIL;
			goto loop_cleanup;
		}

		if (!strings_equal(filter_entry->event_name, parsing_filter_tests[i].expected_filter_entry.event_name)) {
			ast_test_status_update(test,
				"Failed to match event name for '%s = %s'. Expected: '%s' Actual: '%s'\n",
				parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
				parsing_filter_tests[i].expected_filter_entry.event_name, filter_entry->event_name);
			res = AST_TEST_FAIL;
			goto loop_cleanup;
		}

		if (filter_entry->event_name_hash != parsing_filter_tests[i].expected_filter_entry.event_name_hash) {
			ast_test_status_update(test,
				"Event name hashes failed to match for '%s = %s'. Expected: %u Actual: %u\n",
				parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
				parsing_filter_tests[i].expected_filter_entry.event_name_hash, filter_entry->event_name_hash);
			res = AST_TEST_FAIL;
			goto loop_cleanup;
		}

		if (!strings_equal(filter_entry->header_name, parsing_filter_tests[i].expected_filter_entry.header_name)) {
			ast_test_status_update(test,
				"Failed to match header name for '%s = %s'. Expected: '%s' Actual: '%s'\n",
				parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
				parsing_filter_tests[i].expected_filter_entry.header_name, filter_entry->header_name);
			res = AST_TEST_FAIL;
			goto loop_cleanup;
		}

		switch (parsing_filter_tests[i].expected_filter_entry.match_type) {
		case FILTER_MATCH_REGEX:
			if (!filter_entry->regex_filter) {
				ast_test_status_update(test,
					"Failed to compile regex filter for '%s = %s'\n",
					parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter);
				res = AST_TEST_FAIL;
				goto loop_cleanup;
			}
			break;
		case FILTER_MATCH_NONE:
			if (filter_entry->regex_filter || !ast_strlen_zero(filter_entry->string_filter)) {
				ast_test_status_update(test,
					"Unexpected regex filter or string for '%s = %s' with match_type 'none'\n",
					parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter);
				res = AST_TEST_FAIL;
				goto loop_cleanup;
			}
			break;
		case FILTER_MATCH_STARTS_WITH:
		case FILTER_MATCH_ENDS_WITH:
		case FILTER_MATCH_CONTAINS:
		case FILTER_MATCH_EXACT:
			if (filter_entry->regex_filter || ast_strlen_zero(filter_entry->string_filter)) {
				ast_test_status_update(test,
					"Unexpected regex filter or empty string for '%s = %s' with match_type '%s'\n",
					parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
					match_type_names[parsing_filter_tests[i].expected_filter_entry.match_type]);
				res = AST_TEST_FAIL;
				goto loop_cleanup;
			}
			break;
		default:
			res = AST_TEST_FAIL;
			goto loop_cleanup;
		}

		/*
		 * This is a basic test of whether a single event matches a single filter.
		 */
		eqe = ast_calloc(1, sizeof(*eqe) + strlen(parsing_filter_tests[i].test_event_payload) + 1);
		if (!eqe) {
			ast_test_status_update(test, "Failed to allocate eventqent\n");
			res = AST_TEST_FAIL;
			ao2_ref(filter_entry, -1);
			break;
		}
		strcpy(eqe->eventdata, parsing_filter_tests[i].test_event_payload); /* Safe */
		eqe->event_name_hash = ast_str_hash(parsing_filter_tests[i].test_event_name);
		send_event = should_send_event(includefilters, excludefilters, eqe);
		if (send_event != parsing_filter_tests[i].expected_should_send_event) {
			char *escaped = ast_escape_c_alloc(parsing_filter_tests[i].test_event_payload);
			ast_test_status_update(test,
				"Should send event failed to match for '%s = %s' payload '%s'. Expected: %s Actual: %s\n",
				parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter, escaped,
				AST_YESNO(parsing_filter_tests[i].expected_should_send_event), AST_YESNO(send_event));
			ast_free(escaped);
			res = AST_TEST_FAIL;
		}
loop_cleanup:
		ast_free(eqe);
		ao2_cleanup(filter_entry);

	}
	ast_test_status_update(test, "Tested %d filters\n", i);

	return res;
}

struct test_filter_matching {
	const char *criteria;
	const char *pattern;
};

/*
 * These filters are used to test the precedence of include and exclude
 * filters.  When there are both include and exclude filters, the include
 * filters are matched first.  If the event doesn't match an include filter,
 * it's discarded.  If it does match, the exclude filter list is searched and
 * if a match is found, the event is discarded.
 */

/*
 * The order of the filters in the array doesn't really matter.  The
 * include and exclude filters are in separate containers and in each
 * container, traversal stops when a match is found.
 */
static struct test_filter_matching filters_for_matching[] = {
	{ "eventfilter(name(VarSet),method(none))", ""},
	{ "eventfilter(name(Newchannel),method(regex))", "X[XYZ]X"},
	{ "eventfilter(name(Newchannel),method(regex))", "X[abc]X"},
	{ "eventfilter(name(Newchannel),header(Someheader),method(regex))", "ZZZ"},
	{ "eventfilter(action(exclude),name(Newchannel),method(regex))", "X[a]X"},
	{ "eventfilter(action(exclude),name(Newchannel),method(regex))", "X[Z]X"},
	{ "eventfilter(action(exclude),name(VarSet),header(Channel),method(regex))", "YYY"},
};

struct test_event_matching{
	const char *event_name;
	const char *payload;
	int expected_should_send_event;
};

static struct test_event_matching events_for_matching[] = {
	{ "Newchannel", "Event: Newchannel\r\nChannel: XXX\r\nSomeheader: YYY\r\n", 1 },
	{ "Newchannel", "Event: Newchannel\r\nChannel: XZX\r\nSomeheader: YYY\r\n", 0 },
	{ "Newchannel", "Event: Newchannel\r\nChannel: XaX\r\nSomeheader: YYY\r\n", 0 },
	{ "Newchannel", "Event: Newchannel\r\nChannel: XbX\r\nSomeheader: YYY\r\n", 1 },
	{ "Newchannel", "Event: Newchannel\r\nChannel: XcX\r\nSomeheader: YYY\r\n", 1 },
	{ "Newchannel", "Event: Newchannel\r\nChannel: YYY\r\nSomeheader: YYY\r\n", 0 },
	{ "Newchannel", "Event: Newchannel\r\nChannel: YYY\r\nSomeheader: ZZZ\r\n", 1 },
	{ "VarSet", "Event: VarSet\r\nChannel: XXX\r\nSomeheader: YYY\r\n", 1 },
	{ "VarSet", "Event: VarSet\r\nChannel: YYY\r\nSomeheader: YYY\r\n", 0 },
};

AST_TEST_DEFINE(eventfilter_test_matching)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	RAII_VAR(struct ao2_container *, includefilters, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, excludefilters, NULL, ao2_cleanup);
	int i = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "eventfilter_test_matching";
		info->category = "/main/manager/";
		info->summary = "Test eventfilter matching";
		info->description =
			"This creates various eventfilters and tests to make sure they were matched successfully.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	includefilters = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
	excludefilters = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
	if (!includefilters || !excludefilters) {
		ast_test_status_update(test, "Failed to allocate filter containers.\n");
		return AST_TEST_FAIL;
	}

	/* Load all the expected SUCCESS filters */
	for (i = 0; i < ARRAY_LEN(filters_for_matching); i++) {
		enum add_filter_result add_filter_res;

		add_filter_res = manager_add_filter(filters_for_matching[i].criteria,
			filters_for_matching[i].pattern, includefilters, excludefilters);

		if (add_filter_res != FILTER_SUCCESS) {
			ast_test_status_update(test,
				"Unexpected add filter result '%s = %s'. Expected result: %s Actual result: %s\n",
				parsing_filter_tests[i].criteria, parsing_filter_tests[i].filter,
				add_filter_result_enums[FILTER_SUCCESS],
				add_filter_result_enums[add_filter_res]);
			res = AST_TEST_FAIL;
			break;
		}
	}
	ast_test_debug(test, "Loaded %d filters\n", i);

	if (res != AST_TEST_PASS) {
		return res;
	}

	/* Now test them */
	for (i = 0; i < ARRAY_LEN(events_for_matching); i++) {
		int send_event = 0;
		struct eventqent *eqe = NULL;

		eqe = ast_calloc(1, sizeof(*eqe) + strlen(events_for_matching[i].payload) + 1);
		if (!eqe) {
			ast_test_status_update(test, "Failed to allocate eventqent\n");
			res = AST_TEST_FAIL;
			break;
		}
		strcpy(eqe->eventdata, events_for_matching[i].payload); /* Safe */
		eqe->event_name_hash = ast_str_hash(events_for_matching[i].event_name);
		send_event = should_send_event(includefilters, excludefilters, eqe);
		if (send_event != events_for_matching[i].expected_should_send_event) {
			char *escaped = ast_escape_c_alloc(events_for_matching[i].payload);
			ast_test_status_update(test,
				"Should send event failed to match for '%s'. Expected: %s Actual: %s\n",
				escaped,
				AST_YESNO(events_for_matching[i].expected_should_send_event), AST_YESNO(send_event));
			ast_free(escaped);
			res = AST_TEST_FAIL;
		}
		ast_free(eqe);
	}
	ast_test_debug(test, "Tested %d events\n", i);

	return res;
}
#endif

/*!
 * Send any applicable events to the client listening on this socket.
 * Wait only for a finite time on each event, and drop all events whether
 * they are successfully sent or not.
 */
static int process_events(struct mansession *s)
{
	int ret = 0;

	ao2_lock(s->session);
	if (s->session->stream != NULL) {
		struct eventqent *eqe = s->session->last_ev;

		while ((eqe = advance_event(eqe))) {
			if (eqe->category == EVENT_FLAG_SHUTDOWN) {
				ast_debug(3, "Received CloseSession event\n");
				ret = -1;
			}
			if (!ret && s->session->authenticated &&
			    (s->session->readperm & eqe->category) == eqe->category &&
			    (s->session->send_events & eqe->category) == eqe->category) {
					if (should_send_event(s->session->includefilters, s->session->excludefilters, eqe)) {
						if (send_string(s, eqe->eventdata) < 0 || s->write_error)
							ret = -1;	/* don't send more */
					}
			}
			s->session->last_ev = eqe;
		}
	}
	ao2_unlock(s->session);
	return ret;
}

static int action_userevent(struct mansession *s, const struct message *m)
{
	const char *event = astman_get_header(m, "UserEvent");
	struct ast_str *body = ast_str_thread_get(&userevent_buf, 16);
	int x;

	ast_str_reset(body);

	for (x = 0; x < m->hdrcount; x++) {
		if (strncasecmp("UserEvent:", m->headers[x], strlen("UserEvent:")) &&
				strncasecmp("Action:", m->headers[x], strlen("Action:"))) {
			ast_str_append(&body, 0, "%s\r\n", m->headers[x]);
		}
	}

	astman_send_ack(s, m, "Event Sent");
	manager_event(EVENT_FLAG_USER, "UserEvent", "UserEvent: %s\r\n%s", event, ast_str_buffer(body));
	return 0;
}

/*! \brief Show PBX core settings information */
static int action_coresettings(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	char idText[150];

	if (!ast_strlen_zero(actionid)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", actionid);
	} else {
		idText[0] = '\0';
	}

	astman_append(s, "Response: Success\r\n"
			"%s"
			"AMIversion: %s\r\n"
			"AsteriskVersion: %s\r\n"
			"SystemName: %s\r\n"
			"CoreMaxCalls: %d\r\n"
			"CoreMaxLoadAvg: %f\r\n"
			"CoreRunUser: %s\r\n"
			"CoreRunGroup: %s\r\n"
			"CoreMaxFilehandles: %d\r\n"
			"CoreRealTimeEnabled: %s\r\n"
			"CoreCDRenabled: %s\r\n"
			"CoreHTTPenabled: %s\r\n"
			"SoundsSearchCustomDir: %s\r\n"
			"\r\n",
			idText,
			AMI_VERSION,
			ast_get_version(),
			ast_config_AST_SYSTEM_NAME,
			ast_option_maxcalls,
			ast_option_maxload,
			ast_config_AST_RUN_USER,
			ast_config_AST_RUN_GROUP,
			ast_option_maxfiles,
			AST_CLI_YESNO(ast_realtime_enabled()),
			AST_CLI_YESNO(ast_cdr_is_enabled()),
			AST_CLI_YESNO(ast_webmanager_check_enabled()),
			AST_CLI_YESNO(ast_opt_sounds_search_custom)
			);
	return 0;
}

/*! \brief Show PBX core status information */
static int action_corestatus(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	char idText[150];
	char startuptime[150], startupdate[150];
	char reloadtime[150], reloaddate[150];
	struct ast_tm tm;

	if (!ast_strlen_zero(actionid)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", actionid);
	} else {
		idText[0] = '\0';
	}

	ast_localtime(&ast_startuptime, &tm, NULL);
	ast_strftime(startuptime, sizeof(startuptime), "%H:%M:%S", &tm);
	ast_strftime(startupdate, sizeof(startupdate), "%Y-%m-%d", &tm);
	ast_localtime(&ast_lastreloadtime, &tm, NULL);
	ast_strftime(reloadtime, sizeof(reloadtime), "%H:%M:%S", &tm);
	ast_strftime(reloaddate, sizeof(reloaddate), "%Y-%m-%d", &tm);

	astman_append(s, "Response: Success\r\n"
			"%s"
			"CoreStartupDate: %s\r\n"
			"CoreStartupTime: %s\r\n"
			"CoreReloadDate: %s\r\n"
			"CoreReloadTime: %s\r\n"
			"CoreCurrentCalls: %d\r\n"
			"CoreProcessedCalls: %d\r\n"
			"\r\n",
			idText,
			startupdate,
			startuptime,
			reloaddate,
			reloadtime,
			ast_active_channels(),
			ast_processed_calls()
			);
	return 0;
}

/*! \brief Send a reload event */
static int action_reload(struct mansession *s, const struct message *m)
{
	const char *module = astman_get_header(m, "Module");
	enum ast_module_reload_result res = ast_module_reload(S_OR(module, NULL));

	switch (res) {
	case AST_MODULE_RELOAD_NOT_FOUND:
		astman_send_error(s, m, "No such module");
		break;
	case AST_MODULE_RELOAD_NOT_IMPLEMENTED:
		astman_send_error(s, m, "Module does not support reload");
		break;
	case AST_MODULE_RELOAD_ERROR:
		astman_send_error(s, m, "An unknown error occurred");
		break;
	case AST_MODULE_RELOAD_IN_PROGRESS:
		astman_send_error(s, m, "A reload is in progress");
		break;
	case AST_MODULE_RELOAD_UNINITIALIZED:
		astman_send_error(s, m, "Module not initialized");
		break;
	case AST_MODULE_RELOAD_QUEUED:
	case AST_MODULE_RELOAD_SUCCESS:
		/* Treat a queued request as success */
		astman_send_ack(s, m, "Module Reloaded");
		break;
	}
	return 0;
}

/*! \brief  Manager command "CoreShowChannels" - List currently defined channels
 *          and some information about them. */
static int action_coreshowchannels(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	char idText[256];
	int numchans = 0;
	struct ao2_container *channels;
	struct ao2_iterator it_chans;
	struct ast_channel_snapshot *cs;

	if (!ast_strlen_zero(actionid)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", actionid);
	} else {
		idText[0] = '\0';
	}

	channels = ast_channel_cache_by_name();

	astman_send_listack(s, m, "Channels will follow", "start");

	it_chans = ao2_iterator_init(channels, 0);
	for (; (cs = ao2_iterator_next(&it_chans)); ao2_ref(cs, -1)) {
		struct ast_str *built = ast_manager_build_channel_state_string_prefix(cs, "");
		char durbuf[16] = "";

		if (!built) {
			continue;
		}

		if (!ast_tvzero(cs->base->creationtime)) {
			int duration, durh, durm, durs;

			duration = (int)(ast_tvdiff_ms(ast_tvnow(), cs->base->creationtime) / 1000);
			durh = duration / 3600;
			durm = (duration % 3600) / 60;
			durs = duration % 60;
			snprintf(durbuf, sizeof(durbuf), "%02d:%02d:%02d", durh, durm, durs);
		}

		astman_append(s,
			"Event: CoreShowChannel\r\n"
			"%s"
			"%s"
			"Application: %s\r\n"
			"ApplicationData: %s\r\n"
			"Duration: %s\r\n"
			"BridgeId: %s\r\n"
			"\r\n",
			idText,
			ast_str_buffer(built),
			cs->dialplan->appl,
			cs->dialplan->data,
			durbuf,
			cs->bridge->id);

		numchans++;

		ast_free(built);
	}
	ao2_iterator_destroy(&it_chans);

	astman_send_list_complete(s, m, "CoreShowChannelsComplete", numchans);

	ao2_ref(channels, -1);
	return 0;
}

/*! \brief Helper function to add a channel name to the vector */
static int coreshowchannelmap_add_to_map(struct ao2_container *c, const char *s)
{
	char *str;

	str = ast_strdup(s);
	if (!str) {
		ast_log(LOG_ERROR, "Unable to append channel to channel map\n");
		return 1;
	}

	/* If this is a duplicate, it will be ignored */
	ast_str_container_add(c, str);

	return 0;
}

/*! \brief Recursive function to get all channels in a bridge. Follow local channels as well */
static int coreshowchannelmap_add_connected_channels(struct ao2_container *channel_map,
	struct ast_channel_snapshot *channel_snapshot, struct ast_bridge_snapshot *bridge_snapshot)
{
	int res = 0;
	struct ao2_iterator iter;
	char *current_channel_uid;

	iter = ao2_iterator_init(bridge_snapshot->channels, 0);
	while ((current_channel_uid = ao2_iterator_next(&iter))) {
		struct ast_channel_snapshot *current_channel_snapshot;
		int add_channel_res;

		/* Don't add the original channel to the list - it's either already in there,
		 * or it's the channel we want the map for */
		if (!strcmp(current_channel_uid, channel_snapshot->base->uniqueid)) {
			ao2_ref(current_channel_uid, -1);
			continue;
		}

		current_channel_snapshot = ast_channel_snapshot_get_latest(current_channel_uid);
		if (!current_channel_snapshot) {
			ast_debug(5, "Unable to get channel snapshot\n");
			ao2_ref(current_channel_uid, -1);
			continue;
		}

		add_channel_res = coreshowchannelmap_add_to_map(channel_map, current_channel_snapshot->base->name);
		if (add_channel_res) {
			res = 1;
			ao2_ref(current_channel_snapshot, -1);
			ao2_ref(current_channel_uid, -1);
			break;
		}

		/* If this is a local channel that we haven't seen yet, let's go ahead and find out what else is connected to it */
		if (ast_begins_with(current_channel_snapshot->base->name, "Local")) {
			struct ast_channel_snapshot *other_local_snapshot;
			struct ast_bridge_snapshot *other_bridge_snapshot;
			int size = strlen(current_channel_snapshot->base->name);
			char other_local[size + 1];

			/* Don't copy the trailing number - set it to 1 or 2, whichever one it currently is not */
			ast_copy_string(other_local, current_channel_snapshot->base->name, size);
			other_local[size - 1] = ast_ends_with(current_channel_snapshot->base->name, "1") ? '2' : '1';
			other_local[size] = '\0';

			other_local_snapshot = ast_channel_snapshot_get_latest_by_name(other_local);
			if (!other_local_snapshot) {
				ast_debug(5, "Unable to get other local channel snapshot\n");
				ao2_ref(current_channel_snapshot, -1);
				ao2_ref(current_channel_uid, -1);
				continue;
			}

			if (coreshowchannelmap_add_to_map(channel_map, other_local_snapshot->base->name)) {
				res = 1;
				ao2_ref(current_channel_snapshot, -1);
				ao2_ref(current_channel_uid, -1);
				ao2_ref(other_local_snapshot, -1);
				break;
			}

			other_bridge_snapshot = ast_bridge_get_snapshot_by_uniqueid(other_local_snapshot->bridge->id);
			if (other_bridge_snapshot) {
				res = coreshowchannelmap_add_connected_channels(channel_map, other_local_snapshot, other_bridge_snapshot);
			}

			ao2_ref(current_channel_snapshot, -1);
			ao2_ref(current_channel_uid, -1);
			ao2_ref(other_local_snapshot, -1);
			ao2_ref(other_bridge_snapshot, -1);

			if (res) {
				break;
			}
		}
	}
	ao2_iterator_destroy(&iter);

	return res;
}

/*! \brief  Manager command "CoreShowChannelMap" - Lists all channels connected to
 *          the specified channel. */
static int action_coreshowchannelmap(struct mansession *s, const struct message *m)
{
	const char *actionid = astman_get_header(m, "ActionID");
	const char *channel_name = astman_get_header(m, "Channel");
	char *current_channel_name;
	char id_text[256];
	int total = 0;
	struct ao2_container *channel_map;
	struct ao2_iterator i;
	RAII_VAR(struct ast_bridge_snapshot *, bridge_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, channel_snapshot, NULL, ao2_cleanup);

	if (!ast_strlen_zero(actionid)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", actionid);
	} else {
		id_text[0] = '\0';
	}

	if (ast_strlen_zero(channel_name)) {
		astman_send_error(s, m, "CoreShowChannelMap requires a channel.\n");
		return 0;
	}

	channel_snapshot = ast_channel_snapshot_get_latest_by_name(channel_name);
	if (!channel_snapshot) {
		astman_send_error(s, m, "Could not get channel snapshot\n");
		return 0;
	}

	if (ast_strlen_zero(channel_snapshot->bridge->id)) {
		astman_send_listack(s, m, "Channel map will follow", "start");
		astman_send_list_complete_start(s, m, "CoreShowChannelMapComplete", 0);
		astman_send_list_complete_end(s);
		return 0;
	}

	bridge_snapshot = ast_bridge_get_snapshot_by_uniqueid(channel_snapshot->bridge->id);
	if (!bridge_snapshot) {
		astman_send_listack(s, m, "Channel map will follow", "start");
		astman_send_list_complete_start(s, m, "CoreShowChannelMapComplete", 0);
		astman_send_list_complete_end(s);
		return 0;
	}

	channel_map = ast_str_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK | AO2_CONTAINER_ALLOC_OPT_DUPS_OBJ_REJECT, 1);
	if (!channel_map) {
		astman_send_error(s, m, "Could not create channel map\n");
		return 0;
	}

	astman_send_listack(s, m, "Channel map will follow", "start");

	if (coreshowchannelmap_add_connected_channels(channel_map, channel_snapshot, bridge_snapshot)) {
		astman_send_error(s, m, "Could not complete channel map\n");
		ao2_ref(channel_map, -1);
		return 0;
	}

	i = ao2_iterator_init(channel_map, 0);
	while ((current_channel_name = ao2_iterator_next(&i))) {
		astman_append(s,
			"Event: CoreShowChannelMap\r\n"
			"%s"
			"Channel: %s\r\n"
			"ConnectedChannel: %s\r\n\r\n",
			id_text,
			channel_name,
			current_channel_name);
		total++;
	}
	ao2_iterator_destroy(&i);

	ao2_ref(channel_map, -1);
	astman_send_list_complete_start(s, m, "CoreShowChannelMapComplete", total);
	astman_send_list_complete_end(s);

	return 0;
}

/*! \brief  Manager command "LoggerRotate" - reloads and rotates the logger in
 *          the same manner as the CLI command 'logger rotate'. */
static int action_loggerrotate(struct mansession *s, const struct message *m)
{
	if (ast_logger_rotate()) {
		astman_send_error(s, m, "Failed to reload the logger and rotate log files");
		return 0;
	}

	astman_send_ack(s, m, "Reloaded the logger and rotated log files");
	return 0;
}

/*! \brief Manager function to check if module is loaded */
static int manager_modulecheck(struct mansession *s, const struct message *m)
{
	const char *module = astman_get_header(m, "Module");
	const char *id = astman_get_header(m, "ActionID");

	ast_debug(1, "**** ModuleCheck .so file %s\n", module);
	if (!ast_module_check(module)) {
		astman_send_error(s, m, "Module not loaded");
		return 0;
	}

	astman_append(s, "Response: Success\r\n");

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

#if !defined(LOW_MEMORY)
	/* When we switched from subversion to git we lost the ability to
	 * retrieve the 'ASTERISK_FILE_VERSION' from that file, but we retain
	 * the response header here for backwards compatibility. */
	astman_append(s, "Version: \r\n");
#endif

	astman_append(s, "\r\n");

	return 0;
}

/**
 * \brief Check if the given file path is in the modules dir or not
 *
 * \note When the module is being loaded / reloaded / unloaded, the modules dir is
 * automatically prepended
 *
 * \return 1 if inside modules dir
 * \return 0 if outside modules dir
 * \return -1 on failure
 */
static int file_in_modules_dir(const char *filename)
{
	char *stripped_filename;
	RAII_VAR(char *, path, NULL, ast_free);
	RAII_VAR(char *, real_path, NULL, ast_free);

	/* Don't bother checking */
	if (live_dangerously) {
		return 1;
	}

	stripped_filename = ast_strip(ast_strdupa(filename));

	/* Always prepend the modules dir since that is what the code does for ModuleLoad */
	if (ast_asprintf(&path, "%s/%s", ast_config_AST_MODULE_DIR, stripped_filename) == -1) {
		return -1;
	}

	real_path = realpath(path, NULL);
	if (!real_path) {
		return -1;
	}

	return ast_begins_with(real_path, ast_config_AST_MODULE_DIR);
}

static int manager_moduleload(struct mansession *s, const struct message *m)
{
	int res;
	const char *module = astman_get_header(m, "Module");
	const char *loadtype = astman_get_header(m, "LoadType");
	const char *recursive = astman_get_header(m, "Recursive");

	if (!loadtype || strlen(loadtype) == 0) {
		astman_send_error(s, m, "Incomplete ModuleLoad action.");
	}
	if ((!module || strlen(module) == 0) && strcasecmp(loadtype, "reload") != 0) {
		astman_send_error(s, m, "Need module name");
	}

	res = file_in_modules_dir(module);
	if (res == 0) {
		astman_send_error(s, m, "Module must be in the configured modules directory.");
		return 0;
	} else if (res == -1) {
		astman_send_error(s, m, "Module not found.");
		return 0;
	}

	if (!strcasecmp(loadtype, "load")) {
		res = ast_load_resource(module);
		if (res) {
			astman_send_error(s, m, "Could not load module.");
		} else {
			astman_send_ack(s, m, "Module loaded.");
		}
	} else if (!strcasecmp(loadtype, "unload")) {
		res = ast_unload_resource(module, AST_FORCE_SOFT);
		if (res) {
			astman_send_error(s, m, "Could not unload module.");
		} else {
			astman_send_ack(s, m, "Module unloaded.");
		}
	} else if (!strcasecmp(loadtype, "refresh")) {
		res = ast_refresh_resource(module, AST_FORCE_SOFT, !ast_strlen_zero(recursive) && ast_true(recursive));
		if (res) {
			astman_send_error(s, m, "Could not refresh module.");
		} else {
			astman_send_ack(s, m, "Module unloaded and loaded.");
		}
	} else if (!strcasecmp(loadtype, "reload")) {
		/* TODO: Unify the ack/error messages here with action_reload */
		if (!ast_strlen_zero(module)) {
			enum ast_module_reload_result reload_res = ast_module_reload(module);

			switch (reload_res) {
			case AST_MODULE_RELOAD_NOT_FOUND:
				astman_send_error(s, m, "No such module.");
				break;
			case AST_MODULE_RELOAD_NOT_IMPLEMENTED:
				astman_send_error(s, m, "Module does not support reload action.");
				break;
			case AST_MODULE_RELOAD_ERROR:
				astman_send_error(s, m, "An unknown error occurred");
				break;
			case AST_MODULE_RELOAD_IN_PROGRESS:
				astman_send_error(s, m, "A reload is in progress");
				break;
			case AST_MODULE_RELOAD_UNINITIALIZED:
				astman_send_error(s, m, "Module not initialized");
				break;
			case AST_MODULE_RELOAD_QUEUED:
			case AST_MODULE_RELOAD_SUCCESS:
				/* Treat a queued request as success */
				astman_send_ack(s, m, "Module reloaded.");
				break;
			}
		} else {
			ast_module_reload(NULL);	/* Reload all modules */
			astman_send_ack(s, m, "All modules reloaded");
		}
	} else {
		astman_send_error(s, m, "Incomplete ModuleLoad action.");
	}
	return 0;
}

static void log_action(const struct message *m, const char *action)
{
	struct ast_str *buf;
	int x;

	if (!manager_debug) {
		return;
	}

	buf = ast_str_create(256);
	if (!buf) {
		return;
	}

	for (x = 0; x < m->hdrcount; ++x) {
		if (!strncasecmp(m->headers[x], "Secret", 6)) {
			ast_str_append(&buf, 0, "Secret: <redacted from logging>\n");
		} else {
			ast_str_append(&buf, 0, "%s\n", m->headers[x]);
		}
	}

	ast_verbose("<--- Examining AMI action: -->\n%s\n", ast_str_buffer(buf));
	ast_free(buf);
}

/*
 * Done with the action handlers here, we start with the code in charge
 * of accepting connections and serving them.
 * accept_thread() forks a new thread for each connection, session_do(),
 * which in turn calls get_input() repeatedly until a full message has
 * been accumulated, and then invokes process_message() to pass it to
 * the appropriate handler.
 */

/*! \brief
 * Process an AMI message, performing desired action.
 * Return 0 on success, -1 on error that require the session to be destroyed.
 */
static int process_message(struct mansession *s, const struct message *m)
{
	int ret = 0;
	struct manager_action *act_found;
	struct ast_manager_user *user = NULL;
	const char *username;
	const char *action;

	action = __astman_get_header(m, "Action", GET_HEADER_SKIP_EMPTY);
	if (ast_strlen_zero(action)) {
		report_req_bad_format(s, "NONE");
		mansession_lock(s);
		astman_send_error(s, m, "Missing action in request");
		mansession_unlock(s);
		return 0;
	}

	log_action(m, action);

	if (ast_shutting_down()) {
		ast_log(LOG_ERROR, "Unable to process manager action '%s'. Asterisk is shutting down.\n", action);
		mansession_lock(s);
		astman_send_error(s, m, "Asterisk is shutting down");
		mansession_unlock(s);
		return 0;
	}

	if (!s->session->authenticated
		&& strcasecmp(action, "Login")
		&& strcasecmp(action, "Logoff")
		&& strcasecmp(action, "Challenge")) {
		if (!s->session->authenticated) {
			report_req_not_allowed(s, action);
		}
		mansession_lock(s);
		astman_send_error(s, m, "Permission denied");
		mansession_unlock(s);
		return 0;
	}

	if (!s->session->authenticated
		&& (!strcasecmp(action, "Login")
			|| !strcasecmp(action, "Challenge"))) {
		username = astman_get_header(m, "Username");

		if (!ast_strlen_zero(username) && check_manager_session_inuse(username)) {
			AST_RWLIST_WRLOCK(&users);
			user = get_manager_by_name_locked(username);
			if (user && !user->allowmultiplelogin) {
				AST_RWLIST_UNLOCK(&users);
				report_session_limit(s);
				sleep(1);
				mansession_lock(s);
				astman_send_error(s, m, "Login Already In Use");
				mansession_unlock(s);
				return -1;
			}
			AST_RWLIST_UNLOCK(&users);
		}
	}

	act_found = action_find(action);
	if (act_found) {
		/* Found the requested AMI action. */
		int acted = 0;

		if ((s->session->writeperm & act_found->authority)
			|| act_found->authority == 0) {
			/* We have the authority to execute the action. */
			ret = -1;
			ao2_lock(act_found);
			if (act_found->registered && act_found->func) {
				struct ast_module *mod_ref = ast_module_running_ref(act_found->module);

				ao2_unlock(act_found);
				if (mod_ref || !act_found->module) {
					ast_debug(1, "Running action '%s'\n", act_found->action);
					ret = act_found->func(s, m);
					acted = 1;
					ast_module_unref(mod_ref);
				}
			} else {
				ao2_unlock(act_found);
			}
		}
		if (!acted) {
			/*
			 * We did not execute the action because access was denied, it
			 * was no longer registered, or no action was really registered.
			 * Complain about it and leave.
			 */
			report_req_not_allowed(s, action);
			mansession_lock(s);
			astman_send_error(s, m, "Permission denied");
			mansession_unlock(s);
		}
		ao2_t_ref(act_found, -1, "done with found action object");
	} else {
		char buf[512];

		report_req_bad_format(s, action);
		snprintf(buf, sizeof(buf), "Invalid/unknown command: %s. Use Action: ListCommands to show available commands.", action);
		mansession_lock(s);
		astman_send_error(s, m, buf);
		mansession_unlock(s);
	}
	if (ret) {
		return ret;
	}
	/* Once done with our message, deliver any pending events unless the
	   requester doesn't want them as part of this response.
	*/
	if (ast_strlen_zero(astman_get_header(m, "SuppressEvents"))) {
		return process_events(s);
	} else {
		return ret;
	}
}

/*!
 * Read one full line (including crlf) from the manager socket.
 * \note \verbatim
 * \r\n is the only valid terminator for the line.
 * (Note that, later, '\0' will be considered as the end-of-line marker,
 * so everything between the '\0' and the '\r\n' will not be used).
 * Also note that we assume output to have at least "maxlen" space.
 * \endverbatim
 */
static int get_input(struct mansession *s, char *output)
{
	int res, x;
	int maxlen = sizeof(s->session->inbuf) - 1;
	char *src = s->session->inbuf;
	int timeout = -1;
	time_t now;

	/*
	 * Look for \r\n within the buffer. If found, copy to the output
	 * buffer and return, trimming the \r\n (not used afterwards).
	 */
	for (x = 0; x < s->session->inlen; x++) {
		int cr;	/* set if we have \r */
		if (src[x] == '\r' && x+1 < s->session->inlen && src[x + 1] == '\n') {
			cr = 2;	/* Found. Update length to include \r\n */
		} else if (src[x] == '\n') {
			cr = 1;	/* also accept \n only */
		} else {
			continue;
		}
		memmove(output, src, x);	/*... but trim \r\n */
		output[x] = '\0';		/* terminate the string */
		x += cr;			/* number of bytes used */
		s->session->inlen -= x;			/* remaining size */
		memmove(src, src + x, s->session->inlen); /* remove used bytes */
		return 1;
	}
	if (s->session->inlen >= maxlen) {
		/* no crlf found, and buffer full - sorry, too long for us
		 * keep the last character in case we are in the middle of a CRLF. */
		ast_log(LOG_WARNING, "Discarding message from %s. Line too long: %.25s...\n", ast_sockaddr_stringify_addr(&s->session->addr), src);
		src[0] = src[s->session->inlen - 1];
		s->session->inlen = 1;
		s->parsing = MESSAGE_LINE_TOO_LONG;
	}
	res = 0;
	while (res == 0) {
		/* calculate a timeout if we are not authenticated */
		if (!s->session->authenticated) {
			if(time(&now) == -1) {
				ast_log(LOG_ERROR, "error executing time(): %s\n", strerror(errno));
				return -1;
			}

			timeout = (authtimeout - (now - s->session->authstart)) * 1000;
			if (timeout < 0) {
				/* we have timed out */
				return 0;
			}
		}

		ast_mutex_lock(&s->session->notify_lock);
		if (s->session->pending_event) {
			s->session->pending_event = 0;
			ast_mutex_unlock(&s->session->notify_lock);
			return 0;
		}
		s->session->waiting_thread = pthread_self();
		ast_mutex_unlock(&s->session->notify_lock);

		res = ast_wait_for_input(ast_iostream_get_fd(s->session->stream), timeout);

		ast_mutex_lock(&s->session->notify_lock);
		s->session->waiting_thread = AST_PTHREADT_NULL;
		ast_mutex_unlock(&s->session->notify_lock);
	}
	if (res < 0) {
		if (s->session->kicked) {
			ast_debug(1, "Manager session has been kicked\n");
			return -1;
		}
		/* If we get a signal from some other thread (typically because
		 * there are new events queued), return 0 to notify the caller.
		 */
		if (errno == EINTR || errno == EAGAIN) {
			return 0;
		}
		ast_log(LOG_WARNING, "poll() returned error: %s\n", strerror(errno));
		return -1;
	}

	ao2_lock(s->session);
	res = ast_iostream_read(s->session->stream, src + s->session->inlen, maxlen - s->session->inlen);
	if (res < 1) {
		res = -1;	/* error return */
	} else {
		s->session->inlen += res;
		src[s->session->inlen] = '\0';
		res = 0;
	}
	ao2_unlock(s->session);
	return res;
}

/*!
 * \internal
 * \brief Error handling for sending parse errors. This function handles locking, and clearing the
 * parse error flag.
 *
 * \param s AMI session to process action request.
 * \param m Message that's in error.
 * \param error Error message to send.
 */
static void handle_parse_error(struct mansession *s, struct message *m, char *error)
{
	mansession_lock(s);
	astman_send_error(s, m, error);
	s->parsing = MESSAGE_OKAY;
	mansession_unlock(s);
}

/*!
 * \internal
 * \brief Read and process an AMI action request.
 *
 * \param s AMI session to process action request.
 *
 * \retval 0 Retain AMI connection for next command.
 * \retval -1 Drop AMI connection due to logoff or connection error.
 */
static int do_message(struct mansession *s)
{
	struct message m = { 0 };
	char header_buf[sizeof(s->session->inbuf)] = { '\0' };
	int res;
	int hdr_loss;
	time_t now;

	hdr_loss = 0;
	for (;;) {
		/* Check if any events are pending and do them if needed */
		if (process_events(s)) {
			res = -1;
			break;
		}
		res = get_input(s, header_buf);
		if (res == 0) {
			/* No input line received. */
			if (!s->session->authenticated) {
				if (time(&now) == -1) {
					ast_log(LOG_ERROR, "error executing time(): %s\n", strerror(errno));
					res = -1;
					break;
				}

				if (now - s->session->authstart > authtimeout) {
					if (displayconnects) {
						ast_verb(2, "Client from %s, failed to authenticate in %d seconds\n", ast_sockaddr_stringify_addr(&s->session->addr), authtimeout);
					}
					res = -1;
					break;
				}
			}
			continue;
		} else if (res > 0) {
			/* Input line received. */
			if (ast_strlen_zero(header_buf)) {
				if (hdr_loss) {
					mansession_lock(s);
					astman_send_error(s, &m, "Too many lines in message or allocation failure");
					mansession_unlock(s);
					res = 0;
				} else {
					switch (s->parsing) {
					case MESSAGE_OKAY:
						res = process_message(s, &m) ? -1 : 0;
						break;
					case MESSAGE_LINE_TOO_LONG:
						handle_parse_error(s, &m, "Failed to parse message: line too long");
						res = 0;
						break;
					}
				}
				break;
			} else if (m.hdrcount < ARRAY_LEN(m.headers)) {
				m.headers[m.hdrcount] = ast_strdup(header_buf);
				if (!m.headers[m.hdrcount]) {
					/* Allocation failure. */
					hdr_loss = 1;
				} else {
					++m.hdrcount;
				}
			} else {
				/* Too many lines in message. */
				hdr_loss = 1;
			}
		} else {
			/* Input error. */
			break;
		}
	}

	astman_free_headers(&m);

	return res;
}

/*! \brief The body of the individual manager session.
 * Call get_input() to read one line at a time
 * (or be woken up on new events), collect the lines in a
 * message until found an empty line, and execute the request.
 * In any case, deliver events asynchronously through process_events()
 * (called from here if no line is available, or at the end of
 * process_message(). )
 */
static void *session_do(void *data)
{
	struct ast_tcptls_session_instance *ser = data;
	struct mansession_session *session;
	struct mansession s = {
		.tcptls_session = data,
	};
	int res;
	int arg = 1;
	struct ast_sockaddr ser_remote_address_tmp;

	if (ast_atomic_fetchadd_int(&unauth_sessions, +1) >= authlimit) {
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
		goto done;
	}

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	session = build_mansession(&ser_remote_address_tmp);

	if (session == NULL) {
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
		goto done;
	}

	/* here we set TCP_NODELAY on the socket to disable Nagle's algorithm.
	 * This is necessary to prevent delays (caused by buffering) as we
	 * write to the socket in bits and pieces. */
	if (setsockopt(ast_iostream_get_fd(ser->stream), IPPROTO_TCP, TCP_NODELAY, (char *) &arg, sizeof(arg)) < 0) {
		ast_log(LOG_WARNING, "Failed to set TCP_NODELAY on manager connection: %s\n", strerror(errno));
	}
	ast_iostream_nonblock(ser->stream);

	ao2_lock(session);
	/* Hook to the tail of the event queue */
	session->last_ev = grab_last();

	ast_mutex_init(&s.lock);

	/* these fields duplicate those in the 'ser' structure */
	session->stream = s.stream = ser->stream;
	ast_sockaddr_copy(&session->addr, &ser_remote_address_tmp);
	s.session = session;

	AST_LIST_HEAD_INIT_NOLOCK(&session->datastores);

	if(time(&session->authstart) == -1) {
		ast_log(LOG_ERROR, "error executing time(): %s; disconnecting client\n", strerror(errno));
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
		ao2_unlock(session);
		session_destroy(session);
		goto done;
	}
	ao2_unlock(session);

	/*
	 * We cannot let the stream exclusively wait for data to arrive.
	 * We have to wake up the task to send async events.
	 */
	ast_iostream_set_exclusive_input(ser->stream, 0);

	ast_iostream_set_timeout_sequence(ser->stream,
		ast_tvnow(), authtimeout * 1000);

	astman_append(&s, "Asterisk Call Manager/%s\r\n", AMI_VERSION);	/* welcome prompt */
	for (;;) {
		if ((res = do_message(&s)) < 0 || s.write_error || session->kicked) {
			break;
		}
		if (session->authenticated) {
			ast_iostream_set_timeout_disable(ser->stream);
		}
	}
	/* session is over, explain why and terminate */
	if (session->authenticated) {
		if (manager_displayconnects(session)) {
			ast_verb(2, "Manager '%s' %s from %s\n", session->username, session->kicked ? "kicked" : "logged off", ast_sockaddr_stringify_addr(&session->addr));
		}
	} else {
		ast_atomic_fetchadd_int(&unauth_sessions, -1);
		if (displayconnects) {
			ast_verb(2, "Connect attempt from '%s' unable to authenticate\n", ast_sockaddr_stringify_addr(&session->addr));
		}
	}

	session_destroy(session);

	ast_mutex_destroy(&s.lock);
done:
	ao2_ref(ser, -1);
	ser = NULL;
	return NULL;
}

/*! \brief remove at most n_max stale session from the list. */
static int purge_sessions(int n_max)
{
	struct ao2_container *sessions;
	struct mansession_session *session;
	time_t now = time(NULL);
	struct ao2_iterator i;
	int purged = 0;

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return 0;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);

	/* The order of operations is significant */
	while (n_max > 0 && (session = ao2_iterator_next(&i))) {
		ao2_lock(session);
		if (session->sessiontimeout && (now > session->sessiontimeout) && !session->inuse) {
			if (session->authenticated
				&& VERBOSITY_ATLEAST(2)
				&& manager_displayconnects(session)) {
				ast_verb(2, "HTTP Manager '%s' timed out from %s\n",
					session->username, ast_sockaddr_stringify_addr(&session->addr));
			}
			ao2_unlock(session);
			session_destroy(session);
			n_max--;
			purged++;
		} else {
			ao2_unlock(session);
			unref_mansession(session);
		}
	}
	ao2_iterator_destroy(&i);
	return purged;
}

/*! \brief
 * events are appended to a queue from where they
 * can be dispatched to clients.
 */
static int append_event(const char *str, int event_name_hash, int category)
{
	struct eventqent *tmp = ast_malloc(sizeof(*tmp) + strlen(str));
	static int seq;	/* sequence number */

	if (!tmp) {
		return -1;
	}

	/* need to init all fields, because ast_malloc() does not */
	tmp->usecount = 0;
	tmp->category = category;
	tmp->seq = ast_atomic_fetchadd_int(&seq, 1);
	tmp->tv = ast_tvnow();
	tmp->event_name_hash = event_name_hash;
	AST_RWLIST_NEXT(tmp, eq_next) = NULL;
	strcpy(tmp->eventdata, str);

	AST_RWLIST_WRLOCK(&all_events);
	AST_RWLIST_INSERT_TAIL(&all_events, tmp, eq_next);
	AST_RWLIST_UNLOCK(&all_events);

	return 0;
}

static void append_channel_vars(struct ast_str **pbuf, struct ast_channel *chan)
{
	struct varshead *vars;
	struct ast_var_t *var;

	vars = ast_channel_get_manager_vars(chan);
	if (!vars) {
		return;
	}

	AST_LIST_TRAVERSE(vars, var, entries) {
		ast_str_append(pbuf, 0, "ChanVariable(%s): %s=%s\r\n", ast_channel_name(chan), var->name, var->value);
	}
	ao2_ref(vars, -1);
}

/* XXX see if can be moved inside the function */
AST_THREADSTORAGE(manager_event_buf);
#define MANAGER_EVENT_BUF_INITSIZE   256

static int __attribute__((format(printf, 9, 0))) __manager_event_sessions_va(
	struct ao2_container *sessions,
	int category,
	const char *event,
	int chancount,
	struct ast_channel **chans,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list ap)
{
	struct ast_str *auth = ast_str_alloca(MAX_AUTH_PERM_STRING);
	const char *cat_str;
	struct timeval now;
	struct ast_str *buf;
	int i;
	int event_name_hash;

	if (!ast_strlen_zero(manager_disabledevents)) {
		if (ast_in_delimited_string(event, manager_disabledevents, ',')) {
			ast_debug(3, "AMI Event '%s' is globally disabled, skipping\n", event);
			/* Event is globally disabled */
			return -1;
		}
	}

	buf = ast_str_thread_get(&manager_event_buf, MANAGER_EVENT_BUF_INITSIZE);
	if (!buf) {
		return -1;
	}

	cat_str = authority_to_str(category, &auth);
	ast_str_set(&buf, 0,
		"Event: %s\r\n"
		"Privilege: %s\r\n",
		event, cat_str);

	if (timestampevents) {
		now = ast_tvnow();
		ast_str_append(&buf, 0,
			"Timestamp: %ld.%06lu\r\n",
			(long)now.tv_sec, (unsigned long) now.tv_usec);
	}
	if (manager_debug) {
		static int seq;

		ast_str_append(&buf, 0,
			"SequenceNumber: %d\r\n",
			ast_atomic_fetchadd_int(&seq, 1));
		ast_str_append(&buf, 0,
			"File: %s\r\n"
			"Line: %d\r\n"
			"Func: %s\r\n",
			file, line, func);
	}
	if (!ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
		ast_str_append(&buf, 0,
			"SystemName: %s\r\n",
			ast_config_AST_SYSTEM_NAME);
	}

	ast_str_append_va(&buf, 0, fmt, ap);
	for (i = 0; i < chancount; i++) {
		append_channel_vars(&buf, chans[i]);
	}

	ast_str_append(&buf, 0, "\r\n");

	event_name_hash = ast_str_hash(event);

	append_event(ast_str_buffer(buf), event_name_hash, category);

	/* Wake up any sleeping sessions */
	if (sessions) {
		struct ao2_iterator iter;
		struct mansession_session *session;

		iter = ao2_iterator_init(sessions, 0);
		while ((session = ao2_iterator_next(&iter))) {
			ast_mutex_lock(&session->notify_lock);
			if (session->waiting_thread != AST_PTHREADT_NULL) {
				pthread_kill(session->waiting_thread, SIGURG);
			} else {
				/* We have an event to process, but the mansession is
				 * not waiting for it. We still need to indicate that there
				 * is an event waiting so that get_input processes the pending
				 * event instead of polling.
				 */
				session->pending_event = 1;
			}
			ast_mutex_unlock(&session->notify_lock);
			unref_mansession(session);
		}
		ao2_iterator_destroy(&iter);
	}

	if (category != EVENT_FLAG_SHUTDOWN && !AST_RWLIST_EMPTY(&manager_hooks)) {
		struct manager_custom_hook *hook;

		AST_RWLIST_RDLOCK(&manager_hooks);
		AST_RWLIST_TRAVERSE(&manager_hooks, hook, list) {
			hook->helper(category, event, ast_str_buffer(buf));
		}
		AST_RWLIST_UNLOCK(&manager_hooks);
	}

	return 0;
}

static int __attribute__((format(printf, 9, 0))) __manager_event_sessions(
	struct ao2_container *sessions,
	int category,
	const char *event,
	int chancount,
	struct ast_channel **chans,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	...)
{
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = __manager_event_sessions_va(sessions, category, event,
		chancount, chans, file, line, func, fmt, ap);
	va_end(ap);
	return res;
}

int __ast_manager_event_multichan(int category, const char *event, int chancount,
	struct ast_channel **chans, const char *file, int line, const char *func,
	const char *fmt, ...)
{
	struct ao2_container *sessions = ao2_global_obj_ref(mgr_sessions);
	va_list ap;
	int res;

	if (!any_manager_listeners(sessions)) {
		/* Nobody is listening */
		ao2_cleanup(sessions);
		return 0;
	}

	va_start(ap, fmt);
	res = __manager_event_sessions_va(sessions, category, event, chancount, chans,
		file, line, func, fmt, ap);
	va_end(ap);
	ao2_cleanup(sessions);
	return res;
}

/*! \brief
 * support functions to register/unregister AMI action handlers,
 */
int ast_manager_unregister(const char *action)
{
	struct manager_action *cur;

	AST_RWLIST_WRLOCK(&actions);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&actions, cur, list) {
		if (!strcasecmp(action, cur->action)) {
			AST_RWLIST_REMOVE_CURRENT(list);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&actions);

	if (cur) {
		/*
		 * We have removed the action object from the container so we
		 * are no longer in a hurry.
		 */
		ao2_lock(cur);
		cur->registered = 0;
		ao2_unlock(cur);

		ao2_t_ref(cur, -1, "action object removed from list");
		ast_verb(5, "Manager unregistered action %s\n", action);
	}

	return 0;
}

static int manager_state_cb(const char *context, const char *exten, struct ast_state_cb_info *info, void *data)
{
	/* Notify managers of change */
	char hint[512];

	hint[0] = '\0';
	ast_get_hint(hint, sizeof(hint), NULL, 0, NULL, context, exten);

	switch(info->reason) {
	case AST_HINT_UPDATE_DEVICE:
		manager_event(EVENT_FLAG_CALL, "ExtensionStatus",
			"Exten: %s\r\n"
			"Context: %s\r\n"
			"Hint: %s\r\n"
			"Status: %d\r\n"
			"StatusText: %s\r\n",
			exten,
			context,
			hint,
			info->exten_state,
			ast_extension_state2str(info->exten_state));
		break;
	case AST_HINT_UPDATE_PRESENCE:
		manager_event(EVENT_FLAG_CALL, "PresenceStatus",
			"Exten: %s\r\n"
			"Context: %s\r\n"
			"Hint: %s\r\n"
			"Status: %s\r\n"
			"Subtype: %s\r\n"
			"Message: %s\r\n",
			exten,
			context,
			hint,
			ast_presence_state2str(info->presence_state),
			info->presence_subtype,
			info->presence_message);
		break;
	}
	return 0;
}

static int ast_manager_register_struct(struct manager_action *act)
{
	struct manager_action *cur, *prev = NULL;

	AST_RWLIST_WRLOCK(&actions);
	AST_RWLIST_TRAVERSE(&actions, cur, list) {
		int ret;

		ret = strcasecmp(cur->action, act->action);
		if (ret == 0) {
			ast_log(LOG_WARNING, "Manager: Action '%s' already registered\n", act->action);
			AST_RWLIST_UNLOCK(&actions);
			return -1;
		}
		if (ret > 0) { /* Insert these alphabetically */
			break;
		}
		prev = cur;
	}

	ao2_t_ref(act, +1, "action object added to list");
	act->registered = 1;
	if (prev) {
		AST_RWLIST_INSERT_AFTER(&actions, prev, act, list);
	} else {
		AST_RWLIST_INSERT_HEAD(&actions, act, list);
	}

	ast_verb(5, "Manager registered action %s\n", act->action);

	AST_RWLIST_UNLOCK(&actions);

	return 0;
}

/*!
 * \internal
 * \brief Destroy the registered AMI action object.
 *
 * \param obj Object to destroy.
 */
static void action_destroy(void *obj)
{
	struct manager_action *doomed = obj;

	if (doomed->synopsis) {
		/* The string fields were initialized. */
		ast_string_field_free_memory(doomed);
	}
	ao2_cleanup(doomed->final_response);
	ao2_cleanup(doomed->list_responses);
}

/*! \brief register a new command with manager, including online help. This is
	the preferred way to register a manager command */
int ast_manager_register2(const char *action, int auth, int (*func)(struct mansession *s, const struct message *m), struct ast_module *module, const char *synopsis, const char *description)
{
	struct manager_action *cur;

	cur = ao2_t_alloc(sizeof(*cur), action_destroy, action);
	if (!cur) {
		return -1;
	}
	if (ast_string_field_init(cur, 128)) {
		ao2_t_ref(cur, -1, "action object creation failed");
		return -1;
	}

	if (ast_string_field_init_extended(cur, since)) {
		ao2_t_ref(cur, -1, "action object creation failed");
		return -1;
	}

	cur->action = action;
	cur->authority = auth;
	cur->func = func;
	cur->module = module;
#ifdef AST_XML_DOCS
	if (ast_strlen_zero(synopsis) && ast_strlen_zero(description)) {
		char *tmpxml;

		tmpxml = ast_xmldoc_build_since("manager", action, NULL);
		ast_string_field_set(cur, since, tmpxml);
		ast_free(tmpxml);

		tmpxml = ast_xmldoc_build_synopsis("manager", action, NULL);
		ast_string_field_set(cur, synopsis, tmpxml);
		ast_free(tmpxml);

		tmpxml = ast_xmldoc_build_syntax("manager", action, NULL);
		ast_string_field_set(cur, syntax, tmpxml);
		ast_free(tmpxml);

		tmpxml = ast_xmldoc_build_description("manager", action, NULL);
		ast_string_field_set(cur, description, tmpxml);
		ast_free(tmpxml);

		tmpxml = ast_xmldoc_build_seealso("manager", action, NULL);
		ast_string_field_set(cur, seealso, tmpxml);
		ast_free(tmpxml);

		tmpxml = ast_xmldoc_build_arguments("manager", action, NULL);
		ast_string_field_set(cur, arguments, tmpxml);
		ast_free(tmpxml);

		cur->final_response = ast_xmldoc_build_final_response("manager", action, NULL);
		cur->list_responses = ast_xmldoc_build_list_responses("manager", action, NULL);

		cur->docsrc = AST_XML_DOC;
	} else
#endif
	{
		ast_string_field_set(cur, synopsis, synopsis);
		ast_string_field_set(cur, description, description);
#ifdef AST_XML_DOCS
		cur->docsrc = AST_STATIC_DOC;
#endif
	}
	if (ast_manager_register_struct(cur)) {
		ao2_t_ref(cur, -1, "action object registration failed");
		return -1;
	}

	ao2_t_ref(cur, -1, "action object registration successful");
	return 0;
}
/*! @}
 END Doxygen group */

/*
 * The following are support functions for AMI-over-http.
 * The common entry point is generic_http_callback(),
 * which extracts HTTP header and URI fields and reformats
 * them into AMI messages, locates a proper session
 * (using the mansession_id Cookie or GET variable),
 * and calls process_message() as for regular AMI clients.
 * When done, the output (which goes to a temporary file)
 * is read back into a buffer and reformatted as desired,
 * then fed back to the client over the original socket.
 */

enum output_format {
	FORMAT_RAW,
	FORMAT_HTML,
	FORMAT_XML,
};

static const char * const contenttype[] = {
	[FORMAT_RAW] = "plain",
	[FORMAT_HTML] = "html",
	[FORMAT_XML] =  "xml",
};

/*!
 * locate an http session in the list. The search key (ident) is
 * the value of the mansession_id cookie (0 is not valid and means
 * a session on the AMI socket).
 */
static struct mansession_session *find_session(uint32_t ident, int incinuse)
{
	struct ao2_container *sessions;
	struct mansession_session *session;
	struct ao2_iterator i;

	if (ident == 0) {
		return NULL;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return NULL;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);
	while ((session = ao2_iterator_next(&i))) {
		ao2_lock(session);
		if (session->managerid == ident && !session->needdestroy) {
			ast_atomic_fetchadd_int(&session->inuse, incinuse ? 1 : 0);
			break;
		}
		ao2_unlock(session);
		unref_mansession(session);
	}
	ao2_iterator_destroy(&i);

	return session;
}

/*!
 * locate an http session in the list.
 * The search keys (nonce) and (username) is value from received
 * "Authorization" http header.
 * As well as in find_session() function, the value of the nonce can't be zero.
 * (0 meansi, that the session used for AMI socket connection).
 * Flag (stale) is set, if client used valid, but old, nonce value.
 *
 */
static struct mansession_session *find_session_by_nonce(const char *username, unsigned long nonce, int *stale)
{
	struct mansession_session *session;
	struct ao2_container *sessions;
	struct ao2_iterator i;

	if (nonce == 0 || username == NULL || stale == NULL) {
		return NULL;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return NULL;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);
	while ((session = ao2_iterator_next(&i))) {
		ao2_lock(session);
		if (!strcasecmp(session->username, username) && session->managerid == nonce) {
			*stale = 0;
			break;
		} else if (!strcasecmp(session->username, username) && session->oldnonce == nonce) {
			*stale = 1;
			break;
		}
		ao2_unlock(session);
		unref_mansession(session);
	}
	ao2_iterator_destroy(&i);

	return session;
}

int astman_is_authed(uint32_t ident)
{
	int authed;
	struct mansession_session *session;

	if (!(session = find_session(ident, 0)))
		return 0;

	authed = (session->authenticated != 0);

	ao2_unlock(session);
	unref_mansession(session);

	return authed;
}

int astman_verify_session_readpermissions(uint32_t ident, int perm)
{
	int result = 0;
	struct mansession_session *session;
	struct ao2_container *sessions;
	struct ao2_iterator i;

	if (ident == 0) {
		return 0;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return 0;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);
	while ((session = ao2_iterator_next(&i))) {
		ao2_lock(session);
		if ((session->managerid == ident) && (session->readperm & perm)) {
			result = 1;
			ao2_unlock(session);
			unref_mansession(session);
			break;
		}
		ao2_unlock(session);
		unref_mansession(session);
	}
	ao2_iterator_destroy(&i);

	return result;
}

int astman_verify_session_writepermissions(uint32_t ident, int perm)
{
	int result = 0;
	struct mansession_session *session;
	struct ao2_container *sessions;
	struct ao2_iterator i;

	if (ident == 0) {
		return 0;
	}

	sessions = ao2_global_obj_ref(mgr_sessions);
	if (!sessions) {
		return 0;
	}
	i = ao2_iterator_init(sessions, 0);
	ao2_ref(sessions, -1);
	while ((session = ao2_iterator_next(&i))) {
		ao2_lock(session);
		if ((session->managerid == ident) && (session->writeperm & perm)) {
			result = 1;
			ao2_unlock(session);
			unref_mansession(session);
			break;
		}
		ao2_unlock(session);
		unref_mansession(session);
	}
	ao2_iterator_destroy(&i);

	return result;
}

/*
 * convert to xml with various conversion:
 * mode & 1	-> lowercase;
 * mode & 2	-> replace non-alphanumeric chars with underscore
 */
static void xml_copy_escape(struct ast_str **out, const char *src, int mode)
{
	/* store in a local buffer to avoid calling ast_str_append too often */
	char buf[256];
	char *dst = buf;
	const char *save = src;
	int space = sizeof(buf);
	/* repeat until done and nothing to flush */
	for ( ; *src || dst != buf ; src++) {
		if (*src == '\0' || space < 10) {	/* flush */
			*dst++ = '\0';
			ast_str_append(out, 0, "%s", buf);
			dst = buf;
			space = sizeof(buf);
			if (*src == '\0') {
				break;
			}
		}

		if (mode & 2) {
			if (save == src && isdigit(*src)) {
				/* The first character of an XML attribute cannot be a digit */
				*dst++ = '_';
				*dst++ = *src;
				space -= 2;
				continue;
			} else if (!isalnum(*src)) {
				/* Replace non-alphanumeric with an underscore */
				*dst++ = '_';
				space--;
				continue;
			}
		}
		switch (*src) {
		case '<':
			strcpy(dst, "&lt;");
			dst += 4;
			space -= 4;
			break;
		case '>':
			strcpy(dst, "&gt;");
			dst += 4;
			space -= 4;
			break;
		case '\"':
			strcpy(dst, "&quot;");
			dst += 6;
			space -= 6;
			break;
		case '\'':
			strcpy(dst, "&apos;");
			dst += 6;
			space -= 6;
			break;
		case '&':
			strcpy(dst, "&amp;");
			dst += 5;
			space -= 5;
			break;

		default:
			*dst++ = mode ? tolower(*src) : *src;
			space--;
		}
	}
}

struct variable_count {
	char *varname;
	int count;
};

static int variable_count_hash_fn(const void *vvc, const int flags)
{
	const struct variable_count *vc = vvc;

	return ast_str_hash(vc->varname);
}

static int variable_count_cmp_fn(void *obj, void *vstr, int flags)
{
	/* Due to the simplicity of struct variable_count, it makes no difference
	 * if you pass in objects or strings, the same operation applies. This is
	 * due to the fact that the hash occurs on the first element, which means
	 * the address of both the struct and the string are exactly the same. */
	struct variable_count *vc = obj;
	char *str = vstr;
	return !strcmp(vc->varname, str) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Convert the input into XML or HTML.
 * The input is supposed to be a sequence of lines of the form
 *	Name: value
 * optionally followed by a blob of unformatted text.
 * A blank line is a section separator. Basically, this is a
 * mixture of the format of Manager Interface and CLI commands.
 * The unformatted text is considered as a single value of a field
 * named 'Opaque-data'.
 *
 * At the moment the output format is the following (but it may
 * change depending on future requirements so don't count too
 * much on it when writing applications):
 *
 * General: the unformatted text is used as a value of
 * XML output:  to be completed
 *
 * \verbatim
 *   Each section is within <response type="object" id="xxx">
 *   where xxx is taken from ajaxdest variable or defaults to unknown
 *   Each row is reported as an attribute Name="value" of an XML
 *   entity named from the variable ajaxobjtype, default to "generic"
 * \endverbatim
 *
 * HTML output:
 *   each Name-value pair is output as a single row of a two-column table.
 *   Sections (blank lines in the input) are separated by a <HR>
 *
 */
static void xml_translate(struct ast_str **out, char *in, struct ast_variable *get_vars, enum output_format format)
{
	struct ast_variable *v;
	const char *dest = NULL;
	char *var, *val;
	const char *objtype = NULL;
	int in_data = 0;	/* parsing data */
	int inobj = 0;
	int xml = (format == FORMAT_XML);
	struct variable_count *vc = NULL;
	struct ao2_container *vco = NULL;

	if (xml) {
		/* dest and objtype need only for XML format */
		for (v = get_vars; v; v = v->next) {
			if (!strcasecmp(v->name, "ajaxdest")) {
				dest = v->value;
			} else if (!strcasecmp(v->name, "ajaxobjtype")) {
				objtype = v->value;
			}
		}
		if (ast_strlen_zero(dest)) {
			dest = "unknown";
		}
		if (ast_strlen_zero(objtype)) {
			objtype = "generic";
		}
	}

	/* we want to stop when we find an empty line */
	while (in && *in) {
		val = strsep(&in, "\r\n");	/* mark start and end of line */
		if (in && *in == '\n') {	/* remove trailing \n if any */
			in++;
		}
		ast_trim_blanks(val);
		ast_debug(5, "inobj %d in_data %d line <%s>\n", inobj, in_data, val);
		if (ast_strlen_zero(val)) {
			/* empty line */
			if (in_data) {
				/* close data in Opaque mode */
				ast_str_append(out, 0, xml ? "'" : "</td></tr>\n");
				in_data = 0;
			}

			if (inobj) {
				/* close block */
				ast_str_append(out, 0, xml ? " /></response>\n" :
					"<tr><td colspan=\"2\"><hr></td></tr>\r\n");
				inobj = 0;
				ao2_ref(vco, -1);
				vco = NULL;
			}
			continue;
		}

		if (!inobj) {
			/* start new block */
			if (xml) {
				ast_str_append(out, 0, "<response type='object' id='%s'><%s", dest, objtype);
			}
			vco = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, 37,
				variable_count_hash_fn, NULL, variable_count_cmp_fn);
			inobj = 1;
		}

		if (in_data) {
			/* Process data field in Opaque mode. This is a
			 * followup, so we re-add line feeds. */
			ast_str_append(out, 0, xml ? "\n" : "<br>\n");
			xml_copy_escape(out, val, 0);   /* data field */
			continue;
		}

		/* We expect "Name: value" line here */
		var = strsep(&val, ":");
		if (val) {
			/* found the field name */
			val = ast_skip_blanks(val);
			ast_trim_blanks(var);
		} else {
			/* field name not found, switch to opaque mode */
			val = var;
			var = "Opaque-data";
			in_data = 1;
		}


		ast_str_append(out, 0, xml ? " " : "<tr><td>");
		if ((vc = ao2_find(vco, var, 0))) {
			vc->count++;
		} else {
			/* Create a new entry for this one */
			vc = ao2_alloc(sizeof(*vc), NULL);
			vc->varname = var;
			vc->count = 1;
			ao2_link(vco, vc);
		}

		xml_copy_escape(out, var, xml ? 1 | 2 : 0); /* data name */
		if (vc->count > 1) {
			ast_str_append(out, 0, "-%d", vc->count);
		}
		ao2_ref(vc, -1);
		ast_str_append(out, 0, xml ? "='" : "</td><td>");
		xml_copy_escape(out, val, 0);	/* data field */
		if (!in_data || !*in) {
			ast_str_append(out, 0, xml ? "'" : "</td></tr>\n");
		}
	}

	if (inobj) {
		ast_str_append(out, 0, xml ? " /></response>\n" :
			"<tr><td colspan=\"2\"><hr></td></tr>\r\n");
		ao2_ref(vco, -1);
	}
}

static void close_mansession_file(struct mansession *s)
{
	if (s->stream) {
		ast_iostream_close(s->stream);
		s->stream = NULL;
	} else {
		ast_log(LOG_ERROR, "Attempted to close file/file descriptor on mansession without a valid file or file descriptor.\n");
	}
}

static void process_output(struct mansession *s, struct ast_str **out, struct ast_variable *params, enum output_format format)
{
	char *buf;
	off_t l;
	int fd;

	if (!s->stream)
		return;

	/* Ensure buffer is NULL-terminated */
	ast_iostream_write(s->stream, "", 1);

	fd = ast_iostream_get_fd(s->stream);

	l = lseek(fd, 0, SEEK_CUR);
	if (l > 0) {
		if (MAP_FAILED == (buf = mmap(NULL, l, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0))) {
			ast_log(LOG_WARNING, "mmap failed.  Manager output was not processed\n");
		} else {
			if (format == FORMAT_XML || format == FORMAT_HTML) {
				xml_translate(out, buf, params, format);
			} else {
				ast_str_append(out, 0, "%s", buf);
			}
			munmap(buf, l);
		}
	} else if (format == FORMAT_XML || format == FORMAT_HTML) {
		xml_translate(out, "", params, format);
	}

	close_mansession_file(s);
}

static int generic_http_callback(struct ast_tcptls_session_instance *ser,
					     enum ast_http_method method,
					     enum output_format format,
					     const struct ast_sockaddr *remote_address, const char *uri,
					     struct ast_variable *get_params,
					     struct ast_variable *headers)
{
	struct mansession s = { .session = NULL, .tcptls_session = ser };
	struct mansession_session *session = NULL;
	uint32_t ident;
	int fd;
	int blastaway = 0;
	struct ast_variable *params = get_params;
	char template[] = "/tmp/ast-http-XXXXXX";	/* template for temporary file */
	struct ast_str *http_header = NULL, *out = NULL;
	struct message m = { 0 };

	if (method != AST_HTTP_GET && method != AST_HTTP_HEAD && method != AST_HTTP_POST) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return 0;
	}

	ident = ast_http_manid_from_vars(headers);

	if (!(session = find_session(ident, 1))) {

		/**/
		/* Create new session.
		 * While it is not in the list we don't need any locking
		 */
		if (!(session = build_mansession(remote_address))) {
			ast_http_request_close_on_completion(ser);
			ast_http_error(ser, 500, "Server Error", "Internal Server Error (out of memory)");
			return 0;
		}
		ao2_lock(session);
		session->send_events = 0;
		session->inuse = 1;
		/*!
		 * \note There is approximately a 1 in 1.8E19 chance that the following
		 * calculation will produce 0, which is an invalid ID, but due to the
		 * properties of the rand() function (and the constancy of s), that
		 * won't happen twice in a row.
		 */
		while ((session->managerid = ast_random() ^ (unsigned long) session) == 0) {
		}
		session->last_ev = grab_last();
		AST_LIST_HEAD_INIT_NOLOCK(&session->datastores);
	}
	ao2_unlock(session);

	http_header = ast_str_create(128);
	out = ast_str_create(2048);

	ast_mutex_init(&s.lock);

	if (http_header == NULL || out == NULL) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (ast_str_create() out of memory)");
		goto generic_callback_out;
	}

	s.session = session;
	fd = mkstemp(template);	/* create a temporary file for command output */
	unlink(template);
	if (fd <= -1) {
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (mkstemp failed)");
		goto generic_callback_out;
	}
	s.stream = ast_iostream_from_fd(&fd);
	if (!s.stream) {
		ast_log(LOG_WARNING, "HTTP Manager, fdopen failed: %s!\n", strerror(errno));
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (fdopen failed)");
		close(fd);
		goto generic_callback_out;
	}

	if (method == AST_HTTP_POST) {
		params = ast_http_get_post_vars(ser, headers);
		if (!params) {
			switch (errno) {
			case EFBIG:
				ast_http_error(ser, 413, "Request Entity Too Large", "Body too large");
				close_mansession_file(&s);
				goto generic_callback_out;
			case ENOMEM:
				ast_http_request_close_on_completion(ser);
				ast_http_error(ser, 500, "Server Error", "Out of memory");
				close_mansession_file(&s);
				goto generic_callback_out;
			case EIO:
				ast_http_error(ser, 400, "Bad Request", "Error parsing request body");
				close_mansession_file(&s);
				goto generic_callback_out;
			}
		}
	}

	astman_append_headers(&m, params);

	if (process_message(&s, &m)) {
		if (session->authenticated) {
			if (manager_displayconnects(session)) {
				ast_verb(2, "HTTP Manager '%s' logged off from %s\n", session->username, ast_sockaddr_stringify_addr(&session->addr));
			}
		} else {
			if (displayconnects) {
				ast_verb(2, "HTTP Connect attempt from '%s' unable to authenticate\n", ast_sockaddr_stringify_addr(&session->addr));
			}
		}
		session->needdestroy = 1;
	}

	astman_free_headers(&m);

	ast_str_append(&http_header, 0,
		"Content-type: text/%s\r\n"
		"Set-Cookie: mansession_id=\"%08x\"; Version=1; Max-Age=%d\r\n"
		"Pragma: SuppressEvents\r\n",
		contenttype[format],
		session->managerid, httptimeout);

	if (format == FORMAT_XML) {
		ast_str_append(&out, 0, "<ajax-response>\n");
	} else if (format == FORMAT_HTML) {
		/*
		 * When handling AMI-over-HTTP in HTML format, we provide a simple form for
		 * debugging purposes. This HTML code should not be here, we
		 * should read from some config file...
		 */

#define ROW_FMT	"<tr><td colspan=\"2\" bgcolor=\"#f1f1ff\">%s</td></tr>\r\n"
#define TEST_STRING \
	"<form action=\"manager\" method=\"post\">\n\
	Action: <select name=\"action\">\n\
		<option value=\"\">-----&gt;</option>\n\
		<option value=\"login\">login</option>\n\
		<option value=\"command\">Command</option>\n\
		<option value=\"waitevent\">waitevent</option>\n\
		<option value=\"listcommands\">listcommands</option>\n\
	</select>\n\
	or <input name=\"action\"><br/>\n\
	CLI Command <input name=\"command\"><br>\n\
	user <input name=\"username\"> pass <input type=\"password\" name=\"secret\"><br>\n\
	<input type=\"submit\">\n</form>\n"

		ast_str_append(&out, 0, "<title>Asterisk&trade; Manager Interface</title>");
		ast_str_append(&out, 0, "<body bgcolor=\"#ffffff\"><table align=center bgcolor=\"#f1f1f1\" width=\"500\">\r\n");
		ast_str_append(&out, 0, ROW_FMT, "<h1>Manager Tester</h1>");
		ast_str_append(&out, 0, ROW_FMT, TEST_STRING);
	}

	process_output(&s, &out, params, format);

	if (format == FORMAT_XML) {
		ast_str_append(&out, 0, "</ajax-response>\n");
	} else if (format == FORMAT_HTML) {
		ast_str_append(&out, 0, "</table></body>\r\n");
	}

	ao2_lock(session);
	/* Reset HTTP timeout.  If we're not authenticated, keep it extremely short */
	session->sessiontimeout = time(NULL) + ((session->authenticated || httptimeout < 5) ? httptimeout : 5);

	if (session->needdestroy) {
		if (session->inuse == 1) {
			ast_debug(1, "Need destroy, doing it now!\n");
			blastaway = 1;
		} else {
			ast_debug(1, "Need destroy, but can't do it yet!\n");
			ast_mutex_lock(&session->notify_lock);
			if (session->waiting_thread != AST_PTHREADT_NULL) {
				pthread_kill(session->waiting_thread, SIGURG);
			}
			ast_mutex_unlock(&session->notify_lock);
			session->inuse--;
		}
	} else {
		session->inuse--;
	}
	ao2_unlock(session);

	ast_http_send(ser, method, 200, NULL, http_header, out, 0, 0);
	http_header = NULL;
	out = NULL;

generic_callback_out:
	ast_mutex_destroy(&s.lock);

	/* Clear resource */

	if (method == AST_HTTP_POST && params) {
		ast_variables_destroy(params);
	}
	ast_free(http_header);
	ast_free(out);

	if (session) {
		if (blastaway) {
			session_destroy(session);
		} else {
			if (session->stream) {
				ast_iostream_close(session->stream);
				session->stream = NULL;
			}
			unref_mansession(session);
		}
	}

	return 0;
}

static int auth_http_callback(struct ast_tcptls_session_instance *ser,
					     enum ast_http_method method,
					     enum output_format format,
					     const struct ast_sockaddr *remote_address, const char *uri,
					     struct ast_variable *get_params,
					     struct ast_variable *headers)
{
	struct mansession_session *session = NULL;
	struct mansession s = { .session = NULL, .tcptls_session = ser };
	struct ast_variable *v, *params = get_params;
	char template[] = "/tmp/ast-http-XXXXXX";	/* template for temporary file */
	struct ast_str *http_header = NULL, *out = NULL;
	size_t result_size;
	struct message m = { 0 };
	int fd;

	time_t time_now = time(NULL);
	unsigned long nonce = 0, nc;
	struct ast_http_digest d = { NULL, };
	struct ast_manager_user *user = NULL;
	int stale = 0;
	char resp_hash[256]="";
	/* Cache for user data */
	char u_username[80];
	int u_readperm;
	int u_writeperm;
	int u_writetimeout;
	int u_displayconnects;

	if (method != AST_HTTP_GET && method != AST_HTTP_HEAD && method != AST_HTTP_POST) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return 0;
	}

	/* Find "Authorization: " header */
	for (v = headers; v; v = v->next) {
		if (!strcasecmp(v->name, "Authorization")) {
			break;
		}
	}

	if (!v || ast_strlen_zero(v->value)) {
		goto out_401; /* Authorization Header not present - send auth request */
	}

	/* Digest found - parse */
	if (ast_string_field_init(&d, 128)) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (out of memory)");
		return 0;
	}

	if (ast_parse_digest(v->value, &d, 0, 1)) {
		/* Error in Digest - send new one */
		nonce = 0;
		goto out_401;
	}
	if (sscanf(d.nonce, "%30lx", &nonce) != 1) {
		ast_log(LOG_WARNING, "Received incorrect nonce in Digest <%s>\n", d.nonce);
		nonce = 0;
		goto out_401;
	}

	AST_RWLIST_WRLOCK(&users);
	user = get_manager_by_name_locked(d.username);
	if(!user) {
		AST_RWLIST_UNLOCK(&users);
		ast_log(LOG_NOTICE, "%s tried to authenticate with nonexistent user '%s'\n", ast_sockaddr_stringify_addr(&session->addr), d.username);
		nonce = 0;
		goto out_401;
	}

	/* --- We have User for this auth, now check ACL */
	if (user->acl && !ast_apply_acl(user->acl, remote_address, "Manager User ACL:")) {
		AST_RWLIST_UNLOCK(&users);
		ast_log(LOG_NOTICE, "%s failed to pass IP ACL as '%s'\n", ast_sockaddr_stringify_addr(&session->addr), d.username);
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 403, "Permission denied", "Permission denied");
		return 0;
	}

	/* --- We have auth, so check it */

	/* compute the expected response to compare with what we received */
	{
		char *a2;
		/* ast_md5_hash outputs 32 characters plus NULL terminator. */
		char a2_hash[33];
		char resp[256];

		/* XXX Now request method are hardcoded in A2 */
		if (ast_asprintf(&a2, "%s:%s", ast_get_http_method(method), d.uri) < 0) {
			AST_RWLIST_UNLOCK(&users);
			ast_http_request_close_on_completion(ser);
			ast_http_error(ser, 500, "Server Error", "Internal Server Error (out of memory)");
			return 0;
		}

		ast_md5_hash(a2_hash, a2);
		ast_free(a2);

		if (d.qop) {
			/* RFC 2617 */
			snprintf(resp, sizeof(resp), "%s:%08lx:%s:%s:auth:%s", user->a1_hash, nonce, d.nc, d.cnonce, a2_hash);
		}  else {
			/* RFC 2069 */
			snprintf(resp, sizeof(resp), "%s:%08lx:%s", user->a1_hash, nonce, a2_hash);
		}
		ast_md5_hash(resp_hash, resp);
	}

	if (strncasecmp(d.response, resp_hash, strlen(resp_hash))) {
		/* Something was wrong, so give the client to try with a new challenge */
		AST_RWLIST_UNLOCK(&users);
		nonce = 0;
		goto out_401;
	}

	/*
	 * User are pass Digest authentication.
	 * Now, cache the user data and unlock user list.
	 */
	ast_copy_string(u_username, user->username, sizeof(u_username));
	u_readperm = user->readperm;
	u_writeperm = user->writeperm;
	u_displayconnects = user->displayconnects;
	u_writetimeout = user->writetimeout;
	AST_RWLIST_UNLOCK(&users);

	if (!(session = find_session_by_nonce(d.username, nonce, &stale))) {
		/*
		 * Create new session.
		 * While it is not in the list we don't need any locking
		 */
		if (!(session = build_mansession(remote_address))) {
			ast_http_request_close_on_completion(ser);
			ast_http_error(ser, 500, "Server Error", "Internal Server Error (out of memory)");
			return 0;
		}
		ao2_lock(session);

		ast_copy_string(session->username, u_username, sizeof(session->username));
		session->managerid = nonce;
		session->last_ev = grab_last();
		AST_LIST_HEAD_INIT_NOLOCK(&session->datastores);

		session->readperm = u_readperm;
		session->writeperm = u_writeperm;
		session->writetimeout = u_writetimeout;

		if (u_displayconnects) {
			ast_verb(2, "HTTP Manager '%s' logged in from %s\n", session->username, ast_sockaddr_stringify_addr(&session->addr));
		}
		session->noncetime = session->sessionstart = time_now;
		session->authenticated = 1;
	} else if (stale) {
		/*
		 * Session found, but nonce is stale.
		 *
		 * This could be because an old request (w/old nonce) arrived.
		 *
		 * This may be as the result of http proxy usage (separate delay or
		 * multipath) or in a situation where a page was refreshed too quickly
		 * (seen in Firefox).
		 *
		 * In this situation, we repeat the 401 auth with the current nonce
		 * value.
		 */
		nonce = session->managerid;
		ao2_unlock(session);
		stale = 1;
		goto out_401;
	} else {
		sscanf(d.nc, "%30lx", &nc);
		if (session->nc >= nc || ((time_now - session->noncetime) > 62) ) {
			/*
			 * Nonce time expired (> 2 minutes) or something wrong with nonce
			 * counter.
			 *
			 * Create new nonce key and resend Digest auth request. Old nonce
			 * is saved for stale checking...
			 */
			session->nc = 0; /* Reset nonce counter */
			session->oldnonce = session->managerid;
			nonce = session->managerid = ast_random();
			session->noncetime = time_now;
			ao2_unlock(session);
			stale = 1;
			goto out_401;
		} else {
			session->nc = nc; /* All OK, save nonce counter */
		}
	}


	/* Reset session timeout. */
	session->sessiontimeout = time(NULL) + (httptimeout > 5 ? httptimeout : 5);
	ao2_unlock(session);

	ast_mutex_init(&s.lock);
	s.session = session;
	fd = mkstemp(template);	/* create a temporary file for command output */
	unlink(template);
	if (fd <= -1) {
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (mkstemp failed)");
		goto auth_callback_out;
	}
	s.stream = ast_iostream_from_fd(&fd);
	if (!s.stream) {
		ast_log(LOG_WARNING, "HTTP Manager, fdopen failed: %s!\n", strerror(errno));
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (fdopen failed)");
		close(fd);
		goto auth_callback_out;
	}

	if (method == AST_HTTP_POST) {
		params = ast_http_get_post_vars(ser, headers);
		if (!params) {
			switch (errno) {
			case EFBIG:
				ast_http_error(ser, 413, "Request Entity Too Large", "Body too large");
				close_mansession_file(&s);
				goto auth_callback_out;
			case ENOMEM:
				ast_http_request_close_on_completion(ser);
				ast_http_error(ser, 500, "Server Error", "Out of memory");
				close_mansession_file(&s);
				goto auth_callback_out;
			case EIO:
				ast_http_error(ser, 400, "Bad Request", "Error parsing request body");
				close_mansession_file(&s);
				goto auth_callback_out;
			}
		}
	}

	astman_append_headers(&m, params);

	if (process_message(&s, &m)) {
		if (u_displayconnects) {
			ast_verb(2, "HTTP Manager '%s' logged off from %s\n", session->username, ast_sockaddr_stringify_addr(&session->addr));
		}

		session->needdestroy = 1;
	}

	astman_free_headers(&m);

	result_size = lseek(ast_iostream_get_fd(s.stream), 0, SEEK_CUR); /* Calculate approx. size of result */

	http_header = ast_str_create(80);
	out = ast_str_create(result_size * 2 + 512);
	if (http_header == NULL || out == NULL) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Internal Server Error (ast_str_create() out of memory)");
		close_mansession_file(&s);
		goto auth_callback_out;
	}

	ast_str_append(&http_header, 0, "Content-type: text/%s\r\n", contenttype[format]);

	if (format == FORMAT_XML) {
		ast_str_append(&out, 0, "<ajax-response>\n");
	} else if (format == FORMAT_HTML) {
		ast_str_append(&out, 0,
		"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<html><head>\r\n"
		"<title>Asterisk&trade; Manager Interface</title>\r\n"
		"</head><body style=\"background-color: #ffffff;\">\r\n"
		"<form method=\"POST\">\r\n"
		"<table align=\"center\" style=\"background-color: #f1f1f1;\" width=\"500\">\r\n"
		"<tr><th colspan=\"2\" style=\"background-color: #f1f1ff;\"><h1>Manager Tester</h1></th></tr>\r\n"
		"<tr><th colspan=\"2\" style=\"background-color: #f1f1ff;\">Action: <input name=\"action\" /> Cmd: <input name=\"command\" /><br>"
		"<input type=\"submit\" value=\"Send request\" /></th></tr>\r\n");
	}

	process_output(&s, &out, params, format);

	if (format == FORMAT_XML) {
		ast_str_append(&out, 0, "</ajax-response>\n");
	} else if (format == FORMAT_HTML) {
		ast_str_append(&out, 0, "</table></form></body></html>\r\n");
	}

	ast_http_send(ser, method, 200, NULL, http_header, out, 0, 0);
	http_header = NULL;
	out = NULL;

auth_callback_out:
	ast_mutex_destroy(&s.lock);

	/* Clear resources and unlock manager session */
	if (method == AST_HTTP_POST && params) {
		ast_variables_destroy(params);
	}

	ast_free(http_header);
	ast_free(out);

	ao2_lock(session);
	if (session->stream) {
		ast_iostream_close(session->stream);
		session->stream = NULL;
	}
	ao2_unlock(session);

	if (session->needdestroy) {
		ast_debug(1, "Need destroy, doing it now!\n");
		session_destroy(session);
	}
	ast_string_field_free_memory(&d);
	return 0;

out_401:
	if (!nonce) {
		nonce = ast_random();
	}

	ast_http_auth(ser, global_realm, nonce, nonce, stale, NULL);
	ast_string_field_free_memory(&d);
	return 0;
}

static int manager_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params,  struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = generic_http_callback(ser, method, FORMAT_HTML, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static int mxml_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = generic_http_callback(ser, method, FORMAT_XML, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static int rawman_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = generic_http_callback(ser, method, FORMAT_RAW, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static struct ast_http_uri rawmanuri = {
	.description = "Raw HTTP Manager Event Interface",
	.uri = "rawman",
	.callback = rawman_http_callback,
	.data = NULL,
	.key = __FILE__,
};

static struct ast_http_uri manageruri = {
	.description = "HTML Manager Event Interface",
	.uri = "manager",
	.callback = manager_http_callback,
	.data = NULL,
	.key = __FILE__,
};

static struct ast_http_uri managerxmluri = {
	.description = "XML Manager Event Interface",
	.uri = "mxml",
	.callback = mxml_http_callback,
	.data = NULL,
	.key = __FILE__,
};


/* Callback with Digest authentication */
static int auth_manager_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params,  struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = auth_http_callback(ser, method, FORMAT_HTML, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static int auth_mxml_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = auth_http_callback(ser, method, FORMAT_XML, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static int auth_rawman_http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers)
{
	int retval;
	struct ast_sockaddr ser_remote_address_tmp;

	ast_sockaddr_copy(&ser_remote_address_tmp, &ser->remote_address);
	retval = auth_http_callback(ser, method, FORMAT_RAW, &ser_remote_address_tmp, uri, get_params, headers);
	ast_sockaddr_copy(&ser->remote_address, &ser_remote_address_tmp);
	return retval;
}

static struct ast_http_uri arawmanuri = {
	.description = "Raw HTTP Manager Event Interface w/Digest authentication",
	.uri = "arawman",
	.has_subtree = 0,
	.callback = auth_rawman_http_callback,
	.data = NULL,
	.key = __FILE__,
};

static struct ast_http_uri amanageruri = {
	.description = "HTML Manager Event Interface w/Digest authentication",
	.uri = "amanager",
	.has_subtree = 0,
	.callback = auth_manager_http_callback,
	.data = NULL,
	.key = __FILE__,
};

static struct ast_http_uri amanagerxmluri = {
	.description = "XML Manager Event Interface w/Digest authentication",
	.uri = "amxml",
	.has_subtree = 0,
	.callback = auth_mxml_http_callback,
	.data = NULL,
	.key = __FILE__,
};

/*! \brief Get number of logged in sessions for a login name */
static int get_manager_sessions_cb(void *obj, void *arg, void *data, int flags)
{
	struct mansession_session *session = obj;
	const char *login = (char *)arg;
	int *no_sessions = data;

	if (strcasecmp(session->username, login) == 0) {
		(*no_sessions)++;
	}

	return 0;
}


/*! \brief  ${AMI_CLIENT()} Dialplan function - reads manager client data */
static int function_amiclient(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_manager_user *user = NULL;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(name);
		AST_APP_ARG(param);
	);


	if (ast_strlen_zero(data) ) {
		ast_log(LOG_WARNING, "AMI_CLIENT() requires two arguments: AMI_CLIENT(<name>[,<arg>])\n");
		return -1;
	}
	AST_STANDARD_APP_ARGS(args, data);
	args.name = ast_strip(args.name);
	args.param = ast_strip(args.param);

	AST_RWLIST_RDLOCK(&users);
	if (!(user = get_manager_by_name_locked(args.name))) {
		AST_RWLIST_UNLOCK(&users);
		ast_log(LOG_ERROR, "There's no manager user called : \"%s\"\n", args.name);
		return -1;
	}
	AST_RWLIST_UNLOCK(&users);

	if (!strcasecmp(args.param, "sessions")) {
		int no_sessions = 0;
		struct ao2_container *sessions;

		sessions = ao2_global_obj_ref(mgr_sessions);
		if (sessions) {
			ao2_callback_data(sessions, 0, get_manager_sessions_cb, /*login name*/ data, &no_sessions);
			ao2_ref(sessions, -1);
		}
		snprintf(buf, len, "%d", no_sessions);
	} else {
		ast_log(LOG_ERROR, "Invalid arguments provided to function AMI_CLIENT: %s\n", args.param);
		return -1;

	}

	return 0;
}


/*! \brief description of AMI_CLIENT dialplan function */
static struct ast_custom_function managerclient_function = {
	.name = "AMI_CLIENT",
	.read = function_amiclient,
	.read_max = 12,
};

static int webregged = 0;

/*! \brief cleanup code called at each iteration of server_root,
 * guaranteed to happen every 5 seconds at most
 */
static void purge_old_stuff(void *data)
{
	struct ast_tcptls_session_args *ser = data;
	/* purge_sessions will return the number of sessions actually purged,
	 * up to a maximum of it's arguments, purge one at a time, keeping a
	 * purge interval of 1ms as long as we purged a session, otherwise
	 * revert to a purge check every 5s
	 */
	if (purge_sessions(1) == 1) {
		ser->poll_timeout = 1;
	} else {
		ser->poll_timeout = 5000;
	}
	purge_events();
}

static struct ast_tls_config ami_tls_cfg;
static struct ast_tcptls_session_args ami_desc = {
	.accept_fd = -1,
	.master = AST_PTHREADT_NULL,
	.tls_cfg = NULL,
	.poll_timeout = 5000,	/* wake up every 5 seconds */
	.periodic_fn = purge_old_stuff,
	.name = "AMI server",
	.accept_fn = ast_tcptls_server_root,	/* thread doing the accept() */
	.worker_fn = session_do,	/* thread handling the session */
};

static struct ast_tcptls_session_args amis_desc = {
	.accept_fd = -1,
	.master = AST_PTHREADT_NULL,
	.tls_cfg = &ami_tls_cfg,
	.poll_timeout = -1,	/* the other does the periodic cleanup */
	.name = "AMI TLS server",
	.accept_fn = ast_tcptls_server_root,	/* thread doing the accept() */
	.worker_fn = session_do,	/* thread handling the session */
};

/*! \brief CLI command manager show settings */
static char *handle_manager_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show settings";
		e->usage =
			"Usage: manager show settings\n"
			"       Provides detailed list of the configuration of the Manager.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
#define FORMAT "  %-25.25s  %-15.55s\n"
#define FORMAT2 "  %-25.25s  %-15d\n"
#define FORMAT3 "  %-25.25s  %s\n"
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}
	ast_cli(a->fd, "\nGlobal Settings:\n");
	ast_cli(a->fd, "----------------\n");
	ast_cli(a->fd, FORMAT, "Manager (AMI):", AST_CLI_YESNO(manager_enabled));
	ast_cli(a->fd, FORMAT, "Web Manager (AMI/HTTP):", AST_CLI_YESNO(webmanager_enabled));
	ast_cli(a->fd, FORMAT, "TCP Bindaddress:", manager_enabled != 0 ? ast_sockaddr_stringify(&ami_desc.local_address) : "Disabled");
	ast_cli(a->fd, FORMAT2, "HTTP Timeout (seconds):", httptimeout);
	ast_cli(a->fd, FORMAT, "TLS Enable:", AST_CLI_YESNO(ami_tls_cfg.enabled));
	ast_cli(a->fd, FORMAT, "TLS Bindaddress:", ami_tls_cfg.enabled != 0 ? ast_sockaddr_stringify(&amis_desc.local_address) : "Disabled");
	ast_cli(a->fd, FORMAT, "TLS Certfile:", ami_tls_cfg.certfile);
	ast_cli(a->fd, FORMAT, "TLS Privatekey:", ami_tls_cfg.pvtfile);
	ast_cli(a->fd, FORMAT, "TLS Cipher:", ami_tls_cfg.cipher);
	ast_cli(a->fd, FORMAT, "Allow multiple login:", AST_CLI_YESNO(allowmultiplelogin));
	ast_cli(a->fd, FORMAT, "Display connects:", AST_CLI_YESNO(displayconnects));
	ast_cli(a->fd, FORMAT, "Timestamp events:", AST_CLI_YESNO(timestampevents));
	ast_cli(a->fd, FORMAT3, "Channel vars:", S_OR(manager_channelvars, ""));
	ast_cli(a->fd, FORMAT3, "Disabled events:", S_OR(manager_disabledevents, ""));
	ast_cli(a->fd, FORMAT, "Debug:", AST_CLI_YESNO(manager_debug));
#undef FORMAT
#undef FORMAT2
#undef FORMAT3

	return CLI_SUCCESS;
}

#ifdef AST_XML_DOCS

static int ast_xml_doc_item_cmp_fn(const void *a, const void *b)
{
	struct ast_xml_doc_item **item_a = (struct ast_xml_doc_item **)a;
	struct ast_xml_doc_item **item_b = (struct ast_xml_doc_item **)b;
	return strcmp((*item_a)->name, (*item_b)->name);
}

static char *handle_manager_show_events(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *events;
	struct ao2_iterator *it_events;
	struct ast_xml_doc_item *item;
	struct ast_xml_doc_item **items;
	struct ast_str *buffer;
	int i = 0, totalitems = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "manager show events";
		e->usage =
			"Usage: manager show events\n"
				"	Prints a listing of the available Asterisk manager interface events.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	buffer = ast_str_create(128);
	if (!buffer) {
		return CLI_SUCCESS;
	}

	events = ao2_global_obj_ref(event_docs);
	if (!events) {
		ast_cli(a->fd, "No manager event documentation loaded\n");
		ast_free(buffer);
		return CLI_SUCCESS;
	}

	ao2_lock(events);
	if (!(it_events = ao2_callback(events, OBJ_MULTIPLE | OBJ_NOLOCK, NULL, NULL))) {
		ao2_unlock(events);
		ast_log(AST_LOG_ERROR, "Unable to create iterator for events container\n");
		ast_free(buffer);
		ao2_ref(events, -1);
		return CLI_SUCCESS;
	}
	if (!(items = ast_calloc(sizeof(struct ast_xml_doc_item *), ao2_container_count(events)))) {
		ao2_unlock(events);
		ast_log(AST_LOG_ERROR, "Unable to create temporary sorting array for events\n");
		ao2_iterator_destroy(it_events);
		ast_free(buffer);
		ao2_ref(events, -1);
		return CLI_SUCCESS;
	}
	ao2_unlock(events);

	while ((item = ao2_iterator_next(it_events))) {
		items[totalitems++] = item;
		ao2_ref(item, -1);
	}

	qsort(items, totalitems, sizeof(struct ast_xml_doc_item *), ast_xml_doc_item_cmp_fn);

	ast_cli(a->fd, "Events:\n");
	ast_cli(a->fd, "  --------------------  --------------------  --------------------  \n");
	for (i = 0; i < totalitems; i++) {
		ast_str_append(&buffer, 0, "  %-20.20s", items[i]->name);
		if ((i + 1) % 3 == 0) {
			ast_cli(a->fd, "%s\n", ast_str_buffer(buffer));
			ast_str_set(&buffer, 0, "%s", "");
		}
	}
	if ((i + 1) % 3 != 0) {
		ast_cli(a->fd, "%s\n", ast_str_buffer(buffer));
	}

	ao2_iterator_destroy(it_events);
	ast_free(items);
	ao2_ref(events, -1);
	ast_free(buffer);

	return CLI_SUCCESS;
}

static void print_event_instance(struct ast_cli_args *a, struct ast_xml_doc_item *instance)
{
	char *since, *syntax, *description, *synopsis, *seealso, *arguments;

	synopsis = ast_xmldoc_printable(AS_OR(instance->synopsis, "Not available"), 1);
	since = ast_xmldoc_printable(AS_OR(instance->since, "Not available"), 1);
	description = ast_xmldoc_printable(AS_OR(instance->description, "Not available"), 1);
	syntax = ast_xmldoc_printable(AS_OR(instance->syntax, "Not available"), 1);
	arguments = ast_xmldoc_printable(AS_OR(instance->arguments, "Not available"), 1);
	seealso = ast_xmldoc_printable(AS_OR(instance->seealso, "Not available"), 1);

	if (!synopsis || !since || !description || !syntax || !arguments || !seealso) {
		ast_cli(a->fd, "Error: Memory allocation failed\n");
		goto free_docs;
	}

	ast_cli(a->fd, "\n"
		"%s  -= Info about Manager Event '%s' =- %s\n\n"
		COLORIZE_FMT "\n"
		"%s\n\n"
		COLORIZE_FMT "\n"
		"%s\n\n"
		COLORIZE_FMT "\n"
		"%s\n\n"
		COLORIZE_FMT "\n"
		"%s\n\n"
		COLORIZE_FMT "\n"
		"%s\n\n"
		COLORIZE_FMT "\n"
		"%s\n\n",
		ast_term_color(COLOR_MAGENTA, 0), instance->name, ast_term_reset(),
		COLORIZE(COLOR_MAGENTA, 0, "[Synopsis]"), synopsis,
		COLORIZE(COLOR_MAGENTA, 0, "[Since]"), since,
		COLORIZE(COLOR_MAGENTA, 0, "[Description]"), description,
		COLORIZE(COLOR_MAGENTA, 0, "[Syntax]"), syntax,
		COLORIZE(COLOR_MAGENTA, 0, "[Arguments]"), arguments,
		COLORIZE(COLOR_MAGENTA, 0, "[See Also]"), seealso
		);

free_docs:
	ast_free(synopsis);
	ast_free(since);
	ast_free(description);
	ast_free(syntax);
	ast_free(arguments);
	ast_free(seealso);
}

static char *handle_manager_show_event(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ao2_container *, events, NULL, ao2_cleanup);
	struct ao2_iterator it_events;
	struct ast_xml_doc_item *item, *temp;
	int length;

	if (cmd == CLI_INIT) {
		e->command = "manager show event";
		e->usage =
			"Usage: manager show event <eventname>\n"
			"       Provides a detailed description a Manager interface event.\n";
		return NULL;
	}

	events = ao2_global_obj_ref(event_docs);
	if (!events) {
		ast_cli(a->fd, "No manager event documentation loaded\n");
		return CLI_SUCCESS;
	}

	if (cmd == CLI_GENERATE) {
		if (a->pos != 3) {
			return NULL;
		}

		length = strlen(a->word);
		it_events = ao2_iterator_init(events, 0);
		while ((item = ao2_iterator_next(&it_events))) {
			if (!strncasecmp(a->word, item->name, length)) {
				if (ast_cli_completion_add(ast_strdup(item->name))) {
					ao2_ref(item, -1);
					break;
				}
			}
			ao2_ref(item, -1);
		}
		ao2_iterator_destroy(&it_events);

		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (!(item = ao2_find(events, a->argv[3], OBJ_KEY))) {
		ast_cli(a->fd, "Could not find event '%s'\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "Event: %s\n", a->argv[3]);
	for (temp = item; temp; temp = AST_LIST_NEXT(temp, next)) {
		print_event_instance(a, temp);
	}

	ao2_ref(item, -1);
	return CLI_SUCCESS;
}

#endif

static struct ast_cli_entry cli_manager[] = {
	AST_CLI_DEFINE(handle_showmancmd, "Show a manager interface command"),
	AST_CLI_DEFINE(handle_showmancmds, "List manager interface commands"),
	AST_CLI_DEFINE(handle_showmanconn, "List connected manager interface users"),
	AST_CLI_DEFINE(handle_kickmanconn, "Kick a connected manager interface connection"),
	AST_CLI_DEFINE(handle_showmaneventq, "List manager interface queued events"),
	AST_CLI_DEFINE(handle_showmanagers, "List configured manager users"),
	AST_CLI_DEFINE(handle_showmanager, "Display information on a specific manager user"),
	AST_CLI_DEFINE(handle_mandebug, "Show, enable, disable debugging of the manager code"),
	AST_CLI_DEFINE(handle_manager_reload, "Reload manager configurations"),
	AST_CLI_DEFINE(handle_manager_show_settings, "Show manager global settings"),
#ifdef AST_XML_DOCS
	AST_CLI_DEFINE(handle_manager_show_events, "List manager interface events"),
	AST_CLI_DEFINE(handle_manager_show_event, "Show a manager interface event"),
#endif
};

/*!
 * \internal
 * \brief Load the config channelvars variable.
 *
 * \param var Config variable to load.
 */
static void load_channelvars(struct ast_variable *var)
{
	char *parse = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vars)[MAX_VARS];
	);

	ast_free(manager_channelvars);
	manager_channelvars = ast_strdup(var->value);

	/* parse the setting */
	parse = ast_strdupa(manager_channelvars);
	AST_STANDARD_APP_ARGS(args, parse);

	ast_channel_set_manager_vars(args.argc, args.vars);
}

/*!
 * \internal
 * \brief Load the config disabledevents variable.
 *
 * \param var Config variable to load.
 */
static void load_disabledevents(struct ast_variable *var)
{
	ast_free(manager_disabledevents);
	manager_disabledevents = ast_strdup(var->value);
}

/*!
 * \internal
 * \brief Free a user record.  Should already be removed from the list
 */
static void manager_free_user(struct ast_manager_user *user)
{
	ast_free(user->a1_hash);
	ast_free(user->secret);
	if (user->includefilters) {
		ao2_t_ref(user->includefilters, -1, "decrement ref for include container, should be last one");
	}
	if (user->excludefilters) {
		ao2_t_ref(user->excludefilters, -1, "decrement ref for exclude container, should be last one");
	}
	user->acl = ast_free_acl_list(user->acl);
	ast_variables_destroy(user->chanvars);
	ast_free(user);
}

/*!
 * \internal
 * \brief Clean up resources on Asterisk shutdown
 */
static void manager_shutdown(void)
{
	struct ast_manager_user *user;

#ifdef TEST_FRAMEWORK
	AST_TEST_UNREGISTER(eventfilter_test_creation);
	AST_TEST_UNREGISTER(eventfilter_test_matching);
	AST_TEST_UNREGISTER(originate_permissions_test);
#endif

	/* This event is not actually transmitted, but causes all TCP sessions to be closed */
	manager_event(EVENT_FLAG_SHUTDOWN, "CloseSession", "CloseSession: true\r\n");

	ast_manager_unregister("Ping");
	ast_manager_unregister("Events");
	ast_manager_unregister("Logoff");
	ast_manager_unregister("Login");
	ast_manager_unregister("Challenge");
	ast_manager_unregister("Hangup");
	ast_manager_unregister("Status");
	ast_manager_unregister("Setvar");
	ast_manager_unregister("Getvar");
	ast_manager_unregister("GetConfig");
	ast_manager_unregister("GetConfigJSON");
	ast_manager_unregister("UpdateConfig");
	ast_manager_unregister("CreateConfig");
	ast_manager_unregister("ListCategories");
	ast_manager_unregister("Redirect");
	ast_manager_unregister("Atxfer");
	ast_manager_unregister("CancelAtxfer");
	ast_manager_unregister("Originate");
	ast_manager_unregister("Command");
	ast_manager_unregister("ExtensionState");
	ast_manager_unregister("PresenceState");
	ast_manager_unregister("AbsoluteTimeout");
	ast_manager_unregister("MailboxStatus");
	ast_manager_unregister("MailboxCount");
	ast_manager_unregister("ListCommands");
	ast_manager_unregister("SendText");
	ast_manager_unregister("UserEvent");
	ast_manager_unregister("WaitEvent");
	ast_manager_unregister("CoreSettings");
	ast_manager_unregister("CoreStatus");
	ast_manager_unregister("Reload");
	ast_manager_unregister("LoggerRotate");
	ast_manager_unregister("CoreShowChannels");
	ast_manager_unregister("CoreShowChannelMap");
	ast_manager_unregister("ModuleLoad");
	ast_manager_unregister("ModuleCheck");
	ast_manager_unregister("AOCMessage");
	ast_manager_unregister("Filter");
	ast_manager_unregister("BlindTransfer");
	ast_custom_function_unregister(&managerclient_function);
	ast_cli_unregister_multiple(cli_manager, ARRAY_LEN(cli_manager));

#ifdef AST_XML_DOCS
	ao2_t_global_obj_release(event_docs, "Dispose of event_docs");
#endif

#ifdef TEST_FRAMEWORK
	stasis_forward_cancel(test_suite_forwarder);
	test_suite_forwarder = NULL;
#endif

	if (stasis_router) {
		stasis_message_router_unsubscribe_and_join(stasis_router);
		stasis_router = NULL;
	}
	stasis_forward_cancel(rtp_topic_forwarder);
	rtp_topic_forwarder = NULL;
	stasis_forward_cancel(security_topic_forwarder);
	security_topic_forwarder = NULL;
	ao2_cleanup(manager_topic);
	manager_topic = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(ast_manager_get_generic_type);

	ast_tcptls_server_stop(&ami_desc);
	ast_tcptls_server_stop(&amis_desc);

	ast_free(ami_tls_cfg.certfile);
	ami_tls_cfg.certfile = NULL;
	ast_free(ami_tls_cfg.pvtfile);
	ami_tls_cfg.pvtfile = NULL;
	ast_free(ami_tls_cfg.cipher);
	ami_tls_cfg.cipher = NULL;
	ast_free(ami_tls_cfg.cafile);
	ami_tls_cfg.cafile = NULL;
	ast_free(ami_tls_cfg.capath);
	ami_tls_cfg.capath = NULL;

	ao2_global_obj_release(mgr_sessions);

	while ((user = AST_LIST_REMOVE_HEAD(&users, list))) {
		manager_free_user(user);
	}
	acl_change_stasis_unsubscribe();

	ast_free(manager_channelvars);
	ast_free(manager_disabledevents);
}


/*! \brief Initialize all \ref stasis topics and routers used by the various
 * sub-components of AMI
 */
static int manager_subscriptions_init(void)
{
	int res = 0;

	rtp_topic_forwarder = stasis_forward_all(ast_rtp_topic(), manager_topic);
	if (!rtp_topic_forwarder) {
		return -1;
	}

	security_topic_forwarder = stasis_forward_all(ast_security_topic(), manager_topic);
	if (!security_topic_forwarder) {
		return -1;
	}

	stasis_router = stasis_message_router_create(manager_topic);
	if (!stasis_router) {
		return -1;
	}
	stasis_message_router_set_congestion_limits(stasis_router, -1,
		6 * AST_TASKPROCESSOR_HIGH_WATER_LEVEL);

	stasis_message_router_set_formatters_default(stasis_router,
		manager_default_msg_cb, NULL, STASIS_SUBSCRIPTION_FORMATTER_AMI);

	res |= stasis_message_router_add(stasis_router,
		ast_manager_get_generic_type(), manager_generic_msg_cb, NULL);

	if (res != 0) {
		return -1;
	}
	return 0;
}

static int subscribe_all(void)
{
	if (manager_subscriptions_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager subscriptions\n");
		return -1;
	}
	if (manager_system_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager system handling\n");
		return -1;
	}
	if (manager_channels_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager channel handling\n");
		return -1;
	}
	if (manager_mwi_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager MWI handling\n");
		return -1;
	}
	if (manager_bridging_init()) {
		return -1;
	}
	if (manager_endpoints_init()) {
		ast_log(AST_LOG_ERROR, "Failed to initialize manager endpoints handling\n");
		return -1;
	}

	subscribed = 1;
	return 0;
}

static void manager_set_defaults(void)
{
	manager_enabled = 0;
	displayconnects = 1;
	broken_events_action = 0;
	authtimeout = 30;
	authlimit = 50;
	manager_debug = 0;		/* Debug disabled by default */

	/* default values */
	ast_copy_string(global_realm, S_OR(ast_config_AST_SYSTEM_NAME, DEFAULT_REALM),
		sizeof(global_realm));
	ast_sockaddr_setnull(&ami_desc.local_address);
	ast_sockaddr_setnull(&amis_desc.local_address);

	ami_tls_cfg.enabled = 0;
	ast_free(ami_tls_cfg.certfile);
	ami_tls_cfg.certfile = ast_strdup(AST_CERTFILE);
	ast_free(ami_tls_cfg.pvtfile);
	ami_tls_cfg.pvtfile = ast_strdup("");
	ast_free(ami_tls_cfg.cipher);
	ami_tls_cfg.cipher = ast_strdup("");
	ast_free(ami_tls_cfg.cafile);
	ami_tls_cfg.cafile = ast_strdup("");
	ast_free(ami_tls_cfg.capath);
	ami_tls_cfg.capath = ast_strdup("");
}

static int __init_manager(int reload, int by_external_config)
{
	struct ast_config *cfg = NULL;
	const char *val;
	char *cat = NULL;
	int newhttptimeout = 60;
	struct ast_manager_user *user = NULL;
	struct ast_variable *var;
	struct ast_flags config_flags = { (reload && !by_external_config) ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	char a1[337];
	char a1_hash[256];
	struct ast_sockaddr ami_desc_local_address_tmp;
	struct ast_sockaddr amis_desc_local_address_tmp;
	int tls_was_enabled = 0;
	int acl_subscription_flag = 0;

	if (!reload) {
		struct ao2_container *sessions;
#ifdef AST_XML_DOCS
		struct ao2_container *temp_event_docs;
#endif
		int res;

		res = STASIS_MESSAGE_TYPE_INIT(ast_manager_get_generic_type);
		if (res != 0) {
			return -1;
		}
		manager_topic = stasis_topic_create("manager:core");
		if (!manager_topic) {
			return -1;
		}

		/* Register default actions */
		ast_manager_register_xml_core("Ping", 0, action_ping);
		ast_manager_register_xml_core("Events", 0, action_events);
		ast_manager_register_xml_core("Logoff", 0, action_logoff);
		ast_manager_register_xml_core("Login", 0, action_login);
		ast_manager_register_xml_core("Challenge", 0, action_challenge);
		ast_manager_register_xml_core("Hangup", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL, action_hangup);
		ast_manager_register_xml_core("Status", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_status);
		ast_manager_register_xml_core("Setvar", EVENT_FLAG_CALL, action_setvar);
		ast_manager_register_xml_core("Getvar", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_getvar);
		ast_manager_register_xml_core("GetConfig", EVENT_FLAG_SYSTEM | EVENT_FLAG_CONFIG, action_getconfig);
		ast_manager_register_xml_core("GetConfigJSON", EVENT_FLAG_SYSTEM | EVENT_FLAG_CONFIG, action_getconfigjson);
		ast_manager_register_xml_core("UpdateConfig", EVENT_FLAG_CONFIG, action_updateconfig);
		ast_manager_register_xml_core("CreateConfig", EVENT_FLAG_CONFIG, action_createconfig);
		ast_manager_register_xml_core("ListCategories", EVENT_FLAG_CONFIG, action_listcategories);
		ast_manager_register_xml_core("Redirect", EVENT_FLAG_CALL, action_redirect);
		ast_manager_register_xml_core("Atxfer", EVENT_FLAG_CALL, action_atxfer);
		ast_manager_register_xml_core("CancelAtxfer", EVENT_FLAG_CALL, action_cancel_atxfer);
		ast_manager_register_xml_core("Originate", EVENT_FLAG_ORIGINATE, action_originate);
		ast_manager_register_xml_core("Command", EVENT_FLAG_COMMAND, action_command);
		ast_manager_register_xml_core("ExtensionState", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_extensionstate);
		ast_manager_register_xml_core("PresenceState", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_presencestate);
		ast_manager_register_xml_core("AbsoluteTimeout", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL, action_timeout);
		ast_manager_register_xml_core("MailboxStatus", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_mailboxstatus);
		ast_manager_register_xml_core("MailboxCount", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_mailboxcount);
		ast_manager_register_xml_core("ListCommands", 0, action_listcommands);
		ast_manager_register_xml_core("SendText", EVENT_FLAG_CALL, action_sendtext);
		ast_manager_register_xml_core("UserEvent", EVENT_FLAG_USER, action_userevent);
		ast_manager_register_xml_core("WaitEvent", 0, action_waitevent);
		ast_manager_register_xml_core("CoreSettings", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, action_coresettings);
		ast_manager_register_xml_core("CoreStatus", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, action_corestatus);
		ast_manager_register_xml_core("Reload", EVENT_FLAG_CONFIG | EVENT_FLAG_SYSTEM, action_reload);
		ast_manager_register_xml_core("LoggerRotate", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, action_loggerrotate);
		ast_manager_register_xml_core("CoreShowChannels", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, action_coreshowchannels);
		ast_manager_register_xml_core("CoreShowChannelMap", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, action_coreshowchannelmap);
		ast_manager_register_xml_core("ModuleLoad", EVENT_FLAG_SYSTEM, manager_moduleload);
		ast_manager_register_xml_core("ModuleCheck", EVENT_FLAG_SYSTEM, manager_modulecheck);
		ast_manager_register_xml_core("AOCMessage", EVENT_FLAG_AOC, action_aocmessage);
		ast_manager_register_xml_core("Filter", EVENT_FLAG_SYSTEM, action_filter);
		ast_manager_register_xml_core("BlindTransfer", EVENT_FLAG_CALL, action_blind_transfer);

#ifdef TEST_FRAMEWORK
		test_suite_forwarder = stasis_forward_all(ast_test_suite_topic(), manager_topic);
#endif

		ast_cli_register_multiple(cli_manager, ARRAY_LEN(cli_manager));
		__ast_custom_function_register(&managerclient_function, NULL);
		ast_extension_state_add(NULL, NULL, manager_state_cb, NULL);

		/* Append placeholder event so master_eventq never runs dry */
		if (append_event("Event: Placeholder\r\n\r\n",
			ast_str_hash("Placeholder"), 0)) {
			return -1;
		}

#ifdef AST_XML_DOCS
		temp_event_docs = ast_xmldoc_build_documentation("managerEvent");
		if (temp_event_docs) {
			ao2_t_global_obj_replace_unref(event_docs, temp_event_docs, "Toss old event docs");
			ao2_t_ref(temp_event_docs, -1, "Remove creation ref - container holds only ref now");
		}
#endif

		/* If you have a NULL hash fn, you only need a single bucket */
		sessions = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, mansession_cmp_fn);
		if (!sessions) {
			return -1;
		}
		ao2_global_obj_replace_unref(mgr_sessions, sessions);
		ao2_ref(sessions, -1);

		/* Initialize all settings before first configuration load. */
		manager_set_defaults();
	}

	cfg = ast_config_load2("manager.conf", "manager", config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_NOTICE, "Unable to open AMI configuration manager.conf, or configuration is invalid.\n");
		return 0;
	}

	/* If this wasn't performed due to a forced reload (because those can be created by ACL change events, we need to unsubscribe to ACL change events. */
	if (!by_external_config) {
		acl_change_stasis_unsubscribe();
	}

	if (reload) {
		/* Reset all settings before reloading configuration */
		tls_was_enabled = ami_tls_cfg.enabled;
		manager_set_defaults();
	}

	ast_sockaddr_parse(&ami_desc_local_address_tmp, "[::]", 0);
	ast_sockaddr_set_port(&ami_desc_local_address_tmp, DEFAULT_MANAGER_PORT);

	for (var = ast_variable_browse(cfg, "general"); var; var = var->next) {
		val = var->value;

		/* read tls config options while preventing unsupported options from being set */
		if (strcasecmp(var->name, "tlscafile")
			&& strcasecmp(var->name, "tlscapath")
			&& strcasecmp(var->name, "tlscadir")
			&& strcasecmp(var->name, "tlsverifyclient")
			&& strcasecmp(var->name, "tlsdontverifyserver")
			&& strcasecmp(var->name, "tlsclientmethod")
			&& strcasecmp(var->name, "sslclientmethod")
			&& !ast_tls_read_conf(&ami_tls_cfg, &amis_desc, var->name, val)) {
			continue;
		}

		if (!strcasecmp(var->name, "enabled")) {
			manager_enabled = ast_true(val);
		} else if (!strcasecmp(var->name, "webenabled")) {
			webmanager_enabled = ast_true(val);
		} else if (!strcasecmp(var->name, "port")) {
			int bindport;
			if (ast_parse_arg(val, PARSE_UINT32|PARSE_IN_RANGE, &bindport, 1024, 65535)) {
				ast_log(LOG_WARNING, "Invalid port number '%s'\n", val);
			}
			ast_sockaddr_set_port(&ami_desc_local_address_tmp, bindport);
		} else if (!strcasecmp(var->name, "bindaddr")) {
			/* remember port if it has already been set */
			int setport = ast_sockaddr_port(&ami_desc_local_address_tmp);

			if (ast_parse_arg(val, PARSE_ADDR|PARSE_PORT_IGNORE, NULL)) {
				ast_log(LOG_WARNING, "Invalid address '%s' specified, default '%s' will be used\n", val,
						ast_sockaddr_stringify_addr(&ami_desc_local_address_tmp));
			} else {
				ast_sockaddr_parse(&ami_desc_local_address_tmp, val, PARSE_PORT_IGNORE);
			}

			if (setport) {
				ast_sockaddr_set_port(&ami_desc_local_address_tmp, setport);
			}

		} else if (!strcasecmp(var->name, "brokeneventsaction")) {
			broken_events_action = ast_true(val);
		} else if (!strcasecmp(var->name, "allowmultiplelogin")) {
			allowmultiplelogin = ast_true(val);
		} else if (!strcasecmp(var->name, "displayconnects")) {
			displayconnects = ast_true(val);
		} else if (!strcasecmp(var->name, "timestampevents")) {
			timestampevents = ast_true(val);
		} else if (!strcasecmp(var->name, "debug")) {
			manager_debug = ast_true(val);
		} else if (!strcasecmp(var->name, "httptimeout")) {
			newhttptimeout = atoi(val);
		} else if (!strcasecmp(var->name, "authtimeout")) {
			int timeout = atoi(var->value);

			if (timeout < 1) {
				ast_log(LOG_WARNING, "Invalid authtimeout value '%s', using default value\n", var->value);
			} else {
				authtimeout = timeout;
			}
		} else if (!strcasecmp(var->name, "authlimit")) {
			int limit = atoi(var->value);

			if (limit < 1) {
				ast_log(LOG_WARNING, "Invalid authlimit value '%s', using default value\n", var->value);
			} else {
				authlimit = limit;
			}
		} else if (!strcasecmp(var->name, "channelvars")) {
			load_channelvars(var);
		} else if (!strcasecmp(var->name, "disabledevents")) {
			load_disabledevents(var);
		} else {
			ast_log(LOG_NOTICE, "Invalid keyword <%s> = <%s> in manager.conf [general]\n",
				var->name, val);
		}
	}

	if (manager_enabled && !subscribed) {
		if (subscribe_all() != 0) {
			ast_log(LOG_ERROR, "Manager subscription error\n");
			return -1;
		}
	}

	ast_sockaddr_copy(&amis_desc_local_address_tmp, &amis_desc.local_address);

	/* if the amis address has not been set, default is the same as non secure ami */
	if (ast_sockaddr_isnull(&amis_desc_local_address_tmp)) {
		ast_sockaddr_copy(&amis_desc_local_address_tmp, &ami_desc_local_address_tmp);
	}

	/* if the amis address was not set, it will have non-secure ami port set; if
	   amis address was set, we need to check that a port was set or not, if not
	   use the default tls port */
	if (ast_sockaddr_port(&amis_desc_local_address_tmp) == 0 ||
			(ast_sockaddr_port(&ami_desc_local_address_tmp) == ast_sockaddr_port(&amis_desc_local_address_tmp))) {

		ast_sockaddr_set_port(&amis_desc_local_address_tmp, DEFAULT_MANAGER_TLS_PORT);
	}

	if (manager_enabled) {
		ast_sockaddr_copy(&ami_desc.local_address, &ami_desc_local_address_tmp);
		ast_sockaddr_copy(&amis_desc.local_address, &amis_desc_local_address_tmp);
	}

	AST_RWLIST_WRLOCK(&users);

	while ((cat = ast_category_browse(cfg, cat))) {
		struct ast_acl_list *oldacl;

		if (!strcasecmp(cat, "general")) {
			continue;
		}

		/* Look for an existing entry, if none found - create one and add it to the list */
		if (!(user = get_manager_by_name_locked(cat))) {
			if (!(user = ast_calloc(1, sizeof(*user)))) {
				break;
			}
			/* Copy name over */
			ast_copy_string(user->username, cat, sizeof(user->username));

			user->acl = NULL;
			user->readperm = 0;
			user->writeperm = 0;
			/* Default displayconnect from [general] */
			user->displayconnects = displayconnects;
			/* Default allowmultiplelogin from [general] */
			user->allowmultiplelogin = allowmultiplelogin;
			user->writetimeout = 100;
			user->includefilters = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
			user->excludefilters = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, NULL);
			if (!user->includefilters || !user->excludefilters) {
				manager_free_user(user);
				break;
			}

			/* Insert into list */
			AST_RWLIST_INSERT_TAIL(&users, user, list);
		} else {
			ao2_t_callback(user->includefilters, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL, "unlink all include filters");
			ao2_t_callback(user->excludefilters, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL, "unlink all exclude filters");
		}

		/* Make sure we keep this user and don't destroy it during cleanup */
		user->keep = 1;
		oldacl = user->acl;
		user->acl = NULL;
		ast_variables_destroy(user->chanvars);

		var = ast_variable_browse(cfg, cat);
		for (; var; var = var->next) {
			if (!strcasecmp(var->name, "secret")) {
				ast_free(user->secret);
				user->secret = ast_strdup(var->value);
			} else if (!strcasecmp(var->name, "deny") ||
				       !strcasecmp(var->name, "permit") ||
				       !strcasecmp(var->name, "acl")) {
				int acl_error = 0;

				ast_append_acl(var->name, var->value, &user->acl, &acl_error, &acl_subscription_flag);
				if (acl_error) {
					ast_log(LOG_ERROR, "Invalid ACL '%s' for manager user '%s' on line %d. Deleting user\n",
						var->value, user->username, var->lineno);
					user->keep = 0;
				}
			}  else if (!strcasecmp(var->name, "read") ) {
				user->readperm = get_perm(var->value);
			}  else if (!strcasecmp(var->name, "write") ) {
				user->writeperm = get_perm(var->value);
			}  else if (!strcasecmp(var->name, "displayconnects") ) {
				user->displayconnects = ast_true(var->value);
			}  else if (!strcasecmp(var->name, "allowmultiplelogin") ) {
				user->allowmultiplelogin = ast_true(var->value);
			} else if (!strcasecmp(var->name, "writetimeout")) {
				int value = atoi(var->value);
				if (value < 100) {
					ast_log(LOG_WARNING, "Invalid writetimeout value '%s' at line %d\n", var->value, var->lineno);
				} else {
					user->writetimeout = value;
				}
			} else if (!strcasecmp(var->name, "setvar")) {
				struct ast_variable *tmpvar;
				char varbuf[256];
				char *varval;
				char *varname;

				ast_copy_string(varbuf, var->value, sizeof(varbuf));
				varname = varbuf;

				if ((varval = strchr(varname,'='))) {
					*varval++ = '\0';
					if ((tmpvar = ast_variable_new(varname, varval, ""))) {
						tmpvar->next = user->chanvars;
						user->chanvars = tmpvar;
					}
				}
			} else if (ast_begins_with(var->name, "eventfilter")) {
				const char *value = var->value;
				manager_add_filter(var->name, value, user->includefilters, user->excludefilters);
			} else {
				ast_debug(1, "%s is an unknown option.\n", var->name);
			}
		}

		oldacl = ast_free_acl_list(oldacl);
	}
	ast_config_destroy(cfg);

	/* Check the flag for named ACL event subscription and if we need to, register a subscription. */
	if (acl_subscription_flag && !by_external_config) {
		acl_change_stasis_subscribe();
	}

	/* Perform cleanup - essentially prune out old users that no longer exist */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&users, user, list) {
		if (user->keep) {	/* valid record. clear flag for the next round */
			user->keep = 0;

			/* Calculate A1 for Digest auth */
			snprintf(a1, sizeof(a1), "%s:%s:%s", user->username, global_realm, user->secret);
			ast_md5_hash(a1_hash,a1);
			ast_free(user->a1_hash);
			user->a1_hash = ast_strdup(a1_hash);
			continue;
		}
		/* We do not need to keep this user so take them out of the list */
		AST_RWLIST_REMOVE_CURRENT(list);
		ast_debug(4, "Pruning user '%s'\n", user->username);
		manager_free_user(user);
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	AST_RWLIST_UNLOCK(&users);

	if (webmanager_enabled && manager_enabled) {
		if (!webregged) {
			ast_http_uri_link(&rawmanuri);
			ast_http_uri_link(&manageruri);
			ast_http_uri_link(&managerxmluri);

			ast_http_uri_link(&arawmanuri);
			ast_http_uri_link(&amanageruri);
			ast_http_uri_link(&amanagerxmluri);
			webregged = 1;
		}
	} else {
		if (webregged) {
			ast_http_uri_unlink(&rawmanuri);
			ast_http_uri_unlink(&manageruri);
			ast_http_uri_unlink(&managerxmluri);

			ast_http_uri_unlink(&arawmanuri);
			ast_http_uri_unlink(&amanageruri);
			ast_http_uri_unlink(&amanagerxmluri);
			webregged = 0;
		}
	}

	if (newhttptimeout > 0) {
		httptimeout = newhttptimeout;
	}

	ast_tcptls_server_start(&ami_desc);
	if (tls_was_enabled && !ami_tls_cfg.enabled) {
		ast_tcptls_server_stop(&amis_desc);
	} else if (ast_ssl_setup(amis_desc.tls_cfg)) {
		ast_tcptls_server_start(&amis_desc);
	}

	return 0;
}

static void acl_change_stasis_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	if (stasis_message_type(message) != ast_named_acl_change_type()) {
		return;
	}

	/* For now, this is going to be performed simply and just execute a forced reload. */
	ast_log(LOG_NOTICE, "Reloading manager in response to ACL change event.\n");
	__init_manager(1, 1);
}

static int unload_module(void)
{
	return 0;
}

static int load_module(void)
{
	int rc = 0;
	ast_register_cleanup(manager_shutdown);
	rc = __init_manager(0, 0) ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS;
#ifdef TEST_FRAMEWORK
	AST_TEST_REGISTER(eventfilter_test_creation);
	AST_TEST_REGISTER(eventfilter_test_matching);
	AST_TEST_REGISTER(originate_permissions_test);
#endif
	return rc;
}

static int reload_module(void)
{
	return __init_manager(1, 0) ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS;
}

int astman_datastore_add(struct mansession *s, struct ast_datastore *datastore)
{
	AST_LIST_INSERT_HEAD(&s->session->datastores, datastore, entry);

	return 0;
}

int astman_datastore_remove(struct mansession *s, struct ast_datastore *datastore)
{
	return AST_LIST_REMOVE(&s->session->datastores, datastore, entry) ? 0 : -1;
}

struct ast_datastore *astman_datastore_find(struct mansession *s, const struct ast_datastore_info *info, const char *uid)
{
	struct ast_datastore *datastore = NULL;

	if (info == NULL)
		return NULL;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&s->session->datastores, datastore, entry) {
		if (datastore->info != info) {
			continue;
		}

		if (uid == NULL) {
			/* matched by type only */
			break;
		}

		if ((datastore->uid != NULL) && !strcasecmp(uid, datastore->uid)) {
			/* Matched by type AND uid */
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return datastore;
}

int ast_str_append_event_header(struct ast_str **fields_string,
	const char *header, const char *value)
{
	if (!*fields_string) {
		*fields_string = ast_str_create(128);
		if (!*fields_string) {
			return -1;
		}
	}

	return (ast_str_append(fields_string, 0, "%s: %s\r\n", header, value) < 0) ? -1 : 0;
}

static void manager_event_blob_dtor(void *obj)
{
	struct ast_manager_event_blob *ev = obj;

	ast_string_field_free_memory(ev);
}

struct ast_manager_event_blob *
__attribute__((format(printf, 3, 4)))
ast_manager_event_blob_create(
	int event_flags,
	const char *manager_event,
	const char *extra_fields_fmt,
	...)
{
	struct ast_manager_event_blob *ev;
	va_list argp;

	ast_assert(extra_fields_fmt != NULL);
	ast_assert(manager_event != NULL);

	ev = ao2_alloc_options(sizeof(*ev), manager_event_blob_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!ev) {
		return NULL;
	}

	if (ast_string_field_init(ev, 20)) {
		ao2_ref(ev, -1);
		return NULL;
	}

	ev->manager_event = manager_event;
	ev->event_flags = event_flags;

	va_start(argp, extra_fields_fmt);
	ast_string_field_ptr_build_va(ev, &ev->extra_fields, extra_fields_fmt, argp);
	va_end(argp);

	return ev;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Asterisk Manager Interface",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CORE,
	.requires = "extconfig,acl,http",
);
