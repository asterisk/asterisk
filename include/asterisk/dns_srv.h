/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief DNS SRV Record Parsing API
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _ASTERISK_DNS_SRV_H
#define _ASTERISK_DNS_SRV_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*!
 * \brief Get the hostname from an SRV record
 *
 * \param record The DNS record
 *
 * \return the hostname
 */
const char *ast_dns_srv_get_host(const struct ast_dns_record *record);

/*!
 * \brief Get the priority from an SRV record
 *
 * \param record The DNS record
 *
 * \return the priority
 */
unsigned short ast_dns_srv_get_priority(const struct ast_dns_record *record);

/*!
 * \brief Get the weight from an SRV record
 *
 * \param record The DNS record
 *
 * \return the weight
 */
unsigned short ast_dns_srv_get_weight(const struct ast_dns_record *record);

/*!
 * \brief Get the port from an SRV record
 *
 * \param record The DNS record
 *
 * \return the port
 */
unsigned short ast_dns_srv_get_port(const struct ast_dns_record *record);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DNS_SRV_H */
