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

#include <pj/types.h>
#include <pj/pool.h>

struct ast_sockaddr;

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
 */
void ast_pjproject_log_intercept_begin(int fd);

/*!
 * \brief End PJPROJECT log interception for CLI output.
 * \since 13.8.0
 *
 * \note ast_pjproject_log_intercept_begin() and
 * ast_pjproject_log_intercept_end() must always be called
 * in pairs.
 */
void ast_pjproject_log_intercept_end(void);

/*!
 * \brief Initialize the caching pool factory.
 * \since 13.21.0
 *
 * \param cp Caching pool factory to initialize
 * \param policy Pool factory policy
 * \param max_capacity Total capacity to be retained in the cache.  Zero disables caching.
 */
void ast_pjproject_caching_pool_init(pj_caching_pool *cp,
	const pj_pool_factory_policy *policy, pj_size_t max_capacity);

/*!
 * \brief Destroy caching pool factory and all cached pools.
 * \since 13.21.0
 *
 * \param cp Caching pool factory to destroy
 */
void ast_pjproject_caching_pool_destroy(pj_caching_pool *cp);

/*!
 * \brief Fill a pj_sockaddr from an ast_sockaddr
 * \since 13.24.0
 *
 * \param addr The source address to copy
 * \param pjaddr The target address to receive the copied address
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sockaddr_to_pj_sockaddr(const struct ast_sockaddr *addr, pj_sockaddr *pjaddr);

/*!
 * \brief Fill an ast_sockaddr from a pj_sockaddr
 * \since 13.24.0
 *
 * \param addr The target address to receive the copied address
 * \param pjaddr The source address to copy
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_sockaddr_from_pj_sockaddr(struct ast_sockaddr *addr, const pj_sockaddr *pjaddr);

#endif /* _RES_PJPROJECT_H */
