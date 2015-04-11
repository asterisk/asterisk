/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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

/*!
 * \file
 * \brief ACL unit tests
 *
 * \author Mark Michelson <mmichelson@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/test.h"
#include "asterisk/acl.h"
#include "asterisk/module.h"
#include "asterisk/netsock2.h"
#include "asterisk/config.h"

AST_TEST_DEFINE(invalid_acl)
{
	const char * invalid_acls[] = {
		/* Negative netmask */
		"1.3.3.7/-1",
		/* Netmask too large */
		"1.3.3.7/33",
		/* Netmask waaaay too large */
		"1.3.3.7/92342348927389492307420",
		/* Netmask non-numeric */
		"1.3.3.7/California",
		/* Too many octets in Netmask */
		"1.3.3.7/255.255.255.255.255",
		/* Octets in IP address exceed 255 */
		"57.60.278.900/31",
		/* Octets in IP address exceed 255 and are negative */
		"400.32.201029.-6/24",
		/* Invalidly formatted IP address */
		"EGGSOFDEATH/4000",
		/* Too many octets in IP address */
		"33.4.7.8.3/300030",
		/* Too many octets in Netmask */
		"1.2.3.4/6.7.8.9.0",
		/* Too many octets in IP address */
		"3.1.4.1.5.9/3",
		/* IPv6 address has multiple double colons */
		"ff::ff::ff/3",
		/* IPv6 address is too long */
		"1234:5678:90ab:cdef:1234:5678:90ab:cdef:1234/56",
		/* IPv6 netmask is too large */
		"::ffff/129",
		/* IPv4-mapped IPv6 address has too few octets */
		"::ffff:255.255.255/128",
		/* Leading and trailing colons for IPv6 address */
		":1234:/15",
		/* IPv6 address and IPv4 netmask */
		"fe80::1234/255.255.255.0",
	};

	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_ha *ha = NULL;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "invalid_acl";
		info->category = "/main/acl/";
		info->summary = "Invalid ACL unit test";
		info->description =
			"Ensures that garbage ACL values are not accepted";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(invalid_acls); ++i) {
		int err = 0;
		ha = ast_append_ha("permit", invalid_acls[i], ha, &err);
		if (ha || !err) {
			ast_test_status_update(test, "ACL %s accepted even though it is total garbage.\n",
					invalid_acls[i]);
			if (ha) {
				ast_free_ha(ha);
			}
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

struct acl {
	const char *host;
	const char *access;
};

/* These constants are defined for the sole purpose of being shorter
 * than their real names. It makes lines in this test quite a bit shorter
 */

#define TACL_A AST_SENSE_ALLOW
#define TACL_D AST_SENSE_DENY

static int build_ha(const struct acl *acl, size_t len, struct ast_ha **ha, const char *acl_name, int *err, struct ast_test *test, enum ast_test_result_state *res) 
{
	size_t i;

	for (i = 0; i < len; ++i) {
		if (!(*ha = ast_append_ha(acl[i].access, acl[i].host, *ha, err))) {
			ast_test_status_update(test, "Failed to add rule %s with access %s to %s\n",
					       acl[i].host, acl[i].access, acl_name);
			*res = AST_TEST_FAIL;
			return -1;
		}
	}

	return 0;
}

AST_TEST_DEFINE(acl)
{
	struct acl permitallv4 = { "0.0.0.0/0", "permit" };
	struct acl denyallv4 = { "0.0.0.0/0", "deny" };
	struct acl permitallv6 = { "::/0", "permit" };
	struct acl denyallv6 = { "::/0", "deny" };

	struct acl acl1[] = {
		{ "0.0.0.0/0.0.0.0", "deny" },
		{ "10.0.0.0/255.0.0.0", "permit" },
		{ "192.168.0.0/255.255.255.0", "permit" },
	};

	struct acl acl2[] = {
		{ "10.0.0.0/8", "deny" },
		{ "10.0.0.0/8", "permit" },
		{ "10.0.0.0/16", "deny" },
		{ "10.0.0.0/24", "permit" },
	};

	struct acl acl3[] = {
		{ "::/0", "deny" },
		{ "fe80::/64", "permit" },
	};

	struct acl acl4[] = {
		{ "::/0", "deny" },
		{ "fe80::/64", "permit" },
		{ "fe80::ffff:0:0:0/80", "deny" },
		{ "fe80::ffff:0:ffff:0/112", "permit" },
	};

	struct acl acl5[] = {
		{ "0.0.0.0/0.0.0.0", "deny" },
		{ "10.0.0.0/255.0.0.0,192.168.0.0/255.255.255.0", "permit" },
	};

	struct acl acl6[] = {
		{ "10.0.0.0/8", "deny" },
		{ "10.0.0.0/8", "permit" },
		{ "10.0.0.0/16,!10.0.0.0/24", "deny" },
	};

	struct acl acl7[] = {
		{ "::/0,!fe80::/64", "deny" },
		{ "fe80::ffff:0:0:0/80", "deny" },
		{ "fe80::ffff:0:ffff:0/112", "permit" },
	};

	struct {
		const char *test_address;
		int v4_permitall_result;
		int v4_denyall_result;
		int v6_permitall_result;
		int v6_denyall_result;
		int acl1_result;
		int acl2_result;
		int acl3_result;
		int acl4_result;
		int acl5_result;
		int acl6_result;
		int acl7_result;
	} acl_tests[] = {
		{ "10.1.1.5",                  TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A },
		{ "192.168.0.5",               TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A },
		{ "192.168.1.5",               TACL_A, TACL_D, TACL_A, TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_D, TACL_A, TACL_A },
		{ "10.0.0.1",                  TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A },
		{ "10.0.10.10",                TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_D, TACL_A },
		{ "172.16.0.1",                TACL_A, TACL_D, TACL_A, TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_D, TACL_A, TACL_A },
		{ "fe80::1234",                TACL_A, TACL_A, TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A },
		{ "fe80::ffff:1213:dead:beef", TACL_A, TACL_A, TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_D, TACL_A, TACL_A, TACL_D },
		{ "fe80::ffff:0:ffff:ABCD",    TACL_A, TACL_A, TACL_A, TACL_D, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A, TACL_A },
	};

	struct ast_ha *permit_hav4 = NULL;
	struct ast_ha *deny_hav4 = NULL;
	struct ast_ha *permit_hav6 = NULL;
	struct ast_ha *deny_hav6 = NULL;
	struct ast_ha *ha1 = NULL;
	struct ast_ha *ha2 = NULL;
	struct ast_ha *ha3 = NULL;
	struct ast_ha *ha4 = NULL;
	struct ast_ha *ha5 = NULL;
	struct ast_ha *ha6 = NULL;
	struct ast_ha *ha7 = NULL;
	enum ast_test_result_state res = AST_TEST_PASS;
	int err = 0;
	int i;


	switch (cmd) {
	case TEST_INIT:
		info->name = "acl";
		info->category = "/main/acl/";
		info->summary = "ACL unit test";
		info->description =
			"Tests that hosts are properly permitted or denied";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(permit_hav4 = ast_append_ha(permitallv4.access, permitallv4.host, permit_hav4, &err))) {
		ast_test_status_update(test, "Failed to create permit_all ACL\n");
		res = AST_TEST_FAIL;
		goto acl_cleanup;
	}

	if (!(deny_hav4 = ast_append_ha(denyallv4.access, denyallv4.host, deny_hav4, &err))) {
		ast_test_status_update(test, "Failed to create deny_all ACL\n");
		res = AST_TEST_FAIL;
		goto acl_cleanup;
	}

	if (!(permit_hav6 = ast_append_ha(permitallv6.access, permitallv6.host, permit_hav6, &err))) {
		ast_test_status_update(test, "Failed to create permit_all ACL\n");
		res = AST_TEST_FAIL;
		goto acl_cleanup;
	}

	if (!(deny_hav6 = ast_append_ha(denyallv6.access, denyallv6.host, deny_hav6, &err))) {
		ast_test_status_update(test, "Failed to create deny_all ACL\n");
		res = AST_TEST_FAIL;
		goto acl_cleanup;
	}

	if (build_ha(acl1, ARRAY_LEN(acl1), &ha1, "ha1", &err, test, &res) != 0) {
		goto acl_cleanup;
	}

	if (build_ha(acl2, ARRAY_LEN(acl2), &ha2, "ha2", &err, test, &res) != 0) {
		goto acl_cleanup;
	}

	if (build_ha(acl3, ARRAY_LEN(acl3), &ha3, "ha3", &err, test, &res) != 0) {
		goto acl_cleanup;
	}

	if (build_ha(acl4, ARRAY_LEN(acl4), &ha4, "ha4", &err, test, &res) != 0) {
		goto acl_cleanup;
	}

	if (build_ha(acl5, ARRAY_LEN(acl5), &ha5, "ha5", &err, test, &res) != 0) {
		goto acl_cleanup;
	}

	if (build_ha(acl6, ARRAY_LEN(acl6), &ha6, "ha6", &err, test, &res) != 0) {
		goto acl_cleanup;
	}

	if (build_ha(acl7, ARRAY_LEN(acl7), &ha7, "ha7", &err, test, &res) != 0) {
		goto acl_cleanup;
	}

	for (i = 0; i < ARRAY_LEN(acl_tests); ++i) {
		struct ast_sockaddr addr;
		int permit_resv4;
		int permit_resv6;
		int deny_resv4;
		int deny_resv6;
		int acl1_res;
		int acl2_res;
		int acl3_res;
		int acl4_res;
		int acl5_res;
		int acl6_res;
		int acl7_res;

		ast_sockaddr_parse(&addr, acl_tests[i].test_address, PARSE_PORT_FORBID);

		permit_resv4 = ast_apply_ha(permit_hav4, &addr);
		deny_resv4 = ast_apply_ha(deny_hav4, &addr);
		permit_resv6 = ast_apply_ha(permit_hav6, &addr);
		deny_resv6 = ast_apply_ha(deny_hav6, &addr);
		acl1_res = ast_apply_ha(ha1, &addr);
		acl2_res = ast_apply_ha(ha2, &addr);
		acl3_res = ast_apply_ha(ha3, &addr);
		acl4_res = ast_apply_ha(ha4, &addr);
		acl5_res = ast_apply_ha(ha5, &addr);
		acl6_res = ast_apply_ha(ha6, &addr);
		acl7_res = ast_apply_ha(ha7, &addr);

		if (permit_resv4 != acl_tests[i].v4_permitall_result) {
			ast_test_status_update(test, "Access not as expected to %s on permitallv4. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].v4_permitall_result, permit_resv4);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (deny_resv4 != acl_tests[i].v4_denyall_result) {
			ast_test_status_update(test, "Access not as expected to %s on denyallv4. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].v4_denyall_result, deny_resv4);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (permit_resv6 != acl_tests[i].v6_permitall_result) {
			ast_test_status_update(test, "Access not as expected to %s on permitallv6. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].v6_permitall_result, permit_resv6);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (deny_resv6 != acl_tests[i].v6_denyall_result) {
			ast_test_status_update(test, "Access not as expected to %s on denyallv6. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].v6_denyall_result, deny_resv6);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (acl1_res != acl_tests[i].acl1_result) {
			ast_test_status_update(test, "Access not as expected to %s on acl1. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].acl1_result, acl1_res);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (acl2_res != acl_tests[i].acl2_result) {
			ast_test_status_update(test, "Access not as expected to %s on acl2. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].acl2_result, acl2_res);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (acl3_res != acl_tests[i].acl3_result) {
			ast_test_status_update(test, "Access not as expected to %s on acl3. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].acl3_result, acl3_res);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (acl4_res != acl_tests[i].acl4_result) {
			ast_test_status_update(test, "Access not as expected to %s on acl4. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].acl4_result, acl4_res);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (acl5_res != acl_tests[i].acl5_result) {
			ast_test_status_update(test, "Access not as expected to %s on acl5. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].acl5_result, acl5_res);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (acl6_res != acl_tests[i].acl6_result) {
			ast_test_status_update(test, "Access not as expected to %s on acl6. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].acl6_result, acl6_res);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (acl7_res != acl_tests[i].acl7_result) {
			ast_test_status_update(test, "Access not as expected to %s on acl7. Expected %d but "
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].acl7_result, acl7_res);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}
	}

acl_cleanup:
	if (permit_hav4) {
		ast_free_ha(permit_hav4);
	}
	if (deny_hav4) {
		ast_free_ha(deny_hav4);
	}
	if (permit_hav6) {
		ast_free_ha(permit_hav6);
	}
	if (deny_hav6) {
		ast_free_ha(deny_hav6);
	}
	if (ha1) {
		ast_free_ha(ha1);
	}
	if (ha2) {
		ast_free_ha(ha2);
	}
	if (ha3) {
		ast_free_ha(ha3);
	}
	if (ha4) {
		ast_free_ha(ha4);
	}
	if (ha5) {
		ast_free_ha(ha5);
	}
	if (ha6) {
		ast_free_ha(ha6);
	}
	if (ha7) {
		ast_free_ha(ha7);
	}
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(invalid_acl);
	AST_TEST_UNREGISTER(acl);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(invalid_acl);
	AST_TEST_REGISTER(acl);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ACL test module");
