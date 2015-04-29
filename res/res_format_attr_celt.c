/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief CELT format attribute interface
 *
 * \author David Vossel <dvossel@digium.com>
 */

/*** MODULEINFO
	<load_priority>channel_depend</load_priority>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/format.h"

/*!
 * \brief CELT attribute structure.
 *
 * \note The only attribute that affects compatibility here is the sample rate.
 */
struct celt_attr {
	unsigned int samplerate;
	unsigned int maxbitrate;
	unsigned int framesize;
};

static void celt_destroy(struct ast_format *format)
{
	struct celt_attr *attr = ast_format_get_attribute_data(format);

	ast_free(attr);
}

static int celt_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct celt_attr *original = ast_format_get_attribute_data(src);
	struct celt_attr *attr = ast_calloc(1, sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
	}

	ast_format_set_attribute_data(dst, attr);

	return 0;
}

static struct ast_format *celt_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	struct ast_format *cloned;
	struct celt_attr *attr;
	unsigned int val;

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	if (sscanf(attributes, "framesize=%30u", &val) == 1) {
		attr->framesize = val;
	}

	return cloned;
}

static void celt_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct celt_attr *attr = ast_format_get_attribute_data(format);

	if (!attr || !attr->framesize) {
		return;
	}

	ast_str_append(str, 0, "a=fmtp:%u framesize=%u\r\n", payload, attr->framesize);
}

static enum ast_format_cmp_res celt_cmp(const struct ast_format *format1, const struct ast_format *format2)
{
	struct celt_attr *attr1 = ast_format_get_attribute_data(format1);
	struct celt_attr *attr2 = ast_format_get_attribute_data(format2);

	if (((!attr1 || !attr1->samplerate) && (!attr2 || !attr2->samplerate)) ||
		(attr1->samplerate == attr2->samplerate)) {
		return AST_FORMAT_CMP_EQUAL;
	}

	return AST_FORMAT_CMP_NOT_EQUAL;
}

static struct ast_format *celt_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct celt_attr *attr1 = ast_format_get_attribute_data(format1);
	struct celt_attr *attr2 = ast_format_get_attribute_data(format2);
	struct ast_format *jointformat;
	struct celt_attr *jointattr;

	if (attr1 && attr2 && (attr1->samplerate != attr2->samplerate)) {
		return NULL;
	}

	jointformat = ast_format_clone(format1);
	if (!jointformat) {
		return NULL;
	}
	jointattr = ast_format_get_attribute_data(jointformat);

	/* either would work, they are guaranteed the same at this point. */
	jointattr->samplerate = attr1->samplerate;
	/* Take the lowest max bitrate */
	jointattr->maxbitrate = MIN(attr1->maxbitrate, attr2->maxbitrate);

	jointattr->framesize = attr2->framesize; /* TODO figure out what joint framesize means */

	return jointformat;
}

static struct ast_format *celt_set(const struct ast_format *format, const char *name, const char *value)
{
	struct ast_format *cloned;
	struct celt_attr *attr;
	unsigned int val;

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	if (sscanf(value, "%30u", &val) != 1) {
		ast_log(LOG_WARNING, "Unknown value '%s' for attribute type '%s'\n",
			value, name);
		ao2_ref(cloned, -1);
		return NULL;
	}

	if (!strcasecmp(name, "sample_rate")) {
		attr->samplerate = val;
	} else if (!strcasecmp(name, "max_bitrate")) {
		attr->maxbitrate = val;
	} else if (!strcasecmp(name, "frame_size")) {
		attr->framesize = val;
	} else {
		ast_log(LOG_WARNING, "Unknown attribute type '%s'\n", name);
		ao2_ref(cloned, -1);
		return NULL;
	}

	return cloned;
}

static struct ast_format_interface celt_interface = {
	.format_destroy = celt_destroy,
	.format_clone = celt_clone,
	.format_cmp = celt_cmp,
	.format_get_joint = celt_getjoint,
	.format_attribute_set = celt_set,
	.format_parse_sdp_fmtp = celt_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = celt_generate_sdp_fmtp,
};

static int load_module(void)
{
	if (ast_format_interface_register("celt", &celt_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "CELT Format Attribute Module");
