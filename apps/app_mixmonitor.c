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
#include "asterisk/audiohook.h"
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
"asterisk.conf.  Use of StopMixMonitor is required to guarantee\n"
"the audio file is available for processing during dialplan execution.\n\n"
"Valid options:\n"
" a      - Append to the file instead of overwriting it.\n"
" b      - Only save audio to the file while the channel is bridged.\n"
"          Note: Does not include conferences or sounds played to each bridged\n"
"                party.\n"
"          Note: If you utilize this option inside a Local channel, you must\n"
"                 make sure the Local channel is not optimized away. To do this,\n"
"                 be sure to call your Local channel with the '/n' option.\n"
"                 For example: Dial(Local/start@mycontext/n)\n"
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
"Stop recording a call through MixMonitor, and free the recording's file handle.\n"
"";

struct module_symbols *me;

static const char *mixmonitor_spy_type = "MixMonitor";

struct mixmonitor {
	struct ast_audiohook audiohook;
	char *filename;
	char *post_process;
	char *name;
	unsigned int flags;
	struct mixmonitor_ds *mixmonitor_ds;
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

/* This structure is used as a means of making sure that our pointer to
 * the channel we are monitoring remains valid. This is very similar to 
 * what is used in app_chanspy.c.
 */
struct mixmonitor_ds {
	struct ast_channel *chan;
	/* These condition variables are used to be sure that the channel
	 * hangup code completes before the mixmonitor thread attempts to
	 * free this structure. The combination of a bookean flag and a
	 * ast_cond_t ensure that no matter what order the threads run in,
	 * we are guaranteed to never have the waiting thread block forever
	 * in the case that the signaling thread runs first.
	 */
	unsigned int destruction_ok;
	ast_cond_t destruction_condition;
	ast_mutex_t lock;

	/* The filestream is held in the datastore so it can be stopped
	 * immediately during stop_mixmonitor or channel destruction. */
	int fs_quit;
	struct ast_filestream *fs;
	struct ast_audiohook *audiohook;
};

/*!
 * \internal
 * \pre mixmonitor_ds must be locked before calling this function
 */
static void mixmonitor_ds_close_fs(struct mixmonitor_ds *mixmonitor_ds)
{
	if (mixmonitor_ds->fs) {
		ast_closestream(mixmonitor_ds->fs);
		mixmonitor_ds->fs = NULL;
		mixmonitor_ds->fs_quit = 1;
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "MixMonitor close filestream\n");
	}
}

static void mixmonitor_ds_destroy(void *data)
{
	struct mixmonitor_ds *mixmonitor_ds = data;

	ast_mutex_lock(&mixmonitor_ds->lock);
	mixmonitor_ds->audiohook = NULL;
	mixmonitor_ds->chan = NULL;
	mixmonitor_ds->destruction_ok = 1;
	ast_cond_signal(&mixmonitor_ds->destruction_condition);
	ast_mutex_unlock(&mixmonitor_ds->lock);
}

static void mixmonitor_ds_chan_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	struct mixmonitor_ds *mixmonitor_ds = data;

	ast_mutex_lock(&mixmonitor_ds->lock);
	mixmonitor_ds->chan = new_chan;
	ast_mutex_unlock(&mixmonitor_ds->lock);
}

static struct ast_datastore_info mixmonitor_ds_info = {
	.type = "mixmonitor",
	.destroy = mixmonitor_ds_destroy,
	.chan_fixup = mixmonitor_ds_chan_fixup,
};

static void destroy_monitor_audiohook(struct mixmonitor *mixmonitor)
{
	if (mixmonitor->mixmonitor_ds) {
		ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);
		mixmonitor->mixmonitor_ds->audiohook = NULL;
		ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);
	}
	/* kill the audiohook.*/
	ast_audiohook_lock(&mixmonitor->audiohook);
	ast_audiohook_detach(&mixmonitor->audiohook);
	ast_audiohook_unlock(&mixmonitor->audiohook);
	ast_audiohook_destroy(&mixmonitor->audiohook);
}

static int startmon(struct ast_channel *chan, struct ast_audiohook *audiohook) 
{
	struct ast_channel *peer;
	int res;

	if (!chan)
		return -1;

	res = ast_audiohook_attach(chan, audiohook);

	if (!res && ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan)))
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);	

	return res;
}

#define SAMPLES_PER_FRAME 160

static void mixmonitor_free(struct mixmonitor *mixmonitor)
{
	if (mixmonitor) {
		if (mixmonitor->mixmonitor_ds) {
			ast_mutex_destroy(&mixmonitor->mixmonitor_ds->lock);
			ast_cond_destroy(&mixmonitor->mixmonitor_ds->destruction_condition);
			ast_free(mixmonitor->mixmonitor_ds);
		}
		ast_free(mixmonitor);
	}
}

static void *mixmonitor_thread(void *obj) 
{
	struct mixmonitor *mixmonitor = obj;
	struct ast_filestream **fs = NULL;
	unsigned int oflags;
	char *ext;
	char *last_slash;
	int errflag = 0;

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Begin MixMonitor Recording %s\n", mixmonitor->name);

	fs = &mixmonitor->mixmonitor_ds->fs;

	/* The audiohook must enter and exit the loop locked */
	ast_audiohook_lock(&mixmonitor->audiohook);
	while (mixmonitor->audiohook.status == AST_AUDIOHOOK_STATUS_RUNNING && !mixmonitor->mixmonitor_ds->fs_quit) {
		struct ast_frame *fr = NULL;

		if (!(fr = ast_audiohook_read_frame(&mixmonitor->audiohook, SAMPLES_PER_FRAME, AST_AUDIOHOOK_DIRECTION_BOTH, AST_FORMAT_SLINEAR))) {
			ast_audiohook_trigger_wait(&mixmonitor->audiohook);

			if (mixmonitor->audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
				break;
			}
			continue;
		}

		/* audiohook lock is not required for the next block.
		 * Unlock it, but remember to lock it before looping or exiting */
		ast_audiohook_unlock(&mixmonitor->audiohook);

		ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);
		if (!ast_test_flag(mixmonitor, MUXFLAG_BRIDGED) || (mixmonitor->mixmonitor_ds->chan && ast_bridged_channel(mixmonitor->mixmonitor_ds->chan))) {
			ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);
			/* Initialize the file if not already done so */
			if (!*fs && !errflag && !mixmonitor->mixmonitor_ds->fs_quit) {
				oflags = O_CREAT | O_WRONLY;
				oflags |= ast_test_flag(mixmonitor, MUXFLAG_APPEND) ? O_APPEND : O_TRUNC;

				last_slash = strrchr(mixmonitor->filename, '/');
				if ((ext = strrchr(mixmonitor->filename, '.')) && (ext > last_slash))
					*(ext++) = '\0';
				else
					ext = "raw";

				if (!(*fs = ast_writefile(mixmonitor->filename, ext, NULL, oflags, 0, 0644))) {
					ast_log(LOG_ERROR, "Cannot open %s.%s\n", mixmonitor->filename, ext);
					errflag = 1;
				}
			}

			/* Write out the frame(s) */
			if (*fs) {
				struct ast_frame *cur;

				for (cur = fr; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
					ast_writestream(*fs, cur);
				}
			}
		} else {
			ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);
		}

		/* All done! free it. */
		ast_frame_free(fr, 0);

		ast_audiohook_lock(&mixmonitor->audiohook);
	}
	ast_audiohook_unlock(&mixmonitor->audiohook);

	/* Datastore cleanup.  close the filestream and wait for ds destruction */
	ast_mutex_lock(&mixmonitor->mixmonitor_ds->lock);
	mixmonitor_ds_close_fs(mixmonitor->mixmonitor_ds);
	if (!mixmonitor->mixmonitor_ds->destruction_ok) {
		ast_cond_wait(&mixmonitor->mixmonitor_ds->destruction_condition, &mixmonitor->mixmonitor_ds->lock);
	}
	ast_mutex_unlock(&mixmonitor->mixmonitor_ds->lock);

	/* kill the audiohook */
	destroy_monitor_audiohook(mixmonitor);

	if (mixmonitor->post_process) {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_2 "Executing [%s]\n", mixmonitor->post_process);
		ast_safe_system(mixmonitor->post_process);
	}

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "End MixMonitor Recording %s\n", mixmonitor->name);

	mixmonitor_free(mixmonitor);

	return NULL;
}

static int setup_mixmonitor_ds(struct mixmonitor *mixmonitor, struct ast_channel *chan)
{
	struct ast_datastore *datastore = NULL;
	struct mixmonitor_ds *mixmonitor_ds;

	if (!(mixmonitor_ds = ast_calloc(1, sizeof(*mixmonitor_ds)))) {
		return -1;
	}
	
	ast_mutex_init(&mixmonitor_ds->lock);
	ast_cond_init(&mixmonitor_ds->destruction_condition, NULL);

	if (!(datastore = ast_channel_datastore_alloc(&mixmonitor_ds_info, NULL))) {
		ast_mutex_destroy(&mixmonitor_ds->lock);
		ast_cond_destroy(&mixmonitor_ds->destruction_condition);
		ast_free(mixmonitor_ds);
		return -1;
	}

	/* No need to lock mixmonitor_ds since this is still operating in the channel's thread */
	mixmonitor_ds->chan = chan;
	mixmonitor_ds->audiohook = &mixmonitor->audiohook;
	datastore->data = mixmonitor_ds;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	mixmonitor->mixmonitor_ds = mixmonitor_ds;
	return 0;
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

	/* Setup the actual spy before creating our thread */
	if (ast_audiohook_init(&mixmonitor->audiohook, AST_AUDIOHOOK_TYPE_SPY, mixmonitor_spy_type)) {
		mixmonitor_free(mixmonitor);
		return;
	}

	/* Copy over flags and channel name */
	mixmonitor->flags = flags;
	if (setup_mixmonitor_ds(mixmonitor, chan)) {
		mixmonitor_free(mixmonitor);
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

	ast_set_flag(&mixmonitor->audiohook, AST_AUDIOHOOK_TRIGGER_SYNC);
	
	if (readvol)
		mixmonitor->audiohook.options.read_volume = readvol;
	if (writevol)
		mixmonitor->audiohook.options.write_volume = writevol;

	if (startmon(chan, &mixmonitor->audiohook)) {
		ast_log(LOG_WARNING, "Unable to add '%s' spy to channel '%s'\n",
			mixmonitor_spy_type, chan->name);
		/* Since we couldn't add ourselves - bail out! */
		ast_audiohook_destroy(&mixmonitor->audiohook);
		mixmonitor_free(mixmonitor);
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

	if (args.options) {
		char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };

		ast_app_parse_options(mixmonitor_opts, &flags, opts, args.options);

		if (ast_test_flag(&flags, MUXFLAG_READVOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_READVOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the heard volume ('v') option.\n");
			} else if ((sscanf(opts[OPT_ARG_READVOLUME], "%2d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Heard volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_READVOLUME]);
			} else {
				readvol = get_volfactor(x);
			}
		}
		
		if (ast_test_flag(&flags, MUXFLAG_WRITEVOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_WRITEVOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the spoken volume ('V') option.\n");
			} else if ((sscanf(opts[OPT_ARG_WRITEVOLUME], "%2d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Spoken volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_WRITEVOLUME]);
			} else {
				writevol = get_volfactor(x);
			}
		}
		
		if (ast_test_flag(&flags, MUXFLAG_VOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_VOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the combined volume ('W') option.\n");
			} else if ((sscanf(opts[OPT_ARG_VOLUME], "%2d", &x) != 1) || (x < -4) || (x > 4)) {
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
	launch_monitor_thread(chan, args.filename, flags.flags, readvol, writevol, args.post_process);

	ast_module_user_remove(u);

	return 0;
}

static int stop_mixmonitor_exec(struct ast_channel *chan, void *data)
{
	struct ast_datastore *datastore = NULL;

	ast_channel_lock(chan);
	ast_audiohook_detach_source(chan, mixmonitor_spy_type);
	if ((datastore = ast_channel_datastore_find(chan, &mixmonitor_ds_info, NULL))) {
		struct mixmonitor_ds *mixmonitor_ds = datastore->data;

		ast_mutex_lock(&mixmonitor_ds->lock);

		/* closing the filestream here guarantees the file is avaliable to the dialplan
	 	 * after calling StopMixMonitor */
		mixmonitor_ds_close_fs(mixmonitor_ds);

		/* The mixmonitor thread may be waiting on the audiohook trigger.
		 * In order to exit from the mixmonitor loop before waiting on channel
		 * destruction, poke the audiohook trigger. */
		if (mixmonitor_ds->audiohook) {
			ast_audiohook_lock(mixmonitor_ds->audiohook);
			ast_cond_signal(&mixmonitor_ds->audiohook->trigger);
			ast_audiohook_unlock(mixmonitor_ds->audiohook);
			mixmonitor_ds->audiohook = NULL;
		}

		ast_mutex_unlock(&mixmonitor_ds->lock);

		/* Remove the datastore so the monitor thread can exit */
		if (!ast_channel_datastore_remove(chan, datastore)) {
			ast_channel_datastore_free(datastore);
		}
	}
	ast_channel_unlock(chan);

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
		ast_audiohook_detach_source(chan, mixmonitor_spy_type);

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
