/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 Anthony Minessale II (anthmct@yahoo.com)
 * Copyright (C) 2005 - 2006, Digium, Inc.
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
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
 * \brief ChanSpy: Listen in on any channel.
 *
 * \author Anthony Minessale II <anthmct@yahoo.com>
 *
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/chanspy.h"
#include "asterisk/features.h"
#include "asterisk/options.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/say.h"
#include "asterisk/pbx.h"
#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"

#define AST_NAME_STRLEN 256

static const char *tdesc = "Listen to a channel, and optionally whisper into it";
static const char *app_chan = "ChanSpy";
static const char *desc_chan = 
"  ChanSpy([chanprefix][|options]): This application is used to listen to the\n"
"audio from an Asterisk channel. This includes the audio coming in and\n"
"out of the channel being spied on. If the 'chanprefix' parameter is specified,\n"
"only channels beginning with this string will be spied upon.\n"
"  While spying, the following actions may be performed:\n"
"    - Dialing # cycles the volume level.\n"
"    - Dialing * will stop spying and look for another channel to spy on.\n"
"    - Dialing a series of digits followed by # builds a channel name to append\n"
"      to 'chanprefix'. For example, executing ChanSpy(Agent) and then dialing\n"
"      the digits '1234#' while spying will begin spying on the channel\n"
"      'Agent/1234'.\n"
"  Options:\n"
"    b             - Only spy on channels involved in a bridged call.\n"
"    g(grp)        - Match only channels where their ${SPYGROUP} variable is set to\n"
"                    contain 'grp' in an optional : delimited list.\n"
"    q             - Don't play a beep when beginning to spy on a channel, or speak the\n"
"                    selected channel name.\n"
"    r[(basename)] - Record the session to the monitor spool directory. An\n"
"                    optional base for the filename may be specified. The\n"
"                    default is 'chanspy'.\n"
"    v([value])    - Adjust the initial volume in the range from -4 to 4. A\n"
"                    negative value refers to a quieter setting.\n"
"    w             - Enable 'whisper' mode, so the spying channel can talk to\n"
"                    the spied-on channel.\n"
"    W             - Enable 'private whisper' mode, so the spying channel can\n"
"                    talk to the spied-on channel but cannot listen to that\n"
"                    channel.\n"
;

static const char *app_ext = "ExtenSpy";
static const char *desc_ext = 
"  ExtenSpy(exten[@context][|options]): This application is used to listen to the\n"
"audio from an Asterisk channel. This includes the audio coming in and\n"
"out of the channel being spied on. Only channels created by outgoing calls for the\n"
"specified extension will be selected for spying. If the optional context is not\n"
"supplied, the current channel's context will be used.\n"
"  While spying, the following actions may be performed:\n"
"    - Dialing # cycles the volume level.\n"
"    - Dialing * will stop spying and look for another channel to spy on.\n"
"  Options:\n"
"    b             - Only spy on channels involved in a bridged call.\n"
"    g(grp)        - Match only channels where their ${SPYGROUP} variable is set to\n"
"                    contain 'grp' in an optional : delimited list.\n"
"    q             - Don't play a beep when beginning to spy on a channel, or speak the\n"
"                    selected channel name.\n"
"    r[(basename)] - Record the session to the monitor spool directory. An\n"
"                    optional base for the filename may be specified. The\n"
"                    default is 'chanspy'.\n"
"    v([value])    - Adjust the initial volume in the range from -4 to 4. A\n"
"                    negative value refers to a quieter setting.\n"
"    w             - Enable 'whisper' mode, so the spying channel can talk to\n"
"                    the spied-on channel.\n"
"    W             - Enable 'private whisper' mode, so the spying channel can\n"
"                    talk to the spied-on channel but cannot listen to that\n"
"                    channel.\n"
;

enum {
	OPTION_QUIET	 = (1 << 0),	/* Quiet, no announcement */
	OPTION_BRIDGED   = (1 << 1),	/* Only look at bridged calls */
	OPTION_VOLUME    = (1 << 2),	/* Specify initial volume */
	OPTION_GROUP     = (1 << 3),	/* Only look at channels in group */
	OPTION_RECORD    = (1 << 4),
	OPTION_WHISPER	 = (1 << 5),
	OPTION_PRIVATE   = (1 << 6),	/* Private Whisper mode */
} chanspy_opt_flags;

enum {
	OPT_ARG_VOLUME = 0,
	OPT_ARG_GROUP,
	OPT_ARG_RECORD,
	OPT_ARG_ARRAY_SIZE,
} chanspy_opt_args;

AST_APP_OPTIONS(spy_opts, {
	AST_APP_OPTION('q', OPTION_QUIET),
	AST_APP_OPTION('b', OPTION_BRIDGED),
	AST_APP_OPTION('w', OPTION_WHISPER),
	AST_APP_OPTION('W', OPTION_PRIVATE),
	AST_APP_OPTION_ARG('v', OPTION_VOLUME, OPT_ARG_VOLUME),
	AST_APP_OPTION_ARG('g', OPTION_GROUP, OPT_ARG_GROUP),
	AST_APP_OPTION_ARG('r', OPTION_RECORD, OPT_ARG_RECORD),
});


struct chanspy_translation_helper {
	/* spy data */
	struct ast_channel_spy spy;
	int fd;
	int volfactor;
};

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

	ast_channel_lock(chan);
	res = ast_channel_spy_add(chan, spy);
	ast_channel_unlock(chan);

	if (!res && ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan)))
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);	

	return res;
}

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
	csth->spy.read_vol_adjustment = csth->volfactor;
	csth->spy.write_vol_adjustment = csth->volfactor;
}

static int channel_spy(struct ast_channel *chan, struct ast_channel *spyee, int *volfactor, int fd,
		       const struct ast_flags *flags) 
{
	struct chanspy_translation_helper csth;
	int running = 0, res, x = 0;
	char inp[24] = {0};
	char *name;
	struct ast_frame *f;
	struct ast_silence_generator *silgen = NULL;

	if (ast_check_hangup(chan) || ast_check_hangup(spyee))
		return 0;

	name = ast_strdupa(spyee->name);
	if (option_verbose >= 2)
		ast_verbose(VERBOSE_PREFIX_2 "Spying on channel %s\n", name);

	memset(&csth, 0, sizeof(csth));
	ast_set_flag(&csth.spy, CHANSPY_FORMAT_AUDIO);
	ast_set_flag(&csth.spy, CHANSPY_TRIGGER_NONE);
	ast_set_flag(&csth.spy, CHANSPY_MIXAUDIO);
	csth.spy.type = "ChanSpy";
	csth.spy.status = CHANSPY_RUNNING;
	csth.spy.read_queue.format = AST_FORMAT_SLINEAR;
	csth.spy.write_queue.format = AST_FORMAT_SLINEAR;
	ast_mutex_init(&csth.spy.lock);
	csth.volfactor = *volfactor;
	set_volume(chan, &csth);
	csth.fd = fd;
	
	if (start_spying(spyee, chan, &csth.spy)) {
		ast_mutex_destroy(&csth.spy.lock);
		return 0;
	}

	if (ast_test_flag(flags, OPTION_WHISPER)) {
		struct ast_filestream *beepstream;

		ast_channel_whisper_start(csth.spy.chan);
		if ((beepstream = ast_openstream_full(chan, "beep", chan->language, 1))) {
			struct ast_frame *f;

			while ((f = ast_readframe(beepstream))) {
				ast_channel_whisper_feed(csth.spy.chan, f);
				ast_frfree(f);
			}

			ast_closestream(beepstream);
		}
	}

	if (ast_test_flag(flags, OPTION_PRIVATE))
		silgen = ast_channel_start_silence_generator(chan);
	else
		ast_activate_generator(chan, &spygen, &csth);

	/* We can no longer rely on 'spyee' being an actual channel;
	   it can be hung up and freed out from under us. However, the
	   channel destructor will put NULL into our csth.spy.chan
	   field when that happens, so that is our signal that the spyee
	   channel has gone away.
	*/

	/* Note: it is very important that the ast_waitfor() be the first
	   condition in this expression, so that if we wait for some period
	   of time before receiving a frame from our spying channel, we check
	   for hangup on the spied-on channel _after_ knowing that a frame
	   has arrived, since the spied-on channel could have gone away while
	   we were waiting
	*/
	while ((res = ast_waitfor(chan, -1) > -1) &&
	       csth.spy.status == CHANSPY_RUNNING &&
	       csth.spy.chan) {
		if (!(f = ast_read(chan)) || ast_check_hangup(chan)) {
			running = -1;
			break;
		}

		if (ast_test_flag(flags, OPTION_WHISPER) &&
		    (f->frametype == AST_FRAME_VOICE)) {
			ast_channel_whisper_feed(csth.spy.chan, f);
			ast_frfree(f);
			continue;
		}

		res = (f->frametype == AST_FRAME_DTMF) ? f->subclass : 0;
		ast_frfree(f);
		if (!res)
			continue;

		if (x == sizeof(inp))
			x = 0;

		if (res < 0) {
			running = -1;
			break;
		}

		if (res == '*') {
			running = 0;
			break;
		} else if (res == '#') {
			if (!ast_strlen_zero(inp)) {
				running = atoi(inp);
				break;
			}

			(*volfactor)++;
			if (*volfactor > 4)
				*volfactor = -4;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Setting spy volume on %s to %d\n", chan->name, *volfactor);
			csth.volfactor = *volfactor;
			set_volume(chan, &csth);
		} else if (res >= '0' && res <= '9') {
			inp[x++] = res;
		}
	}

	if (ast_test_flag(flags, OPTION_WHISPER) && csth.spy.chan)
		ast_channel_whisper_stop(csth.spy.chan);

	if (ast_test_flag(flags, OPTION_PRIVATE))
		ast_channel_stop_silence_generator(chan, silgen);
	else
		ast_deactivate_generator(chan);

	/* If a channel still exists on our spy structure then we need to remove ourselves */
        if (csth.spy.chan) {
                csth.spy.status = CHANSPY_DONE;
                ast_channel_spy_remove(csth.spy.chan, &csth.spy);
        }
	ast_channel_spy_free(&csth.spy);
	
	if (option_verbose >= 2)
		ast_verbose(VERBOSE_PREFIX_2 "Done Spying on channel %s\n", name);
	
	return running;
}

static struct ast_channel *next_channel(const struct ast_channel *last, const char *spec,
					const char *exten, const char *context)
{
	struct ast_channel *this;

	redo:
	if (spec)
		this = ast_walk_channel_by_name_prefix_locked(last, spec, strlen(spec));
	else if (exten)
		this = ast_walk_channel_by_exten_locked(last, exten, context);
	else
		this = ast_channel_walk_locked(last);

	if (this) {
		ast_channel_unlock(this);
		if (!strncmp(this->name, "Zap/pseudo", 10))
			goto redo;
	}

	return this;
}

static int common_exec(struct ast_channel *chan, const struct ast_flags *flags,
		       int volfactor, const int fd, const char *mygroup, const char *spec,
		       const char *exten, const char *context)
{
	struct ast_channel *peer, *prev, *next;
	char nameprefix[AST_NAME_STRLEN];
	char peer_name[AST_NAME_STRLEN + 5];
	signed char zero_volume = 0;
	int waitms;
	int res;
	char *ptr;
	int num;

	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	ast_set_flag(chan, AST_FLAG_SPYING); /* so nobody can spy on us while we are spying */

	waitms = 100;

	for (;;) {
		if (!ast_test_flag(flags, OPTION_QUIET)) {
			res = ast_streamfile(chan, "beep", chan->language);
			if (!res)
				res = ast_waitstream(chan, "");
			else if (res < 0) {
				ast_clear_flag(chan, AST_FLAG_SPYING);
				break;
			}
		}

		res = ast_waitfordigit(chan, waitms);
		if (res < 0) {
			ast_clear_flag(chan, AST_FLAG_SPYING);
			break;
		}
				
		/* reset for the next loop around, unless overridden later */
		waitms = 100;
		peer = prev = next = NULL;

		for (peer = next_channel(peer, spec, exten, context);
		     peer;
		     prev = peer, peer = next ? next : next_channel(peer, spec, exten, context), next = NULL) {
			const char *group;
			int igrp = !mygroup;
			char *groups[25];
			int num_groups = 0;
			char *dup_group;
			int x;
			char *s;
				
			if (peer == prev)
				break;

			if (peer == chan)
				continue;

			if (ast_test_flag(flags, OPTION_BRIDGED) && !ast_bridged_channel(peer))
				continue;

			if (ast_check_hangup(peer) || ast_test_flag(peer, AST_FLAG_SPYING))
				continue;

			if (mygroup) {
				if ((group = pbx_builtin_getvar_helper(peer, "SPYGROUP"))) {
					dup_group = ast_strdupa(group);
					num_groups = ast_app_separate_args(dup_group, ':', groups,
									   sizeof(groups) / sizeof(groups[0]));
				}
				
				for (x = 0; x < num_groups; x++) {
					if (!strcmp(mygroup, groups[x])) {
						igrp = 1;
						break;
					}
				}
			}
			
			if (!igrp)
				continue;

			strcpy(peer_name, "spy-");
			strncat(peer_name, peer->name, AST_NAME_STRLEN);
			ptr = strchr(peer_name, '/');
			*ptr++ = '\0';
			
			for (s = peer_name; s < ptr; s++)
				*s = tolower(*s);
			
			if (!ast_test_flag(flags, OPTION_QUIET)) {
				if (ast_fileexists(peer_name, NULL, NULL) != -1) {
					res = ast_streamfile(chan, peer_name, chan->language);
					if (!res)
						res = ast_waitstream(chan, "");
					if (res)
						break;
				} else
					res = ast_say_character_str(chan, peer_name, "", chan->language);
				if ((num = atoi(ptr))) 
					ast_say_digits(chan, atoi(ptr), "", chan->language);
			}
			
			waitms = 5000;
			res = channel_spy(chan, peer, &volfactor, fd, flags);
			
			if (res == -1) {
				break;
			} else if (res > 1 && spec) {
				snprintf(nameprefix, AST_NAME_STRLEN, "%s/%d", spec, res);
				if ((next = ast_get_channel_by_name_prefix_locked(nameprefix, strlen(nameprefix)))) {
					ast_channel_unlock(next);
				} else {
					/* stay on this channel */
					next = peer;
				}
				peer = NULL;
			}
		}
		if (res == -1)
			break;
	}
	
	ast_clear_flag(chan, AST_FLAG_SPYING);

	ast_channel_setoption(chan, AST_OPTION_TXGAIN, &zero_volume, sizeof(zero_volume), 0);

	return res;
}

static int chanspy_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	char *options = NULL;
	char *spec = NULL;
	char *argv[2];
	char *mygroup = NULL;
	char *recbase = NULL;
	int fd = 0;
	struct ast_flags flags;
	int oldwf = 0;
	int argc = 0;
	int volfactor = 0;
	int res;

	data = ast_strdupa(data);

	u = ast_module_user_add(chan);

	if ((argc = ast_app_separate_args(data, '|', argv, sizeof(argv) / sizeof(argv[0])))) {
		spec = argv[0];
		if (argc > 1)
			options = argv[1];

		if (ast_strlen_zero(spec) || !strcmp(spec, "all"))
			spec = NULL;
	}

	if (options) {
		char *opts[OPT_ARG_ARRAY_SIZE];
		
		ast_app_parse_options(spy_opts, &flags, opts, options);
		if (ast_test_flag(&flags, OPTION_GROUP))
			mygroup = opts[OPT_ARG_GROUP];

		if (ast_test_flag(&flags, OPTION_RECORD) &&
		    !(recbase = opts[OPT_ARG_RECORD]))
			recbase = "chanspy";

		if (ast_test_flag(&flags, OPTION_VOLUME) && opts[OPT_ARG_VOLUME]) {
			int vol;

			if ((sscanf(opts[OPT_ARG_VOLUME], "%d", &vol) != 1) || (vol > 4) || (vol < -4))
				ast_log(LOG_NOTICE, "Volume factor must be a number between -4 and 4\n");
			else
				volfactor = vol;
		}

		if (ast_test_flag(&flags, OPTION_PRIVATE))
			ast_set_flag(&flags, OPTION_WHISPER);
	}

	oldwf = chan->writeformat;
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
		ast_module_user_remove(u);
		return -1;
	}

	if (recbase) {
		char filename[512];

		snprintf(filename, sizeof(filename), "%s/%s.%d.raw", ast_config_AST_MONITOR_DIR, recbase, (int) time(NULL));
		if ((fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644)) <= 0) {
			ast_log(LOG_WARNING, "Cannot open '%s' for recording\n", filename);
			fd = 0;
		}
	}

	res = common_exec(chan, &flags, volfactor, fd, mygroup, spec, NULL, NULL);

	if (fd)
		close(fd);

	if (oldwf && ast_set_write_format(chan, oldwf) < 0)
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");

	ast_module_user_remove(u);

	return res;
}

static int extenspy_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	char *options = NULL;
	char *exten = NULL;
	char *context = NULL;
	char *argv[2];
	char *mygroup = NULL;
	char *recbase = NULL;
	int fd = 0;
	struct ast_flags flags;
	int oldwf = 0;
	int argc = 0;
	int volfactor = 0;
	int res;

	data = ast_strdupa(data);

	u = ast_module_user_add(chan);

	if ((argc = ast_app_separate_args(data, '|', argv, sizeof(argv) / sizeof(argv[0])))) {
		context = argv[0];
		exten = strsep(&context, "@");
		if (ast_strlen_zero(context))
			context = ast_strdupa(chan->context);
		if (argc > 1)
			options = argv[1];
	}

	if (options) {
		char *opts[OPT_ARG_ARRAY_SIZE];
		
		ast_app_parse_options(spy_opts, &flags, opts, options);
		if (ast_test_flag(&flags, OPTION_GROUP))
			mygroup = opts[OPT_ARG_GROUP];

		if (ast_test_flag(&flags, OPTION_RECORD) &&
		    !(recbase = opts[OPT_ARG_RECORD]))
			recbase = "chanspy";

		if (ast_test_flag(&flags, OPTION_VOLUME) && opts[OPT_ARG_VOLUME]) {
			int vol;

			if ((sscanf(opts[OPT_ARG_VOLUME], "%d", &vol) != 1) || (vol > 4) || (vol < -4))
				ast_log(LOG_NOTICE, "Volume factor must be a number between -4 and 4\n");
			else
				volfactor = vol;
		}

		if (ast_test_flag(&flags, OPTION_PRIVATE))
			ast_set_flag(&flags, OPTION_WHISPER);
	}

	oldwf = chan->writeformat;
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
		ast_module_user_remove(u);
		return -1;
	}

	if (recbase) {
		char filename[512];

		snprintf(filename, sizeof(filename), "%s/%s.%d.raw", ast_config_AST_MONITOR_DIR, recbase, (int) time(NULL));
		if ((fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644)) <= 0) {
			ast_log(LOG_WARNING, "Cannot open '%s' for recording\n", filename);
			fd = 0;
		}
	}

	res = common_exec(chan, &flags, volfactor, fd, mygroup, NULL, exten, context);

	if (fd)
		close(fd);

	if (oldwf && ast_set_write_format(chan, oldwf) < 0)
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");

	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	int res = 0;

	res |= ast_unregister_application(app_chan);
	res |= ast_unregister_application(app_ext);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_register_application(app_chan, chanspy_exec, tdesc, desc_chan);
	res |= ast_register_application(app_ext, extenspy_exec, tdesc, desc_ext);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Listen to the audio of an active channel");
