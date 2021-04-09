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
 * \brief SILK format attribute interface
 *
 * \author David Vossel <dvossel@digium.com>
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
#include "asterisk/utils.h"             /* for MAX, MIN */

/*!
 * \brief SILK attribute structure.
 *
 * \note The only attribute that affects compatibility here is the sample rate.
 */
struct silk_attr {
	unsigned int maxbitrate;
	unsigned int dtx;
	unsigned int fec;
	unsigned int packetloss_percentage;
};

static void silk_destroy(struct ast_format *format)
{
	struct silk_attr *attr = ast_format_get_attribute_data(format);

	ast_free(attr);
}

static void attr_init(struct silk_attr *attr)
{
	memset(attr, 0, sizeof(*attr));
}

static int silk_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct silk_attr *original = ast_format_get_attribute_data(src);
	struct silk_attr *attr = ast_malloc(sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
	} else {
		attr_init(attr);
	}

	ast_format_set_attribute_data(dst, attr);

	return 0;
}

static struct ast_format *silk_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	char *attribs = ast_strdupa(attributes), *attrib;
	struct ast_format *cloned;
	struct silk_attr *attr;
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

	if (sscanf(attribs, "maxaveragebitrate=%30u", &val) == 1) {
		attr->maxbitrate = val;
	}
	if (sscanf(attribs, "usedtx=%30u", &val) == 1) {
		attr->dtx = val;
	}
	if (sscanf(attribs, "useinbandfec=%30u", &val) == 1) {
		attr->fec = val;
	}

	return cloned;
}

static void silk_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct silk_attr *attr = ast_format_get_attribute_data(format);

	if (!attr) {
		return;
	}

	if ((attr->maxbitrate > 5000) && (attr->maxbitrate < 40000)) {
		ast_str_append(str, 0, "a=fmtp:%u maxaveragebitrate=%u\r\n", payload, attr->maxbitrate);
	}

	if (attr->dtx) {
		ast_str_append(str, 0, "a=fmtp:%u usedtx=%u\r\n", payload, attr->dtx);
	}
	if (attr->fec) {
		ast_str_append(str, 0, "a=fmtp:%u useinbandfec=%u\r\n", payload, attr->fec);
	}
}

static enum ast_format_cmp_res silk_cmp(const struct ast_format *format1, const struct ast_format *format2)
{
	if (ast_format_get_sample_rate(format1) == ast_format_get_sample_rate(format2)) {
		return AST_FORMAT_CMP_EQUAL;
	}

	return AST_FORMAT_CMP_NOT_EQUAL;
}

static struct ast_format *silk_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct silk_attr *attr1 = ast_format_get_attribute_data(format1);
	struct silk_attr *attr2 = ast_format_get_attribute_data(format2);
	struct ast_format *jointformat;
	struct silk_attr *attr_res;

	if (ast_format_get_sample_rate(format1) != ast_format_get_sample_rate(format2)) {
		return NULL;
	}

	jointformat = ast_format_clone(format1);
	if (!jointformat) {
		return NULL;
	}
	attr_res = ast_format_get_attribute_data(jointformat);

	if (!attr1 || !attr2) {
		attr_init(attr_res);
	} else {
		/* Take the lowest max bitrate */
		attr_res->maxbitrate = MIN(attr1->maxbitrate, attr2->maxbitrate);

		/* Only do dtx if both sides want it. DTX is a trade off between
		 * computational complexity and bandwidth. */
		attr_res->dtx = attr1->dtx && attr2->dtx ? 1 : 0;

		/* Only do FEC if both sides want it.  If a peer specifically requests not
		 * to receive with FEC, it may be a waste of bandwidth. */
		attr_res->fec = attr1->fec && attr2->fec ? 1 : 0;

		/* Use the maximum packetloss percentage between the two attributes. This affects how
		 * much redundancy is used in the FEC. */
		attr_res->packetloss_percentage = MAX(attr1->packetloss_percentage, attr2->packetloss_percentage);
	}

	return jointformat;
}

static struct ast_format *silk_set(const struct ast_format *format, const char *name, const char *value)
{
	struct ast_format *cloned;
	struct silk_attr *attr;
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

	if (!strcasecmp(name, "max_bitrate")) {
		attr->maxbitrate = val;
	} else if (!strcasecmp(name, "dtx")) {
		attr->dtx = val;
	} else if (!strcasecmp(name, "fec")) {
		attr->fec = val;
	} else if (!strcasecmp(name, "packetloss_percentage")) {
		attr->packetloss_percentage = val;
	} else {
		ast_log(LOG_WARNING, "unknown attribute type %s\n", name);
	}

	return cloned;
}

static const void *silk_get(const struct ast_format *format, const char *name)
{
	struct silk_attr *attr = ast_format_get_attribute_data(format);
	unsigned int *val;

	if (!strcasecmp(name, "max_bitrate")) {
		val = &attr->maxbitrate;
	} else if (!strcasecmp(name, "dtx")) {
		val = &attr->dtx;
	} else if (!strcasecmp(name, "fec")) {
		val = &attr->fec;
	} else if (!strcasecmp(name, "packetloss_percentage")) {
		val = &attr->packetloss_percentage;
	} else {
		ast_log(LOG_WARNING, "unknown attribute type %s\n", name);
		return NULL;
	}

	return val;
}

static struct ast_format_interface silk_interface = {
	.format_destroy = silk_destroy,
	.format_clone = silk_clone,
	.format_cmp = silk_cmp,
	.format_get_joint = silk_getjoint,
	.format_attribute_set = silk_set,
	.format_attribute_get = silk_get,
	.format_parse_sdp_fmtp = silk_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = silk_generate_sdp_fmtp,
};

static int load_module(void)
{
	if (ast_format_interface_register("silk", &silk_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SILK Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
