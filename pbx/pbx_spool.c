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
	char fn[256];
	int retries;                              /*!< Current number of retries */
	int maxretries;                           /*!< Maximum number of retries permitted */
	int retrytime;                            /*!< How long to wait between retries (in seconds) */
	int waittime;                             /*!< How long to wait for an answer */
	long callingpid;                          /*!< PID which is currently calling */
	int format;                               /*!< Formats (codecs) for this call */
	
	char tech[256];                           /*!< Which channel driver to use for outgoing call */
	char dest[256];                           /*!< Which device/line to use for outgoing call */

	char app[256];                            /*!< If application: Application name */
	char data[256];                           /*!< If applicatoin: Application data */

	char exten[AST_MAX_EXTENSION];            /*!< If extension/context/priority: Extension in dialplan */
	char context[AST_MAX_CONTEXT];            /*!< If extension/context/priority: Dialplan context */
	int priority;                             /*!< If extension/context/priority: Dialplan priority */

	char cid_num[256];                        /*!< CallerID Information: Number/extension */
	char cid_name[256];                       /*!< CallerID Information: Name */

	char account[AST_MAX_ACCOUNT_CODE];       /*!< account code */

	struct ast_variable *vars;                /*!< Variables and Functions */
	
	int maxlen;                               /*!< Maximum length of call */

	struct ast_flags options;                 /*!< options */
};

static void init_outgoing(struct outgoing *o)
{
	o->priority = 1;
	o->retrytime = 300;
	o->waittime = 45;
	o->format = AST_FORMAT_SLINEAR;
	ast_set_flag(&o->options, SPOOL_FLAG_ALWAYS_DELETE);
}

static void free_outgoing(struct outgoing *o)
{
	ast_free(o);
}

static int apply_outgoing(struct outgoing *o, char *fn, FILE *f)
{
	char buf[256];
	char *c, *c2;
	int lineno = 0;
	struct ast_variable *var;

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
					ast_copy_string(o->tech, c, sizeof(o->tech));
					if ((c2 = strchr(o->tech, '/'))) {
						*c2 = '\0';
						c2++;
						ast_copy_string(o->dest, c2, sizeof(o->dest));
					} else {
						ast_log(LOG_NOTICE, "Channel should be in form Tech/Dest at line %d of %s\n", lineno, fn);
						o->tech[0] = '\0';
					}
				} else if (!strcasecmp(buf, "callerid")) {
					ast_callerid_split(c, o->cid_name, sizeof(o->cid_name), o->cid_num, sizeof(o->cid_num));
				} else if (!strcasecmp(buf, "application")) {
					ast_copy_string(o->app, c, sizeof(o->app));
				} else if (!strcasecmp(buf, "data")) {
					ast_copy_string(o->data, c, sizeof(o->data));
				} else if (!strcasecmp(buf, "maxretries")) {
					if (sscanf(c, "%d", &o->maxretries) != 1) {
						ast_log(LOG_WARNING, "Invalid max retries at line %d of %s\n", lineno, fn);
						o->maxretries = 0;
					}
				} else if (!strcasecmp(buf, "codecs")) {
					ast_parse_allow_disallow(NULL, &o->format, c, 1);
				} else if (!strcasecmp(buf, "context")) {
					ast_copy_string(o->context, c, sizeof(o->context));
				} else if (!strcasecmp(buf, "extension")) {
					ast_copy_string(o->exten, c, sizeof(o->exten));
				} else if (!strcasecmp(buf, "priority")) {
					if ((sscanf(c, "%d", &o->priority) != 1) || (o->priority < 1)) {
						ast_log(LOG_WARNING, "Invalid priority at line %d of %s\n", lineno, fn);
						o->priority = 1;
					}
				} else if (!strcasecmp(buf, "retrytime")) {
					if ((sscanf(c, "%d", &o->retrytime) != 1) || (o->retrytime < 1)) {
						ast_log(LOG_WARNING, "Invalid retrytime at line %d of %s\n", lineno, fn);
						o->retrytime = 300;
					}
				} else if (!strcasecmp(buf, "waittime")) {
					if ((sscanf(c, "%d", &o->waittime) != 1) || (o->waittime < 1)) {
						ast_log(LOG_WARNING, "Invalid waittime at line %d of %s\n", lineno, fn);
						o->waittime = 45;
					}
				} else if (!strcasecmp(buf, "retry")) {
					o->retries++;
				} else if (!strcasecmp(buf, "startretry")) {
					if (sscanf(c, "%ld", &o->callingpid) != 1) {
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
							var->next = o->vars;
							o->vars = var;
						}
					} else
						ast_log(LOG_WARNING, "Malformed \"%s\" argument.  Should be \"%s: variable=value\"\n", buf, buf);
				} else if (!strcasecmp(buf, "account")) {
					ast_copy_string(o->account, c, sizeof(o->account));
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
	ast_copy_string(o->fn, fn, sizeof(o->fn));
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
		res = ast_pbx_outgoing_app(o->tech, o->format, o->dest, o->waittime * 1000, o->app, o->data, &reason, 2 /* wait to finish */, o->cid_num, o->cid_name, o->vars, o->account, NULL);
	} else {
		ast_verb(3, "Attempting call on %s/%s for %s@%s:%d (Retry %d)\n", o->tech, o->dest, o->exten, o->context,o->priority, o->retries);
		res = ast_pbx_outgoing_exten(o->tech, o->format, o->dest, o->waittime * 1000, o->context, o->exten, o->priority, &reason, 2 /* wait to finish */, o->cid_num, o->cid_name, o->vars, o->account, NULL);
	}
	if (res) {
		ast_log(LOG_NOTICE, "Call failed to go through, reason (%d) %s\n", reason, ast_channel_reason2str(reason));
		if (o->retries >= o->maxretries + 1) {
			/* Max retries exceeded */
			ast_log(LOG_EVENT, "Queued call to %s/%s expired without completion after %d attempt%s\n", o->tech, o->dest, o->retries - 1, ((o->retries - 1) != 1) ? "s" : "");
			remove_from_queue(o, "Expired");
		} else {
			/* Notate that the call is still active */
			safe_append(o, time(NULL), "EndRetry");
		}
	} else {
		ast_log(LOG_NOTICE, "Call completed to %s/%s\n", o->tech, o->dest);
		ast_log(LOG_EVENT, "Queued call to %s/%s completed\n", o->tech, o->dest);
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
	
	init_outgoing(o);

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
		ast_log(LOG_EVENT, "Queued call to %s/%s expired without completion after %d attempt%s\n", o->tech, o->dest, o->retries - 1, ((o->retries - 1) != 1) ? "s" : "");
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

	for(;;) {
		/* Wait a sec */
		sleep(1);
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
				} else if (res)
					ast_log(LOG_WARNING, "Failed to scan service '%s'\n", fn);
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
