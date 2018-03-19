/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Steve Murphy <murf@digium.com>
 * much borrowed from cdr code (cdr_custom.c), author Mark Spencer
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
 * \brief Custom Comma Separated Value CEL records.
 *
 * \author Steve Murphy <murf@digium.com>
 *
 * \arg See also \ref AstCEL
 *
 * Logs in LOG_DIR/cel_custom
 * \ingroup cel_drivers
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/paths.h"
#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/threadstorage.h"
#include "asterisk/strings.h"

#define CUSTOM_LOG_DIR "/cel_custom"
#define CONFIG         "cel_custom.conf"

AST_THREADSTORAGE(custom_buf);

static const char name[] = "cel-custom";

struct cel_config {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(filename);
		AST_STRING_FIELD(format);
	);
	ast_mutex_t lock;
	AST_RWLIST_ENTRY(cel_config) list;
};

#define CUSTOM_BACKEND_NAME "CEL Custom CSV Logging"

static AST_RWLIST_HEAD_STATIC(sinks, cel_config);

static void free_config(void)
{
	struct cel_config *sink;

	while ((sink = AST_RWLIST_REMOVE_HEAD(&sinks, list))) {
		ast_mutex_destroy(&sink->lock);
		ast_string_field_free_memory(sink);
		ast_free(sink);
	}
}

static int load_config(void)
{
	struct ast_config *cfg;
	struct ast_variable *var;
	struct ast_flags config_flags = { 0 };
	int mappings = 0;
	int res = 0;

	cfg = ast_config_load(CONFIG, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Unable to load " CONFIG ". Not logging CEL to custom CSVs.\n");
		return -1;
	}

	if (!(var = ast_variable_browse(cfg, "mappings"))) {
		ast_log(LOG_NOTICE, "No mappings found in " CONFIG ". Not logging CEL to custom CSVs.\n");
	}

	while (var) {
		if (!ast_strlen_zero(var->name) && !ast_strlen_zero(var->value)) {
			struct cel_config *sink = ast_calloc_with_stringfields(1, struct cel_config, 1024);

			if (!sink) {
				ast_log(LOG_ERROR, "Unable to allocate memory for configuration settings.\n");
				res = -2;
				break;
			}

			ast_string_field_build(sink, format, "%s\n", var->value);
			ast_string_field_build(sink, filename, "%s/%s/%s", ast_config_AST_LOG_DIR, name, var->name);
			ast_mutex_init(&sink->lock);

			ast_verb(3, "Added CEL CSV mapping for '%s'.\n", sink->filename);
			mappings += 1;
			AST_RWLIST_INSERT_TAIL(&sinks, sink, list);
		} else {
			ast_log(LOG_NOTICE, "Mapping must have both a filename and a format at line %d\n", var->lineno);
		}
		var = var->next;
	}
	ast_config_destroy(cfg);

	ast_verb(1, "Added CEL CSV mapping for %d files.\n", mappings);

	return res;
}

static void custom_log(struct ast_event *event)
{
	struct ast_channel *dummy;
	struct ast_str *str;
	struct cel_config *config;

	/* Batching saves memory management here.  Otherwise, it's the same as doing an allocation and free each time. */
	if (!(str = ast_str_thread_get(&custom_buf, 16))) {
		return;
	}

	dummy = ast_cel_fabricate_channel_from_event(event);
	if (!dummy) {
		ast_log(LOG_ERROR, "Unable to fabricate channel from CEL event.\n");
		return;
	}

	AST_RWLIST_RDLOCK(&sinks);

	AST_LIST_TRAVERSE(&sinks, config, list) {
		FILE *out;

		ast_str_substitute_variables(&str, 0, dummy, config->format);

		/* Even though we have a lock on the list, we could be being chased by
		   another thread and this lock ensures that we won't step on anyone's
		   toes.  Once each CEL backend gets it's own thread, this lock can be
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

	ast_channel_unref(dummy);
}

static int unload_module(void)
{

	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(LOG_ERROR, "Unable to lock sink list.  Unload failed.\n");
		return -1;
	}

	free_config();
	AST_RWLIST_UNLOCK(&sinks);
	ast_cel_backend_unregister(CUSTOM_BACKEND_NAME);
	return 0;
}

static enum ast_module_load_result load_module(void)
{
	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(LOG_ERROR, "Unable to lock sink list.  Load failed.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	load_config();
	AST_RWLIST_UNLOCK(&sinks);

	if (ast_cel_backend_register(CUSTOM_BACKEND_NAME, custom_log)) {
		free_config();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(LOG_ERROR, "Unable to lock sink list.  Load failed.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	free_config();
	load_config();
	AST_RWLIST_UNLOCK(&sinks);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Customizable Comma Separated Values CEL Backend",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cel",
);
