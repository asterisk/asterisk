/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Matt Jordan
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief
 *
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * HTTP backend for the core media cache
 */

/*** MODULEINFO
	<depend>curl</depend>
	<depend>res_curl</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <curl/curl.h>

#include "asterisk/module.h"
#include "asterisk/bucket.h"
#include "asterisk/sorcery.h"
#include "asterisk/threadstorage.h"

#define GLOBAL_USERAGENT "asterisk-libcurl-agent/1.0"

#define MAX_HEADER_LENGTH 1023

/*! \brief Data passed to cURL callbacks */
struct curl_bucket_file_data {
	/*! The \c ast_bucket_file object that caused the operation */
	struct ast_bucket_file *bucket_file;
	/*! File to write data to */
	FILE *out_file;
};

/*!
 * \internal \brief The cURL header callback function
 */
static size_t curl_header_callback(char *buffer, size_t size, size_t nitems, void *data)
{
	struct curl_bucket_file_data *cb_data = data;
	size_t realsize;
	char *value;
	char *header;

	realsize = size * nitems;

	if (realsize > MAX_HEADER_LENGTH) {
		ast_log(LOG_WARNING, "cURL header length of '%zu' is too large: max %d\n",
			realsize, MAX_HEADER_LENGTH);
		return 0;
	}

	/* buffer may not be NULL terminated */
	header = ast_alloca(realsize + 1);
	memcpy(header, buffer, realsize);
	header[realsize] = '\0';
	value = strchr(header, ':');
	if (!value) {
		/* Not a header we care about; bail */
		return realsize;
	}
	*value++ = '\0';

	if (strcasecmp(header, "ETag")
		&& strcasecmp(header, "Cache-Control")
		&& strcasecmp(header, "Last-Modified")
		&& strcasecmp(header, "Expires")) {
		return realsize;
	}

	value = ast_trim_blanks(ast_skip_blanks(value));
	header = ast_str_to_lower(header);

	ast_bucket_file_metadata_set(cb_data->bucket_file, header, value);

	return realsize;
}

/*!
 * \internal \brief The cURL body callback function
 */
static size_t curl_body_callback(void *ptr, size_t size, size_t nitems, void *data)
{
	struct curl_bucket_file_data *cb_data = data;
	size_t realsize;

	realsize = fwrite(ptr, size, nitems, cb_data->out_file);

	return realsize;
}

/*!
 * \internal \brief Set the expiration metadata on the bucket file based on HTTP caching rules
 */
static void bucket_file_set_expiration(struct ast_bucket_file *bucket_file)
{
	struct ast_bucket_metadata *metadata;
	char time_buf[32];
	struct timeval actual_expires = ast_tvnow();

	metadata = ast_bucket_file_metadata_get(bucket_file, "cache-control");
	if (metadata) {
		char *str_max_age;

		str_max_age = strstr(metadata->value, "s-maxage");
		if (!str_max_age) {
			str_max_age = strstr(metadata->value, "max-age");
		}

		if (str_max_age) {
			unsigned int max_age;
			char *equal = strchr(str_max_age, '=');
			if (equal && (sscanf(equal + 1, "%30u", &max_age) == 1)) {
				actual_expires.tv_sec += max_age;
			}
		}
		ao2_ref(metadata, -1);
	} else {
		metadata = ast_bucket_file_metadata_get(bucket_file, "expires");
		if (metadata) {
			struct tm expires_time;

			strptime(metadata->value, "%a, %d %b %Y %T %z", &expires_time);
			expires_time.tm_isdst = -1;
			actual_expires.tv_sec = mktime(&expires_time);

			ao2_ref(metadata, -1);
		}
	}

	/* Use 'now' if we didn't get an expiration time */
	snprintf(time_buf, sizeof(time_buf), "%30lu", actual_expires.tv_sec);

	ast_bucket_file_metadata_set(bucket_file, "__actual_expires", time_buf);
}

/*! \internal
 * \brief Return whether or not we should always revalidate against the server
 */
static int bucket_file_always_revalidate(struct ast_bucket_file *bucket_file)
{
	RAII_VAR(struct ast_bucket_metadata *, metadata,
		ast_bucket_file_metadata_get(bucket_file, "cache-control"),
		ao2_cleanup);

	if (!metadata) {
		return 0;
	}

	if (strstr(metadata->value, "no-cache")
		|| strstr(metadata->value, "must-revalidate")) {
		return 1;
	}

	return 0;
}

/*! \internal
 * \brief Return whether or not the item has expired
 */
static int bucket_file_expired(struct ast_bucket_file *bucket_file)
{
	RAII_VAR(struct ast_bucket_metadata *, metadata,
		ast_bucket_file_metadata_get(bucket_file, "__actual_expires"),
		ao2_cleanup);
	struct timeval current_time = ast_tvnow();
	struct timeval expires = { .tv_sec = 0, .tv_usec = 0 };

	if (!metadata) {
		return 1;
	}

	if (sscanf(metadata->value, "%lu", &expires.tv_sec) != 1) {
		return 1;
	}

	return ast_tvcmp(current_time, expires) == -1 ? 0 : 1;
}

/*!
 * \internal \brief Obtain a CURL handle with common setup options
 */
static CURL *get_curl_instance(struct curl_bucket_file_data *cb_data)
{
	CURL *curl;

	curl = curl_easy_init();
	if (!curl) {
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_callback);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, GLOBAL_USERAGENT);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_URL, ast_sorcery_object_get_id(cb_data->bucket_file));
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, cb_data);

	return curl;
}

/*!
 * \brief Execute the CURL
 */
static long execute_curl_instance(CURL *curl)
{
	char curl_errbuf[CURL_ERROR_SIZE + 1];
	long http_code;

	curl_errbuf[CURL_ERROR_SIZE] = '\0';
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);

	if (curl_easy_perform(curl)) {
		ast_log(LOG_WARNING, "%s\n", curl_errbuf);
		return -1;
	}

	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	curl_easy_cleanup(curl);

	return http_code;
}

/*!
 * \internal \brief CURL the URI specified by the bucket_file and store it in the provided path
 */
static int bucket_file_run_curl(struct ast_bucket_file *bucket_file)
{
	struct curl_bucket_file_data cb_data = {
		.bucket_file = bucket_file,
	};
	long http_code;
	CURL *curl;

	cb_data.out_file = fopen(bucket_file->path, "wb");
	if (!cb_data.out_file) {
		ast_log(LOG_WARNING, "Failed to open file '%s' for writing: %s (%d)\n",
			bucket_file->path, strerror(errno), errno);
		return -1;
	}

	curl = get_curl_instance(&cb_data);
	if (!curl) {
		fclose(cb_data.out_file);
		return -1;
	}

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_body_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&cb_data);

	http_code = execute_curl_instance(curl);

	fclose(cb_data.out_file);

	if (http_code / 100 == 2) {
		bucket_file_set_expiration(bucket_file);
		return 0;
	} else {
		ast_log(LOG_WARNING, "Failed to retrieve URL '%s': server returned %ld\n",
			ast_sorcery_object_get_id(bucket_file), http_code);
	}

	return -1;
}

static int bucket_http_wizard_is_stale(const struct ast_sorcery *sorcery, void *data, void *object)
{
	struct ast_bucket_file *bucket_file = object;
	struct ast_bucket_metadata *metadata;
	struct curl_slist *header_list = NULL;
	long http_code;
	CURL *curl;
	struct curl_bucket_file_data cb_data = {
		.bucket_file = bucket_file
	};
	char etag_buf[256];

	if (!bucket_file_expired(bucket_file) && !bucket_file_always_revalidate(bucket_file)) {
		return 0;
	}

	/* See if we have an ETag for this item. If not, it's stale. */
	metadata = ast_bucket_file_metadata_get(bucket_file, "etag");
	if (!metadata) {
		return 1;
	}

	curl = get_curl_instance(&cb_data);

	/* Set the ETag header on our outgoing request */
	snprintf(etag_buf, sizeof(etag_buf), "If-None-Match: %s", metadata->value);
	header_list = curl_slist_append(header_list, etag_buf);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	ao2_ref(metadata, -1);

	http_code = execute_curl_instance(curl);

	curl_slist_free_all(header_list);

	if (http_code == 304) {
		bucket_file_set_expiration(bucket_file);
		return 0;
	}

	return 1;
}

static int bucket_http_wizard_create(const struct ast_sorcery *sorcery, void *data,
	void *object)
{
	struct ast_bucket_file *bucket_file = object;

	return bucket_file_run_curl(bucket_file);
}

static void *bucket_http_wizard_retrieve_id(const struct ast_sorcery *sorcery,
	void *data, const char *type, const char *id)
{
	struct ast_bucket_file *bucket_file;

	if (strcmp(type, "file")) {
		ast_log(LOG_WARNING, "Failed to create storage: invalid bucket type '%s'\n", type);
		return NULL;
	}

	if (ast_strlen_zero(id)) {
		ast_log(LOG_WARNING, "Failed to create storage: no URI\n");
		return NULL;
	}

	bucket_file = ast_bucket_file_alloc(id);
	if (!bucket_file) {
		ast_log(LOG_WARNING, "Failed to create storage for '%s'\n", id);
		return NULL;
	}

	if (ast_bucket_file_temporary_create(bucket_file)) {
		ast_log(LOG_WARNING, "Failed to create temporary storage for '%s'\n", id);
		ast_sorcery_delete(sorcery, bucket_file);
		ao2_ref(bucket_file, -1);
		return NULL;
	}

	if (bucket_file_run_curl(bucket_file)) {
		ast_sorcery_delete(sorcery, bucket_file);
		ao2_ref(bucket_file, -1);
		return NULL;
	}

	return bucket_file;
}

static int bucket_http_wizard_delete(const struct ast_sorcery *sorcery, void *data,
	void *object)
{
	struct ast_bucket_file *bucket_file = object;

	unlink(bucket_file->path);

	return 0;
}

static struct ast_sorcery_wizard http_bucket_wizard = {
	.name = "http",
	.create = bucket_http_wizard_create,
	.retrieve_id = bucket_http_wizard_retrieve_id,
	.delete = bucket_http_wizard_delete,
	.is_stale = bucket_http_wizard_is_stale,
};

static struct ast_sorcery_wizard http_bucket_file_wizard = {
	.name = "http",
	.create = bucket_http_wizard_create,
	.retrieve_id = bucket_http_wizard_retrieve_id,
	.delete = bucket_http_wizard_delete,
	.is_stale = bucket_http_wizard_is_stale,
};

static struct ast_sorcery_wizard https_bucket_wizard = {
	.name = "https",
	.create = bucket_http_wizard_create,
	.retrieve_id = bucket_http_wizard_retrieve_id,
	.delete = bucket_http_wizard_delete,
	.is_stale = bucket_http_wizard_is_stale,
};

static struct ast_sorcery_wizard https_bucket_file_wizard = {
	.name = "https",
	.create = bucket_http_wizard_create,
	.retrieve_id = bucket_http_wizard_retrieve_id,
	.delete = bucket_http_wizard_delete,
	.is_stale = bucket_http_wizard_is_stale,
};

static int unload_module(void)
{
	return 0;
}

static int load_module(void)
{
	if (ast_bucket_scheme_register("http", &http_bucket_wizard, &http_bucket_file_wizard,
			NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register Bucket HTTP wizard scheme implementation\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_bucket_scheme_register("https", &https_bucket_wizard, &https_bucket_file_wizard,
			NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register Bucket HTTPS wizard scheme implementation\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "HTTP Media Cache Backend",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_DEFAULT,
	);
