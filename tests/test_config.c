/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \brief Configuration unit tests
 *
 * \author Mark Michelson <mmichelson@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include <math.h> /* HUGE_VAL */

#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/paths.h"
#include "asterisk/config_options.h"
#include "asterisk/netsock2.h"
#include "asterisk/acl.h"
#include "asterisk/frame.h"
#include "asterisk/utils.h"
#include "asterisk/logger.h"

#define CONFIG_FILE "test_config.conf"

/*
 * This builds the folowing config:
 * [Capitals]
 * Germany = Berlin
 * China = Beijing
 * Canada = Ottawa
 *
 * [Protagonists]
 * 1984 = Winston Smith
 * Green Eggs And Ham = Sam I Am
 * The Kalevala = Vainamoinen
 *
 * This config is used for all tests below.
 */
const char cat1[] = "Capitals";
const char cat1varname1[] = "Germany";
const char cat1varvalue1[] = "Berlin";
const char cat1varname2[] = "China";
const char cat1varvalue2[] = "Beijing";
const char cat1varname3[] = "Canada";
const char cat1varvalue3[] = "Ottawa";

const char cat2[] = "Protagonists";
const char cat2varname1[] = "1984";
const char cat2varvalue1[] = "Winston Smith";
const char cat2varname2[] = "Green Eggs And Ham";
const char cat2varvalue2[] = "Sam I Am";
const char cat2varname3[] = "The Kalevala";
const char cat2varvalue3[] = "Vainamoinen";

struct pair {
	const char *name;
	const char *val;
};

struct association {
	const char *category;
	struct pair vars[3];
} categories [] = {
	{ cat1,
		{
			{ cat1varname1, cat1varvalue1 },
			{ cat1varname2, cat1varvalue2 },
			{ cat1varname3, cat1varvalue3 },
		}
	},
	{ cat2,
		{
			{ cat2varname1, cat2varvalue1 },
			{ cat2varname2, cat2varvalue2 },
			{ cat2varname3, cat2varvalue3 },
		}
	},
};

/*!
 * \brief Build ast_config struct from above definitions
 *
 * \retval NULL Failed to build the config
 * \retval non-NULL An ast_config struct populated with data
 */
static struct ast_config *build_cfg(void)
{
	struct ast_config *cfg;
	struct association *cat_iter;
	struct pair *var_iter;
	size_t i;
	size_t j;

	cfg = ast_config_new();
	if (!cfg) {
		goto fail;
	}

	for (i = 0; i < ARRAY_LEN(categories); ++i) {
		struct ast_category *cat;
		cat_iter = &categories[i];

		cat = ast_category_new(cat_iter->category, "", 999999);
		if (!cat) {
			goto fail;
		}
		ast_category_append(cfg, cat);

		for (j = 0; j < ARRAY_LEN(cat_iter->vars); ++j) {
			struct ast_variable *var;
			var_iter = &cat_iter->vars[j];

			var = ast_variable_new(var_iter->name, var_iter->val, "");
			if (!var) {
				goto fail;
			}
			ast_variable_append(cat, var);
		}
	}

	return cfg;

fail:
	ast_config_destroy(cfg);
	return NULL;
}

/*!
 * \brief Tests that the contents of an ast_config is what is expected
 *
 * \param cfg Config to test
 * \retval -1 Failed to pass a test
 * \retval 0 Config passes checks
 */
static int test_config_validity(struct ast_config *cfg)
{
	int i;
	const char *cat_iter = NULL;
	/* Okay, let's see if the correct content is there */
	for (i = 0; i < ARRAY_LEN(categories); ++i) {
		struct ast_variable *var = NULL;
		size_t j;
		cat_iter = ast_category_browse(cfg, cat_iter);
		if (strcmp(cat_iter, categories[i].category)) {
			ast_log(LOG_ERROR, "Category name mismatch, %s does not match %s\n", cat_iter, categories[i].category);
			return -1;
		}
		for (j = 0; j < ARRAY_LEN(categories[i].vars); ++j) {
			var = var ? var->next : ast_variable_browse(cfg, cat_iter);
			if (strcmp(var->name, categories[i].vars[j].name)) {
				ast_log(LOG_ERROR, "Variable name mismatch, %s does not match %s\n", var->name, categories[i].vars[j].name);
				return -1;
			}
			if (strcmp(var->value, categories[i].vars[j].val)) {
				ast_log(LOG_ERROR, "Variable value mismatch, %s does not match %s\n", var->value, categories[i].vars[j].val);
				return -1;
			}
		}
	}
	return 0;
}

AST_TEST_DEFINE(copy_config)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_config *cfg = NULL;
	struct ast_config *copy = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "copy_config";
		info->category = "/main/config/";
		info->summary = "Test copying configuration";
		info->description =
			"Ensure that variables and categories are copied correctly";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cfg = build_cfg();
	if (!cfg) {
		goto out;
	}

	copy = ast_config_copy(cfg);
	if (!copy) {
		goto out;
	}

	if (test_config_validity(copy) != 0) {
		goto out;
	}

	res = AST_TEST_PASS;

out:
	ast_config_destroy(cfg);
	ast_config_destroy(copy);
	return res;
}

AST_TEST_DEFINE(config_basic_ops)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_config *cfg = NULL;
	struct ast_category *cat = NULL;
	struct ast_variable *var;
	char temp[32];
	const char *cat_name;
	const char *var_value;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "config_basic_ops";
		info->category = "/main/config/";
		info->summary = "Test basic config ops";
		info->description =	"Test basic config ops";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cfg = ast_config_new();
	if (!cfg) {
		return res;
	}

	/* load the config */
	for(i = 0; i < 5; i++) {
		snprintf(temp, sizeof(temp), "test%d", i);
		ast_category_append(cfg, ast_category_new(temp, "dummy", -1));
	}

	/* test0 test1 test2 test3 test4 */
	/* check the config has 5 elements */
	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, NULL, cat, NULL))) {
		snprintf(temp, sizeof(temp), "test%d", i);
		if (strcmp(ast_category_get_name(cat), temp)) {
			ast_test_status_update(test, "%s != %s\n", ast_category_get_name(cat), temp);
			goto out;
		}
		i++;
	}
	if (i != 5) {
		ast_test_status_update(test, "There were %d matches instead of 5.\n", i);
		goto out;
	}

	/* search for test2 */
	cat = ast_category_get(cfg, "test2", NULL);
	if (!cat || strcmp(ast_category_get_name(cat), "test2")) {
		ast_test_status_update(test, "Get failed %s != %s\n", ast_category_get_name(cat), "test2");
		goto out;
	}

	/* delete test2 */
	cat = ast_category_delete(cfg, cat);

	/* Now: test0 test1 test3 test4 */
	/* make sure the curr category is test1 */
	if (!cat || strcmp(ast_category_get_name(cat), "test1")) {
		ast_test_status_update(test, "Delete failed %s != %s\n", ast_category_get_name(cat), "test1");
		goto out;
	}

	/* Now: test0 test1 test3 test4 */
	/* make sure the test2 is not found */
	cat = ast_category_get(cfg, "test2", NULL);
	if (cat) {
		ast_test_status_update(test, "Should not have found test2\n");
		goto out;
	}

	/* Now: test0 test1 test3 test4 */
	/* make sure the sequence is correctly missing test2 */
	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, NULL, cat, NULL))) {
		snprintf(temp, sizeof(temp), "test%d", i);
		if (strcmp(ast_category_get_name(cat), temp)) {
			ast_test_status_update(test, "%s != %s\n", ast_category_get_name(cat), temp);
			goto out;
		}
		i++;
		if (i == 2) {
			i++;
		}
	}
	if (i != 5) {
		ast_test_status_update(test, "There were %d matches instead of 5.\n", i);
		goto out;
	}

	/* insert test2 back in before test3 */
	ast_category_insert(cfg, ast_category_new("test2", "dummy", -1), "test3");

	/* Now: test0 test1 test2 test3 test4 */
	/* make sure the sequence is correct again */
	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, NULL, cat, NULL))) {
		snprintf(temp, sizeof(temp), "test%d", i);
		if (strcmp(ast_category_get_name(cat), temp)) {
			ast_test_status_update(test, "%s != %s\n", ast_category_get_name(cat), temp);
			goto out;
		}
		i++;
	}
	if (i != 5) {
		ast_test_status_update(test, "There were %d matches instead of 5.\n", i);
		goto out;
	}

	/* Now: test0 test1 test2 test3 test4 */
	/* make sure non filtered browse still works */
	i = 0;
	cat_name = NULL;
	while ((cat_name = ast_category_browse(cfg, cat_name))) {
		snprintf(temp, sizeof(temp), "test%d", i);
		if (strcmp(cat_name, temp)) {
			ast_test_status_update(test, "%s != %s\n", cat_name, temp);
			goto out;
		}
		i++;
	}
	if (i != 5) {
		ast_test_status_update(test, "There were %d matches instead of 5.\n", i);
		goto out;
	}

	/* append another test2 */
	ast_category_append(cfg, ast_category_new("test2", "dummy", -1));
	/* Now: test0 test1 test2 test3 test4 test2*/
	/* make sure only test2's are returned */
	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, "test2", cat, NULL))) {
		if (strcmp(ast_category_get_name(cat), "test2")) {
			ast_test_status_update(test, "Should have returned test2 instead of %s\n", ast_category_get_name(cat));
			goto out;
		}
		i++;
	}
	/* make sure 2 test2's were found */
	if (i != 2) {
		ast_test_status_update(test, "Should have found 2 test2's %d\n", i);
		goto out;
	}

	/* Test in-flight deletion using ast_category_browse_filtered */
	/* Now: test0 test1 test2 test3 test4 test2 */
	/* Delete the middle test2 and continue */
	cat = NULL;
	for(i = 0; i < 5; i++) {
		snprintf(temp, sizeof(temp), "test%d", i);
		cat = ast_category_browse_filtered(cfg, NULL, cat, NULL);
		cat_name = ast_category_get_name(cat);
		if (strcmp(cat_name, temp)) {
			ast_test_status_update(test, "Should have returned %s instead of %s: %d\n", temp, cat_name, i);
			goto out;
		}
		if (i == 2) {
			cat = ast_category_delete(cfg, cat);
		}
	}

	/* Now: test0 test3 test4 test2 */
	/* delete the head item */
	cat = ast_category_browse_filtered(cfg, NULL, NULL, NULL);
	cat_name = ast_category_get_name(cat);
	if (strcmp(cat_name, "test0")) {
		ast_test_status_update(test, "Should have returned test0 instead of %s\n", cat_name);
		goto out;
	}
	ast_category_delete(cfg, cat);
	/* Now: test3 test4 test2 */

	/* make sure head got updated to the new first element */
	cat = ast_category_browse_filtered(cfg, NULL, NULL, NULL);
	cat_name = ast_category_get_name(cat);
	if (strcmp(cat_name, "test1")) {
		ast_test_status_update(test, "Should have returned test3 instead of %s\n", cat_name);
		goto out;
	}

	/* delete the tail item */
	cat = ast_category_get(cfg, "test2", NULL);
	cat_name = ast_category_get_name(cat);
	if (strcmp(cat_name, "test2")) {
		ast_test_status_update(test, "Should have returned test2 instead of %s\n", cat_name);
		goto out;
	}
	ast_category_delete(cfg, cat);
	/* Now: test3 test4 */

	/* There should now only be 2 elements in the list */
	cat = NULL;
	cat = ast_category_browse_filtered(cfg, NULL, cat, NULL);
	cat_name = ast_category_get_name(cat);
	if (strcmp(cat_name, "test1")) {
		ast_test_status_update(test, "Should have returned test1 instead of %s\n", cat_name);
		goto out;
	}

	cat = ast_category_browse_filtered(cfg, NULL, cat, NULL);
	cat_name = ast_category_get_name(cat);
	if (strcmp(cat_name, "test3")) {
		ast_test_status_update(test, "Should have returned test3 instead of %s\n", cat_name);
		goto out;
	}

	cat = ast_category_browse_filtered(cfg, NULL, cat, NULL);
	cat_name = ast_category_get_name(cat);
	if (strcmp(cat_name, "test4")) {
		ast_test_status_update(test, "Should have returned test4 instead of %s\n", cat_name);
		goto out;
	}

	/* There should be nothing more */
	cat = ast_category_browse_filtered(cfg, NULL, cat, NULL);
	if (cat) {
		ast_test_status_update(test, "Should not have returned anything\n");
		goto out;
	}

	/* Test ast_variable retrieve.
	 * Get the second category.
	 */
	cat = ast_category_browse_filtered(cfg, NULL, NULL, NULL);
	cat = ast_category_browse_filtered(cfg, NULL, cat, NULL);
	cat_name = ast_category_get_name(cat);
	var = ast_variable_new("aaa", "bbb", "dummy");
	if (!var) {
		ast_test_status_update(test, "Couldn't allocate variable.\n");
		goto out;
	}
	ast_variable_append(cat, var);

	/* Make sure we can retrieve with specific category name */
	var_value = ast_variable_retrieve(cfg, cat_name, "aaa");
	if (!var_value || strcmp(var_value, "bbb")) {
		ast_test_status_update(test, "Variable not found or wrong value.\n");
		goto out;
	}

	/* Make sure we can retrieve with NULL category name */
	var_value = ast_variable_retrieve(cfg, NULL, "aaa");
	if (!var_value || strcmp(var_value, "bbb")) {
		ast_test_status_update(test, "Variable not found or wrong value.\n");
		goto out;
	}

	res = AST_TEST_PASS;

out:
	ast_config_destroy(cfg);
	return res;
}

AST_TEST_DEFINE(config_filtered_ops)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_config *cfg = NULL;
	struct ast_category *cat = NULL;
	char temp[32];
	const char *value;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "config_filtered_ops";
		info->category = "/main/config/";
		info->summary = "Test filtered config ops";
		info->description =	"Test filtered config ops";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cfg = ast_config_new();
	if (!cfg) {
		return res;
	}

	/* load the config */
	for(i = 0; i < 5; i++) {
		snprintf(temp, sizeof(temp), "test%d", i);
		cat = ast_category_new(temp, "dummy", -1);
		ast_variable_insert(cat, ast_variable_new("type", "a", "dummy"), "0");
		ast_category_append(cfg, cat);
	}

	for(i = 0; i < 5; i++) {
		snprintf(temp, sizeof(temp), "test%d", i);
		cat = ast_category_new(temp, "dummy", -1);
		ast_variable_insert(cat, ast_variable_new("type", "b", "dummy"), "0");
		ast_category_append(cfg, cat);
	}

	/* check the config has 5 elements for each type*/
	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, NULL, cat, "type=a"))) {
		snprintf(temp, sizeof(temp), "test%d", i);
		if (strcmp(ast_category_get_name(cat), temp)) {
			ast_test_status_update(test, "%s != %s\n", ast_category_get_name(cat), temp);
			goto out;
		}
		value = ast_variable_find(cat, "type");
		if (!value || strcmp(value, "a")) {
			ast_test_status_update(test, "Type %s != %s\n", "a", value);
			goto out;
		}
		i++;
	}
	if (i != 5) {
		ast_test_status_update(test, "There were %d matches instead of 5.\n", i);
		goto out;
	}

	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, NULL, cat, "type=b"))) {
		snprintf(temp, sizeof(temp), "test%d", i);
		if (!cat || strcmp(ast_category_get_name(cat), temp)) {
			ast_test_status_update(test, "%s != %s\n", ast_category_get_name(cat), temp);
			goto out;
		}
		value = ast_variable_find(cat, "type");
		if (!value || strcmp(value, "b")) {
			ast_test_status_update(test, "Type %s != %s\n", "b", value);
			goto out;
		}
		i++;
	}
	if (i != 5) {
		ast_test_status_update(test, "There were %d matches instead of 5.\n", i);
		goto out;
	}

	/* Delete b3 and make sure it's gone and a3 is still there.
	 * Really this is a test of get since delete takes a specific category structure.
	 */
	cat = ast_category_get(cfg, "test3", "type=b");
	value = ast_variable_find(cat, "type");
	if (strcmp(value, "b")) {
		ast_test_status_update(test, "Type %s != %s\n", "b", value);
		goto out;
	}
	ast_category_delete(cfg, cat);

	cat = ast_category_get(cfg, "test3", "type=b");
	if (cat) {
		ast_test_status_update(test, "Category b was not deleted.\n");
		goto out;
	}

	cat = ast_category_get(cfg, "test3", "type=a");
	if (!cat) {
		ast_test_status_update(test, "Category a was deleted.\n");
		goto out;
	}

	value = ast_variable_find(cat, "type");
	if (strcmp(value, "a")) {
		ast_test_status_update(test, "Type %s != %s\n", value, "a");
		goto out;
	}

	/* Basic regex stuff is handled by regcomp/regexec so not testing here.
	 * Still need to test multiple name/value pairs though.
	 */
	ast_category_empty(cat);
	ast_variable_insert(cat, ast_variable_new("type", "bx", "dummy"), "0");
	ast_variable_insert(cat, ast_variable_new("e", "z", "dummy"), "0");

	cat = ast_category_get(cfg, "test3", "type=.,e=z");
	if (!cat) {
		ast_test_status_update(test, "Category not found.\n");
		goto out;
	}

	cat = ast_category_get(cfg, "test3", "type=.,e=zX");
	if (cat) {
		ast_test_status_update(test, "Category found.\n");
		goto out;
	}

	cat = ast_category_get(cfg, "test3", "TEMPLATE=restrict,type=.,e=z");
	if (cat) {
		ast_test_status_update(test, "Category found.\n");
		goto out;
	}

	res = AST_TEST_PASS;

out:
	ast_config_destroy(cfg);
	return res;
}

AST_TEST_DEFINE(config_template_ops)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_config *cfg = NULL;
	struct ast_category *cat = NULL;
	char temp[32];
	const char *value;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "config_template_ops";
		info->category = "/main/config/";
		info->summary = "Test template config ops";
		info->description =	"Test template config ops";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cfg = ast_config_new();
	if (!cfg) {
		return res;
	}

	/* load the config with 5 templates and 5 regular */
	for(i = 0; i < 5; i++) {
		snprintf(temp, sizeof(temp), "test%d", i);
		cat = ast_category_new_template(temp, "dummy", -1);
		ast_variable_insert(cat, ast_variable_new("type", "a", "dummy"), "0");
		ast_category_append(cfg, cat);
	}

	for(i = 0; i < 5; i++) {
		snprintf(temp, sizeof(temp), "test%d", i);
		cat = ast_category_new(temp, "dummy", -1);
		ast_variable_insert(cat, ast_variable_new("type", "b", "dummy"), "0");
		ast_category_append(cfg, cat);
	}

	/* check the config has 5 template elements of type a */
	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, NULL, cat, "TEMPLATES=restrict,type=a"))) {
		snprintf(temp, sizeof(temp), "test%d", i);
		if (strcmp(ast_category_get_name(cat), temp)) {
			ast_test_status_update(test, "%s != %s\n", ast_category_get_name(cat), temp);
			goto out;
		}
		value = ast_variable_find(cat, "type");
		if (!value || strcmp(value, "a")) {
			ast_test_status_update(test, "Type %s != %s\n", value, "a");
			goto out;
		}
		i++;
	}
	if (i != 5) {
		ast_test_status_update(test, "There were %d matches instead of 5.\n", i);
		goto out;
	}

	/* Test again with 'include'.  There should still only be 5 (type a) */
	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, NULL, cat, "TEMPLATES=include,type=a"))) {
		snprintf(temp, sizeof(temp), "test%d", i);
		if (strcmp(ast_category_get_name(cat), temp)) {
			ast_test_status_update(test, "%s != %s\n", ast_category_get_name(cat), temp);
			goto out;
		}
		value = ast_variable_find(cat, "type");
		if (!value || strcmp(value, "a")) {
			ast_test_status_update(test, "Type %s != %s\n", value, "a");
			goto out;
		}
		i++;
	}
	if (i != 5) {
		ast_test_status_update(test, "There were %d matches instead of 5.\n", i);
		goto out;
	}

	/* Test again with 'include' but no type.  There should now be 10 (type a and type b) */
	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, NULL, cat, "TEMPLATES=include"))) {
		i++;
	}
	if (i != 10) {
		ast_test_status_update(test, "There were %d matches instead of 10.\n", i);
		goto out;
	}

	/* Test again with 'restrict' and type b.  There should 0 */
	i = 0;
	cat = NULL;
	while ((cat = ast_category_browse_filtered(cfg, NULL, cat, "TEMPLATES=restrict,type=b"))) {
		i++;
	}
	if (i != 0) {
		ast_test_status_update(test, "There were %d matches instead of 0.\n", i);
		goto out;
	}

	res = AST_TEST_PASS;

out:
	ast_config_destroy(cfg);
	return res;
}

/*!
 * \brief Write the config file to disk
 *
 * This is necessary for testing config hooks since
 * they are only triggered when a config is read from
 * its intended storage medium
 */
static int write_config_file(void)
{
	int i;
	FILE *config_file;
	char filename[PATH_MAX];

	snprintf(filename, sizeof(filename), "%s/%s",
			ast_config_AST_CONFIG_DIR, CONFIG_FILE);
	config_file = fopen(filename, "w");

	if (!config_file) {
		return -1;
	}

	for (i = 0; i < ARRAY_LEN(categories); ++i) {
		int j;
		fprintf(config_file, "[%s]\n", categories[i].category);
		for (j = 0; j < ARRAY_LEN(categories[i].vars); ++j) {
			fprintf(config_file, "%s = %s\n",
					categories[i].vars[j].name,
					categories[i].vars[j].val);
		}
	}

	fclose(config_file);
	return 0;
}

/*!
 * \brief Delete config file created by write_config_file
 */
static void delete_config_file(void)
{
	char filename[PATH_MAX];
	snprintf(filename, sizeof(filename), "%s/%s",
			ast_config_AST_CONFIG_DIR, CONFIG_FILE);
	unlink(filename);
}

/*
 * Boolean to indicate if the config hook has run
 */
static int hook_run;

/*
 * Boolean to indicate if, when the hook runs, the
 * data passed to it is what is expected
 */
static int hook_config_sane;

static int hook_cb(struct ast_config *cfg)
{
	hook_run = 1;
	if (test_config_validity(cfg) == 0) {
		hook_config_sane = 1;
	}
	ast_config_destroy(cfg);
	return 0;
}

AST_TEST_DEFINE(config_hook)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	enum config_hook_flags hook_flags = { 0, };
	struct ast_flags config_flags = { CONFIG_FLAG_FILEUNCHANGED };
	struct ast_config *cfg;

	switch (cmd) {
	case TEST_INIT:
		info->name = "config_hook";
		info->category = "/main/config/";
		info->summary = "Test config hooks";
		info->description =
			"Ensure that config hooks are called at approriate times,"
			"not called at inappropriate times, and that all information"
			"that should be present is present.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	write_config_file();

	/*
	 * Register a config hook to run when CONFIG_FILE is loaded by this module
	 */
	ast_config_hook_register("test_hook",
			CONFIG_FILE,
			AST_MODULE,
			hook_flags,
			hook_cb);

	/*
	 * Try loading the config file. This should result in the hook
	 * being called
	 */
	cfg = ast_config_load(CONFIG_FILE, config_flags);
	ast_config_destroy(cfg);
	if (!hook_run || !hook_config_sane) {
		ast_test_status_update(test, "Config hook either did not run or was given bad data!\n");
		goto out;
	}

	/*
	 * Now try loading the wrong config file but from the right module.
	 * Hook should not run
	 */
	hook_run = 0;
	cfg = ast_config_load("asterisk.conf", config_flags);
	ast_config_destroy(cfg);
	if (hook_run) {
		ast_test_status_update(test, "Config hook ran even though an incorrect file was specified.\n");
		goto out;
	}

	/*
	 * Now try loading the correct config file but from the wrong module.
	 * Hook should not run
	 */
	hook_run = 0;
	cfg = ast_config_load2(CONFIG_FILE, "fake_module.so", config_flags);
	ast_config_destroy(cfg);
	if (hook_run) {
		ast_test_status_update(test, "Config hook ran even though an incorrect module was specified.\n");
		goto out;
	}

	/*
	 * Now try loading the file correctly, but without any changes to the file.
	 * Hook should not run
	 */
	hook_run = 0;
	cfg = ast_config_load(CONFIG_FILE, config_flags);
	/* Only destroy this cfg conditionally. Otherwise a crash happens. */
	if (cfg != CONFIG_STATUS_FILEUNCHANGED) {
		ast_config_destroy(cfg);
	}
	if (hook_run) {
		ast_test_status_update(test, "Config hook ran even though file contents had not changed\n");
		goto out;
	}

	res = AST_TEST_PASS;

out:
	delete_config_file();
	return res;
}

enum {
	EXPECT_FAIL = 0,
	EXPECT_SUCCEED,
};

#define TOOBIG_I32 "2147483649"
#define TOOSMALL_I32 "-2147483649"
#define TOOBIG_U32 "4294967297"
#define TOOSMALL_U32 "-4294967297"
#define DEFAULTVAL 42
#define EPSILON 0.001

#define TEST_PARSE(input, should_succeed, expected_result, flags, result, ...) do {\
	int __res = ast_parse_arg(input, (flags), result, ##__VA_ARGS__); \
	if (!__res == !should_succeed) { \
		ast_test_status_update(test, "ast_parse_arg failed on '%s'. %d/%d\n", input, __res, should_succeed); \
		ret = AST_TEST_FAIL; \
	} else { \
		if (((flags) & PARSE_TYPE) == PARSE_INT32) { \
			int32_t *r = (int32_t *) (void *) result; \
			int32_t e = (int32_t) expected_result; \
			if (*r != e) { \
				ast_test_status_update(test, "ast_parse_arg int32_t failed with %d != %d\n", *r, e); \
				ret = AST_TEST_FAIL; \
			} \
		} else if (((flags) & PARSE_TYPE) == PARSE_UINT32) { \
			uint32_t *r = (uint32_t *) (void *) result; \
			uint32_t e = (uint32_t) expected_result; \
			if (*r != e) { \
				ast_test_status_update(test, "ast_parse_arg uint32_t failed with %u != %u\n", *r, e); \
				ret = AST_TEST_FAIL; \
			} \
		} else if (((flags) & PARSE_TYPE) == PARSE_DOUBLE) { \
			double *r = (double *) (void *) result; \
			double e = (double) expected_result; \
			if (fabs(*r - e) > EPSILON) { \
				ast_test_status_update(test, "ast_parse_arg double failed with %f != %f\n", *r, e); \
				ret = AST_TEST_FAIL; \
			} \
		} \
	} \
	*(result) = DEFAULTVAL; \
} while (0)

AST_TEST_DEFINE(ast_parse_arg_test)
{
	int ret = AST_TEST_PASS;
	int32_t int32_t_val = DEFAULTVAL;
	uint32_t uint32_t_val = DEFAULTVAL;
	double double_val = DEFAULTVAL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_parse_arg";
		info->category = "/config/";
		info->summary = "Test the output of ast_parse_arg";
		info->description =
			"Ensures that ast_parse_arg behaves as expected";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* int32 testing */
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32, &int32_t_val);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32, &int32_t_val);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32, &int32_t_val);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32, &int32_t_val);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32, &int32_t_val);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32, &int32_t_val);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32, &int32_t_val);
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT, &int32_t_val, 7);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, 0, 200);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, -200, 100);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, -1, 0);
	TEST_PARSE("123", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, 0, 122);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, -122, 100);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, 1, 100);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_IN_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("123", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, -200, 100);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, -1, 0);
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, 0, 122);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, -122, 100);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, 1, 100);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_INT32 | PARSE_OUT_RANGE, &int32_t_val, INT_MIN, INT_MAX);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, 0, 200);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, -200, 100);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, -1, 0);
	TEST_PARSE("123", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, 0, 122);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, -122, 100);
	TEST_PARSE("0", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, 1, 100);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("123", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, -200, 100);
	TEST_PARSE("0", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, -1, 0);
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, 0, 122);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, -122, 100);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, 1, 100);
	TEST_PARSE(TOOBIG_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_I32, EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_INT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &int32_t_val, 7, INT_MIN, INT_MAX);

	/* uuint32 testing */
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT, &uint32_t_val, 7);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 0, 200);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 0, 1);

	TEST_PARSE("123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 0, 122);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, 1, 100);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_IN_RANGE, &uint32_t_val, INT_MIN, INT_MAX);

	TEST_PARSE("123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 0, 200);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 0, 1);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 0, 122);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, 1, 100);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32 | PARSE_OUT_RANGE, &uint32_t_val, INT_MIN, INT_MAX);

	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 0, 200);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 0, 1);
	TEST_PARSE("123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 0, 122);
	TEST_PARSE("0", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, 1, 100);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_IN_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 0, 200);
	TEST_PARSE("-123", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 0, 100);
	TEST_PARSE("0", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 0, 1);
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 0, 122);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, 1, 100);
	TEST_PARSE(TOOBIG_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE(TOOSMALL_U32, EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7, PARSE_UINT32 | PARSE_DEFAULT | PARSE_OUT_RANGE, &uint32_t_val, 7, INT_MIN, INT_MAX);

	TEST_PARSE("   -123", EXPECT_FAIL, DEFAULTVAL, PARSE_UINT32, &uint32_t_val);

	/* double testing */
	TEST_PARSE("123", EXPECT_SUCCEED, 123, PARSE_DOUBLE, &double_val);
	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE, &double_val);
	TEST_PARSE("-123", EXPECT_SUCCEED, -123, PARSE_DOUBLE, &double_val);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE, &double_val);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE, &double_val);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE, &double_val);
	TEST_PARSE("7.0not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE, &double_val);
	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);
	TEST_PARSE("not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);
	TEST_PARSE("7.0not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT, &double_val, 7.0);

	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, 0.0, 200.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -200.0, 100.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -1.0, 0.0);
	TEST_PARSE("123.123", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, 0.0, 122.0);
	TEST_PARSE("-123.123", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -122.0, 100.0);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, 1.0, 100.0);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_IN_RANGE, &double_val, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("123.123", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, 0.0, 200.0);
	TEST_PARSE("-123.123", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -200.0, 100.0);
	TEST_PARSE("0", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -1.0, 0.0);
	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, 0.0, 122.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -122.0, 100.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, 1.0, 100.0);
	TEST_PARSE("not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("7not a number", EXPECT_FAIL, DEFAULTVAL, PARSE_DOUBLE | PARSE_OUT_RANGE, &double_val, -HUGE_VAL, HUGE_VAL);

	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, 0.0, 200.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -200.0, 100.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -1.0, 0.0);
	TEST_PARSE("123.123", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, 0.0, 122.0);
	TEST_PARSE("-123.123", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -122.0, 100.0);
	TEST_PARSE("0", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, 1.0, 100.0);
	TEST_PARSE("not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_IN_RANGE, &double_val, 7.0, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("123.123", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, 0.0, 200.0);
	TEST_PARSE("-123.123", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -200.0, 100.0);
	TEST_PARSE("0", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -1.0, 0.0);
	TEST_PARSE("123.123", EXPECT_SUCCEED, 123.123, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, 0.0, 122.0);
	TEST_PARSE("-123.123", EXPECT_SUCCEED, -123.123, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -122.0, 100.0);
	TEST_PARSE("0", EXPECT_SUCCEED, 0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, 1.0, 100.0);
	TEST_PARSE("not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -HUGE_VAL, HUGE_VAL);
	TEST_PARSE("7not a number", EXPECT_FAIL, 7.0, PARSE_DOUBLE | PARSE_DEFAULT | PARSE_OUT_RANGE, &double_val, 7.0, -HUGE_VAL, HUGE_VAL);

	/* ast_sockaddr_parse is tested extensively in test_netsock2.c and PARSE_ADDR is a very simple wrapper */

	return ret;
}
struct test_item {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(stropt);
	);
	int32_t intopt;
	uint32_t uintopt;
	unsigned int flags;
	double doubleopt;
	struct ast_sockaddr sockaddropt;
	int boolopt;
	struct ast_ha *aclopt;
	struct ast_codec_pref codecprefopt;
	struct ast_format_cap *codeccapopt;
	unsigned int customopt:1;
};
struct test_config {
	struct test_item *global;
	struct test_item *global_defaults;
	struct ao2_container *items;
};

static int test_item_hash(const void *obj, const int flags)
{
	const struct test_item *item = obj;
	const char *name = (flags & OBJ_KEY) ? obj : item->name;
	return ast_str_case_hash(name);
}
static int test_item_cmp(void *obj, void *arg, int flags)
{
	struct test_item *one = obj, *two = arg;
	const char *match = (flags & OBJ_KEY) ? arg : two->name;
	return strcasecmp(one->name, match) ? 0 : (CMP_MATCH | CMP_STOP);
}
static void test_item_destructor(void *obj)
{
	struct test_item *item = obj;
	ast_string_field_free_memory(item);
	if (item->codeccapopt) {
		ast_format_cap_destroy(item->codeccapopt);
	}
	if (item->aclopt) {
		ast_free_ha(item->aclopt);
	}
	return;
}
static void *test_item_alloc(const char *cat)
{
	struct test_item *item;
	if (!(item = ao2_alloc(sizeof(*item), test_item_destructor))) {
		return NULL;
	}
	if (ast_string_field_init(item, 128)) {
		ao2_ref(item, -1);
		return NULL;
	}
	if (!(item->codeccapopt = ast_format_cap_alloc(0))) {
		ao2_ref(item, -1);
		return NULL;
	}
	ast_string_field_set(item, name, cat);
	return item;
}
static void test_config_destructor(void *obj)
{
	struct test_config *cfg = obj;
	ao2_cleanup(cfg->global);
	ao2_cleanup(cfg->global_defaults);
	ao2_cleanup(cfg->items);
}
static void *test_config_alloc(void)
{
	struct test_config *cfg;
	if (!(cfg = ao2_alloc(sizeof(*cfg), test_config_destructor))) {
		goto error;
	}
	if (!(cfg->global = test_item_alloc("global"))) {
		goto error;
	}
	if (!(cfg->global_defaults = test_item_alloc("global_defaults"))) {
		goto error;
	}
	if (!(cfg->items = ao2_container_alloc(1, test_item_hash, test_item_cmp))) {
		goto error;
	}
	return cfg;
error:
	ao2_cleanup(cfg);
	return NULL;
}
static void *test_item_find(struct ao2_container *container, const char *cat)
{
	return ao2_find(container, cat, OBJ_KEY);
}

static int customopt_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct test_item *item = obj;
	if (!strcasecmp(var->name, "customopt")) {
		item->customopt = ast_true(var->value);
	} else {
		return -1;
	}

	return 0;
}

static struct aco_type global = {
	.type = ACO_GLOBAL,
	.item_offset = offsetof(struct test_config, global),
	.category_match = ACO_WHITELIST,
	.category = "^global$",
};
static struct aco_type global_defaults = {
	.type = ACO_GLOBAL,
	.item_offset = offsetof(struct test_config, global_defaults),
	.category_match = ACO_WHITELIST,
	.category = "^global_defaults$",
};
static struct aco_type item = {
	.type = ACO_ITEM,
	.category_match = ACO_BLACKLIST,
	.category = "^(global|global_defaults)$",
	.item_alloc = test_item_alloc,
	.item_find = test_item_find,
	.item_offset = offsetof(struct test_config, items),
};

struct aco_file config_test_conf = {
	.filename = "config_test.conf",
	.types = ACO_TYPES(&global, &global_defaults, &item),
};

static AO2_GLOBAL_OBJ_STATIC(global_obj);
CONFIG_INFO_TEST(cfg_info, global_obj, test_config_alloc,
	.files = ACO_FILES(&config_test_conf),
);

AST_TEST_DEFINE(config_options_test)
{
	int res = AST_TEST_PASS, x, error;
	struct test_item defaults = { 0, }, configs = { 0, };
	struct test_item *arr[4];
	struct ast_sockaddr acl_allow = {{ 0, }}, acl_fail = {{ 0, }};
	RAII_VAR(struct test_config *, cfg, NULL, ao2_cleanup);
	RAII_VAR(struct test_item *, item, NULL, ao2_cleanup);
	RAII_VAR(struct test_item *, item_defaults, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "config_options_test";
		info->category = "/config/";
		info->summary = "Config opptions unit test";
		info->description =
			"Tests the Config Options API";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

#define INT_DEFAULT "-2"
#define INT_CONFIG "-1"
#define UINT_DEFAULT "2"
#define UINT_CONFIG "1"
#define DOUBLE_DEFAULT "1.1"
#define DOUBLE_CONFIG "0.1"
#define SOCKADDR_DEFAULT "4.3.2.1:4321"
#define SOCKADDR_CONFIG "1.2.3.4:1234"
#define BOOL_DEFAULT "false"
#define BOOL_CONFIG "true"
#define BOOLFLAG1_DEFAULT "false"
#define BOOLFLAG1_CONFIG "true"
#define BOOLFLAG2_DEFAULT "false"
#define BOOLFLAG2_CONFIG "false"
#define BOOLFLAG3_DEFAULT "false"
#define BOOLFLAG3_CONFIG "true"
#define ACL_DEFAULT NULL
#define ACL_CONFIG_PERMIT "1.2.3.4/32"
#define ACL_CONFIG_DENY "0.0.0.0/0"
#define CODEC_DEFAULT "!all,alaw"
#define CODEC_CONFIG "!all,ulaw,g729"
#define STR_DEFAULT "default"
#define STR_CONFIG "test"
#define CUSTOM_DEFAULT "no"
#define CUSTOM_CONFIG "yes"

#define BOOLFLAG1 1 << 0
#define BOOLFLAG2 1 << 1
#define BOOLFLAG3 1 << 2

	if (aco_info_init(&cfg_info)) {
		ast_test_status_update(test, "Could not init cfg info\n");
		return AST_TEST_FAIL;
	}

	/* Register all options */
	aco_option_register(&cfg_info, "intopt", ACO_EXACT, config_test_conf.types, INT_DEFAULT, OPT_INT_T, 0, FLDSET(struct test_item, intopt));
	aco_option_register(&cfg_info, "uintopt", ACO_EXACT, config_test_conf.types, UINT_DEFAULT, OPT_UINT_T, 0, FLDSET(struct test_item, uintopt));
	aco_option_register(&cfg_info, "doubleopt", ACO_EXACT, config_test_conf.types, DOUBLE_DEFAULT, OPT_DOUBLE_T, 0, FLDSET(struct test_item, doubleopt));
	aco_option_register(&cfg_info, "sockaddropt", ACO_EXACT, config_test_conf.types, SOCKADDR_DEFAULT, OPT_SOCKADDR_T, 0, FLDSET(struct test_item, sockaddropt));
	aco_option_register(&cfg_info, "boolopt", ACO_EXACT, config_test_conf.types, BOOL_DEFAULT, OPT_BOOL_T, 1, FLDSET(struct test_item, boolopt));
	aco_option_register(&cfg_info, "boolflag1", ACO_EXACT, config_test_conf.types, BOOLFLAG1_DEFAULT, OPT_BOOLFLAG_T, 1, FLDSET(struct test_item, flags), BOOLFLAG1);
	aco_option_register(&cfg_info, "boolflag2", ACO_EXACT, config_test_conf.types, BOOLFLAG2_DEFAULT, OPT_BOOLFLAG_T, 1, FLDSET(struct test_item, flags), BOOLFLAG2);
	aco_option_register(&cfg_info, "boolflag3", ACO_EXACT, config_test_conf.types, BOOLFLAG3_DEFAULT, OPT_BOOLFLAG_T, 1, FLDSET(struct test_item, flags), BOOLFLAG3);
	aco_option_register(&cfg_info, "aclpermitopt", ACO_EXACT, config_test_conf.types, ACL_DEFAULT, OPT_ACL_T, 1, FLDSET(struct test_item, aclopt));
	aco_option_register(&cfg_info, "acldenyopt", ACO_EXACT, config_test_conf.types, ACL_DEFAULT, OPT_ACL_T, 0, FLDSET(struct test_item, aclopt));
	aco_option_register(&cfg_info, "codecopt", ACO_EXACT, config_test_conf.types, CODEC_DEFAULT, OPT_CODEC_T, 1, FLDSET(struct test_item, codecprefopt, codeccapopt));
	aco_option_register(&cfg_info, "stropt", ACO_EXACT, config_test_conf.types, STR_DEFAULT, OPT_STRINGFIELD_T, 0, STRFLDSET(struct test_item, stropt));
	aco_option_register_custom(&cfg_info, "customopt", ACO_EXACT, config_test_conf.types, CUSTOM_DEFAULT, customopt_handler, 0);
	aco_option_register_deprecated(&cfg_info, "permit", config_test_conf.types, "aclpermitopt");
	aco_option_register_deprecated(&cfg_info, "deny", config_test_conf.types, "acldenyopt");

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		ast_test_status_update(test, "Could not parse config\n");
		return AST_TEST_FAIL;
	}

	ast_parse_arg(INT_DEFAULT, PARSE_INT32, &defaults.intopt);
	ast_parse_arg(INT_CONFIG, PARSE_INT32, &configs.intopt);
	ast_parse_arg(UINT_DEFAULT, PARSE_UINT32, &defaults.uintopt);
	ast_parse_arg(UINT_CONFIG, PARSE_UINT32, &configs.uintopt);
	ast_parse_arg(DOUBLE_DEFAULT, PARSE_DOUBLE, &defaults.doubleopt);
	ast_parse_arg(DOUBLE_CONFIG, PARSE_DOUBLE, &configs.doubleopt);
	ast_parse_arg(SOCKADDR_DEFAULT, PARSE_ADDR, &defaults.sockaddropt);
	ast_parse_arg(SOCKADDR_CONFIG, PARSE_ADDR, &configs.sockaddropt);
	defaults.boolopt = ast_true(BOOL_DEFAULT);
	configs.boolopt = ast_true(BOOL_CONFIG);
	ast_set2_flag(&defaults, ast_true(BOOLFLAG1_DEFAULT), BOOLFLAG1);
	ast_set2_flag(&defaults, ast_true(BOOLFLAG2_DEFAULT), BOOLFLAG2);
	ast_set2_flag(&defaults, ast_true(BOOLFLAG3_DEFAULT), BOOLFLAG3);
	ast_set2_flag(&configs, ast_true(BOOLFLAG1_CONFIG), BOOLFLAG1);
	ast_set2_flag(&configs, ast_true(BOOLFLAG2_CONFIG), BOOLFLAG2);
	ast_set2_flag(&configs, ast_true(BOOLFLAG3_CONFIG), BOOLFLAG3);

	defaults.aclopt = NULL;
	configs.aclopt = ast_append_ha("deny", ACL_CONFIG_DENY, configs.aclopt, &error);
	configs.aclopt = ast_append_ha("permit", ACL_CONFIG_PERMIT, configs.aclopt, &error);
	ast_sockaddr_parse(&acl_allow, "1.2.3.4", PARSE_PORT_FORBID);
	ast_sockaddr_parse(&acl_fail, "1.1.1.1", PARSE_PORT_FORBID);

	defaults.codeccapopt = ast_format_cap_alloc(0);
	ast_parse_allow_disallow(&defaults.codecprefopt, defaults.codeccapopt, CODEC_DEFAULT, 1);

	configs.codeccapopt = ast_format_cap_alloc(0);
	ast_parse_allow_disallow(&configs.codecprefopt, configs.codeccapopt, CODEC_CONFIG, 1);

	ast_string_field_init(&defaults, 128);
	ast_string_field_init(&configs, 128);
	ast_string_field_set(&defaults, stropt, STR_DEFAULT);
	ast_string_field_set(&configs, stropt, STR_CONFIG);

	defaults.customopt = ast_true(CUSTOM_DEFAULT);
	configs.customopt = ast_true(CUSTOM_CONFIG);


	cfg = ao2_global_obj_ref(global_obj);
	if (!(item = ao2_find(cfg->items, "item", OBJ_KEY))) {
		ast_test_status_update(test, "could not look up 'item'\n");
		return AST_TEST_FAIL;
	}
	if (!(item_defaults = ao2_find(cfg->items, "item_defaults", OBJ_KEY))) {
		ast_test_status_update(test, "could not look up 'item_defaults'\n");
		return AST_TEST_FAIL;
	}
	arr[0] = cfg->global;
	arr[1] = item;
	arr[2] = cfg->global_defaults;
	arr[3] = item_defaults;
	/* Test global and item against configs, global_defaults and item_defaults against defaults */

#define NOT_EQUAL_FAIL(field, format)  \
	if (arr[x]->field != control->field) { \
		ast_test_status_update(test, "%s did not match: " format " != " format " with x = %d\n", #field, arr[x]->field, control->field, x); \
		res = AST_TEST_FAIL; \
	}
	for (x = 0; x < 4; x++) {
		struct test_item *control = x < 2 ? &configs : &defaults;

		NOT_EQUAL_FAIL(intopt, "%d");
		NOT_EQUAL_FAIL(uintopt, "%u");
		NOT_EQUAL_FAIL(boolopt, "%d");
		NOT_EQUAL_FAIL(flags, "%u");
		NOT_EQUAL_FAIL(customopt, "%d");
		if (fabs(arr[x]->doubleopt - control->doubleopt) > 0.001) {
			ast_test_status_update(test, "doubleopt did not match: %f vs %f on loop %d\n", arr[x]->doubleopt, control->doubleopt, x);
			res = AST_TEST_FAIL;
		}
		if (ast_sockaddr_cmp(&arr[x]->sockaddropt, &control->sockaddropt)) {
			ast_test_status_update(test, "sockaddr did not match on loop %d\n", x);
			res = AST_TEST_FAIL;
		}
		if (!ast_format_cap_identical(arr[x]->codeccapopt, control->codeccapopt)) {
			char buf1[128], buf2[128];
			ast_getformatname_multiple(buf1, sizeof(buf1), arr[x]->codeccapopt);
			ast_getformatname_multiple(buf2, sizeof(buf2), control->codeccapopt);
			ast_test_status_update(test, "format did not match: '%s' vs '%s' on loop %d\n", buf1, buf2, x);
			res = AST_TEST_FAIL;
		}
		if (strcasecmp(arr[x]->stropt, control->stropt)) {
			ast_test_status_update(test, "stropt did not match: '%s' vs '%s' on loop %d\n", arr[x]->stropt, control->stropt, x);
			res = AST_TEST_FAIL;
		}
		if (arr[x]->aclopt != control->aclopt && (ast_apply_ha(arr[x]->aclopt, &acl_allow) != ast_apply_ha(control->aclopt, &acl_allow) ||
				ast_apply_ha(arr[x]->aclopt, &acl_fail) != ast_apply_ha(control->aclopt, &acl_fail))) {
			ast_test_status_update(test, "acl not match: on loop %d\n", x);
			res = AST_TEST_FAIL;
		}
	}

	ast_free_ha(configs.aclopt);
	ast_format_cap_destroy(defaults.codeccapopt);
	ast_format_cap_destroy(configs.codeccapopt);
	ast_string_field_free_memory(&defaults);
	ast_string_field_free_memory(&configs);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(config_basic_ops);
	AST_TEST_UNREGISTER(config_filtered_ops);
	AST_TEST_UNREGISTER(config_template_ops);
	AST_TEST_UNREGISTER(copy_config);
	AST_TEST_UNREGISTER(config_hook);
	AST_TEST_UNREGISTER(ast_parse_arg_test);
	AST_TEST_UNREGISTER(config_options_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(config_basic_ops);
	AST_TEST_REGISTER(config_filtered_ops);
	AST_TEST_REGISTER(config_template_ops);
	AST_TEST_REGISTER(copy_config);
	AST_TEST_REGISTER(config_hook);
	AST_TEST_REGISTER(ast_parse_arg_test);
	AST_TEST_REGISTER(config_options_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Config test module");

