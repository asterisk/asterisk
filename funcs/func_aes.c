/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
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
 * \brief AES encryption/decryption dialplan functions
 *
 * \author David Vossel <dvossel@digium.com>
 * \ingroup functions
 */


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/aes.h"
#include "asterisk/strings.h"

#define AES_BLOCK_SIZE 16

/*** DOCUMENTATION
	<function name="AES_ENCRYPT" language="en_US">
		<synopsis>
			Encrypt a string with AES given a 16 character key.
		</synopsis>
		<syntax>
			<parameter name="key" required="true">
				<para>AES Key</para>
			</parameter>
			<parameter name="string" required="true">
				<para>Input string</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns an AES encrypted string encoded in base64.</para>
		</description>
	</function>
	<function name="AES_DECRYPT" language="en_US">
		<synopsis>
			Decrypt a string encoded in base64 with AES given a 16 character key.
		</synopsis>
		<syntax>
			<parameter name="key" required="true">
				<para>AES Key</para>
			</parameter>
			<parameter name="string" required="true">
				<para>Input string.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns the plain text string.</para>
		</description>
	</function>
 ***/


static int aes_helper(struct ast_channel *chan, const char *cmd, char *data,
	       char *buf, struct ast_str **str, ssize_t maxlen)
{
	unsigned char curblock[AES_BLOCK_SIZE] = { 0, };
	char *tmp = NULL;
	char *tmpP;
	int data_len, encrypt;
	int keylen, len, tmplen, elen = 0;
	ast_aes_encrypt_key ecx;                        /*  AES 128 Encryption context */
	ast_aes_decrypt_key dcx;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(key);
		AST_APP_ARG(data);
	);

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.data) || ast_strlen_zero(args.key)) {
		ast_log(LOG_WARNING, "Syntax: %s(<key>,<data>) - missing argument!\n", cmd);
		return -1;
	}

	if ((keylen = strlen(args.key)) != AES_BLOCK_SIZE) {        /* key must be of 16 characters in length, 128 bits */
		ast_log(LOG_WARNING, "Syntax: %s(<key>,<data>) - <key> parameter must be exactly 16 characters%s!\n", cmd, keylen < 16 ? " - padding" : "");
		return -1;
	}

	if (buf) {
		len = maxlen;
	} else if (maxlen == -1) {
		len = ast_str_size(*str);
	} else if (maxlen > 0) {
		len = maxlen;
	} else {
		len = INT_MAX;
	}
	ast_debug(3, "len=%d\n", len);

	encrypt = strcmp("AES_DECRYPT", cmd);           /* -1 if encrypting, 0 if decrypting */
	/* Round up the buffer to an even multiple of 16, plus 1 */
	tmplen = (strlen(args.data) / 16 + 1) * 16 + 1;
	tmp = ast_calloc(1, tmplen);
	tmpP = tmp;

	if (encrypt) {                                  /* if decrypting first decode src to base64 */
		/* encryption:  plaintext -> encryptedtext -> base64 */
		ast_aes_encrypt_key((unsigned char *) args.key, &ecx);
		strcpy(tmp, args.data);
		data_len = strlen(tmp);
	} else {
		/* decryption:  base64 -> encryptedtext -> plaintext */
		ast_aes_decrypt_key((unsigned char *) args.key, &dcx);
		data_len = ast_base64decode((unsigned char *) tmp, args.data, tmplen);
	}

	if (data_len >= len) {                        /* make sure to not go over buffer len */
		ast_log(LOG_WARNING, "Syntax: %s(<keys>,<data>) - <data> exceeds buffer length.  Result may be truncated!\n", cmd);
		data_len = len - 1;
	}

	while (data_len > 0) {
		/* Tricky operation.  We first copy the data into curblock, then
		 * the data is encrypted or decrypted and put back into the original
		 * buffer. */
		memset(curblock, 0, AES_BLOCK_SIZE);
		memcpy(curblock, tmpP, AES_BLOCK_SIZE);
		if (encrypt) {
			ast_aes_encrypt(curblock, (unsigned char *) tmpP, &ecx);
		} else {
			ast_aes_decrypt(curblock, (unsigned char *) tmpP, &dcx);
		}
		tmpP += AES_BLOCK_SIZE;
		data_len -= AES_BLOCK_SIZE;
		elen += AES_BLOCK_SIZE;
	}

	if (encrypt) {                            /* if encrypting encode result to base64 */
		if (buf) {
			ast_base64encode(buf, (unsigned char *) tmp, elen, len);
		} else {
			if (maxlen >= 0) {
				ast_str_make_space(str, maxlen ? maxlen : elen * 4 / 3 + 2);
			}
			ast_base64encode(ast_str_buffer(*str), (unsigned char *) tmp, elen, ast_str_size(*str));
			ast_str_update(*str);
		}
	} else {
		if (buf) {
			memcpy(buf, tmp, len);
		} else {
			ast_str_set(str, maxlen, "%s", tmp);
		}
	}
	ast_free(tmp);

	return 0;
}

static int aes_buf_helper(struct ast_channel *chan, const char *cmd, char *data,
	       char *buf, size_t maxlen)
{
	return aes_helper(chan, cmd, data, buf, NULL, maxlen);
}

static int aes_str_helper(struct ast_channel *chan, const char *cmd, char *data,
	       struct ast_str **buf, ssize_t maxlen)
{
	return aes_helper(chan, cmd, data, NULL, buf, maxlen);
}

static struct ast_custom_function aes_encrypt_function = {
	.name = "AES_ENCRYPT",
	.read = aes_buf_helper,
	.read2 = aes_str_helper,
};

static struct ast_custom_function aes_decrypt_function = {
	.name = "AES_DECRYPT",
	.read = aes_buf_helper,
	.read2 = aes_str_helper,
};

static int unload_module(void)
{
	int res = ast_custom_function_unregister(&aes_decrypt_function);
	return res | ast_custom_function_unregister(&aes_encrypt_function);
}

static int load_module(void)
{
	int res = ast_custom_function_register(&aes_decrypt_function);
	res |= ast_custom_function_register(&aes_encrypt_function);
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "AES dialplan functions");
