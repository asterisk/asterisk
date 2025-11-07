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

static struct ast_sip_security_mechanism *security_mechanisms_alloc(size_t n_params)
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

static struct ast_sip_security_mechanism *security_mechanisms_copy(
	const struct ast_sip_security_mechanism *src)
{
	struct ast_sip_security_mechanism *dst = NULL;
	int i, n_params;
	char *param;

	n_params = AST_VECTOR_SIZE(&src->mechanism_parameters);

	dst = security_mechanisms_alloc(n_params);
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

static void security_mechanism_destroy(struct ast_sip_security_mechanism *mech)
{
	AST_VECTOR_RESET(&mech->mechanism_parameters, ast_free);
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
		AST_VECTOR_APPEND(dst, security_mechanisms_copy(mech));
	}
};

void ast_sip_security_mechanisms_vector_destroy(struct ast_sip_security_mechanism_vector *security_mechanisms)
{
	if (!security_mechanisms) {
		return;
	}

	AST_VECTOR_RESET(security_mechanisms, security_mechanism_destroy);
	AST_VECTOR_FREE(security_mechanisms);
}

static char *mechanism_str[] = {
	[AST_SIP_SECURITY_MECH_NONE] = "none",
	[AST_SIP_SECURITY_MECH_MSRP_TLS] = "msrp-tls",
	[AST_SIP_SECURITY_MECH_SDES_SRTP] = "sdes-srtp",
	[AST_SIP_SECURITY_MECH_DTLS_SRTP] = "dtls-srtp",
};


static int str_to_security_mechanism_type(const char *security_mechanism) {
	int i = 0;

	for (i = 0; i < ARRAY_LEN(mechanism_str); i++) {
		if (!strcasecmp(security_mechanism, mechanism_str[i])) {
			return i;
		}
	}

	return -1;
}

static int security_mechanism_to_str(const struct ast_sip_security_mechanism *security_mechanism, int add_qvalue, char **buf)
{
	size_t size;
	int i;
	int rc = 0;
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);

	if (str == NULL) {
		return ENOMEM;
	}

	if (security_mechanism == NULL) {
		return EINVAL;
	}

	rc = ast_str_set(&str, 0, "%s", mechanism_str[security_mechanism->type]);
	if (rc <= 0) {
		return ENOMEM;
	}
	if (add_qvalue) {
		rc = ast_str_append(&str, 0, ";q=%f.4", security_mechanism->qvalue);
		if (rc <= 0) {
			return ENOMEM;
		}
	}

	size = AST_VECTOR_SIZE(&security_mechanism->mechanism_parameters);
	for (i = 0; i < size; ++i) {
		rc = ast_str_append(&str, 0, ";%s", AST_VECTOR_GET(&security_mechanism->mechanism_parameters, i));
		if (rc <= 0) {
			return ENOMEM;
		}
	}

	*buf = ast_strdup(ast_str_buffer(str));
	return 0;
}

int ast_sip_security_mechanisms_to_str(const struct ast_sip_security_mechanism_vector *security_mechanisms, int add_qvalue, char **buf)
{
	size_t vec_size;
	struct ast_sip_security_mechanism *mech;
	char *tmp_buf;
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);
	size_t i;
	int rc = 0;

	if (str == NULL) {
		return ENOMEM;
	}

	if (!security_mechanisms) {
		return -1;
	}

	vec_size = AST_VECTOR_SIZE(security_mechanisms);
	if (vec_size == 0) {
		return -1;
	}

	for (i = 0; i < vec_size; ++i) {
		mech = AST_VECTOR_GET(security_mechanisms, i);
		rc = security_mechanism_to_str(mech, add_qvalue, &tmp_buf);
		if (rc) {
			return rc;
		}
		rc = ast_str_append(&str, 0, "%s, ", tmp_buf);
		ast_free(tmp_buf);
		if (rc <= 0) {
			return ENOMEM;
		}
	}

	/* ast_str_truncate removes the trailing ", " on the last mechanism */
	*buf = ast_strdup(ast_str_truncate(str, -2));

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

int ast_sip_str_to_security_mechanism(struct ast_sip_security_mechanism **security_mechanism, const char *value) {
	struct ast_sip_security_mechanism *mech;
	char *param;
	char *tmp;
	char *mechanism = ast_strdupa(value);
	int err = 0;
	int type = -1;

	mech = security_mechanisms_alloc(1);
	if (!mech) {
		err = ENOMEM;
		goto out;
	}

	tmp = ast_strsep(&mechanism, ';', AST_STRSEP_ALL);
	type = str_to_security_mechanism_type(tmp);
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
		if (!strncmp(param, "q=", 2)) {
			mech->qvalue = ast_sip_parse_qvalue(&param[2]);
			if (mech->qvalue < 0.0) {
				err = EINVAL;
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
		security_mechanism_destroy(mech);
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
	static const pj_str_t proxy_require = { "Proxy-Require", 13 };
	static const pj_str_t require = { "Require", 7 };

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
		if (security_mechanism_to_str(mech, add_qvalue, &buf)) {
			continue;
		}
		ast_sip_add_header(tdata, header_name, buf);
		ast_free(buf);
	}

	if (pjsip_msg_find_hdr_by_name(tdata->msg, &require, NULL) == NULL) {
		ast_sip_add_header(tdata, "Require", "mediasec");
	}
	if (pjsip_msg_find_hdr_by_name(tdata->msg, &proxy_require, NULL) == NULL) {
		ast_sip_add_header(tdata, "Proxy-Require", "mediasec");
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
