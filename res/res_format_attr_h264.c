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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

enum h264_attr_keys {
	H264_ATTR_KEY_PROFILE_IDC,
	H264_ATTR_KEY_PROFILE_IOP,
	H264_ATTR_KEY_LEVEL,
	H264_ATTR_KEY_MAX_MBPS,
	H264_ATTR_KEY_MAX_FS,
	H264_ATTR_KEY_MAX_CPB,
	H264_ATTR_KEY_MAX_DPB,
	H264_ATTR_KEY_MAX_BR,
	H264_ATTR_KEY_MAX_SMBPS,
	H264_ATTR_KEY_MAX_FPS,
	H264_ATTR_KEY_REDUNDANT_PIC_CAP,
	H264_ATTR_KEY_PARAMETER_ADD,
	H264_ATTR_KEY_PACKETIZATION_MODE,
	H264_ATTR_KEY_SPROP_INTERLEAVING_DEPTH,
	H264_ATTR_KEY_SPROP_DEINT_BUF_REQ,
	H264_ATTR_KEY_DEINT_BUF_CAP,
	H264_ATTR_KEY_SPROP_INIT_BUF_TIME,
	H264_ATTR_KEY_SPROP_MAX_DON_DIFF,
	H264_ATTR_KEY_MAX_RCMD_NALU_SIZE,
	H264_ATTR_KEY_LEVEL_ASYMMETRY_ALLOWED,
	H264_ATTR_KEY_SPS_LEN,
	H264_ATTR_KEY_PPS_LEN,
	H264_ATTR_KEY_SPS,
	H264_ATTR_KEY_PPS = H264_ATTR_KEY_SPS + H264_MAX_SPS_PPS_SIZE,
	H264_ATTR_KEY_END = H264_ATTR_KEY_PPS + H264_MAX_SPS_PPS_SIZE,
};

static enum ast_format_cmp_res h264_format_attr_cmp(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2)
{
	if (!fattr1->format_attr[H264_ATTR_KEY_PROFILE_IDC] || !fattr2->format_attr[H264_ATTR_KEY_PROFILE_IDC] ||
	    (fattr1->format_attr[H264_ATTR_KEY_PROFILE_IDC] == fattr2->format_attr[H264_ATTR_KEY_PROFILE_IDC])) {
		return AST_FORMAT_CMP_EQUAL;
	}

	return AST_FORMAT_CMP_NOT_EQUAL;
}

static int h264_format_attr_get_joint(const struct ast_format_attr *fattr1, const struct ast_format_attr *fattr2, struct ast_format_attr *result)
{
	int i;

	for (i = H264_ATTR_KEY_PROFILE_IDC; i < H264_ATTR_KEY_END; i++) {
		result->format_attr[i] = fattr1->format_attr[i] ? fattr1->format_attr[i] : fattr2->format_attr[i];
	}

	return 0;
}

static int h264_format_attr_sdp_parse(struct ast_format_attr *format_attr, const char *attributes)
{
	char *attribs = ast_strdupa(attributes), *attrib;

	format_attr->format_attr[H264_ATTR_KEY_REDUNDANT_PIC_CAP] = H264_ATTR_KEY_UNSET;
	format_attr->format_attr[H264_ATTR_KEY_PARAMETER_ADD] = H264_ATTR_KEY_UNSET;
	format_attr->format_attr[H264_ATTR_KEY_PACKETIZATION_MODE] = H264_ATTR_KEY_UNSET;
	format_attr->format_attr[H264_ATTR_KEY_LEVEL_ASYMMETRY_ALLOWED] = H264_ATTR_KEY_UNSET;

	while ((attrib = strsep(&attribs, ";"))) {
		unsigned int val;
		unsigned long int val2;
		char sps[H264_MAX_SPS_PPS_SIZE], pps[H264_MAX_SPS_PPS_SIZE];

		if (sscanf(attrib, "profile-level-id=%lx", &val2) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_PROFILE_IDC] = ((val2 >> 16) & 0xFF);
			format_attr->format_attr[H264_ATTR_KEY_PROFILE_IOP] = ((val2 >> 8) & 0xFF);
			format_attr->format_attr[H264_ATTR_KEY_LEVEL] = (val2 & 0xFF);
		} else if (sscanf(attrib, "sprop-parameter-sets=%" H264_MAX_SPS_PPS_SIZE_SCAN_LIMIT "[^','],%" H264_MAX_SPS_PPS_SIZE_SCAN_LIMIT "s", sps, pps) == 2) {
			/* XXX sprop-parameter-sets can actually be of unlimited length. This may need to be addressed later. */
			unsigned char spsdecoded[H264_MAX_SPS_PPS_SIZE] = { 0, }, ppsdecoded[H264_MAX_SPS_PPS_SIZE] = { 0, };
			int i;

			ast_base64decode(spsdecoded, sps, sizeof(spsdecoded));
			ast_base64decode(ppsdecoded, pps, sizeof(ppsdecoded));

			format_attr->format_attr[H264_ATTR_KEY_SPS_LEN] = 0;
			format_attr->format_attr[H264_ATTR_KEY_PPS_LEN] = 0;

			for (i = 0; i < H264_MAX_SPS_PPS_SIZE; i++) {
				if (spsdecoded[i]) {
					format_attr->format_attr[H264_ATTR_KEY_SPS + i] = spsdecoded[i];
					format_attr->format_attr[H264_ATTR_KEY_SPS_LEN]++;
				}
				if (ppsdecoded[i]) {
					format_attr->format_attr[H264_ATTR_KEY_PPS + i] = ppsdecoded[i];
					format_attr->format_attr[H264_ATTR_KEY_PPS_LEN]++;
				}
			}
		} else if (sscanf(attrib, "max-mbps=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_MAX_MBPS] = val;
		} else if (sscanf(attrib, "max-fs=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_MAX_FS] = val;
		} else if (sscanf(attrib, "max-cpb=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_MAX_CPB] = val;
		} else if (sscanf(attrib, "max-dpb=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_MAX_DPB] = val;
		} else if (sscanf(attrib, "max-br=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_MAX_BR] = val;
		} else if (sscanf(attrib, "max-smbps=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_MAX_SMBPS] = val;
		} else if (sscanf(attrib, "max-fps=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_MAX_FPS] = val;
		} else if (sscanf(attrib, "redundant-pic-cap=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_REDUNDANT_PIC_CAP] = val;
		} else if (sscanf(attrib, "parameter-add=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_PARAMETER_ADD] = val;
		} else if (sscanf(attrib, "packetization-mode=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_PACKETIZATION_MODE] = val;
		} else if (sscanf(attrib, "sprop-interleaving-depth=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_SPROP_INTERLEAVING_DEPTH] = val;
		} else if (sscanf(attrib, "sprop-deint-buf-req=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_SPROP_DEINT_BUF_REQ] = val;
		} else if (sscanf(attrib, "deint-buf-cap=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_DEINT_BUF_CAP] = val;
		} else if (sscanf(attrib, "sprop-init-buf-time=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_SPROP_INIT_BUF_TIME] = val;
		} else if (sscanf(attrib, "sprop-max-don-diff=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_SPROP_MAX_DON_DIFF] = val;
		} else if (sscanf(attrib, "max-rcmd-nalu-size=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_MAX_RCMD_NALU_SIZE] = val;
		} else if (sscanf(attrib, "level-asymmetry-allowed=%30u", &val) == 1) {
			format_attr->format_attr[H264_ATTR_KEY_LEVEL_ASYMMETRY_ALLOWED] = val;
		}
	}

	return 0;
}

/*! \brief Helper function which converts a key enum into a string value for SDP */
static const char *h264_attr_key_to_str(enum h264_attr_keys key)
{
	switch (key) {
	case H264_ATTR_KEY_MAX_MBPS:
		return "max-mbps";
	case H264_ATTR_KEY_MAX_FS:
		return "max-fs";
	case H264_ATTR_KEY_MAX_CPB:
		return "max-cpb";
	case H264_ATTR_KEY_MAX_DPB:
		return "max-dpb";
	case H264_ATTR_KEY_MAX_BR:
		return "max-br";
	case H264_ATTR_KEY_MAX_SMBPS:
		return "max-smbps";
	case H264_ATTR_KEY_MAX_FPS:
		return "max-fps";
	case H264_ATTR_KEY_REDUNDANT_PIC_CAP:
		return "redundant-pic-cap";
	case H264_ATTR_KEY_PARAMETER_ADD:
		return "parameter-add";
	case H264_ATTR_KEY_PACKETIZATION_MODE:
		return "packetization-mode";
	case H264_ATTR_KEY_SPROP_INTERLEAVING_DEPTH:
		return "sprop-interleaving-depth";
	case H264_ATTR_KEY_SPROP_DEINT_BUF_REQ:
		return "sprop-deint-buf-req";
	case H264_ATTR_KEY_DEINT_BUF_CAP:
		return "deint-buf-cap";
	case H264_ATTR_KEY_SPROP_INIT_BUF_TIME:
		return "sprop-init-buf-time";
	case H264_ATTR_KEY_SPROP_MAX_DON_DIFF:
		return "sprop-max-don-diff";
	case H264_ATTR_KEY_MAX_RCMD_NALU_SIZE:
		return "max-rcmd-nalu-size";
	case H264_ATTR_KEY_LEVEL_ASYMMETRY_ALLOWED:
		return "level-asymmetry-allowed";
	default:
		return NULL;
	}

	return NULL;
}

/*! \brief Helper function which determines if the value of an attribute can be placed into the SDP */
static int h264_attr_key_addable(const struct ast_format_attr *format_attr, enum h264_attr_keys key)
{
	switch (key) {
	case H264_ATTR_KEY_REDUNDANT_PIC_CAP:
	case H264_ATTR_KEY_PARAMETER_ADD:
	case H264_ATTR_KEY_PACKETIZATION_MODE:
	case H264_ATTR_KEY_LEVEL_ASYMMETRY_ALLOWED:
		return (format_attr->format_attr[key] != H264_ATTR_KEY_UNSET) ? 1 : 0;
	default:
		return format_attr->format_attr[key] ? 1 : 0;
	}

	return 1;
}

static void h264_format_attr_sdp_generate(const struct ast_format_attr *format_attr, unsigned int payload, struct ast_str **str)
{
	int i, added = 0;

        for (i = H264_ATTR_KEY_PROFILE_IDC; i < H264_ATTR_KEY_END; i++) {
                const char *name;

		if (i == H264_ATTR_KEY_SPS && format_attr->format_attr[H264_ATTR_KEY_SPS] && format_attr->format_attr[H264_ATTR_KEY_PPS]) {
			unsigned char spsdecoded[H264_MAX_SPS_PPS_SIZE] = { 0, }, ppsdecoded[H264_MAX_SPS_PPS_SIZE] = { 0, };
			int pos;
			char sps[H264_MAX_SPS_PPS_SIZE], pps[H264_MAX_SPS_PPS_SIZE];

			for (pos = 0; pos < H264_MAX_SPS_PPS_SIZE; pos++) {
				spsdecoded[pos] = format_attr->format_attr[H264_ATTR_KEY_SPS + pos];
				ppsdecoded[pos] = format_attr->format_attr[H264_ATTR_KEY_PPS + pos];
			}

			ast_base64encode(sps, spsdecoded, format_attr->format_attr[H264_ATTR_KEY_SPS_LEN], H264_MAX_SPS_PPS_SIZE);
			ast_base64encode(pps, ppsdecoded, format_attr->format_attr[H264_ATTR_KEY_PPS_LEN], H264_MAX_SPS_PPS_SIZE);

			if (!added) {
				ast_str_append(str, 0, "a=fmtp:%d sprop-parameter-sets=%s,%s", payload, sps, pps);
				added = 1;
			} else {
				ast_str_append(str, 0, ";sprop-parameter-sets=%s,%s", sps, pps);
			}
		} else if (i == H264_ATTR_KEY_PROFILE_IDC && format_attr->format_attr[H264_ATTR_KEY_PROFILE_IDC] &&
		    format_attr->format_attr[H264_ATTR_KEY_PROFILE_IOP] && format_attr->format_attr[H264_ATTR_KEY_LEVEL]) {
			if (!added) {
				ast_str_append(str, 0, "a=fmtp:%d profile-level-id=%X%X%X", payload, format_attr->format_attr[H264_ATTR_KEY_PROFILE_IDC],
					       format_attr->format_attr[H264_ATTR_KEY_PROFILE_IOP], format_attr->format_attr[H264_ATTR_KEY_LEVEL]);
				added = 1;
			} else {
				ast_str_append(str, 0, ";profile-level-id=%X%X%X", format_attr->format_attr[H264_ATTR_KEY_PROFILE_IDC],
					       format_attr->format_attr[H264_ATTR_KEY_PROFILE_IOP], format_attr->format_attr[H264_ATTR_KEY_LEVEL]);
			}
		} else if ((name = h264_attr_key_to_str(i)) && h264_attr_key_addable(format_attr, i)) {
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

static struct ast_format_attr_interface h264_format_attr_interface = {
	.id = AST_FORMAT_H264,
	.format_attr_cmp = h264_format_attr_cmp,
	.format_attr_get_joint = h264_format_attr_get_joint,
	.format_attr_sdp_parse = h264_format_attr_sdp_parse,
	.format_attr_sdp_generate = h264_format_attr_sdp_generate,
};

static int unload_module(void)
{
	ast_format_attr_unreg_interface(&h264_format_attr_interface);

	return 0;
}

static int load_module(void)
{
	if (ast_format_attr_reg_interface(&h264_format_attr_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "H.264 Format Attribute Module",
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEFAULT,
);
