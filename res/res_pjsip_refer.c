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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<depend>res_pjsip_pubsub</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/bridge.h"
#include "asterisk/framehook.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/causes.h"
#include "asterisk/refer.h"

static struct ast_taskprocessor *refer_serializer;

static pj_status_t refer_on_tx_request(pjsip_tx_data *tdata);

/*! \brief REFER Progress structure */
struct refer_progress {
	/*! \brief Subscription to provide updates on */
	pjsip_evsub *sub;
	/*! \brief Dialog for subscription */
	pjsip_dialog *dlg;
	/*! \brief Received packet, used to construct final response in case no subscription exists */
	pjsip_rx_data *rdata;
	/*! \brief Frame hook for monitoring REFER progress */
	int framehook;
	/*! \brief Last received subclass in frame hook */
	int subclass;
	/*! \brief Serializer for notifications */
	struct ast_taskprocessor *serializer;
	/*! \brief Stasis subscription for bridge events */
	struct stasis_subscription *bridge_sub;
	/*! \brief Reference to transfer_channel_data related to the refer */
	struct transfer_channel_data *transfer_data;
	/*! \brief Uniqueid of transferee channel */
	char *transferee;
	/*! \brief Non-zero if the 100 notify has been sent */
	int sent_100;
	/*! \brief Whether to notifies all the progress details on blind transfer */
	unsigned int refer_blind_progress;
};

/*! \brief REFER Progress notification structure */
struct refer_progress_notification {
	/*! \brief Refer progress structure to send notification on */
	struct refer_progress *progress;
	/*! \brief SIP response code to send */
	int response;
	/*! \brief Subscription state */
	pjsip_evsub_state state;
};

/*! \brief REFER Progress module, used to attach REFER progress structure to subscriptions */
static pjsip_module refer_progress_module = {
	.name = { "REFER Progress", 14 },
	.id = -1,
};

/*! \brief Destructor for REFER Progress notification structure */
static void refer_progress_notification_destroy(void *obj)
{
	struct refer_progress_notification *notification = obj;

	ao2_cleanup(notification->progress);
}

/*! \brief Allocator for REFER Progress notification structure */
static struct refer_progress_notification *refer_progress_notification_alloc(struct refer_progress *progress, int response,
	pjsip_evsub_state state)
{
	struct refer_progress_notification *notification = ao2_alloc(sizeof(*notification), refer_progress_notification_destroy);

	if (!notification) {
		return NULL;
	}

	ao2_ref(progress, +1);
	notification->progress = progress;
	notification->response = response;
	notification->state = state;

	return notification;
}

/*! \brief Serialized callback for subscription notification
 *
 * Locking and serialization:
 *
 * Although refer_progress_notify() always runs in the progress serializer,
 * the pjproject evsub module itself can cause the subscription to be
 * destroyed which then triggers refer_progress_on_evsub_state() to clean
 * it up.  In this case, it's possible that refer_progress_notify() could
 * get the subscription pulled out from under it while it's trying to use it.
 *
 * At one point we tried to have refer_progress_on_evsub_state() push the
 * cleanup to the serializer and wait for its return before returning to
 * pjproject but since pjproject calls its state callbacks with the dialog
 * locked, this required us to unlock the dialog while waiting for the
 * serialized cleanup, then lock it again before returning to pjproject.
 * There were also still some cases where other callers of
 * refer_progress_notify() weren't using the serializer and crashes were
 * resulting.
 *
 * Although all callers of refer_progress_notify() now use the progress
 * serializer, we decided to simplify the locking so we didn't have to
 * unlock and relock the dialog in refer_progress_on_evsub_state().
 *
 * Now, refer_progress_notify() holds the dialog lock for all its work
 * rather than just when calling pjsip_evsub_set_mod_data() to clear the
 * module data.  Since pjproject also holds the dialog lock while calling
 * refer_progress_on_evsub_state(), there should be no more chances for
 * the subscription to be cleaned up while still being used to send NOTIFYs.
 */
static int refer_progress_notify(void *data)
{
	RAII_VAR(struct refer_progress_notification *, notification, data, ao2_cleanup);
	pjsip_evsub *sub;
	pjsip_tx_data *tdata;

	pjsip_dlg_inc_lock(notification->progress->dlg);

	/* If the subscription has already been terminated we can't send a notification */
	if (!(sub = notification->progress->sub)) {
		ast_debug(3, "Not sending NOTIFY of response '%d' and state '%u' on progress monitor '%p' as subscription has been terminated\n",
			notification->response, notification->state, notification->progress);
		pjsip_dlg_dec_lock(notification->progress->dlg);
		return 0;
	}

	/* Send a deferred initial 100 Trying SIP frag NOTIFY if we haven't already. */
	if (!notification->progress->sent_100) {
		notification->progress->sent_100 = 1;
		if (notification->response != 100) {
			ast_debug(3, "Sending initial 100 Trying NOTIFY for progress monitor '%p'\n",
				notification->progress);
			if (pjsip_xfer_notify(sub, PJSIP_EVSUB_STATE_ACTIVE, 100, NULL, &tdata) == PJ_SUCCESS) {
				pjsip_xfer_send_request(sub, tdata);
			}
		}
	}

	ast_debug(3, "Sending NOTIFY with response '%d' and state '%u' on subscription '%p' and progress monitor '%p'\n",
		notification->response, notification->state, sub, notification->progress);

	/* Actually send the notification */
	if (pjsip_xfer_notify(sub, notification->state, notification->response, NULL, &tdata) == PJ_SUCCESS) {
		pjsip_xfer_send_request(sub, tdata);
	}

	pjsip_dlg_dec_lock(notification->progress->dlg);

	return 0;
}

static void refer_progress_bridge(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct refer_progress *progress = data;
	struct ast_bridge_blob *enter_blob;
	struct refer_progress_notification *notification;
	struct ast_channel *chan;

	if (stasis_subscription_final_message(sub, message)) {
		ao2_ref(progress, -1);
		return;
	}

	if (ast_channel_entered_bridge_type() != stasis_message_type(message)) {
		/* Don't care */
		return;
	}

	enter_blob = stasis_message_data(message);
	if (strcmp(enter_blob->channel->base->uniqueid, progress->transferee)) {
		/* Don't care */
		return;
	}

	if (!progress->transfer_data->completed) {
		/* We can't act on this message because the transfer_channel_data doesn't show that
		 * the transfer is ready to progress */
		return;
	}

	/* OMG the transferee is joining a bridge. His call got answered! */
	notification = refer_progress_notification_alloc(progress, 200, PJSIP_EVSUB_STATE_TERMINATED);
	if (notification) {
		if (ast_sip_push_task(progress->serializer, refer_progress_notify, notification)) {
			ao2_cleanup(notification);
		}
		progress->bridge_sub = stasis_unsubscribe(progress->bridge_sub);
	}

	chan = ast_channel_get_by_name(progress->transferee);
	if (!chan) {
		/* The channel is already gone */
		return;
	}

	ast_channel_lock(chan);
	ast_debug(3, "Detaching REFER progress monitoring hook from '%s' as it has joined a bridge\n",
		ast_channel_name(chan));
	ast_framehook_detach(chan, progress->framehook);
	ast_channel_unlock(chan);

	ast_channel_unref(chan);
}

/*! \brief Progress monitoring frame hook - examines frames to determine state of transfer */
static struct ast_frame *refer_progress_framehook(struct ast_channel *chan, struct ast_frame *f, enum ast_framehook_event event, void *data)
{
	struct refer_progress *progress = data;
	struct refer_progress_notification *notification = NULL;

	/* We only care about frames *to* the channel */
	if (!f || (event != AST_FRAMEHOOK_EVENT_WRITE)) {
		return f;
	}

	/* If the completed flag hasn't been raised, skip this pass. */
	if (!progress->transfer_data->completed) {
		return f;
	}

	/* Determine the state of the REFER based on the control frames (or voice frames) passing */
	if (f->frametype == AST_FRAME_VOICE && !progress->subclass) {
		/* Media is passing without progress, this means the call has been answered */
		progress->subclass = AST_CONTROL_ANSWER;
		notification = refer_progress_notification_alloc(progress, 200, PJSIP_EVSUB_STATE_TERMINATED);
	} else if (f->frametype == AST_FRAME_CONTROL) {
		/* Based on the control frame being written we can send a NOTIFY advising of the progress */
		if ((f->subclass.integer == AST_CONTROL_RING) || (f->subclass.integer == AST_CONTROL_RINGING)) {
			/* Don't set progress->subclass; an ANSWER can still follow */
			notification = refer_progress_notification_alloc(progress, 180, PJSIP_EVSUB_STATE_ACTIVE);
		} else if (f->subclass.integer == AST_CONTROL_BUSY) {
			progress->subclass = f->subclass.integer;
			notification = refer_progress_notification_alloc(progress, 486, PJSIP_EVSUB_STATE_TERMINATED);
		} else if (f->subclass.integer == AST_CONTROL_CONGESTION) {
			progress->subclass = f->subclass.integer;
			notification = refer_progress_notification_alloc(progress, 503, PJSIP_EVSUB_STATE_TERMINATED);
		} else if (f->subclass.integer == AST_CONTROL_PROGRESS) {
			/* Don't set progress->subclass; an ANSWER can still follow */
			notification = refer_progress_notification_alloc(progress, 183, PJSIP_EVSUB_STATE_ACTIVE);
		} else if (f->subclass.integer == AST_CONTROL_PROCEEDING) {
			/* Don't set progress->subclass; an ANSWER can still follow */
			notification = refer_progress_notification_alloc(progress, 100, PJSIP_EVSUB_STATE_ACTIVE);
		} else if (f->subclass.integer == AST_CONTROL_ANSWER) {
			progress->subclass = f->subclass.integer;
			notification = refer_progress_notification_alloc(progress, 200, PJSIP_EVSUB_STATE_TERMINATED);
		}
	}

	/* If a notification is due to be sent push it to the thread pool */
	if (notification) {
		/* If the subscription is being terminated we don't need the frame hook any longer */
		if (notification->state == PJSIP_EVSUB_STATE_TERMINATED) {
			ast_debug(3, "Detaching REFER progress monitoring hook from '%s' as subscription is being terminated\n",
				ast_channel_name(chan));
			ast_framehook_detach(chan, progress->framehook);
		}

		if (ast_sip_push_task(progress->serializer, refer_progress_notify, notification)) {
			ao2_cleanup(notification);
		}
	}

	return f;
}

/*! \brief Destroy callback for monitoring framehook */
static void refer_progress_framehook_destroy(void *data)
{
	struct refer_progress *progress = data;
	struct refer_progress_notification *notification = refer_progress_notification_alloc(progress, 503, PJSIP_EVSUB_STATE_TERMINATED);

	if (notification && ast_sip_push_task(progress->serializer, refer_progress_notify, notification)) {
		ao2_cleanup(notification);
	}

	if (progress->bridge_sub) {
		progress->bridge_sub = stasis_unsubscribe(progress->bridge_sub);
	}

	ao2_cleanup(progress);
}

/*!
 * \brief Callback for REFER subscription state changes
 * \see refer_progress_notify
 *
 * The documentation attached to refer_progress_notify has more
 * information about the locking issues with cleaning up
 * the subscription.
 *
 * \note pjproject holds the dialog lock while calling this function.
 */
static void refer_progress_on_evsub_state(pjsip_evsub *sub, pjsip_event *event)
{
	struct refer_progress *progress = pjsip_evsub_get_mod_data(sub, refer_progress_module.id);

	/*
	 * If being destroyed, remove the progress object from the subscription
	 * and release the reference it had.
	 */
	if (progress && (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED)) {
		pjsip_evsub_set_mod_data(progress->sub, refer_progress_module.id, NULL);
		progress->sub = NULL;
		ao2_cleanup(progress);
	}
}

/*! \brief Callback structure for subscription */
static pjsip_evsub_user refer_progress_evsub_cb = {
	.on_evsub_state = refer_progress_on_evsub_state,
};

static int dlg_releaser_task(void *data) {
	pjsip_dlg_dec_session((pjsip_dialog *)data, &refer_progress_module);
	return 0;
}

/*! \brief Destructor for REFER progress sutrcture */
static void refer_progress_destroy(void *obj)
{
	struct refer_progress *progress = obj;

	if (progress->bridge_sub) {
		progress->bridge_sub = stasis_unsubscribe(progress->bridge_sub);
	}

	if (progress->dlg) {
		/*
		 * Although the dlg session count was incremented in a pjsip servant
		 * thread, there's no guarantee that the last thread to unref this progress
		 * object was one.  Before we decrement, we need to make sure that this
		 * is either a servant thread or that we push the decrement to a
		 * serializer that is one.
		 *
		 * Because pjsip_dlg_dec_session requires the dialog lock, we don't want
		 * to wait on the task to complete if we had to push it to a serializer.
		 */
		if (ast_sip_thread_is_servant()) {
			pjsip_dlg_dec_session(progress->dlg, &refer_progress_module);
		} else {
			ast_sip_push_task(NULL, dlg_releaser_task, progress->dlg);
		}
	}

	ao2_cleanup(progress->transfer_data);

	ast_free(progress->transferee);
	ast_taskprocessor_unreference(progress->serializer);
}

/*! \brief Internal helper function which sets up a refer progress structure if needed */
static int refer_progress_alloc(struct ast_sip_session *session, pjsip_rx_data *rdata, struct refer_progress **progress)
{
	const pj_str_t str_refer_sub = { "Refer-Sub", 9 };
	pjsip_generic_string_hdr *refer_sub = NULL;
	const pj_str_t str_true = { "true", 4 };
	pjsip_hdr hdr_list;
	char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];

	*progress = NULL;

	/* Grab the optional Refer-Sub header, it can be used to suppress the implicit subscription */
	refer_sub = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str_refer_sub, NULL);
	if ((refer_sub && pj_strnicmp(&refer_sub->hvalue, &str_true, 4))) {
		return 0;
	}

	if (!(*progress = ao2_alloc(sizeof(struct refer_progress), refer_progress_destroy))) {
		return -1;
	}

	ast_debug(3, "Created progress monitor '%p' for transfer occurring from channel '%s' and endpoint '%s'\n",
		progress, ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint));

	(*progress)->refer_blind_progress = session->endpoint->refer_blind_progress;

	(*progress)->framehook = -1;

	/* Create name with seq number appended. */
	ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "pjsip/refer/%s",
		ast_sorcery_object_get_id(session->endpoint));

	if (!((*progress)->serializer = ast_sip_create_serializer(tps_name))) {
		goto error;
	}

	/* Create the implicit subscription for monitoring of this transfer */
	if (pjsip_xfer_create_uas(session->inv_session->dlg, &refer_progress_evsub_cb, rdata, &(*progress)->sub) != PJ_SUCCESS) {
		goto error;
	}

	/* To prevent a potential deadlock we need the dialog so we can lock/unlock */
	(*progress)->dlg = session->inv_session->dlg;
	/* We also need to make sure it stays around until we're done with it */
	pjsip_dlg_inc_session((*progress)->dlg, &refer_progress_module);


	/* Associate the REFER progress structure with the subscription */
	ao2_ref(*progress, +1);
	pjsip_evsub_set_mod_data((*progress)->sub, refer_progress_module.id, *progress);

	pj_list_init(&hdr_list);
	if (refer_sub) {
		pjsip_hdr *hdr = (pjsip_hdr*)pjsip_generic_string_hdr_create(session->inv_session->dlg->pool, &str_refer_sub, &str_true);

		pj_list_push_back(&hdr_list, hdr);
	}

	/* Accept the REFER request */
	ast_debug(3, "Accepting REFER request for progress monitor '%p'\n", *progress);
	pjsip_xfer_accept((*progress)->sub, rdata, 202, &hdr_list);

	return 0;

error:
	ao2_cleanup(*progress);
	*progress = NULL;
	return -1;
}

/*! \brief Structure for attended transfer task */
struct refer_attended {
	/*! \brief Transferer session */
	struct ast_sip_session *transferer;
	/*! \brief Transferer channel */
	struct ast_channel *transferer_chan;
	/*! \brief Second transferer session */
	struct ast_sip_session *transferer_second;
	/*! \brief Optional refer progress structure */
	struct refer_progress *progress;
};

/*! \brief Destructor for attended transfer task */
static void refer_attended_destroy(void *obj)
{
	struct refer_attended *attended = obj;

	ao2_cleanup(attended->transferer);
	ast_channel_cleanup(attended->transferer_chan);
	ao2_cleanup(attended->transferer_second);
	ao2_cleanup(attended->progress);
}

/*! \brief Allocator for attended transfer task */
static struct refer_attended *refer_attended_alloc(struct ast_sip_session *transferer,
	struct ast_sip_session *transferer_second,
	struct refer_progress *progress)
{
	struct refer_attended *attended;

	attended = ao2_alloc_options(sizeof(*attended), refer_attended_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!attended) {
		return NULL;
	}

	ao2_ref(transferer, +1);
	attended->transferer = transferer;
	ast_channel_ref(transferer->channel);
	attended->transferer_chan = transferer->channel;
	ao2_ref(transferer_second, +1);
	attended->transferer_second = transferer_second;

	if (progress) {
		ao2_ref(progress, +1);
		attended->progress = progress;
	}

	return attended;
}

static int session_end_if_deferred_task(void *data)
{
	struct ast_sip_session *session = data;

	ast_sip_session_end_if_deferred(session);
	ao2_ref(session, -1);
	return 0;
}

static int defer_termination_cancel_task(void *data)
{
	struct ast_sip_session *session = data;

	ast_sip_session_end_if_deferred(session);
	ast_sip_session_defer_termination_cancel(session);
	ao2_ref(session, -1);
	return 0;
}

/*!
 * \internal
 * \brief Convert transfer enum to SIP response code.
 * \since 13.3.0
 *
 * \param xfer_code Core transfer function enum result.
 *
 * \return SIP response code
 */
static int xfer_response_code2sip(enum ast_transfer_result xfer_code)
{
	int response;

	response = 503;
	switch (xfer_code) {
	case AST_BRIDGE_TRANSFER_INVALID:
		response = 400;
		break;
	case AST_BRIDGE_TRANSFER_NOT_PERMITTED:
		response = 403;
		break;
	case AST_BRIDGE_TRANSFER_FAIL:
		response = 500;
		break;
	case AST_BRIDGE_TRANSFER_SUCCESS:
		response = 200;
		break;
	}
	return response;
}

/*! \brief Task for attended transfer executed by attended->transferer_second serializer */
static int refer_attended_task(void *data)
{
	struct refer_attended *attended = data;
	int response;
	int (*task_cb)(void *data);

	if (attended->transferer_second->channel) {
		ast_debug(3, "Performing a REFER attended transfer - Transferer #1: %s Transferer #2: %s\n",
			ast_channel_name(attended->transferer_chan),
			ast_channel_name(attended->transferer_second->channel));

		response = xfer_response_code2sip(ast_bridge_transfer_attended(
			attended->transferer_chan,
			attended->transferer_second->channel));

		ast_debug(3, "Final response for REFER attended transfer - Transferer #1: %s Transferer #2: %s is '%d'\n",
			ast_channel_name(attended->transferer_chan),
			ast_channel_name(attended->transferer_second->channel),
			response);
	} else {
		ast_debug(3, "Received REFER request on channel '%s' but other channel has gone.\n",
			ast_channel_name(attended->transferer_chan));
		response = 603;
	}

	if (attended->progress) {
		struct refer_progress_notification *notification;

		notification = refer_progress_notification_alloc(attended->progress, response,
			PJSIP_EVSUB_STATE_TERMINATED);
		if (notification) {
			if (ast_sip_push_task(attended->progress->serializer, refer_progress_notify, notification)) {
				ao2_cleanup(notification);
			}
		}
	}

	if (response == 200) {
		task_cb = session_end_if_deferred_task;
	} else {
		task_cb = defer_termination_cancel_task;
	}
	if (!ast_sip_push_task(attended->transferer->serializer,
		task_cb, attended->transferer)) {
		/* Gave the ref to the pushed task. */
		attended->transferer = NULL;
	} else {
		/* Do this anyway even though it is the wrong serializer. */
		ast_sip_session_end_if_deferred(attended->transferer);
	}

	ao2_ref(attended, -1);
	return 0;
}

/*! \brief Structure for blind transfer callback details */
struct refer_blind {
	/*! \brief Context being used for transfer */
	const char *context;
	/*! \brief Optional progress structure */
	struct refer_progress *progress;
	/*! \brief REFER message */
	pjsip_rx_data *rdata;
	/*! \brief Optional Replaces header */
	pjsip_replaces_hdr *replaces;
	/*! \brief Optional Refer-To header */
	pjsip_sip_uri *refer_to;
	/*! \brief Attended transfer flag */
	unsigned int attended:1;
};

/*! \brief Blind transfer callback function */
static void refer_blind_callback(struct ast_channel *chan, struct transfer_channel_data *user_data_wrapper,
	enum ast_transfer_type transfer_type)
{
	struct refer_blind *refer = user_data_wrapper->data;
	pjsip_generic_string_hdr *referred_by;

	static const pj_str_t str_referred_by = { "Referred-By", 11 };
	static const pj_str_t str_referred_by_s = { "b", 1 };

	pbx_builtin_setvar_helper(chan, "SIPTRANSFER", "yes");

	if (refer->progress && !refer->attended && !refer->progress->refer_blind_progress) {
		/* If blind transfer and endpoint doesn't want to receive all the progress details */
		struct refer_progress_notification *notification = refer_progress_notification_alloc(refer->progress, 200,
			PJSIP_EVSUB_STATE_TERMINATED);

		if (notification) {
			if (ast_sip_push_task(refer->progress->serializer, refer_progress_notify, notification)) {
				ao2_cleanup(notification);
			}
		}
	} else if (refer->progress) {
		/* If attended transfer and progress monitoring is being done attach a frame hook so we can monitor it */
		struct ast_framehook_interface hook = {
			.version = AST_FRAMEHOOK_INTERFACE_VERSION,
			.event_cb = refer_progress_framehook,
			.destroy_cb = refer_progress_framehook_destroy,
			.data = refer->progress,
			.disable_inheritance = 1,
		};

		refer->progress->transferee = ast_strdup(ast_channel_uniqueid(chan));
		if (!refer->progress->transferee) {
			struct refer_progress_notification *notification = refer_progress_notification_alloc(refer->progress, 200,
				PJSIP_EVSUB_STATE_TERMINATED);

			ast_log(LOG_WARNING, "Could not copy channel name '%s' during transfer - assuming success\n",
				ast_channel_name(chan));

			if (notification) {
				if (ast_sip_push_task(refer->progress->serializer, refer_progress_notify, notification)) {
					ao2_cleanup(notification);
				}
			}
		}

		/* Progress needs a reference to the transfer_channel_data so that it can track the completed status of the transfer */
		ao2_ref(user_data_wrapper, +1);
		refer->progress->transfer_data = user_data_wrapper;

		/* We need to bump the reference count up on the progress structure since it is in the frame hook now */
		ao2_ref(refer->progress, +1);

		/* If we can't attach a frame hook for whatever reason send a notification of success immediately */
		ast_channel_lock(chan);
		refer->progress->framehook = ast_framehook_attach(chan, &hook);
		ast_channel_unlock(chan);
		if (refer->progress->framehook < 0) {
			struct refer_progress_notification *notification = refer_progress_notification_alloc(refer->progress, 200,
				PJSIP_EVSUB_STATE_TERMINATED);

			ast_log(LOG_WARNING, "Could not attach REFER transfer progress monitoring hook to channel '%s' - assuming success\n",
				ast_channel_name(chan));

			if (notification) {
				if (ast_sip_push_task(refer->progress->serializer, refer_progress_notify, notification)) {
					ao2_cleanup(notification);
				}
			}

			ao2_cleanup(refer->progress);
		}

		/* We need to bump the reference count for the stasis subscription */
		ao2_ref(refer->progress, +1);
		/* We also will need to detect if the transferee enters a bridge. This is currently the only reliable way to
		 * detect if the transfer target has answered the call
		 */
		refer->progress->bridge_sub = stasis_subscribe_pool(ast_bridge_topic_all(), refer_progress_bridge, refer->progress);
		if (!refer->progress->bridge_sub) {
			struct refer_progress_notification *notification = refer_progress_notification_alloc(refer->progress, 200,
				PJSIP_EVSUB_STATE_TERMINATED);

			ast_log(LOG_WARNING, "Could not create bridge stasis subscription for monitoring progress on transfer of channel '%s' - assuming success\n",
					ast_channel_name(chan));

			if (notification) {
				if (ast_sip_push_task(refer->progress->serializer, refer_progress_notify, notification)) {
					ao2_cleanup(notification);
				}
			}

			ast_channel_lock(chan);
			ast_framehook_detach(chan, refer->progress->framehook);
			ast_channel_unlock(chan);

			ao2_cleanup(refer->progress);
		} else {
			stasis_subscription_accept_message_type(refer->progress->bridge_sub, ast_channel_entered_bridge_type());
			stasis_subscription_accept_message_type(refer->progress->bridge_sub, stasis_subscription_change_type());
			stasis_subscription_set_filter(refer->progress->bridge_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);
		}
	}

	pbx_builtin_setvar_helper(chan, "SIPREFERRINGCONTEXT", S_OR(refer->context, NULL));

	referred_by = pjsip_msg_find_hdr_by_names(refer->rdata->msg_info.msg,
		&str_referred_by, &str_referred_by_s, NULL);
	if (referred_by) {
		size_t uri_size = pj_strlen(&referred_by->hvalue) + 1;
		char *uri = ast_alloca(uri_size);

		ast_copy_pj_str(uri, &referred_by->hvalue, uri_size);
		pbx_builtin_setvar_helper(chan, "__SIPREFERREDBYHDR", S_OR(uri, NULL));
	} else {
		pbx_builtin_setvar_helper(chan, "SIPREFERREDBYHDR", NULL);
	}

	if (refer->replaces) {
		char replaces[512];
		char *replaces_val = NULL;
		int len;

		len = pjsip_hdr_print_on(refer->replaces, replaces, sizeof(replaces) - 1);
		if (len != -1) {
			/* pjsip_hdr_print_on does not NULL terminate the buffer */
			replaces[len] = '\0';
			replaces_val = replaces + sizeof("Replaces:");
		}
		pbx_builtin_setvar_helper(chan, "__SIPREPLACESHDR", replaces_val);
	} else {
		pbx_builtin_setvar_helper(chan, "SIPREPLACESHDR", NULL);
	}

	if (refer->refer_to) {
		char refer_to[PJSIP_MAX_URL_SIZE];

		pjsip_uri_print(PJSIP_URI_IN_REQ_URI, refer->refer_to, refer_to, sizeof(refer_to));
		pbx_builtin_setvar_helper(chan, "SIPREFERTOHDR", S_OR(refer_to, NULL));
	} else {
		pbx_builtin_setvar_helper(chan, "SIPREFERTOHDR", NULL);
	}
}

/*!
 * \internal
 * \brief Set the passed in context variable to the determined transfer context.
 * \since 13.3.0
 *
 * \param context Set to the determined transfer context.
 * \param session INVITE dialog SIP session.
 */
#define DETERMINE_TRANSFER_CONTEXT(context, session)									\
	do {																				\
		ast_channel_lock((session)->channel);											\
		context = pbx_builtin_getvar_helper((session)->channel, "TRANSFER_CONTEXT");	\
		if (ast_strlen_zero(context)) {													\
			context = (session)->endpoint->context;										\
		} else {																		\
			context = ast_strdupa(context);												\
		}																				\
		ast_channel_unlock((session)->channel);											\
	} while (0)																			\

struct refer_data {
	struct ast_refer *refer;
	char *destination;
	char *from;
	char *refer_to;
	int to_self;
};

static void refer_data_destroy(void *obj)
{
	struct refer_data *rdata = obj;

	ast_free(rdata->destination);
	ast_free(rdata->from);
	ast_free(rdata->refer_to);

	ast_refer_destroy(rdata->refer);
}

static struct refer_data *refer_data_create(const struct ast_refer *refer)
{
	char *uri_params;
	const char *destination;
	struct refer_data *rdata = ao2_alloc_options(sizeof(*rdata), refer_data_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);

	if (!rdata) {
		return NULL;
	}

	/* typecast to suppress const warning */
	rdata->refer = ast_refer_ref((struct ast_refer *) refer);
	destination = ast_refer_get_to(refer);

	/* To starts with 'pjsip:' which needs to be removed. */
	if (!(destination = strchr(destination, ':'))) {
		goto failure;
	}
	++destination;/* Now skip the ':' */

	rdata->destination = ast_strdup(destination);
	if (!rdata->destination) {
		goto failure;
	}

	rdata->from = ast_strdup(ast_refer_get_from(refer));
	if (!rdata->from) {
		goto failure;
	}

	rdata->refer_to = ast_strdup(ast_refer_get_refer_to(refer));
	if (!rdata->refer_to) {
		goto failure;
	}
	rdata->to_self = ast_refer_get_to_self(refer);

	/*
	 * Sometimes from URI can contain URI parameters, so remove them.
	 *
	 * sip:user;user-options@domain;uri-parameters
	 */
	uri_params = strchr(rdata->from, '@');
	if (uri_params && (uri_params = strchr(uri_params, ';'))) {
		*uri_params = '\0';
	}
	return rdata;

failure:
	ao2_cleanup(rdata);
	return NULL;
}

/*!
 * \internal
 * \brief Checks if the given refer var name should be blocked.
 *
 * \details Some headers are not allowed to be overridden by the user.
 *  Determine if the given var header name from the user is blocked for
 *  an outgoing REFER.
 *
 * \param name name of header to see if it is blocked.
 *
 * \retval TRUE if the given header is blocked.
 */
static int is_refer_var_blocked(const char *name)
{
	int i;

	/* Don't block the Max-Forwards header because the user can override it */
	static const char *hdr[] = {
		"To",
		"From",
		"Via",
		"Route",
		"Contact",
		"Call-ID",
		"CSeq",
		"Allow",
		"Content-Length",
		"Content-Type",
		"Request-URI",
	};

	for (i = 0; i < ARRAY_LEN(hdr); ++i) {
		if (!strcasecmp(name, hdr[i])) {
			/* Block addition of this header. */
			return 1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Copies any other refer vars over to the request headers.
 *
 * \param refer The refer structure to copy headers from
 * \param tdata The SIP transmission data
 */
static enum pjsip_status_code vars_to_headers(const struct ast_refer *refer, pjsip_tx_data *tdata)
{
	const char *name;
	const char *value;
	struct ast_refer_var_iterator *iter;

	for (iter = ast_refer_var_iterator_init(refer);
		ast_refer_var_iterator_next(iter, &name, &value);
		ast_refer_var_unref_current(iter)) {
		if (!is_refer_var_blocked(name)) {
			ast_sip_add_header(tdata, name, value);
		}
	}
	ast_refer_var_iterator_destroy(iter);

	return PJSIP_SC_OK;
}

struct refer_out_of_dialog {
	pjsip_dialog *dlg;
	int authentication_challenge_count;
};

/*! \brief REFER Out-of-dialog module, used to attach session data structure to subscription */
static pjsip_module refer_out_of_dialog_module = {
	.name = { "REFER Out-of-dialog Module", 26 },
	.id = -1,
	.on_tx_request = refer_on_tx_request,
	/* Ensure that we are called after res_pjsp_nat module and before transport priority */
	.priority = PJSIP_MOD_PRIORITY_TSX_LAYER - 4,
};

/*! \brief Helper function which returns the name-addr of the Refer-To header or NULL */
static pjsip_uri *get_refer_to_uri(pjsip_tx_data *tdata)
{
	const pj_str_t REFER_TO = { "Refer-To", 8 };
	pjsip_generic_string_hdr *refer_to;
	pjsip_uri *parsed_uri;

	if (!(refer_to = pjsip_msg_find_hdr_by_name(tdata->msg, &REFER_TO, NULL))
		|| !(parsed_uri = pjsip_parse_uri(tdata->pool, refer_to->hvalue.ptr, refer_to->hvalue.slen, 0))
		|| (!PJSIP_URI_SCHEME_IS_SIP(parsed_uri) && !PJSIP_URI_SCHEME_IS_SIPS(parsed_uri))) {
		return NULL;
	}

	return parsed_uri;
}

static pj_status_t refer_on_tx_request(pjsip_tx_data *tdata) {
	RAII_VAR(struct ast_str *, refer_to_str, ast_str_create(PJSIP_MAX_URL_SIZE), ast_free_ptr);
	const pj_str_t REFER_TO = { "Refer-To", 8 };
	pjsip_generic_string_hdr *refer_to_hdr;
	pjsip_dialog *dlg;
	struct refer_data *refer_data;
	pjsip_uri *parsed_uri;
	pjsip_sip_uri *refer_to_uri;

	/*
	 * If this is a request in response to a 401/407 Unauthorized challenge, the
	 * Refer-To URI has been rewritten already, so don't attempt to re-write it again.
	 * Checking for presence of the Authorization header is not an ideal solution. We do this because
	 * there exists some race condition where this dialog is not the same as the one used
	 * to send the original request in which case we don't have the correct refer_data.
	 */
	if (!refer_to_str
		|| pjsip_msg_find_hdr(tdata->msg, PJSIP_H_AUTHORIZATION, NULL)
		|| !(dlg = pjsip_tdata_get_dlg(tdata))
		|| !(refer_data = pjsip_dlg_get_mod_data(dlg, refer_out_of_dialog_module.id))
		|| !refer_data->to_self
		|| !(parsed_uri = get_refer_to_uri(tdata))) {
		goto out;
	}
	refer_to_uri = pjsip_uri_get_uri(parsed_uri);
	ast_sip_rewrite_uri_to_local(refer_to_uri, tdata);

	pjsip_uri_print(PJSIP_URI_IN_CONTACT_HDR, parsed_uri, ast_str_buffer(refer_to_str), ast_str_size(refer_to_str));
	refer_to_hdr = pjsip_msg_find_hdr_by_name(tdata->msg, &REFER_TO, NULL);
	pj_strdup2(tdata->pool, &refer_to_hdr->hvalue, ast_str_buffer(refer_to_str));

out:
	return PJ_SUCCESS;
}

static int refer_unreference_dialog(void *obj)
{
	struct refer_out_of_dialog *data = obj;

	/* This is why we keep the dialog on the subscription. When the subscription
	 * is destroyed, there is no guarantee that the underlying dialog is ready
	 * to be destroyed. Furthermore, there's no guarantee in the opposite direction
	 * either. The dialog could be destroyed before our subscription is. We fix
	 * this problem by keeping a reference to the dialog until it is time to
	 * destroy the subscription.
	 */
	pjsip_dlg_dec_session(data->dlg, &refer_out_of_dialog_module);
	data->dlg = NULL;

	return 0;
}
/*! \brief Destructor for REFER out of dialog structure */
static void refer_out_of_dialog_destroy(void *obj) {
	struct refer_out_of_dialog *data = obj;

	if (data->dlg) {
		/* ast_sip_push_task_wait_servant should not be called in a destructor,
		 * however in this case it seems to be fine.
		 */
		ast_sip_push_task_wait_servant(refer_serializer, refer_unreference_dialog, data);
	}
}

/*!
 * \internal
 * \brief Callback function to report status of implicit REFER-NOTIFY subscription.
 *
 * This function will be called on any state change in the REFER-NOTIFY subscription.
 * Its primary purpose is to report SUCCESS/FAILURE of a refer initiated via
 * \ref refer_send as well as to terminate the subscription, if necessary.
 */
static void refer_client_on_evsub_state(pjsip_evsub *sub, pjsip_event *event)
{
	pjsip_tx_data *tdata;
	RAII_VAR(struct ast_sip_endpoint *, endpt, NULL, ao2_cleanup);
	struct refer_out_of_dialog *refer_data;
	int refer_success;
	int res = 0;

	if (!event) {
		return;
	}

	refer_data = pjsip_evsub_get_mod_data(sub, refer_out_of_dialog_module.id);
	if (!refer_data || !refer_data->dlg) {
		return;
	}

	endpt = ast_sip_dialog_get_endpoint(refer_data->dlg);

	if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_ACCEPTED) {
		/* Check if subscription is suppressed and terminate and send completion code, if so. */
		pjsip_rx_data *rdata;
		pjsip_generic_string_hdr *refer_sub;
		const pj_str_t REFER_SUB = { "Refer-Sub", 9 };

		ast_debug(3, "Refer accepted by %s\n", endpt ? ast_sorcery_object_get_id(endpt) : "(unknown endpoint)");

		/* Check if response message */
		if (event->type == PJSIP_EVENT_TSX_STATE && event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
			rdata = event->body.tsx_state.src.rdata;

			/* Find Refer-Sub header */
			refer_sub = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &REFER_SUB, NULL);

			/* Check if subscription is suppressed. If it is, the far end will not terminate it,
			 * and the subscription will remain active until it times out.  Terminating it here
			 * eliminates the unnecessary timeout.
			 */
			if (refer_sub && !pj_stricmp2(&refer_sub->hvalue, "false")) {
				/* Since no subscription is desired, assume that call has been referred successfully
				 * and terminate subscription.
				 */
				pjsip_evsub_set_mod_data(sub, refer_out_of_dialog_module.id, NULL);
				pjsip_evsub_terminate(sub, PJ_TRUE);
				res = -1;
			}
		}
	} else if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_ACTIVE ||
			pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
		/* Check for NOTIFY complete or error. */
		pjsip_msg *msg;
		pjsip_msg_body *body;
		pjsip_status_line status_line = { .code = 0 };
		pj_bool_t is_last;
		pj_status_t status;

		if (event->type == PJSIP_EVENT_TSX_STATE && event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
			pjsip_rx_data *rdata;
			pj_str_t refer_str;
			pj_cstr(&refer_str, "REFER");

			rdata = event->body.tsx_state.src.rdata;
			msg = rdata->msg_info.msg;

			if (msg->type == PJSIP_RESPONSE_MSG
				&& (event->body.tsx_state.tsx->status_code == 401
					|| event->body.tsx_state.tsx->status_code == 407)
				&& pj_stristr(&refer_str, &event->body.tsx_state.tsx->method.name)
				&& ++refer_data->authentication_challenge_count < MAX_RX_CHALLENGES
				&& endpt) {

				if (!ast_sip_create_request_with_auth(&endpt->outbound_auths,
					event->body.tsx_state.src.rdata, event->body.tsx_state.tsx->last_tx, &tdata)) {
					/* Send authed REFER */
					ast_sip_send_request(tdata, refer_data->dlg, NULL, NULL, NULL);
					goto out;
				}
			}

			if (msg->type == PJSIP_REQUEST_MSG) {
				if (!pjsip_method_cmp(&msg->line.req.method, pjsip_get_notify_method())) {
					body = msg->body;
					if (body && !pj_stricmp2(&body->content_type.type, "message")
						&& !pj_stricmp2(&body->content_type.subtype, "sipfrag")) {
						pjsip_parse_status_line((char *)body->data, body->len, &status_line);
					}
				}
			} else {
				status_line.code = msg->line.status.code;
				status_line.reason = msg->line.status.reason;
			}
		} else {
			status_line.code = 500;
			status_line.reason = *pjsip_get_status_text(500);
		}

		is_last = (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED);
		/* If the status code is >= 200, the subscription is finished. */
		if (status_line.code >= 200 || is_last) {
			res = -1;

			refer_success = status_line.code >= 200 && status_line.code < 300;

			/* If subscription not terminated and subscription is finished (status code >= 200)
			 * terminate it */
			if (!is_last) {
				pjsip_tx_data *tdata;

				status = pjsip_evsub_initiate(sub, pjsip_get_subscribe_method(), 0, &tdata);
				if (status == PJ_SUCCESS) {
					pjsip_evsub_send_request(sub, tdata);
				}
			}
			ast_debug(3, "Refer completed: %d %.*s (%s)\n",
					status_line.code,
					(int)status_line.reason.slen, status_line.reason.ptr,
					refer_success ? "Success" : "Failure");
		}
	}

out:
	if (res) {
		ao2_cleanup(refer_data);
	}
}

/*!
 * \internal
 * \brief Send a REFER
 *
 * \param data The outbound refer data structure
 *
 * \return 0: success, -1: failure
 */
static int refer_send(void *data)
{
	struct refer_data *rdata = data; /* The caller holds a reference */
	pjsip_tx_data *tdata;
	pjsip_evsub *sub;
	pj_str_t tmp;
	char refer_to_str[PJSIP_MAX_URL_SIZE];
	char disp_name_escaped[128];
	struct refer_out_of_dialog *refer;
	struct pjsip_evsub_user xfer_cb;
	RAII_VAR(char *, uri, NULL, ast_free);
	RAII_VAR(char *, tmp_str, NULL, ast_free);
	RAII_VAR(char *, display_name, NULL, ast_free);
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_endpoint *, refer_to_endpoint, NULL, ao2_cleanup);

	endpoint = ast_sip_get_endpoint(rdata->destination, 1, &uri);
	if (!endpoint) {
		ast_log(LOG_ERROR,
			"PJSIP REFER - Could not find endpoint '%s' and no default outbound endpoint configured\n",
			rdata->destination);
		return -1;
	}
	ast_debug(3, "Request URI: %s\n", uri);

	refer_to_endpoint = ast_sip_get_endpoint(rdata->refer_to, 0, &tmp_str);
	if (!tmp_str) {
		ast_log(LOG_WARNING, "PJSIP REFER - Refer to not a valid resource identifier or SIP URI\n");
		return -1;
	}
	if (!(refer = ao2_alloc(sizeof(struct refer_out_of_dialog), refer_out_of_dialog_destroy))) {
		ast_log(LOG_ERROR, "PJSIP REFER - Could not allocate resources.\n");
		return -1;
	}
	/* The dialog will be terminated in the subscription event callback
	 * when the subscription has terminated. */
	refer->authentication_challenge_count = 0;
	refer->dlg = ast_sip_create_dialog_uac(endpoint, uri, NULL);
	if (!refer->dlg) {
		ast_log(LOG_WARNING, "PJSIP REFER - Could not create dialog\n");
		ao2_cleanup(refer);
		return -1;
	}
	ast_sip_dialog_set_endpoint(refer->dlg, endpoint);

	pj_bzero(&xfer_cb, sizeof(xfer_cb));
	xfer_cb.on_evsub_state = &refer_client_on_evsub_state;
	if (pjsip_xfer_create_uac(refer->dlg, &xfer_cb, &sub) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "PJSIP REFER - Could not create uac\n");
		ao2_cleanup(refer);
		return -1;
	}

	display_name = ast_refer_get_var_and_unlink(rdata->refer, "display_name");
	if (display_name) {
		ast_escape_quoted(display_name, disp_name_escaped, sizeof(disp_name_escaped));
		snprintf(refer_to_str, sizeof(refer_to_str), "\"%s\" <%s>", disp_name_escaped, tmp_str);
	} else {
		snprintf(refer_to_str, sizeof(refer_to_str), "%s", tmp_str);
	}

	/* refer_out_of_dialog_module requires a reference to dlg
	 * which will be released in refer_client_on_evsub_state()
	 * when the implicit REFER subscription terminates */
	pjsip_evsub_set_mod_data(sub, refer_out_of_dialog_module.id, refer);
	if (pjsip_xfer_initiate(sub, pj_cstr(&tmp, refer_to_str), &tdata) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "PJSIP REFER - Could not create request\n");
		goto failure;
	}

	if (refer_to_endpoint && rdata->to_self) {
		pjsip_dlg_add_usage(refer->dlg, &refer_out_of_dialog_module, rdata);
	}

	ast_sip_update_to_uri(tdata, uri);
	ast_sip_update_from(tdata, rdata->from);

	/*
	 * This copies any headers found in the refer's variables to
	 * tdata.
	 */
	vars_to_headers(rdata->refer, tdata);
	ast_debug(1, "Sending REFER to '%s' (via endpoint %s) from '%s'\n",
		rdata->destination, ast_sorcery_object_get_id(endpoint), rdata->from);

	if (pjsip_xfer_send_request(sub, tdata) == PJ_SUCCESS) {
		return 0;
	}

failure:
	ao2_cleanup(refer);
	pjsip_evsub_set_mod_data(sub, refer_out_of_dialog_module.id, NULL);
	pjsip_evsub_terminate(sub, PJ_FALSE);
	return -1;
}

static int sip_refer_send(const struct ast_refer *refer)
{
	struct refer_data *rdata;
	int res;

	if (ast_strlen_zero(ast_refer_get_to(refer))) {
		ast_log(LOG_ERROR, "SIP REFER - a 'To' URI  must be specified\n");
		return -1;
	}

	rdata = refer_data_create(refer);
	if (!rdata) {
		return -1;
	}

	res = ast_sip_push_task_wait_serializer(refer_serializer, refer_send, rdata);
	ao2_ref(rdata, -1);

	return res;
}

static const struct ast_refer_tech refer_tech = {
	.name = "pjsip",
	.refer_send = sip_refer_send,
};

static int refer_incoming_attended_request(struct ast_sip_session *session, pjsip_rx_data *rdata, pjsip_sip_uri *target_uri,
	pjsip_param *replaces_param, struct refer_progress *progress)
{
	const pj_str_t str_replaces = { "Replaces", 8 };
	pj_str_t replaces_content;
	pjsip_replaces_hdr *replaces;
	int parsed_len;
	pjsip_dialog *dlg;

	pj_strdup_with_null(rdata->tp_info.pool, &replaces_content, &replaces_param->value);

	/* Parsing the parameter as a Replaces header easily grabs the needed information */
	if (!(replaces = pjsip_parse_hdr(rdata->tp_info.pool, &str_replaces, replaces_content.ptr,
		pj_strlen(&replaces_content), &parsed_len))) {
		ast_log(LOG_ERROR, "Received REFER request on channel '%s' from endpoint '%s' with invalid Replaces header, rejecting\n",
			ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint));
		return 400;
	}

	/* See if the dialog is local, or remote */
	if ((dlg = pjsip_ua_find_dialog(&replaces->call_id, &replaces->to_tag, &replaces->from_tag, PJ_TRUE))) {
		RAII_VAR(struct ast_sip_session *, other_session, ast_sip_dialog_get_session(dlg), ao2_cleanup);
		struct refer_attended *attended;

		pjsip_dlg_dec_lock(dlg);

		if (!other_session) {
			ast_debug(3, "Received REFER request on channel '%s' from endpoint '%s' for local dialog but no session exists on it\n",
				ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint));
			return 603;
		}

		/* We defer actually doing the attended transfer to the other session so no deadlock can occur */
		if (!(attended = refer_attended_alloc(session, other_session, progress))) {
			ast_log(LOG_ERROR, "Received REFER request on channel '%s' from endpoint '%s' for local dialog but could not allocate structure to complete, rejecting\n",
				ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint));
			return 500;
		}

		if (ast_sip_session_defer_termination(session)) {
			ast_log(LOG_ERROR, "Received REFER request on channel '%s' from endpoint '%s' for local dialog but could not defer termination, rejecting\n",
				ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint));
			ao2_cleanup(attended);
			return 500;
		}

		/* Push it to the other session, which will have both channels with minimal locking */
		if (ast_sip_push_task(other_session->serializer, refer_attended_task, attended)) {
			ast_sip_session_end_if_deferred(session);
			ast_sip_session_defer_termination_cancel(session);
			ao2_cleanup(attended);
			return 500;
		}

		ast_debug(3, "Attended transfer from '%s' pushed to second channel serializer\n",
			ast_channel_name(session->channel));

		return 200;
	} else {
		const char *context;
		struct refer_blind refer = { 0, };
		int response;

		DETERMINE_TRANSFER_CONTEXT(context, session);

		if (!ast_exists_extension(NULL, context, "external_replaces", 1, NULL)) {
			ast_log(LOG_ERROR, "Received REFER for remote session on channel '%s' from endpoint '%s' but 'external_replaces' extension not found in context %s\n",
				ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint), context);
			return 404;
		}

		refer.context = context;
		refer.progress = progress;
		refer.rdata = rdata;
		refer.replaces = replaces;
		refer.refer_to = target_uri;
		refer.attended = 1;

		if (ast_sip_session_defer_termination(session)) {
			ast_log(LOG_ERROR, "Received REFER for remote session on channel '%s' from endpoint '%s' but could not defer termination, rejecting\n",
				ast_channel_name(session->channel),
				ast_sorcery_object_get_id(session->endpoint));
			return 500;
		}

		response = xfer_response_code2sip(ast_bridge_transfer_blind(1, session->channel,
			"external_replaces", context, refer_blind_callback, &refer));

		ast_sip_session_end_if_deferred(session);
		if (response != 200) {
			ast_sip_session_defer_termination_cancel(session);
		}

		return response;
	}
}

static int refer_incoming_blind_request(struct ast_sip_session *session, pjsip_rx_data *rdata, pjsip_sip_uri *target,
	struct refer_progress *progress)
{
	const char *context;
	char exten[AST_MAX_EXTENSION];
	struct refer_blind refer = { 0, };
	int response;

	/* If no explicit transfer context has been provided use their configured context */
	DETERMINE_TRANSFER_CONTEXT(context, session);

	/* Using the user portion of the target URI see if it exists as a valid extension in their context */
	ast_copy_pj_str(exten, &target->user, sizeof(exten));

	/*
	 * We may want to match in the dialplan without any user
	 * options getting in the way.
	 */
	AST_SIP_USER_OPTIONS_TRUNCATE_CHECK(exten);

	/* Uri without exten */
	if (ast_strlen_zero(exten)) {
		ast_copy_string(exten, "s", sizeof(exten));
		ast_debug(3, "Channel '%s' from endpoint '%s' attempted blind transfer to a target without extension. Target was set to 's@%s'\n",
			ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint), context);
	}

	if (!ast_exists_extension(NULL, context, exten, 1, NULL)) {
		ast_log(LOG_ERROR, "Channel '%s' from endpoint '%s' attempted blind transfer to '%s@%s' but target does not exist\n",
			ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint), exten, context);
		return 404;
	}

	refer.context = context;
	refer.progress = progress;
	refer.rdata = rdata;
	refer.refer_to = target;
	refer.attended = 0;

	if (ast_sip_session_defer_termination(session)) {
		ast_log(LOG_ERROR, "Channel '%s' from endpoint '%s' attempted blind transfer but could not defer termination, rejecting\n",
			ast_channel_name(session->channel),
			ast_sorcery_object_get_id(session->endpoint));
		return 500;
	}

	response = xfer_response_code2sip(ast_bridge_transfer_blind(1, session->channel,
		exten, context, refer_blind_callback, &refer));

	ast_sip_session_end_if_deferred(session);
	if (response != 200) {
		ast_sip_session_defer_termination_cancel(session);
	}

	return response;
}

/*! \brief Structure used to retrieve channel from another session */
struct invite_replaces {
	/*! \brief Session we want the channel from */
	struct ast_sip_session *session;
	/*! \brief Channel from the session (with reference) */
	struct ast_channel *channel;
	/*! \brief Bridge the channel is in */
	struct ast_bridge *bridge;
};

/*! \brief Task for invite replaces */
static int invite_replaces(void *data)
{
	struct invite_replaces *invite = data;

	if (!invite->session->channel) {
		return -1;
	}

	ast_channel_ref(invite->session->channel);
	invite->channel = invite->session->channel;

	invite->bridge = ast_bridge_transfer_acquire_bridge(invite->channel);
	return 0;
}

static int refer_incoming_invite_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	pjsip_dialog *other_dlg = NULL;
	pjsip_tx_data *packet;
	int response = 0;
	RAII_VAR(struct ast_sip_session *, other_session, NULL, ao2_cleanup);
	struct invite_replaces invite;

	/* If a Replaces header is present make sure it is valid */
	if (pjsip_replaces_verify_request(rdata, &other_dlg, PJ_TRUE, &packet) != PJ_SUCCESS) {
		response = packet->msg->line.status.code;
		ast_assert(response != 0);
		pjsip_tx_data_dec_ref(packet);
		goto inv_replace_failed;
	}

	/* If no other dialog exists then this INVITE request does not have a Replaces header */
	if (!other_dlg) {
		return 0;
	}

	other_session = ast_sip_dialog_get_session(other_dlg);
	pjsip_dlg_dec_lock(other_dlg);

	/* Don't accept an in-dialog INVITE with Replaces as it does not make much sense */
	if (session->inv_session->dlg->state == PJSIP_DIALOG_STATE_ESTABLISHED) {
		response = 488;
		goto inv_replace_failed;
	}

	if (!other_session) {
		ast_debug(3, "INVITE with Replaces received on channel '%s' from endpoint '%s', but requested session does not exist\n",
			ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint));
		response = 481;
		goto inv_replace_failed;
	}

	invite.session = other_session;

	if (ast_sip_push_task_wait_serializer(other_session->serializer, invite_replaces,
		&invite)) {
		response = 481;
		goto inv_replace_failed;
	}

	ast_channel_lock(session->channel);
	ast_setstate(session->channel, AST_STATE_RING);
	ast_channel_unlock(session->channel);
	ast_raw_answer(session->channel);

	ast_debug(3, "INVITE with Replaces being attempted.  '%s' --> '%s'\n",
		ast_channel_name(session->channel), ast_channel_name(invite.channel));

	/* Unhold the channel now, as later we are not having access to it anymore */
	ast_queue_unhold(session->channel);
	ast_queue_frame(session->channel, &ast_null_frame);

	if (!invite.bridge) {
		struct ast_channel *chan = session->channel;

		/*
		 * This will use a synchronous task but we aren't operating in
		 * the serializer at this point in time, so it won't deadlock.
		 */
		if (!ast_channel_move(invite.channel, chan)) {
			/*
			 * We can't directly use session->channel because ast_channel_move()
			 * does a masquerade which changes session->channel to a different
			 * channel.  To ensure we work on the right channel we store a
			 * pointer locally before we begin so it remains valid.
			 */
			ast_hangup(chan);
		} else {
			response = AST_CAUSE_FAILURE;
		}
	} else {
		if (ast_bridge_impart(invite.bridge, session->channel, invite.channel, NULL,
			AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
			response = AST_CAUSE_FAILURE;
		}
	}

	ast_channel_unref(invite.channel);
	ao2_cleanup(invite.bridge);

	if (!response) {
		/*
		 * On success we cannot use session->channel in the debug message.
		 * This thread either no longer has a ref to session->channel or
		 * session->channel is no longer the original channel.
		 */
		ast_debug(3, "INVITE with Replaces successfully completed.\n");
	} else {
		ast_debug(3, "INVITE with Replaces failed on channel '%s', hanging up with cause '%d'\n",
			ast_channel_name(session->channel), response);
		ast_channel_lock(session->channel);
		ast_channel_hangupcause_set(session->channel, response);
		ast_channel_unlock(session->channel);
		ast_hangup(session->channel);
	}

	return 1;

inv_replace_failed:
	if (session->inv_session->dlg->state != PJSIP_DIALOG_STATE_ESTABLISHED) {
		ast_debug(3, "INVITE with Replaces failed on channel '%s', sending response of '%d'\n",
			ast_channel_name(session->channel), response);
		session->defer_terminate = 1;
		ast_hangup(session->channel);

		if (pjsip_inv_end_session(session->inv_session, response, NULL, &packet) == PJ_SUCCESS
			&& packet) {
			ast_sip_session_send_response(session, packet);
		}
	} else {
		ast_debug(3, "INVITE with Replaces in-dialog on channel '%s', hanging up\n",
			ast_channel_name(session->channel));
		ast_queue_hangup(session->channel);
	}

	return 1;
}

static int refer_incoming_refer_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	pjsip_generic_string_hdr *refer_to;
	char *uri;
	size_t uri_size;
	pjsip_uri *target;
	pjsip_sip_uri *target_uri;
	RAII_VAR(struct refer_progress *, progress, NULL, ao2_cleanup);
	pjsip_param *replaces;
	int response;

	static const pj_str_t str_refer_to = { "Refer-To", 8 };
	static const pj_str_t str_refer_to_s = { "r", 1 };
	static const pj_str_t str_replaces = { "Replaces", 8 };

	if (!session->channel) {
		/* No channel to refer.  Likely because the call was just hung up. */
		pjsip_dlg_respond(session->inv_session->dlg, rdata, 404, NULL, NULL, NULL);
		ast_debug(3, "Received a REFER on a session with no channel from endpoint '%s'.\n",
			ast_sorcery_object_get_id(session->endpoint));
		return 0;
	}

	if (!session->endpoint->allowtransfer) {
		pjsip_dlg_respond(session->inv_session->dlg, rdata, 603, NULL, NULL, NULL);
		ast_log(LOG_WARNING, "Endpoint %s transfer attempt blocked due to configuration\n",
				ast_sorcery_object_get_id(session->endpoint));
		return 0;
	}

	/* A Refer-To header is required */
	refer_to = pjsip_msg_find_hdr_by_names(rdata->msg_info.msg, &str_refer_to, &str_refer_to_s, NULL);
	if (!refer_to) {
		pjsip_dlg_respond(session->inv_session->dlg, rdata, 400, NULL, NULL, NULL);
		ast_debug(3, "Received a REFER without Refer-To on channel '%s' from endpoint '%s'\n",
			ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint));
		return 0;
	}

	/* The ast_copy_pj_str to uri is needed because it puts the NULL terminator to the uri
	 * as pjsip_parse_uri require a NULL terminated uri
	 */

	uri_size = pj_strlen(&refer_to->hvalue) + 1;
	uri = ast_alloca(uri_size);
	ast_copy_pj_str(uri, &refer_to->hvalue, uri_size);

	target = pjsip_parse_uri(rdata->tp_info.pool, uri, uri_size - 1, 0);

	if (!target
		|| (!PJSIP_URI_SCHEME_IS_SIP(target)
			&& !PJSIP_URI_SCHEME_IS_SIPS(target))) {

		pjsip_dlg_respond(session->inv_session->dlg, rdata, 400, NULL, NULL, NULL);
		ast_debug(3, "Received a REFER without a parseable Refer-To ('%s') on channel '%s' from endpoint '%s'\n",
			uri, ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint));
		return 0;
	}
	target_uri = pjsip_uri_get_uri(target);

	/* Set up REFER progress subscription if requested/possible */
	if (refer_progress_alloc(session, rdata, &progress)) {
		pjsip_dlg_respond(session->inv_session->dlg, rdata, 500, NULL, NULL, NULL);
		ast_debug(3, "Could not set up subscription for REFER on channel '%s' from endpoint '%s'\n",
			ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint));
		return 0;
	}

	/* Determine if this is an attended or blind transfer */
	if ((replaces = pjsip_param_find(&target_uri->header_param, &str_replaces)) ||
		(replaces = pjsip_param_find(&target_uri->other_param, &str_replaces))) {
		response = refer_incoming_attended_request(session, rdata, target_uri, replaces, progress);
	} else {
		response = refer_incoming_blind_request(session, rdata, target_uri, progress);
	}

	if (!progress) {
		/* The transferer has requested no subscription, so send a final response immediately */
		pjsip_tx_data *tdata;
		const pj_str_t str_refer_sub = { "Refer-Sub", 9 };
		const pj_str_t str_false = { "false", 5 };
		pjsip_hdr *hdr;

		ast_debug(3, "Progress monitoring not requested for REFER on channel '%s' from endpoint '%s', sending immediate response of '%d'\n",
			ast_channel_name(session->channel), ast_sorcery_object_get_id(session->endpoint), response);

		if (pjsip_dlg_create_response(session->inv_session->dlg, rdata, response, NULL, &tdata) != PJ_SUCCESS) {
			pjsip_dlg_respond(session->inv_session->dlg, rdata, response, NULL, NULL, NULL);
			return 0;
		}

		hdr = (pjsip_hdr*)pjsip_generic_string_hdr_create(tdata->pool, &str_refer_sub, &str_false);
		pjsip_msg_add_hdr(tdata->msg, hdr);

		pjsip_dlg_send_response(session->inv_session->dlg, pjsip_rdata_get_tsx(rdata), tdata);
	} else if (response != 200) {
		/* Since this failed we can send a final NOTIFY now and terminate the subscription */
		struct refer_progress_notification *notification = refer_progress_notification_alloc(progress, response, PJSIP_EVSUB_STATE_TERMINATED);

		if (notification) {
			/* The refer_progress_notify function will call ao2_cleanup on this for us */
			if (ast_sip_push_task(progress->serializer, refer_progress_notify, notification)) {
				ao2_cleanup(notification);
			}
		}
	}

	return 0;
}

static int refer_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	if (!pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, pjsip_get_refer_method())) {
		return refer_incoming_refer_request(session, rdata);
	} else if (!pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_invite_method)) {
		return refer_incoming_invite_request(session, rdata);
	} else {
		return 0;
	}
}

/*!
 * \brief Use the value of a channel variable as the value of a SIP header
 *
 * This looks up a variable name on a channel, then takes that value and adds
 * it to an outgoing SIP request. If the header already exists on the message,
 * then no action is taken.
 *
 * \pre chan is locked.
 *
 * \param chan The channel on which to find the variable.
 * \param var_name The name of the channel variable to use.
 * \param header_name The name of the SIP header to add to the outgoing message.
 * \param tdata The outgoing SIP message on which to add the header
 */
static void add_header_from_channel_var(struct ast_channel *chan, const char *var_name, const char *header_name, pjsip_tx_data *tdata)
{
	const char *var_value;
	pj_str_t pj_header_name;
	pjsip_hdr *header;

	var_value = pbx_builtin_getvar_helper(chan, var_name);
	if (ast_strlen_zero(var_value)) {
		return;
	}

	pj_cstr(&pj_header_name, header_name);
	header = pjsip_msg_find_hdr_by_name(tdata->msg, &pj_header_name, NULL);
	if (header) {
		return;
	}
	ast_sip_add_header(tdata, header_name, var_value);
}

static void refer_outgoing_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	if (pjsip_method_cmp(&tdata->msg->line.req.method, &pjsip_invite_method)
		|| !session->channel
		|| session->inv_session->state != PJSIP_INV_STATE_NULL) {
		return;
	}

	ast_channel_lock(session->channel);
	add_header_from_channel_var(session->channel, "SIPREPLACESHDR", "Replaces", tdata);
	add_header_from_channel_var(session->channel, "SIPREFERREDBYHDR", "Referred-By", tdata);
	ast_channel_unlock(session->channel);
}

static struct ast_sip_session_supplement refer_supplement = {
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL + 1,
	.incoming_request = refer_incoming_request,
	.outgoing_request = refer_outgoing_request,
};

static int load_module(void)
{
	const pj_str_t str_norefersub = { "norefersub", 10 };

	pjsip_replaces_init_module(ast_sip_get_pjsip_endpoint());
	pjsip_xfer_init_module(ast_sip_get_pjsip_endpoint());

	if (ast_sip_get_norefersub()) {
		pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(), NULL, PJSIP_H_SUPPORTED, NULL, 1, &str_norefersub);
	}

	if (ast_refer_tech_register(&refer_tech)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	refer_serializer = ast_sip_create_serializer("pjsip/refer");
	if (!refer_serializer) {
		ast_refer_tech_unregister(&refer_tech);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sip_register_service(&refer_out_of_dialog_module);
	ast_sip_register_service(&refer_progress_module);
	ast_sip_session_register_supplement(&refer_supplement);

	ast_module_shutdown_ref(ast_module_info->self);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&refer_supplement);
	ast_sip_unregister_service(&refer_out_of_dialog_module);
	ast_sip_unregister_service(&refer_progress_module);
	ast_taskprocessor_unreference(refer_serializer);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Blind and Attended Transfer Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session,res_pjsip_pubsub",
);
