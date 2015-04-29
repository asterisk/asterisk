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
 * \brief Sorcery Unit Tests
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

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/astdb.h"
#include "asterisk/logger.h"

/*! \brief Dummy sorcery object */
struct test_sorcery_object {
	SORCERY_OBJECT(details);
	unsigned int bob;
	unsigned int joe;
};

/*! \brief Internal function to allocate a test object */
static void *test_sorcery_object_alloc(const char *id)
{
	return ast_sorcery_generic_alloc(sizeof(struct test_sorcery_object), NULL);
}

static struct ast_sorcery *alloc_and_initialize_sorcery(void)
{
	struct ast_sorcery *sorcery;

	if (!(sorcery = ast_sorcery_open())) {
		return NULL;
	}

	if ((ast_sorcery_apply_default(sorcery, "test", "astdb", "test") != AST_SORCERY_APPLY_SUCCESS) ||
		ast_sorcery_internal_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_sorcery_unref(sorcery);
		return NULL;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register_nodoc(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	return sorcery;
}

static void deinitialize_sorcery(struct ast_sorcery *sorcery)
{
	ast_db_deltree("test/test", NULL);
	ast_sorcery_unref(sorcery);
}

AST_TEST_DEFINE(object_create)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	char value[2];

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_create";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery astdb object creation unit test";
		info->description =
			"Test object creation in sorcery using astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using astdb wizard\n");
		return AST_TEST_FAIL;
	} else if (ast_db_get("test/test", "blah", value, sizeof(value))) {
		ast_test_status_update(test, "Object was apparently created but does not actually exist in astdb\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_retrieve_id)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_id";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery object retrieval using id unit test";
		info->description =
			"Test object retrieval using id in sorcery with astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah2"))) {
		ast_test_status_update(test, "Failed to allocate second instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create second object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_retrieve_by_id(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to retrieve properly created object using id of 'blah'\n");
		return AST_TEST_FAIL;
	} else if (strcmp(ast_sorcery_object_get_id(obj), "blah")) {
		ast_test_status_update(test, "Retrieved object does not have correct id\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_retrieve_field)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, fields, ast_variable_new("joe", "42", ""), ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_field";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery object retrieval using a specific field unit test";
		info->description =
			"Test object retrieval using a specific field in sorcery with astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!fields) {
		ast_test_status_update(test, "Failed to create fields for object retrieval attempt\n");
		return AST_TEST_FAIL;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	obj->joe = 42;

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_DEFAULT, fields))) {
		ast_test_status_update(test, "Failed to retrieve properly created object using 'joe' field\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);
	ast_variables_destroy(fields);

	if (!(fields = ast_variable_new("joe", "49", ""))) {
		ast_test_status_update(test, "Failed to create fields for object retrieval attempt\n");
		return AST_TEST_FAIL;
	}

	if ((obj = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_DEFAULT, fields))) {
		ast_test_status_update(test, "Retrieved an object using a field with an in-correct value... that should not happen\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_retrieve_multiple_all)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_multiple_all";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery multiple object retrieval unit test";
		info->description =
			"Test multiple object retrieval in sorcery using astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah2"))) {
		ast_test_status_update(test, "Failed to allocate second instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create second object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	if (!(objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL))) {
		ast_test_status_update(test, "Failed to retrieve a container of all objects\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects) != 2) {
		ast_test_status_update(test, "Received a container with no objects in it when there should be some\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_retrieve_multiple_field)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, fields, ast_variable_new("joe", "6", ""), ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_multiple_field";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery multiple object retrieval unit test";
		info->description =
			"Test multiple object retrieval in sorcery using fields using astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!fields) {
		ast_test_status_update(test, "Failed to create fields for multiple retrieve\n");
		return AST_TEST_FAIL;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	obj->joe = 6;

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	if (!(objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE, fields))) {
		ast_test_status_update(test, "Failed to retrieve a container of all objects\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects) != 1) {
		ast_test_status_update(test, "Received a container with no objects in it when there should be some\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(objects);
	ast_variables_destroy(fields);

	if (!(fields = ast_variable_new("joe", "7", ""))) {
		ast_test_status_update(test, "Failed to create fields for multiple retrieval\n");
		return AST_TEST_FAIL;
	} else if (!(objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE, fields))) {
		ast_test_status_update(test, "Failed to retrieve an empty container when retrieving multiple\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects)) {
		ast_test_status_update(test, "Received a container with objects when there should be none in it\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_retrieve_regex)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_regex";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery multiple object retrieval using regex unit test";
		info->description =
			"Test multiple object retrieval in sorcery using regular expression for matching using astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah-98joe"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah-93joe"))) {
		ast_test_status_update(test, "Failed to allocate second instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create second object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "neener-93joe"))) {
		ast_test_status_update(test, "Failed to allocate third instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create third object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	if (!(objects = ast_sorcery_retrieve_by_regex(sorcery, "test", "^blah-"))) {
		ast_test_status_update(test, "Failed to retrieve a container of objects\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects) != 2) {
		ast_test_status_update(test, "Received a container with incorrect number of objects in it\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_update)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct test_sorcery_object *, obj2, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_update";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery object update unit test";
		info->description =
			"Test object updating in sorcery using astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	if (!(obj2 = ast_sorcery_copy(sorcery, obj))) {
		ast_test_status_update(test, "Failed to allocate a known object type for updating\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	obj2->bob = 1000;
	obj2->joe = 2000;

	if (ast_sorcery_update(sorcery, obj2)) {
		ast_test_status_update(test, "Failed to update sorcery with new object\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_retrieve_by_id(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to retrieve properly updated object\n");
		return AST_TEST_FAIL;
	} else if ((obj->bob != obj2->bob) || (obj->joe != obj2->joe)) {
		ast_test_status_update(test, "Object retrieved is not the updated object\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_update_uncreated)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_update_uncreated";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery object update unit test";
		info->description =
			"Test updating of an uncreated object in sorcery using astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_update(sorcery, obj)) {
		ast_test_status_update(test, "Successfully updated an object which has not been created yet\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_delete)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_delete";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery object deletion unit test";
		info->description =
			"Test object deletion in sorcery using astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_delete(sorcery, obj)) {
		ast_test_status_update(test, "Failed to delete object using astdb wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if ((obj = ast_sorcery_retrieve_by_id(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Retrieved deleted object that should not be there\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_delete_uncreated)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_delete_uncreated";
		info->category = "/res/sorcery_astdb/";
		info->summary = "sorcery object deletion unit test";
		info->description =
			"Test object deletion of an uncreated object in sorcery using astdb wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_delete(sorcery, obj)) {
		ast_test_status_update(test, "Successfully deleted an object which was never created\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int load_module(void)
{
	AST_TEST_REGISTER(object_create);
	AST_TEST_REGISTER(object_retrieve_id);
	AST_TEST_REGISTER(object_retrieve_field);
	AST_TEST_REGISTER(object_retrieve_multiple_all);
	AST_TEST_REGISTER(object_retrieve_multiple_field);
	AST_TEST_REGISTER(object_retrieve_regex);
	AST_TEST_REGISTER(object_update);
	AST_TEST_REGISTER(object_update_uncreated);
	AST_TEST_REGISTER(object_delete);
	AST_TEST_REGISTER(object_delete_uncreated);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "Sorcery astdb Wizard test module");
