/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 - 2009, Digium, Inc.
 *
 * Steve Murphy <murf@digium.com>
 * who freely borrowed code from the cdr equivalents
 *     (see cdr/cdr_manager.c)
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
 * \brief Asterisk Channel Event records.
 *
 * See also
 * \arg \ref AstCDR
 * \arg \ref AstAMI
 * \arg \ref Config_ami
 * \ingroup cel_drivers
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"

static const char DATE_FORMAT[] = "%Y-%m-%d %T";

static const char CONF_FILE[] = "cel.conf";

/*! \brief AMI CEL is off by default */
#define CEL_AMI_ENABLED_DEFAULT		0

static int enablecel;

/*! \brief show_user_def is off by default */
#define CEL_SHOW_USERDEF_DEFAULT	0

#define MANAGER_BACKEND_NAME "Manager Event Logging"

/*! TRUE if we should set the EventName header to USER_DEFINED on user events. */
static unsigned char cel_show_user_def;

static void manager_log(struct ast_event *event)
{
	struct ast_tm timeresult;
	char start_time[80] = "";
	char user_defined_header[160];
	const char *event_name;
	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	if (!enablecel) {
		return;
	}

	if (ast_cel_fill_record(event, &record)) {
		return;
	}

	ast_localtime(&record.event_time, &timeresult, NULL);
	ast_strftime(start_time, sizeof(start_time), DATE_FORMAT, &timeresult);

	event_name = record.event_name;
	user_defined_header[0] = '\0';
	if (record.event_type == AST_CEL_USER_DEFINED) {
		if (cel_show_user_def) {
			snprintf(user_defined_header, sizeof(user_defined_header),
				"UserDefType: %s\r\n", record.user_defined_name);
		} else {
			event_name = record.user_defined_name;
		}
	}

	manager_event(EVENT_FLAG_CALL, "CEL",
		"EventName: %s\r\n"
		"AccountCode: %s\r\n"
		"CallerIDnum: %s\r\n"
		"CallerIDname: %s\r\n"
		"CallerIDani: %s\r\n"
		"CallerIDrdnis: %s\r\n"
		"CallerIDdnid: %s\r\n"
		"Exten: %s\r\n"
		"Context: %s\r\n"
		"Channel: %s\r\n"
		"Application: %s\r\n"
		"AppData: %s\r\n"
		"EventTime: %s\r\n"
		"AMAFlags: %s\r\n"
		"UniqueID: %s\r\n"
		"LinkedID: %s\r\n"
		"Userfield: %s\r\n"
		"Peer: %s\r\n"
		"PeerAccount: %s\r\n"
		"%s"
		"Extra: %s\r\n",
		event_name,
		record.account_code,
		record.caller_id_num,
		record.caller_id_name,
		record.caller_id_ani,
		record.caller_id_rdnis,
		record.caller_id_dnid,
		record.extension,
		record.context,
		record.channel_name,
		record.application_name,
		record.application_data,
		start_time,
		ast_channel_amaflags2string(record.amaflag),
		record.unique_id,
		record.linked_id,
		record.user_field,
		record.peer,
		record.peer_account,
		user_defined_header,
		record.extra);
}

static int load_config(int reload)
{
	const char *cat = NULL;
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	struct ast_variable *v;
	int newenablecel = CEL_AMI_ENABLED_DEFAULT;
	int new_cel_show_user_def = CEL_SHOW_USERDEF_DEFAULT;

	cfg = ast_config_load(CONF_FILE, config_flags);
	if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Configuration file '%s' is invalid. CEL manager Module not activated.\n",
			CONF_FILE);
		enablecel = 0;
		return -1;
	} else if (!cfg) {
		ast_log(LOG_WARNING, "Failed to load configuration file. CEL manager Module not activated.\n");
		enablecel = 0;
		return -1;
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		if (strcasecmp(cat, "manager")) {
			continue;
		}

		for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
			if (!strcasecmp(v->name, "enabled")) {
				newenablecel = ast_true(v->value) ? 1 : 0;
			} else if (!strcasecmp(v->name, "show_user_defined")) {
				new_cel_show_user_def = ast_true(v->value) ? 1 : 0;
			} else {
				ast_log(LOG_NOTICE, "Unknown option '%s' specified "
						"for cel_manager.\n", v->name);
			}
		}
	}

	ast_config_destroy(cfg);

	cel_show_user_def = new_cel_show_user_def;
	if (enablecel && !newenablecel) {
		ast_cel_backend_unregister(MANAGER_BACKEND_NAME);
	} else if (!enablecel && newenablecel) {
		if (ast_cel_backend_register(MANAGER_BACKEND_NAME, manager_log)) {
			ast_log(LOG_ERROR, "Unable to register Asterisk Call Manager CEL handling\n");
		}
	}
	enablecel = newenablecel;

	return 0;
}

static int unload_module(void)
{
	ast_cel_backend_unregister(MANAGER_BACKEND_NAME);
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

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk Manager Interface CEL Backend",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CDR_DRIVER,
);
