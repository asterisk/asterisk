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

int get_tn_auth_nid(void)
{
	return tn_auth_list_nid;
}

/* The datastore struct holding verification information for the channel */
struct stir_datastore {
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
static void stir_datastore_free(struct stir_datastore *datastore)
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
static void stir_datastore_destroy_cb(void *data)
{
	struct stir_datastore *datastore = data;
	stir_datastore_free(datastore);
}

/* The stir_shaken_datastore info used to add and compare stir_shaken_datastores on the channel */
static const struct ast_datastore_info stir_shaken_datastore_info = {
	.type = "STIR/SHAKEN VERIFICATION",
	.destroy = stir_datastore_destroy_cb,
};

int ast_stir_shaken_add_result_to_channel(
	struct ast_stir_shaken_vs_ctx *ctx)
{
	struct stir_datastore *stir_datastore;
	struct ast_datastore *chan_datastore;
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

	stir_datastore = ast_calloc(1, sizeof(*stir_datastore));
	if (!stir_datastore) {
		ast_log(LOG_ERROR, "Failed to allocate space for STIR/SHAKEN datastore for "
			"channel %s\n", chan_name);
		return -1;
	}

	stir_datastore->identity = ast_strdup(ctx->identity_hdr);
	if (!stir_datastore->identity) {
		ast_log(LOG_ERROR, "Failed to allocate space for STIR/SHAKEN datastore "
			"identity for channel %s\n", chan_name);
		stir_datastore_free(stir_datastore);
		return -1;
	}

	stir_datastore->attestation = ast_strdup(ctx->attestation);
	if (!stir_datastore->attestation) {
		ast_log(LOG_ERROR, "Failed to allocate space for STIR/SHAKEN datastore "
			"attestation for channel %s\n", chan_name);
		stir_datastore_free(stir_datastore);
		return -1;
	}

	stir_datastore->verify_result = ctx->failure_reason;

	chan_datastore = ast_datastore_alloc(&stir_shaken_datastore_info, NULL);
	if (!chan_datastore) {
		ast_log(LOG_ERROR, "Failed to allocate space for datastore for channel "
			"%s\n", chan_name);
		stir_datastore_free(stir_datastore);
		return -1;
	}

	chan_datastore->data = stir_datastore;

	ast_channel_lock(ctx->chan);
	ast_channel_datastore_add(ctx->chan, chan_datastore);
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
	struct stir_datastore *stir_datastore;
	struct ast_datastore *chan_datastore;
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
		AST_LIST_TRAVERSE(ast_channel_datastores(chan), chan_datastore, entry) {
			if (chan_datastore->info != &stir_shaken_datastore_info) {
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
	AST_LIST_TRAVERSE(ast_channel_datastores(chan), chan_datastore, entry) {
		if (chan_datastore->info != &stir_shaken_datastore_info) {
			continue;
		}

		if (current_index == target_index) {
			break;
		}

		current_index++;
	}
	ast_channel_unlock(chan);
	if (current_index != target_index || !chan_datastore) {
		ast_log(LOG_WARNING, "No STIR/SHAKEN results for index '%s'\n", first);
		return -1;
	}
	stir_datastore = chan_datastore->data;

	if (!strcasecmp(second, "identity")) {
		ast_copy_string(buf, stir_datastore->identity, len);
	} else if (!strcasecmp(second, "attestation")) {
		ast_copy_string(buf, stir_datastore->attestation, len);
	} else if (!strcasecmp(second, "verify_result")) {
		ast_copy_string(buf, vs_response_code_to_str(stir_datastore->verify_result), len);
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

static int reload_module(void)
{
	return common_config_reload();
}

static int unload_module(void)
{
	int res = 0;

	common_config_unload();
	crypto_unload();

	res |= ast_custom_function_unregister(&stir_shaken_function);

	return 0;
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
	if (cfg == CONFIG_STATUS_FILEMISSING) {
		/*
		 * They may be loading from realtime so the fact that there's
		 * no stir-shaken.conf file isn't an issue for this purpose.
		 */
		return AST_MODULE_LOAD_SUCCESS;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		cfg = NULL;
		ast_log(LOG_ERROR, "The stir_shaken.conf file is invalid\n");
		return AST_MODULE_LOAD_DECLINE;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		/* This can never happen but is included for completeness */
		cfg = NULL;
		return AST_MODULE_LOAD_SUCCESS;
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

	res = check_for_old_config();
	if (res != AST_MODULE_LOAD_SUCCESS) {
		return res;
	}

	res = crypto_load();
	if (res != AST_MODULE_LOAD_SUCCESS) {
		return res;
	}

	tn_auth_list_nid = crypto_register_x509_extension(TN_AUTH_LIST_OID,
		TN_AUTH_LIST_SHORT, TN_AUTH_LIST_LONG);
	if (tn_auth_list_nid < 0) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	res = common_config_load();
	if (res != AST_MODULE_LOAD_SUCCESS) {
		unload_module();
		return res;
	}

	res = ast_custom_function_register(&stir_shaken_function);
	if (res != 0) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "STIR/SHAKEN Module for Asterisk",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 1,
	.requires = "res_curl",
);
