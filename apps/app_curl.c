/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C)  2004 - 2005, Tilghman Lesher
 *
 * Tilghman Lesher <curl-20041222@the-tilghman.com>
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

/*
 *
 * Curl - App to load a URL
 * 
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
"external applications of an event.  Returns 0 or -1 on fatal error.\n"
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

static int curl_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct localuser *u;
	CURL *curl;
	char *info, *post_data=NULL, *url;

	if (!data || !strlen((char *)data)) {
		ast_log(LOG_WARNING, "Curl requires an argument (URL)\n");
		return -1;
	}

	if ((info = ast_strdupa((char *)data))) {
		url = strsep(&info, "|");
		post_data = info;
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}

	LOCAL_USER_ADD(u);

	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();

	if (curl) {
		struct MemoryStruct chunk;

		chunk.memory=NULL; /* we expect realloc(NULL, size) to work */
		chunk.size = 0;    /* no data at this point */

		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "asterisk-libcurl-agent/1.0");

		if (post_data) {
			curl_easy_setopt(curl, CURLOPT_POST, 1);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
		}

		curl_easy_perform(curl);
		curl_easy_cleanup(curl);

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

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app);
}

int load_module(void)
{
	return ast_register_application(app, curl_exec, synopsis, descrip);
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
