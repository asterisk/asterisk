/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sean Bright
 *
 * Sean Bright <sean.bright@gmail.com>
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
 * \brief DNS TXT Record Parsing API
 * \author Sean Bright <sean.bright@gmail.com>
 */

#ifndef ASTERISK_DNS_TXT_H
#define ASTERISK_DNS_TXT_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*!
 * \brief Get the number of character strings in a TXT record
 * \since 16.10.0, 17.4.0
 *
 * \param record The DNS record
 *
 * \return the number of character strings in this TXT record
 */
size_t ast_dns_txt_get_count(const struct ast_dns_record *record);

/*!
 * \brief Get the character strings from this TXT record
 * \since 16.10.0, 17.4.0
 *
 * \param record The DNS record
 *
 * \retval NULL Unable to allocate memory
 * \return Vector of strings. Free with ast_dns_txt_free_strings
 */
struct ast_vector_string *ast_dns_txt_get_strings(const struct ast_dns_record *record);

/*!
 * \brief Free strings returned by ast_dns_txt_get_strings
 * \since 16.10.0, 17.4.0
 *
 * \param strings The vector to free
 */
void ast_dns_txt_free_strings(struct ast_vector_string *strings);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* ASTERISK_DNS_TXT_H */
