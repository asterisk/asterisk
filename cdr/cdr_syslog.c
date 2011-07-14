/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, malleable, LLC.
 *
 * Sean Bright <sean@malleable.com>
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

/*!
 * \file
 * \brief syslog CDR logger
 *
 * \author Sean Bright <sean@malleable.com>
 *
 * See also
 * \arg \ref Config_cdr
 * \ingroup cdr_drivers
 */

/*** MODULEINFO
	<depend>syslog</depend>
	<support_level>core</support_level>
***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/cdr.h"
#include "asterisk/pbx.h"

#include <syslog.h>

#include "asterisk/syslog.h"

static const char CONFIG[] = "cdr_syslog.conf";

AST_THREADSTORAGE(syslog_buf);

static const char name[] = "cdr-syslog";

struct cdr_config {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(ident);
		AST_STRING_FIELD(format);
	);
	int facility;
	int priority;
	ast_mutex_t lock;
	AST_LIST_ENTRY(cdr_config) list;
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

static int syslog_log(struct ast_cdr *cdr)
{
	struct ast_channel *dummy;
	struct ast_str *str;
	struct cdr_config *sink;

	/* Batching saves memory management here.  Otherwise, it's the same as doing an
	   allocation and free each time. */
	if (!(str = ast_str_thread_get(&syslog_buf, 16))) {
		return -1;
	}

	if (!(dummy = ast_dummy_channel_alloc())) {
		ast_log(AST_LOG_ERROR, "Unable to allocate channel for variable substitution.\n");
		return -1;
	}

	/* We need to dup here since the cdr actually belongs to the other channel,
	   so when we release this channel we don't want the CDR getting cleaned
	   up prematurely. */
	dummy->cdr = ast_cdr_dup(cdr);

	AST_RWLIST_RDLOCK(&sinks);

	AST_LIST_TRAVERSE(&sinks, sink, list) {

		ast_str_substitute_variables(&str, 0, dummy, sink->format);

		/* Even though we have a lock on the list, we could be being chased by
		   another thread and this lock ensures that we won't step on anyone's
		   toes.  Once each CDR backend gets it's own thread, this lock can be
		   removed. */
		ast_mutex_lock(&sink->lock);

		openlog(sink->ident, LOG_CONS, sink->facility);
		syslog(sink->priority, "%s", ast_str_buffer(str));
		closelog();

		ast_mutex_unlock(&sink->lock);
	}

	AST_RWLIST_UNLOCK(&sinks);

	ast_channel_release(dummy);

	return 0;
}

static int load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	int default_facility = LOG_LOCAL4;
	int default_priority = LOG_INFO;
	const char *catg = NULL, *tmp;

	cfg = ast_config_load(CONFIG, config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(AST_LOG_ERROR,
			"Unable to load %s. Not logging custom CSV CDRs to syslog.\n", CONFIG);
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (reload) {
		free_config();
	}

	if (!(ast_strlen_zero(tmp = ast_variable_retrieve(cfg, "general", "facility")))) {
		int facility = ast_syslog_facility(tmp);
		if (facility < 0) {
			ast_log(AST_LOG_WARNING,
				"Invalid facility '%s' specified, defaulting to '%s'\n",
				tmp, ast_syslog_facility_name(default_facility));
		} else {
			default_facility = facility;
		}
	}

	if (!(ast_strlen_zero(tmp = ast_variable_retrieve(cfg, "general", "priority")))) {
		int priority = ast_syslog_priority(tmp);
		if (priority < 0) {
			ast_log(AST_LOG_WARNING,
				"Invalid priority '%s' specified, defaulting to '%s'\n",
				tmp, ast_syslog_priority_name(default_priority));
		} else {
			default_priority = priority;
		}
	}

	while ((catg = ast_category_browse(cfg, catg))) {
		struct cdr_config *sink;

		if (!strcasecmp(catg, "general")) {
			continue;
		}

		if (ast_strlen_zero(tmp = ast_variable_retrieve(cfg, catg, "template"))) {
			ast_log(AST_LOG_WARNING,
				"No 'template' parameter found for '%s'.  Skipping.\n", catg);
			continue;
		}

		sink = ast_calloc_with_stringfields(1, struct cdr_config, 1024);

		if (!sink) {
			ast_log(AST_LOG_ERROR,
				"Unable to allocate memory for configuration settings.\n");
			free_config();
			break;
		}

		ast_mutex_init(&sink->lock);
		ast_string_field_set(sink, ident, catg);
		ast_string_field_set(sink, format, tmp);

		if (ast_strlen_zero(tmp = ast_variable_retrieve(cfg, catg, "facility"))) {
			sink->facility = default_facility;
		} else {
			int facility = ast_syslog_facility(tmp);
			if (facility < 0) {
				ast_log(AST_LOG_WARNING,
					"Invalid facility '%s' specified for '%s,' defaulting to '%s'\n",
					tmp, catg, ast_syslog_facility_name(default_facility));
			} else {
				sink->facility = facility;
			}
		}

		if (ast_strlen_zero(tmp = ast_variable_retrieve(cfg, catg, "priority"))) {
			sink->priority = default_priority;
		} else {
			int priority = ast_syslog_priority(tmp);
			if (priority < 0) {
				ast_log(AST_LOG_WARNING,
					"Invalid priority '%s' specified for '%s,' defaulting to '%s'\n",
					tmp, catg, ast_syslog_priority_name(default_priority));
			} else {
				sink->priority = priority;
			}
		}

		AST_RWLIST_INSERT_TAIL(&sinks, sink, list);
	}

	ast_config_destroy(cfg);

	return AST_RWLIST_EMPTY(&sinks) ? -1 : 0;
}

static int unload_module(void)
{
	ast_cdr_unregister(name);

	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_cdr_register(name, ast_module_info->description, syslog_log);
		ast_log(AST_LOG_ERROR, "Unable to lock sink list.  Unload failed.\n");
		return -1;
	}

	free_config();
	AST_RWLIST_UNLOCK(&sinks);
	return 0;
}

static enum ast_module_load_result load_module(void)
{
	int res;

	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(AST_LOG_ERROR, "Unable to lock sink list.  Load failed.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	res = load_config(0);
	AST_RWLIST_UNLOCK(&sinks);
	if (res) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_cdr_register(name, ast_module_info->description, syslog_log);
	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	int res;
	if (AST_RWLIST_WRLOCK(&sinks)) {
		ast_log(AST_LOG_ERROR, "Unable to lock sink list.  Load failed.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	if ((res = load_config(1))) {
		free_config();
	}

	AST_RWLIST_UNLOCK(&sinks);

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Customizable syslog CDR Backend",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
);
