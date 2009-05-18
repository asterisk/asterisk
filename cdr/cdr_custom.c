/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2009, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Includes code and algorithms from the Zapata library.
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
 * \brief Custom Comma Separated Value CDR records.
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \arg See also \ref AstCDR
 *
 * Logs in LOG_DIR/cdr_custom
 * \ingroup cdr_drivers
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <time.h>

#include "asterisk/paths.h"	/* use ast_config_AST_LOG_DIR */
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/threadstorage.h"
#include "asterisk/strings.h"

#define CUSTOM_LOG_DIR "/cdr_custom"
#define CONFIG         "cdr_custom.conf"
#define DATE_FORMAT    "%Y-%m-%d %T"

AST_THREADSTORAGE(custom_buf);

static char *name = "cdr-custom";

struct cdr_config {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(filename);
		AST_STRING_FIELD(format);
		);
	ast_mutex_t lock;
	AST_RWLIST_ENTRY(cdr_config) list;
};

static AST_RWLIST_HEAD_STATIC(sinks, cdr_config);

static void free_config(void)
{
	struct cdr_config *sink;
	while ((sink = AST_RWLIST_REMOVE_HEAD(&sinks, list))) {
		ast_mutex_destroy(&sink->lock);
		ast_free(sink);
	}
}

static int load_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_flags config_flags = { 0 };
	int res = 0;

	cfg = ast_config_load(CONFIG, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load " CONFIG ". Not custom CSV CDRs.\n");
		return -1;
	}

	var = ast_variable_browse(cfg, "mappings");
	while (var) {
		if (!ast_strlen_zero(var->name) && !ast_strlen_zero(var->value)) {
			struct cdr_config *sink = ast_calloc_with_stringfields(1, struct cdr_config, 1024);

			if (!sink) {
				ast_log(LOG_ERROR, "Unable to allocate memory for configuration settings.\n");
				res = -2;
				break;
			}

			ast_string_field_build(sink, format, "%s\n", var->value);
			ast_string_field_build(sink, filename, "%s/%s/%s", ast_config_AST_LOG_DIR, name, var->name);
			ast_mutex_init(&sink->lock);

			AST_RWLIST_INSERT_TAIL(&sinks, sink, list);
		} else {
			ast_log(LOG_NOTICE, "Mapping must have both a filename and a format at line %d\n", var->lineno);
		}
		var = var->next;
	}
	ast_config_destroy(cfg);

	return res;
}

static int custom_log(struct ast_cdr *cdr)
{
	struct ast_channel dummy;
	struct ast_str *str;
	struct cdr_config *config;

	/* Batching saves memory management here.  Otherwise, it's the same as doing an allocation and free each time. */
	if (!(str = ast_str_thread_get(&custom_buf, 16))) {
		return -1;
	}

	/* Quite possibly the first use of a static struct ast_channel, we need it so the var funcs will work */
	memset(&dummy, 0, sizeof(dummy));
	dummy.cdr = cdr;

	AST_RWLIST_RDLOCK(&sinks);

	AST_LIST_TRAVERSE(&sinks, config, list) {
		FILE *out;

		ast_str_reset(str);
		ast_str_substitute_variables(&str, 0, &dummy, config->format);

		/* Even though we have a lock on the list, we could be being chased by
		   another thread and this lock ensures that we won't step on anyone's
		   toes.  Once each CDR backend gets it's own thread, this lock can be
		   removed. */
		ast_mutex_lock(&config->lock);

		/* Because of the absolutely unconditional need for the
		   highest reliability possible in writing billing records,
		   we open write and close the log file each time */
		if ((out = fopen(config->filename, "a"))) {
			fputs(ast_str_buffer(str), out);
			fflush(out); /* be particularly anal here */
			fclose(out);
		} else {
			ast_log(LOG_ERROR, "Unable to re-open master file %s : %s\n", config->filename, strerror(errno));
		}

		ast_mutex_unlock(&config->lock);
	}

	AST_RWLIST_UNLOCK(&sinks);

	return 0;
}

static int unload_module(void)
{
	ast_cdr_unregister(name);

	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_cdr_register(name, ast_module_info->description, custom_log);
		ast_log(LOG_ERROR, "Unable to lock sink list.  Unload failed.\n");
		return -1;
	}

	free_config();
	AST_RWLIST_UNLOCK(&sinks);
	return 0;
}

static int load_module(void)
{
	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(LOG_ERROR, "Unable to lock sink list.  Load failed.\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	load_config();
	AST_RWLIST_UNLOCK(&sinks);
	ast_cdr_register(name, ast_module_info->description, custom_log);
	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(LOG_ERROR, "Unable to lock sink list.  Load failed.\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	free_config();
	load_config();
	AST_RWLIST_UNLOCK(&sinks);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Customizable Comma Separated Values CDR Backend",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );

