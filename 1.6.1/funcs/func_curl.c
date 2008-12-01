/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C)  2004 - 2006, Tilghman Lesher
 *
 * Tilghman Lesher <curl-20050919@the-tilghman.com>
 * and Brian Wilkins <bwilkins@cfl.rr.com> (Added POST option)
 *
 * app_curl.c is distributed with no restrictions on usage or
 * redistribution.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 * 
 * \brief Curl - Load a URL
 *
 * \author Tilghman Lesher <curl-20050919@the-tilghman.com>
 *
 * \note Brian Wilkins <bwilkins@cfl.rr.com> (Added POST option) 
 *
 * \extref Depends on the CURL library  - http://curl.haxx.se/
 * 
 * \ingroup functions
 */
 
/*** MODULEINFO
	<depend>curl</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <curl/curl.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/utils.h"
#include "asterisk/threadstorage.h"

static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	register int realsize = size * nmemb;
	struct ast_str **str = (struct ast_str **)data;

	if (ast_str_make_space(str, (*str)->used + realsize + 1) == 0) {
		memcpy(&(*str)->str[(*str)->used], ptr, realsize);
		(*str)->used += realsize;
	}

	return realsize;
}

static const char *global_useragent = "asterisk-libcurl-agent/1.0";

static int curl_instance_init(void *data)
{
	CURL **curl = data;

	if (!(*curl = curl_easy_init()))
		return -1;

	curl_easy_setopt(*curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(*curl, CURLOPT_TIMEOUT, 180);
	curl_easy_setopt(*curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(*curl, CURLOPT_USERAGENT, global_useragent);

	return 0;
}

static void curl_instance_cleanup(void *data)
{
	CURL **curl = data;

	curl_easy_cleanup(*curl);

	ast_free(data);
}

AST_THREADSTORAGE_CUSTOM(curl_instance, curl_instance_init, curl_instance_cleanup);

static int curl_internal(struct ast_str **chunk, char *url, char *post)
{
	CURL **curl;

	if (!(curl = ast_threadstorage_get(&curl_instance, sizeof(*curl))))
		return -1;

	curl_easy_setopt(*curl, CURLOPT_URL, url);
	curl_easy_setopt(*curl, CURLOPT_WRITEDATA, (void *) chunk);

	if (post) {
		curl_easy_setopt(*curl, CURLOPT_POST, 1);
		curl_easy_setopt(*curl, CURLOPT_POSTFIELDS, post);
	}

	curl_easy_perform(*curl);

	if (post)
		curl_easy_setopt(*curl, CURLOPT_POST, 0);

	return 0;
}

static int acf_curl_exec(struct ast_channel *chan, const char *cmd, char *info, char *buf, size_t len)
{
	struct ast_str *str = ast_str_create(16);
	int ret = -1;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(url);
		AST_APP_ARG(postdata);
	);

	*buf = '\0';
	
	if (ast_strlen_zero(info)) {
		ast_log(LOG_WARNING, "CURL requires an argument (URL)\n");
		ast_free(str);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, info);	

	if (chan)
		ast_autoservice_start(chan);

	if (!curl_internal(&str, args.url, args.postdata)) {
		if (str->used) {
			str->str[str->used] = '\0';
			if (str->str[str->used - 1] == '\n') {
				str->str[str->used - 1] = '\0';
			}

			ast_copy_string(buf, str->str, len);
		}
		ret = 0;
	} else {
		ast_log(LOG_ERROR, "Cannot allocate curl structure\n");
	}
	ast_free(str);

	if (chan)
		ast_autoservice_stop(chan);
	
	return ret;
}

struct ast_custom_function acf_curl = {
	.name = "CURL",
	.synopsis = "Retrieves the contents of a URL",
	.syntax = "CURL(url[,post-data])",
	.desc =
	"  url       - URL to retrieve\n"
	"  post-data - Optional data to send as a POST (GET is default action)\n",
	.read = acf_curl_exec,
};

static int unload_module(void)
{
	int res;

	res = ast_custom_function_unregister(&acf_curl);

	return res;
}

static int load_module(void)
{
	int res;

	if (!ast_module_check("res_curl.so")) {
		if (ast_load_resource("res_curl.so") != AST_MODULE_LOAD_SUCCESS) {
			ast_log(LOG_ERROR, "Cannot load res_curl, so func_curl cannot be loaded\n");
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	res = ast_custom_function_register(&acf_curl);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Load external URL");

