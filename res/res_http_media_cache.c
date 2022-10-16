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

/*** DOCUMENTATION
	<configInfo name="res_http_media_cache" language="en_US">
		<synopsis>HTTP media cache</synopsis>
		<configFile name="http_media_cache.conf">
			<configObject name="general">
				<synopsis>General configuration</synopsis>
				<configOption name="timeout_secs" default="180">
					<synopsis>The maximum time the transfer is allowed to complete in seconds. See https://curl.se/libcurl/c/CURLOPT_TIMEOUT.html for details.</synopsis>
				</configOption>
				<configOption name="user_agent">
					<synopsis>The HTTP User-Agent to use for requests. See https://curl.se/libcurl/c/CURLOPT_USERAGENT.html for details.</synopsis>
				</configOption>
				<configOption name="follow_location" default="1">
					<synopsis>Follow HTTP 3xx redirects on requests. See https://curl.se/libcurl/c/CURLOPT_FOLLOWLOCATION.html for details.</synopsis>
				</configOption>
				<configOption name="max_redirects" default="8">
					<synopsis>The maximum number of redirects to follow. See https://curl.se/libcurl/c/CURLOPT_MAXREDIRS.html for details.</synopsis>
				</configOption>
				<configOption name="proxy">
					<synopsis>The proxy to use for requests. See https://curl.se/libcurl/c/CURLOPT_PROXY.html for details.</synopsis>
				</configOption>
				<configOption name="protocols">
					<synopsis>The comma separated list of allowed protocols for the request. Available with cURL 7.85.0 or later. See https://curl.se/libcurl/c/CURLOPT_PROTOCOLS_STR.html for details.</synopsis>
				</configOption>
				<configOption name="redirect_protocols">
					<synopsis>The comma separated list of allowed protocols for redirects. Available with cURL 7.85.0 or later. See https://curl.se/libcurl/c/CURLOPT_REDIR_PROTOCOLS_STR.html for details.</synopsis>
				</configOption>
				<configOption name="dns_cache_timeout_secs" default="60">
					<synopsis>The life-time for DNS cache entries. See https://curl.se/libcurl/c/CURLOPT_DNS_CACHE_TIMEOUT.html for details.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

#include "asterisk.h"

#include <curl/curl.h>

#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/bucket.h"
#include "asterisk/sorcery.h"
#include "asterisk/threadstorage.h"
#include "asterisk/uri.h"

#define MAX_HEADER_LENGTH 1023

#ifdef CURL_AT_LEAST_VERSION
#if CURL_AT_LEAST_VERSION(7, 85, 0)
#define AST_CURL_HAS_PROTOCOLS_STR 1
#endif
#endif

static int http_media_cache_config_pre_apply(void);

/*! \brief General configuration options for http media cache. */
struct conf_general_options {
	/*! \brief Request timeout to use */
	int curl_timeout;

	/*! \brief Follow 3xx redirects automatically. */
	int curl_followlocation;

	/*! \brief Number of redirects to follow for one request. */
	int curl_maxredirs;

	/*! \brief Life-time of CURL DNS cache entries. */
	int curl_dns_cache_timeout;

	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(curl_useragent); /*! \brief User-agent to use for requests. */
		AST_STRING_FIELD(curl_proxy); /*! \brief Proxy to use for requests. None by default. */
		AST_STRING_FIELD(curl_protocols); /*! \brief Allowed protocols to use for requests. All by default. */
		AST_STRING_FIELD(curl_redir_protocols); /*! \brief Allowed protocols to use on redirect. All by default. */
	);
};

/*! \brief All configuration options for http media cache. */
struct conf {
	/*! The general section configuration options. */
	struct conf_general_options *general;
};

/*! \brief Locking container for safe configuration access. */
static AO2_GLOBAL_OBJ_STATIC(confs);

/*! \brief Mapping of the http media cache conf struct's general to the general context in the config file. */
static struct aco_type general_option = {
	.type = ACO_GLOBAL,
	.name = "general",
	.item_offset = offsetof(struct conf, general),
	.category = "general",
	.category_match = ACO_WHITELIST_EXACT,
};

static struct aco_type *general_options[] = ACO_TYPES(&general_option);

/*! \brief Disposes of the http media cache conf object */
static void conf_destructor(void *obj)
{
	struct conf *cfg = obj;
	ast_string_field_free_memory(cfg->general);
	ao2_cleanup(cfg->general);
}

/*! \brief Creates the http media cache conf object. */
static void *conf_alloc(void)
{
	struct conf *cfg;

	if (!(cfg = ao2_alloc(sizeof(*cfg), conf_destructor))) {
		return NULL;
	}

	if (!(cfg->general = ao2_alloc(sizeof(*cfg->general), NULL))) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	if (ast_string_field_init(cfg->general, 256)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

/*! \brief The conf file that's processed for the module. */
static struct aco_file conf_file = {
	/*! The config file name. */
	.filename = "res_http_media_cache.conf",
	/*! The mapping object types to be processed. */
	.types = ACO_TYPES(&general_option),
};

CONFIG_INFO_STANDARD(cfg_info, confs, conf_alloc,
		.pre_apply_config = http_media_cache_config_pre_apply,
		.files = ACO_FILES(&conf_file));

/*!
 * \brief Pre-apply callback for the config framework.
 *
 * This validates that used options match the ones supported by CURL.
 */
static int http_media_cache_config_pre_apply(void)
{
#ifndef AST_CURL_HAS_PROTOCOLS_STR
	struct conf *cfg = aco_pending_config(&cfg_info);

	if (!ast_strlen_zero(cfg->general->curl_protocols)) {
		ast_log(AST_LOG_ERROR, "'protocols' not supported by linked CURL library. Please recompile against newer CURL.\n");
		return -1;
	}

	if (!ast_strlen_zero(cfg->general->curl_redir_protocols)) {
		ast_log(AST_LOG_ERROR, "'redirect_protocols' not supported by linked CURL library. Please recompile against newer CURL.\n");
		return -1;
	}
#endif

	return 0;
}


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
		&& strcasecmp(header, "Content-Type")
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
	char time_buf[32], secs[AST_TIME_T_LEN];
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
	ast_time_t_to_string(actual_expires.tv_sec, secs, sizeof(secs));
	snprintf(time_buf, sizeof(time_buf), "%30s", secs);

	ast_bucket_file_metadata_set(bucket_file, "__actual_expires", time_buf);
}

static char *file_extension_from_string(const char *str, char *buffer, size_t capacity)
{
	const char *ext;

	ext = strrchr(str, '.');
	if (ext && ast_get_format_for_file_ext(ext + 1)) {
		ast_debug(3, "Found extension '%s' at end of string\n", ext);
		ast_copy_string(buffer, ext, capacity);
		return buffer;
	}

	return NULL;
}

/*!
 * \internal
 * \brief Normalize the value of a Content-Type header
 *
 * This will trim off any optional parameters after the type/subtype.
 *
 * \return 0 if no normalization occurred, otherwise true (non-zero)
 */
static int normalize_content_type_header(char *content_type)
{
	char *params = strchr(content_type, ';');

	if (params) {
		*params-- = 0;
		while (params > content_type && (*params == ' ' || *params == '\t')) {
			*params-- = 0;
		}
		return 1;
	}

	return 0;
}

static int derive_extension_from_mime_type(const char *mime_type, char *buffer, size_t capacity)
{
	int res = 0;

	/* Compare the provided Content-Type directly, parameters and all */
	res = ast_get_extension_for_mime_type(mime_type, buffer, sizeof(buffer));
	if (!res) {
		char *m = ast_strdupa(mime_type);
		/* Strip MIME type parameters and then check */
		if (normalize_content_type_header(m)) {
			res = ast_get_extension_for_mime_type(m, buffer, sizeof(buffer));
		}
	}

	return res;
}

static char *file_extension_from_content_type(struct ast_bucket_file *bucket_file, char *buffer, size_t capacity)
{
	/* Check for the extension based on the MIME type passed in the Content-Type
	 * header.
	 *
	 * If a match is found then retrieve the extension from the supported list
	 * corresponding to the mime-type and use that to rename the file */

	struct ast_bucket_metadata *header;

	header = ast_bucket_file_metadata_get(bucket_file, "content-type");
	if (!header) {
		return NULL;
	}

	if (derive_extension_from_mime_type(header->value, buffer, capacity)) {
		ast_debug(3, "Derived extension '%s' from MIME type %s\n",
			buffer,
			header->value);
		ao2_ref(header, -1);
		return buffer;
	}

	ao2_ref(header, -1);

	return NULL;
}

static char *file_extension_from_url_path(struct ast_bucket_file *bucket_file, char *buffer, size_t capacity)
{
	const char *path;
	struct ast_uri *uri;

	uri = ast_uri_parse(ast_sorcery_object_get_id(bucket_file));
	if (!uri) {
		ast_log(LOG_ERROR, "Failed to parse URI: %s\n",
			ast_sorcery_object_get_id(bucket_file));
		return NULL;
	}

	path = ast_uri_path(uri);
	if (!path) {
		ao2_cleanup(uri);
		return NULL;
	}

	/* Just parse it as a string like before, but without the extra cruft */
	buffer = file_extension_from_string(path, buffer, capacity);
	ao2_cleanup(uri);
	return buffer;
}

static void bucket_file_set_extension(struct ast_bucket_file *bucket_file)
{
	/* Using Content-Type first allows for the most flexibility for whomever
	 * is serving up the audio file. If that doesn't turn up anything useful
	 * we'll try to parse the URL and use the extension */

	char buffer[64];

	if (file_extension_from_content_type(bucket_file, buffer, sizeof(buffer))
	   || file_extension_from_url_path(bucket_file, buffer, sizeof(buffer))) {
		ast_bucket_file_metadata_set(bucket_file, "ext", buffer);
	}
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

	if ((expires.tv_sec = ast_string_to_time_t(metadata->value)) == -1) {
		return 1;
	}

	return ast_tvcmp(current_time, expires) == -1 ? 0 : 1;
}

/*!
 * \internal \brief Obtain a CURL handle with common setup options
 */
static CURL *get_curl_instance(struct curl_bucket_file_data *cb_data)
{
	RAII_VAR(struct conf *, cfg, ao2_global_obj_ref(confs), ao2_cleanup);
	CURLcode rc;
	CURL *curl;

	curl = curl_easy_init();
	if (!curl) {
		return NULL;
	}

	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_callback);
	curl_easy_setopt(curl, CURLOPT_URL, ast_sorcery_object_get_id(cb_data->bucket_file));
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, cb_data);

	curl_easy_setopt(curl, CURLOPT_TIMEOUT, cfg->general->curl_timeout);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, cfg->general->curl_useragent);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, cfg->general->curl_followlocation ? 1 : 0);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, cfg->general->curl_maxredirs);

	if (!ast_strlen_zero(cfg->general->curl_proxy)) {
		curl_easy_setopt(curl, CURLOPT_PROXY, cfg->general->curl_proxy);
	}

	if (!ast_strlen_zero(cfg->general->curl_protocols)) {
#ifdef AST_CURL_HAS_PROTOCOLS_STR
		CURLcode rc = curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, cfg->general->curl_protocols);
		if (rc != CURLE_OK) {
			ast_log(AST_LOG_ERROR, "Setting protocols to '%s' failed: %d\n", cfg->general->curl_protocols, rc);
			curl_easy_cleanup(curl);
			return NULL;
		}
#endif
	}
	if (!ast_strlen_zero(cfg->general->curl_redir_protocols)) {
#ifdef AST_CURL_HAS_PROTOCOLS_STR
		CURLcode rc = curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, cfg->general->curl_redir_protocols);
		if (rc != CURLE_OK) {
			ast_log(AST_LOG_ERROR, "Setting redirect_protocols to '%s' failed: %d\n", cfg->general->curl_redir_protocols, rc);
			curl_easy_cleanup(curl);
			return NULL;
		}
#endif
	}

	rc = curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, cfg->general->curl_dns_cache_timeout);
	if (rc != CURLE_OK) {
		ast_log(AST_LOG_ERROR, "Setting dns_cache_timeout to '%d' failed: %d\n", cfg->general->curl_dns_cache_timeout, rc);
		curl_easy_cleanup(curl);
		return NULL;
	}

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
		bucket_file_set_extension(bucket_file);
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
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(confs);
	return 0;
}

static int load_module(void)
{
	if (aco_info_init(&cfg_info)) {
		aco_info_destroy(&cfg_info);
		return AST_MODULE_LOAD_DECLINE;
	}


	aco_option_register(&cfg_info, "timeout_secs", ACO_EXACT, general_options,
			"180", OPT_INT_T, 0,
			FLDSET(struct conf_general_options, curl_timeout));

	aco_option_register(&cfg_info, "user_agent", ACO_EXACT, general_options,
			AST_CURL_USER_AGENT, OPT_STRINGFIELD_T, 0,
			STRFLDSET(struct conf_general_options, curl_useragent));

	aco_option_register(&cfg_info, "follow_location", ACO_EXACT, general_options,
			"yes", OPT_BOOL_T, 1,
			FLDSET(struct conf_general_options, curl_followlocation));

	aco_option_register(&cfg_info, "max_redirects", ACO_EXACT, general_options,
			"8", OPT_INT_T, 0,
			FLDSET(struct conf_general_options, curl_maxredirs));

	aco_option_register(&cfg_info, "proxy", ACO_EXACT, general_options,
			NULL, OPT_STRINGFIELD_T, 1,
			STRFLDSET(struct conf_general_options, curl_proxy));

	aco_option_register(&cfg_info, "dns_cache_timeout_secs", ACO_EXACT, general_options,
			"60", OPT_INT_T, 0,
			FLDSET(struct conf_general_options, curl_dns_cache_timeout));

	aco_option_register(&cfg_info, "protocols", ACO_EXACT, general_options,
			NULL, OPT_STRINGFIELD_T, 1,
			STRFLDSET(struct conf_general_options, curl_protocols));

	aco_option_register(&cfg_info, "redirect_protocols", ACO_EXACT, general_options,
			NULL, OPT_STRINGFIELD_T, 1,
			STRFLDSET(struct conf_general_options, curl_redir_protocols));


	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		struct conf *cfg;

		ast_log(LOG_NOTICE, "Could not load res_http_media_cache config; using defaults\n");
		cfg = conf_alloc();
		if (!cfg) {
			aco_info_destroy(&cfg_info);
			return AST_MODULE_LOAD_DECLINE;
		}

		if (aco_set_defaults(&general_option, "general", cfg->general)) {
			ast_log(LOG_ERROR, "Failed to initialize res_http_media_cache defaults.\n");
			ao2_ref(cfg, -1);
			aco_info_destroy(&cfg_info);
			return AST_MODULE_LOAD_DECLINE;
		}

		ao2_global_obj_replace_unref(confs, cfg);
		ao2_ref(cfg, -1);
	}

	if (ast_bucket_scheme_register("http", &http_bucket_wizard, &http_bucket_file_wizard,
			NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register Bucket HTTP wizard scheme implementation\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_bucket_scheme_register("https", &https_bucket_wizard, &https_bucket_file_wizard,
			NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register Bucket HTTPS wizard scheme implementation\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "HTTP Media Cache Backend",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.requires = "res_curl",
	);
