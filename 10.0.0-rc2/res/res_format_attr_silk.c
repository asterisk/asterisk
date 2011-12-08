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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/format.h"

/*!
 * \brief SILK attribute structure.
 *
 * \note The only attribute that affects compatibility here is the sample rate.
 */
struct silk_attr {
	unsigned int samplerate;
	unsigned int maxbitrate;
	unsigned int dtx;
	unsigned int fec;
	unsigned int packetloss_percentage;
};

static enum ast_format_cmp_res silk_cmp(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2)
{
	struct silk_attr *attr1 = (struct silk_attr *) fattr1;
	struct silk_attr *attr2 = (struct silk_attr *) fattr2;

	if (attr1->samplerate == attr2->samplerate) {
		return AST_FORMAT_CMP_EQUAL;
	}
	return AST_FORMAT_CMP_NOT_EQUAL;
}

static int silk_get_val(const struct ast_format_attr *fattr, int key, void *result)
{
	const struct silk_attr *attr = (struct silk_attr *) fattr;
	int *val = result;

	switch (key) {
	case SILK_ATTR_KEY_SAMP_RATE:
		*val = attr->samplerate;
		break;
	case SILK_ATTR_KEY_MAX_BITRATE:
		*val = attr->maxbitrate;
		break;
	case SILK_ATTR_KEY_DTX:
		*val = attr->dtx;
		break;
	case SILK_ATTR_KEY_FEC:
		*val = attr->fec;
		break;
	case SILK_ATTR_KEY_PACKETLOSS_PERCENTAGE:
		*val = attr->packetloss_percentage;
		break;
	default:
		return -1;
		ast_log(LOG_WARNING, "unknown attribute type %d\n", key);
	}
	return 0;
}

static int silk_isset(const struct ast_format_attr *fattr, va_list ap)
{
	enum silk_attr_keys key;
	const struct silk_attr *attr = (struct silk_attr *) fattr;

	for (key = va_arg(ap, int);
		key != AST_FORMAT_ATTR_END;
		key = va_arg(ap, int))
	{
		switch (key) {
		case SILK_ATTR_KEY_SAMP_RATE:
			if (attr->samplerate != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case SILK_ATTR_KEY_MAX_BITRATE:
			if (attr->maxbitrate != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case SILK_ATTR_KEY_DTX:
			if (attr->dtx != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case SILK_ATTR_KEY_FEC:
			if (attr->fec != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case SILK_ATTR_KEY_PACKETLOSS_PERCENTAGE:
			if (attr->packetloss_percentage != (va_arg(ap, int))) {
				return -1;
			}
			break;
		default:
			return -1;
			ast_log(LOG_WARNING, "unknown attribute type %d\n", key);
		}
	}
	return 0;
}
static int silk_getjoint(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2, struct ast_format_attr *result)
{
	struct silk_attr *attr1 = (struct silk_attr *) fattr1;
	struct silk_attr *attr2 = (struct silk_attr *) fattr2;
	struct silk_attr *attr_res = (struct silk_attr *) result;
	int joint = -1;

	attr_res->samplerate = attr1->samplerate & attr2->samplerate;
	/* sample rate is the only attribute that has any bearing on if joint capabilities exist or not */
	if (attr_res->samplerate) {
		joint = 0;
	}
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
	return joint;
}

static void silk_set(struct ast_format_attr *fattr, va_list ap)
{
	enum silk_attr_keys key;
	struct silk_attr *attr = (struct silk_attr *) fattr;

	for (key = va_arg(ap, int);
		key != AST_FORMAT_ATTR_END;
		key = va_arg(ap, int))
	{
		switch (key) {
		case SILK_ATTR_KEY_SAMP_RATE:
			attr->samplerate = (va_arg(ap, int));
			break;
		case SILK_ATTR_KEY_MAX_BITRATE:
			attr->maxbitrate = (va_arg(ap, int));
			break;
		case SILK_ATTR_KEY_DTX:
			attr->dtx = (va_arg(ap, int));
			break;
		case SILK_ATTR_KEY_FEC:
			attr->fec = (va_arg(ap, int));
			break;
		case SILK_ATTR_KEY_PACKETLOSS_PERCENTAGE:
			attr->packetloss_percentage = (va_arg(ap, int));
			break;
		default:
			ast_log(LOG_WARNING, "unknown attribute type %d\n", key);
		}
	}
}

static struct ast_format_attr_interface silk_interface = {
	.id = AST_FORMAT_SILK,
	.format_attr_cmp = silk_cmp,
	.format_attr_get_joint = silk_getjoint,
	.format_attr_set = silk_set,
	.format_attr_isset = silk_isset,
	.format_attr_get_val = silk_get_val,
};

static int load_module(void)
{
	if (ast_format_attr_reg_interface(&silk_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_format_attr_unreg_interface(&silk_interface);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SILK Format Attribute Module",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
