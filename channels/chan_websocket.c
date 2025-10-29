/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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
 * \author George Joseph <gjoseph@sangoma.com>
 *
 * \brief Websocket Media Channel
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>res_http_websocket</depend>
	<depend>res_websocket_client</depend>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="chan_websocket" language="en_US">
		<synopsis>Configuration for chan_websocket</synopsis>
		<description><para>
			<emphasis>WebSocket Channel Driver</emphasis>
			</para>
		</description>
		<configFile name="chan_websocket.conf">
			<configObject name="global">
				<since>
					<version>20.17.0.0</version>
					<version>22.7.0</version>
					<version>23.1.0</version>
				</since>
				<synopsis>Global settings for chan_websocket</synopsis>
				<configOption name="control_message_format" default="plain-text">
					<since>
						<version>20.17.0.0</version>
						<version>22.7.0</version>
						<version>23.1.0</version>
					</since>
					<synopsis>Determines the format used for sending and receiving
					control mesages on the websocket.
					</synopsis>
					<description>
						<enumlist>
							<enum name="plain-text">
							<para>The legacy plain text single-line message format.</para>
							</enum>
							<enum name="json">
							<para>Properly formatted JSON.</para>
							</enum>
						</enumlist>
					</description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>

	<info name="Dial_Resource" language="en_US" tech="WebSocket">
		<para>WebSocket Dial Strings:</para>
		<para><literal>Dial(WebSocket/connectionid[/websocket_options])</literal></para>
		<para>WebSocket Parameters:</para>
		<enumlist>
			<enum name="connectionid">
				<para>For outgoing WebSockets, this is the ID of the connection
				in websocket_client.conf to use for the call.  To accept incoming
				WebSocket connections use the literal <literal>INCOMING</literal></para>
			</enum>
			<enum name="websocket_options">
				<para>Options to control how the WebSocket channel behaves.</para>
				<enumlist>
					<enum name="c(codec) - Specify the codec to use in the channel">
						<para></para>
						<para> If not specified, the first codec from the caller's channel will be used.
						</para>
					</enum>
					<enum name="n - Don't auto answer">
						<para>Normally, the WebSocket channel will be answered when
						connection is established with the remote app.  If this
						option is specified however, the channel will not be
						answered until the <literal>ANSWER</literal> command is
						received from the remote app or the remote app calls the
						/channels/answer ARI endpoint.
						</para>
					</enum>
					<enum name="p - Passthrough mode">
						<para>In passthrough mode, the channel driver won't attempt
						to re-frame or re-time media coming in over the websocket from
						the remote app.  This can be used for any codec but MUST be used
						for codecs that use packet headers or whose data stream can't be
						broken up on arbitrary byte boundaries. In this case, the remote
						app is fully responsible for correctly framing and timing media
						sent to Asterisk and the MEDIA text commands that could be sent
						over the websocket are disabled.  Currently, passthrough mode is
						automatically set for the opus, speex and g729 codecs.
						</para>
					</enum>
					<enum name="v(uri_parameters) - Add parameters to the outbound URI">
						<para>This option allows you to add additional parameters to the
						outbound URI.  The format is:
						<literal>v(param1=value1,param2=value2...)</literal>
						</para>
						<para>You must ensure that no parameter name or value contains
						characters not valid in a URL.  The easiest way to do this is to
						use the URIENCODE() dialplan function to encode them.  Be aware
						though that each name and value must be encoded separately.  You
						can't simply encode the whole string.</para>
					</enum>
				</enumlist>
			</enum>
		</enumlist>
		<para>Examples:
		</para>
		<example title="Make an outbound WebSocket connection using connection 'connection1' and the 'sln16' codec.">
		same => n,Dial(WebSocket/connection1/c(sln16))
		</example>
		<example title="Make an outbound WebSocket connection using connection 'connection1' and the 'opus' codec. Passthrough mode will automatically be set.">
		same => n,Dial(WebSocket/connection1/c(opus))
		</example>
		<example title="Listen for an incoming WebSocket connection and don't auto-answer it.">
		same => n,Dial(WebSocket/INCOMING/n)
		</example>
		<example title="Add URI parameters.">
		same => n,Dial(WebSocket/connection1/v(${URIENCODE(vari able)}=${URIENCODE(${CHANNEL})},variable2=$(URIENCODE(${EXTEN})}))
		</example>
	</info>
***/
#include "asterisk.h"

#include "asterisk/app.h"
#include "asterisk/causes.h"
#include "asterisk/channel.h"
#include "asterisk/codec.h"
#include "asterisk/http_websocket.h"
#include "asterisk/format_cache.h"
#include "asterisk/frame.h"
#include "asterisk/json.h"
#include "asterisk/lock.h"
#include "asterisk/mod_format.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/uuid.h"
#include "asterisk/timing.h"
#include "asterisk/translate.h"
#include "asterisk/websocket_client.h"
#include "asterisk/sorcery.h"

static struct ast_sorcery *sorcery = NULL;

enum webchan_control_msg_format {
	WEBCHAN_CONTROL_MSG_FORMAT_PLAIN = 0,
	WEBCHAN_CONTROL_MSG_FORMAT_JSON,
	WEBCHAN_CONTROL_MSG_FORMAT_INVALID,
};

static const char *msg_format_map[] = {
	[WEBCHAN_CONTROL_MSG_FORMAT_PLAIN] = "plain-text",
	[WEBCHAN_CONTROL_MSG_FORMAT_JSON] = "json",
	[WEBCHAN_CONTROL_MSG_FORMAT_INVALID] = "invalid",
};

struct webchan_conf_global {
	SORCERY_OBJECT(details);
	enum webchan_control_msg_format control_msg_format;
};

static struct ast_websocket_server *ast_ws_server;

static struct ao2_container *instances = NULL;

struct websocket_pvt {
	enum ast_websocket_type type;
	struct ast_websocket_client *client;
	struct ast_websocket *websocket;
	struct ast_format *native_format;
	struct ast_codec *native_codec;
	struct ast_format *slin_format;
	struct ast_codec *slin_codec;
	struct ast_channel *channel;
	struct ast_timer *timer;
	struct ast_frame silence;
	struct ast_trans_pvt *translator;
	AST_LIST_HEAD(, ast_frame) frame_queue;
	pthread_t outbound_read_thread;
	size_t bytes_read;
	size_t leftover_len;
	char *uri_params;
	char *leftover_data;
	enum webchan_control_msg_format control_msg_format;
	int no_auto_answer;
	int passthrough;
	int optimal_frame_size;
	int bulk_media_in_progress;
	int report_queue_drained;
	int frame_queue_length;
	int queue_full;
	int queue_paused;
	char connection_id[0];
};

#define MEDIA_WEBSOCKET_OPTIMAL_FRAME_SIZE "MEDIA_WEBSOCKET_OPTIMAL_FRAME_SIZE"
#define MEDIA_WEBSOCKET_CONNECTION_ID "MEDIA_WEBSOCKET_CONNECTION_ID"
#define INCOMING_CONNECTION_ID "INCOMING"

#define ANSWER_CHANNEL "ANSWER"
#define HANGUP_CHANNEL "HANGUP"
#define START_MEDIA_BUFFERING "START_MEDIA_BUFFERING"
#define STOP_MEDIA_BUFFERING "STOP_MEDIA_BUFFERING"
#define FLUSH_MEDIA "FLUSH_MEDIA"
#define GET_DRIVER_STATUS "GET_STATUS"
#define REPORT_QUEUE_DRAINED "REPORT_QUEUE_DRAINED"
#define PAUSE_MEDIA "PAUSE_MEDIA"
#define CONTINUE_MEDIA "CONTINUE_MEDIA"

#if 0
#define MEDIA_START "MEDIA_START"
#define MEDIA_XON "MEDIA_XON"
#define MEDIA_XOFF "MEDIA_XOFF"
#define QUEUE_DRAINED "QUEUE_DRAINED"
#define DRIVER_STATUS "STATUS"
#define MEDIA_BUFFERING_COMPLETED "MEDIA_BUFFERING_COMPLETED"
#define DTMF_END "DTMF_END"
#endif

#define QUEUE_LENGTH_MAX 1000
#define QUEUE_LENGTH_XOFF_LEVEL 900
#define QUEUE_LENGTH_XON_LEVEL 800
#define MAX_TEXT_MESSAGE_LEN MIN(128, (AST_WEBSOCKET_MAX_RX_PAYLOAD_SIZE - 1))

/* Forward declarations */
static struct ast_channel *webchan_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int webchan_call(struct ast_channel *ast, const char *dest, int timeout);
static struct ast_frame *webchan_read(struct ast_channel *ast);
static int webchan_write(struct ast_channel *ast, struct ast_frame *f);
static int webchan_hangup(struct ast_channel *ast);
static int webchan_send_dtmf_text(struct ast_channel *ast, char digit, unsigned int duration);

static struct ast_channel_tech websocket_tech = {
	.type = "WebSocket",
	.description = "Media over WebSocket Channel Driver",
	.requester = webchan_request,
	.call = webchan_call,
	.read = webchan_read,
	.write = webchan_write,
	.hangup = webchan_hangup,
	.send_digit_end = webchan_send_dtmf_text,
};

static enum webchan_control_msg_format control_msg_format_from_str(const char *value)
{
	if (ast_strlen_zero(value)) {
		return WEBCHAN_CONTROL_MSG_FORMAT_INVALID;
	} else if (strcasecmp(value, msg_format_map[WEBCHAN_CONTROL_MSG_FORMAT_PLAIN]) == 0) {
		return WEBCHAN_CONTROL_MSG_FORMAT_PLAIN;
	} else if (strcasecmp(value, msg_format_map[WEBCHAN_CONTROL_MSG_FORMAT_JSON]) == 0) {
		return WEBCHAN_CONTROL_MSG_FORMAT_JSON;
	} else {
		return WEBCHAN_CONTROL_MSG_FORMAT_INVALID;
	}
}

static const char *control_msg_format_to_str(enum webchan_control_msg_format value)
{
	if (!ARRAY_IN_BOUNDS(value, msg_format_map)) {
		return NULL;
	}
	return msg_format_map[value];
}

/*!
 * \internal
 * \brief Catch-all to print events that don't have any data.
 * \warning Do not call directly.
 */
static char *_create_event_nodata(struct websocket_pvt *instance, char *event)
{
	char *payload = NULL;
	if (instance->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_JSON) {
		struct ast_json * msg = ast_json_pack("{ s:s s:s }",
			"event", event,
			"channel_id", ast_channel_uniqueid(instance->channel));
		if (!msg) {
			return NULL;
		}
		payload = ast_json_dump_string_format(msg, AST_JSON_COMPACT);
		ast_json_unref(msg);
	} else {
		payload = ast_strdup(event);
	}

	return payload;
}

#define _create_event_MEDIA_XON(_instance) _create_event_nodata(_instance, "MEDIA_XON");
#define _create_event_MEDIA_XOFF(_instance) _create_event_nodata(_instance, "MEDIA_XOFF");
#define _create_event_QUEUE_DRAINED(_instance) _create_event_nodata(_instance, "QUEUE_DRAINED");

/*!
 * \internal
 * \brief Print the MEDIA_START event.
 * \warning Do not call directly.
 */
static char *_create_event_MEDIA_START(struct websocket_pvt *instance)
{
	char *payload = NULL;

	if (instance->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_JSON) {
		struct ast_json *msg = ast_json_pack("{s:s, s:s, s:s, s:s, s:s, s:i, s:i, s:o }",
			"event", "MEDIA_START",
			"connection_id", instance->connection_id,
			"channel", ast_channel_name(instance->channel),
			"channel_id", ast_channel_uniqueid(instance->channel),
			"format", ast_format_get_name(instance->native_format),
			"optimal_frame_size", instance->optimal_frame_size,
			"ptime", instance->native_codec->default_ms,
			"channel_variables", ast_json_channel_vars(ast_channel_varshead(
						instance->channel))
			);
		if (!msg) {
			return NULL;
		}
		payload = ast_json_dump_string_format(msg, AST_JSON_COMPACT);
		ast_json_unref(msg);
	} else {
		ast_asprintf(&payload, "%s %s:%s %s:%s %s:%s %s:%s %s:%d %s:%d",
			"MEDIA_START",
			"connection_id", instance->connection_id,
			"channel", ast_channel_name(instance->channel),
			"channel_id", ast_channel_uniqueid(instance->channel),
			"format", ast_format_get_name(instance->native_format),
			"optimal_frame_size", instance->optimal_frame_size,
			"ptime", instance->native_codec->default_ms
			);
	}

	return payload;
}

/*!
 * \internal
 * \brief Print the MEDIA_BUFFERING_COMPLETED event.
 * \warning Do not call directly.
 */
static char *_create_event_MEDIA_BUFFERING_COMPLETED(struct websocket_pvt *instance,
	const char *id)
{
	char *payload = NULL;
	if (instance->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_JSON) {
		struct ast_json *msg = ast_json_pack("{s:s, s:s, s:s}",
			"event", "MEDIA_BUFFERING_COMPLETED",
			"channel_id", ast_channel_uniqueid(instance->channel),
			"correlation_id", S_OR(id, "")
			);
		if (!msg) {
			return NULL;
		}
		payload = ast_json_dump_string_format(msg, AST_JSON_COMPACT);
		ast_json_unref(msg);
	} else {
		ast_asprintf(&payload, "%s%s%s",
			"MEDIA_BUFFERING_COMPLETED",
			S_COR(id, " ",""), S_OR(id, ""));

	}

	return payload;
}

/*!
 * \internal
 * \brief Print the DTMF_END event.
 * \warning Do not call directly.
 */
static char *_create_event_DTMF_END(struct websocket_pvt *instance,
	const char digit)
{
	char *payload = NULL;
	if (instance->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_JSON) {
		struct ast_json *msg = ast_json_pack("{s:s, s:s, s:s#}",
			"event", "DTMF_END",
			"channel_id", ast_channel_uniqueid(instance->channel),
			"digit", digit, 1
			);
		if (!msg) {
			return NULL;
		}
		payload = ast_json_dump_string_format(msg, AST_JSON_COMPACT);
		ast_json_unref(msg);
	} else {
		ast_asprintf(&payload, "%s digit:%c channel_id:%s",
			"DTMF_END", digit, ast_channel_uniqueid(instance->channel));
	}

	return payload;
}

/*!
 * \internal
 * \brief Print the STATUS event.
 * \warning Do not call directly.
 */
static char *_create_event_STATUS(struct websocket_pvt *instance)
{
	char *payload = NULL;

	if (instance->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_JSON) {
		struct ast_json *msg = ast_json_pack("{s:s, s:s, s:i, s:i, s:i, s:b, s:b, s:b }",
			"event", "STATUS",
			"channel_id", ast_channel_uniqueid(instance->channel),
			"queue_length", instance->frame_queue_length,
			"xon_level", QUEUE_LENGTH_XON_LEVEL,
			"xoff_level", QUEUE_LENGTH_XOFF_LEVEL,
			"queue_full", instance->queue_full,
			"bulk_media", instance->bulk_media_in_progress,
			"media_paused", instance->queue_paused
			);
		if (!msg) {
			return NULL;
		}
		payload = ast_json_dump_string_format(msg, AST_JSON_COMPACT);
		ast_json_unref(msg);
	} else {
		ast_asprintf(&payload, "%s channel_id:%s queue_length:%d xon_level:%d xoff_level:%d queue_full:%s bulk_media:%s media_paused:%s",
			"STATUS",
			ast_channel_uniqueid(instance->channel),
			instance->frame_queue_length, QUEUE_LENGTH_XON_LEVEL,
			QUEUE_LENGTH_XOFF_LEVEL,
			S_COR(instance->queue_full, "true", "false"),
			S_COR(instance->bulk_media_in_progress, "true", "false"),
			S_COR(instance->queue_paused, "true", "false")
			);
	}

	return payload;
}

/*!
 * \internal
 * \brief Print the ERROR event.
 * \warning Do not call directly.
 */
static char *_create_event_ERROR(struct websocket_pvt *instance,
	const char *error_text)
{
	char *payload = NULL;
	if (instance->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_JSON) {
		struct ast_json *msg = ast_json_pack("{s:s, s:s, s:s}",
			"event", "ERROR",
			"channel_id", ast_channel_uniqueid(instance->channel),
			"error_text", error_text);
		if (!msg) {
			return NULL;
		}
		payload = ast_json_dump_string_format(msg, AST_JSON_COMPACT);
		ast_json_unref(msg);
	} else {
		ast_asprintf(&payload, "%s channel_id:%s error_text:%s",
			"ERROR", ast_channel_uniqueid(instance->channel), error_text);
	}

	return payload;
}

/*!
 * \def create_event
 * \brief Use this macro to create events passing in any event-specific parameters.
 */
#define create_event(_instance, _event, ...) \
	_create_event_ ## _event(_instance, ##__VA_ARGS__)

/*!
 * \def send_event
 * \brief Use this macro to create and send events passing in any event-specific parameters.
 */
#define send_event(_instance, _event, ...) \
({ \
	int _res = -1; \
	char *_payload = _create_event_ ## _event(_instance, ##__VA_ARGS__); \
	if (_payload) { \
		_res = ast_websocket_write_string(_instance->websocket, _payload); \
		if (_res != 0) { \
			ast_log(LOG_ERROR, "%s: Unable to send event %s\n", \
				ast_channel_name(instance->channel), _payload); \
		} else { \
			ast_debug(4, "%s: Sent %s\n", \
				ast_channel_name(instance->channel), _payload); \
		}\
		ast_free(_payload); \
	} \
	(_res); \
})

static void set_channel_format(struct websocket_pvt * instance,
	struct ast_format *fmt)
{
	if (ast_format_cmp(ast_channel_rawreadformat(instance->channel), fmt)
		== AST_FORMAT_CMP_NOT_EQUAL) {
		ast_channel_set_rawreadformat(instance->channel, fmt);
		ast_set_read_format(instance->channel, ast_channel_readformat(instance->channel));
		ast_debug(4, "Switching readformat to %s\n", ast_format_get_name(fmt));
	}
}

/*
 * Reminder...  This function gets called by webchan_read which is
 * triggered by the channel timer firing.  It always gets called
 * every 20ms (or whatever the timer is set to) even if there are
 * no frames in the queue.
 */
static struct ast_frame *dequeue_frame(struct websocket_pvt *instance)
{
	struct ast_frame *queued_frame = NULL;
	SCOPED_LOCK(frame_queue_lock, &instance->frame_queue, AST_LIST_LOCK,
		AST_LIST_UNLOCK);

	/*
	 * If the queue is paused, don't read a frame.  Processing
	 * will continue down the function and a silence frame will
	 * be sent in its place.
	 */
	if (instance->queue_paused) {
		return NULL;
	}

	/*
	 * We need to check if we need to send an XON before anything
	 * else because there are multiple escape paths in this function
	 * and we don't want to accidentally keep the queue in a "full"
	 * state.
	 */
	if (instance->queue_full && instance->frame_queue_length < QUEUE_LENGTH_XON_LEVEL) {
		instance->queue_full = 0;
		ast_debug(4, "%s: WebSocket sending MEDIA_XON\n",
			ast_channel_name(instance->channel));
		send_event(instance, MEDIA_XON);
	}

	queued_frame = AST_LIST_REMOVE_HEAD(&instance->frame_queue, frame_list);

	/*
	 * If there are no frames in the queue, we need to
	 * return NULL so we can send a silence frame.  We also need
	 * to send the QUEUE_DRAINED notification if we were requested
	 * to do so.
	 */
	if (!queued_frame) {
		if (instance->report_queue_drained) {
			instance->report_queue_drained = 0;
			ast_debug(4, "%s: WebSocket sending QUEUE_DRAINED\n",
				ast_channel_name(instance->channel));
			send_event(instance, QUEUE_DRAINED);
		}
		return NULL;
	}

	/*
	 * The only way a control frame could be present here is as
	 * a result of us calling queue_option_frame() in response
	 * to an incoming TEXT command from the websocket.
	 * We'll be safe and make sure it's a AST_CONTROL_OPTION
	 * frame anyway.
	 *
	 * It's quite possible that there are multiple control frames
	 * in a row in the queue so we need to process consecutive ones
	 * immediately.
	 *
	 * In any case, processing a control frame MUST not use up
	 * a media timeslot so after all control frames have been
	 * processed, we need to read an audio frame and process it.
	 */
	while (queued_frame && queued_frame->frametype == AST_FRAME_CONTROL) {
		if (queued_frame->subclass.integer == AST_CONTROL_OPTION) {
			/*
			 * We just need to send the data to the websocket.
			 * The data should already be NULL terminated.
			 */
			ast_websocket_write_string(instance->websocket,
				queued_frame->data.ptr);
			ast_debug(4, "%s: Sent %s\n",
				ast_channel_name(instance->channel), (char *)queued_frame->data.ptr);
		}
		/*
		 * We do NOT send these to the core so we need to free
		 * the frame and grab the next one.  If it's also a
		 * control frame, we need to process it otherwise
		 * continue down in the function.
		 */
		ast_frame_free(queued_frame, 0);
		queued_frame = AST_LIST_REMOVE_HEAD(&instance->frame_queue, frame_list);
		/*
		 * Jut FYI... We didn't bump the queue length when we added the control
		 * frames so we don't need to decrement it here.
		 */
	}

	/*
	 * If, after reading all control frames,  there are no frames
	 * left in the queue, we need to return NULL so we can send
	 * a silence frame.
	 */
	if (!queued_frame) {
		return NULL;
	}

	instance->frame_queue_length--;

	return queued_frame;
}
/*!
 * \internal
 *
 * Called by the core channel thread each time the instance timer fires.
 *
 */
static struct ast_frame *webchan_read(struct ast_channel *ast)
{
	struct websocket_pvt *instance = NULL;
	struct ast_frame *native_frame = NULL;
	struct ast_frame *slin_frame = NULL;

	instance = ast_channel_tech_pvt(ast);
	if (!instance) {
		return NULL;
	}

	if (ast_timer_get_event(instance->timer) == AST_TIMING_EVENT_EXPIRED) {
		ast_timer_ack(instance->timer, 1);
	}

	native_frame = dequeue_frame(instance);

	/*
	 * No frame when the timer fires means we have to create and
	 * return a silence frame in its place.
	 */
	if (!native_frame) {
		ast_debug(5, "%s: WebSocket read timer fired with no frame available.  Returning silence.\n", ast_channel_name(ast));
		set_channel_format(instance, instance->slin_format);
		slin_frame = ast_frdup(&instance->silence);
		return slin_frame;
	}

	/*
	 * If we're in passthrough mode or the frame length is already optimal_frame_size,
	 * we can just return it.
	 */
	if (instance->passthrough || native_frame->datalen == instance->optimal_frame_size) {
		set_channel_format(instance, instance->native_format);
		return native_frame;
	}

	/*
	 * If we're here, we have a short frame that we need to pad
	 * with silence.
	 */

	if (instance->translator) {
		slin_frame = ast_translate(instance->translator, native_frame, 0);
		if (!slin_frame) {
			ast_log(LOG_WARNING, "%s: Failed to translate %d byte frame\n",
				ast_channel_name(ast), native_frame->datalen);
			return NULL;
		}
		ast_frame_free(native_frame, 0);
	} else {
		/*
		 * If there was no translator then the native format
		 * was already slin.
		 */
		slin_frame = native_frame;
	}

	set_channel_format(instance, instance->slin_format);

	/*
	 * So now we have an slin frame but it's probably still short
	 * so we create a new data buffer with the correct length
	 * which is filled with zeros courtesy of ast_calloc.
	 * We then copy the short frame data into the new buffer
	 * and set the offset to AST_FRIENDLY_OFFSET so that
	 * the core can read the data without any issues.
	 * If the original frame data was mallocd, we need to free the old
	 * data buffer so we don't leak memory and we need to set
	 * mallocd to AST_MALLOCD_DATA so that the core knows
	 * it needs to free the new data buffer when it's done.
	 */

	if (slin_frame->datalen != instance->silence.datalen) {
		char *old_data = slin_frame->data.ptr;
		int old_len = slin_frame->datalen;
		int old_offset = slin_frame->offset;
		ast_debug(4, "%s: WebSocket read short frame. Expected %d got %d.  Filling with silence\n",
			ast_channel_name(ast), instance->silence.datalen,
			slin_frame->datalen);

		slin_frame->data.ptr = ast_calloc(1, instance->silence.datalen + AST_FRIENDLY_OFFSET);
		if (!slin_frame->data.ptr) {
			ast_frame_free(slin_frame, 0);
			return NULL;
		}
		slin_frame->data.ptr += AST_FRIENDLY_OFFSET;
		slin_frame->offset = AST_FRIENDLY_OFFSET;
		memcpy(slin_frame->data.ptr, old_data, old_len);
		if (slin_frame->mallocd & AST_MALLOCD_DATA) {
			ast_free(old_data - old_offset);
		}
		slin_frame->mallocd |= AST_MALLOCD_DATA;
		slin_frame->datalen = instance->silence.datalen;
		slin_frame->samples = instance->silence.samples;
	}

	return slin_frame;
}

static int queue_frame_from_buffer(struct websocket_pvt *instance,
	char *buffer, size_t len)
{
	struct ast_frame fr = { 0, };
	struct ast_frame *duped_frame = NULL;

	AST_FRAME_SET_BUFFER(&fr, buffer, 0, len);
	fr.frametype = AST_FRAME_VOICE;
	fr.subclass.format = instance->native_format;
	fr.samples = instance->native_codec->samples_count(&fr);

	duped_frame = ast_frisolate(&fr);
	if (!duped_frame) {
		ast_log(LOG_WARNING, "%s: Failed to isolate frame\n",
			ast_channel_name(instance->channel));
		return -1;
	}

	{
		SCOPED_LOCK(frame_queue_lock, &instance->frame_queue, AST_LIST_LOCK,
			AST_LIST_UNLOCK);
		AST_LIST_INSERT_TAIL(&instance->frame_queue, duped_frame, frame_list);
		instance->frame_queue_length++;
		if (!instance->queue_full && instance->frame_queue_length >= QUEUE_LENGTH_XOFF_LEVEL) {
			instance->queue_full = 1;
			send_event(instance, MEDIA_XOFF);
		}
	}

	ast_debug(5, "%s: Queued %d byte frame\n", ast_channel_name(instance->channel),
		duped_frame->datalen);

	return 0;
}

static int queue_option_frame(struct websocket_pvt *instance,
	char *buffer)
{
	struct ast_frame fr = { 0, };
	struct ast_frame *duped_frame = NULL;

	AST_FRAME_SET_BUFFER(&fr, buffer, 0, strlen(buffer) + 1);
	fr.frametype = AST_FRAME_CONTROL;
	fr.subclass.integer = AST_CONTROL_OPTION;

	duped_frame = ast_frisolate(&fr);
	if (!duped_frame) {
		ast_log(LOG_WARNING, "%s: Failed to isolate frame\n",
			ast_channel_name(instance->channel));
		return -1;
	}

	AST_LIST_LOCK(&instance->frame_queue);
	AST_LIST_INSERT_TAIL(&instance->frame_queue, duped_frame, frame_list);
	AST_LIST_UNLOCK(&instance->frame_queue);

	ast_debug(4, "%s: Queued '%s' option frame\n",
		ast_channel_name(instance->channel), buffer);

	return 0;
}

/*!
 * \internal
 * \brief Handle commands from the websocket
 *
 * \param instance
 * \param buffer Allocated by caller so don't free.
 * \retval 0 Success
 * \retval -1 Failure
 */
static int handle_command(struct websocket_pvt *instance, char *buffer)
{
	int res = 0;
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	const char *command = NULL;
	char *data = NULL;

	if (instance->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_JSON) {
		struct ast_json_error json_error;

		json = ast_json_load_buf(buffer, strlen(buffer), &json_error);
		if (!json) {
			send_event(instance, ERROR, "Unable to parse JSON command");
			return -1;
		}
		command = ast_json_object_string_get(json, "command");
	} else {
		command = buffer;
		data = strchr(buffer, ' ');
		if (data) {
			*data = '\0';
			data++;
		}
	}

	if (ast_strings_equal(command, ANSWER_CHANNEL)) {
		ast_queue_control(instance->channel, AST_CONTROL_ANSWER);

	} else if (ast_strings_equal(command, HANGUP_CHANNEL)) {
		ast_queue_control(instance->channel, AST_CONTROL_HANGUP);

	} else if (ast_strings_equal(command, START_MEDIA_BUFFERING)) {
		if (instance->passthrough) {
			ast_debug(4, "%s: WebSocket in passthrough mode. Ignoring %s command.\n",
				ast_channel_name(instance->channel), command);
			return 0;
		}
		AST_LIST_LOCK(&instance->frame_queue);
		instance->bulk_media_in_progress = 1;
		AST_LIST_UNLOCK(&instance->frame_queue);

	} else if (ast_strings_equal(command, STOP_MEDIA_BUFFERING)) {
		const char *id;
		char *option;
		SCOPED_LOCK(frame_queue_lock, &instance->frame_queue, AST_LIST_LOCK,
			AST_LIST_UNLOCK);

		if (instance->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_JSON) {
			id = ast_json_object_string_get(json, "correlation_id");
		} else {
			id = data;
		}

		if (instance->passthrough) {
			ast_debug(4, "%s: WebSocket in passthrough mode. Ignoring %s command.\n",
				ast_channel_name(instance->channel), command);
			return 0;
		}

		ast_debug(4, "%s: WebSocket %s '%s' with %d bytes in leftover_data.\n",
			ast_channel_name(instance->channel), STOP_MEDIA_BUFFERING, id,
			(int)instance->leftover_len);

		instance->bulk_media_in_progress = 0;
		if (instance->leftover_len > 0) {
			res = queue_frame_from_buffer(instance, instance->leftover_data, instance->leftover_len);
			if (res != 0) {
				return res;
			}
		}
		instance->leftover_len = 0;
		option = create_event(instance, MEDIA_BUFFERING_COMPLETED, id);
		if (!option) {
			return -1;
		}
		res = queue_option_frame(instance, option);
		ast_free(option);

	} else if (ast_strings_equal(command, FLUSH_MEDIA)) {
		struct ast_frame *frame = NULL;

		if (instance->passthrough) {
			ast_debug(4, "%s: WebSocket in passthrough mode. Ignoring %s command.\n",
				ast_channel_name(instance->channel), command);
			return 0;
		}

		AST_LIST_LOCK(&instance->frame_queue);
		while ((frame = AST_LIST_REMOVE_HEAD(&instance->frame_queue, frame_list))) {
			ast_frfree(frame);
		}
		instance->frame_queue_length = 0;
		instance->bulk_media_in_progress = 0;
		instance->leftover_len = 0;
		AST_LIST_UNLOCK(&instance->frame_queue);

	} else if (ast_strings_equal(command, REPORT_QUEUE_DRAINED)) {
		if (instance->passthrough) {
			ast_debug(4, "%s: WebSocket in passthrough mode. Ignoring %s command.\n",
				ast_channel_name(instance->channel), command);
			return 0;
		}

		AST_LIST_LOCK(&instance->frame_queue);
		instance->report_queue_drained = 1;
		AST_LIST_UNLOCK(&instance->frame_queue);

	} else if (ast_strings_equal(command, GET_DRIVER_STATUS)) {
		return send_event(instance, STATUS);

	} else if (ast_strings_equal(command, PAUSE_MEDIA)) {
		if (instance->passthrough) {
			ast_debug(4, "%s: WebSocket in passthrough mode. Ignoring %s command.\n",
				ast_channel_name(instance->channel), command);
			return 0;
		}
		AST_LIST_LOCK(&instance->frame_queue);
		instance->queue_paused = 1;
		AST_LIST_UNLOCK(&instance->frame_queue);

	} else if (ast_strings_equal(command, CONTINUE_MEDIA)) {
		if (instance->passthrough) {
			ast_debug(4, "%s: WebSocket in passthrough mode. Ignoring %s command.\n",
				ast_channel_name(instance->channel), command);
			return 0;
		}
		AST_LIST_LOCK(&instance->frame_queue);
		instance->queue_paused = 0;
		AST_LIST_UNLOCK(&instance->frame_queue);

	} else {
		ast_log(LOG_WARNING, "%s: WebSocket %s command unknown\n",
			ast_channel_name(instance->channel), command);
	}

	return res;
}

static int process_text_message(struct websocket_pvt *instance,
	char *payload, uint64_t payload_len)
{
	char *command;

	if (payload_len == 0) {
		ast_log(LOG_WARNING, "%s: WebSocket TEXT message has 0 length\n",
			ast_channel_name(instance->channel));
		return 0;
	}

	if (payload_len > MAX_TEXT_MESSAGE_LEN) {
		ast_log(LOG_WARNING, "%s: WebSocket TEXT message of length %d exceeds maximum length of %d\n",
			ast_channel_name(instance->channel), (int)payload_len, MAX_TEXT_MESSAGE_LEN);
		return 0;
	}

	/*
	 * Unfortunately, payload is not NULL terminated even when it's
	 * a TEXT frame so we need to allocate a new buffer, copy
	 * the data into it, and NULL terminate it.
	 */
	command = ast_alloca(payload_len + 1);
	memcpy(command, payload, payload_len); /* Safe */
	command[payload_len] = '\0';
	command = ast_strip(command);

	ast_debug(4, "%s: Received: %s\n",
		ast_channel_name(instance->channel), command);

	return handle_command(instance, command);
}

static int process_binary_message(struct websocket_pvt *instance,
	char *payload, uint64_t payload_len)
{
	char *next_frame_ptr = NULL;
	size_t bytes_read = 0;
	int res = 0;
	size_t bytes_left = 0;

	{
		SCOPED_LOCK(frame_queue_lock, &instance->frame_queue, AST_LIST_LOCK,
			AST_LIST_UNLOCK);
		if (instance->frame_queue_length >= QUEUE_LENGTH_MAX) {
			ast_debug(4, "%s: WebSocket queue is full. Ignoring incoming binary message.\n",
				ast_channel_name(instance->channel));
			return 0;
		}
	}

	next_frame_ptr = payload;
	instance->bytes_read += payload_len;

	if (instance->passthrough) {
		res = queue_frame_from_buffer(instance, payload, payload_len);
		return res;
	}

	if (instance->bulk_media_in_progress && instance->leftover_len > 0) {
		/*
		 * We have leftover data from a previous websocket message.
		 * Try to make a complete frame by appending data from
		 * the current message to the leftover data.
		 */
		char *append_ptr = instance->leftover_data + instance->leftover_len;
		size_t bytes_needed_for_frame = instance->optimal_frame_size - instance->leftover_len;
		/*
		 * It's possible that even the current message doesn't have enough
		 * data to make a complete frame.
		 */
		size_t bytes_avail_to_copy = MIN(bytes_needed_for_frame, payload_len);

		/*
		 * Append whatever we can to the end of the leftover data
		 * even if it's not enough to make a complete frame.
		 */
		memcpy(append_ptr, payload, bytes_avail_to_copy);

		/*
		 * If leftover data is still short, just return and wait for the
		 * next websocket message.
		 */
		if (bytes_avail_to_copy < bytes_needed_for_frame) {
			ast_debug(4, "%s: Leftover data %d bytes but only %d new bytes available of %d needed. Appending and waiting for next message.\n",
				ast_channel_name(instance->channel), (int)instance->leftover_len, (int)bytes_avail_to_copy, (int)bytes_needed_for_frame);
			instance->leftover_len += bytes_avail_to_copy;
			return 0;
		}

		res = queue_frame_from_buffer(instance, instance->leftover_data, instance->optimal_frame_size);
		if (res < 0) {
			return -1;
		}

		/*
		 * We stole data from the current payload so decrement payload_len
		 * and set the next frame pointer after the data in payload
		 * we just copied.
		 */
		payload_len -= bytes_avail_to_copy;
		next_frame_ptr = payload + bytes_avail_to_copy;

		ast_debug(5, "%s: --- BR: %4d  FQ: %4d  PL: %4d  LOL: %3d  P: %p  NFP: %p  OFF: %4d  NPL: %4d  BAC: %3d\n",
			ast_channel_name(instance->channel),
			instance->frame_queue_length,
			(int)instance->bytes_read,
			(int)(payload_len + bytes_avail_to_copy),
			(int)instance->leftover_len,
			payload,
			next_frame_ptr,
			(int)(next_frame_ptr - payload),
			(int)payload_len,
			(int)bytes_avail_to_copy
			);


		instance->leftover_len = 0;
	}

	if (!instance->bulk_media_in_progress && instance->leftover_len > 0) {
		instance->leftover_len = 0;
	}

	bytes_left = payload_len;
	while (bytes_read < payload_len && bytes_left >= instance->optimal_frame_size) {
		res = queue_frame_from_buffer(instance, next_frame_ptr,
			instance->optimal_frame_size);
		if (res < 0) {
			break;
		}
		bytes_read += instance->optimal_frame_size;
		next_frame_ptr += instance->optimal_frame_size;
		bytes_left -= instance->optimal_frame_size;
	}

	if (instance->bulk_media_in_progress && bytes_left > 0) {
		/*
		 * We have a partial frame.  Save the leftover data.
		 */
		ast_debug(5, "%s: +++ BR: %4d  FQ: %4d  PL: %4d  LOL: %3d  P: %p  NFP: %p  OFF: %4d  BL: %4d\n",
			ast_channel_name(instance->channel),
			(int)instance->bytes_read,
			instance->frame_queue_length,
			(int)payload_len,
			(int)instance->leftover_len,
			payload,
			next_frame_ptr,
			(int)(next_frame_ptr - payload),
			(int)bytes_left
			);
		memcpy(instance->leftover_data, next_frame_ptr, bytes_left);
		instance->leftover_len = bytes_left;
	}

	return 0;
}

static int read_from_ws_and_queue(struct websocket_pvt *instance)
{
	uint64_t payload_len = 0;
	char *payload = NULL;
	enum ast_websocket_opcode opcode;
	int fragmented = 0;
	int res = 0;

	if (!instance || !instance->websocket) {
		ast_log(LOG_WARNING, "%s: WebSocket instance not found\n",
			ast_channel_name(instance->channel));
		return -1;
	}

	ast_debug(9, "%s: Waiting for websocket to have data\n", ast_channel_name(instance->channel));
	res = ast_wait_for_input(
		ast_websocket_fd(instance->websocket), -1);
	if (res <= 0) {
		ast_log(LOG_WARNING, "%s: WebSocket read failed: %s\n",
			ast_channel_name(instance->channel), strerror(errno));
		return -1;
	}

	/*
	 * We need to lock here to prevent the websocket handle from
	 * being pulled out from under us if the core sends us a
	 * hangup request.
	 */
	ao2_lock(instance);
	if (!instance->websocket) {
		ao2_unlock(instance);
		return -1;
	}

	res = ast_websocket_read(instance->websocket, &payload, &payload_len,
		&opcode, &fragmented);
	ao2_unlock(instance);
	if (res) {
		return -1;
	}
	ast_debug(5, "%s: WebSocket read %d bytes\n", ast_channel_name(instance->channel),
		(int)payload_len);

	if (opcode == AST_WEBSOCKET_OPCODE_TEXT) {
		return process_text_message(instance, payload, payload_len);
	}

	if (opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
		ast_debug(5, "%s: WebSocket closed by remote\n",
			ast_channel_name(instance->channel));
		return -1;
	}

	if (opcode != AST_WEBSOCKET_OPCODE_BINARY) {
		ast_debug(5, "%s: WebSocket frame type %d not supported. Ignoring.\n",
			ast_channel_name(instance->channel), (int)opcode);
		return 0;
	}

	return process_binary_message(instance, payload, payload_len);
}

/*!
 * \internal
 *
 * For incoming websocket connections, this function gets called by
 * incoming_ws_established_cb() and is run in the http server thread
 * handling the websocket connection.
 *
 * For outgoing websocket connections, this function gets started as
 * a background thread by webchan_call().
 */
static void *read_thread_handler(void *obj)
{
	RAII_VAR(struct websocket_pvt *, instance, obj, ao2_cleanup);
	int res = 0;

	ast_debug(3, "%s: Read thread started\n", ast_channel_name(instance->channel));

	res = send_event(instance, MEDIA_START);
	if (res != 0 ) {
		ast_queue_control(instance->channel, AST_CONTROL_HANGUP);
		return NULL;
	}

	if (!instance->no_auto_answer) {
		ast_debug(3, "%s: ANSWER by auto_answer\n", ast_channel_name(instance->channel));
		ast_queue_control(instance->channel, AST_CONTROL_ANSWER);
	}

	while (read_from_ws_and_queue(instance) == 0)
	{
	}

	/*
	 * websocket_hangup will take care of closing the websocket if needed.
	 */
	ast_debug(3, "%s: HANGUP by websocket close/error\n", ast_channel_name(instance->channel));
	ast_queue_control(instance->channel, AST_CONTROL_HANGUP);

	return NULL;
}

/*! \brief Function called when we should write a frame to the channel */
static int webchan_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct websocket_pvt *instance = ast_channel_tech_pvt(ast);

	if (!instance || !instance->websocket) {
		ast_log(LOG_WARNING, "%s: WebSocket instance or client not found\n",
			ast_channel_name(ast));
		return -1;
	}

	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "%s: This WebSocket channel only supports AST_FRAME_VOICE frames\n",
			ast_channel_name(ast));
		return -1;
	}

	if (ast_format_cmp(f->subclass.format, instance->native_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_log(LOG_WARNING, "%s: This WebSocket channel only supports the '%s' format, not '%s'\n",
			ast_channel_name(ast), ast_format_get_name(instance->native_format),
			ast_format_get_name(f->subclass.format));
		return -1;
	}

	return ast_websocket_write(instance->websocket, AST_WEBSOCKET_OPCODE_BINARY,
		(char *)f->data.ptr, (uint64_t)f->datalen);
}

/*!
 * \internal
 *
 * Called by the core to actually call the remote.
 */
static int webchan_call(struct ast_channel *ast, const char *dest,
	int timeout)
{
	struct websocket_pvt *instance = ast_channel_tech_pvt(ast);
	int nodelay = 1;
	enum ast_websocket_result result;

	if (!instance) {
		ast_log(LOG_WARNING, "%s: WebSocket instance not found\n",
			ast_channel_name(ast));
		return -1;
	}

	if (instance->type == AST_WS_TYPE_SERVER) {
		ast_debug(3, "%s: Websocket call incoming\n", ast_channel_name(instance->channel));
		return 0;
	}
	ast_debug(3, "%s: Websocket call outgoing\n", ast_channel_name(instance->channel));

	if (!instance->client) {
		ast_log(LOG_WARNING, "%s: WebSocket client not found\n",
			ast_channel_name(ast));
		return -1;
	}

	ast_debug(3, "%s: WebSocket call requested to %s. cid: %s\n",
		ast_channel_name(ast), dest, instance->connection_id);

	if (!ast_strlen_zero(instance->uri_params)) {
		ast_websocket_client_add_uri_params(instance->client, instance->uri_params);
	}

	instance->websocket = ast_websocket_client_connect(instance->client,
		instance, ast_channel_name(ast), &result);
	if (!instance->websocket || result != WS_OK) {
		ast_log(LOG_WARNING, "%s: WebSocket connection failed to %s: %s\n",
			ast_channel_name(ast), dest, ast_websocket_result_to_str(result));
		return -1;
	}

	if (setsockopt(ast_websocket_fd(instance->websocket),
		IPPROTO_TCP, TCP_NODELAY, (char *) &nodelay, sizeof(nodelay)) < 0) {
		ast_log(LOG_WARNING, "Failed to set TCP_NODELAY on websocket connection: %s\n", strerror(errno));
	}

	ast_debug(3, "%s: WebSocket connection to %s established\n",
		ast_channel_name(ast), dest);

	/* read_thread_handler() will clean up the bump */
	if (ast_pthread_create_detached_background(&instance->outbound_read_thread, NULL,
			read_thread_handler, ao2_bump(instance))) {
		ast_log(LOG_WARNING, "%s: Failed to create thread.\n", ast_channel_name(ast));
		ao2_cleanup(instance);
		return -1;
	}

	return 0;
}

static void websocket_destructor(void *data)
{
	struct websocket_pvt *instance = data;
	struct ast_frame *frame = NULL;
	ast_debug(3, "%s: WebSocket instance freed\n", instance->connection_id);

	AST_LIST_LOCK(&instance->frame_queue);
	while ((frame = AST_LIST_REMOVE_HEAD(&instance->frame_queue, frame_list))) {
		ast_frfree(frame);
	}
	AST_LIST_UNLOCK(&instance->frame_queue);

	if (instance->timer) {
		ast_timer_close(instance->timer);
		instance->timer = NULL;
	}

	if (instance->channel) {
		ast_channel_unref(instance->channel);
		instance->channel = NULL;
	}
	if (instance->websocket) {
		ast_websocket_unref(instance->websocket);
		instance->websocket = NULL;
	}

	ao2_cleanup(instance->client);
	instance->client = NULL;

	ao2_cleanup(instance->native_codec);
	instance->native_codec = NULL;

	ao2_cleanup(instance->native_format);
	instance->native_format = NULL;

	ao2_cleanup(instance->slin_codec);
	instance->slin_codec = NULL;

	ao2_cleanup(instance->slin_format);
	instance->slin_format = NULL;

	if (instance->silence.data.ptr) {
		ast_free(instance->silence.data.ptr);
		instance->silence.data.ptr = NULL;
	}

	if (instance->translator) {
		ast_translator_free_path(instance->translator);
		instance->translator = NULL;
	}

	if (instance->leftover_data) {
		ast_free(instance->leftover_data);
		instance->leftover_data = NULL;
	}

	ast_free(instance->uri_params);
}

struct instance_proxy {
	AO2_WEAKPROXY();
	/*! \brief The name of the module owning this sorcery instance */
	char connection_id[0];
};

static void instance_proxy_cb(void *weakproxy, void *data)
{
	struct instance_proxy *proxy = weakproxy;
	ast_debug(3, "%s: WebSocket instance removed from instances\n", proxy->connection_id);
	ao2_unlink(instances, weakproxy);
}

static struct websocket_pvt* websocket_new(const char *chan_name,
	const char *connection_id, struct ast_format *fmt)
{
	RAII_VAR(struct instance_proxy *, proxy, NULL, ao2_cleanup);
	RAII_VAR(struct websocket_pvt *, instance, NULL, ao2_cleanup);
	char uuid[AST_UUID_STR_LEN];
	enum ast_websocket_type ws_type;

	SCOPED_AO2WRLOCK(locker, instances);

	if (ast_strings_equal(connection_id, INCOMING_CONNECTION_ID)) {
		connection_id = ast_uuid_generate_str(uuid, sizeof(uuid));
		ws_type = AST_WS_TYPE_SERVER;
	} else {
		ws_type = AST_WS_TYPE_CLIENT;
	}

	proxy = ao2_weakproxy_alloc(sizeof(*proxy) + strlen(connection_id) + 1, NULL);
	if (!proxy) {
		return NULL;
	}
	strcpy(proxy->connection_id, connection_id); /* Safe */

	instance = ao2_alloc(sizeof(*instance) + strlen(connection_id) + 1,
		websocket_destructor);
	if (!instance) {
		return NULL;
	}
	strcpy(instance->connection_id, connection_id); /* Safe */

	instance->type = ws_type;
	if (ws_type == AST_WS_TYPE_CLIENT) {
		instance->client = ast_websocket_client_retrieve_by_id(instance->connection_id);
		if (!instance->client) {
			ast_log(LOG_ERROR, "%s: WebSocket client connection '%s' not found\n",
				chan_name, instance->connection_id);
			return NULL;
		}
	}

	AST_LIST_HEAD_INIT(&instance->frame_queue);

	/*
	 * We need the codec to calculate the number of samples in a frame
	 * so we'll get it once and store it in the instance.
	 *
	 * References for native_format and native_codec are now held by the
	 * instance and will be released when the instance is destroyed.
	 */
	instance->native_format = fmt;
	instance->native_codec = ast_format_get_codec(instance->native_format);
	/*
	 * References for native_format and native_codec are now held by the
	 * instance and will be released when the instance is destroyed.
	 */

	/*
	 * It's not possible for us to re-time or re-frame media if the data
	 * stream can't be broken up on arbitrary byte boundaries.  This is usually
	 * indicated by the codec's minimum_bytes being small (10 bytes or less).
	 * We need to force passthrough mode in this case.
	 */
	if (instance->native_codec->minimum_bytes <= 10) {
		instance->passthrough = 1;
		instance->optimal_frame_size = 0;
	} else {
		instance->optimal_frame_size =
			(instance->native_codec->default_ms * instance->native_codec->minimum_bytes)
				/ instance->native_codec->minimum_ms;
		instance->leftover_data = ast_calloc(1, instance->optimal_frame_size);
		if (!instance->leftover_data) {
			return NULL;
		}
	}

	ast_debug(3,
		"%s: WebSocket channel native format '%s' Sample rate: %d ptime: %dms minms: %u  minbytes: %u passthrough: %d optimal_frame_size: %d\n",
		chan_name, ast_format_get_name(instance->native_format),
		ast_format_get_sample_rate(instance->native_format),
		ast_format_get_default_ms(instance->native_format),
		ast_format_get_minimum_ms(instance->native_format),
		ast_format_get_minimum_bytes(instance->native_format),
		instance->passthrough,
		instance->optimal_frame_size);

	/* We have exclusive access to proxy and sorcery, no need for locking here. */
	if (ao2_weakproxy_set_object(proxy, instance, OBJ_NOLOCK)) {
		return NULL;
	}

	if (ao2_weakproxy_subscribe(proxy, instance_proxy_cb, NULL, OBJ_NOLOCK)) {
		return NULL;
	}

	if (!ao2_link_flags(instances, proxy, OBJ_NOLOCK)) {
		ast_log(LOG_ERROR, "%s: Unable to link WebSocket instance to instances\n",
			proxy->connection_id);
		return NULL;
	}
	ast_debug(3, "%s: WebSocket instance created and linked\n", proxy->connection_id);

	return ao2_bump(instance);
}

static int set_instance_translator(struct websocket_pvt *instance)
{
	if (ast_format_cache_is_slinear(instance->native_format)) {
		instance->slin_format = ao2_bump(instance->native_format);
		instance->slin_codec = ast_format_get_codec(instance->slin_format);
		return 0;
	}

	instance->slin_format = ao2_bump(ast_format_cache_get_slin_by_rate(instance->native_codec->sample_rate));
	if (!instance->slin_format) {
		ast_log(LOG_ERROR, "%s: Unable to get slin format for rate %d\n",
			ast_channel_name(instance->channel), instance->native_codec->sample_rate);
		return -1;
	}
	ast_debug(3, "%s: WebSocket channel slin format '%s' Sample rate: %d ptime: %dms\n",
		ast_channel_name(instance->channel), ast_format_get_name(instance->slin_format),
		ast_format_get_sample_rate(instance->slin_format),
		ast_format_get_default_ms(instance->slin_format));

	instance->translator = ast_translator_build_path(instance->slin_format, instance->native_format);
	if (!instance->translator) {
		ast_log(LOG_ERROR, "%s: Unable to build translator path from '%s' to '%s'\n",
			ast_channel_name(instance->channel), ast_format_get_name(instance->native_format),
			ast_format_get_name(instance->slin_format));
		return -1;
	}

	instance->slin_codec = ast_format_get_codec(instance->slin_format);
	return 0;
}

static int set_instance_silence_frame(struct websocket_pvt *instance)
{
	instance->silence.frametype = AST_FRAME_VOICE;
	instance->silence.datalen =
		(instance->slin_codec->default_ms * instance->slin_codec->minimum_bytes) / instance->slin_codec->minimum_ms;
	instance->silence.samples = instance->silence.datalen / sizeof(uint16_t);
	/*
	 * Even though we'll calloc the data pointer, we don't mark it as
	 * mallocd because this frame will be around for a while and we don't
	 * want it accidentally freed before we're done with it.
	 */
	instance->silence.mallocd = 0;
	instance->silence.offset = 0;
	instance->silence.src = __PRETTY_FUNCTION__;
	instance->silence.subclass.format = instance->slin_format;
	instance->silence.data.ptr = ast_calloc(1, instance->silence.datalen);
	if (!instance->silence.data.ptr) {
		return -1;
	}

	return 0;
}

static int set_channel_timer(struct websocket_pvt *instance)
{
	int rate = 0;
	instance->timer = ast_timer_open();
	if (!instance->timer) {
		return -1;
	}
	/* Rate is the number of ticks per second, not the interval. */
	rate = 1000 / ast_format_get_default_ms(instance->native_format);
	ast_debug(3, "%s: WebSocket timer rate %d\n",
		ast_channel_name(instance->channel), rate);
	ast_timer_set_rate(instance->timer, rate);
	/*
	 * Calling ast_channel_set_fd will cause the channel thread to call
	 * webchan_read at 'rate' times per second.
	 */
	ast_channel_set_fd(instance->channel, 0, ast_timer_fd(instance->timer));

	return 0;
}

static int set_channel_variables(struct websocket_pvt *instance)
{
	char *pkt_size = NULL;
	int res = ast_asprintf(&pkt_size, "%d", instance->optimal_frame_size);
	if (res <= 0) {
		return -1;
	}

	pbx_builtin_setvar_helper(instance->channel, MEDIA_WEBSOCKET_OPTIMAL_FRAME_SIZE,
		pkt_size);
	ast_free(pkt_size);
	pbx_builtin_setvar_helper(instance->channel, MEDIA_WEBSOCKET_CONNECTION_ID,
		instance->connection_id);

	return 0;
}

static int validate_uri_parameters(const char *uri_params)
{
	char *params = ast_strdupa(uri_params);
	char *nvp = NULL;
	char *nv = NULL;

	/*
	 * uri_params should be a comma-separated list of key=value pairs.
	 * For example:
	 * name1=value1,name2=value2
	 * We're verifying that each name and value either doesn't need
	 * to be encoded or that it already is.
	 */

	while((nvp = ast_strsep(&params, ',', 0))) {
		/* nvp will be name1=value1 */
		while((nv = ast_strsep(&nvp, '=', 0))) {
			/* nv will be either name1 or value1 */
			if (!ast_uri_verify_encoded(nv)) {
				return 0;
			}
		}
	}

	return 1;
}

enum {
	OPT_WS_CODEC =  (1 << 0),
	OPT_WS_NO_AUTO_ANSWER =  (1 << 1),
	OPT_WS_URI_PARAM =  (1 << 2),
	OPT_WS_PASSTHROUGH =  (1 << 3),
	OPT_WS_MSG_FORMAT =  (1 << 4),
};

enum {
	OPT_ARG_WS_CODEC,
	OPT_ARG_WS_NO_AUTO_ANSWER,
	OPT_ARG_WS_URI_PARAM,
	OPT_ARG_WS_PASSTHROUGH,
	OPT_ARG_WS_MSG_FORMAT,
	OPT_ARG_ARRAY_SIZE
};

AST_APP_OPTIONS(websocket_options, BEGIN_OPTIONS
	AST_APP_OPTION_ARG('c', OPT_WS_CODEC, OPT_ARG_WS_CODEC),
	AST_APP_OPTION('n', OPT_WS_NO_AUTO_ANSWER),
	AST_APP_OPTION_ARG('v', OPT_WS_URI_PARAM, OPT_ARG_WS_URI_PARAM),
	AST_APP_OPTION('p', OPT_WS_PASSTHROUGH),
	AST_APP_OPTION_ARG('f', OPT_WS_MSG_FORMAT, OPT_ARG_WS_MSG_FORMAT),
	END_OPTIONS );

static struct ast_channel *webchan_request(const char *type,
	struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const char *data, int *cause)
{
	char *parse;
	RAII_VAR(struct websocket_pvt *, instance, NULL, ao2_cleanup);
	struct ast_channel *chan = NULL;
	struct ast_format *fmt = NULL;
	struct ast_format_cap *caps = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(connection_id);
		AST_APP_ARG(options);
	);
	struct ast_flags opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];
	const char *requestor_name = requestor ? ast_channel_name(requestor) : "no channel";
	RAII_VAR(struct webchan_conf_global *, global_cfg, NULL, ao2_cleanup);

	global_cfg = ast_sorcery_retrieve_by_id(sorcery, "global", "global");

	ast_debug(3, "%s: WebSocket channel requested\n",
		requestor_name);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "%s: A connection id is required for the 'WebSocket' channel\n",
			requestor_name);
		goto failure;
	}
	parse = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(args, parse, '/');

	if (ast_strlen_zero(args.connection_id)) {
		ast_log(LOG_ERROR, "%s: connection_id is required for the 'WebSocket' channel\n",
			requestor_name);
		goto failure;
	}

	if (!ast_strlen_zero(args.options)
		&& ast_app_parse_options(websocket_options, &opts, opt_args,
			ast_strdupa(args.options))) {
		ast_log(LOG_ERROR, "%s: 'WebSocket' channel options '%s' parse error\n",
			requestor_name, args.options);
		goto failure;
	}

	if (ast_test_flag(&opts, OPT_WS_CODEC)
		&& !ast_strlen_zero(opt_args[OPT_ARG_WS_CODEC])) {
		ast_debug(3, "%s: Using specified format %s\n",
			requestor_name, opt_args[OPT_ARG_WS_CODEC]);
		fmt = ast_format_cache_get(opt_args[OPT_ARG_WS_CODEC]);
	} else {
		/*
		 * If codec wasn't specified in the dial string,
		 * use the first format in the capabilities.
		 */
		ast_debug(3, "%s: Using format %s from requesting channel\n",
			requestor_name, opt_args[OPT_ARG_WS_CODEC]);
		fmt = ast_format_cap_get_format(cap, 0);
	}

	if (!fmt) {
		ast_log(LOG_WARNING, "%s: No codec found for sending media to connection '%s'\n",
			requestor_name, args.connection_id);
		goto failure;
	}

	instance = websocket_new(requestor_name, args.connection_id, fmt);
	if (!instance) {
		ast_log(LOG_ERROR, "%s: Failed to allocate WebSocket channel pvt\n",
			requestor_name);
		goto failure;
	}

	instance->no_auto_answer = ast_test_flag(&opts, OPT_WS_NO_AUTO_ANSWER);
	if (!instance->passthrough) {
		instance->passthrough = ast_test_flag(&opts, OPT_WS_PASSTHROUGH);
	}

	if (ast_test_flag(&opts, OPT_WS_URI_PARAM)
		&& !ast_strlen_zero(opt_args[OPT_ARG_WS_URI_PARAM])) {
		char *comma;

		if (ast_strings_equal(args.connection_id, INCOMING_CONNECTION_ID)) {
			ast_log(LOG_ERROR,
				"%s: URI parameters are not allowed for 'WebSocket/INCOMING' channels\n",
				requestor_name);
			goto failure;
		}

		ast_debug(3, "%s: Using URI parameters '%s'\n",
			requestor_name, opt_args[OPT_ARG_WS_URI_PARAM]);

		if (!validate_uri_parameters(opt_args[OPT_ARG_WS_URI_PARAM])) {
			ast_log(LOG_ERROR, "%s: Invalid URI parameters '%s' in WebSocket/%s dial string\n",
				requestor_name, opt_args[OPT_ARG_WS_URI_PARAM],
				args.connection_id);
			goto failure;
		}

		instance->uri_params = ast_strdup(opt_args[OPT_ARG_WS_URI_PARAM]);
		comma = instance->uri_params;
		/*
		 * The normal separator for query string components is an
		 * ampersand ('&') but the Dial app interprets them as additional
		 * channels to dial in parallel so we instruct users to separate
		 * the parameters with commas (',') instead.  We now have to
		 * convert those commas back to ampersands.
		 */
		while ((comma = strchr(comma,','))) {
			*comma = '&';
		}
		ast_debug(3, "%s: Using final URI '%s'\n", requestor_name, instance->uri_params);
	}

	if (ast_test_flag(&opts, OPT_WS_MSG_FORMAT)) {
		instance->control_msg_format = control_msg_format_from_str(opt_args[OPT_ARG_WS_MSG_FORMAT]);

		if (instance->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_INVALID) {
			ast_log(LOG_WARNING, "%s: 'f/control message format' dialstring parameter value missing or invalid. "
				"Defaulting to 'plain-text'\n",
				ast_channel_name(requestor));
			instance->control_msg_format = WEBCHAN_CONTROL_MSG_FORMAT_PLAIN;
		}
	} else if (global_cfg) {
		instance->control_msg_format = global_cfg->control_msg_format;
	}

	chan = ast_channel_alloc(1, AST_STATE_DOWN, "", "", "", "", "", assignedids,
		requestor, 0, "WebSocket/%s/%p", args.connection_id, instance);
	if (!chan) {
		ast_log(LOG_ERROR, "%s: Unable to alloc channel\n", ast_channel_name(requestor));
		goto failure;
	}

	ast_debug(3, "%s: WebSocket channel %s allocated for connection %s\n",
		ast_channel_name(chan), requestor_name,
		instance->connection_id);

	instance->channel = ao2_bump(chan);
	ast_channel_tech_set(instance->channel, &websocket_tech);

	if (set_instance_translator(instance) != 0) {
		goto failure;
	}

	if (set_instance_silence_frame(instance) != 0) {
		goto failure;
	}

	if (set_channel_timer(instance) != 0) {
		goto failure;
	}

	if (set_channel_variables(instance) != 0) {
		goto failure;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_log(LOG_ERROR, "%s: Unable to alloc caps\n", requestor_name);
		goto failure;
	}

	ast_format_cap_append(caps, instance->native_format, 0);
	ast_channel_nativeformats_set(instance->channel, caps);
	ast_channel_set_writeformat(instance->channel, instance->native_format);
	ast_channel_set_rawwriteformat(instance->channel, instance->native_format);
	ast_channel_set_readformat(instance->channel, instance->native_format);
	ast_channel_set_rawreadformat(instance->channel, instance->native_format);
	ast_channel_tech_pvt_set(chan, ao2_bump(instance));
	ast_channel_unlock(chan);
	ao2_cleanup(caps);

	ast_debug(3, "%s: WebSocket channel created to %s\n",
		ast_channel_name(chan), args.connection_id);

	return chan;

failure:
	if (chan) {
		ast_channel_unlock(chan);
	}
	*cause = AST_CAUSE_FAILURE;
	return NULL;
}

/*!
 * \internal
 *
 * Called by the core to hang up the channel.
 */
static int webchan_hangup(struct ast_channel *ast)
{
	struct websocket_pvt *instance = ast_channel_tech_pvt(ast);

	if (!instance) {
		return -1;
	}
	ast_debug(3, "%s: WebSocket call hangup. cid: %s\n",
		ast_channel_name(ast), instance->connection_id);

	/*
	 * We need to lock because read_from_ws_and_queue() is probably waiting
	 * on the websocket file descriptor and will unblock and immediately try to
	 * check the websocket and read from it. We don't want to pull the
	 * websocket out from under it between the check and read.
	 */
	ao2_lock(instance);
	if (instance->websocket) {
		ast_websocket_close(instance->websocket, 1000);
		ast_websocket_unref(instance->websocket);
		instance->websocket = NULL;
	}
	ast_channel_tech_pvt_set(ast, NULL);
	ao2_unlock(instance);

	/* Clean up the reference from adding the instance to the channel */
	ao2_cleanup(instance);

	return 0;
}

static int webchan_send_dtmf_text(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct websocket_pvt *instance = ast_channel_tech_pvt(ast);

	if (!instance) {
		return -1;
	}

	return send_event(instance, DTMF_END, digit);
}

/*!
 * \internal
 *
 * Called by res_http_websocket after a client has connected and
 * successfully upgraded from HTTP to WebSocket.
 *
 * Depends on incoming_ws_http_callback parsing the connection_id from
 * the HTTP request and storing it in get_params.
 */
static void incoming_ws_established_cb(struct ast_websocket *ast_ws_session,
	struct ast_variable *get_params, struct ast_variable *upgrade_headers)
{
	RAII_VAR(struct ast_websocket *, s, ast_ws_session, ast_websocket_unref);
	struct ast_variable *v;
	const char *connection_id = NULL;
	struct websocket_pvt *instance = NULL;
	int nodelay = 1;

	ast_debug(3, "WebSocket established\n");

	for (v = upgrade_headers; v; v = v->next) {
		ast_debug(4, "Header-> %s: %s\n", v->name, v->value);
	}
	for (v = get_params; v; v = v->next) {
		ast_debug(4, " Param-> %s: %s\n", v->name, v->value);
	}

	connection_id = ast_variable_find_in_list(get_params, "CONNECTION_ID");
	if (!connection_id) {
		/*
		 * This can't really happen because websocket_http_callback won't
		 * let it get this far if it can't add the connection_id to the
		 * get_params.
		 * Just in case though...
		 */
		ast_log(LOG_WARNING, "WebSocket connection id not found\n");
		ast_queue_control(instance->channel, AST_CONTROL_HANGUP);
		ast_websocket_close(ast_ws_session, 1000);
		return;
	}

	instance = ao2_weakproxy_find(instances, connection_id, OBJ_SEARCH_KEY | OBJ_NOLOCK, "");
	if (!instance) {
		/*
		 * This also can't really happen because websocket_http_callback won't
		 * let it get this far if it can't find the instance.
		 * Just in case though...
		 */
		ast_log(LOG_WARNING, "%s: WebSocket instance not found\n", connection_id);
		ast_queue_control(instance->channel, AST_CONTROL_HANGUP);
		ast_websocket_close(ast_ws_session, 1000);
		return;
	}
	instance->websocket = ao2_bump(ast_ws_session);

	if (setsockopt(ast_websocket_fd(instance->websocket),
		IPPROTO_TCP, TCP_NODELAY, (char *) &nodelay, sizeof(nodelay)) < 0) {
		ast_log(LOG_WARNING, "Failed to set TCP_NODELAY on manager connection: %s\n", strerror(errno));
	}

	/* read_thread_handler cleans up the bump */
	read_thread_handler(ao2_bump(instance));

	ao2_cleanup(instance);
	ast_debug(3, "WebSocket closed\n");
}

/*!
 * \internal
 *
 * Called by the core http server after a client connects but before
 * the upgrade from HTTP to Websocket.  We need to save the URI in
 * the CONNECTION_ID in a get_param because it contains the connection UUID
 * we gave to the client when they used externalMedia to create the channel.
 * incoming_ws_established_cb() will use this to retrieve the chan_websocket
 * instance.
 */
static int incoming_ws_http_callback(struct ast_tcptls_session_instance *ser,
	const struct ast_http_uri *urih, const char *uri,
	enum ast_http_method method, struct ast_variable *get_params,
	struct ast_variable *headers)
{
	struct ast_http_uri fake_urih = {
		.data = ast_ws_server,
	};
	int res = 0;
	/*
	 * Normally the http server will destroy the get_params
	 * when the session ends but if there weren't any initially
	 * and we create some and add them to the list, the http server
	 * won't know about it so we have to destroy it ourselves.
	 */
	int destroy_get_params = (get_params == NULL);
	struct ast_variable *v = NULL;
	RAII_VAR(struct websocket_pvt *, instance, NULL, ao2_cleanup);

	ast_debug(2, "URI: %s Starting\n", uri);

	/*
	 * The client will have issued the GET request with a URI of
	 * /media/<connection_id>
	 *
	 * Since this callback is registered for the /media URI prefix the
	 * http server will strip that off the front of the URI passing in
	 * only the path components after that in the 'uri' parameter.
	 * This should leave only the connection id without a leading '/'.
	 */
	instance = ao2_weakproxy_find(instances, uri, OBJ_SEARCH_KEY | OBJ_NOLOCK, "");
	if (!instance) {
		ast_log(LOG_WARNING, "%s: WebSocket instance not found\n", uri);
		ast_http_error(ser, 404, "Not found", "WebSocket instance not found");
		return -1;
	}

	/*
	 * We don't allow additional connections using the same connection id.
	 */
	if (instance->websocket) {
		ast_log(LOG_WARNING, "%s: Websocket already connected for channel '%s'\n",
			uri, instance->channel ? ast_channel_name(instance->channel) : "unknown");
		ast_http_error(ser, 409, "Conflict", "Another websocket connection exists for this connection id");
		return -1;
	}

	v = ast_variable_new("CONNECTION_ID", uri, "");
	if (!v) {
		ast_http_error(ser, 500, "Server error", "");
		return -1;
	}
	ast_variable_list_append(&get_params, v);

	for (v = get_params; v; v = v->next) {
		ast_debug(4, " Param-> %s: %s\n", v->name, v->value);
	}

	/*
	 * This will ultimately call internal_ws_established_cb() so
	 * this function will block until the websocket is closed and
	 * internal_ws_established_cb() returns;
	 */
	res = ast_websocket_uri_cb(ser, &fake_urih, uri, method,
		get_params, headers);
	if (destroy_get_params) {
		ast_variables_destroy(get_params);
	}

	ast_debug(2, "URI: %s DONE\n", uri);

	return res;
}

static struct ast_http_uri http_uri = {
	.callback = incoming_ws_http_callback,
	.description = "Media over Websocket",
	.uri = "media",
	.has_subtree = 1,
	.data = NULL,
	.key = __FILE__,
	.no_decode_uri = 1,
};

AO2_STRING_FIELD_HASH_FN(instance_proxy, connection_id)
AO2_STRING_FIELD_CMP_FN(instance_proxy, connection_id)
AO2_STRING_FIELD_SORT_FN(instance_proxy, connection_id)

static int global_control_message_format_from_str(const struct aco_option *opt,
	struct ast_variable *var, void *obj)
{
	struct webchan_conf_global *cfg = obj;

	cfg->control_msg_format = control_msg_format_from_str(var->value);

	if (cfg->control_msg_format == WEBCHAN_CONTROL_MSG_FORMAT_INVALID) {
		ast_log(LOG_ERROR, "chan_websocket.conf: Invalid value '%s' for "
			"control_mesage_format. Must be 'plain-text' or 'json'\n",
			var->value);
		return -1;
	}

	return 0;
}

static int global_control_message_format_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct webchan_conf_global *cfg = obj;

	*buf = ast_strdup(control_msg_format_to_str(cfg->control_msg_format));

	return 0;
}

static void *global_alloc(const char *name)
{
	struct webchan_conf_global *cfg = ast_sorcery_generic_alloc(
		sizeof(*cfg), NULL);

	if (!cfg) {
		return NULL;
	}

	return cfg;
}

static int global_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct webchan_conf_global *cfg = obj;

	ast_debug(1, "control_msg_format: %s\n",
		control_msg_format_to_str(cfg->control_msg_format));

	return 0;
}

static int load_config(void)
{
	ast_debug(2, "Initializing Websocket Client Configuration\n");
	sorcery = ast_sorcery_open();
	if (!sorcery) {
		ast_log(LOG_ERROR, "Failed to open sorcery\n");
		return -1;
	}

	ast_sorcery_apply_default(sorcery, "global", "config",
		"chan_websocket.conf,criteria=type=global,single_object=yes,explicit_name=global");

	if (ast_sorcery_object_register(sorcery, "global", global_alloc, NULL, global_apply)) {
		ast_log(LOG_ERROR, "Failed to register chan_websocket global object with sorcery\n");
		ast_sorcery_unref(sorcery);
		sorcery = NULL;
		return -1;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, "global", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_register_cust(global, control_message_format, "plain-text");

	ast_sorcery_load(sorcery);

	return 0;
}

/*! \brief Function called when our module is unloaded */
static int unload_module(void)
{
	ast_http_uri_unlink(&http_uri);
	ao2_cleanup(ast_ws_server);
	ast_ws_server = NULL;

	ast_channel_unregister(&websocket_tech);
	ao2_cleanup(websocket_tech.capabilities);
	websocket_tech.capabilities = NULL;

	ao2_cleanup(instances);
	instances = NULL;

	ast_sorcery_unref(sorcery);
	sorcery = NULL;

	return 0;
}

static int reload_module(void)
{
	ast_debug(2, "Reloading chan_websocket configuration\n");
	ast_sorcery_reload(sorcery);

	return 0;
}

/*! \brief Function called when our module is loaded */
static int load_module(void)
{
	int res = 0;
	struct ast_websocket_protocol *protocol;

	res = load_config();
	if (res != 0) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (!(websocket_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_format_cap_append_by_type(websocket_tech.capabilities, AST_MEDIA_TYPE_UNKNOWN);
	if (ast_channel_register(&websocket_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'WebSocket'\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	instances = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, 17, instance_proxy_hash_fn,
		instance_proxy_sort_fn, instance_proxy_cmp_fn);
	if (!instances) {
		ast_log(LOG_WARNING,
			"Failed to allocate the chan_websocket instance registry\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_ws_server = ast_websocket_server_create();
	if (!ast_ws_server) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	protocol = ast_websocket_sub_protocol_alloc("media");
	if (!protocol) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	protocol->session_established = incoming_ws_established_cb;
	res = ast_websocket_server_add_protocol2(ast_ws_server, protocol);

	ast_http_uri_link(&http_uri);

	return res == 0 ? AST_MODULE_LOAD_SUCCESS : AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Websocket Media Channel",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	.requires = "res_http_websocket,res_websocket_client",
);
