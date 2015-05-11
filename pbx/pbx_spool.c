/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
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
 * \brief Full-featured outgoing call spool support
 * 
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <dirent.h>
#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#elif defined(HAVE_KQUEUE)
#include <sys/types.h>
#include <sys/time.h>
#include <sys/event.h>
#include <fcntl.h>
#endif

#include "asterisk/paths.h"	/* use ast_config_AST_SPOOL_DIR */
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/callerid.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/format.h"
#include "asterisk/format_cache.h"

/*
 * pbx_spool is similar in spirit to qcall, but with substantially enhanced functionality...
 * The spool file contains a header 
 */

enum {
	/*! Always delete the call file after a call succeeds or the
	 * maximum number of retries is exceeded, even if the
	 * modification time of the call file is in the future.
	 */
	SPOOL_FLAG_ALWAYS_DELETE = (1 << 0),
	/* Don't unlink the call file after processing, move in qdonedir */
	SPOOL_FLAG_ARCHIVE = (1 << 1),
	/* Connect the channel with the outgoing extension once early media is received */
	SPOOL_FLAG_EARLY_MEDIA = (1 << 2),
};

static char qdir[255];
static char qdonedir[255];

struct outgoing {
	int retries;                              /*!< Current number of retries */
	int maxretries;                           /*!< Maximum number of retries permitted */
	int retrytime;                            /*!< How long to wait between retries (in seconds) */
	int waittime;                             /*!< How long to wait for an answer */
	long callingpid;                          /*!< PID which is currently calling */
	struct ast_format_cap *capabilities;                 /*!< Formats (codecs) for this call */
	AST_DECLARE_STRING_FIELDS (
		AST_STRING_FIELD(fn);                 /*!< File name of call file */
		AST_STRING_FIELD(tech);               /*!< Which channel technology to use for outgoing call */
		AST_STRING_FIELD(dest);               /*!< Which device/line to use for outgoing call */
		AST_STRING_FIELD(app);                /*!< If application: Application name */
		AST_STRING_FIELD(data);               /*!< If application: Application data */
		AST_STRING_FIELD(exten);              /*!< If extension/context/priority: Extension in dialplan */
		AST_STRING_FIELD(context);            /*!< If extension/context/priority: Dialplan context */
		AST_STRING_FIELD(cid_num);            /*!< CallerID Information: Number/extension */
		AST_STRING_FIELD(cid_name);           /*!< CallerID Information: Name */
		AST_STRING_FIELD(account);            /*!< account code */
	);
	int priority;                             /*!< If extension/context/priority: Dialplan priority */
	struct ast_variable *vars;                /*!< Variables and Functions */
	int maxlen;                               /*!< Maximum length of call */
	struct ast_flags options;                 /*!< options */
};

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)
struct direntry {
	AST_LIST_ENTRY(direntry) list;
	time_t mtime;
	char name[0];
};

static AST_LIST_HEAD_STATIC(dirlist, direntry);

static void queue_file(const char *filename, time_t when);
#endif

static void free_outgoing(struct outgoing *o)
{
	if (o->vars) {
		ast_variables_destroy(o->vars);
	}
	ao2_cleanup(o->capabilities);
	ast_string_field_free_memory(o);
	ast_free(o);
}

static struct outgoing *new_outgoing(const char *fn)
{
	struct outgoing *o;

	o = ast_calloc(1, sizeof(*o));
	if (!o) {
		return NULL;
	}

	/* Initialize the new object. */
	o->priority = 1;
	o->retrytime = 300;
	o->waittime = 45;
	ast_set_flag(&o->options, SPOOL_FLAG_ALWAYS_DELETE);
	if (ast_string_field_init(o, 128)) {
		/*
		 * No need to call free_outgoing here since the failure was to
		 * allocate string fields and no variables have been allocated
		 * yet.
		 */
		ast_free(o);
		return NULL;
	}
	ast_string_field_set(o, fn, fn);
	if (ast_strlen_zero(o->fn)) {
		/* String field set failed.  Since this string is important we must fail. */
		free_outgoing(o);
		return NULL;
	}

	o->capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!o->capabilities) {
		free_outgoing(o);
		return NULL;
	}
	ast_format_cap_append(o->capabilities, ast_format_slin, 0);

	return o;
}

static int apply_outgoing(struct outgoing *o, FILE *f)
{
	char buf[256];
	char *c, *c2;
	int lineno = 0;
	struct ast_variable *var, *last = o->vars;

	while (last && last->next) {
		last = last->next;
	}

	while(fgets(buf, sizeof(buf), f)) {
		lineno++;
		/* Trim comments */
		c = buf;
		while ((c = strchr(c, '#'))) {
			if ((c == buf) || (*(c-1) == ' ') || (*(c-1) == '\t'))
				*c = '\0';
			else
				c++;
		}

		c = buf;
		while ((c = strchr(c, ';'))) {
			if ((c > buf) && (c[-1] == '\\')) {
				memmove(c - 1, c, strlen(c) + 1);
				c++;
			} else {
				*c = '\0';
				break;
			}
		}

		/* Trim trailing white space */
		ast_trim_blanks(buf);
		if (ast_strlen_zero(buf)) {
			continue;
		}
		c = strchr(buf, ':');
		if (!c) {
			ast_log(LOG_NOTICE, "Syntax error at line %d of %s\n", lineno, o->fn);
			continue;
		}
		*c = '\0';
		c = ast_skip_blanks(c + 1);
#if 0
		printf("'%s' is '%s' at line %d\n", buf, c, lineno);
#endif
		if (!strcasecmp(buf, "channel")) {
			if ((c2 = strchr(c, '/'))) {
				*c2 = '\0';
				c2++;
				ast_string_field_set(o, tech, c);
				ast_string_field_set(o, dest, c2);
			} else {
				ast_log(LOG_NOTICE, "Channel should be in form Tech/Dest at line %d of %s\n", lineno, o->fn);
			}
		} else if (!strcasecmp(buf, "callerid")) {
			char cid_name[80] = {0}, cid_num[80] = {0};
			ast_callerid_split(c, cid_name, sizeof(cid_name), cid_num, sizeof(cid_num));
			ast_string_field_set(o, cid_num, cid_num);
			ast_string_field_set(o, cid_name, cid_name);
		} else if (!strcasecmp(buf, "application")) {
			ast_string_field_set(o, app, c);
		} else if (!strcasecmp(buf, "data")) {
			ast_string_field_set(o, data, c);
		} else if (!strcasecmp(buf, "maxretries")) {
			if (sscanf(c, "%30d", &o->maxretries) != 1) {
				ast_log(LOG_WARNING, "Invalid max retries at line %d of %s\n", lineno, o->fn);
				o->maxretries = 0;
			}
		} else if (!strcasecmp(buf, "codecs")) {
			ast_format_cap_update_by_allow_disallow(o->capabilities, c, 1);
		} else if (!strcasecmp(buf, "context")) {
			ast_string_field_set(o, context, c);
		} else if (!strcasecmp(buf, "extension")) {
			ast_string_field_set(o, exten, c);
		} else if (!strcasecmp(buf, "priority")) {
			if ((sscanf(c, "%30d", &o->priority) != 1) || (o->priority < 1)) {
				ast_log(LOG_WARNING, "Invalid priority at line %d of %s\n", lineno, o->fn);
				o->priority = 1;
			}
		} else if (!strcasecmp(buf, "retrytime")) {
			if ((sscanf(c, "%30d", &o->retrytime) != 1) || (o->retrytime < 1)) {
				ast_log(LOG_WARNING, "Invalid retrytime at line %d of %s\n", lineno, o->fn);
				o->retrytime = 300;
			}
		} else if (!strcasecmp(buf, "waittime")) {
			if ((sscanf(c, "%30d", &o->waittime) != 1) || (o->waittime < 1)) {
				ast_log(LOG_WARNING, "Invalid waittime at line %d of %s\n", lineno, o->fn);
				o->waittime = 45;
			}
		} else if (!strcasecmp(buf, "retry")) {
			o->retries++;
		} else if (!strcasecmp(buf, "startretry")) {
			if (sscanf(c, "%30ld", &o->callingpid) != 1) {
				ast_log(LOG_WARNING, "Unable to retrieve calling PID!\n");
				o->callingpid = 0;
			}
		} else if (!strcasecmp(buf, "endretry") || !strcasecmp(buf, "abortretry")) {
			o->callingpid = 0;
			o->retries++;
		} else if (!strcasecmp(buf, "delayedretry")) {
		} else if (!strcasecmp(buf, "setvar") || !strcasecmp(buf, "set")) {
			c2 = c;
			strsep(&c2, "=");
			if (c2) {
				var = ast_variable_new(c, c2, o->fn);
				if (var) {
					/* Always insert at the end, because some people want to treat the spool file as a script */
					if (last) {
						last->next = var;
					} else {
						o->vars = var;
					}
					last = var;
				}
			} else
				ast_log(LOG_WARNING, "Malformed \"%s\" argument.  Should be \"%s: variable=value\"\n", buf, buf);
		} else if (!strcasecmp(buf, "account")) {
			ast_string_field_set(o, account, c);
		} else if (!strcasecmp(buf, "alwaysdelete")) {
			ast_set2_flag(&o->options, ast_true(c), SPOOL_FLAG_ALWAYS_DELETE);
		} else if (!strcasecmp(buf, "archive")) {
			ast_set2_flag(&o->options, ast_true(c), SPOOL_FLAG_ARCHIVE);
		} else if (!strcasecmp(buf, "early_media")) {
			ast_set2_flag(&o->options, ast_true(c), SPOOL_FLAG_EARLY_MEDIA);
		} else {
			ast_log(LOG_WARNING, "Unknown keyword '%s' at line %d of %s\n", buf, lineno, o->fn);
		}
	}
	if (ast_strlen_zero(o->tech) || ast_strlen_zero(o->dest) || (ast_strlen_zero(o->app) && ast_strlen_zero(o->exten))) {
		ast_log(LOG_WARNING, "At least one of app or extension must be specified, along with tech and dest in file %s\n", o->fn);
		return -1;
	}
	return 0;
}

static void safe_append(struct outgoing *o, time_t now, char *s)
{
	FILE *f;
	struct utimbuf tbuf = { .actime = now, .modtime = now + o->retrytime };

	ast_debug(1, "Outgoing %s/%s: %s\n", o->tech, o->dest, s);

	if ((f = fopen(o->fn, "a"))) {
		fprintf(f, "\n%s: %ld %d (%ld)\n", s, (long)ast_mainpid, o->retries, (long) now);
		fclose(f);
	}

	/* Update the file time */
	if (utime(o->fn, &tbuf)) {
		ast_log(LOG_WARNING, "Unable to set utime on %s: %s\n", o->fn, strerror(errno));
	}
}

/*!
 * \brief Remove a call file from the outgoing queue optionally moving it in the archive dir
 *
 * \param o the pointer to outgoing struct
 * \param status the exit status of the call. Can be "Completed", "Failed" or "Expired"
 */
static int remove_from_queue(struct outgoing *o, const char *status)
{
	FILE *f;
	char newfn[256];
	const char *bname;

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)
	struct direntry *cur;
#endif

	if (!ast_test_flag(&o->options, SPOOL_FLAG_ALWAYS_DELETE)) {
		struct stat current_file_status;

		if (!stat(o->fn, &current_file_status)) {
			if (time(NULL) < current_file_status.st_mtime) {
				return 0;
			}
		}
	}

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)
	AST_LIST_LOCK(&dirlist);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&dirlist, cur, list) {
		if (!strcmp(cur->name, o->fn)) {
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(cur);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	AST_LIST_UNLOCK(&dirlist);
#endif

	if (!ast_test_flag(&o->options, SPOOL_FLAG_ARCHIVE)) {
		unlink(o->fn);
		return 0;
	}

	if (ast_mkdir(qdonedir, 0777)) {
		ast_log(LOG_WARNING, "Unable to create queue directory %s -- outgoing spool archiving disabled\n", qdonedir);
		unlink(o->fn);
		return -1;
	}

	if (!(bname = strrchr(o->fn, '/'))) {
		bname = o->fn;
	} else {
		bname++;
	}

	snprintf(newfn, sizeof(newfn), "%s/%s", qdonedir, bname);
	/* If there is already a call file with the name in the archive dir, it will be overwritten. */
	unlink(newfn);
	if (rename(o->fn, newfn) != 0) {
		unlink(o->fn);
		return -1;
	}

	/* Only append to the file AFTER we move it out of the watched directory,
	 * otherwise the fclose() causes another event for inotify(7) */
	if ((f = fopen(newfn, "a"))) {
		fprintf(f, "Status: %s\n", status);
		fclose(f);
	}

	return 0;
}

static void *attempt_thread(void *data)
{
	struct outgoing *o = data;
	int res, reason;
	if (!ast_strlen_zero(o->app)) {
		ast_verb(3, "Attempting call on %s/%s for application %s(%s) (Retry %d)\n", o->tech, o->dest, o->app, o->data, o->retries);
		res = ast_pbx_outgoing_app(o->tech, o->capabilities, o->dest, o->waittime * 1000,
			o->app, o->data, &reason, 2 /* wait to finish */, o->cid_num, o->cid_name,
			o->vars, o->account, NULL, NULL);
	} else {
		ast_verb(3, "Attempting call on %s/%s for %s@%s:%d (Retry %d)\n", o->tech, o->dest, o->exten, o->context,o->priority, o->retries);
		res = ast_pbx_outgoing_exten(o->tech, o->capabilities, o->dest,
			o->waittime * 1000, o->context, o->exten, o->priority, &reason,
			2 /* wait to finish */, o->cid_num, o->cid_name, o->vars, o->account, NULL,
			ast_test_flag(&o->options, SPOOL_FLAG_EARLY_MEDIA), NULL);
	}
	if (res) {
		ast_log(LOG_NOTICE, "Call failed to go through, reason (%d) %s\n", reason, ast_channel_reason2str(reason));
		if (o->retries >= o->maxretries + 1) {
			/* Max retries exceeded */
			ast_log(LOG_NOTICE, "Queued call to %s/%s expired without completion after %d attempt%s\n", o->tech, o->dest, o->retries - 1, ((o->retries - 1) != 1) ? "s" : "");
			remove_from_queue(o, "Expired");
		} else {
			/* Notate that the call is still active */
			safe_append(o, time(NULL), "EndRetry");
#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)
			queue_file(o->fn, time(NULL) + o->retrytime);
#endif
		}
	} else {
		ast_log(LOG_NOTICE, "Call completed to %s/%s\n", o->tech, o->dest);
		remove_from_queue(o, "Completed");
	}
	free_outgoing(o);
	return NULL;
}

static void launch_service(struct outgoing *o)
{
	pthread_t t;
	int ret;

	if ((ret = ast_pthread_create_detached(&t, NULL, attempt_thread, o))) {
		ast_log(LOG_WARNING, "Unable to create thread :( (returned error: %d)\n", ret);
		free_outgoing(o);
	}
}

/* Called from scan_thread or queue_file */
static int scan_service(const char *fn, time_t now)
{
	struct outgoing *o;
	FILE *f;
	int res;

	o = new_outgoing(fn);
	if (!o) {
		return -1;
	}

	/* Attempt to open the file */
	f = fopen(o->fn, "r");
	if (!f) {
#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)
		/*!
		 * \todo XXX There is some odd delayed duplicate servicing of
		 * call files going on.  We need to suppress the error message
		 * if the file does not exist as a result.
		 */
		if (errno != ENOENT)
#endif
		{
			ast_log(LOG_WARNING, "Unable to open %s: '%s'(%d), deleting\n",
				o->fn, strerror(errno), (int) errno);
		}
		remove_from_queue(o, "Failed");
		free_outgoing(o);
		return -1;
	}

	/* Read in and verify the contents */
	res = apply_outgoing(o, f);
	fclose(f);
	if (res) {
		ast_log(LOG_WARNING, "Invalid file contents in %s, deleting\n", o->fn);
		remove_from_queue(o, "Failed");
		free_outgoing(o);
		return -1;
	}

	ast_debug(1, "Filename: %s, Retries: %d, max: %d\n", o->fn, o->retries, o->maxretries);
	if (o->retries <= o->maxretries) {
		now += o->retrytime;
		if (o->callingpid && (o->callingpid == ast_mainpid)) {
			safe_append(o, time(NULL), "DelayedRetry");
			ast_debug(1, "Delaying retry since we're currently running '%s'\n", o->fn);
			free_outgoing(o);
		} else {
			/* Increment retries */
			o->retries++;
			/* If someone else was calling, they're presumably gone now
			   so abort their retry and continue as we were... */
			if (o->callingpid)
				safe_append(o, time(NULL), "AbortRetry");

			safe_append(o, now, "StartRetry");
			launch_service(o);
		}
		return now;
	}

	ast_log(LOG_NOTICE, "Queued call to %s/%s expired without completion after %d attempt%s\n",
		o->tech, o->dest, o->retries - 1, ((o->retries - 1) != 1) ? "s" : "");
	remove_from_queue(o, "Expired");
	free_outgoing(o);
	return 0;
}


#if defined(HAVE_INOTIFY)
/* Only one thread is accessing this list, so no lock is necessary */
static AST_LIST_HEAD_NOLOCK_STATIC(createlist, direntry);
static AST_LIST_HEAD_NOLOCK_STATIC(openlist, direntry);
#endif

#if defined(HAVE_INOTIFY) || defined(HAVE_KQUEUE)

static void queue_file(const char *filename, time_t when)
{
	struct stat st;
	struct direntry *cur, *new;
	int res;
	time_t now = time(NULL);

	if (!strchr(filename, '/')) {
		char *fn = ast_alloca(strlen(qdir) + strlen(filename) + 2);
		sprintf(fn, "%s/%s", qdir, filename); /* SAFE */
		filename = fn;
	}

	if (when == 0) {
		if (stat(filename, &st)) {
			ast_log(LOG_WARNING, "Unable to stat %s: %s\n", filename, strerror(errno));
			return;
		}

		if (!S_ISREG(st.st_mode)) {
			return;
		}

		when = st.st_mtime;
	}

	/* Need to check the existing list in order to avoid duplicates. */
	AST_LIST_LOCK(&dirlist);
	AST_LIST_TRAVERSE(&dirlist, cur, list) {
		if (cur->mtime == when && !strcmp(filename, cur->name)) {
			AST_LIST_UNLOCK(&dirlist);
			return;
		}
	}

	if ((res = when) > now || (res = scan_service(filename, now)) > 0) {
		if (!(new = ast_calloc(1, sizeof(*new) + strlen(filename) + 1))) {
			AST_LIST_UNLOCK(&dirlist);
			return;
		}
		new->mtime = res;
		strcpy(new->name, filename);
		/* List is ordered by mtime */
		if (AST_LIST_EMPTY(&dirlist)) {
			AST_LIST_INSERT_HEAD(&dirlist, new, list);
		} else {
			int found = 0;
			AST_LIST_TRAVERSE_SAFE_BEGIN(&dirlist, cur, list) {
				if (cur->mtime > new->mtime) {
					AST_LIST_INSERT_BEFORE_CURRENT(new, list);
					found = 1;
					break;
				}
			}
			AST_LIST_TRAVERSE_SAFE_END
			if (!found) {
				AST_LIST_INSERT_TAIL(&dirlist, new, list);
			}
		}
	}
	AST_LIST_UNLOCK(&dirlist);
}

#ifdef HAVE_INOTIFY
static void queue_file_create(const char *filename)
{
	struct direntry *cur;

	AST_LIST_TRAVERSE(&createlist, cur, list) {
		if (!strcmp(cur->name, filename)) {
			return;
		}
	}

	if (!(cur = ast_calloc(1, sizeof(*cur) + strlen(filename) + 1))) {
		return;
	}
	strcpy(cur->name, filename);
	/* We'll handle this file unless an IN_OPEN event occurs within 2 seconds */
	cur->mtime = time(NULL) + 2;
	AST_LIST_INSERT_TAIL(&createlist, cur, list);
}

static void queue_file_open(const char *filename)
{
	struct direntry *cur;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&createlist, cur, list) {
		if (!strcmp(cur->name, filename)) {
			AST_LIST_REMOVE_CURRENT(list);
			AST_LIST_INSERT_TAIL(&openlist, cur, list);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
}

static void queue_created_files(void)
{
	struct direntry *cur;
	time_t now = time(NULL);

	AST_LIST_TRAVERSE_SAFE_BEGIN(&createlist, cur, list) {
		if (cur->mtime > now) {
			break;
		}

		AST_LIST_REMOVE_CURRENT(list);
		queue_file(cur->name, 0);
		ast_free(cur);
	}
	AST_LIST_TRAVERSE_SAFE_END
}

static void queue_file_write(const char *filename)
{
	struct direntry *cur;
	/* Only queue entries where an IN_CREATE preceded the IN_CLOSE_WRITE */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&openlist, cur, list) {
		if (!strcmp(cur->name, filename)) {
			AST_LIST_REMOVE_CURRENT(list);
			ast_free(cur);
			queue_file(filename, 0);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
}
#endif

static void *scan_thread(void *unused)
{
	DIR *dir;
	struct dirent *de;
	time_t now;
	struct timespec ts = { .tv_sec = 1 };
#ifdef HAVE_INOTIFY
	ssize_t res;
	int inotify_fd = inotify_init();
	struct inotify_event *iev;
	char buf[8192] __attribute__((aligned (sizeof(int))));
	struct pollfd pfd = { .fd = inotify_fd, .events = POLLIN };
#else
	struct timespec nowait = { .tv_sec = 0, .tv_nsec = 1 };
	int inotify_fd = kqueue();
	struct kevent kev;
	struct kevent event;
#endif
	struct direntry *cur;

	while (!ast_fully_booted) {
		nanosleep(&ts, NULL);
	}

	if (inotify_fd < 0) {
		ast_log(LOG_ERROR, "Unable to initialize "
#ifdef HAVE_INOTIFY
			"inotify(7)"
#else
			"kqueue(2)"
#endif
			"\n");
		return NULL;
	}

#ifdef HAVE_INOTIFY
	inotify_add_watch(inotify_fd, qdir, IN_CREATE | IN_OPEN | IN_CLOSE_WRITE | IN_MOVED_TO);
#endif

	/* First, run through the directory and clear existing entries */
	if (!(dir = opendir(qdir))) {
		ast_log(LOG_ERROR, "Unable to open directory %s: %s\n", qdir, strerror(errno));
		return NULL;
	}

#ifndef HAVE_INOTIFY
	EV_SET(&kev, dirfd(dir), EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR, NOTE_WRITE, 0, NULL);
	if (kevent(inotify_fd, &kev, 1, &event, 1, &nowait) < 0 && errno != 0) {
		ast_log(LOG_ERROR, "Unable to watch directory %s: %s\n", qdir, strerror(errno));
	}
#endif
	now = time(NULL);
	while ((de = readdir(dir))) {
		queue_file(de->d_name, 0);
	}

#ifdef HAVE_INOTIFY
	/* Directory needs to remain open for kqueue(2) */
	closedir(dir);
#endif

	/* Wait for either a) next timestamp to occur, or b) a change to happen */
	for (;/* ever */;) {
		time_t next = AST_LIST_EMPTY(&dirlist) ? INT_MAX : AST_LIST_FIRST(&dirlist)->mtime;

		time(&now);
		if (next > now) {
#ifdef HAVE_INOTIFY
			int stage = 0;
			/* Convert from seconds to milliseconds, unless there's nothing
			 * in the queue already, in which case, we wait forever. */
			int waittime = next == INT_MAX ? -1 : (next - now) * 1000;
			if (!AST_LIST_EMPTY(&createlist)) {
				waittime = 1000;
			}
			/* When a file arrives, add it to the queue, in mtime order. */
			if ((res = poll(&pfd, 1, waittime)) > 0 && (stage = 1) &&
				(res = read(inotify_fd, &buf, sizeof(buf))) >= sizeof(*iev)) {
				ssize_t len = 0;
				/* File(s) added to directory, add them to my list */
				for (iev = (void *) buf; res >= sizeof(*iev); iev = (struct inotify_event *) (((char *) iev) + len)) {
					/* For an IN_MOVED_TO event, simply process the file. However, if
					 * we get an IN_CREATE event it *might* be an open(O_CREAT) or it
					 * might be a hardlink (like smsq does, since rename() might
					 * overwrite an existing file). So we have to see if we get a
					 * subsequent IN_OPEN event on the same file. If we do, keep it
					 * on the openlist and wait for the corresponding IN_CLOSE_WRITE.
					 * If we *don't* see an IN_OPEN event, then it was a hard link so
					 * it can be processed immediately.
					 *
					 * Unfortunately, although open(O_CREAT) is an atomic file system
					 * operation, the inotify subsystem doesn't give it to us in a
					 * single event with both IN_CREATE|IN_OPEN set. It's two separate
					 * events, and the kernel doesn't even give them to us at the same
					 * time. We can read() from inotify_fd after the IN_CREATE event,
					 * and get *nothing* from it. The IN_OPEN arrives only later! So
					 * we have a very short timeout of 2 seconds. */
					if (iev->mask & IN_CREATE) {
						queue_file_create(iev->name);
					} else if (iev->mask & IN_OPEN) {
						queue_file_open(iev->name);
					} else if (iev->mask & IN_CLOSE_WRITE) {
						queue_file_write(iev->name);
					} else if (iev->mask & IN_MOVED_TO) {
						queue_file(iev->name, 0);
					} else {
						ast_log(LOG_ERROR, "Unexpected event %d for file '%s'\n", (int) iev->mask, iev->name);
					}

					len = sizeof(*iev) + iev->len;
					res -= len;
				}
			} else if (res < 0 && errno != EINTR && errno != EAGAIN) {
				ast_debug(1, "Got an error back from %s(2): %s\n", stage ? "read" : "poll", strerror(errno));
			}
			time(&now);
		}
		queue_created_files();
#else
			int num_events;
			/* If queue empty then wait forever */
			if (next == INT_MAX) {
				num_events = kevent(inotify_fd, &kev, 1, &event, 1, NULL);
			} else {
				struct timespec ts2 = { .tv_sec = (unsigned long int)(next - now), .tv_nsec = 0 };
				num_events = kevent(inotify_fd, &kev, 1, &event, 1, &ts2);
			}
			if ((num_events < 0) || (event.flags == EV_ERROR)) {
				ast_debug(10, "KEvent error %s\n", strerror(errno));
				continue;
			} else if (num_events == 0) {
				/* Interrupt or timeout, restart calculations */
				continue;
			} else {
				/* Directory changed, rescan */
				rewinddir(dir);
				while ((de = readdir(dir))) {
					queue_file(de->d_name, 0);
				}
			}
			time(&now);
		}
#endif

		/* Empty the list of all entries ready to be processed */
		AST_LIST_LOCK(&dirlist);
		while (!AST_LIST_EMPTY(&dirlist) && AST_LIST_FIRST(&dirlist)->mtime <= now) {
			cur = AST_LIST_REMOVE_HEAD(&dirlist, list);
			queue_file(cur->name, cur->mtime);
			ast_free(cur);
		}
		AST_LIST_UNLOCK(&dirlist);
	}
	return NULL;
}

#else
static void *scan_thread(void *unused)
{
	struct stat st;
	DIR *dir;
	struct dirent *de;
	char fn[256];
	int res;
	int force_poll = 1;
	time_t last = 0;
	time_t next = 0;
	time_t now;
	struct timespec ts = { .tv_sec = 1 };

	while (!ast_fully_booted) {
		nanosleep(&ts, NULL);
	}

	for (;;) {
		/* Wait a sec */
		nanosleep(&ts, NULL);
		time(&now);

		if (stat(qdir, &st)) {
			ast_log(LOG_WARNING, "Unable to stat %s\n", qdir);
			continue;
		}

		/* Make sure it is time for us to execute our check */
		if (!force_poll && st.st_mtime == last && (!next || now < next)) {
			/*
			 * The directory timestamp did not change and any delayed
			 * call-file is not ready to be executed.
			 */
			continue;
		}

#if 0
		printf("atime: %ld, mtime: %ld, ctime: %ld\n", st.st_atime, st.st_mtime, st.st_ctime);
		printf("Ooh, something changed / timeout\n");
#endif

		if (!(dir = opendir(qdir))) {
			ast_log(LOG_WARNING, "Unable to open directory %s: %s\n", qdir, strerror(errno));
			continue;
		}

		/*
		 * Since the dir timestamp is available at one second
		 * resolution, we cannot know if it was updated within the same
		 * second after we scanned it.  Therefore, we will force another
		 * scan if the dir was just modified.
		 */
		force_poll = (st.st_mtime == now);

		next = 0;
		last = st.st_mtime;
		while ((de = readdir(dir))) {
			snprintf(fn, sizeof(fn), "%s/%s", qdir, de->d_name);
			if (stat(fn, &st)) {
				ast_log(LOG_WARNING, "Unable to stat %s: %s\n", fn, strerror(errno));
				continue;
			}
			if (!S_ISREG(st.st_mode)) {
				/* Not a regular file. */
				continue;
			}
			if (st.st_mtime <= now) {
				res = scan_service(fn, now);
				if (res > 0) {
					/* The call-file is delayed or to be retried later. */
					if (!next || res < next) {
						/* This delayed call file expires earlier. */
						next = res;
					}
				} else if (res) {
					ast_log(LOG_WARNING, "Failed to scan service '%s'\n", fn);
				} else if (!next) {
					/* Expired entry: must recheck on the next go-around */
					next = st.st_mtime;
				}
			} else {
				/* The file's timestamp is in the future. */
				if (!next || st.st_mtime < next) {
					/* This call-file's timestamp expires earlier. */
					next = st.st_mtime;
				}
			}
		}
		closedir(dir);
	}
	return NULL;
}
#endif

static int unload_module(void)
{
	return -1;
}

static int load_module(void)
{
	pthread_t thread;
	int ret;
	snprintf(qdir, sizeof(qdir), "%s/%s", ast_config_AST_SPOOL_DIR, "outgoing");
	if (ast_mkdir(qdir, 0777)) {
		ast_log(LOG_WARNING, "Unable to create queue directory %s -- outgoing spool disabled\n", qdir);
		return AST_MODULE_LOAD_DECLINE;
	}
	snprintf(qdonedir, sizeof(qdir), "%s/%s", ast_config_AST_SPOOL_DIR, "outgoing_done");

	if ((ret = ast_pthread_create_detached_background(&thread, NULL, scan_thread, NULL))) {
		ast_log(LOG_WARNING, "Unable to create thread :( (returned error: %d)\n", ret);
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Outgoing Spool Support");
