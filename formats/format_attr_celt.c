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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

static enum ast_format_cmp_res celt_cmp(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2)
{
	struct celt_attr *attr1 = (struct celt_attr *) fattr1;
	struct celt_attr *attr2 = (struct celt_attr *) fattr2;

	if (attr1->samplerate == attr2->samplerate) {
		return AST_FORMAT_CMP_EQUAL;
	}
	return AST_FORMAT_CMP_NOT_EQUAL;
}

static int celt_get_val(const struct ast_format_attr *fattr, int key, void *result)
{
	const struct celt_attr *attr = (struct celt_attr *) fattr;
	int *val = result;

	switch (key) {
	case CELT_ATTR_KEY_SAMP_RATE:
		*val = attr->samplerate;
		break;
	case CELT_ATTR_KEY_MAX_BITRATE:
		*val = attr->maxbitrate;
		break;
	case CELT_ATTR_KEY_FRAME_SIZE:
		*val = attr->framesize;
		break;
	default:
		return -1;
		ast_log(LOG_WARNING, "unknown attribute type %d\n", key);
	}
	return 0;
}

static int celt_isset(const struct ast_format_attr *fattr, va_list ap)
{
	enum celt_attr_keys key;
	const struct celt_attr *attr = (struct celt_attr *) fattr;

	for (key = va_arg(ap, int);
		key != AST_FORMAT_ATTR_END;
		key = va_arg(ap, int))
	{
		switch (key) {
		case CELT_ATTR_KEY_SAMP_RATE:
			if (attr->samplerate != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case CELT_ATTR_KEY_MAX_BITRATE:
			if (attr->maxbitrate != (va_arg(ap, int))) {
				return -1;
			}
			break;
		case CELT_ATTR_KEY_FRAME_SIZE:
			if (attr->framesize != (va_arg(ap, int))) {
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
static int celt_getjoint(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2, struct ast_format_attr *result)
{
	struct celt_attr *attr1 = (struct celt_attr *) fattr1;
	struct celt_attr *attr2 = (struct celt_attr *) fattr2;
	struct celt_attr *attr_res = (struct celt_attr *) result;

	/* sample rate is the only attribute that has any bearing on if joint capabilities exist or not */
	if (attr1->samplerate != attr2->samplerate) {
		return -1;
	}
	/* either would work, they are guaranteed the same at this point. */
	attr_res->samplerate = attr1->samplerate;
	/* Take the lowest max bitrate */
	attr_res->maxbitrate = MIN(attr1->maxbitrate, attr2->maxbitrate);

	attr_res->framesize = attr2->framesize; /* TODO figure out what joint framesize means */
	return 0;
}

static void celt_set(struct ast_format_attr *fattr, va_list ap)
{
	enum celt_attr_keys key;
	struct celt_attr *attr = (struct celt_attr *) fattr;

	for (key = va_arg(ap, int);
		key != AST_FORMAT_ATTR_END;
		key = va_arg(ap, int))
	{
		switch (key) {
		case CELT_ATTR_KEY_SAMP_RATE:
			attr->samplerate = (va_arg(ap, int));
			break;
		case CELT_ATTR_KEY_MAX_BITRATE:
			attr->maxbitrate = (va_arg(ap, int));
			break;
		case CELT_ATTR_KEY_FRAME_SIZE:
			attr->framesize = (va_arg(ap, int));
			break;
		default:
			ast_log(LOG_WARNING, "unknown attribute type %d\n", key);
		}
	}
}

static struct ast_format_attr_interface celt_interface = {
	.id = AST_FORMAT_CELT,
	.format_attr_cmp = celt_cmp,
	.format_attr_get_joint = celt_getjoint,
	.format_attr_set = celt_set,
	.format_attr_isset = celt_isset,
	.format_attr_get_val = celt_get_val,
};

static int load_module(void)
{
	if (ast_format_attr_reg_interface(&celt_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_format_attr_unreg_interface(&celt_interface);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "CELT Format Attribute Module",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
