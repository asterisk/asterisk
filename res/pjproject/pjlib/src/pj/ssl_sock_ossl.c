/* $Id$ */
/* 
 * Copyright (C) 2009-2011 Teluu Inc. (http://www.teluu.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 */
#include <pj/ssl_sock.h>
#include <pj/activesock.h>
#include <pj/compat/socket.h>
#include <pj/assert.h>
#include <pj/errno.h>
#include <pj/list.h>
#include <pj/lock.h>
#include <pj/log.h>
#include <pj/math.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/timer.h>


/* Only build when PJ_HAS_SSL_SOCK is enabled */
#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK!=0

#define THIS_FILE		"ssl_sock_ossl.c"

/* Workaround for ticket #985 */
#define DELAYED_CLOSE_TIMEOUT	200

/* Maximum ciphers */
#define MAX_CIPHERS		100

/* 
 * Include OpenSSL headers 
 */
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>


#ifdef _MSC_VER
# ifdef _DEBUG
#  pragma comment( lib, "libeay32MTd")
#  pragma comment( lib, "ssleay32MTd")
#else
#  pragma comment( lib, "libeay32MT")
#  pragma comment( lib, "ssleay32MT")
# endif
#endif


/*
 * SSL/TLS state enumeration.
 */
enum ssl_state {
    SSL_STATE_NULL,
    SSL_STATE_HANDSHAKING,
    SSL_STATE_ESTABLISHED
};

/*
 * Internal timer types.
 */
enum timer_id
{
    TIMER_NONE,
    TIMER_HANDSHAKE_TIMEOUT,
    TIMER_CLOSE
};

/*
 * Structure of SSL socket read buffer.
 */
typedef struct read_data_t
{
    void		 *data;
    pj_size_t		  len;
} read_data_t;

/*
 * Get the offset of pointer to read-buffer of SSL socket from read-buffer
 * of active socket. Note that both SSL socket and active socket employ 
 * different but correlated read-buffers (as much as async_cnt for each),
 * and to make it easier/faster to find corresponding SSL socket's read-buffer
 * from known active socket's read-buffer, the pointer of corresponding 
 * SSL socket's read-buffer is stored right after the end of active socket's
 * read-buffer.
 */
#define OFFSET_OF_READ_DATA_PTR(ssock, asock_rbuf) \
					(read_data_t**) \
					((pj_int8_t*)(asock_rbuf) + \
					ssock->param.read_buffer_size)

/*
 * Structure of SSL socket write buffer.
 */
typedef struct write_data_t {
    pj_ioqueue_op_key_t	 key;
    pj_size_t 	 	 record_len;
    pj_ioqueue_op_key_t	*app_key;
    pj_size_t 	 	 plain_data_len;
    pj_size_t 	 	 data_len;
    unsigned		 flags;
    union {
	char		 content[1];
	const char	*ptr;
    } data;
} write_data_t;

/*
 * Structure of SSL socket write state.
 */
typedef struct write_state_t {
    char		*buf;
    pj_size_t		 max_len;    
    char		*start;
    pj_size_t		 len;
    write_data_t	*last_data;
} write_state_t;

/*
 * Structure of write data pending.
 */
typedef struct write_pending_t {
    PJ_DECL_LIST_MEMBER(struct write_pending_t);
    write_data_t	 data;
} write_pending_t;

/*
 * Secure socket structure definition.
 */
struct pj_ssl_sock_t
{
    pj_pool_t		 *pool;
    pj_ssl_sock_t	 *parent;
    pj_ssl_sock_param	  param;
    pj_ssl_cert_t	 *cert;
    
    pj_ssl_cert_info	  local_cert_info;
    pj_ssl_cert_info	  remote_cert_info;

    pj_bool_t		  is_server;
    enum ssl_state	  ssl_state;
    pj_ioqueue_op_key_t	  handshake_op_key;
    pj_timer_entry	  timer;
    pj_status_t		  verify_status;

    pj_sock_t		  sock;
    pj_activesock_t	 *asock;

    pj_sockaddr		  local_addr;
    pj_sockaddr		  rem_addr;
    int			  addr_len;
    
    pj_bool_t		  read_started;
    pj_size_t		  read_size;
    pj_uint32_t		  read_flags;
    void		**asock_rbuf;
    read_data_t		 *ssock_rbuf;

    write_state_t	  write_state;
    write_pending_t	  write_pending;
    write_pending_t	  write_pending_empty;
    pj_lock_t		 *write_mutex; /* protect write BIO and write_state */

    SSL_CTX		 *ossl_ctx;
    SSL			 *ossl_ssl;
    BIO			 *ossl_rbio;
    BIO			 *ossl_wbio;
};


/*
 * Certificate/credential structure definition.
 */
struct pj_ssl_cert_t
{
    pj_str_t CA_file;
    pj_str_t cert_file;
    pj_str_t privkey_file;
    pj_str_t privkey_pass;
};


static pj_status_t flush_delayed_send(pj_ssl_sock_t *ssock);

/*
 *******************************************************************
 * Static/internal functions.
 *******************************************************************
 */

/**
 * Mapping from OpenSSL error codes to pjlib error space.
 */

#define PJ_SSL_ERRNO_START		(PJ_ERRNO_START_USER + \
					 PJ_ERRNO_SPACE_SIZE*6)

#define PJ_SSL_ERRNO_SPACE_SIZE		PJ_ERRNO_SPACE_SIZE

#define STATUS_FROM_SSL_ERR(err, status) { \
    status = ERR_GET_LIB(err)*300 + ERR_GET_REASON(err);\
    pj_assert(status < PJ_SSL_ERRNO_SPACE_SIZE);\
    if (status) status += PJ_SSL_ERRNO_START;\
}

#define GET_SSL_STATUS(status) { \
    unsigned long e = ERR_get_error();\
    STATUS_FROM_SSL_ERR(e, status);\
}

/*
 * Get error string of OpenSSL.
 */
static pj_str_t ssl_strerror(pj_status_t status, 
			     char *buf, pj_size_t bufsize)
{
    pj_str_t errstr;
    unsigned long ssl_err = status;

    if (ssl_err) {
	unsigned long l, r;
	ssl_err -= PJ_SSL_ERRNO_START;
	l = ssl_err/300;
	r = ssl_err%300;
	ssl_err = ERR_PACK(l, 0, r);
    }

#if defined(PJ_HAS_ERROR_STRING) && (PJ_HAS_ERROR_STRING != 0)

    {
	const char *tmp = NULL;

	if (ssl_err >= 300)
	    tmp = ERR_reason_error_string(ssl_err);
	else
	    tmp = X509_verify_cert_error_string(ssl_err);

	if (tmp) {
	    pj_ansi_strncpy(buf, tmp, bufsize);
	    errstr = pj_str(buf);
	    return errstr;
	}
    }

#endif	/* PJ_HAS_ERROR_STRING */

    errstr.ptr = buf;
    errstr.slen = pj_ansi_snprintf(buf, bufsize, 
				   "Unknown OpenSSL error %lu",
				   ssl_err);

    return errstr;
}


/* OpenSSL library initialization counter */
static int openssl_init_count;

/* OpenSSL available ciphers */
static unsigned openssl_cipher_num;
static struct openssl_ciphers_t {
    pj_ssl_cipher    id;
    const char	    *name;
} openssl_ciphers[MAX_CIPHERS];

/* OpenSSL application data index */
static int sslsock_idx;


/* Initialize OpenSSL */
static pj_status_t init_openssl(void)
{
    pj_status_t status;

    if (openssl_init_count)
	return PJ_SUCCESS;

    openssl_init_count = 1;

    /* Register error subsystem */
    status = pj_register_strerror(PJ_SSL_ERRNO_START, 
				  PJ_SSL_ERRNO_SPACE_SIZE, 
				  &ssl_strerror);
    pj_assert(status == PJ_SUCCESS);

    /* Init OpenSSL lib */
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    /* Init available ciphers */
    if (openssl_cipher_num == 0) {
	SSL_METHOD *meth = NULL;
	SSL_CTX *ctx;
	SSL *ssl;
	STACK_OF(SSL_CIPHER) *sk_cipher;
	unsigned i, n;

	meth = (SSL_METHOD*)SSLv23_server_method();
	if (!meth)
	    meth = (SSL_METHOD*)TLSv1_server_method();
	if (!meth)
	    meth = (SSL_METHOD*)SSLv3_server_method();
#ifndef OPENSSL_NO_SSL2
	if (!meth)
	    meth = (SSL_METHOD*)SSLv2_server_method();
#endif
	pj_assert(meth);

	ctx=SSL_CTX_new(meth);
	SSL_CTX_set_cipher_list(ctx, "ALL");

	ssl = SSL_new(ctx);
	sk_cipher = SSL_get_ciphers(ssl);

	n = sk_SSL_CIPHER_num(sk_cipher);
	if (n > PJ_ARRAY_SIZE(openssl_ciphers))
	    n = PJ_ARRAY_SIZE(openssl_ciphers);

	for (i = 0; i < n; ++i) {
	    SSL_CIPHER *c;
	    c = sk_SSL_CIPHER_value(sk_cipher,i);
	    openssl_ciphers[i].id = (pj_ssl_cipher)
				    (pj_uint32_t)c->id & 0x00FFFFFF;
	    openssl_ciphers[i].name = SSL_CIPHER_get_name(c);
	}

	SSL_free(ssl);
	SSL_CTX_free(ctx);

	openssl_cipher_num = n;
    }

    /* Create OpenSSL application data index for SSL socket */
    sslsock_idx = SSL_get_ex_new_index(0, "SSL socket", NULL, NULL, NULL);

    return PJ_SUCCESS;
}


/* Shutdown OpenSSL */
static void shutdown_openssl(void)
{
    PJ_UNUSED_ARG(openssl_init_count);
}


/* SSL password callback. */
static int password_cb(char *buf, int num, int rwflag, void *user_data)
{
    pj_ssl_cert_t *cert = (pj_ssl_cert_t*) user_data;

    PJ_UNUSED_ARG(rwflag);

    if(num < cert->privkey_pass.slen)
	return 0;
    
    pj_memcpy(buf, cert->privkey_pass.ptr, cert->privkey_pass.slen);
    return cert->privkey_pass.slen;
}


/* SSL password callback. */
static int verify_cb(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
    pj_ssl_sock_t *ssock;
    SSL *ossl_ssl;
    int err;

    /* Get SSL instance */
    ossl_ssl = X509_STORE_CTX_get_ex_data(x509_ctx, 
				    SSL_get_ex_data_X509_STORE_CTX_idx());
    pj_assert(ossl_ssl);

    /* Get SSL socket instance */
    ssock = SSL_get_ex_data(ossl_ssl, sslsock_idx);
    pj_assert(ssock);

    /* Store verification status */
    err = X509_STORE_CTX_get_error(x509_ctx);
    switch (err) {
    case X509_V_OK:
	break;

    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
	ssock->verify_status |= PJ_SSL_CERT_EISSUER_NOT_FOUND;
	break;

    case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
    case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
    case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
    case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
	ssock->verify_status |= PJ_SSL_CERT_EINVALID_FORMAT;
	break;

    case X509_V_ERR_CERT_NOT_YET_VALID:
    case X509_V_ERR_CERT_HAS_EXPIRED:
	ssock->verify_status |= PJ_SSL_CERT_EVALIDITY_PERIOD;
	break;

    case X509_V_ERR_UNABLE_TO_GET_CRL:
    case X509_V_ERR_CRL_NOT_YET_VALID:
    case X509_V_ERR_CRL_HAS_EXPIRED:
    case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
    case X509_V_ERR_CRL_SIGNATURE_FAILURE:
    case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
    case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
	ssock->verify_status |= PJ_SSL_CERT_ECRL_FAILURE;
	break;	

    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
    case X509_V_ERR_CERT_UNTRUSTED:
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
    case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
	ssock->verify_status |= PJ_SSL_CERT_EUNTRUSTED;
	break;	

    case X509_V_ERR_CERT_SIGNATURE_FAILURE:
    case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
    case X509_V_ERR_SUBJECT_ISSUER_MISMATCH:
    case X509_V_ERR_AKID_SKID_MISMATCH:
    case X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH:
    case X509_V_ERR_KEYUSAGE_NO_CERTSIGN:
	ssock->verify_status |= PJ_SSL_CERT_EISSUER_MISMATCH;
	break;

    case X509_V_ERR_CERT_REVOKED:
	ssock->verify_status |= PJ_SSL_CERT_EREVOKED;
	break;	

    case X509_V_ERR_INVALID_PURPOSE:
    case X509_V_ERR_CERT_REJECTED:
    case X509_V_ERR_INVALID_CA:
	ssock->verify_status |= PJ_SSL_CERT_EINVALID_PURPOSE;
	break;

    case X509_V_ERR_CERT_CHAIN_TOO_LONG: /* not really used */
    case X509_V_ERR_PATH_LENGTH_EXCEEDED:
	ssock->verify_status |= PJ_SSL_CERT_ECHAIN_TOO_LONG;
	break;

    /* Unknown errors */
    case X509_V_ERR_OUT_OF_MEM:
    default:
	ssock->verify_status |= PJ_SSL_CERT_EUNKNOWN;
	break;
    }

    /* When verification is not requested just return ok here, however
     * application can still get the verification status.
     */
    if (PJ_FALSE == ssock->param.verify_peer)
	preverify_ok = 1;

    return preverify_ok;
}

/* Setting SSL sock cipher list */
static pj_status_t set_cipher_list(pj_ssl_sock_t *ssock);


/* Create and initialize new SSL context and instance */
static pj_status_t create_ssl(pj_ssl_sock_t *ssock)
{
    SSL_METHOD *ssl_method;
    SSL_CTX *ctx;
    pj_ssl_cert_t *cert;
    int mode, rc;
    pj_status_t status;
        
    pj_assert(ssock);

    cert = ssock->cert;

    /* Make sure OpenSSL library has been initialized */
    init_openssl();

    /* Determine SSL method to use */
    switch (ssock->param.proto) {
    case PJ_SSL_SOCK_PROTO_DEFAULT:
    case PJ_SSL_SOCK_PROTO_TLS1:
	ssl_method = (SSL_METHOD*)TLSv1_method();
	break;
#ifndef OPENSSL_NO_SSL2
    case PJ_SSL_SOCK_PROTO_SSL2:
	ssl_method = (SSL_METHOD*)SSLv2_method();
	break;
#endif
    case PJ_SSL_SOCK_PROTO_SSL3:
	ssl_method = (SSL_METHOD*)SSLv3_method();
	break;
    case PJ_SSL_SOCK_PROTO_SSL23:
	ssl_method = (SSL_METHOD*)SSLv23_method();
	break;
    //case PJ_SSL_SOCK_PROTO_DTLS1:
	//ssl_method = (SSL_METHOD*)DTLSv1_method();
	//break;
    default:
	return PJ_EINVAL;
    }

    /* Create SSL context */
    ctx = SSL_CTX_new(ssl_method);
    if (ctx == NULL) {
	GET_SSL_STATUS(status);
	return status;
    }

    /* Apply credentials */
    if (cert) {
	/* Load CA list if one is specified. */
	if (cert->CA_file.slen) {

	    rc = SSL_CTX_load_verify_locations(ctx, cert->CA_file.ptr, NULL);

	    if (rc != 1) {
		GET_SSL_STATUS(status);
		PJ_LOG(1,(ssock->pool->obj_name, "Error loading CA list file "
			  "'%s'", cert->CA_file.ptr));
		SSL_CTX_free(ctx);
		return status;
	    }
	}
    
	/* Set password callback */
	if (cert->privkey_pass.slen) {
	    SSL_CTX_set_default_passwd_cb(ctx, password_cb);
	    SSL_CTX_set_default_passwd_cb_userdata(ctx, cert);
	}


	/* Load certificate if one is specified */
	if (cert->cert_file.slen) {

	    /* Load certificate chain from file into ctx */
	    rc = SSL_CTX_use_certificate_chain_file(ctx, cert->cert_file.ptr);

	    if(rc != 1) {
		GET_SSL_STATUS(status);
		PJ_LOG(1,(ssock->pool->obj_name, "Error loading certificate "
			  "chain file '%s'", cert->cert_file.ptr));
		SSL_CTX_free(ctx);
		return status;
	    }
	}


	/* Load private key if one is specified */
	if (cert->privkey_file.slen) {
	    /* Adds the first private key found in file to ctx */
	    rc = SSL_CTX_use_PrivateKey_file(ctx, cert->privkey_file.ptr, 
					     SSL_FILETYPE_PEM);

	    if(rc != 1) {
		GET_SSL_STATUS(status);
		PJ_LOG(1,(ssock->pool->obj_name, "Error adding private key "
			  "from '%s'", cert->privkey_file.ptr));
		SSL_CTX_free(ctx);
		return status;
	    }
	}
    }

    /* Create SSL instance */
    ssock->ossl_ctx = ctx;
    ssock->ossl_ssl = SSL_new(ssock->ossl_ctx);
    if (ssock->ossl_ssl == NULL) {
	GET_SSL_STATUS(status);
	return status;
    }

    /* Set SSL sock as application data of SSL instance */
    SSL_set_ex_data(ssock->ossl_ssl, sslsock_idx, ssock);

    /* SSL verification options */
    mode = SSL_VERIFY_PEER;
    if (ssock->is_server && ssock->param.require_client_cert)
	mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;

    SSL_set_verify(ssock->ossl_ssl, mode, &verify_cb);

    /* Set cipher list */
    status = set_cipher_list(ssock);
    if (status != PJ_SUCCESS)
	return status;

    /* Setup SSL BIOs */
    ssock->ossl_rbio = BIO_new(BIO_s_mem());
    ssock->ossl_wbio = BIO_new(BIO_s_mem());
    BIO_set_close(ssock->ossl_rbio, BIO_CLOSE);
    BIO_set_close(ssock->ossl_wbio, BIO_CLOSE);
    SSL_set_bio(ssock->ossl_ssl, ssock->ossl_rbio, ssock->ossl_wbio);

    return PJ_SUCCESS;
}


/* Destroy SSL context and instance */
static void destroy_ssl(pj_ssl_sock_t *ssock)
{
    /* Destroy SSL instance */
    if (ssock->ossl_ssl) {
	SSL_shutdown(ssock->ossl_ssl);
	SSL_free(ssock->ossl_ssl); /* this will also close BIOs */
	ssock->ossl_ssl = NULL;
    }

    /* Destroy SSL context */
    if (ssock->ossl_ctx) {
	SSL_CTX_free(ssock->ossl_ctx);
	ssock->ossl_ctx = NULL;
    }

    /* Potentially shutdown OpenSSL library if this is the last
     * context exists.
     */
    shutdown_openssl();
}


/* Reset SSL socket state */
static void reset_ssl_sock_state(pj_ssl_sock_t *ssock)
{
    ssock->ssl_state = SSL_STATE_NULL;

    destroy_ssl(ssock);

    if (ssock->asock) {
	pj_activesock_close(ssock->asock);
	ssock->asock = NULL;
	ssock->sock = PJ_INVALID_SOCKET;
    }
    if (ssock->sock != PJ_INVALID_SOCKET) {
	pj_sock_close(ssock->sock);
	ssock->sock = PJ_INVALID_SOCKET;
    }

    /* Upon error, OpenSSL may leave any error description in the thread 
     * error queue, which sometime may cause next call to SSL API returning
     * false error alarm, e.g: in Linux, SSL_CTX_use_certificate_chain_file()
     * returning false error after a handshake error (in different SSL_CTX!).
     * For now, just clear thread error queue here.
     */
    ERR_clear_error();
}


/* Generate cipher list with user preference order in OpenSSL format */
static pj_status_t set_cipher_list(pj_ssl_sock_t *ssock)
{
    char buf[1024];
    pj_str_t cipher_list;
    STACK_OF(SSL_CIPHER) *sk_cipher;
    unsigned i;
    int j, ret;

    if (ssock->param.ciphers_num == 0)
	return PJ_SUCCESS;

    pj_strset(&cipher_list, buf, 0);

    /* Set SSL with ALL available ciphers */
    SSL_set_cipher_list(ssock->ossl_ssl, "ALL");

    /* Generate user specified cipher list in OpenSSL format */
    sk_cipher = SSL_get_ciphers(ssock->ossl_ssl);
    for (i = 0; i < ssock->param.ciphers_num; ++i) {
	for (j = 0; j < sk_SSL_CIPHER_num(sk_cipher); ++j) {
	    SSL_CIPHER *c;
	    c = sk_SSL_CIPHER_value(sk_cipher, j);
	    if (ssock->param.ciphers[i] == (pj_ssl_cipher)
					   ((pj_uint32_t)c->id & 0x00FFFFFF))
	    {
		const char *c_name;

		c_name = SSL_CIPHER_get_name(c);

		/* Check buffer size */
		if (cipher_list.slen + pj_ansi_strlen(c_name) + 2 > sizeof(buf)) {
		    pj_assert(!"Insufficient temporary buffer for cipher");
		    return PJ_ETOOMANY;
		}

		/* Add colon separator */
		if (cipher_list.slen)
		    pj_strcat2(&cipher_list, ":");

		/* Add the cipher */
		pj_strcat2(&cipher_list, c_name);
		break;
	    }
	}
    }

    /* Put NULL termination in the generated cipher list */
    cipher_list.ptr[cipher_list.slen] = '\0';

    /* Finally, set chosen cipher list */
    ret = SSL_set_cipher_list(ssock->ossl_ssl, buf);
    if (ret < 1) {
	pj_status_t status;
	GET_SSL_STATUS(status);
	return status;
    }

    return PJ_SUCCESS;
}


/* Parse OpenSSL ASN1_TIME to pj_time_val and GMT info */
static pj_bool_t parse_ossl_asn1_time(pj_time_val *tv, pj_bool_t *gmt,
				      const ASN1_TIME *tm)
{
    unsigned long parts[7] = {0};
    char *p, *end;
    unsigned len;
    pj_bool_t utc;
    pj_parsed_time pt;
    int i;

    utc = tm->type == V_ASN1_UTCTIME;
    p = (char*)tm->data;
    len = tm->length;
    end = p + len - 1;

    /* GMT */
    *gmt = (*end == 'Z');

    /* parse parts */
    for (i = 0; i < 7 && p < end; ++i) {
	pj_str_t st;

	if (i==0 && !utc) {
	    /* 4 digits year part for non-UTC time format */
	    st.slen = 4;
	} else if (i==6) {
	    /* fraction of seconds */
	    if (*p == '.') ++p;
	    st.slen = end - p + 1;
	} else {
	    /* other parts always 2 digits length */
	    st.slen = 2;
	}
	st.ptr = p;

	parts[i] = pj_strtoul(&st);
	p += st.slen;
    }

    /* encode parts to pj_time_val */
    pt.year = parts[0];
    if (utc)
	pt.year += (pt.year < 50)? 2000:1900;
    pt.mon = parts[1] - 1;
    pt.day = parts[2];
    pt.hour = parts[3];
    pt.min = parts[4];
    pt.sec = parts[5];
    pt.msec = parts[6];

    pj_time_encode(&pt, tv);

    return PJ_TRUE;
}


/* Get Common Name field string from a general name string */
static void get_cn_from_gen_name(const pj_str_t *gen_name, pj_str_t *cn)
{
    pj_str_t CN_sign = {"/CN=", 4};
    char *p, *q;

    pj_bzero(cn, sizeof(cn));

    p = pj_strstr(gen_name, &CN_sign);
    if (!p)
	return;

    p += 4; /* shift pointer to value part */
    pj_strset(cn, p, gen_name->slen - (p - gen_name->ptr));
    q = pj_strchr(cn, '/');
    if (q)
	cn->slen = q - p;
}


/* Get certificate info from OpenSSL X509, in case the certificate info
 * hal already populated, this function will check if the contents need 
 * to be updated by inspecting the issuer and the serial number.
 */
static void get_cert_info(pj_pool_t *pool, pj_ssl_cert_info *ci, X509 *x)
{
    pj_bool_t update_needed;
    char buf[512];
    pj_uint8_t serial_no[64] = {0}; /* should be >= sizeof(ci->serial_no) */
    pj_uint8_t *p;
    unsigned len;
    GENERAL_NAMES *names = NULL;

    pj_assert(pool && ci && x);

    /* Get issuer */
    X509_NAME_oneline(X509_get_issuer_name(x), buf, sizeof(buf));

    /* Get serial no */
    p = (pj_uint8_t*) M_ASN1_STRING_data(X509_get_serialNumber(x));
    len = M_ASN1_STRING_length(X509_get_serialNumber(x));
    if (len > sizeof(ci->serial_no)) 
	len = sizeof(ci->serial_no);
    pj_memcpy(serial_no + sizeof(ci->serial_no) - len, p, len);

    /* Check if the contents need to be updated. */
    update_needed = pj_strcmp2(&ci->issuer.info, buf) || 
	            pj_memcmp(ci->serial_no, serial_no, sizeof(ci->serial_no));
    if (!update_needed)
	return;

    /* Update cert info */

    pj_bzero(ci, sizeof(pj_ssl_cert_info));

    /* Version */
    ci->version = X509_get_version(x) + 1;

    /* Issuer */
    pj_strdup2(pool, &ci->issuer.info, buf);
    get_cn_from_gen_name(&ci->issuer.info, &ci->issuer.cn);

    /* Serial number */
    pj_memcpy(ci->serial_no, serial_no, sizeof(ci->serial_no));

    /* Subject */
    pj_strdup2(pool, &ci->subject.info, 
	       X509_NAME_oneline(X509_get_subject_name(x),
				 buf, sizeof(buf)));
    get_cn_from_gen_name(&ci->subject.info, &ci->subject.cn);

    /* Validity */
    parse_ossl_asn1_time(&ci->validity.start, &ci->validity.gmt,
			 X509_get_notBefore(x));
    parse_ossl_asn1_time(&ci->validity.end, &ci->validity.gmt,
			 X509_get_notAfter(x));

    /* Subject Alternative Name extension */
    if (ci->version >= 3) {
	names = (GENERAL_NAMES*) X509_get_ext_d2i(x, NID_subject_alt_name,
						  NULL, NULL);
    }
    if (names) {
        unsigned i, cnt;

        cnt = sk_GENERAL_NAME_num(names);
	ci->subj_alt_name.entry = pj_pool_calloc(pool, cnt, 
					    sizeof(*ci->subj_alt_name.entry));

        for (i = 0; i < cnt; ++i) {
	    unsigned char *p = 0;
	    pj_ssl_cert_name_type type = PJ_SSL_CERT_NAME_UNKNOWN;
            const GENERAL_NAME *name;
	    
	    name = sk_GENERAL_NAME_value(names, i);

            switch (name->type) {
                case GEN_EMAIL:
                    len = ASN1_STRING_to_UTF8(&p, name->d.ia5);
		    type = PJ_SSL_CERT_NAME_RFC822;
                    break;
                case GEN_DNS:
                    len = ASN1_STRING_to_UTF8(&p, name->d.ia5);
		    type = PJ_SSL_CERT_NAME_DNS;
                    break;
                case GEN_URI:
                    len = ASN1_STRING_to_UTF8(&p, name->d.ia5);
		    type = PJ_SSL_CERT_NAME_URI;
                    break;
                case GEN_IPADD:
		    p = ASN1_STRING_data(name->d.ip);
		    len = ASN1_STRING_length(name->d.ip);
		    type = PJ_SSL_CERT_NAME_IP;
                    break;
		default:
		    break;
            }

	    if (p && len && type != PJ_SSL_CERT_NAME_UNKNOWN) {
		ci->subj_alt_name.entry[ci->subj_alt_name.cnt].type = type;
		if (type == PJ_SSL_CERT_NAME_IP) {
		    int af = pj_AF_INET();
		    if (len == sizeof(pj_in6_addr)) af = pj_AF_INET6();
		    pj_inet_ntop2(af, p, buf, sizeof(buf));
		    pj_strdup2(pool, 
		          &ci->subj_alt_name.entry[ci->subj_alt_name.cnt].name,
		          buf);
		} else {
		    pj_strdup2(pool, 
			  &ci->subj_alt_name.entry[ci->subj_alt_name.cnt].name, 
			  (char*)p);
		    OPENSSL_free(p);
		}
		ci->subj_alt_name.cnt++;
	    }
        }
    }
}


/* Update local & remote certificates info. This function should be
 * called after handshake or renegotiation successfully completed.
 */
static void update_certs_info(pj_ssl_sock_t *ssock)
{
    X509 *x;

    pj_assert(ssock->ssl_state == SSL_STATE_ESTABLISHED);

    /* Active local certificate */
    x = SSL_get_certificate(ssock->ossl_ssl);
    if (x) {
	get_cert_info(ssock->pool, &ssock->local_cert_info, x);
	/* Don't free local's X509! */
    } else {
	pj_bzero(&ssock->local_cert_info, sizeof(pj_ssl_cert_info));
    }

    /* Active remote certificate */
    x = SSL_get_peer_certificate(ssock->ossl_ssl);
    if (x) {
	get_cert_info(ssock->pool, &ssock->remote_cert_info, x);
	/* Free peer's X509 */
	X509_free(x);
    } else {
	pj_bzero(&ssock->remote_cert_info, sizeof(pj_ssl_cert_info));
    }
}


/* When handshake completed:
 * - notify application
 * - if handshake failed, reset SSL state
 * - return PJ_FALSE when SSL socket instance is destroyed by application.
 */
static pj_bool_t on_handshake_complete(pj_ssl_sock_t *ssock, 
				       pj_status_t status)
{
    /* Cancel handshake timer */
    if (ssock->timer.id == TIMER_HANDSHAKE_TIMEOUT) {
	pj_timer_heap_cancel(ssock->param.timer_heap, &ssock->timer);
	ssock->timer.id = TIMER_NONE;
    }

    /* Update certificates info on successful handshake */
    if (status == PJ_SUCCESS)
	update_certs_info(ssock);

    /* Accepting */
    if (ssock->is_server) {
	if (status != PJ_SUCCESS) {
	    /* Handshake failed in accepting, destroy our self silently. */

	    char errmsg[PJ_ERR_MSG_SIZE];
	    char buf[PJ_INET6_ADDRSTRLEN+10];

	    pj_strerror(status, errmsg, sizeof(errmsg));
	    PJ_LOG(3,(ssock->pool->obj_name, "Handshake failed in accepting "
		      "%s: %s",
		      pj_sockaddr_print(&ssock->rem_addr, buf, sizeof(buf), 3),
		      errmsg));

	    /* Workaround for ticket #985 */
#if defined(PJ_WIN32) && PJ_WIN32!=0
	    if (ssock->param.timer_heap) {
		pj_time_val interval = {0, DELAYED_CLOSE_TIMEOUT};

		reset_ssl_sock_state(ssock);

		ssock->timer.id = TIMER_CLOSE;
		pj_time_val_normalize(&interval);
		if (pj_timer_heap_schedule(ssock->param.timer_heap, 
					   &ssock->timer, &interval) != 0)
		{
		    ssock->timer.id = TIMER_NONE;
		    pj_ssl_sock_close(ssock);
		}
	    } else 
#endif	/* PJ_WIN32 */
	    {
		pj_ssl_sock_close(ssock);
	    }
	    return PJ_FALSE;
	}
	/* Notify application the newly accepted SSL socket */
	if (ssock->param.cb.on_accept_complete) {
	    pj_bool_t ret;
	    ret = (*ssock->param.cb.on_accept_complete)
		      (ssock->parent, ssock, (pj_sockaddr_t*)&ssock->rem_addr,
		       pj_sockaddr_get_len((pj_sockaddr_t*)&ssock->rem_addr));
	    if (ret == PJ_FALSE)
		return PJ_FALSE;
	}
    }

    /* Connecting */
    else {
	/* On failure, reset SSL socket state first, as app may try to 
	 * reconnect in the callback.
	 */
	if (status != PJ_SUCCESS) {
	    reset_ssl_sock_state(ssock);
	}
	if (ssock->param.cb.on_connect_complete) {
	    pj_bool_t ret;
	    ret = (*ssock->param.cb.on_connect_complete)(ssock, status);
	    if (ret == PJ_FALSE)
		return PJ_FALSE;
	}
    }

    return PJ_TRUE;
}

/* Flush write BIO to network socket. Note that any access to write BIO
 * MUST be serialized, so mutex protection must cover any call to OpenSSL
 * API (that possibly generate data for write BIO) along with the call to
 * this function (flushing all data in write BIO generated by above 
 * OpenSSL API call).
 */
static pj_status_t flush_write_bio(pj_ssl_sock_t *ssock, 
				   pj_ioqueue_op_key_t *send_key,
				   pj_size_t orig_len,
				   unsigned flags)
{
    char *data;
    pj_ssize_t len;

    write_state_t *write_st = &ssock->write_state;
    write_data_t *wdata;
    pj_size_t avail_len, needed_len, skipped_len = 0;
    pj_status_t status;

    /* Check if there is data in write BIO, flush it if any */
    if (!BIO_pending(ssock->ossl_wbio))
	return PJ_SUCCESS;

    /* Get data and its length */
    len = BIO_get_mem_data(ssock->ossl_wbio, &data);
    if (len == 0)
	return PJ_SUCCESS;

    /* Calculate buffer size needed, and align it to 8 */
    needed_len = len + sizeof(write_data_t);
    needed_len = ((needed_len + 7) >> 3) << 3;

    /* Check buffer availability */
    avail_len = write_st->max_len - write_st->len;
    if (avail_len < needed_len)
	return PJ_ENOMEM;

    /* More buffer availability check, note that the write data must be in
     * a contigue buffer.
     */
    if (write_st->len == 0) {

	write_st->start = write_st->buf;
	wdata = (write_data_t*)write_st->start;

    } else {

	char *reg1, *reg2;
	pj_size_t reg1_len, reg2_len;

	/* Unused slots may be wrapped/splitted into two regions, so let's
	 * analyze them if any region can hold the write data.
	 */
	reg1 = write_st->start + write_st->len;
	if (reg1 >= write_st->buf + write_st->max_len)
	    reg1 -= write_st->max_len;
	reg1_len = write_st->max_len - write_st->len;
	if (reg1 + reg1_len > write_st->buf + write_st->max_len) {
	    reg1_len = write_st->buf + write_st->max_len - reg1;
	    reg2 = write_st->buf;
	    reg2_len = write_st->start - write_st->buf;
	} else {
	    reg2 = NULL;
	    reg2_len = 0;
	}
	avail_len = PJ_MAX(reg1_len, reg2_len);
	if (avail_len < needed_len)
	    return PJ_ENOMEM;

	/* Get write data pointer and update buffer length */
	if (reg1_len >= needed_len) {
	    wdata = (write_data_t*)reg1;
	} else {
	    wdata = (write_data_t*)reg2;
	    /* Unused slot in region 1 is skipped as current write data
	     * doesn't fit it.
	     */
	    skipped_len = reg1_len;
	}
    }

    /* Copy the data and set its properties into the buffer */
    pj_bzero(wdata, sizeof(write_data_t));
    wdata->app_key = send_key;
    wdata->record_len = needed_len;
    wdata->data_len = len;
    wdata->plain_data_len = orig_len;
    wdata->flags = flags;
    pj_memcpy(&wdata->data, data, len);

    /* Send it */
    if (ssock->param.sock_type == pj_SOCK_STREAM()) {
	status = pj_activesock_send(ssock->asock, &wdata->key, 
				    wdata->data.content, &len,
				    flags);
    } else {
	status = pj_activesock_sendto(ssock->asock, &wdata->key, 
				      wdata->data.content, &len,
				      flags,
				      (pj_sockaddr_t*)&ssock->rem_addr,
				      ssock->addr_len);
    }

    /* Oh no, EWOULDBLOCK! */
    if (status == PJ_STATUS_FROM_OS(OSERR_EWOULDBLOCK)) {
	/* Just return PJ_SUCCESS here, the pending data will be sent in next
	 * call of this function since the data is still stored in write BIO.
	 */
	return PJ_SUCCESS;
    }

    /* Reset write BIO after flushed */
    BIO_reset(ssock->ossl_wbio);

    if (status == PJ_EPENDING) {
	/* Update write state */
	pj_assert(skipped_len==0 || write_st->last_data);
	write_st->len += needed_len + skipped_len;
	if (write_st->last_data)
	    write_st->last_data->record_len += skipped_len;
	write_st->last_data = wdata;
    }

    return status;
}


static void on_timer(pj_timer_heap_t *th, struct pj_timer_entry *te)
{
    pj_ssl_sock_t *ssock = (pj_ssl_sock_t*)te->user_data;
    int timer_id = te->id;

    te->id = TIMER_NONE;

    PJ_UNUSED_ARG(th);

    switch (timer_id) {
    case TIMER_HANDSHAKE_TIMEOUT:
	PJ_LOG(1,(ssock->pool->obj_name, "SSL timeout after %d.%ds",
		  ssock->param.timeout.sec, ssock->param.timeout.msec));

	on_handshake_complete(ssock, PJ_ETIMEDOUT);
	break;
    case TIMER_CLOSE:
	pj_ssl_sock_close(ssock);
	break;
    default:
	pj_assert(!"Unknown timer");
	break;
    }
}


/* Asynchronouse handshake */
static pj_status_t do_handshake(pj_ssl_sock_t *ssock)
{
    pj_status_t status;
    int err;

    pj_lock_acquire(ssock->write_mutex);

    /* Perform SSL handshake */
    err = SSL_do_handshake(ssock->ossl_ssl);
    if (err < 0) {
	err = SSL_get_error(ssock->ossl_ssl, err);
	if (err != SSL_ERROR_NONE && err != SSL_ERROR_WANT_READ) 
	{
	    /* Handshake fails */
	    GET_SSL_STATUS(status);
	    pj_lock_release(ssock->write_mutex);
	    return status;
	}
    }

    /* SSL_do_handshake() may put some pending data into SSL write BIO, 
     * flush it if any.
     */
    status = flush_write_bio(ssock, &ssock->handshake_op_key, 0, 0);
    if (status != PJ_SUCCESS && status != PJ_EPENDING) {
	pj_lock_release(ssock->write_mutex);
	return status;
    }

    pj_lock_release(ssock->write_mutex);

    /* Check if handshake has been completed */
    if (SSL_is_init_finished(ssock->ossl_ssl)) {
	ssock->ssl_state = SSL_STATE_ESTABLISHED;
	return PJ_SUCCESS;
    }

    return PJ_EPENDING;
}


/*
 *******************************************************************
 * Active socket callbacks.
 *******************************************************************
 */

static pj_bool_t asock_on_data_read (pj_activesock_t *asock,
				     void *data,
				     pj_size_t size,
				     pj_status_t status,
				     pj_size_t *remainder)
{
    pj_ssl_sock_t *ssock = (pj_ssl_sock_t*)
			   pj_activesock_get_user_data(asock);
    pj_size_t nwritten;

    /* Socket error or closed */
    if (data && size > 0) {
	/* Consume the whole data */
	nwritten = BIO_write(ssock->ossl_rbio, data, size);
	if (nwritten < size) {
	    GET_SSL_STATUS(status);
	    goto on_error;
	}
    }

    /* Check if SSL handshake hasn't finished yet */
    if (ssock->ssl_state == SSL_STATE_HANDSHAKING) {
	pj_bool_t ret = PJ_TRUE;

	if (status == PJ_SUCCESS)
	    status = do_handshake(ssock);

	/* Not pending is either success or failed */
	if (status != PJ_EPENDING)
	    ret = on_handshake_complete(ssock, status);

	return ret;
    }

    /* See if there is any decrypted data for the application */
    if (ssock->read_started) {
	do {
	    read_data_t *buf = *(OFFSET_OF_READ_DATA_PTR(ssock, data));
	    void *data_ = (pj_int8_t*)buf->data + buf->len;
	    int size_ = ssock->read_size - buf->len;

	    /* SSL_read() may write some data to BIO write when re-negotiation
	     * is on progress, so let's protect it with write mutex.
	     */
	    pj_lock_acquire(ssock->write_mutex);
	    size_ = SSL_read(ssock->ossl_ssl, data_, size_);
	    pj_lock_release(ssock->write_mutex);

	    if (size_ > 0 || status != PJ_SUCCESS) {
		if (ssock->param.cb.on_data_read) {
		    pj_bool_t ret;
		    pj_size_t remainder_ = 0;

		    if (size_ > 0)
			buf->len += size_;
    		
		    ret = (*ssock->param.cb.on_data_read)(ssock, buf->data,
							  buf->len, status,
							  &remainder_);
		    if (!ret) {
			/* We've been destroyed */
			return PJ_FALSE;
		    }

		    /* Application may have left some data to be consumed 
		     * later.
		     */
		    buf->len = remainder_;
		}

		/* Active socket signalled connection closed/error, this has
		 * been signalled to the application along with any remaining
		 * buffer. So, let's just reset SSL socket now.
		 */
		if (status != PJ_SUCCESS) {
		    reset_ssl_sock_state(ssock);
		    return PJ_FALSE;
		}

	    } else {

		int err = SSL_get_error(ssock->ossl_ssl, size);
		
		/* SSL might just return SSL_ERROR_WANT_READ in 
		 * re-negotiation.
		 */
		if (err != SSL_ERROR_NONE && err != SSL_ERROR_WANT_READ)
		{
		    /* Reset SSL socket state, then return PJ_FALSE */
		    GET_SSL_STATUS(status);
		    reset_ssl_sock_state(ssock);
		    goto on_error;
		}

		status = do_handshake(ssock);
		if (status == PJ_SUCCESS) {
		    /* Renegotiation completed */

		    /* Update certificates */
		    update_certs_info(ssock);

		    pj_lock_acquire(ssock->write_mutex);
		    status = flush_delayed_send(ssock);
		    pj_lock_release(ssock->write_mutex);

		    if (status != PJ_SUCCESS && status != PJ_EPENDING) {
			PJ_PERROR(1,(ssock->pool->obj_name, status, 
				     "Failed to flush delayed send"));
			goto on_error;
		    }
		} else if (status != PJ_EPENDING) {
		    PJ_PERROR(1,(ssock->pool->obj_name, status, 
			         "Renegotiation failed"));
		    goto on_error;
		}

		break;
	    }
	} while (1);
    }

    return PJ_TRUE;

on_error:
    if (ssock->ssl_state == SSL_STATE_HANDSHAKING)
	return on_handshake_complete(ssock, status);

    if (ssock->read_started && ssock->param.cb.on_data_read) {
	pj_bool_t ret;
	ret = (*ssock->param.cb.on_data_read)(ssock, NULL, 0, status,
					      remainder);
	if (!ret) {
	    /* We've been destroyed */
	    return PJ_FALSE;
	}
    }

    reset_ssl_sock_state(ssock);
    return PJ_FALSE;
}


static pj_bool_t asock_on_data_sent (pj_activesock_t *asock,
				     pj_ioqueue_op_key_t *send_key,
				     pj_ssize_t sent)
{
    pj_ssl_sock_t *ssock = (pj_ssl_sock_t*)
			   pj_activesock_get_user_data(asock);

    PJ_UNUSED_ARG(send_key);
    PJ_UNUSED_ARG(sent);

    if (ssock->ssl_state == SSL_STATE_HANDSHAKING) {
	/* Initial handshaking */
	pj_status_t status;
	
	status = do_handshake(ssock);
	/* Not pending is either success or failed */
	if (status != PJ_EPENDING)
	    return on_handshake_complete(ssock, status);

    } else if (send_key != &ssock->handshake_op_key) {
	/* Some data has been sent, notify application */
	write_data_t *wdata = (write_data_t*)send_key;
	if (ssock->param.cb.on_data_sent) {
	    pj_bool_t ret;
	    ret = (*ssock->param.cb.on_data_sent)(ssock, wdata->app_key, 
						  wdata->plain_data_len);
	    if (!ret) {
		/* We've been destroyed */
		return PJ_FALSE;
	    }
	}

	/* Update write buffer state */
	pj_lock_acquire(ssock->write_mutex);
	ssock->write_state.start += wdata->record_len;
	ssock->write_state.len -= wdata->record_len;
	if (ssock->write_state.last_data == wdata) {
	    pj_assert(ssock->write_state.len == 0);
	    ssock->write_state.last_data = NULL;
	}
	pj_lock_release(ssock->write_mutex);

    } else {
	/* SSL re-negotiation is on-progress, just do nothing */
    }

    return PJ_TRUE;
}


static pj_bool_t asock_on_accept_complete (pj_activesock_t *asock,
					   pj_sock_t newsock,
					   const pj_sockaddr_t *src_addr,
					   int src_addr_len)
{
    pj_ssl_sock_t *ssock_parent = (pj_ssl_sock_t*)
				  pj_activesock_get_user_data(asock);
    pj_ssl_sock_t *ssock;
    pj_activesock_cb asock_cb;
    pj_activesock_cfg asock_cfg;
    unsigned i;
    pj_status_t status;

    PJ_UNUSED_ARG(src_addr_len);

    /* Create new SSL socket instance */
    status = pj_ssl_sock_create(ssock_parent->pool, &ssock_parent->param,
				&ssock);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Update new SSL socket attributes */
    ssock->sock = newsock;
    ssock->parent = ssock_parent;
    ssock->is_server = PJ_TRUE;
    if (ssock_parent->cert) {
	status = pj_ssl_sock_set_certificate(ssock, ssock->pool, 
					     ssock_parent->cert);
	if (status != PJ_SUCCESS)
	    goto on_return;
    }

    /* Apply QoS, if specified */
    status = pj_sock_apply_qos2(ssock->sock, ssock->param.qos_type,
				&ssock->param.qos_params, 1, 
				ssock->pool->obj_name, NULL);
    if (status != PJ_SUCCESS && !ssock->param.qos_ignore_error)
	goto on_return;

    /* Update local address */
    ssock->addr_len = src_addr_len;
    status = pj_sock_getsockname(ssock->sock, &ssock->local_addr, 
				 &ssock->addr_len);
    if (status != PJ_SUCCESS) {
	/* This fails on few envs, e.g: win IOCP, just tolerate this and
	 * use parent local address instead.
	 */
	pj_sockaddr_cp(&ssock->local_addr, &ssock_parent->local_addr);
    }

    /* Set remote address */
    pj_sockaddr_cp(&ssock->rem_addr, src_addr);

    /* Create SSL context */
    status = create_ssl(ssock);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Prepare read buffer */
    ssock->asock_rbuf = (void**)pj_pool_calloc(ssock->pool, 
					       ssock->param.async_cnt,
					       sizeof(void*));
    for (i = 0; i<ssock->param.async_cnt; ++i) {
	ssock->asock_rbuf[i] = (void*) pj_pool_alloc(
					    ssock->pool, 
					    ssock->param.read_buffer_size + 
					    sizeof(read_data_t*));
    }

    /* Create active socket */
    pj_activesock_cfg_default(&asock_cfg);
    asock_cfg.async_cnt = ssock->param.async_cnt;
    asock_cfg.concurrency = ssock->param.concurrency;
    asock_cfg.whole_data = PJ_TRUE;

    pj_bzero(&asock_cb, sizeof(asock_cb));
    asock_cb.on_data_read = asock_on_data_read;
    asock_cb.on_data_sent = asock_on_data_sent;

    status = pj_activesock_create(ssock->pool,
				  ssock->sock, 
				  ssock->param.sock_type,
				  &asock_cfg,
				  ssock->param.ioqueue, 
				  &asock_cb,
				  ssock,
				  &ssock->asock);

    if (status != PJ_SUCCESS)
	goto on_return;

    /* Start read */
    status = pj_activesock_start_read2(ssock->asock, ssock->pool, 
				       ssock->param.read_buffer_size,
				       ssock->asock_rbuf,
				       PJ_IOQUEUE_ALWAYS_ASYNC);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Prepare write/send state */
    pj_assert(ssock->write_state.max_len == 0);
    ssock->write_state.buf = (char*)
			     pj_pool_alloc(ssock->pool, 
					   ssock->param.send_buffer_size);
    ssock->write_state.max_len = ssock->param.send_buffer_size;
    ssock->write_state.start = ssock->write_state.buf;
    ssock->write_state.len = 0;

    /* Start handshake timer */
    if (ssock->param.timer_heap && (ssock->param.timeout.sec != 0 ||
	ssock->param.timeout.msec != 0))
    {
	pj_assert(ssock->timer.id == TIMER_NONE);
	ssock->timer.id = TIMER_HANDSHAKE_TIMEOUT;
	status = pj_timer_heap_schedule(ssock->param.timer_heap, 
				        &ssock->timer,
					&ssock->param.timeout);
	if (status != PJ_SUCCESS)
	    ssock->timer.id = TIMER_NONE;
    }

    /* Start SSL handshake */
    ssock->ssl_state = SSL_STATE_HANDSHAKING;
    SSL_set_accept_state(ssock->ossl_ssl);
    status = do_handshake(ssock);

on_return:
    if (ssock && status != PJ_EPENDING)
	on_handshake_complete(ssock, status);

    /* Must return PJ_TRUE whatever happened, as active socket must 
     * continue listening.
     */
    return PJ_TRUE;
}


static pj_bool_t asock_on_connect_complete (pj_activesock_t *asock,
					    pj_status_t status)
{
    pj_ssl_sock_t *ssock = (pj_ssl_sock_t*)
			   pj_activesock_get_user_data(asock);
    unsigned i;

    if (status != PJ_SUCCESS)
	goto on_return;

    /* Update local address */
    ssock->addr_len = sizeof(pj_sockaddr);
    status = pj_sock_getsockname(ssock->sock, &ssock->local_addr, 
				 &ssock->addr_len);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Create SSL context */
    status = create_ssl(ssock);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Prepare read buffer */
    ssock->asock_rbuf = (void**)pj_pool_calloc(ssock->pool, 
					       ssock->param.async_cnt,
					       sizeof(void*));
    for (i = 0; i<ssock->param.async_cnt; ++i) {
	ssock->asock_rbuf[i] = (void*) pj_pool_alloc(
					    ssock->pool, 
					    ssock->param.read_buffer_size + 
					    sizeof(read_data_t*));
    }

    /* Start read */
    status = pj_activesock_start_read2(ssock->asock, ssock->pool, 
				       ssock->param.read_buffer_size,
				       ssock->asock_rbuf,
				       PJ_IOQUEUE_ALWAYS_ASYNC);
    if (status != PJ_SUCCESS)
	goto on_return;

    /* Prepare write/send state */
    pj_assert(ssock->write_state.max_len == 0);
    ssock->write_state.buf = (char*)
			     pj_pool_alloc(ssock->pool, 
					   ssock->param.send_buffer_size);
    ssock->write_state.max_len = ssock->param.send_buffer_size;
    ssock->write_state.start = ssock->write_state.buf;
    ssock->write_state.len = 0;

#ifdef SSL_set_tlsext_host_name
    /* Set server name to connect */
    if (ssock->param.server_name.slen) {
	/* Server name is null terminated already */
	if (!SSL_set_tlsext_host_name(ssock->ossl_ssl, 
				      ssock->param.server_name.ptr))
	{
	    char err_str[PJ_ERR_MSG_SIZE];

	    ERR_error_string_n(ERR_get_error(), err_str, sizeof(err_str));
	    PJ_LOG(3,(ssock->pool->obj_name, "SSL_set_tlsext_host_name() "
		"failed: %s", err_str));
	}
    }
#endif

    /* Start SSL handshake */
    ssock->ssl_state = SSL_STATE_HANDSHAKING;
    SSL_set_connect_state(ssock->ossl_ssl);

    status = do_handshake(ssock);
    if (status != PJ_EPENDING)
	goto on_return;

    return PJ_TRUE;

on_return:
    return on_handshake_complete(ssock, status);
}



/*
 *******************************************************************
 * API
 *******************************************************************
 */

/* Load credentials from files. */
PJ_DEF(pj_status_t) pj_ssl_cert_load_from_files (pj_pool_t *pool,
						 const pj_str_t *CA_file,
						 const pj_str_t *cert_file,
						 const pj_str_t *privkey_file,
						 const pj_str_t *privkey_pass,
						 pj_ssl_cert_t **p_cert)
{
    pj_ssl_cert_t *cert;

    PJ_ASSERT_RETURN(pool && CA_file && cert_file && privkey_file, PJ_EINVAL);

    cert = PJ_POOL_ZALLOC_T(pool, pj_ssl_cert_t);
    pj_strdup_with_null(pool, &cert->CA_file, CA_file);
    pj_strdup_with_null(pool, &cert->cert_file, cert_file);
    pj_strdup_with_null(pool, &cert->privkey_file, privkey_file);
    pj_strdup_with_null(pool, &cert->privkey_pass, privkey_pass);

    *p_cert = cert;

    return PJ_SUCCESS;
}


/* Set SSL socket credentials. */
PJ_DECL(pj_status_t) pj_ssl_sock_set_certificate(
					    pj_ssl_sock_t *ssock,
					    pj_pool_t *pool,
					    const pj_ssl_cert_t *cert)
{
    pj_ssl_cert_t *cert_;

    PJ_ASSERT_RETURN(ssock && pool && cert, PJ_EINVAL);

    cert_ = PJ_POOL_ZALLOC_T(pool, pj_ssl_cert_t);
    pj_memcpy(cert_, cert, sizeof(cert));
    pj_strdup_with_null(pool, &cert_->CA_file, &cert->CA_file);
    pj_strdup_with_null(pool, &cert_->cert_file, &cert->cert_file);
    pj_strdup_with_null(pool, &cert_->privkey_file, &cert->privkey_file);
    pj_strdup_with_null(pool, &cert_->privkey_pass, &cert->privkey_pass);

    ssock->cert = cert_;

    return PJ_SUCCESS;
}


/* Get available ciphers. */
PJ_DEF(pj_status_t) pj_ssl_cipher_get_availables(pj_ssl_cipher ciphers[],
					         unsigned *cipher_num)
{
    unsigned i;

    PJ_ASSERT_RETURN(ciphers && cipher_num, PJ_EINVAL);

    if (openssl_cipher_num == 0) {
	init_openssl();
	shutdown_openssl();
    }

    if (openssl_cipher_num == 0) {
	*cipher_num = 0;
	return PJ_ENOTFOUND;
    }

    *cipher_num = PJ_MIN(*cipher_num, openssl_cipher_num);

    for (i = 0; i < *cipher_num; ++i)
	ciphers[i] = openssl_ciphers[i].id;

    return PJ_SUCCESS;
}


/* Get cipher name string */
PJ_DEF(const char*) pj_ssl_cipher_name(pj_ssl_cipher cipher)
{
    unsigned i;

    if (openssl_cipher_num == 0) {
	init_openssl();
	shutdown_openssl();
    }

    for (i = 0; i < openssl_cipher_num; ++i) {
	if (cipher == openssl_ciphers[i].id)
	    return openssl_ciphers[i].name;
    }

    return NULL;
}

/* Check if the specified cipher is supported by SSL/TLS backend. */
PJ_DEF(pj_bool_t) pj_ssl_cipher_is_supported(pj_ssl_cipher cipher)
{
    unsigned i;

    if (openssl_cipher_num == 0) {
	init_openssl();
	shutdown_openssl();
    }

    for (i = 0; i < openssl_cipher_num; ++i) {
	if (cipher == openssl_ciphers[i].id)
	    return PJ_TRUE;
    }

    return PJ_FALSE;
}


/*
 * Create SSL socket instance. 
 */
PJ_DEF(pj_status_t) pj_ssl_sock_create (pj_pool_t *pool,
					const pj_ssl_sock_param *param,
					pj_ssl_sock_t **p_ssock)
{
    pj_ssl_sock_t *ssock;
    pj_status_t status;

    PJ_ASSERT_RETURN(pool && param && p_ssock, PJ_EINVAL);
    PJ_ASSERT_RETURN(param->sock_type == pj_SOCK_STREAM(), PJ_ENOTSUP);

    pool = pj_pool_create(pool->factory, "ssl%p", 512, 512, NULL);

    /* Create secure socket */
    ssock = PJ_POOL_ZALLOC_T(pool, pj_ssl_sock_t);
    ssock->pool = pool;
    ssock->sock = PJ_INVALID_SOCKET;
    ssock->ssl_state = SSL_STATE_NULL;
    pj_list_init(&ssock->write_pending);
    pj_list_init(&ssock->write_pending_empty);
    pj_timer_entry_init(&ssock->timer, 0, ssock, &on_timer);

    /* Create secure socket mutex */
    status = pj_lock_create_recursive_mutex(pool, pool->obj_name,
					    &ssock->write_mutex);
    if (status != PJ_SUCCESS)
	return status;

    /* Init secure socket param */
    ssock->param = *param;
    ssock->param.read_buffer_size = ((ssock->param.read_buffer_size+7)>>3)<<3;
    if (param->ciphers_num > 0) {
	unsigned i;
	ssock->param.ciphers = (pj_ssl_cipher*)
			       pj_pool_calloc(pool, param->ciphers_num, 
					      sizeof(pj_ssl_cipher));
	for (i = 0; i < param->ciphers_num; ++i)
	    ssock->param.ciphers[i] = param->ciphers[i];
    }

    /* Server name must be null-terminated */
    pj_strdup_with_null(pool, &ssock->param.server_name, 
			&param->server_name);

    /* Finally */
    *p_ssock = ssock;

    return PJ_SUCCESS;
}


/*
 * Close the secure socket. This will unregister the socket from the
 * ioqueue and ultimately close the socket.
 */
PJ_DEF(pj_status_t) pj_ssl_sock_close(pj_ssl_sock_t *ssock)
{
    pj_pool_t *pool;

    PJ_ASSERT_RETURN(ssock, PJ_EINVAL);

    if (!ssock->pool)
	return PJ_SUCCESS;

    if (ssock->timer.id != TIMER_NONE) {
	pj_timer_heap_cancel(ssock->param.timer_heap, &ssock->timer);
	ssock->timer.id = TIMER_NONE;
    }

    reset_ssl_sock_state(ssock);
    pj_lock_destroy(ssock->write_mutex);
    
    pool = ssock->pool;
    ssock->pool = NULL;
    if (pool)
	pj_pool_release(pool);

    return PJ_SUCCESS;
}


/*
 * Associate arbitrary data with the secure socket.
 */
PJ_DEF(pj_status_t) pj_ssl_sock_set_user_data(pj_ssl_sock_t *ssock,
					      void *user_data)
{
    PJ_ASSERT_RETURN(ssock, PJ_EINVAL);

    ssock->param.user_data = user_data;
    return PJ_SUCCESS;
}


/*
 * Retrieve the user data previously associated with this secure
 * socket.
 */
PJ_DEF(void*) pj_ssl_sock_get_user_data(pj_ssl_sock_t *ssock)
{
    PJ_ASSERT_RETURN(ssock, NULL);

    return ssock->param.user_data;
}


/*
 * Retrieve the local address and port used by specified SSL socket.
 */
PJ_DEF(pj_status_t) pj_ssl_sock_get_info (pj_ssl_sock_t *ssock,
					  pj_ssl_sock_info *info)
{
    pj_bzero(info, sizeof(*info));

    /* Established flag */
    info->established = (ssock->ssl_state == SSL_STATE_ESTABLISHED);

    /* Protocol */
    info->proto = ssock->param.proto;

    /* Local address */
    pj_sockaddr_cp(&info->local_addr, &ssock->local_addr);
    
    if (info->established) {
	const SSL_CIPHER *cipher;

	/* Current cipher */
	cipher = SSL_get_current_cipher(ssock->ossl_ssl);
	info->cipher = (cipher->id & 0x00FFFFFF);

	/* Remote address */
	pj_sockaddr_cp(&info->remote_addr, &ssock->rem_addr);

	/* Certificates info */
	info->local_cert_info = &ssock->local_cert_info;
	info->remote_cert_info = &ssock->remote_cert_info;

	/* Verification status */
	info->verify_status = ssock->verify_status;
    }

    return PJ_SUCCESS;
}


/*
 * Starts read operation on this secure socket.
 */
PJ_DEF(pj_status_t) pj_ssl_sock_start_read (pj_ssl_sock_t *ssock,
					    pj_pool_t *pool,
					    unsigned buff_size,
					    pj_uint32_t flags)
{
    void **readbuf;
    unsigned i;

    PJ_ASSERT_RETURN(ssock && pool && buff_size, PJ_EINVAL);
    PJ_ASSERT_RETURN(ssock->ssl_state==SSL_STATE_ESTABLISHED, PJ_EINVALIDOP);

    readbuf = (void**) pj_pool_calloc(pool, ssock->param.async_cnt, 
				      sizeof(void*));

    for (i=0; i<ssock->param.async_cnt; ++i) {
	readbuf[i] = pj_pool_alloc(pool, buff_size);
    }

    return pj_ssl_sock_start_read2(ssock, pool, buff_size, 
				   readbuf, flags);
}


/*
 * Same as #pj_ssl_sock_start_read(), except that the application
 * supplies the buffers for the read operation so that the acive socket
 * does not have to allocate the buffers.
 */
PJ_DEF(pj_status_t) pj_ssl_sock_start_read2 (pj_ssl_sock_t *ssock,
					     pj_pool_t *pool,
					     unsigned buff_size,
					     void *readbuf[],
					     pj_uint32_t flags)
{
    unsigned i;

    PJ_ASSERT_RETURN(ssock && pool && buff_size && readbuf, PJ_EINVAL);
    PJ_ASSERT_RETURN(ssock->ssl_state==SSL_STATE_ESTABLISHED, PJ_EINVALIDOP);

    /* Create SSL socket read buffer */
    ssock->ssock_rbuf = (read_data_t*)pj_pool_calloc(pool, 
					       ssock->param.async_cnt,
					       sizeof(read_data_t));

    /* Store SSL socket read buffer pointer in the activesock read buffer */
    for (i=0; i<ssock->param.async_cnt; ++i) {
	read_data_t **p_ssock_rbuf = 
			OFFSET_OF_READ_DATA_PTR(ssock, ssock->asock_rbuf[i]);

	ssock->ssock_rbuf[i].data = readbuf[i];
	ssock->ssock_rbuf[i].len = 0;

	*p_ssock_rbuf = &ssock->ssock_rbuf[i];
    }

    ssock->read_size = buff_size;
    ssock->read_started = PJ_TRUE;
    ssock->read_flags = flags;

    return PJ_SUCCESS;
}


/*
 * Same as pj_ssl_sock_start_read(), except that this function is used
 * only for datagram sockets, and it will trigger \a on_data_recvfrom()
 * callback instead.
 */
PJ_DEF(pj_status_t) pj_ssl_sock_start_recvfrom (pj_ssl_sock_t *ssock,
						pj_pool_t *pool,
						unsigned buff_size,
						pj_uint32_t flags)
{
    PJ_UNUSED_ARG(ssock);
    PJ_UNUSED_ARG(pool);
    PJ_UNUSED_ARG(buff_size);
    PJ_UNUSED_ARG(flags);

    return PJ_ENOTSUP;
}


/*
 * Same as #pj_ssl_sock_start_recvfrom() except that the recvfrom() 
 * operation takes the buffer from the argument rather than creating
 * new ones.
 */
PJ_DEF(pj_status_t) pj_ssl_sock_start_recvfrom2 (pj_ssl_sock_t *ssock,
						 pj_pool_t *pool,
						 unsigned buff_size,
						 void *readbuf[],
						 pj_uint32_t flags)
{
    PJ_UNUSED_ARG(ssock);
    PJ_UNUSED_ARG(pool);
    PJ_UNUSED_ARG(buff_size);
    PJ_UNUSED_ARG(readbuf);
    PJ_UNUSED_ARG(flags);

    return PJ_ENOTSUP;
}

/* Write plain data to SSL and flush write BIO. Note that accessing
 * write BIO must be serialized, so a call to this function must be
 * protected by write mutex of SSL socket.
 */
static pj_status_t ssl_write(pj_ssl_sock_t *ssock, 
			     pj_ioqueue_op_key_t *send_key,
			     const void *data,
			     pj_ssize_t size,
			     unsigned flags)
{
    pj_status_t status;
    int nwritten;

    /* Write the plain data to SSL, after SSL encrypts it, write BIO will
     * contain the secured data to be sent via socket. Note that re-
     * negotitation may be on progress, so sending data should be delayed
     * until re-negotiation is completed.
     */
    nwritten = SSL_write(ssock->ossl_ssl, data, size);
    
    if (nwritten == size) {
	/* All data written, flush write BIO to network socket */
	status = flush_write_bio(ssock, send_key, size, flags);
    } else if (nwritten <= 0) {
	/* SSL failed to process the data, it may just that re-negotiation
	 * is on progress.
	 */
	int err;
	err = SSL_get_error(ssock->ossl_ssl, nwritten);
	if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_NONE) {
	    /* Re-negotiation is on progress, flush re-negotiation data */
	    status = flush_write_bio(ssock, &ssock->handshake_op_key, 0, 0);
	    if (status == PJ_SUCCESS || status == PJ_EPENDING)
		/* Just return PJ_EBUSY when re-negotiation is on progress */
		status = PJ_EBUSY;
	} else {
	    /* Some problem occured */
	    GET_SSL_STATUS(status);
	}
    } else {
	/* nwritten < *size, shouldn't happen, unless write BIO cannot hold 
	 * the whole secured data, perhaps because of insufficient memory.
	 */
	status = PJ_ENOMEM;
    }

    return status;
}

/* Flush delayed data sending in the write pending list. Note that accessing
 * write pending list must be serialized, so a call to this function must be
 * protected by write mutex of SSL socket.
 */
static pj_status_t flush_delayed_send(pj_ssl_sock_t *ssock)
{
    while (!pj_list_empty(&ssock->write_pending)) {
        write_pending_t *wp;
	pj_status_t status;

	wp = ssock->write_pending.next;

	status = ssl_write(ssock, &wp->data.key, wp->data.data.ptr, 
			   wp->data.plain_data_len, wp->data.flags);
	if (status != PJ_SUCCESS)
	    return status;

	pj_list_erase(wp);
	pj_list_push_back(&ssock->write_pending_empty, wp);
    }

    return PJ_SUCCESS;
}

/* Sending is delayed, push back the sending data into pending list. Note that
 * accessing write pending list must be serialized, so a call to this function
 * must be protected by write mutex of SSL socket.
 */
static pj_status_t delay_send (pj_ssl_sock_t *ssock,
			       pj_ioqueue_op_key_t *send_key,
			       const void *data,
			       pj_ssize_t size,
			       unsigned flags)
{
    write_pending_t *wp;

    /* Init write pending instance */
    if (!pj_list_empty(&ssock->write_pending_empty)) {
	wp = ssock->write_pending_empty.next;
	pj_list_erase(wp);
    } else {
	wp = PJ_POOL_ZALLOC_T(ssock->pool, write_pending_t);
    }

    wp->data.app_key = send_key;
    wp->data.plain_data_len = size;
    wp->data.data.ptr = data;
    wp->data.flags = flags;

    pj_list_push_back(&ssock->write_pending, wp);

    /* Must return PJ_EPENDING */
    return PJ_EPENDING;
}

/**
 * Send data using the socket.
 */
PJ_DEF(pj_status_t) pj_ssl_sock_send (pj_ssl_sock_t *ssock,
				      pj_ioqueue_op_key_t *send_key,
				      const void *data,
				      pj_ssize_t *size,
				      unsigned flags)
{
    pj_status_t status;

    PJ_ASSERT_RETURN(ssock && data && size && (*size>0), PJ_EINVAL);
    PJ_ASSERT_RETURN(ssock->ssl_state==SSL_STATE_ESTABLISHED, PJ_EINVALIDOP);

    pj_lock_acquire(ssock->write_mutex);

    /* Flush delayed send first. Sending data might be delayed when 
     * re-negotiation is on-progress.
     */
    status = flush_delayed_send(ssock);
    if (status == PJ_EBUSY) {
	/* Re-negotiation is on progress, delay sending */
	status = delay_send(ssock, send_key, data, *size, flags);
	goto on_return;
    } else if (status != PJ_SUCCESS) {
	goto on_return;
    }

    /* Write data to SSL */
    status = ssl_write(ssock, send_key, data, *size, flags);
    if (status == PJ_EBUSY) {
	/* Re-negotiation is on progress, delay sending */
	status = delay_send(ssock, send_key, data, *size, flags);
    }

on_return:
    pj_lock_release(ssock->write_mutex);
    return status;
}


/**
 * Send datagram using the socket.
 */
PJ_DEF(pj_status_t) pj_ssl_sock_sendto (pj_ssl_sock_t *ssock,
					pj_ioqueue_op_key_t *send_key,
					const void *data,
					pj_ssize_t *size,
					unsigned flags,
					const pj_sockaddr_t *addr,
					int addr_len)
{
    PJ_UNUSED_ARG(ssock);
    PJ_UNUSED_ARG(send_key);
    PJ_UNUSED_ARG(data);
    PJ_UNUSED_ARG(size);
    PJ_UNUSED_ARG(flags);
    PJ_UNUSED_ARG(addr);
    PJ_UNUSED_ARG(addr_len);

    return PJ_ENOTSUP;
}


/**
 * Starts asynchronous socket accept() operations on this secure socket. 
 */
PJ_DEF(pj_status_t) pj_ssl_sock_start_accept (pj_ssl_sock_t *ssock,
					      pj_pool_t *pool,
					      const pj_sockaddr_t *localaddr,
					      int addr_len)
{
    pj_activesock_cb asock_cb;
    pj_activesock_cfg asock_cfg;
    pj_status_t status;

    PJ_ASSERT_RETURN(ssock && pool && localaddr && addr_len, PJ_EINVAL);

    /* Create socket */
    status = pj_sock_socket(ssock->param.sock_af, ssock->param.sock_type, 0, 
			    &ssock->sock);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Apply QoS, if specified */
    status = pj_sock_apply_qos2(ssock->sock, ssock->param.qos_type,
				&ssock->param.qos_params, 2, 
				ssock->pool->obj_name, NULL);
    if (status != PJ_SUCCESS && !ssock->param.qos_ignore_error)
	goto on_error;

    /* Bind socket */
    status = pj_sock_bind(ssock->sock, localaddr, addr_len);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Start listening to the address */
    status = pj_sock_listen(ssock->sock, PJ_SOMAXCONN);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Create active socket */
    pj_activesock_cfg_default(&asock_cfg);
    asock_cfg.async_cnt = ssock->param.async_cnt;
    asock_cfg.concurrency = ssock->param.concurrency;
    asock_cfg.whole_data = PJ_TRUE;

    pj_bzero(&asock_cb, sizeof(asock_cb));
    asock_cb.on_accept_complete = asock_on_accept_complete;

    status = pj_activesock_create(pool,
				  ssock->sock, 
				  ssock->param.sock_type,
				  &asock_cfg,
				  ssock->param.ioqueue, 
				  &asock_cb,
				  ssock,
				  &ssock->asock);

    if (status != PJ_SUCCESS)
	goto on_error;

    /* Start accepting */
    status = pj_activesock_start_accept(ssock->asock, pool);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Update local address */
    ssock->addr_len = addr_len;
    status = pj_sock_getsockname(ssock->sock, &ssock->local_addr, 
				 &ssock->addr_len);
    if (status != PJ_SUCCESS)
	pj_sockaddr_cp(&ssock->local_addr, localaddr);

    ssock->is_server = PJ_TRUE;

    return PJ_SUCCESS;

on_error:
    reset_ssl_sock_state(ssock);
    return status;
}


/**
 * Starts asynchronous socket connect() operation.
 */
PJ_DECL(pj_status_t) pj_ssl_sock_start_connect(pj_ssl_sock_t *ssock,
					       pj_pool_t *pool,
					       const pj_sockaddr_t *localaddr,
					       const pj_sockaddr_t *remaddr,
					       int addr_len)
{
    pj_activesock_cb asock_cb;
    pj_activesock_cfg asock_cfg;
    pj_status_t status;

    PJ_ASSERT_RETURN(ssock && pool && localaddr && remaddr && addr_len,
		     PJ_EINVAL);

    /* Create socket */
    status = pj_sock_socket(ssock->param.sock_af, ssock->param.sock_type, 0, 
			    &ssock->sock);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Apply QoS, if specified */
    status = pj_sock_apply_qos2(ssock->sock, ssock->param.qos_type,
				&ssock->param.qos_params, 2, 
				ssock->pool->obj_name, NULL);
    if (status != PJ_SUCCESS && !ssock->param.qos_ignore_error)
	goto on_error;

    /* Bind socket */
    status = pj_sock_bind(ssock->sock, localaddr, addr_len);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Create active socket */
    pj_activesock_cfg_default(&asock_cfg);
    asock_cfg.async_cnt = ssock->param.async_cnt;
    asock_cfg.concurrency = ssock->param.concurrency;
    asock_cfg.whole_data = PJ_TRUE;

    pj_bzero(&asock_cb, sizeof(asock_cb));
    asock_cb.on_connect_complete = asock_on_connect_complete;
    asock_cb.on_data_read = asock_on_data_read;
    asock_cb.on_data_sent = asock_on_data_sent;

    status = pj_activesock_create(pool,
				  ssock->sock, 
				  ssock->param.sock_type,
				  &asock_cfg,
				  ssock->param.ioqueue, 
				  &asock_cb,
				  ssock,
				  &ssock->asock);

    if (status != PJ_SUCCESS)
	goto on_error;

    /* Save remote address */
    pj_sockaddr_cp(&ssock->rem_addr, remaddr);

    /* Start timer */
    if (ssock->param.timer_heap && (ssock->param.timeout.sec != 0 ||
	ssock->param.timeout.msec != 0))
    {
	pj_assert(ssock->timer.id == TIMER_NONE);
	ssock->timer.id = TIMER_HANDSHAKE_TIMEOUT;
	status = pj_timer_heap_schedule(ssock->param.timer_heap,
					&ssock->timer,
				        &ssock->param.timeout);
	if (status != PJ_SUCCESS)
	    ssock->timer.id = TIMER_NONE;
    }

    status = pj_activesock_start_connect(ssock->asock, pool, remaddr,
					 addr_len);

    if (status == PJ_SUCCESS)
	asock_on_connect_complete(ssock->asock, PJ_SUCCESS);
    else if (status != PJ_EPENDING)
	goto on_error;

    /* Update local address */
    ssock->addr_len = addr_len;
    status = pj_sock_getsockname(ssock->sock, &ssock->local_addr,
				 &ssock->addr_len);
    /* Note that we may not get an IP address here. This can
     * happen for example on Windows, where getsockname()
     * would return 0.0.0.0 if socket has just started the
     * async connect. In this case, just leave the local
     * address with 0.0.0.0 for now; it will be updated
     * once the socket is established.
     */

    /* Update SSL state */
    ssock->is_server = PJ_FALSE;

    return PJ_EPENDING;

on_error:
    reset_ssl_sock_state(ssock);
    return status;
}


PJ_DEF(pj_status_t) pj_ssl_sock_renegotiate(pj_ssl_sock_t *ssock)
{
    int ret;
    pj_status_t status;

    PJ_ASSERT_RETURN(ssock->ssl_state == SSL_STATE_ESTABLISHED, PJ_EINVALIDOP);

    if (SSL_renegotiate_pending(ssock->ossl_ssl))
	return PJ_EPENDING;

    ret = SSL_renegotiate(ssock->ossl_ssl);
    if (ret <= 0) {
	GET_SSL_STATUS(status);
    } else {
	status = do_handshake(ssock);
    }

    return status;
}

#endif  /* PJ_HAS_SSL_SOCK */

