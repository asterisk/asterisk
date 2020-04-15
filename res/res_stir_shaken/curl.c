/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Ben Ford <bford@sangoma.com>
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

#include "asterisk/utils.h"
#include "asterisk/logger.h"
#include "curl.h"
#include "general.h"

#include <curl/curl.h>

/* Used to check CURL headers */
#define MAX_HEADER_LENGTH 1023

/* Used for CURL requests */
#define GLOBAL_USERAGENT "asterisk-libcurl-agent/1.0"

/* CURL callback data to avoid storing useless info in AstDB */
struct curl_cb_data {
	char *cache_control;
	char *expires;
};

struct curl_cb_data *curl_cb_data_create(void)
{
	struct curl_cb_data *data;

	data = ast_calloc(1, sizeof(data));

	return data;
}

void curl_cb_data_free(struct curl_cb_data *data)
{
	if (!data) {
		return;
	}

	ast_free(data->cache_control);
	ast_free(data->expires);

	ast_free(data);
}

char *curl_cb_data_get_cache_control(const struct curl_cb_data *data)
{
	if (!data) {
		return NULL;
	}

	return data->cache_control;
}

char *curl_cb_data_get_expires(const struct curl_cb_data *data)
{
	if (!data) {
		return NULL;
	}

	return data->expires;
}

/*!
 * \brief Called when a CURL request completes
 *
 * \param data The curl_cb_data structure to store expiration info
 */
static size_t curl_header_callback(char *buffer, size_t size, size_t nitems, void *data)
{
	struct curl_cb_data *cb_data = data;
	size_t realsize;
	char *header;
	char *value;

	realsize = size * nitems;

	if (realsize > MAX_HEADER_LENGTH) {
		ast_log(LOG_WARNING, "CURL header length is too large (size: '%zu' | max: '%d')\n",
			realsize, MAX_HEADER_LENGTH);
		return 0;
	}

	header = ast_alloca(realsize + 1);
	memcpy(header, buffer, realsize);
	header[realsize] = '\0';
	value = strchr(header, ':');
	if (!value) {
		return realsize;
	}
	*value++ = '\0';
	value = ast_trim_blanks(ast_skip_blanks(value));

	if (!strcasecmp(header, "Cache-Control")) {
		cb_data->cache_control = ast_strdup(value);
	} else if (!strcasecmp(header, "Expires")) {
		cb_data->expires = ast_strdup(value);
	}

	return realsize;
}

/*!
 * \brief Prepare a CURL instance to use
 *
 * \param data The CURL callback data
 *
 * \retval NULL on failure
 * \retval CURL instance on success
 */
static CURL *get_curl_instance(struct curl_cb_data *data)
{
	CURL *curl;
	struct stir_shaken_general *cfg;
	unsigned int curl_timeout;

	cfg = stir_shaken_general_get();
	curl_timeout = ast_stir_shaken_curl_timeout(cfg);
	ao2_cleanup(cfg);

	curl = curl_easy_init();
	if (!curl) {
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, curl_timeout);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, GLOBAL_USERAGENT);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_callback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, data);

	return curl;
}

int curl_public_key(const char *public_key_url, const char *path, struct curl_cb_data *data)
{
	FILE *public_key_file;
	long http_code;
	CURL *curl;
	char curl_errbuf[CURL_ERROR_SIZE + 1];
	char hash[41];

	ast_sha1_hash(hash, public_key_url);

	curl_errbuf[CURL_ERROR_SIZE] = '\0';

	public_key_file = fopen(path, "wb");
	if (!public_key_file) {
		ast_log(LOG_ERROR, "Failed to open file '%s' to write public key from '%s': %s (%d)\n",
			path, public_key_url, strerror(errno), errno);
		return -1;
	}

	curl = get_curl_instance(data);
	if (!curl) {
		ast_log(LOG_ERROR, "Failed to set up CURL isntance for '%s'\n", public_key_url);
		fclose(public_key_file);
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_URL, public_key_url);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, public_key_file);
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);

	if (curl_easy_perform(curl)) {
		ast_log(LOG_ERROR, "%s\n", curl_errbuf);
		curl_easy_cleanup(curl);
		fclose(public_key_file);
		return -1;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	curl_easy_cleanup(curl);
	fclose(public_key_file);

	if (http_code / 100 != 2) {
		ast_log(LOG_ERROR, "Failed to retrieve URL '%s': code %ld\n", public_key_url, http_code);
		return -1;
	}

	return 0;
}
