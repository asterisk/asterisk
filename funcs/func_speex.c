/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Brian Degenhardt <bmd@digium.com>
 * Brett Bryant <bbryant@digium.com> 
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
 * \brief Noise reduction and automatic gain control (AGC)
 *
 * \author Brian Degenhardt <bmd@digium.com> 
 * \author Brett Bryant <bbryant@digium.com> 
 *
 * \ingroup functions
 *
 * \extref The Speex 1.2 library - http://www.speex.org
 * \note Requires the 1.2 version of the Speex library (which might not be what you find in Linux packages)
 */

/*** MODULEINFO
	<depend>speex</depend>
	<depend>speex_preprocess</depend>
	<use>speexdsp</use>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <speex/speex_preprocess.h>
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/audiohook.h"

#define DEFAULT_AGC_LEVEL 8000.0

/*** DOCUMENTATION
	<function name="AGC" language="en_US">
		<synopsis>
			Apply automatic gain control to audio on a channel.
		</synopsis>
		<syntax>
			<parameter name="channeldirection" required="true">
				<para>This can be either <literal>rx</literal> or <literal>tx</literal></para>
			</parameter>
		</syntax>
		<description>
			<para>The AGC function will apply automatic gain control to the audio on the
			channel that it is executed on. Using <literal>rx</literal> for audio received
			and <literal>tx</literal> for audio transmitted to the channel. When using this
			function you set a target audio level. It is primarily intended for use with
			analog lines, but could be useful for other channels as well. The target volume 
			is set with a number between <literal>1-32768</literal>. The larger the number
			the louder (more gain) the channel will receive.</para>
			<para>Examples:</para>
			<para>exten => 1,1,Set(AGC(rx)=8000)</para>
			<para>exten => 1,2,Set(AGC(tx)=off)</para>
		</description>
	</function>
	<function name="DENOISE" language="en_US">
		<synopsis>
			Apply noise reduction to audio on a channel.
		</synopsis>
		<syntax>
			<parameter name="channeldirection" required="true">
				<para>This can be either <literal>rx</literal> or <literal>tx</literal> 
				the values that can be set to this are either <literal>on</literal> and
				<literal>off</literal></para>
			</parameter>
		</syntax>
		<description>
			<para>The DENOISE function will apply noise reduction to audio on the channel
			that it is executed on. It is very useful for noisy analog lines, especially
			when adjusting gains or using AGC. Use <literal>rx</literal> for audio received from the channel
			and <literal>tx</literal> to apply the filter to the audio being sent to the channel.</para>
			<para>Examples:</para>
			<para>exten => 1,1,Set(DENOISE(rx)=on)</para>
			<para>exten => 1,2,Set(DENOISE(tx)=off)</para>
		</description>
	</function>
 ***/

struct speex_direction_info {
	SpeexPreprocessState *state;	/*!< speex preprocess state object */
	int agc;						/*!< audio gain control is enabled or not */
	int denoise;					/*!< denoise is enabled or not */
	int samples;					/*!< n of 8Khz samples in last frame */
	float agclevel;					/*!< audio gain control level [1.0 - 32768.0] */
};

struct speex_info {
	struct ast_audiohook audiohook;
	int lastrate;
	struct speex_direction_info *tx, *rx;
};

static void destroy_callback(void *data) 
{
	struct speex_info *si = data;

	ast_audiohook_destroy(&si->audiohook);

	if (si->rx && si->rx->state) {
		speex_preprocess_state_destroy(si->rx->state);
	}

	if (si->tx && si->tx->state) {
		speex_preprocess_state_destroy(si->tx->state);
	}

	if (si->rx) {
		ast_free(si->rx);
	}

	if (si->tx) {
		ast_free(si->tx);
	}

	ast_free(data);
};

static const struct ast_datastore_info speex_datastore = {
	.type = "speex",
	.destroy = destroy_callback
};

static int speex_callback(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *frame, enum ast_audiohook_direction direction)
{
	struct ast_datastore *datastore = NULL;
	struct speex_direction_info *sdi = NULL;
	struct speex_info *si = NULL;
	char source[80];

	/* If the audiohook is stopping it means the channel is shutting down.... but we let the datastore destroy take care of it */
	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE || frame->frametype != AST_FRAME_VOICE) {
		return -1;
	}

	/* We are called with chan already locked */
	if (!(datastore = ast_channel_datastore_find(chan, &speex_datastore, NULL))) {
		return -1;
	}

	si = datastore->data;

	sdi = (direction == AST_AUDIOHOOK_DIRECTION_READ) ? si->rx : si->tx;

	if (!sdi) {
		return -1;
	}

	if ((sdi->samples != frame->samples) || (ast_format_rate(&frame->subclass.format) != si->lastrate)) {
		si->lastrate = ast_format_rate(&frame->subclass.format);
		if (sdi->state) {
			speex_preprocess_state_destroy(sdi->state);
		}

		if (!(sdi->state = speex_preprocess_state_init((sdi->samples = frame->samples), si->lastrate))) {
			return -1;
		}

		speex_preprocess_ctl(sdi->state, SPEEX_PREPROCESS_SET_AGC, &sdi->agc);

		if (sdi->agc) {
			speex_preprocess_ctl(sdi->state, SPEEX_PREPROCESS_SET_AGC_LEVEL, &sdi->agclevel);
		}

		speex_preprocess_ctl(sdi->state, SPEEX_PREPROCESS_SET_DENOISE, &sdi->denoise);
	}

	speex_preprocess(sdi->state, frame->data.ptr, NULL);
	snprintf(source, sizeof(source), "%s/speex", frame->src);
	if (frame->mallocd & AST_MALLOCD_SRC) {
		ast_free((char *) frame->src);
	}
	frame->src = ast_strdup(source);
	frame->mallocd |= AST_MALLOCD_SRC;

	return 0;
}

static int speex_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *datastore = NULL;
	struct speex_info *si = NULL;
	struct speex_direction_info **sdi = NULL;
	int is_new = 0;

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &speex_datastore, NULL))) {
		ast_channel_unlock(chan);

		if (!(datastore = ast_datastore_alloc(&speex_datastore, NULL))) {
			return 0;
		}

		if (!(si = ast_calloc(1, sizeof(*si)))) {
			ast_datastore_free(datastore);
			return 0;
		}

		ast_audiohook_init(&si->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "speex", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
		si->audiohook.manipulate_callback = speex_callback;
		si->lastrate = 8000;
		is_new = 1;
	} else {
		ast_channel_unlock(chan);
		si = datastore->data;
	}

	if (!strcasecmp(data, "rx")) {
		sdi = &si->rx;
	} else if (!strcasecmp(data, "tx")) {
		sdi = &si->tx;
	} else {
		ast_log(LOG_ERROR, "Invalid argument provided to the %s function\n", cmd);

		if (is_new) {
			ast_datastore_free(datastore);
			return -1;
		}
	}

	if (!*sdi) {
		if (!(*sdi = ast_calloc(1, sizeof(**sdi)))) {
			return 0;
		}
		/* Right now, the audiohooks API will _only_ provide us 8 kHz slinear
		 * audio.  When it supports 16 kHz (or any other sample rates, we will
		 * have to take that into account here. */
		(*sdi)->samples = -1;
	}

	if (!strcasecmp(cmd, "agc")) {
		if (!sscanf(value, "%30f", &(*sdi)->agclevel))
			(*sdi)->agclevel = ast_true(value) ? DEFAULT_AGC_LEVEL : 0.0;
	
		if ((*sdi)->agclevel > 32768.0) {
			ast_log(LOG_WARNING, "AGC(%s)=%.01f is greater than 32768... setting to 32768 instead\n", 
					((*sdi == si->rx) ? "rx" : "tx"), (*sdi)->agclevel);
			(*sdi)->agclevel = 32768.0;
		}
	
		(*sdi)->agc = !!((*sdi)->agclevel);

		if ((*sdi)->state) {
			speex_preprocess_ctl((*sdi)->state, SPEEX_PREPROCESS_SET_AGC, &(*sdi)->agc);
			if ((*sdi)->agc) {
				speex_preprocess_ctl((*sdi)->state, SPEEX_PREPROCESS_SET_AGC_LEVEL, &(*sdi)->agclevel);
			}
		}
	} else if (!strcasecmp(cmd, "denoise")) {
		(*sdi)->denoise = (ast_true(value) != 0);

		if ((*sdi)->state) {
			speex_preprocess_ctl((*sdi)->state, SPEEX_PREPROCESS_SET_DENOISE, &(*sdi)->denoise);
		}
	}

	if (!(*sdi)->agc && !(*sdi)->denoise) {
		if ((*sdi)->state)
			speex_preprocess_state_destroy((*sdi)->state);

		ast_free(*sdi);
		*sdi = NULL;
	}

	if (!si->rx && !si->tx) {
		if (is_new) {
			is_new = 0;
		} else {
			ast_channel_lock(chan);
			ast_channel_datastore_remove(chan, datastore);
			ast_channel_unlock(chan);
			ast_audiohook_remove(chan, &si->audiohook);
			ast_audiohook_detach(&si->audiohook);
		}
		
		ast_datastore_free(datastore);
	}

	if (is_new) { 
		datastore->data = si;
		ast_channel_lock(chan);
		ast_channel_datastore_add(chan, datastore);
		ast_channel_unlock(chan);
		ast_audiohook_attach(chan, &si->audiohook);
	}

	return 0;
}

static int speex_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *datastore = NULL;
	struct speex_info *si = NULL;
	struct speex_direction_info *sdi = NULL;

	if (!chan) {
		ast_log(LOG_ERROR, "%s cannot be used without a channel!\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &speex_datastore, NULL))) {
		ast_channel_unlock(chan);
		return -1;
	}
	ast_channel_unlock(chan);

	si = datastore->data;

	if (!strcasecmp(data, "tx"))
		sdi = si->tx;
	else if (!strcasecmp(data, "rx"))
		sdi = si->rx;
	else {
		ast_log(LOG_ERROR, "%s(%s) must either \"tx\" or \"rx\"\n", cmd, data);
		return -1;
	}

	if (!strcasecmp(cmd, "agc"))
		snprintf(buf, len, "%.01f", sdi ? sdi->agclevel : 0.0);
	else
		snprintf(buf, len, "%d", sdi ? sdi->denoise : 0);

	return 0;
}

static struct ast_custom_function agc_function = {
	.name = "AGC",
	.write = speex_write,
	.read = speex_read,
	.read_max = 22,
};

static struct ast_custom_function denoise_function = {
	.name = "DENOISE",
	.write = speex_write,
	.read = speex_read,
	.read_max = 22,
};

static int unload_module(void)
{
	ast_custom_function_unregister(&agc_function);
	ast_custom_function_unregister(&denoise_function);
	return 0;
}

static int load_module(void)
{
	if (ast_custom_function_register(&agc_function)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_custom_function_register(&denoise_function)) {
		ast_custom_function_unregister(&agc_function);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Noise reduction and Automatic Gain Control (AGC)");
