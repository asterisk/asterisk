/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Standard Command Line Interface
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <unistd.h>
#include <stdlib.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/module.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/manager.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <sys/signal.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
/* For rl_filename_completion */
#include "editline/readline/readline.h"
/* For module directory */
#include "asterisk.h"
#include "build.h"
#include "astconf.h"

#define VERSION_INFO "Asterisk " ASTERISK_VERSION " built by " BUILD_USER "@" BUILD_HOSTNAME \
	" on a " BUILD_MACHINE " running " BUILD_OS
	
void ast_cli(int fd, char *fmt, ...)
{
	char *stuff;
	va_list ap;
	va_start(ap, fmt);
	vasprintf(&stuff, fmt, ap);
	va_end(ap);
	ast_carefulwrite(fd, stuff, strlen(stuff), 100);
	free(stuff);
}

AST_MUTEX_DEFINE_STATIC(clilock);

struct ast_cli_entry *helpers = NULL;

static char load_help[] = 
"Usage: load <module name>\n"
"       Loads the specified module into Asterisk.\n";

static char unload_help[] = 
"Usage: unload [-f|-h] <module name>\n"
"       Unloads the specified module from Asterisk.  The -f\n"
"       option causes the module to be unloaded even if it is\n"
"       in use (may cause a crash) and the -h module causes the\n"
"       module to be unloaded even if the module says it cannot, \n"
"       which almost always will cause a crash.\n";

static char help_help[] =
"Usage: help [topic]\n"
"       When called with a topic as an argument, displays usage\n"
"       information on the given command.  If called without a\n"
"       topic, it provides a list of commands.\n";

static char chanlist_help[] = 
"Usage: show channels [concise]\n"
"       Lists currently defined channels and some information about\n"
"       them.  If 'concise' is specified, format is abridged and in\n"
"       a more easily machine parsable format\n";

static char reload_help[] = 
"Usage: reload\n"
"       Reloads configuration files for all modules which support\n"
"       reloading.\n";

static char set_verbose_help[] = 
"Usage: set verbose <level>\n"
"       Sets level of verbose messages to be displayed.  0 means\n"
"       no messages should be displayed.\n";

static char softhangup_help[] =
"Usage: soft hangup <channel>\n"
"       Request that a channel be hung up.  The hangup takes effect\n"
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
	if (argc != 1)
		return RESULT_SHOWUSAGE;
	ast_module_reload();
	return RESULT_SUCCESS;
}

static int handle_set_verbose(int fd, int argc, char *argv[])
{
	int val;
	/* Has a hidden 'at least' argument */
	if ((argc != 3) && (argc != 4))
		return RESULT_SHOWUSAGE;
	if ((argc == 4) && strcasecmp(argv[2], "atleast"))
		return RESULT_SHOWUSAGE;
	if (argc == 3)
		option_verbose = atoi(argv[2]);
	else {
		val = atoi(argv[3]);
		if (val > option_verbose)
			option_verbose = val;
	}
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

#define MODLIST_FORMAT  "%-25s %-40.40s %-10d\n"
#define MODLIST_FORMAT2 "%-25s %-40.40s %-10s\n"

AST_MUTEX_DEFINE_STATIC(climodentrylock);
static int climodentryfd = -1;

static int modlist_modentry(char *module, char *description, int usecnt)
{
	ast_cli(climodentryfd, MODLIST_FORMAT, module, description, usecnt);
	return 0;
}

static char modlist_help[] =
"Usage: show modules\n"
"       Shows Asterisk modules currently in use, and usage "
"statistics.\n";

static char version_help[] =
"Usage: show version\n"
"       Shows Asterisk version information.\n ";

static char *format_uptimestr(time_t timeval)
{
	int years = 0, weeks = 0, days = 0, hours = 0, mins = 0, secs = 0;
	char timestr[256];
	int pos = 0;
#define SECOND (1)
#define MINUTE (SECOND*60)
#define HOUR (MINUTE*60)
#define DAY (HOUR*24)
#define WEEK (DAY*7)
#define YEAR (DAY*365)

	if (timeval < 0)
		return NULL;
	if (timeval > YEAR) {
		years = (timeval / YEAR);
		timeval -= (years * YEAR);
		if (years > 1)
			pos += sprintf(timestr + pos, "%d years, ", years);
		else
			pos += sprintf(timestr + pos, "1 year, ");
	}
	if (timeval > WEEK) {
		weeks = (timeval / WEEK);
		timeval -= (weeks * WEEK);
		if (weeks > 1)
			pos += sprintf(timestr + pos, "%d weeks, ", weeks);
		else
			pos += sprintf(timestr + pos, "1 week, ");
	}
	if (timeval > DAY) {
		days = (timeval / DAY);
		timeval -= (days * DAY);
		if (days > 1)
			pos += sprintf(timestr + pos, "%d days, ", days);
		else
			pos += sprintf(timestr + pos, "1 day, ");

	}
	if (timeval > HOUR) {
		hours = (timeval / HOUR);
		timeval -= (hours * HOUR);
		if (hours > 1)
			pos += sprintf(timestr + pos, "%d hours, ", hours);
		else
			pos += sprintf(timestr + pos, "1 hour, ");
	}
	if (timeval > MINUTE) {
		mins = (timeval / MINUTE);
		timeval -= (mins * MINUTE);
		if (mins > 1)
			pos += sprintf(timestr + pos, "%d minutes, ", mins);
		else if (mins > 0)
			pos += sprintf(timestr + pos, "1 minute, ");
	}
	secs = timeval;

	if (secs > 0)
		pos += sprintf(timestr + pos, "%d seconds", secs);

	return timestr ? strdup(timestr) : NULL;
}

static int handle_showuptime(int fd, int argc, char *argv[])
{
	time_t curtime, tmptime;
	char *timestr;

	time(&curtime);
	if (ast_startuptime) {
		tmptime = curtime - ast_startuptime;
		timestr = format_uptimestr(tmptime);
		if (timestr) {
			ast_cli(fd, "System uptime: %s\n", timestr);
			free(timestr);
		}
	}		
	if (ast_lastreloadtime) {
		tmptime = curtime - ast_lastreloadtime;
		timestr = format_uptimestr(tmptime);
		if (timestr) {
			ast_cli(fd, "Last reload: %s\n", timestr);
			free(timestr);
		}
	}
	return RESULT_SUCCESS;
}

static int handle_modlist(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&climodentrylock);
	climodentryfd = fd;
	ast_cli(fd, MODLIST_FORMAT2, "Module", "Description", "Use Count");
	ast_update_module_list(modlist_modentry);
	climodentryfd = -1;
	ast_mutex_unlock(&climodentrylock);
	return RESULT_SUCCESS;
}

static int handle_version(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, "%s\n", VERSION_INFO);
	return RESULT_SUCCESS;
}
static int handle_chanlist(int fd, int argc, char *argv[])
{
#define FORMAT_STRING  "%15s  (%-10s %-12s %-4d) %7s %-12s  %-15s\n"
#define FORMAT_STRING2 "%15s  (%-10s %-12s %-4s) %7s %-12s  %-15s\n"
#define CONCISE_FORMAT_STRING  "%s:%s:%s:%d:%s:%s:%s:%s:%s:%d\n"

	struct ast_channel *c=NULL;
	int numchans = 0;
	int concise = 0;
	if (argc < 2 || argc > 3)
		return RESULT_SHOWUSAGE;
	
	concise = (argc == 3 && (!strcasecmp(argv[2],"concise")));
	c = ast_channel_walk_locked(NULL);
	if(!concise)
		ast_cli(fd, FORMAT_STRING2, "Channel", "Context", "Extension", "Pri", "State", "Appl.", "Data");
	while(c) {
		if(concise)
			ast_cli(fd, CONCISE_FORMAT_STRING, c->name, c->context, c->exten, c->priority, ast_state2str(c->_state),
					c->appl ? c->appl : "(None)", c->data ? ( !ast_strlen_zero(c->data) ? c->data : "" ): "",
					(c->callerid && !ast_strlen_zero(c->callerid)) ? c->callerid : "",
					(c->accountcode && !ast_strlen_zero(c->accountcode)) ? c->accountcode : "",c->amaflags);
		else
			ast_cli(fd, FORMAT_STRING, c->name, c->context, c->exten, c->priority, ast_state2str(c->_state),
					c->appl ? c->appl : "(None)", c->data ? ( !ast_strlen_zero(c->data) ? c->data : "(Empty)" ): "(None)");

		numchans++;
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if(!concise)
		ast_cli(fd, "%d active channel(s)\n", numchans);
	return RESULT_SUCCESS;
}

static char showchan_help[] = 
"Usage: show channel <channel>\n"
"       Shows lots of information about the specified channel.\n";

static char debugchan_help[] = 
"Usage: debug channel <channel>\n"
"       Enables debugging on a specific channel.\n";

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
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strcasecmp(c->name, argv[2])) {
			ast_cli(fd, "Requested Hangup on channel '%s'\n", c->name);
			ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
			ast_mutex_unlock(&c->lock);
			break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (!c) 
		ast_cli(fd, "%s is not a known channel\n", argv[2]);
	return RESULT_SUCCESS;
}

static char *__ast_cli_generator(char *text, char *word, int state, int lock);

static int handle_commandmatchesarray(int fd, int argc, char *argv[])
{
	char *buf;
	int buflen = 2048;
	int len = 0;
	char **matches;
	int x;

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
			if (len + strlen(matches[x]) >= buflen) {
				buflen += strlen(matches[x]) * 3;
				buf = realloc(buf, buflen);
			}
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

static int handle_debugchan(int fd, int argc, char *argv[])
{
	struct ast_channel *c=NULL;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strcasecmp(c->name, argv[2])) {
			c->fin |= 0x80000000;
			c->fout |= 0x80000000;
			break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (c) {
		ast_cli(fd, "Debugging enabled on channel %s\n", c->name);
		ast_mutex_unlock(&c->lock);
	}
	else
		ast_cli(fd, "No such channel %s\n", argv[2]);
	return RESULT_SUCCESS;
}

static int handle_nodebugchan(int fd, int argc, char *argv[])
{
	struct ast_channel *c=NULL;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strcasecmp(c->name, argv[3])) {
			c->fin &= 0x7fffffff;
			c->fout &= 0x7fffffff;
			break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (c) {
		ast_cli(fd, "Debugging disabled on channel %s\n", c->name);
		ast_mutex_unlock(&c->lock);
	} else
		ast_cli(fd, "No such channel %s\n", argv[2]);
	return RESULT_SUCCESS;
}
		
	

static int handle_showchan(int fd, int argc, char *argv[])
{
	struct ast_channel *c=NULL;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strcasecmp(c->name, argv[2])) {
			ast_cli(fd, 
	" -- General --\n"
	"           Name: %s\n"
	"           Type: %s\n"
	"       UniqueID: %s\n"
	"      Caller ID: %s\n"
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
	" --   PBX   --\n"
	"        Context: %s\n"
	"      Extension: %s\n"
	"       Priority: %d\n"
	"     Call Group: %d\n"
	"   Pickup Group: %d\n"
	"    Application: %s\n"
	"           Data: %s\n"
	"          Stack: %d\n"
	"    Blocking in: %s\n",
	c->name, c->type, c->uniqueid,
	(c->callerid ? c->callerid : "(N/A)"),
	(c->dnid ? c->dnid : "(N/A)" ), ast_state2str(c->_state), c->_state, c->rings, c->nativeformats, c->writeformat, c->readformat,
	c->fds[0], c->fin & 0x7fffffff, (c->fin & 0x80000000) ? " (DEBUGGED)" : "",
	c->fout & 0x7fffffff, (c->fout & 0x80000000) ? " (DEBUGGED)" : "", (long)c->whentohangup,
	c->context, c->exten, c->priority, c->callgroup, c->pickupgroup, ( c->appl ? c->appl : "(N/A)" ),
	( c-> data ? (!ast_strlen_zero(c->data) ? c->data : "(Empty)") : "(None)"),
	c->stack, (c->blocking ? c->blockproc : "(Not Blocking)"));
		ast_mutex_unlock(&c->lock);
		break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (!c) 
		ast_cli(fd, "%s is not a known channel\n", argv[2]);
	return RESULT_SUCCESS;
}

static char *complete_ch(char *line, char *word, int pos, int state)
{
	struct ast_channel *c;
	int which=0;
	char *ret;
	c = ast_channel_walk_locked(NULL);
	while(c) {
		if (!strncasecmp(word, c->name, strlen(word))) {
			if (++which > state)
				break;
		}
		ast_mutex_unlock(&c->lock);
		c = ast_channel_walk_locked(c);
	}
	if (c) {
		ret = strdup(c->name);
		ast_mutex_unlock(&c->lock);
	} else
		ret = NULL;
	return ret;
}

static char *complete_fn(char *line, char *word, int pos, int state)
{
	char *c;
	char filename[256];
	if (pos != 1)
		return NULL;
	if (word[0] == '/')
		strncpy(filename, word, sizeof(filename)-1);
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
	{ { "debug", "channel", NULL }, handle_debugchan, "Enable debugging on a channel", debugchan_help, complete_ch },
	{ { "help", NULL }, handle_help, "Display help list, or specific help on a command", help_help },
	{ { "load", NULL }, handle_load, "Load a dynamic module by name", load_help, complete_fn },
	{ { "no", "debug", "channel", NULL }, handle_nodebugchan, "Disable debugging on a channel", nodebugchan_help, complete_ch },
	{ { "reload", NULL }, handle_reload, "Reload configuration", reload_help },
	{ { "set", "verbose", NULL }, handle_set_verbose, "Set level of verboseness", set_verbose_help },
	{ { "show", "channels", NULL }, handle_chanlist, "Display information on channels", chanlist_help },
	{ { "show", "channel", NULL }, handle_showchan, "Display information on a specific channel", showchan_help, complete_ch },
	{ { "show", "modules", NULL }, handle_modlist, "List modules and info", modlist_help },
	{ { "show", "uptime", NULL }, handle_showuptime, "Show uptime information", modlist_help },
	{ { "show", "version", NULL }, handle_version, "Display version info", version_help },
	{ { "soft", "hangup", NULL }, handle_softhangup, "Request a hangup on a given channel", softhangup_help, complete_ch },
	{ { "unload", NULL }, handle_unload, "Unload a dynamic module by name", unload_help, complete_fn },
	{ { NULL }, NULL, NULL, NULL }
};

static struct ast_cli_entry *find_cli(char *cmds[], int exact)
{
	int x;
	int y;
	int match;
	struct ast_cli_entry *e=NULL;
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
	return e;
}

static void join(char *s, int len, char *w[])
{
	int x;
	/* Join words into a string */
	strcpy(s, "");
	for (x=0;w[x];x++) {
		if (x)
			strncat(s, " ", len - strlen(s));
		strncat(s, w[x], len - strlen(s));
	}
}

static void join2(char *s, int len, char *w[])
{
	int x;
	/* Join words into a string */
	strcpy(s, "");
	for (x=0;w[x];x++) {
		strncat(s, w[x], len - strlen(s));
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

static int help_workhorse(int fd, char *match[])
{
	char fullcmd1[80];
	char fullcmd2[80];
	char matchstr[80];
	char *fullcmd;
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
		if (e) 
			ast_cli(fd, e->usage);
		else {
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

static char *parse_args(char *s, int *max, char *argv[])
{
	char *dup, *cur;
	int x=0;
	int quoted=0;
	int escaped=0;
	int whitespace=1;

	dup = strdup(s);
	if (dup) {
		cur = dup;
		while(*s) {
			switch(*s) {
			case '"':
				/* If it's escaped, put a literal quote */
				if (escaped) 
					goto normal;
				else 
					quoted = !quoted;
				if (quoted && whitespace) {
					/* If we're starting a quote, coming off white space start a new word, too */
					argv[x++] = cur;
					whitespace=0;
				}
				escaped = 0;
				break;
			case ' ':
			case '\t':
				if (!quoted && !escaped) {
					/* If we're not quoted, mark this as whitespace, and
					   end the previous argument */
					whitespace = 1;
					*(cur++) = '\0';
				} else
					/* Otherwise, just treat it as anything else */ 
					goto normal;
				break;
			case '\\':
				/* If we're escaped, print a literal, otherwise enable escaping */
				if (escaped) {
					goto normal;
				} else {
					escaped=1;
				}
				break;
			default:
normal:
				if (whitespace) {
					if (x >= AST_MAX_ARGS -1) {
						ast_log(LOG_WARNING, "Too many arguments, truncating\n");
						break;
					}
					/* Coming off of whitespace, start the next argument */
					argv[x++] = cur;
					whitespace=0;
				}
				*(cur++) = *s;
				escaped=0;
			}
			s++;
		}
		/* Null terminate */
		*(cur++) = '\0';
		argv[x] = NULL;
		*max = x;
	}
	return dup;
}

/* This returns the number of unique matches for the generator */
int ast_cli_generatornummatches(char *text, char *word)
{
	int matches = 0, i = 0;
	char *buf, *oldbuf = NULL;


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
		for (i = 0; i < max_equal && prevstr[i] == match_list[which][i]; i++)
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
	char fullcmd1[80];
	char fullcmd2[80];
	char matchstr[80];
	char *fullcmd;

	if ((dup = parse_args(text, &x, argv))) {
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
			if ((fullcmd[0] != '_') && !strncasecmp(text, fullcmd, strlen(text))) {
				/* We contain the first part of one or more commands */
				matchnum++;
				if (matchnum > state) {
					/* Now, what we're supposed to return is the next word... */
					if (!ast_strlen_zero(word) && x>0) {
						res = e->cmda[x-1];
					} else {
						res = e->cmda[x];
					}
					if (res) {
						if (lock)
							ast_mutex_unlock(&clilock);
						free(dup);
						return res ? strdup(res) : NULL;
					}
				}
			}
			if (e->generator && !strncasecmp(matchstr, fullcmd, strlen(fullcmd))) {
				/* We have a command in its entirity within us -- theoretically only one
				   command can have this occur */
				fullcmd = e->generator(text, word, (!ast_strlen_zero(word) ? (x - 1) : (x)), state);
				if (lock)
					ast_mutex_unlock(&clilock);
				free(dup);
				return fullcmd;
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
	x = AST_MAX_ARGS;
	if ((dup = parse_args(s, &x, argv))) {
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
					ast_cli(fd, e->usage);
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
