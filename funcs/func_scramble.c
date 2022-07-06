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
 * \brief Frequency inverter
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup functions
 *
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<function name="SCRAMBLE" language="en_US">
		<since>
			<version>16.21.0</version>
			<version>18.7.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Scrambles audio on a channel.
		</synopsis>
		<syntax>
			<parameter name="direction" required="false">
				<para>Must be <literal>TX</literal> or <literal>RX</literal>
				to limit to a specific direction, or <literal>both</literal>
				for both directions. <literal>remove</literal>
				will remove an existing scrambler.</para>
			</parameter>
		</syntax>
		<description>
			<para>Scrambles audio on a channel using whole spectrum inversion.
			This is not intended to be used for securely scrambling
			audio. It merely renders obfuscates audio on a channel
			to render it unintelligible, as a privacy enhancement.</para>
		</description>
		<see-also>
			<ref type="application">ChanSpy</ref>
		</see-also>
	</function>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/audiohook.h"
#include "asterisk/app.h"

#include <stdio.h>
#include <string.h>

struct scramble_information {
	struct ast_audiohook audiohook;
	unsigned short int tx;
	unsigned short int rx;
	unsigned short int state;
};

static void destroy_callback(void *data)
{
	struct scramble_information *ni = data;

	/* Destroy the audiohook, and destroy ourselves */
	ast_audiohook_lock(&ni->audiohook);
	ast_audiohook_detach(&ni->audiohook);
	ast_audiohook_unlock(&ni->audiohook);
	ast_audiohook_destroy(&ni->audiohook);
	ast_free(ni);

	return;
}

/*! \brief Static structure for datastore information */
static const struct ast_datastore_info scramble_datastore = {
	.type = "scramble",
	.destroy = destroy_callback
};

/* modifies buffer pointed to by 'amp' with inverted values */
static inline void freq_invert(short *amp, int samples)
{
	int i;
	/* invert every other sample by 1 */
	for (i = 0; i < samples; i += 2) {
		amp[i] = -amp[i];
	}
}

static int scramble_callback(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *frame, enum ast_audiohook_direction direction)
{
	struct ast_datastore *datastore = NULL;
	struct scramble_information *ni = NULL;

	/* If the audiohook is stopping it means the channel is shutting down.... but we let the datastore destroy take care of it */
	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE) {
		return 0;
	}

	/* Grab datastore which contains our gain information */
	if (!(datastore = ast_channel_datastore_find(chan, &scramble_datastore, NULL))) {
		return 0;
	}

	if (frame->frametype == AST_FRAME_VOICE) { /* only invert voice frequencies */
		/* Based on direction of frame, and confirm it is applicable */
		if (!(direction == AST_AUDIOHOOK_DIRECTION_READ ? ni->rx : ni->tx)) {
			return 0;
		}
		/* Scramble the sample now */
		freq_invert(frame->data.ptr, frame->samples);
	}
	return 0;
}

/*! \internal \brief Disable scrambling on the channel */
static int remove_scrambler(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct scramble_information *data;
	SCOPED_CHANNELLOCK(chan_lock, chan);

	datastore = ast_channel_datastore_find(chan, &scramble_datastore, NULL);
	if (!datastore) {
		ast_log(AST_LOG_WARNING, "Cannot remove SCRAMBLE from %s: SCRAMBLE not currently enabled\n",
		        ast_channel_name(chan));
		return -1;
	}
	data = datastore->data;

	if (ast_audiohook_remove(chan, &data->audiohook)) {
		ast_log(AST_LOG_WARNING, "Failed to remove SCRAMBLE audiohook from channel %s\n", ast_channel_name(chan));
		return -1;
	}

	if (ast_channel_datastore_remove(chan, datastore)) {
		ast_log(AST_LOG_WARNING, "Failed to remove SCRAMBLE datastore from channel %s\n",
		        ast_channel_name(chan));
		return -1;
	}
	ast_datastore_free(datastore);

	return 0;
}

static int scramble_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	char *parse;
	struct ast_datastore *datastore = NULL;
	struct scramble_information *ni = NULL;
	int tx = 1, rx = 1;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(direction);
	);

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	parse = ast_strdupa(value);
	AST_STANDARD_APP_ARGS(args, parse);

	if (!strcasecmp(args.direction, "remove")) {
		return remove_scrambler(chan);
	}
	if (!strcasecmp(args.direction, "tx")) {
		tx = 1;
		rx = 0;
	} else if (!strcasecmp(args.direction, "rx")) {
		rx = 0;
		tx = 1;
	} else if (strcasecmp(args.direction, "both")) {
		ast_log(LOG_ERROR, "Direction must be either RX, TX, both, or remove\n");
		return -1;
	}
	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &scramble_datastore, NULL))) {
		/* Allocate a new datastore to hold the reference to this audiohook information */
		if (!(datastore = ast_datastore_alloc(&scramble_datastore, NULL))) {
			return 0;
		}
		if (!(ni = ast_calloc(1, sizeof(*ni)))) {
			ast_datastore_free(datastore);
			return 0;
		}
		ast_audiohook_init(&ni->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "Voice scrambler", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
		ni->audiohook.manipulate_callback = scramble_callback;
		datastore->data = ni;
		ast_channel_datastore_add(chan, datastore);
		ast_audiohook_attach(chan, &ni->audiohook);
	} else {
		ni = datastore->data;
	}
	ni->tx = tx;
	ni->rx = rx;
	ast_channel_unlock(chan);

	return 0;
}

static struct ast_custom_function scramble_function = {
	.name = "SCRAMBLE",
	.write = scramble_write,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&scramble_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&scramble_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Frequency inverting voice scrambler");
