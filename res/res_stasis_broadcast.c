/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Aurora Innovation AB
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
#include "asterisk/http.h"
#include "asterisk/json.h"
#include "asterisk/lock.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis_app.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/time.h"
#include "asterisk/utils.h"

#define AST_API_MODULE  /* Mark this as the module providing the API */
#include "asterisk/stasis_app_broadcast.h"

#define BROADCAST_BUCKETS 37

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

/*! \brief Interval in ms between hangup checks while waiting for a claim */
#define BROADCAST_POLL_INTERVAL_MS 200

/*! \brief Broadcast context stored on channel */
struct stasis_broadcast_ctx {
	/*! The unique ID of the channel */
	char channel_id[AST_MAX_PUBLIC_UNIQUEID];
	/*! Name of the winning application (dynamically allocated, NULL until claimed) */
	char *winner_app;
	/*! Regex pattern used to filter broadcast recipients */
	char app_filter[MAX_REGEX_LENGTH + 1];
	/*! Compiled regex for app_filter (valid only when filter_compiled is set) */
	regex_t compiled_filter;
	/*! Flag indicating if channel was claimed */
	unsigned int claimed:1;
	/*! Whether compiled_filter is valid and must be freed */
	unsigned int filter_compiled:1;
	/*! Set when the PBX thread retrieves the winner; prevents late claims */
	unsigned int finished:1;
	/*! Broadcast behaviour flags (STASIS_BROADCAST_FLAG_*) */
	unsigned int flags;
	/*! Reference to the global container (prevents use-after-free during module unload) */
	struct ao2_container *container;
	/*! Condition variable for claim notification */
	ast_cond_t cond;
};

/*! \brief Container for all active broadcast contexts */
static struct ao2_container *broadcast_contexts;

/*! \brief Destructor for broadcast datastore
 *
 * Called when the channel is destroyed. Ensures the broadcast context
 * is unlinked from the global container even if the caller never
 * reached stasis_app_broadcast_cleanup (e.g. abnormal channel teardown).
 */
static void broadcast_datastore_destroy(void *data)
{
	struct stasis_broadcast_ctx *ctx = data;

	if (ctx->container) {
		ao2_unlink(ctx->container, ctx);
	}
	ao2_cleanup(ctx);
}

/*! \brief Datastore information for broadcast context */
static const struct ast_datastore_info broadcast_datastore_info = {
	.type = "stasis_broadcast_context",
	.destroy = broadcast_datastore_destroy,
};

AO2_STRING_FIELD_HASH_FN(stasis_broadcast_ctx, channel_id)
AO2_STRING_FIELD_CMP_FN(stasis_broadcast_ctx, channel_id)

/*! \brief Destructor for broadcast context */
static void broadcast_ctx_destructor(void *obj)
{
	struct stasis_broadcast_ctx *ctx = obj;
	ast_free(ctx->winner_app);
	if (ctx->filter_compiled) {
		regfree(&ctx->compiled_filter);
	}
	ao2_cleanup(ctx->container);
	ast_cond_destroy(&ctx->cond);
}

static int validate_regex_pattern(const char *pattern);

/*! \brief Create a new broadcast context
 *
 * Validates and compiles the app_filter regex if provided. On regex
 * failure the context is still created but broadcasts will be sent
 * to all applications (i.e. no filtering).
 */
static struct stasis_broadcast_ctx *broadcast_ctx_create(
	const char *channel_id, const char *app_filter, unsigned int flags)
{
	struct stasis_broadcast_ctx *ctx;

	ctx = ao2_alloc(sizeof(*ctx), broadcast_ctx_destructor);
	if (!ctx) {
		return NULL;
	}

	/* ao2_alloc zeroes the struct; only set non-zero fields explicitly */
	ast_copy_string(ctx->channel_id, channel_id, sizeof(ctx->channel_id));
	ctx->flags = flags;
	ctx->container = ao2_bump(broadcast_contexts);
	ast_cond_init(&ctx->cond, NULL);

	/* Validate and compile app_filter regex if provided */
	if (!ast_strlen_zero(app_filter)) {
		ast_copy_string(ctx->app_filter, app_filter, sizeof(ctx->app_filter));
		if (validate_regex_pattern(app_filter) != 0) {
			ast_log(LOG_WARNING,
				"Channel %s: rejecting app_filter regex as potentially dangerous: %s\n",
				channel_id, app_filter);
		} else if (regcomp(&ctx->compiled_filter, app_filter,
				REG_EXTENDED | REG_NOSUB) != 0) {
			ast_log(LOG_WARNING,
				"Channel %s: failed to compile app_filter regex '%s'\n",
				channel_id, app_filter);
		} else {
			ctx->filter_compiled = 1;
		}

		if (!ctx->filter_compiled) {
			ast_log(LOG_WARNING,
				"Channel %s: proceeding without application filtering due to invalid regex\n",
				channel_id);
		}
	}

	ast_debug(1, "Created broadcast context for channel %s (filter: %s, flags: 0x%x)\n",
		ctx->channel_id,
		ctx->filter_compiled ? ctx->app_filter : "none",
		ctx->flags);

	return ctx;
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
		ast_debug(3, "Regex pattern exceeds maximum length of %d characters (got %zu)\n",
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
			/* In POSIX ERE, ']' immediately after '[' or '[^' is a
			 * literal, not the end of the class.  Advance past the
			 * optional negation caret and the literal ']' so the
			 * main loop does not leave in_class prematurely. */
			if (*(p + 1) == '^') {
				p++;
			}
			if (*(p + 1) == ']') {
				p++;
			}
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
				ast_debug(3, "Regex pattern has too many nested groups (max %d)\n",
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
			int digit;
			int overflow = 0;

			if (*q >= '0' && *q <= '9') {
				/* Parse m safely */
				while (*q >= '0' && *q <= '9') {
					digit = (*q - '0');
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
							digit = (*q - '0');
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
				ast_debug(3, "Regex quantifier overflow or exceeds max bound (max %d)\n", MAX_QUANTIFIER_BOUND);
				return -1;
			}
			if (valid) {
				/* Additional bounds check (defensive) */
				if (m > MAX_QUANTIFIER_BOUND || (n != -1 && n > MAX_QUANTIFIER_BOUND)) {
					ast_debug(3, "Regex quantifier bounds too large (max %d)\n", MAX_QUANTIFIER_BOUND);
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
					ast_debug(3,
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
		ast_debug(3, "Regex pattern has too many quantified groups (max %d)\n",
			MAX_NESTED_QUANTIFIERS);
		return -1;
	}

	return 0;
}

/*! \brief Create and send broadcast event to all applications
 *
 * Uses the compiled regex cached in \a ctx for application filtering.
 */
static int send_broadcast_event(struct ast_channel *chan,
	struct stasis_broadcast_ctx *ctx)
{
	RAII_VAR(struct ast_json *, event, NULL, ast_json_unref);
	RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, apps, NULL, ao2_cleanup);
	struct ao2_iterator iter;
	char *app_name;
	const char *caller = NULL;
	const char *called = NULL;

	/* Get snapshot and caller/called info under a single channel lock */
	ast_channel_lock(chan);
	snapshot = ao2_bump(ast_channel_snapshot(chan));
	caller = ast_strdupa(S_OR(ast_channel_caller(chan)->id.number.str, ""));
	called = ast_strdupa(S_OR(ast_channel_exten(chan), ""));
	ast_channel_unlock(chan);

	/* Build the broadcast event.  Channel variables configured in
	 * ari.conf "channelvars" are already included in the channel
	 * snapshot produced by ast_channel_snapshot_to_json(). */
	event = ast_json_pack("{s: s, s: o, s: o, s: s?, s: s?}",
		"type", "CallBroadcast",
		"timestamp", ast_json_timeval(ast_tvnow(), NULL),
		"channel", ast_channel_snapshot_to_json(snapshot, NULL),
		"caller", caller,
		"called", called);

	if (!event) {
		ast_log(LOG_ERROR, "Channel %s: failed to create broadcast event\n",
			ast_channel_name(chan));
		return -1;
	}

	/* Get all registered applications */
	apps = stasis_app_get_all();
	if (!apps) {
		ast_log(LOG_ERROR, "Channel %s: failed to get stasis applications\n",
			ast_channel_name(chan));
		return -1;
	}

	ast_debug(2, "Broadcasting to %d registered Stasis applications\n",
		ao2_container_count(apps));

	/*
	 * Broadcast to all matching applications.
	 *
	 * We collect matching apps into a plain array, Fisher-Yates shuffle it,
	 * then call stasis_app_send() for each.  stasis_app_send() writes
	 * directly to each app's WebSocket socket synchronously on the calling
	 * thread.  The shuffle ensures no single ARI application is consistently
	 * first to receive the event — every app gets a fair chance to claim the
	 * channel regardless of its position in the ao2 hash container.
	 */
	{
		int app_count;
		char **matching_arr;
		int n = 0;
		int i;

		app_count = ao2_container_count(apps);
		if (app_count == 0) {
			ast_debug(2, "Channel %s: no Stasis applications registered\n",
				ast_channel_uniqueid(chan));
			return 0;
		}

		matching_arr = ast_malloc(app_count * sizeof(*matching_arr));
		if (!matching_arr) {
			ast_log(LOG_ERROR, "Channel %s: failed to allocate matching apps array\n",
				ast_channel_name(chan));
			return -1;
		}

		/* First pass: collect all matching app names (transfer refs to array) */
		iter = ao2_iterator_init(apps, 0);
		while ((app_name = ao2_iterator_next(&iter)) && n < app_count) {
			if (ctx->filter_compiled &&
				regexec(&ctx->compiled_filter, app_name, 0, NULL, 0) == REG_NOMATCH) {
				ast_debug(3, "App '%s' does not match filter, skipping\n", app_name);
				ao2_ref(app_name, -1);
				continue;
			}
			matching_arr[n++] = app_name; /* ref transferred to array */
		}
		ao2_iterator_destroy(&iter);

		ast_debug(2, "Broadcasting channel %s to %d matching applications\n",
			ast_channel_uniqueid(chan), n);

		/* Fisher-Yates shuffle: randomise delivery order so no app is
		 * consistently first to receive the broadcast event. */
		for (i = n - 1; i > 0; i--) {
			int j = ast_random() % (i + 1);
			char *tmp = matching_arr[i];
			matching_arr[i] = matching_arr[j];
			matching_arr[j] = tmp;
		}

		/*
		 * Second pass: send to each matching app.  A deep copy of the event
		 * is required for each call because stasis_app_send() mutates the
		 * message in-place (adds "asterisk_id" via ast_json_object_set).
		 */
		for (i = 0; i < n; i++) {
			char *match_name = matching_arr[i];
			struct ast_json *event_copy;

			ast_debug(3, "Sending broadcast to app '%s'\n", match_name);

			event_copy = ast_json_deep_copy(event);
			if (!event_copy) {
				ast_log(LOG_ERROR,
					"Channel %s: failed to deep-copy event for app '%s'\n",
					ast_channel_uniqueid(chan), match_name);
				ao2_ref(match_name, -1);
				continue;
			}

			stasis_app_send(match_name, event_copy);
			ast_json_unref(event_copy);
			ao2_ref(match_name, -1);
		}

		ast_free(matching_arr);
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
	const char *app_filter, unsigned int flags)
{
	RAII_VAR(struct stasis_broadcast_ctx *, ctx, NULL, ao2_cleanup);
	struct ast_datastore *datastore;

	if (!chan) {
		return -1;
	}

	if (!broadcast_contexts) {
		return -1;
	}

	/* Remove any previous broadcast datastore from a prior attempt.
	 * This supports failover scenarios where StasisBroadcast() is
	 * called multiple times for the same channel.  The datastore
	 * destructor unlinks the old context from the container. */
	{
		struct ast_datastore *old_ds;
		ast_channel_lock(chan);
		old_ds = ast_channel_datastore_find(chan, &broadcast_datastore_info, NULL);
		if (old_ds) {
			ast_channel_datastore_remove(chan, old_ds);
		}
		ast_channel_unlock(chan);
		if (old_ds) {
			ast_datastore_free(old_ds);
		}
	}

	/* Create broadcast context (validates and compiles app_filter regex) */
	ctx = broadcast_ctx_create(ast_channel_uniqueid(chan), app_filter, flags);
	if (!ctx) {
		ast_log(LOG_ERROR, "Channel %s: failed to create broadcast context\n",
			ast_channel_uniqueid(chan));
		return -1;
	}

	/* Store context in container */
	ao2_link(broadcast_contexts, ctx);

	/* Create and attach datastore to channel */
	datastore = ast_datastore_alloc(&broadcast_datastore_info, ast_channel_uniqueid(chan));
	if (!datastore) {
		ast_log(LOG_ERROR, "Channel %s: failed to allocate broadcast datastore\n",
			ast_channel_uniqueid(chan));
		ao2_unlink(broadcast_contexts, ctx);
		return -1;
	}

	datastore->data = ao2_bump(ctx);
	ast_channel_lock(chan);
	if (ast_channel_datastore_add(chan, datastore)) {
		ast_channel_unlock(chan);
		ast_log(LOG_ERROR, "Channel %s: failed to attach broadcast datastore\n",
			ast_channel_uniqueid(chan));
		ast_datastore_free(datastore);
		ao2_unlink(broadcast_contexts, ctx);
		return -1;
	}
	ast_channel_unlock(chan);

	ast_debug(1, "Starting broadcast for channel %s (timeout: %dms, filter: %s)\n",
		ast_channel_uniqueid(chan), timeout_ms, app_filter ? app_filter : "none");

	/* Send broadcast event to all matching applications */
	if (send_broadcast_event(chan, ctx) != 0) {
		ast_log(LOG_ERROR, "Channel %s: failed to send broadcast event\n",
			ast_channel_uniqueid(chan));
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

	if (ast_strlen_zero(channel_id) || ast_strlen_zero(app_name)) {
		return -1;
	}

	if (!broadcast_contexts) {
		return -1;
	}

	/* Find broadcast context */
	ctx = ao2_find(broadcast_contexts, channel_id, OBJ_SEARCH_KEY);
	if (!ctx) {
		ast_debug(1, "No broadcast context found for channel %s\n", channel_id);
		return -1;
	}

	/* Atomically check and set claimed flag.
	 * Check claimed before finished: if the channel was claimed and then the
	 * broadcast finished, a late claim should return -2 (409 Conflict) rather
	 * than -1 (404) so callers can distinguish "already taken" from "not found". */
	ao2_lock(ctx);
	if (ctx->claimed) {
		ast_debug(1, "Channel %s already claimed by %s (attempt by %s denied)\n",
			channel_id, ctx->winner_app ? ctx->winner_app : "(unknown)", app_name);
		ao2_unlock(ctx);
		return -2;
	}
	if (ctx->finished) {
		ast_debug(1, "Channel %s broadcast already finished (late claim by %s rejected)\n",
			channel_id, app_name);
		ao2_unlock(ctx);
		return -1;
	}
	ctx->winner_app = ast_strdup(app_name);
	if (!ctx->winner_app) {
		ast_log(LOG_ERROR,
			"Failed to allocate winner app name for channel %s\n",
			channel_id);
		ao2_unlock(ctx);
		return -1;
	}
	ctx->claimed = 1;
	ast_verb(3, "Channel %s claimed by application %s\n",
		channel_id, app_name);
	/* Signal waiting thread that channel was claimed */
	ast_cond_signal(&ctx->cond);
	ao2_unlock(ctx);

	/* Send CallClaimed event to matching apps */
	if (!(ctx->flags & STASIS_BROADCAST_FLAG_SUPPRESS_CLAIMED)) {
		RAII_VAR(struct ast_channel_snapshot *, snapshot, NULL, ao2_cleanup);
		RAII_VAR(struct ast_json *, event, NULL, ast_json_unref);
		RAII_VAR(struct ao2_container *, apps, NULL, ao2_cleanup);
		struct ao2_iterator iter;
		char *app_name_iter;

		snapshot = ast_channel_snapshot_get_latest(channel_id);
		if (snapshot) {
			event = ast_json_pack("{s: s, s: o, s: o, s: s}",
				"type", "CallClaimed",
				"timestamp", ast_json_timeval(ast_tvnow(), NULL),
				"channel", ast_channel_snapshot_to_json(snapshot, NULL),
				"winner_app", app_name);
		}
		if (event) {
			apps = stasis_app_get_all();
		}
		if (apps) {
			iter = ao2_iterator_init(apps, 0);
			while ((app_name_iter = ao2_iterator_next(&iter))) {
				struct ast_json *event_copy;

				/* Only send to apps that matched the original broadcast filter */
				if (ctx->filter_compiled &&
					regexec(&ctx->compiled_filter, app_name_iter,
						0, NULL, 0) == REG_NOMATCH) {
					ao2_ref(app_name_iter, -1);
					continue;
				}

				event_copy = ast_json_deep_copy(event);
				if (!event_copy) {
					ao2_ref(app_name_iter, -1);
					continue;
				}

				stasis_app_send(app_name_iter, event_copy);
				ast_json_unref(event_copy);
				ao2_ref(app_name_iter, -1);
			}
			ao2_iterator_destroy(&iter);
		}
	}

	return 0;
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

	ao2_lock(ctx);
	if (ctx->claimed) {
		winner = ast_strdup(ctx->winner_app);
	}
	/* Mark the broadcast as finished so no new claims can succeed.
	 * This closes the race window between reading the winner and
	 * the subsequent broadcast_cleanup call. */
	ctx->finished = 1;
	ao2_unlock(ctx);

	return winner;
}

/*!
 * \brief Wait for a broadcast channel to be claimed
 *
 * Blocks until the channel is claimed, the timeout expires, or the
 * channel hangs up.  The hangup check runs every
 * #BROADCAST_POLL_INTERVAL_MS so that a dead channel does not tie up
 * a PBX thread for the full timeout period.
 *
 * \param chan The channel
 * \param timeout_ms Maximum time to wait in milliseconds
 * \return 0 if claimed within timeout, -1 otherwise
 */
int AST_OPTIONAL_API_NAME(stasis_app_broadcast_wait)(struct ast_channel *chan, int timeout_ms)
{
	RAII_VAR(struct stasis_broadcast_ctx *, ctx, NULL, ao2_cleanup);
	const char *channel_id;
	struct timeval deadline;
	int result = -1;

	if (!chan) {
		return -1;
	}

	channel_id = ast_channel_uniqueid(chan);

	if (!broadcast_contexts) {
		return -1;
	}

	ctx = ao2_find(broadcast_contexts, channel_id, OBJ_SEARCH_KEY);
	if (!ctx) {
		ast_log(LOG_WARNING, "No broadcast context for channel %s\n", channel_id);
		return -1;
	}

	/* Cap excessive timeouts to prevent arithmetic overflow */
	if (timeout_ms < 0) {
		timeout_ms = 0;
	} else if (timeout_ms > MAX_BROADCAST_TIMEOUT_MS) {
		timeout_ms = MAX_BROADCAST_TIMEOUT_MS;
	}

	/* Calculate absolute deadline */
	deadline = ast_tvadd(ast_tvnow(),
		ast_tv(timeout_ms / 1000, (timeout_ms % 1000) * 1000));

	ao2_lock(ctx);
	while (!ctx->claimed) {
		struct timeval now;
		struct timespec poll_spec;
		long remaining_ms;
		long poll_ms;
		int wait_result;

		/* Check for hangup so we don't block on a dead channel */
		if (ast_check_hangup(chan)) {
			ast_debug(3, "Channel %s hung up during broadcast wait\n",
				channel_id);
			break;
		}

		/* Check if we've passed the overall deadline */
		now = ast_tvnow();
		remaining_ms = ast_tvdiff_ms(deadline, now);
		if (remaining_ms <= 0) {
			ast_debug(3, "Broadcast timeout for channel %s after %dms\n",
				channel_id, timeout_ms);
			break;
		}

		/* Sleep for the shorter of the remaining time and the poll interval */
		poll_ms = remaining_ms;
		if (poll_ms > BROADCAST_POLL_INTERVAL_MS) {
			poll_ms = BROADCAST_POLL_INTERVAL_MS;
		}

		poll_spec.tv_sec = now.tv_sec + (poll_ms / 1000);
		poll_spec.tv_nsec = (long)(now.tv_usec) * 1000L
			+ (long)(poll_ms % 1000) * 1000000L;
		while (poll_spec.tv_nsec >= 1000000000) {
			poll_spec.tv_sec++;
			poll_spec.tv_nsec -= 1000000000;
		}

		wait_result = ast_cond_timedwait(&ctx->cond, ao2_object_get_lockaddr(ctx), &poll_spec);
		if (wait_result != 0 && wait_result != ETIMEDOUT) {
			ast_log(LOG_WARNING,
				"Channel %s: unexpected error waiting for claim: %s (%d)\n",
				channel_id, strerror(wait_result), wait_result);
			break;
		}
		/* Loop back: re-check claimed, then hangup, then deadline */
	}

	if (ctx->claimed) {
		ast_debug(1, "Channel %s claimed by %s\n",
			channel_id, ctx->winner_app);
		result = 0;
	}
	ao2_unlock(ctx);

	return result;
}

/*!
 * \brief Clean up broadcast context for a channel
 *
 * This is the normal-path cleanup called by the dialplan application
 * after the broadcast completes.  The channel datastore destructor
 * (broadcast_datastore_destroy) also unlinks the context as a safety
 * net for abnormal teardown; ao2_unlink is idempotent so the double
 * call is harmless.
 *
 * \param channel_id The unique ID of the channel
 */
void AST_OPTIONAL_API_NAME(stasis_app_broadcast_cleanup)(const char *channel_id)
{
	RAII_VAR(struct stasis_broadcast_ctx *, ctx, NULL, ao2_cleanup);

	if (ast_strlen_zero(channel_id) || !broadcast_contexts) {
		return;
	}

	ctx = ao2_find(broadcast_contexts, channel_id, OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (ctx) {
		ast_debug(3, "Cleaning up broadcast context for %s\n", channel_id);
	}
}

static int load_module(void)
{
	broadcast_contexts = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		BROADCAST_BUCKETS, stasis_broadcast_ctx_hash_fn, NULL, stasis_broadcast_ctx_cmp_fn);

	if (!broadcast_contexts) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_debug(1, "Stasis broadcast module loaded\n");
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	/* NULL the global pointer before releasing the reference so that
	 * concurrent lookups see NULL (safe) rather than a freed pointer. */
	{
		struct ao2_container *old_contexts = broadcast_contexts;
		broadcast_contexts = NULL;
		ao2_cleanup(old_contexts);
	}

	ast_debug(1, "Stasis broadcast module unloaded\n");
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
