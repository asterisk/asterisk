/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
 * Tilghman Lesher <func_config__200803@the-tilghman.com>
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
 * \author Tilghman Lesher <func_config__200803@the-tilghman.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="AST_CONFIG" language="en_US">
		<synopsis>
			Retrieve a variable from a configuration file.
		</synopsis>
		<syntax>
			<parameter name="config_file" required="true" />
			<parameter name="category" required="true" />
			<parameter name="variable_name" required="true" />
			<parameter name="index" required="false">
				<para>If there are multiple variables with the same name, you can specify
				<literal>0</literal> for the first item (default), <literal>-1</literal> for the last
				item, or any other number for that specific item.  <literal>-1</literal> is useful
				when the variable is derived from a template and you want the effective value (the last
				occurrence), not the value from the template (the first occurrence).</para>
			</parameter>
		</syntax>
		<description>
			<para>This function reads a variable from an Asterisk configuration file.</para>
		</description>
	</function>

***/

struct config_item {
	AST_RWLIST_ENTRY(config_item) entry;
	struct ast_config *cfg;
	char filename[0];
};

static AST_RWLIST_HEAD_STATIC(configs, config_item);

static int config_function_read(struct ast_channel *chan, const char *cmd, char *data,
	char *buf, size_t len)
{
	struct ast_config *cfg;
	struct ast_flags cfg_flags = { CONFIG_FLAG_FILEUNCHANGED };
	char *parse;
	struct config_item *cur;
	int index = 0;
	struct ast_variable *var;
	struct ast_variable *found = NULL;
	int ix = 0;
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

	if (!ast_strlen_zero(args.index)) {
		if (!sscanf(args.index, "%d", &index)) {
			ast_log(LOG_ERROR, "AST_CONFIG() index must be an integer\n");
			return -1;
		}
	}

	if (!(cfg = ast_config_load(args.filename, cfg_flags)) || cfg == CONFIG_STATUS_FILEINVALID) {
		return -1;
	}

	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		/* Retrieve cfg from list */
		AST_RWLIST_RDLOCK(&configs);
		AST_RWLIST_TRAVERSE(&configs, cur, entry) {
			if (!strcmp(cur->filename, args.filename)) {
				break;
			}
		}

		if (!cur) {
			/* At worst, we might leak an entry while upgrading locks */
			AST_RWLIST_UNLOCK(&configs);
			AST_RWLIST_WRLOCK(&configs);
			if (!(cur = ast_calloc(1, sizeof(*cur) + strlen(args.filename) + 1))) {
				AST_RWLIST_UNLOCK(&configs);
				return -1;
			}

			strcpy(cur->filename, args.filename);

			ast_clear_flag(&cfg_flags, CONFIG_FLAG_FILEUNCHANGED);
			if (!(cfg = ast_config_load(args.filename, cfg_flags)) || cfg == CONFIG_STATUS_FILEINVALID) {
				ast_free(cur);
				AST_RWLIST_UNLOCK(&configs);
				return -1;
			}

			cur->cfg = cfg;
			AST_RWLIST_INSERT_TAIL(&configs, cur, entry);
		}

		cfg = cur->cfg;
	} else {
		/* Replace cfg in list */
		AST_RWLIST_WRLOCK(&configs);
		AST_RWLIST_TRAVERSE(&configs, cur, entry) {
			if (!strcmp(cur->filename, args.filename)) {
				break;
			}
		}

		if (!cur) {
			if (!(cur = ast_calloc(1, sizeof(*cur) + strlen(args.filename) + 1))) {
				AST_RWLIST_UNLOCK(&configs);
				return -1;
			}

			strcpy(cur->filename, args.filename);
			cur->cfg = cfg;

			AST_RWLIST_INSERT_TAIL(&configs, cur, entry);
		} else {
			ast_config_destroy(cur->cfg);
			cur->cfg = cfg;
		}
	}

	for (var = ast_category_root(cfg, args.category); var; var = var->next) {
		if (strcasecmp(args.variable, var->name)) {
			continue;
		}
		found = var;
		if (index == -1) {
			continue;
		}
		if (ix == index) {
			break;
		}
		found = NULL;
		ix++;
	}

	if (!found) {
		ast_debug(1, "'%s' not found at index %d in [%s] of '%s'.  Maximum index found: %d\n",
			args.variable, index, args.category, args.filename, ix);
		AST_RWLIST_UNLOCK(&configs);
		return -1;
	}

	ast_copy_string(buf, found->value, len);

	/* Unlock down here, so there's no chance the struct goes away while we're using it. */
	AST_RWLIST_UNLOCK(&configs);

	return 0;
}

static struct ast_custom_function config_function = {
	.name = "AST_CONFIG",
	.read = config_function_read,
};

static int unload_module(void)
{
	struct config_item *current;
	int res = ast_custom_function_unregister(&config_function);

	AST_RWLIST_WRLOCK(&configs);
	while ((current = AST_RWLIST_REMOVE_HEAD(&configs, entry))) {
		ast_config_destroy(current->cfg);
		ast_free(current);
	}
	AST_RWLIST_UNLOCK(&configs);

	return res;
}

static int load_module(void)
{
	return ast_custom_function_register(&config_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Asterisk configuration file variable access");
