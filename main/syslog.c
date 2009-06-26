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

/*! \file
 *
 * \brief Asterisk Syslog Utility Functions
 * \author Sean Bright <sean@malleable.com>
 */

#include "asterisk.h"
#include "asterisk/utils.h"
#include "asterisk/syslog.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <syslog.h>

int ast_syslog_facility(const char *facility)
{
	if (!strcasecmp(facility, "KERN")) {
		return LOG_KERN;
	} else if (!strcasecmp(facility, "USER")) {
		return LOG_USER;
	} else if (!strcasecmp(facility, "MAIL")) {
		return LOG_MAIL;
	} else if (!strcasecmp(facility, "DAEMON")) {
		return LOG_DAEMON;
	} else if (!strcasecmp(facility, "AUTH")) {
		return LOG_AUTH;
	} else if (!strcasecmp(facility, "AUTHPRIV")) {
		return LOG_AUTHPRIV;
	} else if (!strcasecmp(facility, "SYSLOG")) {
		return LOG_SYSLOG;
	} else if (!strcasecmp(facility, "SECURITY")) {
		return LOG_AUTH;
	} else if (!strcasecmp(facility, "FTP")) {
		return LOG_FTP;
	} else if (!strcasecmp(facility, "LPR")) {
		return LOG_LPR;
	} else if (!strcasecmp(facility, "NEWS")) {
		return LOG_NEWS;
	} else if (!strcasecmp(facility, "UUCP")) {
		return LOG_UUCP;
	} else if (!strcasecmp(facility, "CRON")) {
		return LOG_CRON;
	} else if (!strcasecmp(facility, "LOCAL0")) {
		return LOG_LOCAL0;
	} else if (!strcasecmp(facility, "LOCAL1")) {
		return LOG_LOCAL1;
	} else if (!strcasecmp(facility, "LOCAL2")) {
		return LOG_LOCAL2;
	} else if (!strcasecmp(facility, "LOCAL3")) {
		return LOG_LOCAL3;
	} else if (!strcasecmp(facility, "LOCAL4")) {
		return LOG_LOCAL4;
	} else if (!strcasecmp(facility, "LOCAL5")) {
		return LOG_LOCAL5;
	} else if (!strcasecmp(facility, "LOCAL6")) {
		return LOG_LOCAL6;
	} else if (!strcasecmp(facility, "LOCAL7")) {
		return LOG_LOCAL7;
	}

	return -1;
}

int ast_syslog_priority(const char *priority)
{
	if (!strcasecmp(priority, "ALERT")) {
		return LOG_ALERT;
	} else if (!strcasecmp(priority, "CRIT")) {
		return LOG_CRIT;
	} else if (!strcasecmp(priority, "DEBUG")) {
		return LOG_DEBUG;
	} else if (!strcasecmp(priority, "EMERG")) {
		return LOG_EMERG;
	} else if (!strcasecmp(priority, "ERR")) {
		return LOG_ERR;
	} else if (!strcasecmp(priority, "INFO")) {
		return LOG_INFO;
	} else if (!strcasecmp(priority, "NOTICE")) {
		return LOG_NOTICE;
	} else if (!strcasecmp(priority, "WARNING")) {
		return LOG_WARNING;
	}

	return -1;
}

static const int logger_level_to_syslog_map[] = {
	[__LOG_DEBUG]   = LOG_DEBUG,
	[1]             = LOG_INFO, /* Only kept for backwards compatibility */
	[__LOG_NOTICE]  = LOG_NOTICE,
	[__LOG_WARNING] = LOG_WARNING,
	[__LOG_ERROR]   = LOG_ERR,
	[__LOG_VERBOSE] = LOG_DEBUG,
	[__LOG_DTMF]    = LOG_DEBUG,
};

int ast_syslog_priority_from_loglevel(int level)
{
	if (level < 0 || level >= ARRAY_LEN(logger_level_to_syslog_map)) {
		return -1;
	}
	return logger_level_to_syslog_map[level];
}
