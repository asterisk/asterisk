/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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
 * \file
 * \brief sip_route header file
 */

#ifndef _SIP_ROUTE_H
#define _SIP_ROUTE_H

#include "asterisk/linkedlists.h"
#include "asterisk/strings.h"

/*!
 * \brief Opaque storage of a sip route hop
 */
struct sip_route_hop;

/*!
 * \internal \brief Internal enum to remember last calculated
 */
enum sip_route_type {
	route_loose = 0,    /*!< The first hop contains ;lr or does not exist */
	route_strict,       /*!< The first hop exists and does not contain ;lr */
	route_invalidated,  /*!< strict/loose routing needs to be rechecked */
};

/*!
 * \brief Structure to store route information
 *
 * \note This must be zero-filled on allocation
 */
struct sip_route {
	AST_LIST_HEAD_NOLOCK(, sip_route_hop) list;
	enum sip_route_type type;
};

/*!
 * \brief Add a new hop to the route
 *
 * \param route Route
 * \param uri Address of this hop
 * \param len Length of hop not including null terminator
 * \param inserthead If true then inserted the new route to the top of the list
 *
 * \retval Pointer to null terminated copy of URI on success
 * \retval NULL on error
 */
const char *sip_route_add(struct sip_route *route, const char *uri, size_t len, int inserthead);

/*!
 * \brief Add routes from header
 *
 * \note This procedure is for headers that require use of <brackets>.
 */
void sip_route_process_header(struct sip_route *route, const char *header, int inserthead);

/*!
 * \brief copy route-set
 *
 * \retval non-zero on failure
 * \retval 0 on success
 */
void sip_route_copy(struct sip_route *dst, const struct sip_route *src);

/*!
 * \brief Free all routes in the list
 */
void sip_route_clear(struct sip_route *route);

/*!
 * \brief Verbose dump of all hops for debugging
 */
void sip_route_dump(const struct sip_route *route);

/*!
 * \brief Make the comma separated list of route hops
 *
 * \param route Source of route list
 * \param formatcli Add's space after comma's, print's N/A if list is empty.
 * \param skip Number of hops to skip
 *
 * \retval an allocated struct ast_str on success
 * \retval NULL on failure
 */
struct ast_str *sip_route_list(const struct sip_route *route, int formatcli, int skip)
	__attribute__((__malloc__)) __attribute__((__warn_unused_result__));

/*!
 * \brief Check if the route is strict
 *
 * \note The result is cached in route->type
 */
int sip_route_is_strict(struct sip_route *route);

/*!
 * \brief Get the URI of the route's first hop
 */
const char *sip_route_first_uri(const struct sip_route *route);

/*!
 * \brief Check if route has no URI's
 */
#define sip_route_empty(route) AST_LIST_EMPTY(&(route)->list)

#endif
