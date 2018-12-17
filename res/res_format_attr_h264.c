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
 * \brief H.264 Format Attribute Module
 *
 * \author\verbatim Joshua Colp <jcolp@digium.com> \endverbatim
 *
 * This is a format attribute module for the H.264 codec.
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/format.h"

/*! \brief Value that indicates an attribute is actually unset */
#define H264_ATTR_KEY_UNSET UINT8_MAX

/*! \brief Maximum size for SPS / PPS values in sprop-parameter-sets attribute
 *   if you change this value then you must change H264_MAX_SPS_PPS_SIZE_SCAN_LIMIT
 *   as well. */
#define H264_MAX_SPS_PPS_SIZE 16
/*! \brief This is used when executing sscanf on buffers of H264_MAX_SPS_PPS_SIZE
 *   length. It must ALWAYS be a string literal representation of one less than
 *   H264_MAX_SPS_PPS_SIZE */
#define H264_MAX_SPS_PPS_SIZE_SCAN_LIMIT "15"

struct h264_attr {
	unsigned int PROFILE_IDC;
	unsigned int PROFILE_IOP;
	unsigned int LEVEL;
	unsigned int MAX_MBPS;
	unsigned int MAX_FS;
	unsigned int MAX_CPB;
	unsigned int MAX_DPB;
	unsigned int MAX_BR;
	unsigned int MAX_SMBPS;
	unsigned int MAX_FPS;
	unsigned int REDUNDANT_PIC_CAP;
	unsigned int PARAMETER_ADD;
	unsigned int PACKETIZATION_MODE;
	unsigned int SPROP_INTERLEAVING_DEPTH;
	unsigned int SPROP_DEINT_BUF_REQ;
	unsigned int DEINT_BUF_CAP;
	unsigned int SPROP_INIT_BUF_TIME;
	unsigned int SPROP_MAX_DON_DIFF;
	unsigned int MAX_RCMD_NALU_SIZE;
	unsigned int LEVEL_ASYMMETRY_ALLOWED;
	char SPS[H264_MAX_SPS_PPS_SIZE];
	char PPS[H264_MAX_SPS_PPS_SIZE];
};

static void h264_destroy(struct ast_format *format)
{
	struct h264_attr *attr = ast_format_get_attribute_data(format);

	ast_free(attr);
}

static int h264_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct h264_attr *original = ast_format_get_attribute_data(src);
	struct h264_attr *attr = ast_calloc(1, sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
	}

	ast_format_set_attribute_data(dst, attr);

	return 0;
}

static enum ast_format_cmp_res h264_cmp(const struct ast_format *format1, const struct ast_format *format2)
{
	struct h264_attr *attr1 = ast_format_get_attribute_data(format1);
	struct h264_attr *attr2 = ast_format_get_attribute_data(format2);

	if (!attr1 || !attr1->PROFILE_IDC || !attr2 || !attr2->PROFILE_IDC ||
		(attr1->PROFILE_IDC == attr2->PROFILE_IDC)) {
		return AST_FORMAT_CMP_EQUAL;
	}

	return AST_FORMAT_CMP_NOT_EQUAL;
}

#define DETERMINE_JOINT(joint, attr1, attr2, field) (joint->field = (attr1 && attr1->field) ? attr1->field : (attr2 && attr2->field) ? attr2->field : 0)

static struct ast_format *h264_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct ast_format *cloned;
	struct h264_attr *attr, *attr1, *attr2;

	cloned = ast_format_clone(format1);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	attr1 = ast_format_get_attribute_data(format1);
	attr2 = ast_format_get_attribute_data(format2);

	DETERMINE_JOINT(attr, attr1, attr2, PROFILE_IDC);
	DETERMINE_JOINT(attr, attr1, attr2, PROFILE_IOP);
	DETERMINE_JOINT(attr, attr1, attr2, LEVEL);
	DETERMINE_JOINT(attr, attr1, attr2, MAX_MBPS);
	DETERMINE_JOINT(attr, attr1, attr2, MAX_FS);
	DETERMINE_JOINT(attr, attr1, attr2, MAX_CPB);
	DETERMINE_JOINT(attr, attr1, attr2, MAX_DPB);
	DETERMINE_JOINT(attr, attr1, attr2, MAX_BR);
	DETERMINE_JOINT(attr, attr1, attr2, MAX_SMBPS);
	DETERMINE_JOINT(attr, attr1, attr2, MAX_FPS);
	DETERMINE_JOINT(attr, attr1, attr2, REDUNDANT_PIC_CAP);
	DETERMINE_JOINT(attr, attr1, attr2, PARAMETER_ADD);
	DETERMINE_JOINT(attr, attr1, attr2, SPROP_INTERLEAVING_DEPTH);
	DETERMINE_JOINT(attr, attr1, attr2, SPROP_DEINT_BUF_REQ);
	DETERMINE_JOINT(attr, attr1, attr2, DEINT_BUF_CAP);
	DETERMINE_JOINT(attr, attr1, attr2, SPROP_INIT_BUF_TIME);
	DETERMINE_JOINT(attr, attr1, attr2, SPROP_MAX_DON_DIFF);
	DETERMINE_JOINT(attr, attr1, attr2, MAX_RCMD_NALU_SIZE);
	DETERMINE_JOINT(attr, attr1, attr2, LEVEL_ASYMMETRY_ALLOWED);
	DETERMINE_JOINT(attr, attr1, attr2, PACKETIZATION_MODE);

	if (attr1 && !ast_strlen_zero(attr1->SPS)) {
		ast_copy_string(attr->SPS, attr1->SPS, sizeof(attr->SPS));
	} else if (attr2 && !ast_strlen_zero(attr2->SPS)) {
		ast_copy_string(attr->SPS, attr2->SPS, sizeof(attr->SPS));
	}

	if (attr1 && !ast_strlen_zero(attr1->PPS)) {
		ast_copy_string(attr->PPS, attr1->PPS, sizeof(attr->PPS));
	} else if (attr2 && !ast_strlen_zero(attr2->PPS)) {
		ast_copy_string(attr->PPS, attr2->PPS, sizeof(attr->PPS));
	}

	return cloned;
}

static struct ast_format *h264_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	char *attribs = ast_strdupa(attributes), *attrib;
	struct ast_format *cloned;
	struct h264_attr *attr;

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	attr->REDUNDANT_PIC_CAP = H264_ATTR_KEY_UNSET;
	attr->PARAMETER_ADD = H264_ATTR_KEY_UNSET;
	attr->PACKETIZATION_MODE = H264_ATTR_KEY_UNSET;
	attr->LEVEL_ASYMMETRY_ALLOWED = H264_ATTR_KEY_UNSET;

	while ((attrib = strsep(&attribs, ";"))) {
		unsigned int val;
		unsigned long int val2;

		attrib = ast_strip(attrib);

		if (sscanf(attrib, "profile-level-id=%lx", &val2) == 1) {
			attr->PROFILE_IDC = ((val2 >> 16) & 0xFF);
			attr->PROFILE_IOP = ((val2 >> 8) & 0xFF);
			attr->LEVEL = (val2 & 0xFF);
		} else if (sscanf(attrib, "sprop-parameter-sets=%" H264_MAX_SPS_PPS_SIZE_SCAN_LIMIT "[^','],%" H264_MAX_SPS_PPS_SIZE_SCAN_LIMIT "s", attr->SPS, attr->PPS) == 2) {
			/* XXX sprop-parameter-sets can actually be of unlimited length. This may need to be addressed later. */
		} else if (sscanf(attrib, "max-mbps=%30u", &val) == 1) {
			attr->MAX_MBPS = val;
		} else if (sscanf(attrib, "max-fs=%30u", &val) == 1) {
			attr->MAX_FS = val;
		} else if (sscanf(attrib, "max-cpb=%30u", &val) == 1) {
			attr->MAX_CPB = val;
		} else if (sscanf(attrib, "max-dpb=%30u", &val) == 1) {
			attr->MAX_DPB = val;
		} else if (sscanf(attrib, "max-br=%30u", &val) == 1) {
			attr->MAX_BR = val;
		} else if (sscanf(attrib, "max-smbps=%30u", &val) == 1) {
			attr->MAX_SMBPS = val;
		} else if (sscanf(attrib, "max-fps=%30u", &val) == 1) {
			attr->MAX_FPS = val;
		} else if (sscanf(attrib, "redundant-pic-cap=%30u", &val) == 1) {
			attr->REDUNDANT_PIC_CAP = val;
		} else if (sscanf(attrib, "parameter-add=%30u", &val) == 1) {
			attr->PARAMETER_ADD = val;
		} else if (sscanf(attrib, "packetization-mode=%30u", &val) == 1) {
			attr->PACKETIZATION_MODE = val;
		} else if (sscanf(attrib, "sprop-interleaving-depth=%30u", &val) == 1) {
			attr->SPROP_INTERLEAVING_DEPTH = val;
		} else if (sscanf(attrib, "sprop-deint-buf-req=%30u", &val) == 1) {
			attr->SPROP_DEINT_BUF_REQ = val;
		} else if (sscanf(attrib, "deint-buf-cap=%30u", &val) == 1) {
			attr->DEINT_BUF_CAP = val;
		} else if (sscanf(attrib, "sprop-init-buf-time=%30u", &val) == 1) {
			attr->SPROP_INIT_BUF_TIME = val;
		} else if (sscanf(attrib, "sprop-max-don-diff=%30u", &val) == 1) {
			attr->SPROP_MAX_DON_DIFF = val;
		} else if (sscanf(attrib, "max-rcmd-nalu-size=%30u", &val) == 1) {
			attr->MAX_RCMD_NALU_SIZE = val;
		} else if (sscanf(attrib, "level-asymmetry-allowed=%30u", &val) == 1) {
			attr->LEVEL_ASYMMETRY_ALLOWED = val;
		}
	}

	return cloned;
}

#define APPEND_IF_NOT_H264_UNSET(field, str, name) do {		\
	if (field != H264_ATTR_KEY_UNSET) {	\
		if (added) {	\
			ast_str_append(str, 0, ";");	\
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {	\
			added = 1;	\
		}	\
		ast_str_append(str, 0, "%s=%u", name, field);	\
	}	\
} while (0)

#define APPEND_IF_NONZERO(field, str, name) do {		\
	if (field) {	\
		if (added) {	\
			ast_str_append(str, 0, ";");	\
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {	\
			added = 1;	\
		}	\
		ast_str_append(str, 0, "%s=%u", name, field);	\
	}	\
} while (0)

static void h264_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct h264_attr *attr = ast_format_get_attribute_data(format);
	int added = 0;

	if (!attr) {
		return;
	}

	APPEND_IF_NONZERO(attr->MAX_MBPS, str, "max-mbps");
	APPEND_IF_NONZERO(attr->MAX_FS, str, "max-fs");
	APPEND_IF_NONZERO(attr->MAX_CPB, str, "max-cpb");
	APPEND_IF_NONZERO(attr->MAX_DPB, str, "max-dpb");
	APPEND_IF_NONZERO(attr->MAX_BR, str, "max-br");
	APPEND_IF_NONZERO(attr->MAX_SMBPS, str, "max-smbps");
	APPEND_IF_NONZERO(attr->MAX_FPS, str, "max-fps");
	APPEND_IF_NONZERO(attr->SPROP_INTERLEAVING_DEPTH, str, "sprop-interleaving-depth");
	APPEND_IF_NONZERO(attr->SPROP_DEINT_BUF_REQ, str, "sprop-deint-buf-req");
	APPEND_IF_NONZERO(attr->DEINT_BUF_CAP, str, "deint-buf-cap");
	APPEND_IF_NONZERO(attr->SPROP_INIT_BUF_TIME, str, "sprop-init-buf-time");
	APPEND_IF_NONZERO(attr->SPROP_MAX_DON_DIFF, str, "sprop-max-don-diff");
	APPEND_IF_NONZERO(attr->MAX_RCMD_NALU_SIZE, str, "max-rcmd-nalu-size");

	APPEND_IF_NOT_H264_UNSET(attr->REDUNDANT_PIC_CAP, str, "redundant-pic-cap");
	APPEND_IF_NOT_H264_UNSET(attr->PARAMETER_ADD, str, "parameter-add");
	APPEND_IF_NOT_H264_UNSET(attr->PACKETIZATION_MODE, str, "packetization-mode");
	APPEND_IF_NOT_H264_UNSET(attr->LEVEL_ASYMMETRY_ALLOWED, str, "level-asymmetry-allowed");

	if (attr->PROFILE_IDC && attr->LEVEL) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "profile-level-id=%02X%02X%02X", attr->PROFILE_IDC, attr->PROFILE_IOP, attr->LEVEL);
	}

	if (!ast_strlen_zero(attr->SPS) && !ast_strlen_zero(attr->PPS)) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "sprop-parameter-sets=%s,%s", attr->SPS, attr->PPS);
	}

	if (added) {
		ast_str_append(str, 0, "\r\n");
	}

	return;
}

static struct ast_format_interface h264_interface = {
	.format_destroy = h264_destroy,
	.format_clone = h264_clone,
	.format_cmp = h264_cmp,
	.format_get_joint = h264_getjoint,
	.format_parse_sdp_fmtp = h264_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = h264_generate_sdp_fmtp,
};

static int unload_module(void)
{
	return 0;
}

static int load_module(void)
{
	if (ast_format_interface_register("h264", &h264_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "H.264 Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
);
