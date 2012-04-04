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

#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/test.h"

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

AST_TEST_DEFINE(copy_config)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	struct ast_config *cfg = NULL;
	struct ast_config *copy = NULL;
	const char *cat_iter = NULL;
	size_t i;

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

	/* Okay, let's see if the correct content is there */
	for (i = 0; i < ARRAY_LEN(categories); ++i) {
		struct ast_variable *var = NULL;
		size_t j;
		cat_iter = ast_category_browse(copy, cat_iter);
		if (strcmp(cat_iter, categories[i].category)) {
			ast_log(LOG_ERROR, "Category name mismatch, %s does not match %s\n", cat_iter, categories[i].category);
			goto out;
		}
		for (j = 0; j < ARRAY_LEN(categories[i].vars); ++j) {
			var = var ? var->next : ast_variable_browse(copy, cat_iter);
			if (strcmp(var->name, categories[i].vars[j].name)) {
				ast_log(LOG_ERROR, "Variable name mismatch, %s does not match %s\n", var->name, categories[i].vars[j].name);
				goto out;
			}
			if (strcmp(var->value, categories[i].vars[j].val)) {
				ast_log(LOG_ERROR, "Variable value mismatch, %s does not match %s\n", var->value, categories[i].vars[j].val);
				goto out;
			}
		}
	}

	res = AST_TEST_PASS;

out:
	ast_config_destroy(cfg);
	ast_config_destroy(copy);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(copy_config);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(copy_config);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Config test module");

