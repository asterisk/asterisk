/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Commend International
 *
 * Maximilian Fridrich <m.fridrich@commend.com>
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
 *
 * \brief Interact with security agreement negotiations and mechanisms
 *
 * \author Maximilian Fridrich <m.fridrich@commend.com>
 */

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"

static struct ast_sip_security_mechanism *ast_sip_security_mechanisms_alloc(size_t n_params)
{
	struct ast_sip_security_mechanism *mech;

	mech = ast_calloc(1, sizeof(struct ast_sip_security_mechanism));
	if (mech == NULL) {
		return NULL;
	}
	mech->qvalue = 0.0;
	if (AST_VECTOR_INIT(&mech->mechanism_parameters, n_params) != 0) {
		ast_free(mech);
		return NULL;
	}

	return mech;
}

static struct ast_sip_security_mechanism *ast_sip_security_mechanisms_copy(
	const struct ast_sip_security_mechanism *src)
{
	struct ast_sip_security_mechanism *dst = NULL;
	int i, n_params;
	char *param;

	n_params = AST_VECTOR_SIZE(&src->mechanism_parameters);

	dst = ast_sip_security_mechanisms_alloc(n_params);
	if (dst == NULL) {
		return NULL;
	}
	dst->type = src->type;
	dst->qvalue = src->qvalue;

	for (i = 0; i < n_params; i++) {
		param = ast_strdup(AST_VECTOR_GET(&src->mechanism_parameters, i));
		AST_VECTOR_APPEND(&dst->mechanism_parameters, param);
	}

	return dst;
}

static void ast_sip_security_mechanisms_destroy(struct ast_sip_security_mechanism *mech)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&mech->mechanism_parameters); i++) {
		ast_free(AST_VECTOR_GET(&mech->mechanism_parameters, i));
	}
	AST_VECTOR_FREE(&mech->mechanism_parameters);
	ast_free(mech);
}

void ast_sip_security_mechanisms_vector_copy(struct ast_sip_security_mechanism_vector *dst,
	const struct ast_sip_security_mechanism_vector *src)
{
	struct ast_sip_security_mechanism *mech;
	int i;

	ast_sip_security_mechanisms_vector_destroy(dst);
	for (i = 0; i < AST_VECTOR_SIZE(src); i++) {
		mech = AST_VECTOR_GET(src, i);
		AST_VECTOR_APPEND(dst, ast_sip_security_mechanisms_copy(mech));
	}
};

void ast_sip_security_mechanisms_vector_destroy(struct ast_sip_security_mechanism_vector *security_mechanisms)
{
	struct ast_sip_security_mechanism *mech;
	int i;

	if (!security_mechanisms) {
		return;
	}

	for (i = 0; i < AST_VECTOR_SIZE(security_mechanisms); i++) {
		mech = AST_VECTOR_GET(security_mechanisms, i);
		ast_sip_security_mechanisms_destroy(mech);
	}
	AST_VECTOR_FREE(security_mechanisms);
}

static int ast_sip_str_to_security_mechanism_type(const char *security_mechanism) {
	int result = -1;

	if (!strcasecmp(security_mechanism, "msrp-tls")) {
		result = AST_SIP_SECURITY_MECH_MSRP_TLS;
	} else if (!strcasecmp(security_mechanism, "sdes-srtp")) {
		result = AST_SIP_SECURITY_MECH_SDES_SRTP;
	} else if (!strcasecmp(security_mechanism, "dtls-srtp")) {
		result = AST_SIP_SECURITY_MECH_DTLS_SRTP;
	}

	return result;
}

static char *ast_sip_security_mechanism_type_to_str(enum ast_sip_security_mechanism_type mech_type) {
	if (mech_type == AST_SIP_SECURITY_MECH_MSRP_TLS) {
		return "msrp-tls";
	} else if (mech_type == AST_SIP_SECURITY_MECH_SDES_SRTP) {
		return "sdes-srtp";
	} else if (mech_type == AST_SIP_SECURITY_MECH_DTLS_SRTP) {
		return "dtls-srtp";
	} else {
		return NULL;
	}
}

static int ast_sip_security_mechanism_to_str(const struct ast_sip_security_mechanism *security_mechanism, int add_qvalue, char **buf) {
	char tmp[64];
	size_t size;
	size_t buf_size = 128;
	int i;
	char *ret = ast_calloc(buf_size, sizeof(char));

	if (ret == NULL) {
		return ENOMEM;
	}
	if (security_mechanism == NULL) {
		ast_free(ret);
		return EINVAL;
	}

	strncat(ret, ast_sip_security_mechanism_type_to_str(security_mechanism->type), buf_size - strlen(ret) - 1);
	if (add_qvalue) {
		snprintf(tmp, sizeof(tmp), ";q=%f.4", security_mechanism->qvalue);
		strncat(ret, tmp, buf_size - strlen(ret) - 1);
	}

	size = AST_VECTOR_SIZE(&security_mechanism->mechanism_parameters);
	for (i = 0; i < size; ++i) {
		snprintf(tmp, sizeof(tmp), ";%s", AST_VECTOR_GET(&security_mechanism->mechanism_parameters, i));
		strncat(ret, tmp, buf_size - strlen(ret) - 1);
	}

	*buf = ret;
	return 0;
}

void ast_sip_remove_headers_by_name_and_value(pjsip_msg *msg, const pj_str_t *hdr_name, const char* value)
{
	struct pjsip_generic_string_hdr *hdr = pjsip_msg_find_hdr_by_name(msg, hdr_name, NULL);
	for (; hdr; hdr = pjsip_msg_find_hdr_by_name(msg, hdr_name, hdr->next)) {
		if (value == NULL || !pj_strcmp2(&hdr->hvalue, value)) {
			pj_list_erase(hdr);
		}
		if (hdr->next == hdr) {
			break;
		}
	}
}

/*!
 * \internal
 * \brief Parses a string representing a q_value to a float.
 *
 * Valid q values must be in the range from 0.0 to 1.0 inclusively.
 * 
 * \param q_value
 * \retval The parsed qvalue or -1.0 on failure.
 */
static float parse_qvalue(const char *q_value) {
	char *end;
	float ret = strtof(q_value, &end);

	if (end == q_value) {
		/* Not a number. */
		return -1.0;
	} else if ('\0' != *end) {
		/* Extra character at end of input. */
		return -1.0;
	} else if (ret > 1.0 || ret < 0.0) {
		/* Out of valid range. */
		return -1.0;
	}
	return ret;
}

int ast_sip_str_to_security_mechanism(struct ast_sip_security_mechanism **security_mechanism, const char *value) {
	struct ast_sip_security_mechanism *mech;
	char *param;
	char *tmp;
	char *mechanism = ast_strdupa(value);
	int err = 0;
	int type = -1;

	mech = ast_sip_security_mechanisms_alloc(1);
	if (!mech) {
		err = ENOMEM;
		goto out;
	}

	tmp = ast_strsep(&mechanism, ';', AST_STRSEP_ALL);
	type = ast_sip_str_to_security_mechanism_type(tmp);
	if (type == -1) {
		err = EINVAL;
		goto out;
	}

	mech->type = type;
	while ((param = ast_strsep(&mechanism, ';', AST_STRSEP_ALL))) {
		if (!param) {
			err = EINVAL;
			goto out;
		}
		if (!strncmp(param, "q=0", 4) || !strncmp(param, "q=1", 4)) {
			mech->qvalue = parse_qvalue(&param[2]);
			if (mech->qvalue < 0.0) {
				goto out;
			}
			continue;
		}
		param = ast_strdup(param);
		AST_VECTOR_APPEND(&mech->mechanism_parameters, param);
	}

	*security_mechanism = mech;

out:
	if (err && (mech != NULL)) {
		ast_sip_security_mechanisms_destroy(mech);
	}
	return err;
}

int ast_sip_add_security_headers(struct ast_sip_security_mechanism_vector *security_mechanisms,
		const char *header_name, int add_qval, pjsip_tx_data *tdata) {
	struct ast_sip_security_mechanism *mech;
	char *buf;
	int mech_cnt;
	int i;
	int add_qvalue = 1;

	if (!security_mechanisms || !tdata) {
		return EINVAL;
	}

	if (!strcmp(header_name, "Security-Client")) {
		add_qvalue = 0;
	} else if (strcmp(header_name, "Security-Server") &&
			strcmp(header_name, "Security-Verify")) {
		return EINVAL;
	}
	/* If we're adding Security-Client headers, don't add q-value
	 * even if the function caller requested it. */
	add_qvalue = add_qvalue && add_qval;

	mech_cnt = AST_VECTOR_SIZE(security_mechanisms);
	for (i = 0; i < mech_cnt; ++i) {
		mech = AST_VECTOR_GET(security_mechanisms, i);
		if (ast_sip_security_mechanism_to_str(mech, add_qvalue, &buf)) {
			continue;
		}
		ast_sip_add_header(tdata, header_name, buf);
		ast_free(buf);
	}
	return 0;
}

void ast_sip_header_to_security_mechanism(const pjsip_generic_string_hdr *hdr,
		struct ast_sip_security_mechanism_vector *security_mechanisms) {

	struct ast_sip_security_mechanism *mech;
	char buf[512];
	char *hdr_val;
	char *mechanism;

	if (!security_mechanisms || !hdr) {
		return;
	}

	if (pj_stricmp2(&hdr->name, "Security-Client") && pj_stricmp2(&hdr->name, "Security-Server") &&
			pj_stricmp2(&hdr->name, "Security-Verify")) {
		return;
	}

	ast_copy_pj_str(buf, &hdr->hvalue, sizeof(buf));
	hdr_val = ast_skip_blanks(buf);

	while ((mechanism = ast_strsep(&hdr_val, ',', AST_STRSEP_ALL))) {
		if (!ast_sip_str_to_security_mechanism(&mech, mechanism)) {
			AST_VECTOR_APPEND(security_mechanisms, mech);
		}
	}
}

int ast_sip_security_mechanism_vector_init(struct ast_sip_security_mechanism_vector *security_mechanisms, const char *value)
{
	char *val = value ? ast_strdupa(value) : NULL;
	struct ast_sip_security_mechanism *mech;
	char *mechanism;

	ast_sip_security_mechanisms_vector_destroy(security_mechanisms);
	if (AST_VECTOR_INIT(security_mechanisms, 1)) {
		return -1;
	}

	if (!val) {
		return 0;
	}

	while ((mechanism = ast_strsep(&val, ',', AST_STRSEP_ALL))) {
		if (!ast_sip_str_to_security_mechanism(&mech, mechanism)) {
			AST_VECTOR_APPEND(security_mechanisms, mech);
		}
	}

	return 0;
}
