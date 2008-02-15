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
 * \extref Depends on the CURL library  - http://curl.haxx.se/
 * 
 */

/*** MODULEINFO
	<depend>curl</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <curl/curl.h>

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"

/*!
 * \brief Execute a curl query and return ast_variable list
 * \param url The base URL from which to retrieve data
 * \param unused Not currently used
 * \param ap list containing one or more field/operator/value set.
 *
 * \retval var on success
 * \retval NULL on failure
*/
static struct ast_variable *realtime_curl(const char *url, const char *unused, va_list ap)
{
	struct ast_str *query;
	char buf1[200], buf2[200];
	const char *newparam, *newval;
	char *stringp, *pair, *key;
	int i;
	struct ast_variable *var=NULL, *prev=NULL;
	const int EncodeSpecialChars = 1, bufsize = 64000;
	char *buffer;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return NULL;
	}

	if (!(query = ast_str_create(1000)))
		return NULL;

	if (!(buffer = ast_malloc(bufsize))) {
		ast_free(query);
		return NULL;
	}

	ast_str_set(&query, 0, "${CURL(%s/single,", url);

	for (i = 0; (newparam = va_arg(ap, const char *)); i++) {
		newval = va_arg(ap, const char *);
		ast_uri_encode(newparam, buf1, sizeof(buf1), EncodeSpecialChars);
		ast_uri_encode(newval, buf2, sizeof(buf2), EncodeSpecialChars);
		ast_str_append(&query, 0, "%s%s=%s", i > 0 ? "&" : "", buf1, buf2);
	}
	va_end(ap);

	ast_str_append(&query, 0, ")}");
	pbx_substitute_variables_helper(NULL, query->str, buffer, bufsize);

	/* Remove any trailing newline characters */
	if ((stringp = strchr(buffer, '\r')) || (stringp = strchr(buffer, '\n')))
		*stringp = '\0';

	stringp = buffer;
	while ((pair = strsep(&stringp, "&"))) {
		key = strsep(&pair, "=");
		ast_uri_decode(key);
		if (pair)
			ast_uri_decode(pair);

		if (!ast_strlen_zero(key)) {
			if (prev) {
				prev->next = ast_variable_new(key, S_OR(pair, ""), "");
				if (prev->next)
					prev = prev->next;
			} else 
				prev = var = ast_variable_new(key, S_OR(pair, ""), "");
		}
	}

	ast_free(buffer);
	ast_free(query);
	return var;
}

/*!
 * \brief Excute an Select query and return ast_config list
 * \param url
 * \param unused
 * \param ap list containing one or more field/operator/value set.
 *
 * \retval struct ast_config pointer on success
 * \retval NULL on failure
*/
static struct ast_config *realtime_multi_curl(const char *url, const char *unused, va_list ap)
{
	struct ast_str *query;
	char buf1[200], buf2[200];
	const char *newparam, *newval;
	char *stringp, *line, *pair, *key, *initfield = NULL;
	int i;
	const int EncodeSpecialChars = 1, bufsize = 256000;
	struct ast_variable *var=NULL;
	struct ast_config *cfg=NULL;
	struct ast_category *cat=NULL;
	char *buffer;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return NULL;
	}

	if (!(query = ast_str_create(1000)))
		return NULL;

	if (!(buffer = ast_malloc(bufsize))) {
		ast_free(query);
		return NULL;
	}

	ast_str_set(&query, 0, "${CURL(%s/multi,", url);

	for (i = 0; (newparam = va_arg(ap, const char *)); i++) {
		newval = va_arg(ap, const char *);
		if (i == 0) {
			char *op;
			initfield = ast_strdupa(newparam);
			if ((op = strchr(initfield, ' ')))
				*op = '\0';
		}
		ast_uri_encode(newparam, buf1, sizeof(buf1), EncodeSpecialChars);
		ast_uri_encode(newval, buf2, sizeof(buf2), EncodeSpecialChars);
		ast_str_append(&query, 0, "%s%s=%s", i > 0 ? "&" : "", buf1, buf2);
	}
	va_end(ap);

	ast_str_append(&query, 0, ")}");

	/* Do the CURL query */
	pbx_substitute_variables_helper(NULL, query->str, buffer, bufsize);

	if (!(cfg = ast_config_new()))
		goto exit_multi;

	/* Line oriented output */
	stringp = buffer;
	while ((line = strsep(&stringp, "\r\n"))) {
		if (ast_strlen_zero(line))
			continue;

		if (!(cat = ast_category_new("", "", 99999)))
			continue;

		while ((pair = strsep(&line, "&"))) {
			key = strsep(&pair, "=");
			ast_uri_decode(key);
			if (pair)
				ast_uri_decode(pair);

			if (!strcasecmp(key, initfield) && pair)
				ast_category_rename(cat, pair);

			if (!ast_strlen_zero(key)) {
				var = ast_variable_new(key, S_OR(pair, ""), "");
				ast_variable_append(cat, var);
			}
		}
		ast_category_append(cfg, cat);
	}

exit_multi:
	ast_free(buffer);
	ast_free(query);
	return cfg;
}

/*!
 * \brief Execute an UPDATE query
 * \param url
 * \param unused
 * \param keyfield where clause field
 * \param lookup value of field for where clause
 * \param ap list containing one or more field/value set(s).
 *
 * Update a database table, prepare the sql statement using keyfield and lookup
 * control the number of records to change. All values to be changed are stored in ap list.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \retval number of rows affected
 * \retval -1 on failure
*/
static int update_curl(const char *url, const char *unused, const char *keyfield, const char *lookup, va_list ap)
{
	struct ast_str *query;
	char buf1[200], buf2[200];
	const char *newparam, *newval;
	char *stringp;
	int i, rowcount = -1;
	const int EncodeSpecialChars = 1, bufsize = 100;
	char *buffer;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return -1;
	}

	if (!(query = ast_str_create(1000)))
		return -1;

	if (!(buffer = ast_malloc(bufsize))) {
		ast_free(query);
		return -1;
	}

	ast_uri_encode(keyfield, buf1, sizeof(buf1), EncodeSpecialChars);
	ast_uri_encode(lookup, buf2, sizeof(buf2), EncodeSpecialChars);
	ast_str_set(&query, 0, "${CURL(%s/update?%s=%s,", url, buf1, buf2);

	for (i = 0; (newparam = va_arg(ap, const char *)); i++) {
		newval = va_arg(ap, const char *);
		ast_uri_encode(newparam, buf1, sizeof(buf1), EncodeSpecialChars);
		ast_uri_encode(newval, buf2, sizeof(buf2), EncodeSpecialChars);
		ast_str_append(&query, 0, "%s%s=%s", i > 0 ? "&" : "", buf1, buf2);
	}
	va_end(ap);

	ast_str_append(&query, 0, ")}");
	pbx_substitute_variables_helper(NULL, query->str, buffer, bufsize);

	/* Line oriented output */
	stringp = buffer;
	while (*stringp <= ' ')
		stringp++;
	sscanf(stringp, "%d", &rowcount);

	ast_free(buffer);
	ast_free(query);

	if (rowcount >= 0)
		return (int)rowcount;

	return -1;
}

/*!
 * \brief Execute an INSERT query
 * \param url
 * \param unused
 * \param ap list containing one or more field/value set(s)
 *
 * Insert a new record into database table, prepare the sql statement.
 * All values to be changed are stored in ap list.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \retval number of rows affected
 * \retval -1 on failure
*/
static int store_curl(const char *url, const char *unused, va_list ap)
{
	struct ast_str *query;
	char buf1[200], buf2[200];
	const char *newparam, *newval;
	char *stringp;
	int i, rowcount = -1;
	const int EncodeSpecialChars = 1, bufsize = 100;
	char *buffer;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return -1;
	}

	if (!(query = ast_str_create(1000)))
		return -1;

	if (!(buffer = ast_malloc(bufsize))) {
		ast_free(query);
		return -1;
	}

	ast_str_set(&query, 0, "${CURL(%s/store,", url);

	for (i = 0; (newparam = va_arg(ap, const char *)); i++) {
		newval = va_arg(ap, const char *);
		ast_uri_encode(newparam, buf1, sizeof(buf1), EncodeSpecialChars);
		ast_uri_encode(newval, buf2, sizeof(buf2), EncodeSpecialChars);
		ast_str_append(&query, 0, "%s%s=%s", i > 0 ? "&" : "", buf1, buf2);
	}
	va_end(ap);

	ast_str_append(&query, 0, ")}");
	pbx_substitute_variables_helper(NULL, query->str, buffer, bufsize);

	stringp = buffer;
	while (*stringp <= ' ')
		stringp++;
	sscanf(stringp, "%d", &rowcount);

	ast_free(buffer);
	ast_free(query);

	if (rowcount >= 0)
		return (int)rowcount;

	return -1;
}

/*!
 * \brief Execute an DELETE query
 * \param url
 * \param unused
 * \param keyfield where clause field
 * \param lookup value of field for where clause
 * \param ap list containing one or more field/value set(s)
 *
 * Delete a row from a database table, prepare the sql statement using keyfield and lookup
 * control the number of records to change. Additional params to match rows are stored in ap list.
 * Sub-in the values to the prepared statement and execute it.
 *
 * \retval number of rows affected
 * \retval -1 on failure
*/
static int destroy_curl(const char *url, const char *unused, const char *keyfield, const char *lookup, va_list ap)
{
	struct ast_str *query;
	char buf1[200], buf2[200];
	const char *newparam, *newval;
	char *stringp;
	int i, rowcount = -1;
	const int EncodeSpecialChars = 1, bufsize = 100;
	char *buffer;

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return -1;
	}

	if (!(query = ast_str_create(1000)))
		return -1;

	if (!(buffer = ast_malloc(bufsize))) {
		ast_free(query);
		return -1;
	}

	ast_uri_encode(keyfield, buf1, sizeof(buf1), EncodeSpecialChars);
	ast_uri_encode(lookup, buf2, sizeof(buf2), EncodeSpecialChars);
	ast_str_set(&query, 0, "${CURL(%s/destroy,%s=%s&", url, buf1, buf2);

	for (i = 0; (newparam = va_arg(ap, const char *)); i++) {
		newval = va_arg(ap, const char *);
		ast_uri_encode(newparam, buf1, sizeof(buf1), EncodeSpecialChars);
		ast_uri_encode(newval, buf2, sizeof(buf2), EncodeSpecialChars);
		ast_str_append(&query, 0, "%s%s=%s", i > 0 ? "&" : "", buf1, buf2);
	}
	va_end(ap);

	ast_str_append(&query, 0, ")}");
	pbx_substitute_variables_helper(NULL, query->str, buffer, bufsize);

	/* Line oriented output */
	stringp = buffer;
	while (*stringp <= ' ')
		stringp++;
	sscanf(stringp, "%d", &rowcount);

	ast_free(buffer);
	ast_free(query);

	if (rowcount >= 0)
		return (int)rowcount;

	return -1;
}


static struct ast_config *config_curl(const char *url, const char *unused, const char *file, struct ast_config *cfg, struct ast_flags flags, const char *sugg_incl)
{
	struct ast_str *query;
	char buf1[200];
	char *stringp, *line, *pair, *key;
	const int EncodeSpecialChars = 1, bufsize = 256000;
	int last_cat_metric = -1, cat_metric = -1;
	struct ast_category *cat=NULL;
	char *buffer, *cur_cat = "";
	char *category = "", *var_name = "", *var_val = "";
	struct ast_flags loader_flags = { 0 };

	if (!ast_custom_function_find("CURL")) {
		ast_log(LOG_ERROR, "func_curl.so must be loaded in order to use res_config_curl.so!!\n");
		return NULL;
	}

	if (!(query = ast_str_create(1000)))
		return NULL;

	if (!(buffer = ast_malloc(bufsize))) {
		ast_free(query);
		return NULL;
	}

	ast_uri_encode(file, buf1, sizeof(buf1), EncodeSpecialChars);
	ast_str_set(&query, 0, "${CURL(%s/static?file=%s)}", url, buf1);

	/* Do the CURL query */
	pbx_substitute_variables_helper(NULL, query->str, buffer, bufsize);

	/* Line oriented output */
	stringp = buffer;
	cat = ast_config_get_current_category(cfg);

	while ((line = strsep(&stringp, "\r\n"))) {
		if (ast_strlen_zero(line))
			continue;

		while ((pair = strsep(&line, "&"))) {
			key = strsep(&pair, "=");
			ast_uri_decode(key);
			if (pair)
				ast_uri_decode(pair);

			if (!strcasecmp(key, "category"))
				category = S_OR(pair, "");
			else if (!strcasecmp(key, "var_name"))
				var_name = S_OR(pair, "");
			else if (!strcasecmp(key, "var_val"))
				var_val = S_OR(pair, "");
			else if (!strcasecmp(key, "cat_metric"))
				cat_metric = pair ? atoi(pair) : 0;
		}

		if (!strcmp(var_name, "#include")) {
			if (!ast_config_internal_load(var_val, cfg, loader_flags, ""))
				return NULL;
		}

		if (strcmp(category, cur_cat) || last_cat_metric != cat_metric) {
			if (!(cat = ast_category_new(category, "", 99999)))
				break;
			cur_cat = category;
			last_cat_metric = cat_metric;
			ast_category_append(cfg, cat);
		}
		ast_variable_append(cat, ast_variable_new(var_name, var_val, ""));
	}

	ast_free(buffer);
	ast_free(query);
	return cfg;
}

static struct ast_config_engine curl_engine = {
	.name = "curl",
	.load_func = config_curl,
	.realtime_func = realtime_curl,
	.realtime_multi_func = realtime_multi_curl,
	.store_func = store_curl,
	.destroy_func = destroy_curl,
	.update_func = update_curl
};

static int unload_module (void)
{
	ast_config_engine_deregister(&curl_engine);
	ast_verb(1, "res_config_curl unloaded.\n");
	return 0;
}

static int load_module (void)
{
	ast_config_engine_register(&curl_engine);
	ast_verb(1, "res_config_curl loaded.\n");
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Realtime Curl configuration");
