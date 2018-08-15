/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2018, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/time.h"
#include "asterisk/test.h"
#include "asterisk/statsd.h"
#include "include/res_pjsip_private.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/threadpool.h"

/*
 * This implementation for OPTIONS support is based around the idea
 * that realistically an AOR generally has very few contacts and is
 * referenced by only a few endpoints. While it is perfectly fine for
 * use in opposite scenarios it works best in the above case. It is
 * also not shy to keeping state but it is reactive to outside changes
 * so it can be updated.
 *
 * The lowest level object in here is a contact and its associated
 * contact status. The result of an OPTIONS request to a contact is
 * reflected in the contact status. The scheduling of these OPTIONS
 * request is driven by the AOR. The AOR periodicially (according to
 * configuration) sends OPTIONS requests out to any contacts
 * associated with it. Contacts themselves are not individually
 * scheduled. Contacts can be added or deleted as appropriate with no
 * requirement to reschedule.
 *
 * The next level object up is the AOR itself. The result of a contact
 * status change is fed into it and the result composited with all
 * other contacts. This may result in the AOR itself changing state
 * (it can be either AVAILABLE or UNAVAILABLE).
 *
 * The highest level object up is the endpoint state compositor (ESC).
 * The result of AOR state changes is fed into it and the result
 * composited with all other referenced AORs. This may result in the
 * endpoint itself changing state (it can be either ONLINE or
 * OFFLINE).  If this occurs the permanent endpoint is updated to
 * reflect it.
 *
 * The threading model errs on the side of a world where things are
 * not constantly changing. That is: A world where AORs and endpoints
 * are not being constantly added/removed. This more closely mirrors
 * the usage of the vast majority of people. This scenario can still
 * be done but it may not be applied immediately.
 *
 * Manipulation of which AORs, endpoint state compositors, and
 * contacts exist is done within a single serializer. This ensures
 * that no matter the source threads order is preserved and you won't
 * get into a weird situation where things are referencing other
 * things that should have already been destroyed.
 *
 * Operations which impact the state of an AOR are done within a
 * serializer that is specific to the AOR. This includes the result of
 * a contact status change. This change is queued and executed on the
 * AOR serializer afterwards.
 *
 * Operations which impact an endpoint state compositor are protected
 * by a lock. This is done as the endpoint state compositor usage is
 * minimal and the overhead of using a serializer and queueing things
 * is not warranted.
 *
 * AORs which do not have a qualify frequency are also kept in here
 * but do not require the same criteria as qualified AORs to be
 * considered available. In their case as long as at least 1 contact
 * is configured on the AOR (or added to it by registration) it is
 * considered available.
 */

#define DEFAULT_LANGUAGE "en"
#define DEFAULT_ENCODING "text/plain"

/*! \brief These are the number of buckets to store AORs in */
#ifdef LOW_MEMORY
#define AOR_BUCKETS 61
#else
#define AOR_BUCKETS 1567
#endif

/*! \brief These are the number of contact status buckets */
#ifdef LOW_MEMORY
#define CONTACT_STATUS_BUCKETS 61
#else
#define CONTACT_STATUS_BUCKETS 1567
#endif

/*! \brief These are the number of buckets (per AOR) to use to store contacts */
#define CONTACT_BUCKETS 13

/*! \brief These are the number of buckets to store endpoint state compositors */
#define ENDPOINT_STATE_COMPOSITOR_BUCKETS 13

/*! \brief The initial vector size for the endpoint state compositors on an AOR */
#define ENDPOINT_STATE_COMPOSITOR_INITIAL_SIZE 1

/*! \brief These are the number of buckets (per endpoint state compositor) to use to store AOR statuses */
#define AOR_STATUS_BUCKETS 3

/*! \brief Maximum wait time to join the below shutdown group */
#define MAX_UNLOAD_TIMEOUT_TIME		10	/* Seconds */

/*! \brief Shutdown group for options serializers */
static struct ast_serializer_shutdown_group *shutdown_group;

/*!
 * \brief Structure which contains status information for an AOR feeding an endpoint state compositor
 */
struct sip_options_endpoint_aor_status {
	/*! \brief The last contributed available status of the named AOR (1 if available, 0 if not available) */
	char available;
	/*! \brief The name of the AOR */
	char name[0];
};

/*!
 * \brief Structure which contains composites information for endpoint state
 */
struct sip_options_endpoint_state_compositor {
	/*! \brief The last contributed available status of the AORs feeding this compositor */
	struct ao2_container *aor_statuses;
	/*!
	 * \brief Non-zero if the compositor is in normal operation. i.e. Not being setup/reconfigured.
	 *
	 * \details
	 * The aor layer can only update its aor_statuses record when not active.
	 * When active the aor layer can update its aor_statuses record, calculate the new
	 * number of available aors, determine if the endpoint compositor changed state,
	 * and report it.
	 */
	char active;
	/*! \brief The name of the endpoint */
	char name[0];
};

/*!
 * \brief Structure which contains an AOR and contacts for qualifying purposes
 */
struct sip_options_aor {
	/*! \brief The scheduler task for this AOR */
	struct ast_sip_sched_task *sched_task;
	/*! \brief The serializer for this AOR */
	struct ast_taskprocessor *serializer;
	/*! \brief All contacts associated with this AOR */
	struct ao2_container *contacts;
	/*!
	 * \brief Only dynamic contacts associated with this AOR
	 * \note Used to speed up applying AOR configuration by
	 * minimizing wild card sorcery access.
	 */
	struct ao2_container *dynamic_contacts;
	/*! \brief The endpoint state compositors we are feeding, a reference is held to each */
	AST_VECTOR(, struct sip_options_endpoint_state_compositor *) compositors;
	/*! \brief The number of available contacts on this AOR */
	unsigned int available;
	/*! \brief Frequency to send OPTIONS requests to AOR contacts. 0 is disabled. */
	unsigned int qualify_frequency;
	/*! If true authenticate the qualify challenge response if needed */
	int authenticate_qualify;
	/*! \brief Qualify timeout. 0 is diabled. */
	double qualify_timeout;
	/*! \brief The name of the AOR */
	char name[0];
};

/*!
 * \internal
 * \brief Container of active SIP AORs for qualifying
 */
static struct ao2_container *sip_options_aors;

/*!
 * \internal
 * \brief Container of contact statuses
 */
static struct ao2_container *sip_options_contact_statuses;

/*!
 * \internal
 * \brief Container of endpoint state compositors
 */
static struct ao2_container *sip_options_endpoint_state_compositors;

/*!
 * \internal
 * \brief Serializer for AOR, endpoint state compositor, and contact existence management
 */
static struct ast_taskprocessor *management_serializer;

static pj_status_t send_options_response(pjsip_rx_data *rdata, int code)
{
	pjsip_endpoint *endpt = ast_sip_get_pjsip_endpoint();
	pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
	pjsip_transaction *trans = pjsip_rdata_get_tsx(rdata);
	pjsip_tx_data *tdata;
	const pjsip_hdr *hdr;
	pj_status_t status;

	/* Make the response object */
	status = ast_sip_create_response(rdata, code, NULL, &tdata);
	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to create response (%d)\n", status);
		return status;
	}

	/* Add appropriate headers */
	if ((hdr = pjsip_endpt_get_capability(endpt, PJSIP_H_ACCEPT, NULL))) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, hdr));
	}
	if ((hdr = pjsip_endpt_get_capability(endpt, PJSIP_H_ALLOW, NULL))) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, hdr));
	}
	if ((hdr = pjsip_endpt_get_capability(endpt, PJSIP_H_SUPPORTED, NULL))) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, hdr));
	}

	/*
	 * XXX TODO: pjsip doesn't care a lot about either of these headers -
	 * while it provides specific methods to create them, they are defined
	 * to be the standard string header creation. We never did add them
	 * in chan_sip, although RFC 3261 says they SHOULD. Hard coded here.
	 */
	ast_sip_add_header(tdata, "Accept-Encoding", DEFAULT_ENCODING);
	ast_sip_add_header(tdata, "Accept-Language", DEFAULT_LANGUAGE);

	if (dlg && trans) {
		status = pjsip_dlg_send_response(dlg, trans, tdata);
	} else {
		struct ast_sip_endpoint *endpoint;

		endpoint = ast_pjsip_rdata_get_endpoint(rdata);
		status = ast_sip_send_stateful_response(rdata, tdata, endpoint);
		ao2_cleanup(endpoint);
	}

	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to send response (%d)\n", status);
	}

	return status;
}

static pj_bool_t options_on_rx_request(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	pjsip_uri *ruri;
	pjsip_sip_uri *sip_ruri;
	char exten[AST_MAX_EXTENSION];

	if (pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_options_method)) {
		return PJ_FALSE;
	}

	if (!(endpoint = ast_pjsip_rdata_get_endpoint(rdata))) {
		return PJ_FALSE;
	}

	ruri = rdata->msg_info.msg->line.req.uri;
	if (!PJSIP_URI_SCHEME_IS_SIP(ruri) && !PJSIP_URI_SCHEME_IS_SIPS(ruri)) {
		send_options_response(rdata, 416);
		return PJ_TRUE;
	}

	sip_ruri = pjsip_uri_get_uri(ruri);
	ast_copy_pj_str(exten, &sip_ruri->user, sizeof(exten));

	/*
	 * We may want to match in the dialplan without any user
	 * options getting in the way.
	 */
	AST_SIP_USER_OPTIONS_TRUNCATE_CHECK(exten);

	if (ast_shutting_down()) {
		/*
		 * Not taking any new calls at this time.
		 * Likely a server availability OPTIONS poll.
		 */
		send_options_response(rdata, 503);
	} else if (!ast_strlen_zero(exten)
		&& !ast_exists_extension(NULL, endpoint->context, exten, 1, NULL)) {
		send_options_response(rdata, 404);
	} else {
		send_options_response(rdata, 200);
	}
	return PJ_TRUE;
}

static pjsip_module options_module = {
	.name = {"Options Module", 14},
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_rx_request = options_on_rx_request,
};

static const char *status_map[] = {
	[UNAVAILABLE] = "Unreachable",
	[AVAILABLE] = "Reachable",
	[UNKNOWN] = "Unknown",
	[CREATED] = "Created",
	[REMOVED] = "Removed",
};

static const char *short_status_map[] = {
	[UNAVAILABLE] = "Unavail",
	[AVAILABLE] = "Avail",
	[UNKNOWN] = "Unknown",
	[CREATED] = "Created",
	[REMOVED] = "Removed",
};

const char *ast_sip_get_contact_status_label(const enum ast_sip_contact_status_type status)
{
	ast_assert(0 <= status && status < ARRAY_LEN(status_map));
	return status_map[status];
}

const char *ast_sip_get_contact_short_status_label(const enum ast_sip_contact_status_type status)
{
	ast_assert(0 <= status && status < ARRAY_LEN(short_status_map));
	return short_status_map[status];
}

/*! \brief Destructor for contact statuses */
static void sip_contact_status_dtor(void *obj)
{
	struct ast_sip_contact_status *contact_status = obj;

	ast_string_field_free_memory(contact_status);
}

static struct ast_sip_contact_status *sip_contact_status_alloc(const char *name)
{
	struct ast_sip_contact_status *contact_status;
	size_t size = sizeof(*contact_status) + strlen(name) + 1;

	contact_status = ao2_alloc_options(size, sip_contact_status_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!contact_status) {
		return NULL;
	}
	if (ast_string_field_init(contact_status, 256)) {
		ao2_ref(contact_status, -1);
		return NULL;
	}
	strcpy(contact_status->name, name); /* SAFE */
	return contact_status;
}

static struct ast_sip_contact_status *sip_contact_status_copy(const struct ast_sip_contact_status *src)
{
	struct ast_sip_contact_status *dst;

	dst = sip_contact_status_alloc(src->name);
	if (!dst) {
		return NULL;
	}

	if (ast_string_fields_copy(dst, src)) {
		ao2_ref(dst, -1);
		return NULL;
	}
	dst->rtt = src->rtt;
	dst->status = src->status;
	dst->last_status = src->last_status;
	return dst;
}

/*! \brief Hashing function for contact statuses */
AO2_STRING_FIELD_HASH_FN(ast_sip_contact_status, name);

/*! \brief Sort function for contact statuses */
AO2_STRING_FIELD_SORT_FN(ast_sip_contact_status, name);

/*! \brief Comparator function for contact statuses */
AO2_STRING_FIELD_CMP_FN(ast_sip_contact_status, name);

/*! \brief Helper function to allocate a contact statuses container */
static struct ao2_container *sip_options_contact_statuses_alloc(void)
{
	/*
	 * Replace duplicate objects so we can update the immutable
	 * contact status objects by simply linking in a new object.
	 */
	return ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, CONTACT_STATUS_BUCKETS,
		ast_sip_contact_status_hash_fn, ast_sip_contact_status_sort_fn,
		ast_sip_contact_status_cmp_fn);
}

/*! \brief Function which publishes a contact status update to all interested endpoints */
static void sip_options_publish_contact_state(const struct sip_options_aor *aor_options,
	const struct ast_sip_contact_status *contact_status)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&aor_options->compositors); ++i) {
		const struct sip_options_endpoint_state_compositor *endpoint_state_compositor;

		endpoint_state_compositor = AST_VECTOR_GET(&aor_options->compositors, i);
		ast_sip_persistent_endpoint_publish_contact_state(endpoint_state_compositor->name,
			contact_status);
	}
}

/*!
 * \brief Task to notify endpoints of a contact status change
 * \note Run by management_serializer
 */
static int contact_status_publish_update_task(void *obj)
{
	struct ast_sip_contact_status *contact_status = obj;
	struct sip_options_aor *aor_options;

	aor_options = ao2_find(sip_options_aors, contact_status->aor, OBJ_SEARCH_KEY);
	if (aor_options) {
		sip_options_publish_contact_state(aor_options, contact_status);
		ao2_ref(aor_options, -1);
	}
	ao2_ref(contact_status, -1);

	return 0;
}

static void sip_options_contact_status_update(struct ast_sip_contact_status *contact_status)
{
	struct ast_taskprocessor *mgmt_serializer = management_serializer;

	if (mgmt_serializer) {
		ao2_ref(contact_status, +1);
		if (ast_sip_push_task(mgmt_serializer, contact_status_publish_update_task,
			contact_status)) {
			ao2_ref(contact_status, -1);
		}
	}
}

struct ast_sip_contact_status *ast_res_pjsip_find_or_create_contact_status(const struct ast_sip_contact *contact)
{
	struct ast_sip_contact_status *contact_status;
	int res;

	/*
	 * At startup a contact status can be retrieved when static contacts
	 * are themselves being setup.  This happens before we are fully setup.
	 * Since we don't actually trigger qualify or anything as a result it
	 * is safe to do so.  They'll just get back a contact status that will
	 * be updated later.  At this time they only care that the contact
	 * status gets created for the static contact anyway.
	 */
	if (!sip_options_contact_statuses) {
		/*
		 * We haven't been pre-initialized or we are shutting down.
		 * Neither situation should happen.
		 */
		ast_assert(0);
		return NULL;
	}

	ao2_lock(sip_options_contact_statuses);

	/* If contact status for this contact already exists just return it */
	contact_status = ao2_find(sip_options_contact_statuses,
		ast_sorcery_object_get_id(contact), OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (contact_status) {
		ao2_unlock(sip_options_contact_statuses);
		return contact_status;
	}

	/* Otherwise we have to create and store a new contact status */
	contact_status = sip_contact_status_alloc(ast_sorcery_object_get_id(contact));
	if (!contact_status) {
		ao2_unlock(sip_options_contact_statuses);
		return NULL;
	}

	contact_status->rtt = 0;
	contact_status->status = CREATED;
	contact_status->last_status = CREATED;
	res = ast_string_field_set(contact_status, uri, contact->uri);
	res |= ast_string_field_set(contact_status, aor, contact->aor);
	if (res) {
		ao2_unlock(sip_options_contact_statuses);
		ao2_ref(contact_status, -1);
		return NULL;
	}

	ao2_link_flags(sip_options_contact_statuses, contact_status, OBJ_NOLOCK);
	ao2_unlock(sip_options_contact_statuses);

	ast_statsd_log_string_va("PJSIP.contacts.states.%s", AST_STATSD_GAUGE,
		"+1", 1.0, ast_sip_get_contact_status_label(contact_status->status));

	sip_options_contact_status_update(contact_status);

	return contact_status;
}

struct ast_sip_contact_status *ast_sip_get_contact_status(const struct ast_sip_contact *contact)
{
	return ao2_find(sip_options_contact_statuses, ast_sorcery_object_get_id(contact),
		OBJ_SEARCH_KEY);
}

/*! \brief Hashing function for OPTIONS AORs */
AO2_STRING_FIELD_HASH_FN(sip_options_aor, name);

/*! \brief Comparator function for SIP OPTIONS AORs */
AO2_STRING_FIELD_CMP_FN(sip_options_aor, name);

/*! \brief Hashing function for endpoint state compositors */
AO2_STRING_FIELD_HASH_FN(sip_options_endpoint_state_compositor, name);

/*! \brief Comparator function for endpoint state compositors */
AO2_STRING_FIELD_CMP_FN(sip_options_endpoint_state_compositor, name);

/*! \brief Structure used to contain information for an OPTIONS callback */
struct sip_options_contact_callback_data {
	/*! \brief The contact we qualified */
	struct ast_sip_contact *contact;
	/*! \brief The AOR options */
	struct sip_options_aor *aor_options;
	/*! \brief The time at which this OPTIONS attempt was started */
	struct timeval rtt_start;
	/*! \brief The new status of the contact */
	enum ast_sip_contact_status_type status;
};

/*!
 * \brief Return the current state of an endpoint state compositor
 * \pre The endpoint_state_compositor lock must be held.
 */
static enum ast_endpoint_state sip_options_get_endpoint_state_compositor_state(
	const struct sip_options_endpoint_state_compositor *endpoint_state_compositor)
{
	struct ao2_iterator it_aor_statuses;
	struct sip_options_endpoint_aor_status *aor_status;
	enum ast_endpoint_state state = AST_ENDPOINT_OFFLINE;

	it_aor_statuses = ao2_iterator_init(endpoint_state_compositor->aor_statuses, 0);
	for (; (aor_status = ao2_iterator_next(&it_aor_statuses)); ao2_ref(aor_status, -1)) {
		if (aor_status->available) {
			state = AST_ENDPOINT_ONLINE;
			ao2_ref(aor_status, -1);
			break;
		}
	}
	ao2_iterator_destroy(&it_aor_statuses);

	return state;
}

/*!
 * \brief Update the AOR status on an endpoint state compositor
 * \pre The endpoint_state_compositor lock must be held.
 */
static void sip_options_update_endpoint_state_compositor_aor(struct sip_options_endpoint_state_compositor *endpoint_state_compositor,
	const char *name, enum ast_sip_contact_status_type status)
{
	struct sip_options_endpoint_aor_status *aor_status;
	enum ast_endpoint_state endpoint_state;

	aor_status = ao2_find(endpoint_state_compositor->aor_statuses, name,
		OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!aor_status) {
		/* The AOR status doesn't exist already so we don't need to go any further */
		if (status == REMOVED) {
			return;
		}

		aor_status = ao2_alloc_options(sizeof(*aor_status) + strlen(name) + 1, NULL,
			AO2_ALLOC_OPT_LOCK_NOLOCK);
		if (!aor_status) {
			return;
		}

		strcpy(aor_status->name, name); /* SAFE */
		ao2_link(endpoint_state_compositor->aor_statuses, aor_status);
	}

	if (status == REMOVED) {
		/*
		 * If the AOR is being removed then remove its AOR status
		 * from the endpoint compositor.
		 */
		ao2_unlink(endpoint_state_compositor->aor_statuses, aor_status);
	} else {
		aor_status->available = (status == AVAILABLE ? 1 : 0);
	}
	ao2_ref(aor_status, -1);

	if (!endpoint_state_compositor->active) {
		return;
	}

	/* If this AOR is available then the endpoint itself has to be online */
	if (status == AVAILABLE) {
		ast_debug(3, "Endpoint state compositor '%s' is online as AOR '%s' is available\n",
			endpoint_state_compositor->name, name);
		endpoint_state = AST_ENDPOINT_ONLINE;
	} else {
		endpoint_state =
			sip_options_get_endpoint_state_compositor_state(endpoint_state_compositor);
	}

	ast_sip_persistent_endpoint_update_state(endpoint_state_compositor->name,
		endpoint_state);
}

/*! \brief Function which notifies endpoint state compositors of a state change of an AOR */
static void sip_options_notify_endpoint_state_compositors(struct sip_options_aor *aor_options,
	enum ast_sip_contact_status_type status)
{
	int i;

	/* Iterate through the associated endpoint state compositors updating them */
	for (i = 0; i < AST_VECTOR_SIZE(&aor_options->compositors); ++i) {
		struct sip_options_endpoint_state_compositor *endpoint_state_compositor;

		endpoint_state_compositor = AST_VECTOR_GET(&aor_options->compositors, i);

		ao2_lock(endpoint_state_compositor);
		sip_options_update_endpoint_state_compositor_aor(endpoint_state_compositor,
			aor_options->name, status);
		ao2_unlock(endpoint_state_compositor);
	}

	if (status == REMOVED) {
		AST_VECTOR_RESET(&aor_options->compositors, ao2_cleanup);
	}
}

/*!
 * \brief Task to notify an AOR of a contact status change
 * \note Run by aor_options->serializer
 */
static int sip_options_contact_status_notify_task(void *obj)
{
	struct sip_options_contact_callback_data *contact_callback_data = obj;
	struct ast_sip_contact *contact;
	struct ast_sip_contact_status *cs_old;
	struct ast_sip_contact_status *cs_new;

	/*
	 * Determine if this is a late arriving notification, as it is
	 * possible that we get a callback from PJSIP giving us contact
	 * status but in the mean time said contact has been removed
	 * from the controlling AOR.
	 */

	if (!contact_callback_data->aor_options->qualify_frequency) {
		/* Contact qualify response is late */
		ao2_ref(contact_callback_data, -1);
		return 0;
	}

	contact = ao2_find(contact_callback_data->aor_options->contacts,
		contact_callback_data->contact, OBJ_SEARCH_OBJECT);
	if (!contact) {
		/* Contact qualify response is late */
		ao2_ref(contact_callback_data, -1);
		return 0;
	}
	ao2_ref(contact, -1);

	cs_old = ao2_find(sip_options_contact_statuses,
		ast_sorcery_object_get_id(contact_callback_data->contact), OBJ_SEARCH_KEY);
	if (!cs_old) {
		/* Contact qualify response is late */
		ao2_ref(contact_callback_data, -1);
		return 0;
	}

	/* Update the contact specific status information */
	cs_new = sip_contact_status_copy(cs_old);
	ao2_ref(cs_old, -1);
	if (!cs_new) {
		ao2_ref(contact_callback_data, -1);
		return 0;
	}
	cs_new->last_status = cs_new->status;
	cs_new->status = contact_callback_data->status;
	cs_new->rtt =
		cs_new->status == AVAILABLE
			? ast_tvdiff_us(ast_tvnow(), contact_callback_data->rtt_start)
			: 0;
	ao2_link(sip_options_contact_statuses, cs_new);

	/*
	 * If the status has changed then notify the endpoint state compositors
	 * and publish our events.
	 */
	if (cs_new->last_status != cs_new->status) {
		if (cs_new->status == AVAILABLE) {
			/* If this is the first available contact then the AOR has become available */
			++contact_callback_data->aor_options->available;
			if (contact_callback_data->aor_options->available == 1) {
				sip_options_notify_endpoint_state_compositors(
					contact_callback_data->aor_options, AVAILABLE);
			}
		} else if (cs_new->last_status == AVAILABLE) {
			ast_assert(cs_new->status == UNAVAILABLE);

			/* If there are no more available contacts then this AOR is unavailable */
			--contact_callback_data->aor_options->available;
			if (!contact_callback_data->aor_options->available) {
				sip_options_notify_endpoint_state_compositors(
					contact_callback_data->aor_options, UNAVAILABLE);
			}
		}

		ast_verb(3, "Contact %s/%s is now %s.  RTT: %.3f msec\n",
			cs_new->aor,
			cs_new->uri,
			ast_sip_get_contact_status_label(cs_new->status),
			cs_new->rtt / 1000.0);

		ast_statsd_log_string_va("PJSIP.contacts.states.%s", AST_STATSD_GAUGE,
			"-1", 1.0, ast_sip_get_contact_status_label(cs_new->last_status));
		ast_statsd_log_string_va("PJSIP.contacts.states.%s", AST_STATSD_GAUGE,
			"+1", 1.0, ast_sip_get_contact_status_label(cs_new->status));

		sip_options_contact_status_update(cs_new);

		ast_test_suite_event_notify("AOR_CONTACT_UPDATE",
			"Contact: %s\r\n"
			"Status: %s",
			cs_new->name,
			ast_sip_get_contact_status_label(cs_new->status));
	} else {
		ast_debug(3, "Contact %s/%s status didn't change: %s, RTT: %.3f msec\n",
			cs_new->aor,
			cs_new->uri,
			ast_sip_get_contact_status_label(cs_new->status),
			cs_new->rtt / 1000.0);
	}

	ast_statsd_log_full_va("PJSIP.contacts.%s.rtt", AST_STATSD_TIMER,
		cs_new->status != AVAILABLE ? -1 : cs_new->rtt / 1000,
		1.0,
		cs_new->name);

	ast_test_suite_event_notify("AOR_CONTACT_QUALIFY_RESULT",
		"Contact: %s\r\n"
		"Status: %s\r\n"
		"RTT: %" PRId64,
		cs_new->name,
		ast_sip_get_contact_status_label(cs_new->status),
		cs_new->rtt);

	ast_debug(3, "AOR '%s' now has %d available contacts\n",
		contact_callback_data->aor_options->name,
		contact_callback_data->aor_options->available);

	ao2_ref(cs_new, -1);
	ao2_ref(contact_callback_data, -1);

	return 0;
}

/*! \brief Callback for when we get a result from a SIP OPTIONS request (a response or a timeout) */
static void qualify_contact_cb(void *token, pjsip_event *e)
{
	struct sip_options_contact_callback_data *contact_callback_data = token;
	enum ast_sip_contact_status_type status;

	switch(e->body.tsx_state.type) {
	default:
		ast_log(LOG_ERROR, "Unexpected PJSIP event %u\n", e->body.tsx_state.type);
		/* Fall through */
	case PJSIP_EVENT_TRANSPORT_ERROR:
	case PJSIP_EVENT_TIMER:
		status = UNAVAILABLE;
		break;
	case PJSIP_EVENT_RX_MSG:
		status = AVAILABLE;
		break;
	}

	/* Update the callback data with the new status, this will get handled in the AOR serializer */
	contact_callback_data->status = status;

	if (ast_sip_push_task(contact_callback_data->aor_options->serializer,
		sip_options_contact_status_notify_task, contact_callback_data)) {
		ast_log(LOG_NOTICE, "Unable to queue contact status update for '%s' on AOR '%s', state will be incorrect\n",
			ast_sorcery_object_get_id(contact_callback_data->contact),
			contact_callback_data->aor_options->name);
		ao2_ref(contact_callback_data, -1);
	}

	/* The task inherited our reference so we don't unreference here */
}

/*! \brief Destructor for contact callback data */
static void sip_options_contact_callback_data_dtor(void *obj)
{
	struct sip_options_contact_callback_data *contact_callback_data = obj;

	ao2_cleanup(contact_callback_data->contact);
	ao2_cleanup(contact_callback_data->aor_options);
}

/*! \brief Contact callback data allocator */
static struct sip_options_contact_callback_data *sip_options_contact_callback_data_alloc(
	struct ast_sip_contact *contact, struct sip_options_aor *aor_options)
{
	struct sip_options_contact_callback_data *contact_callback_data;

	contact_callback_data = ao2_alloc_options(sizeof(*contact_callback_data),
		sip_options_contact_callback_data_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!contact_callback_data) {
		return NULL;
	}

	contact_callback_data->contact = ao2_bump(contact);
	contact_callback_data->aor_options = ao2_bump(aor_options);
	contact_callback_data->rtt_start = ast_tvnow();

	return contact_callback_data;
}

/*! \brief Send a SIP OPTIONS request for a contact */
static int sip_options_qualify_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct sip_options_aor *aor_options = arg;
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	pjsip_tx_data *tdata;
	struct ast_sip_contact_status *contact_status;
	struct sip_options_contact_callback_data *contact_callback_data;

	ast_debug(3, "Qualifying contact '%s' on AOR '%s'\n",
		ast_sorcery_object_get_id(contact), aor_options->name);

	if (!ast_strlen_zero(contact->endpoint_name)) {
		endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
			contact->endpoint_name);
	}
	if (!endpoint && AST_VECTOR_SIZE(&aor_options->compositors)) {
		struct sip_options_endpoint_state_compositor *endpoint_state_compositor;

		endpoint_state_compositor = AST_VECTOR_GET(&aor_options->compositors, 0);
		endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
			endpoint_state_compositor->name);
	}
	if (!endpoint) {
		ast_debug(3, "Could not find an endpoint to qualify contact '%s' on AOR '%s'\n",
			ast_sorcery_object_get_id(contact), aor_options->name);
		return 0;
	}

	if (ast_sip_create_request("OPTIONS", NULL, endpoint, NULL, contact, &tdata)) {
		ast_log(LOG_ERROR, "Unable to create request to qualify contact %s on AOR %s\n",
			contact->uri, aor_options->name);
		return 0;
	}

	/* If an outbound proxy is specified set it on this request */
	if (!ast_strlen_zero(contact->outbound_proxy) &&
		ast_sip_set_outbound_proxy(tdata, contact->outbound_proxy)) {
		ast_log(LOG_ERROR, "Unable to apply outbound proxy on request to qualify contact %s\n",
			contact->uri);
		pjsip_tx_data_dec_ref(tdata);
		return 0;
	}

	contact_status = ast_res_pjsip_find_or_create_contact_status(contact);
	if (!contact_status) {
		ast_log(LOG_ERROR, "Unable to retrieve contact status information for contact %s on AOR %s\n",
			contact->uri, aor_options->name);
		pjsip_tx_data_dec_ref(tdata);
		return 0;
	}
	ao2_ref(contact_status, -1);

	contact_callback_data = sip_options_contact_callback_data_alloc(contact, aor_options);
	if (!contact_callback_data) {
		ast_log(LOG_ERROR, "Unable to create object to contain callback data for contact %s on AOR %s\n",
			contact->uri, aor_options->name);
		pjsip_tx_data_dec_ref(tdata);
		return 0;
	}

	if (ast_sip_send_out_of_dialog_request(tdata, endpoint,
		(int)(aor_options->qualify_timeout * 1000), contact_callback_data,
		qualify_contact_cb)) {
		ast_log(LOG_ERROR, "Unable to send request to qualify contact %s on AOR %s\n",
			contact->uri, aor_options->name);
		ao2_ref(contact_callback_data, -1);
	}

	return 0;
}

/*!
 * \brief Task to qualify contacts of an AOR
 * \note Run by aor_options->serializer
 */
static int sip_options_qualify_aor(void *obj)
{
	struct sip_options_aor *aor_options = obj;

	ast_debug(3, "Qualifying all contacts on AOR '%s'\n", aor_options->name);

	/* Attempt to send an OPTIONS request to every contact on this AOR */
	ao2_callback(aor_options->contacts, OBJ_NODATA, sip_options_qualify_contact,
		(struct sip_options_aor *) aor_options);

	/* Always reschedule to the frequency we should go */
	return aor_options->qualify_frequency * 1000;
}

/*! \brief Forward declaration of this helpful function */
static int sip_options_remove_contact(void *obj, void *arg, int flags);

/*! \brief Destructor function for SIP OPTIONS AORs */
static void sip_options_aor_dtor(void *obj)
{
	struct sip_options_aor *aor_options = obj;

	/*
	 * Any contacts are unreachable since the AOR is being destroyed
	 * so remove their contact status
	 */
	if (aor_options->contacts) {
		ao2_callback(aor_options->contacts, OBJ_NODATA | OBJ_UNLINK,
			sip_options_remove_contact, aor_options);
		ao2_ref(aor_options->contacts, -1);
	}
	ao2_cleanup(aor_options->dynamic_contacts);

	ast_taskprocessor_unreference(aor_options->serializer);

	ast_assert(AST_VECTOR_SIZE(&aor_options->compositors) == 0);
	AST_VECTOR_FREE(&aor_options->compositors);
}

/*! \brief Allocator for AOR OPTIONS */
static struct sip_options_aor *sip_options_aor_alloc(struct ast_sip_aor *aor)
{
	struct sip_options_aor *aor_options;
	char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];

	aor_options = ao2_alloc_options(sizeof(*aor_options) + strlen(ast_sorcery_object_get_id(aor)) + 1,
		sip_options_aor_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!aor_options) {
		return NULL;
	}

	strcpy(aor_options->name, ast_sorcery_object_get_id(aor)); /* SAFE */

	ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "pjsip/options/%s",
		ast_sorcery_object_get_id(aor));
	aor_options->serializer = ast_sip_create_serializer_group_named(tps_name,
		shutdown_group);
	if (!aor_options->serializer) {
		ao2_ref(aor_options, -1);
		return NULL;
	}

	if (AST_VECTOR_INIT(&aor_options->compositors, ENDPOINT_STATE_COMPOSITOR_INITIAL_SIZE)) {
		ao2_ref(aor_options, -1);
		return NULL;
	}

	aor_options->contacts = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, CONTACT_BUCKETS, ast_sorcery_object_id_hash,
		ast_sorcery_object_id_sort, ast_sorcery_object_id_compare);
	if (!aor_options->contacts) {
		ao2_ref(aor_options, -1);
		return NULL;
	}

	aor_options->dynamic_contacts = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, CONTACT_BUCKETS, ast_sorcery_object_id_hash,
		ast_sorcery_object_id_sort, ast_sorcery_object_id_compare);
	if (!aor_options->dynamic_contacts) {
		ao2_ref(aor_options, -1);
		return NULL;
	}

	return aor_options;
}

/*! \brief Remove contact status for a hint */
static void sip_options_remove_contact_status(struct sip_options_aor *aor_options,
	struct ast_sip_contact *contact)
{
	struct ast_sip_contact_status *cs_new;
	struct ast_sip_contact_status *cs_old;

	cs_old = ao2_find(sip_options_contact_statuses, ast_sorcery_object_get_id(contact),
		OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (!cs_old) {
		ast_debug(3, "Attempted to remove contact status for '%s' but it does not exist\n",
			ast_sorcery_object_get_id(contact));
		return;
	}

	ast_verb(2, "Contact %s/%s has been deleted\n", contact->aor, contact->uri);

	/* Update the contact status to reflect its new state */
	cs_new = sip_contact_status_copy(cs_old);
	if (!cs_new) {
		/*
		 * We'll have to violate the immutable property because we
		 * couldn't create a new one to modify and we are deleting
		 * the contact status anyway.
		 */
		cs_new = cs_old;
	} else {
		ao2_ref(cs_old, -1);
	}
	cs_new->last_status = cs_new->status;
	cs_new->status = REMOVED;
	cs_new->rtt = 0;

	ast_statsd_log_string_va("PJSIP.contacts.states.%s", AST_STATSD_GAUGE,
		"-1", 1.0, ast_sip_get_contact_status_label(cs_new->last_status));
	ast_statsd_log_string_va("PJSIP.contacts.states.%s", AST_STATSD_GAUGE,
		"+1", 1.0, ast_sip_get_contact_status_label(cs_new->status));

	sip_options_contact_status_update(cs_new);

	/*
	 * The only time we need to update the AOR is if this contact was
	 * available and qualify is in use, otherwise we can just stop
	 * early.
	 */
	if (!aor_options->qualify_frequency || cs_new->last_status != AVAILABLE) {
		ao2_ref(cs_new, -1);
		return;
	}

	--aor_options->available;
	if (!aor_options->available) {
		sip_options_notify_endpoint_state_compositors(aor_options, UNAVAILABLE);
	}

	ast_debug(3, "AOR '%s' now has %d available contacts\n", aor_options->name,
		aor_options->available);

	ao2_ref(cs_new, -1);
}

/*! \brief Task data for AOR creation or updating */
struct sip_options_synchronize_aor_task_data {
	/*! \brief The AOR options for this AOR */
	struct sip_options_aor *aor_options;
	/*! \brief The AOR which contains the new configuraton */
	struct ast_sip_aor *aor;
	/*! \brief Optional container of existing AOR s*/
	struct ao2_container *existing;
	/*! \brief Whether this AOR is being added */
	int added;
};

/*! \brief Callback function to remove a contact and its contact status from an AOR */
static int sip_options_remove_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct sip_options_aor *aor_options = arg;

	sip_options_remove_contact_status(aor_options, contact);

	return CMP_MATCH;
}

/*! \brief Determine an initial time for scheduling AOR qualifying */
static int sip_options_determine_initial_qualify_time(int qualify_frequency)
{
	int initial_interval;
	int max_time = ast_sip_get_max_initial_qualify_time();

	if (max_time && max_time < qualify_frequency) {
		initial_interval = max_time;
	} else {
		initial_interval = qualify_frequency;
	}

	initial_interval = (int)((initial_interval * 1000) * ast_random_double());
	return 0 < initial_interval ? initial_interval : 1;
}

/*! \brief Set the contact status for a contact */
static void sip_options_set_contact_status(struct ast_sip_contact_status *contact_status,
	enum ast_sip_contact_status_type status)
{
	struct ast_sip_contact_status *cs_new;

	/* Update the contact specific status information */
	cs_new = sip_contact_status_copy(contact_status);
	if (!cs_new) {
		return;
	}
	cs_new->last_status = cs_new->status;
	cs_new->status = status;

	/*
	 * We need to always set the RTT to zero because we haven't completed
	 * an OPTIONS ping so RTT is unknown.  If the OPTIONS ping were still
	 * running it will be refreshed on the next go round anyway.
	 */
	cs_new->rtt = 0;

	ao2_link(sip_options_contact_statuses, cs_new);

	if (cs_new->status != cs_new->last_status) {
		ast_verb(3, "Contact %s/%s is now %s.\n",
			cs_new->aor, cs_new->uri,
			ast_sip_get_contact_status_label(cs_new->status));

		ast_statsd_log_string_va("PJSIP.contacts.states.%s", AST_STATSD_GAUGE,
			"-1", 1.0, ast_sip_get_contact_status_label(cs_new->last_status));
		ast_statsd_log_string_va("PJSIP.contacts.states.%s", AST_STATSD_GAUGE,
			"+1", 1.0, ast_sip_get_contact_status_label(cs_new->status));

		sip_options_contact_status_update(cs_new);

		ast_test_suite_event_notify("AOR_CONTACT_UPDATE",
			"Contact: %s\r\n"
			"Status: %s",
			cs_new->name,
			ast_sip_get_contact_status_label(cs_new->status));
	}
	ao2_ref(cs_new, -1);
}

/*! \brief Transition the contact status to unqualified mode */
static int sip_options_set_contact_status_unqualified(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct ast_sip_contact_status *contact_status;

	contact_status = ast_res_pjsip_find_or_create_contact_status(contact);
	if (!contact_status) {
		return 0;
	}

	switch (contact_status->status) {
	case AVAILABLE:
	case UNAVAILABLE:
	case CREATED:
		sip_options_set_contact_status(contact_status, UNKNOWN);
		break;
	case UNKNOWN:
	case REMOVED:
		break;
	}

	ao2_ref(contact_status, -1);

	return 0;
}

/*! \brief Transition the contact status to qualified mode */
static int sip_options_set_contact_status_qualified(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct ast_sip_contact_status *contact_status;

	contact_status = ast_res_pjsip_find_or_create_contact_status(contact);
	if (!contact_status) {
		return 0;
	}

	switch (contact_status->status) {
	case AVAILABLE:
		sip_options_set_contact_status(contact_status, UNAVAILABLE);
		break;
	case UNAVAILABLE:
	case UNKNOWN:
	case CREATED:
	case REMOVED:
		break;
	}

	ao2_ref(contact_status, -1);

	return 0;
}

/*! \brief Count AVAILABLE qualified contacts. */
static int sip_options_contact_status_available_count(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	unsigned int *available = arg;
	struct ast_sip_contact_status *contact_status;

	contact_status = ast_res_pjsip_find_or_create_contact_status(contact);
	if (!contact_status) {
		return 0;
	}

	/* Count qualified available contacts. */
	switch (contact_status->status) {
	case AVAILABLE:
		++*available;
		break;
	case UNAVAILABLE:
	case UNKNOWN:
	case CREATED:
	case REMOVED:
		break;
	}

	ao2_ref(contact_status, -1);

	return 0;
}

/*!
 * \brief Function which applies configuration to an AOR options structure
 * \note Run by aor_options->serializer (or management_serializer on aor_options creation)
 */
static void sip_options_apply_aor_configuration(struct sip_options_aor *aor_options,
	struct ast_sip_aor *aor, int is_new)
{
	struct ao2_container *existing_contacts;
	struct ast_sip_contact *contact;
	struct ao2_iterator iter;

	ast_debug(3, "Configuring AOR '%s' with current state of configuration and world\n",
		aor_options->name);

	/*
	 * Permanent contacts, since we receive no notification that they
	 * are gone, follow the same approach as AORs.  We create a copy
	 * of the existing container and any reused contacts are removed
	 * from it.  Any contacts remaining in the container after
	 * processing no longer exist so we need to remove their state.
	 */
	existing_contacts = ao2_container_clone(aor_options->contacts, 0);
	if (!existing_contacts) {
		ast_log(LOG_WARNING, "Synchronization of AOR '%s' failed for qualify, retaining existing state\n",
			aor_options->name);
		return;
	}

	ao2_callback(aor_options->contacts, OBJ_NODATA | OBJ_UNLINK | OBJ_MULTIPLE,
		NULL, NULL);

	/* Process permanent contacts */
	if (aor->permanent_contacts) {
		iter = ao2_iterator_init(aor->permanent_contacts, 0);
		for (; (contact = ao2_iterator_next(&iter)); ao2_ref(contact, -1)) {
			ao2_find(existing_contacts, ast_sorcery_object_get_id(contact),
				OBJ_NODATA | OBJ_UNLINK | OBJ_SEARCH_KEY);
			ao2_link(aor_options->contacts, contact);
		}
		ao2_iterator_destroy(&iter);
	}

	/*
	 * If this is newly added we need to see if there are any
	 * existing dynamic contacts to add.  Ones that are added
	 * after creation will occur as a result of the contact
	 * observer creation callback.
	 */
	if (is_new) {
		size_t prefix_len = strlen(ast_sorcery_object_get_id(aor)) + sizeof(";@") - 1;
		char prefix[prefix_len + 1];
		struct ao2_container *contacts;

		sprintf(prefix, "%s;@", ast_sorcery_object_get_id(aor)); /* Safe */
		contacts = ast_sorcery_retrieve_by_prefix(ast_sip_get_sorcery(), "contact",
			prefix, prefix_len);
		if (contacts) {
			ao2_container_dup(aor_options->dynamic_contacts, contacts, 0);
			ao2_ref(contacts, -1);
		}
	}

	/* Process dynamic contacts */
	iter = ao2_iterator_init(aor_options->dynamic_contacts, 0);
	for (; (contact = ao2_iterator_next(&iter)); ao2_ref(contact, -1)) {
		ao2_find(existing_contacts, ast_sorcery_object_get_id(contact),
			OBJ_NODATA | OBJ_UNLINK | OBJ_SEARCH_KEY);
		ao2_link(aor_options->contacts, contact);
	}
	ao2_iterator_destroy(&iter);

	/* Any contacts left no longer exist, so raise events and make them disappear */
	ao2_callback(existing_contacts, OBJ_NODATA | OBJ_UNLINK,
		sip_options_remove_contact, aor_options);
	ao2_ref(existing_contacts, -1);

	/*
	 * Update the available count if we transition between qualified
	 * and unqualified.  In the qualified case we need to start with
	 * 0 available as the qualify process will take care of it.  In
	 * the unqualified case it is based on the number of contacts
	 * present.
	 */
	if (!aor->qualify_frequency) {
		ao2_callback(aor_options->contacts, OBJ_NODATA,
			sip_options_set_contact_status_unqualified, NULL);
		aor_options->available = ao2_container_count(aor_options->contacts);
		ast_debug(3, "AOR '%s' is unqualified, number of available contacts is therefore '%d'\n",
			aor_options->name, aor_options->available);
	} else if (!aor_options->qualify_frequency) {
		ao2_callback(aor_options->contacts, OBJ_NODATA,
			sip_options_set_contact_status_qualified, NULL);
		aor_options->available = 0;
		ast_debug(3, "AOR '%s' has transitioned from unqualified to qualified, reset available contacts to 0\n",
			aor_options->name);
	} else {
		/*
		* Count the number of AVAILABLE qualified contacts to ensure
		* the count is in sync with reality.
		*/
		aor_options->available = 0;
		ao2_callback(aor_options->contacts, OBJ_NODATA,
			sip_options_contact_status_available_count, &aor_options->available);
	}

	aor_options->authenticate_qualify = aor->authenticate_qualify;
	aor_options->qualify_timeout = aor->qualify_timeout;

	/*
	 * If we need to stop or start the scheduled callback then do so.
	 * This occurs due to the following:
	 * 1. The qualify frequency has changed
	 * 2. Contacts were added when previously there were none
	 * 3. There are no contacts but previously there were some
	 */
	if (aor_options->qualify_frequency != aor->qualify_frequency
		|| (!aor_options->sched_task && ao2_container_count(aor_options->contacts))
		|| (aor_options->sched_task && !ao2_container_count(aor_options->contacts))) {
		if (aor_options->sched_task) {
			ast_sip_sched_task_cancel(aor_options->sched_task);
			ao2_ref(aor_options->sched_task, -1);
			aor_options->sched_task = NULL;
		}

		/* If there is still a qualify frequency then schedule this */
		aor_options->qualify_frequency = aor->qualify_frequency;
		if (aor_options->qualify_frequency
			&& ao2_container_count(aor_options->contacts)) {
			aor_options->sched_task = ast_sip_schedule_task(aor_options->serializer,
				sip_options_determine_initial_qualify_time(aor_options->qualify_frequency),
				sip_options_qualify_aor, ast_taskprocessor_name(aor_options->serializer),
				aor_options, AST_SIP_SCHED_TASK_VARIABLE | AST_SIP_SCHED_TASK_DATA_AO2);
			if (!aor_options->sched_task) {
				ast_log(LOG_ERROR, "Unable to schedule qualify for contacts of AOR '%s'\n",
					aor_options->name);
			}
		}
	}

	ast_debug(3, "AOR '%s' now has %d available contacts\n", aor_options->name,
		aor_options->available);
}

/*!
 * \brief Task to synchronize an AOR with our local state
 * \note Run by aor_options->serializer (or management_serializer on aor_options creation)
 */
static int sip_options_synchronize_aor_task(void *obj)
{
	struct sip_options_synchronize_aor_task_data *task_data = obj;
	int i;

	ast_debug(3, "Synchronizing AOR '%s' with current state of configuration and world\n",
		task_data->aor_options->name);

	sip_options_apply_aor_configuration(task_data->aor_options, task_data->aor,
		task_data->added);

	/*
	 * Endpoint state compositors are removed in this operation but not
	 * added.  To reduce the amount of work done they are done later.  In
	 * the mean time things can still qualify and once an endpoint state
	 * compositor is added to the AOR it will be updated with the current
	 * state.
	 */
	for (i = 0; i < AST_VECTOR_SIZE(&task_data->aor_options->compositors); ++i) {
		struct sip_options_endpoint_state_compositor *endpoint_state_compositor;

		endpoint_state_compositor = AST_VECTOR_GET(&task_data->aor_options->compositors, i);

		ao2_lock(endpoint_state_compositor);
		endpoint_state_compositor->active = 0;
		sip_options_update_endpoint_state_compositor_aor(endpoint_state_compositor,
			task_data->aor_options->name, REMOVED);
		ao2_unlock(endpoint_state_compositor);
	}
	AST_VECTOR_RESET(&task_data->aor_options->compositors, ao2_cleanup);

	return 0;
}

/*!
 * \brief Synchronize an AOR with our local state
 * \note Run by management_serializer
 */
static int sip_options_synchronize_aor(void *obj, void *arg, int flags)
{
	struct sip_options_synchronize_aor_task_data task_data = {
		.aor = obj,
		.existing = arg,
	};

	task_data.aor_options = ao2_find(sip_options_aors,
		ast_sorcery_object_get_id(task_data.aor), OBJ_SEARCH_KEY);
	if (!task_data.aor_options) {
		task_data.aor_options = sip_options_aor_alloc(task_data.aor);
		if (!task_data.aor_options) {
			return 0;
		}

		task_data.added = 1;

		/* Nothing is aware of this AOR yet so we can just update it in this thread */
		sip_options_synchronize_aor_task(&task_data);
		ao2_link(sip_options_aors, task_data.aor_options);
	} else {
		/* This AOR already exists so we have to do manipulation in its serializer */
		ast_sip_push_task_wait_serializer(task_data.aor_options->serializer,
			sip_options_synchronize_aor_task, &task_data);
	}

	ao2_ref(task_data.aor_options, -1);

	if (task_data.existing) {
		ao2_find(task_data.existing, ast_sorcery_object_get_id(task_data.aor),
			OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
	}

	return 0;
}

/*! \brief Destructor for endpoint state compositors */
static void sip_options_endpoint_state_compositor_dtor(void *obj)
{
	struct sip_options_endpoint_state_compositor *endpoint_state_compositor = obj;

	ao2_cleanup(endpoint_state_compositor->aor_statuses);
}

/*! \brief Hashing function for endpoint AOR status */
AO2_STRING_FIELD_HASH_FN(sip_options_endpoint_aor_status, name);

/*! \brief Comparator function for endpoint AOR status */
AO2_STRING_FIELD_CMP_FN(sip_options_endpoint_aor_status, name);

/*! \brief Find (or create) an endpoint state compositor */
static struct sip_options_endpoint_state_compositor *sip_options_endpoint_state_compositor_find_or_alloc(const struct ast_sip_endpoint *endpoint)
{
	struct sip_options_endpoint_state_compositor *endpoint_state_compositor;

	ao2_lock(sip_options_endpoint_state_compositors);
	endpoint_state_compositor = ao2_find(sip_options_endpoint_state_compositors,
		ast_sorcery_object_get_id(endpoint), OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (endpoint_state_compositor) {
		ao2_unlock(sip_options_endpoint_state_compositors);
		return endpoint_state_compositor;
	}

	endpoint_state_compositor = ao2_alloc(sizeof(*endpoint_state_compositor)
		+ strlen(ast_sorcery_object_get_id(endpoint)) + 1,
		sip_options_endpoint_state_compositor_dtor);
	if (!endpoint_state_compositor) {
		ao2_unlock(sip_options_endpoint_state_compositors);
		return NULL;
	}

	/*
	 * NOTE: The endpoint_state_compositor->aor_statuses container is
	 * externally protected by the endpoint_state_compositor lock.
	 */
	endpoint_state_compositor->aor_statuses = ao2_container_alloc_hash(
		AO2_ALLOC_OPT_LOCK_NOLOCK, 0, AOR_STATUS_BUCKETS,
		sip_options_endpoint_aor_status_hash_fn, NULL,
		sip_options_endpoint_aor_status_cmp_fn);
	if (!endpoint_state_compositor->aor_statuses) {
		ao2_unlock(sip_options_endpoint_state_compositors);
		ao2_ref(endpoint_state_compositor, -1);
		return NULL;
	}

	strcpy(endpoint_state_compositor->name, ast_sorcery_object_get_id(endpoint)); /* SAFE */

	ao2_link_flags(sip_options_endpoint_state_compositors, endpoint_state_compositor,
		OBJ_NOLOCK);
	ao2_unlock(sip_options_endpoint_state_compositors);

	return endpoint_state_compositor;
}

/*! \brief Task details for adding an AOR to an endpoint state compositor */
struct sip_options_endpoint_compositor_task_data {
	/*! \brief The AOR options that the endpoint state compositor should be added to */
	struct sip_options_aor *aor_options;
	/*! \brief The endpoint state compositor */
	struct sip_options_endpoint_state_compositor *endpoint_state_compositor;
};

/*!
 * \brief Task which adds an AOR to an endpoint state compositor
 * \note Run by aor_options->serializer
 */
static int sip_options_endpoint_compositor_add_task(void *obj)
{
	struct sip_options_endpoint_compositor_task_data *task_data = obj;

	ast_debug(3, "Adding endpoint compositor '%s' to AOR '%s'\n",
		task_data->endpoint_state_compositor->name, task_data->aor_options->name);

	ao2_ref(task_data->endpoint_state_compositor, +1);
	if (AST_VECTOR_APPEND(&task_data->aor_options->compositors,
		task_data->endpoint_state_compositor)) {
		/* Failed to add so no need to update the endpoint status.  Nothing changed. */
		ao2_ref(task_data->endpoint_state_compositor, -1);
		return 0;
	}

	ao2_lock(task_data->endpoint_state_compositor);
	sip_options_update_endpoint_state_compositor_aor(task_data->endpoint_state_compositor,
		task_data->aor_options->name,
		task_data->aor_options->available ? AVAILABLE : UNAVAILABLE);
	ao2_unlock(task_data->endpoint_state_compositor);

	return 0;
}

/*!
 * \brief Task which adds removes an AOR from an endpoint state compositor
 * \note Run by aor_options->serializer
 */
static int sip_options_endpoint_compositor_remove_task(void *obj)
{
	struct sip_options_endpoint_compositor_task_data *task_data = obj;
	int i;

	ast_debug(3, "Removing endpoint compositor '%s' from AOR '%s'\n",
		task_data->endpoint_state_compositor->name,
		task_data->aor_options->name);

	for (i = 0; i < AST_VECTOR_SIZE(&task_data->aor_options->compositors); ++i) {
		struct sip_options_endpoint_state_compositor *endpoint_state_compositor;

		endpoint_state_compositor = AST_VECTOR_GET(&task_data->aor_options->compositors, i);
		if (endpoint_state_compositor != task_data->endpoint_state_compositor) {
			continue;
		}

		AST_VECTOR_REMOVE(&task_data->aor_options->compositors, i, 0);
		ao2_ref(endpoint_state_compositor, -1);
		break;
	}

	return 0;
}

/*!
 * \brief Synchronize an endpoint with our local state
 * \note Run by management_serializer
 */
static int sip_options_synchronize_endpoint(void *obj, void *arg, int flags)
{
	struct ast_sip_endpoint *endpoint = obj;
	struct ast_sip_aor *aor = arg;
	char *aors;
	char *aor_name;
	struct sip_options_endpoint_compositor_task_data task_data = { NULL, };

	if (ast_strlen_zero(endpoint->aors)) {
		/* There are no AORs, so really... who the heck knows */
		ast_debug(3, "Endpoint '%s' is not interested in any AORs so not creating endpoint state compositor\n",
			ast_sorcery_object_get_id(endpoint));
		return 0;
	}

	ast_debug(3, "Synchronizing endpoint '%s' with AORs '%s'\n",
		ast_sorcery_object_get_id(endpoint), endpoint->aors);

	aors = ast_strdupa(endpoint->aors);
	while ((aor_name = ast_strip(strsep(&aors, ",")))) {
		if (ast_strlen_zero(aor_name)) {
			continue;
		}
		if (aor && strcasecmp(ast_sorcery_object_get_id(aor), aor_name)) {
			ast_debug(3, "Filtered AOR '%s' on endpoint '%s' as we are looking for '%s'\n",
				aor_name, ast_sorcery_object_get_id(endpoint),
				ast_sorcery_object_get_id(aor));
			continue;
		}

		task_data.aor_options = ao2_find(sip_options_aors, aor_name, OBJ_SEARCH_KEY);
		if (!task_data.aor_options) {
			/*
			 * They have referenced an invalid AOR. If that's all they've
			 * done we will set them to offline at the end.
			 */
			ast_debug(3, "Endpoint '%s' referenced invalid AOR '%s'\n",
				ast_sorcery_object_get_id(endpoint), aor_name);
			continue;
		}

		if (!task_data.endpoint_state_compositor) {
			/*
			 * We create an endpoint state compositor only after we know
			 * for sure we need it.
			 */
			task_data.endpoint_state_compositor =
				sip_options_endpoint_state_compositor_find_or_alloc(endpoint);
			if (!task_data.endpoint_state_compositor) {
				ast_log(LOG_WARNING,
					"Could not create endpoint state compositor for '%s', endpoint state will be incorrect\n",
					ast_sorcery_object_get_id(endpoint));
				ao2_ref(task_data.aor_options, -1);
				ast_sip_persistent_endpoint_update_state(ast_sorcery_object_get_id(endpoint),
					AST_ENDPOINT_OFFLINE);
				return 0;
			}
		}

		/* We use a synchronous task so that we don't flood the system */
		ast_sip_push_task_wait_serializer(task_data.aor_options->serializer,
			sip_options_endpoint_compositor_add_task, &task_data);

		ao2_ref(task_data.aor_options, -1);

		/*
		 * If we filtered on a specific AOR name then the endpoint can
		 * only reference it once so break early.
		 */
		if (aor) {
			break;
		}
	}

	if (task_data.endpoint_state_compositor) {
		/*
		 * If an endpoint state compositor is present determine the current state
		 * of the endpoint and update it.
		 */
		ao2_lock(task_data.endpoint_state_compositor);
		task_data.endpoint_state_compositor->active = 1;
		ast_sip_persistent_endpoint_update_state(ast_sorcery_object_get_id(endpoint),
			sip_options_get_endpoint_state_compositor_state(task_data.endpoint_state_compositor));
		ao2_unlock(task_data.endpoint_state_compositor);

		ao2_ref(task_data.endpoint_state_compositor, -1);
	} else {
		/* If there is none then they may have referenced an invalid AOR or none at all */
		ast_debug(3, "Endpoint '%s' has no AORs feeding it, setting it to offline state as default\n",
			ast_sorcery_object_get_id(endpoint));
		ast_sip_persistent_endpoint_update_state(ast_sorcery_object_get_id(endpoint),
			AST_ENDPOINT_OFFLINE);
	}

	return 0;
}

/*!
 * \brief Task which removes an AOR from all of the ESCs it is reporting to
 * \note Run by aor_options->serializer
 */
static int sip_options_aor_remove_task(void *obj)
{
	struct sip_options_aor *aor_options = obj;

	sip_options_notify_endpoint_state_compositors(aor_options, REMOVED);

	if (aor_options->sched_task) {
		ast_sip_sched_task_cancel(aor_options->sched_task);
		ao2_ref(aor_options->sched_task, -1);
		aor_options->sched_task = NULL;
	}

	return 0;
}

/*!
 * \brief Callback which removes any unused AORs that remained after reloading
 * \note Run by management_serializer
 */
static int sip_options_unused_aor(void *obj, void *arg, int flags)
{
	struct sip_options_aor *aor_options = obj;

	ast_debug(3, "AOR '%s' is no longer configured, removing it\n", aor_options->name);

	ast_sip_push_task_wait_serializer(aor_options->serializer, sip_options_aor_remove_task,
		aor_options);
	ao2_unlink(sip_options_aors, aor_options);

	return CMP_MATCH;
}

/*!
 * \brief Callback function used to unlink and remove event state compositors that have no AORs feeding them
 * \note Run by management_serializer
 */
static int sip_options_unused_endpoint_state_compositor(void *obj, void *arg, int flags)
{
	struct sip_options_endpoint_state_compositor *endpoint_state_compositor = obj;

	if (ao2_container_count(endpoint_state_compositor->aor_statuses)) {
		return 0;
	}

	/* No AORs are feeding this endpoint state compositor */
	ast_sip_persistent_endpoint_update_state(endpoint_state_compositor->name,
		AST_ENDPOINT_OFFLINE);

	return CMP_MATCH;
}

/*! \brief Structure which contains information required to synchronize */
struct sip_options_synchronize_task_data {
	/*! \brief Whether this is a reload or not */
	int reload;
};

/*!
 * \brief Task to synchronize our local container of AORs and endpoint state compositors with the current configuration
 * \note Run by management_serializer
 */
static int sip_options_synchronize_task(void *obj)
{
	struct sip_options_synchronize_task_data *task_data = obj;
	struct ao2_container *existing = NULL;
	struct ao2_container *objects;

	/*
	 * When reloading we keep track of the existing AORs so we can
	 * terminate old ones that are no longer referenced or used.
	 */
	if (task_data->reload) {
		existing = ao2_container_clone(sip_options_aors, 0);
		if (!existing) {
			return 0;
		}
	}

	objects = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "aor",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (objects) {
		/* Go through the returned AORs and synchronize with our local state */
		ao2_callback(objects, OBJ_NODATA, sip_options_synchronize_aor, existing);
		ao2_ref(objects, -1);
	}

	/*
	 * Any AORs remaining in existing are no longer referenced by
	 * the current container of AORs we retrieved, so remove them.
	 */
	if (existing) {
		ao2_callback(existing, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK,
			sip_options_unused_aor, NULL);
		ao2_ref(existing, -1);
	}

	objects = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "endpoint",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (objects) {
		/* Go through the provided endpoints and update AORs */
		ao2_callback(objects, OBJ_NODATA, sip_options_synchronize_endpoint, NULL);
		ao2_ref(objects, -1);
	}

	/*
	 * All endpoint state compositors that don't have any AORs
	 * feeding them information can be removed.  If they end
	 * up getting needed later they'll just be recreated.
	 */
	ao2_callback(sip_options_endpoint_state_compositors,
		OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK,
		sip_options_unused_endpoint_state_compositor, NULL);

	return 0;
}

/*! \brief Synchronize our local container of AORs and endpoint state compositors with the current configuration */
static void sip_options_synchronize(int reload)
{
	struct sip_options_synchronize_task_data task_data = {
		.reload = reload,
	};

	ast_sip_push_task_wait_serializer(management_serializer, sip_options_synchronize_task,
		&task_data);
}

/*!
 * \brief Unlink AORs feeding the endpoint status compositor
 * \note Run by management_serializer
 */
static void sip_options_endpoint_unlink_aor_feeders(struct ast_sip_endpoint *endpoint,
	struct sip_options_endpoint_state_compositor *endpoint_state_compositor)
{
	struct ao2_iterator it_aor_statuses;
	struct sip_options_endpoint_aor_status *aor_status;
	struct sip_options_endpoint_compositor_task_data task_data = {
		.endpoint_state_compositor = endpoint_state_compositor,
	};

	ao2_lock(endpoint_state_compositor);
	endpoint_state_compositor->active = 0;

	/* Unlink AOR feeders pointing to endpoint */
	it_aor_statuses = ao2_iterator_init(endpoint_state_compositor->aor_statuses, 0);
	for (; (aor_status = ao2_iterator_next(&it_aor_statuses)); ao2_ref(aor_status, -1)) {
		task_data.aor_options = ao2_find(sip_options_aors, aor_status->name,
			OBJ_SEARCH_KEY);
		if (!task_data.aor_options) {
			continue;
		}

		ast_debug(3, "Removing endpoint state compositor '%s' from AOR '%s'\n",
			ast_sorcery_object_get_id(endpoint), aor_status->name);
		ao2_unlock(endpoint_state_compositor);
		ast_sip_push_task_wait_serializer(task_data.aor_options->serializer,
			sip_options_endpoint_compositor_remove_task, &task_data);
		ao2_lock(endpoint_state_compositor);
		ao2_ref(task_data.aor_options, -1);
	}
	ao2_iterator_destroy(&it_aor_statuses);

	/*
	 * We do not need to remove the AOR feeder status memory from the
	 * aor_statuses container.  The endpoint_state_compositor is about
	 * to die and do it for us.
	 */

	ao2_unlock(endpoint_state_compositor);
}

/*!
 * \brief Task to delete an endpoint from the known universe
 * \note Run by management_serializer
 */
static int sip_options_endpoint_observer_deleted_task(void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	struct sip_options_endpoint_state_compositor *endpoint_state_compositor;

	endpoint_state_compositor = ao2_find(sip_options_endpoint_state_compositors,
		ast_sorcery_object_get_id(endpoint), OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (!endpoint_state_compositor) {
		return 0;
	}

	ast_debug(3, "Endpoint '%s' has been deleted, removing endpoint state compositor from AORs\n",
		ast_sorcery_object_get_id(endpoint));
	sip_options_endpoint_unlink_aor_feeders(endpoint, endpoint_state_compositor);
	ao2_ref(endpoint_state_compositor, -1);

	return 0;
}

/*! \brief Observer callback invoked on endpoint deletion */
static void endpoint_observer_deleted(const void *obj)
{
	ast_sip_push_task_wait_serializer(management_serializer,
		sip_options_endpoint_observer_deleted_task, (void *) obj);
}

/*!
 * \brief Task to synchronize the endpoint
 * \note Run by management_serializer
 */
static int sip_options_endpoint_observer_modified_task(void *obj)
{
	struct ast_sip_endpoint *endpoint = obj;
	struct sip_options_endpoint_state_compositor *endpoint_state_compositor;

	ast_debug(3, "Endpoint '%s' has been created or modified, updating state\n",
		ast_sorcery_object_get_id(endpoint));

	endpoint_state_compositor = ao2_find(sip_options_endpoint_state_compositors,
		ast_sorcery_object_get_id(endpoint), OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (endpoint_state_compositor) {
		/* Unlink the AORs currently feeding the endpoint. */
		sip_options_endpoint_unlink_aor_feeders(endpoint, endpoint_state_compositor);
		ao2_ref(endpoint_state_compositor, -1);
	}

	/* Connect the AORs that now feed the endpoint. */
	sip_options_synchronize_endpoint(endpoint, NULL, 0);
	return 0;
}

/*! \brief Observer callback invoked on endpoint creation or modification */
static void endpoint_observer_modified(const void *obj)
{
	ast_sip_push_task_wait_serializer(management_serializer,
		sip_options_endpoint_observer_modified_task, (void *)obj);
}

/*! \brief Observer callbacks for endpoints */
static const struct ast_sorcery_observer endpoint_observer_callbacks = {
	.created = endpoint_observer_modified,
	.updated = endpoint_observer_modified,
	.deleted = endpoint_observer_deleted,
};

/*!
 * \brief Task to synchronize an AOR with our local state
 * \note Run by aor_options->serializer
 */
static int sip_options_update_aor_task(void *obj)
{
	struct sip_options_synchronize_aor_task_data *task_data = obj;
	int available = task_data->aor_options->available;

	ast_debug(3, "Individually updating AOR '%s' with current state of configuration and world\n",
		task_data->aor_options->name);

	sip_options_apply_aor_configuration(task_data->aor_options, task_data->aor,
		task_data->added);

	if (!available && task_data->aor_options->available) {
		ast_debug(3, "After modifying AOR '%s' it has now become available\n",
			task_data->aor_options->name);
		sip_options_notify_endpoint_state_compositors(task_data->aor_options, AVAILABLE);
	} else if (available && !task_data->aor_options->available) {
		ast_debug(3, "After modifying AOR '%s' it has become unavailable\n",
			task_data->aor_options->name);
		sip_options_notify_endpoint_state_compositors(task_data->aor_options, UNAVAILABLE);
	}

	return 0;
}

/*!
 * \brief Task to synchronize the AOR
 * \note Run by management_serializer
 */
static int sip_options_aor_observer_modified_task(void *obj)
{
	struct ast_sip_aor *aor = obj;
	struct sip_options_aor *aor_options;

	aor_options = ao2_find(sip_options_aors, ast_sorcery_object_get_id(aor),
		OBJ_SEARCH_KEY);
	if (!aor_options) {
		struct ao2_container *endpoints;

		aor_options = sip_options_aor_alloc(aor);
		if (!aor_options) {
			return 0;
		}

		/*
		 * This is a newly added AOR and we need to establish any
		 * endpoint state compositors that may reference only the
		 * AOR.  If these need to be updated later then they'll
		 * be done by modifying the endpoint or issuing a reload.
		 */
		sip_options_apply_aor_configuration(aor_options, aor, 1);
		ao2_link(sip_options_aors, aor_options);

		/*
		 * Using LIKE doesn't seem to work very well with non-realtime so we
		 * fetch everything right now and do a filter on our side.
		 */
		endpoints = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(),
			"endpoint", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
		if (endpoints) {
			ao2_callback(endpoints, OBJ_NODATA, sip_options_synchronize_endpoint, aor);
			ao2_ref(endpoints, -1);
		}
	} else {
		struct sip_options_synchronize_aor_task_data task_data = {
			.aor_options = aor_options,
			.aor = aor,
		};

		/*
		 * If this AOR was modified we have to do our work in its serializer
		 * instead of this thread to ensure that things aren't modified by
		 * multiple threads.
		 */
		ast_sip_push_task_wait_serializer(aor_options->serializer,
			sip_options_update_aor_task, &task_data);
	}

	ao2_ref(aor_options, -1);

	return 0;
}

/*! \brief Observer callback invoked on AOR creation or modification */
static void aor_observer_modified(const void *obj)
{
	ast_sip_push_task_wait_serializer(management_serializer,
		sip_options_aor_observer_modified_task, (void *) obj);
}

/*!
 * \brief Task to delete an AOR from the known universe
 * \note Run by management_serializer
 */
static int sip_options_aor_observer_deleted_task(void *obj)
{
	struct ast_sip_aor *aor = obj;
	struct sip_options_aor *aor_options;

	aor_options = ao2_find(sip_options_aors, ast_sorcery_object_get_id(aor),
		OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (!aor_options) {
		return 0;
	}

	ast_debug(3, "AOR '%s' has been deleted, removing it\n", aor_options->name);

	ast_sip_push_task_wait_serializer(aor_options->serializer, sip_options_aor_remove_task,
		aor_options);
	ao2_ref(aor_options, -1);

	return 0;
}

/*! \brief Observer callback invoked on AOR deletion */
static void aor_observer_deleted(const void *obj)
{
	ast_sip_push_task_wait_serializer(management_serializer,
		sip_options_aor_observer_deleted_task, (void *) obj);
}

/*! \brief Observer callbacks for AORs */
static const struct ast_sorcery_observer aor_observer_callbacks = {
	.created = aor_observer_modified,
	.updated = aor_observer_modified,
	.deleted = aor_observer_deleted,
};

/*! \brief Task details for adding an AOR to an endpoint state compositor */
struct sip_options_contact_observer_task_data {
	/*! \brief The AOR options that the contact is referring to */
	struct sip_options_aor *aor_options;
	/*! \brief The contact itself */
	struct ast_sip_contact *contact;
};


/*!
 * \brief Check if the contact qualify options are different than local aor qualify options
 */
static int has_qualify_changed (struct ast_sip_contact *contact, struct sip_options_aor *aor_options)
{
	if (!contact) {
	    return 0;
	}

	if (!aor_options) {
		if (contact->qualify_frequency) {
			return 1;
		}
	} else if (contact->qualify_frequency != aor_options->qualify_frequency
		|| contact->authenticate_qualify != aor_options->authenticate_qualify
		|| ((int)(contact->qualify_timeout * 1000)) != ((int)(aor_options->qualify_timeout * 1000))) {
		return 1;
	}

	return 0;
}

/*!
 * \brief Task which adds a dynamic contact to an AOR
 * \note Run by aor_options->serializer
 */
static int sip_options_contact_add_task(void *obj)
{
	struct sip_options_contact_observer_task_data *task_data = obj;
	struct ast_sip_contact_status *contact_status;

	ao2_link(task_data->aor_options->dynamic_contacts, task_data->contact);
	ao2_link(task_data->aor_options->contacts, task_data->contact);

	contact_status = ast_res_pjsip_find_or_create_contact_status(task_data->contact);
	if (contact_status) {
		if (!task_data->aor_options->qualify_frequency
			&& contact_status->status == CREATED) {
			sip_options_set_contact_status(contact_status, UNKNOWN);
		}
		ao2_ref(contact_status, -1);
	}

	if (task_data->aor_options->qualify_frequency) {
		/* If this is the first contact we need to schedule up qualification */
		if (ao2_container_count(task_data->aor_options->contacts) == 1) {
			ast_debug(3, "Starting scheduled callback on AOR '%s' for qualifying as there is now a contact on it\n",
				task_data->aor_options->name);
			/*
			 * We immediately schedule the initial qualify so that we get
			 * reachable/unreachable as soon as possible.  Realistically
			 * since they pretty much just registered they should be
			 * reachable.
			 */
			if (task_data->aor_options->sched_task) {
				ast_sip_sched_task_cancel(task_data->aor_options->sched_task);
				ao2_ref(task_data->aor_options->sched_task, -1);
				task_data->aor_options->sched_task = NULL;
			}
			task_data->aor_options->sched_task = ast_sip_schedule_task(
				task_data->aor_options->serializer, 1, sip_options_qualify_aor,
				ast_taskprocessor_name(task_data->aor_options->serializer),
				task_data->aor_options,
				AST_SIP_SCHED_TASK_VARIABLE | AST_SIP_SCHED_TASK_DATA_AO2);
			if (!task_data->aor_options->sched_task) {
				ast_log(LOG_ERROR, "Unable to schedule qualify for contacts of AOR '%s'\n",
					task_data->aor_options->name);
			}
		}
	} else {
		/*
		 * If this was the first contact added to a non-qualified AOR then
		 * it should become available.
		 */
		task_data->aor_options->available =
			ao2_container_count(task_data->aor_options->contacts);
		if (task_data->aor_options->available == 1) {
			ast_debug(3, "An unqualified contact has been added to AOR '%s' so it is now available\n",
				task_data->aor_options->name);
			sip_options_notify_endpoint_state_compositors(task_data->aor_options,
				AVAILABLE);
		}
	}

	return 0;
}

/*!
 * \brief Task to add a dynamic contact to an AOR in its serializer
 * \note Run by management_serializer
 */
static int sip_options_contact_add_management_task(void *obj)
{
	struct sip_options_contact_observer_task_data task_data;

	task_data.contact = obj;
	task_data.aor_options = ao2_find(sip_options_aors, task_data.contact->aor,
		OBJ_SEARCH_KEY);

	if (has_qualify_changed(task_data.contact, task_data.aor_options)) {
		struct ast_sip_aor *aor;

		aor = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "aor",
			task_data.contact->aor);
		if (aor) {
			ast_debug(3, "AOR '%s' qualify options have been modified. Synchronize an AOR local state\n",
				task_data.contact->aor);
			sip_options_aor_observer_modified_task(aor);
			ao2_ref(aor, -1);
		}
	}

	if (!task_data.aor_options) {
		return 0;
	}

	ast_sip_push_task_wait_serializer(task_data.aor_options->serializer,
		sip_options_contact_add_task, &task_data);
	ao2_ref(task_data.aor_options, -1);

	return 0;
}

/*! \brief Observer callback invoked on contact creation */
static void contact_observer_created(const void *obj)
{
	ast_sip_push_task_wait_serializer(management_serializer,
		sip_options_contact_add_management_task, (void *) obj);
}

/*!
 * \brief Task which updates a dynamic contact to an AOR
 * \note Run by aor_options->serializer
 */
static int sip_options_contact_update_task(void *obj)
{
	struct sip_options_contact_observer_task_data *task_data = obj;
	struct ast_sip_contact_status *contact_status;

	contact_status = ast_sip_get_contact_status(task_data->contact);
	if (contact_status) {
		switch (contact_status->status) {
		case CREATED:
			sip_options_set_contact_status(contact_status, UNKNOWN);
			break;
		case UNAVAILABLE:
		case AVAILABLE:
		case UNKNOWN:
			/* Refresh the ContactStatus AMI events. */
			sip_options_contact_status_update(contact_status);
			break;
		case REMOVED:
			break;
		}
		ao2_ref(contact_status, -1);
	}

	ao2_ref(task_data->contact, -1);
	ao2_ref(task_data->aor_options, -1);
	ast_free(task_data);
	return 0;
}

/*! \brief Observer callback invoked on contact update */
static void contact_observer_updated(const void *obj)
{
	struct sip_options_contact_observer_task_data *task_data;

	task_data = ast_malloc(sizeof(*task_data));
	if (!task_data) {
		return;
	}

	task_data->contact = (struct ast_sip_contact *) obj;
	task_data->aor_options = ao2_find(sip_options_aors, task_data->contact->aor,
		OBJ_SEARCH_KEY);

	if (has_qualify_changed(task_data->contact, task_data->aor_options)) {
		struct ast_sip_aor *aor;

		aor = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "aor",
			task_data->contact->aor);
		if (aor) {
			ast_debug(3, "AOR '%s' qualify options have been modified. Synchronize an AOR local state\n",
				task_data->contact->aor);
			ast_sip_push_task_wait_serializer(management_serializer,
				sip_options_aor_observer_modified_task, aor);
			ao2_ref(aor, -1);
		}
	}

	if (!task_data->aor_options) {
		ast_free(task_data);
		return;
	}

	ao2_ref(task_data->contact, +1);
	if (ast_sip_push_task(task_data->aor_options->serializer,
		sip_options_contact_update_task, task_data)) {
		ao2_ref(task_data->contact, -1);
		ao2_ref(task_data->aor_options, -1);
		ast_free(task_data);
	}
}

/*!
 * \brief Task which deletes a dynamic contact from an AOR
 * \note Run by aor_options->serializer
 */
static int sip_options_contact_delete_task(void *obj)
{
	struct sip_options_contact_observer_task_data *task_data = obj;

	ao2_find(task_data->aor_options->dynamic_contacts, task_data->contact,
		OBJ_NODATA | OBJ_UNLINK | OBJ_SEARCH_OBJECT);
	ao2_find(task_data->aor_options->contacts, task_data->contact,
		OBJ_NODATA | OBJ_UNLINK | OBJ_SEARCH_OBJECT);

	sip_options_remove_contact_status(task_data->aor_options, task_data->contact);

	if (task_data->aor_options->qualify_frequency) {
		/* If this is the last contact then we need to stop the scheduled callback */
		if (!ao2_container_count(task_data->aor_options->contacts)) {
			ast_debug(3, "Terminating scheduled callback on AOR '%s' as there are no contacts to qualify\n",
				task_data->aor_options->name);
			if (task_data->aor_options->sched_task) {
				ast_sip_sched_task_cancel(task_data->aor_options->sched_task);
				ao2_ref(task_data->aor_options->sched_task, -1);
				task_data->aor_options->sched_task = NULL;
			}
		}
	} else {
		task_data->aor_options->available =
			ao2_container_count(task_data->aor_options->contacts);
		if (!task_data->aor_options->available) {
			ast_debug(3, "An unqualified contact has been removed from AOR '%s' leaving no remaining contacts\n",
				task_data->aor_options->name);
			sip_options_notify_endpoint_state_compositors(task_data->aor_options,
				UNAVAILABLE);
		}
	}

	return 0;
}

/*!
 * \brief Task to delete a contact from an AOR in its serializer
 * \note Run by management_serializer
 */
static int sip_options_contact_delete_management_task(void *obj)
{
	struct sip_options_contact_observer_task_data task_data;

	task_data.contact = obj;
	task_data.aor_options = ao2_find(sip_options_aors, task_data.contact->aor,
		OBJ_SEARCH_KEY);
	if (!task_data.aor_options) {
		/* For contacts that are deleted we don't really care if there is no AOR locally */
		return 0;
	}

	ast_sip_push_task_wait_serializer(task_data.aor_options->serializer,
		sip_options_contact_delete_task, &task_data);
	ao2_ref(task_data.aor_options, -1);

	return 0;
}

/*! \brief Observer callback invoked on contact deletion */
static void contact_observer_deleted(const void *obj)
{
	ast_sip_push_task_wait_serializer(management_serializer,
		sip_options_contact_delete_management_task, (void *) obj);
}

/*! \brief Observer callbacks for contacts */
static const struct ast_sorcery_observer contact_observer_callbacks = {
	.created = contact_observer_created,
	.updated = contact_observer_updated,
	.deleted = contact_observer_deleted,
};

static char *cli_qualify(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	const char *endpoint_name;
	char *aors;
	char *aor_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip qualify";
		e->usage =
			"Usage: pjsip qualify <endpoint>\n"
			"       Send a SIP OPTIONS request to all contacts on the endpoint.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	endpoint_name = a->argv[2];

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
		endpoint_name);
	if (!endpoint) {
		ast_cli(a->fd, "Unable to retrieve endpoint %s\n", endpoint_name);
		return CLI_FAILURE;
	}

	if (ast_strlen_zero(endpoint->aors)) {
		ast_cli(a->fd, "No AORs configured for endpoint '%s'\n", endpoint_name);
		return CLI_FAILURE;
	}

	aors = ast_strdupa(endpoint->aors);
	while ((aor_name = ast_strip(strsep(&aors, ",")))) {
		struct sip_options_aor *aor_options;

		aor_options = ao2_find(sip_options_aors, aor_name, OBJ_SEARCH_KEY);
		if (!aor_options) {
			continue;
		}

		ast_cli(a->fd, "Qualifying AOR '%s' on endpoint '%s'\n", aor_name, endpoint_name);
		ast_sip_push_task_wait_serializer(aor_options->serializer, sip_options_qualify_aor,
			aor_options);
		ao2_ref(aor_options, -1);
	}

	return CLI_SUCCESS;
}

static char *cli_show_qualify_endpoint(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	const char *endpoint_name;
	char *aors;
	char *aor_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show qualify endpoint";
		e->usage =
			"Usage: pjsip show qualify endpoint <id>\n"
			"       Show the current qualify options for all Aors on the PJSIP endpoint.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	endpoint_name = a->argv[4];

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
		endpoint_name);
	if (!endpoint) {
		ast_cli(a->fd, "Unable to retrieve endpoint %s\n", endpoint_name);
		return CLI_FAILURE;
	}

	if (ast_strlen_zero(endpoint->aors)) {
		ast_cli(a->fd, "No AORs configured for endpoint '%s'\n", endpoint_name);
		return CLI_FAILURE;
	}

	aors = ast_strdupa(endpoint->aors);
	while ((aor_name = ast_strip(strsep(&aors, ",")))) {
		struct sip_options_aor *aor_options;

		aor_options = ao2_find(sip_options_aors, aor_name, OBJ_SEARCH_KEY);
		if (!aor_options) {
			continue;
		}

		ast_cli(a->fd, " * AOR '%s' on endpoint '%s'\n", aor_name, endpoint_name);
		ast_cli(a->fd, "  Qualify frequency    : %d sec\n", aor_options->qualify_frequency);
		ast_cli(a->fd, "  Qualify timeout      : %d ms\n", (int)(aor_options->qualify_timeout / 1000));
		ast_cli(a->fd, "  Authenticate qualify : %s\n", aor_options->authenticate_qualify?"yes":"no");
		ast_cli(a->fd, "\n");
		ao2_ref(aor_options, -1);
	}

	return CLI_SUCCESS;
}

static char *cli_show_qualify_aor(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sip_options_aor *aor_options;
	const char *aor_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show qualify aor";
		e->usage =
			"Usage: pjsip show qualify aor <id>\n"
			"       Show the PJSIP Aor current qualify options.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	aor_name = a->argv[4];

	aor_options = ao2_find(sip_options_aors, aor_name, OBJ_SEARCH_KEY);
	if (!aor_options) {
		ast_cli(a->fd, "Unable to retrieve aor '%s' qualify options\n", aor_name);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, " * AOR '%s'\n", aor_name);
	ast_cli(a->fd, "  Qualify frequency    : %d sec\n", aor_options->qualify_frequency);
	ast_cli(a->fd, "  Qualify timeout      : %d ms\n", (int)(aor_options->qualify_timeout / 1000));
	ast_cli(a->fd, "  Authenticate qualify : %s\n", aor_options->authenticate_qualify?"yes":"no");
	ao2_ref(aor_options, -1);

	return CLI_SUCCESS;
}

static char *cli_reload_qualify_endpoint(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	const char *endpoint_name;
	char *aors;
	char *aor_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip reload qualify endpoint";
		e->usage =
			"Usage: pjsip reload qualify endpoint <id>\n"
			"       Synchronize the qualify options for all Aors on the PJSIP endpoint.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	endpoint_name = a->argv[4];

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
		endpoint_name);
	if (!endpoint) {
		ast_cli(a->fd, "Unable to retrieve endpoint %s\n", endpoint_name);
		return CLI_FAILURE;
	}

	if (ast_strlen_zero(endpoint->aors)) {
		ast_cli(a->fd, "No AORs configured for endpoint '%s'\n", endpoint_name);
		return CLI_FAILURE;
	}

	aors = ast_strdupa(endpoint->aors);
	while ((aor_name = ast_strip(strsep(&aors, ",")))) {
		struct ast_sip_aor *aor;

		aor = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "aor", aor_name);
		if (!aor) {
			continue;
		}

		ast_cli(a->fd, "Synchronizing AOR '%s' on endpoint '%s'\n", aor_name, endpoint_name);
		ast_sip_push_task_wait_serializer(management_serializer,
			sip_options_aor_observer_modified_task, aor);
		ao2_ref(aor, -1);
	}

	return CLI_SUCCESS;
}

static char *cli_reload_qualify_aor(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_sip_aor *aor;
	const char *aor_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip reload qualify aor";
		e->usage =
			"Usage: pjsip reload qualify aor <id>\n"
			"       Synchronize the PJSIP Aor qualify options.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	aor_name = a->argv[4];

	aor = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "aor", aor_name);
	if (!aor) {
		ast_cli(a->fd, "Unable to retrieve aor '%s'\n", aor_name);
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Synchronizing AOR '%s'\n", aor_name);
	ast_sip_push_task_wait_serializer(management_serializer,
		sip_options_aor_observer_modified_task, aor);
	ao2_ref(aor, -1);

	return CLI_SUCCESS;
}

static int ami_sip_qualify(struct mansession *s, const struct message *m)
{
	const char *endpoint_name = astman_get_header(m, "Endpoint");
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	char *aors;
	char *aor_name;

	if (ast_strlen_zero(endpoint_name)) {
		astman_send_error(s, m, "Endpoint parameter missing.");
		return 0;
	}

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
		endpoint_name);
	if (!endpoint) {
		astman_send_error(s, m, "Unable to retrieve endpoint\n");
		return 0;
	}

	/* send a qualify for all contacts registered with the endpoint */
	if (ast_strlen_zero(endpoint->aors)) {
		astman_send_error(s, m, "No AoRs configured for endpoint\n");
		return 0;
	}

	aors = ast_strdupa(endpoint->aors);
	while ((aor_name = ast_strip(strsep(&aors, ",")))) {
		struct sip_options_aor *aor_options;

		aor_options = ao2_find(sip_options_aors, aor_name, OBJ_SEARCH_KEY);
		if (!aor_options) {
			continue;
		}

		ast_sip_push_task_wait_serializer(aor_options->serializer, sip_options_qualify_aor,
			aor_options);
		ao2_ref(aor_options, -1);
	}

	astman_send_ack(s, m, "Endpoint found, will qualify");
	return 0;
}

static struct ast_cli_entry cli_options[] = {
	AST_CLI_DEFINE(cli_qualify, "Send an OPTIONS request to a PJSIP endpoint"),
	AST_CLI_DEFINE(cli_show_qualify_endpoint, "Show the current qualify options for all Aors on the PJSIP endpoint"),
	AST_CLI_DEFINE(cli_show_qualify_aor, "Show the PJSIP Aor current qualify options"),
	AST_CLI_DEFINE(cli_reload_qualify_endpoint, "Synchronize the qualify options for all Aors on the PJSIP endpoint"),
	AST_CLI_DEFINE(cli_reload_qualify_aor, "Synchronize the PJSIP Aor qualify options"),
};


int ast_sip_format_contact_ami(void *obj, void *arg, int flags)
{
	struct ast_sip_contact_wrapper *wrapper = obj;
	struct ast_sip_contact *contact = wrapper->contact;
	struct ast_sip_ami *ami = arg;
	struct ast_sip_contact_status *status;
	struct ast_str *buf;
	const struct ast_sip_endpoint *endpoint = ami->arg;

	buf = ast_sip_create_ami_event("ContactStatusDetail", ami);
	if (!buf) {
		return -1;
	}

	status = ast_sip_get_contact_status(contact);

	ast_str_append(&buf, 0, "AOR: %s\r\n", wrapper->aor_id);
	ast_str_append(&buf, 0, "URI: %s\r\n", contact->uri);
	ast_str_append(&buf, 0, "UserAgent: %s\r\n", contact->user_agent);
	ast_str_append(&buf, 0, "RegExpire: %ld\r\n", contact->expiration_time.tv_sec);
	if (!ast_strlen_zero(contact->via_addr)) {
		ast_str_append(&buf, 0, "ViaAddress: %s", contact->via_addr);
		if (contact->via_port) {
			ast_str_append(&buf, 0, ":%d", contact->via_port);
		}
		ast_str_append(&buf, 0, "\r\n");
	}
	if (!ast_strlen_zero(contact->call_id)) {
		ast_str_append(&buf, 0, "CallID: %s\r\n", contact->call_id);
	}
	ast_str_append(&buf, 0, "Status: %s\r\n",
		ast_sip_get_contact_status_label(status ? status->status : UNKNOWN));
	if (!status || status->status == UNKNOWN) {
		ast_str_append(&buf, 0, "RoundtripUsec: N/A\r\n");
	} else {
		ast_str_append(&buf, 0, "RoundtripUsec: %" PRId64 "\r\n", status->rtt);
	}
	ast_str_append(&buf, 0, "EndpointName: %s\r\n",
		endpoint ? ast_sorcery_object_get_id(endpoint) : S_OR(contact->endpoint_name, ""));

	ast_str_append(&buf, 0, "ID: %s\r\n", ast_sorcery_object_get_id(contact));
	ast_str_append(&buf, 0, "AuthenticateQualify: %d\r\n", contact->authenticate_qualify);
	ast_str_append(&buf, 0, "OutboundProxy: %s\r\n", contact->outbound_proxy);
	ast_str_append(&buf, 0, "Path: %s\r\n", contact->path);
	ast_str_append(&buf, 0, "QualifyFrequency: %u\r\n", contact->qualify_frequency);
	ast_str_append(&buf, 0, "QualifyTimeout: %.3f\r\n", contact->qualify_timeout);

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ami->count++;

	ast_free(buf);
	ao2_cleanup(status);
	return 0;
}

static int format_contact_status_for_aor(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;

	return ast_sip_for_each_contact(aor, ast_sip_format_contact_ami, arg);
}

static int format_ami_contact_status(const struct ast_sip_endpoint *endpoint,
		struct ast_sip_ami *ami)
{
	ami->arg = (void *)endpoint;
	return ast_sip_for_each_aor(endpoint->aors, format_contact_status_for_aor, ami);
}

static struct ast_sip_endpoint_formatter contact_status_formatter = {
	.format_ami = format_ami_contact_status
};

/*!
 * \brief Management task to clean up an AOR
 * \note Run by aor_options->serializer
 */
static int sip_options_cleanup_aor_task(void *obj)
{
	struct sip_options_aor *aor_options = obj;

	ast_debug(2, "Cleaning up AOR '%s' for shutdown\n", aor_options->name);

	aor_options->qualify_frequency = 0;
	if (aor_options->sched_task) {
		ast_sip_sched_task_cancel(aor_options->sched_task);
		ao2_ref(aor_options->sched_task, -1);
		aor_options->sched_task = NULL;
	}
	AST_VECTOR_RESET(&aor_options->compositors, ao2_cleanup);

	return 0;
}

/*!
 * \brief Management task to clean up the environment
 * \note Run by management_serializer
 */
static int sip_options_cleanup_task(void *obj)
{
	struct ao2_iterator it_aor;
	struct sip_options_aor *aor_options;

	if (!sip_options_aors) {
		/* Nothing to do */
		return 0;
	}

	it_aor = ao2_iterator_init(sip_options_aors, AO2_ITERATOR_UNLINK);
	for (; (aor_options = ao2_iterator_next(&it_aor)); ao2_ref(aor_options, -1)) {
		ast_sip_push_task_wait_serializer(aor_options->serializer,
			sip_options_cleanup_aor_task, aor_options);
	}
	ao2_iterator_destroy(&it_aor);

	return 0;
}

void ast_res_pjsip_cleanup_options_handling(void)
{
	int remaining;
	struct ast_taskprocessor *mgmt_serializer;

	ast_cli_unregister_multiple(cli_options, ARRAY_LEN(cli_options));
	ast_manager_unregister("PJSIPQualify");
	internal_sip_unregister_endpoint_formatter(&contact_status_formatter);

	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "contact",
		&contact_observer_callbacks);
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "aor",
		&aor_observer_callbacks);
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "endpoint",
		&endpoint_observer_callbacks);

	mgmt_serializer = management_serializer;
	management_serializer = NULL;
	if (mgmt_serializer) {
		ast_sip_push_task_wait_serializer(mgmt_serializer, sip_options_cleanup_task, NULL);
	}

	remaining = ast_serializer_shutdown_group_join(shutdown_group,
		MAX_UNLOAD_TIMEOUT_TIME);
	if (remaining) {
		ast_log(LOG_WARNING, "Cleanup incomplete. Could not stop %d AORs.\n",
			remaining);
	}
	ao2_cleanup(shutdown_group);
	shutdown_group = NULL;

	if (mgmt_serializer) {
		ast_taskprocessor_unreference(mgmt_serializer);
	}

	ao2_cleanup(sip_options_aors);
	sip_options_aors = NULL;
	ao2_cleanup(sip_options_contact_statuses);
	sip_options_contact_statuses = NULL;
	ao2_cleanup(sip_options_endpoint_state_compositors);
	sip_options_endpoint_state_compositors = NULL;

	pjsip_endpt_unregister_module(ast_sip_get_pjsip_endpoint(), &options_module);
}

/*!
 * \brief Management task to finish setting up the environment.
 * \note Run by management_serializer
 */
static int sip_options_init_task(void *mgmt_serializer)
{
	management_serializer = mgmt_serializer;

	shutdown_group = ast_serializer_shutdown_group_alloc();
	if (!shutdown_group) {
		return -1;
	}

	if (ast_sorcery_observer_add(ast_sip_get_sorcery(), "endpoint",
		&endpoint_observer_callbacks)) {
		return -1;
	}
	if (ast_sorcery_observer_add(ast_sip_get_sorcery(), "aor",
		&aor_observer_callbacks)) {
		return -1;
	}
	if (ast_sorcery_observer_add(ast_sip_get_sorcery(), "contact",
		&contact_observer_callbacks)) {
		return -1;
	}

	sip_options_synchronize(0);

	return 0;
}

int ast_res_pjsip_preinit_options_handling(void)
{
	sip_options_contact_statuses = sip_options_contact_statuses_alloc();
	return sip_options_contact_statuses ? 0 : -1;
}

int ast_res_pjsip_init_options_handling(int reload)
{
	struct ast_taskprocessor *mgmt_serializer;

	static const pj_str_t STR_OPTIONS = { "OPTIONS", 7 };

	if (reload) {
		sip_options_synchronize(1);
		return 0;
	}

	if (pjsip_endpt_register_module(ast_sip_get_pjsip_endpoint(), &options_module)
		!= PJ_SUCCESS) {
		return -1;
	}

	if (pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(), NULL, PJSIP_H_ALLOW,
		NULL, 1, &STR_OPTIONS) != PJ_SUCCESS) {
		ast_res_pjsip_cleanup_options_handling();
		return -1;
	}

	sip_options_aors = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0, AOR_BUCKETS,
		sip_options_aor_hash_fn, NULL, sip_options_aor_cmp_fn);
	if (!sip_options_aors) {
		ast_res_pjsip_cleanup_options_handling();
		return -1;
	}
	sip_options_endpoint_state_compositors =
		ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK, 0,
			ENDPOINT_STATE_COMPOSITOR_BUCKETS,
			sip_options_endpoint_state_compositor_hash_fn, NULL,
			sip_options_endpoint_state_compositor_cmp_fn);
	if (!sip_options_endpoint_state_compositors) {
		ast_res_pjsip_cleanup_options_handling();
		return -1;
	}

	mgmt_serializer = ast_sip_create_serializer_named("pjsip/options/manage");
	if (!mgmt_serializer) {
		ast_res_pjsip_cleanup_options_handling();
		return -1;
	}

	/*
	 * Set the water mark levels high because we can get a flood of
	 * contact status updates from sip_options_synchronize() that
	 * quickly clears on initial load or reload.
	 */
	ast_taskprocessor_alert_set_levels(mgmt_serializer, -1,
		10 * AST_TASKPROCESSOR_HIGH_WATER_LEVEL);

	/*
	 * We make sure that the environment is completely setup before we allow
	 * any other threads to post contact_status updates to the
	 * management_serializer.
	 */
	if (ast_sip_push_task_wait_serializer(mgmt_serializer, sip_options_init_task,
		mgmt_serializer)) {
		/* Set management_serializer in case pushing the task actually failed. */
		management_serializer = mgmt_serializer;
		ast_res_pjsip_cleanup_options_handling();
		return -1;
	}

	internal_sip_register_endpoint_formatter(&contact_status_formatter);
	ast_manager_register2("PJSIPQualify", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING,
		ami_sip_qualify, NULL, NULL, NULL);
	ast_cli_register_multiple(cli_options, ARRAY_LEN(cli_options));

	return 0;
}
