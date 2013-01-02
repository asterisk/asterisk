/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \author Mark Spencer <markster@digium.com>
 *
 * \brief Local Proxy Channel
 * 
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <fcntl.h>
#include <sys/signal.h>

#include "asterisk/lock.h"
#include "asterisk/causes.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/manager.h"
#include "asterisk/stringfields.h"
#include "asterisk/devicestate.h"
#include "asterisk/astobj2.h"

/*** DOCUMENTATION
	<manager name="LocalOptimizeAway" language="en_US">
		<synopsis>
			Optimize away a local channel when possible.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>The channel name to optimize away.</para>
			</parameter>
		</syntax>
		<description>
			<para>A local channel created with "/n" will not automatically optimize away.
			Calling this command on the local channel will clear that flag and allow
			it to optimize away if it's bridged or when it becomes bridged.</para>
		</description>
	</manager>
 ***/

static const char tdesc[] = "Local Proxy Channel Driver";

#define IS_OUTBOUND(a,b) (a == b->chan ? 1 : 0)

/* right now we are treating the locals astobj2 container as a
 * list.  If there is ever a reason to make this more efficient
 * increasing the bucket size would help. */
static const int BUCKET_SIZE = 1;

static struct ao2_container *locals;

static unsigned int name_sequence = 0;

static struct ast_jb_conf g_jb_conf = {
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = "",
	.target_extra = -1,
};

static struct ast_channel *local_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause);
static int local_digit_begin(struct ast_channel *ast, char digit);
static int local_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int local_call(struct ast_channel *ast, const char *dest, int timeout);
static int local_hangup(struct ast_channel *ast);
static int local_answer(struct ast_channel *ast);
static struct ast_frame *local_read(struct ast_channel *ast);
static int local_write(struct ast_channel *ast, struct ast_frame *f);
static int local_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int local_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int local_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen);
static int local_sendtext(struct ast_channel *ast, const char *text);
static int local_devicestate(const char *data);
static struct ast_channel *local_bridgedchannel(struct ast_channel *chan, struct ast_channel *bridge);
static int local_queryoption(struct ast_channel *ast, int option, void *data, int *datalen);
static int local_setoption(struct ast_channel *chan, int option, void *data, int datalen);

/* PBX interface structure for channel registration */
static struct ast_channel_tech local_tech = {
	.type = "Local",
	.description = tdesc,
	.requester = local_request,
	.send_digit_begin = local_digit_begin,
	.send_digit_end = local_digit_end,
	.call = local_call,
	.hangup = local_hangup,
	.answer = local_answer,
	.read = local_read,
	.write = local_write,
	.write_video = local_write,
	.exception = local_read,
	.indicate = local_indicate,
	.fixup = local_fixup,
	.send_html = local_sendhtml,
	.send_text = local_sendtext,
	.devicestate = local_devicestate,
	.bridged_channel = local_bridgedchannel,
	.queryoption = local_queryoption,
	.setoption = local_setoption,
};

/*! \brief the local pvt structure for all channels

	The local channel pvt has two ast_chan objects - the "owner" and the "next channel", the outbound channel

	ast_chan owner -> local_pvt -> ast_chan chan -> yet-another-pvt-depending-on-channel-type

*/
struct local_pvt {
	unsigned int flags;             /*!< Private flags */
	char context[AST_MAX_CONTEXT];  /*!< Context to call */
	char exten[AST_MAX_EXTENSION];  /*!< Extension to call */
	struct ast_format_cap *reqcap;  /*!< Requested format capabilities */
	struct ast_jb_conf jb_conf;     /*!< jitterbuffer configuration for this local channel */
	struct ast_channel *owner;      /*!< Master Channel - Bridging happens here */
	struct ast_channel *chan;       /*!< Outbound channel - PBX is run here */
};

#define LOCAL_ALREADY_MASQED  (1 << 0) /*!< Already masqueraded */
#define LOCAL_LAUNCHED_PBX    (1 << 1) /*!< PBX was launched */
#define LOCAL_NO_OPTIMIZATION (1 << 2) /*!< Do not optimize using masquerading */
#define LOCAL_BRIDGE          (1 << 3) /*!< Report back the "true" channel as being bridged to */
#define LOCAL_MOH_PASSTHRU    (1 << 4) /*!< Pass through music on hold start/stop frames */

/*!
 * \brief Send a pvt in with no locks held and get all locks
 *
 * \note NO locks should be held prior to calling this function
 * \note The pvt must have a ref held before calling this function
 * \note if outchan or outowner is set != NULL after calling this function
 *       those channels are locked and reffed.
 * \note Batman.
 */
static void awesome_locking(struct local_pvt *p, struct ast_channel **outchan, struct ast_channel **outowner)
{
	struct ast_channel *chan = NULL;
	struct ast_channel *owner = NULL;

	ao2_lock(p);
	for (;;) {
		if (p->chan) {
			chan = p->chan;
			ast_channel_ref(chan);
		}
		if (p->owner) {
			owner = p->owner;
			ast_channel_ref(owner);
		}
		ao2_unlock(p);

		/* if we don't have both channels, then this is very easy */
		if (!owner || !chan) {
			if (owner) {
				ast_channel_lock(owner);
			} else if(chan) {
				ast_channel_lock(chan);
			}
		} else {
			/* lock both channels first, then get the pvt lock */
			ast_channel_lock_both(chan, owner);
		}
		ao2_lock(p);

		/* Now that we have all the locks, validate that nothing changed */
		if (p->owner != owner || p->chan != chan) {
			if (owner) {
				ast_channel_unlock(owner);
				owner = ast_channel_unref(owner);
			}
			if (chan) {
				ast_channel_unlock(chan);
				chan = ast_channel_unref(chan);
			}
			continue;
		}

		break;
	}
	*outowner = p->owner;
	*outchan = p->chan;
}

/* Called with ast locked */
static int local_setoption(struct ast_channel *ast, int option, void * data, int datalen)
{
	int res = 0;
	struct local_pvt *p;
	struct ast_channel *otherchan = NULL;
	ast_chan_write_info_t *write_info;

	if (option != AST_OPTION_CHANNEL_WRITE) {
		return -1;
	}

	write_info = data;

	if (write_info->version != AST_CHAN_WRITE_INFO_T_VERSION) {
		ast_log(LOG_ERROR, "The chan_write_info_t type has changed, and this channel hasn't been updated!\n");
		return -1;
	}

	if (!strcmp(write_info->function, "CHANNEL")
		&& !strncasecmp(write_info->data, "hangup_handler_", 15)) {
		/* Block CHANNEL(hangup_handler_xxx) writes to the other local channel. */
		return 0;
	}

	/* get the tech pvt */
	if (!(p = ast_channel_tech_pvt(ast))) {
		return -1;
	}
	ao2_ref(p, 1);
	ast_channel_unlock(ast); /* Held when called, unlock before locking another channel */

	/* get the channel we are supposed to write to */
	ao2_lock(p);
	otherchan = (write_info->chan == p->owner) ? p->chan : p->owner;
	if (!otherchan || otherchan == write_info->chan) {
		res = -1;
		otherchan = NULL;
		ao2_unlock(p);
		goto setoption_cleanup;
	}
	ast_channel_ref(otherchan);

	/* clear the pvt lock before grabbing the channel */
	ao2_unlock(p);

	ast_channel_lock(otherchan);
	res = write_info->write_fn(otherchan, write_info->function, write_info->data, write_info->value);
	ast_channel_unlock(otherchan);

setoption_cleanup:
	ao2_ref(p, -1);
	if (otherchan) {
		ast_channel_unref(otherchan);
	}
	ast_channel_lock(ast); /* Lock back before we leave */
	return res;
}

/*! \brief Adds devicestate to local channels */
static int local_devicestate(const char *data)
{
	char *exten = ast_strdupa(data);
	char *context;
	char *opts;
	int res;
	struct local_pvt *lp;
	struct ao2_iterator it;

	/* Strip options if they exist */
	opts = strchr(exten, '/');
	if (opts) {
		*opts = '\0';
	}

	context = strchr(exten, '@');
	if (!context) {
		ast_log(LOG_WARNING,
			"Someone used Local/%s somewhere without a @context. This is bad.\n", data);
		return AST_DEVICE_INVALID;	
	}
	*context++ = '\0';

	ast_debug(3, "Checking if extension %s@%s exists (devicestate)\n", exten, context);
	res = ast_exists_extension(NULL, context, exten, 1, NULL);
	if (!res) {
		return AST_DEVICE_INVALID;
	}

	res = AST_DEVICE_NOT_INUSE;

	it = ao2_iterator_init(locals, 0);
	for (; (lp = ao2_iterator_next(&it)); ao2_ref(lp, -1)) {
		int is_inuse;

		ao2_lock(lp);
		is_inuse = !strcmp(exten, lp->exten)
			&& !strcmp(context, lp->context)
			&& lp->owner
			&& ast_test_flag(lp, LOCAL_LAUNCHED_PBX);
		ao2_unlock(lp);
		if (is_inuse) {
			res = AST_DEVICE_INUSE;
			ao2_ref(lp, -1);
			break;
		}
	}
	ao2_iterator_destroy(&it);

	return res;
}

/*! \brief Return the bridged channel of a Local channel */
static struct ast_channel *local_bridgedchannel(struct ast_channel *chan, struct ast_channel *bridge)
{
	struct local_pvt *p = ast_channel_tech_pvt(bridge);
	struct ast_channel *bridged = bridge;

	if (!p) {
		ast_debug(1, "Asked for bridged channel on '%s'/'%s', returning <none>\n",
			ast_channel_name(chan), ast_channel_name(bridge));
		return NULL;
	}

	ao2_lock(p);

	if (ast_test_flag(p, LOCAL_BRIDGE)) {
		/* Find the opposite channel */
		bridged = (bridge == p->owner ? p->chan : p->owner);

		/* Now see if the opposite channel is bridged to anything */
		if (!bridged) {
			bridged = bridge;
		} else if (ast_channel_internal_bridged_channel(bridged)) {
			bridged = ast_channel_internal_bridged_channel(bridged);
		}
	}

	ao2_unlock(p);

	return bridged;
}

/* Called with ast locked */
static int local_queryoption(struct ast_channel *ast, int option, void *data, int *datalen)
{
	struct local_pvt *p;
	struct ast_channel *bridged = NULL;
	struct ast_channel *tmp = NULL;
	int res = 0;

	if (option != AST_OPTION_T38_STATE) {
		/* AST_OPTION_T38_STATE is the only supported option at this time */
		return -1;
	}

	/* for some reason the channel is not locked in channel.c when this function is called */
	if (!(p = ast_channel_tech_pvt(ast))) {
		return -1;
	}

	ao2_lock(p);
	if (!(tmp = IS_OUTBOUND(ast, p) ? p->owner : p->chan)) {
		ao2_unlock(p);
		return -1;
	}
	ast_channel_ref(tmp);
	ao2_unlock(p);
	ast_channel_unlock(ast); /* Held when called, unlock before locking another channel */

	ast_channel_lock(tmp);
	if (!(bridged = ast_bridged_channel(tmp))) {
		res = -1;
		ast_channel_unlock(tmp);
		goto query_cleanup;
	}
	ast_channel_ref(bridged);
	ast_channel_unlock(tmp);

query_cleanup:
	if (bridged) {
		res = ast_channel_queryoption(bridged, option, data, datalen, 0);
		bridged = ast_channel_unref(bridged);
	}
	if (tmp) {
		tmp = ast_channel_unref(tmp);
	}
	ast_channel_lock(ast); /* Lock back before we leave */

	return res;
}

/*! \brief queue a frame on a to either the p->owner or p->chan
 *
 * \note the local_pvt MUST have it's ref count bumped before entering this function and
 * decremented after this function is called.  This is a side effect of the deadlock
 * avoidance that is necessary to lock 2 channels and a tech_pvt.  Without a ref counted
 * local_pvt, it is impossible to guarantee it will not be destroyed by another thread
 * during deadlock avoidance.
 */
static int local_queue_frame(struct local_pvt *p, int isoutbound, struct ast_frame *f,
	struct ast_channel *us, int us_locked)
{
	struct ast_channel *other = NULL;

	/* Recalculate outbound channel */
	other = isoutbound ? p->owner : p->chan;

	if (!other) {
		return 0;
	}

	/* do not queue frame if generator is on both local channels */
	if (us && ast_channel_generator(us) && ast_channel_generator(other)) {
		return 0;
	}

	/* grab a ref on the channel before unlocking the pvt,
	 * other can not go away from us now regardless of locking */
	ast_channel_ref(other);
	if (us && us_locked) {
		ast_channel_unlock(us);
	}
	ao2_unlock(p);

	if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_RINGING) {
		ast_setstate(other, AST_STATE_RINGING);
	}
	ast_queue_frame(other, f);

	other = ast_channel_unref(other);
	if (us && us_locked) {
		ast_channel_lock(us);
	}
	ao2_lock(p);

	return 0;
}

static int local_answer(struct ast_channel *ast)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	int isoutbound;
	int res = -1;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1);
	ao2_lock(p);
	isoutbound = IS_OUTBOUND(ast, p);
	if (isoutbound) {
		/* Pass along answer since somebody answered us */
		struct ast_frame answer = { AST_FRAME_CONTROL, { AST_CONTROL_ANSWER } };
		res = local_queue_frame(p, isoutbound, &answer, ast, 1);
	} else {
		ast_log(LOG_WARNING, "Huh?  Local is being asked to answer?\n");
	}
	ao2_unlock(p);
	ao2_ref(p, -1);
	return res;
}

/*!
 * \internal
 * \note This function assumes that we're only called from the "outbound" local channel side
 *
 * \note it is assummed p is locked and reffed before entering this function
 */
static void check_bridge(struct ast_channel *ast, struct local_pvt *p)
{
	struct ast_channel *owner;
	struct ast_channel *chan;
	struct ast_channel *bridged_chan;
	struct ast_frame *f;

	/* Do a few conditional checks early on just to see if this optimization is possible */
	if (ast_test_flag(p, LOCAL_NO_OPTIMIZATION | LOCAL_ALREADY_MASQED)
		|| !p->chan || !p->owner) {
		return;
	}

	/* Safely get the channel bridged to p->chan */
	chan = ast_channel_ref(p->chan);

	ao2_unlock(p); /* don't call bridged channel with the pvt locked */
	bridged_chan = ast_bridged_channel(chan);
	ao2_lock(p);

	chan = ast_channel_unref(chan);

	/* since we had to unlock p to get the bridged chan, validate our
	 * data once again and verify the bridged channel is what we expect
	 * it to be in order to perform this optimization */
	if (ast_test_flag(p, LOCAL_NO_OPTIMIZATION | LOCAL_ALREADY_MASQED)
		|| !p->chan || !p->owner
		|| (ast_channel_internal_bridged_channel(p->chan) != bridged_chan)) {
		return;
	}

	/* only do the masquerade if we are being called on the outbound channel,
	   if it has been bridged to another channel and if there are no pending
	   frames on the owner channel (because they would be transferred to the
	   outbound channel during the masquerade)
	*/
	if (!ast_channel_internal_bridged_channel(p->chan) /* Not ast_bridged_channel!  Only go one step! */
		|| !AST_LIST_EMPTY(ast_channel_readq(p->owner))
		|| ast != p->chan /* Sanity check (should always be false) */) {
		return;
	}

	/* Masquerade bridged channel into owner */
	/* Lock everything we need, one by one, and give up if
	   we can't get everything.  Remember, we'll get another
	   chance in just a little bit */
	if (ast_channel_trylock(ast_channel_internal_bridged_channel(p->chan))) {
		return;
	}
	if (ast_check_hangup(ast_channel_internal_bridged_channel(p->chan))
		|| ast_channel_trylock(p->owner)) {
		ast_channel_unlock(ast_channel_internal_bridged_channel(p->chan));
		return;
	}

	/*
	 * At this point we have 4 locks:
	 * p, p->chan (same as ast), p->chan->_bridge, p->owner
	 *
	 * Flush a voice or video frame on the outbound channel to make
	 * the queue empty faster so we can get optimized out.
	 */
	f = AST_LIST_FIRST(ast_channel_readq(p->chan));
	if (f && (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO)) {
		AST_LIST_REMOVE_HEAD(ast_channel_readq(p->chan), frame_list);
		ast_frfree(f);
		f = AST_LIST_FIRST(ast_channel_readq(p->chan));
	}

	if (f
		|| ast_check_hangup(p->owner)
		|| ast_channel_masquerade(p->owner, ast_channel_internal_bridged_channel(p->chan))) {
		ast_channel_unlock(p->owner);
		ast_channel_unlock(ast_channel_internal_bridged_channel(p->chan));
		return;
	}

	/* Masquerade got setup. */
	ast_debug(4, "Masquerading %s <- %s\n",
		ast_channel_name(p->owner),
		ast_channel_name(ast_channel_internal_bridged_channel(p->chan)));
	if (ast_channel_monitor(p->owner)
		&& !ast_channel_monitor(ast_channel_internal_bridged_channel(p->chan))) {
		struct ast_channel_monitor *tmp;

		/* If a local channel is being monitored, we don't want a masquerade
		 * to cause the monitor to go away. Since the masquerade swaps the monitors,
		 * pre-swapping the monitors before the masquerade will ensure that the monitor
		 * ends up where it is expected.
		 */
		tmp = ast_channel_monitor(p->owner);
		ast_channel_monitor_set(p->owner, ast_channel_monitor(ast_channel_internal_bridged_channel(p->chan)));
		ast_channel_monitor_set(ast_channel_internal_bridged_channel(p->chan), tmp);
	}
	if (ast_channel_audiohooks(p->chan)) {
		struct ast_audiohook_list *audiohooks_swapper;

		audiohooks_swapper = ast_channel_audiohooks(p->chan);
		ast_channel_audiohooks_set(p->chan, ast_channel_audiohooks(p->owner));
		ast_channel_audiohooks_set(p->owner, audiohooks_swapper);
	}

	/* If any Caller ID was set, preserve it after masquerade like above. We must check
	 * to see if Caller ID was set because otherwise we'll mistakingly copy info not
	 * set from the dialplan and will overwrite the real channel Caller ID. The reason
	 * for this whole preswapping action is because the Caller ID is set on the channel
	 * thread (which is the to be masqueraded away local channel) before both local
	 * channels are optimized away.
	 */
	if (ast_channel_caller(p->owner)->id.name.valid || ast_channel_caller(p->owner)->id.number.valid
		|| ast_channel_caller(p->owner)->id.subaddress.valid || ast_channel_caller(p->owner)->ani.name.valid
		|| ast_channel_caller(p->owner)->ani.number.valid || ast_channel_caller(p->owner)->ani.subaddress.valid) {
		SWAP(*ast_channel_caller(p->owner), *ast_channel_caller(ast_channel_internal_bridged_channel(p->chan)));
	}
	if (ast_channel_redirecting(p->owner)->from.name.valid || ast_channel_redirecting(p->owner)->from.number.valid
		|| ast_channel_redirecting(p->owner)->from.subaddress.valid || ast_channel_redirecting(p->owner)->to.name.valid
		|| ast_channel_redirecting(p->owner)->to.number.valid || ast_channel_redirecting(p->owner)->to.subaddress.valid) {
		SWAP(*ast_channel_redirecting(p->owner), *ast_channel_redirecting(ast_channel_internal_bridged_channel(p->chan)));
	}
	if (ast_channel_dialed(p->owner)->number.str || ast_channel_dialed(p->owner)->subaddress.valid) {
		SWAP(*ast_channel_dialed(p->owner), *ast_channel_dialed(ast_channel_internal_bridged_channel(p->chan)));
	}
	ast_app_group_update(p->chan, p->owner);
	ast_set_flag(p, LOCAL_ALREADY_MASQED);

	ast_channel_unlock(p->owner);
	ast_channel_unlock(ast_channel_internal_bridged_channel(p->chan));

	/* Do the masquerade now. */
	owner = ast_channel_ref(p->owner);
	ao2_unlock(p);
	ast_channel_unlock(ast);
	ast_do_masquerade(owner);
	ast_channel_unref(owner);
	ast_channel_lock(ast);
	ao2_lock(p);
}

static struct ast_frame  *local_read(struct ast_channel *ast)
{
	return &ast_null_frame;
}

static int local_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	int isoutbound;

	if (!p) {
		return -1;
	}

	/* Just queue for delivery to the other side */
	ao2_ref(p, 1); /* ref for local_queue_frame */
	ao2_lock(p);
	isoutbound = IS_OUTBOUND(ast, p);

	if (isoutbound
		&& (f->frametype == AST_FRAME_VOICE || f->frametype == AST_FRAME_VIDEO)) {
		check_bridge(ast, p);
	}

	if (!ast_test_flag(p, LOCAL_ALREADY_MASQED)) {
		res = local_queue_frame(p, isoutbound, f, ast, 1);
	} else {
		ast_debug(1, "Not posting to '%s' queue since already masqueraded out\n",
			ast_channel_name(ast));
		res = 0;
	}
	ao2_unlock(p);
	ao2_ref(p, -1);

	return res;
}

static int local_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct local_pvt *p = ast_channel_tech_pvt(newchan);

	if (!p) {
		return -1;
	}

	ao2_lock(p);

	if ((p->owner != oldchan) && (p->chan != oldchan)) {
		ast_log(LOG_WARNING, "Old channel %p wasn't %p or %p\n", oldchan, p->owner, p->chan);
		ao2_unlock(p);
		return -1;
	}
	if (p->owner == oldchan) {
		p->owner = newchan;
	} else {
		p->chan = newchan;
	}

	/* Do not let a masquerade cause a Local channel to be bridged to itself! */
	if (!ast_check_hangup(newchan) && ((p->owner && ast_channel_internal_bridged_channel(p->owner) == p->chan) || (p->chan && ast_channel_internal_bridged_channel(p->chan) == p->owner))) {
		ast_log(LOG_WARNING, "You can not bridge a Local channel to itself!\n");
		ao2_unlock(p);
		ast_queue_hangup(newchan);
		return -1;
	}

	ao2_unlock(p);
	return 0;
}

static int local_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	int res = 0;
	struct ast_frame f = { AST_FRAME_CONTROL, };
	int isoutbound;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for local_queue_frame */

	/* If this is an MOH hold or unhold, do it on the Local channel versus real channel */
	if (!ast_test_flag(p, LOCAL_MOH_PASSTHRU) && condition == AST_CONTROL_HOLD) {
		ast_moh_start(ast, data, NULL);
	} else if (!ast_test_flag(p, LOCAL_MOH_PASSTHRU) && condition == AST_CONTROL_UNHOLD) {
		ast_moh_stop(ast);
	} else if (condition == AST_CONTROL_CONNECTED_LINE || condition == AST_CONTROL_REDIRECTING) {
		struct ast_channel *this_channel;
		struct ast_channel *the_other_channel;
		/* A connected line update frame may only contain a partial amount of data, such
		 * as just a source, or just a ton, and not the full amount of information. However,
		 * the collected information is all stored in the outgoing channel's connectedline
		 * structure, so when receiving a connected line update on an outgoing local channel,
		 * we need to transmit the collected connected line information instead of whatever
		 * happens to be in this control frame. The same applies for redirecting information, which
		 * is why it is handled here as well.*/
		ao2_lock(p);
		isoutbound = IS_OUTBOUND(ast, p);
		if (isoutbound) {
			this_channel = p->chan;
			the_other_channel = p->owner;
		} else {
			this_channel = p->owner;
			the_other_channel = p->chan;
		}
		if (the_other_channel) {
			unsigned char frame_data[1024];
			if (condition == AST_CONTROL_CONNECTED_LINE) {
				if (isoutbound) {
					ast_connected_line_copy_to_caller(ast_channel_caller(the_other_channel), ast_channel_connected(this_channel));
				}
				f.datalen = ast_connected_line_build_data(frame_data, sizeof(frame_data), ast_channel_connected(this_channel), NULL);
			} else {
				f.datalen = ast_redirecting_build_data(frame_data, sizeof(frame_data), ast_channel_redirecting(this_channel), NULL);
			}
			f.subclass.integer = condition;
			f.data.ptr = frame_data;
			res = local_queue_frame(p, isoutbound, &f, ast, 1);
		}
		ao2_unlock(p);
	} else {
		/* Queue up a frame representing the indication as a control frame */
		ao2_lock(p);
		/*
		 * Block -1 stop tones events if we are to be optimized out.  We
		 * don't need a flurry of these events on a local channel chain
		 * when initially connected to slow the optimization process.
		 */
		if (0 <= condition || ast_test_flag(p, LOCAL_NO_OPTIMIZATION)) {
			isoutbound = IS_OUTBOUND(ast, p);
			f.subclass.integer = condition;
			f.data.ptr = (void *) data;
			f.datalen = datalen;
			res = local_queue_frame(p, isoutbound, &f, ast, 1);

			if (!res && (condition == AST_CONTROL_T38_PARAMETERS) &&
			    (datalen == sizeof(struct ast_control_t38_parameters))) {
				const struct ast_control_t38_parameters *parameters = data;
				
				if (parameters->request_response == AST_T38_REQUEST_PARMS) {
					res = AST_T38_REQUEST_PARMS;
				}
			}
		} else {
			ast_debug(4, "Blocked indication %d\n", condition);
		}
		ao2_unlock(p);
	}

	ao2_ref(p, -1);
	return res;
}

static int local_digit_begin(struct ast_channel *ast, char digit)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	struct ast_frame f = { AST_FRAME_DTMF_BEGIN, };
	int isoutbound;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for local_queue_frame */
	ao2_lock(p);
	isoutbound = IS_OUTBOUND(ast, p);
	f.subclass.integer = digit;
	res = local_queue_frame(p, isoutbound, &f, ast, 0);
	ao2_unlock(p);
	ao2_ref(p, -1);

	return res;
}

static int local_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	struct ast_frame f = { AST_FRAME_DTMF_END, };
	int isoutbound;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for local_queue_frame */
	ao2_lock(p);
	isoutbound = IS_OUTBOUND(ast, p);
	f.subclass.integer = digit;
	f.len = duration;
	res = local_queue_frame(p, isoutbound, &f, ast, 0);
	ao2_unlock(p);
	ao2_ref(p, -1);

	return res;
}

static int local_sendtext(struct ast_channel *ast, const char *text)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	struct ast_frame f = { AST_FRAME_TEXT, };
	int isoutbound;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for local_queue_frame */
	ao2_lock(p);
	isoutbound = IS_OUTBOUND(ast, p);
	f.data.ptr = (char *) text;
	f.datalen = strlen(text) + 1;
	res = local_queue_frame(p, isoutbound, &f, ast, 0);
	ao2_unlock(p);
	ao2_ref(p, -1);
	return res;
}

static int local_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	int res = -1;
	struct ast_frame f = { AST_FRAME_HTML, };
	int isoutbound;

	if (!p) {
		return -1;
	}

	ao2_ref(p, 1); /* ref for local_queue_frame */
	ao2_lock(p);
	isoutbound = IS_OUTBOUND(ast, p);
	f.subclass.integer = subclass;
	f.data.ptr = (char *)data;
	f.datalen = datalen;
	res = local_queue_frame(p, isoutbound, &f, ast, 0);
	ao2_unlock(p);
	ao2_ref(p, -1);

	return res;
}

/*! \brief Initiate new call, part of PBX interface
 *         dest is the dial string */
static int local_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	int pvt_locked = 0;

	struct ast_channel *owner = NULL;
	struct ast_channel *chan = NULL;
	int res;
	struct ast_var_t *varptr;
	struct ast_var_t *clone_var;
	char *reduced_dest = ast_strdupa(dest);
	char *slash;
	const char *exten;
	const char *context;

	if (!p) {
		return -1;
	}

	/* since we are letting go of channel locks that were locked coming into
	 * this function, then we need to give the tech pvt a ref */
	ao2_ref(p, 1);
	ast_channel_unlock(ast);

	awesome_locking(p, &chan, &owner);
	pvt_locked = 1;

	if (owner != ast) {
		res = -1;
		goto return_cleanup;
	}

	if (!owner || !chan) {
		res = -1;
		goto return_cleanup;
	}

	/*
	 * Note that cid_num and cid_name aren't passed in the ast_channel_alloc
	 * call, so it's done here instead.
	 *
	 * All these failure points just return -1. The individual strings will
	 * be cleared when we destroy the channel.
	 */
	ast_party_redirecting_copy(ast_channel_redirecting(chan), ast_channel_redirecting(owner));

	ast_party_dialed_copy(ast_channel_dialed(chan), ast_channel_dialed(owner));

	ast_connected_line_copy_to_caller(ast_channel_caller(chan), ast_channel_connected(owner));
	ast_connected_line_copy_from_caller(ast_channel_connected(chan), ast_channel_caller(owner));

	ast_channel_language_set(chan, ast_channel_language(owner));
	ast_channel_accountcode_set(chan, ast_channel_accountcode(owner));
	ast_channel_musicclass_set(chan, ast_channel_musicclass(owner));
	ast_cdr_update(chan);

	ast_channel_cc_params_init(chan, ast_channel_get_cc_config_params(owner));

	/* Make sure we inherit the AST_CAUSE_ANSWERED_ELSEWHERE if it's set on the queue/dial call request in the dialplan */
	if (ast_channel_hangupcause(ast) == AST_CAUSE_ANSWERED_ELSEWHERE) {
		ast_channel_hangupcause_set(chan, AST_CAUSE_ANSWERED_ELSEWHERE);
	}

	/* copy the channel variables from the incoming channel to the outgoing channel */
	/* Note that due to certain assumptions, they MUST be in the same order */
	AST_LIST_TRAVERSE(ast_channel_varshead(owner), varptr, entries) {
		clone_var = ast_var_assign(varptr->name, varptr->value);
		if (clone_var) {
			AST_LIST_INSERT_TAIL(ast_channel_varshead(chan), clone_var, entries);
		}
	}
	ast_channel_datastore_inherit(owner, chan);
	/* If the local channel has /n or /b on the end of it,
	 * we need to lop that off for our argument to setting
	 * up the CC_INTERFACES variable
	 */
	if ((slash = strrchr(reduced_dest, '/'))) {
		*slash = '\0';
	}
	ast_set_cc_interfaces_chanvar(chan, reduced_dest);

	exten = ast_strdupa(ast_channel_exten(chan));
	context = ast_strdupa(ast_channel_context(chan));

	ao2_unlock(p);
	pvt_locked = 0;

	ast_channel_unlock(chan);

	if (!ast_exists_extension(chan, context, exten, 1,
		S_COR(ast_channel_caller(owner)->id.number.valid, ast_channel_caller(owner)->id.number.str, NULL))) {
		ast_log(LOG_NOTICE, "No such extension/context %s@%s while calling Local channel\n", exten, context);
		res = -1;
		chan = ast_channel_unref(chan); /* we already unlocked it, so clear it hear so the cleanup label won't touch it. */
		goto return_cleanup;
	}

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when two halves of a Local Channel form a bridge.</synopsis>
			<syntax>
				<parameter name="Channel1">
					<para>The name of the Local Channel half that bridges to another channel.</para>
				</parameter>
				<parameter name="Channel2">
					<para>The name of the Local Channel half that executes the dialplan.</para>
				</parameter>
				<parameter name="Context">
					<para>The context in the dialplan that Channel2 starts in.</para>
				</parameter>
				<parameter name="Exten">
					<para>The extension in the dialplan that Channel2 starts in.</para>
				</parameter>
				<parameter name="LocalOptimization">
					<enumlist>
						<enum name="Yes"/>
						<enum name="No"/>
					</enumlist>
				</parameter>
			</syntax>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_CALL, "LocalBridge",
		      "Channel1: %s\r\n"
		      "Channel2: %s\r\n"
		      "Uniqueid1: %s\r\n"
		      "Uniqueid2: %s\r\n"
		      "Context: %s\r\n"
		      "Exten: %s\r\n"
		      "LocalOptimization: %s\r\n",
			ast_channel_name(p->owner), ast_channel_name(p->chan), ast_channel_uniqueid(p->owner), ast_channel_uniqueid(p->chan),
			p->context, p->exten,
			ast_test_flag(p, LOCAL_NO_OPTIMIZATION) ? "Yes" : "No");


	/* Start switch on sub channel */
	if (!(res = ast_pbx_start(chan))) {
		ao2_lock(p);
		ast_set_flag(p, LOCAL_LAUNCHED_PBX);
		ao2_unlock(p);
	}
	chan = ast_channel_unref(chan); /* chan is already unlocked, clear it here so the cleanup lable won't touch it. */

return_cleanup:
	if (p) {
		if (pvt_locked) {
			ao2_unlock(p);
		}
		ao2_ref(p, -1);
	}
	if (chan) {
		ast_channel_unlock(chan);
		chan = ast_channel_unref(chan);
	}

	/* owner is supposed to be == to ast,  if it
	 * is, don't unlock it because ast must exit locked */
	if (owner) {
		if (owner != ast) {
			ast_channel_unlock(owner);
			ast_channel_lock(ast);
		}
		owner = ast_channel_unref(owner);
	} else {
		/* we have to exit with ast locked */
		ast_channel_lock(ast);
	}

	return res;
}

/*! \brief Hangup a call through the local proxy channel */
static int local_hangup(struct ast_channel *ast)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	int isoutbound;
	int hangup_chan = 0;
	int res = 0;
	struct ast_frame f = { AST_FRAME_CONTROL, { AST_CONTROL_HANGUP }, .data.uint32 = ast_channel_hangupcause(ast) };
	struct ast_channel *owner = NULL;
	struct ast_channel *chan = NULL;

	if (!p) {
		return -1;
	}

	/* give the pvt a ref since we are unlocking the channel. */
	ao2_ref(p, 1);

	/* the pvt isn't going anywhere, we gave it a ref */
	ast_channel_unlock(ast);

	/* lock everything */
	awesome_locking(p, &chan, &owner);

	if (ast != chan && ast != owner) {
		res = -1;
		goto local_hangup_cleanup;
	}

	isoutbound = IS_OUTBOUND(ast, p); /* just comparing pointer of ast */

	if (p->chan && ast_channel_hangupcause(ast) == AST_CAUSE_ANSWERED_ELSEWHERE) {
		ast_channel_hangupcause_set(p->chan, AST_CAUSE_ANSWERED_ELSEWHERE);
		ast_debug(2, "This local call has AST_CAUSE_ANSWERED_ELSEWHERE set.\n");
	}

	if (isoutbound) {
		const char *status = pbx_builtin_getvar_helper(p->chan, "DIALSTATUS");

		if (status && p->owner) {
			ast_channel_hangupcause_set(p->owner, ast_channel_hangupcause(p->chan));
			pbx_builtin_setvar_helper(p->owner, "CHANLOCALSTATUS", status);
		}

		ast_clear_flag(p, LOCAL_LAUNCHED_PBX);
		p->chan = NULL;
	} else {
		if (p->chan) {
			ast_queue_hangup(p->chan);
		}
		p->owner = NULL;
	}

	ast_channel_tech_pvt_set(ast, NULL); /* this is one of our locked channels, doesn't matter which */

	if (!p->owner && !p->chan) {
		ao2_unlock(p);

		/* Remove from list */
		ao2_unlink(locals, p);
		ao2_ref(p, -1);
		p = NULL;
		res = 0;
		goto local_hangup_cleanup;
	}
	if (p->chan && !ast_test_flag(p, LOCAL_LAUNCHED_PBX)) {
		/* Need to actually hangup since there is no PBX */
		hangup_chan = 1;
	} else {
		local_queue_frame(p, isoutbound, &f, NULL, 0);
	}

local_hangup_cleanup:
	if (p) {
		ao2_unlock(p);
		ao2_ref(p, -1);
	}
	if (owner) {
		ast_channel_unlock(owner);
		owner = ast_channel_unref(owner);
	}
	if (chan) {
		ast_channel_unlock(chan);
		if (hangup_chan) {
			ast_hangup(chan);
		}
		chan = ast_channel_unref(chan);
	}

	/* leave with the same stupid channel locked that came in */
	ast_channel_lock(ast);
	return res;
}

/*!
 * \internal
 * \brief struct local_pvt destructor.
 *
 * \param vdoomed Void local_pvt to destroy.
 *
 * \return Nothing
 */
static void local_pvt_destructor(void *vdoomed)
{
	struct local_pvt *doomed = vdoomed;

	doomed->reqcap = ast_format_cap_destroy(doomed->reqcap);

	ast_module_unref(ast_module_info->self);
}

/*! \brief Create a call structure */
static struct local_pvt *local_alloc(const char *data, struct ast_format_cap *cap)
{
	struct local_pvt *tmp = NULL;
	char *parse;
	char *c = NULL;
	char *opts = NULL;

	if (!(tmp = ao2_alloc(sizeof(*tmp), local_pvt_destructor))) {
		return NULL;
	}
	if (!(tmp->reqcap = ast_format_cap_dup(cap))) {
		ao2_ref(tmp, -1);
		return NULL;
	}

	ast_module_ref(ast_module_info->self);

	/* Initialize private structure information */
	parse = ast_strdupa(data);

	memcpy(&tmp->jb_conf, &g_jb_conf, sizeof(tmp->jb_conf));

	/* Look for options */
	if ((opts = strchr(parse, '/'))) {
		*opts++ = '\0';
		if (strchr(opts, 'n'))
			ast_set_flag(tmp, LOCAL_NO_OPTIMIZATION);
		if (strchr(opts, 'j')) {
			if (ast_test_flag(tmp, LOCAL_NO_OPTIMIZATION))
				ast_set_flag(&tmp->jb_conf, AST_JB_ENABLED);
			else {
				ast_log(LOG_ERROR, "You must use the 'n' option for chan_local "
					"to use the 'j' option to enable the jitterbuffer\n");
			}
		}
		if (strchr(opts, 'b')) {
			ast_set_flag(tmp, LOCAL_BRIDGE);
		}
		if (strchr(opts, 'm')) {
			ast_set_flag(tmp, LOCAL_MOH_PASSTHRU);
		}
	}

	/* Look for a context */
	if ((c = strchr(parse, '@'))) {
		*c++ = '\0';
	}

	ast_copy_string(tmp->context, c ? c : "default", sizeof(tmp->context));
	ast_copy_string(tmp->exten, parse, sizeof(tmp->exten));

	/* Add to list */
	ao2_link(locals, tmp);

	return tmp; /* this is returned with a ref */
}

/*! \brief Start new local channel */
static struct ast_channel *local_new(struct local_pvt *p, int state, const char *linkedid, struct ast_callid *callid)
{
	struct ast_channel *tmp = NULL, *tmp2 = NULL;
	struct ast_format fmt;
	int generated_seqno = ast_atomic_fetchadd_int((int *)&name_sequence, +1);
	const char *t;
	int ama;

	/* Allocate two new Asterisk channels */
	/* safe accountcode */
	if (p->owner && ast_channel_accountcode(p->owner))
		t = ast_channel_accountcode(p->owner);
	else
		t = "";

	if (p->owner)
		ama = ast_channel_amaflags(p->owner);
	else
		ama = 0;

	/* Make sure that the ;2 channel gets the same linkedid as ;1. You can't pass linkedid to both
	 * allocations since if linkedid isn't set, then each channel will generate its own linkedid. */
	if (!(tmp = ast_channel_alloc(1, state, 0, 0, t, p->exten, p->context, linkedid, ama, "Local/%s@%s-%08x;1", p->exten, p->context, generated_seqno))
		|| !(tmp2 = ast_channel_alloc(1, AST_STATE_RING, 0, 0, t, p->exten, p->context, ast_channel_linkedid(tmp), ama, "Local/%s@%s-%08x;2", p->exten, p->context, generated_seqno))) {
		if (tmp) {
			tmp = ast_channel_release(tmp);
		}
		ast_log(LOG_WARNING, "Unable to allocate channel structure(s)\n");
		return NULL;
	}

	if (callid) {
		ast_channel_callid_set(tmp, callid);
		ast_channel_callid_set(tmp2, callid);
	}

	ast_channel_tech_set(tmp, &local_tech);
	ast_channel_tech_set(tmp2, &local_tech);

	ast_format_cap_copy(ast_channel_nativeformats(tmp), p->reqcap);
	ast_format_cap_copy(ast_channel_nativeformats(tmp2), p->reqcap);

	/* Determine our read/write format and set it on each channel */
	ast_best_codec(p->reqcap, &fmt);
	ast_format_copy(ast_channel_writeformat(tmp), &fmt);
	ast_format_copy(ast_channel_writeformat(tmp2), &fmt);
	ast_format_copy(ast_channel_rawwriteformat(tmp), &fmt);
	ast_format_copy(ast_channel_rawwriteformat(tmp2), &fmt);
	ast_format_copy(ast_channel_readformat(tmp), &fmt);
	ast_format_copy(ast_channel_readformat(tmp2), &fmt);
	ast_format_copy(ast_channel_rawreadformat(tmp), &fmt);
	ast_format_copy(ast_channel_rawreadformat(tmp2), &fmt);

	ast_channel_tech_pvt_set(tmp, p);
	ast_channel_tech_pvt_set(tmp2, p);

	ast_set_flag(ast_channel_flags(tmp), AST_FLAG_DISABLE_DEVSTATE_CACHE);
	ast_set_flag(ast_channel_flags(tmp2), AST_FLAG_DISABLE_DEVSTATE_CACHE);

	p->owner = tmp;
	p->chan = tmp2;

	ast_channel_context_set(tmp, p->context);
	ast_channel_context_set(tmp2, p->context);
	ast_channel_exten_set(tmp2, p->exten);
	ast_channel_priority_set(tmp, 1);
	ast_channel_priority_set(tmp2, 1);

	ast_jb_configure(tmp, &p->jb_conf);

	return tmp;
}

/*! \brief Part of PBX interface */
static struct ast_channel *local_request(const char *type, struct ast_format_cap *cap, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct local_pvt *p;
	struct ast_channel *chan;
	struct ast_callid *callid = ast_read_threadstorage_callid();

	/* Allocate a new private structure and then Asterisk channel */
	p = local_alloc(data, cap);
	if (!p) {
		chan = NULL;
		goto local_request_end;
	}
	chan = local_new(p, AST_STATE_DOWN, requestor ? ast_channel_linkedid(requestor) : NULL, callid);
	if (!chan) {
		ao2_unlink(locals, p);
	} else if (ast_channel_cc_params_init(chan, requestor ? ast_channel_get_cc_config_params((struct ast_channel *)requestor) : NULL)) {
		ao2_unlink(locals, p);
		p->owner = ast_channel_release(p->owner);
		p->chan = ast_channel_release(p->chan);
		chan = NULL;
	}
	ao2_ref(p, -1); /* kill the ref from the alloc */

local_request_end:

	if (callid) {
		ast_callid_unref(callid);
	}

	return chan;
}

/*! \brief CLI command "local show channels" */
static char *locals_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct local_pvt *p = NULL;
	struct ao2_iterator it;

	switch (cmd) {
	case CLI_INIT:
		e->command = "local show channels";
		e->usage =
			"Usage: local show channels\n"
			"       Provides summary information on active local proxy channels.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	if (ao2_container_count(locals) == 0) {
		ast_cli(a->fd, "No local channels in use\n");
		return RESULT_SUCCESS;
	}

	it = ao2_iterator_init(locals, 0);
	while ((p = ao2_iterator_next(&it))) {
		ao2_lock(p);
		ast_cli(a->fd, "%s -- %s@%s\n", p->owner ? ast_channel_name(p->owner) : "<unowned>", p->exten, p->context);
		ao2_unlock(p);
		ao2_ref(p, -1);
	}
	ao2_iterator_destroy(&it);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_local[] = {
	AST_CLI_DEFINE(locals_show, "List status of local channels"),
};

static int manager_optimize_away(struct mansession *s, const struct message *m)
{
	const char *channel;
	struct local_pvt *p, *tmp = NULL;
	struct ast_channel *c;
	int found = 0;
	struct ao2_iterator it;

	channel = astman_get_header(m, "Channel");

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "'Channel' not specified.");
		return 0;
	}

	c = ast_channel_get_by_name(channel);
	if (!c) {
		astman_send_error(s, m, "Channel does not exist.");
		return 0;
	}

	p = ast_channel_tech_pvt(c);
	ast_channel_unref(c);
	c = NULL;

	it = ao2_iterator_init(locals, 0);
	while ((tmp = ao2_iterator_next(&it))) {
		if (tmp == p) {
			ao2_lock(tmp);
			found = 1;
			ast_clear_flag(tmp, LOCAL_NO_OPTIMIZATION);
			ao2_unlock(tmp);
			ao2_ref(tmp, -1);
			break;
		}
		ao2_ref(tmp, -1);
	}
	ao2_iterator_destroy(&it);

	if (found) {
		astman_send_ack(s, m, "Queued channel to be optimized away");
	} else {
		astman_send_error(s, m, "Unable to find channel");
	}

	return 0;
}


static int locals_cmp_cb(void *obj, void *arg, int flags)
{
	return (obj == arg) ? CMP_MATCH : 0;
}

/*! \brief Load module into PBX, register channel */
static int load_module(void)
{
	if (!(local_tech.capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_format_cap_add_all(local_tech.capabilities);

	if (!(locals = ao2_container_alloc(BUCKET_SIZE, NULL, locals_cmp_cb))) {
		ast_format_cap_destroy(local_tech.capabilities);
		return AST_MODULE_LOAD_FAILURE;
	}

	/* Make sure we can register our channel type */
	if (ast_channel_register(&local_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'Local'\n");
		ao2_ref(locals, -1);
		ast_format_cap_destroy(local_tech.capabilities);
		return AST_MODULE_LOAD_FAILURE;
	}
	ast_cli_register_multiple(cli_local, sizeof(cli_local) / sizeof(struct ast_cli_entry));
	ast_manager_register_xml("LocalOptimizeAway", EVENT_FLAG_SYSTEM|EVENT_FLAG_CALL, manager_optimize_away);

	return AST_MODULE_LOAD_SUCCESS;
}

/*! \brief Unload the local proxy channel from Asterisk */
static int unload_module(void)
{
	struct local_pvt *p = NULL;
	struct ao2_iterator it;

	/* First, take us out of the channel loop */
	ast_cli_unregister_multiple(cli_local, sizeof(cli_local) / sizeof(struct ast_cli_entry));
	ast_manager_unregister("LocalOptimizeAway");
	ast_channel_unregister(&local_tech);

	it = ao2_iterator_init(locals, 0);
	while ((p = ao2_iterator_next(&it))) {
		if (p->owner) {
			ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
		}
		ao2_ref(p, -1);
	}
	ao2_iterator_destroy(&it);
	ao2_ref(locals, -1);

	ast_format_cap_destroy(local_tech.capabilities);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Local Proxy Channel (Note: used internally by other modules)",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	);
