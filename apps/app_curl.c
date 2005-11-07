/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C)  2004 - 2005, Tilghman Lesher
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
 * \brief Curl - App to load a URL
 * 
 * \ingroup applications
 */
 
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/cli.h"
#include "asterisk/options.h"
#include "asterisk/module.h"

static char *tdesc = "Load external URL";

static char *app = "Curl";

static char *synopsis = "Load an external URL";

static char *descrip = 
"  Curl(URL[|postdata]): Requests the URL.  Mainly used for signalling\n"
"external applications of an event.  Curl will fail on fatal errors. \n"
"Argument specified treated as POST data.  Also sets CURL variable with the\n"
"resulting page.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

struct MemoryStruct {
	char *memory;
	size_t size;
};

static void *myrealloc(void *ptr, size_t size)
{
	/* There might be a realloc() out there that doesn't like reallocing
	   NULL pointers, so we take care of it here */
	if (ptr)
		return realloc(ptr, size);
	else
		return malloc(size);
}

static size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data)
{
	register int realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)data;

	mem->memory = (char *)myrealloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory) {
		memcpy(&(mem->memory[mem->size]), ptr, realsize);
		mem->size += realsize;
		mem->memory[mem->size] = 0;
	}
	return realsize;
}

static int curl_internal(struct MemoryStruct *chunk, char *url, char *post)
{
	CURL *curl;

	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();

	if (!curl) {
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "asterisk-libcurl-agent/1.0");

	if (post) {
		curl_easy_setopt(curl, CURLOPT_POST, 1);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
	}

	curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	return 0;
}

static int curl_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	char *info, *post_data=NULL, *url;
	struct MemoryStruct chunk = { NULL, 0 };
	static int dep_warning = 0;
	
	if (!dep_warning) {
		ast_log(LOG_WARNING, "The application Curl is deprecated.  Please use the CURL() function instead.\n");
		dep_warning = 1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Curl requires an argument (URL)\n");
		return -1;
	}
	
	LOCAL_USER_ADD(u);
	
	if ((info = ast_strdupa(data))) {
		url = strsep(&info, "|");
		post_data = info;
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return -1;
	}

	if (! curl_internal(&chunk, url, post_data)) {
		if (chunk.memory) {
			chunk.memory[chunk.size] = '\0';
			if (chunk.memory[chunk.size - 1] == 10)
				chunk.memory[chunk.size - 1] = '\0';

			pbx_builtin_setvar_helper(chan, "CURL", chunk.memory);

			free(chunk.memory);
		}
	} else {
		ast_log(LOG_ERROR, "Cannot allocate curl structure\n");
		res = -1;
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

static char *acf_curl_exec(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	struct localuser *u;
	char *info, *post_data=NULL, *url;
	struct MemoryStruct chunk = { NULL, 0 };

	*buf = '\0';
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "CURL requires an argument (URL)\n");
		return buf;
	}

	LOCAL_USER_ACF_ADD(u);

	info = ast_strdupa(data);
	if (!info) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return buf;
	}
	
	url = strsep(&info, "|");
	post_data = info;
	
	if (! curl_internal(&chunk, url, post_data)) {
		if (chunk.memory) {
			chunk.memory[chunk.size] = '\0';
			if (chunk.memory[chunk.size - 1] == 10)
				chunk.memory[chunk.size - 1] = '\0';

			ast_copy_string(buf, chunk.memory, len);
			free(chunk.memory);
		}
	} else {
		ast_log(LOG_ERROR, "Cannot allocate curl structure\n");
	}

	LOCAL_USER_REMOVE(u);
	return buf;
}

struct ast_custom_function acf_curl = {
	.name = "CURL",
	.synopsis = "Retrieves the contents of a URL",
	.syntax = "CURL(url[|post-data])",
	.desc =
	"  url       - URL to retrieve\n"
	"  post-data - Optional data to send as a POST (GET is default action)\n",
	.read = acf_curl_exec,
};

int unload_module(void)
{
	int res;

	res = ast_custom_function_unregister(&acf_curl);
	res |= ast_unregister_application(app);

	STANDARD_HANGUP_LOCALUSERS;
	
	return res;
}

int load_module(void)
{
	int res;

	res = ast_custom_function_register(&acf_curl);
	res |= ast_register_application(app, curl_exec, synopsis, descrip);

	return res;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
