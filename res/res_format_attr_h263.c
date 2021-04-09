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

#include <ctype.h>                      /* for toupper */

#include "asterisk/module.h"
#include "asterisk/format.h"
#include "asterisk/strings.h"           /* for ast_str_append */
#include "asterisk/utils.h"             /* for ast_strip */

/*! \brief Value that indicates an attribute is actually unset */
#define H263_ATTR_KEY_UNSET UINT8_MAX

struct h263_attr {
	unsigned int SQCIF;       /*!< Minimum picture interval for SQCIF resolution */
	unsigned int QCIF;        /*!< Minimum picture interval for QCIF resolution */
	unsigned int CIF;         /*!< Minimum picture interval for CIF resolution */
	unsigned int CIF4;        /*!< Minimum picture interval for CIF4 resolution */
	unsigned int CIF16;       /*!< Minimum picture interval for CIF16 resolution */
	unsigned int VGA;         /*!< Minimum picture interval for VGA resolution */
	unsigned int CUSTOM_XMAX; /*!< Custom resolution (Xmax) */
	unsigned int CUSTOM_YMAX; /*!< Custom resolution (Ymax) */
	unsigned int CUSTOM_MPI;  /*!< Custom resolution (MPI) */
	unsigned int CPCF;        /*!< Custom Picture Clock Frequency */
	unsigned int CPCF_2;
	unsigned int CPCF_3;
	unsigned int CPCF_4;
	unsigned int CPCF_5;
	unsigned int CPCF_6;
	unsigned int CPCF_7;
	unsigned int CPCF_MPI;
	unsigned int F;           /*!< F annex support */
	unsigned int I;           /*!< I annex support */
	unsigned int J;           /*!< J annex support */
	unsigned int T;           /*!< T annex support */
	unsigned int K;           /*!< K annex support */
	unsigned int N;           /*!< N annex support */
	unsigned int P_SUB1;      /*!< Reference picture resampling (sub mode 1) */
	unsigned int P_SUB2;      /*!< Reference picture resampling (sub mode 2) */
	unsigned int P_SUB3;      /*!< Reference picture resampling (sub mode 3) */
	unsigned int P_SUB4;      /*!< Reference picture resampling (sub mode 4) */
	unsigned int PAR_WIDTH;   /*!< Pixel aspect ratio (width) */
	unsigned int PAR_HEIGHT;  /*!< Pixel aspect ratio (height) */
	unsigned int BPP;         /*!< Bits per picture maximum */
	unsigned int HRD;         /*!< Hypothetical reference decoder status */
	unsigned int MaxBR;       /*!< Vendor Specific: CounterPath Bria (Solo) */
};

static void h263_destroy(struct ast_format *format)
{
	struct h263_attr *attr = ast_format_get_attribute_data(format);

	ast_free(attr);
}

static int h263_clone(const struct ast_format *src, struct ast_format *dst)
{
	struct h263_attr *original = ast_format_get_attribute_data(src);
	struct h263_attr *attr = ast_calloc(1, sizeof(*attr));

	if (!attr) {
		return -1;
	}

	if (original) {
		*attr = *original;
	}

	ast_format_set_attribute_data(dst, attr);

	return 0;
}

static enum ast_format_cmp_res h263_cmp(const struct ast_format *format1, const struct ast_format *format2)
{
	struct h263_attr *attr1 = ast_format_get_attribute_data(format1);
	struct h263_attr *attr2 = ast_format_get_attribute_data(format2);

	if (!attr1 || !attr2 || (attr1 && attr2 && !memcmp(attr1, attr2, sizeof(*attr1)))) {
		return AST_FORMAT_CMP_EQUAL;
	}
	return AST_FORMAT_CMP_NOT_EQUAL;
}

#define DETERMINE_JOINT(joint, attr1, attr2, field) (joint->field = (attr1 && attr1->field) ? attr1->field : (attr2 && attr2->field) ? attr2->field : 0)

static struct ast_format *h263_getjoint(const struct ast_format *format1, const struct ast_format *format2)
{
	struct ast_format *cloned;
	struct h263_attr *attr, *attr1, *attr2;

	cloned = ast_format_clone(format1);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	attr1 = ast_format_get_attribute_data(format1);
	attr2 = ast_format_get_attribute_data(format2);

	DETERMINE_JOINT(attr, attr1, attr2, SQCIF);
	DETERMINE_JOINT(attr, attr1, attr2, QCIF);
	DETERMINE_JOINT(attr, attr1, attr2, CIF);
	DETERMINE_JOINT(attr, attr1, attr2, CIF4);
	DETERMINE_JOINT(attr, attr1, attr2, CIF16);
	DETERMINE_JOINT(attr, attr1, attr2, VGA);
	DETERMINE_JOINT(attr, attr1, attr2, CUSTOM_XMAX);
	DETERMINE_JOINT(attr, attr1, attr2, CUSTOM_YMAX);
	DETERMINE_JOINT(attr, attr1, attr2, CUSTOM_MPI);
	DETERMINE_JOINT(attr, attr1, attr2, CPCF);
	DETERMINE_JOINT(attr, attr1, attr2, CPCF_2);
	DETERMINE_JOINT(attr, attr1, attr2, CPCF_3);
	DETERMINE_JOINT(attr, attr1, attr2, CPCF_4);
	DETERMINE_JOINT(attr, attr1, attr2, CPCF_5);
	DETERMINE_JOINT(attr, attr1, attr2, CPCF_6);
	DETERMINE_JOINT(attr, attr1, attr2, CPCF_7);
	DETERMINE_JOINT(attr, attr1, attr2, CPCF_MPI);
	DETERMINE_JOINT(attr, attr1, attr2, F);
	DETERMINE_JOINT(attr, attr1, attr2, I);
	DETERMINE_JOINT(attr, attr1, attr2, J);
	DETERMINE_JOINT(attr, attr1, attr2, T);
	DETERMINE_JOINT(attr, attr1, attr2, K);
	DETERMINE_JOINT(attr, attr1, attr2, N);
	DETERMINE_JOINT(attr, attr1, attr2, P_SUB1);
	DETERMINE_JOINT(attr, attr1, attr2, P_SUB2);
	DETERMINE_JOINT(attr, attr1, attr2, P_SUB3);
	DETERMINE_JOINT(attr, attr1, attr2, P_SUB4);
	DETERMINE_JOINT(attr, attr1, attr2, PAR_WIDTH);
	DETERMINE_JOINT(attr, attr1, attr2, PAR_HEIGHT);
	DETERMINE_JOINT(attr, attr1, attr2, BPP);
	DETERMINE_JOINT(attr, attr1, attr2, HRD);
	DETERMINE_JOINT(attr, attr1, attr2, MaxBR);

	return cloned;
}

static struct ast_format *h263_parse_sdp_fmtp(const struct ast_format *format, const char *attributes)
{
	char *attribs = ast_strdupa(attributes), *attrib;
	struct ast_format *cloned;
	struct h263_attr *attr;

	cloned = ast_format_clone(format);
	if (!cloned) {
		return NULL;
	}
	attr = ast_format_get_attribute_data(cloned);

	/* upper-case everything, so we are case-insensitive */
	for (attrib = attribs; *attrib; ++attrib) {
		*attrib = toupper(*attrib);
	} /* based on channels/chan_sip.c:process_a_sdp_image() */

	attr->BPP = H263_ATTR_KEY_UNSET;
	attr->MaxBR = H263_ATTR_KEY_UNSET;
	attr->PAR_WIDTH = H263_ATTR_KEY_UNSET;
	attr->PAR_HEIGHT = H263_ATTR_KEY_UNSET;

	while ((attrib = strsep(&attribs, ";"))) {
		unsigned int val, val2 = 0, val3 = 0, val4 = 0, val5 = 0, val6 = 0, val7 = 0, val8 = 0;

		attrib = ast_strip(attrib);

		if (sscanf(attrib, "SQCIF=%30u", &val) == 1) {
			attr->SQCIF = val;
		} else if (strcmp(attrib, "SQCIF") == 0) {
			attr->SQCIF = 1;
		} else if (sscanf(attrib, "QCIF=%30u", &val) == 1) {
			attr->QCIF = val;
		} else if (strcmp(attrib, "QCIF") == 0) {
			attr->QCIF = 1;
		} else if (sscanf(attrib, "CIF=%30u", &val) == 1) {
			attr->CIF = val;
		} else if (strcmp(attrib, "CIF") == 0) {
			attr->CIF = 1;
		} else if (sscanf(attrib, "CIF4=%30u", &val) == 1) {
			attr->CIF4 = val;
		} else if (strcmp(attrib, "CIF4") == 0) {
			attr->CIF4 = 1;
		} else if (sscanf(attrib, "CIF16=%30u", &val) == 1) {
			attr->CIF16 = val;
		} else if (strcmp(attrib, "CIF16") == 0) {
			attr->CIF16 = 1;
		} else if (sscanf(attrib, "VGA=%30u", &val) == 1) {
			attr->VGA = val;
		} else if (strcmp(attrib, "VGA") == 0) {
			attr->VGA = 1;
		} else if (sscanf(attrib, "CUSTOM=%30u,%30u,%30u", &val, &val2, &val3) == 3) {
			attr->CUSTOM_XMAX = val;
			attr->CUSTOM_YMAX = val2;
			attr->CUSTOM_MPI = val3;
		} else if (sscanf(attrib, "CPCF=%30u,%30u,%30u,%30u,%30u,%30u,%30u,%30u",
				&val, &val2, &val3, &val4, &val5, &val6, &val7, &val8) == 8) {
			attr->CPCF = val;
			attr->CPCF_2 = val2;
			attr->CPCF_3 = val3;
			attr->CPCF_4 = val4;
			attr->CPCF_5 = val5;
			attr->CPCF_6 = val6;
			attr->CPCF_7 = val7;
			attr->CPCF_MPI = val8;
		} else if (sscanf(attrib, "F=%30u", &val) == 1) {
			attr->F = val;
		} else if (sscanf(attrib, "I=%30u", &val) == 1) {
			attr->I = val;
		} else if (sscanf(attrib, "J=%30u", &val) == 1) {
			attr->J = val;
		} else if (sscanf(attrib, "T=%30u", &val) == 1) {
			attr->T = val;
		} else if (sscanf(attrib, "K=%30u", &val) == 1) {
			attr->K = val;
		} else if (sscanf(attrib, "N=%30u", &val) == 1) {
			attr->N = val;
		} else if (sscanf(attrib, "PAR=%30u:%30u", &val, &val2) == 2) {
			attr->PAR_WIDTH = val;
			attr->PAR_HEIGHT = val2;
		} else if (sscanf(attrib, "BPP=%30u", &val) == 1) {
			attr->BPP = val;
		} else if (sscanf(attrib, "HRD=%30u", &val) == 1) {
			attr->HRD = val;
		} else if (sscanf(attrib, "P=%30u,%30u,%30u,%30u", &val, &val2, &val3, &val4) > 0) {
			attr->P_SUB1 = val;
			attr->P_SUB2 = val2;
			attr->P_SUB3 = val3;
			attr->P_SUB4 = val4;
		} else if (sscanf(attrib, "MAXBR=%30u", &val) == 1) {
			attr->MaxBR = val;
		}
	}

	return cloned;
}

#define APPEND_IF_NOT_H263_UNSET(field, str, name) do {		\
	if (field != H263_ATTR_KEY_UNSET) {	\
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

static void h263_generate_sdp_fmtp(const struct ast_format *format, unsigned int payload, struct ast_str **str)
{
	struct h263_attr *attr = ast_format_get_attribute_data(format);
	int added = 0;

	if (!attr) {
		return;
	}

	if (attr->CPCF) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "CPCF=%u,%u,%u,%u,%u,%u,%u,%u", attr->CPCF, attr->CPCF_2, attr->CPCF_3,
			attr->CPCF_4, attr->CPCF_5, attr->CPCF_6, attr->CPCF_7, attr->CPCF_MPI);
	}

	APPEND_IF_NONZERO(attr->CIF16, str, "CIF16");
	APPEND_IF_NONZERO(attr->CIF4,  str, "CIF4");
	APPEND_IF_NONZERO(attr->VGA,   str, "VGA");
	APPEND_IF_NONZERO(attr->CIF,   str, "CIF");
	APPEND_IF_NONZERO(attr->QCIF,  str, "QCIF");
	APPEND_IF_NONZERO(attr->SQCIF, str, "SQCIF");

	if (attr->CUSTOM_XMAX && attr->CUSTOM_YMAX && attr->CUSTOM_MPI) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "CUSTOM=%u,%u,%u", attr->CUSTOM_XMAX, attr->CUSTOM_YMAX, attr->CUSTOM_MPI);
	}

	APPEND_IF_NONZERO(attr->F, str, "F");
	APPEND_IF_NONZERO(attr->I, str, "I");
	APPEND_IF_NONZERO(attr->J, str, "J");
	APPEND_IF_NONZERO(attr->T, str, "T");
	APPEND_IF_NONZERO(attr->K, str, "K");
	APPEND_IF_NONZERO(attr->N, str, "N");

	if (attr->P_SUB1) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;
		}
		ast_str_append(str, 0, "P=%u", attr->P_SUB1);
		if (attr->P_SUB2) {
			ast_str_append(str, 0, ",%u", attr->P_SUB2);
		}
		if (attr->P_SUB3) {
			ast_str_append(str, 0, ",%u", attr->P_SUB3);
		}
		if (attr->P_SUB4) {
			ast_str_append(str, 0, ",%u", attr->P_SUB4);
		}
	}

	if (attr->PAR_WIDTH != H263_ATTR_KEY_UNSET && attr->PAR_HEIGHT != H263_ATTR_KEY_UNSET) {
		if (added) {
			ast_str_append(str, 0, ";");
		} else if (0 < ast_str_append(str, 0, "a=fmtp:%u ", payload)) {
			added = 1;	\
		}
		ast_str_append(str, 0, "PAR=%u:%u", attr->PAR_WIDTH, attr->PAR_HEIGHT);
	}

	APPEND_IF_NOT_H263_UNSET(attr->BPP, str, "BPP");

	APPEND_IF_NONZERO(attr->HRD, str, "HRD");

	APPEND_IF_NOT_H263_UNSET(attr->MaxBR, str, "MaxBR");

	ast_str_append(str, 0, "\r\n");

	return;
}

static struct ast_format_interface h263_interface = {
	.format_destroy = h263_destroy,
	.format_clone = h263_clone,
	.format_cmp = h263_cmp,
	.format_get_joint = h263_getjoint,
	.format_parse_sdp_fmtp = h263_parse_sdp_fmtp,
	.format_generate_sdp_fmtp = h263_generate_sdp_fmtp,
};

static int unload_module(void)
{
	return 0;
}

static int load_module(void)
{
	if (ast_format_interface_register("h263", &h263_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_format_interface_register("h263p", &h263_interface)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "H.263 Format Attribute Module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
);
