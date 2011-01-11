/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Utility functions
 *
 * \note These are important for portability and security,
 * so please use them in favour of other routines.
 * Please consult the CODING GUIDELINES for more information.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>
#include <sys/stat.h>
#include <sys/stat.h>

#ifdef HAVE_DEV_URANDOM
#include <fcntl.h>
#endif

#include "asterisk/network.h"

#define AST_API_MODULE		/* ensure that inlinable API functions will be built in lock.h if required */
#include "asterisk/lock.h"
#include "asterisk/io.h"
#include "asterisk/md5.h"
#include "asterisk/sha1.h"
#include "asterisk/cli.h"
#include "asterisk/linkedlists.h"

#define AST_API_MODULE		/* ensure that inlinable API functions will be built in this module if required */
#include "asterisk/strings.h"

#define AST_API_MODULE		/* ensure that inlinable API functions will be built in this module if required */
#include "asterisk/time.h"

#define AST_API_MODULE		/* ensure that inlinable API functions will be built in this module if required */
#include "asterisk/stringfields.h"

#define AST_API_MODULE		/* ensure that inlinable API functions will be built in this module if required */
#include "asterisk/utils.h"

#define AST_API_MODULE
#include "asterisk/threadstorage.h"

#define AST_API_MODULE
#include "asterisk/config.h"

static char base64[64];
static char b2a[256];

AST_THREADSTORAGE(inet_ntoa_buf);

#if !defined(HAVE_GETHOSTBYNAME_R_5) && !defined(HAVE_GETHOSTBYNAME_R_6)

#define ERANGE 34	/*!< duh? ERANGE value copied from web... */
#undef gethostbyname

AST_MUTEX_DEFINE_STATIC(__mutex);

/*! \brief Reentrant replacement for gethostbyname for BSD-based systems.
\note This
routine is derived from code originally written and placed in the public 
domain by Enzo Michelangeli <em@em.no-ip.com> */

static int gethostbyname_r (const char *name, struct hostent *ret, char *buf,
				size_t buflen, struct hostent **result, 
				int *h_errnop) 
{
	int hsave;
	struct hostent *ph;
	ast_mutex_lock(&__mutex); /* begin critical area */
	hsave = h_errno;

	ph = gethostbyname(name);
	*h_errnop = h_errno; /* copy h_errno to *h_herrnop */
	if (ph == NULL) {
		*result = NULL;
	} else {
		char **p, **q;
		char *pbuf;
		int nbytes = 0;
		int naddr = 0, naliases = 0;
		/* determine if we have enough space in buf */

		/* count how many addresses */
		for (p = ph->h_addr_list; *p != 0; p++) {
			nbytes += ph->h_length; /* addresses */
			nbytes += sizeof(*p); /* pointers */
			naddr++;
		}
		nbytes += sizeof(*p); /* one more for the terminating NULL */

		/* count how many aliases, and total length of strings */
		for (p = ph->h_aliases; *p != 0; p++) {
			nbytes += (strlen(*p)+1); /* aliases */
			nbytes += sizeof(*p);  /* pointers */
			naliases++;
		}
		nbytes += sizeof(*p); /* one more for the terminating NULL */

		/* here nbytes is the number of bytes required in buffer */
		/* as a terminator must be there, the minimum value is ph->h_length */
		if (nbytes > buflen) {
			*result = NULL;
			ast_mutex_unlock(&__mutex); /* end critical area */
			return ERANGE; /* not enough space in buf!! */
		}

		/* There is enough space. Now we need to do a deep copy! */
		/* Allocation in buffer:
			from [0] to [(naddr-1) * sizeof(*p)]:
			pointers to addresses
			at [naddr * sizeof(*p)]:
			NULL
			from [(naddr+1) * sizeof(*p)] to [(naddr+naliases) * sizeof(*p)] :
			pointers to aliases
			at [(naddr+naliases+1) * sizeof(*p)]:
			NULL
			then naddr addresses (fixed length), and naliases aliases (asciiz).
		*/

		*ret = *ph;   /* copy whole structure (not its address!) */

		/* copy addresses */
		q = (char **)buf; /* pointer to pointers area (type: char **) */
		ret->h_addr_list = q; /* update pointer to address list */
		pbuf = buf + ((naddr + naliases + 2) * sizeof(*p)); /* skip that area */
		for (p = ph->h_addr_list; *p != 0; p++) {
			memcpy(pbuf, *p, ph->h_length); /* copy address bytes */
			*q++ = pbuf; /* the pointer is the one inside buf... */
			pbuf += ph->h_length; /* advance pbuf */
		}
		*q++ = NULL; /* address list terminator */

		/* copy aliases */
		ret->h_aliases = q; /* update pointer to aliases list */
		for (p = ph->h_aliases; *p != 0; p++) {
			strcpy(pbuf, *p); /* copy alias strings */
			*q++ = pbuf; /* the pointer is the one inside buf... */
			pbuf += strlen(*p); /* advance pbuf */
			*pbuf++ = 0; /* string terminator */
		}
		*q++ = NULL; /* terminator */

		strcpy(pbuf, ph->h_name); /* copy alias strings */
		ret->h_name = pbuf;
		pbuf += strlen(ph->h_name); /* advance pbuf */
		*pbuf++ = 0; /* string terminator */

		*result = ret;  /* and let *result point to structure */

	}
	h_errno = hsave;  /* restore h_errno */
	ast_mutex_unlock(&__mutex); /* end critical area */

	return (*result == NULL); /* return 0 on success, non-zero on error */
}


#endif

/*! \brief Re-entrant (thread safe) version of gethostbyname that replaces the 
   standard gethostbyname (which is not thread safe)
*/
struct hostent *ast_gethostbyname(const char *host, struct ast_hostent *hp)
{
	int res;
	int herrno;
	int dots = 0;
	const char *s;
	struct hostent *result = NULL;
	/* Although it is perfectly legitimate to lookup a pure integer, for
	   the sake of the sanity of people who like to name their peers as
	   integers, we break with tradition and refuse to look up a
	   pure integer */
	s = host;
	res = 0;
	while (s && *s) {
		if (*s == '.')
			dots++;
		else if (!isdigit(*s))
			break;
		s++;
	}
	if (!s || !*s) {
		/* Forge a reply for IP's to avoid octal IP's being interpreted as octal */
		if (dots != 3)
			return NULL;
		memset(hp, 0, sizeof(struct ast_hostent));
		hp->hp.h_addrtype = AF_INET;
		hp->hp.h_addr_list = (void *) hp->buf;
		hp->hp.h_addr = hp->buf + sizeof(void *);
		if (inet_pton(AF_INET, host, hp->hp.h_addr) > 0)
			return &hp->hp;
		return NULL;
		
	}
#ifdef HAVE_GETHOSTBYNAME_R_5
	result = gethostbyname_r(host, &hp->hp, hp->buf, sizeof(hp->buf), &herrno);

	if (!result || !hp->hp.h_addr_list || !hp->hp.h_addr_list[0])
		return NULL;
#else
	res = gethostbyname_r(host, &hp->hp, hp->buf, sizeof(hp->buf), &result, &herrno);

	if (res || !result || !hp->hp.h_addr_list || !hp->hp.h_addr_list[0])
		return NULL;
#endif
	return &hp->hp;
}

/*! \brief Produce 32 char MD5 hash of value. */
void ast_md5_hash(char *output, char *input)
{
	struct MD5Context md5;
	unsigned char digest[16];
	char *ptr;
	int x;

	MD5Init(&md5);
	MD5Update(&md5, (unsigned char *)input, strlen(input));
	MD5Final(digest, &md5);
	ptr = output;
	for (x = 0; x < 16; x++)
		ptr += sprintf(ptr, "%2.2x", digest[x]);
}

/*! \brief Produce 40 char SHA1 hash of value. */
void ast_sha1_hash(char *output, char *input)
{
	struct SHA1Context sha;
	char *ptr;
	int x;
	uint8_t Message_Digest[20];

	SHA1Reset(&sha);
	
	SHA1Input(&sha, (const unsigned char *) input, strlen(input));

	SHA1Result(&sha, Message_Digest);
	ptr = output;
	for (x = 0; x < 20; x++)
		ptr += sprintf(ptr, "%2.2x", Message_Digest[x]);
}

/*! \brief decode BASE64 encoded text */
int ast_base64decode(unsigned char *dst, const char *src, int max)
{
	int cnt = 0;
	unsigned int byte = 0;
	unsigned int bits = 0;
	int incnt = 0;
	while(*src && *src != '=' && (cnt < max)) {
		/* Shift in 6 bits of input */
		byte <<= 6;
		byte |= (b2a[(int)(*src)]) & 0x3f;
		bits += 6;
		src++;
		incnt++;
		/* If we have at least 8 bits left over, take that character 
		   off the top */
		if (bits >= 8)  {
			bits -= 8;
			*dst = (byte >> bits) & 0xff;
			dst++;
			cnt++;
		}
	}
	/* Dont worry about left over bits, they're extra anyway */
	return cnt;
}

/*! \brief encode text to BASE64 coding */
int ast_base64encode_full(char *dst, const unsigned char *src, int srclen, int max, int linebreaks)
{
	int cnt = 0;
	int col = 0;
	unsigned int byte = 0;
	int bits = 0;
	int cntin = 0;
	/* Reserve space for null byte at end of string */
	max--;
	while ((cntin < srclen) && (cnt < max)) {
		byte <<= 8;
		byte |= *(src++);
		bits += 8;
		cntin++;
		if ((bits == 24) && (cnt + 4 <= max)) {
			*dst++ = base64[(byte >> 18) & 0x3f];
			*dst++ = base64[(byte >> 12) & 0x3f];
			*dst++ = base64[(byte >> 6) & 0x3f];
			*dst++ = base64[byte & 0x3f];
			cnt += 4;
			col += 4;
			bits = 0;
			byte = 0;
		}
		if (linebreaks && (cnt < max) && (col == 64)) {
			*dst++ = '\n';
			cnt++;
			col = 0;
		}
	}
	if (bits && (cnt + 4 <= max)) {
		/* Add one last character for the remaining bits, 
		   padding the rest with 0 */
		byte <<= 24 - bits;
		*dst++ = base64[(byte >> 18) & 0x3f];
		*dst++ = base64[(byte >> 12) & 0x3f];
		if (bits == 16)
			*dst++ = base64[(byte >> 6) & 0x3f];
		else
			*dst++ = '=';
		*dst++ = '=';
		cnt += 4;
	}
	if (linebreaks && (cnt < max)) {
		*dst++ = '\n';
		cnt++;
	}
	*dst = '\0';
	return cnt;
}

int ast_base64encode(char *dst, const unsigned char *src, int srclen, int max)
{
	return ast_base64encode_full(dst, src, srclen, max, 0);
}

static void base64_init(void)
{
	int x;
	memset(b2a, -1, sizeof(b2a));
	/* Initialize base-64 Conversion table */
	for (x = 0; x < 26; x++) {
		/* A-Z */
		base64[x] = 'A' + x;
		b2a['A' + x] = x;
		/* a-z */
		base64[x + 26] = 'a' + x;
		b2a['a' + x] = x + 26;
		/* 0-9 */
		if (x < 10) {
			base64[x + 52] = '0' + x;
			b2a['0' + x] = x + 52;
		}
	}
	base64[62] = '+';
	base64[63] = '/';
	b2a[(int)'+'] = 62;
	b2a[(int)'/'] = 63;
}

/*! \brief  ast_uri_encode: Turn text string to URI-encoded %XX version
\note 	At this point, we're converting from ISO-8859-x (8-bit), not UTF8
	as in the SIP protocol spec 
	If doreserved == 1 we will convert reserved characters also.
	RFC 2396, section 2.4
	outbuf needs to have more memory allocated than the instring
	to have room for the expansion. Every char that is converted
	is replaced by three ASCII characters.

	Note: The doreserved option is needed for replaces header in
	SIP transfers.
*/
char *ast_uri_encode(const char *string, char *outbuf, int buflen, int doreserved) 
{
	char *reserved = ";/?:@&=+$,# ";	/* Reserved chars */

 	const char *ptr  = string;	/* Start with the string */
	char *out = outbuf;

	/* If there's no characters to convert, just go through and copy the string */
	while (*ptr && out - outbuf < buflen - 1) {
		if ((*ptr < 32) || (doreserved && strchr(reserved, *ptr))) {
			if (out - outbuf >= buflen - 3) {
				break;
			}

			out += sprintf(out, "%%%02x", (unsigned char) *ptr);
		} else {
			*out = *ptr;	/* copy the character */
			out++;
		}
		ptr++;
	}

	if (buflen) {
		*out = '\0';
	}

	return outbuf;
}

/*! \brief  ast_uri_decode: Decode SIP URI, URN, URL (overwrite the string)  */
void ast_uri_decode(char *s) 
{
	char *o;
	unsigned int tmp;

	for (o = s; *s; s++, o++) {
		if (*s == '%' && strlen(s) > 2 && sscanf(s + 1, "%2x", &tmp) == 1) {
			/* have '%', two chars and correct parsing */
			*o = tmp;
			s += 2;	/* Will be incremented once more when we break out */
		} else /* all other cases, just copy */
			*o = *s;
	}
	*o = '\0';
}

/*! \brief  ast_inet_ntoa: Recursive thread safe replacement of inet_ntoa */
const char *ast_inet_ntoa(struct in_addr ia)
{
	char *buf;

	if (!(buf = ast_threadstorage_get(&inet_ntoa_buf, INET_ADDRSTRLEN)))
		return "";

	return inet_ntop(AF_INET, &ia, buf, INET_ADDRSTRLEN);
}

#ifdef HAVE_DEV_URANDOM
static int dev_urandom_fd;
#endif

#ifndef __linux__
#undef pthread_create /* For ast_pthread_create function only */
#endif /* !__linux__ */

#if !defined(LOW_MEMORY)

#ifdef DEBUG_THREADS

/*! \brief A reasonable maximum number of locks a thread would be holding ... */
#define AST_MAX_LOCKS 64

/* Allow direct use of pthread_mutex_t and friends */
#undef pthread_mutex_t
#undef pthread_mutex_lock
#undef pthread_mutex_unlock
#undef pthread_mutex_init
#undef pthread_mutex_destroy

/*! 
 * \brief Keep track of which locks a thread holds 
 *
 * There is an instance of this struct for every active thread
 */
struct thr_lock_info {
	/*! The thread's ID */
	pthread_t thread_id;
	/*! The thread name which includes where the thread was started */
	const char *thread_name;
	/*! This is the actual container of info for what locks this thread holds */
	struct {
		const char *file;
		int line_num;
		const char *func;
		const char *lock_name;
		void *lock_addr;
		int times_locked;
		enum ast_lock_type type;
		/*! This thread is waiting on this lock */
		int pending:2;
#ifdef HAVE_BKTR
		struct ast_bt *backtrace;
#endif
	} locks[AST_MAX_LOCKS];
	/*! This is the number of locks currently held by this thread.
	 *  The index (num_locks - 1) has the info on the last one in the
	 *  locks member */
	unsigned int num_locks;
	/*! Protects the contents of the locks member 
	 * Intentionally not ast_mutex_t */
	pthread_mutex_t lock;
	AST_LIST_ENTRY(thr_lock_info) entry;
};

/*! 
 * \brief Locked when accessing the lock_infos list 
 */
AST_MUTEX_DEFINE_STATIC(lock_infos_lock);
/*!
 * \brief A list of each thread's lock info 
 */
static AST_LIST_HEAD_NOLOCK_STATIC(lock_infos, thr_lock_info);

/*!
 * \brief Destroy a thread's lock info
 *
 * This gets called automatically when the thread stops
 */
static void lock_info_destroy(void *data)
{
	struct thr_lock_info *lock_info = data;
	int i;

	pthread_mutex_lock(&lock_infos_lock.mutex);
	AST_LIST_REMOVE(&lock_infos, lock_info, entry);
	pthread_mutex_unlock(&lock_infos_lock.mutex);


	for (i = 0; i < lock_info->num_locks; i++) {
		if (lock_info->locks[i].pending == -1) {
			/* This just means that the last lock this thread went for was by
			 * using trylock, and it failed.  This is fine. */
			break;
		}

		ast_log(LOG_ERROR, 
			"Thread '%s' still has a lock! - '%s' (%p) from '%s' in %s:%d!\n", 
			lock_info->thread_name,
			lock_info->locks[i].lock_name,
			lock_info->locks[i].lock_addr,
			lock_info->locks[i].func,
			lock_info->locks[i].file,
			lock_info->locks[i].line_num
		);
	}

	pthread_mutex_destroy(&lock_info->lock);
	if (lock_info->thread_name)
		free((void *) lock_info->thread_name);
	free(lock_info);
}

/*!
 * \brief The thread storage key for per-thread lock info
 */
AST_THREADSTORAGE_CUSTOM(thread_lock_info, NULL, lock_info_destroy);
#ifdef HAVE_BKTR
void ast_store_lock_info(enum ast_lock_type type, const char *filename,
	int line_num, const char *func, const char *lock_name, void *lock_addr, struct ast_bt *bt)
#else
void ast_store_lock_info(enum ast_lock_type type, const char *filename,
	int line_num, const char *func, const char *lock_name, void *lock_addr)
#endif
{
	struct thr_lock_info *lock_info;
	int i;

	if (!(lock_info = ast_threadstorage_get(&thread_lock_info, sizeof(*lock_info))))
		return;

	pthread_mutex_lock(&lock_info->lock);

	for (i = 0; i < lock_info->num_locks; i++) {
		if (lock_info->locks[i].lock_addr == lock_addr) {
			lock_info->locks[i].times_locked++;
#ifdef HAVE_BKTR
			lock_info->locks[i].backtrace = bt;
#endif
			pthread_mutex_unlock(&lock_info->lock);
			return;
		}
	}

	if (lock_info->num_locks == AST_MAX_LOCKS) {
		/* Can't use ast_log here, because it will cause infinite recursion */
		fprintf(stderr, "XXX ERROR XXX A thread holds more locks than '%d'."
			"  Increase AST_MAX_LOCKS!\n", AST_MAX_LOCKS);
		pthread_mutex_unlock(&lock_info->lock);
		return;
	}

	if (i && lock_info->locks[i - 1].pending == -1) {
		/* The last lock on the list was one that this thread tried to lock but
		 * failed at doing so.  It has now moved on to something else, so remove
		 * the old lock from the list. */
		i--;
		lock_info->num_locks--;
		memset(&lock_info->locks[i], 0, sizeof(lock_info->locks[0]));
	}

	lock_info->locks[i].file = filename;
	lock_info->locks[i].line_num = line_num;
	lock_info->locks[i].func = func;
	lock_info->locks[i].lock_name = lock_name;
	lock_info->locks[i].lock_addr = lock_addr;
	lock_info->locks[i].times_locked = 1;
	lock_info->locks[i].type = type;
	lock_info->locks[i].pending = 1;
#ifdef HAVE_BKTR
	lock_info->locks[i].backtrace = bt;
#endif
	lock_info->num_locks++;

	pthread_mutex_unlock(&lock_info->lock);
}

void ast_mark_lock_acquired(void *lock_addr)
{
	struct thr_lock_info *lock_info;

	if (!(lock_info = ast_threadstorage_get(&thread_lock_info, sizeof(*lock_info))))
		return;

	pthread_mutex_lock(&lock_info->lock);
	if (lock_info->locks[lock_info->num_locks - 1].lock_addr == lock_addr) {
		lock_info->locks[lock_info->num_locks - 1].pending = 0;
	}
	pthread_mutex_unlock(&lock_info->lock);
}

void ast_mark_lock_failed(void *lock_addr)
{
	struct thr_lock_info *lock_info;

	if (!(lock_info = ast_threadstorage_get(&thread_lock_info, sizeof(*lock_info))))
		return;

	pthread_mutex_lock(&lock_info->lock);
	if (lock_info->locks[lock_info->num_locks - 1].lock_addr == lock_addr) {
		lock_info->locks[lock_info->num_locks - 1].pending = -1;
		lock_info->locks[lock_info->num_locks - 1].times_locked--;
	}
	pthread_mutex_unlock(&lock_info->lock);
}

int ast_find_lock_info(void *lock_addr, char *filename, size_t filename_size, int *lineno, char *func, size_t func_size, char *mutex_name, size_t mutex_name_size)
{
	struct thr_lock_info *lock_info;
	int i = 0;

	if (!(lock_info = ast_threadstorage_get(&thread_lock_info, sizeof(*lock_info))))
		return -1;

	pthread_mutex_lock(&lock_info->lock);

	for (i = lock_info->num_locks - 1; i >= 0; i--) {
		if (lock_info->locks[i].lock_addr == lock_addr)
			break;
	}

	if (i == -1) {
		/* Lock not found :( */
		pthread_mutex_unlock(&lock_info->lock);
		return -1;
	}

	ast_copy_string(filename, lock_info->locks[i].file, filename_size);
	*lineno = lock_info->locks[i].line_num;
	ast_copy_string(func, lock_info->locks[i].func, func_size);
	ast_copy_string(mutex_name, lock_info->locks[i].lock_name, mutex_name_size);

	pthread_mutex_unlock(&lock_info->lock);

	return 0;
}

#ifdef HAVE_BKTR
void ast_remove_lock_info(void *lock_addr, struct ast_bt *bt)
#else
void ast_remove_lock_info(void *lock_addr)
#endif
{
	struct thr_lock_info *lock_info;
	int i = 0;

	if (!(lock_info = ast_threadstorage_get(&thread_lock_info, sizeof(*lock_info))))
		return;

	pthread_mutex_lock(&lock_info->lock);

	for (i = lock_info->num_locks - 1; i >= 0; i--) {
		if (lock_info->locks[i].lock_addr == lock_addr)
			break;
	}

	if (i == -1) {
		/* Lock not found :( */
		pthread_mutex_unlock(&lock_info->lock);
		return;
	}

	if (lock_info->locks[i].times_locked > 1) {
		lock_info->locks[i].times_locked--;
#ifdef HAVE_BKTR
		lock_info->locks[i].backtrace = bt;
#endif
		pthread_mutex_unlock(&lock_info->lock);
		return;
	}

	if (i < lock_info->num_locks - 1) {
		/* Not the last one ... *should* be rare! */
		memmove(&lock_info->locks[i], &lock_info->locks[i + 1], 
			(lock_info->num_locks - (i + 1)) * sizeof(lock_info->locks[0]));
	}

	lock_info->num_locks--;

	pthread_mutex_unlock(&lock_info->lock);
}

static const char *locktype2str(enum ast_lock_type type)
{
	switch (type) {
	case AST_MUTEX:
		return "MUTEX";
	case AST_RDLOCK:
		return "RDLOCK";
	case AST_WRLOCK:
		return "WRLOCK";
	}

	return "UNKNOWN";
}

#ifdef HAVE_BKTR
static void append_backtrace_information(struct ast_str **str, struct ast_bt *bt)
{
	char **symbols;

	if (!bt) {
		ast_str_append(str, 0, "\tNo backtrace to print\n");
		return;
	}

	if ((symbols = ast_bt_get_symbols(bt->addresses, bt->num_frames))) {
		int frame_iterator;
		
		for (frame_iterator = 0; frame_iterator < bt->num_frames; ++frame_iterator) {
			ast_str_append(str, 0, "\t%s\n", symbols[frame_iterator]);
		}

		free(symbols);
	} else {
		ast_str_append(str, 0, "\tCouldn't retrieve backtrace symbols\n");
	}
}
#endif

static void append_lock_information(struct ast_str **str, struct thr_lock_info *lock_info, int i)
{
	int j;
	ast_mutex_t *lock;
	struct ast_lock_track *lt;
	
	ast_str_append(str, 0, "=== ---> %sLock #%d (%s): %s %d %s %s %p (%d)\n", 
				   lock_info->locks[i].pending > 0 ? "Waiting for " : 
				   lock_info->locks[i].pending < 0 ? "Tried and failed to get " : "", i,
				   lock_info->locks[i].file, 
				   locktype2str(lock_info->locks[i].type),
				   lock_info->locks[i].line_num,
				   lock_info->locks[i].func, lock_info->locks[i].lock_name,
				   lock_info->locks[i].lock_addr, 
				   lock_info->locks[i].times_locked);
#ifdef HAVE_BKTR
	append_backtrace_information(str, lock_info->locks[i].backtrace);
#endif
	
	if (!lock_info->locks[i].pending || lock_info->locks[i].pending == -1)
		return;
	
	/* We only have further details for mutexes right now */
	if (lock_info->locks[i].type != AST_MUTEX)
		return;
	
	lock = lock_info->locks[i].lock_addr;
	lt = &lock->track;
	ast_reentrancy_lock(lt);
	for (j = 0; *str && j < lt->reentrancy; j++) {
		ast_str_append(str, 0, "=== --- ---> Locked Here: %s line %d (%s)\n",
					   lt->file[j], lt->lineno[j], lt->func[j]);
	}
	ast_reentrancy_unlock(lt);	
}


/*! This function can help you find highly temporal locks; locks that happen for a 
    short time, but at unexpected times, usually at times that create a deadlock,
	Why is this thing locked right then? Who is locking it? Who am I fighting
    with for this lock? 

	To answer such questions, just call this routine before you would normally try
	to aquire a lock. It doesn't do anything if the lock is not acquired. If the
	lock is taken, it will publish a line or two to the console via ast_log().

	Sometimes, the lock message is pretty uninformative. For instance, you might
	find that the lock is being aquired deep within the astobj2 code; this tells
	you little about higher level routines that call the astobj2 routines.
	But, using gdb, you can set a break at the ast_log below, and for that
	breakpoint, you can set the commands:
	  where
	  cont
	which will give a stack trace and continue. -- that aught to do the job!

*/
void log_show_lock(void *this_lock_addr)
{
	struct thr_lock_info *lock_info;
	struct ast_str *str;

	if (!(str = ast_str_create(4096))) {
		ast_log(LOG_NOTICE,"Could not create str\n");
		return;
	}
	

	pthread_mutex_lock(&lock_infos_lock.mutex);
	AST_LIST_TRAVERSE(&lock_infos, lock_info, entry) {
		int i;
		pthread_mutex_lock(&lock_info->lock);
		for (i = 0; str && i < lock_info->num_locks; i++) {
			/* ONLY show info about this particular lock, if
			   it's acquired... */
			if (lock_info->locks[i].lock_addr == this_lock_addr) {
				append_lock_information(&str, lock_info, i);
				ast_log(LOG_NOTICE, "%s", ast_str_buffer(str));
				break;
			}
		}
		pthread_mutex_unlock(&lock_info->lock);
	}
	pthread_mutex_unlock(&lock_infos_lock.mutex);
	ast_free(str);
}


static char *handle_show_locks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct thr_lock_info *lock_info;
	struct ast_str *str;

	if (!(str = ast_str_create(4096)))
		return CLI_FAILURE;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show locks";
		e->usage =
			"Usage: core show locks\n"
			"       This command is for lock debugging.  It prints out which locks\n"
			"are owned by each active thread.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;
	}

	ast_str_append(&str, 0, "\n" 
	               "=======================================================================\n"
	               "=== Currently Held Locks ==============================================\n"
	               "=======================================================================\n"
	               "===\n"
	               "=== <pending> <lock#> (<file>): <lock type> <line num> <function> <lock name> <lock addr> (times locked)\n"
	               "===\n");

	if (!str)
		return CLI_FAILURE;

	pthread_mutex_lock(&lock_infos_lock.mutex);
	AST_LIST_TRAVERSE(&lock_infos, lock_info, entry) {
		int i;
		if (lock_info->num_locks) {
			ast_str_append(&str, 0, "=== Thread ID: %ld (%s)\n", (long) lock_info->thread_id,
				lock_info->thread_name);
			pthread_mutex_lock(&lock_info->lock);
			for (i = 0; str && i < lock_info->num_locks; i++) {
				append_lock_information(&str, lock_info, i);
			}
			pthread_mutex_unlock(&lock_info->lock);
			if (!str)
				break;
			ast_str_append(&str, 0, "=== -------------------------------------------------------------------\n"
			               "===\n");
			if (!str)
				break;
		}
	}
	pthread_mutex_unlock(&lock_infos_lock.mutex);

	if (!str)
		return CLI_FAILURE;

	ast_str_append(&str, 0, "=======================================================================\n"
	               "\n");

	if (!str)
		return CLI_FAILURE;

	ast_cli(a->fd, "%s", ast_str_buffer(str));

	ast_free(str);

	return CLI_SUCCESS;
}

static struct ast_cli_entry utils_cli[] = {
	AST_CLI_DEFINE(handle_show_locks, "Show which locks are held by which thread"),
};

#endif /* DEBUG_THREADS */

/*
 * support for 'show threads'. The start routine is wrapped by
 * dummy_start(), so that ast_register_thread() and
 * ast_unregister_thread() know the thread identifier.
 */
struct thr_arg {
	void *(*start_routine)(void *);
	void *data;
	char *name;
};

/*
 * on OS/X, pthread_cleanup_push() and pthread_cleanup_pop()
 * are odd macros which start and end a block, so they _must_ be
 * used in pairs (the latter with a '1' argument to call the
 * handler on exit.
 * On BSD we don't need this, but we keep it for compatibility.
 */
static void *dummy_start(void *data)
{
	void *ret;
	struct thr_arg a = *((struct thr_arg *) data);	/* make a local copy */
#ifdef DEBUG_THREADS
	struct thr_lock_info *lock_info;
	pthread_mutexattr_t mutex_attr;
#endif

	/* note that even though data->name is a pointer to allocated memory,
	   we are not freeing it here because ast_register_thread is going to
	   keep a copy of the pointer and then ast_unregister_thread will
	   free the memory
	*/
	ast_free(data);
	ast_register_thread(a.name);
	pthread_cleanup_push(ast_unregister_thread, (void *) pthread_self());

#ifdef DEBUG_THREADS
	if (!(lock_info = ast_threadstorage_get(&thread_lock_info, sizeof(*lock_info))))
		return NULL;

	lock_info->thread_id = pthread_self();
	lock_info->thread_name = strdup(a.name);

	pthread_mutexattr_init(&mutex_attr);
	pthread_mutexattr_settype(&mutex_attr, AST_MUTEX_KIND);
	pthread_mutex_init(&lock_info->lock, &mutex_attr);
	pthread_mutexattr_destroy(&mutex_attr);

	pthread_mutex_lock(&lock_infos_lock.mutex); /* Intentionally not the wrapper */
	AST_LIST_INSERT_TAIL(&lock_infos, lock_info, entry);
	pthread_mutex_unlock(&lock_infos_lock.mutex); /* Intentionally not the wrapper */
#endif /* DEBUG_THREADS */

	ret = a.start_routine(a.data);

	pthread_cleanup_pop(1);

	return ret;
}

#endif /* !LOW_MEMORY */

int ast_pthread_create_stack(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *),
			     void *data, size_t stacksize, const char *file, const char *caller,
			     int line, const char *start_fn)
{
#if !defined(LOW_MEMORY)
	struct thr_arg *a;
#endif

	if (!attr) {
		attr = alloca(sizeof(*attr));
		pthread_attr_init(attr);
	}

#ifdef __linux__
	/* On Linux, pthread_attr_init() defaults to PTHREAD_EXPLICIT_SCHED,
	   which is kind of useless. Change this here to
	   PTHREAD_INHERIT_SCHED; that way the -p option to set realtime
	   priority will propagate down to new threads by default.
	   This does mean that callers cannot set a different priority using
	   PTHREAD_EXPLICIT_SCHED in the attr argument; instead they must set
	   the priority afterwards with pthread_setschedparam(). */
	if ((errno = pthread_attr_setinheritsched(attr, PTHREAD_INHERIT_SCHED)))
		ast_log(LOG_WARNING, "pthread_attr_setinheritsched: %s\n", strerror(errno));
#endif

	if (!stacksize)
		stacksize = AST_STACKSIZE;

	if ((errno = pthread_attr_setstacksize(attr, stacksize ? stacksize : AST_STACKSIZE)))
		ast_log(LOG_WARNING, "pthread_attr_setstacksize: %s\n", strerror(errno));

#if !defined(LOW_MEMORY)
	if ((a = ast_malloc(sizeof(*a)))) {
		a->start_routine = start_routine;
		a->data = data;
		start_routine = dummy_start;
		if (asprintf(&a->name, "%-20s started at [%5d] %s %s()",
			     start_fn, line, file, caller) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
			a->name = NULL;
		}
		data = a;
	}
#endif /* !LOW_MEMORY */

	return pthread_create(thread, attr, start_routine, data); /* We're in ast_pthread_create, so it's okay */
}


int ast_pthread_create_detached_stack(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *),
			     void *data, size_t stacksize, const char *file, const char *caller,
			     int line, const char *start_fn)
{
	unsigned char attr_destroy = 0;
	int res;

	if (!attr) {
		attr = alloca(sizeof(*attr));
		pthread_attr_init(attr);
		attr_destroy = 1;
	}

	if ((errno = pthread_attr_setdetachstate(attr, PTHREAD_CREATE_DETACHED)))
		ast_log(LOG_WARNING, "pthread_attr_setdetachstate: %s\n", strerror(errno));

	res = ast_pthread_create_stack(thread, attr, start_routine, data, 
	                               stacksize, file, caller, line, start_fn);

	if (attr_destroy)
		pthread_attr_destroy(attr);

	return res;
}

int ast_wait_for_input(int fd, int ms)
{
	struct pollfd pfd[1];
	memset(pfd, 0, sizeof(pfd));
	pfd[0].fd = fd;
	pfd[0].events = POLLIN|POLLPRI;
	return ast_poll(pfd, 1, ms);
}

static int ast_wait_for_output(int fd, int timeoutms)
{
	struct pollfd pfd = {
		.fd = fd,
		.events = POLLOUT,
	};
	int res;
	struct timeval start = ast_tvnow();
	int elapsed = 0;

	/* poll() until the fd is writable without blocking */
	while ((res = ast_poll(&pfd, 1, timeoutms - elapsed)) <= 0) {
		if (res == 0) {
			/* timed out. */
#ifndef STANDALONE
			ast_debug(1, "Timed out trying to write\n");
#endif
			return -1;
		} else if (res == -1) {
			/* poll() returned an error, check to see if it was fatal */

			if (errno == EINTR || errno == EAGAIN) {
				elapsed = ast_tvdiff_ms(ast_tvnow(), start);
				if (elapsed >= timeoutms) {
					return -1;
				}
				/* This was an acceptable error, go back into poll() */
				continue;
			}

			/* Fatal error, bail. */
			ast_log(LOG_ERROR, "poll returned error: %s\n", strerror(errno));

			return -1;
		}
		elapsed = ast_tvdiff_ms(ast_tvnow(), start);
		if (elapsed >= timeoutms) {
			return -1;
		}
	}

	return 0;
}

/*!
 * Try to write string, but wait no more than ms milliseconds before timing out.
 *
 * \note The code assumes that the file descriptor has NONBLOCK set,
 * so there is only one system call made to do a write, unless we actually
 * have a need to wait.  This way, we get better performance.
 * If the descriptor is blocking, all assumptions on the guaranteed
 * detail do not apply anymore.
 */
int ast_carefulwrite(int fd, char *s, int len, int timeoutms) 
{
	struct timeval start = ast_tvnow();
	int res = 0;
	int elapsed = 0;

	while (len) {
		if (ast_wait_for_output(fd, timeoutms - elapsed)) {
			return -1;
		}

		res = write(fd, s, len);

		if (res < 0 && errno != EAGAIN && errno != EINTR) {
			/* fatal error from write() */
			ast_log(LOG_ERROR, "write() returned error: %s\n", strerror(errno));
			return -1;
		}

		if (res < 0) {
			/* It was an acceptable error */
			res = 0;
		}

		/* Update how much data we have left to write */
		len -= res;
		s += res;
		res = 0;

		elapsed = ast_tvdiff_ms(ast_tvnow(), start);
		if (elapsed >= timeoutms) {
			/* We've taken too long to write 
			 * This is only an error condition if we haven't finished writing. */
			res = len ? -1 : 0;
			break;
		}
	}

	return res;
}

int ast_careful_fwrite(FILE *f, int fd, const char *src, size_t len, int timeoutms)
{
	struct timeval start = ast_tvnow();
	int n = 0;
	int elapsed = 0;

	while (len) {
		if (ast_wait_for_output(fd, timeoutms - elapsed)) {
			/* poll returned a fatal error, so bail out immediately. */
			return -1;
		}

		/* Clear any errors from a previous write */
		clearerr(f);

		n = fwrite(src, 1, len, f);

		if (ferror(f) && errno != EINTR && errno != EAGAIN) {
			/* fatal error from fwrite() */
			if (!feof(f)) {
				/* Don't spam the logs if it was just that the connection is closed. */
				ast_log(LOG_ERROR, "fwrite() returned error: %s\n", strerror(errno));
			}
			n = -1;
			break;
		}

		/* Update for data already written to the socket */
		len -= n;
		src += n;

		elapsed = ast_tvdiff_ms(ast_tvnow(), start);
		if (elapsed >= timeoutms) {
			/* We've taken too long to write 
			 * This is only an error condition if we haven't finished writing. */
			n = len ? -1 : 0;
			break;
		}
	}

	while (fflush(f)) {
		if (errno == EAGAIN || errno == EINTR) {
			continue;
		}
		if (!feof(f)) {
			/* Don't spam the logs if it was just that the connection is closed. */
			ast_log(LOG_ERROR, "fflush() returned error: %s\n", strerror(errno));
		}
		n = -1;
		break;
	}

	return n < 0 ? -1 : 0;
}

char *ast_strip_quoted(char *s, const char *beg_quotes, const char *end_quotes)
{
	char *e;
	char *q;

	s = ast_strip(s);
	if ((q = strchr(beg_quotes, *s)) && *q != '\0') {
		e = s + strlen(s) - 1;
		if (*e == *(end_quotes + (q - beg_quotes))) {
			s++;
			*e = '\0';
		}
	}

	return s;
}

char *ast_unescape_semicolon(char *s)
{
	char *e;
	char *work = s;

	while ((e = strchr(work, ';'))) {
		if ((e > work) && (*(e-1) == '\\')) {
			memmove(e - 1, e, strlen(e) + 1);
			work = e;
		} else {
			work = e + 1;
		}
	}

	return s;
}

/* !\brief unescape some C sequences in place, return pointer to the original string.
 */
char *ast_unescape_c(char *src)
{
	char c, *ret, *dst;

	if (src == NULL)
		return NULL;
	for (ret = dst = src; (c = *src++); *dst++ = c ) {
		if (c != '\\')
			continue;	/* copy char at the end of the loop */
		switch ((c = *src++)) {
		case '\0':	/* special, trailing '\' */
			c = '\\';
			break;
		case 'b':	/* backspace */
			c = '\b';
			break;
		case 'f':	/* form feed */
			c = '\f';
			break;
		case 'n':
			c = '\n';
			break;
		case 'r':
			c = '\r';
			break;
		case 't':
			c = '\t';
			break;
		}
		/* default, use the char literally */
	}
	*dst = '\0';
	return ret;
}

int ast_build_string_va(char **buffer, size_t *space, const char *fmt, va_list ap)
{
	int result;

	if (!buffer || !*buffer || !space || !*space)
		return -1;

	result = vsnprintf(*buffer, *space, fmt, ap);

	if (result < 0)
		return -1;
	else if (result > *space)
		result = *space;

	*buffer += result;
	*space -= result;
	return 0;
}

int ast_build_string(char **buffer, size_t *space, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = ast_build_string_va(buffer, space, fmt, ap);
	va_end(ap);

	return result;
}

int ast_true(const char *s)
{
	if (ast_strlen_zero(s))
		return 0;

	/* Determine if this is a true value */
	if (!strcasecmp(s, "yes") ||
	    !strcasecmp(s, "true") ||
	    !strcasecmp(s, "y") ||
	    !strcasecmp(s, "t") ||
	    !strcasecmp(s, "1") ||
	    !strcasecmp(s, "on"))
		return -1;

	return 0;
}

int ast_false(const char *s)
{
	if (ast_strlen_zero(s))
		return 0;

	/* Determine if this is a false value */
	if (!strcasecmp(s, "no") ||
	    !strcasecmp(s, "false") ||
	    !strcasecmp(s, "n") ||
	    !strcasecmp(s, "f") ||
	    !strcasecmp(s, "0") ||
	    !strcasecmp(s, "off"))
		return -1;

	return 0;
}

#define ONE_MILLION	1000000
/*
 * put timeval in a valid range. usec is 0..999999
 * negative values are not allowed and truncated.
 */
static struct timeval tvfix(struct timeval a)
{
	if (a.tv_usec >= ONE_MILLION) {
		ast_log(LOG_WARNING, "warning too large timestamp %ld.%ld\n",
			(long)a.tv_sec, (long int) a.tv_usec);
		a.tv_sec += a.tv_usec / ONE_MILLION;
		a.tv_usec %= ONE_MILLION;
	} else if (a.tv_usec < 0) {
		ast_log(LOG_WARNING, "warning negative timestamp %ld.%ld\n",
			(long)a.tv_sec, (long int) a.tv_usec);
		a.tv_usec = 0;
	}
	return a;
}

struct timeval ast_tvadd(struct timeval a, struct timeval b)
{
	/* consistency checks to guarantee usec in 0..999999 */
	a = tvfix(a);
	b = tvfix(b);
	a.tv_sec += b.tv_sec;
	a.tv_usec += b.tv_usec;
	if (a.tv_usec >= ONE_MILLION) {
		a.tv_sec++;
		a.tv_usec -= ONE_MILLION;
	}
	return a;
}

struct timeval ast_tvsub(struct timeval a, struct timeval b)
{
	/* consistency checks to guarantee usec in 0..999999 */
	a = tvfix(a);
	b = tvfix(b);
	a.tv_sec -= b.tv_sec;
	a.tv_usec -= b.tv_usec;
	if (a.tv_usec < 0) {
		a.tv_sec-- ;
		a.tv_usec += ONE_MILLION;
	}
	return a;
}
#undef ONE_MILLION

/*! \brief glibc puts a lock inside random(3), so that the results are thread-safe.
 * BSD libc (and others) do not. */

#ifndef linux
AST_MUTEX_DEFINE_STATIC(randomlock);
#endif

long int ast_random(void)
{
	long int res;
#ifdef HAVE_DEV_URANDOM
	if (dev_urandom_fd >= 0) {
		int read_res = read(dev_urandom_fd, &res, sizeof(res));
		if (read_res > 0) {
			long int rm = RAND_MAX;
			res = res < 0 ? ~res : res;
			rm++;
			return res % rm;
		}
	}
#endif
#ifdef linux
	res = random();
#else
	ast_mutex_lock(&randomlock);
	res = random();
	ast_mutex_unlock(&randomlock);
#endif
	return res;
}

char *ast_process_quotes_and_slashes(char *start, char find, char replace_with)
{
 	char *dataPut = start;
	int inEscape = 0;
	int inQuotes = 0;

	for (; *start; start++) {
		if (inEscape) {
			*dataPut++ = *start;       /* Always goes verbatim */
			inEscape = 0;
		} else {
			if (*start == '\\') {
				inEscape = 1;      /* Do not copy \ into the data */
			} else if (*start == '\'') {
				inQuotes = 1 - inQuotes;   /* Do not copy ' into the data */
			} else {
				/* Replace , with |, unless in quotes */
				*dataPut++ = inQuotes ? *start : ((*start == find) ? replace_with : *start);
			}
		}
	}
	if (start != dataPut)
		*dataPut = 0;
	return dataPut;
}

void ast_join(char *s, size_t len, char * const w[])
{
	int x, ofs = 0;
	const char *src;

	/* Join words into a string */
	if (!s)
		return;
	for (x = 0; ofs < len && w[x]; x++) {
		if (x > 0)
			s[ofs++] = ' ';
		for (src = w[x]; *src && ofs < len; src++)
			s[ofs++] = *src;
	}
	if (ofs == len)
		ofs--;
	s[ofs] = '\0';
}

/*
 * stringfields support routines.
 */

const char __ast_string_field_empty[] = ""; /*!< the empty string */

/*! \brief add a new block to the pool.
 * We can only allocate from the topmost pool, so the
 * fields in *mgr reflect the size of that only.
 */
static int add_string_pool(struct ast_string_field_mgr *mgr, struct ast_string_field_pool **pool_head,
			   size_t size, const char *file, int lineno, const char *func)
{
	struct ast_string_field_pool *pool;

#if defined(__AST_DEBUG_MALLOC)
	if (!(pool = __ast_calloc(1, sizeof(*pool) + size, file, lineno, func))) {
		return -1;
	}
#else
	if (!(pool = ast_calloc(1, sizeof(*pool) + size))) {
		return -1;
	}
#endif

	pool->prev = *pool_head;
	*pool_head = pool;
	mgr->size = size;
	mgr->used = 0;
	mgr->last_alloc = NULL;

	return 0;
}

/*
 * This is an internal API, code should not use it directly.
 * It initializes all fields as empty, then uses 'size' for 3 functions:
 * size > 0 means initialize the pool list with a pool of given size.
 *	This must be called right after allocating the object.
 * size = 0 means release all pools except the most recent one.
 *	This is useful to e.g. reset an object to the initial value.
 * size < 0 means release all pools.
 *	This must be done before destroying the object.
 */
int __ast_string_field_init(struct ast_string_field_mgr *mgr, struct ast_string_field_pool **pool_head,
			    int needed, const char *file, int lineno, const char *func)
{
	const char **p = (const char **) pool_head + 1;
	struct ast_string_field_pool *cur = NULL;
	struct ast_string_field_pool *preserve = NULL;

	/* clear fields - this is always necessary */
	while ((struct ast_string_field_mgr *) p != mgr)
		*p++ = __ast_string_field_empty;
	mgr->last_alloc = NULL;
#if defined(__AST_DEBUG_MALLOC)
	mgr->owner_file = file;
	mgr->owner_func = func;
	mgr->owner_line = lineno;
#endif
	if (needed > 0) {		/* allocate the initial pool */
		*pool_head = NULL;
		return add_string_pool(mgr, pool_head, needed, file, lineno, func);
	}
	if (needed < 0) {		/* reset all pools */
		if (*pool_head == NULL) {
			ast_log(LOG_WARNING, "trying to reset empty pool\n");
			return -1;
		}
		cur = *pool_head;
	} else {			/* preserve the last pool */
		if (*pool_head == NULL) {
			ast_log(LOG_WARNING, "trying to reset empty pool\n");
			return -1;
		}
		mgr->used = 0;
		preserve = *pool_head;
		cur = preserve->prev;
	}

	if (preserve) {
		preserve->prev = NULL;
	}

	while (cur) {
		struct ast_string_field_pool *prev = cur->prev;

		if (cur != preserve) {
			ast_free(cur);
		}
		cur = prev;
	}

	*pool_head = preserve;

	return 0;
}

ast_string_field __ast_string_field_alloc_space(struct ast_string_field_mgr *mgr,
						struct ast_string_field_pool **pool_head, size_t needed)
{
	char *result = NULL;
	size_t space = mgr->size - mgr->used;

	if (__builtin_expect(needed > space, 0)) {
		size_t new_size = mgr->size * 2;

		while (new_size < needed)
			new_size *= 2;

#if defined(__AST_DEBUG_MALLOC)
		if (add_string_pool(mgr, pool_head, new_size, mgr->owner_file, mgr->owner_line, mgr->owner_func))
			return NULL;
#else
		if (add_string_pool(mgr, pool_head, new_size, __FILE__, __LINE__, __FUNCTION__))
			return NULL;
#endif
	}

	result = (*pool_head)->base + mgr->used;
	mgr->used += needed;
	mgr->last_alloc = result;
	return result;
}

int __ast_string_field_ptr_grow(struct ast_string_field_mgr *mgr, size_t needed,
				const ast_string_field *ptr)
{
	int grow = needed - (strlen(*ptr) + 1);
	size_t space = mgr->size - mgr->used;

	if (grow <= 0) {
		return 0;
	}

	if (*ptr != mgr->last_alloc) {
		return 1;
	}

	if (space < grow) {
		return 1;
	}

	mgr->used += grow;

	return 0;
}

void __ast_string_field_ptr_build_va(struct ast_string_field_mgr *mgr,
				     struct ast_string_field_pool **pool_head,
				     ast_string_field *ptr, const char *format, va_list ap1, va_list ap2)
{
	size_t needed;
	size_t available;
	size_t space = mgr->size - mgr->used;
	char *target;

	/* if the field already has space allocated, try to reuse it;
	   otherwise, use the empty space at the end of the current
	   pool
	*/
	if ((*ptr)[0] != '\0') {
		target = (char *) *ptr;
		available = strlen(target) + 1;
	} else {
		target = (*pool_head)->base + mgr->used;
		available = space;
	}

	needed = vsnprintf(target, available, format, ap1) + 1;

	va_end(ap1);

	if (needed > available) {
		/* if the space needed can be satisfied by using the current
		   pool (which could only occur if we tried to use the field's
		   allocated space and failed), then use that space; otherwise
		   allocate a new pool
		*/
		if (needed > space) {
			size_t new_size = mgr->size * 2;

			while (new_size < needed)
				new_size *= 2;
			
#if defined(__AST_DEBUG_MALLOC)
			if (add_string_pool(mgr, pool_head, new_size, mgr->owner_file, mgr->owner_line, mgr->owner_func))
				return;
#else
			if (add_string_pool(mgr, pool_head, new_size, NULL, 0, NULL))
				return;
#endif
		}

		target = (*pool_head)->base + mgr->used;
		vsprintf(target, format, ap2);
	}

	if (*ptr != target) {
		mgr->last_alloc = *ptr = target;
		mgr->used += needed;
	}
}

void __ast_string_field_ptr_build(struct ast_string_field_mgr *mgr,
				  struct ast_string_field_pool **pool_head,
				  ast_string_field *ptr, const char *format, ...)
{
	va_list ap1, ap2;

	va_start(ap1, format);
	va_start(ap2, format);		/* va_copy does not exist on FreeBSD */

	__ast_string_field_ptr_build_va(mgr, pool_head, ptr, format, ap1, ap2);

	va_end(ap1);
	va_end(ap2);
}
/* end of stringfields support */

AST_MUTEX_DEFINE_STATIC(fetchadd_m); /* used for all fetc&add ops */

int ast_atomic_fetchadd_int_slow(volatile int *p, int v)
{
	int ret;
	ast_mutex_lock(&fetchadd_m);
	ret = *p;
	*p += v;
	ast_mutex_unlock(&fetchadd_m);
	return ret;
}

/*! \brief
 * get values from config variables.
 */
int ast_get_timeval(const char *src, struct timeval *dst, struct timeval _default, int *consumed)
{
	long double dtv = 0.0;
	int scanned;

	if (dst == NULL)
		return -1;

	*dst = _default;

	if (ast_strlen_zero(src))
		return -1;

	/* only integer at the moment, but one day we could accept more formats */
	if (sscanf(src, "%30Lf%n", &dtv, &scanned) > 0) {
		dst->tv_sec = dtv;
		dst->tv_usec = (dtv - dst->tv_sec) * 1000000.0;
		if (consumed)
			*consumed = scanned;
		return 0;
	} else
		return -1;
}

/*! \brief
 * get values from config variables.
 */
int ast_get_time_t(const char *src, time_t *dst, time_t _default, int *consumed)
{
	long t;
	int scanned;

	if (dst == NULL)
		return -1;

	*dst = _default;

	if (ast_strlen_zero(src))
		return -1;

	/* only integer at the moment, but one day we could accept more formats */
	if (sscanf(src, "%30ld%n", &t, &scanned) == 1) {
		*dst = t;
		if (consumed)
			*consumed = scanned;
		return 0;
	} else
		return -1;
}

void ast_enable_packet_fragmentation(int sock)
{
#if defined(HAVE_IP_MTU_DISCOVER)
	int val = IP_PMTUDISC_DONT;
	
	if (setsockopt(sock, IPPROTO_IP, IP_MTU_DISCOVER, &val, sizeof(val)))
		ast_log(LOG_WARNING, "Unable to disable PMTU discovery. Large UDP packets may fail to be delivered when sent from this socket.\n");
#endif /* HAVE_IP_MTU_DISCOVER */
}

int ast_mkdir(const char *path, int mode)
{
	char *ptr;
	int len = strlen(path), count = 0, x, piececount = 0;
	char *tmp = ast_strdupa(path);
	char **pieces;
	char *fullpath = alloca(len + 1);
	int res = 0;

	for (ptr = tmp; *ptr; ptr++) {
		if (*ptr == '/')
			count++;
	}

	/* Count the components to the directory path */
	pieces = alloca(count * sizeof(*pieces));
	for (ptr = tmp; *ptr; ptr++) {
		if (*ptr == '/') {
			*ptr = '\0';
			pieces[piececount++] = ptr + 1;
		}
	}

	*fullpath = '\0';
	for (x = 0; x < piececount; x++) {
		/* This looks funky, but the buffer is always ideally-sized, so it's fine. */
		strcat(fullpath, "/");
		strcat(fullpath, pieces[x]);
		res = mkdir(fullpath, mode);
		if (res && errno != EEXIST)
			return errno;
	}
	return 0;
}

int ast_utils_init(void)
{
#ifdef HAVE_DEV_URANDOM
	dev_urandom_fd = open("/dev/urandom", O_RDONLY);
#endif
	base64_init();
#ifdef DEBUG_THREADS
#if !defined(LOW_MEMORY)
	ast_cli_register_multiple(utils_cli, ARRAY_LEN(utils_cli));
#endif
#endif
	return 0;
}

#ifndef __AST_DEBUG_MALLOC
int _ast_asprintf(char **ret, const char *file, int lineno, const char *func, const char *fmt, ...)
{
	int res;
	va_list ap;

	va_start(ap, fmt);
	if ((res = vasprintf(ret, fmt, ap)) == -1) {
		MALLOC_FAILURE_MSG;
	}
	va_end(ap);

	return res;
}
#endif

char *ast_utils_which(const char *binary, char *fullpath, size_t fullpath_size)
{
	const char *envPATH = getenv("PATH");
	char *tpath, *path;
	struct stat unused;
	if (!envPATH) {
		return NULL;
	}
	tpath = ast_strdupa(envPATH);
	while ((path = strsep(&tpath, ":"))) {
		snprintf(fullpath, fullpath_size, "%s/%s", path, binary);
		if (!stat(fullpath, &unused)) {
			return fullpath;
		}
	}
	return NULL;
}

