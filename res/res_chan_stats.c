/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Statsd channel stats. Exmaple of how to subscribe to Stasis events.
 *
 * This module subscribes to the channel caching topic and issues statsd stats
 * based on the received messages.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 */

/*** MODULEINFO
	<depend>res_statsd</depend>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/statsd.h"
#include "asterisk/time.h"

/*! Regular Stasis subscription */
static struct stasis_subscription *sub;
/*! Stasis message router */
static struct stasis_message_router *router;

/*!
 * \brief Subscription callback for all channel messages.
 * \param data Data pointer given when creating the subscription.
 * \param sub This subscription.
 * \param message The message itself.
 */
static void statsmaker(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, metric, NULL, ast_free);

	if (stasis_subscription_final_message(sub, message)) {
		/* Normally, data points to an object that must be cleaned up.
		 * The final message is an unsubscribe notification that's
		 * guaranteed to be the last message this subscription receives.
		 * This would be a safe place to kick off any needed cleanup.
		 */
		return;
	}

	/* For no good reason, count message types */
	metric = ast_str_create(80);
	if (metric) {
		ast_str_set(&metric, 0, "stasis.message.%s",
			stasis_message_type_name(stasis_message_type(message)));
		ast_statsd_log(ast_str_buffer(metric), AST_STATSD_METER, 1);
	}
}

/*!
 * \brief Router callback for \ref ast_channel_snapshot_update messages.
 * \param data Data pointer given when added to router.
 * \param sub This subscription.
 * \param message The message itself.
 */
static void updates(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	/* Since this came from a message router, we know the type of the
	 * message. We can cast the data without checking its type.
	 */
	struct ast_channel_snapshot_update *update = stasis_message_data(message);

	/* There are three types of channel snapshot updates.
	 * !old && new -> Initial channel creation
	 * old && new -> Updated channel snapshot
	 * old && dead -> Final channel snapshot
	 */

	if (!update->old_snapshot && update->new_snapshot) {
		/* Initial channel snapshot; count a channel creation */
		ast_statsd_log_string("channels.count", AST_STATSD_GAUGE, "+1", 1.0);
	} else if (update->old_snapshot && ast_test_flag(&update->new_snapshot->flags, AST_FLAG_DEAD)) {
		/* Channel is gone. Compute the age of the channel and post
		 * that, as well as decrementing the channel count.
		 */
		int64_t age;

		age = ast_tvdiff_ms(*stasis_message_timestamp(message),
			update->new_snapshot->base->creationtime);
		ast_statsd_log("channels.calltime", AST_STATSD_TIMER, age);

		/* And decrement the channel count */
		ast_statsd_log_string("channels.count", AST_STATSD_GAUGE, "-1", 1.0);
	}
}

/*!
 * \brief Router callback for any message that doesn't otherwise have a route.
 * \param data Data pointer given when added to router.
 * \param sub This subscription.
 * \param message The message itself.
 */
static void default_route(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	if (stasis_subscription_final_message(sub, message)) {
		/* Much like with the regular subscription, you may need to
		 * perform some cleanup when done with a message router. You
		 * can look for the final message in the default route.
		 */
		return;
	}
}

static int unload_module(void)
{
	stasis_unsubscribe_and_join(sub);
	sub = NULL;
	stasis_message_router_unsubscribe_and_join(router);
	router = NULL;
	return 0;
}

static int load_module(void)
{
	/* You can create a message router to route messages by type */
	router = stasis_message_router_create(
		ast_channel_topic_all());
	if (!router) {
		return AST_MODULE_LOAD_DECLINE;
	}
	stasis_message_router_add(router, ast_channel_snapshot_type(),
		updates, NULL);
	stasis_message_router_set_default(router, default_route, NULL);

	/* Or a subscription to receive all of the messages from a topic */
	sub = stasis_subscribe(ast_channel_topic_all(), statsmaker, NULL);
	if (!sub) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Example of how to use Stasis",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_statsd"
);
