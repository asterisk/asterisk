/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II
 * Copyright (C) 2005, Digium, Inc.
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
 * \brief muxmon() - record a call natively
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"
#include "asterisk/options.h"
#include "asterisk/app.h"
#include "asterisk/linkedlists.h"

#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0

static const char *tdesc = "Mixed Audio Monitoring Application";
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
"          Note: does not include conferences.\n"
" v(<x>) - Adjust the heard volume by a factor of <x> (range -4 to 4)\n"	
" V(<x>) - Adjust the spoken volume by a factor of <x> (range -4 to 4)\n"	
" W(<x>) - Adjust the both heard and spoken volumes by a factor of <x>\n"
"         (range -4 to 4)\n\n"	
"<command> will be executed when the recording is over\n"
"Any strings matching ^{X} will be unescaped to ${X} and \n"
"all variables will be evaluated at that time.\n"
"The variable MIXMONITOR_FILENAME will contain the filename used to record.\n"
"";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static const char *mixmonitor_spy_type = "MixMonitor";

struct mixmonitor {
	struct ast_channel *chan;
	char *filename;
	char *post_process;
	unsigned int flags;
	int readvol;
	int writevol;
};

enum {
    MUXFLAG_APPEND = (1 << 1),
    MUXFLAG_BRIDGED = (1 << 2),
    MUXFLAG_VOLUME = (1 << 3),
    MUXFLAG_READVOLUME = (1 << 4),
    MUXFLAG_WRITEVOLUME = (1 << 5),
} mixmonitor_flags;

AST_DECLARE_OPTIONS(mixmonitor_opts,{
	['a'] = { MUXFLAG_APPEND },
	['b'] = { MUXFLAG_BRIDGED },
	['v'] = { MUXFLAG_READVOLUME, 1 },
	['V'] = { MUXFLAG_WRITEVOLUME, 2 },
	['W'] = { MUXFLAG_VOLUME, 3 },
});

static void stopmon(struct ast_channel *chan, struct ast_channel_spy *spy) 
{
	/* If our status has changed, then the channel we're spying on is gone....
	   DON'T TOUCH IT!!!  RUN AWAY!!! */
	if (spy->status != CHANSPY_RUNNING)
		return;

	if (!chan)
		return;

	ast_mutex_lock(&chan->lock);
	ast_channel_spy_remove(chan, spy);
	ast_mutex_unlock(&chan->lock);
}

static int startmon(struct ast_channel *chan, struct ast_channel_spy *spy) 
{
	struct ast_channel *peer;
	int res;

	if (!chan)
		return -1;

	ast_mutex_lock(&chan->lock);
	res = ast_channel_spy_add(chan, spy);
	ast_mutex_unlock(&chan->lock);
		
	if (!res && ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan)))
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);	

	return res;
}

#define SAMPLES_PER_FRAME 160

static void *mixmonitor_thread(void *obj) 
{
	struct mixmonitor *mixmonitor = obj;
	struct ast_channel_spy spy;
	struct ast_filestream *fs = NULL;
	char *ext, *name;
	unsigned int oflags;
	struct ast_frame *f;
	char post_process[1024] = "";
	
	STANDARD_INCREMENT_USECOUNT;

	name = ast_strdupa(mixmonitor->chan->name);

	oflags = O_CREAT|O_WRONLY;
	oflags |= ast_test_flag(mixmonitor, MUXFLAG_APPEND) ? O_APPEND : O_TRUNC;
		
	if ((ext = strchr(mixmonitor->filename, '.'))) {
		*(ext++) = '\0';
	} else {
		ext = "raw";
	}

	fs = ast_writefile(mixmonitor->filename, ext, NULL, oflags, 0, 0644);
	if (!fs) {
		ast_log(LOG_ERROR, "Cannot open %s.%s\n", mixmonitor->filename, ext);
		goto out;
	}

	if (ast_test_flag(mixmonitor, MUXFLAG_APPEND))
		ast_seekstream(fs, 0, SEEK_END);
	
	memset(&spy, 0, sizeof(spy));
	ast_set_flag(&spy, CHANSPY_FORMAT_AUDIO);
	ast_set_flag(&spy, CHANSPY_MIXAUDIO);
	spy.type = mixmonitor_spy_type;
	spy.status = CHANSPY_RUNNING;
	spy.read_queue.format = AST_FORMAT_SLINEAR;
	spy.write_queue.format = AST_FORMAT_SLINEAR;
	if (mixmonitor->readvol) {
		ast_set_flag(&spy, CHANSPY_READ_VOLADJUST);
		spy.read_vol_adjustment = mixmonitor->readvol;
	}
	if (mixmonitor->writevol) {
		ast_set_flag(&spy, CHANSPY_WRITE_VOLADJUST);
		spy.write_vol_adjustment = mixmonitor->writevol;
	}
	ast_mutex_init(&spy.lock);

	if (startmon(mixmonitor->chan, &spy)) {
		ast_log(LOG_WARNING, "Unable to add '%s' spy to channel '%s'\n",
			spy.type, mixmonitor->chan->name);
		goto out2;
	}

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Begin MixMonitor Recording %s\n", name);
	
	while (1) {
		struct ast_frame *next;
		int write;

		ast_mutex_lock(&spy.lock);

		ast_channel_spy_trigger_wait(&spy);
		
		if (ast_check_hangup(mixmonitor->chan) || spy.status != CHANSPY_RUNNING) {
			ast_mutex_unlock(&spy.lock);
			break;
		}
		
		while (1) {
			if (!(f = ast_channel_spy_read_frame(&spy, SAMPLES_PER_FRAME)))
				break;

			write = (!ast_test_flag(mixmonitor, MUXFLAG_BRIDGED) ||
				 ast_bridged_channel(mixmonitor->chan));

			/* it is possible for ast_channel_spy_read_frame() to return a chain
			   of frames if a queue flush was necessary, so process them
			*/
			for (; f; f = next) {
				next = f->next;
				if (write)
					ast_writestream(fs, f);
				ast_frfree(f);
			}
		}

		ast_mutex_unlock(&spy.lock);
	}
	
	if (mixmonitor->post_process) {
		char *p;

		for (p = mixmonitor->post_process; *p ; p++) {
			if (*p == '^' && *(p+1) == '{') {
				*p = '$';
			}
		}
		pbx_substitute_variables_helper(mixmonitor->chan, mixmonitor->post_process, post_process, sizeof(post_process) - 1);
	}

	stopmon(mixmonitor->chan, &spy);

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "End MixMonitor Recording %s\n", name);

	if (!ast_strlen_zero(post_process)) {
		if (option_verbose > 2)
			ast_verbose(VERBOSE_PREFIX_2 "Executing [%s]\n", post_process);
		ast_safe_system(post_process);
	}

out2:
	ast_mutex_destroy(&spy.lock);

	if (fs)
		ast_closestream(fs);

out:
	free(mixmonitor);

	STANDARD_DECREMENT_USECOUNT;

	return NULL;
}

static void launch_monitor_thread(struct ast_channel *chan, const char *filename, unsigned int flags,
				  int readvol, int writevol, const char *post_process) 
{
	pthread_attr_t attr;
	pthread_t thread;
	struct mixmonitor *mixmonitor;
	int len;

	len = sizeof(*mixmonitor) + strlen(filename) + 1;
	if (post_process && !ast_strlen_zero(post_process))
		len += strlen(post_process) + 1;

	if (!(mixmonitor = calloc(1, len))) {
		ast_log(LOG_ERROR, "Memory Error!\n");
		return;
	}

	mixmonitor->chan = chan;
	mixmonitor->filename = (char *) mixmonitor + sizeof(*mixmonitor);
	strcpy(mixmonitor->filename, filename);
	if (post_process && !ast_strlen_zero(post_process)) {
		mixmonitor->post_process = mixmonitor->filename + strlen(filename) + 1;
		strcpy(mixmonitor->post_process, post_process);
	}
	mixmonitor->readvol = readvol;
	mixmonitor->writevol = writevol;
	mixmonitor->flags = flags;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ast_pthread_create(&thread, &attr, mixmonitor_thread, mixmonitor);
	pthread_attr_destroy(&attr);
}

static int mixmonitor_exec(struct ast_channel *chan, void *data)
{
	int x, readvol = 0, writevol = 0;
	struct localuser *u;
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

	LOCAL_USER_ADD(u);

	if (!(parse = ast_strdupa(data))) {
		ast_log(LOG_WARNING, "Memory Error!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);
	
	if (ast_strlen_zero(args.filename)) {
		ast_log(LOG_WARNING, "Muxmon requires an argument (filename)\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (args.options) {
		char *opts[3] = { NULL, };

		ast_parseoptions(mixmonitor_opts, &flags, opts, args.options);

		if (ast_test_flag(&flags, MUXFLAG_READVOLUME)) {
			if (!opts[0] || ast_strlen_zero(opts[0])) {
				ast_log(LOG_WARNING, "No volume level was provided for the heard volume ('v') option.\n");
			} else if ((sscanf(opts[0], "%d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Heard volume must be a number between -4 and 4, not '%s'\n", opts[0]);
			} else {
				readvol = get_volfactor(x);
			}
		}
		
		if (ast_test_flag(&flags, MUXFLAG_WRITEVOLUME)) {
			if (!opts[1] || ast_strlen_zero(opts[1])) {
				ast_log(LOG_WARNING, "No volume level was provided for the spoken volume ('V') option.\n");
			} else if ((sscanf(opts[1], "%d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Spoken volume must be a number between -4 and 4, not '%s'\n", opts[1]);
			} else {
				writevol = get_volfactor(x);
			}
		}
		
		if (ast_test_flag(&flags, MUXFLAG_VOLUME)) {
			if (!opts[2] || ast_strlen_zero(opts[2])) {
				ast_log(LOG_WARNING, "No volume level was provided for the combined volume ('W') option.\n");
			} else if ((sscanf(opts[2], "%d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Combined volume must be a number between -4 and 4, not '%s'\n", opts[2]);
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

	LOCAL_USER_REMOVE(u);

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

	ast_mutex_unlock(&chan->lock);

	return RESULT_SUCCESS;
}


static struct ast_cli_entry cli_mixmonitor = {
	{ "mixmonitor", NULL, NULL },
	mixmonitor_cli, 
	"Execute a MixMonitor command",
	"mixmonitor <start|stop> <chan_name> [<args>]"
};


int unload_module(void)
{
	int res;

	res = ast_cli_unregister(&cli_mixmonitor);
	res |= ast_unregister_application(app);
	
	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;

	res = ast_cli_register(&cli_mixmonitor);
	res |= ast_register_application(app, mixmonitor_exec, synopsis, desc);

	return res;
}

char *description(void)
{
	return (char *) tdesc;
}

int usecount(void)
{
	int res;

	STANDARD_USECOUNT(res);

	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
