/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 Anthony Minessale II (anthmct@yahoo.com)
 *
 * Disclaimed to Digium
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
 * \brief ChanSpy: Listen in on any channel.
 * 
 * \ingroup applications
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/features.h"
#include "asterisk/options.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/say.h"
#include "asterisk/pbx.h"
#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"

AST_MUTEX_DEFINE_STATIC(modlock);

#define AST_NAME_STRLEN 256
#define ALL_DONE(u, ret) LOCAL_USER_REMOVE(u); return ret;
#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0

static const char *synopsis = "Listen to the audio of an active channel\n";
static const char *app = "ChanSpy";
static const char *desc = 
"  ChanSpy([chanprefix][|options]): This application is used to listen to the\n"
"audio from an active Asterisk channel. This includes the audio coming in and\n"
"out of the channel being spied on. If the 'chanprefix' parameter is specified,\n"
"only channels beginning with this string will be spied upon.\n"
"  While Spying, the following actions may be performed:\n"
"    - Dialing # cycles the volume level.\n"
"    - Dialing * will stop spying and look for another channel to spy on.\n"
"    - Dialing a series of digits followed by # builds a channel name to append\n"
"      to 'chanprefix'. For example, executing ChanSpy(Agent) and then dialing\n"
"      the digits '1234#' while spying will begin spying on the channel,\n"
"      'Agent/1234'.\n"
"  Options:\n"
"    b - Only spy on channels involved in a bridged call.\n"
"    g(grp) - Match only channels where their ${SPYGROUP} variable is set to\n"
"             'grp'.\n"
"    q - Don't play a beep when beginning to spy on a channel.\n"
"    r[(basename)] - Record the session to the monitor spool directory. An\n"
"                    optional base for the filename may be specified. The\n"
"                    default is 'chanspy'.\n"
"    v([value]) - Adjust the initial volume in the range from -4 to 4. A\n"
"                 negative value refers to a quieter setting.\n"
;

static const char *chanspy_spy_type = "ChanSpy";

enum {
	OPTION_QUIET	 = (1 << 0),	/* Quiet, no announcement */
	OPTION_BRIDGED   = (1 << 1),	/* Only look at bridged calls */
	OPTION_VOLUME    = (1 << 2),	/* Specify initial volume */
	OPTION_GROUP     = (1 << 3),	/* Only look at channels in group */
	OPTION_RECORD    = (1 << 4),	/* Record */
} chanspy_opt_flags;

enum {
	OPT_ARG_VOLUME = 0,
	OPT_ARG_GROUP,
	OPT_ARG_RECORD,
	OPT_ARG_ARRAY_SIZE,
} chanspy_opt_args;

AST_APP_OPTIONS(chanspy_opts, {
	AST_APP_OPTION('q', OPTION_QUIET),
	AST_APP_OPTION('b', OPTION_BRIDGED),
	AST_APP_OPTION_ARG('v', OPTION_VOLUME, OPT_ARG_VOLUME),
	AST_APP_OPTION_ARG('g', OPTION_GROUP, OPT_ARG_GROUP),
	AST_APP_OPTION_ARG('r', OPTION_RECORD, OPT_ARG_RECORD),
});

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

struct chanspy_translation_helper {
	/* spy data */
	struct ast_channel_spy spy;
	int fd;
	int volfactor;
};

static struct ast_channel *local_channel_walk(struct ast_channel *chan) 
{
	struct ast_channel *ret;
	ast_mutex_lock(&modlock);	
	if ((ret = ast_channel_walk_locked(chan))) {
		ast_mutex_unlock(&ret->lock);
	}
	ast_mutex_unlock(&modlock);			
	return ret;
}

static struct ast_channel *local_get_channel_begin_name(char *name) 
{
	struct ast_channel *chan, *ret = NULL;
	ast_mutex_lock(&modlock);
	chan = local_channel_walk(NULL);
	while (chan) {
		if (!strncmp(chan->name, name, strlen(name))) {
			ret = chan;
			break;
		}
		chan = local_channel_walk(chan);
	}
	ast_mutex_unlock(&modlock);
	
	return ret;
}

static void *spy_alloc(struct ast_channel *chan, void *data)
{
	/* just store the data pointer in the channel structure */
	return data;
}

static void spy_release(struct ast_channel *chan, void *data)
{
	/* nothing to do */
}

static int spy_generate(struct ast_channel *chan, void *data, int len, int samples) 
{
	struct chanspy_translation_helper *csth = data;
	struct ast_frame *f;
		
	if (csth->spy.status != CHANSPY_RUNNING)
		/* Channel is already gone more than likely */
		return -1;

	ast_mutex_lock(&csth->spy.lock);
	f = ast_channel_spy_read_frame(&csth->spy, samples);
	ast_mutex_unlock(&csth->spy.lock);
		
	if (!f)
		return 0;
		
	if (ast_write(chan, f)) {
		ast_frfree(f);
		return -1;
	}

	if (csth->fd)
		write(csth->fd, f->data, f->datalen);

	ast_frfree(f);

	return 0;
}


static struct ast_generator spygen = {
	.alloc = spy_alloc,
	.release = spy_release,
	.generate = spy_generate, 
};

static int start_spying(struct ast_channel *chan, struct ast_channel *spychan, struct ast_channel_spy *spy) 
{
	int res;
	struct ast_channel *peer;

	ast_log(LOG_NOTICE, "Attaching %s to %s\n", spychan->name, chan->name);

	ast_mutex_lock(&chan->lock);
	res = ast_channel_spy_add(chan, spy);
	ast_mutex_unlock(&chan->lock);

	if (!res && ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan))) {
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);	
	}

	return res;
}

static void stop_spying(struct ast_channel *chan, struct ast_channel_spy *spy) 
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
};

/* Map 'volume' levels from -4 through +4 into
   decibel (dB) settings for channel drivers
*/
static signed char volfactor_map[] = {
	-24,
	-18,
	-12,
	-6,
	0,
	6,
	12,
	18,
	24,
};

/* attempt to set the desired gain adjustment via the channel driver;
   if successful, clear it out of the csth structure so the
   generator will not attempt to do the adjustment itself
*/
static void set_volume(struct ast_channel *chan, struct chanspy_translation_helper *csth)
{
	signed char volume_adjust = volfactor_map[csth->volfactor + 4];

	if (!ast_channel_setoption(chan, AST_OPTION_TXGAIN, &volume_adjust, sizeof(volume_adjust), 0))
		csth->volfactor = 0;
}

static int channel_spy(struct ast_channel *chan, struct ast_channel *spyee, int *volfactor, int fd) 
{
	struct chanspy_translation_helper csth;
	int running, res = 0, x = 0;
	char inp[24];
	char *name=NULL;
	struct ast_frame *f;

	running = (chan && !ast_check_hangup(chan) && spyee && !ast_check_hangup(spyee));

	if (running) {
		memset(inp, 0, sizeof(inp));
		name = ast_strdupa(spyee->name);
		if (option_verbose >= 2)
			ast_verbose(VERBOSE_PREFIX_2 "Spying on channel %s\n", name);

		memset(&csth, 0, sizeof(csth));
		ast_set_flag(&csth.spy, CHANSPY_FORMAT_AUDIO);
		ast_set_flag(&csth.spy, CHANSPY_TRIGGER_NONE);
		ast_set_flag(&csth.spy, CHANSPY_MIXAUDIO);
		csth.spy.type = chanspy_spy_type;
		csth.spy.status = CHANSPY_RUNNING;
		csth.spy.read_queue.format = AST_FORMAT_SLINEAR;
		csth.spy.write_queue.format = AST_FORMAT_SLINEAR;
		ast_mutex_init(&csth.spy.lock);
		csth.volfactor = *volfactor;
		set_volume(chan, &csth);
		csth.spy.read_vol_adjustment = csth.volfactor;
		csth.spy.write_vol_adjustment = csth.volfactor;
		csth.fd = fd;

		if (start_spying(spyee, chan, &csth.spy))
			running = 0;
	}

	if (running) {
		running = 1;
		ast_activate_generator(chan, &spygen, &csth);

		while (csth.spy.status == CHANSPY_RUNNING &&
		       chan && !ast_check_hangup(chan) &&
		       spyee &&
		       !ast_check_hangup(spyee) &&
		       running == 1 &&
		       (res = ast_waitfor(chan, -1) > -1)) {
			if ((f = ast_read(chan))) {
				res = 0;
				if (f->frametype == AST_FRAME_DTMF) {
					res = f->subclass;
				}
				ast_frfree(f);
				if (!res) {
					continue;
				}
			} else {
				break;
			}
			if (x == sizeof(inp)) {
				x = 0;
			}
			if (res < 0) {
				running = -1;
			}
			if (res == 0) {
				continue;
			} else if (res == '*') {
				running = 0; 
			} else if (res == '#') {
				if (!ast_strlen_zero(inp)) {
					running = x ? atoi(inp) : -1;
					break;
				} else {
					(*volfactor)++;
					if (*volfactor > 4) {
						*volfactor = -4;
					}
					if (option_verbose > 2) {
						ast_verbose(VERBOSE_PREFIX_3 "Setting spy volume on %s to %d\n", chan->name, *volfactor);
					}
					csth.volfactor = *volfactor;
					set_volume(chan, &csth);
					csth.spy.read_vol_adjustment = csth.volfactor;
					csth.spy.write_vol_adjustment = csth.volfactor;
				}
			} else if (res >= 48 && res <= 57) {
				inp[x++] = res;
			}
		}
		ast_deactivate_generator(chan);
		stop_spying(spyee, &csth.spy);

		if (option_verbose >= 2) {
			ast_verbose(VERBOSE_PREFIX_2 "Done Spying on channel %s\n", name);
		}
	} else {
		running = 0;
	}

	ast_mutex_destroy(&csth.spy.lock);

	return running;
}

static int chanspy_exec(struct ast_channel *chan, void *data)
{
	struct localuser *u;
	struct ast_channel *peer=NULL, *prev=NULL;
	char name[AST_NAME_STRLEN],
		peer_name[AST_NAME_STRLEN + 5],
		*args,
		*ptr = NULL,
		*options = NULL,
		*spec = NULL,
		*argv[5],
		*mygroup = NULL,
		*recbase = NULL;
	int res = -1,
		volfactor = 0,
		silent = 0,
		argc = 0,
		bronly = 0,
		chosen = 0,
		count=0,
		waitms = 100,
		num = 0,
		oldrf = 0,
		oldwf = 0,
		fd = 0;
	struct ast_flags flags;
	signed char zero_volume = 0;

	if (!(args = ast_strdupa((char *)data))) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	oldrf = chan->readformat;
	oldwf = chan->writeformat;
	if (ast_set_read_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Read Format.\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}
	
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	ast_answer(chan);

	ast_set_flag(chan, AST_FLAG_SPYING); /* so nobody can spy on us while we are spying */

	if ((argc = ast_app_separate_args(args, '|', argv, sizeof(argv) / sizeof(argv[0])))) {
		spec = argv[0];
		if ( argc > 1) {
			options = argv[1];
		}
		if (ast_strlen_zero(spec) || !strcmp(spec, "all")) {
			spec = NULL;
		}
	}
	
	if (options) {
		char *opts[OPT_ARG_ARRAY_SIZE];
		ast_app_parse_options(chanspy_opts, &flags, opts, options);
		if (ast_test_flag(&flags, OPTION_GROUP)) {
			mygroup = opts[1];
		}
		if (ast_test_flag(&flags, OPTION_RECORD)) {
			if (!(recbase = opts[2])) {
				recbase = "chanspy";
			}
		}
		silent = ast_test_flag(&flags, OPTION_QUIET);
		bronly = ast_test_flag(&flags, OPTION_BRIDGED);
		if (ast_test_flag(&flags, OPTION_VOLUME) && opts[1]) {
			int vol;

			if ((sscanf(opts[0], "%d", &vol) != 1) || (vol > 4) || (vol < -4))
				ast_log(LOG_NOTICE, "Volume factor must be a number between -4 and 4\n");
			else
				volfactor = vol;
			}
	}

	if (recbase) {
		char filename[512];
		snprintf(filename,sizeof(filename),"%s/%s.%ld.raw",ast_config_AST_MONITOR_DIR, recbase, time(NULL));
		if ((fd = open(filename, O_CREAT | O_WRONLY, O_TRUNC)) <= 0) {
			ast_log(LOG_WARNING, "Cannot open %s for recording\n", filename);
			fd = 0;
		}
	}

	for(;;) {
		if (!silent) {
			res = ast_streamfile(chan, "beep", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
			if (res < 0) {
				ast_clear_flag(chan, AST_FLAG_SPYING);
				break;
			}
		}

		count = 0;
		res = ast_waitfordigit(chan, waitms);
		if (res < 0) {
			ast_clear_flag(chan, AST_FLAG_SPYING);
			break;
		}
				
		peer = local_channel_walk(NULL);
		prev=NULL;
		while(peer) {
			if (peer != chan) {
				char *group = NULL;
				int igrp = 1;

				if (peer == prev && !chosen) {
					break;
				}
				chosen = 0;
				group = pbx_builtin_getvar_helper(peer, "SPYGROUP");
				if (mygroup) {
					if (!group || strcmp(mygroup, group)) {
						igrp = 0;
					}
				}
				
				if (igrp && (!spec || ((strlen(spec) < strlen(peer->name) &&
							!strncasecmp(peer->name, spec, strlen(spec)))))) {
					if (peer && (!bronly || ast_bridged_channel(peer)) &&
					    !ast_check_hangup(peer) && !ast_test_flag(peer, AST_FLAG_SPYING)) {
						int x = 0;
						strncpy(peer_name, "spy-", 5);
						strncpy(peer_name + strlen(peer_name), peer->name, AST_NAME_STRLEN);
						ptr = strchr(peer_name, '/');
						*ptr = '\0';
						ptr++;
						for (x = 0 ; x < strlen(peer_name) ; x++) {
							if (peer_name[x] == '/') {
								break;
							}
							peer_name[x] = tolower(peer_name[x]);
						}

						if (!silent) {
							if (ast_fileexists(peer_name, NULL, NULL) != -1) {
								res = ast_streamfile(chan, peer_name, chan->language);
								if (!res)
									res = ast_waitstream(chan, "");
								if (res)
									break;
							} else
								res = ast_say_character_str(chan, peer_name, "", chan->language);
							if ((num=atoi(ptr))) 
								ast_say_digits(chan, atoi(ptr), "", chan->language);
						}
						count++;
						prev = peer;
						res = channel_spy(chan, peer, &volfactor, fd);
						if (res == -1) {
							break;
						} else if (res > 1 && spec) {
							snprintf(name, AST_NAME_STRLEN, "%s/%d", spec, res);
							if ((peer = local_get_channel_begin_name(name))) {
								chosen = 1;
							}
							continue;
						}
					}
				}
			}
			if ((peer = local_channel_walk(peer)) == NULL) {
				break;
			}
		}
		waitms = count ? 100 : 5000;
	}
	

	if (fd > 0) {
		close(fd);
	}

	if (oldrf && ast_set_read_format(chan, oldrf) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Read Format.\n");
	}
	
	if (oldwf && ast_set_write_format(chan, oldwf) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
	}

	ast_clear_flag(chan, AST_FLAG_SPYING);

	ast_channel_setoption(chan, AST_OPTION_TXGAIN, &zero_volume, sizeof(zero_volume), 0);

	ALL_DONE(u, res);
}

int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	return ast_register_application(app, chanspy_exec, synopsis, desc);
}

char *description(void)
{
	return (char *) synopsis;
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
