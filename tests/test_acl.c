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
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/test.h"
#include "asterisk/acl.h"
#include "asterisk/module.h"

AST_TEST_DEFINE(invalid_acl)
{
	const char * invalid_acls[] = {
		"1.3.3.7/-1",
		"1.3.3.7/33",
		"1.3.3.7/92342348927389492307420",
		"1.3.3.7/California",
		"1.3.3.7/255.255.255.255.255",
		"57.60.278.900/31",
		"400.32.201029.-6/24",
		"EGGSOFDEATH/4000",
		"33.4.7.8.3/300030",
		"1.2.3.4/6.7.8.9.0",
		"3.1.4.1.5.9/3",
	};

	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_ha *ha = NULL;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "invalid_acl";
		info->category = "main/acl/";
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

AST_TEST_DEFINE(acl)
{
	struct acl permitall = { "0.0.0.0/0", "permit" };
	struct acl denyall = { "0.0.0.0/0", "deny" };
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

	struct {
		const char *test_address;
		int acl1_result;
		int acl2_result;
	} acl_tests[] = {
		{ "10.1.1.5", AST_SENSE_ALLOW, AST_SENSE_ALLOW },
		{ "192.168.0.5", AST_SENSE_ALLOW, AST_SENSE_ALLOW },
		{ "192.168.1.5", AST_SENSE_DENY, AST_SENSE_ALLOW },
		{ "10.0.0.1", AST_SENSE_ALLOW, AST_SENSE_ALLOW },
		{ "10.0.10.10", AST_SENSE_ALLOW, AST_SENSE_DENY },
		{ "172.16.0.1", AST_SENSE_DENY, AST_SENSE_ALLOW },
	};

	struct ast_ha *permit_ha = NULL;
	struct ast_ha *deny_ha = NULL;
	struct ast_ha *ha1 = NULL;
	struct ast_ha *ha2 = NULL;
	enum ast_test_result_state res = AST_TEST_PASS;
	int err = 0;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "acl";
		info->category = "main/acl/";
		info->summary = "ACL unit test";
		info->description =
			"Tests that hosts are properly permitted or denied";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(permit_ha = ast_append_ha(permitall.access, permitall.host, permit_ha, &err))) {
		ast_test_status_update(test, "Failed to create permit_all ACL\n");
		res = AST_TEST_FAIL;
		goto acl_cleanup;
	}

	if (!(deny_ha = ast_append_ha(denyall.access, denyall.host, deny_ha, &err))) {
		ast_test_status_update(test, "Failed to create deny_all ACL\n");
		res = AST_TEST_FAIL;
		goto acl_cleanup;
	}

	for (i = 0; i < ARRAY_LEN(acl1); ++i) {
		if (!(ha1 = ast_append_ha(acl1[i].access, acl1[i].host, ha1, &err))) {
			ast_test_status_update(test, "Failed to add rule %s with access %s to ha1\n",
					acl1[i].host, acl1[i].access);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}
	}

	for (i = 0; i < ARRAY_LEN(acl2); ++i) {
		if (!(ha2 = ast_append_ha(acl2[i].access, acl2[i].host, ha2, &err))) {
			ast_test_status_update(test, "Failed to add rule %s with access %s to ha2\n",
					acl2[i].host, acl2[i].access);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}
	}

	for (i = 0; i < ARRAY_LEN(acl_tests); ++i) {
		struct sockaddr_in sin;
		int permit_res;
		int deny_res;
		int acl1_res;
		int acl2_res;

		inet_aton(acl_tests[i].test_address, &sin.sin_addr);

		permit_res = ast_apply_ha(permit_ha, &sin);
		deny_res = ast_apply_ha(deny_ha, &sin);
		acl1_res = ast_apply_ha(ha1, &sin);
		acl2_res = ast_apply_ha(ha2, &sin);

		if (permit_res != AST_SENSE_ALLOW) {
			ast_test_status_update(test, "Access denied to %s on permit_all ACL\n",
					acl_tests[i].test_address);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (deny_res != AST_SENSE_DENY) {
			ast_test_status_update(test, "Access allowed to %s on deny_all ACL\n",
					acl_tests[i].test_address);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (acl1_res != acl_tests[i].acl1_result) {
			ast_test_status_update(test, "Access not as expected to %s on acl1. Expected %d but"
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].acl1_result, acl1_res);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}

		if (acl2_res != acl_tests[i].acl2_result) {
			ast_test_status_update(test, "Access not as expected to %s on acl2. Expected %d but"
					"got %d instead\n", acl_tests[i].test_address, acl_tests[i].acl2_result, acl2_res);
			res = AST_TEST_FAIL;
			goto acl_cleanup;
		}
	}

acl_cleanup:
	if (permit_ha) {
		ast_free_ha(permit_ha);
	}
	if (deny_ha) {
		ast_free_ha(deny_ha);
	}
	if (ha1) {
		ast_free_ha(ha1);
	}
	if (ha1) {
		ast_free_ha(ha2);
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
