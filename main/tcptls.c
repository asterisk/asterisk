/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2008, Digium, Inc.
 *
 * Luigi Rizzo (TCP and TLS server code)
 * Brett Bryant <brettbryant@gmail.com> (updated for client support)
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
 * \brief Code to support TCP and TLS server/client
 *
 * \author Luigi Rizzo
 * \author Brett Bryant <brettbryant@gmail.com>
 */

#include "asterisk.h"

#include "asterisk/tcptls.h"            /* for ast_tls_config, ast_tcptls_se... */
#include "asterisk/iostream.h"          /* for DO_SSL, ast_iostream_close, a... */

#ifdef HAVE_FCNTL_H
#include <fcntl.h>                      /* for O_NONBLOCK */
#endif /* HAVE_FCNTL_H */
#include <netinet/in.h>                 /* for IPPROTO_TCP */
#ifdef DO_SSL
#include <openssl/asn1.h>               /* for ASN1_STRING_to_UTF8 */
#include <openssl/crypto.h>             /* for OPENSSL_free */
#include <openssl/opensslconf.h>        /* for OPENSSL_NO_SSL3_METHOD, OPENS... */
#include <openssl/opensslv.h>           /* for OPENSSL_VERSION_NUMBER */
#include <openssl/safestack.h>          /* for STACK_OF */
#include <openssl/ssl.h>                /* for SSL_CTX_free, SSL_get_error, ... */
#include <openssl/x509.h>               /* for X509_free, X509_NAME_ENTRY_ge... */
#include <openssl/x509v3.h>             /* for GENERAL_NAME, sk_GENERAL_NAME... */
#ifndef OPENSSL_NO_DH
#include <openssl/bio.h>                /* for BIO_free, BIO_new_file */
#include <openssl/dh.h>                 /* for DH_free */
#include <openssl/pem.h>                /* for PEM_read_bio_DHparams */
#endif /* OPENSSL_NO_DH */
#ifndef OPENSSL_NO_EC
#include <openssl/ec.h>                 /* for EC_KEY_free, EC_KEY_new_by_cu... */
#endif /* OPENSSL_NO_EC */
#endif /* DO_SSL */
#include <pthread.h>                    /* for pthread_cancel, pthread_join */
#include <signal.h>                     /* for pthread_kill, SIGURG */
#include <sys/socket.h>                 /* for setsockopt, shutdown, socket */
#include <sys/stat.h>                   /* for stat */

#include "asterisk/app.h"               /* for ast_read_textfile */
#include "asterisk/astobj2.h"           /* for ao2_ref, ao2_t_ref, ao2_alloc */
#include "asterisk/compat.h"            /* for strcasecmp */
#include "asterisk/config.h"            /* for ast_parse_arg, ast_parse_flag... */
#include "asterisk/io.h"                /* for ast_sd_get_fd */
#include "asterisk/lock.h"              /* for AST_PTHREADT_NULL */
#include "asterisk/logger.h"            /* for ast_log, LOG_ERROR, ast_debug */
#include "asterisk/netsock2.h"          /* for ast_sockaddr_copy, ast_sockad... */
#include "asterisk/pbx.h"               /* for ast_thread_inhibit_escalations */
#include "asterisk/utils.h"             /* for ast_true, ast_free, ast_wait_... */

static void session_instance_destructor(void *obj)
{
	struct ast_tcptls_session_instance *i = obj;

	if (i->stream) {
		ast_iostream_close(i->stream);
		i->stream = NULL;
	}
	ast_free(i->overflow_buf);
	ao2_cleanup(i->private_data);
}

#ifdef DO_SSL
static int check_tcptls_cert_name(ASN1_STRING *cert_str, const char *hostname, const char *desc)
{
	unsigned char *str;
	int ret;

	ret = ASN1_STRING_to_UTF8(&str, cert_str);
	if (ret < 0 || !str) {
		return -1;
	}

	if (strlen((char *) str) != ret) {
		ast_log(LOG_WARNING, "Invalid certificate %s length (contains NULL bytes?)\n", desc);

		ret = -1;
	} else if (!strcasecmp(hostname, (char *) str)) {
		ret = 0;
	} else {
		ret = -1;
	}

	ast_debug(3, "SSL %s compare s1='%s' s2='%s'\n", desc, hostname, str);
	OPENSSL_free(str);

	return ret;
}
#endif

/*! \brief
* creates a FILE * from the fd passed by the accept thread.
* This operation is potentially expensive (certificate verification),
* so we do it in the child thread context.
*
* \note must decrement ref count before returning NULL on error
*/
static void *handle_tcptls_connection(void *data)
{
	struct ast_tcptls_session_instance *tcptls_session = data;
#ifdef DO_SSL
	SSL *ssl;
#endif

	/* TCP/TLS connections are associated with external protocols, and
	 * should not be allowed to execute 'dangerous' functions. This may
	 * need to be pushed down into the individual protocol handlers, but
	 * this seems like a good general policy.
	 */
	if (ast_thread_inhibit_escalations()) {
		ast_log(LOG_ERROR, "Failed to inhibit privilege escalations; killing connection\n");
		ast_tcptls_close_session_file(tcptls_session);
		ao2_ref(tcptls_session, -1);
		return NULL;
	}

	/*
	 * TCP/TLS connections are associated with external protocols which can
	 * be considered to be user interfaces (even for SIP messages), and
	 * will not handle channel media.  This may need to be pushed down into
	 * the individual protocol handlers, but this seems like a good start.
	 */
	if (ast_thread_user_interface_set(1)) {
		ast_log(LOG_ERROR, "Failed to set user interface status; killing connection\n");
		ast_tcptls_close_session_file(tcptls_session);
		ao2_ref(tcptls_session, -1);
		return NULL;
	}

	if (tcptls_session->parent->tls_cfg) {
#ifdef DO_SSL
		if (ast_iostream_start_tls(&tcptls_session->stream, tcptls_session->parent->tls_cfg->ssl_ctx, tcptls_session->client) < 0) {
			ast_tcptls_close_session_file(tcptls_session);
			ao2_ref(tcptls_session, -1);
			return NULL;
		}

		ssl = ast_iostream_get_ssl(tcptls_session->stream);
		if ((tcptls_session->client && !ast_test_flag(&tcptls_session->parent->tls_cfg->flags, AST_SSL_DONT_VERIFY_SERVER))
			|| (!tcptls_session->client && ast_test_flag(&tcptls_session->parent->tls_cfg->flags, AST_SSL_VERIFY_CLIENT))) {
			X509 *peer;
			long res;
			peer = SSL_get_peer_certificate(ssl);
			if (!peer) {
				ast_log(LOG_ERROR, "No peer SSL certificate to verify\n");
				ast_tcptls_close_session_file(tcptls_session);
				ao2_ref(tcptls_session, -1);
				return NULL;
			}

			res = SSL_get_verify_result(ssl);
			if (res != X509_V_OK) {
				ast_log(LOG_ERROR, "Certificate did not verify: %s\n", X509_verify_cert_error_string(res));
				X509_free(peer);
				ast_tcptls_close_session_file(tcptls_session);
				ao2_ref(tcptls_session, -1);
				return NULL;
			}
			if (!ast_test_flag(&tcptls_session->parent->tls_cfg->flags, AST_SSL_IGNORE_COMMON_NAME)) {
				ASN1_STRING *str;
				X509_NAME *name = X509_get_subject_name(peer);
				STACK_OF(GENERAL_NAME) *alt_names;
				int pos = -1;
				int found = 0;

				for (;;) {
					/* Walk the certificate to check all available "Common Name" */
					/* XXX Probably should do a gethostbyname on the hostname and compare that as well */
					pos = X509_NAME_get_index_by_NID(name, NID_commonName, pos);
					if (pos < 0) {
						break;
					}
					str = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, pos));
					if (!check_tcptls_cert_name(str, tcptls_session->parent->hostname, "common name")) {
						found = 1;
						break;
					}
				}

				if (!found) {
					alt_names = X509_get_ext_d2i(peer, NID_subject_alt_name, NULL, NULL);
					if (alt_names != NULL) {
						int alt_names_count = sk_GENERAL_NAME_num(alt_names);

						for (pos = 0; pos < alt_names_count; pos++) {
							const GENERAL_NAME *alt_name = sk_GENERAL_NAME_value(alt_names, pos);

							if (alt_name->type != GEN_DNS) {
								continue;
							}

							if (!check_tcptls_cert_name(alt_name->d.dNSName, tcptls_session->parent->hostname, "alt name")) {
								found = 1;
								break;
							}
						}

						sk_GENERAL_NAME_pop_free(alt_names, GENERAL_NAME_free);
					}
				}

				if (!found) {
					ast_log(LOG_ERROR, "Certificate common name did not match (%s)\n", tcptls_session->parent->hostname);
					X509_free(peer);
					ast_tcptls_close_session_file(tcptls_session);
					ao2_ref(tcptls_session, -1);
					return NULL;
				}
			}
			X509_free(peer);
		}
#else
		ast_log(LOG_ERROR, "Attempted a TLS connection without OpenSSL support. This will not work!\n");
		ast_tcptls_close_session_file(tcptls_session);
		ao2_ref(tcptls_session, -1);
		return NULL;
#endif /* DO_SSL */
	}

	if (tcptls_session->parent->worker_fn) {
		return tcptls_session->parent->worker_fn(tcptls_session);
	} else {
		return tcptls_session;
	}
}

void *ast_tcptls_server_root(void *data)
{
	struct ast_tcptls_session_args *desc = data;
	int fd;
	struct ast_sockaddr addr;
	struct ast_tcptls_session_instance *tcptls_session;
	pthread_t launched;

	for (;;) {
		int i;

		if (desc->periodic_fn) {
			desc->periodic_fn(desc);
		}
		i = ast_wait_for_input(desc->accept_fd, desc->poll_timeout);
		if (i <= 0) {
			/* Prevent tight loop from hogging CPU */
			usleep(1);
			continue;
		}
		fd = ast_accept(desc->accept_fd, &addr);
		if (fd < 0) {
			if (errno != EAGAIN
				&& errno != EWOULDBLOCK
				&& errno != EINTR
				&& errno != ECONNABORTED) {
				ast_log(LOG_ERROR, "TCP/TLS accept failed: %s\n", strerror(errno));
				if (errno != EMFILE) {
					break;
				}
			}
			/* Prevent tight loop from hogging CPU */
			usleep(1);
			continue;
		}
		tcptls_session = ao2_alloc(sizeof(*tcptls_session), session_instance_destructor);
		if (!tcptls_session) {
			close(fd);
			continue;
		}

		tcptls_session->overflow_buf = ast_str_create(128);
		if (!tcptls_session->overflow_buf) {
			ao2_ref(tcptls_session, -1);
			close(fd);
			continue;
		}
		ast_fd_clear_flags(fd, O_NONBLOCK);

		tcptls_session->stream = ast_iostream_from_fd(&fd);
		if (!tcptls_session->stream) {
			ao2_ref(tcptls_session, -1);
			close(fd);
			continue;
		}

		tcptls_session->parent = desc;
		ast_sockaddr_copy(&tcptls_session->remote_address, &addr);

		tcptls_session->client = 0;

		/* This thread is now the only place that controls the single ref to tcptls_session */
		if (ast_pthread_create_detached_background(&launched, NULL, handle_tcptls_connection, tcptls_session)) {
			ast_log(LOG_ERROR, "TCP/TLS unable to launch helper thread: %s\n",
				strerror(errno));
			ao2_ref(tcptls_session, -1);
		}
	}

	ast_log(LOG_ERROR, "TCP/TLS listener thread ended abnormally\n");

	/* Close the listener socket so Asterisk doesn't appear dead. */
	fd = desc->accept_fd;
	desc->accept_fd = -1;
	if (0 <= fd) {
		close(fd);
	}
	return NULL;
}

#ifdef DO_SSL
static void __ssl_setup_certs(struct ast_tls_config *cfg, const size_t cert_file_len, const char *key_type_extension, const char *key_type)
{
	char *cert_file = ast_strdupa(cfg->certfile);

	memcpy(cert_file + cert_file_len - 8, key_type_extension, 5);
	if (access(cert_file, F_OK) == 0) {
		if (SSL_CTX_use_certificate_chain_file(cfg->ssl_ctx, cert_file) == 0) {
			ast_log(LOG_WARNING, "TLS/SSL error loading public %s key (certificate) from <%s>.\n", key_type, cert_file);
		} else if (SSL_CTX_use_PrivateKey_file(cfg->ssl_ctx, cert_file, SSL_FILETYPE_PEM) == 0) {
			ast_log(LOG_WARNING, "TLS/SSL error loading private %s key from <%s>.\n", key_type, cert_file);
		} else if (SSL_CTX_check_private_key(cfg->ssl_ctx) == 0) {
			ast_log(LOG_WARNING, "TLS/SSL error matching private %s key and certificate in <%s>.\n", key_type, cert_file);
		}
	}
}
#endif

static int __ssl_setup(struct ast_tls_config *cfg, int client)
{
#ifndef DO_SSL
	if (cfg->enabled) {
		ast_log(LOG_NOTICE, "Configured without OpenSSL Development Headers");
		cfg->enabled = 0;
	}
	return 0;
#else
	int disable_ssl = 0;
	long ssl_opts = 0;

	if (!cfg->enabled) {
		return 0;
	}

	/* Get rid of an old SSL_CTX since we're about to
	 * allocate a new one
	 */
	if (cfg->ssl_ctx) {
		SSL_CTX_free(cfg->ssl_ctx);
		cfg->ssl_ctx = NULL;
	}

	if (client) {
#if !defined(OPENSSL_NO_SSL2) && (OPENSSL_VERSION_NUMBER < 0x10100000L)
		if (ast_test_flag(&cfg->flags, AST_SSL_SSLV2_CLIENT)) {
			ast_log(LOG_WARNING, "Usage of SSLv2 is discouraged due to known vulnerabilities. Please use 'tlsv1' or leave the TLS method unspecified!\n");
			cfg->ssl_ctx = SSL_CTX_new(SSLv2_client_method());
		} else
#endif
#if !defined(OPENSSL_NO_SSL3_METHOD) && !(defined(OPENSSL_API_COMPAT) && (OPENSSL_API_COMPAT >= 0x10100000L))
		if (ast_test_flag(&cfg->flags, AST_SSL_SSLV3_CLIENT)) {
			ast_log(LOG_WARNING, "Usage of SSLv3 is discouraged due to known vulnerabilities. Please use 'tlsv1' or leave the TLS method unspecified!\n");
			cfg->ssl_ctx = SSL_CTX_new(SSLv3_client_method());
		} else
#endif
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
		cfg->ssl_ctx = SSL_CTX_new(TLS_client_method());
#else
		if (ast_test_flag(&cfg->flags, AST_SSL_TLSV1_CLIENT)) {
			cfg->ssl_ctx = SSL_CTX_new(TLSv1_client_method());
		} else {
			disable_ssl = 1;
			cfg->ssl_ctx = SSL_CTX_new(SSLv23_client_method());
		}
#endif
	} else {
		disable_ssl = 1;
		cfg->ssl_ctx = SSL_CTX_new(SSLv23_server_method());
	}

	if (!cfg->ssl_ctx) {
		ast_debug(1, "Sorry, SSL_CTX_new call returned null...\n");
		cfg->enabled = 0;
		return 0;
	}

	/* Due to the POODLE vulnerability, completely disable
	 * SSLv2 and SSLv3 if we are not explicitly told to use
	 * them. SSLv23_*_method supports TLSv1+.
	 */
	if (disable_ssl) {
		ssl_opts |= SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
	}

	if (ast_test_flag(&cfg->flags, AST_SSL_SERVER_CIPHER_ORDER)) {
		ssl_opts |= SSL_OP_CIPHER_SERVER_PREFERENCE;
	}

	if (ast_test_flag(&cfg->flags, AST_SSL_DISABLE_TLSV1)) {
		ssl_opts |= SSL_OP_NO_TLSv1;
	}
#if defined(SSL_OP_NO_TLSv1_1) && defined(SSL_OP_NO_TLSv1_2)
	if (ast_test_flag(&cfg->flags, AST_SSL_DISABLE_TLSV11)) {
		ssl_opts |= SSL_OP_NO_TLSv1_1;
	}
	if (ast_test_flag(&cfg->flags, AST_SSL_DISABLE_TLSV12)) {
		ssl_opts |= SSL_OP_NO_TLSv1_2;
	}
#else
	ast_log(LOG_WARNING, "Your version of OpenSSL leaves you potentially vulnerable "
			"to the SSL BEAST attack. Please upgrade to OpenSSL 1.0.1 or later\n");
#endif

	SSL_CTX_set_options(cfg->ssl_ctx, ssl_opts);

	SSL_CTX_set_verify(cfg->ssl_ctx,
		ast_test_flag(&cfg->flags, AST_SSL_VERIFY_CLIENT) ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT : SSL_VERIFY_NONE,
		NULL);

	if (!ast_strlen_zero(cfg->certfile)) {
		char *tmpprivate = ast_strlen_zero(cfg->pvtfile) ? cfg->certfile : cfg->pvtfile;
		if (SSL_CTX_use_certificate_chain_file(cfg->ssl_ctx, cfg->certfile) == 0) {
			if (!client) {
				/* Clients don't need a certificate, but if its setup we can use it */
				ast_log(LOG_ERROR, "TLS/SSL error loading cert file. <%s>\n", cfg->certfile);
				cfg->enabled = 0;
				SSL_CTX_free(cfg->ssl_ctx);
				cfg->ssl_ctx = NULL;
				return 0;
			}
		}
		if ((SSL_CTX_use_PrivateKey_file(cfg->ssl_ctx, tmpprivate, SSL_FILETYPE_PEM) == 0) || (SSL_CTX_check_private_key(cfg->ssl_ctx) == 0 )) {
			if (!client) {
				/* Clients don't need a private key, but if its setup we can use it */
				ast_log(LOG_ERROR, "TLS/SSL error loading private key file. <%s>\n", tmpprivate);
				cfg->enabled = 0;
				SSL_CTX_free(cfg->ssl_ctx);
				cfg->ssl_ctx = NULL;
				return 0;
			}
		}
		if (!client) {
			size_t certfile_len = strlen(cfg->certfile);

			/* expects a file name which contains _rsa. like asterisk_rsa.pem
			 * ignores any 3-character file-extension like .pem, .cer, .crt
			 */
			if (certfile_len >= 8 && !strncmp(cfg->certfile + certfile_len - 8, "_rsa.", 5)) {
				__ssl_setup_certs(cfg, certfile_len, "_ecc.", "ECC");
				__ssl_setup_certs(cfg, certfile_len, "_dsa.", "DSA");
			}
		}
	}
	if (!ast_strlen_zero(cfg->cipher)) {
		if (SSL_CTX_set_cipher_list(cfg->ssl_ctx, cfg->cipher) == 0 ) {
			if (!client) {
				ast_log(LOG_ERROR, "TLS/SSL cipher error <%s>\n", cfg->cipher);
				cfg->enabled = 0;
				SSL_CTX_free(cfg->ssl_ctx);
				cfg->ssl_ctx = NULL;
				return 0;
			}
		}
	}
	if (!ast_strlen_zero(cfg->cafile) || !ast_strlen_zero(cfg->capath)) {
		if (SSL_CTX_load_verify_locations(cfg->ssl_ctx, S_OR(cfg->cafile, NULL), S_OR(cfg->capath,NULL)) == 0) {
			ast_log(LOG_ERROR, "TLS/SSL CA file(%s)/path(%s) error\n", cfg->cafile, cfg->capath);
		}
	}

#ifndef OPENSSL_NO_DH
	if (!ast_strlen_zero(cfg->pvtfile)) {
		BIO *bio = BIO_new_file(cfg->pvtfile, "r");
		if (bio != NULL) {
			DH *dh = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);
			if (dh != NULL) {
				if (SSL_CTX_set_tmp_dh(cfg->ssl_ctx, dh)) {
					long options = SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_SINGLE_DH_USE | SSL_OP_SINGLE_ECDH_USE;
					options = SSL_CTX_set_options(cfg->ssl_ctx, options);
					ast_verb(2, "TLS/SSL DH initialized, PFS cipher-suites enabled\n");
				}
				DH_free(dh);
			}
			BIO_free(bio);
		}
	}
#endif

	#ifndef SSL_CTRL_SET_ECDH_AUTO
		#define SSL_CTRL_SET_ECDH_AUTO 94
	#endif
	/* SSL_CTX_set_ecdh_auto(cfg->ssl_ctx, on); requires OpenSSL 1.0.2 which wraps: */
	if (SSL_CTX_ctrl(cfg->ssl_ctx, SSL_CTRL_SET_ECDH_AUTO, 1, NULL)) {
		ast_verb(2, "TLS/SSL ECDH initialized (automatic), faster PFS ciphers enabled\n");
#if !defined(OPENSSL_NO_ECDH) && (OPENSSL_VERSION_NUMBER >= 0x10000000L) && (OPENSSL_VERSION_NUMBER < 0x10100000L)
	} else {
		/* enables AES-128 ciphers, to get AES-256 use NID_secp384r1 */
		EC_KEY *ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
		if (ecdh != NULL) {
			if (SSL_CTX_set_tmp_ecdh(cfg->ssl_ctx, ecdh)) {
				ast_verb(2, "TLS/SSL ECDH initialized (secp256r1), faster PFS cipher-suites enabled\n");
			}
			EC_KEY_free(ecdh);
		}
#endif
	}

	ast_verb(2, "TLS/SSL certificate ok\n");	/* We should log which one that is ok. This message doesn't really make sense in production use */
	return 1;
#endif
}

int ast_ssl_setup(struct ast_tls_config *cfg)
{
	return __ssl_setup(cfg, 0);
}

void ast_ssl_teardown(struct ast_tls_config *cfg)
{
#ifdef DO_SSL
	if (cfg && cfg->ssl_ctx) {
		SSL_CTX_free(cfg->ssl_ctx);
		cfg->ssl_ctx = NULL;
	}
#endif
}

struct ast_tcptls_session_instance *ast_tcptls_client_start(struct ast_tcptls_session_instance *tcptls_session)
{
	struct ast_tcptls_session_args *desc;

	if (!(desc = tcptls_session->parent)) {
		goto client_start_error;
	}

	if (ast_connect(desc->accept_fd, &desc->remote_address)) {
		ast_log(LOG_ERROR, "Unable to connect %s to %s: %s\n",
			desc->name,
			ast_sockaddr_stringify(&desc->remote_address),
			strerror(errno));
		goto client_start_error;
	}

	ast_fd_clear_flags(desc->accept_fd, O_NONBLOCK);

	if (desc->tls_cfg) {
		desc->tls_cfg->enabled = 1;
		__ssl_setup(desc->tls_cfg, 1);
	}

	return handle_tcptls_connection(tcptls_session);

client_start_error:
	if (desc) {
		close(desc->accept_fd);
		desc->accept_fd = -1;
	}
	ao2_ref(tcptls_session, -1);
	return NULL;

}

struct ast_tcptls_session_instance *ast_tcptls_client_create(struct ast_tcptls_session_args *desc)
{
	int fd, x = 1;
	struct ast_tcptls_session_instance *tcptls_session = NULL;

	/* Do nothing if nothing has changed */
	if (!ast_sockaddr_cmp(&desc->old_address, &desc->remote_address)) {
		ast_debug(1, "Nothing changed in %s\n", desc->name);
		return NULL;
	}

	/* If we return early, there is no connection */
	ast_sockaddr_setnull(&desc->old_address);

	if (desc->accept_fd != -1) {
		close(desc->accept_fd);
	}

	fd = desc->accept_fd = socket(ast_sockaddr_is_ipv6(&desc->remote_address) ?
				      AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (desc->accept_fd < 0) {
		ast_log(LOG_ERROR, "Unable to allocate socket for %s: %s\n",
			desc->name, strerror(errno));
		return NULL;
	}

	/* if a local address was specified, bind to it so the connection will
	   originate from the desired address */
	if (!ast_sockaddr_isnull(&desc->local_address) &&
	    !ast_sockaddr_is_any(&desc->local_address)) {
		setsockopt(desc->accept_fd, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
		if (ast_bind(desc->accept_fd, &desc->local_address)) {
			ast_log(LOG_ERROR, "Unable to bind %s to %s: %s\n",
				desc->name,
				ast_sockaddr_stringify(&desc->local_address),
				strerror(errno));
			goto error;
		}
	}

	tcptls_session = ao2_alloc(sizeof(*tcptls_session), session_instance_destructor);
	if (!tcptls_session) {
		goto error;
	}

	tcptls_session->overflow_buf = ast_str_create(128);
	if (!tcptls_session->overflow_buf) {
		goto error;
	}
	tcptls_session->client = 1;
	tcptls_session->stream = ast_iostream_from_fd(&fd);
	if (!tcptls_session->stream) {
		goto error;
	}

	tcptls_session->parent = desc;
	tcptls_session->parent->worker_fn = NULL;
	ast_sockaddr_copy(&tcptls_session->remote_address,
			  &desc->remote_address);

	/* Set current info */
	ast_sockaddr_copy(&desc->old_address, &desc->remote_address);
	return tcptls_session;

error:
	close(desc->accept_fd);
	desc->accept_fd = -1;
	ao2_cleanup(tcptls_session);
	return NULL;
}

void ast_tcptls_server_start(struct ast_tcptls_session_args *desc)
{
	int x = 1;
	int tls_changed = 0;
	int sd_socket;

	if (desc->tls_cfg) {
		char hash[41];
		char *str = NULL;
		struct stat st;

		/* Store the hashes of the TLS certificate etc. */
		if (stat(desc->tls_cfg->certfile, &st) || NULL == (str = ast_read_textfile(desc->tls_cfg->certfile))) {
			memset(hash, 0, 41);
		} else {
			ast_sha1_hash(hash, str);
		}
		ast_free(str);
		str = NULL;
		memcpy(desc->tls_cfg->certhash, hash, 41);
		if (stat(desc->tls_cfg->pvtfile, &st) || NULL == (str = ast_read_textfile(desc->tls_cfg->pvtfile))) {
			memset(hash, 0, 41);
		} else {
			ast_sha1_hash(hash, str);
		}
		ast_free(str);
		str = NULL;
		memcpy(desc->tls_cfg->pvthash, hash, 41);
		if (stat(desc->tls_cfg->cafile, &st) || NULL == (str = ast_read_textfile(desc->tls_cfg->cafile))) {
			memset(hash, 0, 41);
		} else {
			ast_sha1_hash(hash, str);
		}
		ast_free(str);
		str = NULL;
		memcpy(desc->tls_cfg->cahash, hash, 41);

		/* Check whether TLS configuration has changed */
		if (!desc->old_tls_cfg) { /* No previous configuration */
			tls_changed = 1;
			desc->old_tls_cfg = ast_calloc(1, sizeof(*desc->old_tls_cfg));
		} else if (memcmp(desc->tls_cfg->certhash, desc->old_tls_cfg->certhash, 41)) {
			tls_changed = 1;
		} else if (memcmp(desc->tls_cfg->pvthash, desc->old_tls_cfg->pvthash, 41)) {
			tls_changed = 1;
		} else if (strcmp(desc->tls_cfg->cipher, desc->old_tls_cfg->cipher)) {
			tls_changed = 1;
		} else if (memcmp(desc->tls_cfg->cahash, desc->old_tls_cfg->cahash, 41)) {
			tls_changed = 1;
		} else if (strcmp(desc->tls_cfg->capath, desc->old_tls_cfg->capath)) {
			tls_changed = 1;
		} else if (memcmp(&desc->tls_cfg->flags, &desc->old_tls_cfg->flags, sizeof(desc->tls_cfg->flags))) {
			tls_changed = 1;
		}

		if (tls_changed) {
			ast_debug(1, "Changed parameters for %s found\n", desc->name);
		}
	}

	/* Do nothing if nothing has changed */
	if (!tls_changed && !ast_sockaddr_cmp(&desc->old_address, &desc->local_address)) {
		ast_debug(1, "Nothing changed in %s\n", desc->name);
		return;
	}

	/* If we return early, there is no one listening */
	ast_sockaddr_setnull(&desc->old_address);

	/* Shutdown a running server if there is one */
	if (desc->master != AST_PTHREADT_NULL) {
		pthread_cancel(desc->master);
		pthread_kill(desc->master, SIGURG);
		pthread_join(desc->master, NULL);
	}

	sd_socket = ast_sd_get_fd(SOCK_STREAM, &desc->local_address);

	if (sd_socket != -1) {
		if (desc->accept_fd != sd_socket) {
			if (desc->accept_fd != -1) {
				close(desc->accept_fd);
			}
			desc->accept_fd = sd_socket;
		}

		goto systemd_socket_activation;
	}

	if (desc->accept_fd != -1) {
		close(desc->accept_fd);
	}

	/* If there's no new server, stop here */
	if (ast_sockaddr_isnull(&desc->local_address)) {
		ast_debug(2, "Server disabled:  %s\n", desc->name);
		return;
	}

	desc->accept_fd = socket(ast_sockaddr_is_ipv6(&desc->local_address) ?
				 AF_INET6 : AF_INET, SOCK_STREAM, 0);
	if (desc->accept_fd < 0) {
		ast_log(LOG_ERROR, "Unable to allocate socket for %s: %s\n", desc->name, strerror(errno));
		return;
	}

	setsockopt(desc->accept_fd, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
	if (ast_bind(desc->accept_fd, &desc->local_address)) {
		ast_log(LOG_ERROR, "Unable to bind %s to %s: %s\n",
			desc->name,
			ast_sockaddr_stringify(&desc->local_address),
			strerror(errno));
		goto error;
	}
	if (listen(desc->accept_fd, 10)) {
		ast_log(LOG_ERROR, "Unable to listen for %s!\n", desc->name);
		goto error;
	}

systemd_socket_activation:
	ast_fd_set_flags(desc->accept_fd, O_NONBLOCK);
	if (ast_pthread_create_background(&desc->master, NULL, desc->accept_fn, desc)) {
		ast_log(LOG_ERROR, "Unable to launch thread for %s on %s: %s\n",
			desc->name,
			ast_sockaddr_stringify(&desc->local_address),
			strerror(errno));
		goto error;
	}

	/* Set current info */
	ast_sockaddr_copy(&desc->old_address, &desc->local_address);
	if (desc->old_tls_cfg) {
		ast_free(desc->old_tls_cfg->certfile);
		ast_free(desc->old_tls_cfg->pvtfile);
		ast_free(desc->old_tls_cfg->cipher);
		ast_free(desc->old_tls_cfg->cafile);
		ast_free(desc->old_tls_cfg->capath);
		desc->old_tls_cfg->certfile = ast_strdup(desc->tls_cfg->certfile);
		desc->old_tls_cfg->pvtfile = ast_strdup(desc->tls_cfg->pvtfile);
		desc->old_tls_cfg->cipher = ast_strdup(desc->tls_cfg->cipher);
		desc->old_tls_cfg->cafile = ast_strdup(desc->tls_cfg->cafile);
		desc->old_tls_cfg->capath = ast_strdup(desc->tls_cfg->capath);
		memcpy(desc->old_tls_cfg->certhash, desc->tls_cfg->certhash, 41);
		memcpy(desc->old_tls_cfg->pvthash, desc->tls_cfg->pvthash, 41);
		memcpy(desc->old_tls_cfg->cahash, desc->tls_cfg->cahash, 41);
		memcpy(&desc->old_tls_cfg->flags, &desc->tls_cfg->flags, sizeof(desc->old_tls_cfg->flags));
	}

	return;

error:
	close(desc->accept_fd);
	desc->accept_fd = -1;
}

void ast_tcptls_close_session_file(struct ast_tcptls_session_instance *tcptls_session)
{
	if (tcptls_session->stream) {
		ast_iostream_close(tcptls_session->stream);
		tcptls_session->stream = NULL;
	} else {
		ast_debug(1, "ast_tcptls_close_session_file invoked on session instance without file or file descriptor\n");
	}
}

void ast_tcptls_server_stop(struct ast_tcptls_session_args *desc)
{
	if (desc->master != AST_PTHREADT_NULL) {
		pthread_cancel(desc->master);
		pthread_kill(desc->master, SIGURG);
		pthread_join(desc->master, NULL);
		desc->master = AST_PTHREADT_NULL;
	}
	if (desc->accept_fd != -1) {
		close(desc->accept_fd);
	}
	desc->accept_fd = -1;

	if (desc->old_tls_cfg) {
		ast_free(desc->old_tls_cfg->certfile);
		ast_free(desc->old_tls_cfg->pvtfile);
		ast_free(desc->old_tls_cfg->cipher);
		ast_free(desc->old_tls_cfg->cafile);
		ast_free(desc->old_tls_cfg->capath);
		ast_free(desc->old_tls_cfg);
		desc->old_tls_cfg = NULL;
	}

	ast_debug(2, "Stopped server :: %s\n", desc->name);
}

int ast_tls_read_conf(struct ast_tls_config *tls_cfg, struct ast_tcptls_session_args *tls_desc, const char *varname, const char *value)
{
	if (!strcasecmp(varname, "tlsenable") || !strcasecmp(varname, "sslenable")) {
		tls_cfg->enabled = ast_true(value) ? 1 : 0;
	} else if (!strcasecmp(varname, "tlscertfile") || !strcasecmp(varname, "sslcert") || !strcasecmp(varname, "tlscert")) {
		ast_free(tls_cfg->certfile);
		tls_cfg->certfile = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlsprivatekey") || !strcasecmp(varname, "sslprivatekey")) {
		ast_free(tls_cfg->pvtfile);
		tls_cfg->pvtfile = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlscipher") || !strcasecmp(varname, "sslcipher")) {
		ast_free(tls_cfg->cipher);
		tls_cfg->cipher = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlscafile")) {
		ast_free(tls_cfg->cafile);
		tls_cfg->cafile = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlscapath") || !strcasecmp(varname, "tlscadir")) {
		ast_free(tls_cfg->capath);
		tls_cfg->capath = ast_strdup(value);
	} else if (!strcasecmp(varname, "tlsverifyclient")) {
		ast_set2_flag(&tls_cfg->flags, ast_true(value), AST_SSL_VERIFY_CLIENT);
	} else if (!strcasecmp(varname, "tlsdontverifyserver")) {
		ast_set2_flag(&tls_cfg->flags, ast_true(value), AST_SSL_DONT_VERIFY_SERVER);
	} else if (!strcasecmp(varname, "tlsbindaddr") || !strcasecmp(varname, "sslbindaddr")) {
		if (ast_parse_arg(value, PARSE_ADDR, &tls_desc->local_address))
			ast_log(LOG_ERROR, "Invalid %s '%s'\n", varname, value);
	} else if (!strcasecmp(varname, "tlsclientmethod") || !strcasecmp(varname, "sslclientmethod")) {
		if (!strcasecmp(value, "tlsv1")) {
			ast_set_flag(&tls_cfg->flags, AST_SSL_TLSV1_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_SSLV3_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_SSLV2_CLIENT);
		} else if (!strcasecmp(value, "sslv3")) {
			ast_set_flag(&tls_cfg->flags, AST_SSL_SSLV3_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_SSLV2_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_TLSV1_CLIENT);
		} else if (!strcasecmp(value, "sslv2")) {
			ast_set_flag(&tls_cfg->flags, AST_SSL_SSLV2_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_TLSV1_CLIENT);
			ast_clear_flag(&tls_cfg->flags, AST_SSL_SSLV3_CLIENT);
		}
	} else if (!strcasecmp(varname, "tlsservercipherorder")) {
		ast_set2_flag(&tls_cfg->flags, ast_true(value), AST_SSL_SERVER_CIPHER_ORDER);
	} else if (!strcasecmp(varname, "tlsdisablev1")) {
		ast_set2_flag(&tls_cfg->flags, ast_true(value), AST_SSL_DISABLE_TLSV1);
	} else if (!strcasecmp(varname, "tlsdisablev11")) {
		ast_set2_flag(&tls_cfg->flags, ast_true(value), AST_SSL_DISABLE_TLSV11);
	} else if (!strcasecmp(varname, "tlsdisablev12")) {
		ast_set2_flag(&tls_cfg->flags, ast_true(value), AST_SSL_DISABLE_TLSV12);
	} else {
		return -1;
	}

	return 0;
}
