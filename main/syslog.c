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

static const struct {
	const char *name;
	int value;
} facility_map[] = {
	/* POSIX only specifies USER and LOCAL0 - LOCAL7 */
	{ "USER",     LOG_USER     },
	{ "LOCAL0",   LOG_LOCAL0   },
	{ "LOCAL1",   LOG_LOCAL1   },
	{ "LOCAL2",   LOG_LOCAL2   },
	{ "LOCAL3",   LOG_LOCAL3   },
	{ "LOCAL4",   LOG_LOCAL4   },
	{ "LOCAL5",   LOG_LOCAL5   },
	{ "LOCAL6",   LOG_LOCAL6   },
	{ "LOCAL7",   LOG_LOCAL7   },
#if defined(HAVE_SYSLOG_FACILITY_LOG_KERN)
	{ "KERN",     LOG_KERN     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_MAIL)
	{ "MAIL",     LOG_MAIL     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_DAEMON)
	{ "DAEMON",   LOG_DAEMON   },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_AUTH)
	{ "AUTH",     LOG_AUTH     },
	{ "SECURITY", LOG_AUTH     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_AUTHPRIV)
	{ "AUTHPRIV", LOG_AUTHPRIV },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_SYSLOG)
	{ "SYSLOG",   LOG_SYSLOG   },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_FTP)
	{ "FTP",      LOG_FTP      },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_LPR)
	{ "LPR",      LOG_LPR      },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_NEWS)
	{ "NEWS",     LOG_NEWS     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_UUCP)
	{ "UUCP",     LOG_UUCP     },
#endif
#if defined(HAVE_SYSLOG_FACILITY_LOG_CRON)
	{ "CRON",     LOG_CRON     },
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

static const struct {
	const char *name;
	int value;
} priority_map[] = {
	{ "ALERT",   LOG_ALERT   },
	{ "CRIT",    LOG_CRIT    },
	{ "DEBUG",   LOG_DEBUG   },
	{ "EMERG",   LOG_EMERG   },
	{ "ERR",     LOG_ERR     },
	{ "ERROR",   LOG_ERR     },
	{ "INFO",    LOG_INFO    },
	{ "NOTICE",  LOG_NOTICE  },
	{ "WARNING", LOG_WARNING }
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
