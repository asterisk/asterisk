/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
 *
 * Includes code and algorithms from the Zapata library.
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

#ifndef DNS_TEST_H
#define DNS_TEST_H

/*!
 * \brief Representation of a string in DNS
 *
 * In DNS, a string has a byte to indicate the length,
 * followed by a series of bytes representing the string.
 * DNS does not NULL-terminate its strings. However, the
 * string stored in this structure is expected to be NULL-
 * terminated.
 */
struct ast_dns_test_string {
	uint8_t len;
	const char *val;
};

/*!
 * \brief Write a DNS string to a buffer
 *
 * This writes the DNS string to the buffer and returns the total
 * number of bytes written to the buffer.
 *
 * There is no buffer size passed to this function. Tests are expected to
 * use a buffer that is sufficiently large for their tests.
 *
 * \param string The string to write
 * \param buf The buffer to write the string into
 * \return The number of bytes written to the buffer
 */
int ast_dns_test_write_string(const struct ast_dns_test_string *string, char *buf);

/*!
 * \brief Write a DNS domain to a buffer
 *
 * A DNS domain consists of a series of labels separated
 * by dots. Each of these labels gets written as a DNS
 * string. A DNS domain ends with a NULL label, which is
 * essentially a zero-length DNS string.
 *
 * There is no buffer size passed to this function. Tests are expected to
 * use a buffer that is sufficiently large for their tests.
 *
 * \param string The DNS domain to write
 * \param buf The buffer to write the domain into
 * \return The number of bytes written to the buffer
 */
int ast_dns_test_write_domain(const char *string, char *buf);

/*!
 * \brief Callback to write specific DNS record to an answer
 *
 * When generating a DNS result, the type of DNS record being generated
 * will need to be performed by individual test cases. This is a callback
 * that tests can define to write a specific type of DNS record to the
 * provided buffer.
 *
 * There is no buffer size passed to this function. Tests are expected to
 * use a buffer that is sufficiently large for their tests.
 *
 * \param record Pointer to test-specific DNS record data
 * \param buf The buffer into which to write the DNS record
 * \return The number of bytes written to the buffer
 */
typedef int (*record_fn)(void *record, char *buf);

/*!
 * \brief Generate a full DNS response for the given DNS records.
 *
 * This function takes care of generating the DNS header, question, and
 * answer sections of a DNS response. In order to place test-specific
 * record data into the DNS answers, a callback is provided as a parameter
 * to this function so that the necessary records can be encoded properly
 * by the tests.
 *
 * There is no buffer size passed to this function. Tests are expected to
 * use a buffer that is sufficiently large for their tests.
 *
 * \param query The DNS query that is being processed
 * \param records An array of test-specific representations of DNS records
 * \param num_records The number of elements in the records array
 * \param record_size The size of each element in the records array
 * \param generate The test-specific encoder for DNS records
 * \param buffer The buffer into which to write the DNS response
 */
int ast_dns_test_generate_result(struct ast_dns_query *query, void *records, size_t num_records,
		size_t record_size, record_fn generate, char *buffer);

#endif /* DNS_TEST_H */
