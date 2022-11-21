/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
 * \brief Function that drops specified frames from channels
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/framehook.h"

/*** DOCUMENTATION
	<function name="FRAME_DROP" language="en_US">
		<since>
			<version>16.21.0</version>
			<version>18.7.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Drops specific frame types in the TX or RX direction on a channel.
		</synopsis>
		<syntax>
			<parameter name="direction" required="true">
				<para>List of frame types to be dropped for the specified direction. Direction can be <literal>TX</literal> or <literal>RX</literal>. The <literal>TX</literal> direction will prevent Asterisk from sending frames to a channel, and the <literal>RX</literal> direction will prevent Asterisk from receiving frames from a channel.</para>
				<para>Subsequent calls to this function will replace previous settings, allowing certain frames to be dropped only temporarily, for instance.</para>
				<para>Below are the different types of frames that can be dropped. Other actions may need to be taken in conjunction with use of this function:
				for instance, if you drop ANSWER control frames, you should explicitly use <literal>Progress()</literal> for your call or undesired behavior
				may occur.</para>
				<enumlist>
					<enum name = "DTMF_BEGIN" />
					<enum name = "DTMF_END" />
					<enum name = "VOICE" />
					<enum name = "VIDEO" />
					<enum name = "CONTROL" />
					<enum name = "NULL" />
					<enum name = "IAX" />
					<enum name = "TEXT" />
					<enum name = "TEXT_DATA" />
					<enum name = "IMAGE" />
					<enum name = "HTML" />
					<enum name = "CNG" />
					<enum name = "MODEM" />
				</enumlist>
				<para>The following CONTROL frames can also be dropped:</para>
				<enumlist>
					<enum name = "RING" />
					<enum name = "RINGING" />
					<enum name = "ANSWER" />
					<enum name = "BUSY" />
					<enum name = "TAKEOFFHOOK" />
					<enum name = "OFFHOOK" />
					<enum name = "CONGESTION" />
					<enum name = "FLASH" />
					<enum name = "WINK" />
					<enum name = "PROGRESS" />
					<enum name = "PROCEEDING" />
					<enum name = "HOLD" />
					<enum name = "UNHOLD" />
					<enum name = "VIDUPDATE" />
					<enum name = "CONNECTED_LINE" />
					<enum name = "REDIRECTING" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Examples:</para>
			<example title="Drop only DTMF frames towards this channel">
			exten => 1,1,Set(FRAME_DROP(TX)=DTMF_BEGIN,DTMF_END)
			</example>
			<example title="Drop only Answer control frames towards this channel">
			exten => 1,1,Set(FRAME_DROP(TX)=ANSWER)
			</example>
			<example title="Drop only DTMF frames received on this channel">
			exten => 1,1,Set(FRAME_DROP(RX)=DTMF_BEGIN,DTMF_END)
			</example>
		</description>
	</function>
 ***/

static struct {
	enum ast_frame_type type;
	const char *str;
} frametype2str[] = {
	{ AST_FRAME_DTMF_BEGIN,   ",DTMF_BEGIN," },
	{ AST_FRAME_DTMF_END,   ",DTMF_END," },
	{ AST_FRAME_VOICE,   ",VOICE," },
	{ AST_FRAME_VIDEO,   ",VIDEO," },
	{ AST_FRAME_CONTROL,   ",CONTROL," },
	{ AST_FRAME_NULL,   ",NULL," },
	{ AST_FRAME_IAX,   ",IAX," },
	{ AST_FRAME_TEXT,   ",TEXT," },
	{ AST_FRAME_TEXT_DATA,   ",TEXT_DATA," },
	{ AST_FRAME_IMAGE,   ",IMAGE," },
	{ AST_FRAME_HTML,   ",HTML," },
	{ AST_FRAME_CNG,   ",CNG," },
	{ AST_FRAME_MODEM,   ",MODEM," },
};

static struct {
	int type;
	const char *str;
} controlframetype2str[] = {
	{ AST_CONTROL_RING,   ",RING," },
	{ AST_CONTROL_RINGING,   ",RINGING," },
	{ AST_CONTROL_ANSWER,   ",ANSWER," },
	{ AST_CONTROL_BUSY,   ",BUSY," },
	{ AST_CONTROL_TAKEOFFHOOK,   ",TAKEOFFHOOK," },
	{ AST_CONTROL_OFFHOOK,   ",OFFHOOK," },
	{ AST_CONTROL_CONGESTION,   ",CONGESTION," },
	{ AST_CONTROL_FLASH,   ",FLASH," },
	{ AST_CONTROL_WINK,   ",WINK," },
	{ AST_CONTROL_PROGRESS,   ",PROGRESS," },
	{ AST_CONTROL_PROCEEDING,   ",PROCEEDING," },
	{ AST_CONTROL_HOLD,   ",HOLD," },
	{ AST_CONTROL_UNHOLD,   ",UNHOLD," },
	{ AST_CONTROL_VIDUPDATE,   ",VIDUPDATE," },
	{ AST_CONTROL_CONNECTED_LINE,   ",CONNECTED_LINE," },
	{ AST_CONTROL_REDIRECTING,   ",REDIRECTING," },
};

enum direction {
    TX = 0,
    RX,
};

struct frame_drop_data {
	enum direction list_type;
	int values[ARRAY_LEN(frametype2str)];
	int controlvalues[ARRAY_LEN(controlframetype2str)];
};

static void datastore_destroy_cb(void *data) {
	ast_free(data);
}

static const struct ast_datastore_info frame_drop_datastore = {
	.type = "framedrop",
	.destroy = datastore_destroy_cb
};

static void hook_destroy_cb(void *framedata)
{
	ast_free(framedata);
}

static struct ast_frame *hook_event_cb(struct ast_channel *chan, struct ast_frame *frame, enum ast_framehook_event event, void *data)
{
	int i;
	int drop_frame = 0;
	struct frame_drop_data *framedata = data;
	if (!frame) {
		return frame;
	}

	if (!((event == AST_FRAMEHOOK_EVENT_WRITE && framedata->list_type == TX) ||
		(event == AST_FRAMEHOOK_EVENT_READ && framedata->list_type == RX))) {
		return frame;
	}

	if (frame->frametype == AST_FRAME_CONTROL) {
		for (i = 0; i < ARRAY_LEN(controlframetype2str); i++) {
			if (frame->subclass.integer == controlframetype2str[i].type) {
				if (framedata->controlvalues[i]) {
					drop_frame = 1;
				}
				break;
			}
		}
	} else {
		for (i = 0; i < ARRAY_LEN(frametype2str); i++) {
			if (frame->frametype == frametype2str[i].type) {
				if (framedata->values[i]) {
					drop_frame = 1;
				}
				break;
			}
		}
	}

	if (drop_frame) {
		ast_frfree(frame);
		frame = &ast_null_frame;
	}
	return frame;
}

static int frame_drop_helper(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	char *buffer;
	struct frame_drop_data *framedata;
	struct ast_datastore *datastore = NULL;
	struct ast_framehook_interface interface = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = hook_event_cb,
		.destroy_cb = hook_destroy_cb,
	};
	int i = 0;

	if (!(framedata = ast_calloc(1, sizeof(*framedata)))) {
		return 0;
	}

	interface.data = framedata;

	if (!strcasecmp(data, "RX")) {
		framedata->list_type = RX;
	} else {
		framedata->list_type = TX;
	}

	buffer = ast_malloc(sizeof(value) + 3); /* leading and trailing comma and null terminator */
	snprintf(buffer, sizeof(value) + 2, ",%s,", value);
	for (i = 0; i < ARRAY_LEN(frametype2str); i++) {
		if (strcasestr(buffer, frametype2str[i].str)) {
			framedata->values[i] = 1;
		}
	}

	for (i = 0; i < ARRAY_LEN(controlframetype2str); i++) {
		if (strcasestr(buffer, controlframetype2str[i].str)) {
			framedata->controlvalues[i] = 1;
		}
	}
	ast_free(buffer);

	ast_channel_lock(chan);
	i = ast_framehook_attach(chan, &interface);
	if (i >= 0) {
		int *id;
		if ((datastore = ast_channel_datastore_find(chan, &frame_drop_datastore, NULL))) {
			id = datastore->data;
			ast_framehook_detach(chan, *id);
			ast_channel_datastore_remove(chan, datastore);
			ast_datastore_free(datastore);
		}

		if (!(datastore = ast_datastore_alloc(&frame_drop_datastore, NULL))) {
			ast_framehook_detach(chan, i);
			ast_channel_unlock(chan);
			return 0;
		}

		if (!(id = ast_calloc(1, sizeof(int)))) {
			ast_datastore_free(datastore);
			ast_framehook_detach(chan, i);
			ast_channel_unlock(chan);
			return 0;
		}

		*id = i; /* Store off the id. The channel is still locked so it is safe to access this ptr. */
		datastore->data = id;
		ast_channel_datastore_add(chan, datastore);
	}
	ast_channel_unlock(chan);

	return 0;
}

static struct ast_custom_function frame_drop_function = {
	.name = "FRAME_DROP",
	.write = frame_drop_helper,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&frame_drop_function);
}

static int load_module(void)
{
	int res = ast_custom_function_register(&frame_drop_function);
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Function to drop frames on a channel.");
