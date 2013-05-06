/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
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

ASTERISK_FILE_VERSION(__FILE__, "")

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"
#include "asterisk/logger.h"
#include "asterisk/json.h"

/*! \brief Dummy sorcery object */
struct test_sorcery_object {
	SORCERY_OBJECT(details);
	unsigned int bob;
	unsigned int joe;
};

/*! \brief Internal function to allocate a test object */
static void *test_sorcery_object_alloc(const char *id)
{
	return ao2_alloc(sizeof(struct test_sorcery_object), NULL);
}

/*! \brief Internal function for object set transformation */
static struct ast_variable *test_sorcery_transform(struct ast_variable *set)
{
	struct ast_variable *field, *transformed = NULL;

	for (field = set; field; field = field->next) {
		struct ast_variable *transformed_field;

		if (!strcmp(field->name, "joe")) {
			transformed_field = ast_variable_new(field->name, "5000", "");
		} else {
			transformed_field = ast_variable_new(field->name, field->value, "");
		}

		if (!transformed_field) {
			ast_variables_destroy(transformed);
			return NULL;
		}

		transformed_field->next = transformed;
		transformed = transformed_field;
	}

	return transformed;
}

/*! \brief Internal function which copies pre-defined data into an object, natively */
static int test_sorcery_copy(const void *src, void *dst)
{
	struct test_sorcery_object *obj = dst;
	obj->bob = 10;
	obj->joe = 20;
	return 0;
}

/*! \brief Internal function which creates a pre-defined diff natively */
static int test_sorcery_diff(const void *original, const void *modified, struct ast_variable **changes)
{
	*changes = ast_variable_new("yes", "itworks", "");
	return 0;
}

/*! \brief Internal function which sets some values */
static int test_sorcery_regex_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct test_sorcery_object *test = obj;

	test->bob = 256;

	return 0;
}

/*! \brief Internal function which creates some ast_variable structures */
static int test_sorcery_regex_fields(const void *obj, struct ast_variable **fields)
{
	*fields = ast_variable_new("toast-bob", "10", "");

	return 0;
}

/*! \brief Test structure for caching */
struct sorcery_test_caching {
	/*! \brief Whether the object has been created in the cache or not */
	unsigned int created:1;

	/*! \brief Whether the object has been updated in the cache or not */
	unsigned int updated:1;

	/*! \brief Whether the object has been deleted from the cache or not */
	unsigned int deleted:1;

	/*! \brief Object to return when asked */
	struct test_sorcery_object object;
};

/*! \brief Test structure for observer */
struct sorcery_test_observer {
	/*! \brief Lock for notification */
	ast_mutex_t lock;

	/*! \brief Condition for notification */
	ast_cond_t cond;

	/*! \brief Pointer to the created object */
	const void *created;

	/*! \brief Pointer to the update object */
	const void *updated;

	/*! \brief Pointer to the deleted object */
	const void *deleted;

	/*! \brief Whether the type has been loaded */
	unsigned int loaded:1;
};

/*! \brief Global scope apply handler integer to make sure it executed */
static int apply_handler_called;

/*! \brief Simple apply handler which sets global scope integer to 1 if called */
static int test_apply_handler(const struct ast_sorcery *sorcery, void *obj)
{
	apply_handler_called = 1;
	return 0;
}

/*! \brief Global scope caching structure for testing */
static struct sorcery_test_caching cache = { 0, };

/*! \brief Global scope observer structure for testing */
static struct sorcery_test_observer observer;

static int sorcery_test_create(const struct ast_sorcery *sorcery, void *data, void *object)
{
	cache.created = 1;
	cache.updated = 0;
	cache.deleted = 0;
	return 0;
}

static void *sorcery_test_retrieve_id(const struct ast_sorcery *sorcery, void *data, const char *type, const char *id)
{
	return (cache.created && !cache.deleted) ? ast_sorcery_alloc(sorcery, type, id) : NULL;
}

static int sorcery_test_update(const struct ast_sorcery *sorcery, void *data, void *object)
{
	cache.updated = 1;
	return 0;
}

static int sorcery_test_delete(const struct ast_sorcery *sorcery, void *data, void *object)
{
	cache.deleted = 1;
	return 0;
}

/*! \brief Dummy sorcery wizard, not actually used so we only populate the name and nothing else */
static struct ast_sorcery_wizard test_wizard = {
	.name = "test",
	.create = sorcery_test_create,
	.retrieve_id = sorcery_test_retrieve_id,
	.update = sorcery_test_update,
	.delete = sorcery_test_delete,
};

static void sorcery_observer_created(const void *object)
{
	SCOPED_MUTEX(lock, &observer.lock);
	observer.created = object;
	ast_cond_signal(&observer.cond);
}

static void sorcery_observer_updated(const void *object)
{
	SCOPED_MUTEX(lock, &observer.lock);
	observer.updated = object;
	ast_cond_signal(&observer.cond);
}

static void sorcery_observer_deleted(const void *object)
{
	SCOPED_MUTEX(lock, &observer.lock);
	observer.deleted = object;
	ast_cond_signal(&observer.cond);
}

static void sorcery_observer_loaded(const char *object_type)
{
	SCOPED_MUTEX(lock, &observer.lock);
	observer.loaded = 1;
	ast_cond_signal(&observer.cond);
}

/*! \brief Test sorcery observer implementation */
static struct ast_sorcery_observer test_observer = {
	.created = sorcery_observer_created,
	.updated = sorcery_observer_updated,
	.deleted = sorcery_observer_deleted,
	.loaded = sorcery_observer_loaded,
};

static struct ast_sorcery *alloc_and_initialize_sorcery(void)
{
	struct ast_sorcery *sorcery;

	if (!(sorcery = ast_sorcery_open())) {
		return NULL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "memory", NULL) ||
		ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_sorcery_unref(sorcery);
		return NULL;
	}

	ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	return sorcery;
}

AST_TEST_DEFINE(wizard_registration)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "wizard_registration";
		info->category = "/main/sorcery/";
		info->summary = "sorcery wizard registration and unregistration unit test";
		info->description =
			"Test registration and unregistration of a sorcery wizard";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_sorcery_wizard_register(&test_wizard)) {
		ast_test_status_update(test, "Failed to register a perfectly valid sorcery wizard\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_wizard_register(&test_wizard)) {
		ast_test_status_update(test, "Successfully registered a sorcery wizard twice, which is bad\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_wizard_unregister(&test_wizard)) {
		ast_test_status_update(test, "Failed to unregister a perfectly valid sorcery wizard\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_wizard_unregister(&test_wizard)) {
		ast_test_status_update(test, "Successfully unregistered a sorcery wizard twice, which is bad\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(sorcery_open)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "open";
		info->category = "/main/sorcery/";
		info->summary = "sorcery open unit test";
		info->description =
			"Test opening of sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open new sorcery structure\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(apply_default)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "apply_default";
		info->category = "/main/sorcery/";
		info->summary = "sorcery default wizard unit test";
		info->description =
			"Test setting default type wizard in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_apply_default(sorcery, "test", "dummy", NULL)) {
		ast_test_status_update(test, "Successfully set a default wizard that doesn't exist\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "memory", NULL)) {
		ast_test_status_update(test, "Failed to set a known wizard as a default\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_apply_default(sorcery, "test", "memory", NULL)) {
		ast_test_status_update(test, "Successfully set a default wizard on a type twice\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(apply_config)
{
	struct ast_flags flags = { CONFIG_FLAG_NOCACHE };
	struct ast_config *config;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "apply_config";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object mapping configuration unit test";
		info->description =
			"Test configured object mapping in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(config = ast_config_load2("sorcery.conf", "test_sorcery", flags))) {
		ast_test_status_update(test, "Sorcery configuration file not present - skipping apply_config test\n");
		return AST_TEST_NOT_RUN;
	}

	if (!ast_category_get(config, "test_sorcery")) {
		ast_test_status_update(test, "Sorcery configuration file does not have test_sorcery section\n");
		ast_config_destroy(config);
		return AST_TEST_NOT_RUN;
	}

	ast_config_destroy(config);

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_config(sorcery, "test_sorcery")) {
		ast_test_status_update(test, "Failed to apply configured object mappings\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_register)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_register";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object type registration unit test";
		info->description =
			"Test object type registration in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "memory", NULL)) {
		ast_test_status_update(test, "Failed to set a known wizard as a default\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Registered object type a second time, despite it being registered already\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_register_without_mapping)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_register_without_mapping";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object type registration (without mapping) unit test";
		info->description =
			"Test object type registration when no mapping exists in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Registered object type when no object mapping exists\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_field_register)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_field_register";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object field registration unit test";
		info->description =
			"Test object field registration in sorcery with a provided id";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob))) {
		ast_test_status_update(test, "Registered an object field successfully when no mappings or object types exist\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "memory", NULL)) {
		ast_test_status_update(test, "Failed to set a known wizard as a default\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob))) {
		ast_test_status_update(test, "Registered an object field successfully when object type does not exist\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob))) {
		ast_test_status_update(test, "Could not successfully register object field when mapping and object type exists\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_fields_register)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_fields_register";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object regex fields registration unit test";
		info->description =
			"Test object regex fields registration in sorcery with a provided id";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_object_fields_register(sorcery, "test", "^toast-", test_sorcery_regex_handler, test_sorcery_regex_fields)) {
		ast_test_status_update(test, "Registered a regex object field successfully when no mappings or object types exist\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "memory", NULL)) {
		ast_test_status_update(test, "Failed to set a known wizard as a default\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_object_fields_register(sorcery, "test", "^toast-", test_sorcery_regex_handler, test_sorcery_regex_fields)) {
		ast_test_status_update(test, "Registered a regex object field successfully when object type does not exist\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_object_fields_register(sorcery, "test", "^toast-", test_sorcery_regex_handler, test_sorcery_regex_fields)) {
		ast_test_status_update(test, "Registered a regex object field successfully when no mappings or object types exist\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_alloc_with_id)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_alloc_with_id";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object allocation (with id) unit test";
		info->description =
			"Test object allocation in sorcery with a provided id";
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
		res = AST_TEST_FAIL;
	} else if (ast_strlen_zero(ast_sorcery_object_get_id(obj))) {
		ast_test_status_update(test, "Allocated object has empty id when it should not\n");
		res = AST_TEST_FAIL;
	} else if (strcmp(ast_sorcery_object_get_id(obj), "blah")) {
		ast_test_status_update(test, "Allocated object does not have correct id\n");
		res = AST_TEST_FAIL;
	} else if (ast_strlen_zero(ast_sorcery_object_get_type(obj))) {
		ast_test_status_update(test, "Allocated object has empty type when it should not\n");
		res = AST_TEST_FAIL;
	} else if (strcmp(ast_sorcery_object_get_type(obj), "test")) {
		ast_test_status_update(test, "Allocated object does not have correct type\n");
		res = AST_TEST_FAIL;
	} else if ((obj->bob != 5) || (obj->joe != 10)) {
		ast_test_status_update(test, "Allocated object does not have defaults set as it should\n");
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(object_alloc_without_id)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_alloc_without_id";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object allocation (without id) unit test";
		info->description =
			"Test object allocation in sorcery with no provided id";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", NULL))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		res = AST_TEST_FAIL;
	} else if (ast_strlen_zero(ast_sorcery_object_get_id(obj))) {
		ast_test_status_update(test, "Allocated object has empty id when it should not\n");
		res = AST_TEST_FAIL;
	}

	return res;
}


AST_TEST_DEFINE(object_copy)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct test_sorcery_object *, copy, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_copy";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object copy unit test";
		info->description =
			"Test object copy in sorcery";
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

	obj->bob = 50;
	obj->joe = 100;

	if (!(copy = ast_sorcery_copy(sorcery, obj))) {
		ast_test_status_update(test, "Failed to create a copy of a known valid object\n");
		res = AST_TEST_FAIL;
	} else if (copy == obj) {
		ast_test_status_update(test, "Created copy is actually the original object\n");
		res = AST_TEST_FAIL;
	} else if (copy->bob != obj->bob) {
		ast_test_status_update(test, "Value of 'bob' on newly created copy is not the same as original\n");
		res = AST_TEST_FAIL;
	} else if (copy->joe != obj->joe) {
		ast_test_status_update(test, "Value of 'joe' on newly created copy is not the same as original\n");
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(object_copy_native)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct test_sorcery_object *, copy, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_copy_native";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object native copy unit test";
		info->description =
			"Test object native copy in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_set_copy_handler(sorcery, "test", test_sorcery_copy);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	obj->bob = 50;
	obj->joe = 100;

	if (!(copy = ast_sorcery_copy(sorcery, obj))) {
		ast_test_status_update(test, "Failed to create a copy of a known valid object\n");
		res = AST_TEST_FAIL;
	} else if (copy == obj) {
		ast_test_status_update(test, "Created copy is actually the original object\n");
		res = AST_TEST_FAIL;
	} else if (copy->bob != 10) {
		ast_test_status_update(test, "Value of 'bob' on newly created copy is not the predefined native copy value\n");
		res = AST_TEST_FAIL;
	} else if (copy->joe != 20) {
		ast_test_status_update(test, "Value of 'joe' on newly created copy is not the predefined native copy value\n");
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(object_diff)
{
       RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
       RAII_VAR(struct test_sorcery_object *, obj1, NULL, ao2_cleanup);
       RAII_VAR(struct test_sorcery_object *, obj2, NULL, ao2_cleanup);
       RAII_VAR(struct ast_variable *, changes, NULL, ast_variables_destroy);
       struct ast_variable *field;
       int res = AST_TEST_PASS;

       switch (cmd) {
       case TEST_INIT:
	       info->name = "object_diff";
	       info->category = "/main/sorcery/";
	       info->summary = "sorcery object diff unit test";
	       info->description =
		       "Test object diffing in sorcery";
	       return AST_TEST_NOT_RUN;
       case TEST_EXECUTE:
	       break;
       }

       if (!(sorcery = alloc_and_initialize_sorcery())) {
	       ast_test_status_update(test, "Failed to open sorcery structure\n");
	       return AST_TEST_FAIL;
       }

       if (!(obj1 = ast_sorcery_alloc(sorcery, "test", "blah"))) {
	       ast_test_status_update(test, "Failed to allocate a known object type\n");
	       return AST_TEST_FAIL;
       }

       obj1->bob = 99;
       obj1->joe = 55;

       if (!(obj2 = ast_sorcery_alloc(sorcery, "test", "blah2"))) {
	       ast_test_status_update(test, "Failed to allocate a second known object type\n");
	       return AST_TEST_FAIL;
       }

       obj2->bob = 99;
       obj2->joe = 42;

       if (ast_sorcery_diff(sorcery, obj1, obj2, &changes)) {
	       ast_test_status_update(test, "Failed to diff obj1 and obj2\n");
       } else if (!changes) {
	       ast_test_status_update(test, "Failed to produce a diff of two objects, despite there being differences\n");
	       return AST_TEST_FAIL;
       }

       for (field = changes; field; field = field->next) {
	       if (!strcmp(field->name, "joe")) {
		       if (strcmp(field->value, "42")) {
			       ast_test_status_update(test, "Object diff produced unexpected value '%s' for joe\n", field->value);
			       res = AST_TEST_FAIL;
		       }
	       } else {
		       ast_test_status_update(test, "Object diff produced unexpected field '%s'\n", field->name);
		       res = AST_TEST_FAIL;
	       }
       }

       return res;
}

AST_TEST_DEFINE(object_diff_native)
{
       RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
       RAII_VAR(struct test_sorcery_object *, obj1, NULL, ao2_cleanup);
       RAII_VAR(struct test_sorcery_object *, obj2, NULL, ao2_cleanup);
       RAII_VAR(struct ast_variable *, changes, NULL, ast_variables_destroy);
       struct ast_variable *field;
       int res = AST_TEST_PASS;

       switch (cmd) {
       case TEST_INIT:
	       info->name = "object_diff_native";
	       info->category = "/main/sorcery/";
	       info->summary = "sorcery object native diff unit test";
	       info->description =
		       "Test native object diffing in sorcery";
	       return AST_TEST_NOT_RUN;
       case TEST_EXECUTE:
	       break;
       }

       if (!(sorcery = alloc_and_initialize_sorcery())) {
	       ast_test_status_update(test, "Failed to open sorcery structure\n");
	       return AST_TEST_FAIL;
       }

       ast_sorcery_object_set_diff_handler(sorcery, "test", test_sorcery_diff);

       if (!(obj1 = ast_sorcery_alloc(sorcery, "test", "blah"))) {
	       ast_test_status_update(test, "Failed to allocate a known object type\n");
	       return AST_TEST_FAIL;
       }

       obj1->bob = 99;
       obj1->joe = 55;

       if (!(obj2 = ast_sorcery_alloc(sorcery, "test", "blah2"))) {
	       ast_test_status_update(test, "Failed to allocate a second known object type\n");
	       return AST_TEST_FAIL;
       }

       obj2->bob = 99;
       obj2->joe = 42;

       if (ast_sorcery_diff(sorcery, obj1, obj2, &changes)) {
	       ast_test_status_update(test, "Failed to diff obj1 and obj2\n");
       } else if (!changes) {
	       ast_test_status_update(test, "Failed to produce a diff of two objects, despite there being differences\n");
	       return AST_TEST_FAIL;
       }

       for (field = changes; field; field = field->next) {
	       if (!strcmp(field->name, "yes")) {
		       if (strcmp(field->value, "itworks")) {
			       ast_test_status_update(test, "Object diff produced unexpected value '%s' for joe\n", field->value);
			       res = AST_TEST_FAIL;
		       }
	       } else {
		       ast_test_status_update(test, "Object diff produced unexpected field '%s'\n", field->name);
		       res = AST_TEST_FAIL;
	       }
       }

       return res;
}

AST_TEST_DEFINE(objectset_create)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);
	struct ast_variable *field;

	switch (cmd) {
	case TEST_INIT:
		info->name = "objectset_create";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object set creation unit test";
		info->description =
			"Test object set creation in sorcery";
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

	if (!(objset = ast_sorcery_objectset_create(sorcery, obj))) {
		ast_test_status_update(test, "Failed to create an object set for a known sane object\n");
		return AST_TEST_FAIL;
	}

	for (field = objset; field; field = field->next) {
		if (!strcmp(field->name, "bob")) {
			if (strcmp(field->value, "5")) {
				ast_test_status_update(test, "Object set failed to create proper value for 'bob'\n");
				res = AST_TEST_FAIL;
			}
		} else if (!strcmp(field->name, "joe")) {
			if (strcmp(field->value, "10")) {
				ast_test_status_update(test, "Object set failed to create proper value for 'joe'\n");
				res = AST_TEST_FAIL;
			}
		} else {
			ast_test_status_update(test, "Object set created field '%s' which is unknown\n", field->name);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(objectset_json_create)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, objset, NULL, ast_json_unref);
	struct ast_json_iter *field;

	switch (cmd) {
	case TEST_INIT:
		info->name = "objectset_json_create";
		info->category = "/main/sorcery/";
		info->summary = "sorcery json object set creation unit test";
		info->description =
			"Test object set creation (for JSON format) in sorcery";
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

	if (!(objset = ast_sorcery_objectset_json_create(sorcery, obj))) {
		ast_test_status_update(test, "Failed to create an object set for a known sane object\n");
		return AST_TEST_FAIL;
	}

	for (field = ast_json_object_iter(objset); field; field = ast_json_object_iter_next(objset, field)) {
		struct ast_json *value = ast_json_object_iter_value(field);

		if (!strcmp(ast_json_object_iter_key(field), "bob")) {
			if (strcmp(ast_json_string_get(value), "5")) {
				ast_test_status_update(test, "Object set failed to create proper value for 'bob'\n");
				res = AST_TEST_FAIL;
			}
		} else if (!strcmp(ast_json_object_iter_key(field), "joe")) {
			if (strcmp(ast_json_string_get(value), "10")) {
				ast_test_status_update(test, "Object set failed to create proper value for 'joe'\n");
				res = AST_TEST_FAIL;
			}
		} else {
			ast_test_status_update(test, "Object set created field '%s' which is unknown\n", ast_json_object_iter_key(field));
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(objectset_create_regex)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);
	struct ast_variable *field;

	switch (cmd) {
	case TEST_INIT:
		info->name = "objectset_create_regex";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object set creation with regex fields unit test";
		info->description =
			"Test object set creation with regex fields in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "memory", NULL) ||
	    ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, test_apply_handler)) {
		ast_test_status_update(test, "Failed to register 'test' object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_fields_register(sorcery, "test", "^toast-", test_sorcery_regex_handler, test_sorcery_regex_fields);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (!(objset = ast_sorcery_objectset_create(sorcery, obj))) {
		ast_test_status_update(test, "Failed to create an object set for a known sane object\n");
		return AST_TEST_FAIL;
	}

	for (field = objset; field; field = field->next) {
		if (!strcmp(field->name, "toast-bob")) {
			if (strcmp(field->value, "10")) {
				ast_test_status_update(test, "Object set failed to create proper value for 'bob'\n");
				res = AST_TEST_FAIL;
			}
		} else {
			ast_test_status_update(test, "Object set created field '%s' which is unknown\n", field->name);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(objectset_apply)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "objectset_apply";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object apply unit test";
		info->description =
			"Test object set applying in sorcery";
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

	if (!(objset = ast_variable_new("joe", "25", ""))) {
		ast_test_status_update(test, "Failed to create an object set, test could not occur\n");
		res = AST_TEST_FAIL;
	} else if (ast_sorcery_objectset_apply(sorcery, obj, objset)) {
		ast_test_status_update(test, "Failed to apply valid object set to object\n");
		res = AST_TEST_FAIL;
	} else if (obj->joe != 25) {
		ast_test_status_update(test, "Object set was not actually applied to object despite it returning success\n");
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(objectset_apply_handler)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "objectset_apply_handler";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object apply handler unit test";
		info->description =
			"Test object set apply handler call in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "memory", NULL) ||
	    ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, test_apply_handler)) {
		ast_test_status_update(test, "Failed to register 'test' object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	apply_handler_called = 0;

	if (!(objset = ast_variable_new("joe", "25", ""))) {
		ast_test_status_update(test, "Failed to create an object set, test could not occur\n");
		res = AST_TEST_FAIL;
	} else if (ast_sorcery_objectset_apply(sorcery, obj, objset)) {
		ast_test_status_update(test, "Failed to apply valid object set to object\n");
		res = AST_TEST_FAIL;
	} else if (!apply_handler_called) {
		ast_test_status_update(test, "Apply handler was not called when it should have been\n");
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(objectset_apply_invalid)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "objectset_apply_invalid";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object invalid apply unit test";
		info->description =
			"Test object set applying of an invalid set in sorcery";
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

	if (!(objset = ast_variable_new("fred", "99", ""))) {
		ast_test_status_update(test, "Failed to create an object set, test could not occur\n");
		return AST_TEST_FAIL;
	} else if (!ast_sorcery_objectset_apply(sorcery, obj, objset)) {
		ast_test_status_update(test, "Successfully applied an invalid object set\n");
		return AST_TEST_FAIL;
	} else if ((obj->bob != 5) || (obj->joe != 10)) {
		ast_test_status_update(test, "Object set modified object fields when it should not have\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(objectset_transform)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "objectset_transform";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object set transformation unit test";
		info->description =
			"Test object set transformation in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "memory", NULL)) {
		ast_test_status_update(test, "Failed to set a known wizard as a default\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, test_sorcery_transform, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (!(objset = ast_sorcery_objectset_create(sorcery, obj))) {
		ast_test_status_update(test, "Failed to create an object set for a known sane object\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_objectset_apply(sorcery, obj, objset)) {
		ast_test_status_update(test, "Failed to apply properly created object set against object\n");
		return AST_TEST_FAIL;
	}

	if (obj->bob != 5) {
		ast_test_status_update(test, "Application of object set produced incorrect value on 'bob'\n");
		return AST_TEST_FAIL;
	} else if (obj->joe == 10) {
		ast_test_status_update(test, "Transformation callback did not change value of 'joe' from provided value\n");
		return AST_TEST_FAIL;
	} else if (obj->joe != 5000) {
		ast_test_status_update(test, "Value of 'joe' differs from default AND from transformation value\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(objectset_apply_fields)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, objset, NULL, ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "objectset_apply_fields";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object apply regex fields unit test";
		info->description =
			"Test object set apply with regex fields in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "memory", NULL) ||
	    ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, test_apply_handler)) {
		ast_test_status_update(test, "Failed to register 'test' object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_fields_register(sorcery, "test", "^toast-", test_sorcery_regex_handler, test_sorcery_regex_fields);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		return AST_TEST_FAIL;
	}

	if (!(objset = ast_variable_new("toast-bob", "20", ""))) {
		ast_test_status_update(test, "Failed to create an object set, test could not occur\n");
		res = AST_TEST_FAIL;
	} else if (ast_sorcery_objectset_apply(sorcery, obj, objset)) {
		ast_test_status_update(test, "Failed to apply valid object set to object\n");
		res = AST_TEST_FAIL;
	} else if (obj->bob != 256) {
		ast_test_status_update(test, "Regex field handler was not called when it should have been\n");
		res = AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(changeset_create)
{
	int res = AST_TEST_PASS;
	RAII_VAR(struct ast_variable *, original, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, modified, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, changes, NULL, ast_variables_destroy);
	struct ast_variable *tmp;

	switch (cmd) {
	case TEST_INIT:
		info->name = "changeset_create";
		info->category = "/main/sorcery/";
		info->summary = "sorcery changeset creation unit test";
		info->description =
			"Test changeset creation in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(tmp = ast_variable_new("bananas", "purple", ""))) {
		ast_test_status_update(test, "Failed to create first field for original objectset\n");
		return AST_TEST_FAIL;
	}
	tmp->next = original;
	original = tmp;

	if (!(tmp = ast_variable_new("apples", "orange", ""))) {
		ast_test_status_update(test, "Failed to create second field for original objectset\n");
		return AST_TEST_FAIL;
	}
	tmp->next = original;
	original = tmp;

	if (!(tmp = ast_variable_new("bananas", "green", ""))) {
		ast_test_status_update(test, "Failed to create first field for modified objectset\n");
		return AST_TEST_FAIL;
	}
	tmp->next = modified;
	modified = tmp;

	if (!(tmp = ast_variable_new("apples", "orange", ""))) {
		ast_test_status_update(test, "Failed to create second field for modified objectset\n");
		return AST_TEST_FAIL;
	}
	tmp->next = modified;
	modified = tmp;

	if (ast_sorcery_changeset_create(original, modified, &changes)) {
		ast_test_status_update(test, "Failed to create a changeset due to an error\n");
		return AST_TEST_FAIL;
	} else if (!changes) {
		ast_test_status_update(test, "Failed to produce a changeset when there should be one\n");
		return AST_TEST_FAIL;
	}

	for (tmp = changes; tmp; tmp = tmp->next) {
		if (!strcmp(tmp->name, "bananas")) {
			if (strcmp(tmp->value, "green")) {
				ast_test_status_update(test, "Changeset produced had unexpected value '%s' for bananas\n", tmp->value);
				res = AST_TEST_FAIL;
			}
		} else {
			ast_test_status_update(test, "Changeset produced had unexpected field '%s'\n", tmp->name);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(changeset_create_unchanged)
{
	RAII_VAR(struct ast_variable *, original, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, changes, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, same, NULL, ast_variables_destroy);
	struct ast_variable *tmp;

	switch (cmd) {
	case TEST_INIT:
		info->name = "changeset_create_unchanged";
		info->category = "/main/sorcery/";
		info->summary = "sorcery changeset creation unit test when no changes exist";
		info->description =
			"Test changeset creation in sorcery when no changes actually exist";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(tmp = ast_variable_new("bananas", "purple", ""))) {
		ast_test_status_update(test, "Failed to create first field for original objectset\n");
		return AST_TEST_FAIL;
	}
	tmp->next = original;
	original = tmp;

	if (!(tmp = ast_variable_new("apples", "orange", ""))) {
		ast_test_status_update(test, "Failed to create second field for original objectset\n");
		return AST_TEST_FAIL;
	}
	tmp->next = original;
	original = tmp;

	if (ast_sorcery_changeset_create(original, original, &changes)) {
		ast_test_status_update(test, "Failed to create a changeset due to an error\n");
		return AST_TEST_FAIL;
	} else if (changes) {
		ast_test_status_update(test, "Created a changeset when no changes actually exist\n");
		return AST_TEST_FAIL;
	}

	if (!(tmp = ast_variable_new("bananas", "purple", ""))) {
		ast_test_status_update(test, "Failed to create first field for same objectset\n");
		return AST_TEST_FAIL;
	}
	tmp->next = same;
	same = tmp;

	if (!(tmp = ast_variable_new("apples", "orange", ""))) {
		ast_test_status_update(test, "Failed to create second field for same objectset\n");
		return AST_TEST_FAIL;
	}
	tmp->next = same;
	same = tmp;

	if (ast_sorcery_changeset_create(original, same, &changes)) {
		ast_test_status_update(test, "Failed to create a changeset due to an error\n");
		return AST_TEST_FAIL;
	} else if (changes) {
		ast_test_status_update(test, "Created a changeset between two different objectsets when no changes actually exist\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_create)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_create";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object creation unit test";
		info->description =
			"Test object creation in sorcery";
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
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_retrieve_id)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_id";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object retrieval using id unit test";
		info->description =
			"Test object retrieval using id in sorcery";
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
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah2"))) {
		ast_test_status_update(test, "Failed to allocate second instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create second object using in-memory wizard\n");
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
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, fields, ast_variable_new("joe", "42", ""), ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_field";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object retrieval using a specific field unit test";
		info->description =
			"Test object retrieval using a specific field in sorcery";
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
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
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
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_multiple_all";
		info->category = "/main/sorcery/";
		info->summary = "sorcery multiple object retrieval unit test";
		info->description =
			"Test multiple object retrieval in sorcery";
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
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah2"))) {
		ast_test_status_update(test, "Failed to allocate second instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create second object using in-memory wizard\n");
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
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, fields, ast_variable_new("joe", "6", ""), ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_multiple_field";
		info->category = "/main/sorcery/";
		info->summary = "sorcery multiple object retrieval unit test";
		info->description =
			"Test multiple object retrieval in sorcery using fields";
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
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
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
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_retrieve_regex";
		info->category = "/main/sorcery/";
		info->summary = "sorcery multiple object retrieval using regex unit test";
		info->description =
			"Test multiple object retrieval in sorcery using regular expression for matching";
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
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah-93joe"))) {
		ast_test_status_update(test, "Failed to allocate second instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create second object using in-memory wizard\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "neener-93joe"))) {
		ast_test_status_update(test, "Failed to allocate third instance of a known object type\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create third object using in-memory wizard\n");
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
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct test_sorcery_object *, obj2, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_update";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object update unit test";
		info->description =
			"Test object updating in sorcery";
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
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
		return AST_TEST_FAIL;
	}

	if (!(obj2 = ast_sorcery_copy(sorcery, obj))) {
		ast_test_status_update(test, "Failed to allocate a known object type for updating\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(obj);

	if (ast_sorcery_update(sorcery, obj2)) {
		ast_test_status_update(test, "Failed to update sorcery with new object\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_retrieve_by_id(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to retrieve properly updated object\n");
		return AST_TEST_FAIL;
	} else if (obj != obj2) {
		ast_test_status_update(test, "Object retrieved is not the updated object\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(object_update_uncreated)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_update_uncreated";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object update unit test";
		info->description =
			"Test updating of an uncreated object in sorcery";
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
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_delete";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object deletion unit test";
		info->description =
			"Test object deletion in sorcery";
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
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_delete(sorcery, obj)) {
		ast_test_status_update(test, "Failed to delete object using in-memory wizard\n");
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
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_delete_uncreated";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object deletion unit test";
		info->description =
			"Test object deletion of an uncreated object in sorcery";
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

AST_TEST_DEFINE(caching_wizard_behavior)
{
	struct ast_flags flags = { CONFIG_FLAG_NOCACHE };
	struct ast_config *config;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct test_sorcery_object *, obj2, NULL, ao2_cleanup);
	int res = AST_TEST_FAIL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "caching_wizard_behavior";
		info->category = "/main/sorcery/";
		info->summary = "sorcery caching wizard behavior unit test";
		info->description =
			"Test internal behavior of caching wizards";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(config = ast_config_load2("sorcery.conf", "test_sorcery_cache", flags))) {
		ast_test_status_update(test, "Sorcery configuration file not present - skipping caching_wizard_behavior test\n");
		return AST_TEST_NOT_RUN;
	}

	if (!ast_category_get(config, "test_sorcery_cache")) {
		ast_test_status_update(test, "Sorcery configuration file does not contain 'test_sorcery_cache' section\n");
		ast_config_destroy(config);
		return AST_TEST_NOT_RUN;
	}

	ast_config_destroy(config);

	if (ast_sorcery_wizard_register(&test_wizard)) {
		ast_test_status_update(test, "Failed to register a perfectly valid sorcery wizard\n");
		return AST_TEST_FAIL;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		goto end;
	}

	if (ast_sorcery_apply_config(sorcery, "test_sorcery_cache")) {
		ast_test_status_update(test, "Failed to apply configured object mappings\n");
		goto end;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		goto end;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		goto end;
	}

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
		goto end;
	}

	ao2_cleanup(obj);

	if (!(obj = ast_sorcery_retrieve_by_id(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to retrieve just created object\n");
		goto end;
	} else if (!cache.created) {
		ast_test_status_update(test, "Caching wizard was not told to cache just created object\n");
		goto end;
	} else if (!(obj2 = ast_sorcery_retrieve_by_id(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to retrieve just cached object\n");
		goto end;
	} else if (obj == obj2) {
		ast_test_status_update(test, "Returned object is *NOT* a cached object\n");
		goto end;
	} else if (ast_sorcery_update(sorcery, obj)) {
		ast_test_status_update(test, "Failed to update a known stored object\n");
		goto end;
	} else if (!cache.updated) {
		ast_test_status_update(test, "Caching wizard was not told to update object\n");
		goto end;
	} else if (ast_sorcery_delete(sorcery, obj)) {
		ast_test_status_update(test, "Failed to delete a known stored object\n");
		goto end;
	} else if (!cache.deleted) {
		ast_test_status_update(test, "Caching wizard was not told to delete object\n");
		goto end;
	}

	ao2_cleanup(obj2);

	if ((obj2 = ast_sorcery_retrieve_by_id(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Retrieved an object that should have been deleted\n");
		goto end;
	}

	res = AST_TEST_PASS;

end:
	ast_sorcery_unref(sorcery);
	sorcery = NULL;

	if (ast_sorcery_wizard_unregister(&test_wizard)) {
		ast_test_status_update(test, "Failed to unregister test sorcery wizard\n");
		return AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(object_type_observer)
{
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	int res = AST_TEST_FAIL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "object_type_observer";
		info->category = "/main/sorcery/";
		info->summary = "sorcery object type observer unit test";
		info->description =
			"Test that object type observers get called when they should";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(sorcery = alloc_and_initialize_sorcery())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (!ast_sorcery_observer_add(sorcery, "test", NULL)) {
		ast_test_status_update(test, "Successfully added a NULL observer when it should not be possible\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_observer_add(sorcery, "test", &test_observer)) {
		ast_test_status_update(test, "Failed to add a proper observer\n");
		return AST_TEST_FAIL;
	}

	if (!(obj = ast_sorcery_alloc(sorcery, "test", "blah"))) {
		ast_test_status_update(test, "Failed to allocate a known object type\n");
		goto end;
	}

	ast_mutex_init(&observer.lock);
	ast_cond_init(&observer.cond, NULL);
	observer.created = NULL;
	observer.updated = NULL;
	observer.deleted = NULL;

	if (ast_sorcery_create(sorcery, obj)) {
		ast_test_status_update(test, "Failed to create object using in-memory wizard\n");
		goto end;
	}

	ast_mutex_lock(&observer.lock);
	while (!observer.created) {
        struct timeval start = ast_tvnow();
        struct timespec end = {
                .tv_sec = start.tv_sec + 10,
                .tv_nsec = start.tv_usec * 1000,
        };
		if (ast_cond_timedwait(&observer.cond, &observer.lock, &end) == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&observer.lock);

	if (!observer.created) {
		ast_test_status_update(test, "Failed to receive observer notification for object creation within suitable timeframe\n");
		goto end;
	}

	if (ast_sorcery_update(sorcery, obj)) {
		ast_test_status_update(test, "Failed to update object using in-memory wizard\n");
		goto end;
	}

	ast_mutex_lock(&observer.lock);
	while (!observer.updated) {
        struct timeval start = ast_tvnow();
        struct timespec end = {
                .tv_sec = start.tv_sec + 10,
                .tv_nsec = start.tv_usec * 1000,
        };
		if (ast_cond_timedwait(&observer.cond, &observer.lock, &end) == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&observer.lock);

	if (!observer.updated) {
		ast_test_status_update(test, "Failed to receive observer notification for object updating within suitable timeframe\n");
		goto end;
	}

	if (ast_sorcery_delete(sorcery, obj)) {
		ast_test_status_update(test, "Failed to delete object using in-memory wizard\n");
		goto end;
	}

	ast_mutex_lock(&observer.lock);
	while (!observer.deleted) {
        struct timeval start = ast_tvnow();
        struct timespec end = {
                .tv_sec = start.tv_sec + 10,
                .tv_nsec = start.tv_usec * 1000,
        };
		if (ast_cond_timedwait(&observer.cond, &observer.lock, &end) == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&observer.lock);

	if (!observer.deleted) {
		ast_test_status_update(test, "Failed to receive observer notification for object deletion within suitable timeframe\n");
		goto end;
	}

	ast_sorcery_reload(sorcery);

	ast_mutex_lock(&observer.lock);
	while (!observer.loaded) {
        struct timeval start = ast_tvnow();
        struct timespec end = {
                .tv_sec = start.tv_sec + 10,
                .tv_nsec = start.tv_usec * 1000,
        };
		if (ast_cond_timedwait(&observer.cond, &observer.lock, &end) == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&observer.lock);

	if (!observer.loaded) {
		ast_test_status_update(test, "Failed to receive observer notification for object type load within suitable timeframe\n");
		goto end;
	}

	res = AST_TEST_PASS;

end:
	observer.created = NULL;
	observer.updated = NULL;
	observer.deleted = NULL;
	ast_mutex_destroy(&observer.lock);
	ast_cond_destroy(&observer.cond);

	return res;
}

AST_TEST_DEFINE(configuration_file_wizard)
{
	struct ast_flags flags = { CONFIG_FLAG_NOCACHE };
	struct ast_config *config;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "configuration_file_wizard";
		info->category = "/main/sorcery/";
		info->summary = "sorcery configuration file wizard unit test";
		info->description =
			"Test the configuration file wizard in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(config = ast_config_load2("test_sorcery.conf", "test_sorcery", flags))) {
		ast_test_status_update(test, "Test sorcery configuration file wizard file not present - skipping configuration_file_wizard test\n");
		return AST_TEST_NOT_RUN;
	}

	ast_config_destroy(config);

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "config", "test_sorcery.conf")) {
		ast_test_status_update(test, "Could not set a default wizard of the 'config' type, so skipping since it may not be loaded\n");
		return AST_TEST_NOT_RUN;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	ast_sorcery_load(sorcery);

	if ((obj = ast_sorcery_retrieve_by_id(sorcery, "test", "hey2"))) {
		ast_test_status_update(test, "Retrieved object which has an unknown field\n");
		return AST_TEST_FAIL;
	} else if (!(obj = ast_sorcery_retrieve_by_id(sorcery, "test", "hey"))) {
		ast_test_status_update(test, "Failed to retrieve a known object that has been configured in the configuration file\n");
		return AST_TEST_FAIL;
	} else if (obj->bob != 98) {
		ast_test_status_update(test, "Value of 'bob' on object is not what is configured in configuration file\n");
		return AST_TEST_FAIL;
	} else if (obj->joe != 41) {
		ast_test_status_update(test, "Value of 'joe' on object is not what is configured in configuration file\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(configuration_file_wizard_with_file_integrity)
{
	struct ast_flags flags = { CONFIG_FLAG_NOCACHE };
	struct ast_config *config;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "configuration_file_wizard_with_file_integrity";
		info->category = "/main/sorcery/";
		info->summary = "sorcery configuration file wizard file integrity unit test";
		info->description =
			"Test the configuration file wizard with file integrity turned on in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(config = ast_config_load2("test_sorcery.conf", "test_sorcery", flags))) {
		ast_test_status_update(test, "Test sorcery configuration file wizard file not present - skipping configuration_file_wizard_with_file_integrity test\n");
		return AST_TEST_NOT_RUN;
	}

	ast_config_destroy(config);

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "config", "test_sorcery.conf,integrity=file")) {
		ast_test_status_update(test, "Could not set a default wizard of the 'config' type, so skipping since it may not be loaded\n");
		return AST_TEST_NOT_RUN;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	ast_sorcery_load(sorcery);

	if ((obj = ast_sorcery_retrieve_by_id(sorcery, "test", "hey"))) {
		ast_test_status_update(test, "Retrieved object which has an unknown field\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(configuration_file_wizard_with_criteria)
{
	struct ast_flags flags = { CONFIG_FLAG_NOCACHE };
	struct ast_config *config;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "configuration_file_wizard_with_criteria";
		info->category = "/main/sorcery/";
		info->summary = "sorcery configuration file wizard with criteria unit test";
		info->description =
			"Test the configuration file wizard with criteria matching in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(config = ast_config_load2("test_sorcery.conf", "test_sorcery", flags))) {
		ast_test_status_update(test, "Test sorcery configuration file wizard file not present - skipping configuration_file_wizard_with_criteria test\n");
		return AST_TEST_NOT_RUN;
	}

	ast_config_destroy(config);

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "config", "test_sorcery.conf,criteria=type=zombies")) {
		ast_test_status_update(test, "Could not set a default wizard of the 'config' type, so skipping since it may not be loaded\n");
		return AST_TEST_NOT_RUN;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));
	ast_sorcery_object_field_register(sorcery, "test", "type", NULL, OPT_NOOP_T, 0, NULL);

	ast_sorcery_load(sorcery);

	if ((obj = ast_sorcery_retrieve_by_id(sorcery, "test", "hey"))) {
		ast_test_status_update(test, "Retrieved object which did not match criteria\n");
		return AST_TEST_FAIL;
	} else if (!(obj = ast_sorcery_retrieve_by_id(sorcery, "test", "hey2"))) {
		ast_test_status_update(test, "Failed to retrieve a known object which matches criteria\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(configuration_file_wizard_retrieve_field)
{
	struct ast_flags flags = { CONFIG_FLAG_NOCACHE };
	struct ast_config *config;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct test_sorcery_object *, obj, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, fields, ast_variable_new("joe", "41", ""), ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "configuration_file_wizard_retrieve_field";
		info->category = "/main/sorcery/";
		info->summary = "sorcery configuration file wizard field retrieval unit test";
		info->description =
			"Test the configuration file wizard retrieval using field in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(config = ast_config_load2("test_sorcery.conf", "test_sorcery", flags))) {
		ast_test_status_update(test, "Test sorcery configuration file wizard file not present - skipping configuration_file_wizard_retrieve_field test\n");
		return AST_TEST_NOT_RUN;
	}

	ast_config_destroy(config);

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "config", "test_sorcery.conf")) {
		ast_test_status_update(test, "Could not set a default wizard of the 'config' type, so skipping since it may not be loaded\n");
		return AST_TEST_NOT_RUN;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	ast_sorcery_load(sorcery);

	if (!(obj = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_DEFAULT, fields))) {
		ast_test_status_update(test, "Failed to retrieve a known object that has been configured with the correct field\n");
		return AST_TEST_FAIL;
	} else if (strcmp(ast_sorcery_object_get_id(obj), "hey")) {
		ast_test_status_update(test, "Retrieved object has incorrect object id of '%s'\n", ast_sorcery_object_get_id(obj));
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(configuration_file_wizard_retrieve_multiple)
{
	struct ast_flags flags = { CONFIG_FLAG_NOCACHE };
	struct ast_config *config;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, fields, ast_variable_new("joe", "99", ""), ast_variables_destroy);

	switch (cmd) {
	case TEST_INIT:
		info->name = "configuration_file_wizard_retrieve_multiple";
		info->category = "/main/sorcery/";
		info->summary = "sorcery configuration file wizard multiple retrieval unit test";
		info->description =
			"Test the configuration file wizard multiple retrieval in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(config = ast_config_load2("test_sorcery.conf", "test_sorcery", flags))) {
		ast_test_status_update(test, "Test sorcery configuration file wizard file not present - skipping configuration_file_wizard_retrieve_multiple test\n");
		return AST_TEST_NOT_RUN;
	}

	ast_config_destroy(config);

	if (!fields) {
		ast_test_status_update(test, "Failed to create fields for multiple retrieve\n");
		return AST_TEST_FAIL;
	}

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "config", "test_sorcery.conf")) {
		ast_test_status_update(test, "Could not set a default wizard of the 'config' type, so skipping since it may not be loaded\n");
		return AST_TEST_NOT_RUN;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	ast_sorcery_load(sorcery);

	if (!(objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE, fields))) {
		ast_test_status_update(test, "Failed to retrieve an empty container when retrieving multiple\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects)) {
		ast_test_status_update(test, "Received a container with objects when there should be none in it\n");
		return AST_TEST_FAIL;
	}

	ao2_cleanup(objects);
	ast_variables_destroy(fields);

	if (!(fields = ast_variable_new("joe", "41", ""))) {
		ast_test_status_update(test, "Failed to create fields for multiple retrieve\n");
		return AST_TEST_FAIL;
	} else if (!(objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE, fields))) {
		ast_test_status_update(test, "Failed to retrieve a container when retrieving multiple\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects) != 1) {
		ast_test_status_update(test, "Received a container with no objects in it when there should be\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(configuration_file_wizard_retrieve_multiple_all)
{
	struct ast_flags flags = { CONFIG_FLAG_NOCACHE };
	struct ast_config *config;
	RAII_VAR(struct ast_sorcery *, sorcery, NULL, ast_sorcery_unref);
	RAII_VAR(struct ao2_container *, objects, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "configuration_file_wizard_retrieve_multiple_all";
		info->category = "/main/sorcery/";
		info->summary = "sorcery configuration file wizard multiple retrieve all unit test";
		info->description =
			"Test the configuration file wizard multiple retrieve all in sorcery";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(config = ast_config_load2("test_sorcery.conf", "test_sorcery", flags))) {
		ast_test_status_update(test, "Test sorcery configuration file wizard file not present - skipping configuration_file_wizard_retrieve_multiple_all test\n");
		return AST_TEST_NOT_RUN;
	}

	ast_config_destroy(config);

	if (!(sorcery = ast_sorcery_open())) {
		ast_test_status_update(test, "Failed to open sorcery structure\n");
		return AST_TEST_FAIL;
	}

	if (ast_sorcery_apply_default(sorcery, "test", "config", "test_sorcery.conf")) {
		ast_test_status_update(test, "Could not set a default wizard of the 'config' type, so skipping since it may not be loaded\n");
		return AST_TEST_NOT_RUN;
	}

	if (ast_sorcery_object_register(sorcery, "test", test_sorcery_object_alloc, NULL, NULL)) {
		ast_test_status_update(test, "Failed to register object type\n");
		return AST_TEST_FAIL;
	}

	ast_sorcery_object_field_register(sorcery, "test", "bob", "5", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, bob));
	ast_sorcery_object_field_register(sorcery, "test", "joe", "10", OPT_UINT_T, 0, FLDSET(struct test_sorcery_object, joe));

	ast_sorcery_load(sorcery);

	if (!(objects = ast_sorcery_retrieve_by_fields(sorcery, "test", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL))) {
		ast_test_status_update(test, "Failed to retrieve a container with all objects when there should be one\n");
		return AST_TEST_FAIL;
	} else if (ao2_container_count(objects) != 2) {
		ast_test_status_update(test, "Returned container does not have the correct number of objects in it\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(wizard_registration);
	AST_TEST_UNREGISTER(sorcery_open);
	AST_TEST_UNREGISTER(apply_default);
	AST_TEST_UNREGISTER(apply_config);
	AST_TEST_UNREGISTER(object_register);
	AST_TEST_UNREGISTER(object_register_without_mapping);
	AST_TEST_UNREGISTER(object_field_register);
	AST_TEST_UNREGISTER(object_fields_register);
	AST_TEST_UNREGISTER(object_alloc_with_id);
	AST_TEST_UNREGISTER(object_alloc_without_id);
	AST_TEST_UNREGISTER(object_copy);
	AST_TEST_UNREGISTER(object_copy_native);
	AST_TEST_UNREGISTER(object_diff);
	AST_TEST_UNREGISTER(object_diff_native);
	AST_TEST_UNREGISTER(objectset_create);
	AST_TEST_UNREGISTER(objectset_json_create);
	AST_TEST_UNREGISTER(objectset_create_regex);
	AST_TEST_UNREGISTER(objectset_apply);
	AST_TEST_UNREGISTER(objectset_apply_handler);
	AST_TEST_UNREGISTER(objectset_apply_invalid);
	AST_TEST_UNREGISTER(objectset_transform);
	AST_TEST_UNREGISTER(objectset_apply_fields);
	AST_TEST_UNREGISTER(changeset_create);
	AST_TEST_UNREGISTER(changeset_create_unchanged);
	AST_TEST_UNREGISTER(object_create);
	AST_TEST_UNREGISTER(object_retrieve_id);
	AST_TEST_UNREGISTER(object_retrieve_field);
	AST_TEST_UNREGISTER(object_retrieve_multiple_all);
	AST_TEST_UNREGISTER(object_retrieve_multiple_field);
	AST_TEST_UNREGISTER(object_retrieve_regex);
	AST_TEST_UNREGISTER(object_update);
	AST_TEST_UNREGISTER(object_update_uncreated);
	AST_TEST_UNREGISTER(object_delete);
	AST_TEST_UNREGISTER(object_delete_uncreated);
	AST_TEST_UNREGISTER(caching_wizard_behavior);
	AST_TEST_UNREGISTER(object_type_observer);
	AST_TEST_UNREGISTER(configuration_file_wizard);
	AST_TEST_UNREGISTER(configuration_file_wizard_with_file_integrity);
	AST_TEST_UNREGISTER(configuration_file_wizard_with_criteria);
	AST_TEST_UNREGISTER(configuration_file_wizard_retrieve_field);
	AST_TEST_UNREGISTER(configuration_file_wizard_retrieve_multiple);
	AST_TEST_UNREGISTER(configuration_file_wizard_retrieve_multiple_all);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(wizard_registration);
	AST_TEST_REGISTER(sorcery_open);
	AST_TEST_REGISTER(apply_default);
	AST_TEST_REGISTER(apply_config);
	AST_TEST_REGISTER(object_register);
	AST_TEST_REGISTER(object_register_without_mapping);
	AST_TEST_REGISTER(object_field_register);
	AST_TEST_REGISTER(object_fields_register);
	AST_TEST_REGISTER(object_alloc_with_id);
	AST_TEST_REGISTER(object_alloc_without_id);
	AST_TEST_REGISTER(object_copy);
	AST_TEST_REGISTER(object_copy_native);
	AST_TEST_REGISTER(object_diff);
	AST_TEST_REGISTER(object_diff_native);
	AST_TEST_REGISTER(objectset_create);
	AST_TEST_REGISTER(objectset_json_create);
	AST_TEST_REGISTER(objectset_create_regex);
	AST_TEST_REGISTER(objectset_apply);
	AST_TEST_REGISTER(objectset_apply_handler);
	AST_TEST_REGISTER(objectset_apply_invalid);
	AST_TEST_REGISTER(objectset_transform);
	AST_TEST_REGISTER(objectset_apply_fields);
	AST_TEST_REGISTER(changeset_create);
	AST_TEST_REGISTER(changeset_create_unchanged);
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
	AST_TEST_REGISTER(caching_wizard_behavior);
	AST_TEST_REGISTER(object_type_observer);
	AST_TEST_REGISTER(configuration_file_wizard);
	AST_TEST_REGISTER(configuration_file_wizard_with_file_integrity);
	AST_TEST_REGISTER(configuration_file_wizard_with_criteria);
	AST_TEST_REGISTER(configuration_file_wizard_retrieve_field);
	AST_TEST_REGISTER(configuration_file_wizard_retrieve_multiple);
	AST_TEST_REGISTER(configuration_file_wizard_retrieve_multiple_all);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Sorcery test module");
