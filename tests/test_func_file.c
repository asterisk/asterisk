/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Tilghman Lesher <tlesher AT digium DOT com>
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
 * \brief Function FILE tests
 *
 * \author\verbatim Tilghman Lesher <tlesher AT digium DOT com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/pbx.h"

#define C1024 \
		"1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890ABCDEF"

static struct {
	const char *contents;
	const char *args;
	const char *value;
} read_tests[] = {
	/* 4 different ways of specifying the first character */
	{ "123456789", "0,1", "1" },
	{ "123456789", "0,-8", "1" },
	{ "123456789", "-9,1", "1" },
	{ "123456789", "-9,-8", "1" },
	/* Does 0-length work? */
	{ "123456789", "0,0", "" },
	{ "123456789", "-9,0", "" },
	{ "123456789", "-9,-9", "" },
	/* Does negative length work? */
	{ "123456789", "5,-6", "" },
	{ "123456789", "-5,-6", "" },
	/* No length */
	{ "123456789", "-5", "56789" },
	{ "123456789", "4", "56789" },
	/* Passed file length */
	{ "123456789", "8,10", "9" },
	{ "123456789", "10,1", "" },
	/* Middle of file */
	{ "123456789", "2,5", "34567" },
	{ "123456789", "-7,5", "34567" },
	/* Line mode, 4 ways of specifying the first character */
	{ "123\n456\n789\n", "0,1,l", "123\n" },
	{ "123\n456\n789\n", "-3,1,l", "123\n" },
	{ "123\n456\n789\n", "0,-2,l", "123\n" },
	{ "123\n456\n789\n", "-3,-2,l", "123\n" },
	/* Line mode, 0-length */
	{ "123\n456\n789\n", "0,0,l", "" },
	{ "123\n456\n789\n", "-3,0,l", "" },
	{ "123\n456\n789\n", "-3,-3,l", "" },
	/* Line mode, negative length */
	{ "123\n456\n789\n", "2,-2,l", "" },
	{ "123\n456\n789\n", "-2,-3,l", "" },
	/* No length */
	{ "123\n456\n789\n", "1,,l", "456\n789\n" },
	{ "123\n456\n789\n", "-2,,l", "456\n789\n" },
};

static struct {
	const char *contents;
	const char *args;
	const char *value;
	const char *contents2;
} write_tests[] = {
	/* Single character replace */
	{ "123456789", "0,1", "a", "a23456789" },
	{ "123456789", "-9,1", "a", "a23456789" },
	{ "123456789", "0,-8", "a", "a23456789" },
	{ "123456789", "-9,-8", "a", "a23456789" },
	{ "123456789", "5,1", "b", "12345b789" },
	{ "123456789", "-4,1", "b", "12345b789" },
	{ "123456789", "5,-3", "b", "12345b789" },
	{ "123456789", "-4,-3", "b", "12345b789" },
	/* Replace 2 characters with 1 */
	{ "123456789", "0,2", "c", "c3456789" },
	{ "123456789", "-9,2", "c", "c3456789" },
	{ "123456789", "0,-7", "c", "c3456789" },
	{ "123456789", "-9,-7", "c", "c3456789" },
	{ "123456789", "4,2", "d", "1234d789" },
	{ "123456789", "-5,2", "d", "1234d789" },
	{ "123456789", "4,-3", "d", "1234d789" },
	{ "123456789", "-5,-3", "d", "1234d789" },
	/* Truncate file */
	{ "123456789", "5", "e", "12345e" },
	{ "123456789", "5", "", "12345" },
	{ "123456789", "-4", "e", "12345e" },
	{ "123456789", "-4", "", "12345" },
	/* Replace 1 character with 2 */
	{ "123456789", "0,1", "fg", "fg23456789" },
	{ "123456789", "0,-8", "fg", "fg23456789" },
	{ "123456789", "-9,1", "fg", "fg23456789" },
	{ "123456789", "-9,-8", "fg", "fg23456789" },
	/* Overwrite file */
	{ "123456789", "", "h", "h" },
	{ "123456789", ",,,", "h", "h" },
	{ "123\n456\n789\n", ",,l", "h", "h\n" },
	{ "123\n456\n789\n", ",,ld", "h", "h" },
	/* Single line replace, same length */
	{ "123\n456\n789\n", "0,1,l", "abc", "abc\n456\n789\n" },
	{ "123\n456\n789\n", "-3,1,l", "abc", "abc\n456\n789\n" },
	{ "123\n456\n789\n", "0,-2,l", "abc", "abc\n456\n789\n" },
	{ "123\n456\n789\n", "-3,-2,l", "abc", "abc\n456\n789\n" },
	{ "123\n456\n789\n", "1,1,l", "abc", "123\nabc\n789\n" },
	{ "123\n456\n789\n", "1,-1,l", "abc", "123\nabc\n789\n" },
	{ "123\n456\n789\n", "-2,1,l", "abc", "123\nabc\n789\n" },
	{ "123\n456\n789\n", "-2,-1,l", "abc", "123\nabc\n789\n" },
	/* Single line replace, one character short */
	{ "123\n456\n789\n", "0,1,l", "ab", "ab\n456\n789\n" },
	{ "123\n456\n789\n", "-3,1,l", "ab", "ab\n456\n789\n" },
	{ "123\n456\n789\n", "0,-2,l", "ab", "ab\n456\n789\n" },
	{ "123\n456\n789\n", "-3,-2,l", "ab", "ab\n456\n789\n" },
	{ "123\n456\n789\n", "1,1,l", "ab", "123\nab\n789\n" },
	{ "123\n456\n789\n", "1,-1,l", "ab", "123\nab\n789\n" },
	{ "123\n456\n789\n", "-2,1,l", "ab", "123\nab\n789\n" },
	{ "123\n456\n789\n", "-2,-1,l", "ab", "123\nab\n789\n" },
	/* Single line replace, one character long */
	{ "123\n456\n789\n", "0,1,l", "abcd", "abcd\n456\n789\n" },
	{ "123\n456\n789\n", "-3,1,l", "abcd", "abcd\n456\n789\n" },
	{ "123\n456\n789\n", "0,-2,l", "abcd", "abcd\n456\n789\n" },
	{ "123\n456\n789\n", "-3,-2,l", "abcd", "abcd\n456\n789\n" },
	{ "123\n456\n789\n", "1,1,l", "abcd", "123\nabcd\n789\n" },
	{ "123\n456\n789\n", "1,-1,l", "abcd", "123\nabcd\n789\n" },
	{ "123\n456\n789\n", "-2,1,l", "abcd", "123\nabcd\n789\n" },
	{ "123\n456\n789\n", "-2,-1,l", "abcd", "123\nabcd\n789\n" },
	/* Multi-line replace, same number of characters, 2 lines for 1 */
	{ "123\n456\n789\n", "0,2,l", "abcdefg", "abcdefg\n789\n" },
	{ "123\n456\n789\n", "-3,2,l", "abcdefg", "abcdefg\n789\n" },
	{ "123\n456\n789\n", "0,-1,l", "abcdefg", "abcdefg\n789\n" },
	{ "123\n456\n789\n", "-3,-1,l", "abcdefg", "abcdefg\n789\n" },
	{ "123\n456\n789\n", "1,2,l", "abcdefg", "123\nabcdefg\n" },
	{ "123\n456\n789\n", "1,,l", "abcdefg", "123\nabcdefg\n" },
	{ "123\n456\n789\n", "-2,2,l", "abcdefg", "123\nabcdefg\n" },
	{ "123\n456\n789\n", "-2,,l", "abcdefg", "123\nabcdefg\n" },
	/* Multi-line replace, shorter number of characters, 2 lines for 1 */
	{ "123\n456\n789\n", "0,2,l", "abcd", "abcd\n789\n" },
	{ "123\n456\n789\n", "-3,2,l", "abcd", "abcd\n789\n" },
	{ "123\n456\n789\n", "0,-1,l", "abcd", "abcd\n789\n" },
	{ "123\n456\n789\n", "-3,-1,l", "abcd", "abcd\n789\n" },
	{ "123\n456\n789\n", "1,2,l", "abcd", "123\nabcd\n" },
	{ "123\n456\n789\n", "1,,l", "abcd", "123\nabcd\n" },
	{ "123\n456\n789\n", "-2,2,l", "abcd", "123\nabcd\n" },
	{ "123\n456\n789\n", "-2,,l", "abcd", "123\nabcd\n" },
	/* Multi-line replace, longer number of characters, 2 lines for 1 */
	{ "123\n456\n789\n", "0,2,l", "abcdefghijklmnop", "abcdefghijklmnop\n789\n" },
	{ "123\n456\n789\n", "-3,2,l", "abcdefghijklmnop", "abcdefghijklmnop\n789\n" },
	{ "123\n456\n789\n", "0,-1,l", "abcdefghijklmnop", "abcdefghijklmnop\n789\n" },
	{ "123\n456\n789\n", "-3,-1,l", "abcdefghijklmnop", "abcdefghijklmnop\n789\n" },
	{ "123\n456\n789\n", "1,2,l", "abcdefghijklmnop", "123\nabcdefghijklmnop\n" },
	{ "123\n456\n789\n", "1,,l", "abcdefghijklmnop", "123\nabcdefghijklmnop\n" },
	{ "123\n456\n789\n", "-2,2,l", "abcdefghijklmnop", "123\nabcdefghijklmnop\n" },
	{ "123\n456\n789\n", "-2,,l", "abcdefghijklmnop", "123\nabcdefghijklmnop\n" },
	/* Insert line */
	{ "123\n456\n789\n", "0,0,l", "abcd", "abcd\n123\n456\n789\n" },
	{ "123\n456\n789\n", "-3,0,l", "abcd", "abcd\n123\n456\n789\n" },
	{ "123\n456\n789\n", "1,0,l", "abcd", "123\nabcd\n456\n789\n" },
	{ "123\n456\n789\n", "-2,0,l", "abcd", "123\nabcd\n456\n789\n" },
	{ "123\n456\n789\n", "2,0,l", "abcd", "123\n456\nabcd\n789\n" },
	{ "123\n456\n789\n", "-1,0,l", "abcd", "123\n456\nabcd\n789\n" },
	{ "123\n456\n789\n", "3,0,l", "abcd", "123\n456\n789\nabcd\n" },
	{ "123\n456\n789\n", ",,la", "abcd", "123\n456\n789\nabcd\n" },
	/* Single line, replace with blank line */
	{ "123\n456\n789\n", "0,1,l", "", "\n456\n789\n" },
	{ "123\n456\n789\n", "-3,1,l", "", "\n456\n789\n" },
	{ "123\n456\n789\n", "0,-2,l", "", "\n456\n789\n" },
	{ "123\n456\n789\n", "-3,-2,l", "", "\n456\n789\n" },
	{ "123\n456\n789\n", "1,1,l", "", "123\n\n789\n" },
	{ "123\n456\n789\n", "1,-1,l", "", "123\n\n789\n" },
	{ "123\n456\n789\n", "-2,1,l", "", "123\n\n789\n" },
	{ "123\n456\n789\n", "-2,-1,l", "", "123\n\n789\n" },
	/* Single line, delete */
	{ "123\n456\n789\n", "0,1,ld", "", "456\n789\n" },
	{ "123\n456\n789\n", "-3,1,ld", "", "456\n789\n" },
	{ "123\n456\n789\n", "0,-2,ld", "", "456\n789\n" },
	{ "123\n456\n789\n", "-3,-2,ld", "", "456\n789\n" },
	{ "123\n456\n789\n", "1,1,ld", "", "123\n789\n" },
	{ "123\n456\n789\n", "1,-1,ld", "", "123\n789\n" },
	{ "123\n456\n789\n", "-2,1,ld", "", "123\n789\n" },
	{ "123\n456\n789\n", "-2,-1,ld", "", "123\n789\n" },
	/* Really long tests */
	{ "1234567890ABCDEF" C1024 C1024 C1024 C1024 C1024,
		"0,1", "a",
		"a234567890ABCDEF" C1024 C1024 C1024 C1024 C1024 },
	{ "1234567890ABCDEF" C1024 C1024 C1024 C1024 C1024,
		"0,1", "abcd",
		"abcd234567890ABCDEF" C1024 C1024 C1024 C1024 C1024 },
	{ "1234567890ABCDEF" C1024 C1024 C1024 C1024 C1024,
		"0,10", "abcd",
		"abcdABCDEF" C1024 C1024 C1024 C1024 C1024 },
	{ "1" C1024 "\n2" C1024 "\n3" C1024 "\n4" C1024 "\n5" C1024 "\n6" C1024 "\n",
		"0,1,l", "abcd",
		"abcd\n2" C1024 "\n3" C1024 "\n4" C1024 "\n5" C1024 "\n6" C1024 "\n" },
	{ "1234\n1" C1024 "\n2" C1024 "\n3" C1024 "\n4" C1024 "\n5" C1024 "\n6" C1024 "\n",
		"0,1,l", "abcd",
		"abcd\n1" C1024 "\n2" C1024 "\n3" C1024 "\n4" C1024 "\n5" C1024 "\n6" C1024 "\n" },
	{ "1234\n1" C1024 "\n2" C1024 "\n3" C1024 "\n4" C1024 "\n5" C1024 "\n6" C1024 "\n",
		"0,1,l", "a",
		"a\n1" C1024 "\n2" C1024 "\n3" C1024 "\n4" C1024 "\n5" C1024 "\n6" C1024 "\n" },
};

static char *file2display(struct ast_str **buf, ssize_t len, const char *input)
{
	const char *ptr;
	ast_str_reset(*buf);
	for (ptr = input; *ptr; ptr++) {
		if (*ptr == '\n') {
			ast_str_append(buf, len, "\\n");
		} else if (*ptr == '\r') {
			ast_str_append(buf, len, "\\r");
		} else if (*ptr == '\t') {
			ast_str_append(buf, len, "\\t");
		} else if (*ptr < ' ' || *ptr > 125) {
			ast_str_append(buf, len, "\\x%hhX", *ptr);
		} else {
			ast_str_append(buf, len, "%c", *ptr);
		}
	}
	return ast_str_buffer(*buf);
}

AST_TEST_DEFINE(test_func_file)
{
	int res = AST_TEST_PASS;
	int i;
	char dir[] = "/tmp/test_func_file.XXXXXX";
	char file[80], expression[256];
	struct ast_str *buf, *disp[2] = { NULL, NULL };
	char fbuf[8192];
	FILE *fh;

	switch (cmd) {
	case TEST_INIT:
		info->name = "func_file";
		info->category = "/funcs/func_env/";
		info->summary = "Verify behavior of the FILE() dialplan function";
		info->description =
			"Verifies that the examples of the FILE() dialplan function documentation work as described.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!mkdtemp(dir)) {
		ast_test_status_update(test, "Cannot create temporary directory: %s\n", strerror(errno));
		return AST_TEST_FAIL;
	}

	disp[0] = ast_str_create(16);
	disp[1] = ast_str_create(16);
	if (!(buf = ast_str_create(16)) || !disp[0] || !disp[1]) {
		ast_free(buf);
		ast_free(disp[0]);
		ast_free(disp[1]);
		rmdir(dir);
		return AST_TEST_FAIL;
	}

	snprintf(file, sizeof(file), "%s/test.txt", dir);

	for (i = 0; i < ARRAY_LEN(read_tests); i++) {
		if (!(fh = fopen(file, "w"))) {
			ast_test_status_update(test, "Cannot open test file: %s\n", strerror(errno));
			ast_free(buf);
			ast_free(disp[0]);
			ast_free(disp[1]);
			unlink(file);
			rmdir(dir);
			return AST_TEST_FAIL;
		}

		if (fwrite(read_tests[i].contents, 1, strlen(read_tests[i].contents), fh) < strlen(read_tests[i].contents)) {
			ast_test_status_update(test, "Cannot write initial values into test file: %s\n", strerror(errno));
			ast_free(buf);
			ast_free(disp[0]);
			ast_free(disp[1]);
			fclose(fh);
			unlink(file);
			rmdir(dir);
			return AST_TEST_FAIL;
		}

		fclose(fh);

		snprintf(expression, sizeof(expression), "${FILE(%s,%s)}", file, read_tests[i].args);
		ast_str_substitute_variables(&buf, 0, NULL, expression);

		if (strcmp(ast_str_buffer(buf), read_tests[i].value)) {
			ast_test_status_update(test, "Expression '${FILE(...,%s)}' did not produce ('%s') the expected value ('%s')\n",
				read_tests[i].args, file2display(&disp[0], 0, ast_str_buffer(buf)), file2display(&disp[1], 0, read_tests[i].value));
			res = AST_TEST_FAIL;
		}
	}

	ast_free(buf);

	for (i = 0; i < ARRAY_LEN(write_tests); i++) {
		if (!(fh = fopen(file, "w"))) {
			ast_test_status_update(test, "Cannot open test file: %s\n", strerror(errno));
			ast_free(disp[0]);
			ast_free(disp[1]);
			unlink(file);
			rmdir(dir);
			return AST_TEST_FAIL;
		}

		if (fwrite(write_tests[i].contents, 1, strlen(write_tests[i].contents), fh) < strlen(write_tests[i].contents)) {
			ast_test_status_update(test, "Cannot write initial values into test file: %s\n", strerror(errno));
			ast_free(disp[0]);
			ast_free(disp[1]);
			fclose(fh);
			unlink(file);
			rmdir(dir);
			return AST_TEST_FAIL;
		}

		fclose(fh);

		snprintf(expression, sizeof(expression), "FILE(%s,%s)", file, write_tests[i].args);
		ast_func_write(NULL, expression, write_tests[i].value);

		if (!(fh = fopen(file, "r"))) {
			ast_test_status_update(test, "Cannot open test file: %s\n", strerror(errno));
			ast_free(disp[0]);
			ast_free(disp[1]);
			unlink(file);
			rmdir(dir);
			return AST_TEST_FAIL;
		}

		memset(fbuf, 0, sizeof(fbuf));
		if (!fread(fbuf, 1, sizeof(fbuf), fh)) {
			ast_test_status_update(test, "Cannot read write results from test file: %s\n", strerror(errno));
			ast_free(disp[0]);
			ast_free(disp[1]);
			fclose(fh);
			unlink(file);
			rmdir(dir);
			return AST_TEST_FAIL;
		}

		fclose(fh);

		if (strcmp(fbuf, write_tests[i].contents2)) {
			ast_test_status_update(test, "Expression 'FILE(...,%s)=%s' did not produce ('%s') the expected result ('%s')\n",
				write_tests[i].args, write_tests[i].value, file2display(&disp[0], 0, fbuf), file2display(&disp[1], 0, write_tests[i].contents2));
			res = AST_TEST_FAIL;
		} else {
			ast_test_status_update(test, "Expression 'FILE(...,%s)=%s'... OK!\n", write_tests[i].args, write_tests[i].value);
		}
	}

	ast_free(disp[0]);
	ast_free(disp[1]);
	unlink(file);
	rmdir(dir);

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_func_file);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_func_file);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "FILE() Tests");
