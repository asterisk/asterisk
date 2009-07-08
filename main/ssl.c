/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include "asterisk/_private.h" /* ast_ssl_init() */

#include "asterisk/utils.h"
#include "asterisk/lock.h"

#ifdef HAVE_OPENSSL

static ast_mutex_t *ssl_locks;

static int ssl_num_locks;

static unsigned long ssl_threadid(void)
{
        return pthread_self();
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

#endif /* HAVE_OPENSSL */

/*!
 * \internal
 * \brief Common OpenSSL initialization for all of Asterisk.
 */
int ast_ssl_init(void)
{
#ifdef HAVE_OPENSSL
        unsigned int i;

        SSL_library_init();
        SSL_load_error_strings();
        ERR_load_crypto_strings();
        ERR_load_BIO_strings();
        OpenSSL_add_all_algorithms();

        /* Make OpenSSL thread-safe. */

        CRYPTO_set_id_callback(ssl_threadid);

        ssl_num_locks = CRYPTO_num_locks();
        if (!(ssl_locks = ast_calloc(ssl_num_locks, sizeof(ssl_locks[0])))) {
                return -1;
        }
        for (i = 0; i < ssl_num_locks; i++) {
                ast_mutex_init(&ssl_locks[i]);
        }
        CRYPTO_set_locking_callback(ssl_lock);

#endif /* HAVE_OPENSSL */
        return 0;
}

