/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2009, Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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
 * \brief After Bridge Execution API
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/bridge_after.h"

struct after_bridge_cb_node {
	/*! Next list node. */
	AST_LIST_ENTRY(after_bridge_cb_node) list;
	/*! Desired callback function. */
	ast_bridge_after_cb callback;
	/*! After bridge callback will not be called and destroy any resources data may contain. */
	ast_bridge_after_cb_failed failed;
	/*! Extra data to pass to the callback. */
	void *data;
	/*! Reason the after bridge callback failed. */
	enum ast_bridge_after_cb_reason reason;
};

struct after_bridge_cb_ds {
	/*! After bridge callbacks container. */
	AST_LIST_HEAD(, after_bridge_cb_node) callbacks;
};

/*!
 * \internal
 * \brief Indicate after bridge callback failed.
 * \since 12.0.0
 *
 * \param node After bridge callback node.
 *
 * \return Nothing
 */
static void after_bridge_cb_failed(struct after_bridge_cb_node *node)
{
	if (node->failed) {
		node->failed(node->reason, node->data);
		node->failed = NULL;
	}
}

/*!
 * \internal
 * \brief Run discarding any after bridge callbacks.
 * \since 12.0.0
 *
 * \param after_bridge After bridge callback container process.
 * \param reason Why are we doing this.
 *
 * \return Nothing
 */
static void after_bridge_cb_run_discard(struct after_bridge_cb_ds *after_bridge, enum ast_bridge_after_cb_reason reason)
{
	struct after_bridge_cb_node *node;

	for (;;) {
		AST_LIST_LOCK(&after_bridge->callbacks);
		node = AST_LIST_REMOVE_HEAD(&after_bridge->callbacks, list);
		AST_LIST_UNLOCK(&after_bridge->callbacks);
		if (!node) {
			break;
		}
		if (!node->reason) {
			node->reason = reason;
		}
		after_bridge_cb_failed(node);
		ast_free(node);
	}
}

/*!
 * \internal
 * \brief Destroy the after bridge callback datastore.
 * \since 12.0.0
 *
 * \param data After bridge callback data to destroy.
 *
 * \return Nothing
 */
static void after_bridge_cb_destroy(void *data)
{
	struct after_bridge_cb_ds *after_bridge = data;

	after_bridge_cb_run_discard(after_bridge, AST_BRIDGE_AFTER_CB_REASON_DESTROY);

	AST_LIST_HEAD_DESTROY(&after_bridge->callbacks);
	ast_free(after_bridge);
}

static struct after_bridge_cb_ds *after_bridge_cb_find(struct ast_channel *chan);

/*!
 * \internal
 * \brief Fixup the after bridge callback datastore.
 * \since 12.0.0
 *
 * \param data After bridge callback data to fixup.
 * \param old_chan The datastore is moving from this channel.
 * \param new_chan The datastore is moving to this channel.
 *
 * \return Nothing
 */
static void after_bridge_cb_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	struct after_bridge_cb_ds *after_bridge;
	struct after_bridge_cb_node *node;

	after_bridge = after_bridge_cb_find(new_chan);
	if (!after_bridge) {
		return;
	}

	AST_LIST_LOCK(&after_bridge->callbacks);
	node = AST_LIST_LAST(&after_bridge->callbacks);
	if (node && !node->reason) {
		node->reason = AST_BRIDGE_AFTER_CB_REASON_MASQUERADE;
	}
	AST_LIST_UNLOCK(&after_bridge->callbacks);
}

static const struct ast_datastore_info after_bridge_cb_info = {
	.type = "after-bridge-cb",
	.destroy = after_bridge_cb_destroy,
	.chan_fixup = after_bridge_cb_fixup,
};

/*!
 * \internal
 * \brief Find an after bridge callback datastore container.
 * \since 12.0.0
 *
 * \param chan Channel to find the after bridge callback container on.
 *
 * \retval after_bridge datastore container on success.
 * \retval NULL on error.
 */
static struct after_bridge_cb_ds *after_bridge_cb_find(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	SCOPED_CHANNELLOCK(lock, chan);

	datastore = ast_channel_datastore_find(chan, &after_bridge_cb_info, NULL);
	if (!datastore) {
		return NULL;
	}
	return datastore->data;
}

/*!
 * \internal
 * \brief Setup/create an after bridge callback datastore container.
 * \since 12.0.0
 *
 * \param chan Channel to setup/create the after bridge callback container on.
 *
 * \retval after_bridge datastore container on success.
 * \retval NULL on error.
 */
static struct after_bridge_cb_ds *after_bridge_cb_setup(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	struct after_bridge_cb_ds *after_bridge;
	SCOPED_CHANNELLOCK(lock, chan);

	datastore = ast_channel_datastore_find(chan, &after_bridge_cb_info, NULL);
	if (datastore) {
		return datastore->data;
	}

	/* Create a new datastore. */
	datastore = ast_datastore_alloc(&after_bridge_cb_info, NULL);
	if (!datastore) {
		return NULL;
	}
	after_bridge = ast_calloc(1, sizeof(*after_bridge));
	if (!after_bridge) {
		ast_datastore_free(datastore);
		return NULL;
	}
	AST_LIST_HEAD_INIT(&after_bridge->callbacks);
	datastore->data = after_bridge;
	ast_channel_datastore_add(chan, datastore);

	return datastore->data;
}

void ast_bridge_run_after_callback(struct ast_channel *chan)
{
	struct after_bridge_cb_ds *after_bridge;
	struct after_bridge_cb_node *node;

	after_bridge = after_bridge_cb_find(chan);
	if (!after_bridge) {
		return;
	}

	for (;;) {
		AST_LIST_LOCK(&after_bridge->callbacks);
		node = AST_LIST_REMOVE_HEAD(&after_bridge->callbacks, list);
		AST_LIST_UNLOCK(&after_bridge->callbacks);
		if (!node) {
			break;
		}
		if (node->reason) {
			after_bridge_cb_failed(node);
		} else {
			node->failed = NULL;
			node->callback(chan, node->data);
		}
		ast_free(node);
	}
}

void ast_bridge_discard_after_callback(struct ast_channel *chan, enum ast_bridge_after_cb_reason reason)
{
	struct after_bridge_cb_ds *after_bridge;

	after_bridge = after_bridge_cb_find(chan);
	if (!after_bridge) {
		return;
	}

	after_bridge_cb_run_discard(after_bridge, reason);
}

int ast_bridge_set_after_callback(struct ast_channel *chan, ast_bridge_after_cb callback, ast_bridge_after_cb_failed failed, void *data)
{
	struct after_bridge_cb_ds *after_bridge;
	struct after_bridge_cb_node *new_node;
	struct after_bridge_cb_node *last_node;

	/* Sanity checks. */
	ast_assert(chan != NULL);
	if (!chan || !callback) {
		return -1;
	}

	after_bridge = after_bridge_cb_setup(chan);
	if (!after_bridge) {
		return -1;
	}

	/* Create a new callback node. */
	new_node = ast_calloc(1, sizeof(*new_node));
	if (!new_node) {
		return -1;
	}
	new_node->callback = callback;
	new_node->failed = failed;
	new_node->data = data;

	/* Put it in the container disabling any previously active one. */
	AST_LIST_LOCK(&after_bridge->callbacks);
	last_node = AST_LIST_LAST(&after_bridge->callbacks);
	if (last_node && !last_node->reason) {
		last_node->reason = AST_BRIDGE_AFTER_CB_REASON_REPLACED;
	}
	AST_LIST_INSERT_TAIL(&after_bridge->callbacks, new_node, list);
	AST_LIST_UNLOCK(&after_bridge->callbacks);
	return 0;
}

const char *reason_strings[] = {
	[AST_BRIDGE_AFTER_CB_REASON_DESTROY] = "Channel destroyed (hungup)",
	[AST_BRIDGE_AFTER_CB_REASON_REPLACED] = "Callback was replaced",
	[AST_BRIDGE_AFTER_CB_REASON_MASQUERADE] = "Channel masqueraded",
	[AST_BRIDGE_AFTER_CB_REASON_DEPART] = "Channel was departed from bridge",
	[AST_BRIDGE_AFTER_CB_REASON_REMOVED] = "Callback was removed",
};

const char *ast_bridge_after_cb_reason_string(enum ast_bridge_after_cb_reason reason)
{
	if (reason < AST_BRIDGE_AFTER_CB_REASON_DESTROY
		|| AST_BRIDGE_AFTER_CB_REASON_REMOVED < reason
		|| !reason_strings[reason]) {
		return "Unknown";
	}

	return reason_strings[reason];
}

struct after_bridge_goto_ds {
	/*! Goto string that can be parsed by ast_parseable_goto(). */
	const char *parseable_goto;
	/*! Specific goto context or default context for parseable_goto. */
	const char *context;
	/*! Specific goto exten or default exten for parseable_goto. */
	const char *exten;
	/*! Specific goto priority or default priority for parseable_goto. */
	int priority;
	/*! TRUE if the peer should run the h exten. */
	unsigned int run_h_exten:1;
	/*! Specific goto location */
	unsigned int specific:1;
};

/*!
 * \internal
 * \brief Destroy the after bridge goto datastore.
 * \since 12.0.0
 *
 * \param data After bridge goto data to destroy.
 *
 * \return Nothing
 */
static void after_bridge_goto_destroy(void *data)
{
	struct after_bridge_goto_ds *after_bridge = data;

	ast_free((char *) after_bridge->parseable_goto);
	ast_free((char *) after_bridge->context);
	ast_free((char *) after_bridge->exten);
	ast_free((char *) after_bridge);
}

/*!
 * \internal
 * \brief Fixup the after bridge goto datastore.
 * \since 12.0.0
 *
 * \param data After bridge goto data to fixup.
 * \param old_chan The datastore is moving from this channel.
 * \param new_chan The datastore is moving to this channel.
 *
 * \return Nothing
 */
static void after_bridge_goto_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	/* There can be only one.  Discard any already on the new channel. */
	ast_bridge_discard_after_goto(new_chan);
}

static const struct ast_datastore_info after_bridge_goto_info = {
	.type = "after-bridge-goto",
	.destroy = after_bridge_goto_destroy,
	.chan_fixup = after_bridge_goto_fixup,
};

/*!
 * \internal
 * \brief Remove channel goto location after the bridge and return it.
 * \since 12.0.0
 *
 * \param chan Channel to remove after bridge goto location.
 *
 * \retval datastore on success.
 * \retval NULL on error or not found.
 */
static struct ast_datastore *after_bridge_goto_remove(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &after_bridge_goto_info, NULL);
	if (datastore && ast_channel_datastore_remove(chan, datastore)) {
		datastore = NULL;
	}
	ast_channel_unlock(chan);

	return datastore;
}

void ast_bridge_discard_after_goto(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	datastore = after_bridge_goto_remove(chan);
	if (datastore) {
		ast_datastore_free(datastore);
	}
}

void ast_bridge_read_after_goto(struct ast_channel *chan, char *buffer, size_t buf_size)
{
	struct ast_datastore *datastore;
	struct after_bridge_goto_ds *after_bridge;
	char *current_pos = buffer;
	size_t remaining_size = buf_size;

	SCOPED_CHANNELLOCK(lock, chan);

	datastore = ast_channel_datastore_find(chan, &after_bridge_goto_info, NULL);
	if (!datastore) {
		buffer[0] = '\0';
		return;
	}

	after_bridge = datastore->data;

	if (after_bridge->parseable_goto) {
		snprintf(buffer, buf_size, "%s", after_bridge->parseable_goto);
		return;
	}

	if (!ast_strlen_zero(after_bridge->context)) {
		snprintf(current_pos, remaining_size, "%s,", after_bridge->context);
		remaining_size = remaining_size - strlen(current_pos);
		current_pos += strlen(current_pos);
	}

	if (after_bridge->run_h_exten) {
		snprintf(current_pos, remaining_size, "h,");
		remaining_size = remaining_size - strlen(current_pos);
		current_pos += strlen(current_pos);
	} else if (!ast_strlen_zero(after_bridge->exten)) {
		snprintf(current_pos, remaining_size, "%s,", after_bridge->exten);
		remaining_size = remaining_size - strlen(current_pos);
		current_pos += strlen(current_pos);
	}

	snprintf(current_pos, remaining_size, "%d", after_bridge->priority);
}

int ast_bridge_setup_after_goto(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	struct after_bridge_goto_ds *after_bridge;
	int goto_failed = -1;

	/* We are going to be leaving the bridging system now;
	 * clear any pending unbridge flags
	 */
	ast_channel_set_unbridged(chan, 0);

	/* Determine if we are going to setup a dialplan location and where. */
	if (ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO) {
		/* An async goto has already setup a location. */
		ast_channel_clear_softhangup(chan, AST_SOFTHANGUP_ASYNCGOTO);
		if (!ast_check_hangup(chan)) {
			goto_failed = 0;
		}
		return goto_failed;
	}

	/* Get after bridge goto datastore. */
	datastore = after_bridge_goto_remove(chan);
	if (!datastore) {
		return goto_failed;
	}

	after_bridge = datastore->data;
	if (after_bridge->run_h_exten) {
		if (ast_exists_extension(chan, after_bridge->context, "h", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid,
				ast_channel_caller(chan)->id.number.str, NULL))) {
			ast_debug(1, "Running after bridge goto h exten %s,h,1\n",
				ast_channel_context(chan));
			ast_pbx_h_exten_run(chan, after_bridge->context);
		}
	} else if (!ast_check_hangup(chan)) {
		/* Clear the outgoing flag */
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING);

		if (after_bridge->specific) {
			goto_failed = ast_explicit_goto(chan, after_bridge->context,
				after_bridge->exten, after_bridge->priority);
		} else if (!ast_strlen_zero(after_bridge->parseable_goto)) {
			char *context;
			char *exten;
			int priority;

			/* Option F(x) for Bridge(), Dial(), and Queue() */

			/* Save current dialplan location in case of failure. */
			context = ast_strdupa(ast_channel_context(chan));
			exten = ast_strdupa(ast_channel_exten(chan));
			priority = ast_channel_priority(chan);

			/* Set current dialplan position to default dialplan position */
			ast_explicit_goto(chan, after_bridge->context, after_bridge->exten,
				after_bridge->priority);

			/* Then perform the goto */
			goto_failed = ast_parseable_goto(chan, after_bridge->parseable_goto);
			if (goto_failed) {
				/* Restore original dialplan location. */
				ast_channel_context_set(chan, context);
				ast_channel_exten_set(chan, exten);
				ast_channel_priority_set(chan, priority);
			}
		} else {
			/* Option F() for Bridge(), Dial(), and Queue() */
			goto_failed = ast_goto_if_exists(chan, after_bridge->context,
				after_bridge->exten, after_bridge->priority + 1);
		}
		if (!goto_failed) {
			if (ast_test_flag(ast_channel_flags(chan), AST_FLAG_IN_AUTOLOOP)) {
				ast_channel_priority_set(chan, ast_channel_priority(chan) + 1);
			}

			ast_debug(1, "Setup after bridge goto location to %s,%s,%d.\n",
				ast_channel_context(chan),
				ast_channel_exten(chan),
				ast_channel_priority(chan));
		}
	}

	/* Discard after bridge goto datastore. */
	ast_datastore_free(datastore);

	return goto_failed;
}

void ast_bridge_run_after_goto(struct ast_channel *chan)
{
	int goto_failed;

	goto_failed = ast_bridge_setup_after_goto(chan);
	if (goto_failed || ast_pbx_run(chan)) {
		ast_hangup(chan);
	}
}

/*!
 * \internal
 * \brief Set after bridge goto location of channel.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 * \param run_h_exten TRUE if the h exten should be run.
 * \param specific TRUE if the context/exten/priority is exactly specified.
 * \param context Context to goto after bridge.
 * \param exten Exten to goto after bridge. (Could be NULL if run_h_exten)
 * \param priority Priority to goto after bridge.
 * \param parseable_goto User specified goto string. (Could be NULL)
 *
 * \details Add a channel datastore to setup the goto location
 * when the channel leaves the bridge and run a PBX from there.
 *
 * If run_h_exten then execute the h exten found in the given context.
 * Else if specific then goto the given context/exten/priority.
 * Else if parseable_goto then use the given context/exten/priority
 *   as the relative position for the parseable_goto.
 * Else goto the given context/exten/priority+1.
 *
 * \return Nothing
 */
static void __after_bridge_set_goto(struct ast_channel *chan, int run_h_exten, int specific, const char *context, const char *exten, int priority, const char *parseable_goto)
{
	struct ast_datastore *datastore;
	struct after_bridge_goto_ds *after_bridge;

	/* Sanity checks. */
	ast_assert(chan != NULL);
	if (!chan) {
		return;
	}
	if (run_h_exten) {
		ast_assert(run_h_exten && context);
		if (!context) {
			return;
		}
	} else {
		ast_assert(context && exten && 0 < priority);
		if (!context || !exten || priority < 1) {
			return;
		}
	}

	/* Create a new datastore. */
	datastore = ast_datastore_alloc(&after_bridge_goto_info, NULL);
	if (!datastore) {
		return;
	}
	after_bridge = ast_calloc(1, sizeof(*after_bridge));
	if (!after_bridge) {
		ast_datastore_free(datastore);
		return;
	}

	/* Initialize it. */
	after_bridge->parseable_goto = ast_strdup(parseable_goto);
	after_bridge->context = ast_strdup(context);
	after_bridge->exten = ast_strdup(exten);
	after_bridge->priority = priority;
	after_bridge->run_h_exten = run_h_exten ? 1 : 0;
	after_bridge->specific = specific ? 1 : 0;
	datastore->data = after_bridge;
	if ((parseable_goto && !after_bridge->parseable_goto)
		|| (context && !after_bridge->context)
		|| (exten && !after_bridge->exten)) {
		ast_datastore_free(datastore);
		return;
	}

	/* Put it on the channel replacing any existing one. */
	ast_channel_lock(chan);
	ast_bridge_discard_after_goto(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
}

void ast_bridge_set_after_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	__after_bridge_set_goto(chan, 0, 1, context, exten, priority, NULL);
}

void ast_bridge_set_after_h(struct ast_channel *chan, const char *context)
{
	__after_bridge_set_goto(chan, 1, 0, context, NULL, 1, NULL);
}

void ast_bridge_set_after_go_on(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *parseable_goto)
{
	char *p_goto;

	if (!ast_strlen_zero(parseable_goto)) {
		p_goto = ast_strdupa(parseable_goto);
		ast_replace_subargument_delimiter(p_goto);
	} else {
		p_goto = NULL;
	}
	__after_bridge_set_goto(chan, 0, 0, context, exten, priority, p_goto);
}
