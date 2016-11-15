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
#include <sys/stat.h>
#include <stdio.h>

#include "asterisk/file.h"
#include "asterisk/paths.h"
#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/strings.h"
#include "asterisk/vector.h"

#define FOUND -7

AST_VECTOR(_filenames, struct ast_str *);

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

	if (filenames) {
		AST_VECTOR_CALLBACK_VOID(filenames, rm_file);
		AST_VECTOR_FREE(filenames);
	}

	if ((res = rmdir(dir_name)) < 0) {
		ast_test_status_update(test, "Failed to remove directory: %s\n", dir_name);
	}

	return res;
}

static int test_files_create(struct ast_test *test, char *dir_name,
			     struct _filenames *filenames, int num)
{
	int i;

	if (!(mkdtemp(dir_name))) {
		ast_test_status_update(test, "Failed to create directory: %s\n", dir_name);
		return -1;
	}


	AST_VECTOR_INIT(filenames, num);

	/*
	 * Create "num" files under the specified directory
	 */
	for (i = 0; i < num; ++i) {
		int fd;
		struct ast_str *filename = ast_str_create(32);

		if (!filename) {
			break;
		}

		ast_str_set(&filename, 0, "%s/XXXXXX", dir_name);

		fd = mkstemp(ast_str_buffer(filename));
		if (fd < 0) {
			ast_test_status_update(test, "Failed to create file: %s\n",
					       ast_str_buffer(filename));
			ast_free(filename);
			break;
		}
		close(fd);

		AST_VECTOR_APPEND(filenames, filename);
	}

	if (i != num) {
		test_files_destroy(test, dir_name, filenames);
		return -1;
	}

	return 0;
}

static char *test_files_get_one(struct _filenames *filenames, int num)
{
	/* Every file is in a directory and contains a '/' so okay to do this */
	return strrchr(ast_str_buffer(
		       AST_VECTOR_GET(filenames, ast_random() % (num - 1))), '/') + 1;
}

static int handle_find_file(const char *dir_name, const char *filename, void *obj)
{
	struct stat statbuf;
	char *full_path = ast_alloca(strlen(dir_name) + strlen(filename) + 2);

	sprintf(full_path, "%s/%s", dir_name, filename);

	errno = 0;
	if (stat(full_path, &statbuf)) {
		ast_log(LOG_ERROR, "Error reading path stats - %s: %s\n",
			full_path, strerror(errno));
		return 0;
	}
	/* obj contains the name of the file we are looking for */
	return strcmp(obj, filename) ? 0 : FOUND;
}

AST_TEST_DEFINE(read_dirs_test)
{
	char tmp_dir[] = "/tmp/tmpdir.XXXXXX";
	struct ast_str *tmp_sub_dir;
	struct _filenames filenames;
	enum ast_test_result_state res;
	const int num_files = 10 + (ast_random() % 10); /* 10-19 random files */

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

	/*
	 * We want to test recursively searching into a subdirectory, so
	 * create a top level tmp directory where we will start the search.
	 */
	if (!(mkdtemp(tmp_dir))) {
		ast_test_status_update(test, "Failed to create directory: %s\n", tmp_dir);
		return AST_TEST_FAIL;
	}

	tmp_sub_dir = ast_str_alloca(32);
	ast_str_set(&tmp_sub_dir, 0, "%s/XXXXXX", tmp_dir);

	if (test_files_create(test, ast_str_buffer(tmp_sub_dir), &filenames, num_files)) {
		test_files_destroy(test, tmp_dir, NULL);
		return AST_TEST_FAIL;
	}

	res = ast_file_read_dirs(tmp_dir, handle_find_file, test_files_get_one(
		 &filenames, num_files), 2) == FOUND ? AST_TEST_PASS : AST_TEST_FAIL;

	if (test_files_destroy(test, ast_str_buffer(tmp_sub_dir), &filenames) ||
	    test_files_destroy(test, tmp_dir, NULL)) {
		res = AST_TEST_FAIL;
	}

	return res;
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
