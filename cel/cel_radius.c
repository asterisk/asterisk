/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief RADIUS CEL Support
 * \author Philippe Sultan
 * The Radius Client Library - http://developer.berlios.de/projects/radiusclient-ng/
 *
 * \arg See also \ref AstCEL
 * \ingroup cel_drivers
 */

/*** MODULEINFO
	<depend>radius</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__, "$Rev$")

#ifdef FREERADIUS_CLIENT
#include <freeradius-client.h>
#else
#include <radiusclient-ng.h>
#endif

#include "asterisk/channel.h"
#include "asterisk/cel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"

/*! ISO 8601 standard format */
#define DATE_FORMAT "%Y-%m-%d %T %z"

#define VENDOR_CODE           22736

enum {
	PW_AST_ACCT_CODE =    101,
	PW_AST_CIDNUM =       102,
	PW_AST_CIDNAME =      103,
	PW_AST_CIDANI =       104,
	PW_AST_CIDRDNIS =     105,
	PW_AST_CIDDNID =      106,
	PW_AST_EXTEN =        107,
	PW_AST_CONTEXT =      108,
	PW_AST_CHANNAME =     109,
	PW_AST_APPNAME =      110,
	PW_AST_APPDATA =      111,
	PW_AST_EVENT_TIME =   112,
	PW_AST_AMA_FLAGS =    113,
	PW_AST_UNIQUE_ID =    114,
	PW_AST_USER_NAME =    115,
	PW_AST_LINKED_ID =    116,
};

enum {
	/*! Log dates and times in UTC */
	RADIUS_FLAG_USEGMTIME = (1 << 0),
	/*! Log Unique ID */
	RADIUS_FLAG_LOGUNIQUEID = (1 << 1),
	/*! Log User Field */
	RADIUS_FLAG_LOGUSERFIELD = (1 << 2)
};

static char *cel_config = "cel.conf";

#ifdef FREERADIUS_CLIENT
static char radiuscfg[PATH_MAX] = "/etc/radiusclient/radiusclient.conf";
#else
static char radiuscfg[PATH_MAX] = "/etc/radiusclient-ng/radiusclient.conf";
#endif

static struct ast_flags global_flags = { RADIUS_FLAG_USEGMTIME | RADIUS_FLAG_LOGUNIQUEID | RADIUS_FLAG_LOGUSERFIELD };

static rc_handle *rh = NULL;

#define RADIUS_BACKEND_NAME "CEL Radius Logging"

#define ADD_VENDOR_CODE(x,y) (rc_avpair_add(rh, send, x, &y, strlen(y), VENDOR_CODE))

static int build_radius_record(VALUE_PAIR **send, struct ast_cel_event_record *record)
{
	int recordtype = PW_STATUS_STOP;
	struct ast_tm tm;
	char timestr[128];
	char *amaflags;

	if (!rc_avpair_add(rh, send, PW_ACCT_STATUS_TYPE, &recordtype, 0, 0)) {
		return -1;
	}
	/* Account code */
	if (!ADD_VENDOR_CODE(PW_AST_ACCT_CODE, record->account_code)) {
		return -1;
	}
	/* Source */
	if (!ADD_VENDOR_CODE(PW_AST_CIDNUM, record->caller_id_num)) {
		return -1;
	}
	/* Destination */
	if (!ADD_VENDOR_CODE(PW_AST_EXTEN, record->extension)) {
		return -1;
	}
	/* Destination context */
	if (!ADD_VENDOR_CODE(PW_AST_CONTEXT, record->context)) {
		return -1;
	}
	/* Caller ID */
	if (!ADD_VENDOR_CODE(PW_AST_CIDNAME, record->caller_id_name)) {
		return -1;
	}
	/* Caller ID ani */
	if (!ADD_VENDOR_CODE(PW_AST_CIDANI, record->caller_id_ani)) {
		return -1;
	}
	/* Caller ID rdnis */
	if (!ADD_VENDOR_CODE(PW_AST_CIDRDNIS, record->caller_id_rdnis)) {
		return -1;
	}
	/* Caller ID dnid */
	if (!ADD_VENDOR_CODE(PW_AST_CIDDNID, record->caller_id_dnid)) {
		return -1;
	}
	/* Channel */
	if (!ADD_VENDOR_CODE(PW_AST_CHANNAME, record->channel_name)) {
		return -1;
	}
	/* Last Application */
	if (!ADD_VENDOR_CODE(PW_AST_APPNAME, record->application_name)) {
		return -1;
	}
	/* Last Data */
	if (!ADD_VENDOR_CODE(PW_AST_APPDATA, record->application_data)) {
		return -1;
	}
	/* Event Time */
	ast_localtime(&record->event_time, &tm,
		ast_test_flag(&global_flags, RADIUS_FLAG_USEGMTIME) ? "GMT" : NULL);
	ast_strftime(timestr, sizeof(timestr), DATE_FORMAT, &tm);
	if (!rc_avpair_add(rh, send, PW_AST_EVENT_TIME, timestr, strlen(timestr), VENDOR_CODE)) {
		return -1;
	}
	/* AMA Flags */
	amaflags = ast_strdupa(ast_channel_amaflags2string(record->amaflag));
	if (!rc_avpair_add(rh, send, PW_AST_AMA_FLAGS, amaflags, strlen(amaflags), VENDOR_CODE)) {
		return -1;
	}
	if (ast_test_flag(&global_flags, RADIUS_FLAG_LOGUNIQUEID)) {
		/* Unique ID */
		if (!ADD_VENDOR_CODE(PW_AST_UNIQUE_ID, record->unique_id)) {
			return -1;
		}
	}
	/* LinkedID */
	if (!ADD_VENDOR_CODE(PW_AST_LINKED_ID, record->linked_id)) {
		return -1;
	}
	/* Setting Acct-Session-Id & User-Name attributes for proper generation
	   of Acct-Unique-Session-Id on server side */
	/* Channel */
	if (!rc_avpair_add(rh, send, PW_USER_NAME, &record->channel_name,
			strlen(record->channel_name), 0)) {
		return -1;
	}
	return 0;
}

static void radius_log(struct ast_event *event)
{
	int result = ERROR_RC;
	VALUE_PAIR *send = NULL;
	struct ast_cel_event_record record = {
		.version = AST_CEL_EVENT_RECORD_VERSION,
	};

	if (ast_cel_fill_record(event, &record)) {
		return;
	}

	if (build_radius_record(&send, &record)) {
		ast_debug(1, "Unable to create RADIUS record. CEL not recorded!\n");
		goto return_cleanup;
	}

	result = rc_acct(rh, 0, send);
	if (result != OK_RC) {
		ast_log(LOG_ERROR, "Failed to record Radius CEL record!\n");
	}

return_cleanup:
	if (send) {
		rc_avpair_free(send);
	}
}

static int unload_module(void)
{
	ast_cel_backend_unregister(RADIUS_BACKEND_NAME);
	if (rh) {
		rc_destroy(rh);
		rh = NULL;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

static int load_module(void)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { 0 };
	const char *tmp;

	if ((cfg = ast_config_load(cel_config, config_flags))) {
		ast_set2_flag(&global_flags, ast_true(ast_variable_retrieve(cfg, "radius", "usegmtime")), RADIUS_FLAG_USEGMTIME);
		if ((tmp = ast_variable_retrieve(cfg, "radius", "radiuscfg"))) {
			ast_copy_string(radiuscfg, tmp, sizeof(radiuscfg));
		}
		ast_config_destroy(cfg);
	} else {
		return AST_MODULE_LOAD_DECLINE;
	}

	/*
	 * start logging
	 *
	 * NOTE: Yes this causes a slight memory leak if the module is
	 * unloaded.  However, it is better than a crash if cdr_radius
	 * and cel_radius are both loaded.
	 */
	tmp = ast_strdup("asterisk");
	if (tmp) {
		rc_openlog((char *) tmp);
	}

	/* read radiusclient-ng config file */
	if (!(rh = rc_read_config(radiuscfg))) {
		ast_log(LOG_NOTICE, "Cannot load radiusclient-ng configuration file %s.\n", radiuscfg);
		return AST_MODULE_LOAD_DECLINE;
	}

	/* read radiusclient-ng dictionaries */
	if (rc_read_dictionary(rh, rc_conf_str(rh, "dictionary"))) {
		ast_log(LOG_NOTICE, "Cannot load radiusclient-ng dictionary file.\n");
		rc_destroy(rh);
		rh = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_cel_backend_register(RADIUS_BACKEND_NAME, radius_log)) {
		rc_destroy(rh);
		rh = NULL;
		return AST_MODULE_LOAD_DECLINE;
	} else {
		return AST_MODULE_LOAD_SUCCESS;
	}
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "RADIUS CEL Backend",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CDR_DRIVER,
);
