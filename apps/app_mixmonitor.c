/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II
 * Copyright (C) 2005 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * Based on app_muxmon.c provided by
 * Anthony Minessale II <anthmct@yahoo.com>
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
 * \brief MixMonitor() - Record a call and mix the audio during the recording
 * \ingroup applications
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Kevin P. Fleming <kpfleming@digium.com>
 *
 * \note Based on app_muxmon.c provided by
 * Anthony Minessale II <anthmct@yahoo.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/paths.h"	/* use ast_config_AST_MONITOR_DIR */
#include "asterisk/file.h"
#include "asterisk/audiohook.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/autochan.h"

/*** DOCUMENTATION
	<application name="MixMonitor" language="en_US">
		<synopsis>
			Record a call and mix the audio during the recording.
		</synopsis>
		<syntax>
			<parameter name="file" required="true" argsep=".">
				<argument name="filename" required="true">
					<para>If <replaceable>filename</replaceable> is an absolute path, uses that path, otherwise
					creates the file in the configured monitoring directory from <filename>asterisk.conf.</filename></para>
				</argument>
				<argument name="extension" required="true" />
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="a">
						<para>Append to the file instead of overwriting it.</para>
					</option>
					<option name="b">
						<para>Only save audio to the file while the channel is bridged.</para>
						<note><para>Does not include conferences or sounds played to each bridged party</para></note>
						<note><para>If you utilize this option inside a Local channel, you must make sure the Local
						channel is not optimized away. To do this, be sure to call your Local channel with the
						<literal>/n</literal> option. For example: Dial(Local/start@mycontext/n)</para></note>
					</option>
					<option name="v">
						<para>Adjust the <emphasis>heard</emphasis> volume by a factor of <replaceable>x</replaceable>
						(range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="V">
						<para>Adjust the <emphasis>spoken</emphasis> volume by a factor
						of <replaceable>x</replaceable> (range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="W">
						<para>Adjust both, <emphasis>heard and spoken</emphasis> volumes by a factor
						of <replaceable>x</replaceable> (range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
				</optionlist>
			</parameter>
			<parameter name="command">
				<para>Will be executed when the recording is over.</para>
				<para>Any strings matching <literal>^{X}</literal> will be unescaped to <variable>X</variable>.</para>
				<para>All variables will be evaluated at the time MixMonitor is called.</para>
			</parameter>
		</syntax>
		<description>
			<para>Records the audio on the current channel to the specified file.</para>
			<variablelist>
				<variable name="MIXMONITOR_FILENAME">
					<para>Will contain the filename used to record.</para>
				</variable>
			</variablelist>	
		</description>
		<see-also>
			<ref type="application">Monitor</ref>
			<ref type="application">StopMixMonitor</ref>
			<ref type="application">PauseMonitor</ref>
			<ref type="application">UnpauseMonitor</ref>
		</see-also>
	</application>
	<application name="StopMixMonitor" language="en_US">
		<synopsis>
			Stop recording a call through MixMonitor.
		</synopsis>
		<syntax />
		<description>
			<para>Stops the audio recording that was started with a call to <literal>MixMonitor()</literal>
			on the current channel.</para>
		</description>
		<see-also>
			<ref type="application">MixMonitor</ref>
		</see-also>
	</application>
		
 ***/

#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0

static const char * const app = "MixMonitor";

static const char * const stop_app = "StopMixMonitor";

static const char * const mixmonitor_spy_type = "MixMonitor";

struct mixmonitor {
	struct ast_audiohook audiohook;
	char *filename;
	char *post_process;
	char *name;
	unsigned int flags;
	struct ast_autochan *autochan;
};

enum mixmonitor_flags {
	MUXFLAG_APPEND = (1 << 1),
	MUXFLAG_BRIDGED = (1 << 2),
	MUXFLAG_VOLUME = (1 << 3),
	MUXFLAG_READVOLUME = (1 << 4),
	MUXFLAG_WRITEVOLUME = (1 << 5),
};

enum mixmonitor_args {
	OPT_ARG_READVOLUME = 0,
	OPT_ARG_WRITEVOLUME,
	OPT_ARG_VOLUME,
	OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(mixmonitor_opts, {
	AST_APP_OPTION('a', MUXFLAG_APPEND),
	AST_APP_OPTION('b', MUXFLAG_BRIDGED),
	AST_APP_OPTION_ARG('v', MUXFLAG_READVOLUME, OPT_ARG_READVOLUME),
	AST_APP_OPTION_ARG('V', MUXFLAG_WRITEVOLUME, OPT_ARG_WRITEVOLUME),
	AST_APP_OPTION_ARG('W', MUXFLAG_VOLUME, OPT_ARG_VOLUME),
});

static int startmon(struct ast_channel *chan, struct ast_audiohook *audiohook) 
{
	struct ast_channel *peer = NULL;
	int res = 0;

	if (!chan)
		return -1;

	ast_audiohook_attach(chan, audiohook);

	if (!res && ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan)))
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);	

	return res;
}

#define SAMPLES_PER_FRAME 160

static void *mixmonitor_thread(void *obj) 
{
	struct mixmonitor *mixmonitor = obj;
	struct ast_filestream *fs = NULL;
	unsigned int oflags;
	char *ext;
	int errflag = 0;

	ast_verb(2, "Begin MixMonitor Recording %s\n", mixmonitor->name);
	
	ast_audiohook_lock(&mixmonitor->audiohook);

	while (mixmonitor->audiohook.status == AST_AUDIOHOOK_STATUS_RUNNING) {
		struct ast_frame *fr = NULL;

		ast_audiohook_trigger_wait(&mixmonitor->audiohook);

		if (mixmonitor->audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING)
			break;

		if (!(fr = ast_audiohook_read_frame(&mixmonitor->audiohook, SAMPLES_PER_FRAME, AST_AUDIOHOOK_DIRECTION_BOTH, AST_FORMAT_SLINEAR)))
			continue;

		if (!ast_test_flag(mixmonitor, MUXFLAG_BRIDGED) || (mixmonitor->autochan->chan && ast_bridged_channel(mixmonitor->autochan->chan))) {
			/* Initialize the file if not already done so */
			if (!fs && !errflag) {
				oflags = O_CREAT | O_WRONLY;
				oflags |= ast_test_flag(mixmonitor, MUXFLAG_APPEND) ? O_APPEND : O_TRUNC;
				
				if ((ext = strrchr(mixmonitor->filename, '.')))
					*(ext++) = '\0';
				else
					ext = "raw";
				
				if (!(fs = ast_writefile(mixmonitor->filename, ext, NULL, oflags, 0, 0666))) {
					ast_log(LOG_ERROR, "Cannot open %s.%s\n", mixmonitor->filename, ext);
					errflag = 1;
				}
			}

			/* Write out the frame(s) */
			if (fs) {
				struct ast_frame *cur;

				for (cur = fr; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
					ast_writestream(fs, cur);
				}
			}
		}
		/* All done! free it. */
		ast_frame_free(fr, 0);

	}

	ast_audiohook_detach(&mixmonitor->audiohook);
	ast_audiohook_unlock(&mixmonitor->audiohook);
	ast_audiohook_destroy(&mixmonitor->audiohook);

	ast_verb(2, "End MixMonitor Recording %s\n", mixmonitor->name);

	if (fs)
		ast_closestream(fs);

	if (mixmonitor->post_process) {
		ast_verb(2, "Executing [%s]\n", mixmonitor->post_process);
		ast_safe_system(mixmonitor->post_process);
	}

	ast_autochan_destroy(mixmonitor->autochan);
	ast_free(mixmonitor);

	return NULL;
}

static void launch_monitor_thread(struct ast_channel *chan, const char *filename, unsigned int flags,
				  int readvol, int writevol, const char *post_process) 
{
	pthread_t thread;
	struct mixmonitor *mixmonitor;
	char postprocess2[1024] = "";
	size_t len;

	len = sizeof(*mixmonitor) + strlen(chan->name) + strlen(filename) + 2;

	postprocess2[0] = 0;
	/* If a post process system command is given attach it to the structure */
	if (!ast_strlen_zero(post_process)) {
		char *p1, *p2;

		p1 = ast_strdupa(post_process);
		for (p2 = p1; *p2 ; p2++) {
			if (*p2 == '^' && *(p2+1) == '{') {
				*p2 = '$';
			}
		}
		pbx_substitute_variables_helper(chan, p1, postprocess2, sizeof(postprocess2) - 1);
		if (!ast_strlen_zero(postprocess2))
			len += strlen(postprocess2) + 1;
	}

	/* Pre-allocate mixmonitor structure and spy */
	if (!(mixmonitor = ast_calloc(1, len))) {
		return;
	}

	/* Copy over flags and channel name */
	mixmonitor->flags = flags;
	if (!(mixmonitor->autochan = ast_autochan_setup(chan))) {
		return;
	}
	mixmonitor->name = (char *) mixmonitor + sizeof(*mixmonitor);
	strcpy(mixmonitor->name, chan->name);
	if (!ast_strlen_zero(postprocess2)) {
		mixmonitor->post_process = mixmonitor->name + strlen(mixmonitor->name) + strlen(filename) + 2;
		strcpy(mixmonitor->post_process, postprocess2);
	}

	mixmonitor->filename = (char *) mixmonitor + sizeof(*mixmonitor) + strlen(chan->name) + 1;
	strcpy(mixmonitor->filename, filename);

	/* Setup the actual spy before creating our thread */
	if (ast_audiohook_init(&mixmonitor->audiohook, AST_AUDIOHOOK_TYPE_SPY, mixmonitor_spy_type)) {
		ast_free(mixmonitor);
		return;
	}

	ast_set_flag(&mixmonitor->audiohook, AST_AUDIOHOOK_TRIGGER_SYNC);

	if (readvol)
		mixmonitor->audiohook.options.read_volume = readvol;
	if (writevol)
		mixmonitor->audiohook.options.write_volume = writevol;

	if (startmon(chan, &mixmonitor->audiohook)) {
		ast_log(LOG_WARNING, "Unable to add '%s' spy to channel '%s'\n",
			mixmonitor_spy_type, chan->name);
		ast_audiohook_destroy(&mixmonitor->audiohook);
		ast_free(mixmonitor);
		return;
	}

	ast_pthread_create_detached_background(&thread, NULL, mixmonitor_thread, mixmonitor);
}

static int mixmonitor_exec(struct ast_channel *chan, const char *data)
{
	int x, readvol = 0, writevol = 0;
	struct ast_flags flags = {0};
	char *parse, *tmp, *slash;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(options);
		AST_APP_ARG(post_process);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MixMonitor requires an argument (filename)\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);
	
	if (ast_strlen_zero(args.filename)) {
		ast_log(LOG_WARNING, "MixMonitor requires an argument (filename)\n");
		return -1;
	}

	if (args.options) {
		char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };

		ast_app_parse_options(mixmonitor_opts, &flags, opts, args.options);

		if (ast_test_flag(&flags, MUXFLAG_READVOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_READVOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the heard volume ('v') option.\n");
			} else if ((sscanf(opts[OPT_ARG_READVOLUME], "%d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Heard volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_READVOLUME]);
			} else {
				readvol = get_volfactor(x);
			}
		}
		
		if (ast_test_flag(&flags, MUXFLAG_WRITEVOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_WRITEVOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the spoken volume ('V') option.\n");
			} else if ((sscanf(opts[OPT_ARG_WRITEVOLUME], "%d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Spoken volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_WRITEVOLUME]);
			} else {
				writevol = get_volfactor(x);
			}
		}
		
		if (ast_test_flag(&flags, MUXFLAG_VOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_VOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the combined volume ('W') option.\n");
			} else if ((sscanf(opts[OPT_ARG_VOLUME], "%d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Combined volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_VOLUME]);
			} else {
				readvol = writevol = get_volfactor(x);
			}
		}
	}

	/* if not provided an absolute path, use the system-configured monitoring directory */
	if (args.filename[0] != '/') {
		char *build;

		build = alloca(strlen(ast_config_AST_MONITOR_DIR) + strlen(args.filename) + 3);
		sprintf(build, "%s/%s", ast_config_AST_MONITOR_DIR, args.filename);
		args.filename = build;
	}

	tmp = ast_strdupa(args.filename);
	if ((slash = strrchr(tmp, '/')))
		*slash = '\0';
	ast_mkdir(tmp, 0777);

	pbx_builtin_setvar_helper(chan, "MIXMONITOR_FILENAME", args.filename);
	launch_monitor_thread(chan, args.filename, flags.flags, readvol, writevol, args.post_process);

	return 0;
}

static int stop_mixmonitor_exec(struct ast_channel *chan, const char *data)
{
	ast_audiohook_detach_source(chan, mixmonitor_spy_type);
	return 0;
}

static char *handle_cli_mixmonitor(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;

	switch (cmd) {
	case CLI_INIT:
		e->command = "mixmonitor {start|stop} {<chan_name>} [args]";
		e->usage =
			"Usage: mixmonitor <start|stop> <chan_name> [args]\n"
			"       The optional arguments are passed to the MixMonitor\n"
			"       application when the 'start' command is used.\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 2);
	}

	if (a->argc < 3)
		return CLI_SHOWUSAGE;

	if (!(chan = ast_channel_get_by_name_prefix(a->argv[2], strlen(a->argv[2])))) {
		ast_cli(a->fd, "No channel matching '%s' found.\n", a->argv[2]);
		/* Technically this is a failure, but we don't want 2 errors printing out */
		return CLI_SUCCESS;
	}

	ast_channel_lock(chan);

	if (!strcasecmp(a->argv[1], "start")) {
		mixmonitor_exec(chan, a->argv[3]);
		ast_channel_unlock(chan);
	} else {
		ast_channel_unlock(chan);
		ast_audiohook_detach_source(chan, mixmonitor_spy_type);
	}

	chan = ast_channel_unref(chan);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_mixmonitor[] = {
	AST_CLI_DEFINE(handle_cli_mixmonitor, "Execute a MixMonitor command")
};

static int unload_module(void)
{
	int res;

	ast_cli_unregister_multiple(cli_mixmonitor, ARRAY_LEN(cli_mixmonitor));
	res = ast_unregister_application(stop_app);
	res |= ast_unregister_application(app);
	
	return res;
}

static int load_module(void)
{
	int res;

	ast_cli_register_multiple(cli_mixmonitor, ARRAY_LEN(cli_mixmonitor));
	res = ast_register_application_xml(app, mixmonitor_exec);
	res |= ast_register_application_xml(stop_app, stop_mixmonitor_exec);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Mixed Audio Monitoring Application");
