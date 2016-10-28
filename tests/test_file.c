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

#include "asterisk/file.h"
#include "asterisk/paths.h"
#include "asterisk/test.h"
#include "asterisk/module.h"

#define FOUND -7

static int handle_find_file(const char *filepath, void *obj)
{
	const char *filename = ast_file_basename(filepath);

	/* obj contains the name of the file we are looking for */
	return strcmp(obj, filename) ? 0 : FOUND;
}

AST_TEST_DEFINE(read_dirs_test)
{
	char *mod_dir, *p;
	int size;

	switch (cmd) {
	case TEST_INIT:
		info->name = "read_dir_test";
		info->category = "/main/file/";
		info->summary = "Read a directory's content";
		info->description = "Iterate over the modules directory looking for this test module.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/*
	 * Start reading from one level up from the modules directory
	 * so a recursive search can also be tested.
	 */
	mod_dir = ast_strdupa(ast_config_AST_MODULE_DIR);

	size = strlen(mod_dir);
	if (mod_dir[size - 1] == '/') {
		mod_dir[size - 1] = '\0';
	}

	if (!(p = strrchr(mod_dir, '/'))) {
		return AST_TEST_FAIL;
	}
	*p++ = '\0';

	return ast_file_read_dirs(mod_dir, handle_find_file, "test_file.so",
				  2) == FOUND ? AST_TEST_PASS : AST_TEST_FAIL;
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
