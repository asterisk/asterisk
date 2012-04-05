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
#include "asterisk/paths.h"

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

static int unload_module(void)
{
	AST_TEST_UNREGISTER(copy_config);
	AST_TEST_UNREGISTER(config_hook);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(copy_config);
	AST_TEST_REGISTER(config_hook);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Config test module");
