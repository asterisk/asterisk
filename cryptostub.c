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

#include <unistd.h>
#include <stdlib.h>

#include "asterisk/crypto.h"
#include "asterisk/logger.h"

/* Hrm, I wonder if the compiler is smart enough to only create two functions
   for all these...  I could force it to only make two, but those would be some
   really nasty looking casts. */
   
static struct ast_key *stub_ast_key_get(const char *kname, int ktype)
{
	ast_log(LOG_NOTICE, "Crypto support not loaded!\n");
	return NULL;
}

static int stub_ast_check_signature(struct ast_key *key, const char *msg, const char *sig)
{
	ast_log(LOG_NOTICE, "Crypto support not loaded!\n");
	return -1;
}

static int stub_ast_check_signature_bin(struct ast_key *key, const char *msg, int msglen, const unsigned char *sig)
{
	ast_log(LOG_NOTICE, "Crypto support not loaded!\n");
	return -1;
}

static int stub_ast_sign(struct ast_key *key, char *msg, char *sig) 
{
	ast_log(LOG_NOTICE, "Crypto support not loaded!\n");
	return -1;
}

static int stub_ast_sign_bin(struct ast_key *key, const char *msg, int msglen, unsigned char *sig)
{
	ast_log(LOG_NOTICE, "Crypto support not loaded!\n");
	return -1;
}

static int stub_ast_encdec_bin(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key)
{
	ast_log(LOG_NOTICE, "Crypto support not loaded!\n");
	return -1;
}

struct ast_key *(*ast_key_get)(const char *key, int type) = 
	stub_ast_key_get;

int (*ast_check_signature)(struct ast_key *key, const char *msg, const char *sig) =
	stub_ast_check_signature;
	
int (*ast_check_signature_bin)(struct ast_key *key, const char *msg, int msglen, const unsigned char *sig) =
	stub_ast_check_signature_bin;
	
int (*ast_sign)(struct ast_key *key, char *msg, char *sig) = 
	stub_ast_sign;

int (*ast_sign_bin)(struct ast_key *key, const char *msg, int msglen, unsigned char *sig) =
	stub_ast_sign_bin;
	
int (*ast_encrypt_bin)(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key) =
	stub_ast_encdec_bin;

int (*ast_decrypt_bin)(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key) =
	stub_ast_encdec_bin;
