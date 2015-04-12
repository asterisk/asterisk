/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \file
 * \brief Playtones application
 *
 * \author Russell Bryant <russell@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/indications.h"

static const char playtones_app[] = "PlayTones";
static const char stopplaytones_app[] = "StopPlayTones";

/*** DOCUMENTATION
	<application name="PlayTones" language="en_US">
		<synopsis>
			Play a tone list.
		</synopsis>
		<syntax>
			<parameter name="arg" required="true">
				<para>Arg is either the tone name defined in the <filename>indications.conf</filename>
				configuration file, or a directly specified list of frequencies and durations.</para>
			</parameter>
		</syntax>
		<description>
			<para>Plays a tone list. Execution will continue with the next step in the dialplan
			immediately while the tones continue to play.</para>
			<para>See the sample <filename>indications.conf</filename> for a description of the
			specification of a tonelist.</para>
		</description>
		<see-also>
			<ref type="application">StopPlayTones</ref>
		</see-also>
	</application>
	<application name="StopPlayTones" language="en_US">
		<synopsis>
			Stop playing a tone list.
		</synopsis>
		<syntax />
		<description>
			<para>Stop playing a tone list, initiated by PlayTones().</para>
		</description>
		<see-also>
			<ref type="application">PlayTones</ref>
		</see-also>
	</application>
 ***/

static int handle_playtones(struct ast_channel *chan, const char *data)
{
	struct ast_tone_zone_sound *ts;
	int res;
	const char *str = data;

	if (ast_strlen_zero(str)) {
		ast_log(LOG_NOTICE,"Nothing to play\n");
		return -1;
	}

	ts = ast_get_indication_tone(ast_channel_zone(chan), str);

	if (ts) {
		res = ast_playtones_start(chan, 0, ts->data, 0);
		ts = ast_tone_zone_sound_unref(ts);
	} else {
		res = ast_playtones_start(chan, 0, str, 0);
	}

	if (res) {
		ast_log(LOG_NOTICE, "Unable to start playtones\n");
	}

	return res;
}

static int handle_stopplaytones(struct ast_channel *chan, const char *data)
{
	ast_playtones_stop(chan);

	return 0;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(playtones_app);
	res |= ast_unregister_application(stopplaytones_app);

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application_xml(playtones_app, handle_playtones);
	res |= ast_register_application_xml(stopplaytones_app, handle_stopplaytones);

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Playtones Application");
