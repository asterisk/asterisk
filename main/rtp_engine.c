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
	/*! Address that we are sending RTP to */
	struct ast_sockaddr remote_address;
	/*! Alternate address that we are receiving RTP from */
	struct ast_sockaddr alt_remote_address;
	/*! Instance that we are bridged to if doing remote or local bridging */
	struct ast_rtp_instance *bridged;
	/*! Payload and packetization information */
	struct ast_rtp_codecs codecs;
	/*! RTP timeout time (negative or zero means disabled, negative value means temporarily disabled) */
	int timeout;
	/*! RTP timeout when on hold (negative or zero means disabled, negative value means temporarily disabled). */
	int holdtimeout;
	/*! DTMF mode in use */
	enum ast_rtp_dtmf_mode dtmf_mode;
	/*! Glue currently in use */
	struct ast_rtp_glue *glue;
	/*! Channel associated with the instance */
	struct ast_channel *chan;
	/*! SRTP info associated with the instance */
	struct ast_srtp *srtp;
};

/*! List of RTP engines that are currently registered */
static AST_RWLIST_HEAD_STATIC(engines, ast_rtp_engine);

/*! List of RTP glues */
static AST_RWLIST_HEAD_STATIC(glues, ast_rtp_glue);

/*! The following array defines the MIME Media type (and subtype) for each
   of our codecs, or RTP-specific data type. */
static const struct ast_rtp_mime_type {
	struct ast_rtp_payload_type payload_type;
	char *type;
	char *subtype;
	unsigned int sample_rate;
} ast_rtp_mime_types[] = {
	{{1, AST_FORMAT_G723_1}, "audio", "G723", 8000},
	{{1, AST_FORMAT_GSM}, "audio", "GSM", 8000},
	{{1, AST_FORMAT_ULAW}, "audio", "PCMU", 8000},
	{{1, AST_FORMAT_ULAW}, "audio", "G711U", 8000},
	{{1, AST_FORMAT_ALAW}, "audio", "PCMA", 8000},
	{{1, AST_FORMAT_ALAW}, "audio", "G711A", 8000},
	{{1, AST_FORMAT_G726}, "audio", "G726-32", 8000},
	{{1, AST_FORMAT_ADPCM}, "audio", "DVI4", 8000},
	{{1, AST_FORMAT_SLINEAR}, "audio", "L16", 8000},
	{{1, AST_FORMAT_SLINEAR16}, "audio", "L16", 16000},
	{{1, AST_FORMAT_LPC10}, "audio", "LPC", 8000},
	{{1, AST_FORMAT_G729A}, "audio", "G729", 8000},
	{{1, AST_FORMAT_G729A}, "audio", "G729A", 8000},
	{{1, AST_FORMAT_G729A}, "audio", "G.729", 8000},
	{{1, AST_FORMAT_SPEEX}, "audio", "speex", 8000},
	{{1, AST_FORMAT_SPEEX16}, "audio", "speex", 16000},
	{{1, AST_FORMAT_ILBC}, "audio", "iLBC", 8000},
	/* this is the sample rate listed in the RTP profile for the G.722
	              codec, *NOT* the actual sample rate of the media stream
	*/
	{{1, AST_FORMAT_G722}, "audio", "G722", 8000},
	{{1, AST_FORMAT_G726_AAL2}, "audio", "AAL2-G726-32", 8000},
	{{0, AST_RTP_DTMF}, "audio", "telephone-event", 8000},
	{{0, AST_RTP_CISCO_DTMF}, "audio", "cisco-telephone-event", 8000},
	{{0, AST_RTP_CN}, "audio", "CN", 8000},
	{{1, AST_FORMAT_JPEG}, "video", "JPEG", 90000},
	{{1, AST_FORMAT_PNG}, "video", "PNG", 90000},
	{{1, AST_FORMAT_H261}, "video", "H261", 90000},
	{{1, AST_FORMAT_H263}, "video", "H263", 90000},
	{{1, AST_FORMAT_H263_PLUS}, "video", "h263-1998", 90000},
	{{1, AST_FORMAT_H264}, "video", "H264", 90000},
	{{1, AST_FORMAT_MP4_VIDEO}, "video", "MP4V-ES", 90000},
	{{1, AST_FORMAT_T140RED}, "text", "RED", 1000},
	{{1, AST_FORMAT_T140}, "text", "T140", 1000},
	{{1, AST_FORMAT_SIREN7}, "audio", "G7221", 16000},
	{{1, AST_FORMAT_SIREN14}, "audio", "G7221", 32000},
	{{1, AST_FORMAT_G719}, "audio", "G719", 48000},
};

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
static const struct ast_rtp_payload_type static_RTP_PT[AST_RTP_MAX_PT] = {
	[0] = {1, AST_FORMAT_ULAW},
	#ifdef USE_DEPRECATED_G726
	[2] = {1, AST_FORMAT_G726}, /* Technically this is G.721, but if Cisco can do it, so can we... */
	#endif
	[3] = {1, AST_FORMAT_GSM},
	[4] = {1, AST_FORMAT_G723_1},
	[5] = {1, AST_FORMAT_ADPCM}, /* 8 kHz */
	[6] = {1, AST_FORMAT_ADPCM}, /* 16 kHz */
	[7] = {1, AST_FORMAT_LPC10},
	[8] = {1, AST_FORMAT_ALAW},
	[9] = {1, AST_FORMAT_G722},
	[10] = {1, AST_FORMAT_SLINEAR}, /* 2 channels */
	[11] = {1, AST_FORMAT_SLINEAR}, /* 1 channel */
	[13] = {0, AST_RTP_CN},
	[16] = {1, AST_FORMAT_ADPCM}, /* 11.025 kHz */
	[17] = {1, AST_FORMAT_ADPCM}, /* 22.050 kHz */
	[18] = {1, AST_FORMAT_G729A},
	[19] = {0, AST_RTP_CN},         /* Also used for CN */
	[26] = {1, AST_FORMAT_JPEG},
	[31] = {1, AST_FORMAT_H261},
	[34] = {1, AST_FORMAT_H263},
	[97] = {1, AST_FORMAT_ILBC},
	[98] = {1, AST_FORMAT_H263_PLUS},
	[99] = {1, AST_FORMAT_H264},
	[101] = {0, AST_RTP_DTMF},
	[102] = {1, AST_FORMAT_SIREN7},
	[103] = {1, AST_FORMAT_H263_PLUS},
	[104] = {1, AST_FORMAT_MP4_VIDEO},
	[105] = {1, AST_FORMAT_T140RED},   /* Real time text chat (with redundancy encoding) */
	[106] = {1, AST_FORMAT_T140},      /* Real time text chat */
	[110] = {1, AST_FORMAT_SPEEX},
	[111] = {1, AST_FORMAT_G726},
	[112] = {1, AST_FORMAT_G726_AAL2},
	[115] = {1, AST_FORMAT_SIREN14},
	[116] = {1, AST_FORMAT_G719},
	[117] = {1, AST_FORMAT_SPEEX16},
	[118] = {1, AST_FORMAT_SLINEAR16}, /* 16 Khz signed linear */
	[121] = {0, AST_RTP_CISCO_DTMF},   /* Must be type 121 */
};

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
		struct sched_context *sched, const struct ast_sockaddr *sa,
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

int ast_rtp_instance_set_remote_address(struct ast_rtp_instance *instance,
		const struct ast_sockaddr *address)
{
	ast_sockaddr_copy(&instance->remote_address, address);

	/* moo */

	if (instance->engine->remote_address_set) {
		instance->engine->remote_address_set(instance, &instance->remote_address);
	}

	return 0;
}

int ast_rtp_instance_set_alt_remote_address(struct ast_rtp_instance *instance,
		const struct ast_sockaddr *address)
{
	ast_sockaddr_copy(&instance->alt_remote_address, address);

	/* oink */

	if (instance->engine->alt_remote_address_set) {
		instance->engine->alt_remote_address_set(instance, &instance->alt_remote_address);
	}

	return 0;
}

int ast_rtp_instance_get_local_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	if (ast_sockaddr_cmp(address, &instance->local_address) != 0) {
		ast_sockaddr_copy(address, &instance->local_address);
		return 1;
	}

	return 0;
}

int ast_rtp_instance_get_remote_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	if (ast_sockaddr_cmp(address, &instance->remote_address) != 0) {
		ast_sockaddr_copy(address, &instance->remote_address);
		return 1;
	}

	return 0;
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

void ast_rtp_codecs_payloads_clear(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance)
{
	int i;

	for (i = 0; i < AST_RTP_MAX_PT; i++) {
		codecs->payloads[i].asterisk_format = 0;
		codecs->payloads[i].code = 0;
		if (instance && instance->engine && instance->engine->payload_set) {
			instance->engine->payload_set(instance, i, 0, 0);
		}
	}
}

void ast_rtp_codecs_payloads_default(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance)
{
	int i;

	for (i = 0; i < AST_RTP_MAX_PT; i++) {
		if (static_RTP_PT[i].code) {
			codecs->payloads[i].asterisk_format = static_RTP_PT[i].asterisk_format;
			codecs->payloads[i].code = static_RTP_PT[i].code;
			if (instance && instance->engine && instance->engine->payload_set) {
				instance->engine->payload_set(instance, i, codecs->payloads[i].asterisk_format, codecs->payloads[i].code);
			}
		}
	}
}

void ast_rtp_codecs_payloads_copy(struct ast_rtp_codecs *src, struct ast_rtp_codecs *dest, struct ast_rtp_instance *instance)
{
	int i;

	for (i = 0; i < AST_RTP_MAX_PT; i++) {
		if (src->payloads[i].code) {
			ast_debug(2, "Copying payload %d from %p to %p\n", i, src, dest);
			dest->payloads[i].asterisk_format = src->payloads[i].asterisk_format;
			dest->payloads[i].code = src->payloads[i].code;
			if (instance && instance->engine && instance->engine->payload_set) {
				instance->engine->payload_set(instance, i, dest->payloads[i].asterisk_format, dest->payloads[i].code);
			}
		}
	}
}

void ast_rtp_codecs_payloads_set_m_type(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload)
{
	if (payload < 0 || payload >= AST_RTP_MAX_PT || !static_RTP_PT[payload].code) {
		return;
	}

	codecs->payloads[payload].asterisk_format = static_RTP_PT[payload].asterisk_format;
	codecs->payloads[payload].code = static_RTP_PT[payload].code;

	ast_debug(1, "Setting payload %d based on m type on %p\n", payload, codecs);

	if (instance && instance->engine && instance->engine->payload_set) {
		instance->engine->payload_set(instance, payload, codecs->payloads[payload].asterisk_format, codecs->payloads[payload].code);
	}
}

int ast_rtp_codecs_payloads_set_rtpmap_type_rate(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int pt,
				 char *mimetype, char *mimesubtype,
				 enum ast_rtp_options options,
				 unsigned int sample_rate)
{
	unsigned int i;
	int found = 0;

	if (pt < 0 || pt >= AST_RTP_MAX_PT)
		return -1; /* bogus payload type */

	for (i = 0; i < ARRAY_LEN(ast_rtp_mime_types); ++i) {
		const struct ast_rtp_mime_type *t = &ast_rtp_mime_types[i];

		if (strcasecmp(mimesubtype, t->subtype)) {
			continue;
		}

		if (strcasecmp(mimetype, t->type)) {
			continue;
		}

		/* if both sample rates have been supplied, and they don't match,
		                      then this not a match; if one has not been supplied, then the
				      rates are not compared */
		if (sample_rate && t->sample_rate &&
		    (sample_rate != t->sample_rate)) {
			continue;
		}

		found = 1;
		codecs->payloads[pt] = t->payload_type;

		if ((t->payload_type.code == AST_FORMAT_G726) &&
		                        t->payload_type.asterisk_format &&
		    (options & AST_RTP_OPT_G726_NONSTANDARD)) {
			codecs->payloads[pt].code = AST_FORMAT_G726_AAL2;
		}

		if (instance && instance->engine && instance->engine->payload_set) {
			instance->engine->payload_set(instance, pt, codecs->payloads[i].asterisk_format, codecs->payloads[i].code);
		}

		break;
	}

	return (found ? 0 : -2);
}

int ast_rtp_codecs_payloads_set_rtpmap_type(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload, char *mimetype, char *mimesubtype, enum ast_rtp_options options)
{
	return ast_rtp_codecs_payloads_set_rtpmap_type_rate(codecs, instance, payload, mimetype, mimesubtype, options, 0);
}

void ast_rtp_codecs_payloads_unset(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload)
{
	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		return;
	}

	ast_debug(2, "Unsetting payload %d on %p\n", payload, codecs);

	codecs->payloads[payload].asterisk_format = 0;
	codecs->payloads[payload].code = 0;

	if (instance && instance->engine && instance->engine->payload_set) {
		instance->engine->payload_set(instance, payload, 0, 0);
	}
}

struct ast_rtp_payload_type ast_rtp_codecs_payload_lookup(struct ast_rtp_codecs *codecs, int payload)
{
	struct ast_rtp_payload_type result = { .asterisk_format = 0, };

	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		return result;
	}

	result.asterisk_format = codecs->payloads[payload].asterisk_format;
	result.code = codecs->payloads[payload].code;

	if (!result.code) {
		result = static_RTP_PT[payload];
	}

	return result;
}

void ast_rtp_codecs_payload_formats(struct ast_rtp_codecs *codecs, format_t *astformats, int *nonastformats)
{
	int i;

	*astformats = *nonastformats = 0;

	for (i = 0; i < AST_RTP_MAX_PT; i++) {
		if (codecs->payloads[i].code) {
			ast_debug(1, "Incorporating payload %d on %p\n", i, codecs);
		}
		if (codecs->payloads[i].asterisk_format) {
			*astformats |= codecs->payloads[i].code;
		} else {
			*nonastformats |= codecs->payloads[i].code;
		}
	}
}

int ast_rtp_codecs_payload_code(struct ast_rtp_codecs *codecs, const int asterisk_format, const format_t code)
{
	int i;

	for (i = 0; i < AST_RTP_MAX_PT; i++) {
		if (codecs->payloads[i].asterisk_format == asterisk_format && codecs->payloads[i].code == code) {
			return i;
		}
	}

	for (i = 0; i < AST_RTP_MAX_PT; i++) {
		if (static_RTP_PT[i].asterisk_format == asterisk_format && static_RTP_PT[i].code == code) {
			return i;
		}
	}

	return -1;
}

const char *ast_rtp_lookup_mime_subtype2(const int asterisk_format, const format_t code, enum ast_rtp_options options)
{
	int i;

	for (i = 0; i < ARRAY_LEN(ast_rtp_mime_types); i++) {
		if (ast_rtp_mime_types[i].payload_type.code == code && ast_rtp_mime_types[i].payload_type.asterisk_format == asterisk_format) {
			if (asterisk_format && (code == AST_FORMAT_G726_AAL2) && (options & AST_RTP_OPT_G726_NONSTANDARD)) {
				return "G726-32";
			} else {
				return ast_rtp_mime_types[i].subtype;
			}
		}
	}

	return "";
}

unsigned int ast_rtp_lookup_sample_rate2(int asterisk_format, format_t code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LEN(ast_rtp_mime_types); ++i) {
		if ((ast_rtp_mime_types[i].payload_type.code == code) && (ast_rtp_mime_types[i].payload_type.asterisk_format == asterisk_format)) {
			return ast_rtp_mime_types[i].sample_rate;
		}
	}

	return 0;
}

char *ast_rtp_lookup_mime_multiple2(struct ast_str *buf, const format_t capability, const int asterisk_format, enum ast_rtp_options options)
{
	format_t format;
	int found = 0;

	if (!buf) {
		return NULL;
	}

	ast_str_append(&buf, 0, "0x%llx (", (unsigned long long) capability);

	for (format = 1; format < AST_RTP_MAX; format <<= 1) {
		if (capability & format) {
			const char *name = ast_rtp_lookup_mime_subtype2(asterisk_format, format, options);
			ast_str_append(&buf, 0, "%s|", name);
			found = 1;
		}
	}

	ast_str_append(&buf, 0, "%s", found ? ")" : "nothing)");

	return ast_str_buffer(buf);
}

void ast_rtp_codecs_packetization_set(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, struct ast_codec_pref *prefs)
{
	codecs->pref = *prefs;

	if (instance && instance->engine->packetization_set) {
		instance->engine->packetization_set(instance, &instance->codecs.pref);
	}
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
	if (!instance->engine->dtmf_mode_set || instance->engine->dtmf_mode_set(instance, dtmf_mode)) {
		return -1;
	}

	instance->dtmf_mode = dtmf_mode;

	return 0;
}

enum ast_rtp_dtmf_mode ast_rtp_instance_dtmf_mode_get(struct ast_rtp_instance *instance)
{
	return instance->dtmf_mode;
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

static enum ast_bridge_result local_bridge_loop(struct ast_channel *c0, struct ast_channel *c1, struct ast_rtp_instance *instance0, struct ast_rtp_instance *instance1, int timeoutms, int flags, struct ast_frame **fo, struct ast_channel **rc, void *pvt0, void *pvt1)
{
	enum ast_bridge_result res = AST_BRIDGE_FAILED;
	struct ast_channel *who = NULL, *other = NULL, *cs[3] = { NULL, };
	struct ast_frame *fr = NULL;

	/* Start locally bridging both instances */
	if (instance0->engine->local_bridge && instance0->engine->local_bridge(instance0, instance1)) {
		ast_debug(1, "Failed to locally bridge %s to %s, backing out.\n", c0->name, c1->name);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}
	if (instance1->engine->local_bridge && instance1->engine->local_bridge(instance1, instance0)) {
		ast_debug(1, "Failed to locally bridge %s to %s, backing out.\n", c1->name, c0->name);
		if (instance0->engine->local_bridge) {
			instance0->engine->local_bridge(instance0, NULL);
		}
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}

	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	instance0->bridged = instance1;
	instance1->bridged = instance0;

	ast_poll_channel_add(c0, c1);

	/* Hop into a loop waiting for a frame from either channel */
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;
	for (;;) {
		/* If the underlying formats have changed force this bridge to break */
		if ((c0->rawreadformat != c1->rawwriteformat) || (c1->rawreadformat != c0->rawwriteformat)) {
			ast_debug(1, "rtp-engine-local-bridge: Oooh, formats changed, backing out\n");
			res = AST_BRIDGE_FAILED_NOWARN;
			break;
		}
		/* Check if anything changed */
		if ((c0->tech_pvt != pvt0) ||
		    (c1->tech_pvt != pvt1) ||
		    (c0->masq || c0->masqr || c1->masq || c1->masqr) ||
		    (c0->monitor || c0->audiohooks || c1->monitor || c1->audiohooks)) {
			ast_debug(1, "rtp-engine-local-bridge: Oooh, something is weird, backing out\n");
			/* If a masquerade needs to happen we have to try to read in a frame so that it actually happens. Without this we risk being called again and going into a loop */
			if ((c0->masq || c0->masqr) && (fr = ast_read(c0))) {
				ast_frfree(fr);
			}
			if ((c1->masq || c1->masqr) && (fr = ast_read(c1))) {
				ast_frfree(fr);
			}
			res = AST_BRIDGE_RETRY;
			break;
		}
		/* Wait on a channel to feed us a frame */
		if (!(who = ast_waitfor_n(cs, 2, &timeoutms))) {
			if (!timeoutms) {
				res = AST_BRIDGE_RETRY;
				break;
			}
			ast_debug(2, "rtp-engine-local-bridge: Ooh, empty read...\n");
			if (ast_check_hangup(c0) || ast_check_hangup(c1)) {
				break;
			}
			continue;
		}
		/* Read in frame from channel */
		fr = ast_read(who);
		other = (who == c0) ? c1 : c0;
		/* Depending on the frame we may need to break out of our bridge */
		if (!fr || ((fr->frametype == AST_FRAME_DTMF_BEGIN || fr->frametype == AST_FRAME_DTMF_END) &&
			    ((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) |
			    ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1)))) {
			/* Record received frame and who */
			*fo = fr;
			*rc = who;
			ast_debug(1, "rtp-engine-local-bridge: Ooh, got a %s\n", fr ? "digit" : "hangup");
			res = AST_BRIDGE_COMPLETE;
			break;
		} else if ((fr->frametype == AST_FRAME_CONTROL) && !(flags & AST_BRIDGE_IGNORE_SIGS)) {
			if ((fr->subclass.integer == AST_CONTROL_HOLD) ||
			    (fr->subclass.integer == AST_CONTROL_UNHOLD) ||
			    (fr->subclass.integer == AST_CONTROL_VIDUPDATE) ||
			    (fr->subclass.integer == AST_CONTROL_SRCUPDATE) ||
			    (fr->subclass.integer == AST_CONTROL_T38_PARAMETERS)) {
				/* If we are going on hold, then break callback mode and P2P bridging */
				if (fr->subclass.integer == AST_CONTROL_HOLD) {
					if (instance0->engine->local_bridge) {
						instance0->engine->local_bridge(instance0, NULL);
					}
					if (instance1->engine->local_bridge) {
						instance1->engine->local_bridge(instance1, NULL);
					}
					instance0->bridged = NULL;
					instance1->bridged = NULL;
				} else if (fr->subclass.integer == AST_CONTROL_UNHOLD) {
					if (instance0->engine->local_bridge) {
						instance0->engine->local_bridge(instance0, instance1);
					}
					if (instance1->engine->local_bridge) {
						instance1->engine->local_bridge(instance1, instance0);
					}
					instance0->bridged = instance1;
					instance1->bridged = instance0;
				}
				ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_CONNECTED_LINE) {
				if (ast_channel_connected_line_macro(who, other, fr, other == c0, 1)) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_REDIRECTING) {
				if (ast_channel_redirecting_macro(who, other, fr, other == c0, 1)) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else {
				*fo = fr;
				*rc = who;
				ast_debug(1, "rtp-engine-local-bridge: Got a FRAME_CONTROL (%d) frame on channel %s\n", fr->subclass.integer, who->name);
				res = AST_BRIDGE_COMPLETE;
				break;
			}
		} else {
			if ((fr->frametype == AST_FRAME_DTMF_BEGIN) ||
			    (fr->frametype == AST_FRAME_DTMF_END) ||
			    (fr->frametype == AST_FRAME_VOICE) ||
			    (fr->frametype == AST_FRAME_VIDEO) ||
			    (fr->frametype == AST_FRAME_IMAGE) ||
			    (fr->frametype == AST_FRAME_HTML) ||
			    (fr->frametype == AST_FRAME_MODEM) ||
			    (fr->frametype == AST_FRAME_TEXT)) {
				ast_write(other, fr);
			}

			ast_frfree(fr);
		}
		/* Swap priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}

	/* Stop locally bridging both instances */
	if (instance0->engine->local_bridge) {
		instance0->engine->local_bridge(instance0, NULL);
	}
	if (instance1->engine->local_bridge) {
		instance1->engine->local_bridge(instance1, NULL);
	}

	instance0->bridged = NULL;
	instance1->bridged = NULL;

	ast_poll_channel_del(c0, c1);

	return res;
}

static enum ast_bridge_result remote_bridge_loop(struct ast_channel *c0, struct ast_channel *c1, struct ast_rtp_instance *instance0, struct ast_rtp_instance *instance1,
						 struct ast_rtp_instance *vinstance0, struct ast_rtp_instance *vinstance1, struct ast_rtp_instance *tinstance0,
						 struct ast_rtp_instance *tinstance1, struct ast_rtp_glue *glue0, struct ast_rtp_glue *glue1, format_t codec0, format_t codec1, int timeoutms,
						 int flags, struct ast_frame **fo, struct ast_channel **rc, void *pvt0, void *pvt1)
{
	enum ast_bridge_result res = AST_BRIDGE_FAILED;
	struct ast_channel *who = NULL, *other = NULL, *cs[3] = { NULL, };
	format_t oldcodec0 = codec0, oldcodec1 = codec1;
	struct ast_sockaddr ac1 = {{0,}}, vac1 = {{0,}}, tac1 = {{0,}}, ac0 = {{0,}}, vac0 = {{0,}}, tac0 = {{0,}};
	struct ast_sockaddr t1 = {{0,}}, vt1 = {{0,}}, tt1 = {{0,}}, t0 = {{0,}}, vt0 = {{0,}}, tt0 = {{0,}};
	struct ast_frame *fr = NULL;

	/* Test the first channel */
	if (!(glue0->update_peer(c0, instance1, vinstance1, tinstance1, codec1, 0))) {
		ast_rtp_instance_get_remote_address(instance1, &ac1);
		if (vinstance1) {
			ast_rtp_instance_get_remote_address(vinstance1, &vac1);
		}
		if (tinstance1) {
			ast_rtp_instance_get_remote_address(tinstance1, &tac1);
		}
	} else {
		ast_log(LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c0->name, c1->name);
	}

	/* Test the second channel */
	if (!(glue1->update_peer(c1, instance0, vinstance0, tinstance0, codec0, 0))) {
		ast_rtp_instance_get_remote_address(instance0, &ac0);
		if (vinstance0) {
			ast_rtp_instance_get_remote_address(instance0, &vac0);
		}
		if (tinstance0) {
			ast_rtp_instance_get_remote_address(instance0, &tac0);
		}
	} else {
		ast_log(LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c1->name, c0->name);
	}

	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	instance0->bridged = instance1;
	instance1->bridged = instance0;

	ast_poll_channel_add(c0, c1);

	/* Go into a loop handling any stray frames that may come in */
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;
	for (;;) {
		/* Check if anything changed */
		if ((c0->tech_pvt != pvt0) ||
		    (c1->tech_pvt != pvt1) ||
		    (c0->masq || c0->masqr || c1->masq || c1->masqr) ||
		    (c0->monitor || c0->audiohooks || c1->monitor || c1->audiohooks)) {
			ast_debug(1, "Oooh, something is weird, backing out\n");
			res = AST_BRIDGE_RETRY;
			break;
		}

		/* Check if they have changed their address */
		ast_rtp_instance_get_remote_address(instance1, &t1);
		if (vinstance1) {
			ast_rtp_instance_get_remote_address(vinstance1, &vt1);
		}
		if (tinstance1) {
			ast_rtp_instance_get_remote_address(tinstance1, &tt1);
		}
		if (glue1->get_codec) {
			codec1 = glue1->get_codec(c1);
		}

		ast_rtp_instance_get_remote_address(instance0, &t0);
		if (vinstance0) {
			ast_rtp_instance_get_remote_address(vinstance0, &vt0);
		}
		if (tinstance0) {
			ast_rtp_instance_get_remote_address(tinstance0, &tt0);
		}
		if (glue0->get_codec) {
			codec0 = glue0->get_codec(c0);
		}

		if ((ast_sockaddr_cmp(&t1, &ac1)) ||
		    (vinstance1 && ast_sockaddr_cmp(&vt1, &vac1)) ||
		    (tinstance1 && ast_sockaddr_cmp(&tt1, &tac1)) ||
		    (codec1 != oldcodec1)) {
			ast_debug(1, "Oooh, '%s' changed end address to %s (format %s)\n",
				  c1->name, ast_sockaddr_stringify(&t1),
				  ast_getformatname(codec1));
			ast_debug(1, "Oooh, '%s' changed end vaddress to %s (format %s)\n",
				  c1->name, ast_sockaddr_stringify(&vt1),
				  ast_getformatname(codec1));
			ast_debug(1, "Oooh, '%s' changed end taddress to %s (format %s)\n",
				  c1->name, ast_sockaddr_stringify(&tt1),
				  ast_getformatname(codec1));
			ast_debug(1, "Oooh, '%s' was %s/(format %s)\n",
				  c1->name, ast_sockaddr_stringify(&ac1),
				  ast_getformatname(oldcodec1));
			ast_debug(1, "Oooh, '%s' was %s/(format %s)\n",
				  c1->name, ast_sockaddr_stringify(&vac1),
				  ast_getformatname(oldcodec1));
			ast_debug(1, "Oooh, '%s' was %s/(format %s)\n",
				  c1->name, ast_sockaddr_stringify(&tac1),
				  ast_getformatname(oldcodec1));
			if (glue0->update_peer(c0,
					       ast_sockaddr_isnull(&t1)  ? NULL : instance1,
					       ast_sockaddr_isnull(&vt1) ? NULL : vinstance1,
					       ast_sockaddr_isnull(&tt1) ? NULL : tinstance1,
					       codec1, 0)) {
				ast_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c0->name, c1->name);
			}
			ast_sockaddr_copy(&ac1, &t1);
			ast_sockaddr_copy(&vac1, &vt1);
			ast_sockaddr_copy(&tac1, &tt1);
			oldcodec1 = codec1;
		}
		if ((ast_sockaddr_cmp(&t0, &ac0)) ||
		    (vinstance0 && ast_sockaddr_cmp(&vt0, &vac0)) ||
		    (tinstance0 && ast_sockaddr_cmp(&tt0, &tac0)) ||
		    (codec0 != oldcodec0)) {
			ast_debug(1, "Oooh, '%s' changed end address to %s (format %s)\n",
				  c0->name, ast_sockaddr_stringify(&t0),
				  ast_getformatname(codec0));
			ast_debug(1, "Oooh, '%s' was %s/(format %s)\n",
				  c0->name, ast_sockaddr_stringify(&ac0),
				  ast_getformatname(oldcodec0));
			if (glue1->update_peer(c1, t0.len ? instance0 : NULL,
						vt0.len ? vinstance0 : NULL,
						tt0.len ? tinstance0 : NULL,
						codec0, 0)) {
				ast_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", c1->name, c0->name);
			}
			ast_sockaddr_copy(&ac0, &t0);
			ast_sockaddr_copy(&vac0, &vt0);
			ast_sockaddr_copy(&tac0, &tt0);
			oldcodec0 = codec0;
		}

		/* Wait for frame to come in on the channels */
		if (!(who = ast_waitfor_n(cs, 2, &timeoutms))) {
			if (!timeoutms) {
				res = AST_BRIDGE_RETRY;
				break;
			}
			ast_debug(1, "Ooh, empty read...\n");
			if (ast_check_hangup(c0) || ast_check_hangup(c1)) {
				break;
			}
			continue;
		}
		fr = ast_read(who);
		other = (who == c0) ? c1 : c0;
		if (!fr || ((fr->frametype == AST_FRAME_DTMF_BEGIN || fr->frametype == AST_FRAME_DTMF_END) &&
			    (((who == c0) && (flags & AST_BRIDGE_DTMF_CHANNEL_0)) ||
			     ((who == c1) && (flags & AST_BRIDGE_DTMF_CHANNEL_1))))) {
			/* Break out of bridge */
			*fo = fr;
			*rc = who;
			ast_debug(1, "Oooh, got a %s\n", fr ? "digit" : "hangup");
			res = AST_BRIDGE_COMPLETE;
			break;
		} else if ((fr->frametype == AST_FRAME_CONTROL) && !(flags & AST_BRIDGE_IGNORE_SIGS)) {
			if ((fr->subclass.integer == AST_CONTROL_HOLD) ||
			    (fr->subclass.integer == AST_CONTROL_UNHOLD) ||
			    (fr->subclass.integer == AST_CONTROL_VIDUPDATE) ||
			    (fr->subclass.integer == AST_CONTROL_SRCUPDATE) ||
			    (fr->subclass.integer == AST_CONTROL_T38_PARAMETERS)) {
				if (fr->subclass.integer == AST_CONTROL_HOLD) {
					/* If we someone went on hold we want the other side to reinvite back to us */
					if (who == c0) {
						glue1->update_peer(c1, NULL, NULL, NULL, 0, 0);
					} else {
						glue0->update_peer(c0, NULL, NULL, NULL, 0, 0);
					}
				} else if (fr->subclass.integer == AST_CONTROL_UNHOLD) {
					/* If they went off hold they should go back to being direct */
					if (who == c0) {
						glue1->update_peer(c1, instance0, vinstance0, tinstance0, codec0, 0);
					} else {
						glue0->update_peer(c0, instance1, vinstance1, tinstance1, codec1, 0);
					}
				}
				/* Update local address information */
				ast_rtp_instance_get_remote_address(instance0, &t0);
				ast_sockaddr_copy(&ac0, &t0);
				ast_rtp_instance_get_remote_address(instance1, &t1);
				ast_sockaddr_copy(&ac1, &t1);
				/* Update codec information */
				if (glue0->get_codec && c0->tech_pvt) {
					oldcodec0 = codec0 = glue0->get_codec(c0);
				}
				if (glue1->get_codec && c1->tech_pvt) {
					oldcodec1 = codec1 = glue1->get_codec(c1);
				}
				ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_CONNECTED_LINE) {
				if (ast_channel_connected_line_macro(who, other, fr, other == c0, 1)) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_REDIRECTING) {
				if (ast_channel_redirecting_macro(who, other, fr, other == c0, 1)) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else {
				*fo = fr;
				*rc = who;
				ast_debug(1, "Got a FRAME_CONTROL (%d) frame on channel %s\n", fr->subclass.integer, who->name);
				return AST_BRIDGE_COMPLETE;
			}
		} else {
			if ((fr->frametype == AST_FRAME_DTMF_BEGIN) ||
			    (fr->frametype == AST_FRAME_DTMF_END) ||
			    (fr->frametype == AST_FRAME_VOICE) ||
			    (fr->frametype == AST_FRAME_VIDEO) ||
			    (fr->frametype == AST_FRAME_IMAGE) ||
			    (fr->frametype == AST_FRAME_HTML) ||
			    (fr->frametype == AST_FRAME_MODEM) ||
			    (fr->frametype == AST_FRAME_TEXT)) {
				ast_write(other, fr);
			}
			ast_frfree(fr);
		}
		/* Swap priority */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}

	if (glue0->update_peer(c0, NULL, NULL, NULL, 0, 0)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c0->name);
	}
	if (glue1->update_peer(c1, NULL, NULL, NULL, 0, 0)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", c1->name);
	}

	instance0->bridged = NULL;
	instance1->bridged = NULL;

	ast_poll_channel_del(c0, c1);

	return res;
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

enum ast_bridge_result ast_rtp_instance_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc, int timeoutms)
{
	struct ast_rtp_instance *instance0 = NULL, *instance1 = NULL,
			*vinstance0 = NULL, *vinstance1 = NULL,
			*tinstance0 = NULL, *tinstance1 = NULL;
	struct ast_rtp_glue *glue0, *glue1;
	struct ast_sockaddr addr1, addr2;
	enum ast_rtp_glue_result audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID, video_glue0_res = AST_RTP_GLUE_RESULT_FORBID, text_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_rtp_glue_result audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID, video_glue1_res = AST_RTP_GLUE_RESULT_FORBID, text_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_bridge_result res = AST_BRIDGE_FAILED;
	format_t codec0 = 0, codec1 = 0;
	int unlock_chans = 1;

	/* Lock both channels so we can look for the glue that binds them together */
	ast_channel_lock(c0);
	while (ast_channel_trylock(c1)) {
		ast_channel_unlock(c0);
		usleep(1);
		ast_channel_lock(c0);
	}

	/* Ensure neither channel got hungup during lock avoidance */
	if (ast_check_hangup(c0) || ast_check_hangup(c1)) {
		ast_log(LOG_WARNING, "Got hangup while attempting to bridge '%s' and '%s'\n", c0->name, c1->name);
		goto done;
	}

	/* Grab glue that binds each channel to something using the RTP engine */
	if (!(glue0 = ast_rtp_instance_get_glue(c0->tech->type)) || !(glue1 = ast_rtp_instance_get_glue(c1->tech->type))) {
		ast_debug(1, "Can't find native functions for channel '%s'\n", glue0 ? c1->name : c0->name);
		goto done;
	}

	audio_glue0_res = glue0->get_rtp_info(c0, &instance0);
	video_glue0_res = glue0->get_vrtp_info ? glue0->get_vrtp_info(c0, &vinstance0) : AST_RTP_GLUE_RESULT_FORBID;
	text_glue0_res = glue0->get_trtp_info ? glue0->get_trtp_info(c0, &tinstance0) : AST_RTP_GLUE_RESULT_FORBID;

	audio_glue1_res = glue1->get_rtp_info(c1, &instance1);
	video_glue1_res = glue1->get_vrtp_info ? glue1->get_vrtp_info(c1, &vinstance1) : AST_RTP_GLUE_RESULT_FORBID;
	text_glue1_res = glue1->get_trtp_info ? glue1->get_trtp_info(c1, &tinstance1) : AST_RTP_GLUE_RESULT_FORBID;

	/* If we are carrying video, and both sides are not going to remotely bridge... fail the native bridge */
	if (video_glue0_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue0_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue0_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (video_glue1_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue1_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue1_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	}

	/* If any sort of bridge is forbidden just completely bail out and go back to generic bridging */
	if (audio_glue0_res == AST_RTP_GLUE_RESULT_FORBID || audio_glue1_res == AST_RTP_GLUE_RESULT_FORBID) {
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}


	/* If address families differ, force a local bridge */
	ast_rtp_instance_get_remote_address(instance0, &addr1);
	ast_rtp_instance_get_remote_address(instance1, &addr2);

	if (addr1.ss.ss_family != addr2.ss.ss_family ||
	   (ast_sockaddr_is_ipv4_mapped(&addr1) != ast_sockaddr_is_ipv4_mapped(&addr2))) {
		audio_glue0_res = AST_RTP_GLUE_RESULT_LOCAL;
		audio_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
	}

	/* If we need to get DTMF see if we can do it outside of the RTP stream itself */
	if ((flags & AST_BRIDGE_DTMF_CHANNEL_0) && instance0->properties[AST_RTP_PROPERTY_DTMF]) {
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}
	if ((flags & AST_BRIDGE_DTMF_CHANNEL_1) && instance1->properties[AST_RTP_PROPERTY_DTMF]) {
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}

	/* If we have gotten to a local bridge make sure that both sides have the same local bridge callback and that they are DTMF compatible */
	if ((audio_glue0_res == AST_RTP_GLUE_RESULT_LOCAL || audio_glue1_res == AST_RTP_GLUE_RESULT_LOCAL) && ((instance0->engine->local_bridge != instance1->engine->local_bridge) || (instance0->engine->dtmf_compatible && !instance0->engine->dtmf_compatible(c0, instance0, c1, instance1)))) {
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}

	/* Make sure that codecs match */
	codec0 = glue0->get_codec ? glue0->get_codec(c0) : 0;
	codec1 = glue1->get_codec ? glue1->get_codec(c1) : 0;
	if (codec0 && codec1 && !(codec0 & codec1)) {
		ast_debug(1, "Channel codec0 = %s is not codec1 = %s, cannot native bridge in RTP.\n", ast_getformatname(codec0), ast_getformatname(codec1));
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}

	instance0->glue = glue0;
	instance1->glue = glue1;
	instance0->chan = c0;
	instance1->chan = c1;

	/* Depending on the end result for bridging either do a local bridge or remote bridge */
	if (audio_glue0_res == AST_RTP_GLUE_RESULT_LOCAL || audio_glue1_res == AST_RTP_GLUE_RESULT_LOCAL) {
		ast_verbose(VERBOSE_PREFIX_3 "Locally bridging %s and %s\n", c0->name, c1->name);
		res = local_bridge_loop(c0, c1, instance0, instance1, timeoutms, flags, fo, rc, c0->tech_pvt, c1->tech_pvt);
	} else {
		ast_verbose(VERBOSE_PREFIX_3 "Remotely bridging %s and %s\n", c0->name, c1->name);
		res = remote_bridge_loop(c0, c1, instance0, instance1, vinstance0, vinstance1,
				tinstance0, tinstance1, glue0, glue1, codec0, codec1, timeoutms, flags,
				fo, rc, c0->tech_pvt, c1->tech_pvt);
	}

	instance0->glue = NULL;
	instance1->glue = NULL;
	instance0->chan = NULL;
	instance1->chan = NULL;

	unlock_chans = 0;

done:
	if (unlock_chans) {
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
	}

	unref_instance_cond(&instance0);
	unref_instance_cond(&instance1);
	unref_instance_cond(&vinstance0);
	unref_instance_cond(&vinstance1);
	unref_instance_cond(&tinstance0);
	unref_instance_cond(&tinstance1);

	return res;
}

struct ast_rtp_instance *ast_rtp_instance_get_bridged(struct ast_rtp_instance *instance)
{
	return instance->bridged;
}

void ast_rtp_instance_early_bridge_make_compatible(struct ast_channel *c0, struct ast_channel *c1)
{
	struct ast_rtp_instance *instance0 = NULL, *instance1 = NULL,
		*vinstance0 = NULL, *vinstance1 = NULL,
		*tinstance0 = NULL, *tinstance1 = NULL;
	struct ast_rtp_glue *glue0, *glue1;
	enum ast_rtp_glue_result audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID, video_glue0_res = AST_RTP_GLUE_RESULT_FORBID, text_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_rtp_glue_result audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID, video_glue1_res = AST_RTP_GLUE_RESULT_FORBID, text_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	format_t codec0 = 0, codec1 = 0;
	int res = 0;

	/* Lock both channels so we can look for the glue that binds them together */
	ast_channel_lock(c0);
	while (ast_channel_trylock(c1)) {
		ast_channel_unlock(c0);
		usleep(1);
		ast_channel_lock(c0);
	}

	/* Grab glue that binds each channel to something using the RTP engine */
	if (!(glue0 = ast_rtp_instance_get_glue(c0->tech->type)) || !(glue1 = ast_rtp_instance_get_glue(c1->tech->type))) {
		ast_debug(1, "Can't find native functions for channel '%s'\n", glue0 ? c1->name : c0->name);
		goto done;
	}

	audio_glue0_res = glue0->get_rtp_info(c0, &instance0);
	video_glue0_res = glue0->get_vrtp_info ? glue0->get_vrtp_info(c0, &vinstance0) : AST_RTP_GLUE_RESULT_FORBID;
	text_glue0_res = glue0->get_trtp_info ? glue0->get_trtp_info(c0, &tinstance0) : AST_RTP_GLUE_RESULT_FORBID;

	audio_glue1_res = glue1->get_rtp_info(c1, &instance1);
	video_glue1_res = glue1->get_vrtp_info ? glue1->get_vrtp_info(c1, &vinstance1) : AST_RTP_GLUE_RESULT_FORBID;
	text_glue1_res = glue1->get_trtp_info ? glue1->get_trtp_info(c1, &tinstance1) : AST_RTP_GLUE_RESULT_FORBID;

	/* If we are carrying video, and both sides are not going to remotely bridge... fail the native bridge */
	if (video_glue0_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue0_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue0_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (video_glue1_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue1_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue1_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (audio_glue0_res == AST_RTP_GLUE_RESULT_REMOTE && (video_glue0_res == AST_RTP_GLUE_RESULT_FORBID || video_glue0_res == AST_RTP_GLUE_RESULT_REMOTE) && glue0->get_codec) {
		codec0 = glue0->get_codec(c0);
	}
	if (audio_glue1_res == AST_RTP_GLUE_RESULT_REMOTE && (video_glue1_res == AST_RTP_GLUE_RESULT_FORBID || video_glue1_res == AST_RTP_GLUE_RESULT_REMOTE) && glue1->get_codec) {
		codec1 = glue1->get_codec(c1);
	}

	/* If any sort of bridge is forbidden just completely bail out and go back to generic bridging */
	if (audio_glue0_res != AST_RTP_GLUE_RESULT_REMOTE || audio_glue1_res != AST_RTP_GLUE_RESULT_REMOTE) {
		goto done;
	}

	/* Make sure we have matching codecs */
	if (!(codec0 & codec1)) {
		goto done;
	}

	ast_rtp_codecs_payloads_copy(&instance0->codecs, &instance1->codecs, instance1);

	if (vinstance0 && vinstance1) {
		ast_rtp_codecs_payloads_copy(&vinstance0->codecs, &vinstance1->codecs, vinstance1);
	}
	if (tinstance0 && tinstance1) {
		ast_rtp_codecs_payloads_copy(&tinstance0->codecs, &tinstance1->codecs, tinstance1);
	}

	res = 0;

done:
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	unref_instance_cond(&instance0);
	unref_instance_cond(&instance1);
	unref_instance_cond(&vinstance0);
	unref_instance_cond(&vinstance1);
	unref_instance_cond(&tinstance0);
	unref_instance_cond(&tinstance1);

	if (!res) {
		ast_debug(1, "Seeded SDP of '%s' with that of '%s'\n", c0->name, c1 ? c1->name : "<unspecified>");
	}
}

int ast_rtp_instance_early_bridge(struct ast_channel *c0, struct ast_channel *c1)
{
	struct ast_rtp_instance *instance0 = NULL, *instance1 = NULL,
			*vinstance0 = NULL, *vinstance1 = NULL,
			*tinstance0 = NULL, *tinstance1 = NULL;
	struct ast_rtp_glue *glue0, *glue1;
	enum ast_rtp_glue_result audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID, video_glue0_res = AST_RTP_GLUE_RESULT_FORBID, text_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_rtp_glue_result audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID, video_glue1_res = AST_RTP_GLUE_RESULT_FORBID, text_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	format_t codec0 = 0, codec1 = 0;
	int res = 0;

	/* If there is no second channel just immediately bail out, we are of no use in that scenario */
	if (!c1) {
		return -1;
	}

	/* Lock both channels so we can look for the glue that binds them together */
	ast_channel_lock(c0);
	while (ast_channel_trylock(c1)) {
		ast_channel_unlock(c0);
		usleep(1);
		ast_channel_lock(c0);
	}

	/* Grab glue that binds each channel to something using the RTP engine */
	if (!(glue0 = ast_rtp_instance_get_glue(c0->tech->type)) || !(glue1 = ast_rtp_instance_get_glue(c1->tech->type))) {
		ast_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", glue0 ? c1->name : c0->name);
		goto done;
	}

	audio_glue0_res = glue0->get_rtp_info(c0, &instance0);
	video_glue0_res = glue0->get_vrtp_info ? glue0->get_vrtp_info(c0, &vinstance0) : AST_RTP_GLUE_RESULT_FORBID;
	text_glue0_res = glue0->get_trtp_info ? glue0->get_trtp_info(c0, &tinstance0) : AST_RTP_GLUE_RESULT_FORBID;

	audio_glue1_res = glue1->get_rtp_info(c1, &instance1);
	video_glue1_res = glue1->get_vrtp_info ? glue1->get_vrtp_info(c1, &vinstance1) : AST_RTP_GLUE_RESULT_FORBID;
	text_glue1_res = glue1->get_trtp_info ? glue1->get_trtp_info(c1, &tinstance1) : AST_RTP_GLUE_RESULT_FORBID;

	/* If we are carrying video, and both sides are not going to remotely bridge... fail the native bridge */
	if (video_glue0_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue0_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue0_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (video_glue1_res != AST_RTP_GLUE_RESULT_FORBID && (audio_glue1_res != AST_RTP_GLUE_RESULT_REMOTE || video_glue1_res != AST_RTP_GLUE_RESULT_REMOTE)) {
		audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	}
	if (audio_glue0_res == AST_RTP_GLUE_RESULT_REMOTE && (video_glue0_res == AST_RTP_GLUE_RESULT_FORBID || video_glue0_res == AST_RTP_GLUE_RESULT_REMOTE) && glue0->get_codec(c0)) {
		codec0 = glue0->get_codec(c0);
	}
	if (audio_glue1_res == AST_RTP_GLUE_RESULT_REMOTE && (video_glue1_res == AST_RTP_GLUE_RESULT_FORBID || video_glue1_res == AST_RTP_GLUE_RESULT_REMOTE) && glue1->get_codec(c1)) {
		codec1 = glue1->get_codec(c1);
	}

	/* If any sort of bridge is forbidden just completely bail out and go back to generic bridging */
	if (audio_glue0_res != AST_RTP_GLUE_RESULT_REMOTE || audio_glue1_res != AST_RTP_GLUE_RESULT_REMOTE) {
		goto done;
	}

	/* Make sure we have matching codecs */
	if (!(codec0 & codec1)) {
		goto done;
	}

	/* Bridge media early */
	if (glue0->update_peer(c0, instance1, vinstance1, tinstance1, codec1, 0)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to setup early bridge to '%s'\n", c0->name, c1 ? c1->name : "<unspecified>");
	}

	res = 0;

done:
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	unref_instance_cond(&instance0);
	unref_instance_cond(&instance1);
	unref_instance_cond(&vinstance0);
	unref_instance_cond(&vinstance1);
	unref_instance_cond(&tinstance0);
	unref_instance_cond(&tinstance1);

	if (!res) {
		ast_debug(1, "Setting early bridge SDP of '%s' with that of '%s'\n", c0->name, c1 ? c1->name : "<unspecified>");
	}

	return res;
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
		snprintf(buf, size, "ssrc=%i;themssrc=%u;lp=%u;rxjitter=%f;rxcount=%u;txjitter=%f;txcount=%u;rlp=%u;rtt=%f",
			 stats.local_ssrc, stats.remote_ssrc, stats.rxploss, stats.txjitter, stats.rxcount, stats.rxjitter, stats.txcount, stats.txploss, stats.rtt);
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
	char quality_buf[AST_MAX_USER_FIELD], *quality;
	struct ast_channel *bridge = ast_bridged_channel(chan);

	if ((quality = ast_rtp_instance_get_quality(instance, AST_RTP_INSTANCE_STAT_FIELD_QUALITY, quality_buf, sizeof(quality_buf)))) {
		pbx_builtin_setvar_helper(chan, "RTPAUDIOQOS", quality);
		if (bridge) {
			pbx_builtin_setvar_helper(bridge, "RTPAUDIOQOSBRIDGED", quality);
		}
	}

	if ((quality = ast_rtp_instance_get_quality(instance, AST_RTP_INSTANCE_STAT_FIELD_QUALITY_JITTER, quality_buf, sizeof(quality_buf)))) {
		pbx_builtin_setvar_helper(chan, "RTPAUDIOQOSJITTER", quality);
		if (bridge) {
			pbx_builtin_setvar_helper(bridge, "RTPAUDIOQOSJITTERBRIDGED", quality);
		}
	}

	if ((quality = ast_rtp_instance_get_quality(instance, AST_RTP_INSTANCE_STAT_FIELD_QUALITY_LOSS, quality_buf, sizeof(quality_buf)))) {
		pbx_builtin_setvar_helper(chan, "RTPAUDIOQOSLOSS", quality);
		if (bridge) {
			pbx_builtin_setvar_helper(bridge, "RTPAUDIOQOSLOSSBRIDGED", quality);
		}
	}

	if ((quality = ast_rtp_instance_get_quality(instance, AST_RTP_INSTANCE_STAT_FIELD_QUALITY_RTT, quality_buf, sizeof(quality_buf)))) {
		pbx_builtin_setvar_helper(chan, "RTPAUDIOQOSRTT", quality);
		if (bridge) {
			pbx_builtin_setvar_helper(bridge, "RTPAUDIOQOSRTTBRIDGED", quality);
		}
	}
}

int ast_rtp_instance_set_read_format(struct ast_rtp_instance *instance, format_t format)
{
	return instance->engine->set_read_format ? instance->engine->set_read_format(instance, format) : -1;
}

int ast_rtp_instance_set_write_format(struct ast_rtp_instance *instance, format_t format)
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

	if (!(glue = ast_rtp_instance_get_glue(peer->tech->type))) {
		ast_channel_unlock(peer);
		return -1;
	}

	glue->get_rtp_info(peer, &peer_instance);

	if (!peer_instance || peer_instance->engine != instance->engine) {
		ast_channel_unlock(peer);
		ao2_ref(peer_instance, -1);
		peer_instance = NULL;
		return -1;
	}

	res = instance->engine->make_compatible(chan, instance, peer, peer_instance);

	ast_channel_unlock(peer);

	ao2_ref(peer_instance, -1);
	peer_instance = NULL;

	return res;
}

format_t ast_rtp_instance_available_formats(struct ast_rtp_instance *instance, format_t to_endpoint, format_t to_asterisk)
{
	format_t formats;

	if (instance->engine->available_formats && (formats = instance->engine->available_formats(instance, to_endpoint, to_asterisk))) {
		return formats;
	}

	return ast_translate_available_formats(to_endpoint, to_asterisk);
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

int ast_rtp_instance_get_timeout(struct ast_rtp_instance *instance)
{
	return instance->timeout;
}

int ast_rtp_instance_get_hold_timeout(struct ast_rtp_instance *instance)
{
	return instance->holdtimeout;
}

struct ast_rtp_engine *ast_rtp_instance_get_engine(struct ast_rtp_instance *instance)
{
	return instance->engine;
}

struct ast_rtp_glue *ast_rtp_instance_get_active_glue(struct ast_rtp_instance *instance)
{
	return instance->glue;
}

struct ast_channel *ast_rtp_instance_get_chan(struct ast_rtp_instance *instance)
{
	return instance->chan;
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

int ast_rtp_instance_add_srtp_policy(struct ast_rtp_instance *instance, struct ast_srtp_policy *policy)
{
	if (!res_srtp) {
		return -1;
	}

	if (!instance->srtp) {
		return res_srtp->create(&instance->srtp, instance, policy);
	} else {
		return res_srtp->add_stream(instance->srtp, policy);
	}
}

struct ast_srtp *ast_rtp_instance_get_srtp(struct ast_rtp_instance *instance)
{
	return instance->srtp;
}
