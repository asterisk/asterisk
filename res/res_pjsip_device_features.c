/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, 2026, Naveen Albert
 *
 * Naveen Albert <asterisk@phreaknet.org>
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
 * \brief Broadworks Device Feature Key Synchronization (Do Not Disturb and Call Forwarding)
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_pubsub</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_outbound_publish.h"
#include "asterisk/res_pjsip_pubsub.h"
#include "asterisk/res_pjsip_body_generator_types.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/sorcery.h"
#include "asterisk/xml.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/app.h"
#include "asterisk/cli.h"
#include "asterisk/sorcery.h"

#define FEATURE_SYNC_MIME_TYPE "application/x-as-feature-event+xml"
#define FEATURE_SYNC_EVENT "as-feature-event"

/*** DOCUMENTATION
	<function name="PJSIP_DEVICE_FEATURES" language="en_US">
		<synopsis>
			Set or get a device feature of a PJSIP endpoint.
		</synopsis>
		<syntax>
			<parameter name="endpoint" required="true">
				<para>Name of PJSIP endpoint, without the technology.</para>
			</parameter>
			<parameter name="feature" required="true">
				<para>Name of the feature.</para>
				<para>Must be one of:</para>
				<enumlist>
					<enum name="do_not_disturb">
						<para>When reading, returns 1 if Do Not Disturb is enabled and 0 if it is disabled.</para>
						<para>If this setting has never been set, an empty value is returned.</para>
					</enum>
					<enum name="call_forwarding_always"/>
					<enum name="call_forwarding_busy"/>
					<enum name="call_forwarding_noanswer"/>
					<enum name="ring_count">
						<para>This applies only to reading the function.</para>
						<para>To set the ring count, pass it as the second data value with call_forwarding_noanswer.</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Can be used to provide device feature key synchronization with supporting IP phones.</para>
			<para>This module does not actually provide any features. It is used to synchronize the indication
			of features on an IP phone when the server itself provides features whose indications are synchronized
			by this module.</para>
			<para>For example:</para>
			<example title="Enable Do Not Disturb indication for endpoint Phone1">
			same => n,Set(PJSIP_DEVICE_FEATURES(Phone1,do_not_disturb)=enabled)
			</example>
			<example title="Set Call Forwarding No Answer to 5551212 after 5 rings">
			same => n,Set(PJSIP_DEVICE_FEATURES(Phone1,call_forwarding_noanswer)=5551212,5)
			</example>
			<example title="Disable Call Forwarding Busy">
			same => n,Set(PJSIP_DEVICE_FEATURES(Phone1,call_forwarding_busy)=)
			</example>
			<example title="Retrieve current Do Not Disturb status">
			same => n,NoOp(${PJSIP_DEVICE_FEATURES(Phone1,do_not_disturb)})
			</example>
			<example title="Retrieve current number of rings">
			same => n,NoOp(${PJSIP_DEVICE_FEATURES(Phone1,ring_count)})
			</example>
		</description>
	</function>
	<configInfo name="res_pjsip_device_features" language="en_US">
		<synopsis>PJSIP device feature key sync</synopsis>
		<configFile name="sorcery.conf">
			<configObject name="device_feature_key_sync">
				<synopsis>Persistent cache of device feature key sync state</synopsis>
				<description>
					<para>Allows the alteration of sorcery backend mapping for the persistent cache of feature key sync data.</para>
				</description>
			</configObject>
		</configFile>
	</configInfo>
***/

/*! \note There are two basic modes of operation for this module.
 *
 * 1) Auto approved updates (default)
 *    In this mode, change requests received from devices are automatically honored and processed, e.g.:
 *    1. Device indicates it would like to enable Do Not Disturb
 *    2. We receive the request, update our internal state to enable DND, and update other AORs that DND is now on
 *       An event is also sent out via AMI to alert of this change. This can be used to update other state elsewhere as well.
 *    3. Reads from PJSIP_DEVICE_FEATURES will indicate DND is now enabled
 *
 * 2) External approval mode
 *    In this mode, change requests received from devices are received and sent out via AMI events.
 *    An external program is responsible for handling the event and using PJSIP_DEVICE_FEATURES to toggle the feature if appropriate. e.g.:
 *    1. Device indicates it would like to enable Do Not Disturb
 *    2. We receive the request and send an event out via AMI. No state is changed at this time.
 *    3. An external program receives the event and determines (through its own business logic) if it should honor the request.
 *       - If no, it does nothing, and no state change occurs. (For example, this subscriber does not pay for Do Not Disturb and is not allowed to enable it.)
 *       - If yes, it calls PJSIP_DEVICE_FEATURES to notify us that it has decided to allow the change (and passes the appropriate info)
 *    4. The external program also simultaneously may decide to update other state elsewhere to keep it in sync.
 *
 * Auto-approval mode is best for simple environments with only PJSIP devices and no need for any synchronization beyond that provided by the module.
 * Note that if the only control require is that certain endpoints should NOT be allowed to make use of device feature key sync,
 * then external approval is not necessarily required; device_feature_key_autoapprove in pjsip.conf can simply be disabled for the endpoint that should not.
 * External approval is only necessary for more advanced scenarios where further logic and processing is needed to gatekeep approvals. */

/*!
 * \brief A subscription for synchronized device feature state
 *
 * This structure acts as the owner for the underlying SIP subscription.
 */
struct feature_state_subscription {
	/*! The SIP subscription */
	struct ast_sip_subscription *sip_sub;
	/*! The serializer to use for notifications */
	struct ast_taskprocessor *serializer;
	/*! Info used by res_pjsip_features_body_generator */
	struct ast_sip_device_feature_sync_data sync_data;
	/* Whether we have data about this feature. */
	unsigned int have_dnd:1;
	unsigned int have_callforwardalways:1;
	unsigned int have_callforwardbusy:1;
	unsigned int have_callforwardnoanswer:1;
};

#define SIP_SORCERY_DEVICE_FEATURE_TYPE "device_feature_key_sync"

#define FEATURE_DATA_PREFIX "device_feature_key_sync"

struct ast_sip_device_feature_object {
	SORCERY_OBJECT(details); /* Sorcery object details, id is the endpoint name */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(fwd_exten_always);
		AST_STRING_FIELD(fwd_exten_busy);
		AST_STRING_FIELD(fwd_exten_noanswer);
	);
	unsigned int ring_count; /* We don't need a separate flag to keep track if this is set, because '0' isn't a valid value for ring count, i.e. means it's unset */
	unsigned int dnd;
	/* These should all be bitfields, but that causes issues for ast_sorcery_object_field_register_nodoc: */
	unsigned int have_dnd;
	unsigned int have_callforwardalways;
	unsigned int have_callforwardbusy;
	unsigned int have_callforwardnoanswer;
};

static struct ast_sorcery *feature_sorcery;

static void feature_sorcery_object_destroy(void *varg)
{
	struct ast_sip_device_feature_object *obj = varg;
	ast_string_field_free_memory(obj);
}

static void *feature_sorcery_object_alloc(const char *id)
{
	struct ast_sip_device_feature_object *obj;

	obj = ast_sorcery_generic_alloc(sizeof(struct ast_sip_device_feature_object), feature_sorcery_object_destroy);
	if (obj) {
		if (ast_string_field_init(obj, 128)) {
			ao2_cleanup(obj);
			obj = NULL;
		}
	}
	return obj;
}

static int device_feature_sorcery_init(void)
{
	int res;

	/* In pjsip_configuration.c, we set the endpoint field 'device_feature_key_autoapprove' per-endpoint.
	 * The only thing we can configure here is the persistent store of feature data, via Sorcery. */

	feature_sorcery = ast_sorcery_open();
	if (!feature_sorcery) {
		ast_log(LOG_ERROR, "Sorcery failed to open\n");
		return -1;
	}

	/* Map the device feature key wizards */
	if (ast_sorcery_apply_default(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "astdb", FEATURE_DATA_PREFIX) == AST_SORCERY_APPLY_FAIL) {
		ast_log(LOG_ERROR, "Sorcery could not set up wizards\n");
		return -1;
	}

	res = ast_sorcery_object_register(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, feature_sorcery_object_alloc, NULL, NULL);
	if (res) {
		ast_log(LOG_ERROR, "Sorcery could not register object type '%s'\n", SIP_SORCERY_DEVICE_FEATURE_TYPE);
		return -1;
	}

	res |= ast_sorcery_object_field_register_nodoc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "call_forwarding_always", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_device_feature_object, fwd_exten_always));
	res |= ast_sorcery_object_field_register_nodoc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "call_forwarding_busy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_device_feature_object, fwd_exten_busy));
	res |= ast_sorcery_object_field_register_nodoc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "call_forwarding_noanswer", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_device_feature_object, fwd_exten_noanswer));
	res |= ast_sorcery_object_field_register_nodoc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "ring_count", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_device_feature_object, ring_count));

	res |= ast_sorcery_object_field_register_nodoc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "do_not_disturb", "0", OPT_BOOL_T, 1, FLDSET(struct ast_sip_device_feature_object, dnd));
	res |= ast_sorcery_object_field_register_nodoc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "have_dnd", "0", OPT_BOOL_T, 1, FLDSET(struct ast_sip_device_feature_object, have_dnd));
	res |= ast_sorcery_object_field_register_nodoc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "have_callforwardalways", "0", OPT_BOOL_T, 1, FLDSET(struct ast_sip_device_feature_object, have_callforwardalways));
	res |= ast_sorcery_object_field_register_nodoc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "have_callforwardbusy", "0", OPT_BOOL_T, 1, FLDSET(struct ast_sip_device_feature_object, have_callforwardbusy));
	res |= ast_sorcery_object_field_register_nodoc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, "have_callforwardnoanswer", "0", OPT_BOOL_T, 1, FLDSET(struct ast_sip_device_feature_object, have_callforwardnoanswer));

	return res ? -1 : 0;
}

static const struct ast_sip_device_feature_object *device_feature_object_get(const char *endpoint)
{
	if (ast_strlen_zero(endpoint)) {
		return NULL;
	}
	return ast_sorcery_retrieve_by_id(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, endpoint);
}

static int restore_cached_settings(const char *endpoint, struct feature_state_subscription *sub)
{
	const struct ast_sip_device_feature_object *obj = device_feature_object_get(endpoint);
	if (!obj) {
		return -1;
	}

	if (obj->have_dnd) {
		sub->sync_data.dnd = obj->dnd;
		sub->have_dnd = 1;
	}

	if (obj->have_callforwardalways) {
		ast_copy_string(sub->sync_data.fwd_exten_always, S_OR(obj->fwd_exten_always, ""), sizeof(sub->sync_data.fwd_exten_always));
		sub->have_callforwardalways = 1;
	}

	if (obj->have_callforwardbusy) {
		ast_copy_string(sub->sync_data.fwd_exten_busy, S_OR(obj->fwd_exten_busy, ""), sizeof(sub->sync_data.fwd_exten_busy));
		sub->have_callforwardbusy = 1;
	}

	if (obj->have_callforwardnoanswer) {
		ast_copy_string(sub->sync_data.fwd_exten_noanswer, S_OR(obj->fwd_exten_noanswer, ""), sizeof(sub->sync_data.fwd_exten_noanswer));
		sub->have_callforwardnoanswer = 1;
	}

	sub->sync_data.ring_count = obj->ring_count;

	ao2_ref((struct ast_sip_device_feature_object *) obj, -1);
	return 0;
}

static int __device_feature_key_update(struct ast_sip_device_feature_object *obj)
{
	const struct ast_sip_device_feature_object *exists;
	int res;

	exists = ast_sorcery_retrieve_by_id(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, ast_sorcery_object_get_id(obj));
	if (exists) {
		res = ast_sorcery_update(feature_sorcery, obj);
	} else {
		res = ast_sorcery_create(feature_sorcery, obj);
	}
	return res;
}

static int device_feature_key_update(const char *endpoint, struct feature_state_subscription *sub)
{
	int res = -1;
	struct ast_sip_device_feature_object *obj = ast_sorcery_alloc(feature_sorcery, SIP_SORCERY_DEVICE_FEATURE_TYPE, endpoint);

	if (obj) {
		struct ast_sip_device_feature_sync_data *sync_data = &sub->sync_data;
		obj->ring_count = sync_data->ring_count;
		obj->dnd = sync_data->dnd;
		if (sub->have_callforwardalways) {
			ast_string_field_set(obj, fwd_exten_always, sync_data->fwd_exten_always);
		}
		if (sub->have_callforwardbusy) {
			ast_string_field_set(obj, fwd_exten_busy, sync_data->fwd_exten_busy);
		}
		if (sub->have_callforwardnoanswer) {
			ast_string_field_set(obj, fwd_exten_noanswer, sync_data->fwd_exten_noanswer);
		}
		obj->have_dnd = sub->have_dnd;
		obj->have_callforwardalways = sub->have_callforwardalways;
		obj->have_callforwardbusy = sub->have_callforwardbusy;
		obj->have_callforwardnoanswer = sub->have_callforwardnoanswer;
		res = __device_feature_key_update(obj);
		if (res) {
			ast_debug(1, "Could not update device feature key store for %s\n", endpoint);
		}
		ao2_ref((struct ast_sip_device_feature_object *) obj, -1);
	}
	return res;
}

static int send_update(struct feature_state_subscription *feature_state_sub);

struct subscription_item {
	const char *endpoint;
	struct feature_state_subscription *sub;
	AST_LIST_ENTRY(subscription_item) entry;
	char data[];
};

static AST_RWLIST_HEAD_STATIC(sublist, subscription_item);

enum forward_type {
	FORWARD_ALWAYS,
	FORWARD_BUSY,
	FORWARD_NOANSWER,
};

static const char *forward_type_str(enum forward_type fwdtype)
{
	switch (fwdtype) {
	case FORWARD_ALWAYS:
		return "Always";
	case FORWARD_BUSY:
		return "Busy";
	case FORWARD_NOANSWER:
		return "No Answer";
	}
	return "";
}

static int send_ami(const char *device_id, const char *endpoint, const char *number, const char *feature, int status, int ring_count, const char *data)
{
	/*** DOCUMENTATION
		<managerEvent language="en_US" name="DeviceFeatureSync">
			<managerEventInstance class="EVENT_FLAG_CALL">
				<synopsis>Raised when a SIP device tries to synchronize a server-side feature</synopsis>
					<syntax>
						<parameter name="DeviceID">
							<para>The unique device ID sent by the endpoint.</para>
							<para>The same device ID can be used in NOTIFY requests, but this is not used within Broadworks.</para>
						</parameter>
						<parameter name="Endpoint">
							<para>The endpoint (device name).</para>
						</parameter>
						<parameter name="CallerIDNum">
							<para>The Caller ID number of the endpoint.</para>
						</parameter>
						<parameter name="Feature">
							<para>Will be one of the following:</para>
							<enumlist>
								<enum name="DoNotDisturb"/>
								<enum name="CallForwardingAlways"/>
								<enum name="CallForwardingBusy"/>
								<enum name="CallForwardingNoAnswer"/>
							</enumlist>
						</parameter>
						<parameter name="Status">
							<para>1 if enabled or 0 if disabled.</para>
						</parameter>
						<parameter name="RingCount">
							<para>For Call Forwarding No Answer, the ring count.</para>
						</parameter>
						<parameter name="Data">
							<para>For call forwarding enablement, the forward-to number.</para>
						</parameter>
					</syntax>
			</managerEventInstance>
		</managerEvent>
	***/

	/* For binary features, it can either be on or off, anything else is invalid */
	if (status != 0 && status != 1) {
		ast_log(LOG_WARNING, "Invalid status: %d\n", status);
		return -1;
	}

	ast_debug(3, "%s (%s): %s => %d\n", endpoint, device_id, feature, status);

	manager_event(EVENT_FLAG_CALL, "DeviceFeatureSync",
		"DeviceID: %s\r\n"
		"Endpoint: %s\r\n"
		"CallerIDNum: %s\r\n"
		"Feature: %s\r\n"
		"Status: %d\r\n"
		"RingCount: %d\r\n"
		"Data: %s\r\n",
		device_id, endpoint, number, feature, status, ring_count, S_OR(data, ""));
	return 0;
}

#define DEVICE_FEATURE_UPDATE(endpoint, db, upd, var, value, have) { \
	feature_state_sub->sync_data.var = value; \
	feature_state_sub->sync_data.upd = 1; \
	feature_state_sub->have = 1; \
	device_feature_key_update(endpoint, feature_state_sub); \
}

#define DEVICE_FEATURE_UPDATE_STR(endpoint, db, upd, var, value, have) { \
	ast_copy_string(feature_state_sub->sync_data.var, value, sizeof(feature_state_sub->sync_data.var)); \
	feature_state_sub->sync_data.upd = 1; \
	feature_state_sub->have = 1; \
	device_feature_key_update(endpoint, feature_state_sub); \
}

static int parse_incoming_xml(struct feature_state_subscription *feature_state_sub, char *xmlbody, int xmllen, struct ast_sip_endpoint *endpoint, const char *endpoint_name, const char *number)
{
	int res = 0;
	const char *nodename;
	const char *tmpstr;
	struct ast_xml_node *root, *tmpnode = NULL;
	struct ast_xml_doc *xmldoc = ast_xml_read_memory(xmlbody, xmllen);

	if (!xmldoc) {
		ast_log(LOG_WARNING, "%s: Failed to parse SUBSCRIBE body as XML: %.*s\n", endpoint_name, xmllen, xmlbody);
		return -1;
	}

	/*
	 * Broadworks 11-BD5196-00 (2012)
	 *
	 * <?xml version="1.0" encoding="ISO-8859-1"?>
	 * <SetDoNotDisturb xmlns="http://www.ecma-international.org/standards/ecma-323/csta/ed3">
	 *    <device>7659366</device>
	 *    <doNotDisturbOn>true</doNotDisturbOn>
	 * </SetDoNotDisturb>
	 *
	 * (OR)
	 *
	 * <?xml version="1.0" encoding="ISO-8859-1"?>
	 * <SetForwarding xmlns="http://www.ecma-international.org/standards/ecma-323/csta/ed3">
	 *    <device>7659366</device>
	 *    <activateForward>true</activateForward>
	 *    <forwardingType>forwardImmediate</forwardingType>
	 *    <forwardDN>2424</forwardDN>
	 *    <ringCount></ringCount>
	 * </SetForwarding>
	 */

	if (DEBUG_ATLEAST(1)) {
		char *doc_str = NULL;
		int doc_len = 0;
		ast_xml_doc_dump_memory(xmldoc, &doc_str, &doc_len);
		ast_debug(4, "Incoming doc len: %d\n%s\n", doc_len, doc_len ? doc_str : "<empty>");
		ast_xml_free_text(doc_str);
		doc_str = NULL;
		doc_len = 0;
	}

	/* I can't figure out how to do this with the presence XML functions, which seem to be used for writing only at this point.
	 * So we use the core XML routines. */
	root = ast_xml_get_root(xmldoc);
	if (!root) {
		ast_debug(1, "No XML root, malformed XML?\n");
		res = -1;
		goto cleanup;
	}
	nodename = ast_xml_node_get_name(root);

	/* The device is not used by Broadworks, and the phone can set this to any value. See 11-BD5196-00, 3.1.2 */
	tmpnode = ast_xml_find_child_element(root, "device", NULL, NULL);
	tmpstr = ast_xml_get_text(tmpnode);
	ast_copy_string(feature_state_sub->sync_data.deviceid, tmpstr, sizeof(feature_state_sub->sync_data.deviceid)); /* Store the device ID so we can use it in the NOTIFY. */

	if (!strcmp(nodename, "SetDoNotDisturb")) {
		int dnd_disp;

		tmpnode = ast_xml_find_child_element(root, "doNotDisturbOn", NULL, NULL);
		tmpstr = ast_xml_get_text(tmpnode);
		dnd_disp = ast_true(tmpstr) ? 1 : 0; /* ast_true returns -1 on success */
		ast_xml_free_text(tmpstr);

		if (!res) {
			res = send_ami(feature_state_sub->sync_data.deviceid, endpoint_name, number, "DoNotDisturb", dnd_disp, 0, NULL);
		}
		if (!res) {
			ast_verb(4, "%s %s Do Not Disturb -> %s\n", endpoint_name, endpoint->device_feature_key_autoapprove ? "set" : "requested", dnd_disp ? "enabled" : "disabled");
			if (endpoint->device_feature_key_autoapprove) {
				DEVICE_FEATURE_UPDATE(endpoint_name, "do_not_disturb", update_needed_dnd, dnd, dnd_disp, have_dnd);
				send_update(feature_state_sub);
			}
		}
	} else if (!strcmp(nodename, "SetForwarding")) {
		char fwd_tgt[AST_MAX_EXTENSION];
		enum forward_type fwdtype;
		int fwd_disp;
		const char *fwd_name = NULL;
		int ring_count = 0;

		tmpnode = ast_xml_find_child_element(root, "activateForward", NULL, NULL);
		tmpstr = ast_xml_get_text(tmpnode);
		fwd_disp = ast_true(tmpstr) ? 1 : 0; /* ast_true returns -1 on success */
		ast_xml_free_text(tmpstr);

		tmpnode = ast_xml_find_child_element(root, "forwardingType", NULL, NULL);
		tmpstr = ast_xml_get_text(tmpnode);
		if (!strcmp(tmpstr, "forwardImmediate")) {
			fwdtype = FORWARD_ALWAYS;
			fwd_name = "CallForwardingAlways";
		} else if (!strcmp(tmpstr, "forwardBusy")) {
			fwdtype = FORWARD_BUSY;
			fwd_name = "CallForwardingBusy";
		} else if (!strcmp(tmpstr, "forwardNoAns")) {
			fwdtype = FORWARD_NOANSWER;
			fwd_name = "CallForwardingNoAnswer";
		} else {
			ast_log(LOG_WARNING, "Invalid forward type: %s\n", tmpstr);
			res = -1;
			goto cleanup; /* That way gcc doesn't complain about fwdtype being used uninitialized */
		}
		ast_xml_free_text(tmpstr);

		tmpnode = ast_xml_find_child_element(root, "forwardDN", NULL, NULL);
		tmpstr = ast_xml_get_text(tmpnode);
		ast_copy_string(fwd_tgt, tmpstr, sizeof(fwd_tgt));
		ast_xml_free_text(tmpstr);

		if (fwdtype == FORWARD_NOANSWER && fwd_disp) {
			tmpnode = ast_xml_find_child_element(root, "ringCount", NULL, NULL);
			tmpstr = ast_xml_get_text(tmpnode);
			ring_count = atoi(S_OR(tmpstr, ""));
			ast_xml_free_text(tmpstr);
			if (ring_count <= 0) {
				ast_log(LOG_WARNING, "Unexpected ring count: %d\n", ring_count);
			}
		}

		if (endpoint->device_feature_key_autoapprove) {
			const char *internal_fwd_tgt = fwd_disp ? fwd_tgt : "";
			switch (fwdtype) {
			case FORWARD_ALWAYS:
				DEVICE_FEATURE_UPDATE_STR(endpoint_name, "call_forwarding_always", update_needed_fwd_always, fwd_exten_always, internal_fwd_tgt, have_callforwardalways);
				break;
			case FORWARD_BUSY:
				DEVICE_FEATURE_UPDATE_STR(endpoint_name, "call_forwarding_busy", update_needed_fwd_busy, fwd_exten_busy, internal_fwd_tgt, have_callforwardbusy);
				break;
			case FORWARD_NOANSWER:
				DEVICE_FEATURE_UPDATE_STR(endpoint_name, "call_forwarding_noanswer", update_needed_fwd_noanswer, fwd_exten_noanswer, internal_fwd_tgt, have_callforwardnoanswer);
				feature_state_sub->sync_data.ring_count = ring_count;
				break;
			}
			send_update(feature_state_sub);
		}

		if (!res) {
			res = send_ami(feature_state_sub->sync_data.deviceid, endpoint_name, number, fwd_name, fwd_disp, ring_count, fwd_tgt);
		}
		if (!res) {
			ast_verb(4, "%s %s %s Forwarding -> %s %s\n", endpoint_name, endpoint->device_feature_key_autoapprove ? "set" : "requested",
				forward_type_str(fwdtype), fwd_disp ? "enabled to" : "disabled", fwd_disp ? fwd_tgt : "");
		}
	} else {
		ast_log(LOG_WARNING, "Unsupported feature root: %s\n", nodename);
		res = -1;
	}

cleanup:
	ast_xml_close(xmldoc);
	return res;
}

static void subscription_shutdown(struct ast_sip_subscription *sub);
static int new_subscribe(struct ast_sip_endpoint *endpoint, const char *resource);
static int refresh_subscribe(struct ast_sip_subscription *sub, pjsip_rx_data *rdata);
static int subscription_established(struct ast_sip_subscription *sub);
static void *get_notify_data(struct ast_sip_subscription *sub);
static int get_resource_display_name(struct ast_sip_endpoint *endpoint, const char *resource, char *display_name, int display_name_size);
static void to_ami(struct ast_sip_subscription *sub, struct ast_str **buf);

struct ast_sip_notifier feature_notifier = {
	.default_accept = FEATURE_SYNC_MIME_TYPE,
	.new_subscribe = new_subscribe,
	.refresh_subscribe = refresh_subscribe,
	.subscription_established = subscription_established,
	.get_notify_data = get_notify_data,
	.get_resource_display_name = get_resource_display_name,
};

struct ast_sip_subscription_handler feature_handler = {
	.event_name = FEATURE_SYNC_EVENT,
	.body_type = AST_SIP_DEVICE_FEATURE_SYNC_DATA,
	.accept = { FEATURE_SYNC_MIME_TYPE, },
	.subscription_shutdown = subscription_shutdown,
	.to_ami = to_ami,
	.notifier = &feature_notifier,
};

static void feature_state_subscription_destructor(void *obj)
{
	struct feature_state_subscription *sub = obj;
	ast_sip_subscription_destroy(sub->sip_sub);
	ast_taskprocessor_unreference(sub->serializer);
}

static inline void update_everything(struct feature_state_subscription *feature_state_sub, struct ast_sip_device_feature_sync_data *sync_data)
{
	if (feature_state_sub->have_dnd) {
		sync_data->update_needed_dnd = 1;
	}
	if (feature_state_sub->have_callforwardalways) {
		sync_data->update_needed_fwd_always = 1;
	}
	if (feature_state_sub->have_callforwardbusy) {
		sync_data->update_needed_fwd_busy = 1;
	}
	if (feature_state_sub->have_callforwardnoanswer) {
		sync_data->update_needed_fwd_noanswer = 1;
	}
}

static inline void clear_everything(struct ast_sip_device_feature_sync_data *sync_data)
{
	sync_data->update_needed_dnd = 0;
	sync_data->update_needed_fwd_always = 0;
	sync_data->update_needed_fwd_busy = 0;
	sync_data->update_needed_fwd_noanswer = 0;
}

/*!
 * \internal
 * \brief Allocates an feature_state_subscription object.
 *
 * Creates the underlying SIP subscription for the given request. First makes
 * sure that there are registered handler and provider objects available.
 */
static struct feature_state_subscription *feature_state_subscription_alloc(struct ast_sip_subscription *sip_sub, struct ast_sip_endpoint *endpoint)
{
	struct feature_state_subscription *feature_state_sub;
	struct subscription_item *subitem;
	const char *endpoint_name = ast_sorcery_object_get_id(endpoint);

	subitem = ast_calloc(1, sizeof(*subitem) + strlen(endpoint_name) + 1);
	if (!subitem) {
		return NULL;
	}
	strcpy(subitem->data, endpoint_name); /* Safe */
	subitem->endpoint = subitem->data;

	feature_state_sub = ao2_alloc(sizeof(*feature_state_sub), feature_state_subscription_destructor);
	if (!feature_state_sub) {
		return NULL;
	}

	ast_debug(2, "Allocating subscription for %s\n", endpoint_name);
	feature_state_sub->sip_sub = sip_sub;

	/* We keep our own reference to the serializer as there is no guarantee in state_changed
	 * that the subscription tree is still valid when it is called. This can occur when
	 * the subscription is terminated at around the same time as the state_changed
	 * callback is invoked. */
	feature_state_sub->serializer = ao2_bump(ast_sip_subscription_get_serializer(sip_sub));

	/* Restore cached settings if we can. */
	restore_cached_settings(endpoint_name, feature_state_sub);

	/* 2.1.1: When the phone first SUBSCRIBEs, the body is empty, and thus it needs an update on everything. */
	update_everything(feature_state_sub, &feature_state_sub->sync_data);

	/* Insert into the linked list. */
	subitem->sub = feature_state_sub;
	AST_RWLIST_WRLOCK(&sublist);
	AST_RWLIST_INSERT_HEAD(&sublist, subitem, entry);
	AST_RWLIST_UNLOCK(&sublist);

	return feature_state_sub;
}

struct notify_task_data {
	struct ast_sip_device_feature_sync_data sync_data;
	struct feature_state_subscription *feature_state_sub;
	int terminate;
};

static void notify_task_data_destructor(void *obj)
{
	struct notify_task_data *task_data = obj;
	ao2_ref(task_data->feature_state_sub, -1);
}

static void dump_sync_data(struct ast_sip_device_feature_sync_data *sync_data)
{
	ast_debug(3, "Updates needed: DND: %d, CFA: %d, CFB: %d, CFNA: %d\n",
		sync_data->update_needed_dnd, sync_data->update_needed_fwd_always, sync_data->update_needed_fwd_busy, sync_data->update_needed_fwd_noanswer);
}

static struct notify_task_data *alloc_notify_task_data(struct feature_state_subscription *feature_state_sub)
{
	struct notify_task_data *task_data = ao2_alloc(sizeof(*task_data), notify_task_data_destructor);

	if (!task_data) {
		ast_log(LOG_WARNING, "Unable to create notify task data\n");
		return NULL;
	}

	task_data->feature_state_sub = feature_state_sub;
	ao2_ref(task_data->feature_state_sub, +1);
	ast_debug(2, "Allocating notify task\n");

	return task_data;
}

static int notify_task(void *obj)
{
	RAII_VAR(struct notify_task_data *, task_data, obj, ao2_cleanup);
	struct ast_sip_body_data data = {
		.body_type = AST_SIP_DEVICE_FEATURE_SYNC_DATA,
		.body_data = &task_data->sync_data,
	};

	/* The subscription was terminated while notify_task was in queue.
	 * Terminated subscriptions are no longer associated with a valid tree, and sending
	 * NOTIFY messages on a subscription which has already been terminated won't work. */
	if (ast_sip_subscription_is_terminated(task_data->feature_state_sub->sip_sub)) {
		return 0;
	}

	/* Pool allocation has to happen here so that we allocate within a PJLIB thread */
	ast_sip_subscription_notify(task_data->feature_state_sub->sip_sub, &data, task_data->terminate);
	return 0;
}

#define NUM_FEATURES 4
static int send_update(struct feature_state_subscription *feature_state_sub)
{
	int i;

	/* XXX This is kind of a kludge. In order to send updates for more than one feature,
	 * we need to send a multipart XML response to the endpoint, otherwise everything
	 * after the first one is ignored.
	 * Ideally, we could take advantage of res_pjsip_pubsub to generate this for us,
	 * but currently it only handles resource lists.
	 * So alternately, we just send one update for each feature that needs updating.
	 * In reality, this will probably only apply to the initial update since subsequent
	 * NOTIFYs are pretty much guaranteed to be singular... it's not pretty but it works
	 * and really makes no difference in the big scheme of things. */

	for (i = 0; i < NUM_FEATURES; i++) {
		struct notify_task_data *task_data;
		/* Surprisingly this is easier than using a switch. */
		if (i == 0 && !feature_state_sub->sync_data.update_needed_dnd) {
			continue;
		} else if (i == 1 && !feature_state_sub->sync_data.update_needed_fwd_always) {
			continue;
		} else if (i == 2 && !feature_state_sub->sync_data.update_needed_fwd_noanswer) {
			continue;
		} else if (i == 3 && !feature_state_sub->sync_data.update_needed_fwd_busy) {
			continue;
		}

		/* Okay, update actually needed for this feature. Go ahead and do it. */
		if (!(task_data = alloc_notify_task_data(feature_state_sub))) {
			return -1;
		}

		/* Copy current feature_state_sub to the data that will be passed to the body generator, since the body_generator doesn't have access to feature_state_sub. */
		memcpy(&task_data->sync_data, &task_data->feature_state_sub->sync_data, sizeof(task_data->sync_data));
		clear_everything(&task_data->sync_data); /* We only want to update the feature for this round. */
		switch (i) {
		case 0:
			task_data->sync_data.update_needed_dnd = 1;
			break;
		case 1:
			task_data->sync_data.update_needed_fwd_always = 1;
			break;
		case 2:
			task_data->sync_data.update_needed_fwd_noanswer = 1;
			break;
		case 3:
			task_data->sync_data.update_needed_fwd_busy = 1;
			break;
		}
		dump_sync_data(&task_data->sync_data);

		ast_debug(2, "Doing NOTIFY for feature at index %d\n", i);
		if (ast_sip_push_task(task_data->feature_state_sub->serializer, notify_task, task_data)) {
			ao2_cleanup(task_data);
			return -1;
		}
	}

	clear_everything(&feature_state_sub->sync_data); /* Mark everything as handled */
	return 0;
}

/*! \note Must ao2_ref -1 when finished with subscription */
static struct feature_state_subscription *feature_state_sub_by_endpoint(const char *endpoint)
{
	struct feature_state_subscription *feature_state_sub = NULL;
	struct subscription_item *subitem;
	AST_RWLIST_RDLOCK(&sublist);
	AST_LIST_TRAVERSE(&sublist, subitem, entry) {
		if (!strcmp(subitem->endpoint, endpoint)) {
			feature_state_sub = subitem->sub;
			break;
		}
	}
	if (feature_state_sub) {
		ao2_ref(feature_state_sub, +1);
	}
	AST_RWLIST_UNLOCK(&sublist);
	return feature_state_sub;
}

static int func_features_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int res = 0;
	const struct ast_sip_device_feature_object *obj;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(endpoint);
		AST_APP_ARG(feature);
	);
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Missing arguments\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);
	*buf = '\0';

	if (ast_strlen_zero(args.endpoint) || ast_strlen_zero(args.feature)) {
		ast_log(LOG_WARNING, "An endpoint and feature are required\n");
		return -1;
	}

	/* We don't actually care if the subscription currently exists or not.
	 * Just try to see if there's anything cached at the moment. */

	/* Retrieve what the user wants from the cache, which should contain the current state, regardless of whether a subscription is currently active or not. */
	obj = device_feature_object_get(args.endpoint);
	if (!obj) {
		ast_debug(1, "No cached data available for endpoint %s, feature %s\n", args.endpoint, args.feature);
		return -1;
	}

	if (!strcasecmp(args.feature, "do_not_disturb")) {
		if (obj->have_dnd) {
			snprintf(buf, len, "%d", obj->dnd ? 1 : 0);
		} else {
			res = -1;
		}
	} else if (!strcasecmp(args.feature, "call_forwarding_always")) {
		if (obj->have_callforwardalways) {
			ast_copy_string(buf, obj->fwd_exten_always, len);
		} else {
			res = -1;
		}
	} else if (!strcasecmp(args.feature, "call_forwarding_busy")) {
		if (obj->have_callforwardbusy) {
			ast_copy_string(buf, obj->fwd_exten_busy, len);
		} else {
			res = -1;
		}
	} else if (!strcasecmp(args.feature, "call_forwarding_noanswer")) {
		if (obj->have_callforwardnoanswer) {
			ast_copy_string(buf, obj->fwd_exten_noanswer, len);
		} else {
			res = -1;
		}
	} else if (!strcasecmp(args.feature, "ring_count")) {
		if (obj->ring_count) {
			snprintf(buf, len, "%u", obj->ring_count);
		} else {
			res = -1;
		}
	} else {
		ast_log(LOG_WARNING, "Invalid feature: %s\n", args.feature);
		res = -1;
	}

	ao2_ref((struct ast_sip_device_feature_object *) obj, -1);
	return res;
}

static int func_features_write(struct ast_channel *chan, const char *function, char *data, const char *value)
{
	struct feature_state_subscription *feature_state_sub;
	int res = 0;
	char *tmp;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(endpoint);
		AST_APP_ARG(feature);
	);
	AST_DECLARE_APP_ARGS(args2,
		AST_APP_ARG(status);
		AST_APP_ARG(rings);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Missing arguments\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);
	tmp = ast_strdupa(S_OR(value, ""));
	AST_STANDARD_APP_ARGS(args2, tmp);

	if (ast_strlen_zero(args.endpoint) || ast_strlen_zero(args.feature)) {
		ast_log(LOG_WARNING, "An endpoint and feature are required\n");
		return -1;
	}

	if (ast_strlen_zero(args2.status)) {
		args2.status = "";
	}

	feature_state_sub = feature_state_sub_by_endpoint(args.endpoint);
	if (!feature_state_sub) {
		/* If there's no active subscription, but there is an entry for the endpoint in the cache,
		 * we can update that, and that'll get sent to the endpoint when it comes online. */
		const struct ast_sip_device_feature_object *cur_obj = device_feature_object_get(args.endpoint);
		if (cur_obj) {
			struct ast_sip_device_feature_object *obj;
			obj = ast_sorcery_copy(feature_sorcery, cur_obj);
			ao2_ref((struct ast_sip_device_feature_object *) cur_obj, -1);
			if (!obj) {
				ast_debug(1, "Failed to create copy of sorcery object\n");
				return -1;
			}
			if (!strcasecmp(args.feature, "do_not_disturb")) {
				obj->dnd = ast_true(args2.status) ? 1 : 0; /* Use 1, not -1 */
				obj->have_dnd = 1;
			} else if (!strcasecmp(args.feature, "call_forwarding_always")) {
				ast_string_field_set(obj, fwd_exten_always, args2.status);
				obj->have_callforwardalways = 1;
			} else if (!strcasecmp(args.feature, "call_forwarding_busy")) {
				ast_string_field_set(obj, fwd_exten_busy, args2.status);
				obj->have_callforwardbusy = 1;
			} else if (!strcasecmp(args.feature, "call_forwarding_noanswer")) {
				ast_string_field_set(obj, fwd_exten_noanswer, args2.status);
				obj->have_callforwardnoanswer = 1;
				if (!ast_strlen_zero(args2.rings)) {
					int num_rings = atoi(args2.rings);
					if (num_rings > 0 && num_rings <= 10) {
						obj->ring_count = num_rings;
					} else {
						ast_log(LOG_WARNING, "Invalid number of rings: %d\n", num_rings);
					}
				}
			} else {
				ast_log(LOG_WARNING, "Invalid feature: %s\n", args.feature);
				ao2_ref((struct ast_sip_device_feature_object *) obj, -1);
				return -1;
			}
			__device_feature_key_update(obj);
			ao2_ref((struct ast_sip_device_feature_object *) obj, -1);
			ast_debug(1, "No device feature subscription for %s, updated cache instead\n", args.endpoint);
			return 0;
		} else {
			if (strcasestr(args.endpoint, "PJSIP/")) {
				ast_log(LOG_WARNING, "Do not include 'PJSIP/', just the endpoint name, for 'endpoint' argument\n");
			}
			ast_debug(1, "No device feature subscription or cached data for %s\n", args.endpoint);
			return -1;
		}
	}

	/* Terminated subscriptions are no longer associated with a valid tree. Do not queue notify_task. */
	if (ast_sip_subscription_is_terminated(feature_state_sub->sip_sub)) {
		ast_log(LOG_WARNING, "Subscription for %s is already terminated\n", args.endpoint);
		ao2_ref(feature_state_sub, -1);
		return -1;
	}

	/* Get the new status, cache it, and then send a NOTIFY. */
	if (!strcasecmp(args.feature, "do_not_disturb")) {
		int new_dnd_status = ast_true(args2.status) ? 1 : 0; /* Use 1, not -1 */
		DEVICE_FEATURE_UPDATE(args.endpoint, "do_not_disturb", update_needed_dnd, dnd, new_dnd_status, have_dnd);
	} else if (!strcasecmp(args.feature, "call_forwarding_always")) {
		DEVICE_FEATURE_UPDATE_STR(args.endpoint, "call_forwarding_always", update_needed_fwd_always, fwd_exten_always, args2.status, have_callforwardalways);
	} else if (!strcasecmp(args.feature, "call_forwarding_busy")) {
		DEVICE_FEATURE_UPDATE_STR(args.endpoint, "call_forwarding_busy", update_needed_fwd_busy, fwd_exten_busy, args2.status, have_callforwardbusy);
	} else if (!strcasecmp(args.feature, "call_forwarding_noanswer")) {
		DEVICE_FEATURE_UPDATE_STR(args.endpoint, "call_forwarding_noanswer", update_needed_fwd_noanswer, fwd_exten_noanswer, args2.status, have_callforwardnoanswer);
		if (!ast_strlen_zero(args2.rings)) {
			int num_rings = atoi(args2.rings);
			if (num_rings > 0 && num_rings <= 10) {
				feature_state_sub->sync_data.ring_count = num_rings;
				device_feature_key_update(args.endpoint, feature_state_sub);
			} else {
				ast_log(LOG_WARNING, "Invalid number of rings: %d\n", num_rings);
			}
		}
	} else {
		ast_log(LOG_WARNING, "Invalid feature: %s\n", args.feature);
		res = -1;
	}

	if (!res) {
		res = send_update(feature_state_sub);
	}

	ao2_ref(feature_state_sub, -1);
	return res;
}

static struct ast_custom_function features_function = {
	.name = "PJSIP_DEVICE_FEATURES",
	.read = func_features_read,
	.write = func_features_write,
};

static char *handle_pjsip_show_devicefeatures(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct subscription_item *subitem;
	char *ret = NULL;
	int which = 0;
	int c = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show devicefeatures";
		e->usage =
			"Usage: pjsip show devicefeatures [<endpoint>]\n"
			"       Show device feature sync data.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			size_t wlen = strlen(a->word);
			AST_RWLIST_RDLOCK(&sublist);
			AST_LIST_TRAVERSE(&sublist, subitem, entry) {
				if (!strncasecmp(a->word, subitem->endpoint, wlen) && ++which > a->n) {
					ret = ast_strdup(subitem->endpoint);
					break;
				}
			}
			AST_RWLIST_UNLOCK(&sublist);
		}
		return ret;
	}

	AST_RWLIST_RDLOCK(&sublist);
	AST_LIST_TRAVERSE(&sublist, subitem, entry) {
		if (a->argc < 4 || !strcmp(subitem->endpoint, a->argv[3])) {
			struct feature_state_subscription *sub = subitem->sub;
			if (!c++) {
				ast_cli(a->fd, "%-25s %3s %12s %12s %12s %7s\n", "Endpoint", "DND", "CFwdAlways", "CFwdBusy", "CFwdNoAns", "RingCnt");
			}
			ast_cli(a->fd, "%-25s %3s %12s %12s %12s %7d\n", subitem->endpoint,
				sub->have_dnd ? sub->sync_data.dnd ? "On" : "Off" : "-",
				sub->have_callforwardalways ? S_OR(sub->sync_data.fwd_exten_always, "") : "-",
				sub->have_callforwardbusy ? S_OR(sub->sync_data.fwd_exten_busy, "") : "-",
				sub->have_callforwardnoanswer ? S_OR(sub->sync_data.fwd_exten_noanswer, "") : "-",
				sub->sync_data.ring_count);
		}
	}
	AST_RWLIST_UNLOCK(&sublist);

	if (!c && a->argc >= 4) {
		ast_cli(a->fd, "No active subscription for endpoint '%s'\n", a->argv[3]);
		return CLI_FAILURE;
	}
	return CLI_SUCCESS;
}

static struct ast_cli_entry devfeature_cli[] = {
	AST_CLI_DEFINE(handle_pjsip_show_devicefeatures, "Show PJSIP device features"),
};

static struct ast_datastore_info ds_info = { };
static const char ds_name[] = "feature state datastore";

/*!
 * \internal
 * \brief Add a datastore for exten feature_state_subscription.
 *
 * Adds the feature_state_subscription wrapper object to a datastore so it can be retrieved
 * later based upon its association with the ast_sip_subscription.
 */
static int add_datastore(struct feature_state_subscription *feature_state_sub)
{
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_subscription_alloc_datastore(&ds_info, ds_name), ao2_cleanup);

	if (!datastore) {
		return -1;
	}

	datastore->data = feature_state_sub;
	ast_sip_subscription_add_datastore(feature_state_sub->sip_sub, datastore);
	ao2_ref(feature_state_sub, +1);
	return 0;
}

/*!
 * \internal
 * \brief Get the feature_state_subscription object associated with the given ast_sip_subscription in the datastore.
 */
static struct feature_state_subscription *get_feature_state_sub(struct ast_sip_subscription *sub)
{
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_subscription_get_datastore(sub, ds_name), ao2_cleanup);
	return datastore ? datastore->data : NULL;
}

static void subscription_shutdown(struct ast_sip_subscription *sub)
{
	struct subscription_item *subitem;
	struct ast_sip_endpoint *endpoint;
	const char *endpoint_name;
	struct feature_state_subscription *feature_state_sub = get_feature_state_sub(sub);

	if (!feature_state_sub) {
		return;
	}

	endpoint = ast_sip_subscription_get_endpoint(sub);
	ast_assert(endpoint != NULL);
	endpoint_name = ast_sorcery_object_get_id(endpoint);

	AST_RWLIST_WRLOCK(&sublist);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&sublist, subitem, entry) {
		if (!strcmp(endpoint_name, subitem->endpoint)) {
			AST_RWLIST_REMOVE_CURRENT(entry);
			ast_free(subitem);
			endpoint = NULL;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&sublist);

	if (endpoint) {
		ast_log(LOG_ERROR, "Failed to remove subscription item on subscription shutdown?\n");
	}

	ast_sip_subscription_remove_datastore(feature_state_sub->sip_sub, ds_name);
	ao2_cleanup(feature_state_sub); /* remove data store reference */
}

static int new_subscribe(struct ast_sip_endpoint *endpoint, const char *resource)
{
	ast_debug(2, "New subscription for %s\n", resource);
	return 200;
}

static int refresh_subscribe(struct ast_sip_subscription *sub, pjsip_rx_data *rdata)
{
	struct ast_sip_endpoint *endpoint;
	const char *endpoint_name, *number;
	char *xmlbody;
	int xmllen;

	/* An OK will automatically get sent out by res_pjsip_pubsub...
	 * All we need to do is parse the XML in the body for the feature data that the client sent.
	 * The user can then do something with this that will trigger the device state change,
	 * which will cause a NOTIFY to go out.
	 */
	struct feature_state_subscription *feature_state_sub;

	feature_state_sub = get_feature_state_sub(sub);
	if (!feature_state_sub) {
		ast_log(LOG_WARNING, "No feature state sub?\n");
		return -1;
	}

	endpoint = ast_sip_subscription_get_endpoint(sub);
	if (!endpoint) {
		ast_log(LOG_WARNING, "No endpoint?\n");
		return -1;
	}
	endpoint_name = ast_sorcery_object_get_id(endpoint);
	number = endpoint->id.self.number.str;
	ast_debug(2, "SUBSCRIBE received for existing subscription for %s\n", endpoint_name);

	if (!rdata->msg_info.msg->body) {
		ast_debug(2, "SUBSCRIBE contains no body, queuing all features for resync\n");
		update_everything(feature_state_sub, &feature_state_sub->sync_data); /* 2.1.1: Mark everything as needing a sync on the next NOTIFY. */
		return send_update(feature_state_sub); /* There's no state change involved to trigger the update, so manually do so. */
	}
	xmllen = rdata->msg_info.msg->body->len;
	xmlbody = rdata->msg_info.msg->body->data;
	return parse_incoming_xml(feature_state_sub, xmlbody, xmllen, endpoint, endpoint_name, number);
}

static int get_resource_display_name(struct ast_sip_endpoint *endpoint, const char *resource, char *display_name, int display_name_size)
{
	if (!endpoint || ast_strlen_zero(resource) || !display_name || display_name_size <= 0) {
		return -1;
	}

	ast_copy_string(display_name, ast_sorcery_object_get_id(endpoint), display_name_size);
	return 0;
}

static int subscription_established(struct ast_sip_subscription *sip_sub)
{
	struct ast_sip_endpoint *endpoint = ast_sip_subscription_get_endpoint(sip_sub);
	struct feature_state_subscription *feature_state_sub;
	int added_subscriptions = 0;

	if (!(feature_state_sub = feature_state_subscription_alloc(sip_sub, endpoint))) {
		ao2_cleanup(endpoint);
		return -1;
	}

	update_everything(feature_state_sub, &feature_state_sub->sync_data); /* If it's the first SUBSCRIBE, send endpoint all the settings. */

	/* Go ahead and cleanup the endpoint since we don't need it anymore */
	ao2_cleanup(endpoint);

	if (add_datastore(feature_state_sub)) {
		ast_log(LOG_WARNING, "Unable to add to subscription datastore.\n");
		ao2_cleanup(feature_state_sub);
		return -1;
	}

	ast_debug(2, "%d subscription(s) added for %s\n", added_subscriptions, ast_sorcery_object_get_id(endpoint));
	send_update(feature_state_sub); /* Send it all of its current settings. */

	ao2_cleanup(feature_state_sub);
	return 0;
}

static struct ast_sip_device_feature_sync_data *sync_data_alloc(struct ast_sip_subscription *sip_sub, struct feature_state_subscription *feature_state_sub)
{
	struct ast_sip_device_feature_sync_data *sync_data;
	sync_data = ao2_alloc(sizeof(*sync_data), NULL);
	return sync_data;
}

static void *get_notify_data(struct ast_sip_subscription *sub)
{
	struct feature_state_subscription *feature_state_sub;

	feature_state_sub = get_feature_state_sub(sub);
	if (!feature_state_sub) {
		return NULL;
	}
	return sync_data_alloc(sub, feature_state_sub);
}

static void to_ami(struct ast_sip_subscription *sub, struct ast_str **buf)
{
	return;
}

static int unload_module(void)
{
#if 0
	ast_sip_unregister_subscription_handler(&feature_handler);
	ast_custom_function_unregister(&features_function);

	ast_cli_unregister_multiple(devfeature_cli, ARRAY_LEN(devfeature_cli));

	ast_sorcery_unref(feature_sorcery);
	feature_sorcery = NULL;

	return 0;
#else
	/* Can't unload modules that call ast_sip_register_subscription_handler */
	return -1;
#endif
}

static int load_module(void)
{
	if (device_feature_sorcery_init()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sip_register_subscription_handler(&feature_handler)) {
		ast_log(LOG_WARNING, "Unable to register subscription handler %s\n", feature_handler.event_name);
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_cli_register_multiple(devfeature_cli, ARRAY_LEN(devfeature_cli));

	return ast_custom_function_register(&features_function);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Device Feature Synchronization",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND + 5,
	.requires = "res_pjsip,res_pjsip_pubsub",
);
