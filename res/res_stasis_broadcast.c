/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Aurora Innovation AB
 *
 * Daniel Donoghue <daniel.donoghue@aurorainnovation.com>
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
 * \brief Stasis application broadcast resource
 *
 * \author Daniel Donoghue <daniel.donoghue@aurorainnovation.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<depend type="module">res_ari</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <errno.h>
#include <regex.h>

#include "asterisk/astobj2.h"
#include "asterisk/channel.h"
#include "asterisk/json.h"
#include "asterisk/lock.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/taskpool.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/threadstorage.h"
#include "asterisk/time.h"
#include "asterisk/utils.h"

#define AST_API_MODULE  /* Mark this as the module providing the API */
#include "asterisk/stasis_app_broadcast.h"
#include "asterisk/http.h"

#define BROADCAST_BUCKETS 37

/*! \brief Taskpool for parallel broadcast dispatch */
static struct ast_taskpool *broadcast_taskpool;

/*! \brief Maximum length for app_filter regex pattern */
#define MAX_REGEX_LENGTH 256

/*! \brief Maximum depth for regex group nesting */
#define MAX_GROUP_DEPTH 10

/*! \brief Maximum number of nested quantifiers in regex */
#define MAX_NESTED_QUANTIFIERS 3

/*! \brief Maximum value for brace quantifier bounds {m,n} */
#define MAX_QUANTIFIER_BOUND 100

/*! \brief Maximum alternations allowed in deeply nested groups */
#define MAX_ALTERNATIONS 20

/*! \brief Group depth threshold for alternation limits */
#define ALTERNATION_DEPTH_THRESHOLD 2

/*! \brief Maximum broadcast timeout in milliseconds (24 hours) */
#define MAX_BROADCAST_TIMEOUT_MS (24 * 60 * 60 * 1000)

/*! \brief Broadcast context stored on channel */
struct stasis_broadcast_ctx {
	/*! The unique ID of the channel */
	char channel_id[AST_MAX_EXTENSION];
	/*! Name of the winning application */
	char winner_app[AST_MAX_EXTENSION];
	/*! Flag indicating if channel was claimed */
	unsigned int claimed:1;
	/*! Lock for atomic claim operations */
	ast_mutex_t lock;
	/*! Condition variable for claim notification */
	ast_cond_t cond;
	/*! Timeout value in milliseconds */
	int timeout_ms;
	/*! Timestamp when broadcast started */
	struct timeval broadcast_time;
};

/*! \brief Container for all active broadcast contexts */
static struct ao2_container *broadcast_contexts;

/*! \brief Destructor for broadcast datastore */
static void broadcast_datastore_destroy(void *data)
{
	ao2_cleanup(data);
}

/*! \brief Datastore information for broadcast context */
static const struct ast_datastore_info broadcast_datastore_info = {
	.type = "stasis_broadcast_context",
	.destroy = broadcast_datastore_destroy,
};

/*! \brief Hash function for broadcast context container */
static int broadcast_ctx_hash_fn(const void *obj, const int flags)
{
	const struct stasis_broadcast_ctx *ctx;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		return ast_str_hash(key);
	case OBJ_SEARCH_OBJECT:
		ctx = obj;
		return ast_str_hash(ctx->channel_id);
	default:
		ast_assert(0);
		return 0;
	}
}

/*! \brief Compare function for broadcast context container */
static int broadcast_ctx_cmp_fn(void *obj, void *arg, int flags)
{
	const struct stasis_broadcast_ctx *ctx1 = obj;
	const struct stasis_broadcast_ctx *ctx2 = arg;
	const char *key = arg;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		return strcmp(ctx1->channel_id, ctx2->channel_id) ? 0 : CMP_MATCH;
	case OBJ_SEARCH_KEY:
		return strcmp(ctx1->channel_id, key) ? 0 : CMP_MATCH;
	case OBJ_SEARCH_PARTIAL_KEY:
		return strncmp(ctx1->channel_id, key, strlen(key)) ? 0 : CMP_MATCH;
	default:
		ast_assert(0);
		return 0;
	}
}

/*! \brief Destructor for broadcast context */
static void broadcast_ctx_destructor(void *obj)
{
	struct stasis_broadcast_ctx *ctx = obj;
	ast_cond_destroy(&ctx->cond);
	ast_mutex_destroy(&ctx->lock);
}

/*! \brief Create a new broadcast context */
static struct stasis_broadcast_ctx *broadcast_ctx_create(
	const char *channel_id, int timeout_ms)
{
	struct stasis_broadcast_ctx *ctx;

	ctx = ao2_alloc(sizeof(*ctx), broadcast_ctx_destructor);
	if (!ctx) {
		return NULL;
	}

	ast_copy_string(ctx->channel_id, channel_id, sizeof(ctx->channel_id));
	ast_mutex_init(&ctx->lock);
	ast_cond_init(&ctx->cond, NULL);
	ctx->timeout_ms = timeout_ms;
	ctx->claimed = 0;
	ctx->winner_app[0] = '\0';
	ctx->broadcast_time = ast_tvnow();

	ast_debug(1, "Created broadcast context for channel %s (timeout: %dms)\n",
		ctx->channel_id, ctx->timeout_ms);

	return ctx;
}

/*! \brief Collect channel variables into JSON object */
static struct ast_json *collect_channel_vars(struct ast_channel *chan)
{
	struct ast_json *vars;
	struct ast_var_t *var;
	struct varshead *varlist;

	vars = ast_json_object_create();
	if (!vars) {
		return NULL;
	}

	ast_channel_lock(chan);
	varlist = ast_channel_varshead(chan);
	if (varlist) {
		AST_LIST_TRAVERSE(varlist, var, entries) {
			const char *name = ast_var_name(var);
			const char *value = ast_var_value(var);

			/*
			 * Skip internal variables: those starting with '_' (inherited vars)
			 * and '__' (globally inherited vars), plus function result variables
			 */
			if (name && name[0] != '_' && value &&
				strcmp(name, "BROADCAST_WINNER") != 0) {
				struct ast_json *jstr = ast_json_string_create(value);
				if (jstr) {
					ast_json_object_set(vars, name, jstr);
				}
			}
		}
	}
	ast_channel_unlock(chan);

	return vars;
}

/*!
 * \brief Validate a regex pattern for safety
 *
 * Checks that the regex pattern is within length limits and doesn't contain
 * patterns that could cause excessive backtracking or denial of service.
 *
 * \param pattern The regex pattern to validate
 * \return 0 if valid, -1 if invalid
 */
static int validate_regex_pattern(const char *pattern)
{
	size_t len;
	int group_depth = 0;
	int quantified_groups = 0;
	int in_class = 0; /* Inside [...] */
	/* Track alternations per group depth. Index 0 is outside groups and unused. */
	int alternations_per_depth[MAX_GROUP_DEPTH + 1] = { 0 };
	const char *p;

	if (ast_strlen_zero(pattern)) {
		return 0; /* Empty pattern is valid (will be skipped) */
	}

	/* Check maximum length to prevent excessive regex compilation time */
	len = strlen(pattern);
	if (len > MAX_REGEX_LENGTH) {
		ast_log(LOG_WARNING, "Regex pattern exceeds maximum length of %d characters (got %zu)\n",
			MAX_REGEX_LENGTH, len);
		return -1;
	}

	/*
	 * Check for potentially dangerous patterns that could cause
	 * excessive regex compilation or matching time. Look for:
	 * - Excessive group nesting depth
	 * - Too many quantified groups (groups followed by +, *, or ?)
	 *
	 * Note: This is a heuristic approach that catches common dangerous
	 * patterns. Combined with the length limit, it provides reasonable
	 * protection against ReDoS while allowing legitimate regex usage.
	 */
	for (p = pattern; *p; p++) {
		/* Handle character classes: enter on unescaped '[' and exit on unescaped ']' */
		if (!in_class && *p == '[' && (p == pattern || *(p - 1) != '\\')) {
			in_class = 1;
			continue;
		} else if (in_class) {
			if (*p == '\\') {
				/* Skip the next escaped character inside character class */
				if (*(p + 1)) {
					p++;
				}
				continue;
			}
			if (*p == ']') {
				in_class = 0;
			}
			/* Ignore everything inside character classes for heuristics */
			continue;
		}
		switch (*p) {
		case '(':
			group_depth++;
			if (group_depth > MAX_GROUP_DEPTH) {
				ast_log(LOG_WARNING, "Regex pattern has too many nested groups (max %d)\n",
					MAX_GROUP_DEPTH);
				return -1;
			}
			/* Reset alternation counter for newly entered group depth */
			alternations_per_depth[group_depth] = 0;
			break;
		case ')':
			if (group_depth > 0) {
				/* Clear alternations count for this depth before leaving */
				alternations_per_depth[group_depth] = 0;
				group_depth--;
			}
			break;
		case '+':
		case '*':
		case '?':
			/*
			 * Count quantified groups - patterns like (...)+ or (...)*
			 * Too many of these can cause slow matching on certain inputs.
			 */
			if (p > pattern && *(p - 1) == ')') {
				quantified_groups++;
			}
			break;
		case '{': {
			/* Parse POSIX quantifier {m}, {m,}, {m,n} with overflow and bound checks */
			const char *q = p + 1;
			long m = 0, n = -1; /* n=-1 means open upper bound */
			int valid = 0;
			int overflow = 0;
			if (*q >= '0' && *q <= '9') {
				/* Parse m safely */
				while (*q >= '0' && *q <= '9') {
					int digit = (*q - '0');
					if (m > (LONG_MAX - digit) / 10) { /* overflow on next step */
						overflow = 1;
						break;
					}
					m = (m * 10) + digit;
					if (m > MAX_QUANTIFIER_BOUND) { /* early bound exceed */
						overflow = 1;
						break;
					}
					q++;
				}
				if (!overflow && *q == ',') {
					q++;
					if (*q >= '0' && *q <= '9') {
						long nn = 0;
						while (*q >= '0' && *q <= '9') {
							int digit = (*q - '0');
							if (nn > (LONG_MAX - digit) / 10) {
								overflow = 1;
								break;
							}
							nn = (nn * 10) + digit;
							if (nn > MAX_QUANTIFIER_BOUND) {
								overflow = 1;
								break;
							}
							q++;
						}
						n = nn;
					} else {
						n = -1; /* open upper bound */
					}
				} else if (!overflow) {
					n = m; /* {m} */
				}
				if (!overflow && *q == '}') {
					valid = 1;
				}
			}
			if (overflow) {
				ast_log(LOG_WARNING, "Regex quantifier overflow or exceeds max bound (max %d)\n", MAX_QUANTIFIER_BOUND);
				return -1;
			}
			if (valid) {
				/* Additional bounds check (defensive) */
				if (m > MAX_QUANTIFIER_BOUND || (n != -1 && n > MAX_QUANTIFIER_BOUND)) {
					ast_log(LOG_WARNING, "Regex quantifier bounds too large (max %d)\n", MAX_QUANTIFIER_BOUND);
					return -1;
				}
				if (p > pattern && *(p - 1) == ')') {
					quantified_groups++;
				}
				p = q; /* q currently points to '}' */
			}
			break;
		}
		case '|':
			if (group_depth > 0) {
				alternations_per_depth[group_depth]++;
				if (group_depth > ALTERNATION_DEPTH_THRESHOLD &&
					alternations_per_depth[group_depth] > MAX_ALTERNATIONS) {
					ast_log(LOG_WARNING,
						"Regex has too many alternations in deep group (depth %d, count %d, max %d)\n",
						group_depth,
						alternations_per_depth[group_depth],
						MAX_ALTERNATIONS);
					return -1;
				}
			}
			break;
		case '\\':
			/*
			 * Skip the next character entirely from heuristic processing.
			 * This ensures escaped characters (metacharacters in BRE or literals
			 * in ERE like \(, \), \+, \*, \?, etc.) do not affect group depth
			 * or quantified group counts.
			 */
			if (*(p + 1)) {
				p++;
			}
			/* Continue to next loop iteration without evaluating the escaped char */
			continue;
		}
	}

	/*
	 * Reject patterns with too many quantified groups, as these are
	 * often indicators of potentially slow patterns that could be
	 * exploited for denial of service.
	 */
	if (quantified_groups > MAX_NESTED_QUANTIFIERS) {
		ast_log(LOG_WARNING, "Regex pattern has too many quantified groups (max %d)\n",
			MAX_NESTED_QUANTIFIERS);
		return -1;
	}

	return 0;
}

/*! \brief Data for parallel broadcast task */
struct broadcast_task_data {
	/*! Name of the app to send to (owned by this struct) */
	char *app_name;
	/*! Event JSON to send (referenced) */
	struct ast_json *event;
};

/*!
 * \brief Task callback to send broadcast message to a single app
 * \param data broadcast_task_data structure
 * \return 0 always
 */
static int broadcast_send_task(void *data)
{
	struct broadcast_task_data *task_data = data;

	/* Send the message to the app */
	stasis_app_send(task_data->app_name, task_data->event);

	/* Cleanup */
	ast_free(task_data->app_name);
	ast_json_unref(task_data->event);
	ast_free(task_data);

	return 0;
}

/*! \brief Create and send broadcast event to all applications */
static int send_broadcast_event(struct ast_channel *chan, const char *app_filter)
{
	RAII_VAR(struct ast_json *, event, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, apps, NULL, ao2_cleanup);
	struct ast_json *vars = NULL;
	struct ao2_iterator iter;
	char *app_name;
	regex_t regex;
	int regex_compiled = 0;
	const char *caller = NULL;
	const char *called = NULL;

	snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(chan));
	if (!snapshot) {
		ast_log(LOG_ERROR, "Failed to get channel snapshot\n");
		return -1;
	}

	/* Get caller and called info - duplicate strings while locked */
	ast_channel_lock(chan);
	caller = ast_strdupa(S_OR(ast_channel_caller(chan)->id.number.str, ""));
	called = ast_strdupa(S_OR(ast_channel_exten(chan), ""));
	ast_channel_unlock(chan);

	/* Collect channel variables */
	vars = collect_channel_vars(chan);

	/* Build the broadcast event */
	event = ast_json_pack("{s: s, s: o, s: o, s: s?, s: s?, s: o?}",
		"type", "CallBroadcast",
		"timestamp", ast_json_timeval(ast_tvnow(), NULL),
		"channel", ast_channel_snapshot_to_json(snapshot, NULL),
		"caller", caller,
		"called", called,
		"variables", ast_json_ref(vars));

	if (!event) {
		ast_log(LOG_ERROR, "Failed to create broadcast event\n");
		ast_json_unref(vars);
		return -1;
	}

	/* Release our original vars reference; event holds its own ref */
	ast_json_unref(vars);

	/* Compile app filter regex if provided */
	if (!ast_strlen_zero(app_filter)) {
		/* Validate regex pattern for length and complexity */
		if (validate_regex_pattern(app_filter) != 0) {
			ast_log(LOG_WARNING, "Rejecting app_filter regex as potentially dangerous: %s\n", app_filter);
		} else if (regcomp(&regex, app_filter, REG_EXTENDED | REG_NOSUB)) {
			ast_log(LOG_WARNING, "Failed to compile app_filter regex: %s\n", app_filter);
		} else {
			regex_compiled = 1;
		}

		if (regex_compiled != 1) {
			ast_log(LOG_WARNING, "Proceeding without application filtering due to invalid regex.");
		}
	}

	/* Get all registered applications */
	apps = stasis_app_get_all();
	if (!apps) {
		ast_log(LOG_ERROR, "Failed to get stasis applications\n");
		if (regex_compiled) {
			regfree(&regex);
		}
		return -1;
	}

	ast_debug(2, "Broadcasting to %d registered Stasis applications\n", ao2_container_count(apps));

	/*
	 * Broadcast to all matching applications in parallel to ensure fair
	 * race conditions for claim operations. We collect all matching apps
	 * first, then send to all simultaneously to avoid giving the first
	 * app in the iterator an unfair time advantage.
	 */
	{
		struct ao2_container *matching_apps;
		struct ao2_iterator match_iter;
		char *match_name;

		/* Create temporary container for matching apps */
		matching_apps = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
		if (!matching_apps) {
			ast_log(LOG_ERROR, "Failed to allocate container for matching apps\n");
			if (regex_compiled) {
				regfree(&regex);
			}
			return -1;
		}

		/* First pass: collect all matching app names */
		iter = ao2_iterator_init(apps, 0);
		while ((app_name = ao2_iterator_next(&iter))) {
			/* Apply filter if specified */
			if (regex_compiled) {
				if (regexec(&regex, app_name, 0, NULL, 0) == REG_NOMATCH) {
					ast_debug(3, "App '%s' does not match filter, skipping\n", app_name);
					ao2_ref(app_name, -1);
					continue;
				}
			}

			/* Add to matching apps (will bump refcount) */
			ao2_link(matching_apps, app_name);
			ao2_ref(app_name, -1);
		}
		ao2_iterator_destroy(&iter);

		ast_debug(2, "Broadcasting channel %s to %d matching applications\n",
			ast_channel_uniqueid(chan), ao2_container_count(matching_apps));

		/*
		 * Second pass: dispatch to all matching apps in parallel via taskpool.
		 * This ensures fair race conditions by having each stasis_app_send call
		 * execute concurrently on different threads, giving all apps equal
		 * opportunity to claim the channel.
		 */
		match_iter = ao2_iterator_init(matching_apps, 0);
		while ((match_name = ao2_iterator_next(&match_iter))) {
			struct broadcast_task_data *task_data;

			ast_debug(3, "Queueing broadcast to app '%s'\n", match_name);

			/* Allocate task data */
			task_data = ast_malloc(sizeof(*task_data));
			if (!task_data) {
				ast_log(LOG_ERROR, "Failed to allocate broadcast task data for app '%s'\n", match_name);
				ao2_ref(match_name, -1);
				continue;
			}

			/* Setup task data - owns app_name string and event reference */
			task_data->app_name = ast_strdup(match_name);
			task_data->event = ast_json_ref(event);

			if (!task_data->app_name) {
				ast_log(LOG_ERROR, "Failed to duplicate app name for '%s'\n", match_name);
				ast_json_unref(task_data->event);
				ast_free(task_data);
				ao2_ref(match_name, -1);
				continue;
			}

			/* Push to taskpool for parallel execution */
			if (ast_taskpool_push(broadcast_taskpool, broadcast_send_task, task_data)) {
				ast_log(LOG_ERROR, "Failed to push broadcast task for app '%s'\n", match_name);
				ast_free(task_data->app_name);
				ast_json_unref(task_data->event);
				ast_free(task_data);
			}

			ao2_ref(match_name, -1);
		}
		ao2_iterator_destroy(&match_iter);
		ao2_ref(matching_apps, -1);
	}

	if (regex_compiled) {
		regfree(&regex);
	}

	return 0;
}

/*!
 * \brief Start a broadcast for a channel
 * \param chan The channel to broadcast
 * \param timeout_ms Timeout in milliseconds
 * \param app_filter Optional regex filter for applications
 * \return 0 on success, -1 on error
 */
int AST_OPTIONAL_API_NAME(stasis_app_broadcast_channel)(struct ast_channel *chan, int timeout_ms,
	const char *app_filter)
{
	RAII_VAR(struct stasis_broadcast_ctx *, ctx, NULL, ao2_cleanup);
	struct ast_datastore *datastore;
	const char *channel_id;

	if (!chan) {
		return -1;
	}

	channel_id = ast_channel_uniqueid(chan);

	/* Create broadcast context */
	ctx = broadcast_ctx_create(channel_id, timeout_ms);
	if (!ctx) {
		ast_log(LOG_ERROR, "Failed to create broadcast context for %s\n", channel_id);
		return -1;
	}

	/* Store context in container */
	ao2_link(broadcast_contexts, ctx);

	/* Create and attach datastore to channel */
	datastore = ast_datastore_alloc(&broadcast_datastore_info, channel_id);
	if (!datastore) {
		ast_log(LOG_ERROR, "Failed to allocate datastore for %s\n", channel_id);
		ao2_unlink(broadcast_contexts, ctx);
		return -1;
	}

	datastore->data = ao2_bump(ctx);
	ast_channel_lock(chan);
	if (ast_channel_datastore_add(chan, datastore)) {
		ast_channel_unlock(chan);
		ast_log(LOG_ERROR, "Failed to add datastore for %s\n", channel_id);
		ast_datastore_free(datastore);
		ao2_unlink(broadcast_contexts, ctx);
		return -1;
	}
	ast_channel_unlock(chan);

	ast_debug(1, "Starting broadcast for channel %s (timeout: %dms, filter: %s)\n",
		channel_id, timeout_ms, app_filter ? app_filter : "none");

	/* Send broadcast event to all matching applications */
	if (send_broadcast_event(chan, app_filter) != 0) {
		ast_log(LOG_ERROR, "Failed to send broadcast event for %s\n", channel_id);
		ast_channel_lock(chan);
		ast_channel_datastore_remove(chan, datastore);
		ast_channel_unlock(chan);
		ast_datastore_free(datastore);
		ao2_unlink(broadcast_contexts, ctx);
		return -1;
	}

	return 0;
}

/*!
 * \brief Attempt to claim a broadcast channel
 * \param channel_id The unique ID of the channel
 * \param app_name The name of the application claiming the channel
 * \return 0 if claim successful, -1 if channel not found, -2 if already claimed
 */
int AST_OPTIONAL_API_NAME(stasis_app_claim_channel)(const char *channel_id, const char *app_name)
{
	RAII_VAR(struct stasis_broadcast_ctx *, ctx, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, event, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, apps, NULL, ao2_cleanup);
	struct ao2_iterator iter;
	char *app_name_iter;
	int result = -1;

	if (ast_strlen_zero(channel_id) || ast_strlen_zero(app_name)) {
		return -1;
	}

	/* Find broadcast context */
	ctx = ao2_find(broadcast_contexts, channel_id, OBJ_SEARCH_KEY);
	if (!ctx) {
		ast_debug(1, "No broadcast context found for channel %s\n", channel_id);
		return -1;
	}

	/* Atomically check and set claimed flag */
	ast_mutex_lock(&ctx->lock);
	if (ctx->claimed) {
		ast_debug(1, "Channel %s already claimed by %s (attempt by %s denied)\n",
			channel_id, ctx->winner_app, app_name);
		result = -2;
	} else {
		ctx->claimed = 1;
		ast_copy_string(ctx->winner_app, app_name, sizeof(ctx->winner_app));
		result = 0;
		ast_verb(3, "Channel %s claimed by application %s\n", channel_id, app_name);
		/* Signal waiting thread that channel was claimed */
		ast_cond_signal(&ctx->cond);
	}
	ast_mutex_unlock(&ctx->lock);

	/* If claim successful, set channel variable */
	if (result == 0) {
		chan = ast_channel_get_by_name(channel_id);
		if (chan) {
			ast_channel_lock(chan);
			pbx_builtin_setvar_helper(chan, "BROADCAST_WINNER", app_name);
			ast_channel_unlock(chan);

			/* Send CallClaimed event to all apps */
			snapshot = ast_channel_snapshot_get_latest(channel_id);
			if (snapshot) {
				event = ast_json_pack("{s: s, s: o, s: o, s: s}",
					"type", "CallClaimed",
					"timestamp", ast_json_timeval(ast_tvnow(), NULL),
					"channel", ast_channel_snapshot_to_json(snapshot, NULL),
					"winner_app", app_name);

				if (event) {
					/* Send to all registered apps for informational purposes */
					apps = stasis_app_get_all();
					if (apps) {
						iter = ao2_iterator_init(apps, 0);
						/* stasis_app_get_all() returns a string container, not stasis_app objects */
						while ((app_name_iter = ao2_iterator_next(&iter))) {
							stasis_app_send(app_name_iter, ast_json_ref(event));
							ao2_ref(app_name_iter, -1);
						}
						ao2_iterator_destroy(&iter);
					}
				}
			}
		}
	}

	return result;
}

/*!
 * \brief Get the winner app name for a broadcast channel
 * \param channel_id The unique ID of the channel
 * \return A copy of the winner app name (caller must free with ast_free),
 *         or NULL if not claimed or not found
 */
char *AST_OPTIONAL_API_NAME(stasis_app_broadcast_winner)(const char *channel_id)
{
	RAII_VAR(struct stasis_broadcast_ctx *, ctx, NULL, ao2_cleanup);
	char *winner = NULL;

	if (ast_strlen_zero(channel_id)) {
		return NULL;
	}

	ctx = ao2_find(broadcast_contexts, channel_id, OBJ_SEARCH_KEY);
	if (!ctx) {
		return NULL;
	}

	ast_mutex_lock(&ctx->lock);
	if (ctx->claimed) {
		winner = ast_strdup(ctx->winner_app);
	}
	ast_mutex_unlock(&ctx->lock);

	return winner;
}

/*!
 * \brief Wait for a broadcast channel to be claimed
 * \param chan The channel
 * \param timeout_ms Maximum time to wait in milliseconds
 * \return 0 if claimed within timeout, -1 otherwise
 */
int AST_OPTIONAL_API_NAME(stasis_app_broadcast_wait)(struct ast_channel *chan, int timeout_ms)
{
	RAII_VAR(struct stasis_broadcast_ctx *, ctx, NULL, ao2_cleanup);
	const char *channel_id;
	struct timespec timeout_spec;
	struct timeval now;
	int result = -1;

	if (!chan) {
		return -1;
	}

	channel_id = ast_channel_uniqueid(chan);
	ctx = ao2_find(broadcast_contexts, channel_id, OBJ_SEARCH_KEY);
	if (!ctx) {
		ast_log(LOG_WARNING, "No broadcast context for channel %s\n", channel_id);
		return -1;
	}

	/* Calculate absolute timeout time */
	now = ast_tvnow();
	/* Cap excessive timeouts to prevent arithmetic overflow */
	if (timeout_ms < 0) {
		timeout_ms = 0;
	} else if (timeout_ms > MAX_BROADCAST_TIMEOUT_MS) {
		timeout_ms = MAX_BROADCAST_TIMEOUT_MS;
	}
	timeout_spec.tv_sec = now.tv_sec + (timeout_ms / 1000);
	{
		long ns_add = (long)(now.tv_usec) * 1000L;
		/* timeout_ms % 1000 yields 0-999, so ns_timeout is at most 999,000,000 */
		long ns_timeout = (long)(timeout_ms % 1000) * 1000000L;
		timeout_spec.tv_nsec = ns_add + ns_timeout;
	}
	/* Handle nanosecond overflow properly */
	while (timeout_spec.tv_nsec >= 1000000000) {
		timeout_spec.tv_sec++;
		timeout_spec.tv_nsec -= 1000000000;
	}

	ast_mutex_lock(&ctx->lock);
	/* Wait for claim with condition variable */
	while (!ctx->claimed) {
		int wait_result = ast_cond_timedwait(&ctx->cond, &ctx->lock, &timeout_spec);
		if (wait_result == ETIMEDOUT) {
			ast_log(LOG_NOTICE, "Broadcast timeout for channel %s after %dms\n",
				channel_id, timeout_ms);
			break;
		} else if (wait_result != 0) {
			/* Handle other errors (spurious wakeups will just re-check the loop condition) */
			ast_log(LOG_WARNING, "Unexpected error waiting for claim on channel %s: %d\n",
				channel_id, wait_result);
			break;
		}
		/* Spurious wakeup - loop will re-check ctx->claimed condition */
	}

	if (ctx->claimed) {
		ast_debug(1, "Channel %s claimed by %s\n", channel_id, ctx->winner_app);
		result = 0;
	}
	ast_mutex_unlock(&ctx->lock);

	return result;
}

/*!
 * \brief Clean up broadcast context for a channel
 * \param channel_id The unique ID of the channel
 */
void AST_OPTIONAL_API_NAME(stasis_app_broadcast_cleanup)(const char *channel_id)
{
	RAII_VAR(struct stasis_broadcast_ctx *, ctx, NULL, ao2_cleanup);

	if (ast_strlen_zero(channel_id)) {
		return;
	}

	ctx = ao2_find(broadcast_contexts, channel_id, OBJ_SEARCH_KEY);
	if (ctx) {
		ast_debug(3, "Cleaning up broadcast context for %s\n", channel_id);
		ao2_unlink(broadcast_contexts, ctx);
	}
}

/*!
 * \brief HTTP handler for /ari/events/claim endpoint
 */
/* HTTP endpoint /ari/events/claim is handled by res_ari through resource_events.c */

static int load_module(void)
{
	struct ast_taskpool_options taskpool_options = {
		.version = AST_TASKPOOL_OPTIONS_VERSION,
		.selector = AST_TASKPOOL_SELECTOR_DEFAULT,
		.idle_timeout = 0,              /* No timeout, keep threads alive */
		.auto_increment = 2,            /* Grow by 2 when needed */
		.minimum_size = 4,              /* Keep at least 4 threads */
		.initial_size = 4,              /* Start with 4 threads */
	};

	/* Create taskpool for parallel broadcast dispatch */
	broadcast_taskpool = ast_taskpool_create("stasis_broadcast", &taskpool_options);
	if (!broadcast_taskpool) {
		ast_log(LOG_ERROR, "Failed to create broadcast taskpool\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	broadcast_contexts = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		BROADCAST_BUCKETS, broadcast_ctx_hash_fn, NULL, broadcast_ctx_cmp_fn);

	if (!broadcast_contexts) {
		ast_taskpool_shutdown(broadcast_taskpool);
		broadcast_taskpool = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_log(LOG_NOTICE, "Stasis broadcast module loaded\n");
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ao2_cleanup(broadcast_contexts);
	broadcast_contexts = NULL;

	/* Shutdown taskpool */
	if (broadcast_taskpool) {
		ast_taskpool_shutdown(broadcast_taskpool);
		broadcast_taskpool = NULL;
	}

	ast_log(LOG_NOTICE, "Stasis broadcast module unloaded\n");
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER,
	"Stasis application broadcast",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_stasis,res_ari,http",
	.load_pri = AST_MODPRI_APP_DEPEND - 1,
);
