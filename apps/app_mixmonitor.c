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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/chanspy.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"
#include "asterisk/options.h"
#include "asterisk/app.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"

#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0

static const char *app = "MixMonitor";
static const char *synopsis = "Record a call and mix the audio during the recording";
static const char *desc = ""
"  MixMonitor(<file>.<ext>[|<options>[|<command>]])\n\n"
"Records the audio on the current channel to the specified file.\n"
"If the filename is an absolute path, uses that path, otherwise\n"
"creates the file in the configured monitoring directory from\n"
"asterisk.conf.\n\n"
"Valid options:\n"
" a      - Append to the file instead of overwriting it.\n"
" b      - Only save audio to the file while the channel is bridged.\n"
"          Note: Does not include conferences or sounds played to each bridged\n"
"                party.\n"
" v(<x>) - Adjust the heard volume by a factor of <x> (range -4 to 4)\n"	
" V(<x>) - Adjust the spoken volume by a factor of <x> (range -4 to 4)\n"	
" W(<x>) - Adjust the both heard and spoken volumes by a factor of <x>\n"
"         (range -4 to 4)\n\n"	
"<command> will be executed when the recording is over\n"
"Any strings matching ^{X} will be unescaped to ${X}.\n"
"All variables will be evaluated at the time MixMonitor is called.\n"
"The variable MIXMONITOR_FILENAME will contain the filename used to record.\n"
"";

static const char *stop_app = "StopMixMonitor";
static const char *stop_synopsis = "Stop recording a call through MixMonitor";
static const char *stop_desc = ""
"  StopMixMonitor()\n\n"
"Stops the audio recording that was started with a call to MixMonitor()\n"
"on the current channel.\n"
"";

struct module_symbols *me;

static const char *mixmonitor_spy_type = "MixMonitor";

struct mixmonitor {
	struct ast_channel_spy spy;
	char *filename;
	char *post_process;
	char *name;
	unsigned int flags;
};

enum {
	MUXFLAG_APPEND = (1 << 1),
	MUXFLAG_BRIDGED = (1 << 2),
	MUXFLAG_VOLUME = (1 << 3),
	MUXFLAG_READVOLUME = (1 << 4),
	MUXFLAG_WRITEVOLUME = (1 << 5),
} mixmonitor_flags;

enum {
	OPT_ARG_READVOLUME = 0,
	OPT_ARG_WRITEVOLUME,
	OPT_ARG_VOLUME,
	OPT_ARG_ARRAY_SIZE,
} mixmonitor_args;

AST_APP_OPTIONS(mixmonitor_opts, {
	AST_APP_OPTION('a', MUXFLAG_APPEND),
	AST_APP_OPTION('b', MUXFLAG_BRIDGED),
	AST_APP_OPTION_ARG('v', MUXFLAG_READVOLUME, OPT_ARG_READVOLUME),
	AST_APP_OPTION_ARG('V', MUXFLAG_WRITEVOLUME, OPT_ARG_WRITEVOLUME),
	AST_APP_OPTION_ARG('W', MUXFLAG_VOLUME, OPT_ARG_VOLUME),
});

static int startmon(struct ast_channel *chan, struct ast_channel_spy *spy) 
{
	struct ast_channel *peer;
	int res;

	if (!chan)
		return -1;

	ast_channel_lock(chan);
	res = ast_channel_spy_add(chan, spy);
	ast_channel_unlock(chan);

	if (!res && ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan)))
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);	

	return res;
}

#define SAMPLES_PER_FRAME 160

static void *mixmonitor_thread(void *obj) 
{
	struct mixmonitor *mixmonitor = obj;
	struct ast_frame *f = NULL;
	struct ast_filestream *fs = NULL;
	unsigned int oflags;
	char *ext;
	int errflag = 0;

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Begin MixMonitor Recording %s\n", mixmonitor->name);
	
	ast_mutex_lock(&mixmonitor->spy.lock);

	while (mixmonitor->spy.chan) {
		struct ast_frame *next;
		int write;

		ast_channel_spy_trigger_wait(&mixmonitor->spy);
		
		if (!mixmonitor->spy.chan || mixmonitor->spy.status != CHANSPY_RUNNING)
			break;
		
		while (1) {
			if (!(f = ast_channel_spy_read_frame(&mixmonitor->spy, SAMPLES_PER_FRAME)))
				break;

			write = (!ast_test_flag(mixmonitor, MUXFLAG_BRIDGED) ||
				 ast_bridged_channel(mixmonitor->spy.chan));

			/* it is possible for ast_channel_spy_read_frame() to return a chain
			   of frames if a queue flush was necessary, so process them
			*/
			for (; f; f = next) {
				next = AST_LIST_NEXT(f, frame_list);
				if (write && errflag == 0) {
					if (!fs) {
						/* Determine creation flags and filename plus extension for filestream */
						oflags = O_CREAT | O_WRONLY;
						oflags |= ast_test_flag(mixmonitor, MUXFLAG_APPEND) ? O_APPEND : O_TRUNC;

						if ((ext = strrchr(mixmonitor->filename, '.')))
							*(ext++) = '\0';
						else
							ext = "raw";

						/* Move onto actually creating the filestream */
						if (!(fs = ast_writefile(mixmonitor->filename, ext, NULL, oflags, 0, 0644))) {
							ast_log(LOG_ERROR, "Cannot open %s.%s\n", mixmonitor->filename, ext);
							errflag = 1;
						}

					}
					if (fs)
						ast_writestream(fs, f);
				}
				ast_frame_free(f, 0);
			}
		}
	}

	ast_mutex_unlock(&mixmonitor->spy.lock);

	ast_channel_spy_free(&mixmonitor->spy);
	
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "End MixMonitor Recording %s\n", mixmonitor->name);

	if (mixmonitor->post_process) {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_2 "Executing [%s]\n", mixmonitor->post_process);
		ast_safe_system(mixmonitor->post_process);
	}
		
	if (fs)
		ast_closestream(fs);

	free(mixmonitor);


	return NULL;
}

static void launch_monitor_thread(struct ast_channel *chan, const char *filename, unsigned int flags,
				  int readvol, int writevol, const char *post_process) 
{
	pthread_attr_t attr;
	pthread_t thread;
	struct mixmonitor *mixmonitor;
	char postprocess2[1024] = "";
	size_t len;

	len = sizeof(*mixmonitor) + strlen(chan->name) + strlen(filename) + 2;

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
	if (!(mixmonitor = calloc(1, len))) {
		return;
	}

	/* Copy over flags and channel name */
	mixmonitor->flags = flags;
	mixmonitor->name = (char *) mixmonitor + sizeof(*mixmonitor);
	strcpy(mixmonitor->name, chan->name);
	if (!ast_strlen_zero(postprocess2)) {
		mixmonitor->post_process = mixmonitor->name + strlen(mixmonitor->name) + strlen(filename) + 2;
		strcpy(mixmonitor->post_process, postprocess2);
	}

	mixmonitor->filename = (char *) mixmonitor + sizeof(*mixmonitor) + strlen(chan->name) + 1;
	strcpy(mixmonitor->filename, filename);

	/* Setup the actual spy before creating our thread */
	ast_set_flag(&mixmonitor->spy, CHANSPY_FORMAT_AUDIO);
	ast_set_flag(&mixmonitor->spy, CHANSPY_MIXAUDIO);
	mixmonitor->spy.type = mixmonitor_spy_type;
	mixmonitor->spy.status = CHANSPY_RUNNING;
	mixmonitor->spy.read_queue.format = AST_FORMAT_SLINEAR;
	mixmonitor->spy.write_queue.format = AST_FORMAT_SLINEAR;
	if (readvol) {
		ast_set_flag(&mixmonitor->spy, CHANSPY_READ_VOLADJUST);
		mixmonitor->spy.read_vol_adjustment = readvol;
	}
	if (writevol) {
		ast_set_flag(&mixmonitor->spy, CHANSPY_WRITE_VOLADJUST);
		mixmonitor->spy.write_vol_adjustment = writevol;
	}
	ast_mutex_init(&mixmonitor->spy.lock);

	if (startmon(chan, &mixmonitor->spy)) {
		ast_log(LOG_WARNING, "Unable to add '%s' spy to channel '%s'\n",
			mixmonitor->spy.type, chan->name);
		/* Since we couldn't add ourselves - bail out! */
		ast_mutex_destroy(&mixmonitor->spy.lock);
		free(mixmonitor);
		return;
	}

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ast_pthread_create_background(&thread, &attr, mixmonitor_thread, mixmonitor);
	pthread_attr_destroy(&attr);

}

static int mixmonitor_exec(struct ast_channel *chan, void *data)
{
	int x, readvol = 0, writevol = 0;
	struct ast_module_user *u;
	struct ast_flags flags = {0};
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(options);
		AST_APP_ARG(post_process);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "MixMonitor requires an argument (filename)\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);
	
	if (ast_strlen_zero(args.filename)) {
		ast_log(LOG_WARNING, "MixMonitor requires an argument (filename)\n");
		ast_module_user_remove(u);
		return -1;
	}

	if (args.argc > 1 && args.options) {
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

	pbx_builtin_setvar_helper(chan, "MIXMONITOR_FILENAME", args.filename);
	launch_monitor_thread(chan, args.filename, flags.flags, readvol, writevol, args.argc > 2 ? args.post_process : "");

	ast_module_user_remove(u);

	return 0;
}

static int stop_mixmonitor_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;

	u = ast_module_user_add(chan);

	ast_channel_lock(chan);
	ast_channel_spy_stop_by_type(chan, mixmonitor_spy_type);
	ast_channel_unlock(chan);

	ast_module_user_remove(u);

	return 0;
}

static int mixmonitor_cli(int fd, int argc, char **argv) 
{
	struct ast_channel *chan;

	if (argc < 3)
		return RESULT_SHOWUSAGE;

	if (!(chan = ast_get_channel_by_name_prefix_locked(argv[2], strlen(argv[2])))) {
		ast_cli(fd, "No channel matching '%s' found.\n", argv[2]);
		return RESULT_SUCCESS;
	}

	if (!strcasecmp(argv[1], "start"))
		mixmonitor_exec(chan, argv[3]);
	else if (!strcasecmp(argv[1], "stop"))
		ast_channel_spy_stop_by_type(chan, mixmonitor_spy_type);

	ast_channel_unlock(chan);

	return RESULT_SUCCESS;
}

static char *complete_mixmonitor_cli(const char *line, const char *word, int pos, int state)
{
	return ast_complete_channels(line, word, pos, state, 2);
}

static struct ast_cli_entry cli_mixmonitor[] = {
	{ { "mixmonitor", NULL, NULL },
	mixmonitor_cli, "Execute a MixMonitor command.",
	"mixmonitor <start|stop> <chan_name> [args]\n\n"
	"The optional arguments are passed to the\n"
	"MixMonitor application when the 'start' command is used.\n",
	complete_mixmonitor_cli },
};

static int unload_module(void)
{
	int res;

	ast_cli_unregister_multiple(cli_mixmonitor, sizeof(cli_mixmonitor) / sizeof(struct ast_cli_entry));
	res = ast_unregister_application(stop_app);
	res |= ast_unregister_application(app);
	
	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;

	ast_cli_register_multiple(cli_mixmonitor, sizeof(cli_mixmonitor) / sizeof(struct ast_cli_entry));
	res = ast_register_application(app, mixmonitor_exec, synopsis, desc);
	res |= ast_register_application(stop_app, stop_mixmonitor_exec, stop_synopsis, stop_desc);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Mixed Audio Monitoring Application");
