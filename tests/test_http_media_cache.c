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
 * \brief Tests for the HTTP media cache backend
 *
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>curl</depend>
	<depend>res_http_media_cache</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <fcntl.h>

#include "asterisk/module.h"
#include "asterisk/http.h"
#include "asterisk/bucket.h"
#include "asterisk/test.h"

#define CATEGORY "/res/http_media_cache/"

#define TEST_URI "test_media_cache"

struct test_options {
	int status_code;
	int send_file;
	struct {
		int s_maxage;
		int maxage;
		int no_cache;
		int must_revalidate;
	} cache_control;
	struct timeval expires;
	const char *status_text;
	const char *etag;
};

static struct test_options options;

static char server_uri[512];

#define VALIDATE_EXPIRES(test, bucket_file, expected, delta) do { \
	RAII_VAR(struct ast_bucket_metadata *, metadata, ast_bucket_file_metadata_get((bucket_file), "__actual_expires"), ao2_cleanup); \
	int actual_expires; \
	ast_test_validate(test, metadata != NULL); \
	ast_test_validate(test, sscanf(metadata->value, "%d", &actual_expires) == 1); \
	ast_test_status_update(test, "Checking %d >= %d and %d <= %d\n", \
			(int) ((expected) + (delta)), actual_expires, \
			(int) ((expected) - (delta)), actual_expires); \
	ast_test_validate(test, (((expected) + (delta) >= actual_expires) && ((expected) - (delta) <= actual_expires))); \
} while (0)

#define VALIDATE_STR_METADATA(test, bucket_file, key, expected) do { \
	RAII_VAR(struct ast_bucket_metadata *, metadata, ast_bucket_file_metadata_get((bucket_file), (key)), ao2_cleanup); \
	ast_test_validate(test, metadata != NULL); \
	ast_test_validate(test, !strcmp(metadata->value, (expected))); \
} while (0)

#define SET_OR_APPEND_CACHE_CONTROL(str) do { \
	if (!ast_str_strlen((str))) { \
		ast_str_set(&(str), 0, "%s", "cache-control: "); \
	} else { \
		ast_str_append(&(str), 0, "%s", ", "); \
	} \
} while (0)

static int http_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_params, struct ast_variable *headers)
{
	char file_name[64] = "/tmp/test-media-cache-XXXXXX";
	struct ast_str *http_header = ast_str_create(128);
	struct ast_str *cache_control = ast_str_create(128);
	int fd = -1;
	int unmodified = 0;
	int send_file = options.send_file && method == AST_HTTP_GET;

	if (!http_header) {
		goto error;
	}

	if (send_file) {
		char buf[1024];

		fd = mkstemp(file_name);
		if (fd == -1) {
			ast_log(LOG_ERROR, "Unable to open temp file for testing: %s (%d)", strerror(errno), errno);
			goto error;
		}

		memset(buf, 1, sizeof(buf));
		if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
			ast_log(LOG_ERROR, "Failed to write expected number of bytes to pipe\n");
			close(fd);
			goto error;
		}
		close(fd);

		fd = open(file_name, 0);
		if (fd == -1) {
			ast_log(LOG_ERROR, "Unable to open temp file for testing: %s (%d)", strerror(errno), errno);
			goto error;
		}
	}

	if (options.cache_control.maxage) {
		SET_OR_APPEND_CACHE_CONTROL(cache_control);
		ast_str_append(&cache_control, 0, "max-age=%d", options.cache_control.maxage);
	}

	if (options.cache_control.s_maxage) {
		SET_OR_APPEND_CACHE_CONTROL(cache_control);
		ast_str_append(&cache_control, 0, "s-maxage=%d", options.cache_control.s_maxage);
	}

	if (options.cache_control.no_cache) {
		SET_OR_APPEND_CACHE_CONTROL(cache_control);
		ast_str_append(&cache_control, 0, "%s", "no-cache");
	}

	if (options.cache_control.must_revalidate) {
		SET_OR_APPEND_CACHE_CONTROL(cache_control);
		ast_str_append(&cache_control, 0, "%s", "must-revalidate");
	}

	if (ast_str_strlen(cache_control)) {
		ast_str_append(&http_header, 0, "%s\r\n", ast_str_buffer(cache_control));
	}

	if (options.expires.tv_sec) {
		struct ast_tm now_time;
		char tmbuf[64];

		ast_localtime(&options.expires, &now_time, NULL);
		ast_strftime(tmbuf, sizeof(tmbuf), "%a, %d %b %Y %T %z", &now_time);
		ast_str_append(&http_header, 0, "Expires: %s\r\n", tmbuf);
	}

	if (!ast_strlen_zero(options.etag)) {
		struct ast_variable *v;

		ast_str_append(&http_header, 0, "ETag: %s\r\n", options.etag);
		for (v = headers; v; v = v->next) {
			if (!strcasecmp(v->name, "If-None-Match") && !strcasecmp(v->value, options.etag)) {
				unmodified = 1;
				break;
			}
		}
	}

	if (!unmodified) {
		ast_http_send(ser, method, options.status_code, options.status_text, http_header, NULL, send_file ? fd : 0, 1);
	} else {
		ast_http_send(ser, method, 304, "Not Modified", http_header, NULL, 0, 1);
	}

	if (send_file) {
		close(fd);
		unlink(file_name);
	}

	ast_free(cache_control);

	return 0;

error:
	ast_free(http_header);
	ast_free(cache_control);
	ast_http_request_close_on_completion(ser);
	ast_http_error(ser, 418, "I'm a Teapot", "Please don't ask me to brew coffee.");

	return 0;
}

static struct ast_http_uri test_uri = {
	.description = "HTTP Media Cache Test URI",
	.uri = TEST_URI,
	.callback = http_callback,
	.has_subtree = 1,
	.data = NULL,
	.key = __FILE__,
};

static int pre_test_cb(struct ast_test_info *info, struct ast_test *test)
{
	memset(&options, 0, sizeof(options));

	return 0;
}

static void bucket_file_cleanup(void *obj)
{
	struct ast_bucket_file *bucket_file = obj;

	if (bucket_file) {
		ast_bucket_file_delete(bucket_file);
		ao2_ref(bucket_file, -1);
	}
}

AST_TEST_DEFINE(retrieve_cache_control_directives)
{
	RAII_VAR(struct ast_bucket_file *, bucket_file, NULL, bucket_file_cleanup);
	struct timeval now = ast_tvnow();
	char uri[1024];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test retrieval of a resource with Cache-Control directives that affect staleness";
		info->description =
			"This test covers retrieval of a resource with the Cache-Control header,\n"
			"which specifies no-cache and/or must-revalidate.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	snprintf(uri, sizeof(uri), "%s/%s", server_uri, "foo.wav");

	options.send_file = 1;
	options.status_code = 200;
	options.status_text = "OK";

	ast_test_status_update(test, "Testing no-cache...\n");
	options.cache_control.no_cache = 1;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 1);
	bucket_file_cleanup(bucket_file);

	ast_test_status_update(test, "Testing no-cache with ETag...\n");
	options.cache_control.no_cache = 1;
	options.etag = "123456789";
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);
	bucket_file_cleanup(bucket_file);

	options.etag = NULL;

	ast_test_status_update(test, "Testing no-cache with max-age...\n");
	options.cache_control.no_cache = 1;
	options.cache_control.maxage = 300;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 300, 3);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 1);
	bucket_file_cleanup(bucket_file);

	options.cache_control.maxage = 0;
	options.cache_control.no_cache = 0;

	ast_test_status_update(test, "Testing must-revalidate...\n");
	options.cache_control.must_revalidate = 1;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 1);
	bucket_file_cleanup(bucket_file);

	ast_test_status_update(test, "Testing must-revalidate with ETag...\n");
	options.cache_control.must_revalidate = 1;
	options.etag = "123456789";
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);
	bucket_file_cleanup(bucket_file);

	options.etag = NULL;

	ast_test_status_update(test, "Testing must-revalidate with max-age...\n");
	options.cache_control.must_revalidate = 1;
	options.cache_control.maxage = 300;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 300, 3);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(retrieve_cache_control_age)
{
	RAII_VAR(struct ast_bucket_file *, bucket_file, NULL, bucket_file_cleanup);
	struct timeval now = ast_tvnow();
	char uri[1024];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test retrieval of a resource with age specifiers in Cache-Control";
		info->description =
			"This test covers retrieval of a resource with the Cache-Control header,\n"
			"which specifies max-age and/or s-maxage. The test verifies proper precedence\n"
			"ordering of the header attributes, along with its relation if the Expires\n"
			"header is present.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	snprintf(uri, sizeof(uri), "%s/%s", server_uri, "foo.wav");

	options.send_file = 1;
	options.status_code = 200;
	options.status_text = "OK";

	ast_test_status_update(test, "Testing max-age...\n");
	options.cache_control.maxage = 300;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 300, 3);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);
	bucket_file_cleanup(bucket_file);

	ast_test_status_update(test, "Testing s-maxage...\n");
	now = ast_tvnow();
	options.cache_control.maxage = 0;
	options.cache_control.s_maxage = 300;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 300, 3);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);
	bucket_file_cleanup(bucket_file);

	ast_test_status_update(test, "Testing max-age and s-maxage...\n");
	now = ast_tvnow();
	options.cache_control.maxage = 300;
	options.cache_control.s_maxage = 600;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 600, 3);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);
	bucket_file_cleanup(bucket_file);

	ast_test_status_update(test, "Testing max-age and Expires...\n");
	now = ast_tvnow();
	options.cache_control.maxage = 300;
	options.cache_control.s_maxage = 0;
	options.expires.tv_sec = now.tv_sec + 3000;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 300, 3);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);
	bucket_file_cleanup(bucket_file);

	ast_test_status_update(test, "Testing s-maxage and Expires...\n");
	now = ast_tvnow();
	options.cache_control.maxage = 0;
	options.cache_control.s_maxage = 300;
	options.expires.tv_sec = now.tv_sec + 3000;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 300, 3);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);
	bucket_file_cleanup(bucket_file);

	ast_test_status_update(test, "Testing s-maxage and Expires...\n");
	now = ast_tvnow();
	options.cache_control.maxage = 0;
	options.cache_control.s_maxage = 300;
	options.expires.tv_sec = now.tv_sec + 3000;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 300, 3);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);
	bucket_file_cleanup(bucket_file);

	ast_test_status_update(test, "Testing max-age, s-maxage, and Expires...\n");
	now = ast_tvnow();
	options.cache_control.maxage = 300;
	options.cache_control.s_maxage = 600;
	options.expires.tv_sec = now.tv_sec + 3000;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 600, 3);
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(retrieve_etag_expired)
{
	RAII_VAR(struct ast_bucket_file *, bucket_file, NULL, bucket_file_cleanup);
	struct timeval now = ast_tvnow();
	char uri[1024];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test retrieval of an expired resource with an ETag";
		info->description =
			"This test covers a staleness check of a resource with an ETag\n"
			"that has also expired. It guarantees that even if a resource\n"
			"is expired, we will still not consider it stale if the resource\n"
			"has not changed per the ETag value.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	options.send_file = 1;
	options.status_code = 200;
	options.status_text = "OK";
	options.etag = "123456789";
	options.expires.tv_sec = now.tv_sec - 1;

	snprintf(uri, sizeof(uri), "%s/%s", server_uri, "foo.wav");

	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	ast_test_validate(test, !strcmp(uri, ast_sorcery_object_get_id(bucket_file)));
	ast_test_validate(test, !ast_strlen_zero(bucket_file->path));
	VALIDATE_STR_METADATA(test, bucket_file, "etag", options.etag);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec - 1, 3);

	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(retrieve_expires)
{
	RAII_VAR(struct ast_bucket_file *, bucket_file, NULL, bucket_file_cleanup);
	struct timeval now = ast_tvnow();
	char uri[1024];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test retrieval with explicit expiration";
		info->description =
			"This test covers retrieving a resource that has an Expires.\n"
			"After retrieval of the resource, staleness is checked. With\n"
			"a non-expired resource, we expect the resource to not be stale.\n"
			"When the expiration has occurred, we expect the staleness check\n"
			"to fail.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	options.send_file = 1;
	options.status_code = 200;
	options.status_text = "OK";
	options.expires.tv_sec = now.tv_sec + 3000;

	snprintf(uri, sizeof(uri), "%s/%s", server_uri, "foo.wav");

	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	ast_test_validate(test, !strcmp(uri, ast_sorcery_object_get_id(bucket_file)));
	ast_test_validate(test, !ast_strlen_zero(bucket_file->path));
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec + 3000, 3);

	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);

	/* Clean up previous result */
	bucket_file_cleanup(bucket_file);

	options.expires.tv_sec = now.tv_sec - 1;
	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec - 1, 3);

	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(retrieve_etag)
{
	RAII_VAR(struct ast_bucket_file *, bucket_file, NULL, bucket_file_cleanup);
	struct timeval now = ast_tvnow();
	char uri[1024];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test retrieval with an ETag";
		info->description =
			"This test covers retrieving a resource that has an ETag.\n"
			"After retrieval of the resource, staleness is checked. With\n"
			"matching ETags, we expect the resource to not be stale. When\n"
			"the ETag does not match, we expect the resource to be stale.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	options.send_file = 1;
	options.status_code = 200;
	options.status_text = "OK";
	options.etag = "123456789";

	snprintf(uri, sizeof(uri), "%s/%s", server_uri, "foo.wav");

	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	ast_test_validate(test, !strcmp(uri, ast_sorcery_object_get_id(bucket_file)));
	ast_test_validate(test, !ast_strlen_zero(bucket_file->path));
	VALIDATE_STR_METADATA(test, bucket_file, "etag", options.etag);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec, 3);

	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 0);

	options.etag = "99999999";
	ast_test_validate(test, ast_bucket_file_is_stale(bucket_file) == 1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(retrieve_nominal)
{
	RAII_VAR(struct ast_bucket_file *, bucket_file, NULL, bucket_file_cleanup);
	struct timeval now = ast_tvnow();
	char uri[1024];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test nominal retrieval";
		info->description =
			"Test nominal retrieval of a resource.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	options.send_file = 1;
	options.status_code = 200;
	options.status_text = "OK";

	snprintf(uri, sizeof(uri), "%s/%s", server_uri, "foo.wav");

	bucket_file = ast_bucket_file_retrieve(uri);
	ast_test_validate(test, bucket_file != NULL);
	ast_test_validate(test, !strcmp(uri, ast_sorcery_object_get_id(bucket_file)));
	ast_test_validate(test, !ast_strlen_zero(bucket_file->path));
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec, 3);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(create_nominal)
{
	RAII_VAR(struct ast_bucket_file *, bucket_file, NULL, bucket_file_cleanup);
	struct timeval now = ast_tvnow();
	char uri[1024];

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test nominal creation";
		info->description =
			"Test nominal creation of a resource.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	options.send_file = 1;
	options.status_code = 200;
	options.status_text = "OK";

	snprintf(uri, sizeof(uri), "%s/%s", server_uri, "foo.wav");

	bucket_file = ast_bucket_file_alloc(uri);
	ast_test_validate(test, bucket_file != NULL);
	ast_test_validate(test, ast_bucket_file_temporary_create(bucket_file) == 0);
	ast_test_validate(test, ast_bucket_file_create(bucket_file) == 0);
	VALIDATE_EXPIRES(test, bucket_file, now.tv_sec, 3);

	return AST_TEST_PASS;
}


static int process_config(int reload)
{
	struct ast_config *config;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *bindaddr;
	const char *bindport;
	const char *prefix;
	const char *enabled;

	config = ast_config_load("http.conf", config_flags);
	if (!config || config == CONFIG_STATUS_FILEINVALID) {
		return -1;
	} else if (config == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	enabled = ast_config_option(config, "general", "enabled");
	if (!enabled || ast_false(enabled)) {
		ast_config_destroy(config);
		return -1;
	}

	/* Construct our Server URI */
	bindaddr = ast_config_option(config, "general", "bindaddr");
	if (!bindaddr) {
		ast_config_destroy(config);
		return -1;
	}

	bindport = ast_config_option(config, "general", "bindport");
	if (!bindport) {
		bindport = "8088";
	}

	prefix = ast_config_option(config, "general", "prefix");

	snprintf(server_uri, sizeof(server_uri), "http://%s:%s%s/%s", bindaddr, bindport, S_OR(prefix, ""), TEST_URI);

	ast_config_destroy(config);

	return 0;
}

static int reload_module(void)
{
	return process_config(1);
}

static int load_module(void)
{
	if (process_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_http_uri_link(&test_uri)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(create_nominal);

	AST_TEST_REGISTER(retrieve_nominal);
	AST_TEST_REGISTER(retrieve_etag);
	AST_TEST_REGISTER(retrieve_expires);
	AST_TEST_REGISTER(retrieve_etag_expired);
	AST_TEST_REGISTER(retrieve_cache_control_age);
	AST_TEST_REGISTER(retrieve_cache_control_directives);

	ast_test_register_init(CATEGORY, pre_test_cb);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_http_uri_unlink(&test_uri);

	AST_TEST_UNREGISTER(create_nominal);

	AST_TEST_UNREGISTER(retrieve_nominal);
	AST_TEST_UNREGISTER(retrieve_etag);
	AST_TEST_UNREGISTER(retrieve_expires);
	AST_TEST_UNREGISTER(retrieve_etag_expired);
	AST_TEST_UNREGISTER(retrieve_cache_control_age);
	AST_TEST_UNREGISTER(retrieve_cache_control_directives);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "HTTP Media Cache Backend Tests",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.reload = reload_module,
		.unload = unload_module,
	);
