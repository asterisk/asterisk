/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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

#ifndef _RES_PJPROJECT_H
#define _RES_PJPROJECT_H

/*! \brief Determines whether the res_pjproject module is loaded */
#define CHECK_PJPROJECT_MODULE_LOADED()                 \
	do {                                                \
		if (!ast_module_check("res_pjproject.so")) {    \
			return AST_MODULE_LOAD_DECLINE;             \
		}                                               \
	} while(0)

/*!
 * \brief Retrieve a pjproject build option
 *
 * \param option The build option requested
 * \param format_string A scanf-style format string to parse the option value into
 * \param ... Pointers to variables to receive the values parsed
 *
 * \retval The number of values parsed
 *
 * \since 13.8.0
 *
 * \note The option requested must be from those returned by pj_dump_config()
 * which can be displayed with the 'pjsip show buildopts' CLI command.
 *
 *   <b>Sample Usage:</b>
 *   \code
 *
 *   int max_hostname;
 *
 *   ast_sip_get_pjproject_buildopt("PJ_MAX_HOSTNAME", "%d", &max_hostname);
 *
 *   \endcode
 *
 */
int ast_pjproject_get_buildopt(char *option, char *format_string, ...) __attribute__((format(scanf, 2, 3)));

/*!
 * \brief Begin PJPROJECT log interception for CLI output.
 * \since 13.8.0
 *
 * \param fd CLI file descriptior to send intercepted output.
 *
 * \note ast_pjproject_log_intercept_begin() and
 * ast_pjproject_log_intercept_end() must always be called
 * in pairs.
 *
 * \return Nothing
 */
void ast_pjproject_log_intercept_begin(int fd);

/*!
 * \brief End PJPROJECT log interception for CLI output.
 * \since 13.8.0
 *
 * \note ast_pjproject_log_intercept_begin() and
 * ast_pjproject_log_intercept_end() must always be called
 * in pairs.
 *
 * \return Nothing
 */
void ast_pjproject_log_intercept_end(void);

/*!
 * \brief Increment the res_pjproject reference count.
 *
 * This ensures graceful shutdown happens in the proper order.
 */
void ast_pjproject_ref(void);

/*!
 * \brief Decrement the res_pjproject reference count.
 *
 * This ensures graceful shutdown happens in the proper order.
 */
void ast_pjproject_unref(void);

#endif /* _RES_PJPROJECT_H */
