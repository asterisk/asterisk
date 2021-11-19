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
			<see-also>
				<ref type="managerEvent">RTCPReceived</ref>
			</see-also>
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
			<see-also>
				<ref type="managerEvent">RTCPSent</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
 ***/

#include "asterisk.h"

#include <math.h>                       /* for sqrt, MAX */
#include <sched.h>                      /* for sched_yield */
#include <sys/time.h>                   /* for timeval */
#include <time.h>                       /* for time_t */

#include "asterisk/_private.h"          /* for ast_rtp_engine_init prototype */
#include "asterisk/astobj2.h"           /* for ao2_cleanup, ao2_ref, etc */
#include "asterisk/channel.h"           /* for ast_channel_name, etc */
#include "asterisk/codec.h"             /* for ast_codec_media_type2str, etc */
#include "asterisk/format.h"            /* for ast_format_cmp, etc */
#include "asterisk/format_cache.h"      /* for ast_format_adpcm, etc */
#include "asterisk/format_cap.h"        /* for ast_format_cap_alloc, etc */
#include "asterisk/json.h"              /* for ast_json_ref, etc */
#include "asterisk/linkedlists.h"       /* for ast_rtp_engine::<anonymous>, etc */
#include "asterisk/lock.h"              /* for ast_rwlock_unlock, etc */
#include "asterisk/logger.h"            /* for ast_log, ast_debug, etc */
#include "asterisk/manager.h"
#include "asterisk/module.h"            /* for ast_module_unref, etc */
#include "asterisk/netsock2.h"          /* for ast_sockaddr_copy, etc */
#include "asterisk/options.h"           /* for ast_option_rtpptdynamic */
#include "asterisk/pbx.h"               /* for pbx_builtin_setvar_helper */
#include "asterisk/res_srtp.h"          /* for ast_srtp_res */
#include "asterisk/rtp_engine.h"        /* for ast_rtp_codecs, etc */
#include "asterisk/stasis.h"            /* for stasis_message_data, etc */
#include "asterisk/stasis_channels.h"   /* for ast_channel_stage_snapshot, etc */
#include "asterisk/strings.h"           /* for ast_str_append, etc */
#include "asterisk/time.h"              /* for ast_tvdiff_ms, ast_tvnow */
#include "asterisk/translate.h"         /* for ast_translate_available_formats */
#include "asterisk/utils.h"             /* for ast_free, ast_strdup, etc */
#include "asterisk/vector.h"            /* for AST_VECTOR_GET, etc */

struct ast_srtp_res *res_srtp = NULL;
struct ast_srtp_policy_res *res_srtp_policy = NULL;

/*! Structure that contains extmap negotiation information */
struct rtp_extmap {
	/*! The RTP extension */
	enum ast_rtp_extension extension;
	/*! The current negotiated direction */
	enum ast_rtp_extension_direction direction;
};

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
	/*! SRTP info dedicated for RTCP associated with the instance */
	struct ast_srtp *rtcp_srtp;
	/*! Channel unique ID */
	char channel_uniqueid[AST_MAX_UNIQUEID];
	/*! Time of last packet sent */
	time_t last_tx;
	/*! Time of last packet received */
	time_t last_rx;
	/*! Enabled RTP extensions */
	AST_VECTOR(, enum ast_rtp_extension_direction) extmap_enabled;
	/*! Negotiated RTP extensions (using index based on extension) */
	AST_VECTOR(, int) extmap_negotiated;
	/*! Negotiated RTP extensions (using index based on unique id) */
	AST_VECTOR(, struct rtp_extmap) extmap_unique_ids;
};

/*!
 * \brief URIs for known RTP extensions
 */
static const char * const rtp_extension_uris[AST_RTP_EXTENSION_MAX] = {
	[AST_RTP_EXTENSION_UNSUPPORTED]		= "",
	[AST_RTP_EXTENSION_ABS_SEND_TIME]	= "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time",
	[AST_RTP_EXTENSION_TRANSPORT_WIDE_CC]	= "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01",
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
static struct ast_rtp_payload_type *static_RTP_PT[AST_RTP_MAX_PT];
static ast_rwlock_t static_RTP_PT_lock;

/*! \brief \ref stasis topic for RTP related messages */
static struct stasis_topic *rtp_topic;


/*!
 * \brief Set given json object into target with name
 *
 * \param target Target json.
 * \param name key of given object.
 * \param obj Json value will be set.
 */
#define SET_AST_JSON_OBJ(target, name, obj) ({					\
	struct ast_json *j_tmp = obj;						\
	if (j_tmp) {						\
		ast_json_object_set(target, name, j_tmp);						\
	}						\
})

/*!
 * \internal
 * \brief Destructor for \c ast_rtp_payload_type
 */
static void rtp_payload_type_dtor(void *obj)
{
	struct ast_rtp_payload_type *payload = obj;

	ao2_cleanup(payload->format);
}

static struct ast_rtp_payload_type *rtp_payload_type_alloc(struct ast_format *format,
	int payload, int rtp_code, int primary_mapping)
{
	struct ast_rtp_payload_type *type = ao2_alloc_options(
		sizeof(*type), rtp_payload_type_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);

	if (!type) {
		return NULL;
	}

	type->format = ao2_bump(format);
	type->asterisk_format = type->format != NULL;
	type->payload = payload;
	type->rtp_code = rtp_code;
	type->primary_mapping = primary_mapping;

	return type;
}

struct ast_rtp_payload_type *ast_rtp_engine_alloc_payload_type(void)
{
	return rtp_payload_type_alloc(NULL, 0, 0, 0);
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
	if (instance->data) {
		/*
		 * Lock in case the RTP engine has other threads that
		 * need synchronization with the destruction.
		 */
		ao2_lock(instance);
		instance->engine->destroy(instance);
		ao2_unlock(instance);
	}

	if (instance->srtp) {
		res_srtp->destroy(instance->srtp);
	}

	if (instance->rtcp_srtp) {
		res_srtp->destroy(instance->rtcp_srtp);
	}

	ast_rtp_codecs_payloads_destroy(&instance->codecs);

	AST_VECTOR_FREE(&instance->extmap_enabled);
	AST_VECTOR_FREE(&instance->extmap_negotiated);
	AST_VECTOR_FREE(&instance->extmap_unique_ids);

	/* Drop our engine reference */
	ast_module_unref(instance->engine->mod);

	ast_debug(1, "Destroyed RTP instance '%p'\n", instance);
}

int ast_rtp_instance_destroy(struct ast_rtp_instance *instance)
{
	ao2_cleanup(instance);

	return 0;
}

struct ast_rtp_instance *ast_rtp_instance_new(const char *engine_name,
		struct ast_sched_context *sched, const struct ast_sockaddr *sa,
		void *data)
{
	struct ast_sockaddr address = {{0,}};
	struct ast_rtp_instance *instance = NULL;
	struct ast_rtp_engine *engine = NULL;
	struct ast_module *mod_ref;

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
	mod_ref = ast_module_running_ref(engine->mod);

	AST_RWLIST_UNLOCK(&engines);

	if (!mod_ref) {
		/* BUGBUG: improve handling of this situation. */
		return NULL;
	}

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

	/* Initialize RTP extension support */
	if (AST_VECTOR_INIT(&instance->extmap_enabled, 0) ||
		AST_VECTOR_INIT(&instance->extmap_negotiated, 0) ||
		AST_VECTOR_INIT(&instance->extmap_unique_ids, 0)) {
		ao2_ref(instance, -1);
		return NULL;
	}

	ast_debug(1, "Using engine '%s' for RTP instance '%p'\n", engine->name, instance);

	/*
	 * And pass it off to the engine to setup
	 *
	 * Lock in case the RTP engine has other threads that
	 * need synchronization with the construction.
	 */
	ao2_lock(instance);
	if (instance->engine->new(instance, sched, &address, data)) {
		ast_debug(1, "Engine '%s' failed to setup RTP instance '%p'\n", engine->name, instance);
		ao2_unlock(instance);
		ao2_ref(instance, -1);
		return NULL;
	}
	ao2_unlock(instance);

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
	int res;

	ao2_lock(instance);
	res = instance->engine->write(instance, frame);
	ao2_unlock(instance);
	return res;
}

struct ast_frame *ast_rtp_instance_read(struct ast_rtp_instance *instance, int rtcp)
{
	struct ast_frame *frame;

	ao2_lock(instance);
	frame = instance->engine->read(instance, rtcp);
	ao2_unlock(instance);
	return frame;
}

int ast_rtp_instance_set_local_address(struct ast_rtp_instance *instance,
		const struct ast_sockaddr *address)
{
	ao2_lock(instance);
	ast_sockaddr_copy(&instance->local_address, address);
	ao2_unlock(instance);
	return 0;
}

static void rtp_instance_set_incoming_source_address_nolock(struct ast_rtp_instance *instance,
	const struct ast_sockaddr *address)
{
	ast_sockaddr_copy(&instance->incoming_source_address, address);
	if (instance->engine->remote_address_set) {
		instance->engine->remote_address_set(instance, &instance->incoming_source_address);
	}
}

int ast_rtp_instance_set_incoming_source_address(struct ast_rtp_instance *instance,
	const struct ast_sockaddr *address)
{
	ao2_lock(instance);
	rtp_instance_set_incoming_source_address_nolock(instance, address);
	ao2_unlock(instance);

	return 0;
}

int ast_rtp_instance_set_requested_target_address(struct ast_rtp_instance *instance,
						  const struct ast_sockaddr *address)
{
	ao2_lock(instance);

	ast_sockaddr_copy(&instance->requested_target_address, address);
	rtp_instance_set_incoming_source_address_nolock(instance, address);

	ao2_unlock(instance);

	return 0;
}

int ast_rtp_instance_get_and_cmp_local_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	ao2_lock(instance);
	if (ast_sockaddr_cmp(address, &instance->local_address) != 0) {
		ast_sockaddr_copy(address, &instance->local_address);
		ao2_unlock(instance);
		return 1;
	}
	ao2_unlock(instance);

	return 0;
}

void ast_rtp_instance_get_local_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	ao2_lock(instance);
	ast_sockaddr_copy(address, &instance->local_address);
	ao2_unlock(instance);
}

int ast_rtp_instance_get_and_cmp_requested_target_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	ao2_lock(instance);
	if (ast_sockaddr_cmp(address, &instance->requested_target_address) != 0) {
		ast_sockaddr_copy(address, &instance->requested_target_address);
		ao2_unlock(instance);
		return 1;
	}
	ao2_unlock(instance);

	return 0;
}

void ast_rtp_instance_get_incoming_source_address(struct ast_rtp_instance *instance,
						  struct ast_sockaddr *address)
{
	ao2_lock(instance);
	ast_sockaddr_copy(address, &instance->incoming_source_address);
	ao2_unlock(instance);
}

void ast_rtp_instance_get_requested_target_address(struct ast_rtp_instance *instance,
						   struct ast_sockaddr *address)
{
	ao2_lock(instance);
	ast_sockaddr_copy(address, &instance->requested_target_address);
	ao2_unlock(instance);
}

void ast_rtp_instance_set_extended_prop(struct ast_rtp_instance *instance, int property, void *value)
{
	if (instance->engine->extended_prop_set) {
		ao2_lock(instance);
		instance->engine->extended_prop_set(instance, property, value);
		ao2_unlock(instance);
	}
}

void *ast_rtp_instance_get_extended_prop(struct ast_rtp_instance *instance, int property)
{
	void *prop;

	if (instance->engine->extended_prop_get) {
		ao2_lock(instance);
		prop = instance->engine->extended_prop_get(instance, property);
		ao2_unlock(instance);
	} else {
		prop = NULL;
	}

	return prop;
}

void ast_rtp_instance_set_prop(struct ast_rtp_instance *instance, enum ast_rtp_property property, int value)
{
	ao2_lock(instance);
	instance->properties[property] = value;

	if (instance->engine->prop_set) {
		instance->engine->prop_set(instance, property, value);
	}
	ao2_unlock(instance);
}

int ast_rtp_instance_get_prop(struct ast_rtp_instance *instance, enum ast_rtp_property property)
{
	int prop;

	ao2_lock(instance);
	prop = instance->properties[property];
	ao2_unlock(instance);

	return prop;
}

struct ast_rtp_codecs *ast_rtp_instance_get_codecs(struct ast_rtp_instance *instance)
{
	return &instance->codecs;
}

int ast_rtp_instance_extmap_enable(struct ast_rtp_instance *instance, int id, enum ast_rtp_extension extension,
	enum ast_rtp_extension_direction direction)
{
	struct rtp_extmap extmap = {
		.extension = extension,
		.direction = direction,
	};

	ao2_lock(instance);

	if (!instance->engine->extension_enable || !instance->engine->extension_enable(instance, extension)) {
		ao2_unlock(instance);
		return 0;
	}

	/* We store enabled extensions separately so we can easily do negotiation */
	if (AST_VECTOR_REPLACE(&instance->extmap_enabled, extension, direction)) {
		ao2_unlock(instance);
		return -1;
	}

	if (id <= 0) {
		/* We find a free unique identifier for this extension by just appending it to the
		 * vector of unique ids. The size of the vector will become its unique identifier.
		 * As well when we are asking for information on the extensions it will be returned,
		 * allowing it to be added to the SDP offer.
		 */
		if (AST_VECTOR_APPEND(&instance->extmap_unique_ids, extmap)) {
			AST_VECTOR_REPLACE(&instance->extmap_enabled, extension, AST_RTP_EXTENSION_DIRECTION_NONE);
			ao2_unlock(instance);
			return -1;
		}
		id = AST_VECTOR_SIZE(&instance->extmap_unique_ids);
	} else {
		/* Otherwise we put it precisely where they want it */
		if (AST_VECTOR_REPLACE(&instance->extmap_unique_ids, id - 1, extmap)) {
			AST_VECTOR_REPLACE(&instance->extmap_enabled, extension, AST_RTP_EXTENSION_DIRECTION_NONE);
			ao2_unlock(instance);
			return -1;
		}
	}

	/* Now that we have an id add the extension to here */
	if (AST_VECTOR_REPLACE(&instance->extmap_negotiated, extension, id)) {
		extmap.extension = AST_RTP_EXTENSION_UNSUPPORTED;
		extmap.direction = AST_RTP_EXTENSION_DIRECTION_NONE;
		AST_VECTOR_REPLACE(&instance->extmap_enabled, extension, AST_RTP_EXTENSION_DIRECTION_NONE);
		AST_VECTOR_REPLACE(&instance->extmap_unique_ids, id - 1, extmap);
		ao2_unlock(instance);
		return -1;
	}

	ao2_unlock(instance);

	return 0;
}

/*! \brief Helper function which negotiates two RTP extension directions to get our current direction */
static enum ast_rtp_extension_direction rtp_extmap_negotiate_direction(enum ast_rtp_extension_direction ours,
	enum ast_rtp_extension_direction theirs)
{
	if (theirs == AST_RTP_EXTENSION_DIRECTION_NONE || ours == AST_RTP_EXTENSION_DIRECTION_NONE) {
		/* This should not occur but if it does tolerate either side not having this extension
		 * in use.
		 */
		return AST_RTP_EXTENSION_DIRECTION_NONE;
	} else if (theirs == AST_RTP_EXTENSION_DIRECTION_INACTIVE) {
		/* Inactive is always inactive on our side */
		return AST_RTP_EXTENSION_DIRECTION_INACTIVE;
	} else if (theirs == AST_RTP_EXTENSION_DIRECTION_SENDRECV) {
		return ours;
	} else if (theirs == AST_RTP_EXTENSION_DIRECTION_SENDONLY) {
		/* If they are send only then we become recvonly if we are configured as sendrecv or recvonly */
		if (ours == AST_RTP_EXTENSION_DIRECTION_SENDRECV || ours == AST_RTP_EXTENSION_DIRECTION_RECVONLY) {
			return AST_RTP_EXTENSION_DIRECTION_RECVONLY;
		}
	} else if (theirs == AST_RTP_EXTENSION_DIRECTION_RECVONLY) {
		/* If they are recv only then we become sendonly if we are configured as sendrecv or sendonly */
		if (ours == AST_RTP_EXTENSION_DIRECTION_SENDRECV || ours == AST_RTP_EXTENSION_DIRECTION_SENDONLY) {
			return AST_RTP_EXTENSION_DIRECTION_SENDONLY;
		}
	}

	return AST_RTP_EXTENSION_DIRECTION_NONE;
}

int ast_rtp_instance_extmap_negotiate(struct ast_rtp_instance *instance, int id, enum ast_rtp_extension_direction direction,
	const char *uri, const char *attributes)
{
	/* 'attributes' is currently unused but exists in the API to ensure it does not need to be altered
	 * in the future in case we need to use it.
	 */
	int idx;
	enum ast_rtp_extension extension = AST_RTP_EXTENSION_UNSUPPORTED;

	/* Per the RFC the identifier has to be 1 or above */
	if (id < 1) {
		return -1;
	}

	/* Convert the provided URI to the internal representation */
	for (idx = 0; idx < ARRAY_LEN(rtp_extension_uris); ++idx) {
		if (!strcasecmp(rtp_extension_uris[idx], uri)) {
			extension = idx;
			break;
		}
	}

	ao2_lock(instance);
	/* We only accept the extension if it is enabled */
	if (extension < AST_VECTOR_SIZE(&instance->extmap_enabled) &&
		AST_VECTOR_GET(&instance->extmap_enabled, extension) != AST_RTP_EXTENSION_DIRECTION_NONE) {
		struct rtp_extmap extmap = {
			.extension = extension,
			.direction = rtp_extmap_negotiate_direction(AST_VECTOR_GET(&instance->extmap_enabled, extension), direction),
		};

		/* If the direction negotiation failed then don't accept or use this extension */
		if (extmap.direction != AST_RTP_EXTENSION_DIRECTION_NONE) {
			if (extension != AST_RTP_EXTENSION_UNSUPPORTED) {
				AST_VECTOR_REPLACE(&instance->extmap_negotiated, extension, id);
			}
			AST_VECTOR_REPLACE(&instance->extmap_unique_ids, id - 1, extmap);
		}
	}
	ao2_unlock(instance);

	return 0;
}

void ast_rtp_instance_extmap_clear(struct ast_rtp_instance *instance)
{
	static const struct rtp_extmap extmap_none = {
		.extension = AST_RTP_EXTENSION_UNSUPPORTED,
		.direction = AST_RTP_EXTENSION_DIRECTION_NONE,
	};
	int idx;

	ao2_lock(instance);

	/* Clear both the known unique ids and the negotiated extensions as we are about to have
	 * new results set on us.
	 */
	for (idx = 0; idx < AST_VECTOR_SIZE(&instance->extmap_unique_ids); ++idx) {
		AST_VECTOR_REPLACE(&instance->extmap_unique_ids, idx, extmap_none);
	}

	for (idx = 0; idx < AST_VECTOR_SIZE(&instance->extmap_negotiated); ++idx) {
		AST_VECTOR_REPLACE(&instance->extmap_negotiated, idx, -1);
	}

	ao2_unlock(instance);
}

int ast_rtp_instance_extmap_get_id(struct ast_rtp_instance *instance, enum ast_rtp_extension extension)
{
	int id = -1;

	ao2_lock(instance);
	if (extension < AST_VECTOR_SIZE(&instance->extmap_negotiated)) {
		id = AST_VECTOR_GET(&instance->extmap_negotiated, extension);
	}
	ao2_unlock(instance);

	return id;
}

size_t ast_rtp_instance_extmap_count(struct ast_rtp_instance *instance)
{
	size_t count;

	ao2_lock(instance);
	count = AST_VECTOR_SIZE(&instance->extmap_unique_ids);
	ao2_unlock(instance);

	return count;
}

enum ast_rtp_extension ast_rtp_instance_extmap_get_extension(struct ast_rtp_instance *instance, int id)
{
	enum ast_rtp_extension extension = AST_RTP_EXTENSION_UNSUPPORTED;

	ao2_lock(instance);

	/* The local unique identifier starts at '1' so the highest unique identifier
	 * can be the actual size of the vector. We compensate (as it is 0 index based)
	 * by dropping it down to 1 to get the correct information.
	 */
	if (0 < id && id <= AST_VECTOR_SIZE(&instance->extmap_unique_ids)) {
		struct rtp_extmap *extmap = AST_VECTOR_GET_ADDR(&instance->extmap_unique_ids, id - 1);

		extension = extmap->extension;
	}
	ao2_unlock(instance);

	return extension;
}

enum ast_rtp_extension_direction ast_rtp_instance_extmap_get_direction(struct ast_rtp_instance *instance, int id)
{
	enum ast_rtp_extension_direction direction = AST_RTP_EXTENSION_DIRECTION_NONE;

	ao2_lock(instance);

	if (0 < id && id <= AST_VECTOR_SIZE(&instance->extmap_unique_ids)) {
		struct rtp_extmap *extmap = AST_VECTOR_GET_ADDR(&instance->extmap_unique_ids, id - 1);

		direction = extmap->direction;
	}
	ao2_unlock(instance);

	return direction;
}

const char *ast_rtp_instance_extmap_get_uri(struct ast_rtp_instance *instance, int id)
{
	enum ast_rtp_extension extension = ast_rtp_instance_extmap_get_extension(instance, id);

	if (extension == AST_RTP_EXTENSION_UNSUPPORTED ||
		(unsigned int)extension >= ARRAY_LEN(rtp_extension_uris)) {
		return NULL;
	}

	return rtp_extension_uris[extension];
}

int ast_rtp_codecs_payloads_initialize(struct ast_rtp_codecs *codecs)
{
	int res;

	codecs->framing = 0;
	ast_rwlock_init(&codecs->codecs_lock);
	res = AST_VECTOR_INIT(&codecs->payload_mapping_rx, AST_RTP_MAX_PT);
	res |= AST_VECTOR_INIT(&codecs->payload_mapping_tx, AST_RTP_MAX_PT);
	if (res) {
		AST_VECTOR_FREE(&codecs->payload_mapping_rx);
		AST_VECTOR_FREE(&codecs->payload_mapping_tx);
	}

	return res;
}

void ast_rtp_codecs_payloads_destroy(struct ast_rtp_codecs *codecs)
{
	int idx;
	struct ast_rtp_payload_type *type;

	for (idx = 0; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_rx); ++idx) {
		type = AST_VECTOR_GET(&codecs->payload_mapping_rx, idx);
		ao2_t_cleanup(type, "destroying ast_rtp_codec rx mapping");
	}
	AST_VECTOR_FREE(&codecs->payload_mapping_rx);

	for (idx = 0; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_tx); ++idx) {
		type = AST_VECTOR_GET(&codecs->payload_mapping_tx, idx);
		ao2_t_cleanup(type, "destroying ast_rtp_codec tx mapping");
	}
	AST_VECTOR_FREE(&codecs->payload_mapping_tx);

	ast_rwlock_destroy(&codecs->codecs_lock);
}

void ast_rtp_codecs_payloads_clear(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance)
{
	ast_rtp_codecs_payloads_destroy(codecs);
	ast_rtp_codecs_payloads_initialize(codecs);

	if (instance && instance->engine && instance->engine->payload_set) {
		int i;

		ao2_lock(instance);
		for (i = 0; i < AST_RTP_MAX_PT; i++) {
			instance->engine->payload_set(instance, i, 0, NULL, 0);
		}
		ao2_unlock(instance);
	}
}

/*!
 * \internal
 * \brief Clear the rx primary mapping flag on all other matching mappings.
 * \since 14.0.0
 *
 * \param codecs Codecs that need rx clearing.
 * \param to_match Payload type object to compare against.
 *
 * \note It is assumed that codecs is write locked before calling.
 */
static void payload_mapping_rx_clear_primary(struct ast_rtp_codecs *codecs, struct ast_rtp_payload_type *to_match)
{
	int idx;
	struct ast_rtp_payload_type *current;
	struct ast_rtp_payload_type *new_type;
	struct timeval now;

	if (!to_match->primary_mapping) {
		return;
	}

	now = ast_tvnow();
	for (idx = 0; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_rx); ++idx) {
		current = AST_VECTOR_GET(&codecs->payload_mapping_rx, idx);

		if (!current || current == to_match || !current->primary_mapping) {
			continue;
		}
		if (current->asterisk_format && to_match->asterisk_format) {
			if (ast_format_cmp(current->format, to_match->format) == AST_FORMAT_CMP_NOT_EQUAL) {
				continue;
			}
		} else if (!current->asterisk_format && !to_match->asterisk_format) {
			if (current->rtp_code != to_match->rtp_code) {
				continue;
			}
		} else {
			continue;
		}

		/* Replace current with non-primary marked version */
		new_type = ast_rtp_engine_alloc_payload_type();
		if (!new_type) {
			continue;
		}
		*new_type = *current;
		new_type->primary_mapping = 0;
		new_type->when_retired = now;
		ao2_bump(new_type->format);
		AST_VECTOR_REPLACE(&codecs->payload_mapping_rx, idx, new_type);
		ao2_ref(current, -1);
	}
}

/*!
 * \internal
 * \brief Put the new_type into the rx payload type mapping.
 * \since 14.0.0
 *
 * \param codecs Codecs structure to put new_type into
 * \param payload type position to replace.
 * \param new_type RTP payload mapping object to store.
 *
 * \note It is assumed that codecs is write locked before calling.
 */
static void rtp_codecs_payload_replace_rx(struct ast_rtp_codecs *codecs, int payload, struct ast_rtp_payload_type *new_type)
{
	ao2_ref(new_type, +1);
	if (payload < AST_VECTOR_SIZE(&codecs->payload_mapping_rx)) {
		ao2_t_cleanup(AST_VECTOR_GET(&codecs->payload_mapping_rx, payload),
			"cleaning up rx mapping vector element about to be replaced");
	}
	if (AST_VECTOR_REPLACE(&codecs->payload_mapping_rx, payload, new_type)) {
		ao2_ref(new_type, -1);
		return;
	}

	payload_mapping_rx_clear_primary(codecs, new_type);
}

/*!
 * \internal
 * \brief Copy the rx payload type mapping to the destination.
 * \since 14.0.0
 *
 * \param src The source codecs structure
 * \param dest The destination codecs structure that the values from src will be copied to
 * \param instance Optionally the instance that the dst codecs structure belongs to
 *
 * \note It is assumed that src is at least read locked before calling.
 * \note It is assumed that dest is write locked before calling.
 */
static void rtp_codecs_payloads_copy_rx(struct ast_rtp_codecs *src, struct ast_rtp_codecs *dest, struct ast_rtp_instance *instance)
{
	int idx;
	struct ast_rtp_payload_type *type;

	for (idx = 0; idx < AST_VECTOR_SIZE(&src->payload_mapping_rx); ++idx) {
		type = AST_VECTOR_GET(&src->payload_mapping_rx, idx);
		if (!type) {
			continue;
		}

		ast_debug(2, "Copying rx payload mapping %d (%p) from %p to %p\n",
			idx, type, src, dest);
		rtp_codecs_payload_replace_rx(dest, idx, type);

		if (instance && instance->engine && instance->engine->payload_set) {
			ao2_lock(instance);
			instance->engine->payload_set(instance, idx, type->asterisk_format, type->format, type->rtp_code);
			ao2_unlock(instance);
		}
	}
}

/*!
 * \internal
 * \brief Determine if a type of payload is already present in mappings.
 * \since 14.0.0
 *
 * \param codecs Codecs to be checked for mappings.
 * \param to_match Payload type object to compare against.
 *
 * \note It is assumed that codecs is write locked before calling.
 *
 * \retval 0 not found
 * \retval 1 found
 */
static int payload_mapping_tx_is_present(const struct ast_rtp_codecs *codecs, const struct ast_rtp_payload_type *to_match)
{
	int idx;
	struct ast_rtp_payload_type *current;

	for (idx = 0; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_tx); ++idx) {
		current = AST_VECTOR_GET(&codecs->payload_mapping_tx, idx);

		if (!current) {
			continue;
		}
		if (current == to_match) {
			/* The exact object is already in the mapping. */
			return 1;
		}
		if (current->asterisk_format && to_match->asterisk_format) {
			if (ast_format_get_codec_id(current->format) != ast_format_get_codec_id(to_match->format)) {
				continue;
			} else if (current->payload == to_match->payload) {
				return 0;
			}
		} else if (!current->asterisk_format && !to_match->asterisk_format) {
			if (current->rtp_code != to_match->rtp_code) {
				continue;
			}
		} else {
			continue;
		}

		return 1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Copy the tx payload type mapping to the destination.
 * \since 14.0.0
 *
 * \param src The source codecs structure
 * \param dest The destination codecs structure that the values from src will be copied to
 * \param instance Optionally the instance that the dst codecs structure belongs to
 *
 * \note It is assumed that src is at least read locked before calling.
 * \note It is assumed that dest is write locked before calling.
 */
static void rtp_codecs_payloads_copy_tx(struct ast_rtp_codecs *src, struct ast_rtp_codecs *dest, struct ast_rtp_instance *instance)
{
	int idx;
	struct ast_rtp_payload_type *type;

	for (idx = 0; idx < AST_VECTOR_SIZE(&src->payload_mapping_tx); ++idx) {
		type = AST_VECTOR_GET(&src->payload_mapping_tx, idx);
		if (!type) {
			continue;
		}

		ast_debug(2, "Copying tx payload mapping %d (%p) from %p to %p\n",
			idx, type, src, dest);
		ao2_ref(type, +1);
		if (idx < AST_VECTOR_SIZE(&dest->payload_mapping_tx)) {
			ao2_t_cleanup(AST_VECTOR_GET(&dest->payload_mapping_tx, idx),
				"cleaning up tx mapping vector element about to be replaced");
		}
		if (AST_VECTOR_REPLACE(&dest->payload_mapping_tx, idx, type)) {
			ao2_ref(type, -1);
			continue;
		}

		if (instance && instance->engine && instance->engine->payload_set) {
			ao2_lock(instance);
			instance->engine->payload_set(instance, idx, type->asterisk_format, type->format, type->rtp_code);
			ao2_unlock(instance);
		}
	}
}

void ast_rtp_codecs_payloads_copy(struct ast_rtp_codecs *src, struct ast_rtp_codecs *dest, struct ast_rtp_instance *instance)
{
	int idx;
	struct ast_rtp_payload_type *type;

	ast_rwlock_wrlock(&dest->codecs_lock);

	/* Deadlock avoidance because of held write lock. */
	while (ast_rwlock_tryrdlock(&src->codecs_lock)) {
		ast_rwlock_unlock(&dest->codecs_lock);
		sched_yield();
		ast_rwlock_wrlock(&dest->codecs_lock);
	}

	/*
	 * This represents a completely new mapping of what the remote party is
	 * expecting for payloads, so we clear out the entire tx payload mapping
	 * vector and replace it.
	 */
	for (idx = 0; idx < AST_VECTOR_SIZE(&dest->payload_mapping_tx); ++idx) {
		type = AST_VECTOR_GET(&dest->payload_mapping_tx, idx);
		ao2_t_cleanup(type, "destroying ast_rtp_codec tx mapping");
		AST_VECTOR_REPLACE(&dest->payload_mapping_tx, idx, NULL);
	}

	rtp_codecs_payloads_copy_rx(src, dest, instance);
	rtp_codecs_payloads_copy_tx(src, dest, instance);
	dest->framing = src->framing;

	ast_rwlock_unlock(&src->codecs_lock);
	ast_rwlock_unlock(&dest->codecs_lock);
}

void ast_rtp_codecs_payloads_xover(struct ast_rtp_codecs *src, struct ast_rtp_codecs *dest, struct ast_rtp_instance *instance)
{
	int idx;
	struct ast_rtp_payload_type *type;

	ast_rwlock_wrlock(&dest->codecs_lock);
	if (src != dest) {
		/* Deadlock avoidance because of held write lock. */
		while (ast_rwlock_tryrdlock(&src->codecs_lock)) {
			ast_rwlock_unlock(&dest->codecs_lock);
			sched_yield();
			ast_rwlock_wrlock(&dest->codecs_lock);
		}
	}

	/* Crossover copy payload type tx mapping to rx mapping. */
	for (idx = 0; idx < AST_VECTOR_SIZE(&src->payload_mapping_tx); ++idx) {
		type = AST_VECTOR_GET(&src->payload_mapping_tx, idx);
		if (!type) {
			continue;
		}

		/* All tx mapping elements should have the primary flag set. */
		ast_assert(type->primary_mapping);

		ast_debug(2, "Crossover copying tx to rx payload mapping %d (%p) from %p to %p\n",
			idx, type, src, dest);
		rtp_codecs_payload_replace_rx(dest, idx, type);

		if (instance && instance->engine && instance->engine->payload_set) {
			ao2_lock(instance);
			instance->engine->payload_set(instance, idx, type->asterisk_format, type->format, type->rtp_code);
			ao2_unlock(instance);
		}
	}

	dest->framing = src->framing;

	if (src != dest) {
		ast_rwlock_unlock(&src->codecs_lock);
	}
	ast_rwlock_unlock(&dest->codecs_lock);
}

void ast_rtp_codecs_payloads_set_m_type(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload)
{
	struct ast_rtp_payload_type *new_type;

	if (payload < 0 || payload >= AST_RTP_MAX_PT || payload > AST_RTP_PT_LAST_STATIC) {
		return;
	}

	ast_rwlock_rdlock(&static_RTP_PT_lock);
	new_type = ao2_bump(static_RTP_PT[payload]);
	ast_rwlock_unlock(&static_RTP_PT_lock);
	if (!new_type) {
		ast_debug(1, "Don't have a default tx payload type %d format for m type on %p\n",
			payload, codecs);
		return;
	}

	ast_debug(1, "Setting tx payload type %d based on m type on %p\n",
		payload, codecs);

	ast_rwlock_wrlock(&codecs->codecs_lock);

	if (!payload_mapping_tx_is_present(codecs, new_type)) {
		if (payload < AST_VECTOR_SIZE(&codecs->payload_mapping_tx)) {
			ao2_t_cleanup(AST_VECTOR_GET(&codecs->payload_mapping_tx, payload),
				"cleaning up replaced tx payload type");
		}

		if (AST_VECTOR_REPLACE(&codecs->payload_mapping_tx, payload, new_type)) {
			ao2_ref(new_type, -1);
		} else if (instance && instance->engine && instance->engine->payload_set) {
			ao2_lock(instance);
			instance->engine->payload_set(instance, payload, new_type->asterisk_format, new_type->format, new_type->rtp_code);
			ao2_unlock(instance);
		}
	} else {
		ao2_ref(new_type, -1);
	}

	ast_rwlock_unlock(&codecs->codecs_lock);
}

int ast_rtp_codecs_payloads_set_rtpmap_type_rate(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int pt,
				 char *mimetype, char *mimesubtype,
				 enum ast_rtp_options options,
				 unsigned int sample_rate)
{
	unsigned int idx;
	int found = 0;

	if (pt < 0 || pt >= AST_RTP_MAX_PT) {
		return -1; /* bogus payload type */
	}

	ast_rwlock_rdlock(&mime_types_lock);
	ast_rwlock_wrlock(&codecs->codecs_lock);

	for (idx = 0; idx < mime_types_len; ++idx) {
		const struct ast_rtp_mime_type *t = &ast_rtp_mime_types[idx];
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

		new_type->asterisk_format = t->payload_type.asterisk_format;
		new_type->rtp_code = t->payload_type.rtp_code;
		new_type->payload = pt;
		new_type->primary_mapping = 1;
		if (t->payload_type.asterisk_format
			&& ast_format_cmp(t->payload_type.format, ast_format_g726) == AST_FORMAT_CMP_EQUAL
			&& (options & AST_RTP_OPT_G726_NONSTANDARD)) {
			new_type->format = ast_format_g726_aal2;
		} else {
			new_type->format = t->payload_type.format;
		}

		if (new_type->format) {
			/* SDP parsing automatically increases the reference count */
			new_type->format = ast_format_parse_sdp_fmtp(new_type->format, "");
		}

		if (!payload_mapping_tx_is_present(codecs, new_type)) {
			if (pt < AST_VECTOR_SIZE(&codecs->payload_mapping_tx)) {
				ao2_t_cleanup(AST_VECTOR_GET(&codecs->payload_mapping_tx, pt),
					"cleaning up replaced tx payload type");
			}

			if (AST_VECTOR_REPLACE(&codecs->payload_mapping_tx, pt, new_type)) {
				ao2_ref(new_type, -1);
			} else if (instance && instance->engine && instance->engine->payload_set) {
				ao2_lock(instance);
				instance->engine->payload_set(instance, pt, new_type->asterisk_format, new_type->format, new_type->rtp_code);
				ao2_unlock(instance);
			}
		} else {
			ao2_ref(new_type, -1);
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

	if (payload < AST_VECTOR_SIZE(&codecs->payload_mapping_tx)) {
		type = AST_VECTOR_GET(&codecs->payload_mapping_tx, payload);
		ao2_cleanup(type);
		AST_VECTOR_REPLACE(&codecs->payload_mapping_tx, payload, NULL);
	}

	if (instance && instance->engine && instance->engine->payload_set) {
		ao2_lock(instance);
		instance->engine->payload_set(instance, payload, 0, NULL, 0);
		ao2_unlock(instance);
	}

	ast_rwlock_unlock(&codecs->codecs_lock);
}

enum ast_media_type ast_rtp_codecs_get_stream_type(struct ast_rtp_codecs *codecs)
{
	enum ast_media_type stream_type = AST_MEDIA_TYPE_UNKNOWN;
	int payload;
	struct ast_rtp_payload_type *type;

	ast_rwlock_rdlock(&codecs->codecs_lock);
	for (payload = 0; payload < AST_VECTOR_SIZE(&codecs->payload_mapping_rx); ++payload) {
		type = AST_VECTOR_GET(&codecs->payload_mapping_rx, payload);
		if (type && type->asterisk_format) {
			stream_type = ast_format_get_type(type->format);
			break;
		}
	}
	ast_rwlock_unlock(&codecs->codecs_lock);

	return stream_type;
}

struct ast_rtp_payload_type *ast_rtp_codecs_get_payload(struct ast_rtp_codecs *codecs, int payload)
{
	struct ast_rtp_payload_type *type = NULL;

	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		return NULL;
	}

	ast_rwlock_rdlock(&codecs->codecs_lock);
	if (payload < AST_VECTOR_SIZE(&codecs->payload_mapping_rx)) {
		type = AST_VECTOR_GET(&codecs->payload_mapping_rx, payload);
		ao2_bump(type);
	}
	ast_rwlock_unlock(&codecs->codecs_lock);

	if (!type && payload <= AST_RTP_PT_LAST_STATIC) {
		ast_rwlock_rdlock(&static_RTP_PT_lock);
		type = ao2_bump(static_RTP_PT[payload]);
		ast_rwlock_unlock(&static_RTP_PT_lock);
	}

	return type;
}

int ast_rtp_codecs_payload_replace_format(struct ast_rtp_codecs *codecs, int payload, struct ast_format *format)
{
	struct ast_rtp_payload_type *type;

	if (payload < 0 || payload >= AST_RTP_MAX_PT || !format) {
		return -1;
	}

	type = ast_rtp_engine_alloc_payload_type();
	if (!type) {
		return -1;
	}
	ao2_ref(format, +1);
	type->format = format;
	type->asterisk_format = 1;
	type->payload = payload;
	type->primary_mapping = 1;

	ast_rwlock_wrlock(&codecs->codecs_lock);
	if (!payload_mapping_tx_is_present(codecs, type)) {
		if (payload < AST_VECTOR_SIZE(&codecs->payload_mapping_tx)) {
			ao2_cleanup(AST_VECTOR_GET(&codecs->payload_mapping_tx, payload));
		}
		if (AST_VECTOR_REPLACE(&codecs->payload_mapping_tx, payload, type)) {
			ao2_ref(type, -1);
		}
	} else {
		ao2_ref(type, -1);
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
	if (payload < AST_VECTOR_SIZE(&codecs->payload_mapping_tx)) {
		type = AST_VECTOR_GET(&codecs->payload_mapping_tx, payload);
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
	int idx;

	ast_format_cap_remove_by_type(astformats, AST_MEDIA_TYPE_UNKNOWN);
	*nonastformats = 0;

	ast_rwlock_rdlock(&codecs->codecs_lock);

	for (idx = 0; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_tx); ++idx) {
		struct ast_rtp_payload_type *type;

		type = AST_VECTOR_GET(&codecs->payload_mapping_tx, idx);
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

/*!
 * \internal
 * \brief Find the static payload type mapping for the format.
 * \since 14.0.0
 *
 * \param asterisk_format Non-zero if the given Asterisk format is present
 * \param format Asterisk format to look for
 * \param code The non-Asterisk format code to look for
 *
 * \note It is assumed that static_RTP_PT_lock is at least read locked before calling.
 *
 * \return Numerical payload type
 * \retval -1 if not found.
 */
static int find_static_payload_type(int asterisk_format, const struct ast_format *format, int code)
{
	int idx;
	int payload = -1;

	if (!asterisk_format) {
		for (idx = 0; idx < AST_RTP_MAX_PT; ++idx) {
			if (static_RTP_PT[idx]
				&& !static_RTP_PT[idx]->asterisk_format
				&& static_RTP_PT[idx]->rtp_code == code) {
				payload = idx;
				break;
			}
		}
	} else if (format) {
		for (idx = 0; idx < AST_RTP_MAX_PT; ++idx) {
			if (static_RTP_PT[idx]
				&& static_RTP_PT[idx]->asterisk_format
				&& ast_format_cmp(format, static_RTP_PT[idx]->format)
					!= AST_FORMAT_CMP_NOT_EQUAL) {
				payload = idx;
				break;
			}
		}
	}

	return payload;
}

/*!
 * \internal
 * \brief Find the first unused payload type in a given range
 *
 * \param codecs The codec structure to look in
 * \param start Starting index
 * \param end Ending index
 * \param ignore Skip these payloads types
 *
 * \note The static_RTP_PT_lock object must be locked before calling
 *
 * \return Numerical payload type
 * \retval -1 if not found.
 */
static int find_unused_payload_in_range(const struct ast_rtp_codecs *codecs,
		int start, int end, struct ast_rtp_payload_type *ignore[])
{
	int x;

	for (x = start; x < end; ++x) {
		struct ast_rtp_payload_type *type;

		if (ignore[x]) {
			continue;
		} else if (!codecs || x >= AST_VECTOR_SIZE(&codecs->payload_mapping_rx)) {
			return x;
		}

		type = AST_VECTOR_GET(&codecs->payload_mapping_rx, x);
		if (!type) {
			return x;
		}
	}
	return -1;
}

/*!
 * \internal
 * \brief Find an unused payload type
 *
 * \param codecs Codecs structure to look in
 *
 * \note Both static_RTP_PT_lock and codecs (if given) must be at least
 *       read locked before calling.
 *
 * \return Numerical payload type
 * \retval -1 if not found.
 */
static int find_unused_payload(const struct ast_rtp_codecs *codecs)
{
	int res;

	/* find next available dynamic payload slot */
	res = find_unused_payload_in_range(
		codecs, AST_RTP_PT_FIRST_DYNAMIC, AST_RTP_MAX_PT, static_RTP_PT);
	if (res != -1) {
		return res;
	}

	if (ast_option_rtpusedynamic) {
		/*
		 * We're using default values for some dynamic types. So if an unused
		 * slot was not found try again, but this time ignore the default
		 * values declared for dynamic types (except for 101 and 121) .
		 */
		static struct ast_rtp_payload_type *ignore[AST_RTP_MAX_PT] = {0};

		ignore[101] = static_RTP_PT[101];
		ignore[121] = static_RTP_PT[121];

		res = find_unused_payload_in_range(
			codecs, AST_RTP_PT_FIRST_DYNAMIC, AST_RTP_MAX_PT, ignore);
		if (res != -1) {
			return res;
		}
	}

	/* http://www.iana.org/assignments/rtp-parameters
	 * RFC 3551, Section 3: "[...] applications which need to define more
	 * than 32 dynamic payload types MAY bind codes below 96, in which case
	 * it is RECOMMENDED that unassigned payload type numbers be used
	 * first". Updated by RFC 5761, Section 4: "[...] values in the range
	 * 64-95 MUST NOT be used [to avoid conflicts with RTCP]". Summaries:
	 * https://tools.ietf.org/html/draft-roach-mmusic-unified-plan#section-3.2.1.2
	 * https://tools.ietf.org/html/draft-wu-avtcore-dynamic-pt-usage#section-3
	 */
	res = find_unused_payload_in_range(
		codecs, MAX(ast_option_rtpptdynamic, AST_RTP_PT_LAST_STATIC + 1),
		AST_RTP_PT_LAST_REASSIGN, static_RTP_PT);
	if (res != -1) {
		return res;
	}

	/* Yet, reusing mappings below AST_RTP_PT_LAST_STATIC (35) is not supported
	 * in Asterisk because when Compact Headers are activated, no rtpmap is
	 * send for those below 35. If you want to use 35 and below
	 * A) do not use Compact Headers,
	 * B) remove that code in chan_sip/res_pjsip, or
	 * C) add a flag that this RTP Payload Type got reassigned dynamically
	 *    and requires a rtpmap even with Compact Headers enabled.
	 */
	res = find_unused_payload_in_range(
		codecs, MAX(ast_option_rtpptdynamic, 20),
		AST_RTP_PT_LAST_STATIC + 1, static_RTP_PT);
	if (res != -1) {
		return res;
	}

	return find_unused_payload_in_range(
		codecs, MAX(ast_option_rtpptdynamic, 0),
		20, static_RTP_PT);
}

/*!
 * \internal
 * \brief Find the oldest non-primary dynamic rx payload type.
 * \since 14.0.0
 *
 * \param codecs Codecs structure to look in
 *
 * \note It is assumed that codecs is at least read locked before calling.
 *
 * \return Numerical payload type
 * \retval -1 if not found.
 */
static int rtp_codecs_find_non_primary_dynamic_rx(struct ast_rtp_codecs *codecs)
{
	struct ast_rtp_payload_type *type;
	struct timeval oldest;
	int idx;
	int payload = -1;

	idx = AST_RTP_PT_FIRST_DYNAMIC;
	for (; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_rx); ++idx) {
		type = AST_VECTOR_GET(&codecs->payload_mapping_rx, idx);
		if (type
			&& !type->primary_mapping
			&& (payload == -1
				|| ast_tvdiff_ms(type->when_retired, oldest) < 0)) {
			oldest = type->when_retired;
			payload = idx;
		}
	}
	return payload;
}

/*!
 * \internal
 * \brief Assign a payload type for the rx mapping.
 * \since 14.0.0
 *
 * \param codecs Codecs structure to look in
 * \param asterisk_format Non-zero if the given Asterisk format is present
 * \param format Asterisk format to look for
 * \param code The format to look for
 * \param explicit Require the provided code to be explicitly used
 *
 * \note It is assumed that static_RTP_PT_lock is at least read locked before calling.
 *
 * \return Numerical payload type
 * \retval -1 if could not assign.
 */
static int rtp_codecs_assign_payload_code_rx(struct ast_rtp_codecs *codecs, int asterisk_format, struct ast_format *format, int code, int explicit)
{
	int payload = code;
	struct ast_rtp_payload_type *new_type;

	if (!explicit) {
		payload = find_static_payload_type(asterisk_format, format, code);

		if (payload < 0 && (!asterisk_format || !ast_option_rtpusedynamic)) {
			return payload;
		}
	}

	new_type = rtp_payload_type_alloc(format, payload, code, 1);
	if (!new_type) {
		return -1;
	}

	ast_rwlock_wrlock(&codecs->codecs_lock);
	if (payload > -1 && (payload < AST_RTP_PT_FIRST_DYNAMIC
		|| AST_VECTOR_SIZE(&codecs->payload_mapping_rx) <= payload
		|| !AST_VECTOR_GET(&codecs->payload_mapping_rx, payload))) {
		/*
		 * The payload type is a static assignment
		 * or our default dynamic position is available.
		 */
		rtp_codecs_payload_replace_rx(codecs, payload, new_type);
	} else if (!explicit && (-1 < (payload = find_unused_payload(codecs))
		|| -1 < (payload = rtp_codecs_find_non_primary_dynamic_rx(codecs)))) {
		/*
		 * We found the first available empty dynamic position
		 * or we found a mapping that should no longer be
		 * actively used.
		 */
		new_type->payload = payload;
		rtp_codecs_payload_replace_rx(codecs, payload, new_type);
	} else if (explicit) {
		/*
		* They explicitly requested this payload number be used but it couldn't be
		*/
		payload = -1;
	} else {
		/*
		 * There are no empty or non-primary dynamic positions
		 * left.  Sadness.
		 *
		 * I don't think this is really possible.
		 */
		ast_log(LOG_WARNING, "No dynamic RTP payload type values available "
			"for %s - %d!\n", format ? ast_format_get_name(format) : "", code);
	}
	ast_rwlock_unlock(&codecs->codecs_lock);

	ao2_ref(new_type, -1);

	return payload;
}

int ast_rtp_codecs_payload_code(struct ast_rtp_codecs *codecs, int asterisk_format, struct ast_format *format, int code)
{
	struct ast_rtp_payload_type *type;
	int idx;
	int payload = -1;

	ast_rwlock_rdlock(&static_RTP_PT_lock);
	if (!asterisk_format) {
		ast_rwlock_rdlock(&codecs->codecs_lock);
		for (idx = 0; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_rx); ++idx) {
			type = AST_VECTOR_GET(&codecs->payload_mapping_rx, idx);
			if (!type) {
				continue;
			}

			if (!type->asterisk_format
				&& type->primary_mapping
				&& type->rtp_code == code) {
				payload = idx;
				break;
			}
		}
		ast_rwlock_unlock(&codecs->codecs_lock);
	} else if (format) {
		ast_rwlock_rdlock(&codecs->codecs_lock);
		for (idx = 0; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_rx); ++idx) {
			type = AST_VECTOR_GET(&codecs->payload_mapping_rx, idx);
			if (!type) {
				continue;
			}

			if (type->asterisk_format
				&& type->primary_mapping
				&& ast_format_cmp(format, type->format) == AST_FORMAT_CMP_EQUAL) {
				payload = idx;
				break;
			}
		}
		ast_rwlock_unlock(&codecs->codecs_lock);
	}

	if (payload < 0) {
		payload = rtp_codecs_assign_payload_code_rx(codecs, asterisk_format, format,
			code, 0);
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);

	return payload;
}

int ast_rtp_codecs_payload_set_rx(struct ast_rtp_codecs *codecs, int code, struct ast_format *format)
{
	return rtp_codecs_assign_payload_code_rx(codecs, 1, format, code, 1);
}

int ast_rtp_codecs_payload_code_tx(struct ast_rtp_codecs *codecs, int asterisk_format, const struct ast_format *format, int code)
{
	struct ast_rtp_payload_type *type;
	int idx;
	int payload = -1;

	if (!asterisk_format) {
		ast_rwlock_rdlock(&codecs->codecs_lock);
		for (idx = 0; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_tx); ++idx) {
			type = AST_VECTOR_GET(&codecs->payload_mapping_tx, idx);
			if (!type) {
				continue;
			}

			if (!type->asterisk_format
				&& type->rtp_code == code) {
				payload = idx;
				break;
			}
		}
		ast_rwlock_unlock(&codecs->codecs_lock);
	} else if (format) {
		ast_rwlock_rdlock(&codecs->codecs_lock);
		for (idx = 0; idx < AST_VECTOR_SIZE(&codecs->payload_mapping_tx); ++idx) {
			type = AST_VECTOR_GET(&codecs->payload_mapping_tx, idx);
			if (!type) {
				continue;
			}

			if (type->asterisk_format
				&& ast_format_cmp(format, type->format) == AST_FORMAT_CMP_EQUAL) {
				payload = idx;
				break;
			}
		}
		ast_rwlock_unlock(&codecs->codecs_lock);
	}

	if (payload < 0) {
		ast_rwlock_rdlock(&static_RTP_PT_lock);
		payload = find_static_payload_type(asterisk_format, format, code);
		ast_rwlock_unlock(&static_RTP_PT_lock);
	}

	return payload;
}

int ast_rtp_codecs_find_payload_code(struct ast_rtp_codecs *codecs, int payload)
{
	struct ast_rtp_payload_type *type;
	int res = -1;

	ast_rwlock_rdlock(&codecs->codecs_lock);
	if (payload < AST_VECTOR_SIZE(&codecs->payload_mapping_tx)) {
		type = AST_VECTOR_GET(&codecs->payload_mapping_tx, payload);
		if (type) {
			res = payload;
		}
	}
	ast_rwlock_unlock(&codecs->codecs_lock);

	return res;
}

const char *ast_rtp_lookup_mime_subtype2(const int asterisk_format,
	const struct ast_format *format, int code, enum ast_rtp_options options)
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

unsigned int ast_rtp_lookup_sample_rate2(int asterisk_format,
	const struct ast_format *format, int code)
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
	int res;

	if (instance->engine->dtmf_begin) {
		ao2_lock(instance);
		res = instance->engine->dtmf_begin(instance, digit);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

int ast_rtp_instance_dtmf_end(struct ast_rtp_instance *instance, char digit)
{
	int res;

	if (instance->engine->dtmf_end) {
		ao2_lock(instance);
		res = instance->engine->dtmf_end(instance, digit);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

int ast_rtp_instance_dtmf_end_with_duration(struct ast_rtp_instance *instance, char digit, unsigned int duration)
{
	int res;

	if (instance->engine->dtmf_end_with_duration) {
		ao2_lock(instance);
		res = instance->engine->dtmf_end_with_duration(instance, digit, duration);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

int ast_rtp_instance_dtmf_mode_set(struct ast_rtp_instance *instance, enum ast_rtp_dtmf_mode dtmf_mode)
{
	int res;

	if (instance->engine->dtmf_mode_set) {
		ao2_lock(instance);
		res = instance->engine->dtmf_mode_set(instance, dtmf_mode);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

enum ast_rtp_dtmf_mode ast_rtp_instance_dtmf_mode_get(struct ast_rtp_instance *instance)
{
	int res;

	if (instance->engine->dtmf_mode_get) {
		ao2_lock(instance);
		res = instance->engine->dtmf_mode_get(instance);
		ao2_unlock(instance);
	} else {
		res = 0;
	}
	return res;
}

void ast_rtp_instance_update_source(struct ast_rtp_instance *instance)
{
	if (instance->engine->update_source) {
		ao2_lock(instance);
		instance->engine->update_source(instance);
		ao2_unlock(instance);
	}
}

void ast_rtp_instance_change_source(struct ast_rtp_instance *instance)
{
	if (instance->engine->change_source) {
		ao2_lock(instance);
		instance->engine->change_source(instance);
		ao2_unlock(instance);
	}
}

int ast_rtp_instance_set_qos(struct ast_rtp_instance *instance, int tos, int cos, const char *desc)
{
	int res;

	if (instance->engine->qos) {
		ao2_lock(instance);
		res = instance->engine->qos(instance, tos, cos, desc);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

void ast_rtp_instance_stop(struct ast_rtp_instance *instance)
{
	if (instance->engine->stop) {
		ao2_lock(instance);
		instance->engine->stop(instance);
		ao2_unlock(instance);
	}
}

int ast_rtp_instance_fd(struct ast_rtp_instance *instance, int rtcp)
{
	int res;

	if (instance->engine->fd) {
		ao2_lock(instance);
		res = instance->engine->fd(instance, rtcp);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
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
	struct ast_rtp_instance *bridged;

	ao2_lock(instance);
	bridged = instance->bridged;
	ao2_unlock(instance);
	return bridged;
}

void ast_rtp_instance_set_bridged(struct ast_rtp_instance *instance, struct ast_rtp_instance *bridged)
{
	ao2_lock(instance);
	instance->bridged = bridged;
	ao2_unlock(instance);
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

	ast_rtp_codecs_payloads_xover(&instance_src->codecs, &instance_dst->codecs, instance_dst);

	if (vinstance_dst && vinstance_src) {
		ast_rtp_codecs_payloads_xover(&vinstance_src->codecs, &vinstance_dst->codecs, vinstance_dst);
	}
	if (tinstance_dst && tinstance_src) {
		ast_rtp_codecs_payloads_xover(&tinstance_src->codecs, &tinstance_dst->codecs, tinstance_dst);
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
	int res;

	if (instance->engine->red_init) {
		ao2_lock(instance);
		res = instance->engine->red_init(instance, buffer_time, payloads, generations);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

int ast_rtp_red_buffer(struct ast_rtp_instance *instance, struct ast_frame *frame)
{
	int res;

	if (instance->engine->red_buffer) {
		ao2_lock(instance);
		res = instance->engine->red_buffer(instance, frame);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

int ast_rtp_instance_get_stats(struct ast_rtp_instance *instance, struct ast_rtp_instance_stats *stats, enum ast_rtp_instance_stat stat)
{
	int res;

	if (instance->engine->get_stat) {
		ao2_lock(instance);
		res = instance->engine->get_stat(instance, stats, stat);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
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
	struct ast_channel *bridge;

	bridge = ast_channel_bridge_peer(chan);
	if (bridge) {
		ast_channel_lock_both(chan, bridge);
		ast_channel_stage_snapshot(bridge);
	} else {
		ast_channel_lock(chan);
	}
	ast_channel_stage_snapshot(chan);

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

	ast_channel_stage_snapshot_done(chan);
	ast_channel_unlock(chan);
	if (bridge) {
		ast_channel_stage_snapshot_done(bridge);
		ast_channel_unlock(bridge);
		ast_channel_unref(bridge);
	}
}

int ast_rtp_instance_set_read_format(struct ast_rtp_instance *instance, struct ast_format *format)
{
	int res;

	if (instance->engine->set_read_format) {
		ao2_lock(instance);
		res = instance->engine->set_read_format(instance, format);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

int ast_rtp_instance_set_write_format(struct ast_rtp_instance *instance, struct ast_format *format)
{
	int res;

	if (instance->engine->set_read_format) {
		ao2_lock(instance);
		res = instance->engine->set_write_format(instance, format);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

/* XXX Nothing calls this */
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

	/*
	 * XXX Good thing nothing calls this function because we would need
	 * deadlock avoidance to get the two instance locks.
	 */
	res = instance->engine->make_compatible(chan, instance, peer, peer_instance);

	ast_channel_unlock(peer);

	ao2_ref(peer_instance, -1);
	peer_instance = NULL;

	return res;
}

void ast_rtp_instance_available_formats(struct ast_rtp_instance *instance, struct ast_format_cap *to_endpoint, struct ast_format_cap *to_asterisk, struct ast_format_cap *result)
{
	if (instance->engine->available_formats) {
		ao2_lock(instance);
		instance->engine->available_formats(instance, to_endpoint, to_asterisk, result);
		ao2_unlock(instance);
		if (ast_format_cap_count(result)) {
			return;
		}
	}

	ast_translate_available_formats(to_endpoint, to_asterisk, result);
}

int ast_rtp_instance_activate(struct ast_rtp_instance *instance)
{
	int res;

	if (instance->engine->activate) {
		ao2_lock(instance);
		res = instance->engine->activate(instance);
		ao2_unlock(instance);
	} else {
		res = 0;
	}
	return res;
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

int ast_rtp_instance_add_srtp_policy(struct ast_rtp_instance *instance, struct ast_srtp_policy *remote_policy, struct ast_srtp_policy *local_policy, int rtcp)
{
	int res = 0;
	struct ast_srtp **srtp;

	if (!res_srtp) {
		return -1;
	}

	ao2_lock(instance);

	srtp = rtcp ? &instance->rtcp_srtp : &instance->srtp;

	if (!*srtp) {
		res = res_srtp->create(srtp, instance, remote_policy);
	} else if (remote_policy) {
		res = res_srtp->replace(srtp, instance, remote_policy);
	}
	if (!res) {
		res = res_srtp->add_stream(*srtp, local_policy);
	}

	ao2_unlock(instance);

	return res;
}

struct ast_srtp *ast_rtp_instance_get_srtp(struct ast_rtp_instance *instance, int rtcp)
{
	if (rtcp && instance->rtcp_srtp) {
		return instance->rtcp_srtp;
	} else {
		return instance->srtp;
	}
}

int ast_rtp_instance_sendcng(struct ast_rtp_instance *instance, int level)
{
	int res;

	if (instance->engine->sendcng) {
		ao2_lock(instance);
		res = instance->engine->sendcng(instance, level);
		ao2_unlock(instance);
	} else {
		res = -1;
	}
	return res;
}

static void rtp_ice_wrap_set_authentication(struct ast_rtp_instance *instance, const char *ufrag, const char *password)
{
	ao2_lock(instance);
	instance->engine->ice->set_authentication(instance, ufrag, password);
	ao2_unlock(instance);
}

static void rtp_ice_wrap_add_remote_candidate(struct ast_rtp_instance *instance, const struct ast_rtp_engine_ice_candidate *candidate)
{
	ao2_lock(instance);
	instance->engine->ice->add_remote_candidate(instance, candidate);
	ao2_unlock(instance);
}

static void rtp_ice_wrap_start(struct ast_rtp_instance *instance)
{
	ao2_lock(instance);
	instance->engine->ice->start(instance);
	ao2_unlock(instance);
}

static void rtp_ice_wrap_stop(struct ast_rtp_instance *instance)
{
	ao2_lock(instance);
	instance->engine->ice->stop(instance);
	ao2_unlock(instance);
}

static const char *rtp_ice_wrap_get_ufrag(struct ast_rtp_instance *instance)
{
	const char *ufrag;

	ao2_lock(instance);
	ufrag = instance->engine->ice->get_ufrag(instance);
	ao2_unlock(instance);
	return ufrag;
}

static const char *rtp_ice_wrap_get_password(struct ast_rtp_instance *instance)
{
	const char *password;

	ao2_lock(instance);
	password = instance->engine->ice->get_password(instance);
	ao2_unlock(instance);
	return password;
}

static struct ao2_container *rtp_ice_wrap_get_local_candidates(struct ast_rtp_instance *instance)
{
	struct ao2_container *local_candidates;

	ao2_lock(instance);
	local_candidates = instance->engine->ice->get_local_candidates(instance);
	ao2_unlock(instance);
	return local_candidates;
}

static void rtp_ice_wrap_ice_lite(struct ast_rtp_instance *instance)
{
	ao2_lock(instance);
	instance->engine->ice->ice_lite(instance);
	ao2_unlock(instance);
}

static void rtp_ice_wrap_set_role(struct ast_rtp_instance *instance,
	enum ast_rtp_ice_role role)
{
	ao2_lock(instance);
	instance->engine->ice->set_role(instance, role);
	ao2_unlock(instance);
}

static void rtp_ice_wrap_turn_request(struct ast_rtp_instance *instance,
	enum ast_rtp_ice_component_type component, enum ast_transport transport,
	const char *server, unsigned int port, const char *username, const char *password)
{
	ao2_lock(instance);
	instance->engine->ice->turn_request(instance, component, transport, server, port,
		username, password);
	ao2_unlock(instance);
}

static void rtp_ice_wrap_change_components(struct ast_rtp_instance *instance,
	int num_components)
{
	ao2_lock(instance);
	instance->engine->ice->change_components(instance, num_components);
	ao2_unlock(instance);
}

static struct ast_rtp_engine_ice rtp_ice_wrappers = {
	.set_authentication = rtp_ice_wrap_set_authentication,
	.add_remote_candidate = rtp_ice_wrap_add_remote_candidate,
	.start = rtp_ice_wrap_start,
	.stop = rtp_ice_wrap_stop,
	.get_ufrag = rtp_ice_wrap_get_ufrag,
	.get_password = rtp_ice_wrap_get_password,
	.get_local_candidates = rtp_ice_wrap_get_local_candidates,
	.ice_lite = rtp_ice_wrap_ice_lite,
	.set_role = rtp_ice_wrap_set_role,
	.turn_request = rtp_ice_wrap_turn_request,
	.change_components = rtp_ice_wrap_change_components,
};

struct ast_rtp_engine_ice *ast_rtp_instance_get_ice(struct ast_rtp_instance *instance)
{
	if (instance->engine->ice) {
		return &rtp_ice_wrappers;
	}
	/* ICE not available */
	return NULL;
}

#ifdef TEST_FRAMEWORK
struct ast_rtp_engine_test *ast_rtp_instance_get_test(struct ast_rtp_instance *instance)
{
	return instance->engine->test;
}
#endif

static int rtp_dtls_wrap_set_configuration(struct ast_rtp_instance *instance,
	const struct ast_rtp_dtls_cfg *dtls_cfg)
{
	int set_configuration;

	ao2_lock(instance);
	set_configuration = instance->engine->dtls->set_configuration(instance, dtls_cfg);
	ao2_unlock(instance);
	return set_configuration;
}

static int rtp_dtls_wrap_active(struct ast_rtp_instance *instance)
{
	int active;

	ao2_lock(instance);
	active = instance->engine->dtls->active(instance);
	ao2_unlock(instance);
	return active;
}

static void rtp_dtls_wrap_stop(struct ast_rtp_instance *instance)
{
	ao2_lock(instance);
	instance->engine->dtls->stop(instance);
	ao2_unlock(instance);
}

static void rtp_dtls_wrap_reset(struct ast_rtp_instance *instance)
{
	ao2_lock(instance);
	instance->engine->dtls->reset(instance);
	ao2_unlock(instance);
}

static enum ast_rtp_dtls_connection rtp_dtls_wrap_get_connection(struct ast_rtp_instance *instance)
{
	enum ast_rtp_dtls_connection get_connection;

	ao2_lock(instance);
	get_connection = instance->engine->dtls->get_connection(instance);
	ao2_unlock(instance);
	return get_connection;
}

static enum ast_rtp_dtls_setup rtp_dtls_wrap_get_setup(struct ast_rtp_instance *instance)
{
	enum ast_rtp_dtls_setup get_setup;

	ao2_lock(instance);
	get_setup = instance->engine->dtls->get_setup(instance);
	ao2_unlock(instance);
	return get_setup;
}

static void rtp_dtls_wrap_set_setup(struct ast_rtp_instance *instance,
	enum ast_rtp_dtls_setup setup)
{
	ao2_lock(instance);
	instance->engine->dtls->set_setup(instance, setup);
	ao2_unlock(instance);
}

static void rtp_dtls_wrap_set_fingerprint(struct ast_rtp_instance *instance,
	enum ast_rtp_dtls_hash hash, const char *fingerprint)
{
	ao2_lock(instance);
	instance->engine->dtls->set_fingerprint(instance, hash, fingerprint);
	ao2_unlock(instance);
}

static enum ast_rtp_dtls_hash rtp_dtls_wrap_get_fingerprint_hash(struct ast_rtp_instance *instance)
{
	enum ast_rtp_dtls_hash get_fingerprint_hash;

	ao2_lock(instance);
	get_fingerprint_hash = instance->engine->dtls->get_fingerprint_hash(instance);
	ao2_unlock(instance);
	return get_fingerprint_hash;
}

static const char *rtp_dtls_wrap_get_fingerprint(struct ast_rtp_instance *instance)
{
	const char *get_fingerprint;

	ao2_lock(instance);
	get_fingerprint = instance->engine->dtls->get_fingerprint(instance);
	ao2_unlock(instance);
	return get_fingerprint;
}

static struct ast_rtp_engine_dtls rtp_dtls_wrappers = {
	.set_configuration = rtp_dtls_wrap_set_configuration,
	.active = rtp_dtls_wrap_active,
	.stop = rtp_dtls_wrap_stop,
	.reset = rtp_dtls_wrap_reset,
	.get_connection = rtp_dtls_wrap_get_connection,
	.get_setup = rtp_dtls_wrap_get_setup,
	.set_setup = rtp_dtls_wrap_set_setup,
	.set_fingerprint = rtp_dtls_wrap_set_fingerprint,
	.get_fingerprint_hash = rtp_dtls_wrap_get_fingerprint_hash,
	.get_fingerprint = rtp_dtls_wrap_get_fingerprint,
};

struct ast_rtp_engine_dtls *ast_rtp_instance_get_dtls(struct ast_rtp_instance *instance)
{
	if (instance->engine->dtls) {
		return &rtp_dtls_wrappers;
	}
	/* DTLS not available */
	return NULL;
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
	} else if (!strcasecmp(name, "dtlsautogeneratecert")) {
		dtls_cfg->ephemeral_cert = ast_true(value) ? 1 : 0;
	} else if (!strcasecmp(name, "dtlscertfile")) {
		if (!ast_strlen_zero(value) && !ast_file_is_readable(value)) {
			ast_log(LOG_ERROR, "%s file %s does not exist or is not readable\n", name, value);
			return -1;
		}
		ast_free(dtls_cfg->certfile);
		dtls_cfg->certfile = ast_strdup(value);
	} else if (!strcasecmp(name, "dtlsprivatekey")) {
		if (!ast_strlen_zero(value) && !ast_file_is_readable(value)) {
			ast_log(LOG_ERROR, "%s file %s does not exist or is not readable\n", name, value);
			return -1;
		}
		ast_free(dtls_cfg->pvtfile);
		dtls_cfg->pvtfile = ast_strdup(value);
	} else if (!strcasecmp(name, "dtlscipher")) {
		ast_free(dtls_cfg->cipher);
		dtls_cfg->cipher = ast_strdup(value);
	} else if (!strcasecmp(name, "dtlscafile")) {
		if (!ast_strlen_zero(value) && !ast_file_is_readable(value)) {
			ast_log(LOG_ERROR, "%s file %s does not exist or is not readable\n", name, value);
			return -1;
		}
		ast_free(dtls_cfg->cafile);
		dtls_cfg->cafile = ast_strdup(value);
	} else if (!strcasecmp(name, "dtlscapath") || !strcasecmp(name, "dtlscadir")) {
		if (!ast_strlen_zero(value) && !ast_file_is_readable(value)) {
			ast_log(LOG_ERROR, "%s file %s does not exist or is not readable\n", name, value);
			return -1;
		}
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

int ast_rtp_dtls_cfg_validate(struct ast_rtp_dtls_cfg *dtls_cfg)
{
	if (dtls_cfg->ephemeral_cert) {
		if (!ast_strlen_zero(dtls_cfg->certfile)) {
			ast_log(LOG_ERROR, "You cannot request automatically generated certificates"
				" (dtls_auto_generate_cert) and also specify a certificate file"
				" (dtls_cert_file) at the same time\n");
			return -1;
		} else if (!ast_strlen_zero(dtls_cfg->pvtfile)
				  || !ast_strlen_zero(dtls_cfg->cafile)
				  || !ast_strlen_zero(dtls_cfg->capath)) {
			ast_log(LOG_NOTICE, "dtls_pvt_file, dtls_cafile, and dtls_ca_path are"
				" ignored when dtls_auto_generate_cert is enabled\n");
		}
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
	dst_cfg->ephemeral_cert = src_cfg->ephemeral_cert;
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
 * \c ast_rtp_mime_types.
 */
static void rtp_engine_mime_type_cleanup(int i)
{
	ao2_cleanup(ast_rtp_mime_types[i].payload_type.format);
	memset(&ast_rtp_mime_types[i], 0, sizeof(struct ast_rtp_mime_type));
}

static void set_next_mime_type(struct ast_format *format, int rtp_code, const char *type, const char *subtype, unsigned int sample_rate)
{
	int x;

	ast_rwlock_wrlock(&mime_types_lock);

	x = mime_types_len;
	if (ARRAY_LEN(ast_rtp_mime_types) <= x) {
		ast_rwlock_unlock(&mime_types_lock);
		return;
	}

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

static void add_static_payload(int payload, struct ast_format *format, int rtp_code)
{
	struct ast_rtp_payload_type *type;

	/*
	 * ARRAY_LEN's result is cast to an int so 'map' is not autocast to a size_t,
	 * which if negative would cause an assertion.
	 */
	ast_assert(payload < (int)ARRAY_LEN(static_RTP_PT));

	if (ast_option_rtpusedynamic && payload < 0) {
		/*
		 * We're going to build dynamic payloads dynamically. An RTP code is
		 * required otherwise one will be dynamically allocated per instance.
		 */
		return;
	}

	/*
	 * Either the given payload is truly a static type, or Asterisk is
	 * globally storing the dynamic payloads in the static_RTP_PT object.
	 */
	ast_rwlock_wrlock(&static_RTP_PT_lock);

	if (payload < 0) {
		/*
		 * This is a dynamic payload that will be stored globally,
		 * so find the next available empty slot.
		 */
		payload = find_unused_payload(NULL);
		if (payload < 0) {
			ast_log(LOG_WARNING, "No dynamic RTP payload type values available "
				"for %s - %d!\n", format ? ast_format_get_name(format) : "", rtp_code);
			ast_rwlock_unlock(&static_RTP_PT_lock);
			return;
		}
	}

	type = rtp_payload_type_alloc(format, payload, rtp_code, 1);
	if (type) {
		ao2_cleanup(static_RTP_PT[payload]);
		static_RTP_PT[payload] = type;
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);
}

int ast_rtp_engine_load_format(struct ast_format *format)
{
	char *codec_name = ast_strdupa(ast_format_get_codec_name(format));

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
		if (static_RTP_PT[x]
			&& ast_format_cmp(static_RTP_PT[x]->format, format) == AST_FORMAT_CMP_EQUAL) {
			ao2_ref(static_RTP_PT[x], -1);
			static_RTP_PT[x] = NULL;
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
			(unsigned long)payload->report->sender_information.ntp_timestamp.tv_usec);
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
	struct ast_json *json_rtcp_report = NULL;
	struct ast_json *json_rtcp_report_blocks;
	struct ast_json *json_rtcp_sender_info = NULL;
	struct ast_json *json_channel = NULL;
	int i;

	json_rtcp_report_blocks = ast_json_array_create();
	if (!json_rtcp_report_blocks) {
		return NULL;
	}

	for (i = 0; i < payload->report->reception_report_count && payload->report->report_block[i]; i++) {
		struct ast_json *json_report_block;
		char str_lsr[32];

		snprintf(str_lsr, sizeof(str_lsr), "%u", payload->report->report_block[i]->lsr);
		json_report_block = ast_json_pack("{s: I, s: I, s: I, s: I, s: I, s: s, s: I}",
			"source_ssrc", (ast_json_int_t)payload->report->report_block[i]->source_ssrc,
			"fraction_lost", (ast_json_int_t)payload->report->report_block[i]->lost_count.fraction,
			"packets_lost", (ast_json_int_t)payload->report->report_block[i]->lost_count.packets,
			"highest_seq_no", (ast_json_int_t)payload->report->report_block[i]->highest_seq_no,
			"ia_jitter", (ast_json_int_t)payload->report->report_block[i]->ia_jitter,
			"lsr", str_lsr,
			"dlsr", (ast_json_int_t)payload->report->report_block[i]->dlsr);
		if (!json_report_block
			|| ast_json_array_append(json_rtcp_report_blocks, json_report_block)) {
			ast_json_unref(json_rtcp_report_blocks);
			return NULL;
		}
	}

	if (payload->report->type == AST_RTP_RTCP_SR) {
		char sec[32];
		char usec[32];

		snprintf(sec, sizeof(sec), "%lu", (unsigned long)payload->report->sender_information.ntp_timestamp.tv_sec);
		snprintf(usec, sizeof(usec), "%lu", (unsigned long)payload->report->sender_information.ntp_timestamp.tv_usec);
		json_rtcp_sender_info = ast_json_pack("{s: s, s: s, s: I, s: I, s: I}",
			"ntp_timestamp_sec", sec,
			"ntp_timestamp_usec", usec,
			"rtp_timestamp", (ast_json_int_t)payload->report->sender_information.rtp_timestamp,
			"packets", (ast_json_int_t)payload->report->sender_information.packet_count,
			"octets", (ast_json_int_t)payload->report->sender_information.octet_count);
		if (!json_rtcp_sender_info) {
			ast_json_unref(json_rtcp_report_blocks);
			return NULL;
		}
	}

	json_rtcp_report = ast_json_pack("{s: I, s: I, s: i, s: o, s: o}",
		"ssrc", (ast_json_int_t)payload->report->ssrc,
		"type", (ast_json_int_t)payload->report->type,
		"report_count", payload->report->reception_report_count,
		"sender_information", json_rtcp_sender_info ?: ast_json_null(),
		"report_blocks", json_rtcp_report_blocks);
	if (!json_rtcp_report) {
		return NULL;
	}

	if (payload->snapshot) {
		json_channel = ast_channel_snapshot_to_json(payload->snapshot, sanitize);
		if (!json_channel) {
			ast_json_unref(json_rtcp_report);
			return NULL;
		}
	}

	return ast_json_pack("{s: s, s: o?, s: o, s: O?}",
		"type", stasis_message_type(msg) == ast_rtp_rtcp_received_type() ? "RTCPReceived" : "RTCPSent",
		"channel", json_channel,
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

static uintmax_t debug_category_rtp_id;

uintmax_t ast_debug_category_rtp_id(void)
{
	return debug_category_rtp_id;
}

static uintmax_t debug_category_rtp_packet_id;

uintmax_t ast_debug_category_rtp_packet_id(void)
{
	return debug_category_rtp_packet_id;
}

static uintmax_t debug_category_rtcp_id;

uintmax_t ast_debug_category_rtcp_id(void)
{
	return debug_category_rtcp_id;
}

static uintmax_t debug_category_rtcp_packet_id;

uintmax_t ast_debug_category_rtcp_packet_id(void)
{
	return debug_category_rtcp_packet_id;
}

static uintmax_t debug_category_dtls_id;

uintmax_t ast_debug_category_dtls_id(void)
{
	return debug_category_dtls_id;
}

static uintmax_t debug_category_dtls_packet_id;

uintmax_t ast_debug_category_dtls_packet_id(void)
{
	return debug_category_dtls_packet_id;
}

static uintmax_t debug_category_ice_id;

uintmax_t ast_debug_category_ice_id(void)
{
	return debug_category_ice_id;
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
		ao2_cleanup(static_RTP_PT[x]);
		static_RTP_PT[x] = NULL;
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);

	ast_rwlock_wrlock(&mime_types_lock);
	for (x = 0; x < mime_types_len; x++) {
		if (ast_rtp_mime_types[x].payload_type.format) {
			rtp_engine_mime_type_cleanup(x);
		}
	}
	mime_types_len = 0;
	ast_rwlock_unlock(&mime_types_lock);

	ast_debug_category_unregister(AST_LOG_CATEGORY_ICE);

	ast_debug_category_unregister(AST_LOG_CATEGORY_DTLS_PACKET);
	ast_debug_category_unregister(AST_LOG_CATEGORY_DTLS);

	ast_debug_category_unregister(AST_LOG_CATEGORY_RTCP_PACKET);
	ast_debug_category_unregister(AST_LOG_CATEGORY_RTCP);

	ast_debug_category_unregister(AST_LOG_CATEGORY_RTP_PACKET);
	ast_debug_category_unregister(AST_LOG_CATEGORY_RTP);
}

int ast_rtp_engine_init(void)
{
	ast_rwlock_init(&mime_types_lock);
	ast_rwlock_init(&static_RTP_PT_lock);

	rtp_topic = stasis_topic_create("rtp:all");
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
	set_next_mime_type(ast_format_slin12, 0, "audio", "L16", 12000);
	set_next_mime_type(ast_format_slin24, 0, "audio", "L16", 24000);
	set_next_mime_type(ast_format_slin32, 0, "audio", "L16", 32000);
	set_next_mime_type(ast_format_slin44, 0, "audio", "L16", 44000);
	set_next_mime_type(ast_format_slin48, 0, "audio", "L16", 48000);
	set_next_mime_type(ast_format_slin96, 0, "audio", "L16", 96000);
	set_next_mime_type(ast_format_slin192, 0, "audio", "L16", 192000);
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
	set_next_mime_type(ast_format_h265, 0, "video", "H265", 90000);
	set_next_mime_type(ast_format_mp4, 0, "video", "MP4V-ES", 90000);
	set_next_mime_type(ast_format_t140_red, 0, "text", "RED", 1000);
	set_next_mime_type(ast_format_t140, 0, "text", "T140", 1000);
	set_next_mime_type(ast_format_siren7, 0, "audio", "G7221", 16000);
	set_next_mime_type(ast_format_siren14, 0, "audio", "G7221", 32000);
	set_next_mime_type(ast_format_g719, 0, "audio", "G719", 48000);
	/* Opus, VP8, and VP9 */
	set_next_mime_type(ast_format_opus, 0,  "audio", "opus", 48000);
	set_next_mime_type(ast_format_vp8, 0,  "video", "VP8", 90000);
	set_next_mime_type(ast_format_vp9, 0, "video", "VP9", 90000);

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

	/*
	 * Dynamic payload types - Even when dynamically assigning them we'll fall
	 * back to using the statically declared values as the default number.
	 */
	add_static_payload(96, ast_format_slin192, 0);
	add_static_payload(97, ast_format_ilbc, 0);

	add_static_payload(99, ast_format_h264, 0);
	add_static_payload(100, ast_format_vp8, 0);
	add_static_payload(101, NULL, AST_RTP_DTMF);
	add_static_payload(102, ast_format_siren7, 0);
	add_static_payload(103, ast_format_h263p, 0);
	add_static_payload(104, ast_format_mp4, 0);
	add_static_payload(105, ast_format_t140_red, 0);   /* Real time text chat (with redundancy encoding) */
	add_static_payload(106, ast_format_t140, 0);     /* Real time text chat */
	add_static_payload(107, ast_format_opus, 0);
	add_static_payload(108, ast_format_vp9, 0);
	add_static_payload(109, ast_format_h265, 0);

	add_static_payload(110, ast_format_speex, 0);
	add_static_payload(111, ast_format_g726, 0);
	add_static_payload(112, ast_format_g726_aal2, 0);

	add_static_payload(115, ast_format_siren14, 0);
	add_static_payload(116, ast_format_g719, 0);
	add_static_payload(117, ast_format_speex16, 0);
	add_static_payload(118, ast_format_slin16, 0); /* 16 Khz signed linear */
	add_static_payload(119, ast_format_speex32, 0);

	add_static_payload(121, NULL, AST_RTP_CISCO_DTMF);   /* Must be type 121 */
	add_static_payload(122, ast_format_slin12, 0);
	add_static_payload(123, ast_format_slin24, 0);
	add_static_payload(124, ast_format_slin32, 0);
	add_static_payload(125, ast_format_slin44, 0);
	add_static_payload(126, ast_format_slin48, 0);
	add_static_payload(127, ast_format_slin96, 0);
	/* payload types above 127 are not valid */

	debug_category_rtp_id = ast_debug_category_register(AST_LOG_CATEGORY_RTP);
	debug_category_rtp_packet_id = ast_debug_category_register(AST_LOG_CATEGORY_RTP_PACKET);
	debug_category_rtcp_id = ast_debug_category_register(AST_LOG_CATEGORY_RTCP);
	debug_category_rtcp_packet_id = ast_debug_category_register(AST_LOG_CATEGORY_RTCP_PACKET);
	debug_category_dtls_id = ast_debug_category_register(AST_LOG_CATEGORY_DTLS);
	debug_category_dtls_packet_id = ast_debug_category_register(AST_LOG_CATEGORY_DTLS_PACKET);
	debug_category_ice_id = ast_debug_category_register(AST_LOG_CATEGORY_ICE);

	return 0;
}

time_t ast_rtp_instance_get_last_tx(const struct ast_rtp_instance *rtp)
{
	return rtp->last_tx;
}

void ast_rtp_instance_set_last_tx(struct ast_rtp_instance *rtp, time_t time)
{
	rtp->last_tx = time;
}

time_t ast_rtp_instance_get_last_rx(const struct ast_rtp_instance *rtp)
{
	return rtp->last_rx;
}

void ast_rtp_instance_set_last_rx(struct ast_rtp_instance *rtp, time_t time)
{
	rtp->last_rx = time;
}

unsigned int ast_rtp_instance_get_ssrc(struct ast_rtp_instance *rtp)
{
	unsigned int ssrc = 0;

	ao2_lock(rtp);
	if (rtp->engine->ssrc_get) {
		ssrc = rtp->engine->ssrc_get(rtp);
	}
	ao2_unlock(rtp);

	return ssrc;
}

const char *ast_rtp_instance_get_cname(struct ast_rtp_instance *rtp)
{
	const char *cname = "";

	ao2_lock(rtp);
	if (rtp->engine->cname_get) {
		cname = rtp->engine->cname_get(rtp);
	}
	ao2_unlock(rtp);

	return cname;
}

int ast_rtp_instance_bundle(struct ast_rtp_instance *child, struct ast_rtp_instance *parent)
{
	int res = -1;

	if (parent && (child->engine != parent->engine)) {
		return -1;
	}

	ao2_lock(child);
	if (child->engine->bundle) {
		res = child->engine->bundle(child, parent);
	}
	ao2_unlock(child);

	return res;
}

void ast_rtp_instance_set_remote_ssrc(struct ast_rtp_instance *rtp, unsigned int ssrc)
{
	ao2_lock(rtp);
	if (rtp->engine->set_remote_ssrc) {
		rtp->engine->set_remote_ssrc(rtp, ssrc);
	}
	ao2_unlock(rtp);
}

void ast_rtp_instance_set_stream_num(struct ast_rtp_instance *rtp, int stream_num)
{
	ao2_lock(rtp);
	if (rtp->engine->set_stream_num) {
		rtp->engine->set_stream_num(rtp, stream_num);
	}
	ao2_unlock(rtp);
}

#ifdef TEST_FRAMEWORK
size_t ast_rtp_instance_get_recv_buffer_max(struct ast_rtp_instance *instance)
{
	size_t res;
	struct ast_rtp_engine_test *test = ast_rtp_instance_get_test(instance);

	if (!test) {
		ast_log(LOG_ERROR, "There is no test engine set up!\n");
		return 0;
	}

	ao2_lock(instance);
	res = test->recv_buffer_max(instance);
	ao2_unlock(instance);

	return res;
}

size_t ast_rtp_instance_get_recv_buffer_count(struct ast_rtp_instance *instance)
{
	size_t res;
	struct ast_rtp_engine_test *test = ast_rtp_instance_get_test(instance);

	if (!test) {
		ast_log(LOG_ERROR, "There is no test engine set up!\n");
		return 0;
	}

	ao2_lock(instance);
	res = test->recv_buffer_count(instance);
	ao2_unlock(instance);

	return res;
}

size_t ast_rtp_instance_get_send_buffer_count(struct ast_rtp_instance *instance)
{
	size_t res;
	struct ast_rtp_engine_test *test = ast_rtp_instance_get_test(instance);

	if (!test) {
		ast_log(LOG_ERROR, "There is no test engine set up!\n");
		return 0;
	}

	ao2_lock(instance);
	res = test->send_buffer_count(instance);
	ao2_unlock(instance);

	return res;
}

void ast_rtp_instance_set_schedid(struct ast_rtp_instance *instance, int id)
{
	struct ast_rtp_engine_test *test = ast_rtp_instance_get_test(instance);

	if (!test) {
		ast_log(LOG_ERROR, "There is no test engine set up!\n");
		return;
	}

	ao2_lock(instance);
	test->set_schedid(instance, id);
	ao2_unlock(instance);
}

void ast_rtp_instance_drop_packets(struct ast_rtp_instance *instance, int num)
{
	struct ast_rtp_engine_test *test = ast_rtp_instance_get_test(instance);

	if (!test) {
		ast_log(LOG_ERROR, "There is no test engine set up!\n");
		return;
	}

	test->packets_to_drop = num;
}

void ast_rtp_instance_queue_report(struct ast_rtp_instance *instance)
{
	struct ast_rtp_engine_test *test = ast_rtp_instance_get_test(instance);

	if (!test) {
		ast_log(LOG_ERROR, "There is no test engine set up!\n");
		return;
	}

	test->send_report = 1;
}

int ast_rtp_instance_get_sdes_received(struct ast_rtp_instance *instance)
{
	struct ast_rtp_engine_test *test = ast_rtp_instance_get_test(instance);

	if (!test) {
		ast_log(LOG_ERROR, "There is no test engine set up!\n");
		return 0;
	}

	return test->sdes_received;
}

void ast_rtp_instance_reset_test_engine(struct ast_rtp_instance *instance)
{
	struct ast_rtp_engine_test *test = ast_rtp_instance_get_test(instance);

	if (!test) {
		ast_log(LOG_ERROR, "There is no test engine set up!\n");
		return;
	}

	test->packets_to_drop = 0;
	test->send_report = 0;
	test->sdes_received = 0;
}
#endif

struct ast_json *ast_rtp_convert_stats_json(const struct ast_rtp_instance_stats *stats)
{
	struct ast_json *j_res;
	int ret;

	j_res = ast_json_object_create();
	if (!j_res) {
		return NULL;
	}

	/* set mandatory items */
	ret = ast_json_object_set(j_res, "txcount", ast_json_integer_create(stats->txcount));
	ret |= ast_json_object_set(j_res, "rxcount", ast_json_integer_create(stats->rxcount));

	ret |= ast_json_object_set(j_res, "txploss", ast_json_integer_create(stats->txploss));
	ret |= ast_json_object_set(j_res, "rxploss", ast_json_integer_create(stats->rxploss));

	ret |= ast_json_object_set(j_res, "local_ssrc", ast_json_integer_create(stats->local_ssrc));
	ret |= ast_json_object_set(j_res, "remote_ssrc", ast_json_integer_create(stats->remote_ssrc));

	ret |= ast_json_object_set(j_res, "txoctetcount", ast_json_integer_create(stats->txoctetcount));
	ret |= ast_json_object_set(j_res, "rxoctetcount", ast_json_integer_create(stats->rxoctetcount));

	ret |= ast_json_object_set(j_res, "channel_uniqueid", ast_json_string_create(stats->channel_uniqueid));
	if (ret) {
		ast_log(LOG_WARNING, "Could not create rtp statistics info. channel: %s\n", stats->channel_uniqueid);
		ast_json_unref(j_res);
		return NULL;
	}

	/* set other items */
	SET_AST_JSON_OBJ(j_res, "txjitter", ast_json_real_create(stats->txjitter));
	SET_AST_JSON_OBJ(j_res, "rxjitter", ast_json_real_create(stats->rxjitter));

	SET_AST_JSON_OBJ(j_res, "remote_maxjitter", ast_json_real_create(stats->remote_maxjitter));
	SET_AST_JSON_OBJ(j_res, "remote_minjitter", ast_json_real_create(stats->remote_minjitter));
	SET_AST_JSON_OBJ(j_res, "remote_normdevjitter", ast_json_real_create(stats->remote_normdevjitter));
	SET_AST_JSON_OBJ(j_res, "remote_stdevjitter", ast_json_real_create(stats->remote_stdevjitter));

	SET_AST_JSON_OBJ(j_res, "local_maxjitter", ast_json_real_create(stats->local_maxjitter));
	SET_AST_JSON_OBJ(j_res, "local_minjitter", ast_json_real_create(stats->local_minjitter));
	SET_AST_JSON_OBJ(j_res, "local_normdevjitter", ast_json_real_create(stats->local_normdevjitter));
	SET_AST_JSON_OBJ(j_res, "local_stdevjitter", ast_json_real_create(stats->local_stdevjitter));

	SET_AST_JSON_OBJ(j_res, "remote_maxrxploss", ast_json_real_create(stats->remote_maxrxploss));
	SET_AST_JSON_OBJ(j_res, "remote_minrxploss", ast_json_real_create(stats->remote_minrxploss));
	SET_AST_JSON_OBJ(j_res, "remote_normdevrxploss", ast_json_real_create(stats->remote_normdevrxploss));
	SET_AST_JSON_OBJ(j_res, "remote_stdevrxploss", ast_json_real_create(stats->remote_stdevrxploss));

	SET_AST_JSON_OBJ(j_res, "local_maxrxploss", ast_json_real_create(stats->local_maxrxploss));
	SET_AST_JSON_OBJ(j_res, "local_minrxploss", ast_json_real_create(stats->local_minrxploss));
	SET_AST_JSON_OBJ(j_res, "local_normdevrxploss", ast_json_real_create(stats->local_normdevrxploss));
	SET_AST_JSON_OBJ(j_res, "local_stdevrxploss", ast_json_real_create(stats->local_stdevrxploss));

	SET_AST_JSON_OBJ(j_res, "rtt", ast_json_real_create(stats->rtt));
	SET_AST_JSON_OBJ(j_res, "maxrtt", ast_json_real_create(stats->maxrtt));
	SET_AST_JSON_OBJ(j_res, "minrtt", ast_json_real_create(stats->minrtt));
	SET_AST_JSON_OBJ(j_res, "normdevrtt", ast_json_real_create(stats->normdevrtt));
	SET_AST_JSON_OBJ(j_res, "stdevrtt", ast_json_real_create(stats->stdevrtt));

	return j_res;
}

struct ast_json *ast_rtp_instance_get_stats_all_json(struct ast_rtp_instance *instance)
{
	struct ast_rtp_instance_stats stats = {0,};

	if(ast_rtp_instance_get_stats(instance, &stats, AST_RTP_INSTANCE_STAT_ALL)) {
		return NULL;
	}

	return ast_rtp_convert_stats_json(&stats);
}

int ast_rtp_get_rate(const struct ast_format *format)
{
	/* For those wondering: due to a fluke in RFC publication, G.722 is advertised
	 * as having a sample rate of 8kHz, while implementations must know that its
	 * real rate is 16kHz. Seriously.
	 */
        return (ast_format_cmp(format, ast_format_g722) == AST_FORMAT_CMP_EQUAL) ? 8000 : (int)ast_format_get_sample_rate(format);
}
