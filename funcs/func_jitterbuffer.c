/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
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
 * \brief Put a jitterbuffer on the read side of a channel
 *
 * \author David Vossel <dvossel@digium.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/framehook.h"
#include "asterisk/frame.h"
#include "asterisk/pbx.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/timing.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="JITTERBUFFER" language="en_US">
		<synopsis>
			Add a Jitterbuffer to the Read side of the channel.  This dejitters the audio stream before it reaches the Asterisk core. This is a write only function.
		</synopsis>
		<syntax>
			<parameter name="jitterbuffer type" required="true">
				<para>Jitterbuffer type can be <literal>fixed</literal>, <literal>adaptive</literal>, or
					<literal>disabled</literal>.</para>
				<para>Used as follows. </para>
				<para>Set(JITTERBUFFER(type)=max_size[,resync_threshold[,target_extra]])</para>
				<para>Set(JITTERBUFFER(type)=default) </para>
			</parameter>
		</syntax>
		<description>
			<para>max_size: Defaults to 200 ms</para>
			<para>Length in milliseconds of buffer.</para>
			<para> </para>
			<para>resync_threshold: Defaults to 1000ms </para>
			<para>The length in milliseconds over which a timestamp difference will result in resyncing the jitterbuffer. </para>
			<para> </para>
			<para>target_extra: Defaults to 40ms</para>
			<para>This option only affects the adaptive jitterbuffer. It represents the amount time in milliseconds by which the new jitter buffer will pad its size.</para>
			<para> </para>
			<para>Examples:</para>
			<para>exten => 1,1,Set(JITTERBUFFER(fixed)=default);Fixed with defaults. </para>
			<para>exten => 1,1,Set(JITTERBUFFER(fixed)=200);Fixed with max size 200ms, default resync threshold and target extra. </para>
			<para>exten => 1,1,Set(JITTERBUFFER(fixed)=200,1500);Fixed with max size 200ms resync threshold 1500. </para>
			<para>exten => 1,1,Set(JITTERBUFFER(adaptive)=default);Adaptive with defaults. </para>
			<para>exten => 1,1,Set(JITTERBUFFER(adaptive)=200,,60);Adaptive with max size 200ms, default resync threshold and 40ms target extra. </para>
			<para>exten => 1,n,Set(JITTERBUFFER(disabled)=);Remove previously applied jitterbuffer </para>
			<note><para>If a channel specifies a jitterbuffer due to channel driver configuration and
			the JITTERBUFFER function has set a jitterbuffer for that channel, the jitterbuffer set by
			the JITTERBUFFER function will take priority and the jitterbuffer set by the channel
			configuration will not be applied.</para></note>
		</description>
	</function>
 ***/

static int jb_helper(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_jb_conf jb_conf;

	/* Initialize and set jb_conf */
	ast_jb_conf_default(&jb_conf);

	/* Now check user options to see if any of the defaults need to change. */
	if (!ast_strlen_zero(data)) {
		if (strcasecmp(data, "fixed") &&
				strcasecmp(data, "adaptive") &&
				strcasecmp(data, "disabled")) {
			ast_log(LOG_WARNING, "Unknown Jitterbuffer type %s. Failed to create jitterbuffer.\n", data);
			return -1;
		}
		ast_copy_string(jb_conf.impl, data, sizeof(jb_conf.impl));
	}

	if (!ast_strlen_zero(value) && strcasecmp(value, "default")) {
		char *parse = ast_strdupa(value);
		int res = 0;
		AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(max_size);
			AST_APP_ARG(resync_threshold);
			AST_APP_ARG(target_extra);
		);

		AST_STANDARD_APP_ARGS(args, parse);
		if (!ast_strlen_zero(args.max_size)) {
			res |= ast_jb_read_conf(&jb_conf,
				"jbmaxsize",
				args.max_size);
		}
		if (!ast_strlen_zero(args.resync_threshold)) {
			res |= ast_jb_read_conf(&jb_conf,
				"jbresyncthreshold",
				args.resync_threshold);
		}
		if (!ast_strlen_zero(args.target_extra)) {
			res |= ast_jb_read_conf(&jb_conf,
				"jbtargetextra",
				args.target_extra);
		}
		if (res) {
			ast_log(LOG_WARNING, "Invalid jitterbuffer parameters %s\n", value);
		}
	}

	ast_jb_create_framehook(chan, &jb_conf, 0);

	return 0;
}


static struct ast_custom_function jb_function = {
	.name = "JITTERBUFFER",
	.write = jb_helper,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&jb_function);
}

static int load_module(void)
{
	int res = ast_custom_function_register(&jb_function);
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Jitter buffer for read side of channel.");

