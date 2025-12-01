/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Aurora Innovation AB
 *
 * Daniel Donoghue <daniel.donoghue@aurorainnovation.com>
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
 * \brief Stasis broadcast dialplan application
 *
 * \author Daniel Donoghue <daniel.donoghue@aurorainnovation.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<depend type="module">res_stasis_broadcast</depend>
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<application name="StasisBroadcast" language="en_US">
		<since>
			<version>21.0.0</version>
		</since>
		<synopsis>Broadcast a channel to multiple ARI applications for claiming.</synopsis>
		<syntax>
			<parameter name="timeout">
				<para>Timeout in milliseconds to wait for a claim.</para>
				<para>Valid range: 0 to 60000ms</para>
				<para>Default: 500ms</para>
			</parameter>
			<parameter name="app_filter">
				<para>Regular expression to filter which ARI applications
				receive the broadcast. Only applications with names matching
				the regex will be notified.</para>
				<para>Default: all connected applications</para>
			</parameter>
		</syntax>
		<description>
			<para>Broadcasts the incoming channel to all connected ARI applications
			(or a filtered subset) via a <literal>CallBroadcast</literal> event.
			ARI applications can respond with a claim request. The first application
			to claim the channel wins, and subsequent claims are rejected.</para>
			<para>After the timeout period, the channel variable
			<variable>BROADCAST_WINNER</variable> will contain the name of the winning
			application, or be empty if no application claimed the channel.</para>
			<para>This application will set the following channel variable:</para>
			<variablelist>
				<variable name="BROADCAST_WINNER">
					<para>The name of the ARI application that successfully claimed
					the channel, or empty if no application claimed it within the
					timeout period.</para>
				</variable>
			</variablelist>
			<example>
			; Broadcast with default timeout (500ms) to all apps
			exten => _X.,1,StasisBroadcast()
			 same => n,GotoIf($["${BROADCAST_WINNER}"=""]?no_route)
			 same => n,Stasis(${BROADCAST_WINNER})
			 same => n(no_route),Playback(sorry-no-agent)
			 same => n,Hangup()
			</example>
			<example>
			; Broadcast with 1 second timeout to sales apps only
			exten => _X.,1,StasisBroadcast(timeout=1000,app_filter=^sales_.*)
			 same => n,GotoIf($["${BROADCAST_WINNER}"=""]?no_route)
			 same => n,Stasis(${BROADCAST_WINNER})
			 same => n(no_route),Playback(sorry-no-agent)
			 same => n,Hangup()
			</example>
		</description>
	</application>
 ***/

#include "asterisk.h"

#include <limits.h>

#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis_app_broadcast.h"

/*! \brief Dialplan application name */
static const char *app = "StasisBroadcast";

/*! \brief Default timeout in milliseconds */
#define DEFAULT_TIMEOUT_MS 500

/*! \brief Maximum timeout in milliseconds */
#define MAX_TIMEOUT_MS 60000

/*! \brief StasisBroadcast dialplan application callback */
static int stasis_broadcast_exec(struct ast_channel *chan, const char *data)
{
	char *parse = NULL;
	char *options_str = NULL;
	int timeout_ms = DEFAULT_TIMEOUT_MS;
	const char *app_filter = NULL;
	char *winner = NULL;
	int result = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(options);
	);

	AST_DECLARE_APP_ARGS(options,
		AST_APP_ARG(option)[10];
	);

	ast_assert(chan != NULL);

	/* Initialize channel variable */
	ast_channel_lock(chan);
	pbx_builtin_setvar_helper(chan, "BROADCAST_WINNER", "");
	ast_channel_unlock(chan);

	/* Parse arguments if provided */
	if (!ast_strlen_zero(data)) {
		parse = ast_strdupa(data);
		AST_STANDARD_APP_ARGS(args, parse);

		if (!ast_strlen_zero(args.options)) {
			int i;
			options_str = ast_strdupa(args.options);
			AST_STANDARD_APP_ARGS(options, options_str);

			/* Parse key=value options */
			for (i = 0; i < options.argc; i++) {
				char *key = options.option[i];
				char *val = strchr(key, '=');

				if (val) {
					*val++ = '\0';
					ast_strip(key);
					ast_strip(val);

					if (!strcasecmp(key, "timeout")) {
						if (sscanf(val, "%d", &timeout_ms) != 1 || timeout_ms < 0 || timeout_ms > MAX_TIMEOUT_MS) {
							ast_log(LOG_WARNING,
								"Invalid timeout value '%s' (must be 0-%dms), using default %dms\n",
								val, MAX_TIMEOUT_MS, DEFAULT_TIMEOUT_MS);
							timeout_ms = DEFAULT_TIMEOUT_MS;
						}
					} else if (!strcasecmp(key, "app_filter")) {
						app_filter = val;
					} else {
						ast_log(LOG_WARNING, "Unknown option '%s'\n", key);
					}
				}
			}
		}
	}

	ast_log(LOG_NOTICE, "Broadcasting channel %s (timeout=%dms, filter=%s)\n",
		ast_channel_name(chan), timeout_ms, app_filter ? app_filter : "none");

	/* Start the broadcast */
	result = stasis_app_broadcast_channel(chan, timeout_ms, app_filter);
	if (result != 0) {
		ast_log(LOG_ERROR, "Failed to broadcast channel %s (return code: %d)\n",
			ast_channel_name(chan), result);
		return 0;
	}

	/* Wait for a claim */
	if (stasis_app_broadcast_wait(chan, timeout_ms) == 0) {
		/* Channel was claimed */
		winner = stasis_app_broadcast_winner(ast_channel_uniqueid(chan));
		if (winner) {
			ast_log(LOG_NOTICE, "Channel %s claimed by %s\n",
				ast_channel_name(chan), winner);
			ast_channel_lock(chan);
			pbx_builtin_setvar_helper(chan, "BROADCAST_WINNER", winner);
			ast_channel_unlock(chan);
			ast_free(winner);
		}
	} else {
		ast_log(LOG_NOTICE, "Channel %s not claimed within timeout\n",
			ast_channel_name(chan));
	}

	/* Clean up broadcast context */
	stasis_app_broadcast_cleanup(ast_channel_uniqueid(chan));

	return 0;
}

static int load_module(void)
{
	return ast_register_application_xml(app, stasis_broadcast_exec);
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT,
	"Stasis application broadcast",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_stasis,res_stasis_broadcast",
);
