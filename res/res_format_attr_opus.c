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

static int opus_sdp_parse(struct ast_format_attr *format_attr, const char *attributes)
{
	struct opus_attr *attr = (struct opus_attr *) format_attr;
	const char *kvp;
	unsigned int val;

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

	return 0;
}

static void opus_sdp_generate(const struct ast_format_attr *format_attr, unsigned int payload, struct ast_str **str)
{
	struct opus_attr *attr = (struct opus_attr *) format_attr;

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

static int opus_get_val(const struct ast_format_attr *fattr, int key, void *result)
{
	const struct opus_attr *attr = (struct opus_attr *) fattr;
	int *val = result;

	switch (key) {
	case OPUS_ATTR_KEY_MAX_BITRATE:
		*val = attr->maxbitrate;
		break;
	case OPUS_ATTR_KEY_MAX_PLAYRATE:
		*val = attr->maxplayrate;
		break;
	case OPUS_ATTR_KEY_MINPTIME:
		*val = attr->minptime;
		break;
	case OPUS_ATTR_KEY_STEREO:
		*val = attr->stereo;
		break;
	case OPUS_ATTR_KEY_CBR:
		*val = attr->cbr;
		break;
	case OPUS_ATTR_KEY_FEC:
		*val = attr->fec;
		break;
	case OPUS_ATTR_KEY_DTX:
		*val = attr->dtx;
		break;
	case OPUS_ATTR_KEY_SPROP_CAPTURE_RATE:
		*val = attr->spropmaxcapturerate;
		break;
	case OPUS_ATTR_KEY_SPROP_STEREO:
		*val = attr->spropstereo;
		break;
	default:
		ast_log(LOG_WARNING, "unknown attribute type %d\n", key);
		return -1;
	}
	return 0;
}

static int opus_isset(const struct ast_format_attr *fattr, va_list ap)
{
	enum opus_attr_keys key;
	const struct opus_attr *attr = (struct opus_attr *) fattr;

	for (key = va_arg(ap, int);
		key != AST_FORMAT_ATTR_END;
		key = va_arg(ap, int))
	{
		switch (key) {
		case OPUS_ATTR_KEY_MAX_BITRATE:
			if (attr->maxbitrate != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case OPUS_ATTR_KEY_MAX_PLAYRATE:
			if (attr->maxplayrate != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case OPUS_ATTR_KEY_MINPTIME:
			if (attr->minptime != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case OPUS_ATTR_KEY_STEREO:
			if (attr->stereo != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case OPUS_ATTR_KEY_CBR:
			if (attr->cbr != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case OPUS_ATTR_KEY_FEC:
			if (attr->fec != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case OPUS_ATTR_KEY_DTX:
			if (attr->dtx != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case OPUS_ATTR_KEY_SPROP_CAPTURE_RATE:
			if (attr->spropmaxcapturerate != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case OPUS_ATTR_KEY_SPROP_STEREO:
			if (attr->spropstereo != (va_arg(ap, int))) {
				return -1;
			}
			break;
		default:
			ast_log(LOG_WARNING, "unknown attribute type %u\n", key);
			return -1;
		}
	}
	return 0;
}
static int opus_getjoint(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2, struct ast_format_attr *result)
{
	struct opus_attr *attr1 = (struct opus_attr *) fattr1;
	struct opus_attr *attr2 = (struct opus_attr *) fattr2;
	struct opus_attr *attr_res = (struct opus_attr *) result;
	int joint = 0;

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

	return joint;
}

static void opus_set(struct ast_format_attr *fattr, va_list ap)
{
	enum opus_attr_keys key;
	struct opus_attr *attr = (struct opus_attr *) fattr;

	for (key = va_arg(ap, int);
		key != AST_FORMAT_ATTR_END;
		key = va_arg(ap, int))
	{
		switch (key) {
		case OPUS_ATTR_KEY_MAX_BITRATE:
			attr->maxbitrate = (va_arg(ap, int));
			break;
		case OPUS_ATTR_KEY_MAX_PLAYRATE:
			attr->maxplayrate = (va_arg(ap, int));
			break;
		case OPUS_ATTR_KEY_MINPTIME:
			attr->minptime = (va_arg(ap, int));
			break;
		case OPUS_ATTR_KEY_STEREO:
			attr->stereo = (va_arg(ap, int));
			break;
		case OPUS_ATTR_KEY_CBR:
			attr->cbr = (va_arg(ap, int));
			break;
		case OPUS_ATTR_KEY_FEC:
			attr->fec = (va_arg(ap, int));
			break;
		case OPUS_ATTR_KEY_DTX:
			attr->dtx = (va_arg(ap, int));
			break;
		case OPUS_ATTR_KEY_SPROP_CAPTURE_RATE:
			attr->spropmaxcapturerate = (va_arg(ap, int));
			break;
		case OPUS_ATTR_KEY_SPROP_STEREO:
			attr->spropstereo = (va_arg(ap, int));
			break;
		default:
			ast_log(LOG_WARNING, "unknown attribute type %u\n", key);
		}
	}
}

static struct ast_format_attr_interface opus_interface = {
	.id = AST_FORMAT_OPUS,
	.format_attr_get_joint = opus_getjoint,
	.format_attr_set = opus_set,
	.format_attr_isset = opus_isset,
	.format_attr_get_val = opus_get_val,
	.format_attr_sdp_parse = opus_sdp_parse,
	.format_attr_sdp_generate = opus_sdp_generate,
};

static int load_module(void)
{
	if (ast_format_attr_reg_interface(&opus_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_format_attr_unreg_interface(&opus_interface);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Opus Format Attribute Module",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
