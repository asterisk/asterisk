/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2004 - 2006 Digium, Inc.  All rights reserved.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This code is released under the GNU General Public License
 * version 2.0.  See LICENSE for more information.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 *
 * \brief page() - Paging application
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<use type="module">app_confbridge</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/chanvars.h"
#include "asterisk/utils.h"
#include "asterisk/devicestate.h"
#include "asterisk/dial.h"

/*** DOCUMENTATION
	<application name="Page" language="en_US">
		<synopsis>
			Page series of phones
		</synopsis>
		<syntax>
			<parameter name="Technology/Resource" required="true" argsep="&amp;">
				<argument name="Technology/Resource" required="true">
					<para>Specification of the device(s) to dial. These must be in the format of
					<literal>Technology/Resource</literal>, where <replaceable>Technology</replaceable>
					represents a particular channel driver, and <replaceable>Resource</replaceable> represents a resource
					available to that particular channel driver.</para>
				</argument>
				<argument name="Technology2/Resource2" multiple="true">
					<para>Optional extra devices to dial in parallel</para>
					<para>If you need more than one, enter them as Technology2/Resource2&amp;
					Technology3/Resourse3&amp;.....</para>
				</argument>
			</parameter>
			<parameter name="options">
				<optionlist>
				<option name="b" argsep="^">
					<para>Before initiating an outgoing call, Gosub to the specified
					location using the newly created channel.  The Gosub will be
					executed for each destination channel.</para>
					<argument name="context" required="false" />
					<argument name="exten" required="false" />
					<argument name="priority" required="true" hasparams="optional" argsep="^">
						<argument name="arg1" multiple="true" required="true" />
						<argument name="argN" />
					</argument>
				</option>
				<option name="B" argsep="^">
					<para>Before initiating the outgoing call(s), Gosub to the specified
					location using the current channel.</para>
					<argument name="context" required="false" />
					<argument name="exten" required="false" />
					<argument name="priority" required="true" hasparams="optional" argsep="^">
						<argument name="arg1" multiple="true" required="true" />
						<argument name="argN" />
					</argument>
				</option>
					<option name="d">
						<para>Full duplex audio</para>
					</option>
					<option name="i">
						<para>Ignore attempts to forward the call</para>
					</option>
					<option name="q">
						<para>Quiet, do not play beep to caller</para>
					</option>
					<option name="r">
						<para>Record the page into a file (<literal>CONFBRIDGE(bridge,record_conference)</literal>)</para>
					</option>
					<option name="s">
						<para>Only dial a channel if its device state says that it is <literal>NOT_INUSE</literal></para>
					</option>
					<option name="A">
						<argument name="x" required="true">
							<para>The announcement to playback to all devices</para>
						</argument>
						<para>Play an announcement to all paged participants</para>
					</option>
					<option name="n">
						<para>Do not play announcement to caller (alters <literal>A(x)</literal> behavior)</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="timeout">
				<para>Specify the length of time that the system will attempt to connect a call.
				After this duration, any page calls that have not been answered will be hung up by the
				system.</para>
			</parameter>
		</syntax>
		<description>
			<para>Places outbound calls to the given <replaceable>technology</replaceable>/<replaceable>resource</replaceable>
			and dumps them into a conference bridge as muted participants. The original
			caller is dumped into the conference as a speaker and the room is
			destroyed when the original caller leaves.</para>
		</description>
		<see-also>
			<ref type="application">ConfBridge</ref>
		</see-also>
	</application>
 ***/
static const char * const app_page= "Page";

enum page_opt_flags {
	PAGE_DUPLEX = (1 << 0),
	PAGE_QUIET = (1 << 1),
	PAGE_RECORD = (1 << 2),
	PAGE_SKIP = (1 << 3),
	PAGE_IGNORE_FORWARDS = (1 << 4),
	PAGE_ANNOUNCE = (1 << 5),
	PAGE_NOCALLERANNOUNCE = (1 << 6),
	PAGE_PREDIAL_CALLEE = (1 << 7),
	PAGE_PREDIAL_CALLER = (1 << 8),
};

enum {
	OPT_ARG_ANNOUNCE = 0,
	OPT_ARG_PREDIAL_CALLEE = 1,
	OPT_ARG_PREDIAL_CALLER = 2,
	OPT_ARG_ARRAY_SIZE = 3,
};

AST_APP_OPTIONS(page_opts, {
	AST_APP_OPTION_ARG('b', PAGE_PREDIAL_CALLEE, OPT_ARG_PREDIAL_CALLEE),
	AST_APP_OPTION_ARG('B', PAGE_PREDIAL_CALLER, OPT_ARG_PREDIAL_CALLER),
	AST_APP_OPTION('d', PAGE_DUPLEX),
	AST_APP_OPTION('q', PAGE_QUIET),
	AST_APP_OPTION('r', PAGE_RECORD),
	AST_APP_OPTION('s', PAGE_SKIP),
	AST_APP_OPTION('i', PAGE_IGNORE_FORWARDS),
	AST_APP_OPTION_ARG('A', PAGE_ANNOUNCE, OPT_ARG_ANNOUNCE),
	AST_APP_OPTION('n', PAGE_NOCALLERANNOUNCE),
});

/* We use this structure as a way to pass this to all dialed channels */
struct page_options {
	char *opts[OPT_ARG_ARRAY_SIZE];
	struct ast_flags flags;
};

/*!
 * \internal
 * \brief Setup the page bridge profile.
 *
 * \param chan Setup bridge profile on this channel.
 * \param options Options to setup bridge profile.
 *
 * \return Nothing
 */
static void setup_profile_bridge(struct ast_channel *chan, struct page_options *options)
{
	/* Use default_bridge as a starting point */
	ast_func_write(chan, "CONFBRIDGE(bridge,template)", "");
	if (ast_test_flag(&options->flags, PAGE_RECORD)) {
		ast_func_write(chan, "CONFBRIDGE(bridge,record_conference)", "yes");
	}
}

/*!
 * \internal
 * \brief Setup the paged user profile.
 *
 * \param chan Setup user profile on this channel.
 * \param options Options to setup paged user profile.
 *
 * \return Nothing
 */
static void setup_profile_paged(struct ast_channel *chan, struct page_options *options)
{
	/* Use default_user as a starting point */
	ast_func_write(chan, "CONFBRIDGE(user,template)", "");
	ast_func_write(chan, "CONFBRIDGE(user,quiet)", "yes");
	ast_func_write(chan, "CONFBRIDGE(user,end_marked)", "yes");
	if (!ast_test_flag(&options->flags, PAGE_DUPLEX)) {
		ast_func_write(chan, "CONFBRIDGE(user,startmuted)", "yes");
	}
	if (ast_test_flag(&options->flags, PAGE_ANNOUNCE)
		&& !ast_strlen_zero(options->opts[OPT_ARG_ANNOUNCE])) {
		ast_func_write(chan, "CONFBRIDGE(user,announcement)", options->opts[OPT_ARG_ANNOUNCE]);
	}
}

/*!
 * \internal
 * \brief Setup the caller user profile.
 *
 * \param chan Setup user profile on this channel.
 * \param options Options to setup caller user profile.
 *
 * \return Nothing
 */
static void setup_profile_caller(struct ast_channel *chan, struct page_options *options)
{
	/* Use default_user as a starting point if not already setup. */
	ast_func_write(chan, "CONFBRIDGE(user,template)", "");
	ast_func_write(chan, "CONFBRIDGE(user,quiet)", "yes");
	ast_func_write(chan, "CONFBRIDGE(user,marked)", "yes");
	if (!ast_test_flag(&options->flags, PAGE_NOCALLERANNOUNCE)
		&& ast_test_flag(&options->flags, PAGE_ANNOUNCE)
		&& !ast_strlen_zero(options->opts[OPT_ARG_ANNOUNCE])) {
		ast_func_write(chan, "CONFBRIDGE(user,announcement)", options->opts[OPT_ARG_ANNOUNCE]);
	}
}

static void page_state_callback(struct ast_dial *dial)
{
	struct ast_channel *chan;
	struct page_options *options;

	if (ast_dial_state(dial) != AST_DIAL_RESULT_ANSWERED ||
	    !(chan = ast_dial_answered(dial)) ||
	    !(options = ast_dial_get_user_data(dial))) {
		return;
	}

	setup_profile_bridge(chan, options);
	setup_profile_paged(chan, options);
}

static int page_exec(struct ast_channel *chan, const char *data)
{
	char *tech, *resource, *tmp;
	char confbridgeopts[128], originator[AST_CHANNEL_NAME];
	struct page_options options = { { 0, }, { 0, } };
	unsigned int confid = ast_random();
	struct ast_app *app;
	int res = 0, pos = 0, i = 0;
	struct ast_dial **dial_list;
	unsigned int num_dials;
	int timeout = 0;
	char *parse;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(devices);
		AST_APP_ARG(options);
		AST_APP_ARG(timeout);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "This application requires at least one argument (destination(s) to page)\n");
		return -1;
	}

	if (!(app = pbx_findapp("ConfBridge"))) {
		ast_log(LOG_WARNING, "There is no ConfBridge application available!\n");
		return -1;
	};

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	ast_copy_string(originator, ast_channel_name(chan), sizeof(originator));
	if ((tmp = strchr(originator, '-'))) {
		*tmp = '\0';
	}

	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(page_opts, &options.flags, options.opts, args.options);
	}

	if (!ast_strlen_zero(args.timeout)) {
		timeout = atoi(args.timeout);
	}

	snprintf(confbridgeopts, sizeof(confbridgeopts), "ConfBridge,%u", confid);

	/* Count number of extensions in list by number of ampersands + 1 */
	num_dials = 1;
	tmp = args.devices;
	while (*tmp) {
		if (*tmp == '&') {
			num_dials++;
		}
		tmp++;
	}

	if (!(dial_list = ast_calloc(num_dials, sizeof(struct ast_dial *)))) {
		ast_log(LOG_ERROR, "Can't allocate %ld bytes for dial list\n", (long)(sizeof(struct ast_dial *) * num_dials));
		return -1;
	}

	if (ast_test_flag(&options.flags, PAGE_PREDIAL_CALLER)
		&& !ast_strlen_zero(options.opts[OPT_ARG_PREDIAL_CALLER])) {
		ast_replace_subargument_delimiter(options.opts[OPT_ARG_PREDIAL_CALLER]);
		ast_app_exec_sub(NULL, chan, options.opts[OPT_ARG_PREDIAL_CALLER], 0);
	}

	/* Go through parsing/calling each device */
	while ((tech = strsep(&args.devices, "&"))) {
		int state = 0;
		struct ast_dial *dial = NULL;

		/* don't call the originating device */
		if (!strcasecmp(tech, originator))
			continue;

		/* If no resource is available, continue on */
		if (!(resource = strchr(tech, '/'))) {
			ast_log(LOG_WARNING, "Incomplete destination '%s' supplied.\n", tech);
			continue;
		}

		/* Ensure device is not in use if skip option is enabled */
		if (ast_test_flag(&options.flags, PAGE_SKIP)) {
			state = ast_device_state(tech);
			if (state == AST_DEVICE_UNKNOWN) {
				ast_log(LOG_WARNING, "Destination '%s' has device state '%s'. Paging anyway.\n", tech, ast_devstate2str(state));
			} else if (state != AST_DEVICE_NOT_INUSE) {
				ast_log(LOG_WARNING, "Destination '%s' has device state '%s'.\n", tech, ast_devstate2str(state));
				continue;
			}
		}

		*resource++ = '\0';

		/* Create a dialing structure */
		if (!(dial = ast_dial_create())) {
			ast_log(LOG_WARNING, "Failed to create dialing structure.\n");
			continue;
		}

		/* Append technology and resource */
		if (ast_dial_append(dial, tech, resource, NULL) == -1) {
			ast_log(LOG_ERROR, "Failed to add %s to outbound dial\n", tech);
			ast_dial_destroy(dial);
			continue;
		}

		/* Set ANSWER_EXEC as global option */
		ast_dial_option_global_enable(dial, AST_DIAL_OPTION_ANSWER_EXEC, confbridgeopts);

		if (ast_test_flag(&options.flags, PAGE_PREDIAL_CALLEE)
			&& !ast_strlen_zero(options.opts[OPT_ARG_PREDIAL_CALLEE])) {
			ast_dial_option_global_enable(dial, AST_DIAL_OPTION_PREDIAL, options.opts[OPT_ARG_PREDIAL_CALLEE]);
		}

		if (timeout) {
			ast_dial_set_global_timeout(dial, timeout * 1000);
		}

		if (ast_test_flag(&options.flags, PAGE_IGNORE_FORWARDS)) {
			ast_dial_option_global_enable(dial, AST_DIAL_OPTION_DISABLE_CALL_FORWARDING, NULL);
		}

		ast_dial_set_state_callback(dial, &page_state_callback);
		ast_dial_set_user_data(dial, &options);

		/* Run this dial in async mode */
		ast_dial_run(dial, chan, 1);

		/* Put in our dialing array */
		dial_list[pos++] = dial;
	}

	if (!ast_test_flag(&options.flags, PAGE_QUIET)) {
		res = ast_streamfile(chan, "beep", ast_channel_language(chan));
		if (!res)
			res = ast_waitstream(chan, "");
	}

	if (!res) {
		setup_profile_bridge(chan, &options);
		setup_profile_caller(chan, &options);

		snprintf(confbridgeopts, sizeof(confbridgeopts), "%u", confid);
		pbx_exec(chan, app, confbridgeopts);
	}
	ao2_ref(app, -1);

	/* Go through each dial attempt cancelling, joining, and destroying */
	for (i = 0; i < pos; i++) {
		struct ast_dial *dial = dial_list[i];

		/* We have to wait for the async thread to exit as it's possible ConfBridge won't throw them out immediately */
		ast_dial_join(dial);

		/* Hangup all channels */
		ast_dial_hangup(dial);

		/* Destroy dialing structure */
		ast_dial_destroy(dial);
	}

	ast_free(dial_list);

	return -1;
}

static int load_module(void)
{
	return ast_register_application_xml(app_page, page_exec);
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Page Multiple Phones");

