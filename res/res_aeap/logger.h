/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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

#ifndef RES_AEAP_LOGGER_H
#define RES_AEAP_LOGGER_H

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/strings.h"

/*!
 * \brief Log an Asterisk external application message
 *
 * \param level The logging level
 * \param obj The object being logged
 * \param name Optional subsystem name
 * \param fmt Format string
 * \param ... Parameters for the format string
 */
#define aeap_log(level, obj, name, fmt, ...) \
	ast_log(level, "AEAP%s%s (%p): " fmt "\n", ast_strlen_zero(name) ? "" : " ", \
			ast_strlen_zero(name) ? "" : name, obj, ##__VA_ARGS__)

/*!
 * \brief Log an Asterisk external application error
 *
 * \param obj The object being logged
 * \param name Optional subsystem name
 * \param fmt Format string
 * \param ... Parameters for the format string
 */
#define aeap_error(obj, name, fmt, ...) aeap_log(LOG_ERROR, obj, name, fmt, ##__VA_ARGS__)

/*!
 * \brief Log an Asterisk external application warning
 *
 * \param obj The object being logged
 * \param name Optional subsystem name
 * \param fmt Format string
 * \param ... Parameters for the format string
 */
#define aeap_warn(obj, name, fmt, ...) aeap_log(LOG_WARNING, obj, name, fmt, ##__VA_ARGS__)

#endif /* RES_AEAP_LOGGER_H */
