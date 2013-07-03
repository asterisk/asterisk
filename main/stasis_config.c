/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Stasis Message Bus configuration API.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/config_options.h"
#include "asterisk/stasis.h"
#include "asterisk/threadpool.h"

#include <limits.h>

/*** DOCUMENTATION
	<configInfo name="stasis" language="en_US">
		<synopsis>Stasis message bus configuration.</synopsis>
		<configFile name="stasis.conf">
			<configObject name="threadpool">
				<synopsis>Threadpool configuration.</synopsis>
				<configOption name="initial_size" default="0">
					<synopsis>Initial number of threads in the message bus threadpool.</synopsis>
				</configOption>
				<configOption name="idle_timeout_sec" default="20">
					<synopsis>Number of seconds for an idle thread to be disposed of.</synopsis>
				</configOption>
				<configOption name="max_size" default="200">
					<synopsis>Maximum number of threads in the threadpool.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/*! \brief Locking container for safe configuration access. */
static AO2_GLOBAL_OBJ_STATIC(confs);

struct stasis_threadpool_conf {
	int initial_size;
	int idle_timeout_sec;
	int max_size;
};

struct stasis_conf {
	struct stasis_threadpool_conf *threadpool;
};

/*! \brief Mapping of the stasis http conf struct's globals to the
 *         threadpool context in the config file. */
static struct aco_type threadpool_option = {
        .type = ACO_GLOBAL,
        .name = "threadpool",
        .item_offset = offsetof(struct stasis_conf, threadpool),
        .category = "^threadpool$",
        .category_match = ACO_WHITELIST,
};

static struct aco_type *threadpool_options[] = ACO_TYPES(&threadpool_option);

#define CONF_FILENAME "stasis.conf"

/*! \brief The conf file that's processed for the module. */
static struct aco_file conf_file = {
        /*! The config file name. */
        .filename = CONF_FILENAME,
        /*! The mapping object types to be processed. */
        .types = ACO_TYPES(&threadpool_option),
};

static void conf_dtor(void *obj)
{
	struct stasis_conf *conf = obj;

	ao2_cleanup(conf->threadpool);
	conf->threadpool = NULL;
}

static void *conf_alloc(void)
{
	RAII_VAR(struct stasis_conf *, conf, NULL, ao2_cleanup);

	conf = ao2_alloc_options(sizeof(*conf), conf_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!conf) {
		return NULL;
	}

	conf->threadpool = ao2_alloc_options(sizeof(*conf->threadpool), NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!conf->threadpool) {
		return NULL;
	}

	aco_set_defaults(&threadpool_option, "threadpool", conf->threadpool);

	ao2_ref(conf, +1);
	return conf;
}

CONFIG_INFO_CORE("stasis", cfg_info, confs, conf_alloc,
	.files = ACO_FILES(&conf_file));

void stasis_config_get_threadpool_options(
	struct ast_threadpool_options *threadpool_options)
{
	RAII_VAR(struct stasis_conf *, conf, NULL, ao2_cleanup);

	conf = ao2_global_obj_ref(confs);

	ast_assert(conf && conf->threadpool);

	{
		struct ast_threadpool_options newopts = {
			.version = AST_THREADPOOL_OPTIONS_VERSION,
			.initial_size = conf->threadpool->initial_size,
			.auto_increment = 1,
			.idle_timeout = conf->threadpool->idle_timeout_sec,
			.max_size = conf->threadpool->max_size,
		};

		*threadpool_options = newopts;
	}
}

/*! \brief Load (or reload) configuration. */
static int process_config(int reload)
{
        switch (aco_process_config(&cfg_info, reload)) {
        case ACO_PROCESS_ERROR:
                return -1;
        case ACO_PROCESS_OK:
        case ACO_PROCESS_UNCHANGED:
                break;
        }

	return 0;
}

static void config_exit(void)
{
	aco_info_destroy(&cfg_info);
}

int stasis_config_init(void)
{
	if (aco_info_init(&cfg_info)) {
		aco_info_destroy(&cfg_info);
		return -1;
	}

	ast_register_atexit(config_exit);

	/* threadpool section */
	aco_option_register(&cfg_info, "initial_size", ACO_EXACT,
		threadpool_options, "0", OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct stasis_threadpool_conf, initial_size), 0,
		INT_MAX);
	aco_option_register(&cfg_info, "idle_timeout_sec", ACO_EXACT,
		threadpool_options, "20", OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct stasis_threadpool_conf, idle_timeout_sec), 0,
		INT_MAX);
	aco_option_register(&cfg_info, "max_size", ACO_EXACT,
		threadpool_options, "200", OPT_INT_T, PARSE_IN_RANGE,
		FLDSET(struct stasis_threadpool_conf, max_size), 0, INT_MAX);

	return process_config(0);
}
