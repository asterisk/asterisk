/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * ChanSpy Listen in on any channel.
 * 
 * Copyright (C) 2005 Anthony Minessale II (anthmct@yahoo.com)
 *
 * Disclaimed to Digium
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#define AST_MONITOR_DIR AST_SPOOL_DIR "/monitor"

#include <../asterisk.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/features.h>
#include <asterisk/options.h>
#include <asterisk/app.h>
#include <asterisk/utils.h>
#include <asterisk/say.h>
#include <asterisk/pbx.h>
#include <asterisk/translate.h>
#include <asterisk/module.h>
#include <asterisk/lock.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

AST_MUTEX_DEFINE_STATIC(modlock);

#define ast_fit_in_short(in) (in < -32768 ? -32768 : in > 32767 ? 32767 : in)
#define AST_NAME_STRLEN 256
#define ALL_DONE(u, ret) LOCAL_USER_REMOVE(u); return ret;
#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0
#define minmax(x,y) x ? (x > y) ? y : ((x < (y * -1)) ? (y * -1) : x) : 0


static char *synopsis = "Tap into any type of asterisk channel and listen to audio";
static char *app = "ChanSpy";
static char *desc = "   Chanspy([<scanspec>][|<options>])\n\n"
"Valid Options:\n"
" - q: quiet, don't announce channels beep, etc.\n"
" - b: bridged, only spy on channels involved in a bridged call.\n"
" - v([-4..4]): adjust the initial volume. (negative is quieter)\n"
" - g(grp): enforce group.  Match only calls where their ${SPYGROUP} is 'grp'.\n"
" - r[(basename)]: Record session to monitor spool dir (with optional basename, default is 'chanspy')\n\n"
"If <scanspec> is specified, only channel names *beginning* with that string will be scanned.\n"
"('all' or an empty string are also both valid <scanspec>)\n\n"
"While Spying:\n\n"
"Dialing # cycles the volume level.\n"
"Dialing * will stop spying and look for another channel to spy on.\n"
"Dialing a series of digits followed by # builds a channel name to append to <scanspec>\n"
"(e.g. run Chanspy(Agent) and dial 1234# while spying to jump to channel Agent/1234)\n\n"
"";

#define OPTION_QUIET	 (1 << 0)	/* Quiet, no announcement */
#define OPTION_BRIDGED   (1 << 1)	/* Only look at bridged calls */
#define OPTION_VOLUME    (1 << 2)	/* Specify initial volume */
#define OPTION_GROUP     (1 << 3)   /* Only look at channels in group */
#define OPTION_RECORD    (1 << 4)   /* Record */

AST_DECLARE_OPTIONS(chanspy_opts,{
	['q'] = { OPTION_QUIET },
	['b'] = { OPTION_BRIDGED },
	['v'] = { OPTION_VOLUME, 1 },
	['g'] = { OPTION_GROUP, 2 },
	['r'] = { OPTION_RECORD, 3 },
});

STANDARD_LOCAL_USER;
LOCAL_USER_DECL;

struct chanspy_translation_helper {
	/* spy data */
	struct ast_channel_spy spy;

	/* read frame */
	int fmt0;
	short *buf0;
	int len0;
	struct ast_trans_pvt *trans0;

	/* write frame */
	int fmt1;
	struct ast_trans_pvt *trans1;
	short *buf1;
	int len1;

	/* muxed frame */
	struct ast_frame frame;
	short *buf;
	int len;
	
	int samples;
	int rsamples;
	int volfactor;
	int fd;
};

/* Prototypes */
static struct ast_channel *local_get_channel_by_name(char *name);
static struct ast_channel *local_get_channel_begin_name(char *name);
static struct ast_channel *local_channel_walk(struct ast_channel *chan);
static void spy_release(struct ast_channel *chan, void *data);
static void *spy_alloc(struct ast_channel *chan, void *params);
static struct ast_frame *spy_queue_shift(struct ast_channel_spy *spy, int qnum);
static void ast_flush_spy_queue(struct ast_channel_spy *spy);
static int spy_generate(struct ast_channel *chan, void *data, int len, int samples);
static void start_spying(struct ast_channel *chan, struct ast_channel *spychan, struct ast_channel_spy *spy);
static void stop_spying(struct ast_channel *chan, struct ast_channel_spy *spy);
static int channel_spy(struct ast_channel *chan, struct ast_channel *spyee, int *volfactor, int fd);
static int chanspy_exec(struct ast_channel *chan, void *data);


static struct ast_channel *local_get_channel_by_name(char *name) 
{
	struct ast_channel *ret;
	ast_mutex_lock(&modlock);
	if ((ret = ast_get_channel_by_name_locked(name))) {
		ast_mutex_unlock(&ret->lock);
	}
	ast_mutex_unlock(&modlock);

	return ret;
}

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


static void spy_release(struct ast_channel *chan, void *data) 
{
	struct chanspy_translation_helper *csth = data;


	if (csth->trans0) {
		ast_translator_free_path(csth->trans0);
		csth->trans0 = NULL;
	}
	if (csth->trans1) {
		ast_translator_free_path(csth->trans1);
		csth->trans1 = NULL;
	}

	if (csth->buf0) {
		free(csth->buf0);
		csth->buf0 = NULL;
	}
	if (csth->buf1) {
		free(csth->buf1);
		csth->buf1 = NULL;
	}
	if (csth->buf) {
		free(csth->buf);
		csth->buf = NULL;
	}
	return;
}

static void *spy_alloc(struct ast_channel *chan, void *params) 
{
	return params;
}

static struct ast_frame *spy_queue_shift(struct ast_channel_spy *spy, int qnum) 
{
	struct ast_frame *f;
	
	if (qnum < 0 || qnum > 1)
		return NULL;

	f = spy->queue[qnum];
	if (f) {
		spy->queue[qnum] = f->next;
		return f;
	}
	return NULL;
}


static void ast_flush_spy_queue(struct ast_channel_spy *spy) 
{
	struct ast_frame *f=NULL;
	int x = 0;
	ast_mutex_lock(&spy->lock);
	for(x=0;x<2;x++) {
		f = NULL;
		while((f = spy_queue_shift(spy, x))) 
			ast_frfree(f);
	}
	ast_mutex_unlock(&spy->lock);
}

static int spy_generate(struct ast_channel *chan, void *data, int len, int samples) 
{
	struct ast_frame *f, *f0, *f1;
	int x, vf, dlen, maxsamp, loops;
	struct chanspy_translation_helper *csth = data;

	if (csth->rsamples < csth->samples) {
		csth->rsamples += samples;
		return 0;
	} 
	csth->rsamples += samples;
	loops = 0;
	do {
		loops++;
		f = f0 = f1 = NULL;
		x = vf = dlen = maxsamp = 0;
		if (csth->rsamples == csth->samples) {
			csth->rsamples = csth->samples = 0;
		}

		ast_mutex_lock(&csth->spy.lock);
		f0 = spy_queue_shift(&csth->spy, 0);
		f1 = spy_queue_shift(&csth->spy, 1);
		ast_mutex_unlock(&csth->spy.lock);

		if (csth->spy.status == CHANSPY_DONE) {
			return -1;
		}
	
		if (!f0 && !f1) {
			return 0;
		}

		if (f0 && csth->fmt0 && csth->fmt0 != f0->subclass) {
			ast_translator_free_path(csth->trans0);
			csth->trans0 = NULL;
			csth->fmt0 = f0->subclass;
		}

		if (f1 && csth->fmt1 && csth->fmt1 != f1->subclass) {
			ast_translator_free_path(csth->trans1);
			csth->trans1 = NULL;
			csth->fmt1 = f1->subclass;
		}
	
		if (!csth->fmt0 && f0) {
			csth->fmt0 = f0->subclass;
		}

		if (!csth->fmt1 && f1) {
			csth->fmt1 = f1->subclass;
		}

		if (csth->fmt0 && csth->fmt0 != AST_FORMAT_SLINEAR && !csth->trans0) {
			if ((csth->trans0 = ast_translator_build_path(AST_FORMAT_SLINEAR, csth->fmt0)) == NULL) {
				ast_log(LOG_WARNING, "Cannot build a path from %s to slin\n", ast_getformatname(csth->fmt0));
				csth->spy.status = CHANSPY_DONE;
				return -1;
			}
		}
		if (csth->fmt1 && csth->fmt1 != AST_FORMAT_SLINEAR && !csth->trans1) {
			if ((csth->trans1 = ast_translator_build_path(AST_FORMAT_SLINEAR, csth->fmt1)) == NULL) {
				ast_log(LOG_WARNING, "Cannot build a path from %s to slin\n", ast_getformatname(csth->fmt1));
				csth->spy.status = CHANSPY_DONE;
				return -1;
			}
		}
	
		if (f0) {
			if (csth->trans0) {
				if ((f = ast_translate(csth->trans0, f0, 0))) {
					if (csth->len0 < f->datalen) {
						if (!csth->len0) {
							if (!(csth->buf0 = malloc(f->datalen * 2))) {
								csth->spy.status = CHANSPY_DONE;
								return -1;
							}
						} else {
							if (!realloc(csth->buf0, f->datalen * 2)) {
								csth->spy.status = CHANSPY_DONE;
								return -1;
							}
						}
						csth->len0 = f->datalen;
					}
					memcpy(csth->buf0, f->data, f->datalen);
					maxsamp = f->samples;
					ast_frfree(f);
				} else {
					return 0;
				}
			} else {
				if (csth->len0 < f0->datalen) {
					if (!csth->len0) {
						if (!(csth->buf0 = malloc(f0->datalen * 2))) {
							csth->spy.status = CHANSPY_DONE;
							return -1;
						}
					} else {
						if (!realloc(csth->buf0, f0->datalen * 2)) {
							csth->spy.status = CHANSPY_DONE;
							return -1;
						}
					}
					csth->len0 = f0->datalen;
				}
				memcpy(csth->buf0, f0->data, f0->datalen);
				maxsamp = f0->samples;
			}
		}
	
		if (f1) {
			if (csth->trans1) {
				if ((f = ast_translate(csth->trans1, f1, 0))) {
					if (csth->len1 < f->datalen) {
						if (!csth->len1) {
							if (!(csth->buf1 = malloc(f->datalen * 2))) {
								csth->spy.status = CHANSPY_DONE;
								return -1;
							}
						} else {
							if (!realloc(csth->buf1, f->datalen * 2)) {
								csth->spy.status = CHANSPY_DONE;
								return -1;
							}
						}
						csth->len1 = f->datalen;
					}
					memcpy(csth->buf1, f->data, f->datalen);
					if (f->samples > maxsamp) {
						maxsamp = f->samples;
					}
					ast_frfree(f);
				
				} else {
					return 0;
				}
			} else {
				if (csth->len1 < f1->datalen) {
					if (!csth->len1) {
						if (!(csth->buf1 = malloc(f1->datalen * 2))) {
							csth->spy.status = CHANSPY_DONE;
							return -1;
						}
					} else {
						if (!realloc(csth->buf1, f1->datalen * 2)) {
							csth->spy.status = CHANSPY_DONE;
							return -1;
						}
					}
					csth->len1 = f1->datalen;
				}
				memcpy(csth->buf1, f1->data, f1->datalen);
				if (f1->samples > maxsamp) {
					maxsamp = f1->samples;
				}
			}
		}

		vf = get_volfactor(csth->volfactor);
		vf = minmax(vf, 16);

		dlen = (csth->len0 > csth->len1) ? csth->len0 : csth->len1;

		if (csth->len < dlen) {
			if (!csth->len) {
				if (!(csth->buf = malloc(dlen*2))) {
					csth->spy.status = CHANSPY_DONE;
					return -1;
				}
			} else {
				if (!realloc(csth->buf, dlen * 2)) {
					csth->spy.status = CHANSPY_DONE;
					return -1;
				}
			}
			csth->len = dlen;
		}

		for(x=0; x < maxsamp; x++) {
			if (vf < 0) {
				if (f0) {
					csth->buf0[x] /= abs(vf);
				}
				if (f1) {
					csth->buf1[x] /= abs(vf);
				}
			} else if (vf > 0) {
				if (f0) {
					csth->buf0[x] *= vf;
				}
				if (f1) {
					csth->buf1[x] *= vf;
				}
			}
			if (f0 && f1) {
				if (x < csth->len0 && x < csth->len1) {
					csth->buf[x] = ast_fit_in_short(csth->buf0[x] + csth->buf1[x]);
				} else if (x < csth->len0) {
					csth->buf[x] = csth->buf0[x];
				} else if (x < csth->len1) {
					csth->buf[x] = csth->buf1[x];
				}
			} else if (f0 && x < csth->len0) {
				csth->buf[x] = csth->buf0[x];
			} else if (f1 && x < csth->len1) {
				csth->buf[x] = csth->buf1[x];
			}
		}

		csth->frame.data = csth->buf;
		csth->frame.samples = maxsamp;
		csth->frame.datalen = csth->frame.samples * 2;
		csth->samples += csth->frame.samples;
		
		if (ast_write(chan, &csth->frame)) {
			csth->spy.status = CHANSPY_DONE;
			return -1;
		}
		if (csth->fd) {
			write(csth->fd, csth->frame.data, csth->frame.datalen);
		}

		if (f0) {
			ast_frfree(f0);
		}
		if (f1) {
			ast_frfree(f1);
		}

		if (loops > 10) {
			ast_log(LOG_WARNING, "Too Many Loops Bailing Out....");
			break;
		}
	} while (csth->samples <  csth->rsamples);

	return 0;
}

static struct ast_generator spygen = {
    alloc: spy_alloc, 
    release: spy_release, 
    generate: spy_generate, 
};

static void start_spying(struct ast_channel *chan, struct ast_channel *spychan, struct ast_channel_spy *spy) 
{

	struct ast_channel_spy *cptr=NULL;
	struct ast_channel *peer;


	ast_log(LOG_WARNING, "Attaching %s to %s\n", spychan->name, chan->name);


	ast_mutex_lock(&chan->lock);
	if (chan->spiers) {
		for(cptr=chan->spiers;cptr && cptr->next;cptr=cptr->next);
		cptr->next = spy;
	} else {
		chan->spiers = spy;
	}
	ast_mutex_unlock(&chan->lock);
	if ( ast_test_flag(chan, AST_FLAG_NBRIDGE) && (peer = ast_bridged_channel(chan))) {
		ast_softhangup(peer, AST_SOFTHANGUP_UNBRIDGE);	
	}

}

static void stop_spying(struct ast_channel *chan, struct ast_channel_spy *spy) 
{
	struct ast_channel_spy *cptr=NULL, *prev=NULL;
	int count = 0;

	while(ast_mutex_trylock(&chan->lock)) {
		/* if its locked already it's almost surely hanging up and we are too late 
		   we can safely remove the head pointer if it points at us without needing a lock.
		   since everybody spying will be in the same boat whomever is pointing at the head
		   will surely erase it which is all we really need since it's a linked list of
		   staticly declared structs that belong to each spy.
		*/
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

static int channel_spy(struct ast_channel *chan, struct ast_channel *spyee, int *volfactor, int fd) 
{
	struct chanspy_translation_helper csth;
	int running = 1, res = 0, x = 0;
	char inp[24];
	char *name=NULL;
	struct ast_frame *f;

	if (chan && !ast_check_hangup(chan) && spyee && !ast_check_hangup(spyee)) {
		memset(inp, 0, sizeof(inp));
		name = ast_strdupa(spyee->name);
		if (option_verbose >= 2)
			ast_verbose(VERBOSE_PREFIX_2 "Spying on channel %s\n", name);

		memset(&csth, 0, sizeof(csth));
		csth.spy.status = CHANSPY_RUNNING;
		ast_mutex_init(&csth.spy.lock);
		csth.volfactor = *volfactor;
		csth.frame.frametype = AST_FRAME_VOICE;
		csth.frame.subclass = AST_FORMAT_SLINEAR;
		csth.frame.datalen = 320;
		csth.frame.samples = 160;
		if (fd) {
			csth.fd = fd;
		}
		start_spying(spyee, chan, &csth.spy);
		ast_activate_generator(chan, &spygen, &csth);

		while(csth.spy.status == CHANSPY_RUNNING && 
			  chan && !ast_check_hangup(chan) && 
			  spyee && 
			  !ast_check_hangup(spyee) 
			  && running == 1 && 
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
					csth.volfactor++;
					if (csth.volfactor > 4) {
						csth.volfactor = -4;
					}
					if (option_verbose > 2) {
						ast_verbose(VERBOSE_PREFIX_3"Setting spy volume on %s to %d\n", chan->name, csth.volfactor);
					}
					*volfactor = csth.volfactor;
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
		ast_flush_spy_queue(&csth.spy);
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
		peer_name[AST_NAME_STRLEN],
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


	if (!(args = ast_strdupa((char *)data))) {
		ast_log(LOG_ERROR, "Out of memory!\n");
		return -1;
	}

	oldrf = chan->readformat;
	oldwf = chan->writeformat;
	if (ast_set_read_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Read Format.\n");
		return -1;
	}
	
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR) < 0) {
		ast_log(LOG_ERROR, "Could Not Set Write Format.\n");
		return -1;
	}

	LOCAL_USER_ADD(u);
	ast_answer(chan);

	ast_set_flag(chan, AST_FLAG_SPYING); /* so nobody can spy on us while we are spying */


	if ((argc = ast_separate_app_args(args, '|', argv, sizeof(argv) / sizeof(argv[0])))) {
		spec = argv[0];
		if ( argc > 1) {
			options = argv[1];
		}
		if (ast_strlen_zero(spec) || !strcmp(spec, "all")) {
			spec = NULL;
		}
	}
	
	if (options) {
		char *opts[3];
		ast_parseoptions(chanspy_opts, &flags, opts, options);
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
			if (sscanf(opts[0], "%d", &volfactor) != 1)
				ast_log(LOG_NOTICE, "volfactor must be a number between -4 and 4\n");
			else {
				volfactor = minmax(volfactor, 4);
			}
		}
	}

	if (recbase) {
		char filename[512];
		snprintf(filename,sizeof(filename),"%s/%s.%ld.raw",AST_MONITOR_DIR, recbase, time(NULL));
		if ((fd = open(filename, O_CREAT | O_WRONLY, O_TRUNC)) <= 0) {
			ast_log(LOG_WARNING, "Cannot open %s for recording\n", filename);
			fd = 0;
		}
	}

	for(;;) {
		res = ast_streamfile(chan, "beep", chan->language);
		if (!res)
			res = ast_waitstream(chan, "");
		if (res < 0) {
			ast_clear_flag(chan, AST_FLAG_SPYING);
			break;
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

						strncpy(peer_name, peer->name, AST_NAME_STRLEN);
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
	ALL_DONE(u, res);
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, chanspy_exec, synopsis, desc);
}

char *description(void)
{
	return synopsis;
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
