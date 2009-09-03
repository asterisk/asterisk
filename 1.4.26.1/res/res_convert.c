/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, 2006, Digium, Inc.
 *
 * redice li <redice_li@yahoo.com>
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

/*! \file
 * 
 * \brief file format conversion CLI command using Asterisk formats and translators
 *
 * \author redice li <redice_li@yahoo.com>
 * \author Russell Bryant <russell@digium.com>
 *
 */ 

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/file.h"

/*! \brief Split the filename to basename and extension */
static int split_ext(char *filename, char **name, char **ext)
{
	*name = *ext = filename;
	
	if ((*ext = strrchr(filename, '.'))) {
		**ext = '\0';
		(*ext)++;
	}

	if (ast_strlen_zero(*name) || ast_strlen_zero(*ext))
		return -1;

	return 0;
}

/*! \brief Convert a file from one format to another */
static int cli_audio_convert_deprecated(int fd, int argc, char *argv[])
{
	int ret = RESULT_FAILURE;
	struct ast_filestream *fs_in = NULL, *fs_out = NULL;
	struct ast_frame *f;
	struct timeval start;
	int cost;
	char *file_in = NULL, *file_out = NULL;
	char *name_in, *ext_in, *name_out, *ext_out;
	
	/* ugly, can be removed when CLI entries have ast_module pointers */
	ast_module_ref(ast_module_info->self);

	if (argc != 3 || ast_strlen_zero(argv[1]) || ast_strlen_zero(argv[2])) {
		ret = RESULT_SHOWUSAGE;
		goto fail_out;	
	}

	file_in = ast_strdupa(argv[1]);
	file_out = ast_strdupa(argv[2]);

	if (split_ext(file_in, &name_in, &ext_in)) {
		ast_cli(fd, "'%s' is an invalid filename!\n", argv[1]);
		goto fail_out;
	}
	if (!(fs_in = ast_readfile(name_in, ext_in, NULL, O_RDONLY, 0, 0))) {
		ast_cli(fd, "Unable to open input file: %s\n", argv[1]);
		goto fail_out;
	}
	
	if (split_ext(file_out, &name_out, &ext_out)) {
		ast_cli(fd, "'%s' is an invalid filename!\n", argv[2]);
		goto fail_out;
	}
	if (!(fs_out = ast_writefile(name_out, ext_out, NULL, O_CREAT|O_TRUNC|O_WRONLY, 0, 0644))) {
		ast_cli(fd, "Unable to open output file: %s\n", argv[2]);
		goto fail_out;
	}

	start = ast_tvnow();
	
	while ((f = ast_readframe(fs_in))) {
		if (ast_writestream(fs_out, f)) {
			ast_frfree(f);
			ast_cli(fd, "Failed to convert %s.%s to %s.%s!\n", name_in, ext_in, name_out, ext_out);
			goto fail_out;
		}
		ast_frfree(f);
	}

	cost = ast_tvdiff_ms(ast_tvnow(), start);
	ast_cli(fd, "Converted %s.%s to %s.%s in %dms\n", name_in, ext_in, name_out, ext_out, cost);
	ret = RESULT_SUCCESS;

fail_out:
	if (fs_out) {
		ast_closestream(fs_out);
		if (ret != RESULT_SUCCESS)
			ast_filedelete(name_out, ext_out);
	}

	if (fs_in) 
		ast_closestream(fs_in);

	ast_module_unref(ast_module_info->self);

	return ret;
}

static int cli_audio_convert(int fd, int argc, char *argv[])
{
	int ret = RESULT_FAILURE;
	struct ast_filestream *fs_in = NULL, *fs_out = NULL;
	struct ast_frame *f;
	struct timeval start;
	int cost;
	char *file_in = NULL, *file_out = NULL;
	char *name_in, *ext_in, *name_out, *ext_out;
	
	/* ugly, can be removed when CLI entries have ast_module pointers */
	ast_module_ref(ast_module_info->self);

	if (argc != 4 || ast_strlen_zero(argv[2]) || ast_strlen_zero(argv[3])) {
		ret = RESULT_SHOWUSAGE;
		goto fail_out;	
	}

	file_in = ast_strdupa(argv[2]);
	file_out = ast_strdupa(argv[3]);

	if (split_ext(file_in, &name_in, &ext_in)) {
		ast_cli(fd, "'%s' is an invalid filename!\n", argv[2]);
		goto fail_out;
	}
	if (!(fs_in = ast_readfile(name_in, ext_in, NULL, O_RDONLY, 0, 0))) {
		ast_cli(fd, "Unable to open input file: %s\n", argv[2]);
		goto fail_out;
	}
	
	if (split_ext(file_out, &name_out, &ext_out)) {
		ast_cli(fd, "'%s' is an invalid filename!\n", argv[3]);
		goto fail_out;
	}
	if (!(fs_out = ast_writefile(name_out, ext_out, NULL, O_CREAT|O_TRUNC|O_WRONLY, 0, 0644))) {
		ast_cli(fd, "Unable to open output file: %s\n", argv[3]);
		goto fail_out;
	}

	start = ast_tvnow();
	
	while ((f = ast_readframe(fs_in))) {
		if (ast_writestream(fs_out, f)) {
			ast_frfree(f);
			ast_cli(fd, "Failed to convert %s.%s to %s.%s!\n", name_in, ext_in, name_out, ext_out);
			goto fail_out;
		}
		ast_frfree(f);
	}

	cost = ast_tvdiff_ms(ast_tvnow(), start);
	ast_cli(fd, "Converted %s.%s to %s.%s in %dms\n", name_in, ext_in, name_out, ext_out, cost);
	ret = RESULT_SUCCESS;

fail_out:
	if (fs_out) {
		ast_closestream(fs_out);
		if (ret != RESULT_SUCCESS)
			ast_filedelete(name_out, ext_out);
	}

	if (fs_in) 
		ast_closestream(fs_in);

	ast_module_unref(ast_module_info->self);

	return ret;
}

static char usage_audio_convert[] =
"Usage: file convert <file_in> <file_out>\n"
"    Convert from file_in to file_out. If an absolute path is not given, the\n"
"default Asterisk sounds directory will be used.\n\n"
"Example:\n"
"    file convert tt-weasels.gsm tt-weasels.ulaw\n";

static struct ast_cli_entry cli_convert_deprecated = {
	{ "convert" , NULL },
	cli_audio_convert_deprecated, NULL,
	NULL };

static struct ast_cli_entry cli_convert[] = {
	{ { "file", "convert" , NULL },
	cli_audio_convert, "Convert audio file",
	usage_audio_convert, NULL, &cli_convert_deprecated },
};

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_convert, sizeof(cli_convert) / sizeof(struct ast_cli_entry));
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_convert, sizeof(cli_convert) / sizeof(struct ast_cli_entry));
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "File format conversion CLI command");
