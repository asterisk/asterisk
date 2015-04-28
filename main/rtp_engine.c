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
	/*! RTP keepalive interval */
	int keepalive;
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
static struct ast_rtp_mime_type {
	struct ast_rtp_payload_type payload_type;
	char *type;
	char *subtype;
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

int ast_rtp_instance_get_and_cmp_remote_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	if (ast_sockaddr_cmp(address, &instance->remote_address) != 0) {
		ast_sockaddr_copy(address, &instance->remote_address);
		return 1;
	}

	return 0;
}

void ast_rtp_instance_get_remote_address(struct ast_rtp_instance *instance,
		struct ast_sockaddr *address)
{
	ast_sockaddr_copy(address, &instance->remote_address);
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

static int rtp_payload_type_hash(const void *obj, const int flags)
{
	const struct ast_rtp_payload_type *type = obj;
	const int *payload = obj;

	return (flags & OBJ_KEY) ? *payload : type->payload;
}

static int rtp_payload_type_cmp(void *obj, void *arg, int flags)
{
	struct ast_rtp_payload_type *type1 = obj, *type2 = arg;
	const int *payload = arg;

	return (type1->payload == (OBJ_KEY ? *payload : type2->payload)) ? CMP_MATCH | CMP_STOP : 0;
}

int ast_rtp_codecs_payloads_initialize(struct ast_rtp_codecs *codecs)
{
	if (!(codecs->payloads = ao2_container_alloc(AST_RTP_MAX_PT, rtp_payload_type_hash, rtp_payload_type_cmp))) {
		return -1;
	}

	return 0;
}

void ast_rtp_codecs_payloads_destroy(struct ast_rtp_codecs *codecs)
{
	ao2_cleanup(codecs->payloads);
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

void ast_rtp_codecs_payloads_default(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance)
{
	int i;

	ast_rwlock_rdlock(&static_RTP_PT_lock);
	for (i = 0; i < AST_RTP_MAX_PT; i++) {
		if (static_RTP_PT[i].rtp_code || static_RTP_PT[i].asterisk_format) {
			struct ast_rtp_payload_type *type;

			if (!(type = ao2_alloc(sizeof(*type), NULL))) {
				/* Unfortunately if this occurs the payloads container will not contain all possible default payloads
				 * but we err on the side of doing what we can in the hopes that the extreme memory conditions which
				 * caused this to occur will go away.
				 */
				continue;
			}

			type->payload = i;
			type->asterisk_format = static_RTP_PT[i].asterisk_format;
			type->rtp_code = static_RTP_PT[i].rtp_code;
			ast_format_copy(&type->format, &static_RTP_PT[i].format);

			ao2_link_flags(codecs->payloads, type, OBJ_NOLOCK);

			if (instance && instance->engine && instance->engine->payload_set) {
				instance->engine->payload_set(instance, i, type->asterisk_format, &type->format, type->rtp_code);
			}

			ao2_ref(type, -1);
		}
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);
}

void ast_rtp_codecs_payloads_copy(struct ast_rtp_codecs *src, struct ast_rtp_codecs *dest, struct ast_rtp_instance *instance)
{
	int i;
	struct ast_rtp_payload_type *type;

	for (i = 0; i < AST_RTP_MAX_PT; i++) {
		struct ast_rtp_payload_type *new_type;

		if (!(type = ao2_find(src->payloads, &i, OBJ_KEY | OBJ_NOLOCK))) {
			continue;
		}

		if (!(new_type = ao2_alloc(sizeof(*new_type), NULL))) {
			continue;
		}

		ast_debug(2, "Copying payload %d from %p to %p\n", i, src, dest);

		new_type->payload = i;
		*new_type = *type;

		ao2_link_flags(dest->payloads, new_type, OBJ_NOLOCK);

		ao2_ref(new_type, -1);

		if (instance && instance->engine && instance->engine->payload_set) {
			instance->engine->payload_set(instance, i, type->asterisk_format, &type->format, type->rtp_code);
		}

		ao2_ref(type, -1);
	}
}

void ast_rtp_codecs_payloads_set_m_type(struct ast_rtp_codecs *codecs, struct ast_rtp_instance *instance, int payload)
{
	struct ast_rtp_payload_type *type;

	ast_rwlock_rdlock(&static_RTP_PT_lock);

	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		ast_rwlock_unlock(&static_RTP_PT_lock);
		return;
	}

	if (!(type = ao2_find(codecs->payloads, &payload, OBJ_KEY | OBJ_NOLOCK))) {
		if (!(type = ao2_alloc(sizeof(*type), NULL))) {
			ast_rwlock_unlock(&static_RTP_PT_lock);
			return;
		}
		type->payload = payload;
		ao2_link_flags(codecs->payloads, type, OBJ_NOLOCK);
	}

	type->asterisk_format = static_RTP_PT[payload].asterisk_format;
	type->rtp_code = static_RTP_PT[payload].rtp_code;
	type->payload = payload;
	ast_format_copy(&type->format, &static_RTP_PT[payload].format);

	ast_debug(1, "Setting payload %d based on m type on %p\n", payload, codecs);

	if (instance && instance->engine && instance->engine->payload_set) {
		instance->engine->payload_set(instance, payload, type->asterisk_format, &type->format, type->rtp_code);
	}

	ao2_ref(type, -1);

	ast_rwlock_unlock(&static_RTP_PT_lock);
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

	ast_rwlock_rdlock(&mime_types_lock);
	for (i = 0; i < mime_types_len; ++i) {
		const struct ast_rtp_mime_type *t = &ast_rtp_mime_types[i];
		struct ast_rtp_payload_type *type;

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

		if (!(type = ao2_find(codecs->payloads, &pt, OBJ_KEY | OBJ_NOLOCK))) {
			if (!(type = ao2_alloc(sizeof(*type), NULL))) {
				continue;
			}
			type->payload = pt;
			ao2_link_flags(codecs->payloads, type, OBJ_NOLOCK);
		}

		*type = t->payload_type;
		type->payload = pt;

		if ((t->payload_type.format.id == AST_FORMAT_G726) && t->payload_type.asterisk_format && (options & AST_RTP_OPT_G726_NONSTANDARD)) {
			ast_format_set(&type->format, AST_FORMAT_G726_AAL2, 0);
		}

		if (instance && instance->engine && instance->engine->payload_set) {
			instance->engine->payload_set(instance, pt, type->asterisk_format, &type->format, type->rtp_code);
		}

		ao2_ref(type, -1);

		break;
	}
	ast_rwlock_unlock(&mime_types_lock);

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

	ao2_find(codecs->payloads, &payload, OBJ_KEY | OBJ_NOLOCK | OBJ_NODATA | OBJ_UNLINK);

	if (instance && instance->engine && instance->engine->payload_set) {
		instance->engine->payload_set(instance, payload, 0, NULL, 0);
	}
}

struct ast_rtp_payload_type ast_rtp_codecs_payload_lookup(struct ast_rtp_codecs *codecs, int payload)
{
	struct ast_rtp_payload_type result = { .asterisk_format = 0, }, *type;

	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		return result;
	}

	if ((type = ao2_find(codecs->payloads, &payload, OBJ_KEY | OBJ_NOLOCK))) {
		result = *type;
		ao2_ref(type, -1);
	}

	if (!result.rtp_code && !result.asterisk_format) {
		ast_rwlock_rdlock(&static_RTP_PT_lock);
		result = static_RTP_PT[payload];
		ast_rwlock_unlock(&static_RTP_PT_lock);
	}

	return result;
}


struct ast_format *ast_rtp_codecs_get_payload_format(struct ast_rtp_codecs *codecs, int payload)
{
	struct ast_rtp_payload_type *type;
	struct ast_format *format;

	if (payload < 0 || payload >= AST_RTP_MAX_PT) {
		return NULL;
	}

	if (!(type = ao2_find(codecs->payloads, &payload, OBJ_KEY | OBJ_NOLOCK))) {
		return NULL;
	}

	format = type->asterisk_format ? &type->format : NULL;

	ao2_ref(type, -1);

	return format;
}

static int rtp_payload_type_add_ast(void *obj, void *arg, int flags)
{
	struct ast_rtp_payload_type *type = obj;
	struct ast_format_cap *astformats = arg;

	if (type->asterisk_format) {
		ast_format_cap_add(astformats, &type->format);
	}

	return 0;
}

static int rtp_payload_type_add_nonast(void *obj, void *arg, int flags)
{
	struct ast_rtp_payload_type *type = obj;
	int *nonastformats = arg;

	if (!type->asterisk_format) {
		*nonastformats |= type->rtp_code;
	}

	return 0;
}

void ast_rtp_codecs_payload_formats(struct ast_rtp_codecs *codecs, struct ast_format_cap *astformats, int *nonastformats)
{
	ast_format_cap_remove_all(astformats);
	*nonastformats = 0;

	ao2_callback(codecs->payloads, OBJ_NODATA | OBJ_MULTIPLE | OBJ_NOLOCK, rtp_payload_type_add_ast, astformats);
	ao2_callback(codecs->payloads, OBJ_NODATA | OBJ_MULTIPLE | OBJ_NOLOCK, rtp_payload_type_add_nonast, nonastformats);
}

static int rtp_payload_type_find_format(void *obj, void *arg, int flags)
{
	struct ast_rtp_payload_type *type = obj;
	struct ast_format *format = arg;

	return (type->asterisk_format && (ast_format_cmp(&type->format, format) != AST_FORMAT_CMP_NOT_EQUAL)) ? CMP_MATCH | CMP_STOP : 0;
}

static int rtp_payload_type_find_nonast_format(void *obj, void *arg, int flags)
{
	struct ast_rtp_payload_type *type = obj;
	int *rtp_code = arg;

	return ((!type->asterisk_format && (type->rtp_code == *rtp_code)) ? CMP_MATCH | CMP_STOP : 0);
}

int ast_rtp_codecs_payload_code(struct ast_rtp_codecs *codecs, int asterisk_format, const struct ast_format *format, int code)
{
	struct ast_rtp_payload_type *type;
	int i, res = -1;

	if (asterisk_format && format && (type = ao2_callback(codecs->payloads, OBJ_NOLOCK, rtp_payload_type_find_format, (void*)format))) {
		res = type->payload;
		ao2_ref(type, -1);
		return res;
	} else if (!asterisk_format && (type = ao2_callback(codecs->payloads, OBJ_NOLOCK, rtp_payload_type_find_nonast_format, (void*)&code))) {
		res = type->payload;
		ao2_ref(type, -1);
		return res;
	}

	ast_rwlock_rdlock(&static_RTP_PT_lock);
	for (i = 0; i < AST_RTP_MAX_PT; i++) {
		if (static_RTP_PT[i].asterisk_format && asterisk_format && format &&
			(ast_format_cmp(format, &static_RTP_PT[i].format) != AST_FORMAT_CMP_NOT_EQUAL)) {
			res = i;
			break;
		} else if (!static_RTP_PT[i].asterisk_format && !asterisk_format &&
			(static_RTP_PT[i].rtp_code == code)) {
			res = i;
			break;
		}
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);

	return res;
}
int ast_rtp_codecs_find_payload_code(struct ast_rtp_codecs *codecs, int code)
{
	struct ast_rtp_payload_type *type;
	int res = -1;

	/* Search the payload type in the codecs passed */
	if ((type = ao2_find(codecs->payloads, &code, OBJ_NOLOCK | OBJ_KEY)))
	{
		res = type->payload;
		ao2_ref(type, -1);
		return res;
	}

	return res;
}
const char *ast_rtp_lookup_mime_subtype2(const int asterisk_format, struct ast_format *format, int code, enum ast_rtp_options options)
{
	int i;
	const char *res = "";

	ast_rwlock_rdlock(&mime_types_lock);
	for (i = 0; i < mime_types_len; i++) {
		if (ast_rtp_mime_types[i].payload_type.asterisk_format && asterisk_format && format &&
			(ast_format_cmp(format, &ast_rtp_mime_types[i].payload_type.format) != AST_FORMAT_CMP_NOT_EQUAL)) {
			if ((format->id == AST_FORMAT_G726_AAL2) && (options & AST_RTP_OPT_G726_NONSTANDARD)) {
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
			(ast_format_cmp(format, &ast_rtp_mime_types[i].payload_type.format) != AST_FORMAT_CMP_NOT_EQUAL)) {
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
		struct ast_format tmp_fmt;
		ast_format_cap_iter_start(ast_format_capability);
		while (!ast_format_cap_iter_next(ast_format_capability, &tmp_fmt)) {
			name = ast_rtp_lookup_mime_subtype2(asterisk_format, &tmp_fmt, 0, options);
			ast_str_append(&buf, 0, "%s|", name);
			found = 1;
		}
		ast_format_cap_iter_end(ast_format_capability);

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

static enum ast_bridge_result local_bridge_loop(struct ast_channel *c0, struct ast_channel *c1, struct ast_rtp_instance *instance0, struct ast_rtp_instance *instance1, int timeoutms, int flags, struct ast_frame **fo, struct ast_channel **rc, void *pvt0, void *pvt1)
{
	enum ast_bridge_result res = AST_BRIDGE_FAILED;
	struct ast_channel *who = NULL, *other = NULL, *cs[3] = { NULL, };
	struct ast_frame *fr = NULL;
	struct timeval start;

	/* Start locally bridging both instances */
	if (instance0->engine->local_bridge && instance0->engine->local_bridge(instance0, instance1)) {
		ast_debug(1, "Failed to locally bridge %s to %s, backing out.\n", ast_channel_name(c0), ast_channel_name(c1));
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return AST_BRIDGE_FAILED_NOWARN;
	}
	if (instance1->engine->local_bridge && instance1->engine->local_bridge(instance1, instance0)) {
		ast_debug(1, "Failed to locally bridge %s to %s, backing out.\n", ast_channel_name(c1), ast_channel_name(c0));
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
	start = ast_tvnow();
	for (;;) {
		int ms;
		/* If the underlying formats have changed force this bridge to break */
		if ((ast_format_cmp(ast_channel_rawreadformat(c0), ast_channel_rawwriteformat(c1)) == AST_FORMAT_CMP_NOT_EQUAL) ||
			(ast_format_cmp(ast_channel_rawreadformat(c1), ast_channel_rawwriteformat(c0)) == AST_FORMAT_CMP_NOT_EQUAL)) {
			ast_debug(1, "rtp-engine-local-bridge: Oooh, formats changed, backing out\n");
			res = AST_BRIDGE_FAILED_NOWARN;
			break;
		}
		/* Check if anything changed */
		if ((ast_channel_tech_pvt(c0) != pvt0) ||
		    (ast_channel_tech_pvt(c1) != pvt1) ||
		    (ast_channel_masq(c0) || ast_channel_masqr(c0) || ast_channel_masq(c1) || ast_channel_masqr(c1)) ||
		    (ast_channel_monitor(c0) || ast_channel_audiohooks(c0) || ast_channel_monitor(c1) || ast_channel_audiohooks(c1)) ||
		    (!ast_framehook_list_is_empty(ast_channel_framehooks(c0)) || !ast_framehook_list_is_empty(ast_channel_framehooks(c1)))) {
			ast_debug(1, "rtp-engine-local-bridge: Oooh, something is weird, backing out\n");
			/* If a masquerade needs to happen we have to try to read in a frame so that it actually happens. Without this we risk being called again and going into a loop */
			if ((ast_channel_masq(c0) || ast_channel_masqr(c0)) && (fr = ast_read(c0))) {
				ast_frfree(fr);
			}
			if ((ast_channel_masq(c1) || ast_channel_masqr(c1)) && (fr = ast_read(c1))) {
				ast_frfree(fr);
			}
			res = AST_BRIDGE_RETRY;
			break;
		}
		/* Wait on a channel to feed us a frame */
		ms = ast_remaining_ms(start, timeoutms);
		if (!(who = ast_waitfor_n(cs, 2, &ms))) {
			if (!ms) {
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
			    (fr->subclass.integer == AST_CONTROL_T38_PARAMETERS) ||
			    (fr->subclass.integer == AST_CONTROL_UPDATE_RTP_PEER)) {
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
				/* Since UPDATE_BRIDGE_PEER is only used by the bridging code, don't forward it */
				if (fr->subclass.integer != AST_CONTROL_UPDATE_RTP_PEER) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_CONNECTED_LINE) {
				if (ast_channel_connected_line_sub(who, other, fr, 1) &&
					ast_channel_connected_line_macro(who, other, fr, other == c0, 1)) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_REDIRECTING) {
				if (ast_channel_redirecting_sub(who, other, fr, 1) &&
					ast_channel_redirecting_macro(who, other, fr, other == c0, 1)) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_PVT_CAUSE_CODE) {
				ast_channel_hangupcause_hash_set(other, fr->data.ptr, fr->datalen);
				ast_frfree(fr);
			} else {
				*fo = fr;
				*rc = who;
				ast_debug(1, "rtp-engine-local-bridge: Got a FRAME_CONTROL (%d) frame on channel %s\n", fr->subclass.integer, ast_channel_name(who));
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

static enum ast_bridge_result remote_bridge_loop(struct ast_channel *c0,
	struct ast_channel *c1,
	struct ast_rtp_instance *instance0,
	struct ast_rtp_instance *instance1,
	struct ast_rtp_instance *vinstance0,
	struct ast_rtp_instance *vinstance1,
	struct ast_rtp_instance *tinstance0,
	struct ast_rtp_instance *tinstance1,
	struct ast_rtp_glue *glue0,
	struct ast_rtp_glue *glue1,
	struct ast_format_cap *cap0,
	struct ast_format_cap *cap1,
	int timeoutms,
	int flags,
	struct ast_frame **fo,
	struct ast_channel **rc,
	void *pvt0,
	void *pvt1)
{
	enum ast_bridge_result res = AST_BRIDGE_FAILED;
	struct ast_channel *who = NULL, *other = NULL, *cs[3] = { NULL, };
	struct ast_format_cap *oldcap0 = ast_format_cap_dup(cap0);
	struct ast_format_cap *oldcap1 = ast_format_cap_dup(cap1);
	struct ast_sockaddr ac1 = {{0,}}, vac1 = {{0,}}, tac1 = {{0,}}, ac0 = {{0,}}, vac0 = {{0,}}, tac0 = {{0,}};
	struct ast_sockaddr t1 = {{0,}}, vt1 = {{0,}}, tt1 = {{0,}}, t0 = {{0,}}, vt0 = {{0,}}, tt0 = {{0,}};
	struct ast_frame *fr = NULL;
	struct timeval start;

	if (!oldcap0 || !oldcap1) {
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		goto remote_bridge_cleanup;
	}
	/* Test the first channel */
	if (!(glue0->update_peer(c0, instance1, vinstance1, tinstance1, cap1, 0))) {
		ast_rtp_instance_get_remote_address(instance1, &ac1);
		if (vinstance1) {
			ast_rtp_instance_get_remote_address(vinstance1, &vac1);
		}
		if (tinstance1) {
			ast_rtp_instance_get_remote_address(tinstance1, &tac1);
		}
	} else {
		ast_log(LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", ast_channel_name(c0), ast_channel_name(c1));
	}

	/* Test the second channel */
	if (!(glue1->update_peer(c1, instance0, vinstance0, tinstance0, cap0, 0))) {
		ast_rtp_instance_get_remote_address(instance0, &ac0);
		if (vinstance0) {
			ast_rtp_instance_get_remote_address(instance0, &vac0);
		}
		if (tinstance0) {
			ast_rtp_instance_get_remote_address(instance0, &tac0);
		}
	} else {
		ast_log(LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", ast_channel_name(c1), ast_channel_name(c0));
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
	start = ast_tvnow();
	for (;;) {
		int ms;
		/* Check if anything changed */
		if ((ast_channel_tech_pvt(c0) != pvt0) ||
		    (ast_channel_tech_pvt(c1) != pvt1) ||
		    (ast_channel_masq(c0) || ast_channel_masqr(c0) || ast_channel_masq(c1) || ast_channel_masqr(c1)) ||
		    (ast_channel_monitor(c0) || ast_channel_audiohooks(c0) || ast_channel_monitor(c1) || ast_channel_audiohooks(c1)) ||
		    (!ast_framehook_list_is_empty(ast_channel_framehooks(c0)) || !ast_framehook_list_is_empty(ast_channel_framehooks(c1)))) {
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
		ast_channel_lock(c1);
		if (glue1->get_codec && ast_channel_tech_pvt(c1)) {
			ast_format_cap_remove_all(cap1);
			glue1->get_codec(c1, cap1);
		}
		ast_channel_unlock(c1);

		ast_rtp_instance_get_remote_address(instance0, &t0);
		if (vinstance0) {
			ast_rtp_instance_get_remote_address(vinstance0, &vt0);
		}
		if (tinstance0) {
			ast_rtp_instance_get_remote_address(tinstance0, &tt0);
		}
		ast_channel_lock(c0);
		if (glue0->get_codec && ast_channel_tech_pvt(c0)) {
			ast_format_cap_remove_all(cap0);
			glue0->get_codec(c0, cap0);
		}
		ast_channel_unlock(c0);

		if ((ast_sockaddr_cmp(&t1, &ac1)) ||
		    (vinstance1 && ast_sockaddr_cmp(&vt1, &vac1)) ||
		    (tinstance1 && ast_sockaddr_cmp(&tt1, &tac1)) ||
		    (!ast_format_cap_identical(cap1, oldcap1))) {
			char tmp_buf[512] = { 0, };
			ast_debug(1, "Oooh, '%s' changed end address to %s (format %s)\n",
				  ast_channel_name(c1), ast_sockaddr_stringify(&t1),
				  ast_getformatname_multiple(tmp_buf, sizeof(tmp_buf), cap1));
			ast_debug(1, "Oooh, '%s' changed end vaddress to %s (format %s)\n",
				  ast_channel_name(c1), ast_sockaddr_stringify(&vt1),
				  ast_getformatname_multiple(tmp_buf, sizeof(tmp_buf), cap1));
			ast_debug(1, "Oooh, '%s' changed end taddress to %s (format %s)\n",
				  ast_channel_name(c1), ast_sockaddr_stringify(&tt1),
				  ast_getformatname_multiple(tmp_buf, sizeof(tmp_buf), cap1));
			ast_debug(1, "Oooh, '%s' was %s/(format %s)\n",
				  ast_channel_name(c1), ast_sockaddr_stringify(&ac1),
				  ast_getformatname_multiple(tmp_buf, sizeof(tmp_buf), oldcap1));
			ast_debug(1, "Oooh, '%s' was %s/(format %s)\n",
				  ast_channel_name(c1), ast_sockaddr_stringify(&vac1),
				  ast_getformatname_multiple(tmp_buf, sizeof(tmp_buf), oldcap1));
			ast_debug(1, "Oooh, '%s' was %s/(format %s)\n",
				  ast_channel_name(c1), ast_sockaddr_stringify(&tac1),
				  ast_getformatname_multiple(tmp_buf, sizeof(tmp_buf), oldcap1));
			if (glue0->update_peer(c0,
					       ast_sockaddr_isnull(&t1)  ? NULL : instance1,
					       ast_sockaddr_isnull(&vt1) ? NULL : vinstance1,
					       ast_sockaddr_isnull(&tt1) ? NULL : tinstance1,
					       cap1, 0)) {
				ast_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", ast_channel_name(c0), ast_channel_name(c1));
			}
			ast_sockaddr_copy(&ac1, &t1);
			ast_sockaddr_copy(&vac1, &vt1);
			ast_sockaddr_copy(&tac1, &tt1);
			ast_format_cap_copy(oldcap1, cap1);
		}
		if ((ast_sockaddr_cmp(&t0, &ac0)) ||
		    (vinstance0 && ast_sockaddr_cmp(&vt0, &vac0)) ||
		    (tinstance0 && ast_sockaddr_cmp(&tt0, &tac0)) ||
		    (!ast_format_cap_identical(cap0, oldcap0))) {
			char tmp_buf[512] = { 0, };
			ast_debug(1, "Oooh, '%s' changed end address to %s (format %s)\n",
				  ast_channel_name(c0), ast_sockaddr_stringify(&t0),
				  ast_getformatname_multiple(tmp_buf, sizeof(tmp_buf), cap0));
			ast_debug(1, "Oooh, '%s' was %s/(format %s)\n",
				  ast_channel_name(c0), ast_sockaddr_stringify(&ac0),
				  ast_getformatname_multiple(tmp_buf, sizeof(tmp_buf), oldcap0));
			if (glue1->update_peer(c1, t0.len ? instance0 : NULL,
						vt0.len ? vinstance0 : NULL,
						tt0.len ? tinstance0 : NULL,
						cap0, 0)) {
				ast_log(LOG_WARNING, "Channel '%s' failed to update to '%s'\n", ast_channel_name(c1), ast_channel_name(c0));
			}
			ast_sockaddr_copy(&ac0, &t0);
			ast_sockaddr_copy(&vac0, &vt0);
			ast_sockaddr_copy(&tac0, &tt0);
			ast_format_cap_copy(oldcap0, cap0);
		}

		ms = ast_remaining_ms(start, timeoutms);
		/* Wait for frame to come in on the channels */
		if (!(who = ast_waitfor_n(cs, 2, &ms))) {
			if (!ms) {
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
			    (fr->subclass.integer == AST_CONTROL_T38_PARAMETERS) ||
				(fr->subclass.integer == AST_CONTROL_UPDATE_RTP_PEER)) {
				if (fr->subclass.integer == AST_CONTROL_HOLD) {
					/* If we someone went on hold we want the other side to reinvite back to us */
					if (who == c0) {
						glue1->update_peer(c1, NULL, NULL, NULL, 0, 0);
					} else {
						glue0->update_peer(c0, NULL, NULL, NULL, 0, 0);
					}
				} else if (fr->subclass.integer == AST_CONTROL_UNHOLD ||
					fr->subclass.integer == AST_CONTROL_UPDATE_RTP_PEER) {
					/* If they went off hold they should go back to being direct, or if we have
					 * been told to force a peer update, go ahead and do it. */
					if (who == c0) {
						glue1->update_peer(c1, instance0, vinstance0, tinstance0, cap0, 0);
					} else {
						glue0->update_peer(c0, instance1, vinstance1, tinstance1, cap1, 0);
					}
				}
				/* Update local address information */
				ast_rtp_instance_get_remote_address(instance0, &t0);
				ast_sockaddr_copy(&ac0, &t0);
				ast_rtp_instance_get_remote_address(instance1, &t1);
				ast_sockaddr_copy(&ac1, &t1);
				/* Update codec information */
				ast_channel_lock(c0);
				if (glue0->get_codec && ast_channel_tech_pvt(c0)) {
					ast_format_cap_remove_all(cap0);
					ast_format_cap_remove_all(oldcap0);
					glue0->get_codec(c0, cap0);
					ast_format_cap_append(oldcap0, cap0);

				}
				ast_channel_unlock(c0);
				ast_channel_lock(c1);
				if (glue1->get_codec && ast_channel_tech_pvt(c1)) {
					ast_format_cap_remove_all(cap1);
					ast_format_cap_remove_all(oldcap1);
					glue1->get_codec(c1, cap1);
					ast_format_cap_append(oldcap1, cap1);
				}
				ast_channel_unlock(c1);
				/* Since UPDATE_BRIDGE_PEER is only used by the bridging code, don't forward it */
				if (fr->subclass.integer != AST_CONTROL_UPDATE_RTP_PEER) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_CONNECTED_LINE) {
				if (ast_channel_connected_line_sub(who, other, fr, 1) &&
					ast_channel_connected_line_macro(who, other, fr, other == c0, 1)) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_REDIRECTING) {
				if (ast_channel_redirecting_sub(who, other, fr, 1) &&
					ast_channel_redirecting_macro(who, other, fr, other == c0, 1)) {
					ast_indicate_data(other, fr->subclass.integer, fr->data.ptr, fr->datalen);
				}
				ast_frfree(fr);
			} else if (fr->subclass.integer == AST_CONTROL_PVT_CAUSE_CODE) {
				ast_channel_hangupcause_hash_set(other, fr->data.ptr, fr->datalen);
				ast_frfree(fr);
			} else {
				*fo = fr;
				*rc = who;
				ast_debug(1, "Got a FRAME_CONTROL (%d) frame on channel %s\n", fr->subclass.integer, ast_channel_name(who));
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

	if (ast_test_flag(ast_channel_flags(c0), AST_FLAG_ZOMBIE)) {
		ast_debug(1, "Channel '%s' Zombie cleardown from bridge\n", ast_channel_name(c0));
	} else if (ast_channel_tech_pvt(c0) != pvt0) {
		ast_debug(1, "Channel c0->'%s' pvt changed, in bridge with c1->'%s'\n", ast_channel_name(c0), ast_channel_name(c1));
	} else if (glue0 != ast_rtp_instance_get_glue(ast_channel_tech(c0)->type)) {
		ast_debug(1, "Channel c0->'%s' technology changed, in bridge with c1->'%s'\n", ast_channel_name(c0), ast_channel_name(c1));
	} else if (glue0->update_peer(c0, NULL, NULL, NULL, 0, 0)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", ast_channel_name(c0));
	}
	if (ast_test_flag(ast_channel_flags(c1), AST_FLAG_ZOMBIE)) {
		ast_debug(1, "Channel '%s' Zombie cleardown from bridge\n", ast_channel_name(c1));
	} else if (ast_channel_tech_pvt(c1) != pvt1) {
		ast_debug(1, "Channel c1->'%s' pvt changed, in bridge with c0->'%s'\n", ast_channel_name(c1), ast_channel_name(c0));
	} else if (glue1 != ast_rtp_instance_get_glue(ast_channel_tech(c1)->type)) {
		ast_debug(1, "Channel c1->'%s' technology changed, in bridge with c0->'%s'\n", ast_channel_name(c1), ast_channel_name(c0));
	} else if (glue1->update_peer(c1, NULL, NULL, NULL, 0, 0)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to break RTP bridge\n", ast_channel_name(c1));
	}

	instance0->bridged = NULL;
	instance1->bridged = NULL;

	ast_poll_channel_del(c0, c1);

remote_bridge_cleanup:
	ast_format_cap_destroy(oldcap0);
	ast_format_cap_destroy(oldcap1);

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
	struct ast_sockaddr addr1 = { {0, }, }, addr2 = { {0, }, };
	enum ast_rtp_glue_result audio_glue0_res = AST_RTP_GLUE_RESULT_FORBID, video_glue0_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_rtp_glue_result audio_glue1_res = AST_RTP_GLUE_RESULT_FORBID, video_glue1_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_bridge_result res = AST_BRIDGE_FAILED;
	enum ast_rtp_dtmf_mode dmode;
	struct ast_format_cap *cap0 = ast_format_cap_alloc_nolock();
	struct ast_format_cap *cap1 = ast_format_cap_alloc_nolock();
	int unlock_chans = 1;
	int read_ptime0, read_ptime1, write_ptime0, write_ptime1;

	if (!cap0 || !cap1) {
		unlock_chans = 0;
		goto done;
	}

	/* Lock both channels so we can look for the glue that binds them together */
	ast_channel_lock(c0);
	while (ast_channel_trylock(c1)) {
		ast_channel_unlock(c0);
		usleep(1);
		ast_channel_lock(c0);
	}

	/* Ensure neither channel got hungup during lock avoidance */
	if (ast_check_hangup(c0) || ast_check_hangup(c1)) {
		ast_log(LOG_WARNING, "Got hangup while attempting to bridge '%s' and '%s'\n", ast_channel_name(c0), ast_channel_name(c1));
		goto done;
	}

	/* Grab glue that binds each channel to something using the RTP engine */
	if (!(glue0 = ast_rtp_instance_get_glue(ast_channel_tech(c0)->type)) || !(glue1 = ast_rtp_instance_get_glue(ast_channel_tech(c1)->type))) {
		ast_debug(1, "Can't find native functions for channel '%s'\n", glue0 ? ast_channel_name(c1) : ast_channel_name(c0));
		goto done;
	}

	audio_glue0_res = glue0->get_rtp_info(c0, &instance0);
	video_glue0_res = glue0->get_vrtp_info ? glue0->get_vrtp_info(c0, &vinstance0) : AST_RTP_GLUE_RESULT_FORBID;

	audio_glue1_res = glue1->get_rtp_info(c1, &instance1);
	video_glue1_res = glue1->get_vrtp_info ? glue1->get_vrtp_info(c1, &vinstance1) : AST_RTP_GLUE_RESULT_FORBID;

	/* If the channels are of the same technology, they might have limitations on remote bridging */
	if (ast_channel_tech(c0) == ast_channel_tech(c1)) {
		if (audio_glue0_res == audio_glue1_res && audio_glue1_res == AST_RTP_GLUE_RESULT_REMOTE) {
			if (glue0->allow_rtp_remote && !(glue0->allow_rtp_remote(c0, c1))) {
				/* If the allow_rtp_remote indicates that remote isn't allowed, revert to local bridge */
				audio_glue0_res = audio_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
			}
		}
		if (video_glue0_res == video_glue1_res && video_glue1_res == AST_RTP_GLUE_RESULT_REMOTE) {
			if (glue0->allow_vrtp_remote && !(glue0->allow_vrtp_remote(c0, c1))) {
				/* if the allow_vrtp_remote indicates that remote isn't allowed, revert to local bridge */
				video_glue0_res = video_glue1_res = AST_RTP_GLUE_RESULT_LOCAL;
			}
		}
	}

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
	dmode = ast_rtp_instance_dtmf_mode_get(instance0);
	if ((flags & AST_BRIDGE_DTMF_CHANNEL_0) && dmode) {
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}
	dmode = ast_rtp_instance_dtmf_mode_get(instance1);
	if ((flags & AST_BRIDGE_DTMF_CHANNEL_1) && dmode) {
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}

	/* If we have gotten to a local bridge make sure that both sides have the same local bridge callback and that they are DTMF compatible */
	if ((audio_glue0_res == AST_RTP_GLUE_RESULT_LOCAL || audio_glue1_res == AST_RTP_GLUE_RESULT_LOCAL) && ((instance0->engine->local_bridge != instance1->engine->local_bridge) || (instance0->engine->dtmf_compatible && !instance0->engine->dtmf_compatible(c0, instance0, c1, instance1)))) {
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}

	/* Make sure that codecs match */
	if (glue0->get_codec){
		glue0->get_codec(c0, cap0);
	}
	if (glue1->get_codec) {
		glue1->get_codec(c1, cap1);
	}
	if (!ast_format_cap_is_empty(cap0) && !ast_format_cap_is_empty(cap1) && !ast_format_cap_has_joint(cap0, cap1)) {
		char tmp0[256] = { 0, };
		char tmp1[256] = { 0, };
		ast_debug(1, "Channel codec0 = %s is not codec1 = %s, cannot native bridge in RTP.\n",
			ast_getformatname_multiple(tmp0, sizeof(tmp0), cap0),
			ast_getformatname_multiple(tmp1, sizeof(tmp1), cap1));
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}

	read_ptime0 = (ast_codec_pref_getsize(&instance0->codecs.pref, ast_channel_rawreadformat(c0))).cur_ms;
	read_ptime1 = (ast_codec_pref_getsize(&instance1->codecs.pref, ast_channel_rawreadformat(c1))).cur_ms;
	write_ptime0 = (ast_codec_pref_getsize(&instance0->codecs.pref, ast_channel_rawwriteformat(c0))).cur_ms;
	write_ptime1 = (ast_codec_pref_getsize(&instance1->codecs.pref, ast_channel_rawwriteformat(c1))).cur_ms;

	if (read_ptime0 != write_ptime1 || read_ptime1 != write_ptime0) {
		ast_debug(1, "Packetization differs between RTP streams (%d != %d or %d != %d). Cannot native bridge in RTP\n",
				read_ptime0, write_ptime1, read_ptime1, write_ptime0);
		res = AST_BRIDGE_FAILED_NOWARN;
		goto done;
	}

	instance0->glue = glue0;
	instance1->glue = glue1;
	instance0->chan = c0;
	instance1->chan = c1;

	/* Depending on the end result for bridging either do a local bridge or remote bridge */
	if (audio_glue0_res == AST_RTP_GLUE_RESULT_LOCAL || audio_glue1_res == AST_RTP_GLUE_RESULT_LOCAL) {
		ast_verb(3, "Locally bridging %s and %s\n", ast_channel_name(c0), ast_channel_name(c1));
		res = local_bridge_loop(c0, c1, instance0, instance1, timeoutms, flags, fo, rc, ast_channel_tech_pvt(c0), ast_channel_tech_pvt(c1));
	} else {
		ast_verb(3, "Remotely bridging %s and %s\n", ast_channel_name(c0), ast_channel_name(c1));
		res = remote_bridge_loop(c0, c1, instance0, instance1, vinstance0, vinstance1,
				tinstance0, tinstance1, glue0, glue1, cap0, cap1, timeoutms, flags,
				fo, rc, ast_channel_tech_pvt(c0), ast_channel_tech_pvt(c1));
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
	ast_format_cap_destroy(cap1);
	ast_format_cap_destroy(cap0);

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

void ast_rtp_instance_early_bridge_make_compatible(struct ast_channel *c_dst, struct ast_channel *c_src)
{
	struct ast_rtp_instance *instance_dst = NULL, *instance_src = NULL,
		*vinstance_dst = NULL, *vinstance_src = NULL,
		*tinstance_dst = NULL, *tinstance_src = NULL;
	struct ast_rtp_glue *glue_dst, *glue_src;
	enum ast_rtp_glue_result audio_glue_dst_res = AST_RTP_GLUE_RESULT_FORBID, video_glue_dst_res = AST_RTP_GLUE_RESULT_FORBID;
	enum ast_rtp_glue_result audio_glue_src_res = AST_RTP_GLUE_RESULT_FORBID, video_glue_src_res = AST_RTP_GLUE_RESULT_FORBID;
	struct ast_format_cap *cap_dst = ast_format_cap_alloc_nolock();
	struct ast_format_cap *cap_src = ast_format_cap_alloc_nolock();

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
	if (!ast_format_cap_has_joint(cap_dst, cap_src)) {
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

	ast_format_cap_destroy(cap_dst);
	ast_format_cap_destroy(cap_src);

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
	struct ast_format_cap *cap0 = ast_format_cap_alloc_nolock();
	struct ast_format_cap *cap1 = ast_format_cap_alloc_nolock();
	int res = 0;

	/* If there is no second channel just immediately bail out, we are of no use in that scenario */
	if (!c1) {
		ast_format_cap_destroy(cap0);
		ast_format_cap_destroy(cap1);
		return -1;
	}

	/* Lock both channels so we can look for the glue that binds them together */
	ast_channel_lock(c0);
	while (ast_channel_trylock(c1)) {
		ast_channel_unlock(c0);
		usleep(1);
		ast_channel_lock(c0);
	}

	if (!cap1 || !cap0) {
		goto done;
	}

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
	if (!ast_format_cap_has_joint(cap0, cap1)) {
		goto done;
	}

	/* Bridge media early */
	if (glue0->update_peer(c0, instance1, vinstance1, tinstance1, cap1, 0)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to setup early bridge to '%s'\n", ast_channel_name(c0), c1 ? ast_channel_name(c1) : "<unspecified>");
	}

	res = 0;

done:
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);

	ast_format_cap_destroy(cap0);
	ast_format_cap_destroy(cap1);

	unref_instance_cond(&instance0);
	unref_instance_cond(&instance1);
	unref_instance_cond(&vinstance0);
	unref_instance_cond(&vinstance1);
	unref_instance_cond(&tinstance0);
	unref_instance_cond(&tinstance1);

	if (!res) {
		ast_debug(1, "Setting early bridge SDP of '%s' with that of '%s'\n", ast_channel_name(c0), c1 ? ast_channel_name(c1) : "<unspecified>");
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
		if (!ast_format_cap_is_empty(result)) {
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
	ast_free(dtls_cfg->pvtfile);
	ast_free(dtls_cfg->cipher);
	ast_free(dtls_cfg->cafile);
	ast_free(dtls_cfg->capath);
}

static void set_next_mime_type(const struct ast_format *format, int rtp_code, char *type, char *subtype, unsigned int sample_rate)
{
	int x = mime_types_len;
	if (ARRAY_LEN(ast_rtp_mime_types) == mime_types_len) {
		return;
	}

	ast_rwlock_wrlock(&mime_types_lock);
	if (format) {
		ast_rtp_mime_types[x].payload_type.asterisk_format = 1;
		ast_format_copy(&ast_rtp_mime_types[x].payload_type.format, format);
	} else {
		ast_rtp_mime_types[x].payload_type.rtp_code = rtp_code;
	}
	ast_rtp_mime_types[x].type = type;
	ast_rtp_mime_types[x].subtype = subtype;
	ast_rtp_mime_types[x].sample_rate = sample_rate;
	mime_types_len++;
	ast_rwlock_unlock(&mime_types_lock);
}

static void add_static_payload(int map, const struct ast_format *format, int rtp_code)
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
		ast_log(LOG_WARNING, "No Dynamic RTP mapping available for format %s\n" ,ast_getformatname(format));
		ast_rwlock_unlock(&static_RTP_PT_lock);
		return;
	}

	if (format) {
		static_RTP_PT[map].asterisk_format = 1;
		ast_format_copy(&static_RTP_PT[map].format, format);
	} else {
		static_RTP_PT[map].rtp_code = rtp_code;
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);
}

int ast_rtp_engine_load_format(const struct ast_format *format)
{
	switch (format->id) {
	case AST_FORMAT_SILK:
		set_next_mime_type(format, 0, "audio", "SILK", ast_format_rate(format));
		add_static_payload(-1, format, 0);
		break;
	case AST_FORMAT_CELT:
		set_next_mime_type(format, 0, "audio", "CELT", ast_format_rate(format));
		add_static_payload(-1, format, 0);
		break;
	default:
		break;
	}

	return 0;
}

int ast_rtp_engine_unload_format(const struct ast_format *format)
{
	int x;
	int y = 0;

	ast_rwlock_wrlock(&static_RTP_PT_lock);
	/* remove everything pertaining to this format id from the lists */
	for (x = 0; x < AST_RTP_MAX_PT; x++) {
		if (ast_format_cmp(&static_RTP_PT[x].format, format) == AST_FORMAT_CMP_EQUAL) {
			memset(&static_RTP_PT[x], 0, sizeof(struct ast_rtp_payload_type));
		}
	}
	ast_rwlock_unlock(&static_RTP_PT_lock);


	ast_rwlock_wrlock(&mime_types_lock);
	/* rebuild the list skipping the items matching this id */
	for (x = 0; x < mime_types_len; x++) {
		if (ast_format_cmp(&ast_rtp_mime_types[x].payload_type.format, format) == AST_FORMAT_CMP_EQUAL) {
			continue;
		}
		ast_rtp_mime_types[y] = ast_rtp_mime_types[x];
		y++;
	}
	mime_types_len = y;
	ast_rwlock_unlock(&mime_types_lock);
	return 0;
}

int ast_rtp_engine_init()
{
	struct ast_format tmpfmt;

	ast_rwlock_init(&mime_types_lock);
	ast_rwlock_init(&static_RTP_PT_lock);

	/* Define all the RTP mime types available */
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_G723_1, 0), 0, "audio", "G723", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_GSM, 0), 0, "audio", "GSM", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0), 0, "audio", "PCMU", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0), 0, "audio", "G711U", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0), 0, "audio", "PCMA", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0), 0, "audio", "G711A", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_G726, 0), 0, "audio", "G726-32", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_ADPCM, 0), 0, "audio", "DVI4", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0), 0, "audio", "L16", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR16, 0), 0, "audio", "L16", 16000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR16, 0), 0, "audio", "L16-256", 16000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_LPC10, 0), 0, "audio", "LPC", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0), 0, "audio", "G729", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0), 0, "audio", "G729A", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0), 0, "audio", "G.729", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_SPEEX, 0), 0, "audio", "speex", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_SPEEX16, 0), 0,  "audio", "speex", 16000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_SPEEX32, 0), 0,  "audio", "speex", 32000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_ILBC, 0), 0, "audio", "iLBC", 8000);
	/* this is the sample rate listed in the RTP profile for the G.722 codec, *NOT* the actual sample rate of the media stream */
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_G722, 0), 0, "audio", "G722", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_G726_AAL2, 0), 0, "audio", "AAL2-G726-32", 8000);
	set_next_mime_type(NULL, AST_RTP_DTMF, "audio", "telephone-event", 8000);
	set_next_mime_type(NULL, AST_RTP_CISCO_DTMF, "audio", "cisco-telephone-event", 8000);
	set_next_mime_type(NULL, AST_RTP_CN, "audio", "CN", 8000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_JPEG, 0), 0, "video", "JPEG", 90000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_PNG, 0), 0, "video", "PNG", 90000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_H261, 0), 0, "video", "H261", 90000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_H263, 0), 0, "video", "H263", 90000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_H263_PLUS, 0), 0, "video", "H263-1998", 90000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_H264, 0), 0, "video", "H264", 90000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_MP4_VIDEO, 0), 0, "video", "MP4V-ES", 90000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_T140RED, 0), 0, "text", "RED", 1000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_T140, 0), 0, "text", "T140", 1000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_SIREN7, 0), 0, "audio", "G7221", 16000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_SIREN14, 0), 0, "audio", "G7221", 32000);
	set_next_mime_type(ast_format_set(&tmpfmt, AST_FORMAT_G719, 0), 0, "audio", "G719", 48000);

	/* Define the static rtp payload mappings */
	add_static_payload(0, ast_format_set(&tmpfmt, AST_FORMAT_ULAW, 0), 0);
	#ifdef USE_DEPRECATED_G726
	add_static_payload(2, ast_format_set(&tmpfmt, AST_FORMAT_G726, 0), 0);/* Technically this is G.721, but if Cisco can do it, so can we... */
	#endif
	add_static_payload(3, ast_format_set(&tmpfmt, AST_FORMAT_GSM, 0), 0);
	add_static_payload(4, ast_format_set(&tmpfmt, AST_FORMAT_G723_1, 0), 0);
	add_static_payload(5, ast_format_set(&tmpfmt, AST_FORMAT_ADPCM, 0), 0);/* 8 kHz */
	add_static_payload(6, ast_format_set(&tmpfmt, AST_FORMAT_ADPCM, 0), 0); /* 16 kHz */
	add_static_payload(7, ast_format_set(&tmpfmt, AST_FORMAT_LPC10, 0), 0);
	add_static_payload(8, ast_format_set(&tmpfmt, AST_FORMAT_ALAW, 0), 0);
	add_static_payload(9, ast_format_set(&tmpfmt, AST_FORMAT_G722, 0), 0);
	add_static_payload(10, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0), 0); /* 2 channels */
	add_static_payload(11, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR, 0), 0); /* 1 channel */
	add_static_payload(13, NULL, AST_RTP_CN);
	add_static_payload(16, ast_format_set(&tmpfmt, AST_FORMAT_ADPCM, 0), 0); /* 11.025 kHz */
	add_static_payload(17, ast_format_set(&tmpfmt, AST_FORMAT_ADPCM, 0), 0); /* 22.050 kHz */
	add_static_payload(18, ast_format_set(&tmpfmt, AST_FORMAT_G729A, 0), 0);
	add_static_payload(19, NULL, AST_RTP_CN);         /* Also used for CN */
	add_static_payload(26, ast_format_set(&tmpfmt, AST_FORMAT_JPEG, 0), 0);
	add_static_payload(31, ast_format_set(&tmpfmt, AST_FORMAT_H261, 0), 0);
	add_static_payload(34, ast_format_set(&tmpfmt, AST_FORMAT_H263, 0), 0);
	add_static_payload(97, ast_format_set(&tmpfmt, AST_FORMAT_ILBC, 0), 0);
	add_static_payload(98, ast_format_set(&tmpfmt, AST_FORMAT_H263_PLUS, 0), 0);
	add_static_payload(99, ast_format_set(&tmpfmt, AST_FORMAT_H264, 0), 0);
	add_static_payload(101, NULL, AST_RTP_DTMF);
	add_static_payload(102, ast_format_set(&tmpfmt, AST_FORMAT_SIREN7, 0), 0);
	add_static_payload(103, ast_format_set(&tmpfmt, AST_FORMAT_H263_PLUS, 0), 0);
	add_static_payload(104, ast_format_set(&tmpfmt, AST_FORMAT_MP4_VIDEO, 0), 0);
	add_static_payload(105, ast_format_set(&tmpfmt, AST_FORMAT_T140RED, 0), 0);   /* Real time text chat (with redundancy encoding) */
	add_static_payload(106, ast_format_set(&tmpfmt, AST_FORMAT_T140, 0), 0);     /* Real time text chat */
	add_static_payload(110, ast_format_set(&tmpfmt, AST_FORMAT_SPEEX, 0), 0);
	add_static_payload(111, ast_format_set(&tmpfmt, AST_FORMAT_G726, 0), 0);
	add_static_payload(112, ast_format_set(&tmpfmt, AST_FORMAT_G726_AAL2, 0), 0);
	add_static_payload(115, ast_format_set(&tmpfmt, AST_FORMAT_SIREN14, 0), 0);
	add_static_payload(116, ast_format_set(&tmpfmt, AST_FORMAT_G719, 0), 0);
	add_static_payload(117, ast_format_set(&tmpfmt, AST_FORMAT_SPEEX16, 0), 0);
	add_static_payload(118, ast_format_set(&tmpfmt, AST_FORMAT_SLINEAR16, 0), 0); /* 16 Khz signed linear */
	add_static_payload(119, ast_format_set(&tmpfmt, AST_FORMAT_SPEEX32, 0), 0);
	add_static_payload(121, NULL, AST_RTP_CISCO_DTMF);   /* Must be type 121 */

	return 0;
}
