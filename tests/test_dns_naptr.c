/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Mark Michelson
 *
 * Mark Michelson <mmichelson@digium.com>
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

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <arpa/nameser.h>

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/dns_core.h"
#include "asterisk/dns_resolver.h"
#include "asterisk/dns_naptr.h"
#include "asterisk/dns_test.h"

struct naptr_record {
	uint16_t order;
	uint16_t preference;
	struct ast_dns_test_string flags;
	struct ast_dns_test_string services;
	struct ast_dns_test_string regexp;
	const char *replacement;
};

/*!
 * \brief Given a NAPTR record, generate a binary form, as would appear in DNS RDATA
 *
 * This is part of a DNS answer, specific to NAPTR. It consists of all parts of
 * the NAPTR record, encoded as it should be in a DNS record.
 *
 * There is no buffer size passed to this function since we provide
 * the data ourselves and have sized the buffer to be way larger
 * than necessary for the tests.
 *
 * \param string The NAPTR record to encode
 * \param buf The buffer to write the record into
 * \return The number of bytes written to the buffer
 */
static int generate_naptr_record(void *dns_record, char *buf)
{
	struct naptr_record *record = dns_record;
	uint16_t net_order = htons(record->order);
	uint16_t net_preference = htons(record->preference);
	char *ptr = buf;

	memcpy(ptr, &net_order, sizeof(net_order));
	ptr += sizeof(net_order);

	memcpy(ptr, &net_preference, sizeof(net_preference));
	ptr += sizeof(net_preference);

	ptr += ast_dns_test_write_string(&record->flags, ptr);
	ptr += ast_dns_test_write_string(&record->services, ptr);
	ptr += ast_dns_test_write_string(&record->regexp, ptr);
	ptr += ast_dns_test_write_domain(record->replacement, ptr);

	return ptr - buf;
}

/*!
 * \brief A pointer to an array of records for a test
 *
 * Each test is expected to set this pointer to its local
 * array of records and then re-set tis pointer to NULL
 * at the end of the test
 */
static struct naptr_record *test_records;
/*!
 * \brief The number of records in the test_records array.
 *
 * Each test must set this to the appropriate value at the
 * beginning of the test and must set this back to zero at
 * the end of the test.
 */
static int num_test_records;
/*!
 * \brief A buffer to place raw DNS records into.
 *
 * This buffer is way larger than any DNS records we actually
 * wish to create during any of the tests, but that's fine.
 */
static char ans_buffer[1024];

/*!
 * \brief Asynchronous NAPTR resolution thread.
 *
 * This builds an appropriate DNS response based on the NAPTR
 * records for a given test. Once the records have been created,
 * the records are added to the DNS result
 */
static void *naptr_thread(void *dns_query)
{
	struct ast_dns_query *query = dns_query;
	int i;
	int ans_size;

	ans_size = ast_dns_test_generate_result(query, test_records, num_test_records,
			sizeof(struct naptr_record), generate_naptr_record, ans_buffer);

	ast_dns_resolver_set_result(query, 0, 0, ns_r_noerror, "goose.feathers", ans_buffer, ans_size);

	for (i = 0; i < num_test_records; ++i) {
		char record[128];
		int naptr_size;

		naptr_size = generate_naptr_record(&test_records[i], record);
		ast_dns_resolver_add_record(query, ns_t_naptr, ns_c_in, 12345, record, naptr_size);
	}

	ast_dns_resolver_completed(query);

	ao2_ref(query, -1);
	return NULL;
}

/*!
 * \brief Mock NAPTR resolution method.
 *
 * This spawns a thread to handle generation of the necessary NAPTR records
 */
static int naptr_resolve(struct ast_dns_query *query)
{
	pthread_t thread;

	return ast_pthread_create_detached(&thread, NULL, naptr_thread, ao2_bump(query));
}

/*!
 * \brief A STUB
 */
static int naptr_cancel(struct ast_dns_query *query)
{
	return 0;
}

/*!
 * \brief Mock NAPTR resolver
 */
static struct ast_dns_resolver naptr_resolver = {
	.name = "naptr_test",
	.priority = 0,
	.resolve = naptr_resolve,
	.cancel = naptr_cancel,
};

AST_TEST_DEFINE(naptr_resolve_nominal)
{
	RAII_VAR(struct ast_dns_result *, result, NULL, ast_dns_result_free);
	const struct ast_dns_record *record;
	struct naptr_record records[] = {
		/* Incredibly plain record */
		{ 200, 100, {1, "A"}, {4, "BLAH"}, {0, ""}, "goose.down" },
		/* Records with valid but unusual flags */
		{ 300,   8, {0, ""}, {4, "BLAH"}, {0, ""}, "goose.down" },
		{ 300,   6, {1, "3"}, {4, "BLAH"}, {0, ""}, "goose.down" },
		{ 100,   2, {2, "32"}, {4, "BLAH"}, {0, ""}, "goose.down" },
		{ 400, 100, {3, "A32"}, {4, "BLAH"}, {0, ""}, "goose.down" },
		/* Records with valid but unusual services */
		{ 100, 700, {0, ""}, {0, ""}, {0, ""}, "goose.down" },
		{ 500, 102, {1, "A"}, {42, "A+B12+C+D+EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE"}, {0, ""}, "goose.down" },
		{ 500, 100, {1, "A"}, {14, "A+B12+C+D+EEEE"}, {0, ""}, "goose.down" },
		/* Records with valid regexes (regexes are always unusual) */
		{ 500, 101, {1, "A"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, "" },
		{ 500,  99, {1, "A"}, {4, "BLAH"}, {15, "0.*0horse.mane0"}, "" },
		{  10, 100, {1, "A"}, {4, "BLAH"}, {11, "!.*!\\!\\!\\!!"}, "" },
		{ 700, 999, {1, "A"}, {4, "BLAH"}, {30, "!(.)(.)(.)(.)!\\1.m.\\2.n\\3.o\\4!"}, "" },
	};

	int naptr_record_order[] = { 10, 3, 5, 0, 2, 1, 4, 9, 7, 8, 6, 11};
	enum ast_test_result_state res = AST_TEST_PASS;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "naptr_resolve";
		info->category = "/main/dns/naptr/";
		info->summary = "Test nominal resolution of NAPTR records";
		info->description = "This test defines four valid NAPTR records and\n"
			"performs a resolution of the domain to which they belong. The test\n"
			"ensures that all fields of the NAPTR records are parsed correctly\n"
			"and that the records are returned in sorted order";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	test_records = records;
	num_test_records = ARRAY_LEN(records);
	memset(ans_buffer, 0, sizeof(ans_buffer));

	ast_dns_resolver_register(&naptr_resolver);

	if (ast_dns_resolve("goose.feathers", ns_t_naptr, ns_c_in, &result)) {
		ast_test_status_update(test, "DNS resolution failed\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	if (!result) {
		ast_test_status_update(test, "DNS resolution returned no result\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	i = 0;
	for (record = ast_dns_result_get_records(result); record; record = ast_dns_record_get_next(record)) {
		if (ast_dns_naptr_get_order(record) != records[naptr_record_order[i]].order) {
			ast_test_status_update(test, "Expected order %hu, got order %hu from NAPTR record\n",
					records[naptr_record_order[i]].order, ast_dns_naptr_get_order(record));
			res = AST_TEST_FAIL;
		}
		if (ast_dns_naptr_get_preference(record) != records[naptr_record_order[i]].preference) {
			ast_test_status_update(test, "Expected preference %hu, got preference %hu from NAPTR record\n",
					records[naptr_record_order[i]].preference, ast_dns_naptr_get_preference(record));
			res = AST_TEST_FAIL;
		}
		if (strcmp(ast_dns_naptr_get_flags(record), records[naptr_record_order[i]].flags.val)) {
			ast_test_status_update(test, "Expected flags %s, got flags %s from NAPTR record\n",
					records[naptr_record_order[i]].flags.val, ast_dns_naptr_get_flags(record));
			res = AST_TEST_FAIL;
		}
		if (strcmp(ast_dns_naptr_get_service(record), records[naptr_record_order[i]].services.val)) {
			ast_test_status_update(test, "Expected services %s, got services %s from NAPTR record\n",
					records[naptr_record_order[i]].services.val, ast_dns_naptr_get_service(record));
			res = AST_TEST_FAIL;
		}
		if (strcmp(ast_dns_naptr_get_regexp(record), records[naptr_record_order[i]].regexp.val)) {
			ast_test_status_update(test, "Expected regexp %s, got regexp %s from NAPTR record\n",
					records[naptr_record_order[i]].regexp.val, ast_dns_naptr_get_regexp(record));
			res = AST_TEST_FAIL;
		}
		if (strcmp(ast_dns_naptr_get_replacement(record), records[naptr_record_order[i]].replacement)) {
			ast_test_status_update(test, "Expected replacement %s, got replacement %s from NAPTR record\n",
					records[naptr_record_order[i]].replacement, ast_dns_naptr_get_replacement(record));
			res = AST_TEST_FAIL;
		}
		++i;
	}

	if (i != ARRAY_LEN(records)) {
		ast_test_status_update(test, "Unexpected number of records returned in NAPTR lookup\n");
		res = AST_TEST_FAIL;
	}

cleanup:

	ast_dns_resolver_unregister(&naptr_resolver);

	test_records = NULL;
	num_test_records = 0;
	memset(ans_buffer, 0, sizeof(ans_buffer));

	return res;
}

static enum ast_test_result_state off_nominal_test(struct ast_test *test, struct naptr_record *records, int num_records)
{
	RAII_VAR(struct ast_dns_result *, result, NULL, ast_dns_result_free);
	enum ast_test_result_state res = AST_TEST_PASS;
	const struct ast_dns_record *record;

	test_records = records;
	num_test_records = num_records;
	memset(ans_buffer, 0, sizeof(ans_buffer));

	ast_dns_resolver_register(&naptr_resolver);

	if (ast_dns_resolve("goose.feathers", ns_t_naptr, ns_c_in, &result)) {
		ast_test_status_update(test, "Failed to perform DNS resolution, despite using valid inputs\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	if (!result) {
		ast_test_status_update(test, "Synchronous DNS resolution failed to set a result\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	record = ast_dns_result_get_records(result);
	if (record) {
		ast_test_status_update(test, "DNS resolution returned records when it was not expected to\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

cleanup:
	ast_dns_resolver_unregister(&naptr_resolver);

	test_records = NULL;
	num_test_records = 0;
	memset(ans_buffer, 0, sizeof(ans_buffer));

	return res;
}

AST_TEST_DEFINE(naptr_resolve_off_nominal_length)
{
	struct naptr_record records[] = {
		{ 100, 100, {255, "A"}, {4, "BLAH"},   {15, "!.*!horse.mane!"}, "" },
		{ 100, 100, {0, "A"},   {4, "BLAH"},   {15, "!.*!horse.mane!"}, "" },
		{ 100, 100, {1, "A"},   {255, "BLAH"}, {15, "!.*!horse.mane!"}, "" },
		{ 100, 100, {1, "A"},   {2, "BLAH"},   {15, "!.*!horse.mane!"}, "" },
		{ 100, 100, {1, "A"},   {4, "BLAH"},   {255, "!.*!horse.mane!"}, "" },
		{ 100, 100, {1, "A"},   {4, "BLAH"},   {3, "!.*!horse.mane!"}, "" },
		{ 100, 100, {255, "A"}, {255, "BLAH"}, {255, "!.*!horse.mane!"}, "" },
		{ 100, 100, {0, "A"},   {2, "BLAH"},   {3, "!.*!horse.mane!"}, "" },
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "naptr_resolve_off_nominal_length";
		info->category = "/main/dns/naptr/";
		info->summary = "Test resolution of NAPTR records with off-nominal lengths";
		info->description = "This test defines a set of records where the strings provided\n"
			"within the record are valid, but the lengths of the strings in the record are\n"
			"invalid, either too large or too small. The goal of this test is to ensure that\n"
			"these invalid lengths result in resolution failures";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return off_nominal_test(test, records, ARRAY_LEN(records));
}

AST_TEST_DEFINE(naptr_resolve_off_nominal_flags)
{
	struct naptr_record records[] = {
		/* Non-alphanumeric flag */
		{ 100, 100, {1, "!"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		/* Mix of valid and non-alphanumeric */
		{ 100, 100, {2, "A!"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "!A"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		/* Invalid combinations of flags */
		{ 100, 100, {2, "sa"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "su"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "sp"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "as"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "au"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "ap"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "ua"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "us"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "up"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "pa"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "ps"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {2, "pu"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, ""},
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "naptr_resolve_off_nominal_flags";
		info->category = "/main/dns/naptr/";
		info->summary = "Ensure that NAPTR records with invalid flags are not presented in results";
		info->description = "This test defines a set of records where the flags provided are\n"
			"invalid in some way. This may be due to providing non-alphanumeric characters or\n"
			"by providing clashing flags. The result should be that none of the defined records\n"
			"are returned by the resolver";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return off_nominal_test(test, records, ARRAY_LEN(records));
}

AST_TEST_DEFINE(naptr_resolve_off_nominal_services)
{
	struct naptr_record records[] = {
		{ 100, 100, {1, "A"}, {5, "BLAH!"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {1, "A"}, {5, "BL!AH"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {1, "A"}, {8, "1SIP+D2U"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {1, "A"}, {8, "SIP+1D2U"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {1, "A"}, {4, "+D2U"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {1, "A"}, {4, "SIP+"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {1, "A"}, {8, "SIP++D2U"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {1, "A"}, {37, "SIPSIPSIPSIPSIPSIPSIPSIPSIPSIPSIP+D2U"}, {15, "!.*!horse.mane!"}, ""},
		{ 100, 100, {1, "A"}, {37, "SIP+D2UD2UD2UD2UD2UD2UD2UD2UD2UD2UD2U"}, {15, "!.*!horse.mane!"}, ""},
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "naptr_resolve_off_nominal_services";
		info->category = "/main/dns/naptr/";
		info->summary = "Ensure that NAPTR records with invalid services are not presented in results";
		info->description = "This test defines a set of records where the services provided are\n"
			"invalid in some way. This may be due to providing non-alphanumeric characters, providing\n"
			"protocols or resolution services that start with a non-alphabetic character, or\n"
			"providing fields that are too long.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return off_nominal_test(test, records, ARRAY_LEN(records));
}

AST_TEST_DEFINE(naptr_resolve_off_nominal_regexp)
{
	struct naptr_record records[] = {
		/* Invalid delim-char */
		{ 100, 100, {1, "A"}, {4, "BLAH"}, {15, "1.*1horse.mane1"}, ""},
		/* Not enough delim-chars */
		{ 100, 100, {1, "A"}, {4, "BLAH"}, {14, "!.*!horse.mane"}, ""},
		/* Not enough delim-chars, part 2 */
		{ 100, 100, {1, "A"}, {4, "BLAH"}, {16, "!.*!horse.mane\\!"}, ""},
		/* Too many delim-chars */
		{ 100, 100, {1, "A"}, {4, "BLAH"}, {15, "!.*!horse!mane!"}, ""},
		/* Invalid regex flag */
		{ 100, 100, {1, "A"}, {4, "BLAH"}, {16, "!.*!horse.mane!o"}, ""},
		/* Invalid backreference */
		{ 100, 100, {1, "A"}, {4, "BLAH"}, {13, "!.*!horse.\\0!"}, ""},
		/* Invalid regex */
		{ 100, 100, {1, "A"}, {4, "BLAH"}, {16, "!(.*!horse.mane!"}, ""},
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "naptr_resolve_off_nominal_regexp";
		info->category = "/main/dns/naptr/";
		info->summary = "Ensure that NAPTR records with invalid regexps are not presented in results";
		info->description = "This test defines a set of records where the regexps provided are\n"
			"invalid in some way. The test ensures that none of the invalid records are returned\n"
			"when performing a NAPTR lookup";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return off_nominal_test(test, records, ARRAY_LEN(records));
}

AST_TEST_DEFINE(naptr_resolve_off_nominal_interactions)
{
	struct naptr_record records[] = {
		/* Both regexp and replacement are specified */
		{ 100, 100, {1, "A"}, {4, "BLAH"}, {15, "!.*!horse.mane!"}, "goose.down"},
		/* XXX RFC 2915 says that a service MUST be present if terminal flags are
		 * specified. However, RFCs 3401-3404 do not specify this behavior, so
		 * I am not putting in a test for it
		 */
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "naptr_resolve_off_nominal_interactions";
		info->category = "/main/dns/naptr/";
		info->summary = "Ensure that NAPTR records with invalid interactions are not presented in results";
		info->description = "This test defines a set of records where all parts are individually valid,\n"
			"but when combined do not make sense and are thus invalid.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return off_nominal_test(test, records, ARRAY_LEN(records));
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(naptr_resolve_nominal);
	AST_TEST_UNREGISTER(naptr_resolve_off_nominal_length);
	AST_TEST_UNREGISTER(naptr_resolve_off_nominal_flags);
	AST_TEST_UNREGISTER(naptr_resolve_off_nominal_services);
	AST_TEST_UNREGISTER(naptr_resolve_off_nominal_regexp);
	AST_TEST_UNREGISTER(naptr_resolve_off_nominal_interactions);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(naptr_resolve_nominal);
	AST_TEST_REGISTER(naptr_resolve_off_nominal_length);
	AST_TEST_REGISTER(naptr_resolve_off_nominal_flags);
	AST_TEST_REGISTER(naptr_resolve_off_nominal_services);
	AST_TEST_REGISTER(naptr_resolve_off_nominal_regexp);
	AST_TEST_REGISTER(naptr_resolve_off_nominal_interactions);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "DNS API Tests");
