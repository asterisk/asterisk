/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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

#ifndef _ASTERISK_URI_H
#define _ASTERISK_URI_H

/*! \brief Opaque structure that stores uri information. */
struct ast_uri;

/*!
 * \brief Create a uri with the given parameters
 *
 * \param scheme the uri scheme (ex: http)
 * \param user_info user credentials (ex: \<name\>\@\<pass\>)
 * \param host host name or ip address
 * \param port the port
 * \param path the path
 * \param query query parameters
 * \return a structure containing parsed uri data.
 * \retval NULL on error
 * \since 13
 */
struct ast_uri *ast_uri_create(const char *scheme, const char *user_info,
			       const char *host, const char *port,
			       const char *path, const char *query);

/*!
 * \brief Copy the given uri replacing any value in the new uri with
 *        any given.
 *
 * \param uri the uri object to copy
 * \param scheme the uri scheme (ex: http)
 * \param user_info user credentials (ex: \<name\>\@\<pass\>)
 * \param host host name or ip address
 * \param port the port
 * \param path the path
 * \param query query parameters
 * \return a copy of the given uri with specified values replaced.
 * \retval NULL on error
 * \since 13
 */
struct ast_uri *ast_uri_copy_replace(const struct ast_uri *uri, const char *scheme,
				     const char *user_info, const char *host,
				     const char *port, const char *path,
				     const char *query);
/*!
 * \brief Retrieve the uri scheme.
 *
 * \return the uri scheme.
 * \since 13
 */
const char *ast_uri_scheme(const struct ast_uri *uri);

/*!
 * \brief Retrieve the uri user information.
 *
 * \return the uri user information.
 * \since 13
 */
const char *ast_uri_user_info(const struct ast_uri *uri);

/*!
 * \brief Retrieve the uri host.
 *
 * \return the uri host.
 * \since 13
 */
const char *ast_uri_host(const struct ast_uri *uri);

/*!
 * \brief Retrieve the uri port
 *
 * \return the uri port.
 * \since 13
 */
const char *ast_uri_port(const struct ast_uri *uri);

/*!
 * \brief Retrieve the uri path.
 *
 * \return the uri path.
 * \since 13
 */
const char *ast_uri_path(const struct ast_uri *uri);

/*!
 * \brief Retrieve the uri query parameters.
 *
 * \return the uri query parameters.
 * \since 13
 */
const char *ast_uri_query(const struct ast_uri *uri);

/*!
 * \brief Retrieve if the uri is of a secure type
 *
 * \note Secure types are recognized by an 's' at the end
 *       of the scheme.
 *
 * \retval True if secure.
 * \retval False otherwise.
 * \since 13
 */
int attribute_pure ast_uri_is_secure(const struct ast_uri *uri);

/*!
 * \brief Parse the given uri into a structure.
 *
 * \note Expects the following form:
 * \verbatim <scheme>://[user:pass@]<host>[:port][/<path>] \endverbatim
 *
 * \param uri a string uri to parse
 * \return a structure containing parsed uri data.
 * \retval NULL on error
 * \since 13
 */
struct ast_uri *ast_uri_parse(const char *uri);

/*!
 * \brief Parse the given http uri into a structure.
 *
 * \note Expects the following form:
 * \verbatim [http[s]://][user:pass@]<host>[:port][/<path>] \endverbatim
 *
 * \note If no scheme is given it defaults to 'http' and if
 *       no port is specified it will default to 443 if marked
 *       secure, otherwise to 80.
 *
 * \param uri an http string uri to parse
 * \return a structure containing parsed http uri data.
 * \retval NULL on error
 * \since 13
 */
struct ast_uri *ast_uri_parse_http(const char *uri);

/*!
 * \brief Parse the given websocket uri into a structure.
 *
 * \note Expects the following form:
 *           [ws[s]://][user:pass@]<host>[:port][/<path>]
 *
 * \note If no scheme is given it defaults to 'ws' and if
 *       no port is specified it will default to 443 if marked
 *       secure, otherwise to 80.
 *
 * \param uri a websocket string uri to parse
 * \return a structure containing parsed http uri data.
 * \retval NULL on error
 * \since 13
 */
struct ast_uri *ast_uri_parse_websocket(const char *uri);

/*!
 * \brief Retrieve a string of the host and port.
 *
 * Combine the host and port (\<host\>:\<port\>) if the port
 * is available, otherwise just return the host.
 *
 * \note Caller is responsible for release the returned string.
 *
 * \param uri the uri object
 * \return a string value of the host and optional port.
 * \since 13
 */
char *ast_uri_make_host_with_port(const struct ast_uri *uri);

#endif /* _ASTERISK_URI_H */
