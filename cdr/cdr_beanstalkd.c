/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017
 *
 * Nir Simionovich <nirs@greenfieldtech.net>
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
 * \brief Asterisk Beanstalkd CDR records.
 *
 * This module requires the beanstalk-client library, available from
 * https://github.com/deepfryed/beanstalk-client
 *
 * See also
 * \arg \ref AstCDR
 * \ingroup cdr_drivers
 */

/*! \li \ref cdr_beanstalkd.c uses the configuration file \ref cdr_beanstalkd.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page cdr_beanstalkd.conf cdr_beanstalkd.conf
 * \verbinclude cdr_beanstalkd.conf.sample
 */

/*** MODULEINFO
	<depend>beanstalk</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <time.h>
#include <stdio.h>

#include "beanstalk.h"
#include "asterisk/channel.h"
#include "asterisk/cdr.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/json.h"

#define DATE_FORMAT    "%Y-%m-%d %T"
#define CONF_FILE    "cdr_beanstalkd.conf"
#define BEANSTALK_JOB_SIZE 4096
#define BEANSTALK_JOB_PRIORITY 99
#define BEANSTALK_JOB_TTR 60
#define BEANSTALK_JOB_DELAY 0
#define DEFAULT_BEANSTALK_HOST "127.0.0.1"
#define DEFAULT_BEANSTALK_PORT 11300
#define DEFAULT_BEANSTALK_TUBE "asterisk-cdr"

static const char name[] = "cdr_beanstalkd";

static int enablecdr = 0;
static char *bs_host;
static int bs_port;
static char *bs_tube;
static int priority;

AST_RWLOCK_DEFINE_STATIC(config_lock);

static int beanstalk_put(struct ast_cdr *cdr);

static int load_config(int reload) {
	char *cat = NULL;
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = {reload ? CONFIG_FLAG_FILEUNCHANGED : 0};
	int newenablecdr = 0;

	cfg = ast_config_load(CONF_FILE, config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file '%s' could not be parsed\n", CONF_FILE);
		return -1;
	}

	if (!cfg) {
		/* Standard configuration */
		ast_log(LOG_WARNING, "Failed to load configuration file. Module not activated.\n");
		if (enablecdr) {
			ast_cdr_backend_suspend(name);
		}
		enablecdr = 0;
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
		if (!strcasecmp(cat, "general")) {
			v = ast_variable_browse(cfg, cat);
			while (v) {

				if (!strcasecmp(v->name, "enabled")) {
					newenablecdr = ast_true(v->value);
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
				}
				v = v->next;

			}
		}
	}

	if (reload) {
		ast_rwlock_unlock(&config_lock);
	}

	ast_config_destroy(cfg);

	if (!newenablecdr) {
		ast_cdr_backend_suspend(name);
	} else if (newenablecdr) {
		ast_cdr_backend_unsuspend(name);
		ast_log(LOG_NOTICE, "Added beanstalkd server %s at port %d with tube %s", bs_host, bs_port, bs_tube);
	}
	enablecdr = newenablecdr;

	return 0;
}

static int beanstalk_put(struct ast_cdr *cdr) {
	struct ast_tm timeresult;
	char strAnswerTime[80] = "";
	char strStartTime[80];
	char strEndTime[80];
	char *cdr_buffer;
	int bs_id;
	int bs_socket;
	struct ast_json *t_cdr_json;

	if (!enablecdr) {
		return 0;
	}

	ast_rwlock_rdlock(&config_lock);
	bs_socket = bs_connect(bs_host, bs_port);

	if (bs_use(bs_socket, bs_tube) != BS_STATUS_OK) {
		ast_log(LOG_ERROR, "Connection to Beanstalk tube %s @ %s:%d had failed", bs_tube, bs_host, bs_port);
		ast_rwlock_unlock(&config_lock);
		return 0;
	}

	ast_localtime(&cdr->start, &timeresult, NULL);
	ast_strftime(strStartTime, sizeof(strStartTime), DATE_FORMAT, &timeresult);

	if (cdr->answer.tv_sec) {
		ast_localtime(&cdr->answer, &timeresult, NULL);
		ast_strftime(strAnswerTime, sizeof(strAnswerTime), DATE_FORMAT, &timeresult);
	}

	ast_localtime(&cdr->end, &timeresult, NULL);
	ast_strftime(strEndTime, sizeof(strEndTime), DATE_FORMAT, &timeresult);

	ast_rwlock_unlock(&config_lock);

	t_cdr_json = ast_json_pack("{s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:s, s:i, s:i, s:s, s:s, s:s, s:s}",
							   "AccountCode", S_OR(cdr->accountcode, ""),
							   "Source", S_OR(cdr->src, ""),
							   "Destination", S_OR(cdr->dst, ""),
							   "DestinationContext", S_OR(cdr->dcontext, ""),
							   "CallerID", S_OR(cdr->clid, ""),
							   "Channel", S_OR(cdr->channel, ""),
							   "DestinationChannel", S_OR(cdr->dstchannel, ""),
							   "LastApplication", S_OR(cdr->lastapp, ""),
							   "LastData", S_OR(cdr->lastdata, ""),
							   "StartTime", S_OR(strStartTime, ""),
							   "AnswerTime", S_OR(strAnswerTime, ""),
							   "EndTime", S_OR(strEndTime, ""),
							   "Duration", cdr->duration,
							   "Billsec", cdr->billsec,
							   "Disposition", S_OR(ast_cdr_disp2str(cdr->disposition), ""),
							   "AMAFlags", S_OR(ast_channel_amaflags2string(cdr->amaflags), ""),
							   "UniqueID", S_OR(cdr->uniqueid, ""),
							   "UserField", S_OR(cdr->userfield, ""));

	cdr_buffer = ast_json_dump_string(t_cdr_json);

	ast_json_unref(t_cdr_json);

	bs_id = bs_put(bs_socket, priority, BEANSTALK_JOB_DELAY, BEANSTALK_JOB_TTR, cdr_buffer, strlen(cdr_buffer));

	if (bs_id > 0) {
		ast_log(LOG_DEBUG, "Successfully created job %d with %s\n", bs_id, cdr_buffer);
	} else {
		ast_log(LOG_ERROR, "CDR job creation failed for %s\n", cdr_buffer);
	}

	bs_disconnect(bs_socket);
	ast_json_free(cdr_buffer);
	return 0;
}

static int unload_module(void) {
	if (ast_cdr_unregister(name)) {
		return -1;
	}

	ast_free(bs_host);
	ast_free(bs_tube);

	return 0;
}

static int load_module(void) {
	if (ast_cdr_register(name, "Asterisk CDR Beanstalkd Backend", beanstalk_put)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (load_config(0)) {
		ast_cdr_unregister(name);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void) {
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk Beanstalkd CDR Backend",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
	.requires = "cdr",
);
