/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/file.h"
#include "asterisk/paths.h"
#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/strings.h"
#include "asterisk/vector.h"

#define FOUND -7

AST_VECTOR(_filenames, struct ast_str *);

static int test_files_create(struct ast_test *test, char *dir_name,
			     struct _filenames *filenames, int num)
{
	int i;

	AST_VECTOR_INIT(filenames, num);

	if (!(mkdtemp(dir_name))) {
		ast_test_status_update(test, "Failed to create directory: %s\n", dir_name);
		return -1;
	}

	/*
	 * Create "num" files under the specified directory
	 */
	for (i = 0; i < num; ++i) {
		int fd;
		struct ast_str *filename = ast_str_create(32);

		if (!filename) {
			return -1;
		}

		ast_str_set(&filename, 0, "%s/XXXXXX", dir_name);

		fd = mkstemp(ast_str_buffer(filename));
		if (fd < 0) {
			ast_test_status_update(test, "Failed to create file: %s\n",
					       ast_str_buffer(filename));
			ast_free(filename);
			return -1;
		}
		close(fd);

		AST_VECTOR_APPEND(filenames, filename);
	}

	return 0;
}

static void rm_file(struct ast_str *filename)
{
	if (unlink(ast_str_buffer(filename))) {
		ast_log(LOG_ERROR, "Unable to remove file: %s\n", ast_str_buffer(filename));
	}

	ast_free(filename);
}

static int test_files_destroy(struct ast_test *test, char *dir_name,
			      struct _filenames *filenames)
{
	int res;

	AST_VECTOR_CALLBACK_VOID(filenames, rm_file);

	if ((res = rmdir(dir_name)) < 0) {
		ast_test_status_update(test, "Failed to remove directory: %s\n", dir_name);
	}

	AST_VECTOR_FREE(filenames);

	return res;
}

static char *test_files_get_one(struct _filenames *filenames, int num)
{
	/* Every file is in a directory and contains a '/' so okay to do this */
	return strrchr(ast_str_buffer(
		       AST_VECTOR_GET(filenames, ast_random() % (num - 1))), '/') + 1;
}

static int handle_find_file(const char *dir_name, const char *filename, void *obj)
{
	/* obj contains the name of the file we are looking for */
	return strcmp(obj, filename) ? 0 : FOUND;
}

AST_TEST_DEFINE(read_dirs_test)
{
	char tmp_dir[] = "/tmp/tmpdir.XXXXXX";
	const int num_files = 10;
	struct _filenames filenames;

	switch (cmd) {
	case TEST_INIT:
		info->name = "read_dir_test";
		info->category = "/main/file/";
		info->summary = "Read a directory's content";
		info->description = "Iterate over directories looking for a file.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (test_files_create(test, tmp_dir, &filenames, num_files)) {
		test_files_destroy(test, tmp_dir, &filenames);
		return AST_TEST_FAIL;
	}

	if (ast_file_read_dirs("/tmp", handle_find_file,
		       test_files_get_one(&filenames, num_files), 2) != FOUND) {
		test_files_destroy(test, tmp_dir, &filenames);
		return AST_TEST_FAIL;
	}

	if (test_files_destroy(test, tmp_dir, &filenames)) {
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(read_dirs_test);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(read_dirs_test);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "File test module");
