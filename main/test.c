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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"

#ifdef TEST_FRAMEWORK
#include "asterisk/test.h"
#include "asterisk/logger.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"
#include "asterisk/ast_version.h"
#include "asterisk/paths.h"
#include "asterisk/time.h"
#include "asterisk/stasis.h"
#include "asterisk/json.h"
#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/json.h"
#include "asterisk/app.h"		/* for ast_replace_sigchld(), etc. */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

/*! \since 12
 * \brief The topic for test suite messages
 */
struct stasis_topic *test_suite_topic;

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
	enum ast_test_result_state state;   /*!< current test state */
	unsigned int time;                  /*!< time in ms test took */
	ast_test_cb_t *cb;                  /*!< test callback function */
	ast_test_init_cb_t *init_cb;        /*!< test init function */
	ast_test_cleanup_cb_t *cleanup_cb;  /*!< test cleanup function */
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

#define zfclose(fp) \
	({ if (fp != NULL) { \
		fclose(fp); \
		fp = NULL; \
	   } \
	   (void)0; \
	 })

#define zclose(fd) \
	({ if (fd != -1) { \
		close(fd); \
		fd = -1; \
	   } \
	   (void)0; \
	 })

#define movefd(oldfd, newfd) \
	({ if (oldfd != newfd) { \
		dup2(oldfd, newfd); \
		close(oldfd); \
		oldfd = -1; \
	   } \
	   (void)0; \
	 })

#define lowerfd(oldfd) \
	({ int newfd = dup(oldfd); \
	   if (newfd > oldfd) \
		close(newfd); \
	   else { \
		close(oldfd); \
		oldfd = newfd; \
	   } \
	   (void)0; \
	 })

/*! List of registered test definitions */
static AST_LIST_HEAD_STATIC(tests, ast_test);

static struct ast_test *test_alloc(ast_test_cb_t *cb);
static struct ast_test *test_free(struct ast_test *test);
static int test_insert(struct ast_test *test);
static struct ast_test *test_remove(ast_test_cb_t *cb);
static int test_cat_cmp(const char *cat1, const char *cat2);
static int registration_errors = 0;

void ast_test_debug(struct ast_test *test, const char *fmt, ...)
{
	struct ast_str *buf = NULL;
	va_list ap;

	buf = ast_str_create(128);
	if (!buf) {
		return;
	}

	va_start(ap, fmt);
	ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);

	if (test->cli) {
		ast_cli(test->cli->fd, "%s", ast_str_buffer(buf));
	}

	ast_free(buf);
}

int __ast_test_status_update(const char *file, const char *func, int line, struct ast_test *test, const char *fmt, ...)
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

int ast_test_register_init(const char *category, ast_test_init_cb_t *cb)
{
	struct ast_test *test;
	int registered = 1;

	AST_LIST_LOCK(&tests);
	AST_LIST_TRAVERSE(&tests, test, entry) {
		if (!(test_cat_cmp(test->info.category, category))) {
			test->init_cb = cb;
			registered = 0;
		}
	}
	AST_LIST_UNLOCK(&tests);

	return registered;
}

int ast_test_register_cleanup(const char *category, ast_test_cleanup_cb_t *cb)
{
	struct ast_test *test;
	int registered = 1;

	AST_LIST_LOCK(&tests);
	AST_LIST_TRAVERSE(&tests, test, entry) {
		if (!(test_cat_cmp(test->info.category, category))) {
			test->cleanup_cb = cb;
			registered = 0;
		}
	}
	AST_LIST_UNLOCK(&tests);

	return registered;
}

int ast_test_register(ast_test_cb_t *cb)
{
	struct ast_test *test;

	if (!cb) {
		ast_log(LOG_ERROR, "Attempted to register test without all required information\n");
		registration_errors++;
		return -1;
	}

	if (!(test = test_alloc(cb))) {
		registration_errors++;
		return -1;
	}

	if (test_insert(test)) {
		test_free(test);
		registration_errors++;
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
	enum ast_test_result_state result;

	ast_str_reset(test->status_str);

	begin = ast_tvnow();
	if (test->init_cb && test->init_cb(&test->info, test)) {
		test->state = AST_TEST_FAIL;
		goto exit;
	}
	test->state = AST_TEST_NOT_RUN;
	result = test->cb(&test->info, TEST_EXECUTE, test);
	if (test->state != AST_TEST_FAIL) {
		test->state = result;
	}
	if (test->cleanup_cb && test->cleanup_cb(&test->info, test)) {
		test->state = AST_TEST_FAIL;
	}
exit:
	test->time = ast_tvdiff_ms(ast_tvnow(), begin);
}

void ast_test_set_result(struct ast_test *test, enum ast_test_result_state state)
{
	if (test->state == AST_TEST_FAIL || state == AST_TEST_NOT_RUN) {
		return;
	}
	test->state = state;
}

void ast_test_capture_init(struct ast_test_capture *capture)
{
	capture->outbuf = capture->errbuf = NULL;
	capture->pid = capture->exitcode = -1;
}

void ast_test_capture_free(struct ast_test_capture *capture)
{
	if (capture) {
		/*
		 * Need to use ast_std_free because this memory wasn't
		 * allocated by the astmm functions.
		 */
		ast_std_free(capture->outbuf);
		capture->outbuf = NULL;
		ast_std_free(capture->errbuf);
		capture->errbuf = NULL;
	}
	capture->pid = -1;
	capture->exitcode = -1;
}

int ast_test_capture_command(struct ast_test_capture *capture, const char *file, char *const argv[], const char *data, unsigned datalen)
{
	int fd0[2] = { -1, -1 }, fd1[2] = { -1, -1 }, fd2[2] = { -1, -1 };
	pid_t pid = -1;
	int status = 0;
	FILE *cmd = NULL, *out = NULL, *err = NULL;

	ast_test_capture_init(capture);

	if (data != NULL && datalen > 0) {
		if (pipe(fd0) == -1) {
			ast_log(LOG_ERROR, "Couldn't open stdin pipe: %s\n", strerror(errno));
			goto cleanup;
		}
		fcntl(fd0[1], F_SETFL, fcntl(fd0[1], F_GETFL, 0) | O_NONBLOCK);
	} else {
		if ((fd0[0] = open("/dev/null", O_RDONLY)) == -1) {
			ast_log(LOG_ERROR, "Couldn't open /dev/null: %s\n", strerror(errno));
			goto cleanup;
		}
	}

	if (pipe(fd1) == -1) {
		ast_log(LOG_ERROR, "Couldn't open stdout pipe: %s\n", strerror(errno));
		goto cleanup;
	}

	if (pipe(fd2) == -1) {
		ast_log(LOG_ERROR, "Couldn't open stderr pipe: %s\n", strerror(errno));
		goto cleanup;
	}

	/* we don't want anyone else reaping our children */
	ast_replace_sigchld();

	if ((pid = fork()) == -1) {
		ast_log(LOG_ERROR, "Failed to fork(): %s\n", strerror(errno));
		goto cleanup;

	} else if (pid == 0) {
		fclose(stdin);
		zclose(fd0[1]);
		zclose(fd1[0]);
		zclose(fd2[0]);

		movefd(fd0[0], 0);
		movefd(fd1[1], 1);
		movefd(fd2[1], 2);

		execvp(file, argv);
		ast_log(LOG_ERROR, "Failed to execv(): %s\n", strerror(errno));
		exit(1);

	} else {
		char buf[BUFSIZ];
		int wstatus, n, nfds;
		fd_set readfds, writefds;
		unsigned i;

		zclose(fd0[0]);
		zclose(fd1[1]);
		zclose(fd2[1]);

		lowerfd(fd0[1]);
		lowerfd(fd1[0]);
		lowerfd(fd2[0]);

		if ((cmd = fmemopen(buf, sizeof(buf), "w")) == NULL) {
			ast_log(LOG_ERROR, "Failed to open memory buffer: %s\n", strerror(errno));
			kill(pid, SIGKILL);
			goto cleanup;
		}
		for (i = 0; argv[i] != NULL; ++i) {
			if (i > 0) {
				fputc(' ', cmd);
			}
			fputs(argv[i], cmd);
		}
		zfclose(cmd);

		ast_log(LOG_TRACE, "run: %.*s\n", (int)sizeof(buf), buf);

		if ((out = open_memstream(&capture->outbuf, &capture->outlen)) == NULL) {
			ast_log(LOG_ERROR, "Failed to open output buffer: %s\n", strerror(errno));
			kill(pid, SIGKILL);
			goto cleanup;
		}

		if ((err = open_memstream(&capture->errbuf, &capture->errlen)) == NULL) {
			ast_log(LOG_ERROR, "Failed to open error buffer: %s\n", strerror(errno));
			kill(pid, SIGKILL);
			goto cleanup;
		}

		while (1) {
			n = waitpid(pid, &wstatus, WNOHANG);

			if (n == pid && WIFEXITED(wstatus)) {
				zclose(fd0[1]);
				zclose(fd1[0]);
				zclose(fd2[0]);
				zfclose(out);
				zfclose(err);

				capture->pid = pid;
				capture->exitcode = WEXITSTATUS(wstatus);

				ast_log(LOG_TRACE, "run: pid %d exits %d\n", capture->pid, capture->exitcode);

				break;
			}

			/* a function that does the opposite of ffs()
			 * would be handy here for finding the highest
			 * descriptor number.
			 */
			nfds = MAX(fd0[1], MAX(fd1[0], fd2[0])) + 1;

			FD_ZERO(&readfds);
			FD_ZERO(&writefds);

			if (fd0[1] != -1) {
				if (data != NULL && datalen > 0)
					FD_SET(fd0[1], &writefds);
			}
			if (fd1[0] != -1) {
				FD_SET(fd1[0], &readfds);
			}
			if (fd2[0] != -1) {
				FD_SET(fd2[0], &readfds);
			}

			/* not clear that exception fds are meaningful
			 * with non-network descriptors.
			 */
			n = select(nfds, &readfds, &writefds, NULL, NULL);

			/* A version of FD_ISSET() that is tolerant of -1 file descriptors */
#define SAFE_FD_ISSET(fd, setptr) ((fd) != -1 && FD_ISSET((fd), setptr))

			if (SAFE_FD_ISSET(fd0[1], &writefds)) {
				n = write(fd0[1], data, datalen);
				if (n > 0) {
					data += n;
					datalen -= MIN(datalen, n);
					/* out of data, so close stdin */
					if (datalen == 0)
						zclose(fd0[1]);
				} else {
					zclose(fd0[1]);
				}
			}

			if (SAFE_FD_ISSET(fd1[0], &readfds)) {
				n = read(fd1[0], buf, sizeof(buf));
				if (n > 0) {
					fwrite(buf, sizeof(char), n, out);
				} else {
					zclose(fd1[0]);
				}
			}

			if (SAFE_FD_ISSET(fd2[0], &readfds)) {
				n = read(fd2[0], buf, sizeof(buf));
				if (n > 0) {
					fwrite(buf, sizeof(char), n, err);
				} else {
					zclose(fd2[0]);
				}
			}

#undef SAFE_FD_ISSET
		}
		status = 1;

cleanup:
		ast_unreplace_sigchld();

		zfclose(cmd);
		zfclose(out);
		zfclose(err);

		zclose(fd0[1]);
		zclose(fd1[0]);
		zclose(fd1[1]);
		zclose(fd2[0]);
		zclose(fd2[1]);

		return status;
	}
}

/*
 * These are the Java reserved words we need to munge so Jenkins
 * doesn't barf on them.
 */
static char *reserved_words[] = {
	"abstract", "arguments", "as", "assert", "await",
	"boolean", "break", "byte", "case", "catch", "char", "class",
	"const", "continue", "debugger", "def", "default", "delete", "do",
	"double", "else", "enum", "eval", "export", "extends", "false",
	"final", "finally", "float", "for", "function", "goto", "if",
	"implements", "import", "in", "instanceof", "int", "interface",
	"let", "long", "native", "new", "null", "package", "private",
	"protected", "public", "return", "short", "static", "strictfp",
	"string", "super", "switch", "synchronized", "this", "throw", "throws",
	"trait", "transient", "true", "try", "typeof", "var", "void",
	"volatile", "while", "with", "yield" };

static int is_reserved_word(const char *word)
{
	int i;

	for (i = 0; i < ARRAY_LEN(reserved_words); i++) {
		if (strcmp(word, reserved_words[i]) == 0) {
			return 1;
		}
	}

	return 0;
}

static void test_xml_entry(struct ast_test *test, FILE *f)
{
	/* We need a copy of the category skipping past the initial '/' */
	char *test_cat = ast_strdupa(test->info.category + 1);
	char *next_cat;
	char *test_name = (char *)test->info.name;
	struct ast_str *category = ast_str_create(strlen(test->info.category) + 32);

	if (!category || test->state == AST_TEST_NOT_RUN) {
		ast_free(category);

		return;
	}

	while ((next_cat = ast_strsep(&test_cat, '/', AST_STRSEP_TRIM))) {
		char *prefix = "";

		if (is_reserved_word(next_cat)) {
			prefix = "_";
		}
		ast_str_append(&category, 0, ".%s%s", prefix, next_cat);
	}
	test_cat = ast_str_buffer(category);
	/* Skip past the initial '.' */
	test_cat++;

	if (is_reserved_word(test->info.name)) {
		size_t name_length = strlen(test->info.name) + 2;

		test_name = ast_alloca(name_length);
		snprintf(test_name, name_length, "_%s", test->info.name);
	}

	fprintf(f, "\t\t<testcase time=\"%u.%u\" classname=\"%s\" name=\"%s\"%s>\n",
			test->time / 1000, test->time % 1000,
			test_cat, test_name,
			test->state == AST_TEST_PASS ? "/" : "");

	ast_free(category);

	if (test->state == AST_TEST_FAIL) {
		fprintf(f, "\t\t\t<failure><![CDATA[\n%s\n\t\t]]></failure>\n",
				S_OR(ast_str_buffer(test->status_str), "NA"));
		fprintf(f, "\t\t</testcase>\n");
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
		fprintf(f,   "Time:              %u\n", test->time);
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
 * \param category category to run (optional)
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
			if (!test_cat_cmp(test->info.category, category) && !test->info.explicit_only) {
				execute = 1;
			}
			break;
		case TEST_NAME_CATEGORY:
			if (!(test_cat_cmp(test->info.category, category)) && !(strcmp(test->info.name, name))) {
				execute = 1;
			}
			break;
		case TEST_ALL:
			execute = !test->info.explicit_only;
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
				ast_cli(cli->fd, "END    %s - %s Time: %s%ums Result: %s\n",
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
		if (test->state != AST_TEST_NOT_RUN) {
			last_results.total_tests++;
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
 * \param category category to generate (optional)
 * \param xml_path path to xml file to generate (optional)
 * \param txt_path path to txt file to generate (optional)
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
		fprintf(f_xml, "<testsuites>\n");
		fprintf(f_xml, "\t<testsuite errors=\"0\" time=\"%u.%u\" tests=\"%u\" failures=\"%u\" "
				"name=\"AsteriskUnitTests\">\n",
				last_results.total_time / 1000, last_results.total_time % 1000,
				last_results.total_tests, last_results.total_failed);
		fprintf(f_xml, "\t\t<properties>\n");
		fprintf(f_xml, "\t\t\t<property name=\"version\" value=\"%s\"/>\n", ast_get_version());
		fprintf(f_xml, "\t\t</properties>\n");
	}

	/* txt header information */
	if (f_txt) {
		fprintf(f_txt, "Asterisk Version:         %s\n", ast_get_version());
		fprintf(f_txt, "Asterisk Version Number:  %s\n", ast_get_version_num());
		fprintf(f_txt, "Number of Tests:          %u\n", last_results.total_tests);
		fprintf(f_txt, "Number of Tests Executed: %u\n", (last_results.total_passed + last_results.total_failed));
		fprintf(f_txt, "Passed Tests:             %u\n", last_results.total_passed);
		fprintf(f_txt, "Failed Tests:             %u\n", last_results.total_failed);
		fprintf(f_txt, "Total Execution Time:     %u\n", last_results.total_time);
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
		fprintf(f_xml, "\t</testsuite>\n");
		fprintf(f_xml, "</testsuites>\n");
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
 * \return ast_test removed from list on success
 * \retval NULL on failure
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

	test = ast_calloc(1, sizeof(*test));
	if (!test) {
		ast_log(LOG_ERROR, "Failed to allocate test, registration failed.\n");
		return NULL;
	}

	test->cb = cb;

	test->cb(&test->info, TEST_INIT, test);

	if (ast_strlen_zero(test->info.name)) {
		ast_log(LOG_ERROR, "Test has no name, test registration refused.\n");
		return test_free(test);
	}

	if (ast_strlen_zero(test->info.category)) {
		ast_log(LOG_ERROR, "Test %s has no category, test registration refused.\n",
			test->info.name);
		return test_free(test);
	}

	if (test->info.category[0] != '/' || test->info.category[strlen(test->info.category) - 1] != '/') {
		ast_log(LOG_WARNING, "Test category '%s' for test '%s' is missing a leading or trailing slash.\n",
			test->info.category, test->info.name);
		/*
		 * Flag an error anyways so test_registrations fails but allow the
		 * test to be registered.
		 */
		++registration_errors;
	}

	if (ast_strlen_zero(test->info.summary)) {
		ast_log(LOG_ERROR, "Test %s%s has no summary, test registration refused.\n",
			test->info.category, test->info.name);
		return test_free(test);
	}
	if (test->info.summary[strlen(test->info.summary) - 1] == '\n') {
		ast_log(LOG_WARNING, "Test %s%s summary has a trailing newline.\n",
			test->info.category, test->info.name);
		/*
		 * Flag an error anyways so test_registrations fails but allow the
		 * test to be registered.
		 */
		++registration_errors;
	}

	if (ast_strlen_zero(test->info.description)) {
		ast_log(LOG_ERROR, "Test %s%s has no description, test registration refused.\n",
			test->info.category, test->info.name);
		return test_free(test);
	}
	if (test->info.description[strlen(test->info.description) - 1] == '\n') {
		ast_log(LOG_WARNING, "Test %s%s description has a trailing newline.\n",
			test->info.category, test->info.name);
		/*
		 * Flag an error anyways so test_registrations fails but allow the
		 * test to be registered.
		 */
		++registration_errors;
	}

	if (!(test->status_str = ast_str_create(128))) {
		ast_log(LOG_ERROR, "Failed to allocate status_str for %s%s, test registration failed.\n",
			test->info.category, test->info.name);
		return test_free(test);
	}

	return test;
}

static char *complete_test_category(const char *word)
{
	int wordlen = strlen(word);
	struct ast_test *test;

	AST_LIST_LOCK(&tests);
	AST_LIST_TRAVERSE(&tests, test, entry) {
		if (!strncasecmp(word, test->info.category, wordlen)) {
			if (ast_cli_completion_add(ast_strdup(test->info.category))) {
				break;
			}
		}
	}
	AST_LIST_UNLOCK(&tests);

	return NULL;
}

static char *complete_test_name(const char *word, const char *category)
{
	int wordlen = strlen(word);
	struct ast_test *test;

	AST_LIST_LOCK(&tests);
	AST_LIST_TRAVERSE(&tests, test, entry) {
		if (!test_cat_cmp(test->info.category, category) && !strncasecmp(word, test->info.name, wordlen)) {
			if (ast_cli_completion_add(ast_strdup(test->info.name))) {
				break;
			}
		}
	}
	AST_LIST_UNLOCK(&tests);

	return NULL;
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
			return ast_cli_complete(a->word, option1, -1);
		}
		if (a->pos == 4 && !strcasecmp(a->argv[3], "category")) {
			return complete_test_category(a->word);
		}
		if (a->pos == 5) {
			return ast_cli_complete(a->word, option2, -1);
		}
		if (a->pos == 6) {
			return complete_test_name(a->word, a->argv[4]);
		}
		return NULL;
	case CLI_HANDLER:
		if ((a->argc < 4) || (a->argc == 6) || (a->argc > 7) ||
			((a->argc == 4) && strcasecmp(a->argv[3], "all")) ||
			((a->argc == 7) && strcasecmp(a->argv[5], "name"))) {
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
			return ast_cli_complete(a->word, option1, -1);
		}
		if (a->pos == 3 && !strcasecmp(a->argv[2], "category")) {
			return complete_test_category(a->word);
		}
		if (a->pos == 4) {
			return ast_cli_complete(a->word, option2, -1);
		}
		if (a->pos == 5) {
			return complete_test_name(a->word, a->argv[3]);
		}
		return NULL;
	case CLI_HANDLER:

		if (a->argc < 3|| a->argc > 6) {
			return CLI_SHOWUSAGE;
		}

		if ((a->argc == 3) && !strcasecmp(a->argv[2], "all")) { /* run all registered tests */
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
		ast_cli(a->fd, "\n%u Test(s) Executed  %u Passed  %u Failed\n",
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
			return ast_cli_complete(a->word, option1, -1);
		}
		return NULL;
	case CLI_HANDLER:

		/* verify input */
		if (a->argc != 4) {
			return CLI_SHOWUSAGE;
		} else if (!strcasecmp(a->argv[3], "passed")) {
			mode = 2;
		} else if (!strcasecmp(a->argv[3], "failed")) {
			mode = 1;
		} else if (!strcasecmp(a->argv[3], "all")) {
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
			return ast_cli_complete(a->word, option, -1);
		}
		return NULL;
	case CLI_HANDLER:

		/* verify input */
		if (a->argc < 4 || a->argc > 5) {
			return CLI_SHOWUSAGE;
		} else if (!strcasecmp(a->argv[3], "xml")) {
			type = "xml";
			isxml = 1;
		} else if (!strcasecmp(a->argv[3], "txt")) {
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

struct stasis_topic *ast_test_suite_topic(void)
{
	return test_suite_topic;
}

/*!
 * \since 12
 * \brief A wrapper object that can be ao2 ref counted around an \ref ast_json blob
 */
struct ast_test_suite_message_payload {
	struct ast_json *blob; /*!< The actual blob that we want to deliver */
};

/*! \internal
 * \since 12
 * \brief Destructor for \ref ast_test_suite_message_payload
 */
static void test_suite_message_payload_dtor(void *obj)
{
	struct ast_test_suite_message_payload *payload = obj;

	if (payload->blob) {
		ast_json_unref(payload->blob);
	}
}

struct ast_json *ast_test_suite_get_blob(struct ast_test_suite_message_payload *payload)
{
	return payload->blob;
}

static struct ast_manager_event_blob *test_suite_event_to_ami(struct stasis_message *msg)
{
	RAII_VAR(struct ast_str *, packet_string, ast_str_create(128), ast_free);
	struct ast_test_suite_message_payload *payload;
	struct ast_json *blob;
	const char *type;

	payload = stasis_message_data(msg);
	if (!payload) {
		return NULL;
	}
	blob = ast_test_suite_get_blob(payload);
	if (!blob) {
		return NULL;
	}

	type = ast_json_string_get(ast_json_object_get(blob, "type"));
	if (ast_strlen_zero(type) || strcmp("testevent", type)) {
		return NULL;
	}

	ast_str_append(&packet_string, 0, "Type: StateChange\r\n");
	ast_str_append(&packet_string, 0, "State: %s\r\n",
		ast_json_string_get(ast_json_object_get(blob, "state")));
	ast_str_append(&packet_string, 0, "AppFile: %s\r\n",
		ast_json_string_get(ast_json_object_get(blob, "appfile")));
	ast_str_append(&packet_string, 0, "AppFunction: %s\r\n",
		ast_json_string_get(ast_json_object_get(blob, "appfunction")));
	ast_str_append(&packet_string, 0, "AppLine: %jd\r\n",
		ast_json_integer_get(ast_json_object_get(blob, "line")));
	ast_str_append(&packet_string, 0, "%s\r\n",
		ast_json_string_get(ast_json_object_get(blob, "data")));

	return ast_manager_event_blob_create(EVENT_FLAG_REPORTING,
		"TestEvent",
		"%s",
		ast_str_buffer(packet_string));
}

/*! \since 12
 * \brief The message type for test suite messages
 */
STASIS_MESSAGE_TYPE_DEFN(ast_test_suite_message_type,
	.to_ami = test_suite_event_to_ami);

void __ast_test_suite_event_notify(const char *file, const char *func, int line, const char *state, const char *fmt, ...)
{
	RAII_VAR(struct ast_test_suite_message_payload *, payload,
			NULL,
			ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, buf, NULL, ast_free);
	va_list ap;

	if (!ast_test_suite_message_type()) {
		return;
	}

	buf = ast_str_create(128);
	if (!buf) {
		return;
	}

	payload = ao2_alloc(sizeof(*payload), test_suite_message_payload_dtor);
	if (!payload) {
		return;
	}

	va_start(ap, fmt);
	ast_str_set_va(&buf, 0, fmt, ap);
	va_end(ap);
	payload->blob = ast_json_pack("{s: s, s: s, s: s, s: s, s: i, s: s}",
			     "type", "testevent",
			     "state", state,
			     "appfile", file,
			     "appfunction", func,
			     "line", line,
			     "data", ast_str_buffer(buf));
	if (!payload->blob) {
		return;
	}
	msg = stasis_message_create(ast_test_suite_message_type(), payload);
	if (!msg) {
		return;
	}
	stasis_publish(ast_test_suite_topic(), msg);
}

AST_TEST_DEFINE(test_registrations)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "registrations";
		info->category = "/main/test/";
		info->summary = "Validate Test Registration Data.";
		info->description = "Validate Test Registration Data.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (registration_errors) {
		ast_test_status_update(test,
			"%d test registration error%s occurred.  See startup logs for details.\n",
			registration_errors, registration_errors > 1 ? "s" : "");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static void test_cleanup(void)
{
	AST_TEST_UNREGISTER(test_registrations);
	ast_cli_unregister_multiple(test_cli, ARRAY_LEN(test_cli));
	ao2_cleanup(test_suite_topic);
	test_suite_topic = NULL;
	STASIS_MESSAGE_TYPE_CLEANUP(ast_test_suite_message_type);
}
#endif /* TEST_FRAMEWORK */

int ast_test_init(void)
{
#ifdef TEST_FRAMEWORK
	ast_register_cleanup(test_cleanup);

	/* Create stasis topic */
	test_suite_topic = stasis_topic_create("testsuite:all");
	if (!test_suite_topic) {
		return -1;
	}

	if (STASIS_MESSAGE_TYPE_INIT(ast_test_suite_message_type) != 0) {
		return -1;
	}

	AST_TEST_REGISTER(test_registrations);

	/* Register cli commands */
	ast_cli_register_multiple(test_cli, ARRAY_LEN(test_cli));
#endif

	return 0;
}

