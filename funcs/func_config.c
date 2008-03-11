/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
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
 * \brief A function to retrieve variables from an Asterisk configuration file
 *
 * \author Russell Bryant <russell@digium.com>
 * 
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"

static int config_function_read(struct ast_channel *chan, const char *cmd, char *data, 
	char *buf, size_t len) 
{
	struct ast_config *cfg;
	struct ast_flags cfg_flags = { 0 };
	const char *val;
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(category);
		AST_APP_ARG(variable);
		AST_APP_ARG(index);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "AST_CONFIG() requires an argument\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.filename)) {
		ast_log(LOG_ERROR, "AST_CONFIG() requires a filename\n");
		return -1;
	}

	if (ast_strlen_zero(args.category)) {
		ast_log(LOG_ERROR, "AST_CONFIG() requires a category\n");
		return -1;
	}
	
	if (ast_strlen_zero(args.variable)) {
		ast_log(LOG_ERROR, "AST_CONFIG() requires a variable\n");
		return -1;
	}

	if (!(cfg = ast_config_load(args.filename, cfg_flags))) {
		return -1;
	}

	if (!(val = ast_variable_retrieve(cfg, args.category, args.variable))) {
		ast_log(LOG_ERROR, "'%s' not found in [%s] of '%s'\n", args.variable, 
			args.category, args.filename);
		return -1;
	}

	ast_copy_string(buf, val, len);

	ast_config_destroy(cfg);

	return 0;
}

static struct ast_custom_function config_function = {
	.name = "AST_CONFIG",
	.syntax = "AST_CONFIG(config_file,category,variable_name[,index])",
	.synopsis = "Retrieve a variable from a configuration file",
	.desc = 
	"   This function reads a variable from an Asterisk configuration file.\n"
	"The optional index parameter would be used in the case that a variable\n"
	"exists more than once in a category.  The index is zero-based, so an\n"
	"index of 0 returns the first instance of the variable.  Also, if the\n"
	"word \"count\" in the index field, the number of instances of that\n"
	"variable will be returned.\n"
	"",
	.read = config_function_read,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&config_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&config_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Asterisk configuration file variable access");
