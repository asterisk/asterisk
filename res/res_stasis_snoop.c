/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Stasis application snoop control support.
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_app_snoop.h"
#include "asterisk/audiohook.h"
#include "asterisk/pbx.h"
#include "asterisk/timing.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/json.h"
#include "asterisk/format_cache.h"

/*! \brief The interval (in milliseconds) that the Snoop timer is triggered, also controls length of audio within frames */
#define SNOOP_INTERVAL 20

/*! \brief Index used to keep Snoop channel names unique */
static unsigned int chan_idx;

/*! \brief Structure which contains all of the snoop information */
struct stasis_app_snoop {
	/*! \brief Timer used for waking up Stasis thread */
	struct ast_timer *timer;
	/*! \brief Audiohook used to spy on the channel */
	struct ast_audiohook spy;
	/*! \brief Direction for spying */
	enum ast_audiohook_direction spy_direction;
	/*! \brief Number of samples to be read in when spying */
	unsigned int spy_samples;
	/*! \brief Format in use by the spy audiohook */
	struct ast_format *spy_format;
	/*! \brief Audiohook used to whisper on the channel */
	struct ast_audiohook whisper;
	/*! \brief Direction for whispering */
	enum ast_audiohook_direction whisper_direction;
	/*! \brief Stasis application and arguments */
	struct ast_str *app;
	/*! \brief Snoop channel */
	struct ast_channel *chan;
	/*! \brief Whether the spy capability is active or not */
	unsigned int spy_active:1;
	/*! \brief Whether the whisper capability is active or not */
	unsigned int whisper_active:1;
	/*! \brief Uniqueid of the channel this snoop is snooping on */
	char uniqueid[AST_MAX_UNIQUEID];
};

/*! \brief Destructor for snoop structure */
static void snoop_destroy(void *obj)
{
	struct stasis_app_snoop *snoop = obj;

	if (snoop->timer) {
		ast_timer_close(snoop->timer);
	}

	if (snoop->spy_active) {
		ast_audiohook_destroy(&snoop->spy);
	}

	if (snoop->whisper_active) {
		ast_audiohook_destroy(&snoop->whisper);
	}

	ast_free(snoop->app);

	ast_channel_cleanup(snoop->chan);
}

/*! \internal
 * \brief Publish the chanspy message over Stasis-Core
 * \param snoop The snoop structure
 * \start start If non-zero, the spying is starting. Otherwise, the spyer is
 * finishing
 */
static void publish_chanspy_message(struct stasis_app_snoop *snoop, int start)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	RAII_VAR(struct ast_multi_channel_blob *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, snoop_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, spyee_snapshot, NULL, ao2_cleanup);
	struct stasis_message_type *type = start ? ast_channel_chanspy_start_type(): ast_channel_chanspy_stop_type();

	blob = ast_json_null();
	if (!blob || !type) {
		return;
	}

	payload = ast_multi_channel_blob_create(blob);
	if (!payload) {
		return;
	}

	snoop_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(snoop->chan));
	if (!snoop_snapshot) {
		return;
	}
	ast_multi_channel_blob_add_channel(payload, "spyer_channel", snoop_snapshot);

	spyee_snapshot = ast_channel_snapshot_get_latest(snoop->uniqueid);
	if (spyee_snapshot) {
		ast_multi_channel_blob_add_channel(payload, "spyee_channel", spyee_snapshot);
	}

	message = stasis_message_create(type, payload);
	if (!message) {
		return;
	}

	stasis_publish(ast_channel_topic(snoop->chan), message);
}

/*! \brief Callback function for writing to a Snoop whisper audiohook */
static int snoop_write(struct ast_channel *chan, struct ast_frame *frame)
{
	struct stasis_app_snoop *snoop = ast_channel_tech_pvt(chan);

	if (!snoop->whisper_active) {
		return 0;
	}

	ast_audiohook_lock(&snoop->whisper);
	if (snoop->whisper_direction == AST_AUDIOHOOK_DIRECTION_BOTH) {
		ast_audiohook_write_frame(&snoop->whisper, AST_AUDIOHOOK_DIRECTION_READ, frame);
		ast_audiohook_write_frame(&snoop->whisper, AST_AUDIOHOOK_DIRECTION_WRITE, frame);
	} else {
		ast_audiohook_write_frame(&snoop->whisper, snoop->whisper_direction, frame);
	}
	ast_audiohook_unlock(&snoop->whisper);

	return 0;
}

/*! \brief Callback function for reading from a Snoop channel */
static struct ast_frame *snoop_read(struct ast_channel *chan)
{
	struct stasis_app_snoop *snoop = ast_channel_tech_pvt(chan);
	struct ast_frame *frame = NULL;

	/* If we fail to ack the timer OR if any active audiohooks are done hangup */
	if ((ast_timer_ack(snoop->timer, 1) < 0) ||
		(snoop->spy_active && snoop->spy.status != AST_AUDIOHOOK_STATUS_RUNNING) ||
		(snoop->whisper_active && snoop->whisper.status != AST_AUDIOHOOK_STATUS_RUNNING)) {
		return NULL;
	}

	/* Only get audio from the spy audiohook if it is active */
	if (!snoop->spy_active) {
		return &ast_null_frame;
	}

	ast_audiohook_lock(&snoop->spy);
	if (snoop->spy_direction == AST_AUDIOHOOK_DIRECTION_WRITE) {
		/*
		 * Frames are still being written to the "in" queue. They must
		 * be read so the queue does not continue to grow, however since
		 * we don't need them for the "out" case they can be dropped.
		 */
		frame = ast_audiohook_read_frame(&snoop->spy, snoop->spy_samples,
						 AST_AUDIOHOOK_DIRECTION_READ, snoop->spy_format);
		ast_free(frame);
	}

	frame = ast_audiohook_read_frame(&snoop->spy, snoop->spy_samples, snoop->spy_direction, snoop->spy_format);
	ast_audiohook_unlock(&snoop->spy);

	return frame ? frame : &ast_null_frame;
}

/*! \brief Callback function for hanging up a Snoop channel */
static int snoop_hangup(struct ast_channel *chan)
{
	struct stasis_app_snoop *snoop = ast_channel_tech_pvt(chan);

	if (snoop->spy_active) {
		ast_audiohook_lock(&snoop->spy);
		ast_audiohook_detach(&snoop->spy);
		ast_audiohook_unlock(&snoop->spy);
	}

	if (snoop->whisper_active) {
		ast_audiohook_lock(&snoop->whisper);
		ast_audiohook_detach(&snoop->whisper);
		ast_audiohook_unlock(&snoop->whisper);
	}

	publish_chanspy_message(snoop, 0);

	ao2_cleanup(snoop);

	ast_channel_tech_pvt_set(chan, NULL);

	return 0;
}

static int snoop_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct stasis_app_snoop *snoop = ast_channel_tech_pvt(oldchan);

	if (snoop->chan != oldchan) {
		return -1;
	}

	ast_channel_unref(snoop->chan);
	ast_channel_ref(newchan);
	snoop->chan = newchan;

	return 0;
}

/*! \brief Channel interface declaration */
static struct ast_channel_tech snoop_tech = {
	.type = "Snoop",
	.description = "Snoop Channel Driver",
	.write = snoop_write,
	.read = snoop_read,
	.hangup = snoop_hangup,
	.fixup = snoop_fixup,
};

/*! \brief Thread used for running the Stasis application */
static void *snoop_stasis_thread(void *obj)
{
	RAII_VAR(struct stasis_app_snoop *, snoop, obj, ao2_cleanup);
	struct ast_app *stasis = pbx_findapp("Stasis");

	if (!stasis) {
		ast_hangup(snoop->chan);
		return NULL;
	}

	pbx_exec(snoop->chan, stasis, ast_str_buffer(snoop->app));

	ast_hangup(snoop->chan);

	return NULL;
}

/*! \brief Internal helper function which sets up and attaches a snoop audiohook */
static int snoop_setup_audiohook(struct ast_channel *chan, enum ast_audiohook_type type, enum stasis_app_snoop_direction requested_direction,
	enum ast_audiohook_direction *direction, struct ast_audiohook *audiohook)
{
	ast_audiohook_init(audiohook, type, "Snoop", 0);

	if (requested_direction == STASIS_SNOOP_DIRECTION_OUT) {
		*direction = AST_AUDIOHOOK_DIRECTION_WRITE;
	} else if (requested_direction == STASIS_SNOOP_DIRECTION_IN) {
		*direction = AST_AUDIOHOOK_DIRECTION_READ;
	} else if (requested_direction == STASIS_SNOOP_DIRECTION_BOTH) {
		*direction = AST_AUDIOHOOK_DIRECTION_BOTH;
	} else {
		return -1;
	}

	return ast_audiohook_attach(chan, audiohook);
}

/*! \brief Helper function which gets the format for a Snoop channel based on the channel being snooped on */
static void snoop_determine_format(struct ast_channel *chan, struct stasis_app_snoop *snoop)
{
	SCOPED_CHANNELLOCK(lock, chan);
	unsigned int rate = MAX(ast_format_get_sample_rate(ast_channel_rawwriteformat(chan)),
		ast_format_get_sample_rate(ast_channel_rawreadformat(chan)));

	snoop->spy_format = ast_format_cache_get_slin_by_rate(rate);
}

struct ast_channel *stasis_app_control_snoop(struct ast_channel *chan,
	enum stasis_app_snoop_direction spy, enum stasis_app_snoop_direction whisper,
	const char *app, const char *app_args, const char *snoop_id)
{
	RAII_VAR(struct stasis_app_snoop *, snoop, NULL, ao2_cleanup);
	struct ast_format_cap *caps;
	pthread_t thread;
	struct ast_assigned_ids assignedids = {
		.uniqueid = snoop_id,
	};

	if (spy == STASIS_SNOOP_DIRECTION_NONE &&
		whisper == STASIS_SNOOP_DIRECTION_NONE) {
		return NULL;
	}

	snoop = ao2_alloc_options(sizeof(*snoop), snoop_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!snoop) {
		return NULL;
	}

	/* Allocate a buffer to store the Stasis application and arguments in */
	snoop->app = ast_str_create(64);
	if (!snoop->app) {
		return NULL;
	}

	ast_str_set(&snoop->app, 0, "%s", app);
	if (!ast_strlen_zero(app_args)) {
		ast_str_append(&snoop->app, 0, ",%s", app_args);
	}

	/* Set up a timer for the Snoop channel so it wakes up at a specific interval */
	snoop->timer = ast_timer_open();
	if (!snoop->timer) {
		return NULL;
	}
	ast_timer_set_rate(snoop->timer, 1000 / SNOOP_INTERVAL);

	/* Determine which signed linear format should be used */
	snoop_determine_format(chan, snoop);

	/* Allocate a Snoop channel and set up various parameters */
	snoop->chan = ast_channel_alloc(1, AST_STATE_UP, "", "", "", "", "", &assignedids, NULL, 0, "Snoop/%s-%08x", ast_channel_uniqueid(chan),
		(unsigned)ast_atomic_fetchadd_int((int *)&chan_idx, +1));
	if (!snoop->chan) {
		return NULL;
	}

	ast_copy_string(snoop->uniqueid, ast_channel_uniqueid(chan), sizeof(snoop->uniqueid));

	/* To keep the channel valid on the Snoop structure until it is destroyed we bump the ref up here */
	ast_channel_ref(snoop->chan);

	ast_channel_tech_set(snoop->chan, &snoop_tech);
	ao2_ref(snoop, +1);
	ast_channel_tech_pvt_set(snoop->chan, snoop);
	ast_channel_set_fd(snoop->chan, 0, ast_timer_fd(snoop->timer));

	/* The format on the Snoop channel will be this signed linear format, and it will never change */
	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_channel_unlock(snoop->chan);
		ast_hangup(snoop->chan);
		return NULL;
	}
	ast_format_cap_append(caps, snoop->spy_format, 0);
	ast_channel_nativeformats_set(snoop->chan, caps);
	ao2_ref(caps, -1);

	ast_channel_set_writeformat(snoop->chan, snoop->spy_format);
	ast_channel_set_rawwriteformat(snoop->chan, snoop->spy_format);
	ast_channel_set_readformat(snoop->chan, snoop->spy_format);
	ast_channel_set_rawreadformat(snoop->chan, snoop->spy_format);

	ast_channel_unlock(snoop->chan);

	if (spy != STASIS_SNOOP_DIRECTION_NONE) {
		if (snoop_setup_audiohook(chan, AST_AUDIOHOOK_TYPE_SPY, spy, &snoop->spy_direction, &snoop->spy)) {
			ast_hangup(snoop->chan);
			return NULL;
		}

		snoop->spy_samples = ast_format_get_sample_rate(snoop->spy_format) / (1000 / SNOOP_INTERVAL);
		snoop->spy_active = 1;
	}

	/* If whispering is enabled set up the audiohook */
	if (whisper != STASIS_SNOOP_DIRECTION_NONE) {
		if (snoop_setup_audiohook(chan, AST_AUDIOHOOK_TYPE_WHISPER, whisper, &snoop->whisper_direction, &snoop->whisper)) {
			ast_hangup(snoop->chan);
			return NULL;
		}

		snoop->whisper_active = 1;
	}

	/* Create the thread which services the Snoop channel */
	ao2_ref(snoop, +1);
	if (ast_pthread_create_detached_background(&thread, NULL, snoop_stasis_thread, snoop)) {
		ao2_cleanup(snoop);

		/* No other thread is servicing this channel so we can immediately hang it up */
		ast_hangup(snoop->chan);
		return NULL;
	}

	publish_chanspy_message(snoop, 1);

	/* The caller of this has a reference as well */
	return ast_channel_ref(snoop->chan);
}

static int load_module(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Stasis application snoop support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis");
