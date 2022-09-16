/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Provide Cryptographic Signature capability
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * Uses the OpenSSL library, available at
 *	http://www.openssl.org/
 */

/*** MODULEINFO
	<depend>openssl</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <dirent.h>                 /* for closedir, opendir, readdir, DIR */
#include <sys/stat.h>               /* for fstat */

#include <openssl/err.h>            /* for ERR_print_errors_fp */
#include <openssl/ssl.h>            /* for NID_sha1, RSA */
#include <openssl/evp.h>            /* for EVP_PKEY, EVP_sha1(), ... */
#include <openssl/md5.h>            /* for MD5_DIGEST_LENGTH */
#include <openssl/sha.h>            /* for SHA_DIGEST_LENGTH */

#include "asterisk/cli.h"           /* for ast_cli, ast_cli_args, ast_cli_entry */
#include "asterisk/compat.h"        /* for strcasecmp */
#include "asterisk/io.h"            /* for ast_hide_password, ast_restore_tty */
#include "asterisk/linkedlists.h"   /* for AST_RWLIST_TRAVERSE, AST_RWLIST_U... */
#include "asterisk/logger.h"        /* for ast_log, LOG_WARNING, LOG_NOTICE */
#include "asterisk/md5.h"           /* for MD5Final, MD5Init, MD5Update, MD5... */
#include "asterisk/module.h"        /* for ast_module_flags::AST_MODFLAG_GLO... */
#include "asterisk/options.h"       /* for ast_opt_init_keys */
#include "asterisk/paths.h"         /* for ast_config_AST_KEY_DIR */
#include "asterisk/utils.h"         /* for ast_copy_string, ast_base64decode */
#include "asterisk/file.h"          /* for ast_file_read_dirs */

#define AST_API_MODULE
#include "asterisk/crypto.h"        /* for AST_KEY_PUBLIC, AST_KEY_PRIVATE */

/*
 * Asterisk uses RSA keys with SHA-1 message digests for its
 * digital signatures.  The choice of RSA is due to its higher
 * throughput on verification, and the choice of SHA-1 based
 * on the recently discovered collisions in MD5's compression
 * algorithm and recommendations of avoiding MD5 in new schemes
 * from various industry experts.
 *
 * We use OpenSSL to provide our crypto routines, although we never
 * actually use full-up SSL
 *
 */

#define KEY_NEEDS_PASSCODE (1 << 16)

/* From RFC-2437, section 9.1.1 the padding size is 1+2*hLen, where
 * the hLen for SHA-1 is 20 bytes (or 160 bits).
 */
#define RSA_PKCS1_OAEP_PADDING_SIZE		(1 + 2 * SHA_DIGEST_LENGTH)

struct ast_key {
	/*! Name of entity */
	char name[80];
	/*! File name */
	char fn[256];
	/*! Key type (AST_KEY_PUB or AST_KEY_PRIV, along with flags from above) */
	int ktype;
	/*! RSA key structure (if successfully loaded) */
	EVP_PKEY *pkey;
	/*! Whether we should be deleted */
	int delme;
	/*! FD for input (or -1 if no input allowed, or -2 if we needed input) */
	int infd;
	/*! FD for output */
	int outfd;
	/*! Last MD5 Digest */
	unsigned char digest[MD5_DIGEST_LENGTH];
	AST_RWLIST_ENTRY(ast_key) list;
};

static AST_RWLIST_HEAD_STATIC(keys, ast_key);

static void crypto_load(int ifd, int ofd);

/*!
 * \brief setting of priv key
 * \param buf
 * \param size
 * \param rwflag
 * \param userdata
 * \return length of string,-1 on failure
*/
static int pw_cb(char *buf, int size, int rwflag, void *userdata)
{
	struct ast_key *key = (struct ast_key *)userdata;
	char prompt[256];
	int tmp;
	int res;

	if (key->infd < 0) {
		/* Note that we were at least called */
		key->infd = -2;
		return -1;
	}

	snprintf(prompt, sizeof(prompt), ">>>> passcode for %s key '%s': ",
		 key->ktype == AST_KEY_PRIVATE ? "PRIVATE" : "PUBLIC", key->name);
	if (write(key->outfd, prompt, strlen(prompt)) < 0) {
		ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
		key->infd = -2;
		return -1;
	}
	tmp = ast_hide_password(key->infd);
	memset(buf, 0, size);
	res = read(key->infd, buf, size);
	if (res == -1) {
		ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
	}
	ast_restore_tty(key->infd, tmp);
	if (buf[strlen(buf) -1] == '\n') {
		buf[strlen(buf) - 1] = '\0';
	}
	return strlen(buf);
}

/*!
 * \brief return the ast_key structure for name
 * \see ast_key_get
*/
struct ast_key * AST_OPTIONAL_API_NAME(ast_key_get)(const char *kname, int ktype)
{
	struct ast_key *key;

	AST_RWLIST_RDLOCK(&keys);
	AST_RWLIST_TRAVERSE(&keys, key, list) {
		if (!strcmp(kname, key->name) &&
		    (ktype == key->ktype)) {
			break;
		}
	}
	AST_RWLIST_UNLOCK(&keys);

	return key;
}

/*!
 * \brief load RSA key from file
 * \param dir directory string
 * \param fname name of file
 * \param ifd incoming file descriptor
 * \param ofd outgoing file descriptor
 * \param not2
 * \return key on success.
 * \retval NULL on failure.
*/
static struct ast_key *try_load_key(const char *dir, const char *fname, int ifd, int ofd, int *not2)
{
	int n, ktype = 0, found = 0;
	const char *c = NULL;
	char ffname[256];
	unsigned char digest[MD5_DIGEST_LENGTH];
	unsigned digestlen;
	FILE *f;
	EVP_MD_CTX *ctx = NULL;
	struct ast_key *key;
	static int notice = 0;
	struct stat st;
	size_t fnamelen = strlen(fname);

	/* Make sure its name is a public or private key */
	if (fnamelen > 4 && !strcmp((c = &fname[fnamelen - 4]), ".pub")) {
		ktype = AST_KEY_PUBLIC;
	} else if (fnamelen > 4 && !strcmp((c = &fname[fnamelen - 4]), ".key")) {
		ktype = AST_KEY_PRIVATE;
	} else {
		return NULL;
	}

	/* Get actual filename */
	snprintf(ffname, sizeof(ffname), "%s/%s", dir, fname);

	/* Open file */
	if (!(f = fopen(ffname, "r"))) {
		ast_log(LOG_WARNING, "Unable to open key file %s: %s\n", ffname, strerror(errno));
		return NULL;
	}

	n = fstat(fileno(f), &st);
	if (n != 0) {
		ast_log(LOG_ERROR, "Unable to stat key file: %s: %s\n", ffname, strerror(errno));
		fclose(f);
		return NULL;
	}

	if (!S_ISREG(st.st_mode)) {
		ast_log(LOG_ERROR, "Key file is not a regular file: %s\n", ffname);
		fclose(f);
		return NULL;
	}

	/* only user read or read/write modes allowed */
	if (ktype == AST_KEY_PRIVATE &&
	    ((st.st_mode & ALLPERMS) & ~(S_IRUSR | S_IWUSR)) != 0) {
		ast_log(LOG_ERROR, "Private key file has bad permissions: %s: %#4o\n", ffname, st.st_mode & ALLPERMS);
		fclose(f);
		return NULL;
	}

	ctx = EVP_MD_CTX_create();
	if (ctx == NULL) {
		ast_log(LOG_ERROR, "Out of memory\n");
		fclose(f);
		return NULL;
	}
	EVP_DigestInit(ctx, EVP_md5());

	while (!feof(f)) {
		/* Calculate a "whatever" quality md5sum of the key */
		char buf[256] = "";
		if (!fgets(buf, sizeof(buf), f)) {
			continue;
		}
		if (!feof(f)) {
			EVP_DigestUpdate(ctx, (unsigned char *)buf, strlen(buf));
		}
	}
	EVP_DigestFinal(ctx, digest, &digestlen);
	EVP_MD_CTX_destroy(ctx);

	/* Look for an existing key */
	AST_RWLIST_TRAVERSE(&keys, key, list) {
		if (!strcasecmp(key->fn, ffname)) {
			break;
		}
	}

	if (key) {
		/* If the MD5 sum is the same, and it isn't awaiting a passcode
		   then this is far enough */
		if (!memcmp(digest, key->digest, sizeof(digest)) &&
		    !(key->ktype & KEY_NEEDS_PASSCODE)) {
			fclose(f);
			key->delme = 0;
			return NULL;
		} else {
			/* Preserve keytype */
			ktype = key->ktype;
			/* Recycle the same structure */
			found++;
		}
	}

	if (!key) {
		if (!(key = ast_calloc(1, sizeof(*key)))) {
			fclose(f);
			return NULL;
		}
	}
	/* First the filename */
	ast_copy_string(key->fn, ffname, sizeof(key->fn));
	/* Then the name minus the suffix */
	snprintf(key->name, sizeof(key->name), "%.*s", (int)(c - fname), fname);
	key->ktype = ktype;
	/* Yes, assume we're going to be deleted */
	key->delme = 1;
	/* Keep the key type */
	memcpy(key->digest, digest, sizeof(key->digest));
	/* Can I/O takes the FD we're given */
	key->infd = ifd;
	key->outfd = ofd;
	/* Reset the file back to the beginning */
	rewind(f);
	/* Now load the key with the right method */
	if (ktype == AST_KEY_PUBLIC) {
		PEM_read_PUBKEY(f, &key->pkey, pw_cb, key);
	} else {
		PEM_read_PrivateKey(f, &key->pkey, pw_cb, key);
	}
	fclose(f);
	if (key->pkey) {
		if (EVP_PKEY_size(key->pkey) == (AST_CRYPTO_RSA_KEY_BITS / 8)) {
			/* Key loaded okay */
			key->ktype &= ~KEY_NEEDS_PASSCODE;
			ast_verb(3, "Loaded %s key '%s'\n", key->ktype == AST_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
			ast_debug(1, "Key '%s' loaded OK\n", key->name);
			key->delme = 0;
		} else {
			ast_log(LOG_NOTICE, "Key '%s' is not expected size.\n", key->name);
		}
	} else if (key->infd != -2) {
		ast_log(LOG_WARNING, "Key load %s '%s' failed\n", key->ktype == AST_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
		if (ofd > -1) {
			ERR_print_errors_fp(stderr);
		} else {
			ERR_print_errors_fp(stderr);
		}
	} else {
		ast_log(LOG_NOTICE, "Key '%s' needs passcode.\n", key->name);
		key->ktype |= KEY_NEEDS_PASSCODE;
		if (!notice) {
			if (!ast_opt_init_keys) {
				ast_log(LOG_NOTICE, "Add the '-i' flag to the asterisk command line if you want to automatically initialize passcodes at launch.\n");
			}
			notice++;
		}
		/* Keep it anyway */
		key->delme = 0;
		/* Print final notice about "keys init" when done */
		*not2 = 1;
	}

	/* If this is a new key add it to the list */
	if (!found) {
		AST_RWLIST_INSERT_TAIL(&keys, key, list);
	}

	return key;
}

static int evp_pkey_sign(EVP_PKEY *pkey, const unsigned char *in, unsigned inlen, unsigned char *sig, unsigned *siglen, unsigned padding)
{
	EVP_PKEY_CTX *ctx = NULL;
	int res = -1;
	size_t _siglen;

	if (*siglen < EVP_PKEY_size(pkey)) {
		return -1;
	}

	if ((ctx = EVP_PKEY_CTX_new(pkey, NULL)) == NULL) {
		return -1;
	}

	do {
		if ((res = EVP_PKEY_sign_init(ctx)) <= 0) {
			break;
		}
		if ((res = EVP_PKEY_CTX_set_rsa_padding(ctx, padding)) <= 0) {
			break;
		}
		if ((res = EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha1())) <= 0) {
			break;
		}
		_siglen = *siglen;
		if ((res = EVP_PKEY_sign(ctx, sig, &_siglen, in, inlen)) <= 0) {
			break;
		}
		*siglen = _siglen;
	} while (0);

	EVP_PKEY_CTX_free(ctx);
	return res;
}

/*!
 * \brief signs outgoing message with public key
 * \see ast_sign_bin
*/
int AST_OPTIONAL_API_NAME(ast_sign_bin)(struct ast_key *key, const char *msg, int msglen, unsigned char *dsig)
{
	unsigned char digest[SHA_DIGEST_LENGTH];
	unsigned digestlen, siglen = 128;
	int res;
	EVP_MD_CTX *ctx = NULL;

	if (key->ktype != AST_KEY_PRIVATE) {
		ast_log(LOG_WARNING, "Cannot sign with a public key\n");
		return -1;
	}

	if (siglen < EVP_PKEY_size(key->pkey)) {
		ast_log(LOG_WARNING, "Signature buffer too small\n");
		return -1;
	}

	/* Calculate digest of message */
	ctx = EVP_MD_CTX_create();
	if (ctx == NULL) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
	EVP_DigestInit(ctx, EVP_sha1());
	EVP_DigestUpdate(ctx, msg, msglen);
	EVP_DigestFinal(ctx, digest, &digestlen);
	EVP_MD_CTX_destroy(ctx);

	/* Verify signature */
	if ((res = evp_pkey_sign(key->pkey, digest, sizeof(digest), dsig, &siglen, RSA_PKCS1_PADDING)) <= 0) {
		ast_log(LOG_WARNING, "RSA Signature (key %s) failed %d\n", key->name, res);
		return -1;
	}

	if (siglen != EVP_PKEY_size(key->pkey)) {
		ast_log(LOG_WARNING, "Unexpected signature length %u, expecting %d\n", siglen, EVP_PKEY_size(key->pkey));
		return -1;
	}

	return 0;
}

static int evp_pkey_decrypt(EVP_PKEY *pkey, const unsigned char *in, unsigned inlen, unsigned char *out, unsigned *outlen, unsigned padding)
{
	EVP_PKEY_CTX *ctx = NULL;
	int res = -1;
	size_t _outlen;

	if (*outlen < EVP_PKEY_size(pkey)) {
		return -1;
	}

	if (inlen != EVP_PKEY_size(pkey)) {
		return -1;
	}

	if ((ctx = EVP_PKEY_CTX_new(pkey, NULL)) == NULL) {
		return -1;
	}

	do {
		if ((res = EVP_PKEY_decrypt_init(ctx)) <= 0) {
			break;
		}
		if ((res = EVP_PKEY_CTX_set_rsa_padding(ctx, padding)) <= 0) {
			break;
		}
		_outlen = *outlen;
		if ((res = EVP_PKEY_decrypt(ctx, out, &_outlen, in, inlen)) <= 0) {
			break;
		}
		res = *outlen = _outlen;
	} while (0);

	EVP_PKEY_CTX_free(ctx);
	return res;
}

/*!
 * \brief decrypt a message
 * \see ast_decrypt_bin
*/
int AST_OPTIONAL_API_NAME(ast_decrypt_bin)(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key)
{
	int res;
	unsigned pos = 0, dstlen, blocksize;

	if (key->ktype != AST_KEY_PRIVATE) {
		ast_log(LOG_WARNING, "Cannot decrypt with a public key\n");
		return -1;
	}

	blocksize = EVP_PKEY_size(key->pkey);

	if (srclen % blocksize) {
		ast_log(LOG_NOTICE, "Tried to decrypt something not a multiple of %u bytes\n", blocksize);
		return -1;
	}

	while (srclen > 0) {
		/* Process chunks 128 bytes at a time */
		dstlen = blocksize;
		if ((res = evp_pkey_decrypt(key->pkey, src, blocksize, dst, &dstlen, RSA_PKCS1_OAEP_PADDING)) <= 0) {
			return -1;
		}
		pos += dstlen;
		src += blocksize;
		srclen -= blocksize;
		dst += dstlen;
	}

	return pos;
}

static int evp_pkey_encrypt(EVP_PKEY *pkey, const unsigned char *in, unsigned inlen, unsigned char *out, unsigned *outlen, unsigned padding)
{
	EVP_PKEY_CTX *ctx = NULL;
	int res = -1;
	size_t _outlen;

	if (padding != RSA_PKCS1_OAEP_PADDING) {
		ast_log(LOG_WARNING, "Only OAEP padding is supported for now\n");
		return -1;
	}

	if (inlen > EVP_PKEY_size(pkey) - RSA_PKCS1_OAEP_PADDING_SIZE) {
		return -1;
	}

	if (*outlen < EVP_PKEY_size(pkey)) {
		return -1;
	}

	do {
		if ((ctx = EVP_PKEY_CTX_new(pkey, NULL)) == NULL) {
			break;
		}

		if ((res = EVP_PKEY_encrypt_init(ctx)) <= 0) {
			break;
		}
		if ((res = EVP_PKEY_CTX_set_rsa_padding(ctx, padding)) <= 0) {
			break;
		}
		_outlen = *outlen;
		if ((res = EVP_PKEY_encrypt(ctx, out, &_outlen, in, inlen)) <= 0) {
			break;
		}
		res = *outlen = _outlen;
	} while (0);

	EVP_PKEY_CTX_free(ctx);
	return res;
}

/*!
 * \brief encrypt a message
 * \see ast_encrypt_bin
*/
int AST_OPTIONAL_API_NAME(ast_encrypt_bin)(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key)
{
	unsigned bytes, pos = 0, dstlen, blocksize;
	int res;

	if (key->ktype != AST_KEY_PUBLIC) {
		ast_log(LOG_WARNING, "Cannot encrypt with a private key\n");
		return -1;
	}

	blocksize = EVP_PKEY_size(key->pkey);

	while (srclen) {
		bytes = srclen;
		if (bytes > blocksize - RSA_PKCS1_OAEP_PADDING_SIZE) {
			bytes = blocksize - RSA_PKCS1_OAEP_PADDING_SIZE;
		}
		/* Process chunks 128-41 bytes at a time */
		dstlen = blocksize;
		if ((res = evp_pkey_encrypt(key->pkey, src, bytes, dst, &dstlen, RSA_PKCS1_OAEP_PADDING)) != blocksize) {
			ast_log(LOG_NOTICE, "How odd, encrypted size is %d\n", res);
			return -1;
		}
		src += bytes;
		srclen -= bytes;
		pos += dstlen;
		dst += dstlen;
	}
	return pos;
}

/*!
 * \brief wrapper for __ast_sign_bin then base64 encode it
 * \see ast_sign
*/
int AST_OPTIONAL_API_NAME(ast_sign)(struct ast_key *key, char *msg, char *sig)
{
	/* assumes 1024 bit RSA key size */
	unsigned char dsig[128];
	int siglen = sizeof(dsig), res;

	if (!(res = ast_sign_bin(key, msg, strlen(msg), dsig))) {
		/* Success -- encode (256 bytes max as documented) */
		ast_base64encode(sig, dsig, siglen, 256);
	}

	return res;
}

static int evp_pkey_verify(EVP_PKEY *pkey, const unsigned char *in, unsigned inlen, const unsigned char *sig, unsigned siglen, unsigned padding)
{
	EVP_PKEY_CTX *ctx = NULL;
	int res = -1;

	if (siglen < EVP_PKEY_size(pkey)) {
		return -1;
	}

	if ((ctx = EVP_PKEY_CTX_new(pkey, NULL)) == NULL) {
		return -1;
	}

	do {
		if ((res = EVP_PKEY_verify_init(ctx)) <= 0) {
			break;
		}
		if ((res = EVP_PKEY_CTX_set_rsa_padding(ctx, padding)) <= 0) {
			break;
		}
		if ((res = EVP_PKEY_CTX_set_signature_md(ctx, EVP_sha1())) <= 0) {
			break;
		}
		if ((res = EVP_PKEY_verify(ctx, sig, siglen, in, inlen)) <= 0) {
			break;
		}
	} while (0);

	EVP_PKEY_CTX_free(ctx);
	return res;
}

/*!
 * \brief check signature of a message
 * \see ast_check_signature_bin
*/
int AST_OPTIONAL_API_NAME(ast_check_signature_bin)(struct ast_key *key, const char *msg, int msglen, const unsigned char *dsig)
{
	unsigned char digest[SHA_DIGEST_LENGTH];
	unsigned digestlen;
	int res;
	EVP_MD_CTX *ctx = NULL;

	if (key->ktype != AST_KEY_PUBLIC) {
		/* Okay, so of course you really *can* but for our purposes
		   we're going to say you can't */
		ast_log(LOG_WARNING, "Cannot check message signature with a private key\n");
		return -1;
	}

	/* Calculate digest of message */
	ctx = EVP_MD_CTX_create();
	if (ctx == NULL) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}
	EVP_DigestInit(ctx, EVP_sha1());
	EVP_DigestUpdate(ctx, msg, msglen);
	EVP_DigestFinal(ctx, digest, &digestlen);
	EVP_MD_CTX_destroy(ctx);

	/* Verify signature */
	if (!(res = evp_pkey_verify(key->pkey, (const unsigned char *)digest, sizeof(digest), (unsigned char *)dsig, 128, RSA_PKCS1_PADDING))) {
		ast_debug(1, "Key failed verification: %s\n", key->name);
		return -1;
	}

	/* Pass */
	return 0;
}

/*!
 * \brief base64 decode then sent to __ast_check_signature_bin
 * \see ast_check_signature
*/
int AST_OPTIONAL_API_NAME(ast_check_signature)(struct ast_key *key, const char *msg, const char *sig)
{
	unsigned char dsig[128];
	int res;

	/* Decode signature */
	if ((res = ast_base64decode(dsig, sig, sizeof(dsig))) != sizeof(dsig)) {
		ast_log(LOG_WARNING, "Signature improper length (expect %d, got %d)\n", (int)sizeof(dsig), (int)res);
		return -1;
	}

	res = ast_check_signature_bin(key, msg, strlen(msg), dsig);

	return res;
}

int AST_OPTIONAL_API_NAME(ast_crypto_loaded)(void)
{
	return 1;
}

int AST_OPTIONAL_API_NAME(ast_crypto_reload)(void)
{
	crypto_load(-1, -1);
	return 1;
}

int AST_OPTIONAL_API_NAME(ast_aes_set_encrypt_key)(const unsigned char *key, ast_aes_encrypt_key *ctx)
{
	if (key == NULL || ctx == NULL) {
		return -1;
	}
	memcpy(ctx->raw, key, AST_CRYPTO_AES_BLOCKSIZE / 8);
	return 0;
}

int AST_OPTIONAL_API_NAME(ast_aes_set_decrypt_key)(const unsigned char *key, ast_aes_decrypt_key *ctx)
{
	if (key == NULL || ctx == NULL) {
		return -1;
	}
	memcpy(ctx->raw, key, AST_CRYPTO_AES_BLOCKSIZE / 8);
	return 0;
}

static int evp_cipher_aes_encrypt(const unsigned char *in, unsigned char *out, unsigned inlen, const ast_aes_encrypt_key *key)
{
	EVP_CIPHER_CTX *ctx = NULL;
	int res, outlen, finallen;
	unsigned char final[AST_CRYPTO_AES_BLOCKSIZE / 8];

	if ((ctx = EVP_CIPHER_CTX_new()) == NULL) {
		return -1;
	}

	do {
		if ((res = EVP_CipherInit(ctx, EVP_aes_128_ecb(), key->raw, NULL, 1)) <= 0) {
			break;
		}
		EVP_CIPHER_CTX_set_padding(ctx, 0);
		if ((res = EVP_CipherUpdate(ctx, out, &outlen, in, inlen)) <= 0) {
			break;
		}
		/* for ECB, this is a no-op */
		if ((res = EVP_CipherFinal(ctx, final, &finallen)) <= 0) {
			break;
		}

		res = outlen;
	} while (0);

	EVP_CIPHER_CTX_free(ctx);

	return res;
}

int AST_OPTIONAL_API_NAME(ast_aes_encrypt)(const unsigned char *in, unsigned char *out, const ast_aes_encrypt_key *key)
{
	int res;

	if ((res = evp_cipher_aes_encrypt(in, out, AST_CRYPTO_AES_BLOCKSIZE / 8, key)) <= 0) {
		ast_log(LOG_ERROR, "AES encryption failed\n");
	}
	return res;
}

static int evp_cipher_aes_decrypt(const unsigned char *in, unsigned char *out, unsigned inlen, const ast_aes_decrypt_key *key)
{
	EVP_CIPHER_CTX *ctx = NULL;
	int res, outlen, finallen;
	unsigned char final[AST_CRYPTO_AES_BLOCKSIZE / 8];

	if ((ctx = EVP_CIPHER_CTX_new()) == NULL) {
		return -1;
	}

	do {
		if ((res = EVP_CipherInit(ctx, EVP_aes_128_ecb(), key->raw, NULL, 0)) <= 0) {
			break;
		}
		EVP_CIPHER_CTX_set_padding(ctx, 0);
		if ((res = EVP_CipherUpdate(ctx, out, &outlen, in, inlen)) <= 0) {
			break;
		}
		/* for ECB, this is a no-op */
		if ((res = EVP_CipherFinal(ctx, final, &finallen)) <= 0) {
			break;
		}

		res = outlen;
	} while (0);

	EVP_CIPHER_CTX_free(ctx);

	return res;
}

int AST_OPTIONAL_API_NAME(ast_aes_decrypt)(const unsigned char *in, unsigned char *out, const ast_aes_decrypt_key *key)
{
	int res;

	if ((res = evp_cipher_aes_decrypt(in, out, AST_CRYPTO_AES_BLOCKSIZE / 8, key)) <= 0) {
		ast_log(LOG_ERROR, "AES decryption failed\n");
	}
	return res;
}

struct crypto_load_on_file {
	int ifd;
	int ofd;
	int note;
};

static int crypto_load_cb(const char *directory, const char *file, void *obj)
{
	struct crypto_load_on_file *on_file = obj;

	try_load_key(directory, file, on_file->ifd, on_file->ofd, &on_file->note);
	return 0;
}

/*!
 * \brief refresh RSA keys from file
 * \param ifd file descriptor
 * \param ofd file descriptor
*/
static void crypto_load(int ifd, int ofd)
{
	struct ast_key *key;
	struct crypto_load_on_file on_file = { ifd, ofd, 0 };

	AST_RWLIST_WRLOCK(&keys);

	/* Mark all keys for deletion */
	AST_RWLIST_TRAVERSE(&keys, key, list) {
		key->delme = 1;
	}

	if (ast_file_read_dirs(ast_config_AST_KEY_DIR, crypto_load_cb, &on_file, 1) == -1) {
		ast_log(LOG_WARNING, "Unable to open key directory '%s'\n", ast_config_AST_KEY_DIR);
	}

	if (on_file.note) {
		ast_log(LOG_NOTICE, "Please run the command 'keys init' to enter the passcodes for the keys\n");
	}

	/* Delete any keys that are no longer present */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&keys, key, list) {
		if (key->delme) {
			ast_debug(1, "Deleting key %s type %d\n", key->name, key->ktype);
			AST_RWLIST_REMOVE_CURRENT(list);
			if (key->pkey) {
				EVP_PKEY_free(key->pkey);
			}
			ast_free(key);
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	AST_RWLIST_UNLOCK(&keys);
}

static void md52sum(char *sum, unsigned char *md5)
{
	int x;
	for (x = 0; x < MD5_DIGEST_LENGTH; x++) {
		sum += sprintf(sum, "%02hhx", *(md5++));
	}
}

/*!
 * \brief show the list of RSA keys
 * \param e CLI command
 * \param cmd
 * \param a list of CLI arguments
 * \retval CLI_SUCCESS
*/
static char *handle_cli_keys_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%-18s %-8s %-16s %-33s\n"

	struct ast_key *key;
	char sum[MD5_DIGEST_LENGTH * 2 + 1];
	int count_keys = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "keys show";
		e->usage =
			"Usage: keys show\n"
			"       Displays information about RSA keys known by Asterisk\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, FORMAT, "Key Name", "Type", "Status", "Sum");
	ast_cli(a->fd, FORMAT, "------------------", "--------", "----------------", "--------------------------------");

	AST_RWLIST_RDLOCK(&keys);
	AST_RWLIST_TRAVERSE(&keys, key, list) {
		md52sum(sum, key->digest);
		ast_cli(a->fd, FORMAT, key->name,
			(key->ktype & 0xf) == AST_KEY_PUBLIC ? "PUBLIC" : "PRIVATE",
			key->ktype & KEY_NEEDS_PASSCODE ? "[Needs Passcode]" : "[Loaded]", sum);
		count_keys++;
	}
	AST_RWLIST_UNLOCK(&keys);

	ast_cli(a->fd, "\n%d known RSA keys.\n", count_keys);

	return CLI_SUCCESS;

#undef FORMAT
}

/*!
 * \brief initialize all RSA keys
 * \param e CLI command
 * \param cmd
 * \param a list of CLI arguments
 * \retval CLI_SUCCESS
*/
static char *handle_cli_keys_init(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_key *key;
	int ign;
	char *kn, tmp[256] = "";

	switch (cmd) {
	case CLI_INIT:
		e->command = "keys init";
		e->usage =
			"Usage: keys init\n"
			"       Initializes private keys (by reading in pass code from\n"
			"       the user)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_WRLOCK(&keys);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&keys, key, list) {
		/* Reload keys that need pass codes now */
		if (key->ktype & KEY_NEEDS_PASSCODE) {
			kn = key->fn + strlen(ast_config_AST_KEY_DIR) + 1;
			ast_copy_string(tmp, kn, sizeof(tmp));
			try_load_key(ast_config_AST_KEY_DIR, tmp, a->fd, a->fd, &ign);
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END
	AST_RWLIST_UNLOCK(&keys);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_crypto[] = {
	AST_CLI_DEFINE(handle_cli_keys_show, "Displays RSA key information"),
	AST_CLI_DEFINE(handle_cli_keys_init, "Initialize RSA key passcodes")
};

/*! \brief initialise the res_crypto module */
static int crypto_init(void)
{
	ast_cli_register_multiple(cli_crypto, ARRAY_LEN(cli_crypto));
	return 0;
}

static int reload(void)
{
	crypto_load(-1, -1);
	return 0;
}

static int load_module(void)
{
	crypto_init();
	if (ast_opt_init_keys) {
		crypto_load(STDIN_FILENO, STDOUT_FILENO);
	} else {
		crypto_load(-1, -1);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_crypto, ARRAY_LEN(cli_crypto));

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Cryptographic Digital Signatures",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND, /*!< Since we don't have a config file, we could move up to REALTIME_DEPEND, if necessary */
);
