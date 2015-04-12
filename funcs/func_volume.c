/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com> 
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
 * \brief Technology independent volume control
 *
 * \author Joshua Colp <jcolp@digium.com> 
 *
 * \ingroup functions
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/audiohook.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="VOLUME" language="en_US">
		<synopsis>
			Set the TX or RX volume of a channel.
		</synopsis>
		<syntax>
			<parameter name="direction" required="true">
				<para>Must be <literal>TX</literal> or <literal>RX</literal>.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="p">
						<para>Enable DTMF volume control</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>The VOLUME function can be used to increase or decrease the <literal>tx</literal> or
			<literal>rx</literal> gain of any channel.</para>
			<para>For example:</para>
			<para>Set(VOLUME(TX)=3)</para>
			<para>Set(VOLUME(RX)=2)</para>
			<para>Set(VOLUME(TX,p)=3)</para>
			<para>Set(VOLUME(RX,p)=3)</para>
		</description>
	</function>
 ***/

struct volume_information {
	struct ast_audiohook audiohook;
	int tx_gain;
	int rx_gain;
	unsigned int flags;
};

enum volume_flags {
	VOLUMEFLAG_CHANGE = (1 << 1),
};

AST_APP_OPTIONS(volume_opts, {
	AST_APP_OPTION('p', VOLUMEFLAG_CHANGE),
});

static void destroy_callback(void *data)
{
	struct volume_information *vi = data;

	/* Destroy the audiohook, and destroy ourselves */
	ast_audiohook_lock(&vi->audiohook);
	ast_audiohook_detach(&vi->audiohook);
	ast_audiohook_unlock(&vi->audiohook);
	ast_audiohook_destroy(&vi->audiohook);
	ast_free(vi);

	return;
}

/*! \brief Static structure for datastore information */
static const struct ast_datastore_info volume_datastore = {
	.type = "volume",
	.destroy = destroy_callback
};

static int volume_callback(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *frame, enum ast_audiohook_direction direction)
{
	struct ast_datastore *datastore = NULL;
	struct volume_information *vi = NULL;
	int *gain = NULL;

	/* If the audiohook is stopping it means the channel is shutting down.... but we let the datastore destroy take care of it */
	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE)
		return 0;

	/* Grab datastore which contains our gain information */
	if (!(datastore = ast_channel_datastore_find(chan, &volume_datastore, NULL)))
		return 0;

	vi = datastore->data;

	/* If this is DTMF then allow them to increase/decrease the gains */
	if (ast_test_flag(vi, VOLUMEFLAG_CHANGE)) {
		if (frame->frametype == AST_FRAME_DTMF) {
			/* Only use DTMF coming from the source... not going to it */
			if (direction != AST_AUDIOHOOK_DIRECTION_READ)
				return 0; 
			if (frame->subclass.integer == '*') {
				vi->tx_gain += 1;
				vi->rx_gain += 1;
			} else if (frame->subclass.integer == '#') {
				vi->tx_gain -= 1;
				vi->rx_gain -= 1;
			}
		}
	}

	
	if (frame->frametype == AST_FRAME_VOICE) {
		/* Based on direction of frame grab the gain, and confirm it is applicable */
		if (!(gain = (direction == AST_AUDIOHOOK_DIRECTION_READ) ? &vi->rx_gain : &vi->tx_gain) || !*gain)
			return 0;
		/* Apply gain to frame... easy as pi */
		ast_frame_adjust_volume(frame, *gain);
	}

	return 0;
}

static int volume_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *datastore = NULL;
	struct volume_information *vi = NULL;
	int is_new = 0;

	/* Separate options from argument */

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(direction);
		AST_APP_ARG(options);
	);

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &volume_datastore, NULL))) {
		ast_channel_unlock(chan);
		/* Allocate a new datastore to hold the reference to this volume and audiohook information */
		if (!(datastore = ast_datastore_alloc(&volume_datastore, NULL)))
			return 0;
		if (!(vi = ast_calloc(1, sizeof(*vi)))) {
			ast_datastore_free(datastore);
			return 0;
		}
		ast_audiohook_init(&vi->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "Volume", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
		vi->audiohook.manipulate_callback = volume_callback;
		ast_set_flag(&vi->audiohook, AST_AUDIOHOOK_WANTS_DTMF);
		is_new = 1;
	} else {
		ast_channel_unlock(chan);
		vi = datastore->data;
	}

	/* Adjust gain on volume information structure */
	if (ast_strlen_zero(args.direction)) {
		ast_log(LOG_ERROR, "Direction must be specified for VOLUME function\n");
		return -1;
	}

	if (!strcasecmp(args.direction, "tx")) { 
		vi->tx_gain = atoi(value); 
	} else if (!strcasecmp(args.direction, "rx")) {
		vi->rx_gain = atoi(value);
	} else {
		ast_log(LOG_ERROR, "Direction must be either RX or TX\n");
	}

	if (is_new) {
		datastore->data = vi;
		ast_channel_lock(chan);
		ast_channel_datastore_add(chan, datastore);
		ast_channel_unlock(chan);
		ast_audiohook_attach(chan, &vi->audiohook);
	}

	/* Add Option data to struct */
	
	if (!ast_strlen_zero(args.options)) {
		struct ast_flags flags = { 0 };
		ast_app_parse_options(volume_opts, &flags, NULL, args.options);
		vi->flags = flags.flags;
	} else { 
		vi->flags = 0; 
	}

	return 0;
}

static struct ast_custom_function volume_function = {
	.name = "VOLUME",
	.write = volume_write,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&volume_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&volume_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Technology independent volume control");
