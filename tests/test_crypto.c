/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Philip Prindeville
 *
 * Philip Prindeville <philipp@redfish-solutions.com>
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
 * \brief Unit Tests for crypto API
 *
 * \author Philip Prindeville <philipp@redfish-solutions.com>
 */

/*** MODULEINFO
        <depend>TEST_FRAMEWORK</depend>
        <depend>res_crypto</depend>
        <depend>crypto</depend>
        <support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/test.h"
#include "asterisk/crypto.h"
#include "asterisk/paths.h"
#include "asterisk/module.h"
#include "asterisk/file.h"

#include <assert.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <openssl/evp.h>

static const char *keypair1 = "rsa_key1";

static const char *old_key_dir = NULL;

static char *hexstring(const unsigned char *data, unsigned datalen)
{
	char *buf = ast_malloc(datalen * 2 + 1);
	unsigned n;

	for (n = 0; n < datalen; ++n) {
		snprintf(&buf[n * 2], 3, "%02x", data[n]);
	}
	buf[datalen * 2] = '\0';

	return buf;
}

static void push_key_dir(const char *dir)
{
	assert(old_key_dir == NULL);

	old_key_dir = ast_config_AST_KEY_DIR;

	ast_config_AST_KEY_DIR = ast_strdup(dir);
}

static void pop_key_dir(void)
{
	assert(old_key_dir != NULL);

	ast_free((char *)ast_config_AST_KEY_DIR);

	ast_config_AST_KEY_DIR = old_key_dir;

	old_key_dir = NULL;
}

AST_TEST_DEFINE(crypto_rsa_encrypt)
{
	int res = AST_TEST_FAIL;
	struct ast_key *key = NULL;
	const unsigned char plaintext[23] = "Mary had a little lamb.";
	char wd[PATH_MAX], key_dir[PATH_MAX], priv[PATH_MAX];
	unsigned char buf[AST_CRYPTO_RSA_KEY_BITS / 8];
	const char *command = "openssl";
	char *args[] = { "openssl", "pkeyutl", "-decrypt", "-inkey", "PRIVATE", "-pkeyopt", "rsa_padding_mode:oaep", NULL };
	enum { PRIVATE = 4 };
	struct ast_test_capture cap;

	switch (cmd) {
	case TEST_INIT:
		info->name = "crypto_rsa_encrypt";
		info->category = "/res/res_crypto/";
		info->summary = "Encrypt w/ RSA public key";
		info->description = "Encrypt string with RSA public key";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing RSA encryption test\n");

	ast_test_capture_init(&cap);

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		ast_test_capture_free(&cap);
		return res;
	}

	if (getcwd(wd, sizeof(wd)) == NULL) {
		ast_test_status_update(test, "Could not determine current working directory\n");
		ast_test_capture_free(&cap);
		return res;
	}

	snprintf(key_dir, sizeof(key_dir), "%s/%s", wd, "tests/keys");
	push_key_dir((const char *)key_dir);
	snprintf(priv, sizeof(priv), "%s/%s.key", key_dir, keypair1);

	/* because git doesn't preserve permissions */
	(void)chmod(priv, 0400);

	if (ast_crypto_reload() != 1) {
		ast_test_status_update(test, "Couldn't force crypto reload\n");
		goto cleanup;
	}

	key = ast_key_get(keypair1, AST_KEY_PUBLIC);

	if (!key) {
		ast_test_status_update(test, "Couldn't read key: %s\n", keypair1);
		goto cleanup;
	}

	memset(buf, 0, sizeof(buf));
	ast_encrypt_bin(buf, plaintext, sizeof(plaintext), key);

	args[PRIVATE] = priv;
	if (ast_test_capture_command(&cap, command, args, (const char *)buf, sizeof(buf)) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		goto cleanup;
	}

	if (cap.outlen != sizeof(plaintext) || memcmp(cap.outbuf, plaintext, cap.outlen)) {
		ast_test_status_update(test, "Unexpected value/length for stdout: '%.*s' (%zu)\n", (int) cap.outlen, cap.outbuf, cap.outlen);
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "Unexpected length for stderr: '%.*s' (%zu)\n", (int) cap.errlen, cap.errbuf, cap.errlen);
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "Invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 0) {
		ast_test_status_update(test, "Child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	ast_test_capture_free(&cap);
	pop_key_dir();
	return res;
}

AST_TEST_DEFINE(crypto_rsa_decrypt)
{
	int res = AST_TEST_FAIL;
	struct ast_key *key = NULL;
	const unsigned char plaintext[23] = "Mary had a little lamb.";
	char wd[PATH_MAX], key_dir[PATH_MAX], pub[PATH_MAX];
	unsigned char buf[AST_CRYPTO_RSA_KEY_BITS / 8];
	const char *command = "openssl";
	char *args[] = { "openssl", "pkeyutl", "-encrypt", "-pubin", "-inkey", "PUBLIC", "-pkeyopt", "rsa_padding_mode:oaep", NULL };
	enum { PUBLIC = 5 };
	struct ast_test_capture cap;
	int len;

	switch (cmd) {
	case TEST_INIT:
		info->name = "crypto_decrypt_pub_key";
		info->category = "/res/res_crypto/";
		info->summary = "Decrypt w/ RSA public key";
		info->description = "Decrypt string with RSA private key";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing RSA decryption test\n");

	ast_test_capture_init(&cap);

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		ast_test_capture_free(&cap);
		return res;
	}

	if (getcwd(wd, sizeof(wd)) == NULL) {
		ast_test_status_update(test, "Could not determine current working directory\n");
		ast_test_capture_free(&cap);
		return res;
	}

	snprintf(key_dir, sizeof(key_dir), "%s/%s", wd, "tests/keys");
	push_key_dir((const char *)key_dir);
	snprintf(pub, sizeof(pub), "%s/%s.pub", key_dir, keypair1);

	if (ast_crypto_reload() != 1) {
		ast_test_status_update(test, "Couldn't force crypto reload\n");
		goto cleanup;
	}

	key = ast_key_get(keypair1, AST_KEY_PRIVATE);

	if (!key) {
		ast_test_status_update(test, "Couldn't read key: %s\n", keypair1);
		goto cleanup;
	}

	args[PUBLIC] = pub;
	if (ast_test_capture_command(&cap, command, args, (const char *)plaintext, sizeof(plaintext)) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		goto cleanup;
	}

	if (cap.outlen != sizeof(buf)) {
		ast_test_status_update(test, "Unexpected length for stdout: %zu\n", cap.outlen);
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "Unexpected value/length for stderr: '%.*s' (%zu)\n", (int) cap.errlen, cap.errbuf, cap.errlen);
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "Invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 0) {
		ast_test_status_update(test, "Child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	memset(buf, 0, sizeof(buf));
	len = ast_decrypt_bin(buf, (unsigned char *)cap.outbuf, cap.outlen, key);

	if (len != sizeof(plaintext) || memcmp(buf, plaintext, len)) {
		ast_test_status_update(test, "Unexpected value for decrypted text\n");
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	ast_test_capture_free(&cap);
	pop_key_dir();
	return res;
}

AST_TEST_DEFINE(crypto_sign)
{
	int res = AST_TEST_FAIL;
	struct ast_key *key = NULL;
	const char plaintext[23] = "Mary had a little lamb.";
	char wd[PATH_MAX], key_dir[PATH_MAX], pub[PATH_MAX];
	unsigned char buf[AST_CRYPTO_RSA_KEY_BITS / 8];
	const char *command = "openssl";
	char *args[] = { "openssl", "pkeyutl", "-verify", "-inkey", "PUBLIC", "-pubin", "-sigfile", "SIGNATURE", "-pkeyopt", "digest:sha1", NULL };
	enum { PUBLIC = 4, SIGNATURE = 7 };
	struct ast_test_capture cap;
	unsigned char digest[20];
	unsigned digestlen;
	EVP_MD_CTX *ctx;
	FILE *fsig = NULL;
	char signpath[64] = "/tmp/signingXXXXXX";
	const char success[] = "Signature Verified Successfully\n";

	switch (cmd) {
	case TEST_INIT:
		info->name = "crypto_sign";
		info->category = "/res/res_crypto/";
		info->summary = "Sign w/ RSA private key";
		info->description = "Sign string with RSA private key";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing RSA signing test\n");

	ast_test_capture_init(&cap);

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		ast_test_capture_free(&cap);
		return res;
	}

	if (getcwd(wd, sizeof(wd)) == NULL) {
		ast_test_status_update(test, "Could not determine current working directory\n");
		ast_test_capture_free(&cap);
		return res;
	}

	snprintf(key_dir, sizeof(key_dir), "%s/%s", wd, "tests/keys");
	push_key_dir((const char *)key_dir);
	snprintf(pub, sizeof(pub), "%s/%s.pub", key_dir, keypair1);

	ctx = EVP_MD_CTX_create();
	EVP_DigestInit(ctx, EVP_sha1());
	EVP_DigestUpdate(ctx, plaintext, sizeof(plaintext));
	EVP_DigestFinal(ctx, digest, &digestlen);
	EVP_MD_CTX_destroy(ctx);
	ctx = NULL;

	if (ast_crypto_reload() != 1) {
		ast_test_status_update(test, "Couldn't force crypto reload\n");
		goto cleanup;
	}

	key = ast_key_get(keypair1, AST_KEY_PRIVATE);

	if (!key) {
		ast_test_status_update(test, "Couldn't read key: %s\n", keypair1);
		goto cleanup;
	}

	memset(buf, 0, sizeof(buf));
	if (ast_sign_bin(key, plaintext, sizeof(plaintext), buf) != 0) {
		ast_test_status_update(test, "ast_sign_bin() failed\n");
		goto cleanup;
	}

	fsig = ast_file_mkftemp(signpath, 0600);
	if (fsig == NULL) {
		ast_test_status_update(test, "Couldn't open temp signing file\n");
		goto cleanup;
	}
	fwrite(buf, sizeof(char), sizeof(buf), fsig);
	fclose(fsig);
	fsig = NULL;

	args[PUBLIC] = pub;
	args[SIGNATURE] = signpath;
	if (ast_test_capture_command(&cap, command, args, (const char *)digest, digestlen) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		goto cleanup;
	}

	if (cap.outlen != sizeof(success) - 1 || memcmp(cap.outbuf, success, cap.outlen)) {
		ast_test_status_update(test, "Unexpected value/length for stdout: '%.*s' (%zu)\n", (int) cap.outlen, cap.outbuf, cap.outlen);
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "Unexpected value for stderr: '%.*s' (%zu)\n", (int) cap.errlen, cap.errbuf, cap.errlen);
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "Invalid process id\n");
		goto cleanup;
	}

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
	if (cap.exitcode != 0) {
#else
	if (cap.exitcode != 0 && cap.exitcode != 1) {
#endif
		ast_test_status_update(test, "Child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	ast_test_capture_free(&cap);
	unlink(signpath);
	pop_key_dir();
	return res;
}

AST_TEST_DEFINE(crypto_verify)
{
	int res = AST_TEST_FAIL;
	struct ast_key *key = NULL;
	const char plaintext[23] = "Mary had a little lamb.";
	char wd[PATH_MAX], key_dir[PATH_MAX], priv[PATH_MAX];
	const char *command = "openssl";
	char *args[] = { "openssl", "pkeyutl", "-sign", "-inkey", "PRIVATE", "-pkeyopt", "digest:sha1", NULL };
	enum { PRIVATE = 4 };
	struct ast_test_capture cap;
	unsigned char digest[20];
	unsigned digestlen;
	EVP_MD_CTX *ctx;

	switch (cmd) {
	case TEST_INIT:
		info->name = "crypto_verify";
		info->category = "/res/res_crypto/";
		info->summary = "Verify w/ RSA public key";
		info->description = "Verify signature with RSA public key";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing RSA signature verification test\n");

	ast_test_capture_init(&cap);

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		ast_test_capture_free(&cap);
		return res;
	}

	if (getcwd(wd, sizeof(wd)) == NULL) {
		ast_test_status_update(test, "Could not determine current working directory\n");
		ast_test_capture_free(&cap);
		return res;
	}

	snprintf(key_dir, sizeof(key_dir), "%s/%s", wd, "tests/keys");
	push_key_dir((const char *)key_dir);
	snprintf(priv, sizeof(priv), "%s/%s.key", key_dir, keypair1);

	/* because git doesn't preserve permissions */
	(void)chmod(priv, 0400);

	if (ast_crypto_reload() != 1) {
		ast_test_status_update(test, "Couldn't force crypto reload\n");
		goto cleanup;
	}

	key = ast_key_get(keypair1, AST_KEY_PUBLIC);

	if (!key) {
		ast_test_status_update(test, "Couldn't read key: %s\n", keypair1);
		goto cleanup;
	}

	ctx = EVP_MD_CTX_create();
	EVP_DigestInit(ctx, EVP_sha1());
	EVP_DigestUpdate(ctx, plaintext, sizeof(plaintext));
	EVP_DigestFinal(ctx, digest, &digestlen);
	EVP_MD_CTX_destroy(ctx);

	args[PRIVATE] = priv;
	if (ast_test_capture_command(&cap, command, args, (const char *)digest, sizeof(digest)) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		goto cleanup;
	}

	if (cap.outlen != (AST_CRYPTO_RSA_KEY_BITS / 8)) {
		ast_test_status_update(test, "Unexpected length for stdout: %zu\n", cap.outlen);
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "Unexpected value/length for stderr: '%.*s'\n", (int) cap.errlen, cap.errbuf);
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "Invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 0) {
		ast_test_status_update(test, "Child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	if (ast_check_signature_bin(key, plaintext, sizeof(plaintext), (const unsigned char *)cap.outbuf) != 0) {
		ast_test_status_update(test, "ast_check_signature_bin() failed\n");
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	ast_test_capture_free(&cap);
	pop_key_dir();
	return res;
}

AST_TEST_DEFINE(crypto_aes_encrypt)
{
	int res = AST_TEST_FAIL;
	const unsigned char key[16] = {
		0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45,
		0x67, 0x89, 0x01, 0x23, 0x45, 0x67, 0x89, 0x01
	};
	const unsigned char plaintext[16] = "Mary had a littl";
	const char *command = "openssl";
	char *args[] = { "openssl", "enc", "-aes-128-ecb", "-d", "-K", "KEY", "-nopad", NULL };
	enum { KEY = 5 };
	struct ast_test_capture cap;
	unsigned char buf[16];
	ast_aes_encrypt_key aes_key;

	switch (cmd) {
	case TEST_INIT:
		info->name = "crypto_aes_encrypt";
		info->category = "/res/res_crypto/";
		info->summary = "Encrypt test AES-128-ECB";
		info->description = "Encrypt a test string using AES-128 and ECB";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing AES-ECB encryption test\n");

	ast_test_capture_init(&cap);

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		return res;
	}

	memset(buf, 0, sizeof(buf));
	ast_aes_set_encrypt_key(key, &aes_key);
	if (ast_aes_encrypt(plaintext, buf, &aes_key) <= 0) {
		ast_test_status_update(test, "ast_aes_encrypt() failed\n");
		goto cleanup;
	}

	args[KEY] = hexstring(key, sizeof(key));
	if (ast_test_capture_command(&cap, command, args, (const char *)buf, sizeof(buf)) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		goto cleanup;
	}

	if (cap.outlen != sizeof(plaintext) || memcmp(cap.outbuf, plaintext, cap.outlen)) {
		ast_test_status_update(test, "Unexpected value/length for stdout: '%.*s' (%zu)\n", (int) cap.outlen, cap.outbuf, cap.outlen);
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "Unexpected value/length for stderr: '%.*s'\n", (int) cap.errlen, cap.errbuf);
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "Invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 0) {
		ast_test_status_update(test, "Child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	ast_free(args[KEY]);
	ast_test_capture_free(&cap);
	return res;
}

AST_TEST_DEFINE(crypto_aes_decrypt)
{
	int res = AST_TEST_FAIL;
	const unsigned char key[16] = {
		0x01, 0x23, 0x45, 0x67, 0x89, 0x01, 0x23, 0x45,
		0x67, 0x89, 0x01, 0x23, 0x45, 0x67, 0x89, 0x01
	};
	const unsigned char plaintext[16] = "Mary had a littl";
	unsigned char buf[16];
	const char *command = "openssl";
	char *args[] = { "openssl", "enc", "-aes-128-ecb", "-e", "-K", "KEY", "-nopad", NULL };
	enum { KEY = 5 };
	struct ast_test_capture cap;
	ast_aes_encrypt_key aes_key;

	switch (cmd) {
	case TEST_INIT:
		info->name = "crypto_aes_decrypt";
		info->category = "/res/res_crypto/";
		info->summary = "Decrypt test AES-128-ECB";
		info->description = "Decrypt a test string using AES-128 and ECB";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing AES-ECB decryption test\n");

	ast_test_capture_init(&cap);

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		return res;
	}

	args[KEY] = hexstring(key, sizeof(key));
	if (ast_test_capture_command(&cap, command, args, (const char *)plaintext, sizeof(plaintext)) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		goto cleanup;
	}

	if (cap.outlen != sizeof(buf)) {
		ast_test_status_update(test, "Unexpected length for stdout: %zu\n", cap.outlen);
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "Unexpected value/length for stderr: '%.*s'\n", (int) cap.errlen, cap.errbuf);
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "Invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 0) {
		ast_test_status_update(test, "Child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	memset(buf, 0, sizeof(buf));
	ast_aes_set_decrypt_key(key, &aes_key);
	if (ast_aes_decrypt((const unsigned char *)cap.outbuf, buf, &aes_key) <= 0) {
		ast_test_status_update(test, "ast_aes_decrypt() failed\n");
		goto cleanup;
	}

	if (memcmp(plaintext, buf, sizeof(plaintext))) {
		ast_test_status_update(test, "AES decryption mismatch\n");
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	ast_free(args[KEY]);
	ast_test_capture_free(&cap);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(crypto_rsa_encrypt);
	AST_TEST_UNREGISTER(crypto_rsa_decrypt);
	AST_TEST_UNREGISTER(crypto_sign);
	AST_TEST_UNREGISTER(crypto_verify);
	AST_TEST_UNREGISTER(crypto_aes_encrypt);
	AST_TEST_UNREGISTER(crypto_aes_decrypt);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(crypto_rsa_encrypt);
	AST_TEST_REGISTER(crypto_rsa_decrypt);
	AST_TEST_REGISTER(crypto_sign);
	AST_TEST_REGISTER(crypto_verify);
	AST_TEST_REGISTER(crypto_aes_encrypt);
	AST_TEST_REGISTER(crypto_aes_decrypt);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Crypto test module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_crypto",
);
