/*
 * abstract_jb: common implementation-independent jitterbuffer stuff
 *
 * Copyright (C) 2005, Attractel OOD
 *
 * Contributors:
 * Slav Klenov <slav@securax.org>
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
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
 */

/*! \file
 *
 * \brief Common implementation-independent jitterbuffer stuff.
 *
 * \author Slav Klenov <slav@securax.org>
 *
 *
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/term.h"
#include "asterisk/utils.h"

#include "asterisk/abstract_jb.h"
#include "fixedjitterbuf.h"
#include "jitterbuf.h"

/*! Internal jb flags */
enum {
	JB_USE =                  (1 << 0),
	JB_TIMEBASE_INITIALIZED = (1 << 1),
	JB_CREATED =              (1 << 2)
};

/* Hooks for the abstract jb implementation */

/*! \brief Create */
typedef void * (*jb_create_impl)(struct ast_jb_conf *general_config, long resynch_threshold);
/*! \brief Destroy */
typedef void (*jb_destroy_impl)(void *jb);
/*! \brief Put first frame */
typedef int (*jb_put_first_impl)(void *jb, struct ast_frame *fin, long now);
/*! \brief Put frame */
typedef int (*jb_put_impl)(void *jb, struct ast_frame *fin, long now);
/*! \brief Get frame for now */
typedef int (*jb_get_impl)(void *jb, struct ast_frame **fout, long now, long interpl);
/*! \brief Get next */
typedef long (*jb_next_impl)(void *jb);
/*! \brief Remove first frame */
typedef int (*jb_remove_impl)(void *jb, struct ast_frame **fout);
/*! \brief Force resynch */
typedef void (*jb_force_resynch_impl)(void *jb);
/*! \brief Empty and reset jb */
typedef void (*jb_empty_and_reset_impl)(void *jb);

/*!
 * \brief Jitterbuffer implementation private struct.
 */
struct ast_jb_impl
{
	char name[AST_JB_IMPL_NAME_SIZE];
	jb_create_impl create;
	jb_destroy_impl destroy;
	jb_put_first_impl put_first;
	jb_put_impl put;
	jb_get_impl get;
	jb_next_impl next;
	jb_remove_impl remove;
	jb_force_resynch_impl force_resync;
	jb_empty_and_reset_impl empty_and_reset;
};

/* Implementation functions */
/* fixed */
static void *jb_create_fixed(struct ast_jb_conf *general_config, long resynch_threshold);
static void jb_destroy_fixed(void *jb);
static int jb_put_first_fixed(void *jb, struct ast_frame *fin, long now);
static int jb_put_fixed(void *jb, struct ast_frame *fin, long now);
static int jb_get_fixed(void *jb, struct ast_frame **fout, long now, long interpl);
static long jb_next_fixed(void *jb);
static int jb_remove_fixed(void *jb, struct ast_frame **fout);
static void jb_force_resynch_fixed(void *jb);
static void jb_empty_and_reset_fixed(void *jb);
/* adaptive */
static void * jb_create_adaptive(struct ast_jb_conf *general_config, long resynch_threshold);
static void jb_destroy_adaptive(void *jb);
static int jb_put_first_adaptive(void *jb, struct ast_frame *fin, long now);
static int jb_put_adaptive(void *jb, struct ast_frame *fin, long now);
static int jb_get_adaptive(void *jb, struct ast_frame **fout, long now, long interpl);
static long jb_next_adaptive(void *jb);
static int jb_remove_adaptive(void *jb, struct ast_frame **fout);
static void jb_force_resynch_adaptive(void *jb);
static void jb_empty_and_reset_adaptive(void *jb);

/* Available jb implementations */
static const struct ast_jb_impl avail_impl[] = {
	{
		.name = "fixed",
		.create = jb_create_fixed,
		.destroy = jb_destroy_fixed,
		.put_first = jb_put_first_fixed,
		.put = jb_put_fixed,
		.get = jb_get_fixed,
		.next = jb_next_fixed,
		.remove = jb_remove_fixed,
		.force_resync = jb_force_resynch_fixed,
		.empty_and_reset = jb_empty_and_reset_fixed,
	},
	{
		.name = "adaptive",
		.create = jb_create_adaptive,
		.destroy = jb_destroy_adaptive,
		.put_first = jb_put_first_adaptive,
		.put = jb_put_adaptive,
		.get = jb_get_adaptive,
		.next = jb_next_adaptive,
		.remove = jb_remove_adaptive,
		.force_resync = jb_force_resynch_adaptive,
		.empty_and_reset = jb_empty_and_reset_adaptive,
	}
};

static int default_impl = 0;


/*! Abstract return codes */
enum {
	JB_IMPL_OK,
	JB_IMPL_DROP,
	JB_IMPL_INTERP,
	JB_IMPL_NOFRAME
};

/* Translations between impl and abstract return codes */
static const int fixed_to_abstract_code[] =
	{JB_IMPL_OK, JB_IMPL_DROP, JB_IMPL_INTERP, JB_IMPL_NOFRAME};
static const int adaptive_to_abstract_code[] =
	{JB_IMPL_OK, JB_IMPL_NOFRAME, JB_IMPL_NOFRAME, JB_IMPL_INTERP, JB_IMPL_DROP, JB_IMPL_OK};

/* JB_GET actions (used only for the frames log) */
static const char * const jb_get_actions[] = {"Delivered", "Dropped", "Interpolated", "No"};

/*! \brief Macros for the frame log files */
#define jb_framelog(...) do { \
	if (jb->logfile) { \
		fprintf(jb->logfile, __VA_ARGS__); \
		fflush(jb->logfile); \
	} \
} while (0)


/* Internal utility functions */
static void jb_choose_impl(struct ast_channel *chan);
static void jb_get_and_deliver(struct ast_channel *chan);
static int create_jb(struct ast_channel *chan, struct ast_frame *first_frame);
static long get_now(struct ast_jb *jb, struct timeval *tv);


/* Interface ast jb functions impl */


static void jb_choose_impl(struct ast_channel *chan)
{
	struct ast_jb *jb = &chan->jb;
	struct ast_jb_conf *jbconf = &jb->conf;
	const struct ast_jb_impl *test_impl;
	int i, avail_impl_count = ARRAY_LEN(avail_impl);

	jb->impl = &avail_impl[default_impl];

	if (ast_strlen_zero(jbconf->impl)) {
		return;
	}

	for (i = 0; i < avail_impl_count; i++) {
		test_impl = &avail_impl[i];
		if (!strcasecmp(jbconf->impl, test_impl->name)) {
			jb->impl = test_impl;
			return;
		}
	}
}

int ast_jb_do_usecheck(struct ast_channel *c0, struct ast_channel *c1)
{
	struct ast_jb *jb0 = &c0->jb;
	struct ast_jb *jb1 = &c1->jb;
	struct ast_jb_conf *conf0 = &jb0->conf;
	struct ast_jb_conf *conf1 = &jb1->conf;
	int c0_wants_jitter = c0->tech->properties & AST_CHAN_TP_WANTSJITTER;
	int c0_creates_jitter = c0->tech->properties & AST_CHAN_TP_CREATESJITTER;
	int c0_jb_enabled = ast_test_flag(conf0, AST_JB_ENABLED);
	int c0_force_jb = ast_test_flag(conf0, AST_JB_FORCED);
	int c0_jb_timebase_initialized = ast_test_flag(jb0, JB_TIMEBASE_INITIALIZED);
	int c0_jb_created = ast_test_flag(jb0, JB_CREATED);
	int c1_wants_jitter = c1->tech->properties & AST_CHAN_TP_WANTSJITTER;
	int c1_creates_jitter = c1->tech->properties & AST_CHAN_TP_CREATESJITTER;
	int c1_jb_enabled = ast_test_flag(conf1, AST_JB_ENABLED);
	int c1_force_jb = ast_test_flag(conf1, AST_JB_FORCED);
	int c1_jb_timebase_initialized = ast_test_flag(jb1, JB_TIMEBASE_INITIALIZED);
	int c1_jb_created = ast_test_flag(jb1, JB_CREATED);
	int inuse = 0;

	/* Determine whether audio going to c0 needs a jitter buffer */
	if (((!c0_wants_jitter && c1_creates_jitter) || (c0_force_jb && c1_creates_jitter)) && c0_jb_enabled) {
		ast_set_flag(jb0, JB_USE);
		if (!c0_jb_timebase_initialized) {
			if (c1_jb_timebase_initialized) {
				memcpy(&jb0->timebase, &jb1->timebase, sizeof(struct timeval));
			} else {
				gettimeofday(&jb0->timebase, NULL);
			}
			ast_set_flag(jb0, JB_TIMEBASE_INITIALIZED);
		}

		if (!c0_jb_created) {
			jb_choose_impl(c0);
		}

		inuse = 1;
	}

	/* Determine whether audio going to c1 needs a jitter buffer */
	if (((!c1_wants_jitter && c0_creates_jitter) || (c1_force_jb && c0_creates_jitter)) && c1_jb_enabled) {
		ast_set_flag(jb1, JB_USE);
		if (!c1_jb_timebase_initialized) {
			if (c0_jb_timebase_initialized) {
				memcpy(&jb1->timebase, &jb0->timebase, sizeof(struct timeval));
			} else {
				gettimeofday(&jb1->timebase, NULL);
			}
			ast_set_flag(jb1, JB_TIMEBASE_INITIALIZED);
		}

		if (!c1_jb_created) {
			jb_choose_impl(c1);
		}

		inuse = 1;
	}

	return inuse;
}

int ast_jb_get_when_to_wakeup(struct ast_channel *c0, struct ast_channel *c1, int time_left)
{
	struct ast_jb *jb0 = &c0->jb;
	struct ast_jb *jb1 = &c1->jb;
	int c0_use_jb = ast_test_flag(jb0, JB_USE);
	int c0_jb_is_created = ast_test_flag(jb0, JB_CREATED);
	int c1_use_jb = ast_test_flag(jb1, JB_USE);
	int c1_jb_is_created = ast_test_flag(jb1, JB_CREATED);
	int wait, wait0, wait1;
	struct timeval tv_now;

	if (time_left == 0) {
		/* No time left - the bridge will be retried */
		/* TODO: Test disable this */
		/*return 0;*/
	}

	if (time_left < 0) {
		time_left = INT_MAX;
	}

	gettimeofday(&tv_now, NULL);

	wait0 = (c0_use_jb && c0_jb_is_created) ? jb0->next - get_now(jb0, &tv_now) : time_left;
	wait1 = (c1_use_jb && c1_jb_is_created) ? jb1->next - get_now(jb1, &tv_now) : time_left;

	wait = wait0 < wait1 ? wait0 : wait1;
	wait = wait < time_left ? wait : time_left;

	if (wait == INT_MAX) {
		wait = -1;
	} else if (wait < 1) {
		/* don't let wait=0, because this can cause the pbx thread to loop without any sleeping at all */
		wait = 1;
	}

	return wait;
}


int ast_jb_put(struct ast_channel *chan, struct ast_frame *f)
{
	struct ast_jb *jb = &chan->jb;
	const struct ast_jb_impl *jbimpl = jb->impl;
	void *jbobj = jb->jbobj;
	struct ast_frame *frr;
	long now = 0;

	if (!ast_test_flag(jb, JB_USE))
		return -1;

	if (f->frametype != AST_FRAME_VOICE) {
		if (f->frametype == AST_FRAME_DTMF && ast_test_flag(jb, JB_CREATED)) {
			jb_framelog("JB_PUT {now=%ld}: Received DTMF frame. Force resynching jb...\n", now);
			jbimpl->force_resync(jbobj);
		}

		return -1;
	}

	/* We consider an enabled jitterbuffer should receive frames with valid timing info. */
	if (!ast_test_flag(f, AST_FRFLAG_HAS_TIMING_INFO) || f->len < 2 || f->ts < 0) {
		ast_log(LOG_WARNING, "%s received frame with invalid timing info: "
			"has_timing_info=%d, len=%ld, ts=%ld, src=%s\n",
			chan->name, ast_test_flag(f, AST_FRFLAG_HAS_TIMING_INFO), f->len, f->ts, f->src);
		return -1;
	}

	frr = ast_frdup(f);

	if (!frr) {
		ast_log(LOG_ERROR, "Failed to isolate frame for the jitterbuffer on channel '%s'\n", chan->name);
		return -1;
	}

	if (!ast_test_flag(jb, JB_CREATED)) {
		if (create_jb(chan, frr)) {
			ast_frfree(frr);
			/* Disable the jitterbuffer */
			ast_clear_flag(jb, JB_USE);
			return -1;
		}

		ast_set_flag(jb, JB_CREATED);
		return 0;
	} else {
		now = get_now(jb, NULL);
		if (jbimpl->put(jbobj, frr, now) != JB_IMPL_OK) {
			jb_framelog("JB_PUT {now=%ld}: Dropped frame with ts=%ld and len=%ld\n", now, frr->ts, frr->len);
			ast_frfree(frr);
			/*return -1;*/
			/* TODO: Check this fix - should return 0 here, because the dropped frame shouldn't
			   be delivered at all */
			return 0;
		}

		jb->next = jbimpl->next(jbobj);

		jb_framelog("JB_PUT {now=%ld}: Queued frame with ts=%ld and len=%ld\n", now, frr->ts, frr->len);

		return 0;
	}
}


void ast_jb_get_and_deliver(struct ast_channel *c0, struct ast_channel *c1)
{
	struct ast_jb *jb0 = &c0->jb;
	struct ast_jb *jb1 = &c1->jb;
	int c0_use_jb = ast_test_flag(jb0, JB_USE);
	int c0_jb_is_created = ast_test_flag(jb0, JB_CREATED);
	int c1_use_jb = ast_test_flag(jb1, JB_USE);
	int c1_jb_is_created = ast_test_flag(jb1, JB_CREATED);

	if (c0_use_jb && c0_jb_is_created)
		jb_get_and_deliver(c0);

	if (c1_use_jb && c1_jb_is_created)
		jb_get_and_deliver(c1);
}


static void jb_get_and_deliver(struct ast_channel *chan)
{
	struct ast_jb *jb = &chan->jb;
	const struct ast_jb_impl *jbimpl = jb->impl;
	void *jbobj = jb->jbobj;
	struct ast_frame *f, finterp = { .frametype = AST_FRAME_VOICE, };
	long now;
	int interpolation_len, res;

	now = get_now(jb, NULL);
	jb->next = jbimpl->next(jbobj);
	if (now < jb->next) {
		jb_framelog("\tJB_GET {now=%ld}: now < next=%ld\n", now, jb->next);
		return;
	}

	while (now >= jb->next) {
		interpolation_len = ast_codec_interp_len(&jb->last_format);

		res = jbimpl->get(jbobj, &f, now, interpolation_len);

		switch (res) {
		case JB_IMPL_OK:
			/* deliver the frame */
			ast_write(chan, f);
		case JB_IMPL_DROP:
			jb_framelog("\tJB_GET {now=%ld}: %s frame with ts=%ld and len=%ld\n",
				now, jb_get_actions[res], f->ts, f->len);
			ast_format_copy(&jb->last_format, &f->subclass.format);
			ast_frfree(f);
			break;
		case JB_IMPL_INTERP:
			/* interpolate a frame */
			f = &finterp;
			ast_format_copy(&f->subclass.format, &jb->last_format);
			f->samples  = interpolation_len * 8;
			f->src  = "JB interpolation";
			f->delivery = ast_tvadd(jb->timebase, ast_samp2tv(jb->next, 1000));
			f->offset = AST_FRIENDLY_OFFSET;
			/* deliver the interpolated frame */
			ast_write(chan, f);
			jb_framelog("\tJB_GET {now=%ld}: Interpolated frame with len=%d\n", now, interpolation_len);
			break;
		case JB_IMPL_NOFRAME:
			ast_log(LOG_WARNING,
				"JB_IMPL_NOFRAME is returned from the %s jb when now=%ld >= next=%ld, jbnext=%ld!\n",
				jbimpl->name, now, jb->next, jbimpl->next(jbobj));
			jb_framelog("\tJB_GET {now=%ld}: No frame for now!?\n", now);
			return;
		default:
			ast_log(LOG_ERROR, "This should never happen!\n");
			ast_assert("JB type unknown" == NULL);
			break;
		}

		jb->next = jbimpl->next(jbobj);
	}
}


static int create_jb(struct ast_channel *chan, struct ast_frame *frr)
{
	struct ast_jb *jb = &chan->jb;
	struct ast_jb_conf *jbconf = &jb->conf;
	const struct ast_jb_impl *jbimpl = jb->impl;
	void *jbobj;
	struct ast_channel *bridged;
	long now;
	char logfile_pathname[20 + AST_JB_IMPL_NAME_SIZE + 2*AST_CHANNEL_NAME + 1];
	char name1[AST_CHANNEL_NAME], name2[AST_CHANNEL_NAME], *tmp;
	int res;

	jbobj = jb->jbobj = jbimpl->create(jbconf, jbconf->resync_threshold);
	if (!jbobj) {
		ast_log(LOG_WARNING, "Failed to create jitterbuffer on channel '%s'\n", chan->name);
		return -1;
	}

	now = get_now(jb, NULL);
	res = jbimpl->put_first(jbobj, frr, now);

	/* The result of putting the first frame should not differ from OK. However, its possible
	   some implementations (i.e. adaptive's when resynch_threshold is specified) to drop it. */
	if (res != JB_IMPL_OK) {
		ast_log(LOG_WARNING, "Failed to put first frame in the jitterbuffer on channel '%s'\n", chan->name);
		/*
		jbimpl->destroy(jbobj);
		return -1;
		*/
	}

	/* Init next */
	jb->next = jbimpl->next(jbobj);

	/* Init last format for a first time. */
	ast_format_copy(&jb->last_format, &frr->subclass.format);

	/* Create a frame log file */
	if (ast_test_flag(jbconf, AST_JB_LOG)) {
		char safe_logfile[30] = "/tmp/logfile-XXXXXX";
		int safe_fd;
		snprintf(name2, sizeof(name2), "%s", chan->name);
		if ((tmp = strchr(name2, '/'))) {
			*tmp = '#';
		}

		bridged = ast_bridged_channel(chan);
		/* We should always have bridged chan if a jitterbuffer is in use */
		ast_assert(bridged != NULL);

		snprintf(name1, sizeof(name1), "%s", bridged->name);
		if ((tmp = strchr(name1, '/'))) {
			*tmp = '#';
		}

		snprintf(logfile_pathname, sizeof(logfile_pathname),
			"/tmp/ast_%s_jb_%s--%s.log", jbimpl->name, name1, name2);
		unlink(logfile_pathname);
		safe_fd = mkstemp(safe_logfile);
		if (safe_fd < 0 || link(safe_logfile, logfile_pathname) || unlink(safe_logfile) || !(jb->logfile = fdopen(safe_fd, "w+b"))) {
			ast_log(LOG_ERROR, "Failed to create frame log file with pathname '%s': %s\n", logfile_pathname, strerror(errno));
			jb->logfile = NULL;
			if (safe_fd > -1) {
				close(safe_fd);
			}
		}

		if (res == JB_IMPL_OK) {
			jb_framelog("JB_PUT_FIRST {now=%ld}: Queued frame with ts=%ld and len=%ld\n",
				now, frr->ts, frr->len);
		} else {
			jb_framelog("JB_PUT_FIRST {now=%ld}: Dropped frame with ts=%ld and len=%ld\n",
				now, frr->ts, frr->len);
		}
	}

	ast_verb(3, "%s jitterbuffer created on channel %s\n", jbimpl->name, chan->name);

	/* Free the frame if it has not been queued in the jb */
	if (res != JB_IMPL_OK) {
		ast_frfree(frr);
	}

	return 0;
}


void ast_jb_destroy(struct ast_channel *chan)
{
	struct ast_jb *jb = &chan->jb;
	const struct ast_jb_impl *jbimpl = jb->impl;
	void *jbobj = jb->jbobj;
	struct ast_frame *f;

	if (jb->logfile) {
		fclose(jb->logfile);
		jb->logfile = NULL;
	}

	if (ast_test_flag(jb, JB_CREATED)) {
		/* Remove and free all frames still queued in jb */
		while (jbimpl->remove(jbobj, &f) == JB_IMPL_OK) {
			ast_frfree(f);
		}

		jbimpl->destroy(jbobj);
		jb->jbobj = NULL;

		ast_clear_flag(jb, JB_CREATED);

		ast_verb(3, "%s jitterbuffer destroyed on channel %s\n", jbimpl->name, chan->name);
	}
}


static long get_now(struct ast_jb *jb, struct timeval *when)
{
	struct timeval now;

	if (!when) {
		when = &now;
		gettimeofday(when, NULL);
	}

	return ast_tvdiff_ms(*when, jb->timebase);
}


int ast_jb_read_conf(struct ast_jb_conf *conf, const char *varname, const char *value)
{
	int prefixlen = sizeof(AST_JB_CONF_PREFIX) - 1;
	const char *name;
	int tmp;

	if (strncasecmp(AST_JB_CONF_PREFIX, varname, prefixlen)) {
		return -1;
	}

	name = varname + prefixlen;

	if (!strcasecmp(name, AST_JB_CONF_ENABLE)) {
		ast_set2_flag(conf, ast_true(value), AST_JB_ENABLED);
	} else if (!strcasecmp(name, AST_JB_CONF_FORCE)) {
		ast_set2_flag(conf, ast_true(value), AST_JB_FORCED);
	} else if (!strcasecmp(name, AST_JB_CONF_MAX_SIZE)) {
		if ((tmp = atoi(value)) > 0)
			conf->max_size = tmp;
	} else if (!strcasecmp(name, AST_JB_CONF_RESYNCH_THRESHOLD)) {
		if ((tmp = atoi(value)) > 0)
			conf->resync_threshold = tmp;
	} else if (!strcasecmp(name, AST_JB_CONF_IMPL)) {
		if (!ast_strlen_zero(value))
			snprintf(conf->impl, sizeof(conf->impl), "%s", value);
	} else if (!strcasecmp(name, AST_JB_CONF_TARGET_EXTRA)) {
		if (sscanf(value, "%30d", &tmp) == 1) {
			conf->target_extra = tmp;
		}
	} else if (!strcasecmp(name, AST_JB_CONF_LOG)) {
		ast_set2_flag(conf, ast_true(value), AST_JB_LOG);
	} else {
		return -1;
	}

	return 0;
}


void ast_jb_configure(struct ast_channel *chan, const struct ast_jb_conf *conf)
{
	memcpy(&chan->jb.conf, conf, sizeof(*conf));
}


void ast_jb_get_config(const struct ast_channel *chan, struct ast_jb_conf *conf)
{
	memcpy(conf, &chan->jb.conf, sizeof(*conf));
}

void ast_jb_empty_and_reset(struct ast_channel *c0, struct ast_channel *c1)
{
	struct ast_jb *jb0 = &c0->jb;
	struct ast_jb *jb1 = &c1->jb;
	int c0_use_jb = ast_test_flag(jb0, JB_USE);
	int c0_jb_is_created = ast_test_flag(jb0, JB_CREATED);
	int c1_use_jb = ast_test_flag(jb1, JB_USE);
	int c1_jb_is_created = ast_test_flag(jb1, JB_CREATED);

	if (c0_use_jb && c0_jb_is_created && jb0->impl->empty_and_reset) {
		jb0->impl->empty_and_reset(jb0->jbobj);
	}

	if (c1_use_jb && c1_jb_is_created && jb1->impl->empty_and_reset) {
		jb1->impl->empty_and_reset(jb1->jbobj);
	}
}

/* Implementation functions */

/* fixed */
static void * jb_create_fixed(struct ast_jb_conf *general_config, long resynch_threshold)
{
	struct fixed_jb_conf conf;

	conf.jbsize = general_config->max_size;
	conf.resync_threshold = resynch_threshold;

	return fixed_jb_new(&conf);
}

static void jb_destroy_fixed(void *jb)
{
	struct fixed_jb *fixedjb = (struct fixed_jb *) jb;

	/* destroy the jb */
	fixed_jb_destroy(fixedjb);
}


static int jb_put_first_fixed(void *jb, struct ast_frame *fin, long now)
{
	struct fixed_jb *fixedjb = (struct fixed_jb *) jb;
	int res;

	res = fixed_jb_put_first(fixedjb, fin, fin->len, fin->ts, now);

	return fixed_to_abstract_code[res];
}


static int jb_put_fixed(void *jb, struct ast_frame *fin, long now)
{
	struct fixed_jb *fixedjb = (struct fixed_jb *) jb;
	int res;

	res = fixed_jb_put(fixedjb, fin, fin->len, fin->ts, now);

	return fixed_to_abstract_code[res];
}


static int jb_get_fixed(void *jb, struct ast_frame **fout, long now, long interpl)
{
	struct fixed_jb *fixedjb = (struct fixed_jb *) jb;
	struct fixed_jb_frame frame;
	int res;

	res = fixed_jb_get(fixedjb, &frame, now, interpl);
	*fout = frame.data;

	return fixed_to_abstract_code[res];
}


static long jb_next_fixed(void *jb)
{
	struct fixed_jb *fixedjb = (struct fixed_jb *) jb;

	return fixed_jb_next(fixedjb);
}


static int jb_remove_fixed(void *jb, struct ast_frame **fout)
{
	struct fixed_jb *fixedjb = (struct fixed_jb *) jb;
	struct fixed_jb_frame frame;
	int res;

	res = fixed_jb_remove(fixedjb, &frame);
	*fout = frame.data;

	return fixed_to_abstract_code[res];
}


static void jb_force_resynch_fixed(void *jb)
{
	struct fixed_jb *fixedjb = (struct fixed_jb *) jb;

	fixed_jb_set_force_resynch(fixedjb);
}

static void jb_empty_and_reset_fixed(void *jb)
{
	struct fixed_jb *fixedjb = jb;
	struct fixed_jb_frame f;

	while (fixed_jb_remove(fixedjb, &f) == FIXED_JB_OK) {
		ast_frfree(f.data);
	}
}

/* adaptive */

static void *jb_create_adaptive(struct ast_jb_conf *general_config, long resynch_threshold)
{
	jb_conf jbconf;
	jitterbuf *adaptivejb;

	adaptivejb = jb_new();
	if (adaptivejb) {
		jbconf.max_jitterbuf = general_config->max_size;
		jbconf.resync_threshold = general_config->resync_threshold;
		jbconf.max_contig_interp = 10;
		jbconf.target_extra = general_config->target_extra;
		jb_setconf(adaptivejb, &jbconf);
	}

	return adaptivejb;
}


static void jb_destroy_adaptive(void *jb)
{
	jitterbuf *adaptivejb = (jitterbuf *) jb;

	jb_destroy(adaptivejb);
}


static int jb_put_first_adaptive(void *jb, struct ast_frame *fin, long now)
{
	jitterbuf *adaptivejb = (jitterbuf *) jb;

	/* Initialize the offset to that of the first frame's timestamp */
	adaptivejb->info.resync_offset = fin->ts;

	return jb_put_adaptive(jb, fin, now);
}


static int jb_put_adaptive(void *jb, struct ast_frame *fin, long now)
{
	jitterbuf *adaptivejb = (jitterbuf *) jb;
	int res;

	res = jb_put(adaptivejb, fin, JB_TYPE_VOICE, fin->len, fin->ts, now);

	return adaptive_to_abstract_code[res];
}


static int jb_get_adaptive(void *jb, struct ast_frame **fout, long now, long interpl)
{
	jitterbuf *adaptivejb = (jitterbuf *) jb;
	jb_frame frame;
	int res;

	res = jb_get(adaptivejb, &frame, now, interpl);
	*fout = frame.data;

	return adaptive_to_abstract_code[res];
}


static long jb_next_adaptive(void *jb)
{
	jitterbuf *adaptivejb = (jitterbuf *) jb;

	return jb_next(adaptivejb);
}


static int jb_remove_adaptive(void *jb, struct ast_frame **fout)
{
	jitterbuf *adaptivejb = (jitterbuf *) jb;
	jb_frame frame;
	int res;

	res = jb_getall(adaptivejb, &frame);
	*fout = frame.data;

	return adaptive_to_abstract_code[res];
}


static void jb_force_resynch_adaptive(void *jb)
{
}

static void jb_empty_and_reset_adaptive(void *jb)
{
	jitterbuf *adaptivejb = jb;
	jb_frame f;

	while (jb_getall(adaptivejb, &f) == JB_OK) {
		ast_frfree(f.data);
	}

	jb_reset(adaptivejb);
}
