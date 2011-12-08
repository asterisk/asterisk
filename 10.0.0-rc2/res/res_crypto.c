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
 * \extref Uses the OpenSSL library, available at
 *	http://www.openssl.org/
 */

/*** MODULEINFO
	<depend>openssl</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/paths.h"	/* use ast_config_AST_KEY_DIR */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <dirent.h>

#include "asterisk/module.h"
#include "asterisk/md5.h"
#include "asterisk/cli.h"
#include "asterisk/io.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"

#define AST_API_MODULE
#include "asterisk/crypto.h"

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

struct ast_key {
	/*! Name of entity */
	char name[80];
	/*! File name */
	char fn[256];
	/*! Key type (AST_KEY_PUB or AST_KEY_PRIV, along with flags from above) */
	int ktype;
	/*! RSA structure (if successfully loaded) */
	RSA *rsa;
	/*! Whether we should be deleted */
	int delme;
	/*! FD for input (or -1 if no input allowed, or -2 if we needed input) */
	int infd;
	/*! FD for output */
	int outfd;
	/*! Last MD5 Digest */
	unsigned char digest[16];
	AST_RWLIST_ENTRY(ast_key) list;
};

static AST_RWLIST_HEAD_STATIC(keys, ast_key);

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
	memset(buf, 0, sizeof(buf));
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
 * \retval key on success.
 * \retval NULL on failure.
*/
static struct ast_key *try_load_key(const char *dir, const char *fname, int ifd, int ofd, int *not2)
{
	int ktype = 0, found = 0;
	char *c = NULL, ffname[256];
	unsigned char digest[16];
	FILE *f;
	struct MD5Context md5;
	struct ast_key *key;
	static int notice = 0;

	/* Make sure its name is a public or private key */
	if ((c = strstr(fname, ".pub")) && !strcmp(c, ".pub")) {
		ktype = AST_KEY_PUBLIC;
	} else if ((c = strstr(fname, ".key")) && !strcmp(c, ".key")) {
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

	MD5Init(&md5);
	while (!feof(f)) {
		/* Calculate a "whatever" quality md5sum of the key */
		char buf[256] = "";
		if (!fgets(buf, sizeof(buf), f)) {
			continue;
		}
		if (!feof(f)) {
			MD5Update(&md5, (unsigned char *) buf, strlen(buf));
		}
	}
	MD5Final(digest, &md5);

	/* Look for an existing key */
	AST_RWLIST_TRAVERSE(&keys, key, list) {
		if (!strcasecmp(key->fn, ffname)) {
			break;
		}
	}

	if (key) {
		/* If the MD5 sum is the same, and it isn't awaiting a passcode
		   then this is far enough */
		if (!memcmp(digest, key->digest, 16) &&
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

	/* Make fname just be the normal name now */
	*c = '\0';
	if (!key) {
		if (!(key = ast_calloc(1, sizeof(*key)))) {
			fclose(f);
			return NULL;
		}
	}
	/* First the filename */
	ast_copy_string(key->fn, ffname, sizeof(key->fn));
	/* Then the name */
	ast_copy_string(key->name, fname, sizeof(key->name));
	key->ktype = ktype;
	/* Yes, assume we're going to be deleted */
	key->delme = 1;
	/* Keep the key type */
	memcpy(key->digest, digest, 16);
	/* Can I/O takes the FD we're given */
	key->infd = ifd;
	key->outfd = ofd;
	/* Reset the file back to the beginning */
	rewind(f);
	/* Now load the key with the right method */
	if (ktype == AST_KEY_PUBLIC) {
		key->rsa = PEM_read_RSA_PUBKEY(f, NULL, pw_cb, key);
	} else {
		key->rsa = PEM_read_RSAPrivateKey(f, NULL, pw_cb, key);
	}
	fclose(f);
	if (key->rsa) {
		if (RSA_size(key->rsa) == 128) {
			/* Key loaded okay */
			key->ktype &= ~KEY_NEEDS_PASSCODE;
			ast_verb(3, "Loaded %s key '%s'\n", key->ktype == AST_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
			ast_debug(1, "Key '%s' loaded OK\n", key->name);
			key->delme = 0;
		} else {
			ast_log(LOG_NOTICE, "Key '%s' is not expected size.\n", key->name);
		}
	} else if (key->infd != -2) {
		ast_log(LOG_WARNING, "Key load %s '%s' failed\n",key->ktype == AST_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
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

/*!
 * \brief signs outgoing message with public key
 * \see ast_sign_bin
*/
int AST_OPTIONAL_API_NAME(ast_sign_bin)(struct ast_key *key, const char *msg, int msglen, unsigned char *dsig)
{
	unsigned char digest[20];
	unsigned int siglen = 128;
	int res;

	if (key->ktype != AST_KEY_PRIVATE) {
		ast_log(LOG_WARNING, "Cannot sign with a public key\n");
		return -1;
	}

	/* Calculate digest of message */
	SHA1((unsigned char *)msg, msglen, digest);

	/* Verify signature */
	if (!(res = RSA_sign(NID_sha1, digest, sizeof(digest), dsig, &siglen, key->rsa))) {
		ast_log(LOG_WARNING, "RSA Signature (key %s) failed\n", key->name);
		return -1;
	}

	if (siglen != 128) {
		ast_log(LOG_WARNING, "Unexpected signature length %d, expecting %d\n", (int)siglen, (int)128);
		return -1;
	}

	return 0;
}

/*!
 * \brief decrypt a message
 * \see ast_decrypt_bin
*/
int AST_OPTIONAL_API_NAME(ast_decrypt_bin)(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key)
{
	int res, pos = 0;

	if (key->ktype != AST_KEY_PRIVATE) {
		ast_log(LOG_WARNING, "Cannot decrypt with a public key\n");
		return -1;
	}

	if (srclen % 128) {
		ast_log(LOG_NOTICE, "Tried to decrypt something not a multiple of 128 bytes\n");
		return -1;
	}

	while (srclen) {
		/* Process chunks 128 bytes at a time */
		if ((res = RSA_private_decrypt(128, src, dst, key->rsa, RSA_PKCS1_OAEP_PADDING)) < 0) {
			return -1;
		}
		pos += res;
		src += 128;
		srclen -= 128;
		dst += res;
	}

	return pos;
}

/*!
 * \brief encrypt a message
 * \see ast_encrypt_bin
*/
int AST_OPTIONAL_API_NAME(ast_encrypt_bin)(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key)
{
	int res, bytes, pos = 0;

	if (key->ktype != AST_KEY_PUBLIC) {
		ast_log(LOG_WARNING, "Cannot encrypt with a private key\n");
		return -1;
	}

	while (srclen) {
		bytes = srclen;
		if (bytes > 128 - 41) {
			bytes = 128 - 41;
		}
		/* Process chunks 128-41 bytes at a time */
		if ((res = RSA_public_encrypt(bytes, src, dst, key->rsa, RSA_PKCS1_OAEP_PADDING)) != 128) {
			ast_log(LOG_NOTICE, "How odd, encrypted size is %d\n", res);
			return -1;
		}
		src += bytes;
		srclen -= bytes;
		pos += res;
		dst += res;
	}
	return pos;
}

/*!
 * \brief wrapper for __ast_sign_bin then base64 encode it
 * \see ast_sign
*/
int AST_OPTIONAL_API_NAME(ast_sign)(struct ast_key *key, char *msg, char *sig)
{
	unsigned char dsig[128];
	int siglen = sizeof(dsig), res;

	if (!(res = ast_sign_bin(key, msg, strlen(msg), dsig))) {
		/* Success -- encode (256 bytes max as documented) */
		ast_base64encode(sig, dsig, siglen, 256);
	}

	return res;
}

/*!
 * \brief check signature of a message
 * \see ast_check_signature_bin
*/
int AST_OPTIONAL_API_NAME(ast_check_signature_bin)(struct ast_key *key, const char *msg, int msglen, const unsigned char *dsig)
{
	unsigned char digest[20];
	int res;

	if (key->ktype != AST_KEY_PUBLIC) {
		/* Okay, so of course you really *can* but for our purposes
		   we're going to say you can't */
		ast_log(LOG_WARNING, "Cannot check message signature with a private key\n");
		return -1;
	}

	/* Calculate digest of message */
	SHA1((unsigned char *)msg, msglen, digest);

	/* Verify signature */
	if (!(res = RSA_verify(NID_sha1, digest, sizeof(digest), (unsigned char *)dsig, 128, key->rsa))) {
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

int AST_OPTIONAL_API_NAME(ast_aes_set_encrypt_key)(const unsigned char *key, ast_aes_encrypt_key *ctx)
{
	return AES_set_encrypt_key(key, 128, ctx);
}

int AST_OPTIONAL_API_NAME(ast_aes_set_decrypt_key)(const unsigned char *key, ast_aes_decrypt_key *ctx)
{
	return AES_set_decrypt_key(key, 128, ctx);
}

void AST_OPTIONAL_API_NAME(ast_aes_encrypt)(const unsigned char *in, unsigned char *out, const ast_aes_encrypt_key *ctx)
{
	return AES_encrypt(in, out, ctx);
}

void AST_OPTIONAL_API_NAME(ast_aes_decrypt)(const unsigned char *in, unsigned char *out, const ast_aes_decrypt_key *ctx)
{
	return AES_decrypt(in, out, ctx);
}

/*!
 * \brief refresh RSA keys from file
 * \param ifd file descriptor
 * \param ofd file descriptor
 * \return void
*/
static void crypto_load(int ifd, int ofd)
{
	struct ast_key *key;
	DIR *dir = NULL;
	struct dirent *ent;
	int note = 0;

	AST_RWLIST_WRLOCK(&keys);

	/* Mark all keys for deletion */
	AST_RWLIST_TRAVERSE(&keys, key, list) {
		key->delme = 1;
	}

	/* Load new keys */
	if ((dir = opendir(ast_config_AST_KEY_DIR))) {
		while ((ent = readdir(dir))) {
			try_load_key(ast_config_AST_KEY_DIR, ent->d_name, ifd, ofd, &note);
		}
		closedir(dir);
	} else {
		ast_log(LOG_WARNING, "Unable to open key directory '%s'\n", ast_config_AST_KEY_DIR);
	}

	if (note) {
		ast_log(LOG_NOTICE, "Please run the command 'keys init' to enter the passcodes for the keys\n");
	}

	/* Delete any keys that are no longer present */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&keys, key, list) {
		if (key->delme) {
			ast_debug(1, "Deleting key %s type %d\n", key->name, key->ktype);
			AST_RWLIST_REMOVE_CURRENT(list);
			if (key->rsa) {
				RSA_free(key->rsa);
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
	for (x = 0; x < 16; x++) {
		sum += sprintf(sum, "%02x", *(md5++));
	}
}

/*!
 * \brief show the list of RSA keys
 * \param e CLI command
 * \param cmd
 * \param a list of CLI arguments
 * \return CLI_SUCCESS
*/
static char *handle_cli_keys_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT "%-18s %-8s %-16s %-33s\n"

	struct ast_key *key;
	char sum[16 * 2 + 1];
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
 * \return CLI_SUCCESS
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
	/* Can't unload this once we're loaded */
	return -1;
}

/* needs usecount semantics defined */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Cryptographic Digital Signatures",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND, /*!< Since we don't have a config file, we could move up to REALTIME_DEPEND, if necessary */
	);
