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
 * \brief Tests for the media cache API
 *
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/bucket.h"
#include "asterisk/media_cache.h"

/*! The unit test category */
#define CATEGORY "/main/media_cache/"

/*! A 'valid' resource for the test bucket behind the media cache facade */
#define VALID_RESOURCE "httptest://localhost:8088/test_media_cache/monkeys.wav"

/*! An 'invalid' resource for the test bucket behind the media cache facade */
#define INVALID_RESOURCE "httptest://localhost:8088/test_media_cache/bad.wav"

/*! An 'invalid' scheme, not mapping to a valid bucket backend */
#define INVALID_SCHEME "foo://localhost:8088/test_media_cache/monkeys.wav"

/*! A URI with no scheme */
#define NO_SCHEME "localhost:8088/test_media_cache/monkeys.wav"

/*!
 * \internal
 * \brief Create callback for the httptest bucket backend
 */
static int bucket_http_test_wizard_create(const struct ast_sorcery *sorcery, void *data,
	void *object)
{
	if (!strcmp(ast_sorcery_object_get_id(object), VALID_RESOURCE)) {
		return 0;
	}

	return -1;
}

/*!
 * \internal
 * \brief Update callback for the httptest bucket backend
 */
static int bucket_http_test_wizard_update(const struct ast_sorcery *sorcery, void *data,
	void *object)
{
	if (!strcmp(ast_sorcery_object_get_id(object), VALID_RESOURCE)) {
		return 0;
	}

	return -1;
}

/*!
 * \internal
 * \brief Retrieve callback for the httptest bucket backend
 */
static void *bucket_http_test_wizard_retrieve_id(const struct ast_sorcery *sorcery,
	void *data, const char *type, const char *id)
{
	struct ast_bucket_file *bucket_file;

	if (!strcmp(type, "file") && !strcmp(id, VALID_RESOURCE)) {
		bucket_file = ast_bucket_file_alloc(id);
		if (!bucket_file) {
			return NULL;
		}

		ast_bucket_file_temporary_create(bucket_file);
		return bucket_file;
	}
	return NULL;
}

/*!
 * \internal
 * \brief Delete callback for the httptest bucket backend
 */
static int bucket_http_test_wizard_delete(const struct ast_sorcery *sorcery, void *data,
	void *object)
{
	if (!strcmp(ast_sorcery_object_get_id(object), VALID_RESOURCE)) {
		return 0;
	}

	return -1;
}

static struct ast_sorcery_wizard bucket_test_wizard = {
	.name = "httptest",
	.create = bucket_http_test_wizard_create,
	.retrieve_id = bucket_http_test_wizard_retrieve_id,
	.delete = bucket_http_test_wizard_delete,
};

static struct ast_sorcery_wizard bucket_file_test_wizard = {
	.name = "httptest",
	.create = bucket_http_test_wizard_create,
	.update = bucket_http_test_wizard_update,
	.retrieve_id = bucket_http_test_wizard_retrieve_id,
	.delete = bucket_http_test_wizard_delete,
};

AST_TEST_DEFINE(exists_nominal)
{
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test nominal existance of resources in the cache";
		info->description =
			"This test verifies that if a known resource is in the cache, "
			"calling ast_media_cache_exists will return logical True. If "
			"a resource does not exist, the same function call will return "
			"logical False.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = ast_media_cache_exists(INVALID_RESOURCE);
	ast_test_validate(test, res == 0);

	res = ast_media_cache_exists(VALID_RESOURCE);
	ast_test_validate(test, res == 1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(exists_off_nominal)
{
	int res;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test off nominal existance of resources in the cache";
		info->description =
			"This test verifies that checking for bad resources (NULL, bad "
			"scheme, etc.) does not result in false positivies.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	res = ast_media_cache_exists("");
	ast_test_validate(test, res != 1);

	res = ast_media_cache_exists(NULL);
	ast_test_validate(test, res != 1);

	res = ast_media_cache_exists(NO_SCHEME);
	ast_test_validate(test, res != 1);

	res = ast_media_cache_exists(INVALID_SCHEME);
	ast_test_validate(test, res != 1);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(create_update_nominal)
{
	int res;
	char file_path[PATH_MAX];
	char tmp_path_one[PATH_MAX] = "/tmp/test-media-cache-XXXXXX";
	char tmp_path_two[PATH_MAX] = "/tmp/test-media-cache-XXXXXX";
	int fd;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test nominal creation/updating of a resource";
		info->description =
			"This test creates a resource and associates it with a file. "
			"It then updates the resource with a new file. In both cases, "
			"the test verifies that the resource is associated with the "
			"file.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Create two local files to associate with a resource */
	fd = mkstemp(tmp_path_one);
	if (fd < 0) {
		ast_test_status_update(test, "Failed to create first tmp file: %s\n",
			tmp_path_one);
		return AST_TEST_FAIL;
	}
	/* We don't need anything in the file */
	close(fd);

	fd = mkstemp(tmp_path_two);
	if (fd < 0) {
		ast_test_status_update(test, "Failed to create second tmp file: %s\n",
			tmp_path_two);
		return AST_TEST_FAIL;
	}
	close(fd);

	ast_test_status_update(test, "Creating resource with %s\n", tmp_path_one);
	res = ast_media_cache_create_or_update(VALID_RESOURCE, tmp_path_one, NULL);
	ast_test_validate(test, res == 0);

	res = ast_media_cache_retrieve(VALID_RESOURCE, NULL, file_path, PATH_MAX);
	ast_test_status_update(test, "Got %s for first file path\n", file_path);
	ast_test_validate(test, res == 0);
	ast_test_validate(test, strcmp(file_path, tmp_path_one) == 0);

	ast_test_status_update(test, "Creating resource with %s\n", tmp_path_two);
	res = ast_media_cache_create_or_update(VALID_RESOURCE, tmp_path_two, NULL);
	ast_test_validate(test, res == 0);

	res = ast_media_cache_retrieve(VALID_RESOURCE, NULL, file_path, PATH_MAX);
	ast_test_status_update(test, "Got %s for second file path\n", file_path);
	ast_test_validate(test, res == 0);
	ast_test_validate(test, strcmp(file_path, tmp_path_two) == 0);

	unlink(tmp_path_one);
	unlink(tmp_path_two);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(create_update_off_nominal)
{
	int res;
	char tmp_path[PATH_MAX] = "/tmp/test-media-cache-XXXXXX";
	int fd;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test off nominal creation/updating of a resource";
		info->description =
			"Test creation/updating of a resource with a variety of invalid\n"
			"inputs.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Create two local files to associate with a resource */
	fd = mkstemp(tmp_path);
	if (fd < 0) {
		ast_test_status_update(test, "Failed to create first tmp file: %s\n",
			tmp_path);
		return AST_TEST_FAIL;
	}
	/* We don't need anything in the file */
	close(fd);

	res = ast_media_cache_create_or_update(VALID_RESOURCE, NULL, NULL);
	ast_test_validate(test, res != 0);

	res = ast_media_cache_create_or_update(VALID_RESOURCE, "", NULL);
	ast_test_validate(test, res != 0);

	res = ast_media_cache_create_or_update(VALID_RESOURCE, "I don't exist", NULL);
	ast_test_validate(test, res != 0);

	res = ast_media_cache_create_or_update(INVALID_RESOURCE, tmp_path, NULL);
	ast_test_validate(test, res != 0);

	res = ast_media_cache_create_or_update(INVALID_SCHEME, tmp_path, NULL);
	ast_test_validate(test, res != 0);

	res = ast_media_cache_create_or_update(NO_SCHEME, tmp_path, NULL);
	ast_test_validate(test, res != 0);

	unlink(tmp_path);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(create_update_metadata)
{
	int res;
	char tmp_path[PATH_MAX] = "/tmp/test-media-cache-XXXXXX";
	char file_path[PATH_MAX];
	char actual_metadata[32];
	struct ast_variable *meta_list = NULL;
	struct ast_variable *tmp;
	int fd;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Test nominal creation/updating of a resource";
		info->description =
			"This test creates a resource and associates it with a file. "
			"It then updates the resource with a new file. In both cases, "
			"the test verifies that the resource is associated with the "
			"file.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Create two local files to associate with a resource */
	fd = mkstemp(tmp_path);
	if (fd < 0) {
		ast_test_status_update(test, "Failed to create first tmp file: %s\n",
			tmp_path);
		return AST_TEST_FAIL;
	}
	/* We don't need anything in the file */
	close(fd);

	tmp = ast_variable_new("meta1", "value1", __FILE__);
	if (!tmp) {
		ast_test_status_update(test, "Failed to create metadata 1 for test\n");
		return AST_TEST_FAIL;
	}
	ast_variable_list_append(&meta_list, tmp);

	tmp = ast_variable_new("meta2", "value2", __FILE__);
	if (!tmp) {
		ast_test_status_update(test, "Failed to create metadata 2 for test\n");
		return AST_TEST_FAIL;
	}
	ast_variable_list_append(&meta_list, tmp);

	res = ast_media_cache_create_or_update(VALID_RESOURCE, tmp_path, meta_list);
	ast_test_validate(test, res == 0);

	res = ast_media_cache_retrieve(VALID_RESOURCE, NULL, file_path, PATH_MAX);
	ast_test_status_update(test, "Got %s for second file path\n", file_path);
	ast_test_validate(test, res == 0);
	ast_test_validate(test, strcmp(file_path, tmp_path) == 0);

	res = ast_media_cache_retrieve_metadata(VALID_RESOURCE, "meta1",
		actual_metadata, sizeof(actual_metadata));
	ast_test_validate(test, res == 0);
	ast_test_validate(test, strcmp(actual_metadata, "value1") == 0);

	res = ast_media_cache_retrieve_metadata(VALID_RESOURCE, "meta2",
		actual_metadata, sizeof(actual_metadata));
	ast_test_validate(test, res == 0);
	ast_test_validate(test, strcmp(actual_metadata, "value2") == 0);

	unlink(tmp_path);

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(exists_nominal);
	AST_TEST_UNREGISTER(exists_off_nominal);

	AST_TEST_UNREGISTER(create_update_nominal);
	AST_TEST_UNREGISTER(create_update_metadata);
	AST_TEST_UNREGISTER(create_update_off_nominal);

	return 0;
}

static int load_module(void)
{
	if (ast_bucket_scheme_register("httptest", &bucket_test_wizard,
		&bucket_file_test_wizard, NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register Bucket HTTP test wizard scheme implementation\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(exists_nominal);
	AST_TEST_REGISTER(exists_off_nominal);

	AST_TEST_REGISTER(create_update_nominal);
	AST_TEST_REGISTER(create_update_metadata);
	AST_TEST_REGISTER(create_update_off_nominal);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Media Cache Tests");
