/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Olle E. Johansson
 *
 * Olle E. Johansson <oej@edvina.net>
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
 * \brief MUTESTREAM audiohooks
 *
 * \author Olle E. Johansson <oej@edvina.net>
 *
 *  \ingroup functions
 *
 * \note This module only handles audio streams today, but can easily be appended to also
 * zero out text streams if there's an application for it.
 * When we know and understands what happens if we zero out video, we can do that too.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 89545 $")

//#include <time.h>
//#include <string.h>
//#include <stdio.h>
//#include <stdlib.h>
//#include <unistd.h>
//#include <errno.h>

#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/file.h"
#include "asterisk/pbx.h"
#include "asterisk/frame.h"
#include "asterisk/utils.h"
#include "asterisk/audiohook.h"
#include "asterisk/manager.h"

/*** DOCUMENTATION
	<function name="MUTEAUDIO" language="en_US">
		<synopsis>
			Muting audio streams in the channel
		</synopsis>
		<syntax>
			<parameter name="direction" required="true">
				<para>Must be one of </para>
				<enumlist>
					<enum name="in">
						<para>Inbound stream (to the PBX)</para>
					</enum>
					<enum name="out">
						<para>Outbound stream (from the PBX)</para>
					</enum>
					<enum name="all">
						<para>Both streams</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>The MUTEAUDIO function can be used to mute inbound (to the PBX) or outbound audio in a call.
			Example:
			</para>
			<para>
			MUTEAUDIO(in)=on
			MUTEAUDIO(in)=off
			</para>
		</description>
	</function>
 ***/


/*! Our own datastore */
struct mute_information {
	struct ast_audiohook audiohook;
	int mute_write;
	int mute_read;
};


#define TRUE 1
#define FALSE 0

/*! Datastore destroy audiohook callback */
static void destroy_callback(void *data)
{
	struct mute_information *mute = data;

	/* Destroy the audiohook, and destroy ourselves */
	ast_audiohook_destroy(&mute->audiohook);
	ast_free(mute);
	ast_module_unref(ast_module_info->self);

	return;
}

/*! \brief Static structure for datastore information */
static const struct ast_datastore_info mute_datastore = {
	.type = "mute",
	.destroy = destroy_callback
};

/*! \brief The callback from the audiohook subsystem. We basically get a frame to have fun with */
static int mute_callback(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *frame, enum ast_audiohook_direction direction)
{
	struct ast_datastore *datastore = NULL;
	struct mute_information *mute = NULL;


	/* If the audiohook is stopping it means the channel is shutting down.... but we let the datastore destroy take care of it */
	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE) {
		return 0;
	}

	ast_channel_lock(chan);
	/* Grab datastore which contains our mute information */
	if (!(datastore = ast_channel_datastore_find(chan, &mute_datastore, NULL))) {
		ast_channel_unlock(chan);
		ast_debug(2, "Can't find any datastore to use. Bad. \n");
		return 0;
	}

	mute = datastore->data;


	/* If this is audio then allow them to increase/decrease the gains */
	if (frame->frametype == AST_FRAME_VOICE) {
		ast_debug(2, "Audio frame - direction %s  mute READ %s WRITE %s\n", direction == AST_AUDIOHOOK_DIRECTION_READ ? "read" : "write", mute->mute_read ? "on" : "off", mute->mute_write ? "on" : "off");

		/* Based on direction of frame grab the gain, and confirm it is applicable */
		if ((direction == AST_AUDIOHOOK_DIRECTION_READ && mute->mute_read) || (direction == AST_AUDIOHOOK_DIRECTION_WRITE && mute->mute_write)) {
			/* Ok, we just want to reset all audio in this frame. Keep NOTHING, thanks. */
			ast_frame_clear(frame);
		}
	}
	ast_channel_unlock(chan);

	return 0;
}

/*! \brief Initialize mute hook on channel, but don't activate it
	\pre Assumes that the channel is locked
*/
static struct ast_datastore *initialize_mutehook(struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct mute_information *mute = NULL;

	ast_debug(2, "Initializing new Mute Audiohook \n");

	/* Allocate a new datastore to hold the reference to this mute_datastore and audiohook information */
	if (!(datastore = ast_datastore_alloc(&mute_datastore, NULL))) {
		return NULL;
	}

	if (!(mute = ast_calloc(1, sizeof(*mute)))) {
		ast_datastore_free(datastore);
		return NULL;
	}
	ast_audiohook_init(&mute->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "Mute", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
	mute->audiohook.manipulate_callback = mute_callback;
	datastore->data = mute;
	return datastore;
}

/*! \brief Add or activate mute audiohook on channel
	Assumes channel is locked
*/
static int mute_add_audiohook(struct ast_channel *chan, struct mute_information *mute, struct ast_datastore *datastore)
{
	/* Activate the settings */
	ast_channel_datastore_add(chan, datastore);
	if (ast_audiohook_attach(chan, &mute->audiohook)) {
		ast_log(LOG_ERROR, "Failed to attach audiohook for muting channel %s\n", chan->name);
		return -1;
	}
	ast_module_ref(ast_module_info->self);
	ast_debug(2, "Initialized audiohook on channel %s\n", chan->name);
	return 0;
}

/*! \brief Mute dialplan function */
static int func_mute_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *datastore = NULL;
	struct mute_information *mute = NULL;
	int is_new = 0;

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &mute_datastore, NULL))) {
		if (!(datastore = initialize_mutehook(chan))) {
			ast_channel_unlock(chan);
			return 0;
		}
		is_new = 1;
	}

	mute = datastore->data;

	if (!strcasecmp(data, "out")) {
		mute->mute_write = ast_true(value);
		ast_debug(1, "%s channel - outbound \n", ast_true(value) ? "Muting" : "Unmuting");
	} else if (!strcasecmp(data, "in")) {
		mute->mute_read = ast_true(value);
		ast_debug(1, "%s channel - inbound  \n", ast_true(value) ? "Muting" : "Unmuting");
	} else if (!strcasecmp(data,"all")) {
		mute->mute_write = mute->mute_read = ast_true(value);
	}

	if (is_new) {
		if (mute_add_audiohook(chan, mute, datastore)) {
			/* Can't add audiohook - already printed error message */
			ast_datastore_free(datastore);
			ast_free(mute);
		}
	}
	ast_channel_unlock(chan);

	return 0;
}

/* Function for debugging - might be useful */
static struct ast_custom_function mute_function = {
        .name = "MUTEAUDIO",
        .write = func_mute_write,
};

static int manager_mutestream(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m,"ActionID");
	const char *state = astman_get_header(m,"State");
	const char *direction = astman_get_header(m,"Direction");
	char id_text[256] = "";
	struct ast_channel *c = NULL;
	struct ast_datastore *datastore = NULL;
	struct mute_information *mute = NULL;
	int is_new = 0;
	int turnon = TRUE;

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}
	if (ast_strlen_zero(state)) {
		astman_send_error(s, m, "State not specified");
		return 0;
	}
	if (ast_strlen_zero(direction)) {
		astman_send_error(s, m, "Direction not specified");
		return 0;
	}
	/* Ok, we have everything */
	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	}

	c = ast_channel_get_by_name(channel);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	ast_channel_lock(c);

	if (!(datastore = ast_channel_datastore_find(c, &mute_datastore, NULL))) {
		if (!(datastore = initialize_mutehook(c))) {
			ast_channel_unlock(c);
			ast_channel_unref(c);
			return 0;
		}
		is_new = 1;
	}
	mute = datastore->data;
	turnon = ast_true(state);

	if (!strcasecmp(direction, "in")) {
		mute->mute_read = turnon;
	} else if (!strcasecmp(direction, "out")) {
		mute->mute_write = turnon;
	} else if (!strcasecmp(direction, "all")) {
		mute->mute_read = mute->mute_write = turnon;
	}

	if (is_new) {
		if (mute_add_audiohook(c, mute, datastore)) {
			/* Can't add audiohook - already printed error message */
			ast_datastore_free(datastore);
			ast_free(mute);
		}
	}
	ast_channel_unlock(c);
	ast_channel_unref(c);

	astman_append(s, "Response: Success\r\n"
				   "%s"
				   "\r\n\r\n", id_text);
	return 0;
}


static const char mandescr_mutestream[] =
"Description: Mute an incoming or outbound audio stream in a channel.\n"
"Variables: \n"
"  Channel: <name>           The channel you want to mute.\n"
"  Direction: in | out |all  The stream you want to mute.\n"
"  State: on | off           Whether to turn mute on or off.\n"
"  ActionID: <id>            Optional action ID for this AMI transaction.\n";


static int load_module(void)
{
	int res;
	res = ast_custom_function_register(&mute_function);

	res |= ast_manager_register2("MuteAudio", EVENT_FLAG_SYSTEM, manager_mutestream,
                        "Mute an audio stream", mandescr_mutestream);

	return (res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS);
}

static int unload_module(void)
{
	ast_custom_function_unregister(&mute_function);
	/* Unregister AMI actions */
        ast_manager_unregister("MuteAudio");

	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Mute audio stream resources");
