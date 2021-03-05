/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Alexander Traud
 *
 * Alexander Traud <pabstraud@compuserve.com>
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
 * \brief VP8 format attribute interface
 *
 * \author Alexander Traud <pabstraud@compuserve.com>
 *
 * \note http://tools.ietf.org/html/draft-ietf-payload-vp8
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <ctype.h>                      /* for tolower */

#include "asterisk/module.h"
#include "asterisk/format.h"
#include "asterisk/logger.h"            /* for ast_log, LOG_WARNING */
#include "asterisk/strings.h"           /* for ast_str_append */
#include "asterisk/utils.h"             /* for MIN, ast_malloc, ast_free */

struct vp8_attr {
	unsigned int maximum_frame_rate;
	unsigned int maximum_frame_size;
};

static struct vp8_attr default_vp8_attr = {
	.maximum_frame_rate = UINT_MAX,
	.maximum_frame_size = UINT_MAX,
};

static void vp8_destroy(struct ast_format *format)
{
	struct vp8_attr *attr = ast_format_get_attribute_data(format);

	ast_free(attr);
}

static int vp8_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct vp8_attr *original = ast_format_get_attribute_data(src);
	struct vp8_attr *attr = ast_malloc(sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
	} else {
		*attr = default_vp8_attr;
	}

	ast_format_set_attribute_data(dst, attr);

	return 0;
}

static struct ast_format *vp8_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	char *attribs = ast_strdupa(attributes), *attrib;
	struct ast_format *cloned;
	struct vp8_attr *attr;
	const char *kvp;
	unsigned int val;

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	/* lower-case everything, so we are case-insensitive */
	for (attrib = attribs; *attrib; ++attrib) {
		*attrib = tolower(*attrib);
	} /* based on channels/chan_sip.c:process_a_sdp_image() */

	if ((kvp = strstr(attribs, "max-fr")) && sscanf(kvp, "max-fr=%30u", &val) == 1) {
		attr->maximum_frame_rate = val;
	} else {
		attr->maximum_frame_rate = UINT_MAX;
	}

	if ((kvp = strstr(attribs, "max-fs")) && sscanf(kvp, "max-fs=%30u", &val) == 1) {
		attr->maximum_frame_size = val;
	} else {
		attr->maximum_frame_size = UINT_MAX;
	}

	return cloned;
}

static void vp8_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct vp8_attr *attr = ast_format_get_attribute_data(format);
	int added = 0;

	if (!attr) {
		/*
		 * (Only) cached formats do not have attribute data assigned because
		 * they were created before this attribute module was registered.
		 * Therefore, we assume the default attribute values here.
		 */
		attr = &default_vp8_attr;
	}

	if (UINT_MAX != attr->maximum_frame_rate) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "max-fr=%u", attr->maximum_frame_rate);
	}

	if (UINT_MAX != attr->maximum_frame_size) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "max-fs=%u", attr->maximum_frame_size);
	}

	if (added) {
		ast_str_append(str, 0, "\r\n");
	}
}

static struct ast_format *vp8_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct vp8_attr *attr1 = ast_format_get_attribute_data(format1);
	struct vp8_attr *attr2 = ast_format_get_attribute_data(format2);
	struct ast_format *jointformat;
	struct vp8_attr *attr_res;

	if (!attr1) {
		attr1 = &default_vp8_attr;
	}

	if (!attr2) {
		attr2 = &default_vp8_attr;
	}

	jointformat = ast_format_clone(format1);
	if (!jointformat) {
		return NULL;
	}
	attr_res = ast_format_get_attribute_data(jointformat);

	attr_res->maximum_frame_rate = MIN(attr1->maximum_frame_rate, attr2->maximum_frame_rate);
	attr_res->maximum_frame_size = MIN(attr1->maximum_frame_size, attr2->maximum_frame_size);

	return jointformat;
}

static struct ast_format *vp8_set(const struct ast_format *format, const char *name, const char *value)
{
	struct ast_format *cloned;
	struct vp8_attr *attr;
	unsigned int val;

	if (sscanf(value, "%30u", &val) != 1) {
		ast_log(LOG_WARNING, "Unknown value '%s' for attribute type '%s'\n",
			value, name);
		return NULL;
	}

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	if (!strcasecmp(name, "maximum_frame_rate")) {
		attr->maximum_frame_rate = val;
	} else if (!strcasecmp(name, "maximum_frame_size")) {
		attr->maximum_frame_size = val;
	} else {
		ast_log(LOG_WARNING, "unknown attribute type %s\n", name);
	}

	return cloned;
}

static struct ast_format_interface vp8_interface = {
	.format_destroy = vp8_destroy,
	.format_clone = vp8_clone,
	.format_get_joint = vp8_getjoint,
	.format_attribute_set = vp8_set,
	.format_parse_sdp_fmtp = vp8_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = vp8_generate_sdp_fmtp,
};

static int load_module(void)
{
	if (ast_format_interface_register("vp8", &vp8_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "VP8 Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
