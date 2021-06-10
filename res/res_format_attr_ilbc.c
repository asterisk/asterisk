/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Alexander Traud
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
 * \brief iLBC format attribute interface
 *
 * \author Alexander Traud <pabstraud@compuserve.com>
 *
 * \note http://tools.ietf.org/html/rfc3952
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <ctype.h>                      /* for tolower */

#include "asterisk/module.h"
#include "asterisk/format.h"
#include "asterisk/strings.h"           /* for ast_str_append */
#include "asterisk/utils.h"             /* for ast_free */

#include "asterisk/ilbc.h"

static struct ilbc_attr default_ilbc_attr = {
	.mode = 20,
};

static void ilbc_destroy(struct ast_format *format)
{
	struct ilbc_attr *attr = ast_format_get_attribute_data(format);

	ast_free(attr);
}

static int ilbc_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct ilbc_attr *original = ast_format_get_attribute_data(src);
	struct ilbc_attr *attr = ast_malloc(sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
	} else {
		*attr = default_ilbc_attr;
	}

	ast_format_set_attribute_data(dst, attr);

	return 0;
}

static struct ast_format *ilbc_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	char *attribs = ast_strdupa(attributes), *attrib;
	struct ast_format *cloned;
	struct ilbc_attr *attr;
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

	if ((kvp = strstr(attribs, "mode")) && sscanf(kvp, "mode=%30u", &val) == 1) {
		attr->mode = val;
	} else {
		attr->mode = 30; /* optional attribute; 30 is default value */
	}

	return cloned;
}

static void ilbc_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct ilbc_attr *attr = ast_format_get_attribute_data(format);

	if (!attr) {
		attr = &default_ilbc_attr;
	}

	/* When the VoIP/SIP client Zoiper calls Asterisk and its
	 * iLBC 20 is disabled but iLBC 30 enabled, Zoiper still
	 * falls back to iLBC 20, when there is no mode=30 in the
	 * answer. Consequently, Zoiper defaults to iLBC 20. To
	 * make that client happy, Asterisk sends mode always.
	 * tested in June 2016, Zoiper Premium 1.13.2 for iPhone
	 */
	/* if (attr->mode != 30) */ {
		ast_str_append(str, 0, "a=fmtp:%u mode=%u\r\n", payload, attr->mode);
	}
}

static struct ast_format *ilbc_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct ast_format *jointformat;
	struct ilbc_attr *attr1 = ast_format_get_attribute_data(format1);
	struct ilbc_attr *attr2 = ast_format_get_attribute_data(format2);
	struct ilbc_attr *attr_res;

	if (!attr1) {
		attr1 = &default_ilbc_attr;
	}

	if (!attr2) {
		attr2 = &default_ilbc_attr;
	}

	jointformat = ast_format_clone(format1);
	if (!jointformat) {
		return NULL;
	}
	attr_res = ast_format_get_attribute_data(jointformat);

	if (attr1->mode != attr2->mode) {
		attr_res->mode = 30;
	}

	return jointformat;
}

static struct ast_format_interface ilbc_interface = {
	.format_destroy = ilbc_destroy,
	.format_clone = ilbc_clone,
	.format_cmp = NULL,
	.format_get_joint = ilbc_getjoint,
	.format_attribute_set = NULL,
	.format_parse_sdp_fmtp = ilbc_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = ilbc_generate_sdp_fmtp,
};

static int load_module(void)
{
	if (ast_format_interface_register("ilbc", &ilbc_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "iLBC Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
