/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Russell Bryant <russell@digium.com>
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
 * \brief Automatic channel service routines
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <sys/time.h>
#include <signal.h>

#include "asterisk/_private.h" /* prototype for ast_autoservice_init() */

#include "asterisk/pbx.h"
#include "asterisk/frame.h"
#include "asterisk/sched.h"
#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/translate.h"
#include "asterisk/manager.h"
#include "asterisk/chanvars.h"
#include "asterisk/linkedlists.h"
#include "asterisk/indications.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"

#define MAX_AUTOMONS 1500

struct asent {
	struct ast_channel *chan;
	/*! This gets incremented each time autoservice gets started on the same
	 *  channel.  It will ensure that it doesn't actually get stopped until
	 *  it gets stopped for the last time. */
	unsigned int use_count;
	unsigned int orig_end_dtmf_flag:1;
	unsigned int ignore_frame_types;
	/*! Frames go on at the head of deferred_frames, so we have the frames
	 *  from newest to oldest.  As we put them at the head of the readq, we'll
	 *  end up with them in the right order for the channel's readq. */
	AST_LIST_HEAD_NOLOCK(, ast_frame) deferred_frames;
	AST_LIST_ENTRY(asent) list;
};

static AST_LIST_HEAD_STATIC(aslist, asent);
static ast_cond_t as_cond;

static pthread_t asthread = AST_PTHREADT_NULL;
static volatile int asexit = 0;

static int as_chan_list_state;

static void *autoservice_run(void *ign)
{
	ast_callid callid = 0;
	struct ast_frame hangup_frame = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = AST_CONTROL_HANGUP,
	};

	while (!asexit) {
		struct ast_channel *mons[MAX_AUTOMONS];
		struct asent *ents[MAX_AUTOMONS];
		struct ast_channel *chan;
		struct asent *as;
		int i, x = 0, ms = 50;
		struct ast_frame *f = NULL;
		struct ast_frame *defer_frame = NULL;

		AST_LIST_LOCK(&aslist);

		/* At this point, we know that no channels that have been removed are going
		 * to get used again. */
		as_chan_list_state++;

		if (AST_LIST_EMPTY(&aslist)) {
			ast_cond_wait(&as_cond, &aslist.lock);
		}

		AST_LIST_TRAVERSE(&aslist, as, list) {
			if (!ast_check_hangup(as->chan)) {
				if (x < MAX_AUTOMONS) {
					ents[x] = as;
					mons[x++] = as->chan;
				} else {
					ast_log(LOG_WARNING, "Exceeded maximum number of automatic monitoring events.  Fix autoservice.c\n");
				}
			}
		}

		AST_LIST_UNLOCK(&aslist);

		if (!x) {
			/* If we don't sleep, this becomes a busy loop, which causes
			 * problems when Asterisk runs at a different priority than other
			 * user processes.  As long as we check for new channels at least
			 * once every 10ms, we should be fine. */
			usleep(10000);
			continue;
		}

		chan = ast_waitfor_n(mons, x, &ms);
		if (!chan) {
			continue;
		}

		callid = ast_channel_callid(chan);
		ast_callid_threadassoc_change(callid);

		f = ast_read(chan);

		if (!f) {
			/* No frame means the channel has been hung up.
			 * A hangup frame needs to be queued here as ast_waitfor() may
			 * never return again for the condition to be detected outside
			 * of autoservice.  So, we'll leave a HANGUP queued up so the
			 * thread in charge of this channel will know. */

			defer_frame = &hangup_frame;
		} else if (ast_is_deferrable_frame(f)) {
			defer_frame = f;
		} else {
			/* Can't defer. Discard and continue with next. */
			ast_frfree(f);
			continue;
		}

		for (i = 0; i < x; i++) {
			struct ast_frame *dup_f;

			if (mons[i] != chan) {
				continue;
			}

			if (!f) { /* defer_frame == &hangup_frame */
				if ((dup_f = ast_frdup(defer_frame))) {
					AST_LIST_INSERT_HEAD(&ents[i]->deferred_frames, dup_f, frame_list);
				}
			} else {
				if ((dup_f = ast_frisolate(defer_frame))) {
					AST_LIST_INSERT_HEAD(&ents[i]->deferred_frames, dup_f, frame_list);
				}
				if (dup_f != defer_frame) {
					ast_frfree(defer_frame);
				}
			}

			break;
		}
		/* The ast_waitfor_n() call will only read frames from
		 * the channels' file descriptors. If ast_waitfor_n()
		 * returns non-NULL, then one of the channels in the
		 * mons array must have triggered the return. It's
		 * therefore impossible that we got here while (i >= x).
		 * If we did, we'd need to ast_frfree(f) if (f). */
	}

	ast_callid_threadassoc_change(0);
	asthread = AST_PTHREADT_NULL;

	return NULL;
}

int ast_autoservice_start(struct ast_channel *chan)
{
	int res = 0;
	struct asent *as;

	AST_LIST_LOCK(&aslist);
	AST_LIST_TRAVERSE(&aslist, as, list) {
		if (as->chan == chan) {
			as->use_count++;
			break;
		}
	}
	AST_LIST_UNLOCK(&aslist);

	if (as) {
		/* Entry exists, autoservice is already handling this channel */
		return 0;
	}

	if (!(as = ast_calloc(1, sizeof(*as))))
		return -1;

	/* New entry created */
	as->chan = chan;
	as->use_count = 1;

	ast_channel_lock(chan);
	as->orig_end_dtmf_flag = ast_test_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY) ? 1 : 0;
	if (!as->orig_end_dtmf_flag)
		ast_set_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY);
	ast_channel_unlock(chan);

	AST_LIST_LOCK(&aslist);

	if (AST_LIST_EMPTY(&aslist) && asthread != AST_PTHREADT_NULL) {
		ast_cond_signal(&as_cond);
	}

	AST_LIST_INSERT_HEAD(&aslist, as, list);

	if (asthread == AST_PTHREADT_NULL) { /* need start the thread */
		if (ast_pthread_create_background(&asthread, NULL, autoservice_run, NULL)) {
			ast_log(LOG_WARNING, "Unable to create autoservice thread :(\n");
			/* There will only be a single member in the list at this point,
			   the one we just added. */
			AST_LIST_REMOVE(&aslist, as, list);
			ast_free(as);
			asthread = AST_PTHREADT_NULL;
			res = -1;
		} else {
			pthread_kill(asthread, SIGURG);
		}
	}

	AST_LIST_UNLOCK(&aslist);

	return res;
}

int ast_autoservice_stop(struct ast_channel *chan)
{
	int res = -1;
	struct asent *as, *removed = NULL;
	struct ast_frame *f;
	int chan_list_state;

	AST_LIST_LOCK(&aslist);

	/* Save the autoservice channel list state.  We _must_ verify that the channel
	 * list has been rebuilt before we return.  Because, after we return, the channel
	 * could get destroyed and we don't want our poor autoservice thread to step on
	 * it after its gone! */
	chan_list_state = as_chan_list_state;

	/* Find the entry, but do not free it because it still can be in the
	   autoservice thread array */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&aslist, as, list) {
		if (as->chan == chan) {
			as->use_count--;
			if (as->use_count < 1) {
				AST_LIST_REMOVE_CURRENT(list);
				removed = as;
			}
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (removed && asthread != AST_PTHREADT_NULL) {
		pthread_kill(asthread, SIGURG);
	}

	AST_LIST_UNLOCK(&aslist);

	if (!removed) {
		return 0;
	}

	/* Wait while autoservice thread rebuilds its list. */
	while (chan_list_state == as_chan_list_state) {
		usleep(1000);
	}

	/* Now autoservice thread should have no references to our entry
	   and we can safely destroy it */

	if (!ast_channel_softhangup_internal_flag(chan)) {
		res = 0;
	}

	if (!as->orig_end_dtmf_flag) {
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_END_DTMF_ONLY);
	}

	ast_channel_lock(chan);
	while ((f = AST_LIST_REMOVE_HEAD(&as->deferred_frames, frame_list))) {
		if (!((1 << f->frametype) & as->ignore_frame_types)) {
			ast_queue_frame_head(chan, f);
		}
		ast_frfree(f);
	}
	ast_channel_unlock(chan);

	ast_free(as);

	return res;
}

void ast_autoservice_chan_hangup_peer(struct ast_channel *chan, struct ast_channel *peer)
{
	if (chan && !ast_autoservice_start(chan)) {
		ast_hangup(peer);
		ast_autoservice_stop(chan);
	} else {
		ast_hangup(peer);
	}
}

int ast_autoservice_ignore(struct ast_channel *chan, enum ast_frame_type ftype)
{
	struct asent *as;
	int res = -1;

	AST_LIST_LOCK(&aslist);
	AST_LIST_TRAVERSE(&aslist, as, list) {
		if (as->chan == chan) {
			res = 0;
			as->ignore_frame_types |= (1 << ftype);
			break;
		}
	}
	AST_LIST_UNLOCK(&aslist);
	return res;
}

static void autoservice_shutdown(void)
{
	pthread_t th = asthread;
	asexit = 1;
	if (th != AST_PTHREADT_NULL) {
		ast_cond_signal(&as_cond);
		pthread_kill(th, SIGURG);
		pthread_join(th, NULL);
	}
}

void ast_autoservice_init(void)
{
	ast_register_cleanup(autoservice_shutdown);
	ast_cond_init(&as_cond, NULL);
}
