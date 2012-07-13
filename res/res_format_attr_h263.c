/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 *
 */

/*! \file
 *
 * \brief H.263 Format Attribute Module
 *
 * \author\verbatim Joshua Colp <jcolp@digium.com> \endverbatim
 *
 * This is a format attribute module for the H.263 codec.
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/format.h"

enum h263_attr_keys {
	H263_ATTR_KEY_SQCIF,       /*!< Minimum picture interval for SQCIF resolution */
	H263_ATTR_KEY_QCIF,        /*!< Minimum picture interval for QCIF resolution */
	H263_ATTR_KEY_CIF,         /*!< Minimum picture interval for CIF resolution */
	H263_ATTR_KEY_CIF4,        /*!< Minimum picture interval for CIF4 resolution */
	H263_ATTR_KEY_CIF16,       /*!< Minimum picture interval for CIF16 resolution */
	H263_ATTR_KEY_VGA,         /*!< Minimum picture interval for VGA resolution */
	H263_ATTR_KEY_CUSTOM_XMAX, /*!< Custom resolution (Xmax) */
	H263_ATTR_KEY_CUSTOM_YMAX, /*!< Custom resolution (Ymax) */
	H263_ATTR_KEY_CUSTOM_MPI,  /*!< Custom resolution (MPI) */
	H263_ATTR_KEY_F,           /*!< F annex support */
	H263_ATTR_KEY_I,           /*!< I annex support */
	H263_ATTR_KEY_J,           /*!< J annex support */
	H263_ATTR_KEY_T,           /*!< T annex support */
	H263_ATTR_KEY_K,           /*!< K annex support */
	H263_ATTR_KEY_N,           /*!< N annex support */
	H263_ATTR_KEY_P_SUB1,      /*!< Reference picture resampling (sub mode 1) */
	H263_ATTR_KEY_P_SUB2,      /*!< Reference picture resampling (sub mode 2) */
	H263_ATTR_KEY_P_SUB3,      /*!< Reference picture resampling (sub mode 3) */
	H263_ATTR_KEY_P_SUB4,      /*!< Reference picture resampling (sub mode 4) */
	H263_ATTR_KEY_PAR_WIDTH,   /*!< Pixel aspect ratio (width) */
	H263_ATTR_KEY_PAR_HEIGHT,  /*!< Pixel aspect ratio (height) */
	H263_ATTR_KEY_BPP,         /*!< Bits per picture maximum */
	H263_ATTR_KEY_HRD,         /*!< Hypothetical reference decoder status */
	H263_ATTR_KEY_END,         /*!< End terminator for list */
};

static int h263_format_attr_get_joint(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2, struct ast_format_attr *result)
{
	int i;

	/* These are all receiver options so we just copy over what they sent */
	for (i = H263_ATTR_KEY_SQCIF; i < H263_ATTR_KEY_END; i++) {
		result->format_attr[i] = fattr1->format_attr[i] ? fattr1->format_attr[i] : fattr2->format_attr[i];
	}

	return 0;
}

static int h263_format_attr_sdp_parse(struct ast_format_attr *format_attr, const char *attributes)
{
	char *attribs = ast_strdupa(attributes), *attrib;

	while ((attrib = strsep(&attribs, ";"))) {
		unsigned int val, val2 = 0, val3 = 0, val4 = 0;

		if (sscanf(attrib, "SQCIF=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_SQCIF] = val;
		} else if (sscanf(attrib, "QCIF=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_QCIF] = val;
		} else if (sscanf(attrib, "CIF=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_CIF] = val;
		} else if (sscanf(attrib, "CIF4=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_CIF4] = val;
		} else if (sscanf(attrib, "CIF16=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_CIF16] = val;
		} else if (sscanf(attrib, "VGA=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_VGA] = val;
		} else if (sscanf(attrib, "CUSTOM=%30u,%30u,%30u", &val, &val2, &val3) == 3) {
			format_attr->format_attr[H263_ATTR_KEY_CUSTOM_XMAX] = val;
			format_attr->format_attr[H263_ATTR_KEY_CUSTOM_YMAX] = val2;
			format_attr->format_attr[H263_ATTR_KEY_CUSTOM_MPI] = val3;
		} else if (sscanf(attrib, "F=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_F] = val;
		} else if (sscanf(attrib, "I=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_I] = val;
		} else if (sscanf(attrib, "J=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_J] = val;
		} else if (sscanf(attrib, "T=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_T] = val;
		} else if (sscanf(attrib, "K=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_K] = val;
		} else if (sscanf(attrib, "N=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_N] = val;
		} else if (sscanf(attrib, "PAR=%30u:%30u", &val, &val2) == 2) {
			format_attr->format_attr[H263_ATTR_KEY_PAR_WIDTH] = val;
			format_attr->format_attr[H263_ATTR_KEY_PAR_HEIGHT] = val2;
		} else if (sscanf(attrib, "BPP=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_BPP] = val;
		} else if (sscanf(attrib, "HRD=%30u", &val) == 1) {
			format_attr->format_attr[H263_ATTR_KEY_HRD] = val;
		} else if (sscanf(attrib, "P=%30u,%30u,%30u,%30u", &val, &val2, &val3, &val4) > 0) {
			format_attr->format_attr[H263_ATTR_KEY_P_SUB1] = val;
			format_attr->format_attr[H263_ATTR_KEY_P_SUB2] = val2;
			format_attr->format_attr[H263_ATTR_KEY_P_SUB3] = val3;
			format_attr->format_attr[H263_ATTR_KEY_P_SUB4] = val4;
		}
	}

	return 0;
}

/*! \brief Helper function which converts a key enum into a string value for SDP */
static const char *h263_attr_key_to_str(enum h263_attr_keys key, const struct ast_format_attr *format_attr)
{
	switch (key) {
	case H263_ATTR_KEY_SQCIF:
		return format_attr->format_attr[key] ? "SQCIF" : NULL;
	case H263_ATTR_KEY_QCIF:
		return format_attr->format_attr[key] ? "QCIF" : NULL;
	case H263_ATTR_KEY_CIF:
		return format_attr->format_attr[key] ? "CIF" : NULL;
	case H263_ATTR_KEY_CIF4:
		return format_attr->format_attr[key] ? "CIF4" : NULL;
	case H263_ATTR_KEY_CIF16:
		return format_attr->format_attr[key] ? "CIF16" : NULL;
	case H263_ATTR_KEY_VGA:
		return format_attr->format_attr[key] ? "VGA" : NULL;
	case H263_ATTR_KEY_F:
		return "F";
	case H263_ATTR_KEY_I:
		return "I";
	case H263_ATTR_KEY_J:
		return "J";
	case H263_ATTR_KEY_T:
		return "T";
	case H263_ATTR_KEY_K:
		return "K";
	case H263_ATTR_KEY_N:
		return "N";
	case H263_ATTR_KEY_BPP:
		return "BPP";
	case H263_ATTR_KEY_HRD:
		return "HRD";
	case H263_ATTR_KEY_CUSTOM_XMAX:
	case H263_ATTR_KEY_CUSTOM_YMAX:
	case H263_ATTR_KEY_CUSTOM_MPI:
	case H263_ATTR_KEY_P_SUB1:
	case H263_ATTR_KEY_P_SUB2:
	case H263_ATTR_KEY_P_SUB3:
	case H263_ATTR_KEY_P_SUB4:
	case H263_ATTR_KEY_PAR_WIDTH:
	case H263_ATTR_KEY_PAR_HEIGHT:
	case H263_ATTR_KEY_END:
	default:
		return NULL;
	}

	return NULL;
}

static void h263_format_attr_sdp_generate(const struct ast_format_attr *format_attr, unsigned int payload, struct ast_str **str)
{
	int i, added = 0;

	for (i = H263_ATTR_KEY_SQCIF; i < H263_ATTR_KEY_END; i++) {
		const char *name;

		if (i == H263_ATTR_KEY_CUSTOM_XMAX) {
			if (!format_attr->format_attr[H263_ATTR_KEY_CUSTOM_XMAX] || !format_attr->format_attr[H263_ATTR_KEY_CUSTOM_YMAX] ||
			    !format_attr->format_attr[H263_ATTR_KEY_CUSTOM_MPI]) {
				continue;
			}

			if (!added) {
				ast_str_append(str, 0, "a=fmtp:%d CUSTOM=%u,%u,%u", payload, format_attr->format_attr[H263_ATTR_KEY_CUSTOM_XMAX],
					       format_attr->format_attr[H263_ATTR_KEY_CUSTOM_YMAX], format_attr->format_attr[H263_ATTR_KEY_CUSTOM_MPI]);
				added = 1;
			} else {
				ast_str_append(str, 0, ";CUSTOM=%u,%u,%u", format_attr->format_attr[H263_ATTR_KEY_CUSTOM_XMAX],
					       format_attr->format_attr[H263_ATTR_KEY_CUSTOM_YMAX], format_attr->format_attr[H263_ATTR_KEY_CUSTOM_MPI]);
			}
		} else if (i == H263_ATTR_KEY_PAR_WIDTH) {
			if (!format_attr->format_attr[H263_ATTR_KEY_PAR_WIDTH] || !format_attr->format_attr[H263_ATTR_KEY_PAR_HEIGHT]) {
				continue;
			}

			if (!added) {
				ast_str_append(str, 0, "a=fmtp:%d PAR=%u:%u", payload, format_attr->format_attr[H263_ATTR_KEY_PAR_WIDTH],
					       format_attr->format_attr[H263_ATTR_KEY_PAR_HEIGHT]);
				added = 1;
			} else {
				ast_str_append(str, 0, ";PAR=%u:%u", format_attr->format_attr[H263_ATTR_KEY_PAR_WIDTH],
					       format_attr->format_attr[H263_ATTR_KEY_PAR_HEIGHT]);
			}
		} else if (i == H263_ATTR_KEY_P_SUB1) {
			if (!format_attr->format_attr[H263_ATTR_KEY_P_SUB1]) {
				continue;
			}

			if (!added) {
				ast_str_append(str, 0, "a=fmtp:%d P=%u", payload, format_attr->format_attr[H263_ATTR_KEY_P_SUB1]);
				added = 1;
			} else {
				ast_str_append(str, 0, ";P=%u", format_attr->format_attr[H263_ATTR_KEY_P_SUB1]);
			}

			if (format_attr->format_attr[H263_ATTR_KEY_P_SUB2]) {
				ast_str_append(str, 0, ",%u", format_attr->format_attr[H263_ATTR_KEY_P_SUB2]);
			}
                        if (format_attr->format_attr[H263_ATTR_KEY_P_SUB3]) {
                                ast_str_append(str, 0, ",%u", format_attr->format_attr[H263_ATTR_KEY_P_SUB3]);
                        }
                        if (format_attr->format_attr[H263_ATTR_KEY_P_SUB4]) {
                                ast_str_append(str, 0, ",%u", format_attr->format_attr[H263_ATTR_KEY_P_SUB4]);
                        }

		} else if ((name = h263_attr_key_to_str(i, format_attr))) {
			if (!added) {
				ast_str_append(str, 0, "a=fmtp:%d %s=%u", payload, name, format_attr->format_attr[i]);
				added = 1;
			} else {
				ast_str_append(str, 0, ";%s=%u", name, format_attr->format_attr[i]);
			}
		}
	}

	if (added) {
		ast_str_append(str, 0, "\r\n");
	}

	return;
}

static struct ast_format_attr_interface h263_format_attr_interface = {
	.id = AST_FORMAT_H263,
	.format_attr_get_joint = h263_format_attr_get_joint,
	.format_attr_sdp_parse = h263_format_attr_sdp_parse,
	.format_attr_sdp_generate = h263_format_attr_sdp_generate,
};

static struct ast_format_attr_interface h263p_format_attr_interface = {
        .id = AST_FORMAT_H263_PLUS,
	.format_attr_get_joint = h263_format_attr_get_joint,
        .format_attr_sdp_parse = h263_format_attr_sdp_parse,
        .format_attr_sdp_generate = h263_format_attr_sdp_generate,
};

static int unload_module(void)
{
	ast_format_attr_unreg_interface(&h263_format_attr_interface);
	ast_format_attr_unreg_interface(&h263p_format_attr_interface);

	return 0;
}

static int load_module(void)
{
	if (ast_format_attr_reg_interface(&h263_format_attr_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_format_attr_reg_interface(&h263p_format_attr_interface)) {
		ast_format_attr_unreg_interface(&h263_format_attr_interface);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "H.263 Format Attribute Module",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEFAULT,
);
