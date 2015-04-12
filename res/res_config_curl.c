/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Tilghman Lesher <res_config_curl_v1@the-tilghman.com>
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

/*! \file
 *
 * \brief curl plugin for portable configuration engine
 *
 * \author Tilghman Lesher <res_config_curl_v1@the-tilghman.com>
 *
 * Depends on the CURL library - http://curl.haxx.se/
 * 
 */

/*** MODULEINFO
	<depend>curl</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <curl/curl.h>

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"

AST_THREADSTORAGE(query_buf);
AST_THREADSTORAGE(result_buf);

/*!
 * \brief Execute a curl query and return ast_variable list
 * \param url The base URL from which to retrieve data
 * \param unused Not currently used
 * \param fields list containing one or more field/operator/value set.
 *
 * \retval var on success
 * \retval NULL on failure
*/
static struct ast_variable *realtime_curl(const char *url, const char *unused, const struct ast_variable *fields)
{
	struct ast_str *query, *buffer;
	char buf1[256], buf2[256];
	const struct ast_variable *field;
	char *stringp, *pair, *key;
	unsigned int start = 1;
	struct ast_variable *var = NULL, *prev = NULL;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return NULL;
	}

	if (!(query = ast_str_thread_get(&query_buf, 16))) {
		return NULL;
	}

	if (!(buffer = ast_str_thread_get(&result_buf, 16))) {
		return NULL;
	}

	ast_str_set(&query, 0, "${CURL(%s/single,", url);

	for (field = fields; field; field = field->next) {
		ast_uri_encode(field->name, buf1, sizeof(buf1), ast_uri_http);
		ast_uri_encode(field->value, buf2, sizeof(buf2), ast_uri_http);
		ast_str_append(&query, 0, "%s%s=%s", !start ? "&" : "", buf1, buf2);
		start = 0;
	}

	ast_str_append(&query, 0, ")}");
	ast_str_substitute_variables(&buffer, 0, NULL, ast_str_buffer(query));

	/* Remove any trailing newline characters */
	if ((stringp = strchr(ast_str_buffer(buffer), '\r')) || (stringp = strchr(ast_str_buffer(buffer), '\n'))) {
		*stringp = '\0';
	}

	stringp = ast_str_buffer(buffer);
	while ((pair = strsep(&stringp, "&"))) {
		key = strsep(&pair, "=");
		ast_uri_decode(key, ast_uri_http);
		if (pair) {
			ast_uri_decode(pair, ast_uri_http);
		}

		if (!ast_strlen_zero(key)) {
			if (prev) {
				prev->next = ast_variable_new(key, S_OR(pair, ""), "");
				if (prev->next) {
					prev = prev->next;
				}
			} else {
				prev = var = ast_variable_new(key, S_OR(pair, ""), "");
			}
		}
	}

	return var;
}

/*!
 * \brief Excute an Select query and return ast_config list
 * \param url
 * \param unused
 * \param fields list containing one or more field/operator/value set.
 *
 * \retval struct ast_config pointer on success
 * \retval NULL on failure
*/
static struct ast_config *realtime_multi_curl(const char *url, const char *unused, const struct ast_variable *fields)
{
	struct ast_str *query, *buffer;
	char buf1[256], buf2[256];
	const struct ast_variable *field;
	char *stringp, *line, *pair, *key, *initfield = NULL;
	int start = 1;
	struct ast_variable *var = NULL;
	struct ast_config *cfg = NULL;
	struct ast_category *cat = NULL;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return NULL;
	}

	if (!(query = ast_str_thread_get(&query_buf, 16))) {
		return NULL;
	}

	if (!(buffer = ast_str_thread_get(&result_buf, 16))) {
		return NULL;
	}

	ast_str_set(&query, 0, "${CURL(%s/multi,", url);

	for (field = fields; field; field = field->next) {
		if (start) {
			char *op;
			initfield = ast_strdupa(field->name);
			if ((op = strchr(initfield, ' ')))
				*op = '\0';
		}
		ast_uri_encode(field->name, buf1, sizeof(buf1), ast_uri_http);
		ast_uri_encode(field->value, buf2, sizeof(buf2), ast_uri_http);
		ast_str_append(&query, 0, "%s%s=%s", !start ? "&" : "", buf1, buf2);
		start = 0;
	}

	ast_str_append(&query, 0, ")}");

	/* Do the CURL query */
	ast_str_substitute_variables(&buffer, 0, NULL, ast_str_buffer(query));

	if (!(cfg = ast_config_new())) {
		return NULL;
	}

	/* Line oriented output */
	stringp = ast_str_buffer(buffer);
	while ((line = strsep(&stringp, "\r\n"))) {
		if (ast_strlen_zero(line)) {
			continue;
		}

		if (!(cat = ast_category_new("", "", 99999))) {
			continue;
		}

		while ((pair = strsep(&line, "&"))) {
			key = strsep(&pair, "=");
			ast_uri_decode(key, ast_uri_http);
			if (pair) {
				ast_uri_decode(pair, ast_uri_http);
			}

			if (!strcasecmp(key, initfield) && pair) {
				ast_category_rename(cat, pair);
			}

			if (!ast_strlen_zero(key)) {
				var = ast_variable_new(key, S_OR(pair, ""), "");
				ast_variable_append(cat, var);
			}
		}
		ast_category_append(cfg, cat);
	}

	return cfg;
}

/*!
 * \brief Execute an UPDATE query
 * \param url
 * \param unused
 * \param keyfield where clause field
 * \param lookup value of field for where clause
 * \param fields list containing one or more field/value set(s).
 *
 * Update a database table, prepare the sql statement using keyfield and lookup
 * control the number of records to change. All values to be changed are stored in ap list.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \retval number of rows affected
 * \retval -1 on failure
*/
static int update_curl(const char *url, const char *unused, const char *keyfield, const char *lookup, const struct ast_variable *fields)
{
	struct ast_str *query, *buffer;
	char buf1[256], buf2[256];
	const struct ast_variable *field;
	char *stringp;
	int start = 1, rowcount = -1;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return -1;
	}

	if (!(query = ast_str_thread_get(&query_buf, 16))) {
		return -1;
	}

	if (!(buffer = ast_str_thread_get(&result_buf, 16))) {
		return -1;
	}

	ast_uri_encode(keyfield, buf1, sizeof(buf1), ast_uri_http);
	ast_uri_encode(lookup, buf2, sizeof(buf2), ast_uri_http);
	ast_str_set(&query, 0, "${CURL(%s/update?%s=%s,", url, buf1, buf2);

	for (field = fields; field; field = field->next) {
		ast_uri_encode(field->name, buf1, sizeof(buf1), ast_uri_http);
		ast_uri_encode(field->value, buf2, sizeof(buf2), ast_uri_http);
		ast_str_append(&query, 0, "%s%s=%s", !start ? "&" : "", buf1, buf2);
		start = 0;
	}

	ast_str_append(&query, 0, ")}");
	ast_str_substitute_variables(&buffer, 0, NULL, ast_str_buffer(query));

	/* Line oriented output */
	stringp = ast_str_buffer(buffer);
	while (*stringp <= ' ') {
		stringp++;
	}
	sscanf(stringp, "%30d", &rowcount);

	if (rowcount >= 0) {
		return (int)rowcount;
	}

	return -1;
}

static int update2_curl(const char *url, const char *unused, const struct ast_variable *lookup_fields, const struct ast_variable *update_fields)
{
	struct ast_str *query, *buffer;
	char buf1[200], buf2[200];
	const struct ast_variable *field;
	char *stringp;
	unsigned int start = 1;
	int rowcount = -1;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return -1;
	}

	if (!(query = ast_str_thread_get(&query_buf, 1000)))
		return -1;

	if (!(buffer = ast_str_thread_get(&result_buf, 16))) {
		return -1;
	}

	ast_str_set(&query, 0, "${CURL(%s/update?", url);

	for (field = lookup_fields; field; field = field->next) {
		ast_uri_encode(field->name, buf1, sizeof(buf1), ast_uri_http);
		ast_uri_encode(field->value, buf2, sizeof(buf2), ast_uri_http);
		ast_str_append(&query, 0, "%s%s=%s", !start ? "" : "&", buf1, buf2);
		start = 0;
	}
	ast_str_append(&query, 0, ",");
	start = 1;

	for (field = update_fields; field; field = field->next) {
		ast_uri_encode(field->name, buf1, sizeof(buf1), ast_uri_http);
		ast_uri_encode(field->value, buf2, sizeof(buf2), ast_uri_http);
		ast_str_append(&query, 0, "%s%s=%s", !start ? "" : "&", buf1, buf2);
		start = 0;
	}

	ast_str_append(&query, 0, ")}");
	/* Proxies work, by setting CURLOPT options in the [globals] section of
	 * extensions.conf.  Unfortunately, this means preloading pbx_config.so
	 * so that they have an opportunity to be set prior to startup realtime
	 * queries. */
	ast_str_substitute_variables(&buffer, 0, NULL, ast_str_buffer(query));

	/* Line oriented output */
	stringp = ast_str_buffer(buffer);
	while (*stringp <= ' ') {
		stringp++;
	}
	sscanf(stringp, "%30d", &rowcount);

	if (rowcount >= 0) {
		return (int)rowcount;
	}

	return -1;
}

/*!
 * \brief Execute an INSERT query
 * \param url
 * \param unused
 * \param fields list containing one or more field/value set(s)
 *
 * Insert a new record into database table, prepare the sql statement.
 * All values to be changed are stored in ap list.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \retval number of rows affected
 * \retval -1 on failure
*/
static int store_curl(const char *url, const char *unused, const struct ast_variable *fields)
{
	struct ast_str *query, *buffer;
	char buf1[256], buf2[256];
	const struct ast_variable *field;
	char *stringp;
	int start = 1, rowcount = -1;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return -1;
	}

	if (!(query = ast_str_thread_get(&query_buf, 1000))) {
		return -1;
	}

	if (!(buffer = ast_str_thread_get(&result_buf, 16))) {
		return -1;
	}

	ast_str_set(&query, 0, "${CURL(%s/store,", url);

	for (field = fields; field; field = field->next) {
		ast_uri_encode(field->name, buf1, sizeof(buf1), ast_uri_http);
		ast_uri_encode(field->value, buf2, sizeof(buf2), ast_uri_http);
		ast_str_append(&query, 0, "%s%s=%s", !start ? "&" : "", buf1, buf2);
		start = 0;
	}

	ast_str_append(&query, 0, ")}");
	ast_str_substitute_variables(&buffer, 0, NULL, ast_str_buffer(query));

	stringp = ast_str_buffer(buffer);
	while (*stringp <= ' ') {
		stringp++;
	}
	sscanf(stringp, "%30d", &rowcount);

	if (rowcount >= 0) {
		return rowcount;
	}

	return -1;
}

/*!
 * \brief Execute an DELETE query
 * \param url
 * \param unused
 * \param keyfield where clause field
 * \param lookup value of field for where clause
 * \param fields list containing one or more field/value set(s)
 *
 * Delete a row from a database table, prepare the sql statement using keyfield and lookup
 * control the number of records to change. Additional params to match rows are stored in ap list.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \retval number of rows affected
 * \retval -1 on failure
*/
static int destroy_curl(const char *url, const char *unused, const char *keyfield, const char *lookup, const struct ast_variable *fields)
{
	struct ast_str *query, *buffer;
	char buf1[200], buf2[200];
	const struct ast_variable *field;
	char *stringp;
	int start = 1, rowcount = -1;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return -1;
	}

	if (!(query = ast_str_thread_get(&query_buf, 1000))) {
		return -1;
	}

	if (!(buffer = ast_str_thread_get(&result_buf, 16))) {
		return -1;
	}

	ast_uri_encode(keyfield, buf1, sizeof(buf1), ast_uri_http);
	ast_uri_encode(lookup, buf2, sizeof(buf2), ast_uri_http);
	ast_str_set(&query, 0, "${CURL(%s/destroy,%s=%s&", url, buf1, buf2);

	for (field = fields; field; field = field->next) {
		ast_uri_encode(field->name, buf1, sizeof(buf1), ast_uri_http);
		ast_uri_encode(field->value, buf2, sizeof(buf2), ast_uri_http);
		ast_str_append(&query, 0, "%s%s=%s", !start ? "&" : "", buf1, buf2);
		start = 0;
	}

	ast_str_append(&query, 0, ")}");
	ast_str_substitute_variables(&buffer, 0, NULL, ast_str_buffer(query));

	/* Line oriented output */
	stringp = ast_str_buffer(buffer);
	while (*stringp <= ' ') {
		stringp++;
	}
	sscanf(stringp, "%30d", &rowcount);

	if (rowcount >= 0) {
		return (int)rowcount;
	}

	return -1;
}

static int require_curl(const char *url, const char *unused, va_list ap)
{
	struct ast_str *query, *buffer;
	char *elm, field[256];
	int type, size, i = 0;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return -1;
	}

	if (!(query = ast_str_thread_get(&query_buf, 100))) {
		return -1;
	}

	if (!(buffer = ast_str_thread_get(&result_buf, 16))) {
		return -1;
	}

	ast_str_set(&query, 0, "${CURL(%s/require,", url);

	while ((elm = va_arg(ap, char *))) {
		type = va_arg(ap, require_type);
		size = va_arg(ap, int);
		ast_uri_encode(elm, field, sizeof(field), ast_uri_http);
		ast_str_append(&query, 0, "%s%s=%s%%3A%d",
			i > 0 ? "&" : "",
			field,
			type == RQ_CHAR ? "char" :
			type == RQ_INTEGER1 ? "integer1" :
			type == RQ_UINTEGER1 ? "uinteger1" :
			type == RQ_INTEGER2 ? "integer2" :
			type == RQ_UINTEGER2 ? "uinteger2" :
			type == RQ_INTEGER3 ? "integer3" :
			type == RQ_UINTEGER3 ? "uinteger3" :
			type == RQ_INTEGER4 ? "integer4" :
			type == RQ_UINTEGER4 ? "uinteger4" :
			type == RQ_INTEGER8 ? "integer8" :
			type == RQ_UINTEGER8 ? "uinteger8" :
			type == RQ_DATE ? "date" :
			type == RQ_DATETIME ? "datetime" :
			type == RQ_FLOAT ? "float" :
			"unknown", size);
		i++;
	}

	ast_str_append(&query, 0, ")}");
	ast_str_substitute_variables(&buffer, 0, NULL, ast_str_buffer(query));
	return atoi(ast_str_buffer(buffer));
}

static struct ast_config *config_curl(const char *url, const char *unused, const char *file, struct ast_config *cfg, struct ast_flags flags, const char *sugg_incl, const char *who_asked)
{
	struct ast_str *query, *buffer;
	char buf1[200];
	char *stringp, *line, *pair, *key;
	int last_cat_metric = -1, cat_metric = -1;
	struct ast_category *cat = NULL;
	char *cur_cat = "";
	char *category = "", *var_name = "", *var_val = "";
	struct ast_flags loader_flags = { 0 };

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return NULL;
	}

	if (!(query = ast_str_thread_get(&query_buf, 100))) {
		return NULL;
	}

	if (!(buffer = ast_str_thread_get(&result_buf, 16))) {
		return NULL;
	}

	ast_uri_encode(file, buf1, sizeof(buf1), ast_uri_http);
	ast_str_set(&query, 0, "${CURL(%s/static?file=%s)}", url, buf1);

	/* Do the CURL query */
	ast_str_substitute_variables(&buffer, 0, NULL, ast_str_buffer(query));

	/* Line oriented output */
	stringp = ast_str_buffer(buffer);
	cat = ast_config_get_current_category(cfg);

	while ((line = strsep(&stringp, "\r\n"))) {
		if (ast_strlen_zero(line)) {
			continue;
		}

		while ((pair = strsep(&line, "&"))) {
			key = strsep(&pair, "=");
			ast_uri_decode(key, ast_uri_http);
			if (pair) {
				ast_uri_decode(pair, ast_uri_http);
			}

			if (!strcasecmp(key, "category")) {
				category = S_OR(pair, "");
			} else if (!strcasecmp(key, "var_name")) {
				var_name = S_OR(pair, "");
			} else if (!strcasecmp(key, "var_val")) {
				var_val = S_OR(pair, "");
			} else if (!strcasecmp(key, "cat_metric")) {
				cat_metric = pair ? atoi(pair) : 0;
			}
		}

		if (!strcmp(var_name, "#include")) {
			if (!ast_config_internal_load(var_val, cfg, loader_flags, "", who_asked))
				return NULL;
		}

		if (!cat || strcmp(category, cur_cat) || last_cat_metric != cat_metric) {
			if (!(cat = ast_category_new(category, "", 99999)))
				break;
			cur_cat = category;
			last_cat_metric = cat_metric;
			ast_category_append(cfg, cat);
		}
		ast_variable_append(cat, ast_variable_new(var_name, var_val, ""));
	}

	return cfg;
}

static struct ast_config_engine curl_engine = {
	.name = "curl",
	.load_func = config_curl,
	.realtime_func = realtime_curl,
	.realtime_multi_func = realtime_multi_curl,
	.store_func = store_curl,
	.destroy_func = destroy_curl,
	.update_func = update_curl,
	.update2_func = update2_curl,
	.require_func = require_curl,
};

static int reload_module(void)
{
	struct ast_flags flags = { CONFIG_FLAG_NOREALTIME };
	struct ast_config *cfg;
	struct ast_variable *var;

	if (!(cfg = ast_config_load("res_curl.conf", flags))) {
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "res_curl.conf could not be parsed!\n");
		return 0;
	}

	if (!(var = ast_variable_browse(cfg, "globals")) && !(var = ast_variable_browse(cfg, "global")) && !(var = ast_variable_browse(cfg, "general"))) {
		ast_log(LOG_WARNING, "[globals] not found in res_curl.conf\n");
		ast_config_destroy(cfg);
		return 0;
	}

	for (; var; var = var->next) {
		if (strncmp(var->name, "CURLOPT(", 8)) {
			char name[256];
			snprintf(name, sizeof(name), "CURLOPT(%s)", var->name);
			pbx_builtin_setvar_helper(NULL, name, var->value);
		} else {
			pbx_builtin_setvar_helper(NULL, var->name, var->value);
		}
	}
	ast_config_destroy(cfg);
	return 0;
}

static int unload_module(void)
{
	ast_config_engine_deregister(&curl_engine);

	return 0;
}

static int load_module(void)
{
	if (!ast_module_check("res_curl.so")) {
		if (ast_load_resource("res_curl.so") != AST_MODULE_LOAD_SUCCESS) {
			ast_log(LOG_ERROR, "Cannot load res_curl, so res_config_curl cannot be loaded\n");
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	if (!ast_module_check("func_curl.so")) {
		if (ast_load_resource("func_curl.so") != AST_MODULE_LOAD_SUCCESS) {
			ast_log(LOG_ERROR, "Cannot load func_curl, so res_config_curl cannot be loaded\n");
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	reload_module();

	ast_config_engine_register(&curl_engine);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Realtime Curl configuration",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.load_pri = AST_MODPRI_REALTIME_DRIVER,
	);
