/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Utility functions
 *
 * Copyright (C)  2004 - 2005, Digium, Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifdef Linux	/* For strcasestr */
#define __USE_GNU
#endif
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <asterisk/lock.h>
#include <asterisk/utils.h>
#include <asterisk/logger.h>
#include <asterisk/md5.h>

static char base64[64];
static char b2a[256];

char *ast_strip(char *buf)
{
	char *start;
	/* Strip off trailing whitespace, returns, etc */
	while (!ast_strlen_zero(buf) && (buf[strlen(buf)-1]<33))
		buf[strlen(buf)-1] = '\0';
	start = buf;
	/* Strip off leading whitespace, returns, etc */
	while (*start && (*start < 33))
		*start++ = '\0';
	return start;
}

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined( __NetBSD__ ) || defined(__APPLE__)

/* duh? ERANGE value copied from web... */
#define ERANGE 34
#undef gethostbyname

AST_MUTEX_DEFINE_STATIC(__mutex);

/* Recursive replacement for gethostbyname for BSD-based systems */
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
		int nbytes=0;
		int naddr=0, naliases=0;
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
		if(nbytes > buflen) {
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
		pbuf = buf + ((naddr+naliases+2)*sizeof(*p)); /* skip that area */
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

/* Recursive thread safe version of gethostbyname that replaces the 
   standard gethostbyname (which is not recursive)
*/
struct hostent *ast_gethostbyname(const char *host, struct ast_hostent *hp)
{
	int res;
	int herrno;
	const char *s;
	struct hostent *result = NULL;
	/* Although it is perfectly legitimate to lookup a pure integer, for
	   the sake of the sanity of people who like to name their peers as
	   integers, we break with tradition and refuse to look up a
	   pure integer */
	s = host;
	while(s && *s) {
		if (!isdigit(*s))
			break;
		s++;
	}
	if (!s || !*s)
		return NULL;
#ifdef SOLARIS
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


/* This is a regression test for recursive mutexes.
   test_for_thread_safety() will return 0 if recursive mutex locks are
   working properly, and non-zero if they are not working properly. */

AST_MUTEX_DEFINE_STATIC(test_lock);
AST_MUTEX_DEFINE_STATIC(test_lock2);
static pthread_t test_thread; 
static int lock_count = 0;
static int test_errors = 0;

static void *test_thread_body(void *data) 
{ 
	ast_mutex_lock(&test_lock);
	lock_count += 10;
	if (lock_count != 10) 
		test_errors++;
	ast_mutex_lock(&test_lock);
	lock_count += 10;
	if (lock_count != 20) 
		test_errors++;
	ast_mutex_lock(&test_lock2);
	ast_mutex_unlock(&test_lock);
	lock_count -= 10;
	if (lock_count != 10) 
		test_errors++;
	ast_mutex_unlock(&test_lock);
	lock_count -= 10;
	ast_mutex_unlock(&test_lock2);
	if (lock_count != 0) 
		test_errors++;
	return NULL;
} 

int test_for_thread_safety(void)
{ 
	ast_mutex_lock(&test_lock2);
	ast_mutex_lock(&test_lock);
	lock_count += 1;
	ast_mutex_lock(&test_lock);
	lock_count += 1;
	ast_pthread_create(&test_thread, NULL, test_thread_body, NULL); 
	usleep(100);
	if (lock_count != 2) 
		test_errors++;
	ast_mutex_unlock(&test_lock);
	lock_count -= 1;
	usleep(100); 
	if (lock_count != 1) 
		test_errors++;
	ast_mutex_unlock(&test_lock);
	lock_count -= 1;
	if (lock_count != 0) 
		test_errors++;
	ast_mutex_unlock(&test_lock2);
	usleep(100);
	if (lock_count != 0) 
		test_errors++;
	pthread_join(test_thread, NULL);
	return(test_errors);          /* return 0 on success. */
}

/*--- ast_md5_hash: Produce 16 char MD5 hash of value. ---*/
void ast_md5_hash(char *output, char *input)
{
		struct MD5Context md5;
		unsigned char digest[16];
		char *ptr;
		int x;
		MD5Init(&md5);
		MD5Update(&md5, input, strlen(input));
		MD5Final(digest, &md5);
		ptr = output;
		for (x=0;x<16;x++)
			ptr += sprintf(ptr, "%2.2x", digest[x]);
}

int ast_base64decode(unsigned char *dst, char *src, int max)
{
	int cnt = 0;
	unsigned int byte = 0;
	unsigned int bits = 0;
	int incnt = 0;
#if 0
	unsigned char *odst = dst;
#endif
	while(*src && (cnt < max)) {
		/* Shift in 6 bits of input */
		byte <<= 6;
		byte |= (b2a[(int)(*src)]) & 0x3f;
		bits += 6;
#if 0
		printf("Add: %c %s\n", *src, binary(b2a[(int)(*src)] & 0x3f, 6));
#endif
		src++;
		incnt++;
		/* If we have at least 8 bits left over, take that character 
		   off the top */
		if (bits >= 8)  {
			bits -= 8;
			*dst = (byte >> bits) & 0xff;
#if 0
			printf("Remove: %02x %s\n", *dst, binary(*dst, 8));
#endif
			dst++;
			cnt++;
		}
	}
#if 0
	dump(odst, cnt);
#endif
	/* Dont worry about left over bits, they're extra anyway */
	return cnt;
}

int ast_base64encode(char *dst, unsigned char *src, int srclen, int max)
{
	int cnt = 0;
	unsigned int byte = 0;
	int bits = 0;
	int index;
	int cntin = 0;
#if 0
	char *odst = dst;
	dump(src, srclen);
#endif
	/* Reserve one bit for end */
	max--;
	while((cntin < srclen) && (cnt < max)) {
		byte <<= 8;
#if 0
		printf("Add: %02x %s\n", *src, binary(*src, 8));
#endif
		byte |= *(src++);
		bits += 8;
		cntin++;
		while((bits >= 6) && (cnt < max)) {
			bits -= 6;
			/* We want only the top */
			index = (byte >> bits) & 0x3f;
			*dst = base64[index];
#if 0
			printf("Remove: %c %s\n", *dst, binary(index, 6));
#endif
			dst++;
			cnt++;
		}
	}
	if (bits && (cnt < max)) {
		/* Add one last character for the remaining bits, 
		   padding the rest with 0 */
		byte <<= (6 - bits);
		index = (byte) & 0x3f;
		*(dst++) = base64[index];
		cnt++;
	}
	*dst = '\0';
	return cnt;
}

static void base64_init(void)
{
	int x;
	memset(b2a, -1, sizeof(b2a));
	/* Initialize base-64 Conversion table */
	for (x=0;x<26;x++) {
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
#if 0
	for (x=0;x<64;x++) {
		if (b2a[(int)base64[x]] != x) {
			fprintf(stderr, "!!! %d failed\n", x);
		} else
			fprintf(stderr, "--- %d passed\n", x);
	}
#endif
}

/* Recursive thread safe replacement of inet_ntoa */
const char *ast_inet_ntoa(char *buf, int bufsiz, struct in_addr ia)
{
	return inet_ntop(AF_INET, &ia, buf, bufsiz);
}

int ast_utils_init(void)
{
	base64_init();
	return 0;
}


int ast_pthread_create(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *), void *data)
{
	pthread_attr_t lattr;
	if (!attr) {
		pthread_attr_init(&lattr);
		attr = &lattr;
	}
	errno = pthread_attr_setstacksize(attr, AST_STACKSIZE);
	if (errno)
		ast_log(LOG_WARNING, "pthread_attr_setstacksize returned non-zero: %s\n", strerror(errno));
	return pthread_create(thread, attr, start_routine, data); /* We're in ast_pthread_create, so it's okay */
}

/* Case-insensitive substring matching */
#ifndef LINUX
static char *upper(const char *orig, char *buf, int bufsize)
{
	int i;
	memset(buf, 0, bufsize);
	for (i=0; i<bufsize - 1; i++) {
		buf[i] = toupper(orig[i]);
		if (orig[i] == '\0') {
			break;
		}
	}
	return buf;
}

char *ast_strcasestr(const char *haystack, const char *needle)
{
	char *u1, *u2;
	int u1len = strlen(haystack) + 1, u2len = strlen(needle) + 1;

	u1 = alloca(u1len);
	u2 = alloca(u2len);
	if (u1 && u2) {
		char *offset;
		if (u2len > u1len) {
			/* Needle bigger than haystack */
			return NULL;
		}
		offset = strstr(upper(haystack, u1, u1len), upper(needle, u2, u2len));
		if (offset) {
			/* Return the offset into the original string */
			return ((char *)((unsigned int)haystack + (unsigned int)(offset - u1)));
		} else {
			return NULL;
		}
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		return NULL;
	}
}
#endif
