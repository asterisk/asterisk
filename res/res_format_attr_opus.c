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
	<load_priority>channel_depend</load_priority>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/format.h"

/*!
 * \brief Opus attribute structure.
 *
 * \note http://tools.ietf.org/html/draft-ietf-payload-rtp-opus-00.
 */
struct opus_attr {
	unsigned int maxbitrate;	        /* Default 64-128 kb/s for FB stereo music */
	unsigned int maxplayrate	        /* Default 48000 */;
	unsigned int minptime;	            /* Default 3, but it's 10 in format.c */
	unsigned int stereo;	            /* Default 0 */
	unsigned int cbr;	                /* Default 0 */
	unsigned int fec;	                /* Default 0 */
	unsigned int dtx;	                /* Default 0 */
	unsigned int spropmaxcapturerate;	/* Default 48000 */
	unsigned int spropstereo;	        /* Default 0 */
};

static struct opus_attr default_opus_attr = {
	.fec    = 0,
	.dtx    = 0,
	.stereo = 0,
};

static void opus_destroy(struct ast_format *format)
{
	struct opus_attr *attr = ast_format_get_attribute_data(format);

	ast_free(attr);
}

static int opus_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct opus_attr *original = ast_format_get_attribute_data(src);
	struct opus_attr *attr = ast_calloc(1, sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
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
	}
	if ((kvp = strstr(attributes, "sprop-maxcapturerate")) && sscanf(kvp, "sprop-maxcapturerate=%30u", &val) == 1) {
		attr->spropmaxcapturerate = val;
	}
	if ((kvp = strstr(attributes, "minptime")) && sscanf(kvp, "minptime=%30u", &val) == 1) {
		attr->minptime = val;
	}
	if ((kvp = strstr(attributes, "maxaveragebitrate")) && sscanf(kvp, "maxaveragebitrate=%30u", &val) == 1) {
		attr->maxbitrate = val;
	}
	if ((kvp = strstr(attributes, " stereo")) && sscanf(kvp, " stereo=%30u", &val) == 1) {
		attr->stereo = val;
	}
	if ((kvp = strstr(attributes, ";stereo")) && sscanf(kvp, ";stereo=%30u", &val) == 1) {
		attr->stereo = val;
	}
	if ((kvp = strstr(attributes, "sprop-stereo")) && sscanf(kvp, "sprop-stereo=%30u", &val) == 1) {
		attr->spropstereo = val;
	}
	if ((kvp = strstr(attributes, "cbr")) && sscanf(kvp, "cbr=%30u", &val) == 1) {
		attr->cbr = val;
	}
	if ((kvp = strstr(attributes, "useinbandfec")) && sscanf(kvp, "useinbandfec=%30u", &val) == 1) {
		attr->fec = val;
	}
	if ((kvp = strstr(attributes, "usedtx")) && sscanf(kvp, "usedtx=%30u", &val) == 1) {
		attr->dtx = val;
	}

	return cloned;
}

static void opus_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct opus_attr *attr = ast_format_get_attribute_data(format);

	if (!attr) {
		return;
	}

	/* FIXME should we only generate attributes that were explicitly set? */
	ast_str_append(str, 0,
				"a=fmtp:%u "
					"maxplaybackrate=%u;"
					"sprop-maxcapturerate=%u;"
					"minptime=%u;"
					"maxaveragebitrate=%u;"
					"stereo=%d;"
					"sprop-stereo=%d;"
					"cbr=%d;"
					"useinbandfec=%d;"
					"usedtx=%d\r\n",
			payload,
			attr->maxplayrate ? attr->maxplayrate : 48000,	/* maxplaybackrate */
			attr->spropmaxcapturerate ? attr->spropmaxcapturerate : 48000,	/* sprop-maxcapturerate */
			attr->minptime > 10 ? attr->minptime : 10,	/* minptime */
			attr->maxbitrate ? attr->maxbitrate : 20000,	/* maxaveragebitrate */
			attr->stereo ? 1 : 0,		/* stereo */
			attr->spropstereo ? 1 : 0,		/* sprop-stereo */
			attr->cbr ? 1 : 0,		/* cbr */
			attr->fec ? 1 : 0,		/* useinbandfec */
			attr->dtx ? 1 : 0		/* usedtx */
	);
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

	/* Only do dtx if both sides want it. DTX is a trade off between
	 * computational complexity and bandwidth. */
	attr_res->dtx = attr1->dtx && attr2->dtx ? 1 : 0;

	/* Only do FEC if both sides want it.  If a peer specifically requests not
	 * to receive with FEC, it may be a waste of bandwidth. */
	attr_res->fec = attr1->fec && attr2->fec ? 1 : 0;

	/* Only do stereo if both sides want it.  If a peer specifically requests not
	 * to receive stereo signals, it may be a waste of bandwidth. */
	attr_res->stereo = attr1->stereo && attr2->stereo ? 1 : 0;

	/* FIXME: do we need to join other attributes as well, e.g., minptime, cbr, etc.? */

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
		attr->minptime = val;
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

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Opus Format Attribute Module");
