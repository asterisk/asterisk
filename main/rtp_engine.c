/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
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
 * \brief Pluggable RTP Architecture
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
***/

/*** DOCUMENTATION
	<managerEvent language="en_US" name="RTCPSent">
		<managerEventInstance class="EVENT_FLAG_REPORTING">
			<synopsis>Raised when an RTCP packet is sent.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="SSRC">
					<para>The SSRC identifier for our stream</para>
				</parameter>
				<parameter name="PT">
					<para>The type of packet for this RTCP report.</para>
					<enumlist>
						<enum name="200(SR)"/>
						<enum name="201(RR)"/>
					</enumlist>
				</parameter>
				<parameter name="To">
					<para>The address the report is sent to.</para>
				</parameter>
				<parameter name="ReportCount">
					<para>The number of reports that were sent.</para>
					<para>The report count determines the number of ReportX headers in
					the message. The X for each set of report headers will range from 0 to
					<literal>ReportCount - 1</literal>.</para>
				</parameter>
				<parameter name="SentNTP" required="false">
					<para>The time the sender generated the report. Only valid when
					PT is <literal>200(SR)</literal>.</para>
				</parameter>
				<parameter name="SentRTP" required="false">
					<para>The sender's last RTP timestamp. Only valid when PT is
					<literal>200(SR)</literal>.</para>
				</parameter>
				<parameter name="SentPackets" required="false">
					<para>The number of packets the sender has sent. Only valid when PT
					is <literal>200(SR)</literal>.</para>
				</parameter>
				<parameter name="SentOctets" required="false">
					<para>The number of bytes the sender has sent. Only valid when PT is
					<literal>200(SR)</literal>.</para>
				</parameter>
				<parameter name="ReportXSourceSSRC">
					<para>The SSRC for the source of this report block.</para>
				</parameter>
				<parameter name="ReportXFractionLost">
					<para>The fraction of RTP data packets from <literal>ReportXSourceSSRC</literal>
					lost since the previous SR or RR report was sent.</para>
				</parameter>
				<parameter name="ReportXCumulativeLost">
					<para>The total number of RTP data packets from <literal>ReportXSourceSSRC</literal>
					lost since the beginning of reception.</para>
				</parameter>
				<parameter name="ReportXHighestSequence">
					<para>The highest sequence number received in an RTP data packet from
					<literal>ReportXSourceSSRC</literal>.</para>
				</parameter>
				<parameter name="ReportXSequenceNumberCycles">
					<para>The number of sequence number cycles seen for the RTP data
					received from <literal>ReportXSourceSSRC</literal>.</para>
				</parameter>
				<parameter name="ReportXIAJitter">
					<para>An estimate of the statistical variance of the RTP data packet
					interarrival time, measured in timestamp units.</para>
				</parameter>
				<parameter name="ReportXLSR">
					<para>The last SR timestamp received from <literal>ReportXSourceSSRC</literal>.
					If no SR has been received from <literal>ReportXSourceSSRC</literal>,
					then 0.</para>
				</parameter>
				<parameter name="ReportXDLSR">
					<para>The delay, expressed in units of 1/65536 seconds, between
					receiving the last SR packet from <literal>ReportXSourceSSRC</literal>
					and sending this report.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="RTCPReceived">
		<managerEventInstance class="EVENT_FLAG_REPORTING">
			<synopsis>Raised when an RTCP packet is received.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="SSRC">
					<para>The SSRC identifier for the remote system</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='RTCPSent']/managerEventInstance/syntax/parameter[@name='PT'])" />
				<parameter name="From">
					<para>The address the report was received from.</para>
				</parameter>
				<parameter name="RTT">
					<para>Calculated Round-Trip Time in seconds</para>
				</parameter>
				<parameter name="ReportCount">
					<para>The number of reports that were received.</para>
					<para>The report count determines the number of ReportX headers in
					the message. The X for each set of report headers will range from 0 to
					<literal>ReportCount - 1</literal>.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='RTCPSent']/managerEventInstance/syntax/parameter[@name='SentNTP'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='RTCPSent']/managerEventInstance/syntax/parameter[@name='SentRTP'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='RTCPSent']/managerEventInstance/syntax/parameter[@name='SentPackets'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='RTCPSent']/managerEventInstance/syntax/parameter[@name='SentOctets'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='RTCPSent']/managerEventInstance/syntax/parameter[contains(@name, 'ReportX')])" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <math.h>

#include "asterisk/channel.h"
#include "asterisk/frame.h"
#include "asterisk/module.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/manager.h"
#include "asterisk/options.h"
#include "asterisk/astobj2.h"
#include "asterisk/pbx.h"
#include "asterisk/translate.h"
#include "asterisk/netsock2.h"
#include "asterisk/_private.h"
#include "asterisk/framehook.h"
#include "asterisk/stasis.h"
#include "asterisk/json.h"
#include "asterisk/stasis_channels.h"

struct ast_srtp_res *res_srtp = NULL;
struct ast_srtp_policy_res *res_srtp_policy = NULL;

/*! Structure that represents an RTP session (instance) */
struct ast_rtp_instance {
	/*! Engine that is handling this RTP instance */
	struct ast_rtp_engine *engine;
	/*! Data unique to the RTP engine */
	void *data;
	/*! RTP properties that have been set and their value */
	int properties[AST_RTP_PROPERTY_MAX];
	/*! Address that we are expecting RTP to come in to */
	struct ast_sockaddr local_address;
	/*! The original source address */
	struct ast_sockaddr requested_target_address;
	/*! Address that we are sending RTP to */
	struct ast_sockaddr incoming_source_address;
	/*! Instance that we are bridged to if doing remote or local bridging */
	struct ast_rtp_instance *bridged;
	/*! Payload and packetization information */
	struct ast_rtp_codecs codecs;
	/*! RTP timeout time (negative or zero means disabled, negative value means temporarily disabled) */
	int timeout;
	/*! RTP timeout when on hold (negative or zero means disabled, negative value means temporarily disabled). */
	int holdtimeout;
	/*! RTP keepalive interval */
	int keepalive;
	/*! Glue currently in use */
	struct ast_rtp_glue *glue;
	/*! SRTP info associated with the instance */
	struct ast_srtp *srtp;
	/*! Channel unique ID */
	char channel_uniqueid[AST_MAX_UNIQUEID];
};

/*! List of RTP engines that are currently registered */
static AST_RWLIST_HEAD_STATIC(engines, ast_rtp_engine);

/*! List of RTP glues */
static AST_RWLIST_HEAD_STATIC(glues, ast_rtp_glue);

#define MAX_RTP_MIME_TYPES 128

/*! The following array defines the MIME Media type (and subtype) for each
   of our codecs, or RTP-specific data type. */
static struct ast_rtp_mime_type {
	/*! \brief A mapping object between the Asterisk codec and this RTP payload */
	struct ast_rtp_payload_type payload_type;
	/*! \brief The media type */
	char type[16];
	/*! \brief The format type */
	char subtype[64];
	/*! \brief Expected sample rate of the /c subtype */
	unsigned int sample_rate;
} ast_rtp_mime_types[128]; /* This will Likely not need to grow any time soon. */
static ast_rwlock_t mime_types_lock;
static int mime_types_len = 0;

/*!
 * \brief Mapping between Asterisk codecs and rtp payload types
 *
 * Static (i.e., well-known) RTP payload types for our "AST_FORMAT..."s:
 * also, our own choices for dynamic payload types.  This is our master
 * table for transmission
 *
 * See http://www.iana.org/assignments/rtp-parameters for a list of
 * assigned values
 */
static struct ast_rtp_payload_type static_RTP_PT[AST_RTP_MAX_PT];
static ast_rwlock_t static_RTP_PT_lock;

/*! \brief \ref stasis topic for RTP related messages */
static struct stasis_topic *rtp_topic;


/*! \internal \brief Destructor for \c ast_rtp_payload_type */
static void rtp_payload_type_dtor(void *obj)
{
	struct ast_rtp_payload_type *payload = obj;

	ao2_cleanup(payload->format);
}

struct ast_rtp_payload_type *ast_rtp_engine_alloc_payload_type(void)
{
	struct ast_rtp_payload_type *payload;

	payload = ao2_alloc(sizeof(*payload), rtp_payload_type_dtor);

	return payload;
}

int ast_rtp_engine_register2(struct ast_rtp_engine *engine, struct ast_module *module)
{
	struct ast_rtp_engine *current_engine;

	/* Perform a sanity check on the engine structure to make sure it has the basics */
	if (ast_strlen_zero(engine->name) || !engine->new || !engine->destroy || !engine->write || !engine->read) {
		ast_log(LOG_WARNING, "RTP Engine '%s' failed sanity check so it was not registered.\n", !ast_strlen_zero(engine->name) ? engine->name : "Unknown");
		return -1;
	}

	/* Link owner module to the RTP engine for reference counting purposes */
	engine->mod = module;

	AST_RWLIST_WRLOCK(&engines);

	/* Ensure that no two modules with the same name are registered at the same time */
	AST_RWLIST_TRAVERSE(&engines, current_engine, entry) {
		if (!strcmp(current_engine->name, engine->name)) {
			ast_log(LOG_WARNING, "An RTP engine with the name '%s' has already been registered.\n", engine->name);
			AST_RWLIST_UNLOCK(&engines);
			return -1;
		}
	}

	/* The engine survived our critique. Off to the list it goes to be used */
	AST_RWLIST_INSERT_TAIL(&engines, engine, entry);

	AST_RWLIST_UNLOCK(&engines);

	ast_verb(2, "Registered RTP engine '%s'\n", engine->name);

	return 0;
}

int ast_rtp_engine_unregister(struct ast_rtp_engine *engine)
{
	struct ast_rtp_engine *current_engine = NULL;

	AST_RWLIST_WRLOCK(&engines);

	if ((current_engine = AST_RWLIST_REMOVE(&engines, engine, entry))) {
		ast_verb(2, "Unregistered RTP engine '%s'\n", engine->name);
	}

	AST_RWLIST_UNLOCK(&engines);

	return current_engine ? 0 : -1;
}

int ast_rtp_glue_register2(struct ast_rtp_glue *glue, struct ast_module *module)
{
	struct ast_rtp_glue *current_glue = NULL;

	if (ast_strlen_zero(glue->type)) {
		return -1;
	}

	glue->mod = module;

	AST_RWLIST_WRLOCK(&glues);

	AST_RWLIST_TRAVERSE(&glues, current_glue, entry) {
		if (!strcasecmp(current_glue->type, glue->type)) {
			ast_log(LOG_WARNING, "RTP glue with the name '%s' has already been registered.\n", glue->type);
			AST_RWLIST_UNLOCK(&glues);
			return -1;
		}
	}

	AST_RWLIST_INSERT_TAIL(&glues, glue, entry);

	AST_RWLIST_UNLOCK(&glues);

	ast_verb(2, "Registered RTP glue '%s'\n", glue->type);

	return 0;
}

int ast_rtp_glue_unregister(struct ast_rtp_glue *glue)
{
	struct ast_rtp_glue *current_glue = NULL;

	AST_RWLIST_WRLOCK(&glues);

	if ((current_glue = AST_RWLIST_REMOVE(&glues, glue, entry))) {
		ast_verb(2, "Unregistered RTP glue '%s'\n", glue->type);
	}

	AST_RWLIST_UNLOCK(&glues);

	return current_glue ? 0 : -1;
}

static void instance_destructor(void *obj)
{
	struct ast_rtp_instance *instance = obj;

	/* Pass us off to the engine to destroy */
	if (instance->data && instance->engine->destroy(instance)) {
		ast_debug(1, "Engine '%s' failed to destroy RTP instance '%p'\n", instance->engine->name, instance);
		return;
	}

	if (instance->srtp) {
		res_srtp->destroy(instance->srtp);
	}

	ast_rtp_codecs_payloads_destroy(&instance->codecs);

	/* Drop our engine reference */
	ast_module_unref(instance->engine->mod);

	ast_debug(1, "Destroyed RTP instance '%p'\n", instance);
}

int ast_rtp_instance_destroy(struct ast_rtp_instance *instance)
{
	ao2_ref(instance, -1);

	return 0;
}

struct ast_rtp_instance *ast_rtp_instance_new(const char *engine_name,
		struct ast_sched_context *sched, const struct ast_sockaddr *sa,
		void *data)
{
	struct ast_sockaddr address = {{0,}};
	struct ast_rtp_instance *instance = NULL;
	struct ast_rtp_engine *engine = NULL;

	AST_RWLIST_RDLOCK(&engines);

	/* If an engine name was specified try to use it or otherwise use the first one registered */
	if (!ast_strlen_zero(engine_name)) {
		AST_RWLIST_TRAVERSE(&engines, engine, entry) {
			if (!strcmp(engine->name, engine_name)) {
				break;
			}
		}
	} else {
		engine = AST_RWLIST_FIRST(&engines);
	}

	/* If no engine was actually found bail out now */
	if (!engine) {
		ast_log(LOG_ERROR, "No RTP engine was found. Do you have one loaded?\n");
		AST_RWLIST_UNLOCK(&engines);
		return NULL;
	}

	/* Bump up the reference count before we return so the module can not be unloaded */
	ast_module_ref(engine->mod);

	AST_RWLIST_UNLOCK(&engines);

	/* Allocate a new RTP instance */
	if (!(instance = ao2_alloc(sizeof(*instance), instance_destructor))) {
		ast_module_unref(engine->mod);
		return NULL;
	}
	instance->engine = engine;
	ast_sockaddr_copy(&instance->local_address, sa);
	ast_sockaddr_copy(&address, sa);

	if (ast_rtp_codecs_payloads_initialize(&instance->codecs)) {
		ao2_ref(instance, -1);
		return NULL;
	}

	ast_debug(1, "Using engine '%s' for RTP instance '%p'\n", engine->name, instance);

	/* And pass it off to the engine to setup */
	if (instance->engine->new(instance, sched, &address, data)) {
		ast_debug(1, "Engine '%s' failed to setup RTP instance '%p'\n", engine->name, instance);
		ao2_ref(instance, -1);
		return NULL;
	}

	ast_debug(1, "RTP instance '%p' is setup and ready to go\n", instance);

	return instance;
}

const char *ast_rtp_instance_get_channel_id(struct ast_rtp_instance *instance)
{
	return instance->channel_uniqueid;
}

void ast_rtp_instance_set_channel_id(struct ast_rtp_instance *instance, const char *uniqueid)
{
	ast_copy_string(instance->channel_uniqueid, uniqueid, sizeof(instance->channel_uniqueid));
}

void ast_rtp_instance_set_data(struct ast_rtp_instance *instance, void *data)
{
	instance->data = data;
}

void *ast_rtp_instance_get_data(struct ast_rtp_instance *instance)
{
	return instance->data;
}

int ast_rtp_instance_write(struct ast_rtp_instance *instance, struct ast_frame *frame)
{
	return instance->engine->write(instance, frame);
}

struct ast_frame *ast_rtp_instance_read(struct ast_rtp_instance *instance, int rtcp)
{
	return instance->engine->read(instance, rtcp);
}

int ast_rtp_instance_set_local_address(struct ast_rtp_instance *instance,
		const struct ast_sockaddr *address)
{
	ast_sockaddr_copy(&instance->local_address, address);
	return 0;
}

int ast_rtp_instance_set_incoming_source_address(struct ast_rtp_instance *instance,
						 const struct ast_sockaddr *address)
{
	ast_sockaddr_copy(&instance->incoming_source_address, address);

	/* moo */

	if (instance->engine->remote_address_set) {
		instance->engine->remote_address_set(instance, &instance->incoming_source_address);
	}

	return 0;
}

int ast_rtp_instance_set_requested_target_address(struct ast_rtp_instance *instance,
						  const struct ast_sockaddr *address)
{
	ast_sockaddr_copy(&instance->requested_target_address, address);

	return ast_rtp_instance_set_incoming_source_address(instance, address);
}

int ast_rtp_instance_get_and_cmp_local_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	if (ast_sockaddr_cmp(address, &instance->local_address) != 0) {
		ast_sockaddr_copy(address, &instance->local_address);
		return 1;
	}

	return 0;
}

void ast_rtp_instance_get_local_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	ast_sockaddr_copy(address, &instance->local_address);
}

int ast_rtp_instance_get_and_cmp_requested_target_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	if (ast_sockaddr_cmp(address, &instance->requested_target_address) != 0) {
		ast_sockaddr_copy(address, &instance->requested_target_address);
		return 1;
	}

	return 0;
}

void ast_rtp_instance_get_incoming_source_address(struct ast_rtp_instance *instance,
						  struct ast_sockaddr *address)
{
	ast_sockaddr_copy(address, &instance->incoming_source_address);
}

void ast_rtp_instance_get_requested_target_address(struct ast_rtp_instance *instance,
						   struct ast_sockaddr *address)
{
	ast_sockaddr_copy(address, &instance->requested_target_address);
}

void ast_rtp_instance_set_extended_prop(struct ast_rtp_instance *instance, int property, void *value)
{
	if (instance->engine->extended_prop_set) {
		instance->engine->extended_prop_set(instance, property, value);
	}
}

void *ast_rtp_instance_get_extended_prop(struct ast_rtp_instance *instance, int property)
{
	if (instance->engine->extended_prop_get) {
		return instance->engine->extended_prop_get(instance, property);
	}

	return NULL;
}

void ast_rtp_instance_set_prop(struct ast_rtp_instance *instance, enum ast_rtp_property property, int value)
{
	instance->properties[property] = value;

	if (instance->engine->prop_set) {
		instance->engine->prop_set(instance, property, value);
	}
}

int ast_rtp_instance_get_prop(struct ast_rtp_instance *instance, enum ast_rtp_property property)
{
	return instance->properties[property];
}

struct ast_rtp_codecs *ast_rtp_instance_get_codecs(struct ast_rtp_instance *instance)
{
	return &instance->codecs;
}

int ast_rtp_codecs_payloads_initialize(struct ast_rtp_codecs *codecs)
{
	int res;

	codecs->framing = 0;
	ast_rwlock_init(&codecs->codecs_lock);
	res = AST_VECTOR_INIT(&codecs->payloads, AST_RTP_MAX_PT);

	return res;
}

void ast_rtp_codecs_payloads_destroy(struct ast_rtp_codecs *codecs)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&codecs->payloads); i++) {
		struct ast_rtp_payload_type *type;

		type = AST_VECTOR_GET(&codecs->payloads, i);
		ao2_t_cleanup(type, "destroying ast_rtp_codec");
	}
	AST_VECTOR_FREE(&codecs->payloads);

	ast_rwlock_destroy(&codecs->codecs_lock);
}

void ast_rtp_codecs_payloads_clear(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance)
{
	ast_rtp_codecs_payloads_destroy(codecs);

	if (instance && instance->engine && instance->engine->payload_set) {
		int i;
		for (i = 0; i < AST_RTP_MAX_PT; i++) {
			instance->engine->payload_set(instance, i, 0, NULL, 0);
		}
	}

	ast_rtp_codecs_payloads_initialize(codecs);
}

void ast_rtp_codecs_payloads_copy(struct ast_rtp_codecs *src, struct ast_rtp_codecs *dest, struct ast_rtp_instance *instance)
{
	int i;

	ast_rwlock_rdlock(&src->codecs_lock);
	ast_rwlock_wrlock(&dest->codecs_lock);

	for (i = 0; i < AST_VECTOR_SIZE(&src->payloads); i++) {
		struct ast_rtp_payload_type *type;

		type = AST_VECTOR_GET(&src->payloads, i);
		if (!type) {
			continue;
		}
		if (i < AST_VECTOR_SIZE(&dest->payloads)) {
			ao2_t_cleanup(AST_VECTOR_GET(&dest->payloads, i), "cleaning up vector element about to be replaced");
		}
		ast_debug(2, "Copying payload %d (%p) from %p to %p\n", i, type, src, dest);
		ao2_bump(type);
		AST_VECTOR_REPLACE(&dest->payloads, i, type);

		if (instance && instance->engine && instance->engine->payload_set) {
			instance->engine->payload_set(instance, i, type->asterisk_format, type->format, type->rtp_code);
		}
	}
	dest->framing = src->framing;
	ast_rwlock_unlock(&dest->codecs_lock);
	ast_rwlock_unlock(&src->codecs_lock);
}

void ast_rtp_codecs_payloads_set_m_type(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload)
{
	struct ast_rtp_payload_type *new_type;

	new_type = ast_rtp_engine_alloc_payload_type();
	if (!new_type) {
		return;
	}

	ast_rwlock_rdlock(&static_RTP_PT_lock);
	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		ast_rwlock_unlock(&static_RTP_PT_lock);
		return;
	}

	ast_rwlock_wrlock(&codecs->codecs_lock);
	if (payload < AST_VECTOR_SIZE(&codecs->payloads)) {
		ao2_t_cleanup(AST_VECTOR_GET(&codecs->payloads, payload), "cleaning up replaced payload type");
	}

	new_type->asterisk_format = static_RTP_PT[payload].asterisk_format;
	new_type->rtp_code = static_RTP_PT[payload].rtp_code;
	new_type->payload = payload;
	new_type->format = ao2_bump(static_RTP_PT[payload].format);

	ast_debug(1, "Setting payload %d (%p) based on m type on %p\n", payload, new_type, codecs);
	AST_VECTOR_REPLACE(&codecs->payloads, payload, new_type);

	if (instance && instance->engine && instance->engine->payload_set) {
		instance->engine->payload_set(instance, payload, new_type->asterisk_format, new_type->format, new_type->rtp_code);
	}

	ast_rwlock_unlock(&codecs->codecs_lock);
	ast_rwlock_unlock(&static_RTP_PT_lock);
}

int ast_rtp_codecs_payloads_set_rtpmap_type_rate(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int pt,
				 char *mimetype, char *mimesubtype,
				 enum ast_rtp_options options,
				 unsigned int sample_rate)
{
	unsigned int i;
	int found = 0;

	ast_rwlock_rdlock(&mime_types_lock);
	if (pt < 0 || pt >= AST_RTP_MAX_PT) {
		ast_rwlock_unlock(&mime_types_lock);
		return -1; /* bogus payload type */
	}

	ast_rwlock_wrlock(&codecs->codecs_lock);
	for (i = 0; i < mime_types_len; ++i) {
		const struct ast_rtp_mime_type *t = &ast_rtp_mime_types[i];
		struct ast_rtp_payload_type *new_type;

		if (strcasecmp(mimesubtype, t->subtype)) {
			continue;
		}

		if (strcasecmp(mimetype, t->type)) {
			continue;
		}

		/* if both sample rates have been supplied, and they don't match,
		 * then this not a match; if one has not been supplied, then the
		 * rates are not compared */
		if (sample_rate && t->sample_rate &&
		    (sample_rate != t->sample_rate)) {
			continue;
		}

		found = 1;

		new_type = ast_rtp_engine_alloc_payload_type();
		if (!new_type) {
			continue;
		}

		if (pt < AST_VECTOR_SIZE(&codecs->payloads)) {
			ao2_t_cleanup(AST_VECTOR_GET(&codecs->payloads, pt), "cleaning up replaced payload type");
		}

		new_type->payload = pt;
		new_type->asterisk_format = t->payload_type.asterisk_format;
		new_type->rtp_code = t->payload_type.rtp_code;
		if ((ast_format_cmp(t->payload_type.format, ast_format_g726) == AST_FORMAT_CMP_EQUAL) &&
				t->payload_type.asterisk_format && (options & AST_RTP_OPT_G726_NONSTANDARD)) {
			new_type->format = ao2_bump(ast_format_g726_aal2);
		} else {
			new_type->format = ao2_bump(t->payload_type.format);
		}
		AST_VECTOR_REPLACE(&codecs->payloads, pt, new_type);

		if (instance && instance->engine && instance->engine->payload_set) {
			instance->engine->payload_set(instance, pt, new_type->asterisk_format, new_type->format, new_type->rtp_code);
		}

		break;
	}
	ast_rwlock_unlock(&codecs->codecs_lock);
	ast_rwlock_unlock(&mime_types_lock);

	return (found ? 0 : -2);
}

int ast_rtp_codecs_payloads_set_rtpmap_type(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload, char *mimetype, char *mimesubtype, enum ast_rtp_options options)
{
	return ast_rtp_codecs_payloads_set_rtpmap_type_rate(codecs, instance, payload, mimetype, mimesubtype, options, 0);
}

void ast_rtp_codecs_payloads_unset(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload)
{
	struct ast_rtp_payload_type *type;

	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		return;
	}

	ast_debug(2, "Unsetting payload %d on %p\n", payload, codecs);

	ast_rwlock_wrlock(&codecs->codecs_lock);
	if (payload < AST_VECTOR_SIZE(&codecs->payloads)) {
		type = AST_VECTOR_GET(&codecs->payloads, payload);
		ao2_cleanup(type);
		AST_VECTOR_REPLACE(&codecs->payloads, payload, NULL);
	}

	if (instance && instance->engine && instance->engine->payload_set) {
		instance->engine->payload_set(instance, payload, 0, NULL, 0);
	}

	ast_rwlock_unlock(&codecs->codecs_lock);
}

struct ast_rtp_payload_type *ast_rtp_codecs_get_payload(struct ast_rtp_codecs *codecs, int payload)
{
	struct ast_rtp_payload_type *type = NULL;

	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		return NULL;
	}

	ast_rwlock_rdlock(&codecs->codecs_lock);
	if (payload < AST_VECTOR_SIZE(&codecs->payloads)) {
		type = AST_VECTOR_GET(&codecs->payloads, payload);
		ao2_bump(type);
	}
	ast_rwlock_unlock(&codecs->codecs_lock);

	if (!type) {
		type = ast_rtp_engine_alloc_payload_type();
		if (!type) {
			return NULL;
		}
		ast_rwlock_rdlock(&static_RTP_PT_lock);
		type->asterisk_format = static_RTP_PT[payload].asterisk_format;
		type->rtp_code = static_RTP_PT[payload].rtp_code;
		type->payload = payload;
		type->format = ao2_bump(static_RTP_PT[payload].format);
		ast_rwlock_unlock(&static_RTP_PT_lock);
	}

	return type;
}

int ast_rtp_codecs_payload_replace_format(struct ast_rtp_codecs *codecs, int payload, struct ast_format *format)
{
	struct ast_rtp_payload_type *type;

	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		return -1;
	}

	ast_rwlock_wrlock(&codecs->codecs_lock);
	if (payload < AST_VECTOR_SIZE(&codecs->payloads)) {
		type = AST_VECTOR_GET(&codecs->payloads, payload);
		if (type && type->asterisk_format) {
			ao2_replace(type->format, format);
		}
	}
	ast_rwlock_unlock(&codecs->codecs_lock);

	return 0;
}

struct ast_format *ast_rtp_codecs_get_payload_format(struct ast_rtp_codecs *codecs, int payload)
{
	struct ast_rtp_payload_type *type;
	struct ast_format *format = NULL;

	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		return NULL;
	}

	ast_rwlock_rdlock(&codecs->codecs_lock);
	if (payload < AST_VECTOR_SIZE(&codecs->payloads)) {
		type = AST_VECTOR_GET(&codecs->payloads, payload);
		if (type && type->asterisk_format) {
			format = ao2_bump(type->format);
		}
	}
	ast_rwlock_unlock(&codecs->codecs_lock);

	return format;
}

void ast_rtp_codecs_set_framing(struct ast_rtp_codecs *codecs, unsigned int framing)
{
	if (!framing) {
		return;
	}

	ast_rwlock_wrlock(&codecs->codecs_lock);
	codecs->framing = framing;
	ast_rwlock_unlock(&codecs->codecs_lock);
}

unsigned int ast_rtp_codecs_get_framing(struct ast_rtp_codecs *codecs)
{
	unsigned int framing;

	ast_rwlock_rdlock(&codecs->codecs_lock);
	framing = codecs->framing;
	ast_rwlock_unlock(&codecs->codecs_lock);

	return framing;
}

void ast_rtp_codecs_payload_formats(struct ast_rtp_codecs *codecs, struct ast_format_cap *astformats, int *nonastformats)
{
	int i;

	ast_format_cap_remove_by_type(astformats, AST_MEDIA_TYPE_UNKNOWN);
	*nonastformats = 0;

	ast_rwlock_rdlock(&codecs->codecs_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&codecs->payloads); i++) {
		struct ast_rtp_payload_type *type;

		type = AST_VECTOR_GET(&codecs->payloads, i);
		if (!type) {
			continue;
		}

		if (type->asterisk_format) {
			ast_format_cap_append(astformats, type->format, 0);
		} else {
			*nonastformats |= type->rtp_code;
		}
	}

	if (codecs->framing) {
		ast_format_cap_set_framing(astformats, codecs->framing);
	}

	ast_rwlock_unlock(&codecs->codecs_lock);
}

int ast_rtp_codecs_payload_code(struct ast_rtp_codecs *codecs, int asterisk_format, const struct ast_format *format, int code)
{
	struct ast_rtp_payload_type *type;
	int i;
	int payload = -1;

	ast_rwlock_rdlock(&codecs->codecs_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&codecs->payloads); i++) {
		type = AST_VECTOR_GET(&codecs->payloads, i);
		if (!type) {
			continue;
		}

		if ((asterisk_format && format && ast_format_cmp(format, type->format) == AST_FORMAT_CMP_EQUAL)
			|| (!asterisk_format && type->rtp_code == code)) {
			payload = i;
			break;
		}
	}
	ast_rwlock_unlock(&codecs->codecs_lock);

	if (payload < 0) {
		ast_rwlock_rdlock(&static_RTP_PT_lock);
		for (i = 0; i < AST_RTP_MAX_PT; i++) {
			if (static_RTP_PT[i].asterisk_format && asterisk_format && format &&
				(ast_format_cmp(format, static_RTP_PT[i].format) != AST_FORMAT_CMP_NOT_EQUAL)) {
				payload = i;
				break;
			} else if (!static_RTP_PT[i].asterisk_format && !asterisk_format &&
				(static_RTP_PT[i].rtp_code == code)) {
				payload = i;
				break;
			}
		}
		ast_rwlock_unlock(&static_RTP_PT_lock);
	}

	return payload;
}

int ast_rtp_codecs_find_payload_code(struct ast_rtp_codecs *codecs, int code)
{
	struct ast_rtp_payload_type *type;
	int res = -1;

	ast_rwlock_rdlock(&codecs->codecs_lock);
	if (code < AST_VECTOR_SIZE(&codecs->payloads)) {
		type = AST_VECTOR_GET(&codecs->payloads, code);
		if (type) {
			res = type->payload;
		}
	}
	ast_rwlock_unlock(&codecs->codecs_lock);

	return res;
}

const char *ast_rtp_lookup_mime_subtype2(const int asterisk_format, struct ast_format *format, int code, enum ast_rtp_options options)
{
	int i;
	const char *res = "";

	ast_rwlock_rdlock(&mime_types_lock);
	for (i = 0; i < mime_types_len; i++) {
		if (ast_rtp_mime_types[i].payload_type.asterisk_format && asterisk_format && format &&
			(ast_format_cmp(format, ast_rtp_mime_types[i].payload_type.format) != AST_FORMAT_CMP_NOT_EQUAL)) {
			if ((ast_format_cmp(format, ast_format_g726_aal2) == AST_FORMAT_CMP_EQUAL) &&
					(options & AST_RTP_OPT_G726_NONSTANDARD)) {
				res = "G726-32";
				break;
			} else {
				res = ast_rtp_mime_types[i].subtype;
				break;
			}
		} else if (!ast_rtp_mime_types[i].payload_type.asterisk_format && !asterisk_format &&
			ast_rtp_mime_types[i].payload_type.rtp_code == code) {

			res = ast_rtp_mime_types[i].subtype;
			break;
		}
	}
	ast_rwlock_unlock(&mime_types_lock);

	return res;
}

unsigned int ast_rtp_lookup_sample_rate2(int asterisk_format, struct ast_format *format, int code)
{
	unsigned int i;
	unsigned int res = 0;

	ast_rwlock_rdlock(&mime_types_lock);
	for (i = 0; i < mime_types_len; ++i) {
		if (ast_rtp_mime_types[i].payload_type.asterisk_format && asterisk_format && format &&
			(ast_format_cmp(format, ast_rtp_mime_types[i].payload_type.format) != AST_FORMAT_CMP_NOT_EQUAL)) {
			res = ast_rtp_mime_types[i].sample_rate;
			break;
		} else if (!ast_rtp_mime_types[i].payload_type.asterisk_format && !asterisk_format &&
			ast_rtp_mime_types[i].payload_type.rtp_code == code) {
			res = ast_rtp_mime_types[i].sample_rate;
			break;
		}
	}
	ast_rwlock_unlock(&mime_types_lock);

	return res;
}

char *ast_rtp_lookup_mime_multiple2(struct ast_str *buf, struct ast_format_cap *ast_format_capability, int rtp_capability, const int asterisk_format, enum ast_rtp_options options)
{
	int found = 0;
	const char *name;
	if (!buf) {
		return NULL;
	}


	if (asterisk_format) {
		int x;
		struct ast_format *tmp_fmt;
		for (x = 0; x < ast_format_cap_count(ast_format_capability); x++) {
			tmp_fmt = ast_format_cap_get_format(ast_format_capability, x);
			name = ast_rtp_lookup_mime_subtype2(asterisk_format, tmp_fmt, 0, options);
			ao2_ref(tmp_fmt, -1);
			ast_str_append(&buf, 0, "%s|", name);
			found = 1;
		}
	} else {
		int x;
		ast_str_append(&buf, 0, "0x%x (", (unsigned int) rtp_capability);
		for (x = 1; x <= AST_RTP_MAX; x <<= 1) {
			if (rtp_capability & x) {
				name = ast_rtp_lookup_mime_subtype2(asterisk_format, NULL, x, options);
				ast_str_append(&buf, 0, "%s|", name);
				found = 1;
			}
		}
	}

	ast_str_append(&buf, 0, "%s", found ? ")" : "nothing)");

	return ast_str_buffer(buf);
}

int ast_rtp_instance_dtmf_begin(struct ast_rtp_instance *instance, char digit)
{
	return instance->engine->dtmf_begin ? instance->engine->dtmf_begin(instance, digit) : -1;
}

int ast_rtp_instance_dtmf_end(struct ast_rtp_instance *instance, char digit)
{
	return instance->engine->dtmf_end ? instance->engine->dtmf_end(instance, digit) : -1;
}
int ast_rtp_instance_dtmf_end_with_duration(struct ast_rtp_instance *instance, char digit, unsigned int duration)
{
	return instance->engine->dtmf_end_with_duration ? instance->engine->dtmf_end_with_duration(instance, digit, duration) : -1;
}

int ast_rtp_instance_dtmf_mode_set(struct ast_rtp_instance *instance, enum ast_rtp_dtmf_mode dtmf_mode)
{
	return (!instance->engine->dtmf_mode_set || instance->engine->dtmf_mode_set(instance, dtmf_mode)) ? -1 : 0;
}

enum ast_rtp_dtmf_mode ast_rtp_instance_dtmf_mode_get(struct ast_rtp_instance *instance)
{
	return instance->engine->dtmf_mode_get ? instance->engine->dtmf_mode_get(instance) : 0;
}

void ast_rtp_instance_update_source(struct ast_rtp_instance *instance)
{
	if (instance->engine->update_source) {
		instance->engine->update_source(instance);
	}
}

void ast_rtp_instance_change_source(struct ast_rtp_instance *instance)
{
	if (instance->engine->change_source) {
		instance->engine->change_source(instance);
	}
}

int ast_rtp_instance_set_qos(struct ast_rtp_instance *instance, int tos, int cos, const char *desc)
{
	return instance->engine->qos ? instance->engine->qos(instance, tos, cos, desc) : -1;
}

void ast_rtp_instance_stop(struct ast_rtp_instance *instance)
{
	if (instance->engine->stop) {
		instance->engine->stop(instance);
	}
}

int ast_rtp_instance_fd(struct ast_rtp_instance *instance, int rtcp)
{
	return instance->engine->fd ? instance->engine->fd(instance, rtcp) : -1;
}

struct ast_rtp_glue *ast_rtp_instance_get_glue(const char *type)
{
	struct ast_rtp_glue *glue = NULL;

	AST_RWLIST_RDLOCK(&glues);

	AST_RWLIST_TRAVERSE(&glues, glue, entry) {
		if (!strcasecmp(glue->type, type)) {
			break;
		}
	}

	AST_RWLIST_UNLOCK(&glues);

	return glue;
}

/*!
 * \brief Conditionally unref an rtp instance
 */
static void unref_instance_cond(struct ast_rtp_instance **instance)
{
	if (*instance) {
		ao2_ref(*instance, -1);
		*instance = NULL;
	}
}

struct ast_rtp_instance *ast_rtp_instance_get_bridged(struct ast_rtp_instance *instance)
{
	return instance->bridged;
}

void ast_rtp_instance_set_bridged(struct ast_rtp_instance *instance, struct ast_rtp_instance *bridged)
{
	instance->bridged = bridged;
}

void ast_rtp_instance_early_bridge_make_compatible(struct ast_channel *c_dst, struct ast_channel *c_src)
{
	struct ast_rtp_instance *instance_dst = NULL, *instance_src = NULL,
		*vinstance_dst = NULL, *vinstance_src = NULL,
		*tinstance_dst = NULL, *tinstance_src = NULL;
	struct ast_rtp_glue *glue_dst, *glue_src;
	enum ast_rtp_glue_result audio_glue_dst_res = AST_RTP_GLUE_RESULT_FORBID, video_glue_dst_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_rtp_glue_result audio_glue_src_res = AST_RTP_GLUE_RESULT_FORBID, video_glue_src_res = AST_RTP_GLUE_RESULT_FORBID;
	struct ast_format_cap *cap_dst = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	struct ast_format_cap *cap_src = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	/* Lock both channels so we can look for the glue that binds them together */
	ast_channel_lock_both(c_dst, c_src);

	if (!cap_src || !cap_dst) {
		goto done;
	}

	/* Grab glue that binds each channel to something using the RTP engine */
	if (!(glue_dst = ast_rtp_instance_get_glue(ast_channel_tech(c_dst)->type)) || !(glue_src = ast_rtp_instance_get_glue(ast_channel_tech(c_src)->type))) {
		ast_debug(1, "Can't find native functions for channel '%s'\n", glue_dst ? ast_channel_name(c_src) : ast_channel_name(c_dst));
		goto done;
	}

	audio_glue_dst_res = glue_dst->get_rtp_info(c_dst, &instance_dst);
	video_glue_dst_res = glue_dst->get_vrtp_info ? glue_dst->get_vrtp_info(c_dst, &vinstance_dst) : AST_RTP_GLUE_RESULT_FORBID;

	audio_glue_src_res = glue_src->get_rtp_info(c_src, &instance_src);
	video_glue_src_res = glue_src->get_vrtp_info ? glue_src->get_vrtp_info(c_src, &vinstance_src) : AST_RTP_GLUE_RESULT_FORBID;

	/* If we are carrying video, and both sides are not going to remotely bridge... fail the native bridge */
	if (video_glue_dst_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue_dst_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue_dst_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue_dst_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (video_glue_src_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue_src_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue_src_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue_src_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (audio_glue_dst_res == AST_RTP_GLUE_RESULT_REMOTE && (video_glue_dst_res == AST_RTP_GLUE_RESULT_FORBID || video_glue_dst_res == AST_RTP_GLUE_RESULT_REMOTE) && glue_dst->get_codec) {
		glue_dst->get_codec(c_dst, cap_dst);
	}
	if (audio_glue_src_res == AST_RTP_GLUE_RESULT_REMOTE && (video_glue_src_res == AST_RTP_GLUE_RESULT_FORBID || video_glue_src_res == AST_RTP_GLUE_RESULT_REMOTE) && glue_src->get_codec) {
		glue_src->get_codec(c_src, cap_src);
	}

	/* If any sort of bridge is forbidden just completely bail out and go back to generic bridging */
	if (audio_glue_dst_res != AST_RTP_GLUE_RESULT_REMOTE || audio_glue_src_res != AST_RTP_GLUE_RESULT_REMOTE) {
		goto done;
	}

	/* Make sure we have matching codecs */
	if (!ast_format_cap_iscompatible(cap_dst, cap_src)) {
		goto done;
	}

	ast_rtp_codecs_payloads_copy(&instance_src->codecs, &instance_dst->codecs, instance_dst);

	if (vinstance_dst && vinstance_src) {
		ast_rtp_codecs_payloads_copy(&vinstance_src->codecs, &vinstance_dst->codecs, vinstance_dst);
	}
	if (tinstance_dst && tinstance_src) {
		ast_rtp_codecs_payloads_copy(&tinstance_src->codecs, &tinstance_dst->codecs, tinstance_dst);
	}

	if (glue_dst->update_peer(c_dst, instance_src, vinstance_src, tinstance_src, cap_src, 0)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to setup early bridge to '%s'\n",
			ast_channel_name(c_dst), ast_channel_name(c_src));
	} else {
		ast_debug(1, "Seeded SDP of '%s' with that of '%s'\n",
			ast_channel_name(c_dst), ast_channel_name(c_src));
	}

done:
	ast_channel_unlock(c_dst);
	ast_channel_unlock(c_src);

	ao2_cleanup(cap_dst);
	ao2_cleanup(cap_src);

	unref_instance_cond(&instance_dst);
	unref_instance_cond(&instance_src);
	unref_instance_cond(&vinstance_dst);
	unref_instance_cond(&vinstance_src);
	unref_instance_cond(&tinstance_dst);
	unref_instance_cond(&tinstance_src);
}

int ast_rtp_instance_early_bridge(struct ast_channel *c0, struct ast_channel *c1)
{
	struct ast_rtp_instance *instance0 = NULL, *instance1 = NULL,
			*vinstance0 = NULL, *vinstance1 = NULL,
			*tinstance0 = NULL, *tinstance1 = NULL;
	struct ast_rtp_glue *glue0, *glue1;
	enum ast_rtp_glue_result audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID, video_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_rtp_glue_result audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID, video_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	struct ast_format_cap *cap0 = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	struct ast_format_cap *cap1 = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	/* If there is no second channel just immediately bail out, we are of no use in that scenario */
	if (!c1 || !cap1 || !cap0) {
		ao2_cleanup(cap0);
		ao2_cleanup(cap1);
		return -1;
	}

	/* Lock both channels so we can look for the glue that binds them together */
	ast_channel_lock_both(c0, c1);

	/* Grab glue that binds each channel to something using the RTP engine */
	if (!(glue0 = ast_rtp_instance_get_glue(ast_channel_tech(c0)->type)) || !(glue1 = ast_rtp_instance_get_glue(ast_channel_tech(c1)->type))) {
		ast_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", glue0 ? ast_channel_name(c1) : ast_channel_name(c0));
		goto done;
	}

	audio_glue0_res = glue0->get_rtp_info(c0, &instance0);
	video_glue0_res = glue0->get_vrtp_info ? glue0->get_vrtp_info(c0, &vinstance0) : AST_RTP_GLUE_RESULT_FORBID;

	audio_glue1_res = glue1->get_rtp_info(c1, &instance1);
	video_glue1_res = glue1->get_vrtp_info ? glue1->get_vrtp_info(c1, &vinstance1) : AST_RTP_GLUE_RESULT_FORBID;

	/* If we are carrying video, and both sides are not going to remotely bridge... fail the native bridge */
	if (video_glue0_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue0_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue0_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (video_glue1_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue1_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue1_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (audio_glue0_res == AST_RTP_GLUE_RESULT_REMOTE && (video_glue0_res == AST_RTP_GLUE_RESULT_FORBID || video_glue0_res == AST_RTP_GLUE_RESULT_REMOTE) && glue0->get_codec) {
		glue0->get_codec(c0, cap0);
	}
	if (audio_glue1_res == AST_RTP_GLUE_RESULT_REMOTE && (video_glue1_res == AST_RTP_GLUE_RESULT_FORBID || video_glue1_res == AST_RTP_GLUE_RESULT_REMOTE) && glue1->get_codec) {
		glue1->get_codec(c1, cap1);
	}

	/* If any sort of bridge is forbidden just completely bail out and go back to generic bridging */
	if (audio_glue0_res != AST_RTP_GLUE_RESULT_REMOTE || audio_glue1_res != AST_RTP_GLUE_RESULT_REMOTE) {
		goto done;
	}

	/* Make sure we have matching codecs */
	if (!ast_format_cap_iscompatible(cap0, cap1)) {
		goto done;
	}

	/* Bridge media early */
	if (glue0->update_peer(c0, instance1, vinstance1, tinstance1, cap1, 0)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to setup early bridge to '%s'\n", ast_channel_name(c0), c1 ? ast_channel_name(c1) : "<unspecified>");
	}

done:
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	ao2_cleanup(cap0);
	ao2_cleanup(cap1);

	unref_instance_cond(&instance0);
	unref_instance_cond(&instance1);
	unref_instance_cond(&vinstance0);
	unref_instance_cond(&vinstance1);
	unref_instance_cond(&tinstance0);
	unref_instance_cond(&tinstance1);

	ast_debug(1, "Setting early bridge SDP of '%s' with that of '%s'\n", ast_channel_name(c0), c1 ? ast_channel_name(c1) : "<unspecified>");

	return 0;
}

int ast_rtp_red_init(struct ast_rtp_instance *instance, int buffer_time, int *payloads, int generations)
{
	return instance->engine->red_init ? instance->engine->red_init(instance, buffer_time, payloads, generations) : -1;
}

int ast_rtp_red_buffer(struct ast_rtp_instance *instance, struct ast_frame *frame)
{
	return instance->engine->red_buffer ? instance->engine->red_buffer(instance, frame) : -1;
}

int ast_rtp_instance_get_stats(struct ast_rtp_instance *instance, struct ast_rtp_instance_stats *stats, enum ast_rtp_instance_stat stat)
{
	return instance->engine->get_stat ? instance->engine->get_stat(instance, stats, stat) : -1;
}

char *ast_rtp_instance_get_quality(struct ast_rtp_instance *instance, enum ast_rtp_instance_stat_field field, char *buf, size_t size)
{
	struct ast_rtp_instance_stats stats = { 0, };
	enum ast_rtp_instance_stat stat;

	/* Determine what statistics we will need to retrieve based on field passed in */
	if (field == AST_RTP_INSTANCE_STAT_FIELD_QUALITY) {
		stat = AST_RTP_INSTANCE_STAT_ALL;
	} else if (field == AST_RTP_INSTANCE_STAT_FIELD_QUALITY_JITTER) {
		stat = AST_RTP_INSTANCE_STAT_COMBINED_JITTER;
	} else if (field == AST_RTP_INSTANCE_STAT_FIELD_QUALITY_LOSS) {
		stat = AST_RTP_INSTANCE_STAT_COMBINED_LOSS;
	} else if (field == AST_RTP_INSTANCE_STAT_FIELD_QUALITY_RTT) {
		stat = AST_RTP_INSTANCE_STAT_COMBINED_RTT;
	} else {
		return NULL;
	}

	/* Attempt to actually retrieve the statistics we need to generate the quality string */
	if (ast_rtp_instance_get_stats(instance, &stats, stat)) {
		return NULL;
	}

	/* Now actually fill the buffer with the good information */
	if (field == AST_RTP_INSTANCE_STAT_FIELD_QUALITY) {
		snprintf(buf, size, "ssrc=%u;themssrc=%u;lp=%u;rxjitter=%f;rxcount=%u;txjitter=%f;txcount=%u;rlp=%u;rtt=%f",
			 stats.local_ssrc, stats.remote_ssrc, stats.rxploss, stats.rxjitter, stats.rxcount, stats.txjitter, stats.txcount, stats.txploss, stats.rtt);
	} else if (field == AST_RTP_INSTANCE_STAT_FIELD_QUALITY_JITTER) {
		snprintf(buf, size, "minrxjitter=%f;maxrxjitter=%f;avgrxjitter=%f;stdevrxjitter=%f;reported_minjitter=%f;reported_maxjitter=%f;reported_avgjitter=%f;reported_stdevjitter=%f;",
			 stats.local_minjitter, stats.local_maxjitter, stats.local_normdevjitter, sqrt(stats.local_stdevjitter), stats.remote_minjitter, stats.remote_maxjitter, stats.remote_normdevjitter, sqrt(stats.remote_stdevjitter));
	} else if (field == AST_RTP_INSTANCE_STAT_FIELD_QUALITY_LOSS) {
		snprintf(buf, size, "minrxlost=%f;maxrxlost=%f;avgrxlost=%f;stdevrxlost=%f;reported_minlost=%f;reported_maxlost=%f;reported_avglost=%f;reported_stdevlost=%f;",
			 stats.local_minrxploss, stats.local_maxrxploss, stats.local_normdevrxploss, sqrt(stats.local_stdevrxploss), stats.remote_minrxploss, stats.remote_maxrxploss, stats.remote_normdevrxploss, sqrt(stats.remote_stdevrxploss));
	} else if (field == AST_RTP_INSTANCE_STAT_FIELD_QUALITY_RTT) {
		snprintf(buf, size, "minrtt=%f;maxrtt=%f;avgrtt=%f;stdevrtt=%f;", stats.minrtt, stats.maxrtt, stats.normdevrtt, stats.stdevrtt);
	}

	return buf;
}

void ast_rtp_instance_set_stats_vars(struct ast_channel *chan, struct ast_rtp_instance *instance)
{
	char quality_buf[AST_MAX_USER_FIELD];
	char *quality;
	struct ast_channel *bridge = ast_channel_bridge_peer(chan);

	ast_channel_lock(chan);
	ast_channel_stage_snapshot(chan);
	ast_channel_unlock(chan);
	if (bridge) {
		ast_channel_lock(bridge);
		ast_channel_stage_snapshot(bridge);
		ast_channel_unlock(bridge);
	}

	quality = ast_rtp_instance_get_quality(instance, AST_RTP_INSTANCE_STAT_FIELD_QUALITY,
		quality_buf, sizeof(quality_buf));
	if (quality) {
		pbx_builtin_setvar_helper(chan, "RTPAUDIOQOS", quality);
		if (bridge) {
			pbx_builtin_setvar_helper(bridge, "RTPAUDIOQOSBRIDGED", quality);
		}
	}

	quality = ast_rtp_instance_get_quality(instance,
		AST_RTP_INSTANCE_STAT_FIELD_QUALITY_JITTER, quality_buf, sizeof(quality_buf));
	if (quality) {
		pbx_builtin_setvar_helper(chan, "RTPAUDIOQOSJITTER", quality);
		if (bridge) {
			pbx_builtin_setvar_helper(bridge, "RTPAUDIOQOSJITTERBRIDGED", quality);
		}
	}

	quality = ast_rtp_instance_get_quality(instance,
		AST_RTP_INSTANCE_STAT_FIELD_QUALITY_LOSS, quality_buf, sizeof(quality_buf));
	if (quality) {
		pbx_builtin_setvar_helper(chan, "RTPAUDIOQOSLOSS", quality);
		if (bridge) {
			pbx_builtin_setvar_helper(bridge, "RTPAUDIOQOSLOSSBRIDGED", quality);
		}
	}

	quality = ast_rtp_instance_get_quality(instance,
		AST_RTP_INSTANCE_STAT_FIELD_QUALITY_RTT, quality_buf, sizeof(quality_buf));
	if (quality) {
		pbx_builtin_setvar_helper(chan, "RTPAUDIOQOSRTT", quality);
		if (bridge) {
			pbx_builtin_setvar_helper(bridge, "RTPAUDIOQOSRTTBRIDGED", quality);
		}
	}

	ast_channel_lock(chan);
	ast_channel_stage_snapshot_done(chan);
	ast_channel_unlock(chan);
	if (bridge) {
		ast_channel_lock(bridge);
		ast_channel_stage_snapshot_done(bridge);
		ast_channel_unlock(bridge);
		ast_channel_unref(bridge);
	}
}

int ast_rtp_instance_set_read_format(struct ast_rtp_instance *instance, struct ast_format *format)
{
	return instance->engine->set_read_format ? instance->engine->set_read_format(instance, format) : -1;
}

int ast_rtp_instance_set_write_format(struct ast_rtp_instance *instance, struct ast_format *format)
{
	return instance->engine->set_write_format ? instance->engine->set_write_format(instance, format) : -1;
}

int ast_rtp_instance_make_compatible(struct ast_channel *chan, struct ast_rtp_instance *instance, struct ast_channel *peer)
{
	struct ast_rtp_glue *glue;
	struct ast_rtp_instance *peer_instance = NULL;
	int res = -1;

	if (!instance->engine->make_compatible) {
		return -1;
	}

	ast_channel_lock(peer);

	if (!(glue = ast_rtp_instance_get_glue(ast_channel_tech(peer)->type))) {
		ast_channel_unlock(peer);
		return -1;
	}

	glue->get_rtp_info(peer, &peer_instance);
	if (!peer_instance) {
		ast_log(LOG_ERROR, "Unable to get_rtp_info for peer type %s\n", glue->type);
		ast_channel_unlock(peer);
		return -1;
	}
	if (peer_instance->engine != instance->engine) {
		ast_log(LOG_ERROR, "Peer engine mismatch for type %s\n", glue->type);
		ast_channel_unlock(peer);
		ao2_ref(peer_instance, -1);
		return -1;
	}

	res = instance->engine->make_compatible(chan, instance, peer, peer_instance);

	ast_channel_unlock(peer);

	ao2_ref(peer_instance, -1);
	peer_instance = NULL;

	return res;
}

void ast_rtp_instance_available_formats(struct ast_rtp_instance *instance, struct ast_format_cap *to_endpoint, struct ast_format_cap *to_asterisk, struct ast_format_cap *result)
{
	if (instance->engine->available_formats) {
		instance->engine->available_formats(instance, to_endpoint, to_asterisk, result);
		if (ast_format_cap_count(result)) {
			return;
		}
	}

	ast_translate_available_formats(to_endpoint, to_asterisk, result);
}

int ast_rtp_instance_activate(struct ast_rtp_instance *instance)
{
	return instance->engine->activate ? instance->engine->activate(instance) : 0;
}

void ast_rtp_instance_stun_request(struct ast_rtp_instance *instance,
				   struct ast_sockaddr *suggestion,
				   const char *username)
{
	if (instance->engine->stun_request) {
		instance->engine->stun_request(instance, suggestion, username);
	}
}

void ast_rtp_instance_set_timeout(struct ast_rtp_instance *instance, int timeout)
{
	instance->timeout = timeout;
}

void ast_rtp_instance_set_hold_timeout(struct ast_rtp_instance *instance, int timeout)
{
	instance->holdtimeout = timeout;
}

void ast_rtp_instance_set_keepalive(struct ast_rtp_instance *instance, int interval)
{
	instance->keepalive = interval;
}

int ast_rtp_instance_get_timeout(struct ast_rtp_instance *instance)
{
	return instance->timeout;
}

int ast_rtp_instance_get_hold_timeout(struct ast_rtp_instance *instance)
{
	return instance->holdtimeout;
}

int ast_rtp_instance_get_keepalive(struct ast_rtp_instance *instance)
{
	return instance->keepalive;
}

struct ast_rtp_engine *ast_rtp_instance_get_engine(struct ast_rtp_instance *instance)
{
	return instance->engine;
}

struct ast_rtp_glue *ast_rtp_instance_get_active_glue(struct ast_rtp_instance *instance)
{
	return instance->glue;
}

int ast_rtp_engine_register_srtp(struct ast_srtp_res *srtp_res, struct ast_srtp_policy_res *policy_res)
{
	if (res_srtp || res_srtp_policy) {
		return -1;
	}
	if (!srtp_res || !policy_res) {
		return -1;
	}

	res_srtp = srtp_res;
	res_srtp_policy = policy_res;

	return 0;
}

void ast_rtp_engine_unregister_srtp(void)
{
	res_srtp = NULL;
	res_srtp_policy = NULL;
}

int ast_rtp_engine_srtp_is_registered(void)
{
	return res_srtp && res_srtp_policy;
}

int ast_rtp_instance_add_srtp_policy(struct ast_rtp_instance *instance, struct ast_srtp_policy *remote_policy, struct ast_srtp_policy *local_policy)
{
	int res = 0;

	if (!res_srtp) {
		return -1;
	}

	if (!instance->srtp) {
		res = res_srtp->create(&instance->srtp, instance, remote_policy);
	} else {
		res = res_srtp->replace(&instance->srtp, instance, remote_policy);
	}
	if (!res) {
		res = res_srtp->add_stream(instance->srtp, local_policy);
	}

	return res;
}

struct ast_srtp *ast_rtp_instance_get_srtp(struct ast_rtp_instance *instance)
{
	return instance->srtp;
}

int ast_rtp_instance_sendcng(struct ast_rtp_instance *instance, int level)
{
	if (instance->engine->sendcng) {
		return instance->engine->sendcng(instance, level);
	}

	return -1;
}

struct ast_rtp_engine_ice *ast_rtp_instance_get_ice(struct ast_rtp_instance *instance)
{
	return instance->engine->ice;
}

struct ast_rtp_engine_dtls *ast_rtp_instance_get_dtls(struct ast_rtp_instance *instance)
{
	return instance->engine->dtls;
}

int ast_rtp_dtls_cfg_parse(struct ast_rtp_dtls_cfg *dtls_cfg, const char *name, const char *value)
{
	if (!strcasecmp(name, "dtlsenable")) {
		dtls_cfg->enabled = ast_true(value) ? 1 : 0;
	} else if (!strcasecmp(name, "dtlsverify")) {
		if (!strcasecmp(value, "yes")) {
			dtls_cfg->verify = AST_RTP_DTLS_VERIFY_FINGERPRINT | AST_RTP_DTLS_VERIFY_CERTIFICATE;
		} else if (!strcasecmp(value, "fingerprint")) {
			dtls_cfg->verify = AST_RTP_DTLS_VERIFY_FINGERPRINT;
		} else if (!strcasecmp(value, "certificate")) {
			dtls_cfg->verify = AST_RTP_DTLS_VERIFY_CERTIFICATE;
		} else if (!strcasecmp(value, "no")) {
			dtls_cfg->verify = AST_RTP_DTLS_VERIFY_NONE;
		} else {
			return -1;
		}
	} else if (!strcasecmp(name, "dtlsrekey")) {
		if (sscanf(value, "%30u", &dtls_cfg->rekey) != 1) {
			return -1;
		}
	} else if (!strcasecmp(name, "dtlscertfile")) {
		ast_free(dtls_cfg->certfile);
		dtls_cfg->certfile = ast_strdup(value);
	} else if (!strcasecmp(name, "dtlsprivatekey")) {
		ast_free(dtls_cfg->pvtfile);
		dtls_cfg->pvtfile = ast_strdup(value);
	} else if (!strcasecmp(name, "dtlscipher")) {
		ast_free(dtls_cfg->cipher);
		dtls_cfg->cipher = ast_strdup(value);
	} else if (!strcasecmp(name, "dtlscafile")) {
		ast_free(dtls_cfg->cafile);
		dtls_cfg->cafile = ast_strdup(value);
	} else if (!strcasecmp(name, "dtlscapath") || !strcasecmp(name, "dtlscadir")) {
		ast_free(dtls_cfg->capath);
		dtls_cfg->capath = ast_strdup(value);
	} else if (!strcasecmp(name, "dtlssetup")) {
		if (!strcasecmp(value, "active")) {
			dtls_cfg->default_setup = AST_RTP_DTLS_SETUP_ACTIVE;
		} else if (!strcasecmp(value, "passive")) {
			dtls_cfg->default_setup = AST_RTP_DTLS_SETUP_PASSIVE;
		} else if (!strcasecmp(value, "actpass")) {
			dtls_cfg->default_setup = AST_RTP_DTLS_SETUP_ACTPASS;
		}
	} else if (!strcasecmp(name, "dtlsfingerprint")) {
		if (!strcasecmp(value, "sha-256")) {
			dtls_cfg->hash = AST_RTP_DTLS_HASH_SHA256;
		} else if (!strcasecmp(value, "sha-1")) {
			dtls_cfg->hash = AST_RTP_DTLS_HASH_SHA1;
		}
	} else {
		return -1;
	}

	return 0;
}

void ast_rtp_dtls_cfg_copy(const struct ast_rtp_dtls_cfg *src_cfg, struct ast_rtp_dtls_cfg *dst_cfg)
{
	ast_rtp_dtls_cfg_free(dst_cfg);         /* Prevent a double-call leaking memory via ast_strdup */

	dst_cfg->enabled = src_cfg->enabled;
	dst_cfg->verify = src_cfg->verify;
	dst_cfg->rekey = src_cfg->rekey;
	dst_cfg->suite = src_cfg->suite;
	dst_cfg->hash = src_cfg->hash;
	dst_cfg->certfile = ast_strdup(src_cfg->certfile);
	dst_cfg->pvtfile = ast_strdup(src_cfg->pvtfile);
	dst_cfg->cipher = ast_strdup(src_cfg->cipher);
	dst_cfg->cafile = ast_strdup(src_cfg->cafile);
	dst_cfg->capath = ast_strdup(src_cfg->capath);
	dst_cfg->default_setup = src_cfg->default_setup;
}

void ast_rtp_dtls_cfg_free(struct ast_rtp_dtls_cfg *dtls_cfg)
{
	ast_free(dtls_cfg->certfile);
	dtls_cfg->certfile = NULL;
	ast_free(dtls_cfg->pvtfile);
	dtls_cfg->pvtfile = NULL;
	ast_free(dtls_cfg->cipher);
	dtls_cfg->cipher = NULL;
	ast_free(dtls_cfg->cafile);
	dtls_cfg->cafile = NULL;
	ast_free(dtls_cfg->capath);
	dtls_cfg->capath = NULL;
}

/*! \internal
 * \brief Small helper routine that cleans up entry i in
 * \c static_RTP_PT.
 */
static void rtp_engine_static_RTP_PT_cleanup(int i)
{
	ao2_cleanup(static_RTP_PT[i].format);
	memset(&static_RTP_PT[i], 0, sizeof(struct ast_rtp_payload_type));
}

/*! \internal
 * \brief Small helper routine that cleans up entry i in
 * \c ast_rtp_mime_types.
 */
static void rtp_engine_mime_type_cleanup(int i)
{
	ao2_cleanup(ast_rtp_mime_types[i].payload_type.format);
	memset(&ast_rtp_mime_types[i], 0, sizeof(struct ast_rtp_mime_type));
}

static void set_next_mime_type(struct ast_format *format, int rtp_code, const char *type, const char *subtype, unsigned int sample_rate)
{
	int x = mime_types_len;
	if (ARRAY_LEN(ast_rtp_mime_types) == mime_types_len) {
		return;
	}

	ast_rwlock_wrlock(&mime_types_lock);
	/* Make sure any previous value in ast_rtp_mime_types is cleaned up */
	memset(&ast_rtp_mime_types[x], 0, sizeof(struct ast_rtp_mime_type));	
	if (format) {
		ast_rtp_mime_types[x].payload_type.asterisk_format = 1;
		ast_rtp_mime_types[x].payload_type.format = ao2_bump(format);
	} else {
		ast_rtp_mime_types[x].payload_type.rtp_code = rtp_code;
	}
	ast_copy_string(ast_rtp_mime_types[x].type, type, sizeof(ast_rtp_mime_types[x].type));
	ast_copy_string(ast_rtp_mime_types[x].subtype, subtype, sizeof(ast_rtp_mime_types[x].subtype));
	ast_rtp_mime_types[x].sample_rate = sample_rate;
	mime_types_len++;
	ast_rwlock_unlock(&mime_types_lock);
}

static void add_static_payload(int map, struct ast_format *format, int rtp_code)
{
	int x;
	ast_rwlock_wrlock(&static_RTP_PT_lock);
	if (map < 0) {
		/* find next available dynamic payload slot */
		for (x = 96; x < 127; x++) {
			if (!static_RTP_PT[x].asterisk_format && !static_RTP_PT[x].rtp_code) {
				map = x;
				break;
			}
		}
	}

	if (map < 0) {
		ast_log(LOG_WARNING, "No Dynamic RTP mapping available for format %s\n",
			ast_format_get_name(format));
		ast_rwlock_unlock(&static_RTP_PT_lock);
		return;
	}

	if (format) {
		static_RTP_PT[map].asterisk_format = 1;
		static_RTP_PT[map].format = ao2_bump(format);
	} else {
		static_RTP_PT[map].rtp_code = rtp_code;
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);
}

int ast_rtp_engine_load_format(struct ast_format *format)
{
	char *codec_name = ast_strdupa(ast_format_get_name(format));

	codec_name = ast_str_to_upper(codec_name);

	set_next_mime_type(format,
		0,
		ast_codec_media_type2str(ast_format_get_type(format)),
		codec_name,
		ast_format_get_sample_rate(format));
	add_static_payload(-1, format, 0);

	return 0;
}

int ast_rtp_engine_unload_format(struct ast_format *format)
{
	int x;
	int y = 0;

	ast_rwlock_wrlock(&static_RTP_PT_lock);
	/* remove everything pertaining to this format id from the lists */
	for (x = 0; x < AST_RTP_MAX_PT; x++) {
		if (ast_format_cmp(static_RTP_PT[x].format, format) == AST_FORMAT_CMP_EQUAL) {
			rtp_engine_static_RTP_PT_cleanup(x);
		}
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);

	ast_rwlock_wrlock(&mime_types_lock);
	/* rebuild the list skipping the items matching this id */
	for (x = 0; x < mime_types_len; x++) {
		if (ast_format_cmp(ast_rtp_mime_types[x].payload_type.format, format) == AST_FORMAT_CMP_EQUAL) {
			rtp_engine_mime_type_cleanup(x);
			continue;
		}
		if (x != y) {
			ast_rtp_mime_types[y] = ast_rtp_mime_types[x];
		}
		y++;
	}
	mime_types_len = y;
	ast_rwlock_unlock(&mime_types_lock);
	return 0;
}

/*!
 * \internal
 * \brief \ref stasis message payload for RTCP messages
 */
struct rtcp_message_payload {
	struct ast_channel_snapshot *snapshot;  /*< The channel snapshot, if available */
	struct ast_rtp_rtcp_report *report;     /*< The RTCP report */
	struct ast_json *blob;                  /*< Extra JSON data to publish */
};

static void rtcp_message_payload_dtor(void *obj)
{
	struct rtcp_message_payload *payload = obj;

	ao2_cleanup(payload->report);
	ao2_cleanup(payload->snapshot);
	ast_json_unref(payload->blob);
}

static struct ast_manager_event_blob *rtcp_report_to_ami(struct stasis_message *msg)
{
	struct rtcp_message_payload *payload = stasis_message_data(msg);
	RAII_VAR(struct ast_str *, channel_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, packet_string, ast_str_create(512), ast_free);
	unsigned int ssrc = payload->report->ssrc;
	unsigned int type = payload->report->type;
	unsigned int report_count = payload->report->reception_report_count;
	int i;

	if (!packet_string) {
		return NULL;
	}

	if (payload->snapshot) {
		channel_string = ast_manager_build_channel_state_string(payload->snapshot);
		if (!channel_string) {
			return NULL;
		}
	}

	if (payload->blob) {
		/* Optional data */
		struct ast_json *to = ast_json_object_get(payload->blob, "to");
		struct ast_json *from = ast_json_object_get(payload->blob, "from");
		struct ast_json *rtt = ast_json_object_get(payload->blob, "rtt");
		if (to) {
			ast_str_append(&packet_string, 0, "To: %s\r\n", ast_json_string_get(to));
		}
		if (from) {
			ast_str_append(&packet_string, 0, "From: %s\r\n", ast_json_string_get(from));
		}
		if (rtt) {
			ast_str_append(&packet_string, 0, "RTT: %4.4f\r\n", ast_json_real_get(rtt));
		}
	}

	ast_str_append(&packet_string, 0, "SSRC: 0x%.8x\r\n", ssrc);
	ast_str_append(&packet_string, 0, "PT: %u(%s)\r\n", type, type== AST_RTP_RTCP_SR ? "SR" : "RR");
	ast_str_append(&packet_string, 0, "ReportCount: %u\r\n", report_count);
	if (type == AST_RTP_RTCP_SR) {
		ast_str_append(&packet_string, 0, "SentNTP: %lu.%06lu\r\n",
			(unsigned long)payload->report->sender_information.ntp_timestamp.tv_sec,
			(unsigned long)payload->report->sender_information.ntp_timestamp.tv_usec * 4096);
		ast_str_append(&packet_string, 0, "SentRTP: %u\r\n",
				payload->report->sender_information.rtp_timestamp);
		ast_str_append(&packet_string, 0, "SentPackets: %u\r\n",
				payload->report->sender_information.packet_count);
		ast_str_append(&packet_string, 0, "SentOctets: %u\r\n",
				payload->report->sender_information.octet_count);
	}

	for (i = 0; i < report_count; i++) {
		RAII_VAR(struct ast_str *, report_string, NULL, ast_free);

		if (!payload->report->report_block[i]) {
			break;
		}

		report_string = ast_str_create(256);
		if (!report_string) {
			return NULL;
		}

		ast_str_append(&report_string, 0, "Report%dSourceSSRC: 0x%.8x\r\n",
				i, payload->report->report_block[i]->source_ssrc);
		ast_str_append(&report_string, 0, "Report%dFractionLost: %d\r\n",
				i, payload->report->report_block[i]->lost_count.fraction);
		ast_str_append(&report_string, 0, "Report%dCumulativeLost: %u\r\n",
				i, payload->report->report_block[i]->lost_count.packets);
		ast_str_append(&report_string, 0, "Report%dHighestSequence: %u\r\n",
				i, payload->report->report_block[i]->highest_seq_no & 0xffff);
		ast_str_append(&report_string, 0, "Report%dSequenceNumberCycles: %u\r\n",
				i, payload->report->report_block[i]->highest_seq_no >> 16);
		ast_str_append(&report_string, 0, "Report%dIAJitter: %u\r\n",
				i, payload->report->report_block[i]->ia_jitter);
		ast_str_append(&report_string, 0, "Report%dLSR: %u\r\n",
				i, payload->report->report_block[i]->lsr);
		ast_str_append(&report_string, 0, "Report%dDLSR: %4.4f\r\n",
				i, ((double)payload->report->report_block[i]->dlsr) / 65536);
		ast_str_append(&packet_string, 0, "%s", ast_str_buffer(report_string));
	}

	return ast_manager_event_blob_create(EVENT_FLAG_REPORTING,
		stasis_message_type(msg) == ast_rtp_rtcp_received_type() ? "RTCPReceived" : "RTCPSent",
		"%s%s",
		AS_OR(channel_string, ""),
		ast_str_buffer(packet_string));
}

static struct ast_json *rtcp_report_to_json(struct stasis_message *msg,
	const struct stasis_message_sanitizer *sanitize)
{
	struct rtcp_message_payload *payload = stasis_message_data(msg);
	RAII_VAR(struct ast_json *, json_rtcp_report, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, json_rtcp_report_blocks, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, json_rtcp_sender_info, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, json_channel, NULL, ast_json_unref);
	int i;

	json_rtcp_report_blocks = ast_json_array_create();
	if (!json_rtcp_report_blocks) {
		return NULL;
	}

	for (i = 0; i < payload->report->reception_report_count && payload->report->report_block[i]; i++) {
		struct ast_json *json_report_block;
		char str_lsr[32];
		snprintf(str_lsr, sizeof(str_lsr), "%u", payload->report->report_block[i]->lsr);
		json_report_block = ast_json_pack("{s: i, s: i, s: i, s: i, s: i, s: s, s: i}",
				"source_ssrc", payload->report->report_block[i]->source_ssrc,
				"fraction_lost", payload->report->report_block[i]->lost_count.fraction,
				"packets_lost", payload->report->report_block[i]->lost_count.packets,
				"highest_seq_no", payload->report->report_block[i]->highest_seq_no,
				"ia_jitter", payload->report->report_block[i]->ia_jitter,
				"lsr", str_lsr,
				"dlsr", payload->report->report_block[i]->dlsr);
		if (!json_report_block) {
			return NULL;
		}

		if (ast_json_array_append(json_rtcp_report_blocks, json_report_block)) {
			return NULL;
		}
	}

	if (payload->report->type == AST_RTP_RTCP_SR) {
		char sec[32];
		char usec[32];
		snprintf(sec, sizeof(sec), "%lu", (unsigned long)payload->report->sender_information.ntp_timestamp.tv_sec);
		snprintf(usec, sizeof(usec), "%lu", (unsigned long)payload->report->sender_information.ntp_timestamp.tv_usec);
		json_rtcp_sender_info = ast_json_pack("{s: s, s: s, s: i, s: i, s: i}",
				"ntp_timestamp_sec", sec,
				"ntp_timestamp_usec", usec,
				"rtp_timestamp", payload->report->sender_information.rtp_timestamp,
				"packets", payload->report->sender_information.packet_count,
				"octets", payload->report->sender_information.octet_count);
		if (!json_rtcp_sender_info) {
			return NULL;
		}
	}

	json_rtcp_report = ast_json_pack("{s: i, s: i, s: i, s: O, s: O}",
			"ssrc", payload->report->ssrc,
			"type", payload->report->type,
			"report_count", payload->report->reception_report_count,
			"sender_information", json_rtcp_sender_info ? json_rtcp_sender_info : ast_json_null(),
			"report_blocks", json_rtcp_report_blocks);
	if (!json_rtcp_report) {
		return NULL;
	}

	if (payload->snapshot) {
		json_channel = ast_channel_snapshot_to_json(payload->snapshot, sanitize);
		if (!json_channel) {
			return NULL;
		}
	}

	return ast_json_pack("{s: O, s: O, s: O}",
		"channel", payload->snapshot ? json_channel : ast_json_null(),
		"rtcp_report", json_rtcp_report,
		"blob", payload->blob);
}

static void rtp_rtcp_report_dtor(void *obj)
{
	int i;
	struct ast_rtp_rtcp_report *rtcp_report = obj;

	for (i = 0; i < rtcp_report->reception_report_count; i++) {
		ast_free(rtcp_report->report_block[i]);
	}
}

struct ast_rtp_rtcp_report *ast_rtp_rtcp_report_alloc(unsigned int report_blocks)
{
	struct ast_rtp_rtcp_report *rtcp_report;

	/* Size of object is sizeof the report + the number of report_blocks * sizeof pointer */
	rtcp_report = ao2_alloc((sizeof(*rtcp_report) + report_blocks * sizeof(struct ast_rtp_rtcp_report_block *)),
		rtp_rtcp_report_dtor);

	return rtcp_report;
}

void ast_rtp_publish_rtcp_message(struct ast_rtp_instance *rtp,
		struct stasis_message_type *message_type,
		struct ast_rtp_rtcp_report *report,
		struct ast_json *blob)
{
	RAII_VAR(struct rtcp_message_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, message, NULL, ao2_cleanup);

	if (!message_type) {
		return;
	}

	payload = ao2_alloc(sizeof(*payload), rtcp_message_payload_dtor);
	if (!payload || !report) {
		return;
	}

	if (!ast_strlen_zero(rtp->channel_uniqueid)) {
		payload->snapshot = ast_channel_snapshot_get_latest(rtp->channel_uniqueid);
	}
	if (blob) {
		payload->blob = blob;
		ast_json_ref(blob);
	}
	ao2_ref(report, +1);
	payload->report = report;

	message = stasis_message_create(message_type, payload);
	if (!message) {
		return;
	}

	stasis_publish(ast_rtp_topic(), message);
}

/*!
 * @{ \brief Define RTCP/RTP message types.
 */
STASIS_MESSAGE_TYPE_DEFN(ast_rtp_rtcp_sent_type,
		.to_ami = rtcp_report_to_ami,
		.to_json = rtcp_report_to_json,);
STASIS_MESSAGE_TYPE_DEFN(ast_rtp_rtcp_received_type,
		.to_ami = rtcp_report_to_ami,
		.to_json = rtcp_report_to_json,);
/*! @} */

struct stasis_topic *ast_rtp_topic(void)
{
	return rtp_topic;
}

static void rtp_engine_shutdown(void)
{
	int x;

	ao2_cleanup(rtp_topic);
	rtp_topic = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(ast_rtp_rtcp_received_type);
	STASIS_MESSAGE_TYPE_CLEANUP(ast_rtp_rtcp_sent_type);

	ast_rwlock_wrlock(&static_RTP_PT_lock);
	for (x = 0; x < AST_RTP_MAX_PT; x++) {
		if (static_RTP_PT[x].format) {
			rtp_engine_static_RTP_PT_cleanup(x);
		}
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);

	ast_rwlock_wrlock(&mime_types_lock);
	for (x = 0; x < mime_types_len; x++) {
		if (ast_rtp_mime_types[x].payload_type.format) {
			rtp_engine_mime_type_cleanup(x);
		}
	}
	ast_rwlock_unlock(&mime_types_lock);
}

int ast_rtp_engine_init()
{
	ast_rwlock_init(&mime_types_lock);
	ast_rwlock_init(&static_RTP_PT_lock);

	rtp_topic = stasis_topic_create("rtp_topic");
	if (!rtp_topic) {
		return -1;
	}
	STASIS_MESSAGE_TYPE_INIT(ast_rtp_rtcp_sent_type);
	STASIS_MESSAGE_TYPE_INIT(ast_rtp_rtcp_received_type);
	ast_register_cleanup(rtp_engine_shutdown);

	/* Define all the RTP mime types available */
	set_next_mime_type(ast_format_g723, 0, "audio", "G723", 8000);
	set_next_mime_type(ast_format_gsm, 0, "audio", "GSM", 8000);
	set_next_mime_type(ast_format_ulaw, 0, "audio", "PCMU", 8000);
	set_next_mime_type(ast_format_ulaw, 0, "audio", "G711U", 8000);
	set_next_mime_type(ast_format_alaw, 0, "audio", "PCMA", 8000);
	set_next_mime_type(ast_format_alaw, 0, "audio", "G711A", 8000);
	set_next_mime_type(ast_format_g726, 0, "audio", "G726-32", 8000);
	set_next_mime_type(ast_format_adpcm, 0, "audio", "DVI4", 8000);
	set_next_mime_type(ast_format_slin, 0, "audio", "L16", 8000);
	set_next_mime_type(ast_format_slin16, 0, "audio", "L16", 16000);
	set_next_mime_type(ast_format_slin16, 0, "audio", "L16-256", 16000);
	set_next_mime_type(ast_format_lpc10, 0, "audio", "LPC", 8000);
	set_next_mime_type(ast_format_g729, 0, "audio", "G729", 8000);
	set_next_mime_type(ast_format_g729, 0, "audio", "G729A", 8000);
	set_next_mime_type(ast_format_g729, 0, "audio", "G.729", 8000);
	set_next_mime_type(ast_format_speex, 0, "audio", "speex", 8000);
	set_next_mime_type(ast_format_speex16, 0,  "audio", "speex", 16000);
	set_next_mime_type(ast_format_speex32, 0,  "audio", "speex", 32000);
	set_next_mime_type(ast_format_ilbc, 0, "audio", "iLBC", 8000);
	/* this is the sample rate listed in the RTP profile for the G.722 codec, *NOT* the actual sample rate of the media stream */
	set_next_mime_type(ast_format_g722, 0, "audio", "G722", 8000);
	set_next_mime_type(ast_format_g726_aal2, 0, "audio", "AAL2-G726-32", 8000);
	set_next_mime_type(NULL, AST_RTP_DTMF, "audio", "telephone-event", 8000);
	set_next_mime_type(NULL, AST_RTP_CISCO_DTMF, "audio", "cisco-telephone-event", 8000);
	set_next_mime_type(NULL, AST_RTP_CN, "audio", "CN", 8000);
	set_next_mime_type(ast_format_jpeg, 0, "video", "JPEG", 90000);
	set_next_mime_type(ast_format_png, 0, "video", "PNG", 90000);
	set_next_mime_type(ast_format_h261, 0, "video", "H261", 90000);
	set_next_mime_type(ast_format_h263, 0, "video", "H263", 90000);
	set_next_mime_type(ast_format_h263p, 0, "video", "h263-1998", 90000);
	set_next_mime_type(ast_format_h264, 0, "video", "H264", 90000);
	set_next_mime_type(ast_format_mp4, 0, "video", "MP4V-ES", 90000);
	set_next_mime_type(ast_format_t140_red, 0, "text", "RED", 1000);
	set_next_mime_type(ast_format_t140, 0, "text", "T140", 1000);
	set_next_mime_type(ast_format_siren7, 0, "audio", "G7221", 16000);
	set_next_mime_type(ast_format_siren14, 0, "audio", "G7221", 32000);
	set_next_mime_type(ast_format_g719, 0, "audio", "G719", 48000);
	/* Opus and VP8 */
	set_next_mime_type(ast_format_opus, 0,  "audio", "opus", 48000);
	set_next_mime_type(ast_format_vp8, 0,  "video", "VP8", 90000);

	/* Define the static rtp payload mappings */
	add_static_payload(0, ast_format_ulaw, 0);
	#ifdef USE_DEPRECATED_G726
	add_static_payload(2, ast_format_g726, 0);/* Technically this is G.721, but if Cisco can do it, so can we... */
	#endif
	add_static_payload(3, ast_format_gsm, 0);
	add_static_payload(4, ast_format_g723, 0);
	add_static_payload(5, ast_format_adpcm, 0);/* 8 kHz */
	add_static_payload(6, ast_format_adpcm, 0); /* 16 kHz */
	add_static_payload(7, ast_format_lpc10, 0);
	add_static_payload(8, ast_format_alaw, 0);
	add_static_payload(9, ast_format_g722, 0);
	add_static_payload(10, ast_format_slin, 0); /* 2 channels */
	add_static_payload(11, ast_format_slin, 0); /* 1 channel */
	add_static_payload(13, NULL, AST_RTP_CN);
	add_static_payload(16, ast_format_adpcm, 0); /* 11.025 kHz */
	add_static_payload(17, ast_format_adpcm, 0); /* 22.050 kHz */
	add_static_payload(18, ast_format_g729, 0);
	add_static_payload(19, NULL, AST_RTP_CN);         /* Also used for CN */
	add_static_payload(26, ast_format_jpeg, 0);
	add_static_payload(31, ast_format_h261, 0);
	add_static_payload(34, ast_format_h263, 0);
	add_static_payload(97, ast_format_ilbc, 0);
	add_static_payload(98, ast_format_h263p, 0);
	add_static_payload(99, ast_format_h264, 0);
	add_static_payload(101, NULL, AST_RTP_DTMF);
	add_static_payload(102, ast_format_siren7, 0);
	add_static_payload(103, ast_format_h263p, 0);
	add_static_payload(104, ast_format_mp4, 0);
	add_static_payload(105, ast_format_t140_red, 0);   /* Real time text chat (with redundancy encoding) */
	add_static_payload(106, ast_format_t140, 0);     /* Real time text chat */
	add_static_payload(110, ast_format_speex, 0);
	add_static_payload(111, ast_format_g726, 0);
	add_static_payload(112, ast_format_g726_aal2, 0);
	add_static_payload(115, ast_format_siren14, 0);
	add_static_payload(116, ast_format_g719, 0);
	add_static_payload(117, ast_format_speex16, 0);
	add_static_payload(118, ast_format_slin16, 0); /* 16 Khz signed linear */
	add_static_payload(119, ast_format_speex32, 0);
	add_static_payload(121, NULL, AST_RTP_CISCO_DTMF);   /* Must be type 121 */
	/* Opus and VP8 */
	add_static_payload(100, ast_format_vp8, 0);
	add_static_payload(107, ast_format_opus, 0);

	return 0;
}
