/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Greenfield Technologies Ltd.
 *
 * Nir Simionovich <nirs@greenfieldtech.net>
 * who freely borrowed code from the cel manager equivalents
 *     (see cel/cel_manager.c)
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
 * \brief Asterisk Channel Event Beanstalkd backend
 *
 * This module requires the beanstalk-client library, avaialble from
 * https://github.com/deepfryed/beanstalk-client
 * \ingroup cel_drivers
 */

/*! \li \ref cek_beanstalkd.c uses the configuration file \ref cel.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cel.conf cel.conf
 * \verbinclude cel.conf.sample
 */

/*** MODULEINFO
	<depend>beanstalk</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/json.h"

#include "beanstalk.h"

static const char DATE_FORMAT[] = "%Y-%m-%d %T";

static const char CONF_FILE[] = "cel_beanstalkd.conf";

/*! \brief Beanstalk CEL is off by default */
#define CEL_BEANSTALK_ENABLED_DEFAULT		0

static int enablecel;

/*! \brief show_user_def is off by default */
#define CEL_SHOW_USERDEF_DEFAULT	0

#define CEL_BACKEND_NAME "Beanstalk Event Logging"

#define BEANSTALK_JOB_SIZE 4096
#define BEANSTALK_JOB_PRIORITY 99
#define BEANSTALK_JOB_TTR 60
#define BEANSTALK_JOB_DELAY 0
#define DEFAULT_BEANSTALK_HOST "127.0.0.1"
#define DEFAULT_BEANSTALK_PORT 11300
#define DEFAULT_BEANSTALK_TUBE "asterisk-cel"

static char *bs_host;
static int bs_port;
static char *bs_tube;
static int priority;

AST_RWLOCK_DEFINE_STATIC(config_lock);

static void cel_bs_put(struct ast_event *event)
{
	struct ast_tm timeresult;
	char start_time[80];
	char *cel_buffer;
	int bs_id;
	int bs_socket;
	struct ast_json *t_cel_json;

	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	if (!enablecel) {
		return;
	}

	if (ast_cel_fill_record(event, &record)) {
		return;
	}

	ast_rwlock_rdlock(&config_lock);
	bs_socket = bs_connect(bs_host, bs_port);

	if (bs_use(bs_socket, bs_tube) != BS_STATUS_OK) {
		ast_log(LOG_ERROR, "Connection to Beanstalk tube %s @ %s:%d had failed", bs_tube, bs_host, bs_port);
		ast_rwlock_unlock(&config_lock);
		return;
	}

	ast_localtime(&record.event_time, &timeresult, NULL);
	ast_strftime(start_time, sizeof(start_time), DATE_FORMAT, &timeresult);

	ast_rwlock_unlock(&config_lock);

	t_cel_json = ast_json_pack("{s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s}",
							   "EventName", S_OR(record.event_name, ""),
							   "AccountCode", S_OR(record.account_code, ""),
							   "CallerIDnum", S_OR(record.caller_id_num, ""),
							   "CallerIDname", S_OR(record.caller_id_name, ""),
							   "CallerIDani", S_OR(record.caller_id_ani, ""),
							   "CallerIDrdnis", S_OR(record.caller_id_rdnis, ""),
							   "CallerIDdnid", S_OR(record.caller_id_dnid, ""),
							   "Exten", S_OR(record.extension, ""),
							   "Context", S_OR(record.context, ""),
							   "Channel", S_OR(record.channel_name, ""),
							   "Application", S_OR(record.application_name, ""),
							   "AppData", S_OR(record.application_data, ""),
							   "EventTime", S_OR(start_time, ""),
							   "AMAFlags", S_OR(ast_channel_amaflags2string(record.amaflag), ""),
							   "UniqueID", S_OR(record.unique_id, ""),
							   "LinkedID", S_OR(record.linked_id, ""),
							   "Userfield", S_OR(record.user_field, ""),
							   "Peer", S_OR(record.peer_account, ""),
							   "PeerAccount", S_OR(record.peer_account, ""),
							   "Extra", S_OR(record.extra, "")

	);

	cel_buffer = ast_json_dump_string(t_cel_json);

	ast_json_unref(t_cel_json);

	bs_id = bs_put(bs_socket, priority, BEANSTALK_JOB_DELAY, BEANSTALK_JOB_TTR, cel_buffer, strlen(cel_buffer));

	if (bs_id > 0) {
		ast_log(LOG_DEBUG, "Successfully created job %d with %s\n", bs_id, cel_buffer);
	} else {
		ast_log(LOG_ERROR, "CDR job creation failed for %s\n", cel_buffer);
	}

	bs_disconnect(bs_socket);
	ast_json_free(cel_buffer);
}

static int load_config(int reload)
{
	const char *cat = NULL;
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_variable *v;
	int newenablecel = CEL_BEANSTALK_ENABLED_DEFAULT;

	cfg = ast_config_load(CONF_FILE, config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Configuration file '%s' is invalid. CEL Beanstalkd Module not activated.\n",
			CONF_FILE);
		return -1;
	} else if (!cfg) {
		ast_log(LOG_WARNING, "Failed to load configuration file. CEL Beanstalkd Module not activated.\n");
		if (enablecel) {
			ast_cel_backend_unregister(CEL_BACKEND_NAME);
		}
		enablecel = 0;
		return -1;
	}

	if (reload) {
		ast_rwlock_wrlock(&config_lock);
		ast_free(bs_host);
		ast_free(bs_tube);
	}

	/* Bootstrap the default configuration */
	bs_host = ast_strdup(DEFAULT_BEANSTALK_HOST);
	bs_port = DEFAULT_BEANSTALK_PORT;
	bs_tube = ast_strdup(DEFAULT_BEANSTALK_TUBE);
	priority = BEANSTALK_JOB_PRIORITY;

	while ((cat = ast_category_browse(cfg, cat))) {

		if (strcasecmp(cat, "general")) {
			continue;
		}

		for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
			if (!strcasecmp(v->name, "enabled")) {
				newenablecel = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "host")) {
				ast_free(bs_host);
				bs_host = ast_strdup(v->value);
			} else if (!strcasecmp(v->name, "port")) {
				bs_port = atoi(v->value);
			} else if (!strcasecmp(v->name, "tube")) {
				ast_free(bs_tube);
				bs_tube = ast_strdup(v->value);
			} else if (!strcasecmp(v->name, "priority")) {
				priority = atoi(v->value);
			} else {
				ast_log(LOG_NOTICE, "Unknown option '%s' specified "
						"for CEL beanstalk backend.\n", v->name);
			}
		}
	}

	if (reload) {
		ast_rwlock_unlock(&config_lock);
	}

	ast_config_destroy(cfg);

	if (enablecel && !newenablecel) {
		ast_cel_backend_unregister(CEL_BACKEND_NAME);
	} else if (!enablecel && newenablecel) {
		if (ast_cel_backend_register(CEL_BACKEND_NAME, cel_bs_put)) {
			ast_log(LOG_ERROR, "Unable to register Beanstalkd CEL handling\n");
		}
	}

	enablecel = newenablecel;

	return 0;
}

static int unload_module(void)
{
	ast_cel_backend_unregister(CEL_BACKEND_NAME);
	ast_free(bs_host);
	ast_free(bs_tube);
	return 0;
}

static int load_module(void)
{
	if (load_config(0)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Beanstalkd CEL Backend",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cel",
);
