/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Digium, Inc.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "asterisk.h"
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/file.h"

STANDARD_USECOUNT_DECL;

static char *desc = "File format conversion CLI command";

/*! \brief Split the filename to basename and extension */
static int split_ext(char *filename, char **name, char **ext)
{
	*name = *ext = filename;
	
	strsep(ext, ".");

	if (ast_strlen_zero(*name) || ast_strlen_zero(*ext))
		return -1;

	return 0;
}

/*! \brief Convert a file from one format to another */
static int cli_audio_convert(int fd, int argc, char *argv[])
{
	int ret = RESULT_FAILURE;
	struct ast_filestream *fs_in = NULL, *fs_out = NULL;
	struct ast_frame *f;
	struct timeval start;
	int cost;
	char *file_in = NULL, *file_out = NULL;
	char *name_in, *ext_in, *name_out, *ext_out;
	
	STANDARD_INCREMENT_USECOUNT;
	
	if (argc != 3 || ast_strlen_zero(argv[1]) || ast_strlen_zero(argv[2])) {
		ret = RESULT_SHOWUSAGE;
		goto fail_out;	
	}

	if (!(file_in = ast_strdupa(argv[1])) || !(file_out = ast_strdupa(argv[2])))
		goto fail_out;

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
			ast_cli(fd, "Failed to convert %s.%s to %s.%s!\n", name_in, ext_in, name_out, ext_out);
			goto fail_out;
		}
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

	STANDARD_DECREMENT_USECOUNT;
	
	return ret;
}

static char usage_audio_convert[] =
"Usage: convert <file_in> <file_out>\n"
"    Convert from file_in to file_out. If an absolute path is not given, the\n"
"default Asterisk sounds directory will be used.\n\n"
"Example:\n"
"    convert tt-weasels.gsm tt-weasels.ulaw\n";

static struct ast_cli_entry audio_convert_cli={
	{ "convert" , NULL }, cli_audio_convert, "Convert audio files", usage_audio_convert
};

int unload_module(void)
{
	return ast_cli_unregister(&audio_convert_cli);
}

int load_module(void)
{
	return ast_cli_register(&audio_convert_cli);
}

const char *description(void)
{
	return desc;
}

int usecount(void)
{
	int res;
	
	STANDARD_USECOUNT(res);

	return res;
}

const char *key()
{
	return ASTERISK_GPL_KEY;
}

