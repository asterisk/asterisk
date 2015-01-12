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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/utils.h"
#include "asterisk/syslog.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <syslog.h>

static const struct {
	const char *name;
	int value;
} facility_map[] = {
	/* POSIX only specifies USER and LOCAL0 - LOCAL7 */
	{ "user",     LOG_USER     },
	{ "local0",   LOG_LOCAL0   },
	{ "local1",   LOG_LOCAL1   },
	{ "local2",   LOG_LOCAL2   },
	{ "local3",   LOG_LOCAL3   },
	{ "local4",   LOG_LOCAL4   },
	{ "local5",   LOG_LOCAL5   },
	{ "local6",   LOG_LOCAL6   },
	{ "local7",   LOG_LOCAL7   },
#if defined(HAVE_SYSLOG_FACILITY_LOG_KERN)
	{ "kern",     LOG_KERN     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_MAIL)
	{ "mail",     LOG_MAIL     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_DAEMON)
	{ "daemon",   LOG_DAEMON   },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_AUTH)
	{ "auth",     LOG_AUTH     },
	{ "security", LOG_AUTH     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_AUTHPRIV)
	{ "authpriv", LOG_AUTHPRIV },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_SYSLOG)
	{ "syslog",   LOG_SYSLOG   },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_FTP)
	{ "ftp",      LOG_FTP      },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_LPR)
	{ "lpr",      LOG_LPR      },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_NEWS)
	{ "news",     LOG_NEWS     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_UUCP)
	{ "uucp",     LOG_UUCP     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_CRON)
	{ "cron",     LOG_CRON     },
#endif
};

int ast_syslog_facility(const char *facility)
{
	int index;

	for (index = 0; index < ARRAY_LEN(facility_map); index++) {
		if (!strcasecmp(facility_map[index].name, facility)) {
			return facility_map[index].value;
		}
	}

	return -1;
}

const char *ast_syslog_facility_name(int facility)
{
	int index;

	for (index = 0; index < ARRAY_LEN(facility_map); index++) {
		if (facility_map[index].value == facility) {
			return facility_map[index].name;
		}
	}

	return NULL;
}

static const struct {
	const char *name;
	int value;
} priority_map[] = {
	{ "alert",   LOG_ALERT   },
	{ "crit",    LOG_CRIT    },
	{ "debug",   LOG_DEBUG   },
	{ "emerg",   LOG_EMERG   },
	{ "err",     LOG_ERR     },
	{ "error",   LOG_ERR     },
	{ "info",    LOG_INFO    },
	{ "notice",  LOG_NOTICE  },
	{ "warning", LOG_WARNING },
};

int ast_syslog_priority(const char *priority)
{
	int index;

	for (index = 0; index < ARRAY_LEN(priority_map); index++) {
		if (!strcasecmp(priority_map[index].name, priority)) {
			return priority_map[index].value;
		}
	}

	return -1;
}

const char *ast_syslog_priority_name(int priority)
{
	int index;

	for (index = 0; index < ARRAY_LEN(priority_map); index++) {
		if (priority_map[index].value == priority) {
			return priority_map[index].name;
		}
	}

	return NULL;
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
	/* First 16 levels are reserved for system use.
	 * Default to using LOG_NOTICE for dynamic logging.
	 */
	if (level >= 16 && level < ASTNUMLOGLEVELS) {
		return LOG_NOTICE;
	}

	if (level < 0 || level >= ARRAY_LEN(logger_level_to_syslog_map)) {
		return -1;
	}

	return logger_level_to_syslog_map[level];
}
