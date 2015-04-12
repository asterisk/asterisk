/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Asterisk wrapper for crypt(3)
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include <unistd.h>
#if defined(HAVE_CRYPT_R)
#include <crypt.h>
#endif

#include "asterisk/utils.h"

/*!
 * \brief Max length of a salt string.
 *
 * $[1,5,6]$[a–zA–Z0–9./]{1,16}$, plus null terminator
 */
#define MAX_SALT_LEN 21

static char salt_chars[] =
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"0123456789"
	"./";

/*! Randomly select a character for a salt string */
static char gen_salt_char(void)
{
	int which = ast_random_double() * 64;
	return salt_chars[which];
}

/*!
 * \brief Generates a salt to try with crypt.
 *
 * If given an empty string, will generate a salt for the most secure algorithm
 * to try with crypt(). If given a previously generated salt, the algorithm will
 * be lowered by one level of security.
 *
 * \param[out] current_salt Output string in which to generate the salt.
 *                          This can be an empty string, or the results of a
 *                          prior gen_salt call.
 * \param max_len Length of \a current_salt.
 * \return 0 on success.
 * \return Non-zero on error.
 */
static int gen_salt(char *current_salt, size_t maxlen)
{
	int i;

	if (maxlen < MAX_SALT_LEN || current_salt == NULL) {
		return -1;
	}

	switch (current_salt[0]) {
	case '\0':
		/* Initial generation; $6$ = SHA-512 */
		*current_salt++ = '$';
		*current_salt++ = '6';
		*current_salt++ = '$';
		for (i = 0; i < 16; ++i) {
			*current_salt++ = gen_salt_char();
		}
		*current_salt++ = '$';
		*current_salt++ = '\0';
		return 0;
	case '$':
		switch (current_salt[1]) {
		case '6':
			/* Downgrade to SHA-256 */
			current_salt[1] = '5';
			return 0;
		case '5':
			/* Downgrade to MD5 */
			current_salt[1] = '1';
			return 0;
		case '1':
			/* Downgrade to traditional crypt */
			*current_salt++ = gen_salt_char();
			*current_salt++ = gen_salt_char();
			*current_salt++ = '\0';
			return 0;
		default:
			/* Unrecognized algorithm */
			return -1;
		}
	default:
		/* Was already as insecure as it gets */
		return -1;
	}

}

#if defined(HAVE_CRYPT_R)

char *ast_crypt(const char *key, const char *salt)
{
	struct crypt_data data = {};
	const char *crypted = crypt_r(key, salt, &data);

	/* Crypt may return success even if it doesn't recognize the salt. But
	 * in those cases it always mangles the salt in some way.
	 */
	if (!crypted || !ast_begins_with(crypted, salt)) {
		return NULL;
	}

	return ast_strdup(crypted);
}

int ast_crypt_validate(const char *key, const char *expected)
{
	struct crypt_data data = {};
	return strcmp(expected, crypt_r(key, expected, &data)) == 0;
}

#elif defined(HAVE_CRYPT)

/* crypt is not reentrant. A global mutex is neither ideal nor perfect, but good
 * enough if crypt_r support is unavailable
 */
AST_MUTEX_DEFINE_STATIC(crypt_mutex);

char *ast_crypt(const char *key, const char *salt)
{
	const char *crypted;
	SCOPED_MUTEX(lock, &crypt_mutex);

	crypted = crypt(key, salt);

	/* Crypt may return success even if it doesn't recognize the salt. But
	 * in those cases it always mangles the salt in some way.
	 */
	if (!crypted || !ast_begins_with(crypted, salt)) {
		return NULL;
	}

	return ast_strdup(crypted);
}

int ast_crypt_validate(const char *key, const char *expected)
{
	SCOPED_MUTEX(lock, &crypt_mutex);
	return strcmp(expected, crypt(key, expected)) == 0;
}

#else /* No crypt support */

char *ast_crypt(const char *key, const char *salt)
{
	ast_log(LOG_WARNING,
		"crypt() support not available; cannot encrypt password\n");
	return NULL;
}

int ast_crypt_validate(const char *key, const char *expected)
{
	ast_log(LOG_WARNING,
		"crypt() support not available; cannot validate password\n");
	return 0;
}

#endif  /* No crypt support */

char *ast_crypt_encrypt(const char *key)
{
	char salt[MAX_SALT_LEN] = {};
	while (gen_salt(salt, sizeof(salt)) == 0) {
		char *crypted = ast_crypt(key, salt);
		if (crypted) {
			return crypted;
		}
	}
	return NULL;
}
