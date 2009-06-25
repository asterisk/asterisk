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
 * \file syslog.h
 * \brief Syslog support functions for Asterisk logging.
 */

#ifndef _ASTERISK_SYSLOG_H
#define _ASTERISK_SYSLOG_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*!
 * \since 1.6.3
 * \brief Maps a syslog facility name from a string to a syslog facility
 *        constant.
 *
 * \param facility Facility name to map (i.e. "daemon")
 *
 * \retval syslog facility constant (i.e. LOG_DAEMON) if found
 * \retval -1 if facility is not found
 */
int ast_syslog_facility(const char *facility);

/*!
 * \since 1.6.3
 * \brief Maps a syslog priority name from a string to a syslog priority
 *        constant.
 *
 * \param priority Priority name to map (i.e. "notice")
 *
 * \retval syslog priority constant (i.e. LOG_NOTICE) if found
 * \retval -1 if priority is not found
 */
int ast_syslog_priority(const char *priority);

/*!
 * \since 1.6.3
 * \brief Maps an Asterisk log level (i.e. LOG_ERROR) to a syslog priority
 *        constant.
 *
 * \param level Asterisk log level constant (i.e. LOG_ERROR)
 *
 * \retval syslog priority constant (i.e. LOG_ERR) if found
 * \retval -1 if priority is not found
 */
int ast_syslog_priority_from_loglevel(int level);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_SYSLOG_H */
