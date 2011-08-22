/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009-2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
 * Russell Bryant <russell@digium.com>
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
 * \brief Unit Test Framework
 *
 * \author David Vossel <dvossel@digium.com>
 * \author Russell Bryant <russell@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/_private.h"

#ifdef TEST_FRAMEWORK
#include "asterisk/test.h"
#include "asterisk/logger.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"
#include "asterisk/version.h"
#include "asterisk/paths.h"
#include "asterisk/time.h"
#include "asterisk/manager.h"

/*! This array corresponds to the values defined in the ast_test_state enum */
static const char * const test_result2str[] = {
	[AST_TEST_NOT_RUN] = "NOT RUN",
	[AST_TEST_PASS]    = "PASS",
	[AST_TEST_FAIL]    = "FAIL",
};

/*! holds all the information pertaining to a single defined test */
struct ast_test {
	struct ast_test_info info;        /*!< holds test callback information */
	/*!
	 * \brief Test defined status output from last execution
	 */
	struct ast_str *status_str;
	/*!
	 * \brief CLI arguments, if tests being run from the CLI
	 *
	 * If this is set, status updates from the tests will be sent to the
	 * CLI in addition to being saved off in status_str.
	 */
	struct ast_cli_args *cli;
	enum ast_test_result_state state; /*!< current test state */
	unsigned int time;                /*!< time in ms test took */
	ast_test_cb_t *cb;                /*!< test callback function */
	AST_LIST_ENTRY(ast_test) entry;
};

/*! global structure containing both total and last test execution results */
static struct ast_test_execute_results {
	unsigned int total_tests;  /*!< total number of tests, regardless if they have been executed or not */
	unsigned int total_passed; /*!< total number of executed tests passed */
	unsigned int total_failed; /*!< total number of executed tests failed */
	unsigned int total_time;   /*!< total time of all executed tests */
	unsigned int last_passed;  /*!< number of passed tests during last execution */
	unsigned int last_failed;  /*!< number of failed tests during last execution */
	unsigned int last_time;    /*!< total time of the last test execution */
} last_results;

enum test_mode {
	TEST_ALL = 0,
	TEST_CATEGORY = 1,
	TEST_NAME_CATEGORY = 2,
};

/*! List of registered test definitions */
static AST_LIST_HEAD_STATIC(tests, ast_test);

static struct ast_test *test_alloc(ast_test_cb_t *cb);
static struct ast_test *test_free(struct ast_test *test);
static int test_insert(struct ast_test *test);
static struct ast_test *test_remove(ast_test_cb_t *cb);
static int test_cat_cmp(const char *cat1, const char *cat2);

int __ast_test_status_update(const char *file, const char *func, int line,
		struct ast_test *test, const char *fmt, ...)
{
	struct ast_str *buf = NULL;
	va_list ap;

	if (!(buf = ast_str_create(128))) {
		return -1;
	}

	va_start(ap, fmt);
	ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);

	if (test->cli) {
		ast_cli(test->cli->fd, "[%s:%s:%d]: %s",
				file, func, line, ast_str_buffer(buf));
	}

	ast_str_append(&test->status_str, 0, "[%s:%s:%d]: %s",
			file, func, line, ast_str_buffer(buf));

	ast_free(buf);

	return 0;
}

int ast_test_register(ast_test_cb_t *cb)
{
	struct ast_test *test;

	if (!cb) {
		ast_log(LOG_WARNING, "Attempted to register test without all required information\n");
		return -1;
	}

	if (!(test = test_alloc(cb))) {
		return -1;
	}

	if (test_insert(test)) {
		test_free(test);
		return -1;
	}

	return 0;
}

int ast_test_unregister(ast_test_cb_t *cb)
{
	struct ast_test *test;

	if (!(test = test_remove(cb))) {
		return -1; /* not found */
	}

	test_free(test);

	return 0;
}

/*!
 * \internal
 * \brief executes a single test, storing the results in the test->result structure.
 *
 * \note The last_results structure which contains global statistics about test execution
 * must be updated when using this function. See use in test_execute_multiple().
 */
static void test_execute(struct ast_test *test)
{
	struct timeval begin;

	ast_str_reset(test->status_str);

	begin = ast_tvnow();
	test->state = test->cb(&test->info, TEST_EXECUTE, test);
	test->time = ast_tvdiff_ms(ast_tvnow(), begin);
}

static void test_xml_entry(struct ast_test *test, FILE *f)
{
	if (!f || !test || test->state == AST_TEST_NOT_RUN) {
		return;
	}

	fprintf(f, "\t<testcase time=\"%d.%d\" name=\"%s%s\"%s>\n",
			test->time / 1000, test->time % 1000,
			test->info.category, test->info.name,
			test->state == AST_TEST_PASS ? "/" : "");

	if (test->state == AST_TEST_FAIL) {
		fprintf(f, "\t\t<failure><![CDATA[\n%s\n\t\t]]></failure>\n",
				S_OR(ast_str_buffer(test->status_str), "NA"));
		fprintf(f, "\t</testcase>\n");
	}

}

static void test_txt_entry(struct ast_test *test, FILE *f)
{
	if (!f || !test) {
		return;
	}

	fprintf(f, "\nName:              %s\n", test->info.name);
	fprintf(f,   "Category:          %s\n", test->info.category);
	fprintf(f,   "Summary:           %s\n", test->info.summary);
	fprintf(f,   "Description:       %s\n", test->info.description);
	fprintf(f,   "Result:            %s\n", test_result2str[test->state]);
	if (test->state != AST_TEST_NOT_RUN) {
		fprintf(f,   "Time:              %d\n", test->time);
	}
	if (test->state == AST_TEST_FAIL) {
		fprintf(f,   "Error Description: %s\n\n", S_OR(ast_str_buffer(test->status_str), "NA"));
	}
}

/*!
 * \internal
 * \brief Executes registered unit tests
 *
 * \param name of test to run (optional)
 * \param test category to run (optional)
 * \param cli args for cli test updates (optional)
 *
 * \return number of tests executed.
 *
 * \note This function has three modes of operation
 * -# When given a name and category, a matching individual test will execute if found.
 * -# When given only a category all matching tests within that category will execute.
 * -# If given no name or category all registered tests will execute.
 */
static int test_execute_multiple(const char *name, const char *category, struct ast_cli_args *cli)
{
	char result_buf[32] = { 0 };
	struct ast_test *test = NULL;
	enum test_mode mode = TEST_ALL; /* 3 modes, 0 = run all, 1 = only by category, 2 = only by name and category */
	int execute = 0;
	int res = 0;

	if (!ast_strlen_zero(category)) {
		if (!ast_strlen_zero(name)) {
			mode = TEST_NAME_CATEGORY;
		} else {
			mode = TEST_CATEGORY;
		}
	}

	AST_LIST_LOCK(&tests);
	/* clear previous execution results */
	memset(&last_results, 0, sizeof(last_results));
	AST_LIST_TRAVERSE(&tests, test, entry) {

		execute = 0;
		switch (mode) {
		case TEST_CATEGORY:
			if (!test_cat_cmp(test->info.category, category)) {
				execute = 1;
			}
			break;
		case TEST_NAME_CATEGORY:
			if (!(test_cat_cmp(test->info.category, category)) && !(strcmp(test->info.name, name))) {
				execute = 1;
			}
			break;
		case TEST_ALL:
			execute = 1;
		}

		if (execute) {
			if (cli) {
				ast_cli(cli->fd, "START  %s - %s \n", test->info.category, test->info.name);
			}

			/* set the test status update argument. it is ok if cli is NULL */
			test->cli = cli;

			/* execute the test and save results */
			test_execute(test);

			test->cli = NULL;

			/* update execution specific counts here */
			last_results.last_time += test->time;
			if (test->state == AST_TEST_PASS) {
				last_results.last_passed++;
			} else if (test->state == AST_TEST_FAIL) {
				last_results.last_failed++;
			}

			if (cli) {
				term_color(result_buf,
					test_result2str[test->state],
					(test->state == AST_TEST_FAIL) ? COLOR_RED : COLOR_GREEN,
					0,
					sizeof(result_buf));
				ast_cli(cli->fd, "END    %s - %s Time: %s%dms Result: %s\n",
					test->info.category,
					test->info.name,
					test->time ? "" : "<",
					test->time ? test->time : 1,
					result_buf);
			}
		}

		/* update total counts as well during this iteration
		 * even if the current test did not execute this time */
		last_results.total_time += test->time;
		last_results.total_tests++;
		if (test->state != AST_TEST_NOT_RUN) {
			if (test->state == AST_TEST_PASS) {
				last_results.total_passed++;
			} else {
				last_results.total_failed++;
			}
		}
	}
	res = last_results.last_passed + last_results.last_failed;
	AST_LIST_UNLOCK(&tests);

	return res;
}

/*!
 * \internal
 * \brief Generate test results.
 *
 * \param name of test result to generate (optional)
 * \param test category to generate (optional)
 * \param path to xml file to generate. (optional)
 * \param path to txt file to generate, (optional)
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note This function has three modes of operation.
 * -# When given both a name and category, results will be generated for that single test.
 * -# When given only a category, results for every test within the category will be generated.
 * -# When given no name or category, results for every registered test will be generated.
 *
 * In order for the results to be generated, an xml and or txt file path must be provided.
 */
static int test_generate_results(const char *name, const char *category, const char *xml_path, const char *txt_path)
{
	enum test_mode mode = TEST_ALL;  /* 0 generate all, 1 generate by category only, 2 generate by name and category */
	FILE *f_xml = NULL, *f_txt = NULL;
	int res = 0;
	struct ast_test *test = NULL;

	/* verify at least one output file was given */
	if (ast_strlen_zero(xml_path) && ast_strlen_zero(txt_path)) {
		return -1;
	}

	/* define what mode is to be used */
	if (!ast_strlen_zero(category)) {
		if (!ast_strlen_zero(name)) {
			mode = TEST_NAME_CATEGORY;
		} else {
			mode = TEST_CATEGORY;
		}
	}
	/* open files for writing */
	if (!ast_strlen_zero(xml_path)) {
		if (!(f_xml = fopen(xml_path, "w"))) {
			ast_log(LOG_WARNING, "Could not open file %s for xml test results\n", xml_path);
			res = -1;
			goto done;
		}
	}
	if (!ast_strlen_zero(txt_path)) {
		if (!(f_txt = fopen(txt_path, "w"))) {
			ast_log(LOG_WARNING, "Could not open file %s for text output of test results\n", txt_path);
			res = -1;
			goto done;
		}
	}

	AST_LIST_LOCK(&tests);
	/* xml header information */
	if (f_xml) {
		/*
		 * http://confluence.atlassian.com/display/BAMBOO/JUnit+parsing+in+Bamboo
		 */
		fprintf(f_xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
		fprintf(f_xml, "<testsuite errors=\"0\" time=\"%d.%d\" tests=\"%d\" "
				"name=\"AsteriskUnitTests\">\n",
				last_results.total_time / 1000, last_results.total_time % 1000,
				last_results.total_tests);
		fprintf(f_xml, "\t<properties>\n");
		fprintf(f_xml, "\t\t<property name=\"version\" value=\"%s\"/>\n", ASTERISK_VERSION);
		fprintf(f_xml, "\t</properties>\n");
	}

	/* txt header information */
	if (f_txt) {
		fprintf(f_txt, "Asterisk Version:         %s\n", ASTERISK_VERSION);
		fprintf(f_txt, "Asterisk Version Number:  %d\n", ASTERISK_VERSION_NUM);
		fprintf(f_txt, "Number of Tests:          %d\n", last_results.total_tests);
		fprintf(f_txt, "Number of Tests Executed: %d\n", (last_results.total_passed + last_results.total_failed));
		fprintf(f_txt, "Passed Tests:             %d\n", last_results.total_passed);
		fprintf(f_txt, "Failed Tests:             %d\n", last_results.total_failed);
		fprintf(f_txt, "Total Execution Time:     %d\n", last_results.total_time);
	}

	/* export each individual test */
	AST_LIST_TRAVERSE(&tests, test, entry) {
		switch (mode) {
		case TEST_CATEGORY:
			if (!test_cat_cmp(test->info.category, category)) {
				test_xml_entry(test, f_xml);
				test_txt_entry(test, f_txt);
			}
			break;
		case TEST_NAME_CATEGORY:
			if (!(strcmp(test->info.category, category)) && !(strcmp(test->info.name, name))) {
				test_xml_entry(test, f_xml);
				test_txt_entry(test, f_txt);
			}
			break;
		case TEST_ALL:
			test_xml_entry(test, f_xml);
			test_txt_entry(test, f_txt);
		}
	}
	AST_LIST_UNLOCK(&tests);

done:
	if (f_xml) {
		fprintf(f_xml, "</testsuite>\n");
		fclose(f_xml);
	}
	if (f_txt) {
		fclose(f_txt);
	}

	return res;
}

/*!
 * \internal
 * \brief adds test to container sorted first by category then by name
 *
 * \retval 0 success
 * \retval -1 failure
 */
static int test_insert(struct ast_test *test)
{
	/* This is a slow operation that may need to be optimized in the future
	 * as the test framework expands.  At the moment we are doing string
	 * comparisons on every item within the list to insert in sorted order. */

	AST_LIST_LOCK(&tests);
	AST_LIST_INSERT_SORTALPHA(&tests, test, entry, info.category);
	AST_LIST_UNLOCK(&tests);

	return 0;
}

/*!
 * \internal
 * \brief removes test from container
 *
 * \return ast_test removed from list on success, or NULL on failure
 */
static struct ast_test *test_remove(ast_test_cb_t *cb)
{
	struct ast_test *cur = NULL;

	AST_LIST_LOCK(&tests);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&tests, cur, entry) {
		if (cur->cb == cb) {
			AST_LIST_REMOVE_CURRENT(entry);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&tests);

	return cur;
}

/*!
 * \brief compares two test categories to determine if cat1 resides in cat2
 * \internal
 *
 * \retval 0 true
 * \retval non-zero false
 */

static int test_cat_cmp(const char *cat1, const char *cat2)
{
	int len1 = 0;
	int len2 = 0;

	if (!cat1 || !cat2) {
		return -1;
	}

	len1 = strlen(cat1);
	len2 = strlen(cat2);

	if (len2 > len1) {
		return -1;
	}

	return strncmp(cat1, cat2, len2) ? 1 : 0;
}

/*!
 * \internal
 * \brief free an ast_test object and all it's data members
 */
static struct ast_test *test_free(struct ast_test *test)
{
	if (!test) {
		return NULL;
	}

	ast_free(test->status_str);
	ast_free(test);

	return NULL;
}

/*!
 * \internal
 * \brief allocate an ast_test object.
 */
static struct ast_test *test_alloc(ast_test_cb_t *cb)
{
	struct ast_test *test;

	if (!cb || !(test = ast_calloc(1, sizeof(*test)))) {
		return NULL;
	}

	test->cb = cb;

	test->cb(&test->info, TEST_INIT, test);

	if (ast_strlen_zero(test->info.name)) {
		ast_log(LOG_WARNING, "Test has no name, test registration refused.\n");
		return test_free(test);
	}

	if (ast_strlen_zero(test->info.category)) {
		ast_log(LOG_WARNING, "Test %s has no category, test registration refused.\n",
				test->info.name);
		return test_free(test);
	}

	if (test->info.category[0] != '/' || test->info.category[strlen(test->info.category) - 1] != '/') {
		ast_log(LOG_WARNING, "Test category is missing a leading or trailing backslash for test %s%s\n",
				test->info.category, test->info.name);
	}

	if (ast_strlen_zero(test->info.summary)) {
		ast_log(LOG_WARNING, "Test %s/%s has no summary, test registration refused.\n",
				test->info.category, test->info.name);
		return test_free(test);
	}

	if (ast_strlen_zero(test->info.description)) {
		ast_log(LOG_WARNING, "Test %s/%s has no description, test registration refused.\n",
				test->info.category, test->info.name);
		return test_free(test);
	}

	if (!(test->status_str = ast_str_create(128))) {
		return test_free(test);
	}

	return test;
}

static char *complete_test_category(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	int wordlen = strlen(word);
	char *ret = NULL;
	struct ast_test *test;

	AST_LIST_LOCK(&tests);
	AST_LIST_TRAVERSE(&tests, test, entry) {
		if (!strncasecmp(word, test->info.category, wordlen) && ++which > state) {
			ret = ast_strdup(test->info.category);
			break;
		}
	}
	AST_LIST_UNLOCK(&tests);
	return ret;
}

static char *complete_test_name(const char *line, const char *word, int pos, int state, const char *category)
{
	int which = 0;
	int wordlen = strlen(word);
	char *ret = NULL;
	struct ast_test *test;

	AST_LIST_LOCK(&tests);
	AST_LIST_TRAVERSE(&tests, test, entry) {
		if (!test_cat_cmp(test->info.category, category) && (!strncasecmp(word, test->info.name, wordlen) && ++which > state)) {
			ret = ast_strdup(test->info.name);
			break;
		}
	}
	AST_LIST_UNLOCK(&tests);
	return ret;
}

/* CLI commands */
static char *test_cli_show_registered(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%-25.25s %-30.30s %-40.40s %-13.13s\n"
	static const char * const option1[] = { "all", "category", NULL };
	static const char * const option2[] = { "name", NULL };
	struct ast_test *test = NULL;
	int count = 0;
	switch (cmd) {
	case CLI_INIT:
		e->command = "test show registered";

		e->usage =
			"Usage: 'test show registered' can be used in three ways.\n"
			"       1. 'test show registered all' shows all registered tests\n"
			"       2. 'test show registered category [test category]' shows all tests in the given\n"
			"          category.\n"
			"       3. 'test show registered category [test category] name [test name]' shows all\n"
			"           tests in a given category matching a given name\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return ast_cli_complete(a->word, option1, a->n);
		}
		if (a->pos == 4) {
			return complete_test_category(a->line, a->word, a->pos, a->n);
		}
		if (a->pos == 5) {
			return ast_cli_complete(a->word, option2, a->n);
		}
		if (a->pos == 6) {
			return complete_test_name(a->line, a->word, a->pos, a->n, a->argv[3]);
		}
		return NULL;
	case CLI_HANDLER:
		if ((a->argc < 4) || (a->argc == 6) || (a->argc > 7) ||
			((a->argc == 4) && strcmp(a->argv[3], "all")) ||
			((a->argc == 7) && strcmp(a->argv[5], "name"))) {
			return CLI_SHOWUSAGE;
		}
		ast_cli(a->fd, FORMAT, "Category", "Name", "Summary", "Test Result");
		ast_cli(a->fd, FORMAT, "--------", "----", "-------", "-----------");
		AST_LIST_LOCK(&tests);
		AST_LIST_TRAVERSE(&tests, test, entry) {
			if ((a->argc == 4) ||
				 ((a->argc == 5) && !test_cat_cmp(test->info.category, a->argv[4])) ||
				 ((a->argc == 7) && !strcmp(test->info.category, a->argv[4]) && !strcmp(test->info.name, a->argv[6]))) {

				ast_cli(a->fd, FORMAT, test->info.category, test->info.name,
						test->info.summary, test_result2str[test->state]);
				count++;
			}
		}
		AST_LIST_UNLOCK(&tests);
		ast_cli(a->fd, FORMAT, "--------", "----", "-------", "-----------");
		ast_cli(a->fd, "\n%d Registered Tests Matched\n", count);
	default:
		return NULL;
	}

	return CLI_SUCCESS;
}

static char *test_cli_execute_registered(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	static const char * const option1[] = { "all", "category", NULL };
	static const char * const option2[] = { "name", NULL };

	switch (cmd) {
	case CLI_INIT:
		e->command = "test execute";
		e->usage =
			"Usage: test execute can be used in three ways.\n"
			"       1. 'test execute all' runs all registered tests\n"
			"       2. 'test execute category [test category]' runs all tests in the given\n"
			"          category.\n"
			"       3. 'test execute category [test category] name [test name]' runs all\n"
			"           tests in a given category matching a given name\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return ast_cli_complete(a->word, option1, a->n);
		}
		if (a->pos == 3) {
			return complete_test_category(a->line, a->word, a->pos, a->n);
		}
		if (a->pos == 4) {
			return ast_cli_complete(a->word, option2, a->n);
		}
		if (a->pos == 5) {
			return complete_test_name(a->line, a->word, a->pos, a->n, a->argv[3]);
		}
		return NULL;
	case CLI_HANDLER:

		if (a->argc < 3|| a->argc > 6) {
			return CLI_SHOWUSAGE;
		}

		if ((a->argc == 3) && !strcmp(a->argv[2], "all")) { /* run all registered tests */
			ast_cli(a->fd, "Running all available tests...\n\n");
			test_execute_multiple(NULL, NULL, a);
		} else if (a->argc == 4) { /* run only tests within a category */
			ast_cli(a->fd, "Running all available tests matching category %s\n\n", a->argv[3]);
			test_execute_multiple(NULL, a->argv[3], a);
		} else if (a->argc == 6) { /* run only a single test matching the category and name */
			ast_cli(a->fd, "Running all available tests matching category %s and name %s\n\n", a->argv[3], a->argv[5]);
			test_execute_multiple(a->argv[5], a->argv[3], a);
		} else {
			return CLI_SHOWUSAGE;
		}

		AST_LIST_LOCK(&tests);
		if (!(last_results.last_passed + last_results.last_failed)) {
			ast_cli(a->fd, "--- No Tests Found! ---\n");
		}
		ast_cli(a->fd, "\n%d Test(s) Executed  %d Passed  %d Failed\n",
			(last_results.last_passed + last_results.last_failed),
			last_results.last_passed,
			last_results.last_failed);
		AST_LIST_UNLOCK(&tests);
	default:
		return NULL;
	}

	return CLI_SUCCESS;
}

static char *test_cli_show_results(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT_RES_ALL1 "%s%s %-30.30s %-25.25s %-10.10s\n"
#define FORMAT_RES_ALL2 "%s%s %-30.30s %-25.25s %s%ums\n"
	static const char * const option1[] = { "all", "failed", "passed", NULL };
	char result_buf[32] = { 0 };
	struct ast_test *test = NULL;
	int failed = 0;
	int passed = 0;
	int mode;  /* 0 for show all, 1 for show fail, 2 for show passed */

	switch (cmd) {
	case CLI_INIT:
		e->command = "test show results";
		e->usage =
			"Usage: test show results can be used in three ways\n"
			"       1. 'test show results all' Displays results for all executed tests.\n"
			"       2. 'test show results passed' Displays results for all passed tests.\n"
			"       3. 'test show results failed' Displays results for all failed tests.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return ast_cli_complete(a->word, option1, a->n);
		}
		return NULL;
	case CLI_HANDLER:

		/* verify input */
		if (a->argc != 4) {
			return CLI_SHOWUSAGE;
		} else if (!strcmp(a->argv[3], "passed")) {
			mode = 2;
		} else if (!strcmp(a->argv[3], "failed")) {
			mode = 1;
		} else if (!strcmp(a->argv[3], "all")) {
			mode = 0;
		} else {
			return CLI_SHOWUSAGE;
		}

		ast_cli(a->fd, FORMAT_RES_ALL1, "Result", "", "Name", "Category", "Time");
		AST_LIST_LOCK(&tests);
		AST_LIST_TRAVERSE(&tests, test, entry) {
			if (test->state == AST_TEST_NOT_RUN) {
				continue;
			}
			test->state == AST_TEST_FAIL ? failed++ : passed++;
			if (!mode || ((mode == 1) && (test->state == AST_TEST_FAIL)) || ((mode == 2) && (test->state == AST_TEST_PASS))) {
				/* give our results pretty colors */
				term_color(result_buf, test_result2str[test->state],
					(test->state == AST_TEST_FAIL) ? COLOR_RED : COLOR_GREEN,
					0, sizeof(result_buf));

				ast_cli(a->fd, FORMAT_RES_ALL2,
					result_buf,
					"  ",
					test->info.name,
					test->info.category,
					test->time ? " " : "<",
					test->time ? test->time : 1);
			}
		}
		AST_LIST_UNLOCK(&tests);

		ast_cli(a->fd, "%d Test(s) Executed  %d Passed  %d Failed\n", (failed + passed), passed, failed);
	default:
		return NULL;
	}
	return CLI_SUCCESS;
}

static char *test_cli_generate_results(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	static const char * const option[] = { "xml", "txt", NULL };
	const char *file = NULL;
	const char *type = "";
	int isxml = 0;
	int res = 0;
	struct ast_str *buf = NULL;
	struct timeval time = ast_tvnow();

	switch (cmd) {
	case CLI_INIT:
		e->command = "test generate results";
		e->usage =
			"Usage: 'test generate results'\n"
			"       Generates test results in either xml or txt format. An optional \n"
			"       file path may be provided to specify the location of the xml or\n"
			"       txt file\n"
			"       \nExample usage:\n"
			"       'test generate results xml' this writes to a default file\n"
			"       'test generate results xml /path/to/file.xml' writes to specified file\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return ast_cli_complete(a->word, option, a->n);
		}
		return NULL;
	case CLI_HANDLER:

		/* verify input */
		if (a->argc < 4 || a->argc > 5) {
			return CLI_SHOWUSAGE;
		} else if (!strcmp(a->argv[3], "xml")) {
			type = "xml";
			isxml = 1;
		} else if (!strcmp(a->argv[3], "txt")) {
			type = "txt";
		} else {
			return CLI_SHOWUSAGE;
		}

		if (a->argc == 5) {
			file = a->argv[4];
		} else {
			if (!(buf = ast_str_create(256))) {
				return NULL;
			}
			ast_str_set(&buf, 0, "%s/asterisk_test_results-%ld.%s", ast_config_AST_LOG_DIR, (long) time.tv_sec, type);

			file = ast_str_buffer(buf);
		}

		if (isxml) {
			res = test_generate_results(NULL, NULL, file, NULL);
		} else {
			res = test_generate_results(NULL, NULL, NULL, file);
		}

		if (!res) {
			ast_cli(a->fd, "Results Generated Successfully: %s\n", S_OR(file, ""));
		} else {
			ast_cli(a->fd, "Results Could Not Be Generated: %s\n", S_OR(file, ""));
		}

		ast_free(buf);
	default:
		return NULL;
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry test_cli[] = {
	AST_CLI_DEFINE(test_cli_show_registered,           "show registered tests"),
	AST_CLI_DEFINE(test_cli_execute_registered,        "execute registered tests"),
	AST_CLI_DEFINE(test_cli_show_results,              "show last test results"),
	AST_CLI_DEFINE(test_cli_generate_results,          "generate test results to file"),
};

int __ast_test_suite_event_notify(const char *file, const char *func, int line,
		const char *state, const char *fmt, ...)
{
	struct ast_str *buf = NULL;
	va_list ap;

	if (!(buf = ast_str_create(128))) {
		return -1;
	}

	va_start(ap, fmt);
	ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);

	manager_event(EVENT_FLAG_TEST, "TestEvent",
		"Type: StateChange\r\n"
		"State: %s\r\n"
		"AppFile: %s\r\n"
		"AppFunction: %s\r\n"
		"AppLine: %d\r\n%s\r\n",
		state, file, func, line, ast_str_buffer(buf));

	ast_free(buf);

	return 0;
}

int __ast_test_suite_assert_notify(const char *file, const char *func, int line,
		const char *exp)
{
	manager_event(EVENT_FLAG_TEST, "TestEvent",
		"Type: Assert\r\n"
		"AppFile: %s\r\n"
		"AppFunction: %s\r\n"
		"AppLine: %d\r\n"
		"Expression: %s\r\n",
		file, func, line, exp);

	return 0;
}

#endif /* TEST_FRAMEWORK */

int ast_test_init()
{
#ifdef TEST_FRAMEWORK
	/* Register cli commands */
	ast_cli_register_multiple(test_cli, ARRAY_LEN(test_cli));
#endif

	return 0;
}
