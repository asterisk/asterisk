/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Bucket Unit Tests
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <sys/stat.h>

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/bucket.h"
#include "asterisk/logger.h"
#include "asterisk/json.h"
#include "asterisk/file.h"

/*! \brief Test state structure for scheme wizards */
struct bucket_test_state {
	/*! \brief Whether the object has been created or not */
	unsigned int created:1;
	/*! \brief Whether the object has been updated or not */
	unsigned int updated:1;
	/*! \brief Whether the object has been deleted or not */
	unsigned int deleted:1;
};

/*! \brief Global scope structure for testing bucket wizards */
static struct bucket_test_state bucket_test_wizard_state;

static void bucket_test_wizard_clear(void)
{
	bucket_test_wizard_state.created = 0;
	bucket_test_wizard_state.updated = 0;
	bucket_test_wizard_state.deleted = 0;
}

static int bucket_test_wizard_create(const struct ast_sorcery *sorcery, void *data, void *object)
{
	if (bucket_test_wizard_state.created) {
		return -1;
	}

	bucket_test_wizard_state.created = 1;

	return 0;
}

static int bucket_test_wizard_update(const struct ast_sorcery *sorcery, void *data, void *object)
{
	if (bucket_test_wizard_state.updated) {
		return -1;
	}

	bucket_test_wizard_state.updated = 1;

	return 0;
}

static void *bucket_test_wizard_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type,
	const char *id)
{
	if (!strcmp(type, "bucket")) {
		return ast_bucket_alloc(id);
	} else if (!strcmp(type, "file")) {
		return ast_bucket_file_alloc(id);
	} else {
		return NULL;
	}
}

static int bucket_test_wizard_delete(const struct ast_sorcery *sorcery, void *data, void *object)
{
	if (bucket_test_wizard_state.deleted) {
		return -1;
	}

	bucket_test_wizard_state.deleted = 1;

	return 0;
}

static struct ast_sorcery_wizard bucket_test_wizard = {
	.name = "test",
	.create = bucket_test_wizard_create,
	.retrieve_id = bucket_test_wizard_retrieve_id,
	.delete = bucket_test_wizard_delete,
};

static struct ast_sorcery_wizard bucket_file_test_wizard = {
	.name = "test",
	.create = bucket_test_wizard_create,
	.update = bucket_test_wizard_update,
	.retrieve_id = bucket_test_wizard_retrieve_id,
	.delete = bucket_test_wizard_delete,
};

AST_TEST_DEFINE(bucket_scheme_register)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_scheme_register_unregister";
		info->category = "/main/bucket/";
		info->summary = "bucket scheme registration/unregistration unit test";
		info->description =
			"Test registration and unregistration of bucket scheme";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!ast_bucket_scheme_register("", NULL, NULL, NULL, NULL)) {
		ast_test_status_update(test, "Successfully registered a Bucket scheme without name or wizards\n");
		return AST_TEST_FAIL;
	}

	if (!ast_bucket_scheme_register("test", &bucket_test_wizard, &bucket_file_test_wizard, NULL, NULL)) {
		ast_test_status_update(test, "Successfully registered a Bucket scheme twice\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_alloc)
{
	RAII_VAR(struct ast_bucket *, bucket, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_alloc";
		info->category = "/main/bucket/";
		info->summary = "bucket allocation unit test";
		info->description =
			"Test allocation of buckets";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if ((bucket = ast_bucket_alloc(""))) {
		ast_test_status_update(test, "Allocated a bucket with no URI provided\n");
		return AST_TEST_FAIL;
	}

	if (!(bucket = ast_bucket_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate bucket\n");
		return AST_TEST_FAIL;
	}

	if (strcmp(ast_sorcery_object_get_id(bucket), "test:///tmp/bob")) {
		ast_test_status_update(test, "URI within allocated bucket is '%s' and should be test:///tmp/bob\n",
			ast_sorcery_object_get_id(bucket));
		return AST_TEST_FAIL;
	}

	if (strcmp(bucket->scheme, "test")) {
		ast_test_status_update(test, "Scheme within allocated bucket is '%s' and should be test\n",
			bucket->scheme);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_create)
{
	RAII_VAR(struct ast_bucket *, bucket, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_create";
		info->category = "/main/bucket/";
		info->summary = "bucket creation unit test";
		info->description =
			"Test creation of buckets";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(bucket = ast_bucket_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate bucket\n");
		return AST_TEST_FAIL;
	}

	bucket_test_wizard_clear();

	if (ast_bucket_create(bucket)) {
		ast_test_status_update(test, "Failed to create bucket with URI '%s'\n",
			ast_sorcery_object_get_id(bucket));
		return AST_TEST_FAIL;
	}

	if (!bucket_test_wizard_state.created) {
		ast_test_status_update(test, "Bucket creation returned success but scheme implementation never actually created it\n");
		return AST_TEST_FAIL;
	}

	if (!ast_bucket_create(bucket)) {
		ast_test_status_update(test, "Successfully created bucket with URI '%s' twice\n",
			ast_sorcery_object_get_id(bucket));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_delete)
{
	RAII_VAR(struct ast_bucket *, bucket, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_delete";
		info->category = "/main/bucket/";
		info->summary = "bucket deletion unit test";
		info->description =
			"Test deletion of buckets";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(bucket = ast_bucket_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate bucket\n");
		return AST_TEST_FAIL;
	}

	bucket_test_wizard_clear();

	if (ast_bucket_delete(bucket)) {
		ast_test_status_update(test, "Failed to delete bucket with URI '%s'\n",
			ast_sorcery_object_get_id(bucket));
		return AST_TEST_FAIL;
	}

	if (!bucket_test_wizard_state.deleted) {
		ast_test_status_update(test, "Bucket deletion returned success but scheme implementation never actually deleted it\n");
		return AST_TEST_FAIL;
	}

	if (!ast_bucket_delete(bucket)) {
		ast_test_status_update(test, "Successfully deleted bucket with URI '%s' twice\n",
			ast_sorcery_object_get_id(bucket));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_json)
{
	RAII_VAR(struct ast_bucket *, bucket, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_json";
		info->category = "/main/bucket/";
		info->summary = "bucket json unit test";
		info->description =
			"Test creation of JSON for a bucket";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(bucket = ast_bucket_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate bucket\n");
		return AST_TEST_FAIL;
	}

	ast_str_container_add(bucket->buckets, "test:///tmp/bob/joe");
	ast_str_container_add(bucket->files, "test:///tmp/bob/recording.wav");

	expected = ast_json_pack("{s: s, s: s, s: [s], s: s, s: [s], s: s}",
		"modified", "0.000000", "created", "0.000000",
		"buckets", "test:///tmp/bob/joe",
		"scheme", "test",
		"files", "test:///tmp/bob/recording.wav",
		"id", "test:///tmp/bob");
	if (!expected) {
		ast_test_status_update(test, "Could not produce JSON for expected bucket value\n");
		return AST_TEST_FAIL;
	}

	json = ast_bucket_json(bucket);
	if (!json) {
		ast_test_status_update(test, "Could not produce JSON for a valid bucket\n");
		return AST_TEST_FAIL;
	}

	if (!ast_json_equal(json, expected)) {
		ast_test_status_update(test, "Bucket JSON does not match expected output\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_retrieve)
{
	RAII_VAR(struct ast_bucket *, bucket, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_retrieve";
		info->category = "/main/bucket/";
		info->summary = "bucket retrieval unit test";
		info->description =
			"Test retrieval of buckets";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(bucket = ast_bucket_retrieve("test://tmp/bob"))) {
		ast_test_status_update(test, "Failed to retrieve known valid bucket\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_alloc)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_alloc";
		info->category = "/main/bucket/";
		info->summary = "bucket file allocation unit test";
		info->description =
			"Test allocation of bucket files";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if ((file = ast_bucket_file_alloc(""))) {
		ast_test_status_update(test, "Allocated a file with no URI provided\n");
		return AST_TEST_FAIL;
	}

	if (!(file = ast_bucket_file_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate file\n");
		return AST_TEST_FAIL;
	}

	if (ast_strlen_zero(file->path)) {
		ast_test_status_update(test, "Expected temporary path in allocated file");
		return AST_TEST_FAIL;
	}

	if (strcmp(ast_sorcery_object_get_id(file), "test:///tmp/bob")) {
		ast_test_status_update(test, "URI within allocated file is '%s' and should be test:///tmp/bob\n",
			ast_sorcery_object_get_id(file));
		return AST_TEST_FAIL;
	}

	if (strcmp(file->scheme, "test")) {
		ast_test_status_update(test, "Scheme within allocated file is '%s' and should be test\n",
			file->scheme);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_create)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_create";
		info->category = "/main/bucket/";
		info->summary = "file creation unit test";
		info->description =
			"Test creation of files";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(file = ast_bucket_file_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate file\n");
		return AST_TEST_FAIL;
	}

	bucket_test_wizard_clear();

	if (ast_bucket_file_create(file)) {
		ast_test_status_update(test, "Failed to create file with URI '%s'\n",
			ast_sorcery_object_get_id(file));
		return AST_TEST_FAIL;
	}

	if (!bucket_test_wizard_state.created) {
		ast_test_status_update(test, "Bucket file creation returned success but scheme implementation never actually created it\n");
		return AST_TEST_FAIL;
	}

	if (!ast_bucket_file_create(file)) {
		ast_test_status_update(test, "Successfully created file with URI '%s' twice\n",
			ast_sorcery_object_get_id(file));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_copy)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bucket_file *, copy, NULL, ao2_cleanup);
	FILE *temporary;
	struct stat old, new;
	RAII_VAR(struct ast_bucket_metadata *, metadata, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_copy";
		info->category = "/main/bucket/";
		info->summary = "bucket file copying unit test";
		info->description =
			"Test copying of bucket files";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(file = ast_bucket_file_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate file\n");
		return AST_TEST_FAIL;
	}

	ast_bucket_file_metadata_set(file, "bob", "joe");

	if (!(temporary = fopen(file->path, "w"))) {
		ast_test_status_update(test, "Failed to open temporary file '%s'\n", file->path);
		return AST_TEST_FAIL;
	}

	fprintf(temporary, "bob");
	fclose(temporary);

	if (!(copy = ast_bucket_file_copy(file, "test:///tmp/bob2"))) {
		ast_test_status_update(test, "Failed to copy file '%s' to test:///tmp/bob2\n",
			ast_sorcery_object_get_id(file));
		return AST_TEST_FAIL;
	}

	if (stat(file->path, &old)) {
		ast_test_status_update(test, "Failed to retrieve information on old file '%s'\n", file->path);
		return AST_TEST_FAIL;
	}

	if (stat(copy->path, &new)) {
		ast_test_status_update(test, "Failed to retrieve information on copy file '%s'\n", copy->path);
		return AST_TEST_FAIL;
	}

	if (old.st_size != new.st_size) {
		ast_test_status_update(test, "Copying of underlying temporary file failed\n");
		return AST_TEST_FAIL;
	}

	if (ao2_container_count(file->metadata) != ao2_container_count(copy->metadata)) {
		ast_test_status_update(test, "Number of metadata entries does not match original\n");
		return AST_TEST_FAIL;
	}

	metadata = ast_bucket_file_metadata_get(copy, "bob");
	if (!metadata) {
		ast_test_status_update(test, "Copy of file does not have expected metadata\n");
		return AST_TEST_FAIL;
	}

	if (strcmp(metadata->value, "joe")) {
		ast_test_status_update(test, "Copy of file contains metadata for 'bob' but value is not what it should be\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_retrieve)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_retrieve";
		info->category = "/main/bucket/";
		info->summary = "file retrieval unit test";
		info->description =
			"Test retrieval of files";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(file = ast_bucket_file_retrieve("test://tmp/bob"))) {
		ast_test_status_update(test, "Failed to retrieve known valid file\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_update)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_update";
		info->category = "/main/bucket/";
		info->summary = "file updating unit test";
		info->description =
			"Test updating of files";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(file = ast_bucket_file_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate file\n");
		return AST_TEST_FAIL;
	}

	bucket_test_wizard_clear();

	if (ast_bucket_file_update(file)) {
		ast_test_status_update(test, "Failed to update file with URI '%s'\n",
			ast_sorcery_object_get_id(file));
		return AST_TEST_FAIL;
	}

	if (!bucket_test_wizard_state.updated) {
		ast_test_status_update(test, "Successfully returned file was updated, but it was not\n");
		return AST_TEST_FAIL;
	}

	if (!ast_bucket_file_update(file)) {
		ast_test_status_update(test, "Successfully updated file with URI '%s' twice\n",
			ast_sorcery_object_get_id(file));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_delete)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_delete";
		info->category = "/main/bucket/";
		info->summary = "file deletion unit test";
		info->description =
			"Test deletion of files";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(file = ast_bucket_file_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate file\n");
		return AST_TEST_FAIL;
	}

	bucket_test_wizard_clear();

	if (ast_bucket_file_delete(file)) {
		ast_test_status_update(test, "Failed to delete file with URI '%s'\n",
			ast_sorcery_object_get_id(file));
		return AST_TEST_FAIL;
	}

	if (!bucket_test_wizard_state.deleted) {
		ast_test_status_update(test, "Bucket file deletion returned success but scheme implementation never actually deleted it\n");
		return AST_TEST_FAIL;
	}

	if (!ast_bucket_file_delete(file)) {
		ast_test_status_update(test, "Successfully deleted file with URI '%s' twice\n",
			ast_sorcery_object_get_id(file));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_metadata_set)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bucket_metadata *, metadata, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_metadata_set";
		info->category = "/main/bucket/";
		info->summary = "file metadata setting unit test";
		info->description =
			"Test setting of metadata on files";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(file = ast_bucket_file_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate file\n");
		return AST_TEST_FAIL;
	}

	if (ao2_container_count(file->metadata) != 0) {
		ast_test_status_update(test, "Newly allocated file has metadata count of '%d' when should be 0\n",
			ao2_container_count(file->metadata));
		return AST_TEST_FAIL;
	}

	if (ast_bucket_file_metadata_set(file, "bob", "joe")) {
		ast_test_status_update(test, "Failed to set metadata 'bob' to 'joe' on newly allocated file\n");
		return AST_TEST_FAIL;
	}

	if (!(metadata = ao2_find(file->metadata, "bob", OBJ_KEY))) {
		ast_test_status_update(test, "Failed to find set metadata 'bob' on newly allocated file\n");
		return AST_TEST_FAIL;
	}

	if (strcmp(metadata->value, "joe")) {
		ast_test_status_update(test, "Metadata has value '%s' when should be 'joe'\n",
			metadata->value);
		return AST_TEST_FAIL;
	}

	ao2_cleanup(metadata);
	metadata = NULL;

	if (ast_bucket_file_metadata_set(file, "bob", "fred")) {
		ast_test_status_update(test, "Failed to overwrite metadata 'bob' with new value 'fred'\n");
		return AST_TEST_FAIL;
	}

	if (!(metadata = ao2_find(file->metadata, "bob", OBJ_KEY))) {
		ast_test_status_update(test, "Failed to find overwritten metadata 'bob' on newly allocated file\n");
		return AST_TEST_FAIL;
	}

	if (strcmp(metadata->value, "fred")) {
		ast_test_status_update(test, "Metadata has value '%s' when should be 'fred'\n",
			metadata->value);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_metadata_unset)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bucket_metadata *, metadata, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_metadata_unset";
		info->category = "/main/bucket/";
		info->summary = "file metadata unsetting unit test";
		info->description =
			"Test unsetting of metadata on files";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(file = ast_bucket_file_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate file\n");
		return AST_TEST_FAIL;
	}

	if (ast_bucket_file_metadata_set(file, "bob", "joe")) {
		ast_test_status_update(test, "Failed to set metadata 'bob' to 'joe' on newly allocated file\n");
		return AST_TEST_FAIL;
	}

	if (ast_bucket_file_metadata_unset(file, "bob")) {
		ast_test_status_update(test, "Failed to unset metadata 'bob' on newly allocated file\n");
		return AST_TEST_FAIL;
	}

	if ((metadata = ao2_find(file->metadata, "bob", OBJ_KEY))) {
		ast_test_status_update(test, "Metadata 'bob' was unset, but can still be found\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_metadata_get)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bucket_metadata *, metadata, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_metadata_get";
		info->category = "/main/bucket/";
		info->summary = "file metadata getting unit test";
		info->description =
			"Test getting of metadata on files";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(file = ast_bucket_file_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate file\n");
		return AST_TEST_FAIL;
	}

	if (ast_bucket_file_metadata_set(file, "bob", "joe")) {
		ast_test_status_update(test, "Failed to set metadata 'bob' to 'joe' on newly allocated file\n");
		return AST_TEST_FAIL;
	}

	if (!(metadata = ast_bucket_file_metadata_get(file, "bob"))) {
		ast_test_status_update(test, "Failed to retrieve metadata 'bob' that was just set\n");
		return AST_TEST_FAIL;
	}

	if (strcmp(metadata->value, "joe")) {
		ast_test_status_update(test, "Retrieved metadata value is '%s' while it should be 'joe'\n",
			metadata->value);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(bucket_file_json)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "bucket_file_json";
		info->category = "/main/bucket/";
		info->summary = "file json unit test";
		info->description =
			"Test creation of JSON for a file";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(file = ast_bucket_file_alloc("test:///tmp/bob"))) {
		ast_test_status_update(test, "Failed to allocate bucket\n");
		return AST_TEST_FAIL;
	}

	if (ast_bucket_file_metadata_set(file, "bob", "joe")) {
		ast_test_status_update(test, "Failed to set metadata 'bob' to 'joe' on newly allocated file\n");
		return AST_TEST_FAIL;
	}

	expected = ast_json_pack("{s: s, s: s, s: s, s: s, s: {s :s}}",
		"modified", "0.000000", "created", "0.000000", "scheme", "test",
		"id", "test:///tmp/bob", "metadata", "bob", "joe");
	if (!expected) {
		ast_test_status_update(test, "Could not produce JSON for expected bucket file value\n");
		return AST_TEST_FAIL;
	}

	json = ast_bucket_file_json(file);
	if (!json) {
		ast_test_status_update(test, "Could not produce JSON for a valid file\n");
		return AST_TEST_FAIL;
	}

	if (!ast_json_equal(json, expected)) {
		ast_test_status_update(test, "Bucket file JSON does not match expected output\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(bucket_scheme_register);
	AST_TEST_UNREGISTER(bucket_alloc);
	AST_TEST_UNREGISTER(bucket_create);
	AST_TEST_UNREGISTER(bucket_delete);
	AST_TEST_UNREGISTER(bucket_retrieve);
	AST_TEST_UNREGISTER(bucket_json);
	AST_TEST_UNREGISTER(bucket_file_alloc);
	AST_TEST_UNREGISTER(bucket_file_create);
	AST_TEST_UNREGISTER(bucket_file_copy);
	AST_TEST_UNREGISTER(bucket_file_retrieve);
	AST_TEST_UNREGISTER(bucket_file_update);
	AST_TEST_UNREGISTER(bucket_file_delete);
	AST_TEST_UNREGISTER(bucket_file_metadata_set);
	AST_TEST_UNREGISTER(bucket_file_metadata_unset);
	AST_TEST_UNREGISTER(bucket_file_metadata_get);
	AST_TEST_UNREGISTER(bucket_file_json);
	return 0;
}

static int load_module(void)
{
	if (ast_bucket_scheme_register("test", &bucket_test_wizard, &bucket_file_test_wizard,
		ast_bucket_file_temporary_create, ast_bucket_file_temporary_destroy)) {
		ast_log(LOG_ERROR, "Failed to register Bucket test wizard scheme implementation\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	AST_TEST_REGISTER(bucket_scheme_register);
	AST_TEST_REGISTER(bucket_alloc);
	AST_TEST_REGISTER(bucket_create);
	AST_TEST_REGISTER(bucket_delete);
	AST_TEST_REGISTER(bucket_retrieve);
	AST_TEST_REGISTER(bucket_json);
	AST_TEST_REGISTER(bucket_file_alloc);
	AST_TEST_REGISTER(bucket_file_create);
	AST_TEST_REGISTER(bucket_file_copy);
	AST_TEST_REGISTER(bucket_file_retrieve);
	AST_TEST_REGISTER(bucket_file_update);
	AST_TEST_REGISTER(bucket_file_delete);
	AST_TEST_REGISTER(bucket_file_metadata_set);
	AST_TEST_REGISTER(bucket_file_metadata_unset);
	AST_TEST_REGISTER(bucket_file_metadata_get);
	AST_TEST_REGISTER(bucket_file_json);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Bucket test module");
