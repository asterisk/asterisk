/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2013, Digium, Inc.
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

/*
 * DNS SRV record support
 */

#ifndef _ASTERISK_SRV_H
#define _ASTERISK_SRV_H

/*!
 * \file
 *
 * \brief Support for DNS SRV records, used in to locate SIP services.
 *
 * \note Note: This SRV record support will respect the priority and weight
 *	elements of the records that are returned, but there are no provisions
 * 	for retrying or failover between records.
 */

/*! \brief An opaque type, for lookup usage */
struct srv_context;

/*! \brief Retrieve set of SRV lookups, in order
 *
 * \param[in] context A pointer in which to hold the result
 * \param[in] service The service name to look up
 * \param[out] host Result host
 * \param[out] port Associated TCP portnum
 *
 * \retval -1 Query failed
 * \retval 0 Result exists in host and port
 * \retval 1 No more results
 */
extern int ast_srv_lookup(struct srv_context **context, const char *service, const char **host, unsigned short *port);

/*! \brief Cleanup resources associated with ast_srv_lookup
 *
 * \param context Pointer passed into ast_srv_lookup
 */
void ast_srv_cleanup(struct srv_context **context);

/*! \brief Lookup entry in SRV records Returns 1 if found, 0 if not found, -1 on hangup
 *
 * Only do SRV record lookup if you get a domain without a port. If you get a port #, it's a DNS host name.
 *
 * \param chan Ast channel
 * \param host host name (return value)
 * \param hostlen Length of string "host"
 * \param port Port number (return value)
 * \param service Service tag for SRV lookup (like "_sip._udp" or "_stun._udp"
 */
extern int ast_get_srv(struct ast_channel *chan, char *host, int hostlen, int *port, const char *service);

/*!
 * \brief Get the number of records for a given SRV context
 *
 * \details
 * This is meant to be used after calling ast_srv_lookup, so that
 * one may retrieve the number of records returned during a specific
 * SRV lookup.
 *
 * \param context The context returned by ast_srv_lookup
 *
 * \return Number of records in context
 */
unsigned int ast_srv_get_record_count(struct srv_context *context);

/*!
 * \brief Retrieve details from a specific SRV record
 *
 * \details
 * After calling ast_srv_lookup, the srv_context will contain
 * the data from several records. You can retrieve the data
 * of a specific one by asking for a specific record number. The
 * records are sorted based on priority and secondarily based on
 * weight. See RFC 2782 for the exact sorting rules.
 *
 * \param context The context returned by ast_srv_lookup
 * \param record_num The 1-indexed record number to retrieve
 * \param[out] host The host portion of the record
 * \param[out] port The port portion of the record
 * \param[out] priority The priority portion of the record
 * \param[out] weight The weight portion of the record
 *
 * \retval -1 Failed to retrieve information.
 * 	Likely due to an out of range record_num
 * \retval 0 Success
 */
int ast_srv_get_nth_record(struct srv_context *context, int record_num, const char **host,
		unsigned short *port, unsigned short *priority, unsigned short *weight);
#endif /* _ASTERISK_SRV_H */
