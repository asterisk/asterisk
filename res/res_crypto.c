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
 */

/*** MODULEINFO
	<depend>ssl</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/say.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/crypto.h"
#include "asterisk/md5.h"
#include "asterisk/cli.h"
#include "asterisk/io.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"

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

/*
 * XXX This module is not very thread-safe.  It is for everyday stuff
 *     like reading keys and stuff, but there are all kinds of weird
 *     races with people running reload and key init at the same time
 *     for example
 *
 * XXXX
 */

AST_MUTEX_DEFINE_STATIC(keylock);

#define KEY_NEEDS_PASSCODE (1 << 16)

struct ast_key {
	/* Name of entity */
	char name[80];
	/* File name */
	char fn[256];
	/* Key type (AST_KEY_PUB or AST_KEY_PRIV, along with flags from above) */
	int ktype;
	/* RSA structure (if successfully loaded) */
	RSA *rsa;
	/* Whether we should be deleted */
	int delme;
	/* FD for input (or -1 if no input allowed, or -2 if we needed input) */
	int infd;
	/* FD for output */
	int outfd;
	/* Last MD5 Digest */
	unsigned char digest[16];
	struct ast_key *next;
};

static struct ast_key *keys = NULL;

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

#if 0
static int fdprint(int fd, char *s)
{
        return write(fd, s, strlen(s) + 1);
}
#endif
static int pw_cb(char *buf, int size, int rwflag, void *userdata)
{
	struct ast_key *key = (struct ast_key *)userdata;
	char prompt[256];
	int res;
	int tmp;
	if (key->infd > -1) {
		snprintf(prompt, sizeof(prompt), ">>>> passcode for %s key '%s': ",
			 key->ktype == AST_KEY_PRIVATE ? "PRIVATE" : "PUBLIC", key->name);
		if (write(key->outfd, prompt, strlen(prompt)) < 0) {
			/* Note that we were at least called */
			key->infd = -2;
			return -1;
		}
		memset(buf, 0, sizeof(buf));
		tmp = ast_hide_password(key->infd);
		memset(buf, 0, size);
		res = read(key->infd, buf, size);
		ast_restore_tty(key->infd, tmp);
		if (buf[strlen(buf) -1] == '\n')
			buf[strlen(buf) - 1] = '\0';
		return strlen(buf);
	} else {
		/* Note that we were at least called */
		key->infd = -2;
	}
	return -1;
}

static struct ast_key *__ast_key_get(const char *kname, int ktype)
{
	struct ast_key *key;
	ast_mutex_lock(&keylock);
	key = keys;
	while(key) {
		if (!strcmp(kname, key->name) &&
		    (ktype == key->ktype))
			break;
		key = key->next;
	}
	ast_mutex_unlock(&keylock);
	return key;
}

static struct ast_key *try_load_key (char *dir, char *fname, int ifd, int ofd, int *not2)
{
	int ktype = 0;
	char *c = NULL;
	char ffname[256];
	unsigned char digest[16];
	FILE *f;
	struct MD5Context md5;
	struct ast_key *key;
	static int notice = 0;
	int found = 0;

	/* Make sure its name is a public or private key */

	if ((c = strstr(fname, ".pub")) && !strcmp(c, ".pub")) {
		ktype = AST_KEY_PUBLIC;
	} else if ((c = strstr(fname, ".key")) && !strcmp(c, ".key")) {
		ktype = AST_KEY_PRIVATE;
	} else
		return NULL;

	/* Get actual filename */
	snprintf(ffname, sizeof(ffname), "%s/%s", dir, fname);

	ast_mutex_lock(&keylock);
	key = keys;
	while(key) {
		/* Look for an existing version already */
		if (!strcasecmp(key->fn, ffname)) 
			break;
		key = key->next;
	}
	ast_mutex_unlock(&keylock);

	/* Open file */
	f = fopen(ffname, "r");
	if (!f) {
		ast_log(LOG_WARNING, "Unable to open key file %s: %s\n", ffname, strerror(errno));
		return NULL;
	}
	MD5Init(&md5);
	while(!feof(f)) {
		/* Calculate a "whatever" quality md5sum of the key */
		char buf[256];
		memset(buf, 0, 256);
		if (fgets(buf, sizeof(buf), f)) {
			MD5Update(&md5, (unsigned char *) buf, strlen(buf));
		}
	}
	MD5Final(digest, &md5);
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
	/* At this point we have a key structure (old or new).  Time to
	   fill it with what we know */
	/* Gotta lock if this one already exists */
	if (found)
		ast_mutex_lock(&keylock);
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
	if (ktype == AST_KEY_PUBLIC)
		key->rsa = PEM_read_RSA_PUBKEY(f, NULL, pw_cb, key);
	else
		key->rsa = PEM_read_RSAPrivateKey(f, NULL, pw_cb, key);
	fclose(f);
	if (key->rsa) {
		if (RSA_size(key->rsa) == 128) {
			/* Key loaded okay */
			key->ktype &= ~KEY_NEEDS_PASSCODE;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Loaded %s key '%s'\n", key->ktype == AST_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
			if (option_debug)
				ast_log(LOG_DEBUG, "Key '%s' loaded OK\n", key->name);
			key->delme = 0;
		} else
			ast_log(LOG_NOTICE, "Key '%s' is not expected size.\n", key->name);
	} else if (key->infd != -2) {
		ast_log(LOG_WARNING, "Key load %s '%s' failed\n",key->ktype == AST_KEY_PUBLIC ? "PUBLIC" : "PRIVATE", key->name);
		if (ofd > -1) {
			ERR_print_errors_fp(stderr);
		} else
			ERR_print_errors_fp(stderr);
	} else {
		ast_log(LOG_NOTICE, "Key '%s' needs passcode.\n", key->name);
		key->ktype |= KEY_NEEDS_PASSCODE;
		if (!notice) {
			if (!ast_opt_init_keys) 
				ast_log(LOG_NOTICE, "Add the '-i' flag to the asterisk command line if you want to automatically initialize passcodes at launch.\n");
			notice++;
		}
		/* Keep it anyway */
		key->delme = 0;
		/* Print final notice about "init keys" when done */
		*not2 = 1;
	}
	if (found)
		ast_mutex_unlock(&keylock);
	if (!found) {
		ast_mutex_lock(&keylock);
		key->next = keys;
		keys = key;
		ast_mutex_unlock(&keylock);
	}
	return key;
}

#if 0

static void dump(unsigned char *src, int len)
{
	int x; 
	for (x=0;x<len;x++)
		printf("%02x", *(src++));
	printf("\n");
}

static char *binary(int y, int len)
{
	static char res[80];
	int x;
	memset(res, 0, sizeof(res));
	for (x=0;x<len;x++) {
		if (y & (1 << x))
			res[(len - x - 1)] = '1';
		else
			res[(len - x - 1)] = '0';
	}
	return res;
}

#endif

static int __ast_sign_bin(struct ast_key *key, const char *msg, int msglen, unsigned char *dsig)
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
	res = RSA_sign(NID_sha1, digest, sizeof(digest), dsig, &siglen, key->rsa);
	
	if (!res) {
		ast_log(LOG_WARNING, "RSA Signature (key %s) failed\n", key->name);
		return -1;
	}

	if (siglen != 128) {
		ast_log(LOG_WARNING, "Unexpected signature length %d, expecting %d\n", (int)siglen, (int)128);
		return -1;
	}

	return 0;
	
}

static int __ast_decrypt_bin(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key)
{
	int res;
	int pos = 0;
	if (key->ktype != AST_KEY_PRIVATE) {
		ast_log(LOG_WARNING, "Cannot decrypt with a public key\n");
		return -1;
	}

	if (srclen % 128) {
		ast_log(LOG_NOTICE, "Tried to decrypt something not a multiple of 128 bytes\n");
		return -1;
	}
	while(srclen) {
		/* Process chunks 128 bytes at a time */
		res = RSA_private_decrypt(128, src, dst, key->rsa, RSA_PKCS1_OAEP_PADDING);
		if (res < 0)
			return -1;
		pos += res;
		src += 128;
		srclen -= 128;
		dst += res;
	}
	return pos;
}

static int __ast_encrypt_bin(unsigned char *dst, const unsigned char *src, int srclen, struct ast_key *key)
{
	int res;
	int bytes;
	int pos = 0;
	if (key->ktype != AST_KEY_PUBLIC) {
		ast_log(LOG_WARNING, "Cannot encrypt with a private key\n");
		return -1;
	}
	
	while(srclen) {
		bytes = srclen;
		if (bytes > 128 - 41)
			bytes = 128 - 41;
		/* Process chunks 128-41 bytes at a time */
		res = RSA_public_encrypt(bytes, src, dst, key->rsa, RSA_PKCS1_OAEP_PADDING);
		if (res != 128) {
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

static int __ast_sign(struct ast_key *key, char *msg, char *sig)
{
	unsigned char dsig[128];
	int siglen = sizeof(dsig);
	int res;
	res = ast_sign_bin(key, msg, strlen(msg), dsig);
	if (!res)
		/* Success -- encode (256 bytes max as documented) */
		ast_base64encode(sig, dsig, siglen, 256);
	return res;
	
}

static int __ast_check_signature_bin(struct ast_key *key, const char *msg, int msglen, const unsigned char *dsig)
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
	res = RSA_verify(NID_sha1, digest, sizeof(digest), (unsigned char *)dsig, 128, key->rsa);
	
	if (!res) {
		ast_log(LOG_DEBUG, "Key failed verification: %s\n", key->name);
		return -1;
	}
	/* Pass */
	return 0;
}

static int __ast_check_signature(struct ast_key *key, const char *msg, const char *sig)
{
	unsigned char dsig[128];
	int res;

	/* Decode signature */
	res = ast_base64decode(dsig, sig, sizeof(dsig));
	if (res != sizeof(dsig)) {
		ast_log(LOG_WARNING, "Signature improper length (expect %d, got %d)\n", (int)sizeof(dsig), (int)res);
		return -1;
	}
	res = ast_check_signature_bin(key, msg, strlen(msg), dsig);
	return res;
}

static void crypto_load(int ifd, int ofd)
{
	struct ast_key *key, *nkey, *last;
	DIR *dir = NULL;
	struct dirent *ent;
	int note = 0;
	/* Mark all keys for deletion */
	ast_mutex_lock(&keylock);
	key = keys;
	while(key) {
		key->delme = 1;
		key = key->next;
	}
	ast_mutex_unlock(&keylock);
	/* Load new keys */
	dir = opendir((char *)ast_config_AST_KEY_DIR);
	if (dir) {
		while((ent = readdir(dir))) {
			try_load_key((char *)ast_config_AST_KEY_DIR, ent->d_name, ifd, ofd, &note);
		}
		closedir(dir);
	} else
		ast_log(LOG_WARNING, "Unable to open key directory '%s'\n", (char *)ast_config_AST_KEY_DIR);
	if (note) {
		ast_log(LOG_NOTICE, "Please run the command 'init keys' to enter the passcodes for the keys\n");
	}
	ast_mutex_lock(&keylock);
	key = keys;
	last = NULL;
	while(key) {
		nkey = key->next;
		if (key->delme) {
			ast_log(LOG_DEBUG, "Deleting key %s type %d\n", key->name, key->ktype);
			/* Do the delete */
			if (last)
				last->next = nkey;
			else
				keys = nkey;
			if (key->rsa)
				RSA_free(key->rsa);
			free(key);
		} else 
			last = key;
		key = nkey;
	}
	ast_mutex_unlock(&keylock);
}

static void md52sum(char *sum, unsigned char *md5)
{
	int x;
	for (x=0;x<16;x++) 
		sum += sprintf(sum, "%02x", *(md5++));
}

static int show_keys(int fd, int argc, char *argv[])
{
	struct ast_key *key;
	char sum[16 * 2 + 1];
	int count_keys = 0;

	ast_mutex_lock(&keylock);
	key = keys;
	ast_cli(fd, "%-18s %-8s %-16s %-33s\n", "Key Name", "Type", "Status", "Sum");
	while(key) {
		md52sum(sum, key->digest);
		ast_cli(fd, "%-18s %-8s %-16s %-33s\n", key->name, 
			(key->ktype & 0xf) == AST_KEY_PUBLIC ? "PUBLIC" : "PRIVATE",
			key->ktype & KEY_NEEDS_PASSCODE ? "[Needs Passcode]" : "[Loaded]", sum);
				
		key = key->next;
		count_keys++;
	}
	ast_mutex_unlock(&keylock);
	ast_cli(fd, "%d known RSA keys.\n", count_keys);
	return RESULT_SUCCESS;
}

static int init_keys(int fd, int argc, char *argv[])
{
	struct ast_key *key;
	int ign;
	char *kn;
	char tmp[256] = "";

	key = keys;
	while(key) {
		/* Reload keys that need pass codes now */
		if (key->ktype & KEY_NEEDS_PASSCODE) {
			kn = key->fn + strlen(ast_config_AST_KEY_DIR) + 1;
			ast_copy_string(tmp, kn, sizeof(tmp));
			try_load_key((char *)ast_config_AST_KEY_DIR, tmp, fd, fd, &ign);
		}
		key = key->next;
	}
	return RESULT_SUCCESS;
}

static char show_key_usage[] =
"Usage: keys show\n"
"       Displays information about RSA keys known by Asterisk\n";

static char init_keys_usage[] =
"Usage: keys init\n"
"       Initializes private keys (by reading in pass code from the user)\n";

static struct ast_cli_entry cli_show_keys_deprecated = {
	{ "show", "keys", NULL },
	show_keys, NULL,
	NULL };

static struct ast_cli_entry cli_init_keys_deprecated = {
	{ "init", "keys", NULL },
	init_keys, NULL,
	NULL };

static struct ast_cli_entry cli_crypto[] = {
	{ { "keys", "show", NULL },
	show_keys, "Displays RSA key information",
	show_key_usage, NULL, &cli_show_keys_deprecated },

	{ { "keys", "init", NULL },
	init_keys, "Initialize RSA key passcodes",
	init_keys_usage, NULL, &cli_init_keys_deprecated },
};

static int crypto_init(void)
{
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
		return AST_MODULE_LOAD_DECLINE;
	}
	for (i = 0; i < ssl_num_locks; i++) {
		ast_mutex_init(&ssl_locks[i]);
	}
	CRYPTO_set_locking_callback(ssl_lock);

	ast_cli_register_multiple(cli_crypto, sizeof(cli_crypto) / sizeof(struct ast_cli_entry));

	/* Install ourselves into stubs */
	ast_key_get = __ast_key_get;
	ast_check_signature = __ast_check_signature;
	ast_check_signature_bin = __ast_check_signature_bin;
	ast_sign = __ast_sign;
	ast_sign_bin = __ast_sign_bin;
	ast_encrypt_bin = __ast_encrypt_bin;
	ast_decrypt_bin = __ast_decrypt_bin;

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	crypto_load(-1, -1);
	return 0;
}

static int load_module(void)
{
	crypto_init();
	if (ast_opt_init_keys)
		crypto_load(STDIN_FILENO, STDOUT_FILENO);
	else
		crypto_load(-1, -1);
	return 0;
}

static int unload_module(void)
{
	/* Can't unload this once we're loaded */
	return -1;
}

/* needs usecount semantics defined */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Cryptographic Digital Signatures",
		.load = load_module,
		.unload = unload_module,
		.reload = reload
	);
