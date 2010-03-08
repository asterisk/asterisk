/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Most of this code is in the public domain, so clarified as of
 * June 5, 1996 by Arthur David Olson (arthur_david_olson@nih.gov).
 *
 * All modifications to this code to abstract timezones away from
 * the environment are by Tilghman Lesher, <tlesher@vcch.com>, with
 * the copyright assigned to Digium.
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
 * Multi-timezone Localtime code
 *
 * The original source from this file may be obtained from ftp://elsie.nci.nih.gov/pub/
 */

/*
** This file is in the public domain, so clarified as of
** 1996-06-05 by Arthur David Olson.
*/

/*
** Leap second handling from Bradley White.
** POSIX-style TZ environment variable handling from Guy Harris.
*/

/* #define DEBUG */

/*LINTLIBRARY*/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>
#include <fcntl.h>
#include <float.h>
#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#endif

#include "private.h"
#include "tzfile.h"

#include "asterisk/lock.h"
#include "asterisk/localtime.h"
#include "asterisk/strings.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"

#ifndef lint
#ifndef NOID
static char	__attribute__((unused)) elsieid[] = "@(#)localtime.c	8.5";
#endif /* !defined NOID */
#endif /* !defined lint */

#ifndef TZ_ABBR_MAX_LEN
#define TZ_ABBR_MAX_LEN	16
#endif /* !defined TZ_ABBR_MAX_LEN */

#ifndef TZ_ABBR_CHAR_SET
#define TZ_ABBR_CHAR_SET \
	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 :+-._"
#endif /* !defined TZ_ABBR_CHAR_SET */

#ifndef TZ_ABBR_ERR_CHAR
#define TZ_ABBR_ERR_CHAR	'_'
#endif /* !defined TZ_ABBR_ERR_CHAR */

/*
** SunOS 4.1.1 headers lack O_BINARY.
*/

#ifdef O_BINARY
#define OPEN_MODE	(O_RDONLY | O_BINARY)
#endif /* defined O_BINARY */
#ifndef O_BINARY
#define OPEN_MODE	O_RDONLY
#endif /* !defined O_BINARY */

static const char	gmt[] = "GMT";
static const struct timeval WRONG = { 0, 0 };

/*! \note
 * The DST rules to use if TZ has no rules and we can't load TZDEFRULES.
 * We default to US rules as of 1999-08-17.
 * POSIX 1003.1 section 8.1.1 says that the default DST rules are
 * implementation dependent; for historical reasons, US rules are a
 * common default.
 */
#ifndef TZDEFRULESTRING
#define TZDEFRULESTRING ",M4.1.0,M10.5.0"
#endif /* !defined TZDEFDST */

/*!< \brief time type information */
struct ttinfo {				/* time type information */
	long		tt_gmtoff;	/* UTC offset in seconds */
	int		tt_isdst;	/* used to set tm_isdst */
	int		tt_abbrind;	/* abbreviation list index */
	int		tt_ttisstd;	/* TRUE if transition is std time */
	int		tt_ttisgmt;	/* TRUE if transition is UTC */
};

/*! \brief leap second information */
struct lsinfo {				/* leap second information */
	time_t		ls_trans;	/* transition time */
	long		ls_corr;	/* correction to apply */
};

#define BIGGEST(a, b)	(((a) > (b)) ? (a) : (b))

#ifdef TZNAME_MAX
#define MY_TZNAME_MAX	TZNAME_MAX
#endif /* defined TZNAME_MAX */
#ifndef TZNAME_MAX
#define MY_TZNAME_MAX	255
#endif /* !defined TZNAME_MAX */
#ifndef TZ_STRLEN_MAX
#define TZ_STRLEN_MAX	255
#endif /* !defined TZ_STRLEN_MAX */

struct state {
	/*! Name of the file that this references */
	char    name[TZ_STRLEN_MAX + 1];
	int		leapcnt;
	int		timecnt;
	int		typecnt;
	int		charcnt;
	int		goback;
	int		goahead;
	time_t		ats[TZ_MAX_TIMES];
	unsigned char	types[TZ_MAX_TIMES];
	struct ttinfo	ttis[TZ_MAX_TYPES];
	char		chars[BIGGEST(BIGGEST(TZ_MAX_CHARS + 1, sizeof gmt),
				(2 * (MY_TZNAME_MAX + 1)))];
	struct lsinfo	lsis[TZ_MAX_LEAPS];
#ifdef HAVE_INOTIFY
	int wd[2];
#else
	time_t  mtime[2];
#endif
	AST_LIST_ENTRY(state) list;
};

struct rule {
	int		r_type;		/* type of rule--see below */
	int		r_day;		/* day number of rule */
	int		r_week;		/* week number of rule */
	int		r_mon;		/* month number of rule */
	long		r_time;		/* transition time of rule */
};

#define JULIAN_DAY		0	/* Jn - Julian day */
#define DAY_OF_YEAR		1	/* n - day of year */
#define MONTH_NTH_DAY_OF_WEEK	2	/* Mm.n.d - month, week, day of week */

/*
** Prototypes for static functions.
*/

static long		detzcode P((const char * codep));
static time_t		detzcode64 P((const char * codep));
static int		differ_by_repeat P((time_t t1, time_t t0));
static const char *	getzname P((const char * strp));
static const char *	getqzname P((const char * strp, const int delim));
static const char *	getnum P((const char * strp, int * nump, int min,
				int max));
static const char *	getsecs P((const char * strp, long * secsp));
static const char *	getoffset P((const char * strp, long * offsetp));
static const char *	getrule P((const char * strp, struct rule * rulep));
static int		gmtload P((struct state * sp));
static struct ast_tm *	gmtsub P((const struct timeval * timep, long offset,
				struct ast_tm * tmp));
static struct ast_tm *	localsub P((const struct timeval * timep, long offset,
				struct ast_tm * tmp, const struct state *sp));
static int		increment_overflow P((int * number, int delta));
static int		leaps_thru_end_of P((int y));
static int		long_increment_overflow P((long * number, int delta));
static int		long_normalize_overflow P((long * tensptr,
				int * unitsptr, const int base));
static int		normalize_overflow P((int * tensptr, int * unitsptr,
				const int base));
static struct timeval	time1 P((struct ast_tm * tmp,
				struct ast_tm * (*funcp) P((const struct timeval *,
				long, struct ast_tm *, const struct state *sp)),
				long offset, const struct state *sp));
static struct timeval	time2 P((struct ast_tm *tmp,
				struct ast_tm * (*funcp) P((const struct timeval *,
				long, struct ast_tm*, const struct state *sp)),
				long offset, int * okayp, const struct state *sp));
static struct timeval	time2sub P((struct ast_tm *tmp,
				struct ast_tm * (*funcp) (const struct timeval *,
				long, struct ast_tm*, const struct state *sp),
				long offset, int * okayp, int do_norm_secs, const struct state *sp));
static struct ast_tm *	timesub P((const struct timeval * timep, long offset,
				const struct state * sp, struct ast_tm * tmp));
static int		tmcomp P((const struct ast_tm * atmp,
				const struct ast_tm * btmp));
static time_t		transtime P((time_t janfirst, int year,
				const struct rule * rulep, long offset));
static int		tzload P((const char * name, struct state * sp,
				int doextend));
static int		tzparse P((const char * name, struct state * sp,
				int lastditch));

static AST_LIST_HEAD_STATIC(zonelist, state);

#ifndef TZ_STRLEN_MAX
#define TZ_STRLEN_MAX 255
#endif /* !defined TZ_STRLEN_MAX */

static pthread_t inotify_thread = AST_PTHREADT_NULL;
static ast_cond_t initialization;
static ast_mutex_t initialization_lock;
#ifdef HAVE_INOTIFY
static int inotify_fd = -1;

static void *inotify_daemon(void *data)
{
	struct {
		struct inotify_event iev;
		char name[FILENAME_MAX + 1];
	} buf;
	ssize_t res;
	struct state *cur;
	struct timespec ten_seconds = { 10, 0 };

	inotify_fd = inotify_init();

	ast_mutex_lock(&initialization_lock);
	ast_cond_signal(&initialization);
	ast_mutex_unlock(&initialization_lock);

	if (inotify_fd < 0) {
		ast_log(LOG_ERROR, "Cannot initialize file notification service: %s (%d)\n", strerror(errno), errno);
		inotify_thread = AST_PTHREADT_NULL;
		return NULL;
	}

	for (;/*ever*/;) {
		/* This read should block, most of the time. */
		if ((res = read(inotify_fd, &buf, sizeof(buf))) < sizeof(buf.iev) && res > 0) {
			/* This should never happen */
			ast_log(LOG_ERROR, "Inotify read less than a full event (%zd < %zd)?!!\n", res, sizeof(buf.iev));
			break;
		} else if (res < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				/* If read fails, then wait a bit, then continue */
				nanosleep(&ten_seconds, NULL);
				continue;
			}
			/* Sanity check -- this should never happen, either */
			ast_log(LOG_ERROR, "Inotify failed: %s\n", strerror(errno));
			break;
		}
		AST_LIST_LOCK(&zonelist);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&zonelist, cur, list) {
			if (cur->wd[0] == buf.iev.wd || cur->wd[1] == buf.iev.wd) {
				AST_LIST_REMOVE_CURRENT(list);
				ast_free(cur);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
		AST_LIST_UNLOCK(&zonelist);
	}
	close(inotify_fd);
	inotify_thread = AST_PTHREADT_NULL;
	return NULL;
}

static void add_notify(struct state *sp, const char *path)
{
	if (inotify_thread == AST_PTHREADT_NULL) {
		ast_cond_init(&initialization, NULL);
		ast_mutex_init(&initialization_lock);
		ast_mutex_lock(&initialization_lock);
		if (!(ast_pthread_create_background(&inotify_thread, NULL, inotify_daemon, NULL))) {
			/* Give the thread a chance to initialize */
			ast_cond_wait(&initialization, &initialization_lock);
		} else {
			ast_log(LOG_ERROR, "Unable to start notification thread\n");
			ast_mutex_unlock(&initialization_lock);
			return;
		}
		ast_mutex_unlock(&initialization_lock);
	}

	if (inotify_fd > -1) {
		char fullpath[FILENAME_MAX + 1] = "";
		if (readlink(path, fullpath, sizeof(fullpath) - 1) != -1) {
			/* If file the symlink points to changes */
			sp->wd[1] = inotify_add_watch(inotify_fd, fullpath, IN_ATTRIB | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF | IN_CLOSE_WRITE );
		} else {
			sp->wd[1] = -1;
		}
		/* or if the symlink itself changes (or the real file is here, if path is not a symlink) */
		sp->wd[0] = inotify_add_watch(inotify_fd, path, IN_ATTRIB | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF | IN_CLOSE_WRITE
#ifdef IN_DONT_FOLLOW   /* Only defined in glibc 2.5 and above */
			| IN_DONT_FOLLOW
#endif
		);
	}
}
#else
static void *notify_daemon(void *data)
{
	struct stat st, lst;
	struct state *cur;
	struct timespec sixty_seconds = { 60, 0 };

	ast_mutex_lock(&initialization_lock);
	ast_cond_signal(&initialization);
	ast_mutex_unlock(&initialization_lock);

	for (;/*ever*/;) {
		char		fullname[FILENAME_MAX + 1];

		nanosleep(&sixty_seconds, NULL);
		AST_LIST_LOCK(&zonelist);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&zonelist, cur, list) {
			char *name = cur->name;

			if (name[0] == ':')
				++name;
			if (name[0] != '/') {
				(void) strcpy(fullname, TZDIR "/");
				(void) strcat(fullname, name);
				name = fullname;
			}
			stat(name, &st);
			lstat(name, &lst);
			if (st.st_mtime > cur->mtime[0] || lst.st_mtime > cur->mtime[1]) {
				AST_LIST_REMOVE_CURRENT(list);
				ast_free(cur);
				continue;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
		AST_LIST_UNLOCK(&zonelist);
	}
	inotify_thread = AST_PTHREADT_NULL;
	return NULL;
}

static void add_notify(struct state *sp, const char *path)
{
	struct stat st;

	if (inotify_thread == AST_PTHREADT_NULL) {
		ast_cond_init(&initialization, NULL);
		ast_mutex_init(&initialization_lock);
		ast_mutex_lock(&initialization_lock);
		if (!(ast_pthread_create_background(&inotify_thread, NULL, notify_daemon, NULL))) {
			/* Give the thread a chance to initialize */
			ast_cond_wait(&initialization, &initialization_lock);
		}
		ast_mutex_unlock(&initialization_lock);
	}

	stat(path, &st);
	sp->mtime[0] = st.st_mtime;
	lstat(path, &st);
	sp->mtime[1] = st.st_mtime;
}
#endif

/*! \note
** Section 4.12.3 of X3.159-1989 requires that
**	Except for the strftime function, these functions [asctime,
**	ctime, gmtime, localtime] return values in one of two static
**	objects: a broken-down time structure and an array of char.
** Thanks to Paul Eggert for noting this.
*/

static long detzcode(const char * const codep)
{
	long	result;
	int	i;

	result = (codep[0] & 0x80) ? ~0L : 0;
	for (i = 0; i < 4; ++i)
		result = (result << 8) | (codep[i] & 0xff);
	return result;
}

static time_t detzcode64(const char * const codep)
{
	time_t	result;
	int	i;

	result = (codep[0] & 0x80) ?  (~(int_fast64_t) 0) : 0;
	for (i = 0; i < 8; ++i)
		result = result * 256 + (codep[i] & 0xff);
	return result;
}

static int differ_by_repeat(const time_t t1, const time_t t0)
{
	const long long at1 = t1, at0 = t0;
	if (TYPE_INTEGRAL(time_t) &&
		TYPE_BIT(time_t) - TYPE_SIGNED(time_t) < SECSPERREPEAT_BITS)
			return 0;
	return at1 - at0 == SECSPERREPEAT;
}

static int tzload(const char *name, struct state * const sp, const int doextend)
{
	const char *		p;
	int			i;
	int			fid;
	int			stored;
	int			nread;
	union {
		struct tzhead	tzhead;
		char		buf[2 * sizeof(struct tzhead) +
					2 * sizeof *sp +
					4 * TZ_MAX_TIMES];
	} u;

	if (name == NULL && (name = TZDEFAULT) == NULL)
		return -1;
	{
		int	doaccess;
		/*
		** Section 4.9.1 of the C standard says that
		** "FILENAME_MAX expands to an integral constant expression
		** that is the size needed for an array of char large enough
		** to hold the longest file name string that the implementation
		** guarantees can be opened."
		*/
		char		fullname[FILENAME_MAX + 1];

		if (name[0] == ':')
			++name;
		doaccess = name[0] == '/';
		if (!doaccess) {
			if ((p = TZDIR) == NULL)
				return -1;
			if ((strlen(p) + strlen(name) + 1) >= sizeof fullname)
				return -1;
			(void) strcpy(fullname, p);
			(void) strcat(fullname, "/");
			(void) strcat(fullname, name);
			/*
			** Set doaccess if '.' (as in "../") shows up in name.
			*/
			if (strchr(name, '.') != NULL)
				doaccess = TRUE;
			name = fullname;
		}
		if (doaccess && access(name, R_OK) != 0)
			return -1;
		if ((fid = open(name, OPEN_MODE)) == -1)
			return -1;
		add_notify(sp, name);
	}
	nread = read(fid, u.buf, sizeof u.buf);
	if (close(fid) < 0 || nread <= 0)
		return -1;
	for (stored = 4; stored <= 8; stored *= 2) {
		int		ttisstdcnt;
		int		ttisgmtcnt;

		ttisstdcnt = (int) detzcode(u.tzhead.tzh_ttisstdcnt);
		ttisgmtcnt = (int) detzcode(u.tzhead.tzh_ttisgmtcnt);
		sp->leapcnt = (int) detzcode(u.tzhead.tzh_leapcnt);
		sp->timecnt = (int) detzcode(u.tzhead.tzh_timecnt);
		sp->typecnt = (int) detzcode(u.tzhead.tzh_typecnt);
		sp->charcnt = (int) detzcode(u.tzhead.tzh_charcnt);
		p = u.tzhead.tzh_charcnt + sizeof u.tzhead.tzh_charcnt;
		if (sp->leapcnt < 0 || sp->leapcnt > TZ_MAX_LEAPS ||
			sp->typecnt <= 0 || sp->typecnt > TZ_MAX_TYPES ||
			sp->timecnt < 0 || sp->timecnt > TZ_MAX_TIMES ||
			sp->charcnt < 0 || sp->charcnt > TZ_MAX_CHARS ||
			(ttisstdcnt != sp->typecnt && ttisstdcnt != 0) ||
			(ttisgmtcnt != sp->typecnt && ttisgmtcnt != 0))
				return -1;
		if (nread - (p - u.buf) <
			sp->timecnt * stored +		/* ats */
			sp->timecnt +			/* types */
			sp->typecnt * 6 +		/* ttinfos */
			sp->charcnt +			/* chars */
			sp->leapcnt * (stored + 4) +	/* lsinfos */
			ttisstdcnt +			/* ttisstds */
			ttisgmtcnt)			/* ttisgmts */
				return -1;
		for (i = 0; i < sp->timecnt; ++i) {
			sp->ats[i] = (stored == 4) ?
				detzcode(p) : detzcode64(p);
			p += stored;
		}
		for (i = 0; i < sp->timecnt; ++i) {
			sp->types[i] = (unsigned char) *p++;
			if (sp->types[i] >= sp->typecnt)
				return -1;
		}
		for (i = 0; i < sp->typecnt; ++i) {
			struct ttinfo *	ttisp;

			ttisp = &sp->ttis[i];
			ttisp->tt_gmtoff = detzcode(p);
			p += 4;
			ttisp->tt_isdst = (unsigned char) *p++;
			if (ttisp->tt_isdst != 0 && ttisp->tt_isdst != 1)
				return -1;
			ttisp->tt_abbrind = (unsigned char) *p++;
			if (ttisp->tt_abbrind < 0 ||
				ttisp->tt_abbrind > sp->charcnt)
					return -1;
		}
		for (i = 0; i < sp->charcnt; ++i)
			sp->chars[i] = *p++;
		sp->chars[i] = '\0';	/* ensure '\0' at end */
		for (i = 0; i < sp->leapcnt; ++i) {
			struct lsinfo *	lsisp;

			lsisp = &sp->lsis[i];
			lsisp->ls_trans = (stored == 4) ?
				detzcode(p) : detzcode64(p);
			p += stored;
			lsisp->ls_corr = detzcode(p);
			p += 4;
		}
		for (i = 0; i < sp->typecnt; ++i) {
			struct ttinfo *	ttisp;

			ttisp = &sp->ttis[i];
			if (ttisstdcnt == 0)
				ttisp->tt_ttisstd = FALSE;
			else {
				ttisp->tt_ttisstd = *p++;
				if (ttisp->tt_ttisstd != TRUE &&
					ttisp->tt_ttisstd != FALSE)
						return -1;
			}
		}
		for (i = 0; i < sp->typecnt; ++i) {
			struct ttinfo *	ttisp;

			ttisp = &sp->ttis[i];
			if (ttisgmtcnt == 0)
				ttisp->tt_ttisgmt = FALSE;
			else {
				ttisp->tt_ttisgmt = *p++;
				if (ttisp->tt_ttisgmt != TRUE &&
					ttisp->tt_ttisgmt != FALSE)
						return -1;
			}
		}
		/*
		** Out-of-sort ats should mean we're running on a
		** signed time_t system but using a data file with
		** unsigned values (or vice versa).
		*/
		for (i = 0; i < sp->timecnt - 2; ++i)
			if (sp->ats[i] > sp->ats[i + 1]) {
				++i;
				if (TYPE_SIGNED(time_t)) {
					/*
					** Ignore the end (easy).
					*/
					sp->timecnt = i;
				} else {
					/*
					** Ignore the beginning (harder).
					*/
					int	j;

					for (j = 0; j + i < sp->timecnt; ++j) {
						sp->ats[j] = sp->ats[j + i];
						sp->types[j] = sp->types[j + i];
					}
					sp->timecnt = j;
				}
				break;
			}
		/*
		** If this is an old file, we're done.
		*/
		if (u.tzhead.tzh_version[0] == '\0')
			break;
		nread -= p - u.buf;
		for (i = 0; i < nread; ++i)
			u.buf[i] = p[i];
		/*
		** If this is a narrow integer time_t system, we're done.
		*/
		if (stored >= (int) sizeof(time_t) && TYPE_INTEGRAL(time_t))
			break;
	}
	if (doextend && nread > 2 &&
		u.buf[0] == '\n' && u.buf[nread - 1] == '\n' &&
		sp->typecnt + 2 <= TZ_MAX_TYPES) {
			struct state	ts;
			int	result;

			u.buf[nread - 1] = '\0';
			result = tzparse(&u.buf[1], &ts, FALSE);
			if (result == 0 && ts.typecnt == 2 &&
				sp->charcnt + ts.charcnt <= TZ_MAX_CHARS) {
					for (i = 0; i < 2; ++i)
						ts.ttis[i].tt_abbrind +=
							sp->charcnt;
					for (i = 0; i < ts.charcnt; ++i)
						sp->chars[sp->charcnt++] =
							ts.chars[i];
					i = 0;
					while (i < ts.timecnt &&
						ts.ats[i] <=
						sp->ats[sp->timecnt - 1])
							++i;
					while (i < ts.timecnt &&
					    sp->timecnt < TZ_MAX_TIMES) {
						sp->ats[sp->timecnt] =
							ts.ats[i];
						sp->types[sp->timecnt] =
							sp->typecnt +
							ts.types[i];
						++sp->timecnt;
						++i;
					}
					sp->ttis[sp->typecnt++] = ts.ttis[0];
					sp->ttis[sp->typecnt++] = ts.ttis[1];
			}
	}
	i = 2 * YEARSPERREPEAT;
	sp->goback = sp->goahead = sp->timecnt > i;
	sp->goback = sp->goback && sp->types[i] == sp->types[0] &&
		differ_by_repeat(sp->ats[i], sp->ats[0]);
	sp->goahead = sp->goahead &&
		sp->types[sp->timecnt - 1] == sp->types[sp->timecnt - 1 - i] &&
		differ_by_repeat(sp->ats[sp->timecnt - 1],
			 sp->ats[sp->timecnt - 1 - i]);
	return 0;
}

static const int	mon_lengths[2][MONSPERYEAR] = {
	{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

static const int	year_lengths[2] = {
	DAYSPERNYEAR, DAYSPERLYEAR
};

/*! \brief
** Given a pointer into a time zone string, scan until a character that is not
** a valid character in a zone name is found. Return a pointer to that
** character.
*/

static const char * getzname(const char *strp)
{
	char	c;

	while ((c = *strp) != '\0' && !is_digit(c) && c != ',' && c != '-' &&
		c != '+')
			++strp;
	return strp;
}

/*! \brief
** Given a pointer into an extended time zone string, scan until the ending
** delimiter of the zone name is located. Return a pointer to the delimiter.
**
** As with getzname above, the legal character set is actually quite
** restricted, with other characters producing undefined results.
** We don't do any checking here; checking is done later in common-case code.
*/

static const char * getqzname(const char *strp, const int delim)
{
	int	c;

	while ((c = *strp) != '\0' && c != delim)
		++strp;
	return strp;
}

/*! \brief
** Given a pointer into a time zone string, extract a number from that string.
** Check that the number is within a specified range; if it is not, return
** NULL.
** Otherwise, return a pointer to the first character not part of the number.
*/

static const char *getnum(const char *strp, int *nump, const int min, const int max)
{
	char	c;
	int	num;

	if (strp == NULL || !is_digit(c = *strp))
		return NULL;
	num = 0;
	do {
		num = num * 10 + (c - '0');
		if (num > max)
			return NULL;	/* illegal value */
		c = *++strp;
	} while (is_digit(c));
	if (num < min)
		return NULL;		/* illegal value */
	*nump = num;
	return strp;
}

/*! \brief
** Given a pointer into a time zone string, extract a number of seconds,
** in hh[:mm[:ss]] form, from the string.
** If any error occurs, return NULL.
** Otherwise, return a pointer to the first character not part of the number
** of seconds.
*/

static const char *getsecs(const char *strp, long * const secsp)
{
	int	num;

	/*
	** `HOURSPERDAY * DAYSPERWEEK - 1' allows quasi-Posix rules like
	** "M10.4.6/26", which does not conform to Posix,
	** but which specifies the equivalent of
	** ``02:00 on the first Sunday on or after 23 Oct''.
	*/
	strp = getnum(strp, &num, 0, HOURSPERDAY * DAYSPERWEEK - 1);
	if (strp == NULL)
		return NULL;
	*secsp = num * (long) SECSPERHOUR;
	if (*strp == ':') {
		++strp;
		strp = getnum(strp, &num, 0, MINSPERHOUR - 1);
		if (strp == NULL)
			return NULL;
		*secsp += num * SECSPERMIN;
		if (*strp == ':') {
			++strp;
			/* `SECSPERMIN' allows for leap seconds. */
			strp = getnum(strp, &num, 0, SECSPERMIN);
			if (strp == NULL)
				return NULL;
			*secsp += num;
		}
	}
	return strp;
}

/*! \brief
** Given a pointer into a time zone string, extract an offset, in
** [+-]hh[:mm[:ss]] form, from the string.
** If any error occurs, return NULL.
** Otherwise, return a pointer to the first character not part of the time.
*/

static const char *getoffset(const char *strp, long *offsetp)
{
	int	neg = 0;

	if (*strp == '-') {
		neg = 1;
		++strp;
	} else if (*strp == '+')
		++strp;
	strp = getsecs(strp, offsetp);
	if (strp == NULL)
		return NULL;		/* illegal time */
	if (neg)
		*offsetp = -*offsetp;
	return strp;
}

/*! \brief
** Given a pointer into a time zone string, extract a rule in the form
** date[/time]. See POSIX section 8 for the format of "date" and "time".
** If a valid rule is not found, return NULL.
** Otherwise, return a pointer to the first character not part of the rule.
*/

static const char *getrule(const char *strp, struct rule *rulep)
{
	if (*strp == 'J') {
		/*
		** Julian day.
		*/
		rulep->r_type = JULIAN_DAY;
		++strp;
		strp = getnum(strp, &rulep->r_day, 1, DAYSPERNYEAR);
	} else if (*strp == 'M') {
		/*
		** Month, week, day.
		*/
		rulep->r_type = MONTH_NTH_DAY_OF_WEEK;
		++strp;
		strp = getnum(strp, &rulep->r_mon, 1, MONSPERYEAR);
		if (strp == NULL)
			return NULL;
		if (*strp++ != '.')
			return NULL;
		strp = getnum(strp, &rulep->r_week, 1, 5);
		if (strp == NULL)
			return NULL;
		if (*strp++ != '.')
			return NULL;
		strp = getnum(strp, &rulep->r_day, 0, DAYSPERWEEK - 1);
	} else if (is_digit(*strp)) {
		/*
		** Day of year.
		*/
		rulep->r_type = DAY_OF_YEAR;
		strp = getnum(strp, &rulep->r_day, 0, DAYSPERLYEAR - 1);
	} else	return NULL;		/* invalid format */
	if (strp == NULL)
		return NULL;
	if (*strp == '/') {
		/*
		** Time specified.
		*/
		++strp;
		strp = getsecs(strp, &rulep->r_time);
	} else	rulep->r_time = 2 * SECSPERHOUR;	/* default = 2:00:00 */
	return strp;
}

/*! \brief
** Given the Epoch-relative time of January 1, 00:00:00 UTC, in a year, the
** year, a rule, and the offset from UTC at the time that rule takes effect,
** calculate the Epoch-relative time that rule takes effect.
*/

static time_t transtime(const time_t janfirst, const int year, const struct rule *rulep, const long offset)
{
	int	leapyear;
	time_t	value;
	int	i;
	int		d, m1, yy0, yy1, yy2, dow;

	INITIALIZE(value);
	leapyear = isleap(year);
	switch (rulep->r_type) {

	case JULIAN_DAY:
		/*
		** Jn - Julian day, 1 == January 1, 60 == March 1 even in leap
		** years.
		** In non-leap years, or if the day number is 59 or less, just
		** add SECSPERDAY times the day number-1 to the time of
		** January 1, midnight, to get the day.
		*/
		value = janfirst + (rulep->r_day - 1) * SECSPERDAY;
		if (leapyear && rulep->r_day >= 60)
			value += SECSPERDAY;
		break;

	case DAY_OF_YEAR:
		/*
		** n - day of year.
		** Just add SECSPERDAY times the day number to the time of
		** January 1, midnight, to get the day.
		*/
		value = janfirst + rulep->r_day * SECSPERDAY;
		break;

	case MONTH_NTH_DAY_OF_WEEK:
		/*
		** Mm.n.d - nth "dth day" of month m.
		*/
		value = janfirst;
		for (i = 0; i < rulep->r_mon - 1; ++i)
			value += mon_lengths[leapyear][i] * SECSPERDAY;

		/*
		** Use Zeller's Congruence to get day-of-week of first day of
		** month.
		*/
		m1 = (rulep->r_mon + 9) % 12 + 1;
		yy0 = (rulep->r_mon <= 2) ? (year - 1) : year;
		yy1 = yy0 / 100;
		yy2 = yy0 % 100;
		dow = ((26 * m1 - 2) / 10 +
			1 + yy2 + yy2 / 4 + yy1 / 4 - 2 * yy1) % 7;
		if (dow < 0)
			dow += DAYSPERWEEK;

		/*
		** "dow" is the day-of-week of the first day of the month. Get
		** the day-of-month (zero-origin) of the first "dow" day of the
		** month.
		*/
		d = rulep->r_day - dow;
		if (d < 0)
			d += DAYSPERWEEK;
		for (i = 1; i < rulep->r_week; ++i) {
			if (d + DAYSPERWEEK >=
				mon_lengths[leapyear][rulep->r_mon - 1])
					break;
			d += DAYSPERWEEK;
		}

		/*
		** "d" is the day-of-month (zero-origin) of the day we want.
		*/
		value += d * SECSPERDAY;
		break;
	}

	/*
	** "value" is the Epoch-relative time of 00:00:00 UTC on the day in
	** question. To get the Epoch-relative time of the specified local
	** time on that day, add the transition time and the current offset
	** from UTC.
	*/
	return value + rulep->r_time + offset;
}

/*! \note
** Given a POSIX section 8-style TZ string, fill in the rule tables as
** appropriate.
*/

static int tzparse(const char *name, struct state *sp, const int lastditch)
{
	const char *			stdname;
	const char *			dstname;
	size_t				stdlen;
	size_t				dstlen;
	long				stdoffset;
	long				dstoffset;
	time_t *		atp;
	unsigned char *	typep;
	char *			cp;
	int			load_result;

	INITIALIZE(dstname);
	stdname = name;
	if (lastditch) {
		stdlen = strlen(name);	/* length of standard zone name */
		name += stdlen;
		if (stdlen >= sizeof sp->chars)
			stdlen = (sizeof sp->chars) - 1;
		stdoffset = 0;
	} else {
		if (*name == '<') {
			name++;
			stdname = name;
			name = getqzname(name, '>');
			if (*name != '>')
				return -1;
			stdlen = name - stdname;
			name++;
		} else {
			name = getzname(name);
			stdlen = name - stdname;
		}
		if (*name == '\0')
			return -1;
		name = getoffset(name, &stdoffset);
		if (name == NULL)
			return -1;
	}
	load_result = tzload(TZDEFRULES, sp, FALSE);
	if (load_result != 0)
		sp->leapcnt = 0;		/* so, we're off a little */
	if (*name != '\0') {
		if (*name == '<') {
			dstname = ++name;
			name = getqzname(name, '>');
			if (*name != '>')
				return -1;
			dstlen = name - dstname;
			name++;
		} else {
			dstname = name;
			name = getzname(name);
			dstlen = name - dstname; /* length of DST zone name */
		}
		if (*name != '\0' && *name != ',' && *name != ';') {
			name = getoffset(name, &dstoffset);
			if (name == NULL)
				return -1;
		} else	dstoffset = stdoffset - SECSPERHOUR;
		if (*name == '\0' && load_result != 0)
			name = TZDEFRULESTRING;
		if (*name == ',' || *name == ';') {
			struct rule	start;
			struct rule	end;
			int	year;
			time_t	janfirst;
			time_t		starttime;
			time_t		endtime;

			++name;
			if ((name = getrule(name, &start)) == NULL)
				return -1;
			if (*name++ != ',')
				return -1;
			if ((name = getrule(name, &end)) == NULL)
				return -1;
			if (*name != '\0')
				return -1;
			sp->typecnt = 2;	/* standard time and DST */
			/*
			** Two transitions per year, from EPOCH_YEAR forward.
			*/
			sp->ttis[0].tt_gmtoff = -dstoffset;
			sp->ttis[0].tt_isdst = 1;
			sp->ttis[0].tt_abbrind = stdlen + 1;
			sp->ttis[1].tt_gmtoff = -stdoffset;
			sp->ttis[1].tt_isdst = 0;
			sp->ttis[1].tt_abbrind = 0;
			atp = sp->ats;
			typep = sp->types;
			janfirst = 0;
			sp->timecnt = 0;
			for (year = EPOCH_YEAR;
			    sp->timecnt + 2 <= TZ_MAX_TIMES;
			    ++year) {
			    	time_t	newfirst;

				starttime = transtime(janfirst, year, &start,
					stdoffset);
				endtime = transtime(janfirst, year, &end,
					dstoffset);
				if (starttime > endtime) {
					*atp++ = endtime;
					*typep++ = 1;	/* DST ends */
					*atp++ = starttime;
					*typep++ = 0;	/* DST begins */
				} else {
					*atp++ = starttime;
					*typep++ = 0;	/* DST begins */
					*atp++ = endtime;
					*typep++ = 1;	/* DST ends */
				}
				sp->timecnt += 2;
				newfirst = janfirst;
				newfirst += year_lengths[isleap(year)] *
					SECSPERDAY;
				if (newfirst <= janfirst)
					break;
				janfirst = newfirst;
			}
		} else {
			long	theirstdoffset;
			long	theirdstoffset;
			long	theiroffset;
			int	isdst;
			int	i;
			int	j;

			if (*name != '\0')
				return -1;
			/*
			** Initial values of theirstdoffset and theirdstoffset.
			*/
			theirstdoffset = 0;
			for (i = 0; i < sp->timecnt; ++i) {
				j = sp->types[i];
				if (!sp->ttis[j].tt_isdst) {
					theirstdoffset =
						-sp->ttis[j].tt_gmtoff;
					break;
				}
			}
			theirdstoffset = 0;
			for (i = 0; i < sp->timecnt; ++i) {
				j = sp->types[i];
				if (sp->ttis[j].tt_isdst) {
					theirdstoffset =
						-sp->ttis[j].tt_gmtoff;
					break;
				}
			}
			/*
			** Initially we're assumed to be in standard time.
			*/
			isdst = FALSE;
			theiroffset = theirstdoffset;
			/*
			** Now juggle transition times and types
			** tracking offsets as you do.
			*/
			for (i = 0; i < sp->timecnt; ++i) {
				j = sp->types[i];
				sp->types[i] = sp->ttis[j].tt_isdst;
				if (sp->ttis[j].tt_ttisgmt) {
					/* No adjustment to transition time */
				} else {
					/*
					** If summer time is in effect, and the
					** transition time was not specified as
					** standard time, add the summer time
					** offset to the transition time;
					** otherwise, add the standard time
					** offset to the transition time.
					*/
					/*
					** Transitions from DST to DDST
					** will effectively disappear since
					** POSIX provides for only one DST
					** offset.
					*/
					if (isdst && !sp->ttis[j].tt_ttisstd) {
						sp->ats[i] += dstoffset -
							theirdstoffset;
					} else {
						sp->ats[i] += stdoffset -
							theirstdoffset;
					}
				}
				theiroffset = -sp->ttis[j].tt_gmtoff;
				if (sp->ttis[j].tt_isdst)
					theirdstoffset = theiroffset;
				else	theirstdoffset = theiroffset;
			}
			/*
			** Finally, fill in ttis.
			** ttisstd and ttisgmt need not be handled.
			*/
			sp->ttis[0].tt_gmtoff = -stdoffset;
			sp->ttis[0].tt_isdst = FALSE;
			sp->ttis[0].tt_abbrind = 0;
			sp->ttis[1].tt_gmtoff = -dstoffset;
			sp->ttis[1].tt_isdst = TRUE;
			sp->ttis[1].tt_abbrind = stdlen + 1;
			sp->typecnt = 2;
		}
	} else {
		dstlen = 0;
		sp->typecnt = 1;		/* only standard time */
		sp->timecnt = 0;
		sp->ttis[0].tt_gmtoff = -stdoffset;
		sp->ttis[0].tt_isdst = 0;
		sp->ttis[0].tt_abbrind = 0;
	}
	sp->charcnt = stdlen + 1;
	if (dstlen != 0)
		sp->charcnt += dstlen + 1;
	if ((size_t) sp->charcnt > sizeof sp->chars)
		return -1;
	cp = sp->chars;
	(void) strncpy(cp, stdname, stdlen);
	cp += stdlen;
	*cp++ = '\0';
	if (dstlen != 0) {
		(void) strncpy(cp, dstname, dstlen);
		*(cp + dstlen) = '\0';
	}
	return 0;
}

static int gmtload(struct state *sp)
{
	if (tzload(gmt, sp, TRUE) != 0)
		return tzparse(gmt, sp, TRUE);
	else
		return -1;
}

static const struct state *ast_tzset(const char *zone)
{
	struct state *sp;

	if (ast_strlen_zero(zone))
		zone = "/etc/localtime";

	AST_LIST_LOCK(&zonelist);
	AST_LIST_TRAVERSE(&zonelist, sp, list) {
		if (!strcmp(sp->name, zone)) {
			AST_LIST_UNLOCK(&zonelist);
			return sp;
		}
	}
	AST_LIST_UNLOCK(&zonelist);

	if (!(sp = ast_calloc(1, sizeof *sp)))
		return NULL;

	if (tzload(zone, sp, TRUE) != 0) {
		if (zone[0] == ':' || tzparse(zone, sp, FALSE) != 0)
			(void) gmtload(sp);
	}
	ast_copy_string(sp->name, zone, sizeof(sp->name));
	AST_LIST_LOCK(&zonelist);
	AST_LIST_INSERT_TAIL(&zonelist, sp, list);
	AST_LIST_UNLOCK(&zonelist);
	return sp;
}

/*! \note
** The easy way to behave "as if no library function calls" localtime
** is to not call it--so we drop its guts into "localsub", which can be
** freely called. (And no, the PANS doesn't require the above behavior--
** but it *is* desirable.)
**
** The unused offset argument is for the benefit of mktime variants.
*/

static struct ast_tm *localsub(const struct timeval *timep, const long offset, struct ast_tm *tmp, const struct state *sp)
{
	const struct ttinfo *	ttisp;
	int			i;
	struct ast_tm *		result;
	struct timeval	t;
	memcpy(&t, timep, sizeof(t));

	if (sp == NULL)
		return gmtsub(timep, offset, tmp);
	if ((sp->goback && t.tv_sec < sp->ats[0]) ||
		(sp->goahead && t.tv_sec > sp->ats[sp->timecnt - 1])) {
			struct timeval	newt = t;
			time_t		seconds;
			time_t		tcycles;
			int_fast64_t	icycles;

			if (t.tv_sec < sp->ats[0])
				seconds = sp->ats[0] - t.tv_sec;
			else	seconds = t.tv_sec - sp->ats[sp->timecnt - 1];
			--seconds;
			tcycles = seconds / YEARSPERREPEAT / AVGSECSPERYEAR;
			++tcycles;
			icycles = tcycles;
			if (tcycles - icycles >= 1 || icycles - tcycles >= 1)
				return NULL;
			seconds = icycles;
			seconds *= YEARSPERREPEAT;
			seconds *= AVGSECSPERYEAR;
			if (t.tv_sec < sp->ats[0])
				newt.tv_sec += seconds;
			else	newt.tv_sec -= seconds;
			if (newt.tv_sec < sp->ats[0] ||
				newt.tv_sec > sp->ats[sp->timecnt - 1])
					return NULL;	/* "cannot happen" */
			result = localsub(&newt, offset, tmp, sp);
			if (result == tmp) {
				time_t	newy;

				newy = tmp->tm_year;
				if (t.tv_sec < sp->ats[0])
					newy -= icycles * YEARSPERREPEAT;
				else
					newy += icycles * YEARSPERREPEAT;
				tmp->tm_year = newy;
				if (tmp->tm_year != newy)
					return NULL;
			}
			return result;
	}
	if (sp->timecnt == 0 || t.tv_sec < sp->ats[0]) {
		i = 0;
		while (sp->ttis[i].tt_isdst) {
			if (++i >= sp->typecnt) {
				i = 0;
				break;
			}
		}
	} else {
		int	lo = 1;
		int	hi = sp->timecnt;

		while (lo < hi) {
			int	mid = (lo + hi) >> 1;

			if (t.tv_sec < sp->ats[mid])
				hi = mid;
			else
				lo = mid + 1;
		}
		i = (int) sp->types[lo - 1];
	}
	ttisp = &sp->ttis[i];
	/*
	** To get (wrong) behavior that's compatible with System V Release 2.0
	** you'd replace the statement below with
	**	t += ttisp->tt_gmtoff;
	**	timesub(&t, 0L, sp, tmp);
	*/
	result = timesub(&t, ttisp->tt_gmtoff, sp, tmp);
	tmp->tm_isdst = ttisp->tt_isdst;
#ifndef SOLARIS /* Solaris doesn't have this element */
	tmp->tm_gmtoff = ttisp->tt_gmtoff;
#endif
#ifdef TM_ZONE
	tmp->TM_ZONE = &sp->chars[ttisp->tt_abbrind];
#endif /* defined TM_ZONE */
	tmp->tm_usec = timep->tv_usec;
	return result;
}

struct ast_tm *ast_localtime(const struct timeval *timep, struct ast_tm *tmp, const char *zone)
{
	const struct state *sp = ast_tzset(zone);
	memset(tmp, 0, sizeof(*tmp));
	return sp ? localsub(timep, 0L, tmp, sp) : NULL;
}

/*
** This function provides informaton about daylight savings time 
** for the given timezone.  This includes whether it can determine 
** if daylight savings is used for this timezone, the UTC times for 
** when daylight savings transitions, and the offset in seconds from 
** UTC. 
*/

void ast_get_dst_info(const time_t * const timep, int *dst_enabled, time_t *dst_start, time_t *dst_end, int *gmt_off, const char * const zone)
{
	int i;	
	int transition1 = -1;
	int transition2 = -1;
	time_t		seconds;
	int  bounds_exceeded = 0;
	time_t  t = *timep;
	const struct state *sp;
	
	if (NULL == dst_enabled)
		return;
	*dst_enabled = 0;

	if (NULL == dst_start || NULL == dst_end || NULL == gmt_off)
		return;

	*gmt_off = 0; 
	
	sp = ast_tzset(zone);
	if (NULL == sp) 
		return;
	
	/* If the desired time exceeds the bounds of the defined time transitions  
	* then give give up on determining DST info and simply look for gmt offset 
	* This requires that I adjust the given time using increments of Gregorian 
	* repeats to place the time within the defined time transitions in the 
	* timezone structure.  
	*/
	if ((sp->goback && t < sp->ats[0]) ||
			(sp->goahead && t > sp->ats[sp->timecnt - 1])) {
		time_t		tcycles;
		int_fast64_t	icycles;

		if (t < sp->ats[0])
			seconds = sp->ats[0] - t;
		else	seconds = t - sp->ats[sp->timecnt - 1];
		--seconds;
		tcycles = seconds / YEARSPERREPEAT / AVGSECSPERYEAR;
		++tcycles;
		icycles = tcycles;
		if (tcycles - icycles >= 1 || icycles - tcycles >= 1)
			return;
		seconds = icycles;
		seconds *= YEARSPERREPEAT;
		seconds *= AVGSECSPERYEAR;
		if (t < sp->ats[0])
			t += seconds;
		else
			t -= seconds;
		
		if (t < sp->ats[0] || t > sp->ats[sp->timecnt - 1])
			return;	/* "cannot happen" */

		bounds_exceeded = 1;
	}

	if (sp->timecnt == 0 || t < sp->ats[0]) {
		/* I have no transition times or I'm before time */
		*dst_enabled = 0;
		/* Find where I can get gmtoff */
		i = 0;
		while (sp->ttis[i].tt_isdst)
			if (++i >= sp->typecnt) {
			i = 0;
			break;
			}
			*gmt_off = sp->ttis[i].tt_gmtoff;
			return;
	} 

	for (i = 1; i < sp->timecnt; ++i) {
		if (t < sp->ats[i]) {
			transition1 = sp->types[i - 1];
			transition2 = sp->types[i];
			break;
		} 
	}
	/* if I found transition times that do not bounded the given time and these correspond to 
		or the bounding zones do not reflect a changes in day light savings, then I do not have dst active */
	if (i >= sp->timecnt || 0 > transition1 || 0 > transition2 ||
			(sp->ttis[transition1].tt_isdst == sp->ttis[transition2].tt_isdst)) {
		*dst_enabled = 0;
		*gmt_off 	 = sp->ttis[sp->types[sp->timecnt -1]].tt_gmtoff;
	} else {
		/* I have valid daylight savings information. */
		if(sp->ttis[transition2].tt_isdst) 
			*gmt_off = sp->ttis[transition1].tt_gmtoff;
		else 
			*gmt_off = sp->ttis[transition2].tt_gmtoff;

		/* If I adjusted the time earlier, indicate that the dst is invalid */
		if (!bounds_exceeded) {
			*dst_enabled = 1;
			/* Determine which of the bounds is the start of daylight savings and which is the end */
			if(sp->ttis[transition2].tt_isdst) {
				*dst_start = sp->ats[i];
				*dst_end = sp->ats[i -1];
			} else {
				*dst_start = sp->ats[i -1];
				*dst_end = sp->ats[i];
			}
		}
	}	
	return;
}

/*
** gmtsub is to gmtime as localsub is to localtime.
*/

static struct ast_tm *gmtsub(const struct timeval *timep, const long offset, struct ast_tm *tmp)
{
	struct ast_tm *	result;
	struct state *sp;

	AST_LIST_LOCK(&zonelist);
	AST_LIST_TRAVERSE(&zonelist, sp, list) {
		if (!strcmp(sp->name, "UTC"))
			break;
	}

	if (!sp) {
		if (!(sp = (struct state *) ast_calloc(1, sizeof *sp)))
			return NULL;
		gmtload(sp);
		AST_LIST_INSERT_TAIL(&zonelist, sp, list);
	}
	AST_LIST_UNLOCK(&zonelist);

	result = timesub(timep, offset, sp, tmp);
#ifdef TM_ZONE
	/*
	** Could get fancy here and deliver something such as
	** "UTC+xxxx" or "UTC-xxxx" if offset is non-zero,
	** but this is no time for a treasure hunt.
	*/
	if (offset != 0)
		tmp->TM_ZONE = "    ";
	else
		tmp->TM_ZONE = sp->chars;
#endif /* defined TM_ZONE */
	return result;
}

/*! \brief
** Return the number of leap years through the end of the given year
** where, to make the math easy, the answer for year zero is defined as zero.
*/

static int leaps_thru_end_of(const int y)
{
	return (y >= 0) ? (y / 4 - y / 100 + y / 400) :
		-(leaps_thru_end_of(-(y + 1)) + 1);
}

static struct ast_tm *timesub(const struct timeval *timep, const long offset, const struct state *sp, struct ast_tm *tmp)
{
	const struct lsinfo *	lp;
	time_t			tdays;
	int			idays;	/* unsigned would be so 2003 */
	long			rem;
	int				y;
	const int *		ip;
	long			corr;
	int			hit;
	int			i;
	long	seconds;


	corr = 0;
	hit = 0;
	i = (sp == NULL) ? 0 : sp->leapcnt;
	while (--i >= 0) {
		lp = &sp->lsis[i];
		if (timep->tv_sec >= lp->ls_trans) {
			if (timep->tv_sec == lp->ls_trans) {
				hit = ((i == 0 && lp->ls_corr > 0) ||
					lp->ls_corr > sp->lsis[i - 1].ls_corr);
				if (hit)
					while (i > 0 &&
						sp->lsis[i].ls_trans ==
						sp->lsis[i - 1].ls_trans + 1 &&
						sp->lsis[i].ls_corr ==
						sp->lsis[i - 1].ls_corr + 1) {
							++hit;
							--i;
					}
			}
			corr = lp->ls_corr;
			break;
		}
	}
	y = EPOCH_YEAR;
	tdays = timep->tv_sec / SECSPERDAY;
	rem = timep->tv_sec - tdays * SECSPERDAY;
	while (tdays < 0 || tdays >= year_lengths[isleap(y)]) {
		int		newy;
		time_t	tdelta;
		int	idelta;
		int	leapdays;

		tdelta = tdays / DAYSPERLYEAR;
		idelta = tdelta;
		if (tdelta - idelta >= 1 || idelta - tdelta >= 1)
			return NULL;
		if (idelta == 0)
			idelta = (tdays < 0) ? -1 : 1;
		newy = y;
		if (increment_overflow(&newy, idelta))
			return NULL;
		leapdays = leaps_thru_end_of(newy - 1) -
			leaps_thru_end_of(y - 1);
		tdays -= ((time_t) newy - y) * DAYSPERNYEAR;
		tdays -= leapdays;
		y = newy;
	}

	seconds = tdays * SECSPERDAY + 0.5;
	tdays = seconds / SECSPERDAY;
	rem += seconds - tdays * SECSPERDAY;

	/*
	** Given the range, we can now fearlessly cast...
	*/
	idays = tdays;
	rem += offset - corr;
	while (rem < 0) {
		rem += SECSPERDAY;
		--idays;
	}
	while (rem >= SECSPERDAY) {
		rem -= SECSPERDAY;
		++idays;
	}
	while (idays < 0) {
		if (increment_overflow(&y, -1))
			return NULL;
		idays += year_lengths[isleap(y)];
	}
	while (idays >= year_lengths[isleap(y)]) {
		idays -= year_lengths[isleap(y)];
		if (increment_overflow(&y, 1))
			return NULL;
	}
	tmp->tm_year = y;
	if (increment_overflow(&tmp->tm_year, -TM_YEAR_BASE))
		return NULL;
	tmp->tm_yday = idays;
	/*
	** The "extra" mods below avoid overflow problems.
	*/
	tmp->tm_wday = EPOCH_WDAY +
		((y - EPOCH_YEAR) % DAYSPERWEEK) *
		(DAYSPERNYEAR % DAYSPERWEEK) +
		leaps_thru_end_of(y - 1) -
		leaps_thru_end_of(EPOCH_YEAR - 1) +
		idays;
	tmp->tm_wday %= DAYSPERWEEK;
	if (tmp->tm_wday < 0)
		tmp->tm_wday += DAYSPERWEEK;
	tmp->tm_hour = (int) (rem / SECSPERHOUR);
	rem %= SECSPERHOUR;
	tmp->tm_min = (int) (rem / SECSPERMIN);
	/*
	** A positive leap second requires a special
	** representation. This uses "... ??:59:60" et seq.
	*/
	tmp->tm_sec = (int) (rem % SECSPERMIN) + hit;
	ip = mon_lengths[isleap(y)];
	for (tmp->tm_mon = 0; idays >= ip[tmp->tm_mon]; ++(tmp->tm_mon))
		idays -= ip[tmp->tm_mon];
	tmp->tm_mday = (int) (idays + 1);
	tmp->tm_isdst = 0;
#ifdef TM_GMTOFF
	tmp->TM_GMTOFF = offset;
#endif /* defined TM_GMTOFF */
	tmp->tm_usec = timep->tv_usec;
	return tmp;
}

/*! \note
** Adapted from code provided by Robert Elz, who writes:
**	The "best" way to do mktime I think is based on an idea of Bob
**	Kridle's (so its said...) from a long time ago.
**	It does a binary search of the time_t space. Since time_t's are
**	just 32 bits, its a max of 32 iterations (even at 64 bits it
**	would still be very reasonable).
*/

/*! \brief
** Simplified normalize logic courtesy Paul Eggert.
*/

static int increment_overflow(int *number, int delta)
{
	int	number0;

	number0 = *number;
	*number += delta;
	return (*number < number0) != (delta < 0);
}

static int long_increment_overflow(long *number, int delta)
{
	long	number0;

	number0 = *number;
	*number += delta;
	return (*number < number0) != (delta < 0);
}

static int normalize_overflow(int *tensptr, int *unitsptr, const int base)
{
	int	tensdelta;

	tensdelta = (*unitsptr >= 0) ?
		(*unitsptr / base) :
		(-1 - (-1 - *unitsptr) / base);
	*unitsptr -= tensdelta * base;
	return increment_overflow(tensptr, tensdelta);
}

static int long_normalize_overflow(long *tensptr, int *unitsptr, const int base)
{
	int	tensdelta;

	tensdelta = (*unitsptr >= 0) ?
		(*unitsptr / base) :
		(-1 - (-1 - *unitsptr) / base);
	*unitsptr -= tensdelta * base;
	return long_increment_overflow(tensptr, tensdelta);
}

static int tmcomp(const struct ast_tm *atmp, const struct ast_tm *btmp)
{
	int	result;

	if ((result = (atmp->tm_year - btmp->tm_year)) == 0 &&
		(result = (atmp->tm_mon - btmp->tm_mon)) == 0 &&
		(result = (atmp->tm_mday - btmp->tm_mday)) == 0 &&
		(result = (atmp->tm_hour - btmp->tm_hour)) == 0 &&
		(result = (atmp->tm_min - btmp->tm_min)) == 0 &&
		(result = (atmp->tm_sec - btmp->tm_sec)) == 0)
			result = atmp->tm_usec - btmp->tm_usec;
	return result;
}

static struct timeval time2sub(struct ast_tm *tmp, struct ast_tm * (* const funcp) (const struct timeval *, long, struct ast_tm *, const struct state *), const long offset, int *okayp, const int do_norm_secs, const struct state *sp)
{
	int			dir;
	int			i, j;
	int			saved_seconds;
	long			li;
	time_t			lo;
	time_t			hi;
	long				y;
	struct timeval			newt = { 0, 0 };
	struct timeval			t = { 0, 0 };
	struct ast_tm			yourtm, mytm;

	*okayp = FALSE;
	yourtm = *tmp;
	if (do_norm_secs) {
		if (normalize_overflow(&yourtm.tm_min, &yourtm.tm_sec,
			SECSPERMIN))
				return WRONG;
	}
	if (normalize_overflow(&yourtm.tm_hour, &yourtm.tm_min, MINSPERHOUR))
		return WRONG;
	if (normalize_overflow(&yourtm.tm_mday, &yourtm.tm_hour, HOURSPERDAY))
		return WRONG;
	y = yourtm.tm_year;
	if (long_normalize_overflow(&y, &yourtm.tm_mon, MONSPERYEAR))
		return WRONG;
	/*
	** Turn y into an actual year number for now.
	** It is converted back to an offset from TM_YEAR_BASE later.
	*/
	if (long_increment_overflow(&y, TM_YEAR_BASE))
		return WRONG;
	while (yourtm.tm_mday <= 0) {
		if (long_increment_overflow(&y, -1))
			return WRONG;
		li = y + (1 < yourtm.tm_mon);
		yourtm.tm_mday += year_lengths[isleap(li)];
	}
	while (yourtm.tm_mday > DAYSPERLYEAR) {
		li = y + (1 < yourtm.tm_mon);
		yourtm.tm_mday -= year_lengths[isleap(li)];
		if (long_increment_overflow(&y, 1))
			return WRONG;
	}
	for ( ; ; ) {
		i = mon_lengths[isleap(y)][yourtm.tm_mon];
		if (yourtm.tm_mday <= i)
			break;
		yourtm.tm_mday -= i;
		if (++yourtm.tm_mon >= MONSPERYEAR) {
			yourtm.tm_mon = 0;
			if (long_increment_overflow(&y, 1))
				return WRONG;
		}
	}
	if (long_increment_overflow(&y, -TM_YEAR_BASE))
		return WRONG;
	yourtm.tm_year = y;
	if (yourtm.tm_year != y)
		return WRONG;
	if (yourtm.tm_sec >= 0 && yourtm.tm_sec < SECSPERMIN)
		saved_seconds = 0;
	else if (y + TM_YEAR_BASE < EPOCH_YEAR) {
		/*
		** We can't set tm_sec to 0, because that might push the
		** time below the minimum representable time.
		** Set tm_sec to 59 instead.
		** This assumes that the minimum representable time is
		** not in the same minute that a leap second was deleted from,
		** which is a safer assumption than using 58 would be.
		*/
		if (increment_overflow(&yourtm.tm_sec, 1 - SECSPERMIN))
			return WRONG;
		saved_seconds = yourtm.tm_sec;
		yourtm.tm_sec = SECSPERMIN - 1;
	} else {
		saved_seconds = yourtm.tm_sec;
		yourtm.tm_sec = 0;
	}
	/*
	** Do a binary search (this works whatever time_t's type is).
	*/
	if (!TYPE_SIGNED(time_t)) {
		lo = 0;
		hi = lo - 1;
	} else if (!TYPE_INTEGRAL(time_t)) {
		if (sizeof(time_t) > sizeof(float))
			hi = (time_t) DBL_MAX;
		else	hi = (time_t) FLT_MAX;
		lo = -hi;
	} else {
		lo = 1;
		for (i = 0; i < (int) TYPE_BIT(time_t) - 1; ++i)
			lo *= 2;
		hi = -(lo + 1);
	}
	for ( ; ; ) {
		t.tv_sec = lo / 2 + hi / 2;
		if (t.tv_sec < lo)
			t.tv_sec = lo;
		else if (t.tv_sec > hi)
			t.tv_sec = hi;
		if ((*funcp)(&t, offset, &mytm, sp) == NULL) {
			/*
			** Assume that t is too extreme to be represented in
			** a struct ast_tm; arrange things so that it is less
			** extreme on the next pass.
			*/
			dir = (t.tv_sec > 0) ? 1 : -1;
		} else	dir = tmcomp(&mytm, &yourtm);
		if (dir != 0) {
			if (t.tv_sec == lo) {
				++t.tv_sec;
				if (t.tv_sec <= lo)
					return WRONG;
				++lo;
			} else if (t.tv_sec == hi) {
				--t.tv_sec;
				if (t.tv_sec >= hi)
					return WRONG;
				--hi;
			}
			if (lo > hi)
				return WRONG;
			if (dir > 0)
				hi = t.tv_sec;
			else	lo = t.tv_sec;
			continue;
		}
		if (yourtm.tm_isdst < 0 || mytm.tm_isdst == yourtm.tm_isdst)
			break;
		/*
		** Right time, wrong type.
		** Hunt for right time, right type.
		** It's okay to guess wrong since the guess
		** gets checked.
		*/
		/*
		** The (void *) casts are the benefit of SunOS 3.3 on Sun 2's.
		*/
		for (i = sp->typecnt - 1; i >= 0; --i) {
			if (sp->ttis[i].tt_isdst != yourtm.tm_isdst)
				continue;
			for (j = sp->typecnt - 1; j >= 0; --j) {
				if (sp->ttis[j].tt_isdst == yourtm.tm_isdst)
					continue;
				newt.tv_sec = t.tv_sec + sp->ttis[j].tt_gmtoff -
					sp->ttis[i].tt_gmtoff;
				if ((*funcp)(&newt, offset, &mytm, sp) == NULL)
					continue;
				if (tmcomp(&mytm, &yourtm) != 0)
					continue;
				if (mytm.tm_isdst != yourtm.tm_isdst)
					continue;
				/*
				** We have a match.
				*/
				t = newt;
				goto label;
			}
		}
		return WRONG;
	}
label:
	newt.tv_sec = t.tv_sec + saved_seconds;
	if ((newt.tv_sec < t.tv_sec) != (saved_seconds < 0))
		return WRONG;
	t.tv_sec = newt.tv_sec;
	if ((*funcp)(&t, offset, tmp, sp))
		*okayp = TRUE;
	return t;
}

static struct timeval time2(struct ast_tm *tmp, struct ast_tm * (* const funcp) (const struct timeval *, long, struct ast_tm*, const struct state *sp), const long offset, int *okayp, const struct state *sp)
{
	struct timeval	t;

	/*! \note
	** First try without normalization of seconds
	** (in case tm_sec contains a value associated with a leap second).
	** If that fails, try with normalization of seconds.
	*/
	t = time2sub(tmp, funcp, offset, okayp, FALSE, sp);
	return *okayp ? t : time2sub(tmp, funcp, offset, okayp, TRUE, sp);
}

static struct timeval time1(struct ast_tm *tmp, struct ast_tm * (* const funcp) (const struct timeval *, long, struct ast_tm *, const struct state *), const long offset, const struct state *sp)
{
	struct timeval			t;
	int			samei, otheri;
	int			sameind, otherind;
	int			i;
	int			nseen;
	int				seen[TZ_MAX_TYPES];
	int				types[TZ_MAX_TYPES];
	int				okay;

	if (tmp->tm_isdst > 1)
		tmp->tm_isdst = 1;
	t = time2(tmp, funcp, offset, &okay, sp);
#ifdef PCTS
	/*
	** PCTS code courtesy Grant Sullivan.
	*/
	if (okay)
		return t;
	if (tmp->tm_isdst < 0)
		tmp->tm_isdst = 0;	/* reset to std and try again */
#endif /* defined PCTS */
#ifndef PCTS
	if (okay || tmp->tm_isdst < 0)
		return t;
#endif /* !defined PCTS */
	/*
	** We're supposed to assume that somebody took a time of one type
	** and did some math on it that yielded a "struct ast_tm" that's bad.
	** We try to divine the type they started from and adjust to the
	** type they need.
	*/
	if (sp == NULL)
		return WRONG;
	for (i = 0; i < sp->typecnt; ++i)
		seen[i] = FALSE;
	nseen = 0;
	for (i = sp->timecnt - 1; i >= 0; --i)
		if (!seen[sp->types[i]]) {
			seen[sp->types[i]] = TRUE;
			types[nseen++] = sp->types[i];
		}
	for (sameind = 0; sameind < nseen; ++sameind) {
		samei = types[sameind];
		if (sp->ttis[samei].tt_isdst != tmp->tm_isdst)
			continue;
		for (otherind = 0; otherind < nseen; ++otherind) {
			otheri = types[otherind];
			if (sp->ttis[otheri].tt_isdst == tmp->tm_isdst)
				continue;
			tmp->tm_sec += sp->ttis[otheri].tt_gmtoff -
					sp->ttis[samei].tt_gmtoff;
			tmp->tm_isdst = !tmp->tm_isdst;
			t = time2(tmp, funcp, offset, &okay, sp);
			if (okay)
				return t;
			tmp->tm_sec -= sp->ttis[otheri].tt_gmtoff -
					sp->ttis[samei].tt_gmtoff;
			tmp->tm_isdst = !tmp->tm_isdst;
		}
	}
	return WRONG;
}

struct timeval ast_mktime(struct ast_tm *tmp, const char *zone)
{
	const struct state *sp;
	if (!(sp = ast_tzset(zone)))
		return WRONG;
	return time1(tmp, localsub, 0L, sp);
}

int ast_strftime(char *buf, size_t len, const char *tmp, const struct ast_tm *tm)
{
	size_t fmtlen = strlen(tmp) + 1;
	char *format = ast_calloc(1, fmtlen), *fptr = format, *newfmt;
	int decimals = -1, i, res;
	long fraction;

	if (!format)
		return -1;
	for (; *tmp; tmp++) {
		if (*tmp == '%') {
			switch (tmp[1]) {
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
				if (tmp[2] != 'q')
					goto defcase;
				decimals = tmp[1] - '0';
				tmp++;
				/* Fall through */
			case 'q': /* Milliseconds */
				if (decimals == -1)
					decimals = 3;

				/* Juggle some memory to fit the item */
				newfmt = ast_realloc(format, fmtlen + decimals);
				if (!newfmt) {
					ast_free(format);
					return -1;
				}
				fptr = fptr - format + newfmt;
				format = newfmt;
				fmtlen += decimals;

				/* Reduce the fraction of time to the accuracy needed */
				for (i = 6, fraction = tm->tm_usec; i > decimals; i--)
					fraction /= 10;
				fptr += sprintf(fptr, "%0*ld", decimals, fraction);

				/* Reset, in case more than one 'q' specifier exists */
				decimals = -1;
				tmp++;
				break;
			default:
				goto defcase;
			}
		} else
defcase:	*fptr++ = *tmp;
	}
	*fptr = '\0';
#undef strftime
	res = (int)strftime(buf, len, format, (struct tm *)tm);
	ast_free(format);
	return res;
}

char *ast_strptime(const char *s, const char *format, struct ast_tm *tm)
{
	struct tm tm2 = { 0, };
	char *res = strptime(s, format, &tm2);
	memcpy(tm, &tm2, sizeof(*tm));
	tm->tm_usec = 0;
	/* strptime(3) doesn't set .tm_isdst correctly, so to force ast_mktime(3)
	 * to deal with it correctly, we set it to -1. */
	tm->tm_isdst = -1;
	return res;
}

