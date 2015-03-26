/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Local proxy channel driver.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

/* ------------------------------------------------------------------- */

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h"
#include "asterisk/manager.h"
#include "asterisk/devicestate.h"
#include "asterisk/astobj2.h"
#include "asterisk/bridge.h"
#include "asterisk/core_unreal.h"
#include "asterisk/core_local.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/_private.h"
#include "asterisk/stasis_channels.h"

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
	<managerEvent language="en_US" name="LocalBridge">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when two halves of a Local Channel form a bridge.</synopsis>
			<syntax>
				<channel_snapshot prefix="LocalOne"/>
				<channel_snapshot prefix="LocalTwo"/>
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
	</managerEvent>
	<managerEvent language="en_US" name="LocalOptimizationBegin">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when two halves of a Local Channel begin to optimize
			themselves out of the media path.</synopsis>
			<syntax>
				<channel_snapshot prefix="LocalOne"/>
				<channel_snapshot prefix="LocalTwo"/>
				<channel_snapshot prefix="Source"/>
				<parameter name="DestUniqueId">
					<para>The unique ID of the bridge into which the local channel is optimizing.</para>
				</parameter>
				<parameter name="Id">
					<para>Identification for the optimization operation.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">LocalOptimizationEnd</ref>
				<ref type="manager">LocalOptimizeAway</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="LocalOptimizationEnd">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when two halves of a Local Channel have finished optimizing
			themselves out of the media path.</synopsis>
			<syntax>
				<channel_snapshot prefix="LocalOne"/>
				<channel_snapshot prefix="LocalTwo"/>
				<parameter name="Success">
					<para>Indicates whether the local optimization succeeded.</para>
				</parameter>
				<parameter name="Id">
					<para>Identification for the optimization operation. Matches the <replaceable>Id</replaceable>
					from a previous <literal>LocalOptimizationBegin</literal></para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">LocalOptimizationBegin</ref>
				<ref type="manager">LocalOptimizeAway</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
 ***/

static const char tdesc[] = "Local Proxy Channel Driver";

static struct ao2_container *locals;

static struct ast_channel *local_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int local_call(struct ast_channel *ast, const char *dest, int timeout);
static int local_hangup(struct ast_channel *ast);
static int local_devicestate(const char *data);
static void local_optimization_started_cb(struct ast_unreal_pvt *base, struct ast_channel *source,
		enum ast_unreal_channel_indicator dest, unsigned int id);
static void local_optimization_finished_cb(struct ast_unreal_pvt *base, int success, unsigned int id);

static struct ast_manager_event_blob *local_message_to_ami(struct stasis_message *msg);

/*!
 * @{ \brief Define local channel message types.
 */
STASIS_MESSAGE_TYPE_DEFN(ast_local_bridge_type,
	.to_ami = local_message_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_local_optimization_begin_type,
	.to_ami = local_message_to_ami,
	);
STASIS_MESSAGE_TYPE_DEFN(ast_local_optimization_end_type,
	.to_ami = local_message_to_ami,
	);
/*! @} */

/*! \brief Callbacks from the unreal core when channel optimization occurs */
struct ast_unreal_pvt_callbacks local_unreal_callbacks = {
	.optimization_started = local_optimization_started_cb,
	.optimization_finished = local_optimization_finished_cb,
};

/* PBX interface structure for channel registration */
static struct ast_channel_tech local_tech = {
	.type = "Local",
	.description = tdesc,
	.requester = local_request,
	.send_digit_begin = ast_unreal_digit_begin,
	.send_digit_end = ast_unreal_digit_end,
	.call = local_call,
	.hangup = local_hangup,
	.answer = ast_unreal_answer,
	.read = ast_unreal_read,
	.write = ast_unreal_write,
	.write_video = ast_unreal_write,
	.exception = ast_unreal_read,
	.indicate = ast_unreal_indicate,
	.fixup = ast_unreal_fixup,
	.send_html = ast_unreal_sendhtml,
	.send_text = ast_unreal_sendtext,
	.devicestate = local_devicestate,
	.queryoption = ast_unreal_queryoption,
	.setoption = ast_unreal_setoption,
};

/*! What to do with the ;2 channel when ast_call() happens. */
enum local_call_action {
	/* The ast_call() will run dialplan on the ;2 channel. */
	LOCAL_CALL_ACTION_DIALPLAN,
	/* The ast_call() will impart the ;2 channel into a bridge. */
	LOCAL_CALL_ACTION_BRIDGE,
	/* The ast_call() will masquerade the ;2 channel into a channel. */
	LOCAL_CALL_ACTION_MASQUERADE,
};

/*! Join a bridge on ast_call() parameters. */
struct local_bridge {
	/*! Bridge to join. */
	struct ast_bridge *join;
	/*! Channel to swap with when joining bridge. */
	struct ast_channel *swap;
	/*! Features that are specific to this channel when pushed into the bridge. */
	struct ast_bridge_features *features;
};

/*!
 * \brief the local pvt structure for all channels
 *
 * The local channel pvt has two ast_chan objects - the "owner" and the "next channel", the outbound channel
 *
 * ast_chan owner -> local_pvt -> ast_chan chan
 */
struct local_pvt {
	/*! Unreal channel driver base class values. */
	struct ast_unreal_pvt base;
	/*! Additional action arguments */
	union {
		/*! Make ;2 join a bridge on ast_call(). */
		struct local_bridge bridge;
		/*! Make ;2 masquerade into this channel on ast_call(). */
		struct ast_channel *masq;
	} action;
	/*! What to do with the ;2 channel on ast_call(). */
	enum local_call_action type;
	/*! Context to call */
	char context[AST_MAX_CONTEXT];
	/*! Extension to call */
	char exten[AST_MAX_EXTENSION];
};

struct ast_channel *ast_local_get_peer(struct ast_channel *ast)
{
	struct local_pvt *p = ast_channel_tech_pvt(ast);
	struct local_pvt *found;
	struct ast_channel *peer;

	if (!p) {
		return NULL;
	}

	found = p ? ao2_find(locals, p, 0) : NULL;
	if (!found) {
		/* ast is either not a local channel or it has alredy been hungup */
		return NULL;
	}
	ao2_lock(found);
	if (ast == p->base.owner) {
		peer = p->base.chan;
	} else if (ast == p->base.chan) {
		peer = p->base.owner;
	} else {
		peer = NULL;
	}
	if (peer) {
		ast_channel_ref(peer);
	}
	ao2_unlock(found);
	ao2_ref(found, -1);
	return peer;
}

/*! \brief Adds devicestate to local channels */
static int local_devicestate(const char *data)
{
	int is_inuse = 0;
	int res = AST_DEVICE_INVALID;
	char *exten = ast_strdupa(data);
	char *context;
	char *opts;
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

	it = ao2_iterator_init(locals, 0);
	for (; (lp = ao2_iterator_next(&it)); ao2_ref(lp, -1)) {
		ao2_lock(lp);
		if (!strcmp(exten, lp->exten)
			&& !strcmp(context, lp->context)) {
			res = AST_DEVICE_NOT_INUSE;
			if (lp->base.owner
				&& ast_test_flag(&lp->base, AST_UNREAL_CARETAKER_THREAD)) {
				is_inuse = 1;
			}
		}
		ao2_unlock(lp);
		if (is_inuse) {
			res = AST_DEVICE_INUSE;
			ao2_ref(lp, -1);
			break;
		}
	}
	ao2_iterator_destroy(&it);

	if (res == AST_DEVICE_INVALID) {
		ast_debug(3, "Checking if extension %s@%s exists (devicestate)\n", exten, context);
		if (ast_exists_extension(NULL, context, exten, 1, NULL)) {
			res = AST_DEVICE_NOT_INUSE;
		}
	}

	return res;
}

static struct ast_multi_channel_blob *local_channel_optimization_blob(struct local_pvt *p,
		struct ast_json *json_object)
{
	struct ast_multi_channel_blob *payload;
	RAII_VAR(struct ast_channel_snapshot *, local_one_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, local_two_snapshot, NULL, ao2_cleanup);

	local_one_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(p->base.owner));
	if (!local_one_snapshot) {
		return NULL;
	}

	local_two_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(p->base.chan));
	if (!local_two_snapshot) {
		return NULL;
	}

	payload = ast_multi_channel_blob_create(json_object);
	if (!payload) {
		return NULL;
	}
	ast_multi_channel_blob_add_channel(payload, "1", local_one_snapshot);
	ast_multi_channel_blob_add_channel(payload, "2", local_two_snapshot);

	return payload;
}

/*! \brief Callback for \ref ast_unreal_pvt_callbacks \ref optimization_started_cb */
static void local_optimization_started_cb(struct ast_unreal_pvt *base, struct ast_channel *source,
		enum ast_unreal_channel_indicator dest, unsigned int id)
{
	RAII_VAR(struct ast_json *, json_object, ast_json_null(), ast_json_unref);
	RAII_VAR(struct ast_multi_channel_blob *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct local_pvt *p = (struct local_pvt *)base;

	if (!ast_local_optimization_begin_type()) {
		return;
	}

	json_object = ast_json_pack("{s: i, s: i}",
			"dest", dest, "id", id);

	if (!json_object) {
		return;
	}

	payload = local_channel_optimization_blob(p, json_object);
	if (!payload) {
		return;
	}

	if (source) {
		RAII_VAR(struct ast_channel_snapshot *, source_snapshot, NULL, ao2_cleanup);
		source_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(source));
		if (!source_snapshot) {
			return;
		}

		ast_multi_channel_blob_add_channel(payload, "source", source_snapshot);
	}

	msg = stasis_message_create(ast_local_optimization_begin_type(), payload);
	if (!msg) {
		return;
	}

	stasis_publish(ast_channel_topic(p->base.owner), msg);
}

/*! \brief Callback for \ref ast_unreal_pvt_callbacks \ref optimization_finished_cb */
static void local_optimization_finished_cb(struct ast_unreal_pvt *base, int success, unsigned int id)
{
	RAII_VAR(struct ast_json *, json_object, ast_json_null(), ast_json_unref);
	RAII_VAR(struct ast_multi_channel_blob *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	struct local_pvt *p = (struct local_pvt *)base;

	if (!ast_local_optimization_end_type()) {
		return;
	}

	json_object = ast_json_pack("{s: i, s: i}", "success", success, "id", id);

	if (!json_object) {
		return;
	}

	payload = local_channel_optimization_blob(p, json_object);
	if (!payload) {
		return;
	}

	msg = stasis_message_create(ast_local_optimization_end_type(), payload);
	if (!msg) {
		return;
	}

	stasis_publish(ast_channel_topic(p->base.owner), msg);
}

static struct ast_manager_event_blob *local_message_to_ami(struct stasis_message *message)
{
	struct ast_multi_channel_blob *obj = stasis_message_data(message);
	struct ast_json *blob = ast_multi_channel_blob_get_json(obj);
	struct ast_channel_snapshot *local_snapshot_one;
	struct ast_channel_snapshot *local_snapshot_two;
	RAII_VAR(struct ast_str *, local_channel_one, NULL, ast_free);
	RAII_VAR(struct ast_str *, local_channel_two, NULL, ast_free);
	RAII_VAR(struct ast_str *, event_buffer, NULL, ast_free);
	const char *event;

	local_snapshot_one = ast_multi_channel_blob_get_channel(obj, "1");
	local_snapshot_two = ast_multi_channel_blob_get_channel(obj, "2");
	if (!local_snapshot_one || !local_snapshot_two) {
		return NULL;
	}

	event_buffer = ast_str_create(1024);
	local_channel_one = ast_manager_build_channel_state_string_prefix(local_snapshot_one, "LocalOne");
	local_channel_two = ast_manager_build_channel_state_string_prefix(local_snapshot_two, "LocalTwo");
	if (!event_buffer || !local_channel_one || !local_channel_two) {
		return NULL;
	}

	if (stasis_message_type(message) == ast_local_optimization_begin_type()) {
		struct ast_channel_snapshot *source_snapshot;
		RAII_VAR(struct ast_str *, source_str, NULL, ast_free);
		const char *dest_uniqueid;

		source_snapshot = ast_multi_channel_blob_get_channel(obj, "source");
		if (source_snapshot) {
			source_str = ast_manager_build_channel_state_string_prefix(source_snapshot, "Source");
			if (!source_str) {
				return NULL;
			}
		}

		dest_uniqueid = ast_json_object_get(blob, "dest") == AST_UNREAL_OWNER ?
				local_snapshot_one->uniqueid : local_snapshot_two->uniqueid;

		event = "LocalOptimizationBegin";
		if (source_str) {
			ast_str_append(&event_buffer, 0, "%s", ast_str_buffer(source_str));
		}
		ast_str_append(&event_buffer, 0, "DestUniqueId: %s\r\n", dest_uniqueid);
		ast_str_append(&event_buffer, 0, "Id: %u\r\n", (unsigned int) ast_json_integer_get(ast_json_object_get(blob, "id")));
	} else if (stasis_message_type(message) == ast_local_optimization_end_type()) {
		event = "LocalOptimizationEnd";
		ast_str_append(&event_buffer, 0, "Success: %s\r\n", ast_json_integer_get(ast_json_object_get(blob, "success")) ? "Yes" : "No");
		ast_str_append(&event_buffer, 0, "Id: %u\r\n", (unsigned int) ast_json_integer_get(ast_json_object_get(blob, "id")));
	} else if (stasis_message_type(message) == ast_local_bridge_type()) {
		event = "LocalBridge";
		ast_str_append(&event_buffer, 0, "Context: %s\r\n", ast_json_string_get(ast_json_object_get(blob, "context")));
		ast_str_append(&event_buffer, 0, "Exten: %s\r\n", ast_json_string_get(ast_json_object_get(blob, "exten")));
		ast_str_append(&event_buffer, 0, "LocalOptimization: %s\r\n", ast_json_is_true(ast_json_object_get(blob, "can_optimize")) ? "Yes" : "No");
	} else {
		return NULL;
	}

	return ast_manager_event_blob_create(EVENT_FLAG_CALL, event,
		"%s"
		"%s"
		"%s",
		ast_str_buffer(local_channel_one),
		ast_str_buffer(local_channel_two),
		ast_str_buffer(event_buffer));
}

/*!
 * \internal
 * \brief Post the \ref ast_local_bridge_type \ref stasis message
 * \since 12.0.0
 *
 * \param p local_pvt to raise the local bridge message
 *
 * \return Nothing
 */
static void publish_local_bridge_message(struct local_pvt *p)
{
	RAII_VAR(struct ast_multi_channel_blob *, multi_blob, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, one_snapshot, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, two_snapshot, NULL, ao2_cleanup);
	struct ast_channel *owner;
	struct ast_channel *chan;

	if (!ast_local_bridge_type()) {
		return;
	}

	ast_unreal_lock_all(&p->base, &chan, &owner);

	blob = ast_json_pack("{s: s, s: s, s: b}",
		"context", p->context,
		"exten", p->exten,
		"can_optimize", !ast_test_flag(&p->base, AST_UNREAL_NO_OPTIMIZATION));
	if (!blob) {
		goto end;
	}

	multi_blob = ast_multi_channel_blob_create(blob);
	if (!multi_blob) {
		goto end;
	}

	one_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(owner));
	if (!one_snapshot) {
		goto end;
	}

	two_snapshot = ast_channel_snapshot_get_latest(ast_channel_uniqueid(chan));
	if (!two_snapshot) {
		goto end;
	}

	ast_multi_channel_blob_add_channel(multi_blob, "1", one_snapshot);
	ast_multi_channel_blob_add_channel(multi_blob, "2", two_snapshot);

	msg = stasis_message_create(ast_local_bridge_type(), multi_blob);
	if (!msg) {
		goto end;
	}

	stasis_publish(ast_channel_topic(owner), msg);

end:
	ast_channel_unlock(owner);
	ast_channel_unref(owner);

	ast_channel_unlock(chan);
	ast_channel_unref(chan);

	ao2_unlock(&p->base);
}

int ast_local_setup_bridge(struct ast_channel *ast, struct ast_bridge *bridge, struct ast_channel *swap, struct ast_bridge_features *features)
{
	struct local_pvt *p;
	struct local_pvt *found;
	int res = -1;

	/* Sanity checks. */
	if (!ast || !bridge) {
		ast_bridge_features_destroy(features);
		return -1;
	}

	ast_channel_lock(ast);
	p = ast_channel_tech_pvt(ast);
	ast_channel_unlock(ast);

	found = p ? ao2_find(locals, p, 0) : NULL;
	if (found) {
		ao2_lock(found);
		if (found->type == LOCAL_CALL_ACTION_DIALPLAN
			&& found->base.owner
			&& found->base.chan
			&& !ast_test_flag(&found->base, AST_UNREAL_CARETAKER_THREAD)) {
			ao2_ref(bridge, +1);
			if (swap) {
				ast_channel_ref(swap);
			}
			found->type = LOCAL_CALL_ACTION_BRIDGE;
			found->action.bridge.join = bridge;
			found->action.bridge.swap = swap;
			found->action.bridge.features = features;
			res = 0;
		} else {
			ast_bridge_features_destroy(features);
		}
		ao2_unlock(found);
		ao2_ref(found, -1);
	}

	return res;
}

int ast_local_setup_masquerade(struct ast_channel *ast, struct ast_channel *masq)
{
	struct local_pvt *p;
	struct local_pvt *found;
	int res = -1;

	/* Sanity checks. */
	if (!ast || !masq) {
		return -1;
	}

	ast_channel_lock(ast);
	p = ast_channel_tech_pvt(ast);
	ast_channel_unlock(ast);

	found = p ? ao2_find(locals, p, 0) : NULL;
	if (found) {
		ao2_lock(found);
		if (found->type == LOCAL_CALL_ACTION_DIALPLAN
			&& found->base.owner
			&& found->base.chan
			&& !ast_test_flag(&found->base, AST_UNREAL_CARETAKER_THREAD)) {
			ast_channel_ref(masq);
			found->type = LOCAL_CALL_ACTION_MASQUERADE;
			found->action.masq = masq;
			res = 0;
		}
		ao2_unlock(found);
		ao2_ref(found, -1);
	}

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
	char *reduced_dest = ast_strdupa(dest);
	char *slash;
	const char *chan_cid;

	if (!p) {
		return -1;
	}

	/* since we are letting go of channel locks that were locked coming into
	 * this function, then we need to give the tech pvt a ref */
	ao2_ref(p, 1);
	ast_channel_unlock(ast);

	ast_unreal_lock_all(&p->base, &chan, &owner);
	pvt_locked = 1;

	if (owner != ast) {
		res = -1;
		goto return_cleanup;
	}

	if (!owner || !chan) {
		res = -1;
		goto return_cleanup;
	}

	ast_unreal_call_setup(owner, chan);

	/*
	 * If the local channel has /n on the end of it, we need to lop
	 * that off for our argument to setting up the CC_INTERFACES
	 * variable.
	 */
	if ((slash = strrchr(reduced_dest, '/'))) {
		*slash = '\0';
	}
	ast_set_cc_interfaces_chanvar(chan, reduced_dest);

	ao2_unlock(p);
	pvt_locked = 0;

	ast_channel_unlock(owner);

	chan_cid = S_COR(ast_channel_caller(chan)->id.number.valid,
		ast_channel_caller(chan)->id.number.str, NULL);
	if (chan_cid) {
		chan_cid = ast_strdupa(chan_cid);
	}
	ast_channel_unlock(chan);

	res = -1;
	switch (p->type) {
	case LOCAL_CALL_ACTION_DIALPLAN:
		if (!ast_exists_extension(NULL, p->context, p->exten, 1, chan_cid)) {
			ast_log(LOG_NOTICE, "No such extension/context %s@%s while calling Local channel\n",
				p->exten, p->context);
		} else {
			publish_local_bridge_message(p);

			/* Start switch on sub channel */
			res = ast_pbx_start(chan);
		}
		break;
	case LOCAL_CALL_ACTION_BRIDGE:
		publish_local_bridge_message(p);
		ast_answer(chan);
		res = ast_bridge_impart(p->action.bridge.join, chan, p->action.bridge.swap,
			p->action.bridge.features, AST_BRIDGE_IMPART_CHAN_INDEPENDENT);
		ao2_ref(p->action.bridge.join, -1);
		p->action.bridge.join = NULL;
		ao2_cleanup(p->action.bridge.swap);
		p->action.bridge.swap = NULL;
		p->action.bridge.features = NULL;
		break;
	case LOCAL_CALL_ACTION_MASQUERADE:
		publish_local_bridge_message(p);
		ast_answer(chan);
		res = ast_channel_move(p->action.masq, chan);
		if (!res) {
			/* Chan is now an orphaned zombie.  Destroy it. */
			ast_hangup(chan);
		}
		p->action.masq = ast_channel_unref(p->action.masq);
		break;
	}
	if (!res) {
		ao2_lock(p);
		ast_set_flag(&p->base, AST_UNREAL_CARETAKER_THREAD);
		ao2_unlock(p);
	}

	/* we already unlocked them, clear them here so the cleanup label won't touch them. */
	owner = ast_channel_unref(owner);
	chan = ast_channel_unref(chan);

return_cleanup:
	if (p) {
		if (pvt_locked) {
			ao2_unlock(p);
		}
		ao2_ref(p, -1);
	}
	if (chan) {
		ast_channel_unlock(chan);
		ast_channel_unref(chan);
	}

	/*
	 * owner is supposed to be == to ast, if it is, don't unlock it
	 * because ast must exit locked
	 */
	if (owner) {
		if (owner != ast) {
			ast_channel_unlock(owner);
			ast_channel_lock(ast);
		}
		ast_channel_unref(owner);
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
	int res;

	if (!p) {
		return -1;
	}

	/* give the pvt a ref to fulfill calling requirements. */
	ao2_ref(p, +1);
	res = ast_unreal_hangup(&p->base, ast);
	if (!res) {
		int unlink;

		ao2_lock(p);
		unlink = !p->base.owner && !p->base.chan;
		ao2_unlock(p);
		if (unlink) {
			ao2_unlink(locals, p);
		}
	}
	ao2_ref(p, -1);

	return res;
}

/*!
 * \internal
 * \brief struct local_pvt destructor.
 *
 * \param vdoomed Object to destroy.
 *
 * \return Nothing
 */
static void local_pvt_destructor(void *vdoomed)
{
	struct local_pvt *doomed = vdoomed;

	switch (doomed->type) {
	case LOCAL_CALL_ACTION_DIALPLAN:
		break;
	case LOCAL_CALL_ACTION_BRIDGE:
		ao2_cleanup(doomed->action.bridge.join);
		ao2_cleanup(doomed->action.bridge.swap);
		ast_bridge_features_destroy(doomed->action.bridge.features);
		break;
	case LOCAL_CALL_ACTION_MASQUERADE:
		ao2_cleanup(doomed->action.masq);
		break;
	}
	ast_unreal_destructor(&doomed->base);
}

/*! \brief Create a call structure */
static struct local_pvt *local_alloc(const char *data, struct ast_format_cap *cap)
{
	struct local_pvt *pvt;
	char *parse;
	char *context;
	char *opts;

	pvt = (struct local_pvt *) ast_unreal_alloc(sizeof(*pvt), local_pvt_destructor, cap);
	if (!pvt) {
		return NULL;
	}
	pvt->base.callbacks = &local_unreal_callbacks;

	parse = ast_strdupa(data);

	/*
	 * Local channels intercept MOH by default.
	 *
	 * This is a silly default because it represents state held by
	 * the local channels.  Unless local channel optimization is
	 * disabled, the state will dissapear when the local channels
	 * optimize out.
	 */
	ast_set_flag(&pvt->base, AST_UNREAL_MOH_INTERCEPT);

	/* Look for options */
	if ((opts = strchr(parse, '/'))) {
		*opts++ = '\0';
		if (strchr(opts, 'n')) {
			ast_set_flag(&pvt->base, AST_UNREAL_NO_OPTIMIZATION);
		}
		if (strchr(opts, 'j')) {
			if (ast_test_flag(&pvt->base, AST_UNREAL_NO_OPTIMIZATION)) {
				ast_set_flag(&pvt->base.jb_conf, AST_JB_ENABLED);
			} else {
				ast_log(LOG_ERROR, "You must use the 'n' option with the 'j' option to enable the jitter buffer\n");
			}
		}
		if (strchr(opts, 'm')) {
			ast_clear_flag(&pvt->base, AST_UNREAL_MOH_INTERCEPT);
		}
	}

	/* Look for a context */
	if ((context = strchr(parse, '@'))) {
		*context++ = '\0';
	}

	ast_copy_string(pvt->context, S_OR(context, "default"), sizeof(pvt->context));
	ast_copy_string(pvt->exten, parse, sizeof(pvt->exten));
	snprintf(pvt->base.name, sizeof(pvt->base.name), "%s@%s", pvt->exten, pvt->context);

	return pvt; /* this is returned with a ref */
}

/*! \brief Part of PBX interface */
static struct ast_channel *local_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct local_pvt *p;
	struct ast_channel *chan;
	struct ast_callid *callid;

	/* Allocate a new private structure and then Asterisk channels */
	p = local_alloc(data, cap);
	if (!p) {
		return NULL;
	}
	callid = ast_read_threadstorage_callid();
	chan = ast_unreal_new_channels(&p->base, &local_tech, AST_STATE_DOWN, AST_STATE_RING,
		p->exten, p->context, assignedids, requestor, callid);
	if (chan) {
		ao2_link(locals, p);
	}
	if (callid) {
		ast_callid_unref(callid);
	}
	ao2_ref(p, -1); /* kill the ref from the alloc */

	return chan;
}

/*! \brief CLI command "local show channels" */
static char *locals_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct local_pvt *p;
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
		ast_cli(a->fd, "%s -- %s\n",
			p->base.owner ? ast_channel_name(p->base.owner) : "<unowned>",
			p->base.name);
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
	struct local_pvt *p;
	struct local_pvt *found;
	struct ast_channel *chan;

	channel = astman_get_header(m, "Channel");
	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "'Channel' not specified.");
		return 0;
	}

	chan = ast_channel_get_by_name(channel);
	if (!chan) {
		astman_send_error(s, m, "Channel does not exist.");
		return 0;
	}

	p = ast_channel_tech_pvt(chan);
	ast_channel_unref(chan);

	found = p ? ao2_find(locals, p, 0) : NULL;
	if (found) {
		ao2_lock(found);
		ast_clear_flag(&found->base, AST_UNREAL_NO_OPTIMIZATION);
		ao2_unlock(found);
		ao2_ref(found, -1);
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

/*!
 * \internal
 * \brief Shutdown the local proxy channel.
 * \since 12.0.0
 *
 * \return Nothing
 */
static void local_shutdown(void)
{
	struct local_pvt *p;
	struct ao2_iterator it;

	/* First, take us out of the channel loop */
	ast_cli_unregister_multiple(cli_local, ARRAY_LEN(cli_local));
	ast_manager_unregister("LocalOptimizeAway");
	ast_channel_unregister(&local_tech);

	ao2_ref(locals, -1);
	locals = NULL;

	ao2_cleanup(local_tech.capabilities);
	local_tech.capabilities = NULL;

	STASIS_MESSAGE_TYPE_CLEANUP(ast_local_optimization_begin_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_local_optimization_end_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_local_bridge_type);
}

int ast_local_init(void)
{

	if (STASIS_MESSAGE_TYPE_INIT(ast_local_optimization_begin_type)) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(ast_local_optimization_end_type)) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(ast_local_bridge_type)) {
		return -1;
	}

	if (!(local_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return -1;
	}
	ast_format_cap_append_by_type(local_tech.capabilities, AST_MEDIA_TYPE_UNKNOWN);

	locals = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL, locals_cmp_cb);
	if (!locals) {
		ao2_cleanup(local_tech.capabilities);
		local_tech.capabilities = NULL;
		return -1;
	}

	/* Make sure we can register our channel type */
	if (ast_channel_register(&local_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class 'Local'\n");
		ao2_ref(locals, -1);
		ao2_cleanup(local_tech.capabilities);
		local_tech.capabilities = NULL;
		return -1;
	}
	ast_cli_register_multiple(cli_local, ARRAY_LEN(cli_local));
	ast_manager_register_xml_core("LocalOptimizeAway", EVENT_FLAG_SYSTEM|EVENT_FLAG_CALL, manager_optimize_away);

	ast_register_cleanup(local_shutdown);
	return 0;
}
