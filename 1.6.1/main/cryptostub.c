/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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

/*! \file
 *
 * \brief Stubs for res_crypto routines
 *
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/crypto.h"
#include "asterisk/logger.h"

static struct ast_key *stub_ast_key_get(const char *kname, int ktype)
{
	ast_log(LOG_NOTICE, "Crypto support not loaded!\n");
	return NULL;
}

#ifdef SKREP
#define build_stub(func_name,...) \
static int stub_ ## func_name(__VA_ARGS__) \
{ \
	ast_log(LOG_NOTICE, "Crypto support not loaded!\n"); \
	return -1; \
} \
\
int (*func_name)(__VA_ARGS__) = \
	stub_ ## func_name;
#endif
#define build_stub(func_name,...) \
static int stub_##func_name(__VA_ARGS__) \
{ \
	ast_log(LOG_NOTICE, "Crypto support not loaded!\n"); \
	return -1; \
} \
\
int (*func_name)(__VA_ARGS__) = \
	stub_##func_name;

struct ast_key *(*ast_key_get)(const char *key, int type) =
stub_ast_key_get;

build_stub(ast_check_signature, struct ast_key *key, const char *msg, const char *sig);
build_stub(ast_check_signature_bin, struct ast_key *key, const char *msg, int msglen, const unsigned char *sig);
build_stub(ast_sign, struct ast_key *key, char *msg, char *sig);
build_stub(ast_sign_bin, struct ast_key *key, const char *msg, int msglen, unsigned char *sig);
build_stub(ast_encrypt_bin, unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key);
build_stub(ast_decrypt_bin, unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key);
