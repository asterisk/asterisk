/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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

/*** MODULEINFO
	<depend>curl</depend>
	<depend>res_curl</depend>
	<depend>libjwt</depend>
	<support_level>core</support_level>
 ***/

#define _TRACE_PREFIX_ "rss",__LINE__, ""

#include "asterisk.h"

#include "asterisk/app.h"
#include "asterisk/cli.h"
#include "asterisk/conversions.h"
#include "asterisk/module.h"
#include "asterisk/global_datastores.h"
#include "asterisk/pbx.h"

#include "res_stir_shaken/stir_shaken.h"

static int tn_auth_list_nid;

int ss_get_tn_auth_nid(void)
{
	return tn_auth_list_nid;
}

/* The datastore struct holding verification information for the channel */
struct ss_datastore {
	/* The identitifier for the STIR/SHAKEN verification */
	char *identity;
	/* The attestation value */
	char *attestation;
	/* The actual verification result */
	enum ast_stir_shaken_vs_response_code verify_result;
};

/*!
 * \brief Frees a stir_shaken_datastore structure
 *
 * \param datastore The datastore to free
 */
static void ss_datastore_free(struct ss_datastore *datastore)
{
	if (!datastore) {
		return;
	}

	ast_free(datastore->identity);
	ast_free(datastore->attestation);
	ast_free(datastore);
}

/*!
 * \brief The callback to destroy a stir_shaken_datastore
 *
 * \param data The stir_shaken_datastore
 */
static void ss_datastore_destroy_cb(void *data)
{
	struct ss_datastore *datastore = data;
	ss_datastore_free(datastore);
}

/* The stir_shaken_datastore info used to add and compare stir_shaken_datastores on the channel */
static const struct ast_datastore_info stir_shaken_datastore_info = {
	.type = "STIR/SHAKEN VERIFICATION",
	.destroy = ss_datastore_destroy_cb,
};

int ast_stir_shaken_add_result_to_channel(
	struct ast_stir_shaken_vs_ctx *ctx)
{
	struct ss_datastore *ss_datastore;
	struct ast_datastore *datastore;
	const char *chan_name;

	if (!ctx->chan) {
		ast_log(LOG_ERROR, "Channel is required to add STIR/SHAKEN verification\n");
		return -1;
	}

	chan_name = ast_channel_name(ctx->chan);

	if (!ctx->identity_hdr) {
		ast_log(LOG_ERROR, "No identity to add STIR/SHAKEN verification to channel "
			"%s\n", chan_name);
		return -1;
	}

	if (!ctx->attestation) {
		ast_log(LOG_ERROR, "Attestation cannot be NULL to add STIR/SHAKEN verification to "
			"channel %s\n", chan_name);
		return -1;
	}

	ss_datastore = ast_calloc(1, sizeof(*ss_datastore));
	if (!ss_datastore) {
		ast_log(LOG_ERROR, "Failed to allocate space for STIR/SHAKEN datastore for "
			"channel %s\n", chan_name);
		return -1;
	}

	ss_datastore->identity = ast_strdup(ctx->identity_hdr);
	if (!ss_datastore->identity) {
		ast_log(LOG_ERROR, "Failed to allocate space for STIR/SHAKEN datastore "
			"identity for channel %s\n", chan_name);
		ss_datastore_free(ss_datastore);
		return -1;
	}

	ss_datastore->attestation = ast_strdup(ctx->attestation);
	if (!ss_datastore->attestation) {
		ast_log(LOG_ERROR, "Failed to allocate space for STIR/SHAKEN datastore "
			"attestation for channel %s\n", chan_name);
		ss_datastore_free(ss_datastore);
		return -1;
	}

	ss_datastore->verify_result = ctx->failure_reason;

	datastore = ast_datastore_alloc(&stir_shaken_datastore_info, NULL);
	if (!datastore) {
		ast_log(LOG_ERROR, "Failed to allocate space for datastore for channel "
			"%s\n", chan_name);
		ss_datastore_free(ss_datastore);
		return -1;
	}

	datastore->data = ss_datastore;

	ast_channel_lock(ctx->chan);
	ast_channel_datastore_add(ctx->chan, datastore);
	ast_channel_unlock(ctx->chan);

	return 0;
}

/*!
 * \brief Retrieves STIR/SHAKEN verification information for the channel via dialplan.
 * Examples:
 *
 * STIR_SHAKEN(count)
 * STIR_SHAKEN(0, identity)
 * STIR_SHAKEN(1, attestation)
 * STIR_SHAKEN(27, verify_result)
 *
 * \retval -1 on failure
 * \retval 0 on success
 */
static int func_read(struct ast_channel *chan, const char *function,
	char *data, char *buf, size_t len)
{
	struct ss_datastore *ss_datastore;
	struct ast_datastore *datastore;
	char *parse;
	char *first;
	char *second;
	unsigned int target_index, current_index = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(first_param);
		AST_APP_ARG(second_param);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires at least one argument\n", function);
		return -1;
	}

	if (!chan) {
		ast_log(LOG_ERROR, "No channel for %s function\n", function);
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	first = ast_strip(args.first_param);
	if (ast_strlen_zero(first)) {
		ast_log(LOG_ERROR, "An argument must be passed to %s\n", function);
		return -1;
	}

	second = ast_strip(args.second_param);

	/* Check if we are only looking for the number of STIR/SHAKEN verification results */
	if (!strcasecmp(first, "count")) {
		size_t count = 0;

		if (!ast_strlen_zero(second)) {
			ast_log(LOG_ERROR, "%s only takes 1 paramater for 'count'\n", function);
			return -1;
		}

		ast_channel_lock(chan);
		AST_LIST_TRAVERSE(ast_channel_datastores(chan), datastore, entry) {
			if (datastore->info != &stir_shaken_datastore_info) {
				continue;
			}
			count++;
		}
		ast_channel_unlock(chan);

		snprintf(buf, len, "%zu", count);
		return 0;
	}

	/* If we aren't doing a count, then there should be two parameters. The field
	 * we are searching for will be the second parameter. The index is the first.
	 */
	if (ast_strlen_zero(second)) {
		ast_log(LOG_ERROR, "Retrieving a value using %s requires two paramaters (index, value) "
			"- only index was given\n", function);
		return -1;
	}

	if (ast_str_to_uint(first, &target_index)) {
		ast_log(LOG_ERROR, "Failed to convert index %s to integer for function %s\n",
			first, function);
		return -1;
	}

	/* We don't store by uid for the datastore, so just search for the specified index */
	ast_channel_lock(chan);
	AST_LIST_TRAVERSE(ast_channel_datastores(chan), datastore, entry) {
		if (datastore->info != &stir_shaken_datastore_info) {
			continue;
		}

		if (current_index == target_index) {
			break;
		}

		current_index++;
	}
	ast_channel_unlock(chan);
	if (current_index != target_index || !datastore) {
		ast_log(LOG_WARNING, "No STIR/SHAKEN results for index '%s'\n", first);
		return -1;
	}
	ss_datastore = datastore->data;

	if (!strcasecmp(second, "identity")) {
		ast_copy_string(buf, ss_datastore->identity, len);
	} else if (!strcasecmp(second, "attestation")) {
		ast_copy_string(buf, ss_datastore->attestation, len);
	} else if (!strcasecmp(second, "verify_result")) {
		ast_copy_string(buf, ast_stir_shaken_vs_response_code_to_str(ss_datastore->verify_result), len);
	} else {
		ast_log(LOG_ERROR, "No such value '%s' for %s\n", second, function);
		return -1;
	}

	return 0;
}

static struct ast_custom_function stir_shaken_function = {
	.name = "STIR_SHAKEN",
	.read = func_read,
};

int stir_shaken_cli_show(void *obj, void *arg, int flags)
{
	struct ast_cli_args *a = arg;
	struct ast_variable *options;
	struct ast_variable *i;

	if (!obj) {
		ast_cli(a->fd, "No stir/shaken configuration found\n");
		return 0;
	}

	options = ast_variable_list_sort(ast_sorcery_objectset_create2(
		ss_sorcery(), obj, AST_HANDLER_ONLY_STRING));
	if (!options) {
		return 0;
	}

	ast_cli(a->fd, "%s: %s\n", ast_sorcery_object_get_type(obj),
		ast_sorcery_object_get_id(obj));

	for (i = options; i; i = i->next) {
		ast_cli(a->fd, "\t%s: %s\n", i->name, i->value);
	}

	ast_cli(a->fd, "\n");

	ast_variables_destroy(options);

	return 0;
}

char *stir_shaken_tab_complete_name(const char *word, struct ao2_container *container)
{
	void *obj;
	struct ao2_iterator it;
	int wordlen = strlen(word);
	int ret;

	it = ao2_iterator_init(container, 0);
	while ((obj = ao2_iterator_next(&it))) {
		if (!strncasecmp(word, ast_sorcery_object_get_id(obj), wordlen)) {
			ret = ast_cli_completion_add(ast_strdup(ast_sorcery_object_get_id(obj)));
			if (ret) {
				ao2_ref(obj, -1);
				break;
			}
		}
		ao2_ref(obj, -1);
	}
	ao2_iterator_destroy(&it);

	return NULL;
}

static int reload_module(void)
{
	return ss_config_reload();
}

static int unload_module(void)
{
	int res = 0;

	ss_config_unload();
	ss_crypto_unload();

	res |= ast_custom_function_unregister(&stir_shaken_function);

	return res;
}

#define TN_AUTH_LIST_OID "1.3.6.1.5.5.7.1.26"
#define TN_AUTH_LIST_SHORT "TNAuthList"
#define TN_AUTH_LIST_LONG "TNAuthorizationList"

static int check_for_old_config(void)
{
	const char *error_msg = "There appears to be a 'stir_shaken.conf' file"
	" with old configuration options in it.  Please see the new config"
	" file format in the configs/samples/stir_shaken.conf.sample file"
	" in the source tree at https://github.com/asterisk/asterisk/raw/master/configs/samples/stir_shaken.conf.sample"
	" or visit https://docs.asterisk.org/Deployment/STIR-SHAKEN for more information.";
	RAII_VAR(struct ast_config *, cfg, NULL, ast_config_destroy);
	struct ast_flags config_flags = { 0 };
	char *cat = NULL;

	cfg = ast_config_load("stir_shaken.conf", config_flags);
	if (cfg == NULL) {
		/*
		 * They may be loading from realtime so the fact that there's
		 * no stir-shaken.conf file isn't an issue for this purpose.
		 */
		return AST_MODULE_LOAD_DECLINE;
	}
	while ((cat = ast_category_browse(cfg, cat))) {
		const char *val;
		if (strcasecmp(cat, "general") == 0) {
			ast_log(LOG_ERROR, "%s\n", error_msg);
			return AST_MODULE_LOAD_DECLINE;
		}
		val = ast_variable_retrieve(cfg, cat, "type");
		if (val && (strcasecmp(val, "store") == 0 ||
			strcasecmp(val, "certificate") == 0)) {
			ast_log(LOG_ERROR, "%s\n", error_msg);
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int load_module(void)
{
	int res = 0;

	if (check_for_old_config()) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_crypto_load()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	tn_auth_list_nid = ast_crypto_register_x509_extension(TN_AUTH_LIST_OID,
		TN_AUTH_LIST_SHORT, TN_AUTH_LIST_LONG);
	if (tn_auth_list_nid < 0) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ss_config_load()) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	res |= ast_custom_function_register(&stir_shaken_function);

	return res;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "STIR/SHAKEN Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 1,
	.requires = "res_curl",
);
