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
 * \brief DNS TLSA Record Parsing API
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _ASTERISK_DNS_TLSA_H
#define _ASTERISK_DNS_TLSA_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*!
 * \brief Get the certificate usage field from a TLSA record
 *
 * \param record The DNS record
 *
 * \return the certificate usage field
 */

unsigned int ast_dns_tlsa_get_usage(const struct ast_dns_record *record);

/*!
 * \brief Get the selector field from a TLSA record
 *
 * \param record The DNS record
 *
 * \return the selector field
 */
unsigned int ast_dns_tlsa_get_selector(const struct ast_dns_record *record);

/*!
 * \brief Get the matching type field from a TLSA record
 *
 * \param record The DNS record
 *
 * \return the matching type field
 */
unsigned int ast_dns_tlsa_get_matching_type(const struct ast_dns_record *record);

/*!
 * \brief Get the certificate association data from a TLSA record
 *
 * \param record The DNS record
 *
 * \return the certificate association data
 */
const char *ast_dns_tlsa_get_association_data(const struct ast_dns_record *record);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DNS_TLSA_H */
