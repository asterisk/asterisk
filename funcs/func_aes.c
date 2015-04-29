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

/*** MODULEINFO
	<use type="external">crypto</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/crypto.h"

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
		<see-also>
			<ref type="function">AES_DECRYPT</ref>
			<ref type="function">BASE64_ENCODE</ref>
			<ref type="function">BASE64_DECODE</ref>
		</see-also>
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
		<see-also>
			<ref type="function">AES_ENCRYPT</ref>
			<ref type="function">BASE64_ENCODE</ref>
			<ref type="function">BASE64_DECODE</ref>
		</see-also>
	</function>
 ***/


static int aes_helper(struct ast_channel *chan, const char *cmd, char *data,
	       char *buf, size_t len)
{
	unsigned char curblock[AES_BLOCK_SIZE] = { 0, };
	char *tmp;
	char *tmpP;
	int data_len, encrypt;
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

	if (strlen(args.key) != AES_BLOCK_SIZE) {        /* key must be of 16 characters in length, 128 bits */
		ast_log(LOG_WARNING, "Syntax: %s(<key>,<data>) - <key> parameter must be exactly 16 characters!\n", cmd);
		return -1;
	}

	ast_aes_set_encrypt_key((unsigned char *) args.key, &ecx);   /* encryption:  plaintext -> encryptedtext -> base64 */
	ast_aes_set_decrypt_key((unsigned char *) args.key, &dcx);   /* decryption:  base64 -> encryptedtext -> plaintext */
	tmp = ast_calloc(1, len);                     /* requires a tmp buffer for the base64 decode */
	if (!tmp) {
		ast_log(LOG_ERROR, "Unable to allocate memory for data\n");
		return -1;
	}
	tmpP = tmp;
	encrypt = strcmp("AES_DECRYPT", cmd);           /* -1 if encrypting, 0 if decrypting */

	if (encrypt) {                                  /* if decrypting first decode src to base64 */
		ast_copy_string(tmp, args.data, len);
		data_len = strlen(tmp);
	} else {
		data_len = ast_base64decode((unsigned char *) tmp, args.data, len);
	}

	if (data_len >= len) {                        /* make sure to not go over buffer len */
		ast_log(LOG_WARNING, "Syntax: %s(<keys>,<data>) - <data> exceeds buffer length.  Result may be truncated!\n", cmd);
		data_len = len - 1;
	}

	while (data_len > 0) {
		memset(curblock, 0, AES_BLOCK_SIZE);
		memcpy(curblock, tmpP, (data_len < AES_BLOCK_SIZE) ? data_len : AES_BLOCK_SIZE);
		if (encrypt) {
			ast_aes_encrypt(curblock, (unsigned char *) tmpP, &ecx);
		} else {
			ast_aes_decrypt(curblock, (unsigned char *) tmpP, &dcx);
		}
		tmpP += AES_BLOCK_SIZE;
		data_len -= AES_BLOCK_SIZE;
	}

	if (encrypt) {                            /* if encrypting encode result to base64 */
		ast_base64encode(buf, (unsigned char *) tmp, strlen(tmp), len);
	} else {
		memcpy(buf, tmp, len);
	}
	ast_free(tmp);

	return 0;
}

static struct ast_custom_function aes_encrypt_function = {
	.name = "AES_ENCRYPT",
	.read = aes_helper,
};

static struct ast_custom_function aes_decrypt_function = {
	.name = "AES_DECRYPT",
	.read = aes_helper,
};

static int load_module(void)
{
	int res = ast_custom_function_register(&aes_decrypt_function);
	res |= ast_custom_function_register(&aes_encrypt_function);
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "AES dialplan functions");
