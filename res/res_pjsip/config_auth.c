/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>
#include "asterisk/res_pjsip.h"
#include "asterisk/logger.h"
#include "asterisk/sorcery.h"
#include "asterisk/cli.h"
#include "asterisk/vector.h"
#include "include/res_pjsip_private.h"
#include "asterisk/res_pjsip_cli.h"

#ifndef HAVE_PJSIP_AUTH_NEW_DIGESTS
/*
 * These are needed if the version of pjproject in use
 * does not have the new digests.
 * NOTE: We don't support AKA but we need to specify
 * it to be compatible with the pjproject definition.
 */
#ifdef HAVE_OPENSSL
#include "openssl/md5.h"
#include "openssl/sha.h"
#else
#define MD5_DIGEST_LENGTH     16
#define SHA256_DIGEST_LENGTH  32
#endif

const pjsip_auth_algorithm pjsip_auth_algorithms[] = {
/*    TYPE                             IANA name            OpenSSL name */
/*      Raw digest byte length  Hex representation length                */
    { PJSIP_AUTH_ALGORITHM_NOT_SET,    {"", 0},             "",
        0,                      0},
    { PJSIP_AUTH_ALGORITHM_MD5,        {"MD5", 3},          "MD5",
        MD5_DIGEST_LENGTH,      MD5_DIGEST_LENGTH * 2},
    { PJSIP_AUTH_ALGORITHM_SHA256,     {"SHA-256", 7},      "SHA256",
        SHA256_DIGEST_LENGTH,   SHA256_DIGEST_LENGTH * 2},
    { PJSIP_AUTH_ALGORITHM_SHA512_256, {"SHA-512-256", 11}, "SHA512-256",
        SHA256_DIGEST_LENGTH,   SHA256_DIGEST_LENGTH * 2},
    { PJSIP_AUTH_ALGORITHM_AKAV1_MD5,  {"AKAv1-MD5", 9},    "",
        MD5_DIGEST_LENGTH,      MD5_DIGEST_LENGTH * 2},
    { PJSIP_AUTH_ALGORITHM_AKAV1_MD5,  {"AKAv2-MD5", 9},    "",
        MD5_DIGEST_LENGTH,      MD5_DIGEST_LENGTH * 2},
    { PJSIP_AUTH_ALGORITHM_COUNT,      {"", 0},             "",
        0,                      0},
};
#endif

const pjsip_auth_algorithm *ast_sip_auth_get_algorithm_by_type(
	pjsip_auth_algorithm_type algorithm_type)
{
#ifdef HAVE_PJSIP_AUTH_NEW_DIGESTS
	return pjsip_auth_get_algorithm_by_type(algorithm_type);
#else
	/*
	 * If we don't have a pjproject with the new algorithms, the
	 * only one we support is MD5.
	 */
	if (algorithm_type == PJSIP_AUTH_ALGORITHM_MD5) {
		return &pjsip_auth_algorithms[algorithm_type];
	}
	return NULL;
#endif
}

const pjsip_auth_algorithm *ast_sip_auth_get_algorithm_by_iana_name(
	const pj_str_t *iana_name)
{
#ifdef HAVE_PJSIP_AUTH_NEW_DIGESTS
	return pjsip_auth_get_algorithm_by_iana_name(iana_name);
#else
	if (!iana_name) {
		return NULL;
	}
	/*
	 * If we don't have a pjproject with the new algorithms, the
	 * only one we support is MD5.  If iana_name is empty (but not NULL),
	 * the default is MD5.
	 */
	if (iana_name->slen == 0 || pj_stricmp2(iana_name, "MD5") == 0) {
		return &pjsip_auth_algorithms[PJSIP_AUTH_ALGORITHM_MD5];
	}
	return NULL;
#endif
}

pj_bool_t ast_sip_auth_is_algorithm_supported(
	pjsip_auth_algorithm_type algorithm_type)
{
#ifdef HAVE_PJSIP_AUTH_NEW_DIGESTS
	return pjsip_auth_is_algorithm_supported(algorithm_type);
#else
	return algorithm_type == PJSIP_AUTH_ALGORITHM_MD5;
#endif
}

static void auth_destroy(void *obj)
{
	struct ast_sip_auth *auth = obj;
	int i = 0;

	ast_string_field_free_memory(auth);

	for (i = PJSIP_AUTH_ALGORITHM_NOT_SET + 1; i < PJSIP_AUTH_ALGORITHM_COUNT; i++) {
		ast_free(auth->password_digests[i]);
	}

	AST_VECTOR_FREE(&auth->supported_algorithms_uac);
	AST_VECTOR_FREE(&auth->supported_algorithms_uas);
}

static void *auth_alloc(const char *name)
{
	struct ast_sip_auth *auth = ast_sorcery_generic_alloc(sizeof(*auth), auth_destroy);

	if (!auth) {
		return NULL;
	}

	if (ast_string_field_init(auth, 64)) {
		ao2_cleanup(auth);
		return NULL;
	}

	AST_VECTOR_INIT(&auth->supported_algorithms_uac, 0);
	AST_VECTOR_INIT(&auth->supported_algorithms_uas, 0);

	return auth;
}

static int auth_type_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_auth *auth = obj;
	if (!strcasecmp(var->value, "userpass")) {
		auth->type = AST_SIP_AUTH_TYPE_USER_PASS;
	} else if (!strcasecmp(var->value, "md5")) {
		auth->type = AST_SIP_AUTH_TYPE_MD5;
	} else if (!strcasecmp(var->value, "digest")) {
		auth->type = AST_SIP_AUTH_TYPE_DIGEST;
	} else if (!strcasecmp(var->value, "google_oauth")) {
#ifdef HAVE_PJSIP_OAUTH_AUTHENTICATION
		auth->type = AST_SIP_AUTH_TYPE_GOOGLE_OAUTH;
#else
		ast_log(LOG_WARNING, "OAuth support is not available in the version of PJSIP in use\n");
		return -1;
#endif
	} else {
		ast_log(LOG_WARNING, "Unknown authentication storage type '%s' specified for %s\n",
				var->value, var->name);
		return -1;
	}
	return 0;
}

static const char *auth_types_map[] = {
	[AST_SIP_AUTH_TYPE_USER_PASS] = "userpass",
	[AST_SIP_AUTH_TYPE_MD5] = "md5",
	[AST_SIP_AUTH_TYPE_DIGEST] = "digest",
	[AST_SIP_AUTH_TYPE_GOOGLE_OAUTH] = "google_oauth"
};

const char *ast_sip_auth_type_to_str(enum ast_sip_auth_type type)
{
	return ARRAY_IN_BOUNDS(type, auth_types_map) ?
		auth_types_map[type] : "";
}

static int auth_type_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_auth *auth = obj;
	*buf = ast_strdup(ast_sip_auth_type_to_str(auth->type));
	return 0;
}

int ast_sip_auth_digest_algorithms_vector_init(const char *id,
	struct pjsip_auth_algorithm_type_vector *algorithms, const char *agent_type,
	const char *value)
{
	char *iana_names = ast_strdupa(value);
	pj_str_t val;
	int res = 0;

	ast_assert(algorithms != NULL);

	while ((val.ptr = ast_strip(strsep(&iana_names, ",")))) {
		const pjsip_auth_algorithm *algo;

		if (ast_strlen_zero(val.ptr)) {
			continue;
		}
		val.slen = strlen(val.ptr);

		algo = ast_sip_auth_get_algorithm_by_iana_name(&val);
		if (!algo) {
			ast_log(LOG_WARNING, "%s: Unknown %s digest algorithm '%s' specified\n",
				id, agent_type, val.ptr);
			res = -1;
			continue;
		}
		if (!ast_sip_auth_is_algorithm_supported(algo->algorithm_type)) {
			ast_log(LOG_WARNING, "%s: %s digest algorithm '%s' is not supported by the version of OpenSSL in use\n",
				id, agent_type, val.ptr);
			res = -1;
			continue;
		}

		if (AST_VECTOR_APPEND(algorithms, algo->algorithm_type)) {
			AST_VECTOR_FREE(algorithms);
			return -1;
		}
	}
	return res;
}

static int uac_algorithms_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_auth *auth = obj;

	return ast_sip_auth_digest_algorithms_vector_init(ast_sorcery_object_get_id(auth),
		&auth->supported_algorithms_uac, "UAC", var->value);
}

static int uas_algorithms_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_auth *auth = obj;

	return ast_sip_auth_digest_algorithms_vector_init(ast_sorcery_object_get_id(auth),
		&auth->supported_algorithms_uas, "UAS", var->value);
}

int ast_sip_auth_digest_algorithms_vector_to_str(
	const struct pjsip_auth_algorithm_type_vector *algorithms, char **buf)
{
	struct ast_str *str = NULL;
	int i = 0;

	if (!algorithms || !AST_VECTOR_SIZE(algorithms)) {
		return 0;
	}

	str = ast_str_alloca(256);
	if (!str) {
		return -1;
	}

	for (i = 0; i < AST_VECTOR_SIZE(algorithms); ++i) {
		const pjsip_auth_algorithm *algo = ast_sip_auth_get_algorithm_by_type(
			AST_VECTOR_GET(algorithms, i));
		ast_str_append(&str, 0, "%s" PJSTR_PRINTF_SPEC, i > 0 ? "," : "",
			PJSTR_PRINTF_VAR(algo->iana_name));
	}

	*buf = ast_strdup(ast_str_buffer(str));

	return *buf ? 0 : -1;
}

static int uac_algorithms_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_auth *auth = obj;
	return ast_sip_auth_digest_algorithms_vector_to_str(&auth->supported_algorithms_uac, buf);
}

static int uas_algorithms_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_auth *auth = obj;
	return ast_sip_auth_digest_algorithms_vector_to_str(&auth->supported_algorithms_uas, buf);
}

static int password_digest_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_auth *auth = obj;
	const char *auth_name = ast_sorcery_object_get_id(auth);
	char *value = ast_strdupa(var->value);
	char *unparsed_digest = NULL;

	while ((unparsed_digest = ast_strsep(&value, ',', AST_STRSEP_TRIM))) {
		const pjsip_auth_algorithm *algo;
		char *iana_name;
		char *digest;
		struct ast_sip_auth_password_digest *pw;
		pj_str_t pj_iana_name;

		if (ast_strlen_zero(unparsed_digest)) {
			continue;
		}

		if (strchr(unparsed_digest, ':') != NULL) {
			iana_name = ast_strsep(&unparsed_digest, ':', AST_STRSEP_TRIM);
		} else {
			/*
			 * md5_cred doesn't have the algorithm name in front
			 * so we need to force it.
			 */
			iana_name = "MD5";
		}
		digest = unparsed_digest;

		pj_iana_name = pj_str(iana_name);

		algo = ast_sip_auth_get_algorithm_by_iana_name(&pj_iana_name);
		if (!algo) {
			ast_log(LOG_WARNING, "%s: Unknown password_digest algorithm '%s' specified\n",
				auth_name, iana_name);
			return -1;
		}
		if (!ast_sip_auth_is_algorithm_supported(algo->algorithm_type)) {
			ast_log(LOG_WARNING, "%s: password_digest algorithm '%s' is not supported by the version of OpenSSL in use\n",
				auth_name, iana_name);
			return -1;
		}
		if (strlen(digest) != algo->digest_str_length) {
			ast_log(LOG_WARNING, "%s: password_digest algorithm '%s' length (%d) must be %d\n",
				auth_name, iana_name, (int)strlen(digest), (int)algo->digest_str_length);
			return -1;
		}

		pw = ast_calloc(1, sizeof(*pw) + strlen(digest) + 1);
		if (!pw) {
			return -1;
		}
		pw->algorithm_type = algo->algorithm_type;
		strcpy(pw->digest, digest); /* Safe */
		auth->password_digests[pw->algorithm_type] = pw;
	}

	return 0;
}

static int password_digest_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_auth *auth = obj;
	struct ast_str *str = ast_str_alloca(256);
	int i = 0;
	int count = 0;

	for (i = PJSIP_AUTH_ALGORITHM_NOT_SET + 1; i < PJSIP_AUTH_ALGORITHM_COUNT; i++) {
		struct ast_sip_auth_password_digest *pw =
			auth->password_digests[i];
		const pjsip_auth_algorithm *algorithm;

		if (!pw) {
			continue;
		}

		algorithm = ast_sip_auth_get_algorithm_by_type(pw->algorithm_type);

		ast_str_append(&str, 0, "%s" PJSTR_PRINTF_SPEC ":%s", count > 0 ? "," : "",
			PJSTR_PRINTF_VAR(algorithm->iana_name), pw->digest);
		count++;
	}

	*buf = ast_strdup(ast_str_buffer(str));

	return 0;
}

static int md5cred_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_auth *auth = obj;

	if (auth->password_digests[PJSIP_AUTH_ALGORITHM_MD5]) {
		*buf = ast_strdup(auth->password_digests[PJSIP_AUTH_ALGORITHM_MD5]->digest);
	}

	return 0;
}

int ast_sip_auth_is_algorithm_available(const struct ast_sip_auth *auth,
	const struct pjsip_auth_algorithm_type_vector *algorithms,
	pjsip_auth_algorithm_type algorithm_type)
{
	int i;

	if (!algorithms) {
		return 0;
	}

	for (i = 0; i < AST_VECTOR_SIZE(algorithms); ++i) {
		if (AST_VECTOR_GET(algorithms, i) == algorithm_type) {
			if (auth->password_digests[algorithm_type] || !ast_strlen_zero(auth->auth_pass)) {
				return 1;
			}
		}
	}

	return 0;
}

const char *ast_sip_auth_get_creds(const struct ast_sip_auth *auth,
	const pjsip_auth_algorithm_type algorithm_type, int *cred_type)
{
	struct ast_sip_auth_password_digest *pw_digest =
		auth->password_digests[algorithm_type];

	if (pw_digest) {
		*cred_type = PJSIP_CRED_DATA_DIGEST;
		return pw_digest->digest;
	}

	*cred_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
	return auth->auth_pass;
}

static int check_algorithm(const struct ast_sip_auth *auth,
	const pjsip_auth_algorithm_type algorithm_type, const char *which_supported)
{
	const pjsip_auth_algorithm *algo = ast_sip_auth_get_algorithm_by_type(algorithm_type);
	struct ast_sip_auth_password_digest *pw_digest =
		auth->password_digests[algorithm_type];

	if (!pw_digest && ast_strlen_zero(auth->auth_pass)) {
		ast_log(LOG_ERROR, "%s: No plain text or digest password found for algorithm "
			PJSTR_PRINTF_SPEC " in supported_algorithms_%s\n",
			ast_sorcery_object_get_id(auth), PJSTR_PRINTF_VAR(algo->iana_name), which_supported);
		return -1;
	}

	return 0;
}

static int auth_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_sip_auth *auth = obj;
	const char *id = ast_sorcery_object_get_id(auth);
	int i = 0;
	int res = 0;

	if (ast_strlen_zero(auth->auth_user)) {
		ast_log(LOG_ERROR, "%s: No authentication username\n", id);
		return -1;
	}

	if (auth->type == AST_SIP_AUTH_TYPE_GOOGLE_OAUTH) {
		if (ast_strlen_zero(auth->refresh_token)
			|| ast_strlen_zero(auth->oauth_clientid)
			|| ast_strlen_zero(auth->oauth_secret)) {
			ast_log(LOG_ERROR, "%s: 'google_oauth' authentication specified but refresh_token,"
				" oauth_clientid, or oauth_secret not specified\n", id);
			res = -1;
		}
		return res;
	}

	if (AST_VECTOR_SIZE(&auth->supported_algorithms_uas) == 0) {
		char *default_algo_uas = ast_alloca(AST_SIP_AUTH_MAX_SUPPORTED_ALGORITHMS_LENGTH + 1);
		ast_sip_get_default_auth_algorithms_uas(default_algo_uas, AST_SIP_AUTH_MAX_SUPPORTED_ALGORITHMS_LENGTH);
		ast_sip_auth_digest_algorithms_vector_init(id, &auth->supported_algorithms_uas, "UAS", default_algo_uas);
	}
	if (AST_VECTOR_SIZE(&auth->supported_algorithms_uac) == 0) {
		char *default_algo_uac = ast_alloca(AST_SIP_AUTH_MAX_SUPPORTED_ALGORITHMS_LENGTH + 1);
		ast_sip_get_default_auth_algorithms_uac(default_algo_uac, AST_SIP_AUTH_MAX_SUPPORTED_ALGORITHMS_LENGTH);
		ast_sip_auth_digest_algorithms_vector_init(id, &auth->supported_algorithms_uac, "UAC", default_algo_uac);
	}

	for (i = 0; i < AST_VECTOR_SIZE(&auth->supported_algorithms_uas); i++) {
		res += check_algorithm(auth, AST_VECTOR_GET(&auth->supported_algorithms_uas, i), "uas");
	}

	for (i = 0; i < AST_VECTOR_SIZE(&auth->supported_algorithms_uac); i++) {
		res += check_algorithm(auth, AST_VECTOR_GET(&auth->supported_algorithms_uac, i), "uac");
	}

	return res;
}

int ast_sip_for_each_auth(const struct ast_sip_auth_vector *vector,
			  ao2_callback_fn on_auth, void *arg)
{
	int i;

	if (!vector || !AST_VECTOR_SIZE(vector)) {
		return 0;
	}

	for (i = 0; i < AST_VECTOR_SIZE(vector); ++i) {
		/* AST_VECTOR_GET is safe to use since the vector is immutable */
		RAII_VAR(struct ast_sip_auth *, auth, ast_sorcery_retrieve_by_id(
				 ast_sip_get_sorcery(), SIP_SORCERY_AUTH_TYPE,
				 AST_VECTOR_GET(vector,i)), ao2_cleanup);

		if (!auth) {
			continue;
		}

		if (on_auth(auth, arg, 0)) {
			return -1;
		}
	}

	return 0;
}

static int sip_auth_to_ami(const struct ast_sip_auth *auth,
			   struct ast_str **buf)
{
	return ast_sip_sorcery_object_to_ami(auth, buf);
}

static int format_ami_auth_handler(void *obj, void *arg, int flags)
{
	const struct ast_sip_auth *auth = obj;
	struct ast_sip_ami *ami = arg;
	const struct ast_sip_endpoint *endpoint = ami->arg;
	RAII_VAR(struct ast_str *, buf,
		 ast_sip_create_ami_event("AuthDetail", ami), ast_free);

	if (!buf) {
		return -1;
	}

	if (sip_auth_to_ami(auth, &buf)) {
		return -1;
	}

	if (endpoint) {
		ast_str_append(&buf, 0, "EndpointName: %s\r\n",
		       ast_sorcery_object_get_id(endpoint));
	}

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ami->count++;

	return 0;
}

int ast_sip_format_auths_ami(const struct ast_sip_auth_vector *auths,
			     struct ast_sip_ami *ami)
{
	return ast_sip_for_each_auth(auths, format_ami_auth_handler, ami);
}

static int format_ami_endpoint_auth(const struct ast_sip_endpoint *endpoint,
				    struct ast_sip_ami *ami)
{
	ami->arg = (void *)endpoint;
	if (ast_sip_format_auths_ami(&endpoint->inbound_auths, ami)) {
		return -1;
	}

	return ast_sip_format_auths_ami(&endpoint->outbound_auths, ami);
}

static struct ast_sip_endpoint_formatter endpoint_auth_formatter = {
	.format_ami = format_ami_endpoint_auth
};

static struct ao2_container *cli_get_auths(void)
{
	struct ao2_container *auths;

	auths = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "auth",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	return auths;
}

static int format_ami_authlist_handler(void *obj, void *arg, int flags)
{
	struct ast_sip_auth *auth = obj;
	struct ast_sip_ami *ami = arg;
	struct ast_str *buf;

	buf = ast_sip_create_ami_event("AuthList", ami);
	if (!buf) {
		return CMP_STOP;
	}

	sip_auth_to_ami(auth, &buf);

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ami->count++;

	ast_free(buf);

	return 0;
}

static int ami_show_auths(struct mansession *s, const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"), };
	struct ao2_container *auths;

	auths = cli_get_auths();
	if (!auths) {
		astman_send_error(s, m, "Could not get Auths\n");
		return 0;
	}

	if (!ao2_container_count(auths)) {
		astman_send_error(s, m, "No Auths found\n");
		ao2_ref(auths, -1);
		return 0;
	}

	astman_send_listack(s, m, "A listing of Auths follows, presented as AuthList events",
			"start");

	ao2_callback(auths, OBJ_NODATA, format_ami_authlist_handler, &ami);

	astman_send_list_complete_start(s, m, "AuthListComplete", ami.count);
	astman_send_list_complete_end(s);

	ao2_ref(auths, -1);

	return 0;
}

static struct ao2_container *cli_get_container(const char *regex)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	struct ao2_container *s_container;

	container = ast_sorcery_retrieve_by_regex(ast_sip_get_sorcery(), "auth", regex);
	if (!container) {
		return NULL;
	}

	s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, ast_sorcery_object_id_compare);
	if (!s_container) {
		return NULL;
	}

	if (ao2_container_dup(s_container, container, 0)) {
		ao2_ref(s_container, -1);
		return NULL;
	}

	return s_container;
}

static int cli_iterator(void *container, ao2_callback_fn callback, void *args)
{
	return ast_sip_for_each_auth(container, callback, args);
}

static void *cli_retrieve_by_id(const char *id)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), SIP_SORCERY_AUTH_TYPE, id);
}

static int cli_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_MAX_WIDTH - indent - 20;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <AuthId/UserName%*.*s>\n", indent, "I/OAuth", filler, filler,
		CLI_HEADER_FILLER);

	return 0;
}

static int cli_print_body(void *obj, void *arg, int flags)
{
	struct ast_sip_auth *auth = obj;
	struct ast_sip_cli_context *context = arg;
	char title[32];

	ast_assert(context->output_buffer != NULL);

	snprintf(title, sizeof(title), "%sAuth",
		context->auth_direction ? context->auth_direction : "");

	ast_str_append(&context->output_buffer, 0, "%*s:  %s/%s\n",
		CLI_INDENT_TO_SPACES(context->indent_level), title,
		ast_sorcery_object_get_id(auth), auth->auth_user);

	if (context->show_details
		|| (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(auth, context, 0);
	}

	return 0;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Auths",
		.command = "pjsip list auths",
		.usage = "Usage: pjsip list auths [ like <pattern> ]\n"
				"       List the configured PJSIP Auths\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Auths",
		.command = "pjsip show auths",
		.usage = "Usage: pjsip show auths [ like <pattern> ]\n"
				"       Show the configured PJSIP Auths\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Auth",
		.command = "pjsip show auth",
		.usage = "Usage: pjsip show auth <id>\n"
				 "       Show the configured PJSIP Auth\n"),
};

static struct ast_sip_cli_formatter_entry *cli_formatter;

static void global_loaded(const char *object_type)
{
	ast_sorcery_force_reload_object(ast_sip_get_sorcery(), "auth");
}

/*! \brief Observer which is used to update our interval and default_realm when the global setting changes */
static struct ast_sorcery_observer global_observer = {
	.loaded = global_loaded,
};

/*! \brief Initialize sorcery with auth support */
int ast_sip_initialize_sorcery_auth(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();

	ast_sorcery_apply_default(sorcery, SIP_SORCERY_AUTH_TYPE, "config", "pjsip.conf,criteria=type=auth");

	if (ast_sorcery_object_register(sorcery, SIP_SORCERY_AUTH_TYPE, auth_alloc, NULL, auth_apply)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "type", "",
			OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "username",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, auth_user));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "password",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, auth_pass));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "refresh_token",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, refresh_token));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "oauth_clientid",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, oauth_clientid));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "oauth_secret",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, oauth_secret));
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_AUTH_TYPE, "md5_cred",
			NULL, password_digest_handler, md5cred_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "realm",
			"", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_auth, realm));
	ast_sorcery_object_field_register(sorcery, SIP_SORCERY_AUTH_TYPE, "nonce_lifetime",
			"32", OPT_UINT_T, 0, FLDSET(struct ast_sip_auth, nonce_lifetime));
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_AUTH_TYPE, "auth_type",
			"userpass", auth_type_handler, auth_type_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_AUTH_TYPE, "password_digest",
		NULL, password_digest_handler, password_digest_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_AUTH_TYPE, "supported_algorithms_uac",
		"", uac_algorithms_handler, uac_algorithms_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, SIP_SORCERY_AUTH_TYPE, "supported_algorithms_uas",
		"", uas_algorithms_handler, uas_algorithms_to_str, NULL, 0, 0);

	ast_sip_register_endpoint_formatter(&endpoint_auth_formatter);

	cli_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!cli_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for cli formatter\n");
		return -1;
	}
	cli_formatter->name = SIP_SORCERY_AUTH_TYPE;
	cli_formatter->print_header = cli_print_header;
	cli_formatter->print_body = cli_print_body;
	cli_formatter->get_container = cli_get_container;
	cli_formatter->iterate = cli_iterator;
	cli_formatter->get_id = ast_sorcery_object_get_id;
	cli_formatter->retrieve_by_id = cli_retrieve_by_id;

	ast_sip_register_cli_formatter(cli_formatter);
	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	if (ast_manager_register_xml("PJSIPShowAuths", EVENT_FLAG_SYSTEM, ami_show_auths)) {
		return -1;
	}

	ast_sorcery_observer_add(sorcery, "global", &global_observer);
	return 0;
}

int ast_sip_destroy_sorcery_auth(void)
{
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "global", &global_observer);

	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sip_unregister_cli_formatter(cli_formatter);
	ast_sip_unregister_endpoint_formatter(&endpoint_auth_formatter);

	ast_manager_unregister("PJSIPShowAuths");

	return 0;
}
