/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
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
 * \brief Media Format API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/logger.h"
#include "asterisk/codec.h"
#include "asterisk/format.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"

/*! \brief Number of buckets to use for format interfaces (should be prime for performance reasons) */
#define FORMAT_INTERFACE_BUCKETS 53

/*! \brief Definition of a media format */
struct ast_format {
	/*! Name of the format */
	const char *name;
	/*! \brief Pointer to the codec in use for this format */
	struct ast_codec *codec;
	/*! \brief Attribute specific data, implementation specific */
	void *attribute_data;
	/*! \brief Pointer to the optional format interface */
	const struct ast_format_interface *interface;
};

/*! \brief Structure used when registering a format interface */
struct format_interface {
	/*! \brief Pointer to the format interface itself */
	const struct ast_format_interface *interface;
	/*! \brief Name of the codec the interface is for */
	char codec[0];
};

/*! \brief Container for registered format interfaces */
static struct ao2_container *interfaces;

static int format_interface_hash(const void *obj, int flags)
{
	const struct format_interface *format_interface;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		return ast_str_hash(key);
	case OBJ_SEARCH_OBJECT:
		format_interface = obj;
		return ast_str_hash(format_interface->codec);
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
}

static int format_interface_cmp(void *obj, void *arg, int flags)
{
	const struct format_interface *left = obj;
	const struct format_interface *right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		cmp = strcmp(left->codec, right->codec);
		break;
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left->codec, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left->codec, right_key, strlen(right_key));
		break;
	default:
		ast_assert(0);
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}

	return CMP_MATCH;
}

/*! \brief Function called when the process is shutting down */
static void format_shutdown(void)
{
	ao2_cleanup(interfaces);
	interfaces = NULL;
}

int ast_format_init(void)
{
	interfaces = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_RWLOCK, FORMAT_INTERFACE_BUCKETS, format_interface_hash,
		format_interface_cmp);
	if (!interfaces) {
		return -1;
	}

	ast_register_cleanup(format_shutdown);

	return 0;
}

int __ast_format_interface_register(const char *codec, const struct ast_format_interface *interface, struct ast_module *mod)
{
	SCOPED_AO2WRLOCK(lock, interfaces);
	struct format_interface *format_interface;

	if (!interface->format_clone || !interface->format_destroy) {
		ast_log(LOG_ERROR, "Format interface for codec '%s' does not implement required callbacks\n", codec);
		return -1;
	}

	format_interface = ao2_find(interfaces, codec, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (format_interface) {
		ast_log(LOG_ERROR, "A format interface is already present for codec '%s'\n", codec);
		ao2_ref(format_interface, -1);
		return -1;
	}

	format_interface = ao2_alloc_options(sizeof(*format_interface) + strlen(codec) + 1,
		NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!format_interface) {
		return -1;
	}
	format_interface->interface = interface;
	strcpy(format_interface->codec, codec); /* Safe */

	ao2_link_flags(interfaces, format_interface, OBJ_NOLOCK);
	ao2_ref(format_interface, -1);

	ast_verb(2, "Registered format interface for codec '%s'\n", codec);

	return 0;
}

void *ast_format_get_attribute_data(const struct ast_format *format)
{
	return format->attribute_data;
}

void ast_format_set_attribute_data(struct ast_format *format, void *attribute_data)
{
	format->attribute_data = attribute_data;
}

/*! \brief Destructor for media formats */
static void format_destroy(void *obj)
{
	struct ast_format *format = obj;

	if (format->interface) {
		format->interface->format_destroy(format);
	}

	ao2_cleanup(format->codec);
}

struct ast_format *ast_format_create_named(const char *format_name, struct ast_codec *codec)
{
	struct ast_format *format;
	struct format_interface *format_interface;

	format = ao2_t_alloc_options(sizeof(*format), format_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK, S_OR(codec->description, ""));
	if (!format) {
		return NULL;
	}
	format->name = format_name;
	format->codec = ao2_bump(codec);

	format_interface = ao2_find(interfaces, codec->name, OBJ_SEARCH_KEY);
	if (format_interface) {
		format->interface = format_interface->interface;
		ao2_ref(format_interface, -1);
	}

	return format;
}

struct ast_format *ast_format_clone(const struct ast_format *format)
{
	struct ast_format *cloned = ast_format_create_named(format->name, format->codec);

	if (!cloned) {
		return NULL;
	}

	if (cloned->interface && cloned->interface->format_clone(format, cloned)) {
		ao2_ref(cloned, -1);
		return NULL;
	}

	return cloned;
}

struct ast_format *ast_format_create(struct ast_codec *codec)
{
	return ast_format_create_named(codec->name, codec);
}

enum ast_format_cmp_res ast_format_cmp(const struct ast_format *format1, const struct ast_format *format2)
{
	const struct ast_format_interface *interface;

	if (format1 == NULL || format2 == NULL) {
		return AST_FORMAT_CMP_NOT_EQUAL;
	}

	if (format1 == format2) {
		return AST_FORMAT_CMP_EQUAL;
	}

	if (format1->codec != format2->codec) {
		return AST_FORMAT_CMP_NOT_EQUAL;
	}

	interface = format1->interface ? format1->interface : format2->interface;

	if (interface && interface->format_cmp) {
		return interface->format_cmp(format1, format2);
	}

	return AST_FORMAT_CMP_EQUAL;
}

struct ast_format *ast_format_joint(const struct ast_format *format1, const struct ast_format *format2)
{
	const struct ast_format_interface *interface;

	if (format1->codec != format2->codec) {
		return NULL;
	}

	/* If the two formats are the same structure OR if the codec is the same and no attributes
	 * exist we can immediately return a format with reference count bumped up, since they are
	 * the same.
	 */
	if ((ast_format_cmp(format1, format2) == AST_FORMAT_CMP_EQUAL && !format1->attribute_data && !format2->attribute_data)) {
		return ao2_bump((struct ast_format*)format1);
	}

	interface = format1->interface ? format1->interface : format2->interface;

	/* If there is attribute data on either there has to be an interface */
	return interface->format_get_joint(format1, format2);
}

struct ast_format *ast_format_attribute_set(const struct ast_format *format, const char *name, const char *value)
{
	const struct ast_format_interface *interface = format->interface;

	if (!interface) {
		struct format_interface *format_interface = ao2_find(interfaces, format->codec->name, OBJ_SEARCH_KEY);
		if (format_interface) {
			interface = format_interface->interface;
			ao2_ref(format_interface, -1);
		}
	}

	if (!interface || !interface->format_attribute_set) {
		return ao2_bump((struct ast_format*)format);
	}

	return interface->format_attribute_set(format, name, value);
}

struct ast_format *ast_format_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	const struct ast_format_interface *interface = format->interface;

	if (!interface) {
		struct format_interface *format_interface = ao2_find(interfaces, format->codec->name, OBJ_SEARCH_KEY);
		if (format_interface) {
			interface = format_interface->interface;
			ao2_ref(format_interface, -1);
		}
	}

	if (!interface || !interface->format_parse_sdp_fmtp) {
		return ao2_bump((struct ast_format*)format);
	}

	return interface->format_parse_sdp_fmtp(format, attributes);
}

void ast_format_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	if (!format->interface || !format->interface->format_generate_sdp_fmtp) {
		return;
	}

	format->interface->format_generate_sdp_fmtp(format, payload, str);
}

struct ast_codec *ast_format_get_codec(const struct ast_format *format)
{
	return ao2_bump(format->codec);
}

unsigned int ast_format_get_codec_id(const struct ast_format *format)
{
	return format->codec->id;
}

const char *ast_format_get_name(const struct ast_format *format)
{
	return format->name;
}

const char *ast_format_get_codec_name(const struct ast_format *format)
{
	return format->codec->name;
}

int ast_format_can_be_smoothed(const struct ast_format *format)
{
	return format->codec->smooth;
}

enum ast_media_type ast_format_get_type(const struct ast_format *format)
{
	return format->codec->type;
}

unsigned int ast_format_get_default_ms(const struct ast_format *format)
{
	return format->codec->default_ms;
}

unsigned int ast_format_get_minimum_ms(const struct ast_format *format)
{
	return format->codec->minimum_ms;
}

unsigned int ast_format_get_maximum_ms(const struct ast_format *format)
{
	return format->codec->maximum_ms;
}

unsigned int ast_format_get_minimum_bytes(const struct ast_format *format)
{
	return format->codec->minimum_bytes;
}

unsigned int ast_format_get_sample_rate(const struct ast_format *format)
{
	return format->codec->sample_rate ?: 8000;
}

unsigned int ast_format_determine_length(const struct ast_format *format, unsigned int samples)
{
	return ast_codec_determine_length(format->codec, samples);
}
