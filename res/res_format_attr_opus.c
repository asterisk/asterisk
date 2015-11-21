/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Lorenzo Miniero <lorenzo@meetecho.com>
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
 * \brief Opus format attribute interface
 *
 * \author Lorenzo Miniero <lorenzo@meetecho.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/format.h"
#include "asterisk/logger.h"            /* for ast_log, LOG_WARNING */
#include "asterisk/strings.h"           /* for ast_str_append */
#include "asterisk/utils.h"             /* for MIN, ast_malloc, ast_free */

/*!
 * \brief Opus attribute structure.
 *
 * \note http://tools.ietf.org/html/rfc7587#section-6
 */
struct opus_attr {
	unsigned int maxbitrate;
	unsigned int maxplayrate;
	unsigned int unused; /* was minptime, kept for binary compatibility */
	unsigned int stereo;
	unsigned int cbr;
	unsigned int fec;
	unsigned int dtx;
	unsigned int spropmaxcapturerate;
	unsigned int spropstereo;
};

static struct opus_attr default_opus_attr = {
	.maxplayrate         = 48000,
	.spropmaxcapturerate = 48000,
	.maxbitrate          = 510000,
	.stereo              = 0,
	.spropstereo         = 0,
	.cbr                 = 0,
	.fec                 = 1,
	.dtx                 = 0,
};

static void opus_destroy(struct ast_format *format)
{
	struct opus_attr *attr = ast_format_get_attribute_data(format);

	ast_free(attr);
}

static int opus_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct opus_attr *original = ast_format_get_attribute_data(src);
	struct opus_attr *attr = ast_malloc(sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
	} else {
		*attr = default_opus_attr;
	}

	ast_format_set_attribute_data(dst, attr);

	return 0;
}

static struct ast_format *opus_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	struct ast_format *cloned;
	struct opus_attr *attr;
	const char *kvp;
	unsigned int val;

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	if ((kvp = strstr(attributes, "maxplaybackrate")) && sscanf(kvp, "maxplaybackrate=%30u", &val) == 1) {
		attr->maxplayrate = val;
	} else {
		attr->maxplayrate = 48000;
	}

	if ((kvp = strstr(attributes, "sprop-maxcapturerate")) && sscanf(kvp, "sprop-maxcapturerate=%30u", &val) == 1) {
		attr->spropmaxcapturerate = val;
	} else {
		attr->spropmaxcapturerate = 48000;
	}

	if ((kvp = strstr(attributes, "maxaveragebitrate")) && sscanf(kvp, "maxaveragebitrate=%30u", &val) == 1) {
		attr->maxbitrate = val;
	} else {
		attr->maxbitrate = 510000;
	}

	if (!strncmp(attributes, "stereo=1", 8)) {
		attr->stereo = 1;
	} else if (strstr(attributes, " stereo=1")) {
		attr->stereo = 1;
	} else if (strstr(attributes, ";stereo=1")) {
		attr->stereo = 1;
	} else {
		attr->stereo = 0;
	}

	if (strstr(attributes, "sprop-stereo=1")) {
		attr->spropstereo = 1;
	} else {
		attr->spropstereo = 0;
	}

	if (strstr(attributes, "cbr=1")) {
		attr->cbr = 1;
	} else {
		attr->cbr = 0;
	}

	if (strstr(attributes, "useinbandfec=1")) {
		attr->fec = 1;
	} else {
		attr->fec = 0;
	}

	if (strstr(attributes, "usedtx=1")) {
		attr->dtx = 1;
	} else {
		attr->dtx = 0;
	}

	return cloned;
}

static void opus_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct opus_attr *attr = ast_format_get_attribute_data(format);
	int added = 0;

	if (!attr) {
		/*
		 * (Only) cached formats do not have attribute data assigned because
		 * they were created before this attribute module was registered.
		 * Therefore, we assume the default attribute values here.
		 */
		attr = &default_opus_attr;
	}

	if (48000 != attr->maxplayrate) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "maxplaybackrate=%u", attr->maxplayrate);
	}

	if (48000 != attr->spropmaxcapturerate) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "sprop-maxcapturerate=%u", attr->spropmaxcapturerate);
	}

	if (510000 != attr->maxbitrate) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "maxaveragebitrate=%u", attr->maxbitrate);
	}

	if (0 != attr->stereo) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "stereo=%u", attr->stereo);
	}

	if (0 != attr->spropstereo) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "sprop-stereo=%u", attr->spropstereo);
	}

	if (0 != attr->cbr) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "cbr=%u", attr->cbr);
	}

	if (0 != attr->fec) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "useinbandfec=%u", attr->fec);
	}

	if (0 != attr->dtx) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "usedtx=%u", attr->dtx);
	}

	if (added) {
		ast_str_append(str, 0, "\r\n");
	}
}

static struct ast_format *opus_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct opus_attr *attr1 = ast_format_get_attribute_data(format1);
	struct opus_attr *attr2 = ast_format_get_attribute_data(format2);
	struct ast_format *jointformat;
	struct opus_attr *attr_res;

	if (!attr1) {
		attr1 = &default_opus_attr;
	}

	if (!attr2) {
		attr2 = &default_opus_attr;
	}

	jointformat = ast_format_clone(format1);
	if (!jointformat) {
		return NULL;
	}
	attr_res = ast_format_get_attribute_data(jointformat);

	attr_res->dtx = attr1->dtx || attr2->dtx ? 1 : 0;

	/* Only do FEC if both sides want it.  If a peer specifically requests not
	 * to receive with FEC, it may be a waste of bandwidth. */
	attr_res->fec = attr1->fec && attr2->fec ? 1 : 0;

	attr_res->cbr = attr1->cbr || attr2->cbr ? 1 : 0;
	attr_res->spropstereo = attr1->spropstereo || attr2->spropstereo ? 1 : 0;

	/* Only do stereo if both sides want it.  If a peer specifically requests not
	 * to receive stereo signals, it may be a waste of bandwidth. */
	attr_res->stereo = attr1->stereo && attr2->stereo ? 1 : 0;

	attr_res->maxbitrate = MIN(attr1->maxbitrate, attr2->maxbitrate);
	attr_res->spropmaxcapturerate = MIN(attr1->spropmaxcapturerate, attr2->spropmaxcapturerate);
	attr_res->maxplayrate = MIN(attr1->maxplayrate, attr2->maxplayrate);

	return jointformat;
}

static struct ast_format *opus_set(const struct ast_format *format, const char *name, const char *value)
{
	struct ast_format *cloned;
	struct opus_attr *attr;
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
	} else if (!strcasecmp(name, "max_playrate")) {
		attr->maxplayrate = val;
	} else if (!strcasecmp(name, "minptime")) {
		attr->unused = val;
	} else if (!strcasecmp(name, "stereo")) {
		attr->stereo = val;
	} else if (!strcasecmp(name, "cbr")) {
		attr->cbr = val;
	} else if (!strcasecmp(name, "fec")) {
		attr->fec = val;
	} else if (!strcasecmp(name, "dtx")) {
		attr->dtx = val;
	} else if (!strcasecmp(name, "sprop_capture_rate")) {
		attr->spropmaxcapturerate = val;
	} else if (!strcasecmp(name, "sprop_stereo")) {
		attr->spropstereo = val;
	} else {
		ast_log(LOG_WARNING, "unknown attribute type %s\n", name);
	}

	return cloned;
}

static struct ast_format_interface opus_interface = {
	.format_destroy = opus_destroy,
	.format_clone = opus_clone,
	.format_get_joint = opus_getjoint,
	.format_attribute_set = opus_set,
	.format_parse_sdp_fmtp = opus_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = opus_generate_sdp_fmtp,
};

static int load_module(void)
{
	if (ast_format_interface_register("opus", &opus_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Opus Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
