/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief Function that intercepts HOLD frames from channels and raises events
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/frame.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"

/*** DOCUMENTATION
	<function name="HOLD_INTERCEPT" language="en_US">
		<synopsis>
			Intercepts hold frames on a channel and raises an event instead of passing the frame on
		</synopsis>
		<syntax>
			<parameter name="action" required="true">
				<optionlist>
					<option name="remove">
						<para>W/O. Removes the hold interception function.</para>
					</option>
					<option name="set">
						<para>W/O. Enable hold interception on the channel. When
						enabled, the channel will intercept any hold action that
						is signalled from the device, and instead simply raise an
						event (AMI/ARI) indicating that the channel wanted to put other
						parties on hold.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
	</function>
***/

/*! \brief Private data structure used with the function's datastore */
struct hold_intercept_data {
	int framehook_id;
};

/*! \brief The channel datastore the function uses to store state */
static const struct ast_datastore_info hold_intercept_datastore = {
	.type = "hold_intercept",
};

/*! \internal \brief Disable hold interception on the channel */
static int remove_hold_intercept(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct hold_intercept_data *data;
	SCOPED_CHANNELLOCK(chan_lock, chan);

	datastore = ast_channel_datastore_find(chan, &hold_intercept_datastore, NULL);
	if (!datastore) {
		ast_log(AST_LOG_WARNING, "Cannot remove HOLD_INTERCEPT from %s: HOLD_INTERCEPT not currently enabled\n",
		        ast_channel_name(chan));
		return -1;
	}
	data = datastore->data;

	if (ast_framehook_detach(chan, data->framehook_id)) {
		ast_log(AST_LOG_WARNING, "Failed to remove HOLD_INTERCEPT framehook from channel %s\n",
		        ast_channel_name(chan));
		return -1;
	}

	if (ast_channel_datastore_remove(chan, datastore)) {
		ast_log(AST_LOG_WARNING, "Failed to remove HOLD_INTERCEPT datastore from channel %s\n",
		        ast_channel_name(chan));
		return -1;
	}
	ast_datastore_free(datastore);

	return 0;
}

/*! \brief Frame hook that is called to intercept hold/unhold */
static struct ast_frame *hold_intercept_framehook(struct ast_channel *chan,
	struct ast_frame *f, enum ast_framehook_event event, void *data)
{
	int frame_type;

	if (!f || (event != AST_FRAMEHOOK_EVENT_WRITE)) {
		return f;
	}

	if (f->frametype != AST_FRAME_CONTROL) {
		return f;
	}

	frame_type = f->subclass.integer;
	if (frame_type != AST_CONTROL_HOLD && frame_type != AST_CONTROL_UNHOLD) {
		return f;
	}

	/* Munch munch */
	ast_frfree(f);
	f = &ast_null_frame;

	ast_channel_publish_cached_blob(chan,
		frame_type == AST_CONTROL_HOLD ? ast_channel_hold_type() : ast_channel_unhold_type(),
		NULL);

	return f;
}

/*! \brief Callback function which informs upstream if we are consuming a frame of a specific type */
static int hold_intercept_framehook_consume(void *data, enum ast_frame_type type)
{
	return (type == AST_FRAME_CONTROL ? 1 : 0);
}

/*! \internal \brief Enable hold interception on the channel */
static int set_hold_intercept(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	struct hold_intercept_data *data;
	static struct ast_framehook_interface hold_framehook_interface = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = hold_intercept_framehook,
		.consume_cb = hold_intercept_framehook_consume,
		.disable_inheritance = 1,
	};
	SCOPED_CHANNELLOCK(chan_lock, chan);

	datastore = ast_channel_datastore_find(chan, &hold_intercept_datastore, NULL);
	if (datastore) {
		ast_log(AST_LOG_WARNING, "HOLD_INTERCEPT already set on '%s'\n",
		        ast_channel_name(chan));
		return 0;
	}

	datastore = ast_datastore_alloc(&hold_intercept_datastore, NULL);
	if (!datastore) {
		return -1;
	}

	data = ast_calloc(1, sizeof(*data));
	if (!data) {
		ast_datastore_free(datastore);
		return -1;
	}

	data->framehook_id = ast_framehook_attach(chan, &hold_framehook_interface);
	if (data->framehook_id < 0) {
		ast_log(AST_LOG_WARNING, "Failed to attach HOLD_INTERCEPT framehook to '%s'\n",
		        ast_channel_name(chan));
		ast_datastore_free(datastore);
		ast_free(data);
		return -1;
	}
	datastore->data = data;

	ast_channel_datastore_add(chan, datastore);

	return 0;
}

/*! \internal \brief HOLD_INTERCEPT write function callback */
static int hold_intercept_fn_write(struct ast_channel *chan, const char *function,
	char *data, const char *value)
{
	int res;

	if (!chan) {
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(AST_LOG_WARNING, "HOLD_INTERCEPT requires an argument\n");
		return -1;
	}

	if (!strcasecmp(data, "set")) {
		res = set_hold_intercept(chan);
	} else if (!strcasecmp(data, "remove")) {
		res = remove_hold_intercept(chan);
	} else {
		ast_log(AST_LOG_WARNING, "HOLD_INTERCEPT: unknown option %s\n", data);
		res = -1;
	}

	return res;
}

/*! \brief Definition of the HOLD_INTERCEPT function */
static struct ast_custom_function hold_intercept_function = {
	.name = "HOLD_INTERCEPT",
	.write = hold_intercept_fn_write,
};

/*! \internal \brief Unload the module */
static int unload_module(void)
{
	return ast_custom_function_unregister(&hold_intercept_function);
}

/*! \internal \brief Load the module */
static int load_module(void)
{
	return ast_custom_function_register(&hold_intercept_function) ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Hold interception dialplan function");
