/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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
 * \brief Trace internal ast_frames on a channel.
 *
 * \author David Vossel <dvossel@digium.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/framehook.h"

/*** DOCUMENTATION
	<function name="FRAME_TRACE" language="en_US">
		<synopsis>
			View internal ast_frames as they are read and written on a channel.
		</synopsis>
		<syntax>
			<parameter name="filter list type" required="true">
				<para>A filter can be applied to the trace to limit what frames are viewed.  This
				filter can either be a <literal>white</literal> or <literal>black</literal> list
				of frame types.  When no filter type is present, <literal>white</literal> is
				used.  If no arguments are provided at all, all frames will be output.
				</para>

				<para>Below are the different types of frames that can be filtered.</para>
				<enumlist>
					<enum name = "DTMF_BEGIN" />
					<enum name = "DTMF_END" />
					<enum name = "VOICE" />
					<enum name = "VIDEO" />
					<enum name = "CONTROL" />
					<enum name = "NULL" />
					<enum name = "IAX" />
					<enum name = "TEXT" />
					<enum name = "IMAGE" />
					<enum name = "HTML" />
					<enum name = "CNG" />
					<enum name = "MODEM" />
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Examples:</para>
			<para>exten => 1,1,Set(FRAME_TRACE(white)=DTMF_BEGIN,DTMF_END); view only DTMF frames. </para>
			<para>exten => 1,1,Set(FRAME_TRACE()=DTMF_BEGIN,DTMF_END); view only DTMF frames. </para>
			<para>exten => 1,1,Set(FRAME_TRACE(black)=DTMF_BEGIN,DTMF_END); view everything except DTMF frames. </para>
		</description>
	</function>
 ***/

static void print_frame(struct ast_frame *frame);
static struct {
	enum ast_frame_type type;
	const char *str;
} frametype2str[] = {
	{ AST_FRAME_DTMF_BEGIN,   "DTMF_BEGIN" },
	{ AST_FRAME_DTMF_END,   "DTMF_END" },
	{ AST_FRAME_VOICE,   "VOICE" },
	{ AST_FRAME_VIDEO,   "VIDEO" },
	{ AST_FRAME_CONTROL,   "CONTROL" },
	{ AST_FRAME_NULL,   "NULL" },
	{ AST_FRAME_IAX,   "IAX" },
	{ AST_FRAME_TEXT,   "TEXT" },
	{ AST_FRAME_IMAGE,   "IMAGE" },
	{ AST_FRAME_HTML,   "HTML" },
	{ AST_FRAME_CNG,   "CNG" },
	{ AST_FRAME_MODEM,   "MODEM" },
};

struct frame_trace_data {
	int list_type; /* 0 = white, 1 = black */
	int values[ARRAY_LEN(frametype2str)];
};

static void datastore_destroy_cb(void *data) {
	ast_free(data);
}

static const struct ast_datastore_info frame_trace_datastore = {
	.type = "frametrace",
	.destroy = datastore_destroy_cb
};

static void hook_destroy_cb(void *framedata)
{
	ast_free(framedata);
}

static struct ast_frame *hook_event_cb(struct ast_channel *chan, struct ast_frame *frame, enum ast_framehook_event event, void *data)
{
	int i;
	int show_frame = 0;
	struct frame_trace_data *framedata = data;
	if (!frame) {
		return frame;
	}

	if ((event != AST_FRAMEHOOK_EVENT_WRITE) && (event != AST_FRAMEHOOK_EVENT_READ)) {
		return frame;
	}

	for (i = 0; i < ARRAY_LEN(frametype2str); i++) {
		if (frame->frametype == frametype2str[i].type) {
			if ((framedata->list_type == 0) && (framedata->values[i])) { /* white list */
				show_frame = 1;
			} else if ((framedata->list_type == 1) && (!framedata->values[i])){ /* black list */
				show_frame = 1;
			}
			break;
		}
	}

	if (show_frame) {
		ast_verbose("%s on Channel %s\n", event == AST_FRAMEHOOK_EVENT_READ ? "<--Read" : "--> Write", chan->name);
		print_frame(frame);
	}
	return frame;
}

static int frame_trace_helper(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct frame_trace_data *framedata;
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

	if (!strcasecmp(data, "black")) {
		framedata->list_type = 1;
	}
	for (i = 0; i < ARRAY_LEN(frametype2str); i++) {
		if (strcasestr(value, frametype2str[i].str)) {
			framedata->values[i] = 1;
		}
	}

	ast_channel_lock(chan);
	i = ast_framehook_attach(chan, &interface);
	if (i >= 0) {
		int *id;
		if ((datastore = ast_channel_datastore_find(chan, &frame_trace_datastore, NULL))) {
			id = datastore->data;
			ast_framehook_detach(chan, *id);
			ast_channel_datastore_remove(chan, datastore);
		}

		if (!(datastore = ast_datastore_alloc(&frame_trace_datastore, NULL))) {
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

static void print_frame(struct ast_frame *frame)
{
	switch (frame->frametype) {
	case AST_FRAME_DTMF_END:
		ast_verbose("FrameType: DTMF END\n");
		ast_verbose("Digit: %d\n", frame->subclass.integer);
		break;
	case AST_FRAME_VOICE:
		ast_verbose("FrameType: VOICE\n");
		ast_verbose("Codec: %s\n", ast_getformatname(&frame->subclass.format));
		ast_verbose("MS: %ld\n", frame->len);
		ast_verbose("Samples: %d\n", frame->samples);
		ast_verbose("Bytes: %d\n", frame->datalen);
		break;
	case AST_FRAME_VIDEO:
		ast_verbose("FrameType: VIDEO\n");
		ast_verbose("Codec: %s\n", ast_getformatname(&frame->subclass.format));
		ast_verbose("MS: %ld\n", frame->len);
		ast_verbose("Samples: %d\n", frame->samples);
		ast_verbose("Bytes: %d\n", frame->datalen);
		break;
	case AST_FRAME_CONTROL:
		ast_verbose("FrameType: CONTROL\n");
		switch ((enum ast_control_frame_type) frame->subclass.integer) {
		case AST_CONTROL_HANGUP:
			ast_verbose("SubClass: HANGUP\n");
			break;
		case AST_CONTROL_RING:
			ast_verbose("SubClass: RING\n");
			break;
		case AST_CONTROL_RINGING:
			ast_verbose("SubClass: RINGING\n");
			break;
		case AST_CONTROL_ANSWER:
			ast_verbose("SubClass: ANSWER\n");
			break;
		case AST_CONTROL_BUSY:
			ast_verbose("SubClass: BUSY\n");
			break;
		case AST_CONTROL_TAKEOFFHOOK:
			ast_verbose("SubClass: TAKEOFFHOOK\n");
			break;
		case AST_CONTROL_OFFHOOK:
			ast_verbose("SubClass: OFFHOOK\n");
			break;
		case AST_CONTROL_CONGESTION:
			ast_verbose("SubClass: CONGESTION\n");
			break;
		case AST_CONTROL_FLASH:
			ast_verbose("SubClass: FLASH\n");
			break;
		case AST_CONTROL_WINK:
			ast_verbose("SubClass: WINK\n");
			break;
		case AST_CONTROL_OPTION:
			ast_verbose("SubClass: OPTION\n");
			break;
		case AST_CONTROL_RADIO_KEY:
			ast_verbose("SubClass: RADIO KEY\n");
			break;
		case AST_CONTROL_RADIO_UNKEY:
			ast_verbose("SubClass: RADIO UNKEY\n");
			break;
		case AST_CONTROL_PROGRESS:
			ast_verbose("SubClass: PROGRESS\n");
			break;
		case AST_CONTROL_PROCEEDING:
			ast_verbose("SubClass: PROCEEDING\n");
			break;
		case AST_CONTROL_HOLD:
			ast_verbose("SubClass: HOLD\n");
			break;
		case AST_CONTROL_UNHOLD:
			ast_verbose("SubClass: UNHOLD\n");
			break;
		case AST_CONTROL_VIDUPDATE:
			ast_verbose("SubClass: VIDUPDATE\n");
			break;
		case _XXX_AST_CONTROL_T38:
			ast_verbose("SubClass: XXX T38\n");
			break;
		case AST_CONTROL_SRCUPDATE:
			ast_verbose("SubClass: SRCUPDATE\n");
			break;
		case AST_CONTROL_TRANSFER:
			ast_verbose("SubClass: TRANSFER\n");
			break;
		case AST_CONTROL_CONNECTED_LINE:
			ast_verbose("SubClass: CONNECTED LINE\n");
			break;
		case AST_CONTROL_REDIRECTING:
			ast_verbose("SubClass: REDIRECTING\n");
			break;
		case AST_CONTROL_T38_PARAMETERS:
			ast_verbose("SubClass: T38 PARAMETERS\n");
			break;
		case AST_CONTROL_CC:
			ast_verbose("SubClass: CC\n");
			break;
		case AST_CONTROL_SRCCHANGE:
			ast_verbose("SubClass: SRCCHANGE\n");
			break;
		case AST_CONTROL_READ_ACTION:
			ast_verbose("SubClass: READ ACTION\n");
			break;
		case AST_CONTROL_AOC:
			ast_verbose("SubClass: AOC\n");
			break;
		case AST_CONTROL_MCID:
			ast_verbose("SubClass: MCID\n");
			break;
		case AST_CONTROL_INCOMPLETE:
			ast_verbose("SubClass: INCOMPLETE\n");
			break;
		 case AST_CONTROL_END_OF_Q:
			ast_verbose("SubClass: END_OF_Q\n");
			break;
		case AST_CONTROL_UPDATE_RTP_PEER:
			ast_verbose("SubClass: UPDATE_RTP_PEER\n");
			break;
		}
		
		if (frame->subclass.integer == -1) {
			ast_verbose("SubClass: %d\n", frame->subclass.integer);
		}
		ast_verbose("Bytes: %d\n", frame->datalen);
		break;
	case AST_FRAME_NULL:
		ast_verbose("FrameType: NULL\n");
		break;
	case AST_FRAME_IAX:
		ast_verbose("FrameType: IAX\n");
		break;
	case AST_FRAME_TEXT:
		ast_verbose("FrameType: TXT\n");
		break;
	case AST_FRAME_IMAGE:
		ast_verbose("FrameType: IMAGE\n");
		break;
	case AST_FRAME_HTML:
		ast_verbose("FrameType: HTML\n");
		break;
	case AST_FRAME_CNG:
		ast_verbose("FrameType: CNG\n");
		break;
	case AST_FRAME_MODEM:
		ast_verbose("FrameType: MODEM\n");
		break;
	case AST_FRAME_DTMF_BEGIN:
		ast_verbose("FrameType: DTMF BEGIN\n");
		ast_verbose("Digit: %d\n", frame->subclass.integer);
		break;
	}

	ast_verbose("Src: %s\n", ast_strlen_zero(frame->src) ? "NOT PRESENT" : frame->src);
	ast_verbose("\n");
}

static struct ast_custom_function frame_trace_function = {
	.name = "FRAME_TRACE",
	.write = frame_trace_helper,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&frame_trace_function);
}

static int load_module(void)
{
	int res = ast_custom_function_register(&frame_trace_function);
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Frame Trace for internal ast_frame debugging.");

