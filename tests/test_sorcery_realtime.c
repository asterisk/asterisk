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

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/astdb.h"
#include "asterisk/logger.h"

/*! \brief Configuration structure which contains all stored objects */
static struct ast_config *realtime_objects;

static struct ast_variable *realtime_sorcery(const char *database, const char *table, const struct ast_variable *fields)
{
	char *object_id = NULL;

	while ((object_id = ast_category_browse(realtime_objects, object_id))) {
		if (!ast_variable_lists_match(ast_category_root(realtime_objects, object_id), fields, 0)) {
			continue;
		}

		return ast_variables_dup(ast_category_root(realtime_objects, object_id));
	}

	return NULL;
}

static struct ast_config *realtime_sorcery_multi(const char *database, const char *table, const struct ast_variable *fields)
{
	struct ast_config *objects;
	char *object_id = NULL;

	if (!(objects = ast_config_new())) {
		return NULL;
	}

	while ((object_id = ast_category_browse(realtime_objects, object_id))) {
		struct ast_category *object;
		const struct ast_variable *object_fields = ast_category_root(realtime_objects, object_id);

		if (!ast_variable_lists_match(object_fields, fields, 0)) {
			continue;
		}

		if (!(object = ast_category_new("", "", 0))) {
			ast_config_destroy(objects);
			return NULL;
		}

		ast_variable_append(object, ast_variables_dup(ast_category_root(realtime_objects, object_id)));
		ast_category_append(objects, object);
	}

	return objects;
}

static int realtime_sorcery_update(const char *database, const char *table, const char *keyfield, const char *entity, const struct ast_variable *fields)
{
	struct ast_category *object, *found;

	if (!(found = ast_category_get(realtime_objects, entity, NULL))) {
		return 0;
	} else if (!(object = ast_category_new(entity, "", 0))) {
		return -1;
	}

	ast_category_delete(realtime_objects, found);
	ast_variable_append(object, ast_variables_dup((struct ast_variable*)fields));
	ast_variable_append(object, ast_variable_new(keyfield, entity, ""));
	ast_category_append(realtime_objects, object);

	return 1;
}

static int realtime_sorcery_store(const char *database, const char *table, const struct ast_variable *fields)
{
	/* The key field is explicit within res_sorcery_realtime */
	const struct ast_variable *keyfield = ast_variable_find_variable_in_list(fields, "id");
	struct ast_category *object;

	if (!keyfield || ast_category_exist(realtime_objects, keyfield->value, NULL) || !(object = ast_category_new(keyfield->value, "", 0))) {
		return -1;
	}

	ast_variable_append(object, ast_variables_dup((struct ast_variable*)fields));
	ast_category_append(realtime_objects, object);

	return 1;
}

static int realtime_sorcery_destroy(const char *database, const char *table, const char *keyfield, const char *entity, const struct ast_variable *fields)
{
	struct ast_category *found;
	if (!(found = ast_category_get(realtime_objects, entity, NULL))) {
		return 0;
	}

	ast_category_delete(realtime_objects, found);

	return 1;
}

struct ast_config_engine sorcery_config_engine = {
	.name = "sorcery_realtime_test",
	.realtime_func = realtime_sorcery,
	.realtime_multi_func = realtime_sorcery_multi,
	.update_func = realtime_sorcery_update,
	.store_func = realtime_sorcery_store,
	.destroy_func = realtime_sorcery_destroy,
};

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

static struct ast_sorcery *alloc_and_initialize_sorcery(char *table)
{
	struct ast_sorcery *sorcery;

	if (!(sorcery = ast_sorcery_open())) {
		return NULL;
	}

	if ((ast_sorcery_apply_default(sorcery, "test", "realtime", table) != AST_SORCERY_APPLY_SUCCESS) ||
		ast_sorcery_internal_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL) ||
		!(realtime_objects = ast_config_new())) {
		ast_sorcery_unref(sorcery);
		return NULL;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register_nodoc(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	return sorcery;
}

static void deinitialize_sorcery(struct ast_sorcery *sorcery)
{
	ast_config_destroy(realtime_objects);
	realtime_objects = NULL;
	ast_sorcery_unref(sorcery);
}

AST_TEST_DEFINE(object_create)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_create";
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery realtime object creation unit test";
		info->description =
			"Test object creation in sorcery using realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using realtime wizard\n");
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
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery object retrieval using id unit test";
		info->description =
			"Test object retrieval using id in sorcery with realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
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
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery object retrieval using a specific field unit test";
		info->description =
			"Test object retrieval using a specific field in sorcery with realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!fields) {
		ast_test_status_update(test, "Failed to create fields for object retrieval attempt\n");
		return AST_TEST_FAIL;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	obj->joe = 42;

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using realtime wizard\n");
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
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery multiple object retrieval unit test";
		info->description =
			"Test multiple object retrieval in sorcery using realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using realtime wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah2"))) {
		ast_test_status_update(test, "Failed to allocate second instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create second object using realtime wizard\n");
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

AST_TEST_DEFINE(object_retrieve_multiple_all_nofetch)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_multiple_all_nofetch";
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery multiple object retrieval unit test";
		info->description =
			"Test multiple object retrieval in sorcery using realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test,allow_unqualified_fetch=no"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using realtime wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah2"))) {
		ast_test_status_update(test, "Failed to allocate second instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create second object using realtime wizard\n");
		return AST_TEST_FAIL;
	}

	if (!(objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL))) {
		ast_test_status_update(test, "Failed to retrieve a container of all objects\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects) != 0) {
		ast_test_status_update(test, "Received a container with objects in it when there should be none\n");
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
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery multiple object retrieval unit test";
		info->description =
			"Test multiple object retrieval in sorcery using fields using realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!fields) {
		ast_test_status_update(test, "Failed to create fields for multiple retrieve\n");
		return AST_TEST_FAIL;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	obj->joe = 6;

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using realtime wizard\n");
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
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery multiple object retrieval using regex unit test";
		info->description =
			"Test multiple object retrieval in sorcery using regular expression for matching using realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah-98joe"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using realtime wizard\n");
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

	if (!(objects = ast_sorcery_retrieve_by_regex(sorcery, "test", "blah-"))) {
		ast_test_status_update(test, "Failed to retrieve a container of objects\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects) != 2) {
		ast_test_status_update(test, "Received a container with incorrect number of objects in it: %d instead of 2\n", ao2_container_count(objects));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_retrieve_regex_nofetch)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_regex_nofetch";
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery multiple object retrieval using regex unit test";
		info->description =
			"Test multiple object retrieval in sorcery using regular expression for matching using realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test,allow_unqualified_fetch=no"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah-98joe"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using realtime wizard\n");
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

	if (!(objects = ast_sorcery_retrieve_by_regex(sorcery, "test", ""))) {
		ast_test_status_update(test, "Failed to retrieve a container of objects\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects) != 0) {
		ast_test_status_update(test, "Received a container with incorrect number of objects in it: %d instead of 0\n", ao2_container_count(objects));
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
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery object update unit test";
		info->description =
			"Test object updating in sorcery using realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using realtime wizard\n");
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

AST_TEST_DEFINE(object_delete)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_delete";
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery object deletion unit test";
		info->description =
			"Test object deletion in sorcery using realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using realtime wizard\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_delete(sorcery, obj)) {
		ast_test_status_update(test, "Failed to delete object using realtime wizard\n");
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
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery object deletion unit test";
		info->description =
			"Test object deletion of an uncreated object in sorcery using realtime wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
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

AST_TEST_DEFINE(object_allocate_on_retrieval)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	struct ast_category *cat;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_allocate_on_retrieval";
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery object allocation upon retrieval unit test";
		info->description =
			"This test creates data in a realtime backend, not through sorcery. Sorcery is then\n"
			"instructed to retrieve an object with the id of the object that was created in the\n"
			"realtime backend. Sorcery should be able to allocate the object appropriately";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	cat = ast_category_new("blah", "", 0);
	ast_variable_append(cat, ast_variable_new("id", "blah", ""));
	ast_variable_append(cat, ast_variable_new("bob", "42", ""));
	ast_variable_append(cat, ast_variable_new("joe", "93", ""));
	ast_category_append(realtime_objects, cat);

	if (!(obj = ast_sorcery_retrieve_by_id(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate object 'blah' base on realtime data\n");
		return AST_TEST_FAIL;
	}

	if (obj->bob != 42) {
		ast_test_status_update(test, "Object's 'bob' field does not have expected value: %u != 42\n",
				obj->bob);
		return AST_TEST_FAIL;
	} else if (obj->joe != 93) {
		ast_test_status_update(test, "Object's 'joe' field does not have expected value: %u != 93\n",
				obj->joe);
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}


AST_TEST_DEFINE(object_filter)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, deinitialize_sorcery);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	struct ast_category *cat;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_filter";
		info->category = "/res/sorcery_realtime/";
		info->summary = "sorcery object field filter unit test";
		info->description =
			"This test creates data in a realtime backend, not through sorcery. In addition to\n"
			"the object fields that have been registered with sorcery, there is data in the\n"
			"realtime backend that is unknown to sorcery. When sorcery attempts to retrieve\n"
			"the object from the realtime backend, the data unknown to sorcery should be\n"
			"filtered out of the returned objectset, and the object should be successfully\n"
			"allocated by sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery("sorcery_realtime_test"))) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	cat = ast_category_new("blah", "", 0);
	ast_variable_append(cat, ast_variable_new("id", "blah", ""));
	ast_variable_append(cat, ast_variable_new("bob", "42", ""));
	ast_variable_append(cat, ast_variable_new("joe", "93", ""));
	ast_variable_append(cat, ast_variable_new("fred", "50", ""));
	ast_category_append(realtime_objects, cat);

	if (!(obj = ast_sorcery_retrieve_by_id(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to retrieve properly created object using id of 'blah'\n");
		return AST_TEST_FAIL;
	}

	if (obj->bob != 42) {
		ast_test_status_update(test, "Object's 'bob' field does not have expected value: %u != 42\n",
				obj->bob);
		return AST_TEST_FAIL;
	} else if (obj->joe != 93) {
		ast_test_status_update(test, "Object's 'joe' field does not have expected value: %u != 93\n",
				obj->joe);
		return AST_TEST_FAIL;
	}
	return AST_TEST_PASS;
}

static int unload_module(void)
{
	ast_config_engine_deregister(&sorcery_config_engine);
	AST_TEST_UNREGISTER(object_create);
	AST_TEST_UNREGISTER(object_retrieve_id);
	AST_TEST_UNREGISTER(object_retrieve_field);
	AST_TEST_UNREGISTER(object_retrieve_multiple_all);
	AST_TEST_UNREGISTER(object_retrieve_multiple_all_nofetch);
	AST_TEST_UNREGISTER(object_retrieve_multiple_field);
	AST_TEST_UNREGISTER(object_retrieve_regex);
	AST_TEST_UNREGISTER(object_retrieve_regex_nofetch);
	AST_TEST_UNREGISTER(object_update);
	AST_TEST_UNREGISTER(object_delete);
	AST_TEST_UNREGISTER(object_delete_uncreated);
	AST_TEST_UNREGISTER(object_allocate_on_retrieval);
	AST_TEST_UNREGISTER(object_filter);

	return 0;
}

static int load_module(void)
{
	ast_config_engine_register(&sorcery_config_engine);
	ast_realtime_append_mapping("sorcery_realtime_test", "sorcery_realtime_test", "test", "test", 1);
	AST_TEST_REGISTER(object_create);
	AST_TEST_REGISTER(object_retrieve_id);
	AST_TEST_REGISTER(object_retrieve_field);
	AST_TEST_REGISTER(object_retrieve_multiple_all);
	AST_TEST_REGISTER(object_retrieve_multiple_all_nofetch);
	AST_TEST_REGISTER(object_retrieve_multiple_field);
	AST_TEST_REGISTER(object_retrieve_regex);
	AST_TEST_REGISTER(object_retrieve_regex_nofetch);
	AST_TEST_REGISTER(object_update);
	AST_TEST_REGISTER(object_delete);
	AST_TEST_REGISTER(object_delete_uncreated);
	AST_TEST_REGISTER(object_allocate_on_retrieval);
	AST_TEST_REGISTER(object_filter);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Sorcery Realtime Wizard test module");
