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

/*
 *
 * Standard Command Line Interface
 * 
 */

#include <unistd.h>
#include <stdlib.h>
#include <sys/signal.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/channel.h"
#include "asterisk/manager.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
/* For rl_filename_completion */
#include "editline/readline/readline.h"
/* For module directory */
#include "asterisk/version.h"
#include "asterisk/build.h"

#define VERSION_INFO "Asterisk " ASTERISK_VERSION " built by " BUILD_USER "@" BUILD_HOSTNAME \
	" on a " BUILD_MACHINE " running " BUILD_OS " on " BUILD_DATE
	
extern unsigned long global_fin, global_fout;
	
void ast_cli(int fd, char *fmt, ...)
{
	char *stuff;
	int res = 0;
	va_list ap;

	va_start(ap, fmt);
	res = vasprintf(&stuff, fmt, ap);
	va_end(ap);
	if (res == -1) {
		ast_log(LOG_ERROR, "Out of memory\n");
	} else {
		ast_carefulwrite(fd, stuff, strlen(stuff), 100);
		free(stuff);
	}
}

AST_MUTEX_DEFINE_STATIC(clilock);

struct ast_cli_entry *helpers = NULL;

static char load_help[] = 
"Usage: load <module name>\n"
"       Loads the specified module into Asterisk.\n";

static char unload_help[] = 
"Usage: unload [-f|-h] <module name>\n"
"       Unloads the specified module from Asterisk. The -f\n"
"       option causes the module to be unloaded even if it is\n"
"       in use (may cause a crash) and the -h module causes the\n"
"       module to be unloaded even if the module says it cannot, \n"
"       which almost always will cause a crash.\n";

static char help_help[] =
"Usage: help [topic]\n"
"       When called with a topic as an argument, displays usage\n"
"       information on the given command. If called without a\n"
"       topic, it provides a list of commands.\n";

static char chanlist_help[] = 
"Usage: show channels [concise|verbose]\n"
"       Lists currently defined channels and some information about them. If\n"
"       'concise' is specified, the format is abridged and in a more easily\n"
"       machine parsable format. If 'verbose' is specified, the output includes\n"
"       more and longer fields.\n";

static char reload_help[] = 
"Usage: reload [module ...]\n"
"       Reloads configuration files for all listed modules which support\n"
"       reloading, or for all supported modules if none are listed.\n";

static char set_verbose_help[] = 
"Usage: set verbose <level>\n"
"       Sets level of verbose messages to be displayed.  0 means\n"
"       no messages should be displayed. Equivalent to -v[v[v...]]\n"
"       on startup\n";

static char set_debug_help[] = 
"Usage: set debug <level>\n"
"       Sets level of core debug messages to be displayed.  0 means\n"
"       no messages should be displayed. Equivalent to -d[d[d...]]\n"
"       on startup.\n";

static char softhangup_help[] =
"Usage: soft hangup <channel>\n"
"       Request that a channel be hung up. The hangup takes effect\n"
"       the next time the driver reads or writes from the channel\n";

static int handle_load(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	if (ast_load_resource(argv[1])) {
		ast_cli(fd, "Unable to load module %s\n", argv[1]);
		return RESULT_FAILURE;
	}
	return RESULT_SUCCESS;
}

static int handle_reload(int fd, int argc, char *argv[])
{
	int x;
	int res;
	if (argc < 1)
		return RESULT_SHOWUSAGE;
	if (argc > 1) { 
		for (x=1;x<argc;x++) {
			res = ast_module_reload(argv[x]);
			switch(res) {
			case 0:
				ast_cli(fd, "No such module '%s'\n", argv[x]);
				break;
			case 1:
				ast_cli(fd, "Module '%s' does not support reload\n", argv[x]);
				break;
			}
		}
	} else
		ast_module_reload(NULL);
	return RESULT_SUCCESS;
}

static int handle_set_verbose(int fd, int argc, char *argv[])
{
	int val = 0;
	int oldval = 0;

	/* Has a hidden 'at least' argument */
	if ((argc != 3) && (argc != 4))
		return RESULT_SHOWUSAGE;
	if ((argc == 4) && strcasecmp(argv[2], "atleast"))
		return RESULT_SHOWUSAGE;
	oldval = option_verbose;
	if (argc == 3)
		option_verbose = atoi(argv[2]);
	else {
		val = atoi(argv[3]);
		if (val > option_verbose)
			option_verbose = val;
	}
	if (oldval != option_verbose && option_verbose > 0)
		ast_cli(fd, "Verbosity was %d and is now %d\n", oldval, option_verbose);
	else if (oldval > 0 && option_verbose > 0)
		ast_cli(fd, "Verbosity is at least %d\n", option_verbose);
	else if (oldval > 0 && option_verbose == 0)
		ast_cli(fd, "Verbosity is now OFF\n");
	return RESULT_SUCCESS;
}

static int handle_set_debug(int fd, int argc, char *argv[])
{
	int val = 0;
	int oldval = 0;
	/* Has a hidden 'at least' argument */
	if ((argc != 3) && (argc != 4))
		return RESULT_SHOWUSAGE;
	if ((argc == 4) && strcasecmp(argv[2], "atleast"))
		return RESULT_SHOWUSAGE;
	oldval = option_debug;
	if (argc == 3)
		option_debug = atoi(argv[2]);
	else {
		val = atoi(argv[3]);
		if (val > option_debug)
			option_debug = val;
	}
	if (oldval != option_debug && option_debug > 0)
		ast_cli(fd, "Core debug was %d and is now %d\n", oldval, option_debug);
	else if (oldval > 0 && option_debug > 0)
		ast_cli(fd, "Core debug is at least %d\n", option_debug);
	else if (oldval > 0 && option_debug == 0)
		ast_cli(fd, "Core debug is now OFF\n");
	return RESULT_SUCCESS;
}

static int handle_unload(int fd, int argc, char *argv[])
{
	int x;
	int force=AST_FORCE_SOFT;
	if (argc < 2)
		return RESULT_SHOWUSAGE;
	for (x=1;x<argc;x++) {
		if (argv[x][0] == '-') {
			switch(argv[x][1]) {
			case 'f':
				force = AST_FORCE_FIRM;
				break;
			case 'h':
				force = AST_FORCE_HARD;
				break;
			default:
				return RESULT_SHOWUSAGE;
			}
		} else if (x !=  argc - 1) 
			return RESULT_SHOWUSAGE;
		else if (ast_unload_resource(argv[x], force)) {
			ast_cli(fd, "Unable to unload resource %s\n", argv[x]);
			return RESULT_FAILURE;
		}
	}
	return RESULT_SUCCESS;
}

#define MODLIST_FORMAT  "%-30s %-40.40s %-10d\n"
#define MODLIST_FORMAT2 "%-30s %-40.40s %-10s\n"

AST_MUTEX_DEFINE_STATIC(climodentrylock);
static int climodentryfd = -1;

static int modlist_modentry(const char *module, const char *description, int usecnt, const char *like)
{
	/* Comparing the like with the module */
	if (strcasestr(module, like) ) {
		ast_cli(climodentryfd, MODLIST_FORMAT, module, description, usecnt);
		return 1;
	} 
	return 0;
}

static char modlist_help[] =
"Usage: show modules [like keyword]\n"
"       Shows Asterisk modules currently in use, and usage statistics.\n";

static char version_help[] =
"Usage: show version\n"
"       Shows Asterisk version information.\n";

static char uptime_help[] =
"Usage: show uptime [seconds]\n"
"       Shows Asterisk uptime information.\n"
"       The seconds word returns the uptime in seconds only.\n";

static char *format_uptimestr(time_t timeval)
{
	int years = 0, weeks = 0, days = 0, hours = 0, mins = 0, secs = 0;
	char timestr[256]="";
	int bytes = 0;
	int maxbytes = 0;
	int offset = 0;
#define SECOND (1)
#define MINUTE (SECOND*60)
#define HOUR (MINUTE*60)
#define DAY (HOUR*24)
#define WEEK (DAY*7)
#define YEAR (DAY*365)
#define ESS(x) ((x == 1) ? "" : "s")

	maxbytes = sizeof(timestr);
	if (timeval < 0)
		return NULL;
	if (timeval > YEAR) {
		years = (timeval / YEAR);
		timeval -= (years * YEAR);
		if (years > 0) {
			snprintf(timestr + offset, maxbytes, "%d year%s, ", years, ESS(years));
			bytes = strlen(timestr + offset);
			offset += bytes;
			maxbytes -= bytes;
		}
	}
	if (timeval > WEEK) {
		weeks = (timeval / WEEK);
		timeval -= (weeks * WEEK);
		if (weeks > 0) {
			snprintf(timestr + offset, maxbytes, "%d week%s, ", weeks, ESS(weeks));
			bytes = strlen(timestr + offset);
			offset += bytes;
			maxbytes -= bytes;
		}
	}
	if (timeval > DAY) {
		days = (timeval / DAY);
		timeval -= (days * DAY);
		if (days > 0) {
			snprintf(timestr + offset, maxbytes, "%d day%s, ", days, ESS(days));
			bytes = strlen(timestr + offset);
			offset += bytes;
			maxbytes -= bytes;
		}
	}
	if (timeval > HOUR) {
		hours = (timeval / HOUR);
		timeval -= (hours * HOUR);
		if (hours > 0) {
			snprintf(timestr + offset, maxbytes, "%d hour%s, ", hours, ESS(hours));
			bytes = strlen(timestr + offset);
			offset += bytes;
			maxbytes -= bytes;
		}
	}
	if (timeval > MINUTE) {
		mins = (timeval / MINUTE);
		timeval -= (mins * MINUTE);
		if (mins > 0) {
			snprintf(timestr + offset, maxbytes, "%d minute%s, ", mins, ESS(mins));
			bytes = strlen(timestr + offset);
			offset += bytes;
			maxbytes -= bytes;
		}
	}
	secs = timeval;

	if (secs > 0) {
		snprintf(timestr + offset, maxbytes, "%d second%s", secs, ESS(secs));
	}

	return timestr ? strdup(timestr) : NULL;
}

static int handle_showuptime(int fd, int argc, char *argv[])
{
	time_t curtime, tmptime;
	char *timestr;
	int printsec;

	printsec = ((argc == 3) && (!strcasecmp(argv[2],"seconds")));
	if ((argc != 2) && (!printsec))
		return RESULT_SHOWUSAGE;

	time(&curtime);
	if (ast_startuptime) {
		tmptime = curtime - ast_startuptime;
		if (printsec) {
			ast_cli(fd, "System uptime: %lu\n",tmptime);
		} else {
			timestr = format_uptimestr(tmptime);
			if (timestr) {
				ast_cli(fd, "System uptime: %s\n", timestr);
				free(timestr);
			}
		}
	}		
	if (ast_lastreloadtime) {
		tmptime = curtime - ast_lastreloadtime;
		if (printsec) {
			ast_cli(fd, "Last reload: %lu\n", tmptime);
		} else {
			timestr = format_uptimestr(tmptime);
			if ((timestr) && (!printsec)) {
				ast_cli(fd, "Last reload: %s\n", timestr);
				free(timestr);
			} 
		}
	}
	return RESULT_SUCCESS;
}

static int handle_modlist(int fd, int argc, char *argv[])
{
	char *like = "";
	if (argc == 3)
		return RESULT_SHOWUSAGE;
	else if (argc >= 4) {
		if (strcmp(argv[2],"like")) 
			return RESULT_SHOWUSAGE;
		like = argv[3];
	}
		
	ast_mutex_lock(&climodentrylock);
	climodentryfd = fd;
	ast_cli(fd, MODLIST_FORMAT2, "Module", "Description", "Use Count");
	ast_cli(fd,"%d modules loaded\n", ast_update_module_list(modlist_modentry, like));
	climodentryfd = -1;
	ast_mutex_unlock(&climodentrylock);
	return RESULT_SUCCESS;
}
#undef MODLIST_FORMAT
#undef MODLIST_FORMAT2

static int handle_version(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, "%s\n", VERSION_INFO);
	return RESULT_SUCCESS;
}
static int handle_chanlist(int fd, int argc, char *argv[])
{
#define FORMAT_STRING  "%-20.20s %-20.20s %-7.7s %-30.30s\n"
#define FORMAT_STRING2 "%-20.20s %-20.20s %-7.7s %-30.30s\n"
#define CONCISE_FORMAT_STRING  "%s:%s:%s:%d:%s:%s:%s:%s:%s:%d:%s:%s\n"
#define VERBOSE_FORMAT_STRING  "%-20.20s %-20.20s %-16.16s %4d %-7.7s %-12.12s %-25.25s %-15.15s %8.8s %-11.11s %-20.20s\n"
#define VERBOSE_FORMAT_STRING2 "%-20.20s %-20.20s %-16.16s %-4.4s %-7.7s %-12.12s %-25.25s %-15.15s %8.8s %-11.11s %-20.20s\n"

	struct ast_channel *c = NULL, *bc = NULL;
	char durbuf[10] = "-";
	char locbuf[40];
	char appdata[40];
	int duration;
	int durh, durm, durs;
	int numchans = 0, concise = 0, verbose = 0;

	concise = (argc == 3 && (!strcasecmp(argv[2],"concise")));
	verbose = (argc == 3 && (!strcasecmp(argv[2],"verbose")));

	if (argc < 2 || argc > 3 || (argc == 3 && !concise && !verbose))
		return RESULT_SHOWUSAGE;

	if (!concise && !verbose)
		ast_cli(fd, FORMAT_STRING2, "Channel", "Location", "State", "Application(Data)");
	else if (verbose)
		ast_cli(fd, VERBOSE_FORMAT_STRING2, "Channel", "Context", "Extension", "Priority", "State", "Application", "Data", 
		        "CallerID", "Duration", "Accountcode", "BridgedTo");
	while ((c = ast_channel_walk_locked(c)) != NULL) {
		bc = ast_bridged_channel(c);
		if ((concise || verbose)  && c->cdr && !ast_tvzero(c->cdr->start)) {
			duration = (int)(ast_tvdiff_ms(ast_tvnow(), c->cdr->start) / 1000);
			if (verbose) {
				durh = duration / 3600;
				durm = (duration % 3600) / 60;
				durs = duration % 60;
				snprintf(durbuf, sizeof(durbuf), "%02d:%02d:%02d", durh, durm, durs);
			} else {
				snprintf(durbuf, sizeof(durbuf), "%d", duration);
			}				
		} else {
			durbuf[0] = '\0';
		}
		if (concise) {
			ast_cli(fd, CONCISE_FORMAT_STRING, c->name, c->context, c->exten, c->priority, ast_state2str(c->_state),
			        c->appl ? c->appl : "(None)", c->data ? ( !ast_strlen_zero(c->data) ? c->data : "" ): "",
			        (c->cid.cid_num && !ast_strlen_zero(c->cid.cid_num)) ? c->cid.cid_num : "",
			        (c->accountcode && !ast_strlen_zero(c->accountcode)) ? c->accountcode : "", c->amaflags, 
			        durbuf, bc ? bc->name : "(None)");
		} else if (verbose) {
			ast_cli(fd, VERBOSE_FORMAT_STRING, c->name, c->context, c->exten, c->priority, ast_state2str(c->_state),
			        c->appl ? c->appl : "(None)", c->data ? ( !ast_strlen_zero(c->data) ? c->data : "(Empty)" ): "(None)",
			        (c->cid.cid_num && !ast_strlen_zero(c->cid.cid_num)) ? c->cid.cid_num : "", durbuf,
			        (c->accountcode && !ast_strlen_zero(c->accountcode)) ? c->accountcode : "", bc ? bc->name : "(None)");
		} else {
			if (!ast_strlen_zero(c->context) && !ast_strlen_zero(c->exten)) 
				snprintf(locbuf, sizeof(locbuf), "%s@%s:%d", c->exten, c->context, c->priority);
			else
				strcpy(locbuf, "(None)");
			if (c->appl) {
				snprintf(appdata, sizeof(appdata), "%s(%s)", c->appl, c->data ? c->data : "");
			} else {
				strcpy(appdata, "(None)");
			}
			ast_cli(fd, FORMAT_STRING, c->name, locbuf, ast_state2str(c->_state), appdata);
		}
		numchans++;
		ast_mutex_unlock(&c->lock);
	}
	if (!concise) {
		ast_cli(fd, "%d active channel%s\n", numchans, (numchans!=1) ? "s" : "");
		if (option_maxcalls)
			ast_cli(fd, "%d of %d max active call%s (%5.2f%% of capacity)\n", ast_active_calls(), option_maxcalls, (ast_active_calls()!=1) ? "s" : "", ((float)ast_active_calls() / (float)option_maxcalls) * 100.0);
		else
			ast_cli(fd, "%d active call%s\n", ast_active_calls(), (ast_active_calls()!=1) ? "s" : "");
	}
	return RESULT_SUCCESS;
	
#undef FORMAT_STRING
#undef FORMAT_STRING2
#undef CONCISE_FORMAT_STRING
#undef VERBOSE_FORMAT_STRING
#undef VERBOSE_FORMAT_STRING2
}

static char showchan_help[] = 
"Usage: show channel <channel>\n"
"       Shows lots of information about the specified channel.\n";

static char debugchan_help[] = 
"Usage: debug channel <channel>\n"
"       Enables debugging on a specific channel.\n";

static char debuglevel_help[] = 
"Usage: debug level <level> [filename]\n"
"       Set debug to specified level (0 to disable).  If filename\n"
"is specified, debugging will be limited to just that file.\n";

static char nodebugchan_help[] = 
"Usage: no debug channel <channel>\n"
"       Disables debugging on a specific channel.\n";

static char commandcomplete_help[] = 
"Usage: _command complete \"<line>\" text state\n"
"       This function is used internally to help with command completion and should.\n"
"       never be called by the user directly.\n";

static char commandnummatches_help[] = 
"Usage: _command nummatches \"<line>\" text \n"
"       This function is used internally to help with command completion and should.\n"
"       never be called by the user directly.\n";

static char commandmatchesarray_help[] = 
"Usage: _command matchesarray \"<line>\" text \n"
"       This function is used internally to help with command completion and should.\n"
"       never be called by the user directly.\n";

static int handle_softhangup(int fd, int argc, char *argv[])
{
	struct ast_channel *c=NULL;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	c = ast_get_channel_by_name_locked(argv[2]);
	if (c) {
		ast_cli(fd, "Requested Hangup on channel '%s'\n", c->name);
		ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
		ast_mutex_unlock(&c->lock);
	} else
		ast_cli(fd, "%s is not a known channel\n", argv[2]);
	return RESULT_SUCCESS;
}

static char *__ast_cli_generator(char *text, char *word, int state, int lock);

static int handle_commandmatchesarray(int fd, int argc, char *argv[])
{
	char *buf, *obuf;
	int buflen = 2048;
	int len = 0;
	char **matches;
	int x, matchlen;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	buf = malloc(buflen);
	if (!buf)
		return RESULT_FAILURE;
	buf[len] = '\0';
	matches = ast_cli_completion_matches(argv[2], argv[3]);
	if (matches) {
		for (x=0; matches[x]; x++) {
#if 0
			printf("command matchesarray for '%s' %s got '%s'\n", argv[2], argv[3], matches[x]);
#endif
			matchlen = strlen(matches[x]) + 1;
			if (len + matchlen >= buflen) {
				buflen += matchlen * 3;
				obuf = buf;
				buf = realloc(obuf, buflen);
				if (!buf) 
					/* Out of memory...  Just free old buffer and be done */
					free(obuf);
			}
			if (buf)
				len += sprintf( buf + len, "%s ", matches[x]);
			free(matches[x]);
			matches[x] = NULL;
		}
		free(matches);
	}
#if 0
	printf("array for '%s' %s got '%s'\n", argv[2], argv[3], buf);
#endif
	
	if (buf) {
		ast_cli(fd, "%s%s",buf, AST_CLI_COMPLETE_EOF);
		free(buf);
	} else
		ast_cli(fd, "NULL\n");

	return RESULT_SUCCESS;
}



static int handle_commandnummatches(int fd, int argc, char *argv[])
{
	int matches = 0;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	matches = ast_cli_generatornummatches(argv[2], argv[3]);

#if 0
	printf("Search for '%s' %s got '%d'\n", argv[2], argv[3], matches);
#endif
	ast_cli(fd, "%d", matches);

	return RESULT_SUCCESS;
}

static int handle_commandcomplete(int fd, int argc, char *argv[])
{
	char *buf;
#if 0
	printf("Search for %d args: '%s', '%s', '%s', '%s'\n", argc, argv[0], argv[1], argv[2], argv[3]);
#endif	
	if (argc != 5)
		return RESULT_SHOWUSAGE;
	buf = __ast_cli_generator(argv[2], argv[3], atoi(argv[4]), 0);
#if 0
	printf("Search for '%s' %s %d got '%s'\n", argv[2], argv[3], atoi(argv[4]), buf);
#endif	
	if (buf) {
		ast_cli(fd, buf);
		free(buf);
	} else
		ast_cli(fd, "NULL\n");
	return RESULT_SUCCESS;
}

static int handle_debuglevel(int fd, int argc, char *argv[])
{
	int newlevel;
	char *filename = "<any>";
	if ((argc < 3) || (argc > 4))
		return RESULT_SHOWUSAGE;
	if (sscanf(argv[2], "%d", &newlevel) != 1)
		return RESULT_SHOWUSAGE;
	option_debug = newlevel;
	if (argc == 4) {
		filename = argv[3];
		ast_copy_string(debug_filename, filename, sizeof(debug_filename));
	} else {
		debug_filename[0] = '\0';
	}
	ast_cli(fd, "Debugging level set to %d, file '%s'\n", newlevel, filename);
	return RESULT_SUCCESS;
}

#define	DEBUGCHAN_FLAG	0x80000000
/* XXX todo: merge next two functions!!! */
static int handle_debugchan(int fd, int argc, char *argv[])
{
	struct ast_channel *c=NULL;
	int is_all;
	if (argc != 3)
		return RESULT_SHOWUSAGE;

	is_all = !strcasecmp("all", argv[2]);
	if (is_all) {
		global_fin |= DEBUGCHAN_FLAG;
		global_fout |= DEBUGCHAN_FLAG;
		c = ast_channel_walk_locked(NULL);
	} else {
		c = ast_get_channel_by_name_locked(argv[2]);
		if (c == NULL)
			ast_cli(fd, "No such channel %s\n", argv[2]);
	}
	while(c) {
		if (!(c->fin & DEBUGCHAN_FLAG) || !(c->fout & DEBUGCHAN_FLAG)) {
			c->fin |= DEBUGCHAN_FLAG;
			c->fout |= DEBUGCHAN_FLAG;
			ast_cli(fd, "Debugging enabled on channel %s\n", c->name);
		}
		ast_mutex_unlock(&c->lock);
		if (!is_all)
			break;
		c = ast_channel_walk_locked(c);
	}
	ast_cli(fd, "Debugging on new channels is enabled\n");
	return RESULT_SUCCESS;
}

static int handle_nodebugchan(int fd, int argc, char *argv[])
{
	struct ast_channel *c=NULL;
	int is_all;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	is_all = !strcasecmp("all", argv[3]);
	if (is_all) {
		global_fin &= ~DEBUGCHAN_FLAG;
		global_fout &= ~DEBUGCHAN_FLAG;
		c = ast_channel_walk_locked(NULL);
	} else {
		c = ast_get_channel_by_name_locked(argv[3]);
		if (c == NULL)
			ast_cli(fd, "No such channel %s\n", argv[3]);
    }
	while(c) {
		if ((c->fin & DEBUGCHAN_FLAG) || (c->fout & DEBUGCHAN_FLAG)) {
			c->fin &= ~DEBUGCHAN_FLAG;
			c->fout &= ~DEBUGCHAN_FLAG;
			ast_cli(fd, "Debugging disabled on channel %s\n", c->name);
		}
		ast_mutex_unlock(&c->lock);
		if (!is_all)
			break;
		c = ast_channel_walk_locked(c);
	}
	ast_cli(fd, "Debugging on new channels is disabled\n");
	return RESULT_SUCCESS;
}
		
	

static int handle_showchan(int fd, int argc, char *argv[])
{
	struct ast_channel *c=NULL;
	struct timeval now;
	char buf[2048];
	char cdrtime[256];
	long elapsed_seconds=0;
	int hour=0, min=0, sec=0;
	
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	now = ast_tvnow();
	c = ast_get_channel_by_name_locked(argv[2]);
	if (!c) {
		ast_cli(fd, "%s is not a known channel\n", argv[2]);
		return RESULT_SUCCESS;
	}
	if(c->cdr) {
		elapsed_seconds = now.tv_sec - c->cdr->start.tv_sec;
		hour = elapsed_seconds / 3600;
		min = (elapsed_seconds % 3600) / 60;
		sec = elapsed_seconds % 60;
		snprintf(cdrtime, sizeof(cdrtime), "%dh%dm%ds", hour, min, sec);
	} else
		strcpy(cdrtime, "N/A");
	ast_cli(fd, 
		" -- General --\n"
		"           Name: %s\n"
		"           Type: %s\n"
		"       UniqueID: %s\n"
		"      Caller ID: %s\n"
		" Caller ID Name: %s\n"
		"    DNID Digits: %s\n"
		"          State: %s (%d)\n"
		"          Rings: %d\n"
		"   NativeFormat: %d\n"
		"    WriteFormat: %d\n"
		"     ReadFormat: %d\n"
		"1st File Descriptor: %d\n"
		"      Frames in: %d%s\n"
		"     Frames out: %d%s\n"
		" Time to Hangup: %ld\n"
		"   Elapsed Time: %s\n"
		"  Direct Bridge: %s\n"
		"Indirect Bridge: %s\n"
		" --   PBX   --\n"
		"        Context: %s\n"
		"      Extension: %s\n"
		"       Priority: %d\n"
		"     Call Group: %d\n"
		"   Pickup Group: %d\n"
		"    Application: %s\n"
		"           Data: %s\n"
		"    Blocking in: %s\n",
		c->name, c->type, c->uniqueid,
		(c->cid.cid_num ? c->cid.cid_num : "(N/A)"),
		(c->cid.cid_name ? c->cid.cid_name : "(N/A)"),
		(c->cid.cid_dnid ? c->cid.cid_dnid : "(N/A)" ), ast_state2str(c->_state), c->_state, c->rings, c->nativeformats, c->writeformat, c->readformat,
		c->fds[0], c->fin & 0x7fffffff, (c->fin & 0x80000000) ? " (DEBUGGED)" : "",
		c->fout & 0x7fffffff, (c->fout & 0x80000000) ? " (DEBUGGED)" : "", (long)c->whentohangup,
		cdrtime, c->_bridge ? c->_bridge->name : "<none>", ast_bridged_channel(c) ? ast_bridged_channel(c)->name : "<none>", 
		c->context, c->exten, c->priority, c->callgroup, c->pickupgroup, ( c->appl ? c->appl : "(N/A)" ),
		( c-> data ? (!ast_strlen_zero(c->data) ? c->data : "(Empty)") : "(None)"),
		(ast_test_flag(c, AST_FLAG_BLOCKING) ? c->blockproc : "(Not Blocking)"));
	
	if(pbx_builtin_serialize_variables(c,buf,sizeof(buf)))
		ast_cli(fd,"      Variables:\n%s\n",buf);
	if(c->cdr && ast_cdr_serialize_variables(c->cdr,buf, sizeof(buf), '=', '\n', 1))
		ast_cli(fd,"  CDR Variables:\n%s\n",buf);
	
	ast_mutex_unlock(&c->lock);
	return RESULT_SUCCESS;
}

static char *complete_show_channels(char *line, char *word, int pos, int state)
{
	static char *choices[] = { "concise", "verbose" };
	int match = 0;
	int x;
	if (pos != 2) 
		return NULL;
	for (x=0;x<sizeof(choices) / sizeof(choices[0]);x++) {
		if (!strncasecmp(word, choices[x], strlen(word))) {
			match++;
			if (match > state) return strdup(choices[x]);
		}
	}
	return NULL;
}

static char *complete_ch_helper(char *line, char *word, int pos, int state, int rpos)
{
	struct ast_channel *c = NULL;
	int which=0;
	char *ret = NULL;

	if (pos != rpos)
		return NULL;
	while ( (c = ast_channel_walk_locked(c)) != NULL) {
		if (!strncasecmp(word, c->name, strlen(word))) {
			if (++which > state) {
				ret = strdup(c->name);
				ast_mutex_unlock(&c->lock);
				break;
			}
		}
		ast_mutex_unlock(&c->lock);
	}
	return ret;
}

static char *complete_ch_3(char *line, char *word, int pos, int state)
{
	return complete_ch_helper(line, word, pos, state, 2);
}

static char *complete_ch_4(char *line, char *word, int pos, int state)
{
	return complete_ch_helper(line, word, pos, state, 3);
}

static char *complete_mod_2(char *line, char *word, int pos, int state)
{
	return ast_module_helper(line, word, pos, state, 1, 1);
}

static char *complete_mod_4(char *line, char *word, int pos, int state)
{
	return ast_module_helper(line, word, pos, state, 3, 0);
}

static char *complete_fn(char *line, char *word, int pos, int state)
{
	char *c;
	char filename[256];
	if (pos != 1)
		return NULL;
	if (word[0] == '/')
		ast_copy_string(filename, word, sizeof(filename));
	else
		snprintf(filename, sizeof(filename), "%s/%s", (char *)ast_config_AST_MODULE_DIR, word);
	c = (char*)filename_completion_function(filename, state);
	if (c && word[0] != '/')
		c += (strlen((char*)ast_config_AST_MODULE_DIR) + 1);
	return c ? strdup(c) : c;
}

static int handle_help(int fd, int argc, char *argv[]);

static struct ast_cli_entry builtins[] = {
	/* Keep alphabetized, with longer matches first (example: abcd before abc) */
	{ { "_command", "complete", NULL }, handle_commandcomplete, "Command complete", commandcomplete_help },
	{ { "_command", "nummatches", NULL }, handle_commandnummatches, "Returns number of command matches", commandnummatches_help },
	{ { "_command", "matchesarray", NULL }, handle_commandmatchesarray, "Returns command matches array", commandmatchesarray_help },
	{ { "debug", "channel", NULL }, handle_debugchan, "Enable debugging on a channel", debugchan_help, complete_ch_3 },
	{ { "debug", "level", NULL }, handle_debuglevel, "Set global debug level", debuglevel_help },
	{ { "help", NULL }, handle_help, "Display help list, or specific help on a command", help_help },
	{ { "load", NULL }, handle_load, "Load a dynamic module by name", load_help, complete_fn },
	{ { "no", "debug", "channel", NULL }, handle_nodebugchan, "Disable debugging on a channel", nodebugchan_help, complete_ch_4 },
	{ { "reload", NULL }, handle_reload, "Reload configuration", reload_help, complete_mod_2 },
	{ { "set", "debug", NULL }, handle_set_debug, "Set level of debug chattiness", set_debug_help },
	{ { "set", "verbose", NULL }, handle_set_verbose, "Set level of verboseness", set_verbose_help },
	{ { "show", "channel", NULL }, handle_showchan, "Display information on a specific channel", showchan_help, complete_ch_3 },
	{ { "show", "channels", NULL }, handle_chanlist, "Display information on channels", chanlist_help, complete_show_channels },
	{ { "show", "modules", NULL }, handle_modlist, "List modules and info", modlist_help },
	{ { "show", "modules", "like", NULL }, handle_modlist, "List modules and info", modlist_help, complete_mod_4 },
 	{ { "show", "uptime", NULL }, handle_showuptime, "Show uptime information", uptime_help },
	{ { "show", "version", NULL }, handle_version, "Display version info", version_help },
	{ { "soft", "hangup", NULL }, handle_softhangup, "Request a hangup on a given channel", softhangup_help, complete_ch_3 },
	{ { "unload", NULL }, handle_unload, "Unload a dynamic module by name", unload_help, complete_fn },
	{ { NULL }, NULL, NULL, NULL }
};

static struct ast_cli_entry *find_cli(char *cmds[], int exact)
{
	int x;
	int y;
	int match;
	struct ast_cli_entry *e=NULL;

	for (e=helpers;e;e=e->next) {
		match = 1;
		for (y=0;match && cmds[y]; y++) {
			if (!e->cmda[y] && !exact)
				break;
			if (!e->cmda[y] || strcasecmp(e->cmda[y], cmds[y]))
				match = 0;
		}
		if ((exact > -1) && e->cmda[y])
			match = 0;
		if (match)
			break;
	}
	if (e)
		return e;
	for (x=0;builtins[x].cmda[0];x++) {
		/* start optimistic */
		match = 1;
		for (y=0;match && cmds[y]; y++) {
			/* If there are no more words in the candidate command, then we're
			   there.  */
			if (!builtins[x].cmda[y] && !exact)
				break;
			/* If there are no more words in the command (and we're looking for
			   an exact match) or there is a difference between the two words,
			   then this is not a match */
			if (!builtins[x].cmda[y] || strcasecmp(builtins[x].cmda[y], cmds[y]))
				match = 0;
		}
		/* If more words are needed to complete the command then this is not
		   a candidate (unless we're looking for a really inexact answer  */
		if ((exact > -1) && builtins[x].cmda[y])
			match = 0;
		if (match)
			return &builtins[x];
	}
	return NULL;
}

static void join(char *dest, size_t destsize, char *w[])
{
	int x;
	/* Join words into a string */
	if (!dest || destsize < 1) {
		return;
	}
	dest[0] = '\0';
	for (x=0;w[x];x++) {
		if (x)
			strncat(dest, " ", destsize - strlen(dest) - 1);
		strncat(dest, w[x], destsize - strlen(dest) - 1);
	}
}

static void join2(char *dest, size_t destsize, char *w[])
{
	int x;
	/* Join words into a string */
	if (!dest || destsize < 1) {
		return;
	}
	dest[0] = '\0';
	for (x=0;w[x];x++) {
		strncat(dest, w[x], destsize - strlen(dest) - 1);
	}
}

static char *find_best(char *argv[])
{
	static char cmdline[80];
	int x;
	/* See how close we get, then print the  */
	char *myargv[AST_MAX_CMD_LEN];
	for (x=0;x<AST_MAX_CMD_LEN;x++)
		myargv[x]=NULL;
	for (x=0;argv[x];x++) {
		myargv[x] = argv[x];
		if (!find_cli(myargv, -1))
			break;
	}
	join(cmdline, sizeof(cmdline), myargv);
	return cmdline;
}

int ast_cli_unregister(struct ast_cli_entry *e)
{
	struct ast_cli_entry *cur, *l=NULL;
	ast_mutex_lock(&clilock);
	cur = helpers;
	while(cur) {
		if (e == cur) {
			if (e->inuse) {
				ast_log(LOG_WARNING, "Can't remove command that is in use\n");
			} else {
				/* Rewrite */
				if (l)
					l->next = e->next;
				else
					helpers = e->next;
				e->next = NULL;
				break;
			}
		}
		l = cur;
		cur = cur->next;
	}
	ast_mutex_unlock(&clilock);
	return 0;
}

int ast_cli_register(struct ast_cli_entry *e)
{
	struct ast_cli_entry *cur, *l=NULL;
	char fulle[80] ="", fulltst[80] ="";
	static int len;
	ast_mutex_lock(&clilock);
	join2(fulle, sizeof(fulle), e->cmda);
	if (find_cli(e->cmda, -1)) {
		ast_mutex_unlock(&clilock);
		ast_log(LOG_WARNING, "Command '%s' already registered (or something close enough)\n", fulle);
		return -1;
	}
	cur = helpers;
	while(cur) {
		join2(fulltst, sizeof(fulltst), cur->cmda);
		len = strlen(fulltst);
		if (strlen(fulle) < len)
			len = strlen(fulle);
		if (strncasecmp(fulle, fulltst, len) < 0) {
			if (l) {
				e->next = l->next;
				l->next = e;
			} else {
				e->next = helpers;
				helpers = e;
			}
			break;
		}
		l = cur;
		cur = cur->next;
	}
	if (!cur) {
		if (l)
			l->next = e;
		else
			helpers = e;
		e->next = NULL;
	}
	ast_mutex_unlock(&clilock);
	return 0;
}

/*
 * register/unregister an array of entries.
 */
void ast_cli_register_multiple(struct ast_cli_entry *e, int len)
{
	int i;

	for (i=0; i < len; i++)
		ast_cli_register(e + i);
}

void ast_cli_unregister_multiple(struct ast_cli_entry *e, int len)
{
	int i;

	for (i=0; i < len; i++)
		ast_cli_unregister(e + i);
}

static int help_workhorse(int fd, char *match[])
{
	char fullcmd1[80] = "";
	char fullcmd2[80] = "";
	char matchstr[80];
	char *fullcmd = NULL;
	struct ast_cli_entry *e, *e1, *e2;
	e1 = builtins;
	e2 = helpers;
	if (match)
		join(matchstr, sizeof(matchstr), match);
	while(e1->cmda[0] || e2) {
		if (e2)
			join(fullcmd2, sizeof(fullcmd2), e2->cmda);
		if (e1->cmda[0])
			join(fullcmd1, sizeof(fullcmd1), e1->cmda);
		if (!e1->cmda[0] || 
				(e2 && (strcmp(fullcmd2, fullcmd1) < 0))) {
			/* Use e2 */
			e = e2;
			fullcmd = fullcmd2;
			/* Increment by going to next */
			e2 = e2->next;
		} else {
			/* Use e1 */
			e = e1;
			fullcmd = fullcmd1;
			e1++;
		}
		/* Hide commands that start with '_' */
		if (fullcmd[0] == '_')
			continue;
		if (match) {
			if (strncasecmp(matchstr, fullcmd, strlen(matchstr))) {
				continue;
			}
		}
		ast_cli(fd, "%25.25s  %s\n", fullcmd, e->summary);
	}
	return 0;
}

static int handle_help(int fd, int argc, char *argv[]) {
	struct ast_cli_entry *e;
	char fullcmd[80];
	if ((argc < 1))
		return RESULT_SHOWUSAGE;
	if (argc > 1) {
		e = find_cli(argv + 1, 1);
		if (e) {
			if (e->usage)
				ast_cli(fd, "%s", e->usage);
			else {
				join(fullcmd, sizeof(fullcmd), argv+1);
				ast_cli(fd, "No help text available for '%s'.\n", fullcmd);
			}
		} else {
			if (find_cli(argv + 1, -1)) {
				return help_workhorse(fd, argv + 1);
			} else {
				join(fullcmd, sizeof(fullcmd), argv+1);
				ast_cli(fd, "No such command '%s'.\n", fullcmd);
			}
		}
	} else {
		return help_workhorse(fd, NULL);
	}
	return RESULT_SUCCESS;
}

static char *parse_args(char *s, int *argc, char *argv[], int max)
{
	char *dup, *cur;
	int x = 0;
	int quoted = 0;
	int escaped = 0;
	int whitespace = 1;

	if (!(dup = strdup(s)))
		return NULL;

	cur = dup;
	while (*s) {
		if ((*s == '"') && !escaped) {
			quoted = !quoted;
			if (quoted & whitespace) {
				/* If we're starting a quoted string, coming off white space, start a new argument */
				if (x >= (max - 1)) {
					ast_log(LOG_WARNING, "Too many arguments, truncating\n");
					break;
				}
				argv[x++] = cur;
				whitespace = 0;
			}
			escaped = 0;
		} else if (((*s == ' ') || (*s == '\t')) && !(quoted || escaped)) {
			/* If we are not already in whitespace, and not in a quoted string or
			   processing an escape sequence, and just entered whitespace, then
			   finalize the previous argument and remember that we are in whitespace
			*/
			if (!whitespace) {
				*(cur++) = '\0';
				whitespace = 1;
			}
		} else if ((*s == '\\') && !escaped) {
			escaped = 1;
		} else {
			if (whitespace) {
				/* If we are coming out of whitespace, start a new argument */
				if (x >= (max - 1)) {
					ast_log(LOG_WARNING, "Too many arguments, truncating\n");
					break;
				}
				argv[x++] = cur;
				whitespace = 0;
			}
			*(cur++) = *s;
			escaped = 0;
		}
		s++;
	}
	/* Null terminate */
	*(cur++) = '\0';
	argv[x] = NULL;
	*argc = x;

	return dup;
}

/* This returns the number of unique matches for the generator */
int ast_cli_generatornummatches(char *text, char *word)
{
	int matches = 0, i = 0;
	char *buf = NULL, *oldbuf = NULL;

	while ( (buf = ast_cli_generator(text, word, i)) ) {
		if (++i > 1 && strcmp(buf,oldbuf) == 0)  {
				continue;
		}
		oldbuf = buf;
		matches++;
	}

	return matches;
}

char **ast_cli_completion_matches(char *text, char *word)
{
	char **match_list = NULL, *retstr, *prevstr;
	size_t match_list_len, max_equal, which, i;
	int matches = 0;

	match_list_len = 1;
	while ((retstr = ast_cli_generator(text, word, matches)) != NULL) {
		if (matches + 1 >= match_list_len) {
			match_list_len <<= 1;
			match_list = realloc(match_list, match_list_len * sizeof(char *));
		}
		match_list[++matches] = retstr;
	}

	if (!match_list)
		return (char **) NULL;

	which = 2;
	prevstr = match_list[1];
	max_equal = strlen(prevstr);
	for (; which <= matches; which++) {
		for (i = 0; i < max_equal && toupper(prevstr[i]) == toupper(match_list[which][i]); i++)
			continue;
		max_equal = i;
	}

	retstr = malloc(max_equal + 1);
	(void) strncpy(retstr, match_list[1], max_equal);
	retstr[max_equal] = '\0';
	match_list[0] = retstr;

	if (matches + 1 >= match_list_len)
		match_list = realloc(match_list, (match_list_len + 1) * sizeof(char *));
	match_list[matches + 1] = (char *) NULL;

	return (match_list);
}

static char *__ast_cli_generator(char *text, char *word, int state, int lock)
{
	char *argv[AST_MAX_ARGS];
	struct ast_cli_entry *e, *e1, *e2;
	int x;
	int matchnum=0;
	char *dup, *res;
	char fullcmd1[80] = "";
	char fullcmd2[80] = "";
	char matchstr[80] = "";
	char *fullcmd = NULL;

	if ((dup = parse_args(text, &x, argv, sizeof(argv) / sizeof(argv[0])))) {
		join(matchstr, sizeof(matchstr), argv);
		if (lock)
			ast_mutex_lock(&clilock);
		e1 = builtins;
		e2 = helpers;
		while(e1->cmda[0] || e2) {
			if (e2)
				join(fullcmd2, sizeof(fullcmd2), e2->cmda);
			if (e1->cmda[0])
				join(fullcmd1, sizeof(fullcmd1), e1->cmda);
			if (!e1->cmda[0] || 
					(e2 && (strcmp(fullcmd2, fullcmd1) < 0))) {
				/* Use e2 */
				e = e2;
				fullcmd = fullcmd2;
				/* Increment by going to next */
				e2 = e2->next;
			} else {
				/* Use e1 */
				e = e1;
				fullcmd = fullcmd1;
				e1++;
			}
			if ((fullcmd[0] != '_') && !strncasecmp(matchstr, fullcmd, strlen(matchstr))) {
				/* We contain the first part of one or more commands */
				/* Now, what we're supposed to return is the next word... */
				if (!ast_strlen_zero(word) && x>0) {
					res = e->cmda[x-1];
				} else {
					res = e->cmda[x];
				}
				if (res) {
					matchnum++;
					if (matchnum > state) {
						if (lock)
							ast_mutex_unlock(&clilock);
						free(dup);
						return strdup(res);
					}
				}
			}
			if (e->generator && !strncasecmp(matchstr, fullcmd, strlen(fullcmd)) &&
				(matchstr[strlen(fullcmd)] < 33)) {
				/* We have a command in its entirity within us -- theoretically only one
				   command can have this occur */
				fullcmd = e->generator(matchstr, word, (!ast_strlen_zero(word) ? (x - 1) : (x)), state);
				if (fullcmd) {
					if (lock)
						ast_mutex_unlock(&clilock);
					free(dup);
					return fullcmd;
				}
			}
			
		}
		if (lock)
			ast_mutex_unlock(&clilock);
		free(dup);
	}
	return NULL;
}

char *ast_cli_generator(char *text, char *word, int state)
{
	return __ast_cli_generator(text, word, state, 1);
}

int ast_cli_command(int fd, char *s)
{
	char *argv[AST_MAX_ARGS];
	struct ast_cli_entry *e;
	int x;
	char *dup;

	if ((dup = parse_args(s, &x, argv, sizeof(argv) / sizeof(argv[0])))) {
		/* We need at least one entry, or ignore */
		if (x > 0) {
			ast_mutex_lock(&clilock);
			e = find_cli(argv, 0);
			if (e)
				e->inuse++;
			ast_mutex_unlock(&clilock);
			if (e) {
				switch(e->handler(fd, x, argv)) {
				case RESULT_SHOWUSAGE:
					ast_cli(fd, "%s", e->usage);
					break;
				}
			} else 
				ast_cli(fd, "No such command '%s' (type 'help' for help)\n", find_best(argv));
			if (e) {
				ast_mutex_lock(&clilock);
				e->inuse--;
				ast_mutex_unlock(&clilock);
			}
		}
		free(dup);
	} else {
		ast_log(LOG_WARNING, "Out of memory\n");	
		return -1;
	}
	return 0;
}
