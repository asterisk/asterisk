/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, GILAWA Ltd
 *
 * Henning Westerholt <hw@gilawa.com>
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
 * \brief RED format attribute interface
 *
 * \author Henning Westerholt <hw@gilawa.com>
 *
 * \note https://www.rfc-editor.org/rfc/rfc4103.html
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
#include "asterisk/utils.h"             /* for ast_free */
#include "asterisk/rtp_engine.h"        /* for AST_RED_MAX_GENERATION */

/*
 * From RFC 4103: "Therefore, text/t140 is RECOMMENDED
 * to be the only payload type in the RTP stream."
 * For this reason we only support uniform payload types.
 *
 * If in the future this attribute should be used also for other redundant rtp streams,
 * it needs to be adapted together with the other infrastructure supporting it.
 */
struct red_attr {
	int red_num_gen; /* Number of generations */
	int red_payload; /* Payload for each generation (actually related to t140) */
	int cps;         /* Characters per second */
};

static struct red_attr default_red_attr = {
	.red_num_gen = 2,
	.red_payload = 98, /* default type that two test clients used */
	.cps = 30
};

static void red_destroy(struct ast_format *format)
{
	struct red_attr *attr = ast_format_get_attribute_data(format);
	ast_free(attr);
}

static int red_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct red_attr *original = ast_format_get_attribute_data(src);
	struct red_attr *attr = ast_malloc(sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
	} else {
		*attr = default_red_attr;
	}

	ast_format_set_attribute_data(dst, attr);
	return 0;
}

static struct ast_format *red_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	char *attribs = ast_strdupa(attributes);
	char *rest = NULL;
	struct ast_format *cloned;
	struct red_attr *attr;
	int red_num_gen = -1;
	int red_payload = -1;

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	attribs = strtok_r(attribs, "/", &rest);
	/* number of redundant generations is one less of the attributes */
	while (attribs && red_num_gen++ < AST_RED_MAX_GENERATION-1) {
		if (sscanf(attribs, "%30u", &red_payload) == 1) {
			attr->red_payload = red_payload;
		}
		attribs = strtok_r(NULL, "/", &rest);
	}

	attr->red_num_gen = red_num_gen;

	return cloned;
}

static void red_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct red_attr *attr = ast_format_get_attribute_data(format);
	int base_fmtp_size;
	int original_size;
	if (!attr) {
		ast_log(LOG_ERROR, "Invalid RED attributes\n");
		return;
	}

	/*
	* Generate the SDP fmtp line for RED
	* Assuming attr->red_payload holds the payload types for redundancy
	* and attr->red_num_gen holds the number of generations
	*/
	original_size = ast_str_strlen(*str);
	base_fmtp_size = ast_str_append(str, 0, "a=fmtp:%u ", payload);

	if (attr->red_num_gen > 0) {
		/* attributes are one more as the number of generations */
		for (int i = 0; i < attr->red_num_gen + 1; i++) {
			ast_str_append(str, 0, "%d/", attr->red_payload);
		}
	}

	if (base_fmtp_size == ast_str_strlen(*str) - original_size) {
		ast_str_truncate(*str, original_size);
	} else {
		ast_str_truncate(*str, -1);
		ast_str_append(str, 0, "\r\n");
	}

	ast_debug(3, "RED sdp written: %s\n", ast_str_buffer(*str));
}


static const void *red_attribute_get(const struct ast_format *format, const char *name)
{
	struct red_attr *attr = ast_format_get_attribute_data(format);
	int *val;

	if (!strcasecmp(name, "red_num_gen")) {
		val = &attr->red_num_gen;
	} else if (!strcasecmp(name, "red_payload")) {
		val = &attr->red_payload;
	} else if (!strcasecmp(name, "cps")) {
		val = &attr->cps;
	} else {
		ast_log(LOG_WARNING, "unknown attribute type %s\n", name);
		return NULL;
	}

	return val;
}


static struct ast_format *red_attribute_set(const struct ast_format *format, const char *name, const char *value)
{
	struct ast_format *cloned;
	struct red_attr *attr;
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

	if (!strcasecmp(name, "red_num_gen")) {
		attr->red_num_gen = val;
	} else if (!strcasecmp(name, "red_payload")) {
		attr->red_payload = val;
	} else if (!strcasecmp(name, "cps")) {
		attr->cps = val;
	} else {
		ast_log(LOG_WARNING, "unknown attribute type %s\n", name);
	}

	return cloned;
}


static struct ast_format *red_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct red_attr *attr1 = ast_format_get_attribute_data(format1);
	struct red_attr *attr2 = ast_format_get_attribute_data(format2);
	struct ast_format *jointformat;
	struct red_attr *attr_res;
	/* To re-use offered RED payload types */
	unsigned int use_side = 0;

	if (!attr1) {
		attr1 = &default_red_attr;
		use_side = 2;
	}

	if (!attr2) {
		attr2 = &default_red_attr;
		use_side = 1;
	}

	jointformat = ast_format_clone(use_side == 2 ? format2 : format1);
	if (!jointformat) {
		return NULL;
	}

	/* Get the resulting attributes for the joint format */
	attr_res = ast_format_get_attribute_data(jointformat);

	/* Determine joint attributes */
	if (attr1->red_num_gen == 0 || attr2->red_num_gen == 0) {
		/* If either format has no redundancy, set joint to no redundancy */
		attr_res->red_num_gen = 0;
	} else {
		/*
		* If both formats have redundancy, take the minimum number of generations.
		* Some tested clients only support e.g. 2 generations and it would be a waste
		* of bandwitdh or could even lead to incompatibilities.
		*/
		attr_res->red_num_gen = MIN(attr1->red_num_gen, attr2->red_num_gen);
		attr_res->red_payload = (use_side == 2) ? attr2->red_payload : attr1->red_payload;
	}

	ast_debug(3, "RED final joint: generations %d, payload %d\n", attr_res->red_num_gen,
		attr_res->red_payload);

	return jointformat;
}

static struct ast_format_interface red_interface = {
	.format_destroy = red_destroy,
	.format_clone = red_clone,
	.format_cmp = NULL,
	.format_get_joint = red_getjoint,
	.format_attribute_set = red_attribute_set,
	.format_attribute_get = red_attribute_get,
	.format_parse_sdp_fmtp = red_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = red_generate_sdp_fmtp,
};

static int load_module(void)
{
	if (ast_format_interface_register("red", &red_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "RED Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);