/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009-2012, Digium, Inc.
 *
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
 * \brief Common OpenSSL support code
 *
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include <dlfcn.h>

#include "asterisk/_private.h" /* ast_ssl_init() */

#include "asterisk/utils.h"
#include "asterisk/lock.h"

#ifdef HAVE_OPENSSL

#define get_OpenSSL_function(func) do { real_##func = dlsym(RTLD_NEXT, __stringify(func)); } while(0)

static int startup_complete;

static ast_mutex_t *ssl_locks;

static int ssl_num_locks;

static unsigned long ssl_threadid(void)
{
	return (unsigned long) pthread_self();
}

static void ssl_lock(int mode, int n, const char *file, int line)
{
	if (n < 0 || n >= ssl_num_locks) {
		ast_log(LOG_ERROR, "OpenSSL is full of LIES!!! - "
				"ssl_num_locks '%d' - n '%d'\n",
				ssl_num_locks, n);
		return;
	}

	if (mode & CRYPTO_LOCK) {
		ast_mutex_lock(&ssl_locks[n]);
	} else {
		ast_mutex_unlock(&ssl_locks[n]);
	}
}

int SSL_library_init(void)
{
#if defined(AST_DEVMODE)
	if (startup_complete) {
		ast_debug(1, "Called after startup... ignoring!\n");
	}
#endif
	return 1;
}

void SSL_load_error_strings(void)
{
#if defined(AST_DEVMODE)
	if (startup_complete) {
		ast_debug(1, "Called after startup... ignoring!\n");
	}
#endif
}

void CRYPTO_set_id_callback(unsigned long (*func)(void))
{
#if defined(AST_DEVMODE)
	if (startup_complete) {
		ast_debug(1, "Called after startup... ignoring!\n");
	}
#endif
}

void CRYPTO_set_locking_callback(void (*func)(int mode,int type, const char *file, int line))
{
#if defined(AST_DEVMODE)
	if (startup_complete) {
		ast_debug(1, "Called after startup... ignoring!\n");
	}
#endif
}

void ERR_free_strings(void)
{
	/* we can't allow this to be called, ever */
}

#endif /* HAVE_OPENSSL */

/*!
 * \internal
 * \brief Common OpenSSL initialization for all of Asterisk.
 */
int ast_ssl_init(void)
{
#ifdef HAVE_OPENSSL
	unsigned int i;
	int (*real_SSL_library_init)(void);
	void (*real_CRYPTO_set_id_callback)(unsigned long (*)(void));
	void (*real_CRYPTO_set_locking_callback)(void (*)(int, int, const char *, int));
	void (*real_SSL_load_error_strings)(void);
	const char *errstr;

	/* clear any previous dynamic linker errors */
	dlerror();
	get_OpenSSL_function(SSL_library_init);
	if ((errstr = dlerror()) != NULL) {
		ast_debug(1, "unable to get real address of SSL_library_init: %s\n", errstr);
		/* there is no way to continue in this situation... SSL will
		 * likely be broken in this process
		 */
		return -1;
	} else {
		real_SSL_library_init();
	}

	/* Make OpenSSL usage thread-safe. */

	dlerror();
	get_OpenSSL_function(CRYPTO_set_id_callback);
	if ((errstr = dlerror()) != NULL) {
		ast_debug(1, "unable to get real address of CRYPTO_set_id_callback: %s\n", errstr);
		/* there is no way to continue in this situation... SSL will
		 * likely be broken in this process
		 */
		return -1;
	} else {
		real_CRYPTO_set_id_callback(ssl_threadid);
	}

	dlerror();
	get_OpenSSL_function(CRYPTO_set_locking_callback);
	if ((errstr = dlerror()) != NULL) {
		ast_debug(1, "unable to get real address of CRYPTO_set_locking_callback: %s\n", errstr);
		/* there is no way to continue in this situation... SSL will
		 * likely be broken in this process
		 */
		return -1;
	} else {
		ssl_num_locks = CRYPTO_num_locks();
		if (!(ssl_locks = ast_calloc(ssl_num_locks, sizeof(ssl_locks[0])))) {
			return -1;
		}
		for (i = 0; i < ssl_num_locks; i++) {
			ast_mutex_init(&ssl_locks[i]);
		}
		real_CRYPTO_set_locking_callback(ssl_lock);
	}

	/* after this point, we don't check for errors from the dlsym() calls,
	 * under the assumption that if the ones above were successful, all
	 * the rest will be too. this assumption holds as long as OpenSSL still
	 * provides all of these functions.
	 */

	get_OpenSSL_function(SSL_load_error_strings);
	real_SSL_load_error_strings();

	startup_complete = 1;

#endif /* HAVE_OPENSSL */
	return 0;
}

