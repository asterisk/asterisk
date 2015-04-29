/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
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
 * \brief Function that raises events when talking is detected on a channel
 *
 * \author Matt Jordan <mjordan@digium.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/audiohook.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"

/*** DOCUMENTATION
	<function name="TALK_DETECT" language="en_US">
		<synopsis>
			Raises notifications when Asterisk detects silence or talking on a channel.
		</synopsis>
		<syntax>
			<parameter name="action" required="true">
				<optionlist>
					<option name="remove">
						<para>W/O. Remove talk detection from the channel.</para>
					</option>
					<option name="set">
						<para>W/O. Enable TALK_DETECT and/or configure talk detection
						parameters. Can be called multiple times to change parameters
						on a channel with talk detection already enabled.</para>
						<argument name="dsp_silence_threshold" required="false">
							<para>The time in milliseconds before which a user is considered silent.</para>
						</argument>
						<argument name="dsp_talking_threshold" required="false">
							<para>The time in milliseconds after which a user is considered talking.</para>
						</argument>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>The TALK_DETECT function enables events on the channel
			it is applied to. These events can be emited over AMI, ARI, and
			potentially other Asterisk modules that listen for the internal
			notification.</para>
			<para>The function has two parameters that can optionally be passed
			when <literal>set</literal> on a channel: <replaceable>dsp_talking_threshold</replaceable>
			and <replaceable>dsp_silence_threshold</replaceable>.</para>
			<para><replaceable>dsp_talking_threshold</replaceable> is the time in milliseconds of sound
			above what the dsp has established as base line silence for a user
			before a user is considered to be talking. By default, the value of
			<replaceable>silencethreshold</replaceable> from <filename>dsp.conf</filename>
			is used. If this value is set too tight events may be
			falsely triggered by variants in room noise.</para>
			<para>Valid values are 1 through 2^31.</para>
			<para><replaceable>dsp_silence_threshold</replaceable> is the time in milliseconds of sound
			falling within what the dsp has established as baseline silence before
			a user is considered be silent. If this value is set too low events
			indicating the user has stopped talking may get falsely sent out when
			the user briefly pauses during mid sentence.</para>
			<para>The best way to approach this option is to set it slightly above
			the maximum amount of ms of silence a user may generate during
			natural speech.</para>
			<para>By default this value is 2500ms. Valid values are 1
			through 2^31.</para>
			<para>Example:</para>
			<para>same => n,Set(TALK_DETECT(set)=)     ; Enable talk detection</para>
			<para>same => n,Set(TALK_DETECT(set)=1200) ; Update existing talk detection's silence threshold to 1200 ms</para>
			<para>same => n,Set(TALK_DETECT(remove)=)  ; Remove talk detection</para>
			<para>same => n,Set(TALK_DETECT(set)=,128) ; Enable and set talk threshold to 128</para>
			<para>This function will set the following variables:</para>
			<note>
				<para>The TALK_DETECT function uses an audiohook to inspect the
				voice media frames on a channel. Other functions, such as JITTERBUFFER,
				DENOISE, and AGC use a similar mechanism. Audiohooks are processed
				in the order in which they are placed on the channel. As such,
				it typically makes sense to place functions that modify the voice
				media data prior to placing the TALK_DETECT function, as this will
				yield better results.</para>
				<para>Example:</para>
				<para>same => n,Set(DENOISE(rx)=on)    ; Denoise received audio</para>
				<para>same => n,Set(TALK_DETECT(set)=) ; Perform talk detection on the denoised received audio</para>
			</note>
		</description>
	</function>
 ***/

#define DEFAULT_SILENCE_THRESHOLD 2500

/*! \brief Private data structure used with the function's datastore */
struct talk_detect_params {
	/*! The audiohook for the function */
	struct ast_audiohook audiohook;
	/*! Our threshold above which we consider someone talking */
	int dsp_talking_threshold;
	/*! How long we'll wait before we decide someone is silent */
	int dsp_silence_threshold;
	/*! Whether or not the user is currently talking */
	int talking;
	/*! The time the current burst of talking started */
	struct timeval talking_start;
	/*! The DSP used to do the heavy lifting */
	struct ast_dsp *dsp;
};

/*! \internal \brief Destroy the datastore */
static void datastore_destroy_cb(void *data) {
	struct talk_detect_params *td_params = data;

	ast_audiohook_destroy(&td_params->audiohook);

	if (td_params->dsp) {
		ast_dsp_free(td_params->dsp);
	}
	ast_free(data);
}

/*! \brief The channel datastore the function uses to store state */
static const struct ast_datastore_info talk_detect_datastore = {
	.type = "talk_detect",
	.destroy = datastore_destroy_cb
};

/*! \internal \brief An audiohook modification callback
 *
 * This processes the read side of a channel's voice data to see if
 * they are talking
 *
 * \note We don't actually modify the audio, so this function always
 * returns a 'failure' indicating that it didn't modify the data
 */
static int talk_detect_audiohook_cb(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *frame, enum ast_audiohook_direction direction)
{
	int total_silence;
	int update_talking = 0;
	struct ast_datastore *datastore;
	struct talk_detect_params *td_params;
	struct stasis_message *message;

	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE) {
		return 1;
	}

	if (direction != AST_AUDIOHOOK_DIRECTION_READ) {
		return 1;
	}

	if (frame->frametype != AST_FRAME_VOICE) {
		return 1;
	}

	if (!(datastore = ast_channel_datastore_find(chan, &talk_detect_datastore, NULL))) {
		return 1;
	}
	td_params = datastore->data;

	ast_dsp_silence(td_params->dsp, frame, &total_silence);

	if (total_silence < td_params->dsp_silence_threshold) {
		if (!td_params->talking) {
			update_talking = 1;
			td_params->talking_start = ast_tvnow();
		}
		td_params->talking = 1;
	} else {
		if (td_params->talking) {
			update_talking = 1;
		}
		td_params->talking = 0;
	}

	if (update_talking) {
		struct ast_json *blob = NULL;

		if (!td_params->talking) {
			int64_t diff_ms = ast_tvdiff_ms(ast_tvnow(), td_params->talking_start);
			diff_ms -= td_params->dsp_silence_threshold;

			blob = ast_json_pack("{s: i}", "duration", diff_ms);
			if (!blob) {
				return 1;
			}
		}

		ast_verb(4, "%s is now %s\n", ast_channel_name(chan),
		            td_params->talking ? "talking" : "silent");
		message = ast_channel_blob_create_from_cache(ast_channel_uniqueid(chan),
		                td_params->talking ? ast_channel_talking_start() : ast_channel_talking_stop(),
		                blob);
		if (message) {
			stasis_publish(ast_channel_topic(chan), message);
			ao2_ref(message, -1);
		}

		ast_json_unref(blob);
	}

	return 1;
}

/*! \internal \brief Disable talk detection on the channel */
static int remove_talk_detect(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct talk_detect_params *td_params;
	SCOPED_CHANNELLOCK(chan_lock, chan);

	datastore = ast_channel_datastore_find(chan, &talk_detect_datastore, NULL);
	if (!datastore) {
		ast_log(AST_LOG_WARNING, "Cannot remove TALK_DETECT from %s: TALK_DETECT not currently enabled\n",
		        ast_channel_name(chan));
		return -1;
	}
	td_params = datastore->data;

	if (ast_audiohook_remove(chan, &td_params->audiohook)) {
		ast_log(AST_LOG_WARNING, "Failed to remove TALK_DETECT audiohook from channel %s\n",
		        ast_channel_name(chan));
		return -1;
	}

	if (ast_channel_datastore_remove(chan, datastore)) {
		ast_log(AST_LOG_WARNING, "Failed to remove TALK_DETECT datastore from channel %s\n",
		        ast_channel_name(chan));
		return -1;
	}
	ast_datastore_free(datastore);

	return 0;
}

/*! \internal \brief Enable talk detection on the channel */
static int set_talk_detect(struct ast_channel *chan, int dsp_silence_threshold, int dsp_talking_threshold)
{
	struct ast_datastore *datastore = NULL;
	struct talk_detect_params *td_params;
	SCOPED_CHANNELLOCK(chan_lock, chan);

	datastore = ast_channel_datastore_find(chan, &talk_detect_datastore, NULL);
	if (!datastore) {
		datastore = ast_datastore_alloc(&talk_detect_datastore, NULL);
		if (!datastore) {
			return -1;
		}

		td_params = ast_calloc(1, sizeof(*td_params));
		if (!td_params) {
			ast_datastore_free(datastore);
			return -1;
		}

		ast_audiohook_init(&td_params->audiohook,
		                   AST_AUDIOHOOK_TYPE_MANIPULATE,
		                   "TALK_DETECT",
		                   AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
		td_params->audiohook.manipulate_callback = talk_detect_audiohook_cb;
		ast_set_flag(&td_params->audiohook, AST_AUDIOHOOK_TRIGGER_READ);

		td_params->dsp = ast_dsp_new_with_rate(ast_format_get_sample_rate(ast_channel_rawreadformat(chan)));
		if (!td_params->dsp) {
			ast_datastore_free(datastore);
			ast_free(td_params);
			return -1;
		}
		datastore->data = td_params;

		ast_channel_datastore_add(chan, datastore);
		ast_audiohook_attach(chan, &td_params->audiohook);
	} else {
		/* Talk detection already enabled; update existing settings */
		td_params = datastore->data;
	}

	td_params->dsp_talking_threshold = dsp_talking_threshold;
	td_params->dsp_silence_threshold = dsp_silence_threshold;

	ast_dsp_set_threshold(td_params->dsp, td_params->dsp_talking_threshold);

	return 0;
}

/*! \internal \brief TALK_DETECT write function callback */
static int talk_detect_fn_write(struct ast_channel *chan, const char *function, char *data, const char *value)
{
	int res;

	if (!chan) {
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(AST_LOG_WARNING, "TALK_DETECT requires an argument\n");
		return -1;
	}

	if (!strcasecmp(data, "set")) {
		int dsp_silence_threshold = DEFAULT_SILENCE_THRESHOLD;
		int dsp_talking_threshold = ast_dsp_get_threshold_from_settings(THRESHOLD_SILENCE);

		if (!ast_strlen_zero(value)) {
			char *parse = ast_strdupa(value);

			AST_DECLARE_APP_ARGS(args,
				AST_APP_ARG(silence_threshold);
				AST_APP_ARG(talking_threshold);
			);

			AST_STANDARD_APP_ARGS(args, parse);

			if (!ast_strlen_zero(args.silence_threshold)) {
				if (sscanf(args.silence_threshold, "%30d", &dsp_silence_threshold) != 1) {
					ast_log(AST_LOG_WARNING, "Failed to parse %s for dsp_silence_threshold\n",
					        args.silence_threshold);
					return -1;
				}

				if (dsp_silence_threshold < 1) {
					ast_log(AST_LOG_WARNING, "Invalid value %d for dsp_silence_threshold\n",
					        dsp_silence_threshold);
					return -1;
				}
			}

			if (!ast_strlen_zero(args.talking_threshold)) {
				if (sscanf(args.talking_threshold, "%30d", &dsp_talking_threshold) != 1) {
					ast_log(AST_LOG_WARNING, "Failed to parse %s for dsp_talking_threshold\n",
					        args.talking_threshold);
					return -1;
				}

				if (dsp_talking_threshold < 1) {
					ast_log(AST_LOG_WARNING, "Invalid value %d for dsp_talking_threshold\n",
					        dsp_silence_threshold);
					return -1;
				}
			}
		}

		res = set_talk_detect(chan, dsp_silence_threshold, dsp_talking_threshold);
	} else if (!strcasecmp(data, "remove")) {
		res = remove_talk_detect(chan);
	} else {
		ast_log(AST_LOG_WARNING, "TALK_DETECT: unknown option %s\n", data);
		res = -1;
	}

	return res;
}

/*! \brief Definition of the TALK_DETECT function */
static struct ast_custom_function talk_detect_function = {
	.name = "TALK_DETECT",
	.write = talk_detect_fn_write,
};

/*! \internal \brief Load the module */
static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&talk_detect_function);

	return res ? AST_MODULE_LOAD_FAILURE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Talk detection dialplan function");
