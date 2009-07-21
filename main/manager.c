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
 * \brief The Asterisk Management Interface - AMI
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * Channel Management and more
 * 
 * \ref amiconf
 */

/*! \addtogroup Group_AMI AMI functions 
*/
/*! @{ 
 Doxygen group */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/manager.h"
#include "asterisk/config.h"
#include "asterisk/callerid.h"
#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/md5.h"
#include "asterisk/acl.h"
#include "asterisk/utils.h"
#include "asterisk/http.h"
#include "asterisk/threadstorage.h"
#include "asterisk/linkedlists.h"
#include "asterisk/term.h"
#include "asterisk/astobj2.h"

struct fast_originate_helper {
	char tech[AST_MAX_EXTENSION];
	/*! data can contain a channel name, extension number, username, password, etc. */
	char data[512];
	int timeout;
	int format;
	char app[AST_MAX_APP];
	char appdata[AST_MAX_EXTENSION];
	char cid_name[AST_MAX_EXTENSION];
	char cid_num[AST_MAX_EXTENSION];
	char context[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	char idtext[AST_MAX_EXTENSION];
	char account[AST_MAX_ACCOUNT_CODE];
	int priority;
	struct ast_variable *vars;
};

struct eventqent {
	int usecount;
	int category;
	struct eventqent *next;
	char eventdata[1];
};

static int enabled;
static int portno = DEFAULT_MANAGER_PORT;
static int asock = -1;
static int displayconnects = 1;
static int timestampevents;
static int httptimeout = 60;

static pthread_t t;
static int block_sockets;
static int num_sessions;

/* Protected by the sessions list lock */
struct eventqent *master_eventq = NULL;

AST_THREADSTORAGE(manager_event_buf, manager_event_buf_init);
#define MANAGER_EVENT_BUF_INITSIZE   256

AST_THREADSTORAGE(astman_append_buf, astman_append_buf_init);
#define ASTMAN_APPEND_BUF_INITSIZE   256

static struct permalias {
	int num;
	char *label;
} perms[] = {
	{ EVENT_FLAG_SYSTEM, "system" },
	{ EVENT_FLAG_CALL, "call" },
	{ EVENT_FLAG_LOG, "log" },
	{ EVENT_FLAG_VERBOSE, "verbose" },
	{ EVENT_FLAG_COMMAND, "command" },
	{ EVENT_FLAG_AGENT, "agent" },
	{ EVENT_FLAG_USER, "user" },
	{ EVENT_FLAG_CONFIG, "config" },
	{ -1, "all" },
	{ 0, "none" },
};

#define MAX_BLACKLIST_CMD_LEN 2
static struct {
	char *words[AST_MAX_CMD_LEN];
} command_blacklist[] = {
	{{ "module", "load", NULL }},
	{{ "module", "unload", NULL }},
	{{ "restart", "gracefully", NULL }},
};

/* In order to understand what the heck is going on with the
 * mansession_session and mansession structs, we need to have a bit of a history
 * lesson.
 *
 * In the beginning, there was the mansession. The mansession contained data that was
 * intrinsic to a manager session, such as the time that it started, the name of the logged-in
 * user, etc. In addition to these parameters were the f and fd parameters. For typical manager
 * sessions, these were used to represent the TCP socket over which the AMI session was taking
 * place. It makes perfect sense for these fields to be a part of the session-specific data since
 * the session actually defines this information.
 *
 * Then came the HTTP AMI sessions. With these, the f and fd fields need to be opened and closed
 * for every single action that occurs. Thus the f and fd fields aren't really specific to the session
 * but rather to the action that is being executed. Because a single session may execute many commands
 * at once, some sort of safety needed to be added in order to be sure that we did not end up with fd
 * leaks from one action overwriting the f and fd fields used by a previous action before the previous action
 * has had a chance to properly close its handles.
 *
 * The initial idea to solve this was to use thread synchronization, but this prevented multiple actions
 * from being run at the same time in a single session. Some manager actions may block for a long time, thus
 * creating a large queue of actions to execute. In addition, this fix did not address the basic architectural
 * issue that for HTTP manager sessions, the f and fd variables are not really a part of the session, but are
 * part of the action instead.
 *
 * The new idea was to create a structure on the stack for each HTTP Manager action. This structure would
 * contain the action-specific information, such as which file to write to. In order to maintain expectations
 * of action handlers and not have to change the public API of the manager code, we would need to name this
 * new stacked structure 'mansession' and contain within it the old mansession struct that we used to use.
 * We renamed the old mansession struct 'mansession_session' to hopefully convey that what is in this structure
 * is session-specific data. The structure that it is wrapped in, called a 'mansession' really contains action-specific
 * data.
 */
struct mansession_session {
	/*! Execution thread */
	pthread_t t;
	/*! Thread lock -- don't use in action callbacks, it's already taken care of  */
	ast_mutex_t __lock;
	/*! socket address */
	struct sockaddr_in sin;
	/*! TCP socket */
	int fd;
	/*! Whether an HTTP manager is in use */
	int inuse;
	/*! Whether an HTTP session should be destroyed */
	int needdestroy;
	/*! Whether an HTTP session has someone waiting on events */
	pthread_t waiting_thread;
	/*! Unique manager identifer */
	uint32_t managerid;
	/*! Session timeout if HTTP */
	time_t sessiontimeout;
	/*! Output from manager interface */
	struct ast_dynamic_str *outputstr;
	/*! Logged in username */
	char username[80];
	/*! Authentication challenge */
	char challenge[10];
	/*! Authentication status */
	int authenticated;
	/*! Authorization for reading */
	int readperm;
	/*! Authorization for writing */
	int writeperm;
	/*! Buffer */
	char inbuf[1024];
	int inlen;
	int send_events;
	int displaysystemname;		/*!< Add system name to manager responses and events */
	/* Queued events that we've not had the ability to send yet */
	struct eventqent *eventq;
	/* Timeout for ast_carefulwrite() */
	int writetimeout;
	int pending_event;         /*!< Pending events indicator in case when waiting_thread is NULL */
	AST_LIST_ENTRY(mansession_session) list;
};

/* In case you didn't read that giant block of text above the mansession_session struct, the
 * 'mansession' struct is named this solely to keep the API the same in Asterisk. This structure really
 * represents data that is different from Manager action to Manager action. The mansession_session pointer
 * contained within points to session-specific data.
 */
struct mansession {
	FILE *f;
	struct mansession_session *session;
	int fd;
};

static AST_LIST_HEAD_STATIC(sessions, mansession_session);

struct ast_manager_user {
	char username[80];
	char *secret;
	char *deny;
	char *permit;
	char *read;
	char *write;
	unsigned int displayconnects:1;
	int keep;
	AST_LIST_ENTRY(ast_manager_user) list;
};

static AST_LIST_HEAD_STATIC(users, ast_manager_user);

static struct manager_action *first_action;
AST_RWLOCK_DEFINE_STATIC(actionlock);

/*! \brief Convert authority code to string with serveral options */
static char *authority_to_str(int authority, char *res, int reslen)
{
	int running_total = 0, i;

	memset(res, 0, reslen);
	for (i = 0; i < (sizeof(perms) / sizeof(perms[0])) - 1; i++) {
		if (authority & perms[i].num) {
			if (*res) {
				strncat(res, ",", (reslen > running_total) ? reslen - running_total - 1 : 0);
				running_total++;
			}
			strncat(res, perms[i].label, (reslen > running_total) ? reslen - running_total - 1 : 0);
			running_total += strlen(perms[i].label);
		}
	}

	if (ast_strlen_zero(res))
		ast_copy_string(res, "<none>", reslen);
	
	return res;
}

static char *complete_show_mancmd(const char *line, const char *word, int pos, int state)
{
	struct manager_action *cur;
	int which = 0;
	char *ret = NULL;

	ast_rwlock_rdlock(&actionlock);
	for (cur = first_action; cur; cur = cur->next) { /* Walk the list of actions */
		if (!strncasecmp(word, cur->action, strlen(word)) && ++which > state) {
			ret = ast_strdup(cur->action);
			break;	/* make sure we exit even if ast_strdup() returns NULL */
		}
	}
	ast_rwlock_unlock(&actionlock);

	return ret;
}

static void xml_copy_escape(char **dst, size_t *maxlen, const char *src, int lower)
{
	while (*src && (*maxlen > 6)) {
		switch (*src) {
		case '<':
			strcpy(*dst, "&lt;");
			(*dst) += 4;
			*maxlen -= 4;
			break;
		case '>':
			strcpy(*dst, "&gt;");
			(*dst) += 4;
			*maxlen -= 4;
			break;
		case '\"':
			strcpy(*dst, "&quot;");
			(*dst) += 6;
			*maxlen -= 6;
			break;
		case '\'':
			strcpy(*dst, "&apos;");
			(*dst) += 6;
			*maxlen -= 6;
			break;
		case '&':
			strcpy(*dst, "&amp;");
			(*dst) += 5;
			*maxlen -= 5;
			break;		
		default:
			*(*dst)++ = lower ? tolower(*src) : *src;
			(*maxlen)--;
		}
		src++;
	}
}

struct variable_count {
	char *varname;
	int count;
};

static int compress_char(char c)
{
	c &= 0x7f;
	if (c < 32)
		return 0;
	else if (c >= 'a' && c <= 'z')
		return c - 64;
	else if (c > 'z')
		return '_';
	else
		return c - 32;
}

static int variable_count_hash_fn(const void *vvc, const int flags)
{
	const struct variable_count *vc = vvc;
	int res = 0, i;
	for (i = 0; i < 5; i++) {
		if (vc->varname[i] == '\0')
			break;
		res += compress_char(vc->varname[i]) << (i * 6);
	}
	return res;
}

static int variable_count_cmp_fn(void *obj, void *vstr, int flags)
{
	/* Due to the simplicity of struct variable_count, it makes no difference
	 * if you pass in objects or strings, the same operation applies. This is
	 * due to the fact that the hash occurs on the first element, which means
	 * the address of both the struct and the string are exactly the same. */
	struct variable_count *vc = obj;
	char *str = vstr;
	return !strcmp(vc->varname, str) ? CMP_MATCH | CMP_STOP : 0;
}

static char *xml_translate(char *in, struct ast_variable *vars)
{
	struct ast_variable *v;
	char *dest = NULL;
	char *out, *tmp, *var, *val;
	char *objtype = NULL;
	int colons = 0;
	int breaks = 0;
	size_t len;
	int count = 1;
	int escaped = 0;
	int inobj = 0;
	int x;
	struct variable_count *vc = NULL;
	struct ao2_container *vco = NULL;

	for (v = vars; v; v = v->next) {
		if (!dest && !strcasecmp(v->name, "ajaxdest"))
			dest = v->value;
		else if (!objtype && !strcasecmp(v->name, "ajaxobjtype")) 
			objtype = v->value;
	}
	if (!dest)
		dest = "unknown";
	if (!objtype)
		objtype = "generic";
	for (x = 0; in[x]; x++) {
		if (in[x] == ':')
			colons++;
		else if (in[x] == '\n')
			breaks++;
		else if (strchr("&\"<>\'", in[x]))
			escaped++;
	}
	len = (size_t) (strlen(in) + colons * 5 + breaks * (40 + strlen(dest) + strlen(objtype)) + escaped * 10); /* foo="bar", "<response type=\"object\" id=\"dest\"", "&amp;" */
	out = ast_malloc(len);
	if (!out)
		return 0;
	tmp = out;
	while (*in) {
		var = in;
		while (*in && (*in >= 32))
			in++;
		if (*in) {
			if ((count > 3) && inobj) {
				ast_build_string(&tmp, &len, " /></response>\n");
				inobj = 0;

				/* Entity is closed, so close out the name cache */
				ao2_ref(vco, -1);
				vco = NULL;
			}
			count = 0;
			while (*in && (*in < 32)) {
				*in = '\0';
				in++;
				count++;
			}
			val = strchr(var, ':');
			if (val) {
				*val = '\0';
				val++;
				if (*val == ' ')
					val++;
				if (!inobj) {
					vco = ao2_container_alloc(37, variable_count_hash_fn, variable_count_cmp_fn);
					ast_build_string(&tmp, &len, "<response type='object' id='%s'><%s", dest, objtype);
					inobj = 1;
				}

				/* Check if the var has been used already */
				if ((vc = ao2_find(vco, var, 0)))
					vc->count++;
				else {
					/* Create a new entry for this one */
					vc = ao2_alloc(sizeof(*vc), NULL);
					vc->varname = var;
					vc->count = 1;
					ao2_link(vco, vc);
				}

				ast_build_string(&tmp, &len, " ");
				xml_copy_escape(&tmp, &len, var, 1);
				if (vc->count > 1)
					ast_build_string(&tmp, &len, "-%d", vc->count);
				ast_build_string(&tmp, &len, "='");
				xml_copy_escape(&tmp, &len, val, 0);
				ast_build_string(&tmp, &len, "'");
				ao2_ref(vc, -1);
			}
		}
	}
	if (inobj)
		ast_build_string(&tmp, &len, " /></response>\n");
	if (vco)
		ao2_ref(vco, -1);
	return out;
}

static char *html_translate(char *in)
{
	int x;
	int colons = 0;
	int breaks = 0;
	size_t len;
	int count = 1;
	char *tmp, *var, *val, *out;

	for (x=0; in[x]; x++) {
		if (in[x] == ':')
			colons++;
		if (in[x] == '\n')
			breaks++;
	}
	len = strlen(in) + colons * 40 + breaks * 40; /* <tr><td></td><td></td></tr>, "<tr><td colspan=\"2\"><hr></td></tr> */
	out = ast_malloc(len);
	if (!out)
		return 0;
	tmp = out;
	while (*in) {
		var = in;
		while (*in && (*in >= 32))
			in++;
		if (*in) {
			if ((count % 4) == 0){
				ast_build_string(&tmp, &len, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");
			}
			count = 0;
			while (*in && (*in < 32)) {
				*in = '\0';
				in++;
				count++;
			}
			val = strchr(var, ':');
			if (val) {
				*val = '\0';
				val++;
				if (*val == ' ')
					val++;
				ast_build_string(&tmp, &len, "<tr><td>%s</td><td>%s</td></tr>\r\n", var, val);
			}
		}
	}
	return out;
}



static struct ast_manager_user *ast_get_manager_by_name_locked(const char *name)
{
	struct ast_manager_user *user = NULL;

	AST_LIST_TRAVERSE(&users, user, list)
		if (!strcasecmp(user->username, name))
			break;
	return user;
}

void astman_append(struct mansession *s, const char *fmt, ...)
{
	va_list ap;
	struct ast_dynamic_str *buf;

	ast_mutex_lock(&s->session->__lock);

	if (!(buf = ast_dynamic_str_thread_get(&astman_append_buf, ASTMAN_APPEND_BUF_INITSIZE))) {
		ast_mutex_unlock(&s->session->__lock);
		return;
	}

	va_start(ap, fmt);
	ast_dynamic_str_thread_set_va(&buf, 0, &astman_append_buf, fmt, ap);
	va_end(ap);
	
	if (s->fd > -1)
		ast_carefulwrite(s->fd, buf->str, strlen(buf->str), s->session->writetimeout);
	else {
		if (!s->session->outputstr && !(s->session->outputstr = ast_calloc(1, sizeof(*s->session->outputstr)))) {
			ast_mutex_unlock(&s->session->__lock);
			return;
		}

		ast_dynamic_str_append(&s->session->outputstr, 0, "%s", buf->str);	
	}

	ast_mutex_unlock(&s->session->__lock);
}

static int handle_showmancmd(int fd, int argc, char *argv[])
{
	struct manager_action *cur;
	char authority[80];
	int num;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	ast_rwlock_rdlock(&actionlock);
	for (cur = first_action; cur; cur = cur->next) { /* Walk the list of actions */
		for (num = 3; num < argc; num++) {
			if (!strcasecmp(cur->action, argv[num])) {
				ast_cli(fd, "Action: %s\nSynopsis: %s\nPrivilege: %s\n%s\n", cur->action, cur->synopsis, authority_to_str(cur->authority, authority, sizeof(authority) -1), cur->description ? cur->description : "");
			}
		}
	}
	ast_rwlock_unlock(&actionlock);

	return RESULT_SUCCESS;
}

static int handle_showmanager(int fd, int argc, char *argv[])
{
	struct ast_manager_user *user = NULL;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	AST_LIST_LOCK(&users);

	if (!(user = ast_get_manager_by_name_locked(argv[3]))) {
		ast_cli(fd, "There is no manager called %s\n", argv[3]);
		AST_LIST_UNLOCK(&users);
		return -1;
	}

	ast_cli(fd,"\n");
	ast_cli(fd,
		"       username: %s\n"
		"         secret: %s\n"
		"           deny: %s\n"
		"         permit: %s\n"
		"           read: %s\n"
		"          write: %s\n"
		"displayconnects: %s\n",
		(user->username ? user->username : "(N/A)"),
		(user->secret ? "<Set>" : "(N/A)"),
		(user->deny ? user->deny : "(N/A)"),
		(user->permit ? user->permit : "(N/A)"),
		(user->read ? user->read : "(N/A)"),
		(user->write ? user->write : "(N/A)"),
		(user->displayconnects ? "yes" : "no"));

	AST_LIST_UNLOCK(&users);

	return RESULT_SUCCESS;
}


static int handle_showmanagers(int fd, int argc, char *argv[])
{
	struct ast_manager_user *user = NULL;
	int count_amu = 0;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	AST_LIST_LOCK(&users);

	/* If there are no users, print out something along those lines */
	if (AST_LIST_EMPTY(&users)) {
		ast_cli(fd, "There are no manager users.\n");
		AST_LIST_UNLOCK(&users);
		return RESULT_SUCCESS;
	}

	ast_cli(fd, "\nusername\n--------\n");

	AST_LIST_TRAVERSE(&users, user, list) {
		ast_cli(fd, "%s\n", user->username);
		count_amu++;
	}

	AST_LIST_UNLOCK(&users);

	ast_cli(fd,"-------------------\n");
	ast_cli(fd,"%d manager users configured.\n", count_amu);

	return RESULT_SUCCESS;
}


/*! \brief  CLI command 
	Should change to "manager show commands" */
static int handle_showmancmds(int fd, int argc, char *argv[])
{
	struct manager_action *cur;
	char authority[80];
	char *format = "  %-15.15s  %-15.15s  %-55.55s\n";

	ast_cli(fd, format, "Action", "Privilege", "Synopsis");
	ast_cli(fd, format, "------", "---------", "--------");
	
	ast_rwlock_rdlock(&actionlock);
	for (cur = first_action; cur; cur = cur->next) /* Walk the list of actions */
		ast_cli(fd, format, cur->action, authority_to_str(cur->authority, authority, sizeof(authority) -1), cur->synopsis);
	ast_rwlock_unlock(&actionlock);
	
	return RESULT_SUCCESS;
}

/*! \brief CLI command show manager connected */
/* Should change to "manager show connected" */
static int handle_showmanconn(int fd, int argc, char *argv[])
{
	struct mansession_session *s;
	char *format = "  %-15.15s  %-15.15s\n";

	ast_cli(fd, format, "Username", "IP Address");
	
	AST_LIST_LOCK(&sessions);
	AST_LIST_TRAVERSE(&sessions, s, list)
		ast_cli(fd, format,s->username, ast_inet_ntoa(s->sin.sin_addr));
	AST_LIST_UNLOCK(&sessions);

	return RESULT_SUCCESS;
}

/*! \brief CLI command show manager connected */
/* Should change to "manager show connected" */
static int handle_showmaneventq(int fd, int argc, char *argv[])
{
	struct eventqent *s;

	AST_LIST_LOCK(&sessions);
	for (s = master_eventq; s; s = s->next) {
		ast_cli(fd, "Usecount: %d\n",s->usecount);
		ast_cli(fd, "Category: %d\n", s->category);
		ast_cli(fd, "Event:\n%s", s->eventdata);
	}
	AST_LIST_UNLOCK(&sessions);

	return RESULT_SUCCESS;
}

static char showmancmd_help[] = 
"Usage: manager show command <actionname>\n"
"	Shows the detailed description for a specific Asterisk manager interface command.\n";

static char showmancmds_help[] = 
"Usage: manager show commands\n"
"	Prints a listing of all the available Asterisk manager interface commands.\n";

static char showmanconn_help[] = 
"Usage: manager show connected\n"
"	Prints a listing of the users that are currently connected to the\n"
"Asterisk manager interface.\n";

static char showmaneventq_help[] = 
"Usage: manager show eventq\n"
"	Prints a listing of all events pending in the Asterisk manger\n"
"event queue.\n";

static char showmanagers_help[] =
"Usage: manager show users\n"
"       Prints a listing of all managers that are currently configured on that\n"
" system.\n";

static char showmanager_help[] =
" Usage: manager show user <user>\n"
"        Display all information related to the manager user specified.\n";

static struct ast_cli_entry cli_show_manager_command_deprecated = {
	{ "show", "manager", "command", NULL },
	handle_showmancmd, NULL,
	NULL, complete_show_mancmd };

static struct ast_cli_entry cli_show_manager_commands_deprecated = {
	{ "show", "manager", "commands", NULL },
	handle_showmancmds, NULL,
	NULL };

static struct ast_cli_entry cli_show_manager_connected_deprecated = {
	{ "show", "manager", "connected", NULL },
	handle_showmanconn, NULL,
	NULL };

static struct ast_cli_entry cli_show_manager_eventq_deprecated = {
	{ "show", "manager", "eventq", NULL },
	handle_showmaneventq, NULL,
	NULL };

static struct ast_cli_entry cli_manager[] = {
	{ { "manager", "show", "command", NULL },
	handle_showmancmd, "Show a manager interface command",
	showmancmd_help, complete_show_mancmd, &cli_show_manager_command_deprecated },

	{ { "manager", "show", "commands", NULL },
	handle_showmancmds, "List manager interface commands",
	showmancmds_help, NULL, &cli_show_manager_commands_deprecated },

	{ { "manager", "show", "connected", NULL },
	handle_showmanconn, "List connected manager interface users",
	showmanconn_help, NULL, &cli_show_manager_connected_deprecated },

	{ { "manager", "show", "eventq", NULL },
	handle_showmaneventq, "List manager interface queued events",
	showmaneventq_help, NULL, &cli_show_manager_eventq_deprecated },

	{ { "manager", "show", "users", NULL },
	handle_showmanagers, "List configured manager users",
	showmanagers_help, NULL, NULL },

	{ { "manager", "show", "user", NULL },
	handle_showmanager, "Display information on a specific manager user",
	showmanager_help, NULL, NULL },
};

static void unuse_eventqent(struct eventqent *e)
{
	if (ast_atomic_dec_and_test(&e->usecount) && e->next)
		pthread_kill(t, SIGURG);
}

static void free_session(struct mansession_session *s)
{
	struct eventqent *eqe;
	if (s->fd > -1)
		close(s->fd);
	if (s->outputstr)
		free(s->outputstr);
	ast_mutex_destroy(&s->__lock);
	while (s->eventq) {
		eqe = s->eventq;
		s->eventq = s->eventq->next;
		unuse_eventqent(eqe);
	}
	free(s);
}

static void destroy_session(struct mansession_session *s)
{
	AST_LIST_LOCK(&sessions);
	AST_LIST_REMOVE(&sessions, s, list);
	num_sessions--;
	free_session(s);
	AST_LIST_UNLOCK(&sessions);
}

const char *astman_get_header(const struct message *m, char *var)
{
	char cmp[80];
	int x;

	snprintf(cmp, sizeof(cmp), "%s: ", var);

	for (x = 0; x < m->hdrcount; x++) {
		if (!strncasecmp(cmp, m->headers[x], strlen(cmp)))
			return m->headers[x] + strlen(cmp);
	}

	return "";
}

struct ast_variable *astman_get_variables(const struct message *m)
{
	int varlen, x, y;
	struct ast_variable *head = NULL, *cur;
	char *var, *val;

	char *parse;    
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vars)[32];
	);

	varlen = strlen("Variable: ");	

	for (x = 0; x < m->hdrcount; x++) {
		if (strncasecmp("Variable: ", m->headers[x], varlen))
			continue;

		parse = ast_strdupa(m->headers[x] + varlen);

		AST_STANDARD_APP_ARGS(args, parse);
		if (args.argc) {
			for (y = 0; y < args.argc; y++) {
				if (!args.vars[y])
					continue;
				var = val = ast_strdupa(args.vars[y]);
				strsep(&val, "=");
				if (!val || ast_strlen_zero(var))
					continue;
				cur = ast_variable_new(var, val);
				if (head) {
					cur->next = head;
					head = cur;
				} else
					head = cur;
			}
		}
	}

	return head;
}

/*! \note NOTE:
   Callers of astman_send_error(), astman_send_response() or astman_send_ack() must EITHER
   hold the session lock _or_ be running in an action callback (in which case s->busy will
   be non-zero). In either of these cases, there is no need to lock-protect the session's
   fd, since no other output will be sent (events will be queued), and no input will
   be read until either the current action finishes or get_input() obtains the session
   lock.
 */
void astman_send_error(struct mansession *s, const struct message *m, char *error)
{
	const char *id = astman_get_header(m,"ActionID");

	astman_append(s, "Response: Error\r\n");
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n", id);
	astman_append(s, "Message: %s\r\n\r\n", error);
}

void astman_send_response(struct mansession *s, const struct message *m, char *resp, char *msg)
{
	const char *id = astman_get_header(m,"ActionID");

	astman_append(s, "Response: %s\r\n", resp);
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n", id);
	if (msg)
		astman_append(s, "Message: %s\r\n\r\n", msg);
	else
		astman_append(s, "\r\n");
}

void astman_send_ack(struct mansession *s, const struct message *m, char *msg)
{
	astman_send_response(s, m, "Success", msg);
}

/*! Tells you if smallstr exists inside bigstr
   which is delim by delim and uses no buf or stringsep
   ast_instring("this|that|more","this",',') == 1;

   feel free to move this to app.c -anthm */
static int ast_instring(const char *bigstr, const char *smallstr, char delim) 
{
	const char *val = bigstr, *next;

	do {
		if ((next = strchr(val, delim))) {
			if (!strncmp(val, smallstr, (next - val)))
				return 1;
			else
				continue;
		} else
			return !strcmp(smallstr, val);

	} while (*(val = (next + 1)));

	return 0;
}

static int get_perm(const char *instr)
{
	int x = 0, ret = 0;

	if (!instr)
		return 0;

	for (x = 0; x < (sizeof(perms) / sizeof(perms[0])); x++) {
		if (ast_instring(instr, perms[x].label, ','))
			ret |= perms[x].num;
	}
	
	return ret;
}

static int ast_is_number(const char *string) 
{
	int ret = 1, x = 0;

	if (!string)
		return 0;

	for (x = 0; x < strlen(string); x++) {
		if (!(string[x] >= 48 && string[x] <= 57)) {
			ret = 0;
			break;
		}
	}
	
	return ret ? atoi(string) : 0;
}

static int strings_to_mask(const char *string) 
{
	int x, ret = -1;
	
	x = ast_is_number(string);

	if (x)
		ret = x;
	else if (ast_strlen_zero(string))
		ret = -1;
	else if (ast_false(string))
		ret = 0;
	else if (ast_true(string)) {
		ret = 0;
		for (x=0; x<sizeof(perms) / sizeof(perms[0]); x++)
			ret |= perms[x].num;		
	} else {
		ret = 0;
		for (x=0; x<sizeof(perms) / sizeof(perms[0]); x++) {
			if (ast_instring(string, perms[x].label, ',')) 
				ret |= perms[x].num;		
		}
	}

	return ret;
}

/*! \brief
   Rather than braindead on,off this now can also accept a specific int mask value 
   or a ',' delim list of mask strings (the same as manager.conf) -anthm
*/
static int set_eventmask(struct mansession_session *s, const char *eventmask)
{
	int maskint = strings_to_mask(eventmask);

	ast_mutex_lock(&s->__lock);
	if (maskint >= 0)	
		s->send_events = maskint;
	ast_mutex_unlock(&s->__lock);
	
	return maskint;
}

static int authenticate(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	char *cat;
	const char *user = astman_get_header(m, "Username");
	const char *pass = astman_get_header(m, "Secret");
	const char *authtype = astman_get_header(m, "AuthType");
	const char *key = astman_get_header(m, "Key");
	const char *events = astman_get_header(m, "Events");
	
	cfg = ast_config_load("manager.conf");
	if (!cfg)
		return -1;
	cat = ast_category_browse(cfg, NULL);
	while (cat) {
		if (strcasecmp(cat, "general")) {
			/* This is a user */
			if (!strcasecmp(cat, user)) {
				struct ast_variable *v;
				struct ast_ha *ha = NULL;
				char *password = NULL;

				for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
					if (!strcasecmp(v->name, "secret")) {
						password = v->value;
					} else if (!strcasecmp(v->name, "displaysystemname")) {
						if (ast_true(v->value)) {
							if (ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
								s->session->displaysystemname = 1;
							} else {
								ast_log(LOG_ERROR, "Can't enable displaysystemname in manager.conf - no system name configured in asterisk.conf\n");
							}
						}
					} else if (!strcasecmp(v->name, "permit") ||
						   !strcasecmp(v->name, "deny")) {
						ha = ast_append_ha(v->name, v->value, ha);
					} else if (!strcasecmp(v->name, "writetimeout")) {
						int val = atoi(v->value);

						if (val < 100)
							ast_log(LOG_WARNING, "Invalid writetimeout value '%s' at line %d\n", v->value, v->lineno);
						else
							s->session->writetimeout = val;
					}
				    		
				}
				if (ha && !ast_apply_ha(ha, &(s->session->sin))) {
					ast_log(LOG_NOTICE, "%s failed to pass IP ACL as '%s'\n", ast_inet_ntoa(s->session->sin.sin_addr), user);
					ast_free_ha(ha);
					ast_config_destroy(cfg);
					return -1;
				} else if (ha)
					ast_free_ha(ha);
				if (!strcasecmp(authtype, "MD5")) {
					if (!ast_strlen_zero(key) && 
					    !ast_strlen_zero(s->session->challenge) && !ast_strlen_zero(password)) {
						int x;
						int len = 0;
						char md5key[256] = "";
						struct MD5Context md5;
						unsigned char digest[16];
						MD5Init(&md5);
						MD5Update(&md5, (unsigned char *) s->session->challenge, strlen(s->session->challenge));
						MD5Update(&md5, (unsigned char *) password, strlen(password));
						MD5Final(digest, &md5);
						for (x=0; x<16; x++)
							len += sprintf(md5key + len, "%2.2x", digest[x]);
						if (!strcmp(md5key, key))
							break;
						else {
							ast_config_destroy(cfg);
							return -1;
						}
					} else {
						ast_log(LOG_DEBUG, "MD5 authentication is not possible.  challenge: '%s'\n", 
							S_OR(s->session->challenge, ""));
						ast_config_destroy(cfg);
						return -1;
					}
				} else if (password && !strcmp(password, pass)) {
					break;
				} else {
					ast_log(LOG_NOTICE, "%s failed to authenticate as '%s'\n", ast_inet_ntoa(s->session->sin.sin_addr), user);
					ast_config_destroy(cfg);
					return -1;
				}	
			}
		}
		cat = ast_category_browse(cfg, cat);
	}
	if (cat) {
		ast_copy_string(s->session->username, cat, sizeof(s->session->username));
		s->session->readperm = get_perm(ast_variable_retrieve(cfg, cat, "read"));
		s->session->writeperm = get_perm(ast_variable_retrieve(cfg, cat, "write"));
		ast_config_destroy(cfg);
		if (events)
			set_eventmask(s->session, events);
		return 0;
	}
	ast_config_destroy(cfg);
	cfg = ast_config_load("users.conf");
	if (!cfg)
		return -1;
	cat = ast_category_browse(cfg, NULL);
	while (cat) {
		struct ast_variable *v;
		const char *password = NULL;
		int hasmanager = 0;
		const char *readperms = NULL;
		const char *writeperms = NULL;

		if (strcasecmp(cat, user) || !strcasecmp(cat, "general")) {
			cat = ast_category_browse(cfg, cat);
			continue;
		}
		for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
			if (!strcasecmp(v->name, "secret"))
				password = v->value;
			else if (!strcasecmp(v->name, "hasmanager"))
				hasmanager = ast_true(v->value);
			else if (!strcasecmp(v->name, "managerread"))
				readperms = v->value;
			else if (!strcasecmp(v->name, "managerwrite"))
				writeperms = v->value;
		}
		if (!hasmanager)
			break;
		if (!password || strcmp(password, pass)) {
			ast_log(LOG_NOTICE, "%s failed to authenticate as '%s'\n", ast_inet_ntoa(s->session->sin.sin_addr), user);
			ast_config_destroy(cfg);
			return -1;
		}
		ast_copy_string(s->session->username, cat, sizeof(s->session->username));
		s->session->readperm = readperms ? get_perm(readperms) : -1;
		s->session->writeperm = writeperms ? get_perm(writeperms) : -1;
		ast_config_destroy(cfg);
		if (events)
			set_eventmask(s->session, events);
		return 0;
	}
	ast_log(LOG_NOTICE, "%s tried to authenticate with nonexistent user '%s'\n", ast_inet_ntoa(s->session->sin.sin_addr), user);
	ast_config_destroy(cfg);
	return -1;
}

/*! \brief Manager PING */
static char mandescr_ping[] = 
"Description: A 'Ping' action will ellicit a 'Pong' response.  Used to keep the\n"
"  manager connection open.\n"
"Variables: NONE\n";

static int action_ping(struct mansession *s, const struct message *m)
{
	astman_send_response(s, m, "Pong", NULL);
	return 0;
}

static char mandescr_getconfig[] =
"Description: A 'GetConfig' action will dump the contents of a configuration\n"
"file by category and contents.\n"
"Variables:\n"
"   Filename: Configuration filename (e.g. foo.conf)\n";

static int action_getconfig(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *fn = astman_get_header(m, "Filename");
	int catcount = 0;
	int lineno = 0;
	char *category=NULL;
	struct ast_variable *v;
	char idText[256] = "";
	const char *id = astman_get_header(m, "ActionID");

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}
	if (!(cfg = ast_config_load_with_comments(fn))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	}
	astman_append(s, "Response: Success\r\n%s", idText);
	while ((category = ast_category_browse(cfg, category))) {
		lineno = 0;
		astman_append(s, "Category-%06d: %s\r\n", catcount, category);
		for (v = ast_variable_browse(cfg, category); v; v = v->next)
			astman_append(s, "Line-%06d-%06d: %s=%s\r\n", catcount, lineno++, v->name, v->value);
		catcount++;
	}
	ast_config_destroy(cfg);
	astman_append(s, "\r\n");

	return 0;
}


static void handle_updates(struct mansession *s, const struct message *m, struct ast_config *cfg)
{
	int x;
	char hdr[40];
	const char *action, *cat, *var, *value, *match;
	struct ast_category *category;
	struct ast_variable *v;
	
	for (x=0;x<100000;x++) {
		unsigned int object = 0;

		snprintf(hdr, sizeof(hdr), "Action-%06d", x);
		action = astman_get_header(m, hdr);
		if (ast_strlen_zero(action))
			break;
		snprintf(hdr, sizeof(hdr), "Cat-%06d", x);
		cat = astman_get_header(m, hdr);
		snprintf(hdr, sizeof(hdr), "Var-%06d", x);
		var = astman_get_header(m, hdr);
		snprintf(hdr, sizeof(hdr), "Value-%06d", x);
		value = astman_get_header(m, hdr);
		if (!ast_strlen_zero(value) && *value == '>') {
			object = 1;
			value++;
		}
		snprintf(hdr, sizeof(hdr), "Match-%06d", x);
		match = astman_get_header(m, hdr);
		if (!strcasecmp(action, "newcat")) {
			if (!ast_strlen_zero(cat)) {
				category = ast_category_new(cat);
				if (category) {
					ast_category_append(cfg, category);
				}
			}
		} else if (!strcasecmp(action, "renamecat")) {
			if (!ast_strlen_zero(cat) && !ast_strlen_zero(value)) {
				category = ast_category_get(cfg, cat);
				if (category) 
					ast_category_rename(category, value);
			}
		} else if (!strcasecmp(action, "delcat")) {
			if (!ast_strlen_zero(cat))
				ast_category_delete(cfg, (char *) cat);
		} else if (!strcasecmp(action, "update")) {
			if (!ast_strlen_zero(cat) && !ast_strlen_zero(var) && (category = ast_category_get(cfg, cat)))
				ast_variable_update(category, var, value, match, object);
		} else if (!strcasecmp(action, "delete")) {
			if (!ast_strlen_zero(cat) && !ast_strlen_zero(var) && (category = ast_category_get(cfg, cat)))
				ast_variable_delete(category, (char *) var, (char *) match);
		} else if (!strcasecmp(action, "append")) {
			if (!ast_strlen_zero(cat) && !ast_strlen_zero(var) && 
				(category = ast_category_get(cfg, cat)) && 
				(v = ast_variable_new(var, value))){
				if (object || (match && !strcasecmp(match, "object")))
					v->object = 1;
				ast_variable_append(category, v);
			}
		}
	}
}

static char mandescr_updateconfig[] =
"Description: A 'UpdateConfig' action will modify, create, or delete\n"
"configuration elements in Asterisk configuration files.\n"
"Variables (X's represent 6 digit number beginning with 000000):\n"
"   SrcFilename:   Configuration filename to read(e.g. foo.conf)\n"
"   DstFilename:   Configuration filename to write(e.g. foo.conf)\n"
"   Reload:        Whether or not a reload should take place (or name of specific module)\n"
"   Action-XXXXXX: Action to Take (NewCat,RenameCat,DelCat,Update,Delete,Append)\n"
"   Cat-XXXXXX:    Category to operate on\n"
"   Var-XXXXXX:    Variable to work on\n"
"   Value-XXXXXX:  Value to work on\n"
"   Match-XXXXXX:  Extra match required to match line\n";

static int action_updateconfig(struct mansession *s, const struct message *m)
{
	struct ast_config *cfg;
	const char *sfn = astman_get_header(m, "SrcFilename");
	const char *dfn = astman_get_header(m, "DstFilename");
	int res;
	char idText[256] = "";
	const char *id = astman_get_header(m, "ActionID");
	const char *rld = astman_get_header(m, "Reload");

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	if (ast_strlen_zero(sfn) || ast_strlen_zero(dfn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}
	if (!(cfg = ast_config_load_with_comments(sfn))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	}
	handle_updates(s, m, cfg);
	res = config_text_file_save(dfn, cfg, "Manager");
	ast_config_destroy(cfg);
	if (res) {
		astman_send_error(s, m, "Save of config failed");
		return 0;
	}
	astman_append(s, "Response: Success\r\n%s\r\n", idText);
	if (!ast_strlen_zero(rld)) {
		if (ast_true(rld))
			rld = NULL;
		ast_module_reload(rld); 
	}
	return 0;
}

/*! \brief Manager WAITEVENT */
static char mandescr_waitevent[] = 
"Description: A 'WaitEvent' action will ellicit a 'Success' response.  Whenever\n"
"a manager event is queued.  Once WaitEvent has been called on an HTTP manager\n"
"session, events will be generated and queued.\n"
"Variables: \n"
"   Timeout: Maximum time to wait for events\n";

static int action_waitevent(struct mansession *s, const struct message *m)
{
	const char *timeouts = astman_get_header(m, "Timeout");
	int timeout = -1, max;
	int x;
	int needexit = 0;
	time_t now;
	struct eventqent *eqe;
	const char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	if (!ast_strlen_zero(timeouts)) {
		sscanf(timeouts, "%i", &timeout);
	}
	
	ast_mutex_lock(&s->session->__lock);
	if (s->session->waiting_thread != AST_PTHREADT_NULL) {
		pthread_kill(s->session->waiting_thread, SIGURG);
	}
	if (s->session->sessiontimeout) {
		time(&now);
		max = s->session->sessiontimeout - now - 10;
		if (max < 0)
			max = 0;
		if ((timeout < 0) || (timeout > max))
			timeout = max;
		if (!s->session->send_events)
			s->session->send_events = -1;
		/* Once waitevent is called, always queue events from now on */
	}
	ast_mutex_unlock(&s->session->__lock);
	s->session->waiting_thread = pthread_self();
	if (option_debug)
		ast_log(LOG_DEBUG, "Starting waiting for an event!\n");
	for (x=0; ((x < timeout) || (timeout < 0)); x++) {
		ast_mutex_lock(&s->session->__lock);
		if (s->session->eventq && s->session->eventq->next)
			needexit = 1;
		if (s->session->waiting_thread != pthread_self())
			needexit = 1;
		if (s->session->needdestroy)
			needexit = 1;
		ast_mutex_unlock(&s->session->__lock);
		if (needexit)
			break;
		if (s->session->fd > 0) {
			if (ast_wait_for_input(s->session->fd, 1000))
				break;
		} else {
			sleep(1);
		}
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Finished waiting for an event!\n");
	ast_mutex_lock(&s->session->__lock);
	if (s->session->waiting_thread == pthread_self()) {
		astman_send_response(s, m, "Success", "Waiting for Event...");
		/* Only show events if we're the most recent waiter */
		while(s->session->eventq->next) {
			eqe = s->session->eventq->next;
			if (((s->session->readperm & eqe->category) == eqe->category) &&
			    ((s->session->send_events & eqe->category) == eqe->category)) {
				astman_append(s, "%s", eqe->eventdata);
			}
			unuse_eventqent(s->session->eventq);
			s->session->eventq = eqe;
		}
		astman_append(s,
			"Event: WaitEventComplete\r\n"
			"%s"
			"\r\n", idText);
		s->session->waiting_thread = AST_PTHREADT_NULL;
	} else {
		ast_log(LOG_DEBUG, "Abandoning event request!\n");
	}
	ast_mutex_unlock(&s->session->__lock);
	return 0;
}

static char mandescr_listcommands[] = 
"Description: Returns the action name and synopsis for every\n"
"  action that is available to the user\n"
"Variables: NONE\n";

/*! \note The actionlock is read-locked by the caller of this function */
static int action_listcommands(struct mansession *s, const struct message *m)
{
	struct manager_action *cur;
	char idText[256] = "";
	char temp[BUFSIZ];
	const char *id = astman_get_header(m,"ActionID");

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	astman_append(s, "Response: Success\r\n%s", idText);
	for (cur = first_action; cur; cur = cur->next) {
		if ((s->session->writeperm & cur->authority) == cur->authority)
			astman_append(s, "%s: %s (Priv: %s)\r\n", cur->action, cur->synopsis, authority_to_str(cur->authority, temp, sizeof(temp)));
	}
	astman_append(s, "\r\n");

	return 0;
}

static char mandescr_events[] = 
"Description: Enable/Disable sending of events to this manager\n"
"  client.\n"
"Variables:\n"
"	EventMask: 'on' if all events should be sent,\n"
"		'off' if no events should be sent,\n"
"		'system,call,log' to select which flags events should have to be sent.\n";

static int action_events(struct mansession *s, const struct message *m)
{
	const char *mask = astman_get_header(m, "EventMask");
	int res;

	res = set_eventmask(s->session, mask);
	if (res > 0)
		astman_send_response(s, m, "Events On", NULL);
	else if (res == 0)
		astman_send_response(s, m, "Events Off", NULL);

	return 0;
}

static char mandescr_logoff[] = 
"Description: Logoff this manager session\n"
"Variables: NONE\n";

static int action_logoff(struct mansession *s, const struct message *m)
{
	astman_send_response(s, m, "Goodbye", "Thanks for all the fish.");
	return -1;
}

static char mandescr_hangup[] = 
"Description: Hangup a channel\n"
"Variables: \n"
"	Channel: The channel name to be hungup\n";

static int action_hangup(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	c = ast_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
	ast_channel_unlock(c);
	astman_send_ack(s, m, "Channel Hungup");
	return 0;
}

static char mandescr_setvar[] = 
"Description: Set a global or local channel variable.\n"
"Variables: (Names marked with * are required)\n"
"	Channel: Channel to set variable for\n"
"	*Variable: Variable name\n"
"	*Value: Value\n";

static int action_setvar(struct mansession *s, const struct message *m)
{
        struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *varname = astman_get_header(m, "Variable");
	const char *varval = astman_get_header(m, "Value");
	
	if (ast_strlen_zero(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}
	
	if (!ast_strlen_zero(name)) {
		c = ast_get_channel_by_name_locked(name);
		if (!c) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}
	
	pbx_builtin_setvar_helper(c, varname, S_OR(varval, ""));
	  
	if (c)
		ast_channel_unlock(c);

	astman_send_ack(s, m, "Variable Set");	

	return 0;
}

static char mandescr_getvar[] = 
"Description: Get the value of a global or local channel variable.\n"
"Variables: (Names marked with * are required)\n"
"	Channel: Channel to read variable from\n"
"	*Variable: Variable name\n"
"	ActionID: Optional Action id for message matching.\n";

static int action_getvar(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	const char *varname = astman_get_header(m, "Variable");
	const char *id = astman_get_header(m,"ActionID");
	char *varval;
	char workspace[1024] = "";

	if (ast_strlen_zero(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}

	if (!ast_strlen_zero(name)) {
		c = ast_get_channel_by_name_locked(name);
		if (!c) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}

	if (varname[strlen(varname) - 1] == ')') {
		char *copy = ast_strdupa(varname);
		if (!c) {
			c = ast_channel_alloc(0, 0, "", "", "", "", "", 0, "Bogus/manager");
			if (c) {
				ast_func_read(c, copy, workspace, sizeof(workspace));
				ast_channel_free(c);
				c = NULL;
			} else
				ast_log(LOG_ERROR, "Unable to allocate bogus channel for variable substitution.  Function results may be blank.\n");
		} else
			ast_func_read(c, copy, workspace, sizeof(workspace));
		varval = workspace;
	} else {
		pbx_retrieve_variable(c, varname, &varval, workspace, sizeof(workspace), NULL);
	}

	if (c)
		ast_channel_unlock(c);
	astman_append(s, "Response: Success\r\n"
		"Variable: %s\r\nValue: %s\r\n", varname, varval);
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n",id);
	astman_append(s, "\r\n");

	return 0;
}


/*! \brief Manager "status" command to show channels */
/* Needs documentation... */
static int action_status(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m,"ActionID");
    	const char *name = astman_get_header(m,"Channel");
	char idText[256] = "";
	struct ast_channel *c;
	char bridge[256];
	struct timeval now = ast_tvnow();
	long elapsed_seconds = 0;
	int all = ast_strlen_zero(name); /* set if we want all channels */

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	if (all)
		c = ast_channel_walk_locked(NULL);
	else {
		c = ast_get_channel_by_name_locked(name);
		if (!c) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}
	astman_send_ack(s, m, "Channel status will follow");
	/* if we look by name, we break after the first iteration */
	while (c) {
		if (c->_bridge)
			snprintf(bridge, sizeof(bridge), "Link: %s\r\n", c->_bridge->name);
		else
			bridge[0] = '\0';
		if (c->pbx) {
			if (c->cdr) {
				elapsed_seconds = now.tv_sec - c->cdr->start.tv_sec;
			}
			astman_append(s,
			"Event: Status\r\n"
			"Privilege: Call\r\n"
			"Channel: %s\r\n"
			"CallerID: %s\r\n"		/* This parameter is deprecated and will be removed post-1.4 */
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n"
			"Account: %s\r\n"
			"State: %s\r\n"
			"Context: %s\r\n"
			"Extension: %s\r\n"
			"Priority: %d\r\n"
			"Seconds: %ld\r\n"
			"%s"
			"Uniqueid: %s\r\n"
			"%s"
			"\r\n",
			c->name, 
			S_OR(c->cid.cid_num, "<unknown>"), 
			S_OR(c->cid.cid_num, "<unknown>"), 
			S_OR(c->cid.cid_name, "<unknown>"), 
			c->accountcode,
			ast_state2str(c->_state), c->context,
			c->exten, c->priority, (long)elapsed_seconds, bridge, c->uniqueid, idText);
		} else {
			astman_append(s,
			"Event: Status\r\n"
			"Privilege: Call\r\n"
			"Channel: %s\r\n"
			"CallerID: %s\r\n"		/* This parameter is deprecated and will be removed post-1.4 */
			"CallerIDNum: %s\r\n"
			"CallerIDName: %s\r\n"
			"Account: %s\r\n"
			"State: %s\r\n"
			"%s"
			"Uniqueid: %s\r\n"
			"%s"
			"\r\n",
			c->name, 
			S_OR(c->cid.cid_num, "<unknown>"), 
			S_OR(c->cid.cid_num, "<unknown>"), 
			S_OR(c->cid.cid_name, "<unknown>"), 
			c->accountcode,
			ast_state2str(c->_state), bridge, c->uniqueid, idText);
		}
		ast_channel_unlock(c);
		if (!all)
			break;
		c = ast_channel_walk_locked(c);
	}
	astman_append(s,
	"Event: StatusComplete\r\n"
	"%s"
	"\r\n",idText);
	return 0;
}

static char mandescr_redirect[] = 
"Description: Redirect (transfer) a call.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel to redirect\n"
"	ExtraChannel: Second call leg to transfer (optional)\n"
"	*Exten: Extension to transfer to\n"
"	*Context: Context to transfer to\n"
"	*Priority: Priority to transfer to\n"
"	ActionID: Optional Action id for message matching.\n";

/*! \brief  action_redirect: The redirect manager command */
static int action_redirect(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *name2 = astman_get_header(m, "ExtraChannel");
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	const char *priority = astman_get_header(m, "Priority");
	struct ast_channel *chan, *chan2 = NULL;
	int pi = 0;
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}
	if (!ast_strlen_zero(priority) && (sscanf(priority, "%d", &pi) != 1)) {
		if ((pi = ast_findlabel_extension(NULL, context, exten, priority, NULL)) < 1) {
			astman_send_error(s, m, "Invalid priority");
			return 0;
		}
	}
	/* XXX watch out, possible deadlock!!! */
	chan = ast_get_channel_by_name_locked(name);
	if (!chan) {
		char buf[BUFSIZ];
		snprintf(buf, sizeof(buf), "Channel does not exist: %s", name);
		astman_send_error(s, m, buf);
		return 0;
	}
	if (ast_check_hangup(chan)) {
		astman_send_error(s, m, "Redirect failed, channel not up.");
		ast_channel_unlock(chan);
		return 0;
	}
	if (!ast_strlen_zero(name2))
		chan2 = ast_get_channel_by_name_locked(name2);
	if (chan2 && ast_check_hangup(chan2)) {
		astman_send_error(s, m, "Redirect failed, extra channel not up.");
		ast_channel_unlock(chan);
		ast_channel_unlock(chan2);
		return 0;
	}
	if (chan->pbx) {
		ast_channel_lock(chan);
		ast_set_flag(chan, AST_FLAG_BRIDGE_HANGUP_DONT); /* don't let the after-bridge code run the h-exten */
		ast_channel_unlock(chan);
	}
	res = ast_async_goto(chan, context, exten, pi);
	if (!res) {
		if (!ast_strlen_zero(name2)) {
			if (chan2) {
				if (chan2->pbx) {
					ast_channel_lock(chan2);
					ast_set_flag(chan2, AST_FLAG_BRIDGE_HANGUP_DONT); /* don't let the after-bridge code run the h-exten */
					ast_channel_unlock(chan2);
				}
				res = ast_async_goto(chan2, context, exten, pi);
			} else {
				res = -1;
			}
			if (!res)
				astman_send_ack(s, m, "Dual Redirect successful");
			else
				astman_send_error(s, m, "Secondary redirect failed");
		} else
			astman_send_ack(s, m, "Redirect successful");
	} else
		astman_send_error(s, m, "Redirect failed");
	if (chan)
		ast_channel_unlock(chan);
	if (chan2)
		ast_channel_unlock(chan2);
	return 0;
}

static int check_blacklist(const char *cmd)
{
	char *cmd_copy, *cur_cmd;
	char *cmd_words[MAX_BLACKLIST_CMD_LEN] = { NULL, };
	int i;

	cmd_copy = ast_strdupa(cmd);
	for (i = 0; i < MAX_BLACKLIST_CMD_LEN && (cur_cmd = strsep(&cmd_copy, " ")); i++) {
		cur_cmd = ast_strip(cur_cmd);
		if (ast_strlen_zero(cur_cmd)) {
			i--;
			continue;
		}

		cmd_words[i] = cur_cmd;
	}

	for (i = 0; i < ARRAY_LEN(command_blacklist); i++) {
		int j, match = 1;

		for (j = 0; command_blacklist[i].words[j]; j++) {
			if (ast_strlen_zero(cmd_words[j]) || strcasecmp(cmd_words[j], command_blacklist[i].words[j])) {
				match = 0;
				break;
			}
		}

		if (match) {
			return 1;
		}
	}

	return 0;
}

static char mandescr_command[] = 
"Description: Run a CLI command.\n"
"Variables: (Names marked with * are required)\n"
"	*Command: Asterisk CLI command to run\n"
"	ActionID: Optional Action id for message matching.\n";

/*! \brief  action_command: Manager command "command" - execute CLI command */
static int action_command(struct mansession *s, const struct message *m)
{
	const char *cmd = astman_get_header(m, "Command");
	const char *id = astman_get_header(m, "ActionID");
	char *buf, *final_buf;
	char template[] = "/tmp/ast-ami-XXXXXX";	/* template for temporary file */
	int fd = mkstemp(template);
	off_t l;

	if (ast_strlen_zero(cmd)) {
		astman_send_error(s, m, "No command provided");
		return 0;
	}

	if (check_blacklist(cmd)) {
		astman_send_error(s, m, "Command blacklisted");
		return 0;
	}

	astman_append(s, "Response: Follows\r\nPrivilege: Command\r\n");
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n", id);
	/* FIXME: Wedge a ActionID response in here, waiting for later changes */
	ast_cli_command(fd, cmd);	/* XXX need to change this to use a FILE * */
	l = lseek(fd, 0, SEEK_END);	/* how many chars available */

	/* This has a potential to overflow the stack.  Hence, use the heap. */
	buf = ast_calloc(1, l + 1);
	final_buf = ast_calloc(1, l + 1);
	if (buf) {
		lseek(fd, 0, SEEK_SET);
		if (read(fd, buf, l) < 0) {
			ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
		}
		buf[l] = '\0';
		if (final_buf) {
			term_strip(final_buf, buf, l);
			final_buf[l] = '\0';
		}
		astman_append(s, "%s", S_OR(final_buf, buf));
		ast_free(buf);
	}
	close(fd);
	unlink(template);
	astman_append(s, "--END COMMAND--\r\n\r\n");
	if (final_buf)
		ast_free(final_buf);
	return 0;
}

static void *fast_originate(void *data)
{
	struct fast_originate_helper *in = data;
	int res;
	int reason = 0;
	struct ast_channel *chan = NULL;
	char requested_channel[AST_CHANNEL_NAME];

	if (!ast_strlen_zero(in->app)) {
		res = ast_pbx_outgoing_app(in->tech, in->format, in->data, in->timeout, in->app, in->appdata, &reason, 1, 
			S_OR(in->cid_num, NULL), 
			S_OR(in->cid_name, NULL),
			in->vars, in->account, &chan);
	} else {
		res = ast_pbx_outgoing_exten(in->tech, in->format, in->data, in->timeout, in->context, in->exten, in->priority, &reason, 1, 
			S_OR(in->cid_num, NULL), 
			S_OR(in->cid_name, NULL),
			in->vars, in->account, &chan);
	}

	if (!chan)
		snprintf(requested_channel, AST_CHANNEL_NAME, "%s/%s", in->tech, in->data);	
	/* Tell the manager what happened with the channel */
	manager_event(EVENT_FLAG_CALL, "OriginateResponse",
		"%s%s"
		"Response: %s\r\n"
		"Channel: %s\r\n"
		"Context: %s\r\n"
		"Exten: %s\r\n"
		"Reason: %d\r\n"
		"Uniqueid: %s\r\n"
		"CallerID: %s\r\n"		/* This parameter is deprecated and will be removed post-1.4 */
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n",
		in->idtext, ast_strlen_zero(in->idtext) ? "" : "\r\n", res ? "Failure" : "Success", 
		chan ? chan->name : requested_channel, in->context, in->exten, reason, 
		chan ? chan->uniqueid : "<null>",
		S_OR(in->cid_num, "<unknown>"),
		S_OR(in->cid_num, "<unknown>"),
		S_OR(in->cid_name, "<unknown>")
		);

	/* Locked by ast_pbx_outgoing_exten or ast_pbx_outgoing_app */
	if (chan)
		ast_channel_unlock(chan);
	free(in);
	return NULL;
}

static char mandescr_originate[] = 
"Description: Generates an outgoing call to a Extension/Context/Priority or\n"
"  Application/Data\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel name to call\n"
"	Exten: Extension to use (requires 'Context' and 'Priority')\n"
"	Context: Context to use (requires 'Exten' and 'Priority')\n"
"	Priority: Priority to use (requires 'Exten' and 'Context')\n"
"	Application: Application to use\n"
"	Data: Data to use (requires 'Application')\n"
"	Timeout: How long to wait for call to be answered (in ms. Default: 30000)\n"
"	CallerID: Caller ID to be set on the outgoing channel\n"
"	Variable: Channel variable to set, multiple Variable: headers are allowed\n"
"	Account: Account code\n"
"	Async: Set to 'true' for fast origination\n";

static int action_originate(struct mansession *s, const struct message *m)
{
	const char *name = astman_get_header(m, "Channel");
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	const char *priority = astman_get_header(m, "Priority");
	const char *timeout = astman_get_header(m, "Timeout");
	const char *callerid = astman_get_header(m, "CallerID");
	const char *account = astman_get_header(m, "Account");
	const char *app = astman_get_header(m, "Application");
	const char *appdata = astman_get_header(m, "Data");
	const char *async = astman_get_header(m, "Async");
	const char *id = astman_get_header(m, "ActionID");
	const char *codecs = astman_get_header(m, "Codecs");
	struct ast_variable *vars = astman_get_variables(m);
	char *tech, *data;
	char *l = NULL, *n = NULL;
	int pi = 0;
	int res;
	int to = 30000;
	int reason = 0;
	char tmp[256];
	char tmp2[256];
	int format = AST_FORMAT_SLINEAR;
	
	pthread_t th;
	pthread_attr_t attr;
	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}
	if (!ast_strlen_zero(priority) && (sscanf(priority, "%d", &pi) != 1)) {
		if ((pi = ast_findlabel_extension(NULL, context, exten, priority, NULL)) < 1) {
			astman_send_error(s, m, "Invalid priority");
			return 0;
		}
	}
	if (!ast_strlen_zero(timeout) && (sscanf(timeout, "%d", &to) != 1)) {
		astman_send_error(s, m, "Invalid timeout");
		return 0;
	}
	ast_copy_string(tmp, name, sizeof(tmp));
	tech = tmp;
	data = strchr(tmp, '/');
	if (!data) {
		astman_send_error(s, m, "Invalid channel");
		return 0;
	}
	*data++ = '\0';
	ast_copy_string(tmp2, callerid, sizeof(tmp2));
	ast_callerid_parse(tmp2, &n, &l);
	if (n) {
		if (ast_strlen_zero(n))
			n = NULL;
	}
	if (l) {
		ast_shrink_phone_number(l);
		if (ast_strlen_zero(l))
			l = NULL;
	}
	if (!ast_strlen_zero(codecs)) {
		format = 0;
		ast_parse_allow_disallow(NULL, &format, codecs, 1);
	}
	if (ast_true(async)) {
		struct fast_originate_helper *fast = ast_calloc(1, sizeof(*fast));
		if (!fast) {
			res = -1;
		} else {
			if (!ast_strlen_zero(id))
				snprintf(fast->idtext, sizeof(fast->idtext), "ActionID: %s", id);
			ast_copy_string(fast->tech, tech, sizeof(fast->tech));
   			ast_copy_string(fast->data, data, sizeof(fast->data));
			ast_copy_string(fast->app, app, sizeof(fast->app));
			ast_copy_string(fast->appdata, appdata, sizeof(fast->appdata));
			if (l)
				ast_copy_string(fast->cid_num, l, sizeof(fast->cid_num));
			if (n)
				ast_copy_string(fast->cid_name, n, sizeof(fast->cid_name));
			fast->vars = vars;	
			ast_copy_string(fast->context, context, sizeof(fast->context));
			ast_copy_string(fast->exten, exten, sizeof(fast->exten));
			ast_copy_string(fast->account, account, sizeof(fast->account));
			fast->format = format;
			fast->timeout = to;
			fast->priority = pi;
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			if (ast_pthread_create(&th, &attr, fast_originate, fast)) {
				ast_free(fast);
				res = -1;
			} else {
				res = 0;
			}
			pthread_attr_destroy(&attr);
		}
	} else if (!ast_strlen_zero(app)) {
        	res = ast_pbx_outgoing_app(tech, format, data, to, app, appdata, &reason, 1, l, n, vars, account, NULL);
    	} else {
		if (exten && context && pi)
	        	res = ast_pbx_outgoing_exten(tech, format, data, to, context, exten, pi, &reason, 1, l, n, vars, account, NULL);
		else {
			astman_send_error(s, m, "Originate with 'Exten' requires 'Context' and 'Priority'");
			return 0;
		}
	}   
	if (!res)
		astman_send_ack(s, m, "Originate successfully queued");
	else
		astman_send_error(s, m, "Originate failed");
	return 0;
}

/*! \brief Help text for manager command mailboxstatus
 */
static char mandescr_mailboxstatus[] = 
"Description: Checks a voicemail account for status.\n"
"Variables: (Names marked with * are required)\n"
"	*Mailbox: Full mailbox ID <mailbox>@<vm-context>\n"
"	ActionID: Optional ActionID for message matching.\n"
"Returns number of messages.\n"
"	Message: Mailbox Status\n"
"	Mailbox: <mailboxid>\n"
"	Waiting: <count>\n"
"\n";

static int action_mailboxstatus(struct mansession *s, const struct message *m)
{
	const char *mailbox = astman_get_header(m, "Mailbox");
	const char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	int ret;
	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
        if (!ast_strlen_zero(id))
                snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	ret = ast_app_has_voicemail(mailbox, NULL);
	astman_append(s, "Response: Success\r\n"
				   "%s"
				   "Message: Mailbox Status\r\n"
				   "Mailbox: %s\r\n"
		 		   "Waiting: %d\r\n\r\n", idText, mailbox, ret);
	return 0;
}

static char mandescr_mailboxcount[] = 
"Description: Checks a voicemail account for new messages.\n"
"Variables: (Names marked with * are required)\n"
"	*Mailbox: Full mailbox ID <mailbox>@<vm-context>\n"
"	ActionID: Optional ActionID for message matching.\n"
"Returns number of new and old messages.\n"
"	Message: Mailbox Message Count\n"
"	Mailbox: <mailboxid>\n"
"	NewMessages: <count>\n"
"	OldMessages: <count>\n"
"\n";
static int action_mailboxcount(struct mansession *s, const struct message *m)
{
	const char *mailbox = astman_get_header(m, "Mailbox");
	const char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	int newmsgs = 0, oldmsgs = 0;
	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	ast_app_inboxcount(mailbox, &newmsgs, &oldmsgs);
	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n",id);
	}
	astman_append(s, "Response: Success\r\n"
				   "%s"
				   "Message: Mailbox Message Count\r\n"
				   "Mailbox: %s\r\n"
		 		   "NewMessages: %d\r\n"
				   "OldMessages: %d\r\n" 
				   "\r\n",
				    idText,mailbox, newmsgs, oldmsgs);
	return 0;
}

static char mandescr_extensionstate[] = 
"Description: Report the extension state for given extension.\n"
"  If the extension has a hint, will use devicestate to check\n"
"  the status of the device connected to the extension.\n"
"Variables: (Names marked with * are required)\n"
"	*Exten: Extension to check state on\n"
"	*Context: Context for extension\n"
"	ActionId: Optional ID for this transaction\n"
"Will return an \"Extension Status\" message.\n"
"The response will include the hint for the extension and the status.\n";

static int action_extensionstate(struct mansession *s, const struct message *m)
{
	const char *exten = astman_get_header(m, "Exten");
	const char *context = astman_get_header(m, "Context");
	const char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	char hint[256] = "";
	int status;
	if (ast_strlen_zero(exten)) {
		astman_send_error(s, m, "Extension not specified");
		return 0;
	}
	if (ast_strlen_zero(context))
		context = "default";
	status = ast_extension_state(NULL, context, exten);
	ast_get_hint(hint, sizeof(hint) - 1, NULL, 0, NULL, context, exten);
        if (!ast_strlen_zero(id)) {
                snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
        }
	astman_append(s, "Response: Success\r\n"
			           "%s"
				   "Message: Extension Status\r\n"
				   "Exten: %s\r\n"
				   "Context: %s\r\n"
				   "Hint: %s\r\n"
		 		   "Status: %d\r\n\r\n",
				   idText,exten, context, hint, status);
	return 0;
}

static char mandescr_timeout[] = 
"Description: Hangup a channel after a certain time.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel name to hangup\n"
"	*Timeout: Maximum duration of the call (sec)\n"
"Acknowledges set time with 'Timeout Set' message\n";

static int action_timeout(struct mansession *s, const struct message *m)
{
	struct ast_channel *c = NULL;
	const char *name = astman_get_header(m, "Channel");
	int timeout = atoi(astman_get_header(m, "Timeout"));
	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}
	if (!timeout) {
		astman_send_error(s, m, "No timeout specified");
		return 0;
	}
	c = ast_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}
	ast_channel_setwhentohangup(c, timeout);
	ast_channel_unlock(c);
	astman_send_ack(s, m, "Timeout Set");
	return 0;
}

static int process_events(struct mansession *s)
{
	struct eventqent *eqe;
	int ret = 0;
	ast_mutex_lock(&s->session->__lock);
	if (!s->session->eventq)
		s->session->eventq = master_eventq;
	while(s->session->eventq->next) {
		eqe = s->session->eventq->next;
		if ((s->session->authenticated && (s->session->readperm & eqe->category) == eqe->category) &&
				   ((s->session->send_events & eqe->category) == eqe->category)) {
			if (s->fd > -1) {
				if (!ret && ast_carefulwrite(s->fd, eqe->eventdata, strlen(eqe->eventdata), s->session->writetimeout) < 0)
					ret = -1;
			} else if (!s->session->outputstr && !(s->session->outputstr = ast_calloc(1, sizeof(*s->session->outputstr)))) 
				ret = -1;
			else 
				ast_dynamic_str_append(&s->session->outputstr, 0, "%s", eqe->eventdata);
		}
		unuse_eventqent(s->session->eventq);
		s->session->eventq = eqe;
	}
	ast_mutex_unlock(&s->session->__lock);
	return ret;
}

static char mandescr_userevent[] =
"Description: Send an event to manager sessions.\n"
"Variables: (Names marked with * are required)\n"
"       *UserEvent: EventStringToSend\n"
"       Header1: Content1\n"
"       HeaderN: ContentN\n";

static int action_userevent(struct mansession *s, const struct message *m)
{
	const char *event = astman_get_header(m, "UserEvent");
	char body[2048] = "";
	int x, bodylen = 0, xlen;
	for (x = 0; x < m->hdrcount; x++) {
		if (strncasecmp("UserEvent:", m->headers[x], strlen("UserEvent:"))) {
			if (sizeof(body) < bodylen + (xlen = strlen(m->headers[x])) + 3) {
				ast_log(LOG_WARNING, "UserEvent exceeds our buffer length.  Truncating.\n");
				break;
			}
			ast_copy_string(body + bodylen, m->headers[x], sizeof(body) - bodylen - 3);
			bodylen += xlen;
			ast_copy_string(body + bodylen, "\r\n", 3);
			bodylen += 2;
		}
	}

	manager_event(EVENT_FLAG_USER, "UserEvent", "UserEvent: %s\r\n%s", event, body);
	return 0;
}

static int process_message(struct mansession *s, const struct message *m)
{
	char action[80] = "";
	struct manager_action *tmp;
	const char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	int ret = 0;

	ast_copy_string(action, astman_get_header(m, "Action"), sizeof(action));
	if (option_debug)
		ast_log( LOG_DEBUG, "Manager received command '%s'\n", action );

	if (ast_strlen_zero(action)) {
		astman_send_error(s, m, "Missing action in request");
		return 0;
	}
	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	}
	if (!s->session->authenticated) {
		if (!strcasecmp(action, "Challenge")) {
			const char *authtype = astman_get_header(m, "AuthType");

			if (!strcasecmp(authtype, "MD5")) {
				if (ast_strlen_zero(s->session->challenge))
					snprintf(s->session->challenge, sizeof(s->session->challenge), "%ld", ast_random());
				astman_append(s, "Response: Success\r\n"
						"%s"
						"Challenge: %s\r\n\r\n",
						idText, s->session->challenge);
				return 0;
			} else {
				astman_send_error(s, m, "Must specify AuthType");
				return 0;
			}
		} else if (!strcasecmp(action, "Login")) {
			if (authenticate(s, m)) {
				sleep(1);
				astman_send_error(s, m, "Authentication failed");
				return -1;
			} else {
				s->session->authenticated = 1;
				if (option_verbose > 1) {
					if (displayconnects) {
						ast_verbose(VERBOSE_PREFIX_2 "%sManager '%s' logged on from %s\n", 
							(s->session->sessiontimeout ? "HTTP " : ""), s->session->username, ast_inet_ntoa(s->session->sin.sin_addr));
					}
				}
				ast_log(LOG_EVENT, "%sManager '%s' logged on from %s\n", 
					(s->session->sessiontimeout ? "HTTP " : ""), s->session->username, ast_inet_ntoa(s->session->sin.sin_addr));
				astman_send_ack(s, m, "Authentication accepted");
			}
		} else if (!strcasecmp(action, "Logoff")) {
			astman_send_ack(s, m, "See ya");
			return -1;
		} else
			astman_send_error(s, m, "Authentication Required");
	} else {
		if (!strcasecmp(action, "Login"))
			astman_send_ack(s, m, "Already logged in");
		else {
			ast_rwlock_rdlock(&actionlock);
			for (tmp = first_action; tmp; tmp = tmp->next) { 		
				if (strcasecmp(action, tmp->action))
					continue;
				if ((s->session->writeperm & tmp->authority) == tmp->authority) {
					if (tmp->func(s, m))
						ret = -1;
				} else
					astman_send_error(s, m, "Permission denied");
				break;
			}
			ast_rwlock_unlock(&actionlock);
			if (!tmp)
				astman_send_error(s, m, "Invalid/unknown command");
		}
	}
	if (ret)
		return ret;
	return process_events(s);
}

static int get_input(struct mansession_session *s, char *output)
{
	/* output must have at least sizeof(s->inbuf) space */
	int res;
	int x;
	struct pollfd fds[1];
	for (x = 1; x < s->inlen; x++) {
		if ((s->inbuf[x] == '\n') && (s->inbuf[x-1] == '\r')) {
			/* Copy output data up to and including \r\n */
			memcpy(output, s->inbuf, x + 1);
			/* Add trailing \0 */
			output[x+1] = '\0';
			/* Move remaining data back to the front */
			memmove(s->inbuf, s->inbuf + x + 1, s->inlen - x);
			s->inlen -= (x + 1);
			return 1;
		}
	} 
	if (s->inlen >= sizeof(s->inbuf) - 1) {
		ast_log(LOG_WARNING, "Dumping long line with no return from %s: %s\n", ast_inet_ntoa(s->sin.sin_addr), s->inbuf);
		s->inlen = 0;
	}
	fds[0].fd = s->fd;
	fds[0].events = POLLIN;
	do {
		ast_mutex_lock(&s->__lock);
		if (s->pending_event) {
			s->pending_event = 0;
			ast_mutex_unlock(&s->__lock);
			return 0;
		}
		s->waiting_thread = pthread_self();
		ast_mutex_unlock(&s->__lock);

		res = ast_poll(fds, 1, -1);

		ast_mutex_lock(&s->__lock);
		s->waiting_thread = AST_PTHREADT_NULL;
		ast_mutex_unlock(&s->__lock);
		if (res < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				return 0;
			}
			ast_log(LOG_WARNING, "Select returned error: %s\n", strerror(errno));
	 		return -1;
		} else if (res > 0) {
			ast_mutex_lock(&s->__lock);
			res = read(s->fd, s->inbuf + s->inlen, sizeof(s->inbuf) - 1 - s->inlen);
			ast_mutex_unlock(&s->__lock);
			if (res < 1)
				return -1;
			break;
		}
	} while(1);
	s->inlen += res;
	s->inbuf[s->inlen] = '\0';
	return 0;
}

static int do_message(struct mansession *s)
{
	struct message m = { 0 };
	char header_buf[sizeof(s->session->inbuf)] = { '\0' };
	int res;

	for (;;) {
		/* Check if any events are pending and do them if needed */
		if (s->session->eventq->next) {
			if (process_events(s))
				return -1;
		}
		res = get_input(s->session, header_buf);
		if (res == 0) {
			continue;
		} else if (res > 0) {
			/* Strip trailing \r\n */
			if (strlen(header_buf) < 2)
				continue;
			header_buf[strlen(header_buf) - 2] = '\0';
			if (ast_strlen_zero(header_buf))
				return process_message(s, &m) ? -1 : 0;
			else if (m.hdrcount < (AST_MAX_MANHEADERS - 1))
				m.headers[m.hdrcount++] = ast_strdupa(header_buf);
		} else {
			return res;
		}
	}
}

static void *session_do(void *data)
{
	struct mansession_session *session = data;
	int res;
	struct mansession s = { .session = session, .fd = session->fd };

	astman_append(&s, "Asterisk Call Manager/1.0\r\n");
	for (;;) {
		if ((res = do_message(&s)) < 0)
			break;
	}
	if (session->authenticated) {
		if (option_verbose > 1) {
			if (displayconnects) 
				ast_verbose(VERBOSE_PREFIX_2 "Manager '%s' logged off from %s\n", session->username, ast_inet_ntoa(session->sin.sin_addr));
		}
		ast_log(LOG_EVENT, "Manager '%s' logged off from %s\n", session->username, ast_inet_ntoa(session->sin.sin_addr));
	} else {
		if (option_verbose > 1) {
			if (displayconnects)
				ast_verbose(VERBOSE_PREFIX_2 "Connect attempt from '%s' unable to authenticate\n", ast_inet_ntoa(session->sin.sin_addr));
		}
		ast_log(LOG_EVENT, "Failed attempt from %s\n", ast_inet_ntoa(session->sin.sin_addr));
	}

	/* It is possible under certain circumstances for this session thread
	   to complete its work and exit *before* the thread that created it
	   has finished executing the ast_pthread_create_background() function.
	   If this occurs, some versions of glibc appear to act in a buggy
	   fashion and attempt to write data into memory that it thinks belongs
	   to the thread but is in fact not owned by the thread (or may have
	   been freed completely).

	   Causing this thread to yield to other threads at least one time
	   appears to work around this bug.
	*/
	usleep(1);

	destroy_session(session);
	return NULL;
}

static void *accept_thread(void *ignore)
{
	int as;
	struct sockaddr_in sin;
	socklen_t sinlen;
	struct eventqent *eqe;
	struct mansession_session *s;
	struct protoent *p;
	int arg = 1;
	int flags;
	pthread_attr_t attr;
	time_t now;
	struct pollfd pfds[1];

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	for (;;) {
		time(&now);
		AST_LIST_LOCK(&sessions);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&sessions, s, list) {
			if (s->sessiontimeout && (now > s->sessiontimeout) && !s->inuse) {
				AST_LIST_REMOVE_CURRENT(&sessions, list);
				num_sessions--;
				if (s->authenticated && (option_verbose > 1) && displayconnects) {
					ast_verbose(VERBOSE_PREFIX_2 "HTTP Manager '%s' timed out from %s\n",
						s->username, ast_inet_ntoa(s->sin.sin_addr));
				}
				free_session(s);
				break;	
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
		/* Purge master event queue of old, unused events, but make sure we
		   always keep at least one in the queue */
		eqe = master_eventq;
		while (master_eventq->next && !master_eventq->usecount) {
			eqe = master_eventq;
			master_eventq = master_eventq->next;
			free(eqe);
		}
		AST_LIST_UNLOCK(&sessions);

		sinlen = sizeof(sin);
		pfds[0].fd = asock;
		pfds[0].events = POLLIN;
		/* Wait for something to happen, but timeout every few seconds so
		   we can ditch any old manager sessions */
		if (ast_poll(pfds, 1, 5000) < 1)
			continue;
		as = accept(asock, (struct sockaddr *)&sin, &sinlen);
		if (as < 0) {
			ast_log(LOG_NOTICE, "Accept returned -1: %s\n", strerror(errno));
			continue;
		}
		p = getprotobyname("tcp");
		if (p) {
			if( setsockopt(as, p->p_proto, TCP_NODELAY, (char *)&arg, sizeof(arg) ) < 0 ) {
				ast_log(LOG_WARNING, "Failed to set manager tcp connection to TCP_NODELAY mode: %s\n", strerror(errno));
			}
		}
		if (!(s = ast_calloc(1, sizeof(*s))))
			continue;

		memcpy(&s->sin, &sin, sizeof(sin));
		s->writetimeout = 100;
		s->waiting_thread = AST_PTHREADT_NULL;

		if (!block_sockets) {
			/* For safety, make sure socket is non-blocking */
			flags = fcntl(as, F_GETFL);
			fcntl(as, F_SETFL, flags | O_NONBLOCK);
		} else {
			flags = fcntl(as, F_GETFL);
			fcntl(as, F_SETFL, flags & ~O_NONBLOCK);
		}
		ast_mutex_init(&s->__lock);
		s->fd = as;
		s->send_events = -1;
		AST_LIST_LOCK(&sessions);
		AST_LIST_INSERT_HEAD(&sessions, s, list);
		num_sessions++;
		/* Find the last place in the master event queue and hook ourselves
		   in there */
		s->eventq = master_eventq;
		while(s->eventq->next)
			s->eventq = s->eventq->next;
		ast_atomic_fetchadd_int(&s->eventq->usecount, 1);
		AST_LIST_UNLOCK(&sessions);
		if (ast_pthread_create_background(&s->t, &attr, session_do, s))
			destroy_session(s);
	}
	pthread_attr_destroy(&attr);
	return NULL;
}

static int append_event(const char *str, int category)
{
	struct eventqent *tmp, *prev = NULL;
	tmp = ast_malloc(sizeof(*tmp) + strlen(str));

	if (!tmp)
		return -1;

	tmp->next = NULL;
	tmp->category = category;
	strcpy(tmp->eventdata, str);
	
	if (master_eventq) {
		prev = master_eventq;
		while (prev->next) 
			prev = prev->next;
		prev->next = tmp;
	} else {
		master_eventq = tmp;
	}
	
	tmp->usecount = num_sessions;
	
	return 0;
}

/*! \brief  manager_event: Send AMI event to client */
int manager_event(int category, const char *event, const char *fmt, ...)
{
	struct mansession_session *s;
	char auth[80];
	va_list ap;
	struct timeval now;
	struct ast_dynamic_str *buf;

	/* Abort if there aren't any manager sessions */
	if (!num_sessions)
		return 0;

	if (!(buf = ast_dynamic_str_thread_get(&manager_event_buf, MANAGER_EVENT_BUF_INITSIZE)))
		return -1;

	ast_dynamic_str_thread_set(&buf, 0, &manager_event_buf,
			"Event: %s\r\nPrivilege: %s\r\n",
			 event, authority_to_str(category, auth, sizeof(auth)));

	if (timestampevents) {
		now = ast_tvnow();
		ast_dynamic_str_thread_append(&buf, 0, &manager_event_buf,
				"Timestamp: %ld.%06lu\r\n",
				 now.tv_sec, (unsigned long) now.tv_usec);
	}

	va_start(ap, fmt);
	ast_dynamic_str_thread_append_va(&buf, 0, &manager_event_buf, fmt, ap);
	va_end(ap);
	
	ast_dynamic_str_thread_append(&buf, 0, &manager_event_buf, "\r\n");	
	
	/* Append event to master list and wake up any sleeping sessions */
	AST_LIST_LOCK(&sessions);
	append_event(buf->str, category);
	AST_LIST_TRAVERSE(&sessions, s, list) {
		ast_mutex_lock(&s->__lock);
		if (s->waiting_thread != AST_PTHREADT_NULL)
			pthread_kill(s->waiting_thread, SIGURG);
		else
			/* We have an event to process, but the mansession is
			 * not waiting for it. We still need to indicate that there
			 * is an event waiting so that get_input processes the pending
			 * event instead of polling.
			 */
			s->pending_event = 1;
		ast_mutex_unlock(&s->__lock);
	}
	AST_LIST_UNLOCK(&sessions);

	return 0;
}

int ast_manager_unregister(char *action) 
{
	struct manager_action *cur, *prev;
	struct timespec tv = { 5, };

	if (ast_rwlock_timedwrlock(&actionlock, &tv)) {
		ast_log(LOG_ERROR, "Could not obtain lock on manager list\n");
		return -1;
	}
	cur = prev = first_action;
	while (cur) {
		if (!strcasecmp(action, cur->action)) {
			prev->next = cur->next;
			free(cur);
			if (option_verbose > 1) 
				ast_verbose(VERBOSE_PREFIX_2 "Manager unregistered action %s\n", action);
			ast_rwlock_unlock(&actionlock);
			return 0;
		}
		prev = cur;
		cur = cur->next;
	}
	ast_rwlock_unlock(&actionlock);
	return 0;
}

static int manager_state_cb(char *context, char *exten, int state, void *data)
{
	/* Notify managers of change */
	manager_event(EVENT_FLAG_CALL, "ExtensionStatus", "Exten: %s\r\nContext: %s\r\nStatus: %d\r\n", exten, context, state);
	return 0;
}

static int ast_manager_register_struct(struct manager_action *act)
{
	struct manager_action *cur, *prev = NULL;
	int ret;
	struct timespec tv = { 5, };

	if (ast_rwlock_timedwrlock(&actionlock, &tv)) {
		ast_log(LOG_ERROR, "Could not obtain lock on manager list\n");
		return -1;
	}
	cur = first_action;
	while (cur) { /* Walk the list of actions */
		ret = strcasecmp(cur->action, act->action);
		if (ret == 0) {
			ast_log(LOG_WARNING, "Manager: Action '%s' already registered\n", act->action);
			ast_rwlock_unlock(&actionlock);
			return -1;
		} else if (ret > 0) {
			/* Insert these alphabetically */
			if (prev) {
				act->next = prev->next;
				prev->next = act;
			} else {
				act->next = first_action;
				first_action = act;
			}
			break;
		}
		prev = cur; 
		cur = cur->next;
	}
	
	if (!cur) {
		if (prev)
			prev->next = act;
		else
			first_action = act;
		act->next = NULL;
	}

	if (option_verbose > 1) 
		ast_verbose(VERBOSE_PREFIX_2 "Manager registered action %s\n", act->action);
	ast_rwlock_unlock(&actionlock);
	return 0;
}

/*! \brief register a new command with manager, including online help. This is 
	the preferred way to register a manager command */
int ast_manager_register2(const char *action, int auth, int (*func)(struct mansession *s, const struct message *m), const char *synopsis, const char *description)
{
	struct manager_action *cur;

	cur = ast_malloc(sizeof(*cur));
	if (!cur)
		return -1;
	
	cur->action = action;
	cur->authority = auth;
	cur->func = func;
	cur->synopsis = synopsis;
	cur->description = description;
	cur->next = NULL;

	if (ast_manager_register_struct(cur)) {
		ast_free(cur);
		return -1;
	}

	return 0;
}
/*! @}
 END Doxygen group */

static struct mansession_session *find_session(uint32_t ident)
{
	struct mansession_session *s;

	AST_LIST_LOCK(&sessions);
	AST_LIST_TRAVERSE(&sessions, s, list) {
		ast_mutex_lock(&s->__lock);
		if (s->sessiontimeout && (s->managerid == ident) && !s->needdestroy) {
			s->inuse++;
			break;
		}
		ast_mutex_unlock(&s->__lock);
	}
	AST_LIST_UNLOCK(&sessions);

	return s;
}

int astman_verify_session_readpermissions(uint32_t ident, int perm)
{
	int result = 0;
	struct mansession_session *s;

	AST_LIST_LOCK(&sessions);
	AST_LIST_TRAVERSE(&sessions, s, list) {
		ast_mutex_lock(&s->__lock);
		if ((s->managerid == ident) && (s->readperm & perm)) {
			result = 1;
			ast_mutex_unlock(&s->__lock);
			break;
		}
		ast_mutex_unlock(&s->__lock);
	}
	AST_LIST_UNLOCK(&sessions);
	return result;
}

int astman_verify_session_writepermissions(uint32_t ident, int perm)
{
	int result = 0;
	struct mansession_session *s;

	AST_LIST_LOCK(&sessions);
	AST_LIST_TRAVERSE(&sessions, s, list) {
		ast_mutex_lock(&s->__lock);
		if ((s->managerid == ident) && (s->writeperm & perm)) {
			result = 1;
			ast_mutex_unlock(&s->__lock);
			break;
		}
		ast_mutex_unlock(&s->__lock);
	}
	AST_LIST_UNLOCK(&sessions);
	return result;
}

enum {
	FORMAT_RAW,
	FORMAT_HTML,
	FORMAT_XML,
};
static char *contenttype[] = { "plain", "html", "xml" };

static char *generic_http_callback(int format, struct sockaddr_in *requestor, const char *uri, struct ast_variable *params, int *status, char **title, int *contentlength)
{
	struct mansession_session *s = NULL;
	struct mansession ss = { .session = NULL, };
	uint32_t ident = 0;
	char workspace[512];
	char cookie[128];
	size_t len = sizeof(workspace);
	int blastaway = 0;
	char *c = workspace;
	char *retval = NULL;
	struct ast_variable *v;

	for (v = params; v; v = v->next) {
		if (!strcasecmp(v->name, "mansession_id")) {
			sscanf(v->value, "%x", &ident);
			break;
		}
	}
	
	if (!(s = find_session(ident))) {
		/* Create new session */
		if (!(s = ast_calloc(1, sizeof(*s)))) {
			*status = 500;
			goto generic_callback_out;
		}
		memcpy(&s->sin, requestor, sizeof(s->sin));
		s->fd = -1;
		s->waiting_thread = AST_PTHREADT_NULL;
		s->send_events = 0;
		ast_mutex_init(&s->__lock);
		ast_mutex_lock(&s->__lock);
		s->inuse = 1;
		/*!\note There is approximately a 1 in 1.8E19 chance that the following
		 * calculation will produce 0, which is an invalid ID, but due to the
		 * properties of the rand() function (and the constantcy of s), that
		 * won't happen twice in a row.
		 */
		while ((s->managerid = rand() ^ (unsigned long) s) == 0);
		AST_LIST_LOCK(&sessions);
		AST_LIST_INSERT_HEAD(&sessions, s, list);
		/* Hook into the last spot in the event queue */
		s->eventq = master_eventq;
		while (s->eventq->next)
			s->eventq = s->eventq->next;
		ast_atomic_fetchadd_int(&s->eventq->usecount, 1);
		ast_atomic_fetchadd_int(&num_sessions, 1);
		AST_LIST_UNLOCK(&sessions);
	}

	/* Reset HTTP timeout.  If we're not yet authenticated, keep it extremely short */
	time(&s->sessiontimeout);
	if (!s->authenticated && (httptimeout > 5))
		s->sessiontimeout += 5;
	else
		s->sessiontimeout += httptimeout;
	ss.session = s;
	ast_mutex_unlock(&s->__lock);

	ss.f = tmpfile();
	ss.fd = fileno(ss.f);

	if (s) {
		struct message m = { 0 };
		char tmp[80];
		unsigned int x;
		size_t hdrlen;

		for (x = 0, v = params; v && (x < AST_MAX_MANHEADERS); x++, v = v->next) {
			hdrlen = strlen(v->name) + strlen(v->value) + 3;
			m.headers[m.hdrcount] = alloca(hdrlen);
			snprintf((char *) m.headers[m.hdrcount], hdrlen, "%s: %s", v->name, v->value);
			m.hdrcount = x + 1;
		}

		if (process_message(&ss, &m)) {
			if (s->authenticated) {
				if (option_verbose > 1) {
					if (displayconnects) 
						ast_verbose(VERBOSE_PREFIX_2 "HTTP Manager '%s' logged off from %s\n", s->username, ast_inet_ntoa(s->sin.sin_addr));    
				}
				ast_log(LOG_EVENT, "HTTP Manager '%s' logged off from %s\n", s->username, ast_inet_ntoa(s->sin.sin_addr));
			} else {
				if (option_verbose > 1) {
					if (displayconnects)
						ast_verbose(VERBOSE_PREFIX_2 "HTTP Connect attempt from '%s' unable to authenticate\n", ast_inet_ntoa(s->sin.sin_addr));
				}
				ast_log(LOG_EVENT, "HTTP Failed attempt from %s\n", ast_inet_ntoa(s->sin.sin_addr));
			}
			s->needdestroy = 1;
		}
		ast_build_string(&c, &len, "Content-type: text/%s\r\n", contenttype[format]);
		sprintf(tmp, "%08x", s->managerid);
		ast_build_string(&c, &len, "%s\r\n", ast_http_setcookie("mansession_id", tmp, httptimeout, cookie, sizeof(cookie)));
		if (format == FORMAT_HTML)
			ast_build_string(&c, &len, "<title>Asterisk&trade; Manager Interface</title>");
		if (format == FORMAT_XML) {
			ast_build_string(&c, &len, "<ajax-response>\n");
		} else if (format == FORMAT_HTML) {
			ast_build_string(&c, &len, "<body bgcolor=\"#ffffff\"><table align=center bgcolor=\"#f1f1f1\" width=\"500\">\r\n");
			ast_build_string(&c, &len, "<tr><td colspan=\"2\" bgcolor=\"#f1f1ff\"><h1>&nbsp;&nbsp;Manager Tester</h1></td></tr>\r\n");
		}
		ast_mutex_lock(&s->__lock);
		if (ss.fd > -1) {
			char *buf;
			size_t l = lseek(ss.fd, 0, SEEK_END);
			if (l) {
				if (MAP_FAILED == (buf = mmap(NULL, l, PROT_READ | PROT_WRITE, MAP_PRIVATE, ss.fd, 0))) {
					ast_log(LOG_WARNING, "mmap failed.  Manager request output was not processed\n");
				} else {
					char *tmpbuf;
					if (format == FORMAT_XML)
						tmpbuf = xml_translate(buf, params);
					else if (format == FORMAT_HTML)
						tmpbuf = html_translate(buf);
					else
						tmpbuf = buf;
					if (tmpbuf) {
						size_t wlen, tlen;
						if ((retval = malloc((wlen = strlen(workspace)) + (tlen = strlen(tmpbuf)) + 128))) {
							strcpy(retval, workspace);
							strcpy(retval + wlen, tmpbuf);
							c = retval + wlen + tlen;
							/* Leftover space for footer, if any */
							len = 120;
						}
					}
					if (tmpbuf != buf)
						free(tmpbuf);
					free(s->outputstr);
					s->outputstr = NULL;
					munmap(buf, l);
				}
			}
			fclose(ss.f);
			ss.f = NULL;
			ss.fd = -1;
		} else if (s->outputstr) {
			char *tmp;
			if (format == FORMAT_XML)
				tmp = xml_translate(s->outputstr->str, params);
			else if (format == FORMAT_HTML)
				tmp = html_translate(s->outputstr->str);
			else
				tmp = s->outputstr->str;
			if (tmp) {
				retval = malloc(strlen(workspace) + strlen(tmp) + 128);
				if (retval) {
					strcpy(retval, workspace);
					strcpy(retval + strlen(retval), tmp);
					c = retval + strlen(retval);
					len = 120;
				}
			}
			if (tmp != s->outputstr->str)
				free(tmp);
			free(s->outputstr);
			s->outputstr = NULL;
		}
		ast_mutex_unlock(&s->__lock);
		/* Still okay because c would safely be pointing to workspace even
		   if retval failed to allocate above */
		if (format == FORMAT_XML) {
			ast_build_string(&c, &len, "</ajax-response>\n");
		} else if (format == FORMAT_HTML)
			ast_build_string(&c, &len, "</table></body>\r\n");
	} else {
		*status = 500;
		*title = strdup("Server Error");
	}
	ast_mutex_lock(&s->__lock);
	if (s->needdestroy) {
		if (s->inuse == 1) {
			ast_log(LOG_DEBUG, "Need destroy, doing it now!\n");
			blastaway = 1;
		} else {
			ast_log(LOG_DEBUG, "Need destroy, but can't do it yet!\n");
			if (s->waiting_thread != AST_PTHREADT_NULL)
				pthread_kill(s->waiting_thread, SIGURG);
			s->inuse--;
		}
	} else
		s->inuse--;
	ast_mutex_unlock(&s->__lock);
	
	if (blastaway)
		destroy_session(s);
generic_callback_out:
	if (*status != 200)
		return ast_http_error(500, "Server Error", NULL, "Internal Server Error (out of memory)\n"); 
	return retval;
}

static char *manager_http_callback(struct sockaddr_in *requestor, const char *uri, struct ast_variable *params, int *status, char **title, int *contentlength)
{
	return generic_http_callback(FORMAT_HTML, requestor, uri, params, status, title, contentlength);
}

static char *mxml_http_callback(struct sockaddr_in *requestor, const char *uri, struct ast_variable *params, int *status, char **title, int *contentlength)
{
	return generic_http_callback(FORMAT_XML, requestor, uri, params, status, title, contentlength);
}

static char *rawman_http_callback(struct sockaddr_in *requestor, const char *uri, struct ast_variable *params, int *status, char **title, int *contentlength)
{
	return generic_http_callback(FORMAT_RAW, requestor, uri, params, status, title, contentlength);
}

struct ast_http_uri rawmanuri = {
	.description = "Raw HTTP Manager Event Interface",
	.uri = "rawman",
	.has_subtree = 0,
	.callback = rawman_http_callback,
};

struct ast_http_uri manageruri = {
	.description = "HTML Manager Event Interface",
	.uri = "manager",
	.has_subtree = 0,
	.callback = manager_http_callback,
};

struct ast_http_uri managerxmluri = {
	.description = "XML Manager Event Interface",
	.uri = "mxml",
	.has_subtree = 0,
	.callback = mxml_http_callback,
};

static int registered = 0;
static int webregged = 0;

int init_manager(void)
{
	struct ast_config *cfg = NULL, *ucfg = NULL;
	const char *val;
	char *cat = NULL;
	int oldportno = portno;
	static struct sockaddr_in ba;
	int x = 1;
	int flags;
	int webenabled = 0;
	int newhttptimeout = 60;
	struct ast_manager_user *user = NULL;

	if (!registered) {
		/* Register default actions */
		ast_manager_register2("Ping", 0, action_ping, "Keepalive command", mandescr_ping);
		ast_manager_register2("Events", 0, action_events, "Control Event Flow", mandescr_events);
		ast_manager_register2("Logoff", 0, action_logoff, "Logoff Manager", mandescr_logoff);
		ast_manager_register2("Hangup", EVENT_FLAG_CALL, action_hangup, "Hangup Channel", mandescr_hangup);
		ast_manager_register("Status", EVENT_FLAG_CALL, action_status, "Lists channel status" );
		ast_manager_register2("Setvar", EVENT_FLAG_CALL, action_setvar, "Set Channel Variable", mandescr_setvar );
		ast_manager_register2("Getvar", EVENT_FLAG_CALL, action_getvar, "Gets a Channel Variable", mandescr_getvar );
		ast_manager_register2("GetConfig", EVENT_FLAG_CONFIG, action_getconfig, "Retrieve configuration", mandescr_getconfig);
		ast_manager_register2("UpdateConfig", EVENT_FLAG_CONFIG, action_updateconfig, "Update basic configuration", mandescr_updateconfig);
		ast_manager_register2("Redirect", EVENT_FLAG_CALL, action_redirect, "Redirect (transfer) a call", mandescr_redirect );
		ast_manager_register2("Originate", EVENT_FLAG_CALL, action_originate, "Originate Call", mandescr_originate);
		ast_manager_register2("Command", EVENT_FLAG_COMMAND, action_command, "Execute Asterisk CLI Command", mandescr_command );
		ast_manager_register2("ExtensionState", EVENT_FLAG_CALL, action_extensionstate, "Check Extension Status", mandescr_extensionstate );
		ast_manager_register2("AbsoluteTimeout", EVENT_FLAG_CALL, action_timeout, "Set Absolute Timeout", mandescr_timeout );
		ast_manager_register2("MailboxStatus", EVENT_FLAG_CALL, action_mailboxstatus, "Check Mailbox", mandescr_mailboxstatus );
		ast_manager_register2("MailboxCount", EVENT_FLAG_CALL, action_mailboxcount, "Check Mailbox Message Count", mandescr_mailboxcount );
		ast_manager_register2("ListCommands", 0, action_listcommands, "List available manager commands", mandescr_listcommands);
		ast_manager_register2("UserEvent", EVENT_FLAG_USER, action_userevent, "Send an arbitrary event", mandescr_userevent);
		ast_manager_register2("WaitEvent", 0, action_waitevent, "Wait for an event to occur", mandescr_waitevent);

		ast_cli_register_multiple(cli_manager, sizeof(cli_manager) / sizeof(struct ast_cli_entry));
		ast_extension_state_add(NULL, NULL, manager_state_cb, NULL);
		registered = 1;
		/* Append placeholder event so master_eventq never runs dry */
		append_event("Event: Placeholder\r\n\r\n", 0);
	}
	portno = DEFAULT_MANAGER_PORT;
	displayconnects = 1;
	cfg = ast_config_load("manager.conf");
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open management configuration manager.conf.  Call management disabled.\n");
		return 0;
	}
	val = ast_variable_retrieve(cfg, "general", "enabled");
	if (val)
		enabled = ast_true(val);

	val = ast_variable_retrieve(cfg, "general", "block-sockets");
	if (val)
		block_sockets = ast_true(val);

	val = ast_variable_retrieve(cfg, "general", "webenabled");
	if (val)
		webenabled = ast_true(val);

	if ((val = ast_variable_retrieve(cfg, "general", "port"))) {
		if (sscanf(val, "%d", &portno) != 1) {
			ast_log(LOG_WARNING, "Invalid port number '%s'\n", val);
			portno = DEFAULT_MANAGER_PORT;
		}
	}

	if ((val = ast_variable_retrieve(cfg, "general", "displayconnects")))
		displayconnects = ast_true(val);

	if ((val = ast_variable_retrieve(cfg, "general", "timestampevents")))
		timestampevents = ast_true(val);

	if ((val = ast_variable_retrieve(cfg, "general", "httptimeout")))
		newhttptimeout = atoi(val);

	memset(&ba, 0, sizeof(ba));
	ba.sin_family = AF_INET;
	ba.sin_port = htons(portno);

	if ((val = ast_variable_retrieve(cfg, "general", "bindaddr"))) {
		if (!inet_aton(val, &ba.sin_addr)) { 
			ast_log(LOG_WARNING, "Invalid address '%s' specified, using 0.0.0.0\n", val);
			memset(&ba.sin_addr, 0, sizeof(ba.sin_addr));
		}
	}
	

	if ((asock > -1) && ((portno != oldportno) || !enabled)) {
#if 0
		/* Can't be done yet */
		close(asock);
		asock = -1;
#else
		ast_log(LOG_WARNING, "Unable to change management port / enabled\n");
#endif
	}

	AST_LIST_LOCK(&users);

	if ((ucfg = ast_config_load("users.conf"))) {
		while ((cat = ast_category_browse(ucfg, cat))) {
			int hasmanager = 0;
			struct ast_variable *var = NULL;

			if (!strcasecmp(cat, "general")) {
				continue;
			}

			if (!(hasmanager = ast_true(ast_variable_retrieve(ucfg, cat, "hasmanager")))) {
				continue;
			}

			/* Look for an existing entry, if none found - create one and add it to the list */
			if (!(user = ast_get_manager_by_name_locked(cat))) {
				if (!(user = ast_calloc(1, sizeof(*user)))) {
					break;
				}
				/* Copy name over */
				ast_copy_string(user->username, cat, sizeof(user->username));
				/* Insert into list */
				AST_LIST_INSERT_TAIL(&users, user, list);
			}

			/* Make sure we keep this user and don't destroy it during cleanup */
			user->keep = 1;

			for (var = ast_variable_browse(ucfg, cat); var; var = var->next) {
				if (!strcasecmp(var->name, "secret")) {
					if (user->secret) {
						free(user->secret);
					}
					user->secret = ast_strdup(var->value);
				} else if (!strcasecmp(var->name, "deny") ) {
					if (user->deny) {
						free(user->deny);
					}
					user->deny = ast_strdup(var->value);
				} else if (!strcasecmp(var->name, "permit") ) {
					if (user->permit) {
						free(user->permit);
					}
					user->permit = ast_strdup(var->value);
				} else if (!strcasecmp(var->name, "read") ) {
					if (user->read) {
						free(user->read);
					}
					user->read = ast_strdup(var->value);
				} else if (!strcasecmp(var->name, "write") ) {
					if (user->write) {
						free(user->write);
					}
					user->write = ast_strdup(var->value);
				} else if (!strcasecmp(var->name, "displayconnects") ) {
					user->displayconnects = ast_true(var->value);
				} else if (!strcasecmp(var->name, "hasmanager")) {
					/* already handled */
				} else {
					ast_log(LOG_DEBUG, "%s is an unknown option (to the manager module).\n", var->name);
				}
			}
		}
		ast_config_destroy(ucfg);
	}

	while ((cat = ast_category_browse(cfg, cat))) {
		struct ast_variable *var = NULL;

		if (!strcasecmp(cat, "general"))
			continue;

		/* Look for an existing entry, if none found - create one and add it to the list */
		if (!(user = ast_get_manager_by_name_locked(cat))) {
			if (!(user = ast_calloc(1, sizeof(*user))))
				break;
			/* Copy name over */
			ast_copy_string(user->username, cat, sizeof(user->username));
			/* Insert into list */
			AST_LIST_INSERT_TAIL(&users, user, list);
		}

		/* Make sure we keep this user and don't destroy it during cleanup */
		user->keep = 1;

		var = ast_variable_browse(cfg, cat);
		while (var) {
			if (!strcasecmp(var->name, "secret")) {
				if (user->secret)
					free(user->secret);
				user->secret = ast_strdup(var->value);
			} else if (!strcasecmp(var->name, "deny") ) {
				if (user->deny)
					free(user->deny);
				user->deny = ast_strdup(var->value);
			} else if (!strcasecmp(var->name, "permit") ) {
				if (user->permit)
					free(user->permit);
				user->permit = ast_strdup(var->value);
			}  else if (!strcasecmp(var->name, "read") ) {
				if (user->read)
					free(user->read);
				user->read = ast_strdup(var->value);
			}  else if (!strcasecmp(var->name, "write") ) {
				if (user->write)
					free(user->write);
				user->write = ast_strdup(var->value);
			}  else if (!strcasecmp(var->name, "displayconnects") )
				user->displayconnects = ast_true(var->value);
			else
				ast_log(LOG_DEBUG, "%s is an unknown option.\n", var->name);
			var = var->next;
		}
	}

	/* Perform cleanup - essentially prune out old users that no longer exist */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&users, user, list) {
		if (user->keep) {
			user->keep = 0;
			continue;
		}
		/* We do not need to keep this user so take them out of the list */
		AST_LIST_REMOVE_CURRENT(&users, list);
		/* Free their memory now */
		if (user->secret)
			free(user->secret);
		if (user->deny)
			free(user->deny);
		if (user->permit)
			free(user->permit);
		if (user->read)
			free(user->read);
		if (user->write)
			free(user->write);
		free(user);
	}
	AST_LIST_TRAVERSE_SAFE_END

	AST_LIST_UNLOCK(&users);

	ast_config_destroy(cfg);
	
	if (webenabled && enabled) {
		if (!webregged) {
			ast_http_uri_link(&rawmanuri);
			ast_http_uri_link(&manageruri);
			ast_http_uri_link(&managerxmluri);
			webregged = 1;
		}
	} else {
		if (webregged) {
			ast_http_uri_unlink(&rawmanuri);
			ast_http_uri_unlink(&manageruri);
			ast_http_uri_unlink(&managerxmluri);
			webregged = 0;
		}
	}

	if (newhttptimeout > 0)
		httptimeout = newhttptimeout;

	/* If not enabled, do nothing */
	if (!enabled)
		return 0;

	if (asock < 0) {
		asock = socket(AF_INET, SOCK_STREAM, 0);
		if (asock < 0) {
			ast_log(LOG_WARNING, "Unable to create socket: %s\n", strerror(errno));
			return -1;
		}
		setsockopt(asock, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
		if (bind(asock, (struct sockaddr *)&ba, sizeof(ba))) {
			ast_log(LOG_WARNING, "Unable to bind socket: %s\n", strerror(errno));
			close(asock);
			asock = -1;
			return -1;
		}
		if (listen(asock, 2)) {
			ast_log(LOG_WARNING, "Unable to listen on socket: %s\n", strerror(errno));
			close(asock);
			asock = -1;
			return -1;
		}
		flags = fcntl(asock, F_GETFL);
		fcntl(asock, F_SETFL, flags | O_NONBLOCK);
		if (option_verbose)
			ast_verbose("Asterisk Management interface listening on port %d\n", portno);
		ast_pthread_create_background(&t, NULL, accept_thread, NULL);
	}
	return 0;
}

int reload_manager(void)
{
	manager_event(EVENT_FLAG_SYSTEM, "Reload", "Message: Reload Requested\r\n");
	return init_manager();
}
