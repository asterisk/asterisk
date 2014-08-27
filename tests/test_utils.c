/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
 * Russell Bryant <russell@digium.com>
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
 * \brief Unit Tests for utils API
 *
 * \author David Vossel <dvossel@digium.com>
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include "asterisk/utils.h"
#include "asterisk/test.h"
#include "asterisk/crypto.h"
#include "asterisk/adsi.h"
#include "asterisk/agi.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"

#include <sys/stat.h>

AST_TEST_DEFINE(uri_encode_decode_test)
{
	int res = AST_TEST_PASS;
	const char *in = "abcdefghijklmnopurstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ 1234567890 ~`!@#$%^&*()_-+={[}]|\\:;\"'<,>.?/";
	char out[256] = { 0 };
	char small[4] = { 0 };
	const struct ast_flags none = {0};
	int i = 0;

	static struct {
		const char *spec_str;
		struct ast_flags spec;

		char *buf;
		size_t buflen;

		const char *input;
		const char *output;
		const char *decoded_output;
	} tests[5];

#define INIT_ENCODE_TEST(s, buffer, in, out, dec_out) do { \
	if (i < ARRAY_LEN(tests)) { \
		tests[i].spec_str = #s; \
		tests[i].spec = s; \
		tests[i].buf = buffer; \
		tests[i].buflen = sizeof(buffer); \
		tests[i].input = in; \
		tests[i].output = out; \
		tests[i].decoded_output = dec_out; \
		i++; \
	} else { \
			ast_test_status_update(test, "error: 'tests' array too small\n"); \
			res = AST_TEST_FAIL; \
	} \
	} while (0)

	INIT_ENCODE_TEST(ast_uri_http, out, in,
		"abcdefghijklmnopurstuvwxyz%20ABCDEFGHIJKLMNOPQRSTUVWXYZ%201234567890%20~%60!%40%23%24%25%5E%26*()_-%2B%3D%7B%5B%7D%5D%7C%5C%3A%3B%22'%3C%2C%3E.%3F%2F", in);
	INIT_ENCODE_TEST(ast_uri_http_legacy, out, in,
		"abcdefghijklmnopurstuvwxyz+ABCDEFGHIJKLMNOPQRSTUVWXYZ+1234567890+~%60!%40%23%24%25%5E%26*()_-%2B%3D%7B%5B%7D%5D%7C%5C%3A%3B%22'%3C%2C%3E.%3F%2F", in);
	INIT_ENCODE_TEST(ast_uri_sip_user, out, in,
		"abcdefghijklmnopurstuvwxyz%20ABCDEFGHIJKLMNOPQRSTUVWXYZ%201234567890%20~%60!%40%23$%25%5E&*()_-+=%7B%5B%7D%5D%7C%5C%3A;%22'%3C,%3E.?/", in);
	INIT_ENCODE_TEST(none, small, in, "%61", "a");
	INIT_ENCODE_TEST(ast_uri_http, small, in, "abc", "abc");

	switch (cmd) {
	case TEST_INIT:
		info->name = "uri_encode_decode_test";
		info->category = "/main/utils/";
		info->summary = "encode and decode a hex escaped string";
		info->description = "encode a string, verify encoded string matches what we expect.  Decode the encoded string, verify decoded string matches the original string.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(tests); i++) {
		ast_uri_encode(tests[i].input, tests[i].buf, tests[i].buflen, tests[i].spec);
		if (strcmp(tests[i].output, tests[i].buf)) {
			ast_test_status_update(test, "encoding with %s did not match expected output, FAIL\n", tests[i].spec_str);
			ast_test_status_update(test, "original: %s\n", tests[i].input);
			ast_test_status_update(test, "expected: %s\n", tests[i].output);
			ast_test_status_update(test, "result: %s\n", tests[i].buf);
			res = AST_TEST_FAIL;
			continue;
		}

		ast_uri_decode(tests[i].buf, tests[i].spec);
		if (strcmp(tests[i].decoded_output, tests[i].buf)) {
			ast_test_status_update(test, "decoding with %s did not match the original input (or expected decoded output)\n", tests[i].spec_str);
			ast_test_status_update(test, "original: %s\n", tests[i].input);
			ast_test_status_update(test, "expected: %s\n", tests[i].decoded_output);
			ast_test_status_update(test, "decoded: %s\n", tests[i].buf);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(quoted_escape_test)
{
	int res = AST_TEST_PASS;
	const char *in = "a\"bcdefg\"hijkl\\mnopqrs tuv\twxyz";
	char out[256] = { 0 };
	char small[4] = { 0 };
	int i;

	static struct {
		char *buf;
		const size_t buflen;

		const char *output;
	} tests[] = {
		{0, sizeof(out),
			"a\\\"bcdefg\\\"hijkl\\\\mnopqrs tuv\twxyz"},
		{0, sizeof(small),
			"a\\\""},
	};

	tests[0].buf = out;
	tests[1].buf = small;

	switch (cmd) {
	case TEST_INIT:
		info->name = "quoted_escape_test";
		info->category = "/main/utils/";
		info->summary = "escape a quoted string";
		info->description = "Escape a string to be quoted and check the result.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(tests); i++) {
		ast_escape_quoted(in, tests[i].buf, tests[i].buflen);
		if (strcmp(tests[i].output, tests[i].buf)) {
			ast_test_status_update(test, "ESCAPED DOES NOT MATCH EXPECTED, FAIL\n");
			ast_test_status_update(test, "original: %s\n", in);
			ast_test_status_update(test, "expected: %s\n", tests[i].output);
			ast_test_status_update(test, "result: %s\n", tests[i].buf);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(md5_test)
{
	static const struct {
		const char *input;
		const char *expected_output;
	} tests[] = {
		{ "apples",                          "daeccf0ad3c1fc8c8015205c332f5b42" },
		{ "bananas",                         "ec121ff80513ae58ed478d5c5787075b" },
		{ "reallylongstringaboutgoatcheese", "0a2d9280d37e2e37545cfef6e7e4e890" },
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "md5_test";
		info->category = "/main/utils/";
		info->summary = "MD5 test";
		info->description =
			"This test exercises MD5 calculations."
			"";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing MD5 ...\n");

	for (i = 0; i < ARRAY_LEN(tests); i++) {
		char md5_hash[33];
		ast_md5_hash(md5_hash, tests[i].input);
		if (strcasecmp(md5_hash, tests[i].expected_output)) {
			ast_test_status_update(test,
					"input: '%s'  hash: '%s'  expected hash: '%s'\n",
					tests[i].input, md5_hash, tests[i].expected_output);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(sha1_test)
{
	static const struct {
		const char *input;
		const char *expected_output;
	} tests[] = {
		{ "giraffe",
			"fac8f1a31d2998734d6a5253e49876b8e6a08239" },
		{ "platypus",
			"1dfb21b7a4d35e90d943e3a16107ccbfabd064d5" },
		{ "ParastratiosphecomyiaStratiosphecomyioides",
			"58af4e8438676f2bd3c4d8df9e00ee7fe06945bb" },
	};
	enum ast_test_result_state res = AST_TEST_PASS;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sha1_test";
		info->category = "/main/utils/";
		info->summary = "SHA1 test";
		info->description =
			"This test exercises SHA1 calculations."
			"";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing SHA1 ...\n");

	for (i = 0; i < ARRAY_LEN(tests); i++) {
		char sha1_hash[64];
		ast_sha1_hash(sha1_hash, tests[i].input);
		if (strcasecmp(sha1_hash, tests[i].expected_output)) {
			ast_test_status_update(test,
					"input: '%s'  hash: '%s'  expected hash: '%s'\n",
					tests[i].input, sha1_hash, tests[i].expected_output);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(base64_test)
{
	static const struct {
		const char *input;
		const char *decoded;
	} tests[] = {
		{ "giraffe",
			"Z2lyYWZmZQ==" },
		{ "platypus",
			"cGxhdHlwdXM=" },
		{ "ParastratiosphecomyiaStratiosphecomyioides",
			"UGFyYXN0cmF0aW9zcGhlY29teWlhU3RyYXRpb3NwaGVjb215aW9pZGVz" },
	};
	int i;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "base64_test";
		info->category = "/main/utils/";
		info->summary = "base64 test";
		info->description = "This test exercises the base64 conversions.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}


	for (i = 0; i < ARRAY_LEN(tests); i++) {
		char tmp[64];
		ast_base64encode(tmp, (unsigned char *)tests[i].input, strlen(tests[i].input), sizeof(tmp));
		if (strcasecmp(tmp, tests[i].decoded)) {
			ast_test_status_update(test,
					"input: '%s'  base64 output: '%s'  expected base64 output: '%s'\n",
					tests[i].input, tmp, tests[i].decoded);
			res = AST_TEST_FAIL;
		}

		memset(tmp, 0, sizeof(tmp));
		ast_base64decode((unsigned char *) tmp, tests[i].decoded, (sizeof(tmp) - 1));
		if (strcasecmp(tmp, tests[i].input)) {
			ast_test_status_update(test,
					"base64 input: '%s'  output: '%s'  expected output: '%s'\n",
					tests[i].decoded, tmp, tests[i].input);
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(crypto_loaded_test)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "crypto_loaded_test";
		info->category = "/res/crypto/";
		info->summary = "Crypto loaded into memory";
		info->description = "Verifies whether the crypto functions overrode the stubs";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

#if 0 /* Not defined on Solaris */
	ast_test_status_update(test,
			       "address of __stub__ast_crypto_loaded is %p\n",
			       __stub__ast_crypto_loaded);
#ifndef HAVE_ATTRIBUTE_weak_import
	ast_test_status_update(test,
			       "address of __ref__ast_crypto_loaded is %p\n",
			       __ref__ast_crypto_loaded);
#endif
	ast_test_status_update(test,
			       "pointer to ast_crypto_loaded is %p\n",
			       ast_crypto_loaded);
#endif

	return ast_crypto_loaded() ? AST_TEST_PASS : AST_TEST_FAIL;
}

AST_TEST_DEFINE(adsi_loaded_test)
{
	struct ast_channel *c;
	int res;
	switch (cmd) {
	case TEST_INIT:
		info->name = "adsi_loaded_test";
		info->category = "/res/adsi/";
		info->summary = "ADSI loaded into memory";
		info->description = "Verifies whether the adsi functions overrode the stubs";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(c = ast_dummy_channel_alloc())) {
		return AST_TEST_FAIL;
	}
	ast_channel_adsicpe_set(c, AST_ADSI_AVAILABLE);
	res = ast_adsi_available(c) ? AST_TEST_PASS : AST_TEST_FAIL;
	c = ast_channel_unref(c);
	return res;
}

static int handle_noop(struct ast_channel *chan, AGI *agi, int arg, const char * const argv[])
{
	ast_agi_send(agi->fd, chan, "200 result=0\n");
	return RESULT_SUCCESS;
}

AST_TEST_DEFINE(agi_loaded_test)
{
	int res = AST_TEST_PASS;
	struct agi_command noop_command =
		{ { "testnoop", NULL }, handle_noop, NULL, NULL, 0 };

	switch (cmd) {
	case TEST_INIT:
		info->name = "agi_loaded_test";
		info->category = "/res/agi/";
		info->summary = "AGI loaded into memory";
		info->description = "Verifies whether the agi functions overrode the stubs";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

#if 0
	ast_test_status_update(test,
			       "address of __stub__ast_agi_register is %p\n",
			       __stub__ast_agi_register);
#ifndef HAVE_ATTRIBUTE_weak_import
	ast_test_status_update(test,
			       "address of __ref__ast_agi_register is %p\n",
			       __ref__ast_agi_register);
#endif
	ast_test_status_update(test,
			       "pointer to ast_agi_register is %p\n",
			       ast_agi_register);
#endif

	if (ast_agi_register(ast_module_info->self, &noop_command) == AST_OPTIONAL_API_UNAVAILABLE) {
		ast_test_status_update(test, "Unable to register testnoop command, because res_agi is not loaded.\n");
		return AST_TEST_FAIL;
	}

#ifndef HAVE_NULLSAFE_PRINTF
	/* Test for condition without actually crashing Asterisk */
	if (noop_command.usage == NULL) {
		ast_test_status_update(test, "AGI testnoop usage was not updated properly.\n");
		res = AST_TEST_FAIL;
	}
	if (noop_command.syntax == NULL) {
		ast_test_status_update(test, "AGI testnoop syntax was not updated properly.\n");
		res = AST_TEST_FAIL;
	}
#endif

	ast_agi_unregister(ast_module_info->self, &noop_command);
	return res;
}

AST_TEST_DEFINE(safe_mkdir_test)
{
	char base_path[] = "/tmp/safe_mkdir.XXXXXX";
	char path[80] = {};
	int res;
	struct stat actual;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/main/utils/";
		info->summary = "Safe mkdir test";
		info->description =
			"This test ensures that ast_safe_mkdir does what it is "
			"supposed to";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (mkdtemp(base_path) == NULL) {
		ast_test_status_update(test, "Failed to create tmpdir for test\n");
		return AST_TEST_FAIL;
	}

	snprintf(path, sizeof(path), "%s/should_work", base_path);
	res = ast_safe_mkdir(base_path, path, 0777);
	ast_test_validate(test, 0 == res);
	res = stat(path, &actual);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, S_ISDIR(actual.st_mode));

	snprintf(path, sizeof(path), "%s/should/also/work", base_path);
	res = ast_safe_mkdir(base_path, path, 0777);
	ast_test_validate(test, 0 == res);
	res = stat(path, &actual);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, S_ISDIR(actual.st_mode));

	snprintf(path, sizeof(path), "%s/even/this/../should/work", base_path);
	res = ast_safe_mkdir(base_path, path, 0777);
	ast_test_validate(test, 0 == res);
	snprintf(path, sizeof(path), "%s/even/should/work", base_path);
	res = stat(path, &actual);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, S_ISDIR(actual.st_mode));

	snprintf(path, sizeof(path),
		"%s/surprisingly/this/should//////////////////work", base_path);
	res = ast_safe_mkdir(base_path, path, 0777);
	ast_test_validate(test, 0 == res);
	snprintf(path, sizeof(path),
		"%s/surprisingly/this/should/work", base_path);
	res = stat(path, &actual);
	ast_test_validate(test, 0 == res);
	ast_test_validate(test, S_ISDIR(actual.st_mode));

	snprintf(path, sizeof(path), "/should_not_work");
	res = ast_safe_mkdir(base_path, path, 0777);
	ast_test_validate(test, 0 != res);
	ast_test_validate(test, EPERM == errno);
	res = stat(path, &actual);
	ast_test_validate(test, 0 != res);
	ast_test_validate(test, ENOENT == errno);

	snprintf(path, sizeof(path), "%s/../nor_should_this", base_path);
	res = ast_safe_mkdir(base_path, path, 0777);
	ast_test_validate(test, 0 != res);
	ast_test_validate(test, EPERM == errno);
	strncpy(path, "/tmp/nor_should_this", sizeof(path));
	res = stat(path, &actual);
	ast_test_validate(test, 0 != res);
	ast_test_validate(test, ENOENT == errno);

	snprintf(path, sizeof(path),
		"%s/this/especially/should/not/../../../../../work", base_path);
	res = ast_safe_mkdir(base_path, path, 0777);
	ast_test_validate(test, 0 != res);
	ast_test_validate(test, EPERM == errno);
	strncpy(path, "/tmp/work", sizeof(path));
	res = stat(path, &actual);
	ast_test_validate(test, 0 != res);
	ast_test_validate(test, ENOENT == errno);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(crypt_test)
{
	RAII_VAR(char *, password_crypted, NULL, ast_free);
	RAII_VAR(char *, blank_crypted, NULL, ast_free);
	const char *password = "Passw0rd";
	const char *not_a_password = "not-a-password";

	switch (cmd) {
	case TEST_INIT:
		info->name = "crypt_test";
		info->category = "/main/utils/";
		info->summary = "Test ast_crypt wrappers";
		info->description = "Verifies that the ast_crypt wrappers work as expected.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	password_crypted = ast_crypt_encrypt(password);
	ast_test_validate(test, NULL != password_crypted);
	ast_test_validate(test, 0 != strcmp(password, password_crypted));
	ast_test_validate(test, ast_crypt_validate(password, password_crypted));
	ast_test_validate(test,
		!ast_crypt_validate(not_a_password, password_crypted));

	blank_crypted = ast_crypt_encrypt("");
	ast_test_validate(test, NULL != blank_crypted);
	ast_test_validate(test, 0 != strcmp(blank_crypted, ""));
	ast_test_validate(test, ast_crypt_validate("", blank_crypted));
	ast_test_validate(test,
		!ast_crypt_validate(not_a_password, blank_crypted));

	return AST_TEST_PASS;
}


struct quote_set {
	char *input;
	char *output;
};

AST_TEST_DEFINE(quote_mutation)
{
	char escaped[64];
	static const struct quote_set escape_sets[] = {
		{"\"string\"", "\\\"string\\\""},
		{"\"string", "\\\"string"},
		{"string\"", "string\\\""},
		{"string", "string"},
		{"str\"ing", "str\\\"ing"},
		{"\"", "\\\""},
		{"\\\"", "\\\\\\\""},
	};
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "quote_mutation";
		info->category = "/main/utils/";
		info->summary = "Test mutation of quotes in strings";
		info->description =
			"This tests escaping and unescaping of quotes in strings to "
			"verify that the original string is recovered.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(escape_sets); i++) {
		ast_escape_quoted(escape_sets[i].input, escaped, sizeof(escaped));

		if (strcmp(escaped, escape_sets[i].output)) {
			ast_test_status_update(test,
				"Expected escaped string '%s' instead of '%s'\n",
				escape_sets[i].output, escaped);
			return AST_TEST_FAIL;
		}

		ast_unescape_quoted(escaped);
		if (strcmp(escaped, escape_sets[i].input)) {
			ast_test_status_update(test,
				"Expected unescaped string '%s' instead of '%s'\n",
				escape_sets[i].input, escaped);
			return AST_TEST_FAIL;
		}
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(quote_unescaping)
{
	static const struct quote_set escape_sets[] = {
		{"\"string\"", "\"string\""},
		{"\\\"string\"", "\"string\""},
		{"\"string\\\"", "\"string\""},
		{"str\\ing", "string"},
		{"string\\", "string"},
		{"\\string", "string"},
	};
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "quote_unescaping";
		info->category = "/main/utils/";
		info->summary = "Test unescaping of off-nominal strings";
		info->description =
			"This tests unescaping of strings which contain a mix of "
			"escaped and unescaped sequences.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(escape_sets); i++) {
		RAII_VAR(char *, escaped, ast_strdup(escape_sets[i].input), ast_free);

		ast_unescape_quoted(escaped);
		if (strcmp(escaped, escape_sets[i].output)) {
			ast_test_status_update(test,
				"Expected unescaped string '%s' instead of '%s'\n",
				escape_sets[i].output, escaped);
			return AST_TEST_FAIL;
		}
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(uri_encode_decode_test);
	AST_TEST_UNREGISTER(quoted_escape_test);
	AST_TEST_UNREGISTER(md5_test);
	AST_TEST_UNREGISTER(sha1_test);
	AST_TEST_UNREGISTER(base64_test);
	AST_TEST_UNREGISTER(crypto_loaded_test);
	AST_TEST_UNREGISTER(adsi_loaded_test);
	AST_TEST_UNREGISTER(agi_loaded_test);
	AST_TEST_UNREGISTER(safe_mkdir_test);
	AST_TEST_UNREGISTER(crypt_test);
	AST_TEST_UNREGISTER(quote_mutation);
	AST_TEST_UNREGISTER(quote_unescaping);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(uri_encode_decode_test);
	AST_TEST_REGISTER(quoted_escape_test);
	AST_TEST_REGISTER(md5_test);
	AST_TEST_REGISTER(sha1_test);
	AST_TEST_REGISTER(base64_test);
	AST_TEST_REGISTER(crypto_loaded_test);
	AST_TEST_REGISTER(adsi_loaded_test);
	AST_TEST_REGISTER(agi_loaded_test);
	AST_TEST_REGISTER(safe_mkdir_test);
	AST_TEST_REGISTER(crypt_test);
	AST_TEST_REGISTER(quote_mutation);
	AST_TEST_REGISTER(quote_unescaping);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Utils test module");
