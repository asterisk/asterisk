/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * muxmon Application For Asterisk
 *
 * Copyright (C) 2005, Anthony Minessale II
 *
 * Anthony Minessale II <anthmct@yahoo.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <asterisk/cli.h>
#include <asterisk/options.h>
#include <asterisk/app.h>
#include <asterisk/translate.h>
#include <asterisk/slinfactory.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0
#define minmax(x,y) x ? (x > y) ? y : ((x < (y * -1)) ? (y * -1) : x) : 0 

static char *tdesc = "Native Channel Monitoring Module";
static char *app = "MuxMon";
static char *synopsis = "Record A Call Natively";
static char *desc = ""
"  MuxMon(<file>.<ext>[|<options>[|<command>]])\n\n"
"Records The audio on the current channel to the specified file.\n\n"
"Valid Options:\n"
" b    - Only save audio to the file while the channel is bridged. Note: does\n"
"        not include conferences\n"
" a    - Append to the file instead of overwriting it.\n"
" v(<x>) - Adjust the heard volume by a factor of <x> (range -4 to 4)\n"	
" V(<x>) - Adjust the spoken volume by a factor of <x> (range -4 to 4)\n"	
" W(<x>) - Adjust the both heard and spoken volumes by a factor of <x>\n"
"         (range -4 to 4)\n\n"	
"<command> will be executed when the recording is over\n"
"Any strings matching ^{X} will be unescaped to ${X} and \n"
"all variables will be evaluated at that time.\n"
"The variable MUXMON_FILENAME will contain the filename used to record.\n"
"";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

struct muxmon {
	struct ast_channel *chan;
	char *filename;
	char *post_process;
	unsigned int flags;
	int readvol;
	int writevol;
};

typedef enum {
    MUXFLAG_RUNNING = (1 << 0),
    MUXFLAG_APPEND = (1 << 1),
    MUXFLAG_BRIDGED = (1 << 2),
    MUXFLAG_VOLUME = (1 << 3),
    MUXFLAG_READVOLUME = (1 << 4),
    MUXFLAG_WRITEVOLUME = (1 << 5)
} muxflags;


AST_DECLARE_OPTIONS(muxmon_opts,{
    ['a'] = { MUXFLAG_APPEND },
	['b'] = { MUXFLAG_BRIDGED },
	['v'] = { MUXFLAG_READVOLUME, 1 },
	['V'] = { MUXFLAG_WRITEVOLUME, 2 },
	['W'] = { MUXFLAG_VOLUME, 3 },
});


static void stopmon(struct ast_channel *chan, struct ast_channel_spy *spy) 
{
	struct ast_channel_spy *cptr=NULL, *prev=NULL;
	int count = 0;

	if (chan) {
		while(ast_mutex_trylock(&chan->lock)) {
			if (chan->spiers == spy) {
				chan->spiers = NULL;
				return;
			}
			count++;
			if (count > 10) {
				return;
			}
			sched_yield();
		}
		
		for(cptr=chan->spiers; cptr; cptr=cptr->next) {
			if (cptr == spy) {
				if (prev) {
					prev->next = cptr->next;
					cptr->next = NULL;
				} else
					chan->spiers = NULL;
			}
			prev = cptr;
		}

		ast_mutex_unlock(&chan->lock);
	}
}

static void startmon(struct ast_channel *chan, struct ast_channel_spy *spy) 
{

	struct ast_channel_spy *cptr=NULL;
	struct ast_channel *peer;

	if (chan) {
		ast_mutex_lock(&chan->lock);
		if (chan->spiers) {
			for(cptr=chan->spiers;cptr->next;cptr=cptr->next);
			cptr->next = spy;
		} else {
			chan->spiers = spy;
		}
		ast_mutex_unlock(&chan->lock);
		
		if (ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan))) {
			ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);	
		}
	}
}

static int spy_queue_translate(struct ast_channel_spy *spy,
							   struct ast_slinfactory *slinfactory0,
							   struct ast_slinfactory *slinfactory1)
{
	int res = 0;
	struct ast_frame *f;
	
	ast_mutex_lock(&spy->lock);
	while((f = spy->queue[0])) {
		spy->queue[0] = f->next;
		ast_slinfactory_feed(slinfactory0, f);
		ast_frfree(f);
	}
	ast_mutex_unlock(&spy->lock);
	ast_mutex_lock(&spy->lock);
	while((f = spy->queue[1])) {
		spy->queue[1] = f->next;
		ast_slinfactory_feed(slinfactory1, f);
		ast_frfree(f);
	}
	ast_mutex_unlock(&spy->lock);
	return res;
}

static void *muxmon_thread(void *obj) 
{

	int len0 = 0, len1 = 0, samp0 = 0, samp1 = 0, framelen, maxsamp = 0, x = 0;
	short buf0[1280], buf1[1280], buf[1280];
	struct ast_frame frame;
	struct muxmon *muxmon = obj;
	struct ast_channel_spy spy;
	struct ast_filestream *fs = NULL;
	char *ext, *name;
	unsigned int oflags;
	struct ast_slinfactory slinfactory[2];
	char post_process[1024] = "";
	
	name = ast_strdupa(muxmon->chan->name);

	framelen = 320;
	frame.frametype = AST_FRAME_VOICE;
	frame.subclass = AST_FORMAT_SLINEAR;
	frame.data = buf;
	ast_set_flag(muxmon, MUXFLAG_RUNNING);
	oflags = O_CREAT|O_WRONLY;
	ast_slinfactory_init(&slinfactory[0]);
	ast_slinfactory_init(&slinfactory[1]);
	


	/* for efficiency, use a flag to bypass volume logic when it's not needed */
	if (muxmon->readvol || muxmon->writevol) {
		ast_set_flag(muxmon, MUXFLAG_VOLUME);
	}

	if ((ext = strchr(muxmon->filename, '.'))) {
		*(ext++) = '\0';
	} else {
		ext = "raw";
	}

	memset(&spy, 0, sizeof(spy));
	spy.status = CHANSPY_RUNNING;
	ast_mutex_init(&spy.lock);
	startmon(muxmon->chan, &spy);
	if (ast_test_flag(muxmon, MUXFLAG_RUNNING)) {
		if (option_verbose > 1) {
			ast_verbose(VERBOSE_PREFIX_2 "Begin Muxmon Recording %s\n", name);
		}

		oflags |= ast_test_flag(muxmon, MUXFLAG_APPEND) ? O_APPEND : O_TRUNC;
		
		if (!(fs = ast_writefile(muxmon->filename, ext, NULL, oflags, 0, 0644))) {
			ast_log(LOG_ERROR, "Cannot open %s\n", muxmon->filename);
			spy.status = CHANSPY_DONE;
		}  else {

			if (ast_test_flag(muxmon, MUXFLAG_APPEND)) {
				ast_seekstream(fs, 0, SEEK_END);
			}

			while (ast_test_flag(muxmon, MUXFLAG_RUNNING)) {
				samp0 = samp1 = len0 = len1 = 0;

				if (ast_check_hangup(muxmon->chan) || spy.status != CHANSPY_RUNNING) {
					ast_clear_flag(muxmon, MUXFLAG_RUNNING);
					break;
				}

				if (ast_test_flag(muxmon, MUXFLAG_BRIDGED) && !ast_bridged_channel(muxmon->chan)) {
					usleep(1000);
					sched_yield();
					continue;
				}
				
				spy_queue_translate(&spy, &slinfactory[0], &slinfactory[1]);
				
				if (slinfactory[0].size < framelen || slinfactory[1].size < framelen) {
					usleep(1000);
					sched_yield();
					continue;
				}

				if ((len0 = ast_slinfactory_read(&slinfactory[0], buf0, framelen))) {
					samp0 = len0 / 2;
				}
				if((len1 = ast_slinfactory_read(&slinfactory[1], buf1, framelen))) {
					samp1 = len1 / 2;
				}
				
				if (ast_test_flag(muxmon, MUXFLAG_VOLUME)) {
					if (samp0 && muxmon->readvol > 0) {
						for(x=0; x < samp0 / 2; x++) {
							buf0[x] *= muxmon->readvol;
						}
					} else if (samp0 && muxmon->readvol < 0) {
						for(x=0; x < samp0 / 2; x++) {
							buf0[x] /= muxmon->readvol;
						}
					}
					if (samp1 && muxmon->writevol > 0) {
						for(x=0; x < samp1 / 2; x++) {
							buf1[x] *= muxmon->writevol;
						}
					} else if (muxmon->writevol < 0) {
						for(x=0; x < samp1 / 2; x++) {
							buf1[x] /= muxmon->writevol;
						}
					}
				}
				
				maxsamp = (samp0 > samp1) ? samp0 : samp1;

				if (samp0 && samp1) {
					for(x=0; x < maxsamp; x++) {
						if (x < samp0 && x < samp1) {
							buf[x] = buf0[x] + buf1[x];
						} else if (x < samp0) {
							buf[x] = buf0[x];
						} else if (x < samp1) {
							buf[x] = buf1[x];
						}
					}
				} else if(samp0) {
					memcpy(buf, buf0, len0);
					x = samp0;
				} else if(samp1) {
					memcpy(buf, buf1, len1);
					x = samp1;
				}

				frame.samples = x;
				frame.datalen = x * 2;
				ast_writestream(fs, &frame);
		
				usleep(1000);
				sched_yield();
			}
		}
	}

	if (muxmon->post_process) {
		char *p;
		for(p = muxmon->post_process; *p ; p++) {
			if (*p == '^' && *(p+1) == '{') {
				*p = '$';
			}
		}
		pbx_substitute_variables_helper(muxmon->chan, muxmon->post_process, post_process, sizeof(post_process) - 1);
		free(muxmon->post_process);
		muxmon->post_process = NULL;
	}

	stopmon(muxmon->chan, &spy);
	if (option_verbose > 1) {
		ast_verbose(VERBOSE_PREFIX_2 "Finished Recording %s\n", name);
	}
	ast_mutex_destroy(&spy.lock);
	
	if(fs) {
		ast_closestream(fs);
	}
	
	ast_slinfactory_destroy(&slinfactory[0]);
	ast_slinfactory_destroy(&slinfactory[1]);

	if (muxmon) {
		if (muxmon->filename) {
			free(muxmon->filename);
		}
		free(muxmon);
	}

	if (!ast_strlen_zero(post_process)) {
		if (option_verbose > 2) {
			ast_verbose(VERBOSE_PREFIX_2 "Executing [%s]\n", post_process);
		}
		ast_safe_system(post_process);
	}

	return NULL;
}

static void launch_monitor_thread(struct ast_channel *chan, char *filename, unsigned int flags, int readvol , int writevol, char *post_process) 
{
	pthread_attr_t attr;
	int result = 0;
	pthread_t thread;
	struct muxmon *muxmon;


	if (!(muxmon = malloc(sizeof(struct muxmon)))) {
		ast_log(LOG_ERROR, "Memory Error!\n");
		return;
	}

	memset(muxmon, 0, sizeof(struct muxmon));
	muxmon->chan = chan;
	muxmon->filename = strdup(filename);
	if(post_process) {
		muxmon->post_process = strdup(post_process);
	}
	muxmon->readvol = readvol;
	muxmon->writevol = writevol;
	muxmon->flags = flags;

	result = pthread_attr_init(&attr);
	pthread_attr_setschedpolicy(&attr, SCHED_RR);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	result = ast_pthread_create(&thread, &attr, muxmon_thread, muxmon);
	result = pthread_attr_destroy(&attr);
}


static int muxmon_exec(struct ast_channel *chan, void *data)
{
	int res = 0, x = 0, readvol = 0, writevol = 0;
	struct localuser *u;
	struct ast_flags flags = {0};
	int argc;
	char *options = NULL,
		*args,
		*argv[3],
		*filename = NULL,
		*post_process = NULL;
	
	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "muxmon requires an argument\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	args = ast_strdupa(data);	
	if (!args) {
		ast_log(LOG_WARNING, "Memory Error!\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if ((argc = ast_separate_app_args(args, '|', argv, sizeof(argv) / sizeof(argv[0])))) {
		filename = argv[0];
		if (argc > 1) {
			options = argv[1];
		}
		if (argc > 2) {
			post_process = argv[2];
		}
	}
	
	if (!filename || ast_strlen_zero(filename)) {
		ast_log(LOG_WARNING, "Muxmon requires an argument (filename)\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (options) {
		char *opts[3] = {};
		ast_parseoptions(muxmon_opts, &flags, opts, options);

		if (ast_test_flag(&flags, MUXFLAG_READVOLUME) && opts[0]) {
			if (sscanf(opts[0], "%d", &x) != 1)
				ast_log(LOG_NOTICE, "volume must be a number between -4 and 4\n");
			else {
				readvol = minmax(x, 4);
				x = get_volfactor(readvol);
				readvol = minmax(x, 16);
			}
		}
		
		if (ast_test_flag(&flags, MUXFLAG_WRITEVOLUME) && opts[1]) {
			if (sscanf(opts[1], "%d", &x) != 1)
				ast_log(LOG_NOTICE, "volume must be a number between -4 and 4\n");
			else {
				writevol = minmax(x, 4);
				x = get_volfactor(writevol);
				writevol = minmax(x, 16);
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_VOLUME) && opts[2]) {
			if (sscanf(opts[2], "%d", &x) != 1)
				ast_log(LOG_NOTICE, "volume must be a number between -4 and 4\n");
			else {
				readvol = writevol = minmax(x, 4);
				x = get_volfactor(readvol);
				readvol = minmax(x, 16);
				x = get_volfactor(writevol);
				writevol = minmax(x, 16);
			}
		}
	}
	pbx_builtin_setvar_helper(chan, "MUXMON_FILENAME", filename);
	launch_monitor_thread(chan, filename, flags.flags, readvol, writevol, post_process);

	LOCAL_USER_REMOVE(u);
	return res;
}


static int muxmon_cli(int fd, int argc, char **argv) 
{
	char *op, *chan_name = NULL, *args = NULL;
	struct ast_channel *chan;

	if (argc > 2) {
		op = argv[1];
		chan_name = argv[2];

		if (argv[3]) {
			args = argv[3];
		}

		if (!(chan = ast_get_channel_by_name_prefix_locked(chan_name, strlen(chan_name)))) {
			ast_cli(fd, "Invalid Channel!\n");
			return -1;
		}
		if (!strcasecmp(op, "start")) {
			muxmon_exec(chan, args);
		} else if (!strcasecmp(op, "stop")) {
			struct ast_channel_spy *cptr=NULL;
			for(cptr=chan->spiers; cptr; cptr=cptr->next) {
				cptr->status = CHANSPY_DONE;
			}
		}
		ast_mutex_unlock(&chan->lock);
		return 0;
	}

	ast_cli(fd, "Usage: muxmon <start|stop> <chan_name> <args>\n");
	return -1;
}


static struct ast_cli_entry cli_muxmon = {
	{ "muxmon", NULL, NULL }, muxmon_cli, 
	"Execute a monitor command", "muxmon <start|stop> <chan_name> <args>"};


int unload_module(void)
{
	int res;

	res = ast_cli_unregister(&cli_muxmon);
	res |= ast_unregister_application(app);
	
	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;

	res = ast_cli_register(&cli_muxmon);
	res |= ast_register_application(app, muxmon_exec, synopsis, desc);

	return res;
}

char *description(void)
{
	return tdesc;
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

