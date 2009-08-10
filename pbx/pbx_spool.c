/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>
#include <time.h>
#include <utime.h>
#include <dirent.h>

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
	SPOOL_FLAG_ARCHIVE = (1 << 1)
};

static char qdir[255];
static char qdonedir[255];

struct outgoing {
	int retries;                              /*!< Current number of retries */
	int maxretries;                           /*!< Maximum number of retries permitted */
	int retrytime;                            /*!< How long to wait between retries (in seconds) */
	int waittime;                             /*!< How long to wait for an answer */
	long callingpid;                          /*!< PID which is currently calling */
	int format;                               /*!< Formats (codecs) for this call */
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

static int init_outgoing(struct outgoing *o)
{
	o->priority = 1;
	o->retrytime = 300;
	o->waittime = 45;
	o->format = AST_FORMAT_SLINEAR;
	ast_set_flag(&o->options, SPOOL_FLAG_ALWAYS_DELETE);
	if (ast_string_field_init(o, 128)) {
		return -1;
	}
	return 0;
}

static void free_outgoing(struct outgoing *o)
{
	if (o->vars) {
		ast_variables_destroy(o->vars);
	}
	ast_string_field_free_memory(o);
	ast_free(o);
}

static int apply_outgoing(struct outgoing *o, char *fn, FILE *f)
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
		while(!ast_strlen_zero(buf) && buf[strlen(buf) - 1] < 33)
			buf[strlen(buf) - 1] = '\0';
		if (!ast_strlen_zero(buf)) {
			c = strchr(buf, ':');
			if (c) {
				*c = '\0';
				c++;
				while ((*c) && (*c < 33))
					c++;
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
						ast_log(LOG_NOTICE, "Channel should be in form Tech/Dest at line %d of %s\n", lineno, fn);
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
						ast_log(LOG_WARNING, "Invalid max retries at line %d of %s\n", lineno, fn);
						o->maxretries = 0;
					}
				} else if (!strcasecmp(buf, "codecs")) {
					ast_parse_allow_disallow(NULL, &o->format, c, 1);
				} else if (!strcasecmp(buf, "context")) {
					ast_string_field_set(o, context, c);
				} else if (!strcasecmp(buf, "extension")) {
					ast_string_field_set(o, exten, c);
				} else if (!strcasecmp(buf, "priority")) {
					if ((sscanf(c, "%30d", &o->priority) != 1) || (o->priority < 1)) {
						ast_log(LOG_WARNING, "Invalid priority at line %d of %s\n", lineno, fn);
						o->priority = 1;
					}
				} else if (!strcasecmp(buf, "retrytime")) {
					if ((sscanf(c, "%30d", &o->retrytime) != 1) || (o->retrytime < 1)) {
						ast_log(LOG_WARNING, "Invalid retrytime at line %d of %s\n", lineno, fn);
						o->retrytime = 300;
					}
				} else if (!strcasecmp(buf, "waittime")) {
					if ((sscanf(c, "%30d", &o->waittime) != 1) || (o->waittime < 1)) {
						ast_log(LOG_WARNING, "Invalid waittime at line %d of %s\n", lineno, fn);
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
						var = ast_variable_new(c, c2, fn);
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
				} else {
					ast_log(LOG_WARNING, "Unknown keyword '%s' at line %d of %s\n", buf, lineno, fn);
				}
			} else
				ast_log(LOG_NOTICE, "Syntax error at line %d of %s\n", lineno, fn);
		}
	}
	ast_string_field_set(o, fn, fn);
	if (ast_strlen_zero(o->tech) || ast_strlen_zero(o->dest) || (ast_strlen_zero(o->app) && ast_strlen_zero(o->exten))) {
		ast_log(LOG_WARNING, "At least one of app or extension must be specified, along with tech and dest in file %s\n", fn);
		return -1;
	}
	return 0;
}

static void safe_append(struct outgoing *o, time_t now, char *s)
{
	int fd;
	FILE *f;
	struct utimbuf tbuf;

	if ((fd = open(o->fn, O_WRONLY | O_APPEND)) < 0)
		return;

	if ((f = fdopen(fd, "a"))) {
		fprintf(f, "\n%s: %ld %d (%ld)\n", s, (long)ast_mainpid, o->retries, (long) now);
		fclose(f);
	} else
		close(fd);

	/* Update the file time */
	tbuf.actime = now;
	tbuf.modtime = now + o->retrytime;
	if (utime(o->fn, &tbuf))
		ast_log(LOG_WARNING, "Unable to set utime on %s: %s\n", o->fn, strerror(errno));
}

/*!
 * \brief Remove a call file from the outgoing queue optionally moving it in the archive dir
 *
 * \param o the pointer to outgoing struct
 * \param status the exit status of the call. Can be "Completed", "Failed" or "Expired"
 */
static int remove_from_queue(struct outgoing *o, const char *status)
{
	int fd;
	FILE *f;
	char newfn[256];
	const char *bname;

	if (!ast_test_flag(&o->options, SPOOL_FLAG_ALWAYS_DELETE)) {
		struct stat current_file_status;

		if (!stat(o->fn, &current_file_status)) {
			if (time(NULL) < current_file_status.st_mtime)
				return 0;
		}
	}

	if (!ast_test_flag(&o->options, SPOOL_FLAG_ARCHIVE)) {
		unlink(o->fn);
		return 0;
	}

	if (ast_mkdir(qdonedir, 0777)) {
		ast_log(LOG_WARNING, "Unable to create queue directory %s -- outgoing spool archiving disabled\n", qdonedir);
		unlink(o->fn);
		return -1;
	}

	if ((fd = open(o->fn, O_WRONLY | O_APPEND))) {
		if ((f = fdopen(fd, "a"))) {
			fprintf(f, "Status: %s\n", status);
			fclose(f);
		} else
			close(fd);
	}

	if (!(bname = strrchr(o->fn, '/')))
		bname = o->fn;
	else
		bname++;	
	snprintf(newfn, sizeof(newfn), "%s/%s", qdonedir, bname);
	/* a existing call file the archive dir is overwritten */
	unlink(newfn);
	if (rename(o->fn, newfn) != 0) {
		unlink(o->fn);
		return -1;
	} else
		return 0;
}

static void *attempt_thread(void *data)
{
	struct outgoing *o = data;
	int res, reason;
	if (!ast_strlen_zero(o->app)) {
		ast_verb(3, "Attempting call on %s/%s for application %s(%s) (Retry %d)\n", o->tech, o->dest, o->app, o->data, o->retries);
		res = ast_pbx_outgoing_app(o->tech, o->format, (void *) o->dest, o->waittime * 1000, o->app, o->data, &reason, 2 /* wait to finish */, o->cid_num, o->cid_name, o->vars, o->account, NULL);
		o->vars = NULL;
	} else {
		ast_verb(3, "Attempting call on %s/%s for %s@%s:%d (Retry %d)\n", o->tech, o->dest, o->exten, o->context,o->priority, o->retries);
		res = ast_pbx_outgoing_exten(o->tech, o->format, (void *) o->dest, o->waittime * 1000, o->context, o->exten, o->priority, &reason, 2 /* wait to finish */, o->cid_num, o->cid_name, o->vars, o->account, NULL);
		o->vars = NULL;
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

static int scan_service(char *fn, time_t now, time_t atime)
{
	struct outgoing *o = NULL;
	FILE *f;
	int res = 0;

	if (!(o = ast_calloc(1, sizeof(*o)))) {
		ast_log(LOG_WARNING, "Out of memory ;(\n");
		return -1;
	}
	
	if (init_outgoing(o)) {
		/* No need to call free_outgoing here since we know the failure
		 * was to allocate string fields and no variables have been allocated
		 * yet.
		 */
		ast_free(o);
		return -1;
	}

	/* Attempt to open the file */
	if (!(f = fopen(fn, "r+"))) {
		remove_from_queue(o, "Failed");
		free_outgoing(o);
		ast_log(LOG_WARNING, "Unable to open %s: %s, deleting\n", fn, strerror(errno));
		return -1;
	}

	/* Read in and verify the contents */
	if (apply_outgoing(o, fn, f)) {
		remove_from_queue(o, "Failed");
		free_outgoing(o);
		ast_log(LOG_WARNING, "Invalid file contents in %s, deleting\n", fn);
		fclose(f);
		return -1;
	}
	
#if 0
	printf("Filename: %s, Retries: %d, max: %d\n", fn, o->retries, o->maxretries);
#endif
	fclose(f);
	if (o->retries <= o->maxretries) {
		now += o->retrytime;
		if (o->callingpid && (o->callingpid == ast_mainpid)) {
			safe_append(o, time(NULL), "DelayedRetry");
			ast_log(LOG_DEBUG, "Delaying retry since we're currently running '%s'\n", o->fn);
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
		res = now;
	} else {
		ast_log(LOG_NOTICE, "Queued call to %s/%s expired without completion after %d attempt%s\n", o->tech, o->dest, o->retries - 1, ((o->retries - 1) != 1) ? "s" : "");
		remove_from_queue(o, "Expired");
		free_outgoing(o);
	}

	return res;
}

static void *scan_thread(void *unused)
{
	struct stat st;
	DIR *dir;
	struct dirent *de;
	char fn[256];
	int res;
	time_t last = 0, next = 0, now;
	struct timespec ts = { .tv_sec = 1 };
  
	while (!ast_fully_booted) {
		nanosleep(&ts, NULL);
	}

	for(;;) {
		/* Wait a sec */
		nanosleep(&ts, NULL);
		time(&now);

		if (stat(qdir, &st)) {
			ast_log(LOG_WARNING, "Unable to stat %s\n", qdir);
			continue;
		}

		/* Make sure it is time for us to execute our check */
		if ((st.st_mtime == last) && (next && (next > now)))
			continue;
		
#if 0
		printf("atime: %ld, mtime: %ld, ctime: %ld\n", st.st_atime, st.st_mtime, st.st_ctime);
		printf("Ooh, something changed / timeout\n");
#endif
		next = 0;
		last = st.st_mtime;

		if (!(dir = opendir(qdir))) {
			ast_log(LOG_WARNING, "Unable to open directory %s: %s\n", qdir, strerror(errno));
			continue;
		}

		while ((de = readdir(dir))) {
			snprintf(fn, sizeof(fn), "%s/%s", qdir, de->d_name);
			if (stat(fn, &st)) {
				ast_log(LOG_WARNING, "Unable to stat %s: %s\n", fn, strerror(errno));
				continue;
			}
			if (!S_ISREG(st.st_mode))
				continue;
			if (st.st_mtime <= now) {
				res = scan_service(fn, now, st.st_atime);
				if (res > 0) {
					/* Update next service time */
					if (!next || (res < next)) {
						next = res;
					}
				} else if (res) {
					ast_log(LOG_WARNING, "Failed to scan service '%s'\n", fn);
				} else if (!next) {
					/* Expired entry: must recheck on the next go-around */
					next = st.st_mtime;
				}
			} else {
				/* Update "next" update if necessary */
				if (!next || (st.st_mtime < next))
					next = st.st_mtime;
			}
		}
		closedir(dir);
	}
	return NULL;
}

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
