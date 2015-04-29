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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/channel.h"
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

/*! 
 * \brief Convert a file from one format to another 
 * \param e CLI entry
 * \param cmd command number
 * \param a list of cli arguments
 * \retval CLI_SUCCESS on success.
 * \retval CLI_SHOWUSAGE or CLI_FAILURE on failure.
*/
static char *handle_cli_file_convert(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *ret = CLI_FAILURE;
	struct ast_filestream *fs_in = NULL, *fs_out = NULL;
	struct ast_frame *f;
	struct timeval start;
	int cost;
	char *file_in = NULL, *file_out = NULL;
	char *name_in, *ext_in, *name_out, *ext_out;

	switch (cmd) {
	case CLI_INIT:
		e->command = "file convert";
		e->usage =
			"Usage: file convert <file_in> <file_out>\n"
			"       Convert from file_in to file_out. If an absolute path\n"
			"       is not given, the default Asterisk sounds directory\n"
			"       will be used.\n\n"
			"       Example:\n"
			"           file convert tt-weasels.gsm tt-weasels.ulaw\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}
	
	if (a->argc != 4 || ast_strlen_zero(a->argv[2]) || ast_strlen_zero(a->argv[3])) {
		ret = CLI_SHOWUSAGE;
		goto fail_out;	
	}

	file_in = ast_strdupa(a->argv[2]);
	file_out = ast_strdupa(a->argv[3]);

	if (split_ext(file_in, &name_in, &ext_in)) {
		ast_cli(a->fd, "'%s' is an invalid filename!\n", a->argv[2]);
		goto fail_out;
	}
	if (!(fs_in = ast_readfile(name_in, ext_in, NULL, O_RDONLY, 0, 0))) {
		ast_cli(a->fd, "Unable to open input file: %s\n", a->argv[2]);
		goto fail_out;
	}
	
	if (split_ext(file_out, &name_out, &ext_out)) {
		ast_cli(a->fd, "'%s' is an invalid filename!\n", a->argv[3]);
		goto fail_out;
	}
	if (!(fs_out = ast_writefile(name_out, ext_out, NULL, O_CREAT|O_TRUNC|O_WRONLY, 0, AST_FILE_MODE))) {
		ast_cli(a->fd, "Unable to open output file: %s\n", a->argv[3]);
		goto fail_out;
	}

	start = ast_tvnow();
	
	while ((f = ast_readframe(fs_in))) {
		if (ast_writestream(fs_out, f)) {
			ast_frfree(f);
			ast_cli(a->fd, "Failed to convert %s.%s to %s.%s!\n", name_in, ext_in, name_out, ext_out);
			goto fail_out;
		}
		ast_frfree(f);
	}

	cost = ast_tvdiff_ms(ast_tvnow(), start);
	ast_cli(a->fd, "Converted %s.%s to %s.%s in %dms\n", name_in, ext_in, name_out, ext_out, cost);
	ret = CLI_SUCCESS;

fail_out:
	if (fs_out) {
		ast_closestream(fs_out);
		if (ret != CLI_SUCCESS)
			ast_filedelete(name_out, ext_out);
	}

	if (fs_in) 
		ast_closestream(fs_in);

	return ret;
}

static struct ast_cli_entry cli_convert[] = {
	AST_CLI_DEFINE(handle_cli_file_convert, "Convert audio file")
};

static int load_module(void)
{
	ast_cli_register_multiple(cli_convert, ARRAY_LEN(cli_convert));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "File format conversion CLI command");
