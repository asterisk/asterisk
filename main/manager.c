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
 * At the moment this file contains a number of functions, namely:
 *
 * - data structures storing AMI state
 * - AMI-related API functions, used by internal asterisk components
 * - handlers for AMI-related CLI functions
 * - handlers for AMI functions (available through the AMI socket)
 * - the code for the main AMI listener thread and individual session threads
 * - the http handlers invoked for AMI-over-HTTP by the threads in main/http.c
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

/*!
 * Linked list of events.
 * Global events are appended to the list by append_event().
 * The usecount is the number of stored pointers to the element,
 * excluding the list pointers. So an element that is only in
 * the list has a usecount of 0, not 1.
 *
 * Clients have a pointer to the last event processed, and for each
 * of these clients we track the usecount of the elements.
 * If we have a pointer to an entry in the list, it is safe to navigate
 * it forward because elements will not be deleted, but only appended.
 * The worst that can happen is seeing the pointer still NULL.
 *
 * When the usecount of an element drops to 0, and the element is the
 * first in the list, we can remove it. Removal is done within the
 * main thread, which is woken up for the purpose.
 *
 * For simplicity of implementation, we make sure the list is never empty.
 */
struct eventqent {
	int usecount;		/*!< # of clients who still need the event */
	int category;
	unsigned int seq;	/*!< sequence number */
	AST_LIST_ENTRY(eventqent) eq_next;
	char eventdata[1];	/*!< really variable size, allocated by append_event() */
};

static AST_LIST_HEAD_STATIC(all_events, eventqent);

static int enabled = 0;
static int portno = DEFAULT_MANAGER_PORT;
static int asock = -1;	/* the accept socket */
static int displayconnects = 1;
static int timestampevents = 0;
static int httptimeout = 60;

static pthread_t accept_thread_ptr;	/*!< the accept thread */
static int block_sockets = 0;
static int num_sessions = 0;

static int manager_debug;	/*!< enable some debugging code in the manager */

AST_THREADSTORAGE(manager_event_buf);
#define MANAGER_EVENT_BUF_INITSIZE   256

AST_THREADSTORAGE(astman_append_buf);
#define ASTMAN_APPEND_BUF_INITSIZE   256

/*!
 * Descriptor for a manager session, either on the AMI socket or over HTTP.
 * AMI session have managerid == 0; the entry is created upon a connect,
 * and destroyed with the socket.
 * HTTP sessions have managerid != 0, the value is used as a search key
 * to lookup sessions (using the mansession_id cookie).
 */
struct mansession {
	pthread_t ms_t;		/*!< Execution thread, basically useless */
	ast_mutex_t __lock;	/*!< Thread lock -- don't use in action callbacks, it's already taken care of  */
				/* XXX need to document which fields it is protecting */
	struct sockaddr_in sin;	/*!< address we are connecting from */
	int fd;			/*!< descriptor used for output. Either the socket (AMI) or a temporary file (HTTP) */
	int inuse;		/*!< number of HTTP sessions using this entry */
	int needdestroy;	/*!< Whether an HTTP session should be destroyed */
	pthread_t waiting_thread;	/*!< Whether an HTTP session has someone waiting on events */
	unsigned long managerid;	/*!< Unique manager identifier, 0 for AMI sessions */
	time_t sessiontimeout;	/*!< Session timeout if HTTP */
	struct ast_dynamic_str *outputstr;	/*!< Output from manager interface */
	char username[80];	/*!< Logged in username */
	char challenge[10];	/*!< Authentication challenge */
	int authenticated;	/*!< Authentication status */
	int readperm;		/*!< Authorization for reading */
	int writeperm;		/*!< Authorization for writing */
	char inbuf[AST_MAX_MANHEADER_LEN+1];	/*!< Buffer */
		/* we use the extra byte to add a '\0' and simplify parsing */
	int inlen;		/*!< number of buffered bytes */
	int send_events;	/*!<  XXX what ? */
	struct eventqent *last_ev;	/*!< last event processed. */
	int writetimeout;	/*!< Timeout for ast_carefulwrite() */
	AST_LIST_ENTRY(mansession) list;
};

#define NEW_EVENT(m)	(AST_LIST_NEXT(m->last_ev, eq_next))

static AST_LIST_HEAD_STATIC(sessions, mansession);

/*! \brief user descriptor, as read from the config file.
 * \note It is still missing some fields -- e.g. we can have multiple permit and deny
 * lines which are not supported here, and readperm/writeperm/writetimeout
 * are not stored.
 */
struct ast_manager_user {
	char username[80];
	char *secret;
	char *deny;
	char *permit;
	char *read;
	char *write;
	int displayconnects;	/*!< XXX unused */
	int keep;	/*!< mark entries created on a reload */
	AST_LIST_ENTRY(ast_manager_user) list;
};

/*! \brief list of users found in the config file */
static AST_LIST_HEAD_STATIC(users, ast_manager_user);

/*! \brief list of actions registered */
static struct manager_action *first_action = NULL;
AST_MUTEX_DEFINE_STATIC(actionlock);

static AST_RWLIST_HEAD_STATIC(manager_hooks, manager_custom_hook);

/*! \brief Add a custom hook to be called when an event is fired */
void ast_manager_register_hook(struct manager_custom_hook *hook)
{
	AST_RWLIST_WRLOCK(&manager_hooks);
	AST_RWLIST_INSERT_TAIL(&manager_hooks, hook, list);
	AST_RWLIST_UNLOCK(&manager_hooks);
	return;
}

/*! \brief Delete a custom hook to be called when an event is fired */
void ast_manager_unregister_hook(struct manager_custom_hook *hook)
{
	AST_RWLIST_WRLOCK(&manager_hooks);
	AST_RWLIST_REMOVE(&manager_hooks, hook, list);
	AST_RWLIST_UNLOCK(&manager_hooks);
	return;
}

/*! \brief
 * Event list management functions.
 * We assume that the event list always has at least one element,
 * and the delete code will not remove the last entry even if the
 * 
 */
#if 0
static time_t __deb(time_t start, const char *msg)
{
	time_t now = time(NULL);
	ast_verbose("%4d th %p %s\n", (int)(now % 3600), pthread_self(), msg);
	if (start != 0 && now - start > 5)
		ast_verbose("+++ WOW, %s took %d seconds\n", msg, (int)(now - start));
	return now;
}

static void LOCK_EVENTS(void)
{
	time_t start = __deb(0, "about to lock events");
	AST_LIST_LOCK(&all_events);
	__deb(start, "done lock events");
}

static void UNLOCK_EVENTS(void)
{
	__deb(0, "about to unlock events");
	AST_LIST_UNLOCK(&all_events);
}

static void LOCK_SESS(void)
{
	time_t start = __deb(0, "about to lock sessions");
	AST_LIST_LOCK(&sessions);
	__deb(start, "done lock sessions");
}

static void UNLOCK_SESS(void)
{
	__deb(0, "about to unlock sessions");
	AST_LIST_UNLOCK(&sessions);
}
#endif

/*!
 * Grab a reference to the last event, update usecount as needed.
 * Can handle a NULL pointer.
 */
static struct eventqent *grab_last(void)
{
	struct eventqent *ret;

	AST_LIST_LOCK(&all_events);
	ret = AST_LIST_LAST(&all_events);
	/* the list is never empty now, but may become so when
	 * we optimize it in the future, so be prepared.
	 */
	if (ret)
		ast_atomic_fetchadd_int(&ret->usecount, 1);
	AST_LIST_UNLOCK(&all_events);
	return ret;
}

/*!
 * Purge unused events. Remove elements from the head
 * as long as their usecount is 0 and there is a next element.
 */
static void purge_unused(void)
{
	struct eventqent *ev;

	AST_LIST_LOCK(&all_events);
	while ( (ev = AST_LIST_FIRST(&all_events)) &&
	    ev->usecount == 0 && AST_LIST_NEXT(ev, eq_next)) {
		AST_LIST_REMOVE_HEAD(&all_events, eq_next);
		free(ev);
	}
	AST_LIST_UNLOCK(&all_events);
}

/*!
 * helper functions to convert back and forth between
 * string and numeric representation of set of flags
 */
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

/*! \brief Convert authority code to a list of options */
static char *authority_to_str(int authority, char *res, int reslen)
{
	int i;
	char *dst = res, *sep = "";
	size_t len = reslen;

	res[0] = '\0';
	for (i = 0; i < (sizeof(perms) / sizeof(perms[0])) - 1; i++) {
		if (authority & perms[i].num) {
			ast_build_string(&dst, &len, "%s%s", sep, perms[i].label);
			sep = ",";
		}
	}

	if (ast_strlen_zero(res))	/* replace empty string with something sensible */
		ast_copy_string(res, "<none>", reslen);

	return res;
}

/*! Tells you if smallstr exists inside bigstr
   which is delim by delim and uses no buf or stringsep
   ast_instring("this|that|more","this",'|') == 1;

   feel free to move this to app.c -anthm */
static int ast_instring(const char *bigstr, const char *smallstr, const char delim)
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

/*!
 * A number returns itself, false returns 0, true returns all flags,
 * other strings return the flags that are set.
 */
static int ast_strings_to_mask(const char *string)
{
	const char *p;

	if (ast_strlen_zero(string))
		return -1;

	for (p = string; *p; p++)
		if (*p < '0' || *p > '9')
			break;
	if (!p)	/* all digits */
		return atoi(string);
	if (ast_false(string))
		return 0;
	if (ast_true(string)) {	/* all permissions */
		int x, ret = 0;
		for (x=0; x<sizeof(perms) / sizeof(perms[0]); x++)
			ret |= perms[x].num;
		return ret;
	}
	return get_perm(string);
}

static char *complete_show_mancmd(const char *line, const char *word, int pos, int state)
{
	struct manager_action *cur;
	int l = strlen(word), which = 0;
	char *ret = NULL;

	ast_mutex_lock(&actionlock);
	for (cur = first_action; cur; cur = cur->next) { /* Walk the list of actions */
		if (!strncasecmp(word, cur->action, l) && ++which > state) {
			ret = ast_strdup(cur->action);
			break;	/* make sure we exit even if ast_strdup() returns NULL */
		}
	}
	ast_mutex_unlock(&actionlock);

	return ret;
}

/*!
 * lookup an entry in the list of registered users.
 * must be called with the list lock held.
 */
static struct ast_manager_user *get_manager_by_name_locked(const char *name)
{
	struct ast_manager_user *user = NULL;

	AST_LIST_TRAVERSE(&users, user, list)
		if (!strcasecmp(user->username, name))
			break;
	return user;
}


static int handle_showmancmd(int fd, int argc, char *argv[])
{
	struct manager_action *cur;
	char authority[80];
	int num;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(&actionlock);
	for (cur = first_action; cur; cur = cur->next) { /* Walk the list of actions */
		for (num = 3; num < argc; num++) {
			if (!strcasecmp(cur->action, argv[num])) {
				ast_cli(fd, "Action: %s\nSynopsis: %s\nPrivilege: %s\n%s\n",
					cur->action, cur->synopsis,
					authority_to_str(cur->authority, authority, sizeof(authority) -1),
					S_OR(cur->description, "") );
			}
		}
	}
	ast_mutex_unlock(&actionlock);

	return RESULT_SUCCESS;
}

static int handle_mandebug(int fd, int argc, char *argv[])
{
	if (argc == 2)
		ast_cli(fd, "manager debug is %s\n", manager_debug? "on" : "off");
	else if (argc == 3) {
		if (!strcasecmp(argv[2], "on"))
			manager_debug = 1;
		else if (!strcasecmp(argv[2], "off"))
			manager_debug = 0;
		else
			return RESULT_SHOWUSAGE;
	}
	return RESULT_SUCCESS;
}

static int handle_showmanager(int fd, int argc, char *argv[])
{
	struct ast_manager_user *user = NULL;

	if (argc != 4)
		return RESULT_SHOWUSAGE;

	AST_LIST_LOCK(&users);

	if (!(user = get_manager_by_name_locked(argv[3]))) {
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
		(user->secret ? user->secret : "(N/A)"),
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


/*! \brief  CLI command  manager list commands */
static int handle_showmancmds(int fd, int argc, char *argv[])
{
	struct manager_action *cur;
	char authority[80];
	char *format = "  %-15.15s  %-15.15s  %-55.55s\n";

	ast_cli(fd, format, "Action", "Privilege", "Synopsis");
	ast_cli(fd, format, "------", "---------", "--------");

	ast_mutex_lock(&actionlock);
	for (cur = first_action; cur; cur = cur->next) /* Walk the list of actions */
		ast_cli(fd, format, cur->action, authority_to_str(cur->authority, authority, sizeof(authority) -1), cur->synopsis);
	ast_mutex_unlock(&actionlock);

	return RESULT_SUCCESS;
}

/*! \brief CLI command manager list connected */
static int handle_showmanconn(int fd, int argc, char *argv[])
{
	struct mansession *s;
	char *format = "  %-15.15s  %-15.15s\n";

	ast_cli(fd, format, "Username", "IP Address");

	AST_LIST_LOCK(&sessions);
	AST_LIST_TRAVERSE(&sessions, s, list)
		ast_cli(fd, format,s->username, ast_inet_ntoa(s->sin.sin_addr));
	AST_LIST_UNLOCK(&sessions);

	return RESULT_SUCCESS;
}

/*! \brief CLI command manager list eventq */
/* Should change to "manager show connected" */
static int handle_showmaneventq(int fd, int argc, char *argv[])
{
	struct eventqent *s;

	AST_LIST_LOCK(&all_events);
	AST_LIST_TRAVERSE(&all_events, s, eq_next) {
		ast_cli(fd, "Usecount: %d\n",s->usecount);
		ast_cli(fd, "Category: %d\n", s->category);
		ast_cli(fd, "Event:\n%s", s->eventdata);
	}
	AST_LIST_UNLOCK(&all_events);

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

static struct ast_cli_entry cli_manager[] = {
	{ { "manager", "show", "command", NULL },
	handle_showmancmd, "Show a manager interface command",
	showmancmd_help, complete_show_mancmd },

	{ { "manager", "show", "commands", NULL },
	handle_showmancmds, "List manager interface commands",
	showmancmds_help },

	{ { "manager", "show", "connected", NULL },
	handle_showmanconn, "List connected manager interface users",
	showmanconn_help },

	{ { "manager", "show", "eventq", NULL },
	handle_showmaneventq, "List manager interface queued events",
	showmaneventq_help },

	{ { "manager", "show", "users", NULL },
	handle_showmanagers, "List configured manager users",
	showmanagers_help, NULL, NULL },

	{ { "manager", "show", "user", NULL },
	handle_showmanager, "Display information on a specific manager user",
	showmanager_help, NULL, NULL },

	{ { "manager", "debug", NULL },
	handle_mandebug, "Show, enable, disable debugging of the manager code",
	"Usage: manager debug [on|off]\n	Show, enable, disable debugging of the manager code.\n", NULL, NULL },
};

/*
 * Decrement the usecount for the event; if it goes to zero,
 * (why check for e->next ?) wakeup the
 * main thread, which is in charge of freeing the record.
 * Returns the next record.
 */
static struct eventqent *unref_event(struct eventqent *e)
{
	struct eventqent *ret = AST_LIST_NEXT(e, eq_next);
	if (ast_atomic_dec_and_test(&e->usecount) && ret)
		pthread_kill(accept_thread_ptr, SIGURG);
	return ret;
}

static void ref_event(struct eventqent *e)
{
	ast_atomic_fetchadd_int(&e->usecount, 1);
}

/*
 * destroy a session, leaving the usecount
 */
static void free_session(struct mansession *s)
{
	struct eventqent *eqe = s->last_ev;
	if (s->fd > -1)
		close(s->fd);
	if (s->outputstr)
		free(s->outputstr);
	ast_mutex_destroy(&s->__lock);
	free(s);
	unref_event(eqe);
}

static void destroy_session(struct mansession *s)
{
	AST_LIST_LOCK(&sessions);
	AST_LIST_REMOVE(&sessions, s, list);
	AST_LIST_UNLOCK(&sessions);

	ast_atomic_fetchadd_int(&num_sessions, -1);
	free_session(s);
}

char *astman_get_header(struct message *m, char *var)
{
	int x, l = strlen(var);

	for (x = 0; x < m->hdrcount; x++) {
		char *h = m->headers[x];
		if (!strncasecmp(var, h, l) && h[l] == ':' && h[l+1] == ' ')
			return h + l + 2;
	}

	return "";
}

struct ast_variable *astman_get_variables(struct message *m)
{
	int varlen, x, y;
	struct ast_variable *head = NULL, *cur;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(vars)[32];
	);

	varlen = strlen("Variable: ");

	for (x = 0; x < m->hdrcount; x++) {
		char *parse, *var, *val;

		if (strncasecmp("Variable: ", m->headers[x], varlen))
			continue;
		parse = ast_strdupa(m->headers[x] + varlen);

		AST_STANDARD_APP_ARGS(args, parse);
		if (!args.argc)
			continue;
		for (y = 0; y < args.argc; y++) {
			if (!args.vars[y])
				continue;
			var = val = ast_strdupa(args.vars[y]);
			strsep(&val, "=");
			if (!val || ast_strlen_zero(var))
				continue;
			cur = ast_variable_new(var, val);
			cur->next = head;
			head = cur;
		}
	}

	return head;
}

/*
 * utility functions for creating AMI replies
 */
void astman_append(struct mansession *s, const char *fmt, ...)
{
	va_list ap;
	struct ast_dynamic_str *buf;

	if (!(buf = ast_dynamic_str_thread_get(&astman_append_buf, ASTMAN_APPEND_BUF_INITSIZE)))
		return;

	va_start(ap, fmt);
	ast_dynamic_str_thread_set_va(&buf, 0, &astman_append_buf, fmt, ap);
	va_end(ap);

	if (s->fd > -1)
		ast_carefulwrite(s->fd, buf->str, strlen(buf->str), s->writetimeout);
	else {
		if (!s->outputstr && !(s->outputstr = ast_calloc(1, sizeof(*s->outputstr))))
			return;

		ast_dynamic_str_append(&s->outputstr, 0, "%s", buf->str);
	}
}

/*! \note NOTE: XXX this comment is unclear and possibly wrong.
   Callers of astman_send_error(), astman_send_response() or astman_send_ack() must EITHER
   hold the session lock _or_ be running in an action callback (in which case s->busy will
   be non-zero). In either of these cases, there is no need to lock-protect the session's
   fd, since no other output will be sent (events will be queued), and no input will
   be read until either the current action finishes or get_input() obtains the session
   lock.
 */

/*! \brief send a response with an optional message,
 * and terminate it with an empty line.
 * m is used only to grab the 'ActionID' field.
 *
 * Use the explicit constant MSG_MOREDATA to remove the empty line.
 * XXX MSG_MOREDATA should go to a header file.
 */
#define MSG_MOREDATA	((char *)astman_send_response)
static void astman_send_response_full(struct mansession *s, struct message *m, char *resp, char *msg, char *listflag)
{
	char *id = astman_get_header(m,"ActionID");

	astman_append(s, "Response: %s\r\n", resp);
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n", id);
	if (listflag)
		astman_append(s, "Eventlist: %s\r\n", listflag);	/* Start, complete, cancelled */
	if (msg == MSG_MOREDATA)
		return;
	else if (msg)
		astman_append(s, "Message: %s\r\n\r\n", msg);
	else
		astman_append(s, "\r\n");
}

void astman_send_response(struct mansession *s, struct message *m, char *resp, char *msg)
{
	astman_send_response_full(s, m, resp, msg, NULL);
}

void astman_send_error(struct mansession *s, struct message *m, char *error)
{
	astman_send_response_full(s, m, "Error", error, NULL);
}

void astman_send_ack(struct mansession *s, struct message *m, char *msg)
{
	astman_send_response_full(s, m, "Success", msg, NULL);
}

static void astman_start_ack(struct mansession *s, struct message *m)
{
	astman_send_response_full(s, m, "Success", MSG_MOREDATA, NULL);
}

void astman_send_listack(struct mansession *s, struct message *m, char *msg, char *listflag)
{
	astman_send_response_full(s, m, "Success", msg, listflag);
}



/*! \brief
   Rather than braindead on,off this now can also accept a specific int mask value
   or a ',' delim list of mask strings (the same as manager.conf) -anthm
*/
static int set_eventmask(struct mansession *s, char *eventmask)
{
	int maskint = ast_strings_to_mask(eventmask);

	ast_mutex_lock(&s->__lock);
	if (maskint >= 0)
		s->send_events = maskint;
	ast_mutex_unlock(&s->__lock);

	return maskint;
}

/*
 * Here we start with action_ handlers for AMI actions,
 * and the internal functions used by them.
 * Generally, the handlers are called action_foo()
 */

/* helper function for action_login() */
static int authenticate(struct mansession *s, struct message *m)
{
	char *user = astman_get_header(m, "Username");
	int error = -1;
	struct ast_ha *ha = NULL;
	char *password = NULL;
	int readperm = 0, writeperm = 0;

	if (ast_strlen_zero(user))	/* missing username */
		return -1;

    {
	/*
	 * XXX there should be no need to scan the config file again here,
	 * suffices to call get_manager_by_name_locked() to fetch
	 * the user's entry.
	 */
	struct ast_config *cfg = ast_config_load("manager.conf");
	char *cat = NULL;
	struct ast_variable *v;

	if (!cfg)
		return -1;
	while ( (cat = ast_category_browse(cfg, cat)) ) {
		/* "general" is not a valid user */
		if (!strcasecmp(cat, user) && strcasecmp(cat, "general"))
			break;
	}
	if (!cat) {
		ast_log(LOG_NOTICE, "%s tried to authenticate with nonexistent user '%s'\n", ast_inet_ntoa(s->sin.sin_addr), user);
		ast_config_destroy(cfg);
		return -1;
	}

	/* collect parameters for the user's entry */
	for (v = ast_variable_browse(cfg, cat); v; v = v->next) {
		if (!strcasecmp(v->name, "secret"))
			password = ast_strdupa(v->value);
		else if (!strcasecmp(v->name, "read"))
			readperm = get_perm(v->value);
		else if (!strcasecmp(v->name, "write"))
			writeperm = get_perm(v->value);
		else if (!strcasecmp(v->name, "permit") ||
			   !strcasecmp(v->name, "deny")) {
			ha = ast_append_ha(v->name, v->value, ha);
		} else if (!strcasecmp(v->name, "writetimeout")) {
			int val = atoi(v->value);

			if (val < 100)
				ast_log(LOG_WARNING, "Invalid writetimeout value '%s' at line %d\n", v->value, v->lineno);
			else
				s->writetimeout = val;
		}
	}
	ast_config_destroy(cfg);
    }

	if (ha) {
		int good = ast_apply_ha(ha, &(s->sin));
		ast_free_ha(ha);
		if (!good) {
			ast_log(LOG_NOTICE, "%s failed to pass IP ACL as '%s'\n", ast_inet_ntoa(s->sin.sin_addr), user);
			return -1;
		}
	}
	if (!strcasecmp(astman_get_header(m, "AuthType"), "MD5")) {
		char *key = astman_get_header(m, "Key");
		if (!ast_strlen_zero(key) && !ast_strlen_zero(s->challenge)) {
			int x;
			int len = 0;
			char md5key[256] = "";
			struct MD5Context md5;
			unsigned char digest[16];

			MD5Init(&md5);
			MD5Update(&md5, (unsigned char *) s->challenge, strlen(s->challenge));
			MD5Update(&md5, (unsigned char *) password, strlen(password));
			MD5Final(digest, &md5);
			for (x=0; x<16; x++)
				len += sprintf(md5key + len, "%2.2x", digest[x]);
			if (!strcmp(md5key, key))
				error = 0;
		}
	} else if (password) {
		char *pass = astman_get_header(m, "Secret");
		if (!strcmp(password, pass))
			error = 0;
	}
	if (error) {
		ast_log(LOG_NOTICE, "%s failed to authenticate as '%s'\n", ast_inet_ntoa(s->sin.sin_addr), user);
		return -1;
	}
	ast_copy_string(s->username, user, sizeof(s->username));
	s->readperm = readperm;
	s->writeperm = writeperm;
	set_eventmask(s, astman_get_header(m, "Events"));
	return 0;
}

/*! \brief Manager PING */
static char mandescr_ping[] =
"Description: A 'Ping' action will ellicit a 'Pong' response.  Used to keep the\n"
"  manager connection open.\n"
"Variables: NONE\n";

static int action_ping(struct mansession *s, struct message *m)
{
	astman_send_response(s, m, "Pong", NULL);
	return 0;
}

static char mandescr_getconfig[] =
"Description: A 'GetConfig' action will dump the contents of a configuration\n"
"file by category and contents.\n"
"Variables:\n"
"   Filename: Configuration filename (e.g. foo.conf)\n";

static int action_getconfig(struct mansession *s, struct message *m)
{
	struct ast_config *cfg;
	char *fn = astman_get_header(m, "Filename");
	int catcount = 0;
	int lineno = 0;
	char *category=NULL;
	struct ast_variable *v;

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}
	if (!(cfg = ast_config_load_with_comments(fn))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	}
	astman_start_ack(s, m);
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

/* helper function for action_updateconfig */
static void handle_updates(struct mansession *s, struct message *m, struct ast_config *cfg)
{
	int x;
	char hdr[40];
	char *action, *cat, *var, *value, *match;
	struct ast_category *category;
	struct ast_variable *v;

	for (x=0;x<100000;x++) {
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
				ast_category_delete(cfg, cat);
		} else if (!strcasecmp(action, "update")) {
			if (!ast_strlen_zero(cat) && !ast_strlen_zero(var) && (category = ast_category_get(cfg, cat)))
				ast_variable_update(category, var, value, match);
		} else if (!strcasecmp(action, "delete")) {
			if (!ast_strlen_zero(cat) && !ast_strlen_zero(var) && (category = ast_category_get(cfg, cat)))
				ast_variable_delete(category, var, match);
		} else if (!strcasecmp(action, "append")) {
			if (!ast_strlen_zero(cat) && !ast_strlen_zero(var) &&
				(category = ast_category_get(cfg, cat)) &&
				(v = ast_variable_new(var, value))){
				if (match && !strcasecmp(match, "object"))
					v->object = 1;
				ast_variable_append(category, v);
			}
		}
	}
}

static char mandescr_updateconfig[] =
"Description: A 'UpdateConfig' action will dump the contents of a configuration\n"
"file by category and contents.\n"
"Variables (X's represent 6 digit number beginning with 000000):\n"
"   SrcFilename:   Configuration filename to read(e.g. foo.conf)\n"
"   DstFilename:   Configuration filename to write(e.g. foo.conf)\n"
"   Reload:        Whether or not a reload should take place (or name of specific module)\n"
"   Action-XXXXXX: Action to Take (NewCat,RenameCat,DelCat,Update,Delete,Append)\n"
"   Cat-XXXXXX:    Category to operate on\n"
"   Var-XXXXXX:    Variable to work on\n"
"   Value-XXXXXX:  Value to work on\n"
"   Match-XXXXXX:  Extra match required to match line\n";

static int action_updateconfig(struct mansession *s, struct message *m)
{
	struct ast_config *cfg;
	char *sfn = astman_get_header(m, "SrcFilename");
	char *dfn = astman_get_header(m, "DstFilename");
	int res;
	char *rld = astman_get_header(m, "Reload");

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
	astman_send_ack(s, m, NULL);
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

static int action_waitevent(struct mansession *s, struct message *m)
{
	char *timeouts = astman_get_header(m, "Timeout");
	int timeout = -1, max;
	int x;
	int needexit = 0;
	time_t now;
	char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	if (!ast_strlen_zero(timeouts)) {
		sscanf(timeouts, "%i", &timeout);
	}

	ast_mutex_lock(&s->__lock);
	if (s->waiting_thread != AST_PTHREADT_NULL) {
		pthread_kill(s->waiting_thread, SIGURG);
	}
	if (s->sessiontimeout) {
		time(&now);
		max = s->sessiontimeout - now - 10;
		if (max < 0)
			max = 0;
		if ((timeout < 0) || (timeout > max))
			timeout = max;
		if (!s->send_events)
			s->send_events = -1;
		/* Once waitevent is called, always queue events from now on */
	}
	ast_mutex_unlock(&s->__lock);
	s->waiting_thread = pthread_self();
	if (option_debug)
		ast_log(LOG_DEBUG, "Starting waiting for an event!\n");
	for (x=0; ((x < timeout) || (timeout < 0)); x++) {
		ast_mutex_lock(&s->__lock);
		if (NEW_EVENT(s))
			needexit = 1;
		if (s->waiting_thread != pthread_self())
			needexit = 1;
		if (s->needdestroy)
			needexit = 1;
		ast_mutex_unlock(&s->__lock);
		if (needexit)
			break;
		if (s->managerid == 0) {	/* AMI session */
			if (ast_wait_for_input(s->fd, 1000))
				break;
		} else {	/* HTTP session */
			sleep(1);
		}
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Finished waiting for an event!\n");
	ast_mutex_lock(&s->__lock);
	if (s->waiting_thread == pthread_self()) {
		struct eventqent *eqe;
		astman_send_response(s, m, "Success", "Waiting for Event...");
		/* Only show events if we're the most recent waiter */
		while ( (eqe = NEW_EVENT(s)) ) {
			ref_event(eqe);
			if (((s->readperm & eqe->category) == eqe->category) &&
			    ((s->send_events & eqe->category) == eqe->category)) {
				astman_append(s, "%s", eqe->eventdata);
			}
			s->last_ev = unref_event(s->last_ev);
		}
		astman_append(s,
			"Event: WaitEventComplete\r\n"
			"%s"
			"\r\n", idText);
		s->waiting_thread = AST_PTHREADT_NULL;
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "Abandoning event request!\n");
	}
	ast_mutex_unlock(&s->__lock);
	return 0;
}

static char mandescr_listcommands[] =
"Description: Returns the action name and synopsis for every\n"
"  action that is available to the user\n"
"Variables: NONE\n";

static int action_listcommands(struct mansession *s, struct message *m)
{
	struct manager_action *cur;
	char temp[BUFSIZ];

	astman_start_ack(s, m);
	ast_mutex_lock(&actionlock);
	for (cur = first_action; cur; cur = cur->next) { /* Walk the list of actions */
		if ((s->writeperm & cur->authority) == cur->authority)
			astman_append(s, "%s: %s (Priv: %s)\r\n", cur->action, cur->synopsis, authority_to_str(cur->authority, temp, sizeof(temp)));
	}
	ast_mutex_unlock(&actionlock);
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

static int action_events(struct mansession *s, struct message *m)
{
	char *mask = astman_get_header(m, "EventMask");
	int res;

	res = set_eventmask(s, mask);
	if (res > 0)
		astman_send_response(s, m, "Events On", NULL);
	else if (res == 0)
		astman_send_response(s, m, "Events Off", NULL);

	return 0;
}

static char mandescr_logoff[] =
"Description: Logoff this manager session\n"
"Variables: NONE\n";

static int action_logoff(struct mansession *s, struct message *m)
{
	astman_send_response(s, m, "Goodbye", "Thanks for all the fish.");
	return -1;
}

static int action_login(struct mansession *s, struct message *m)
{
	if (authenticate(s, m)) {
		sleep(1);
		astman_send_error(s, m, "Authentication failed");
		return -1;
	}
	s->authenticated = 1;
	if (option_verbose > 1) {
		if (displayconnects) {
			ast_verbose(VERBOSE_PREFIX_2 "%sManager '%s' logged on from %s\n", (s->managerid ? "HTTP " : ""), s->username, ast_inet_ntoa(s->sin.sin_addr));
		}
	}
	ast_log(LOG_EVENT, "%sManager '%s' logged on from %s\n", (s->managerid ? "HTTP " : ""), s->username, ast_inet_ntoa(s->sin.sin_addr));
	astman_send_ack(s, m, "Authentication accepted");
	return 0;
}

static int action_challenge(struct mansession *s, struct message *m)
{
	char *authtype = astman_get_header(m, "AuthType");

	if (!strcasecmp(authtype, "MD5")) {
		if (ast_strlen_zero(s->challenge))
			snprintf(s->challenge, sizeof(s->challenge), "%ld", ast_random());
		ast_mutex_lock(&s->__lock);
		astman_start_ack(s, m);
		astman_append(s, "Challenge: %s\r\n\r\n", s->challenge);
		ast_mutex_unlock(&s->__lock);
	} else {
		astman_send_error(s, m, "Must specify AuthType");
	}
	return 0;
}

static char mandescr_hangup[] =
"Description: Hangup a channel\n"
"Variables: \n"
"	Channel: The channel name to be hungup\n";

static int action_hangup(struct mansession *s, struct message *m)
{
	struct ast_channel *c = NULL;
	char *name = astman_get_header(m, "Channel");
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

static int action_setvar(struct mansession *s, struct message *m)
{
        struct ast_channel *c = NULL;
        char *name = astman_get_header(m, "Channel");
        char *varname = astman_get_header(m, "Variable");
        char *varval = astman_get_header(m, "Value");

	if (ast_strlen_zero(varname)) {
		astman_send_error(s, m, "No variable specified");
		return 0;
	}

	if (ast_strlen_zero(varval)) {
		astman_send_error(s, m, "No value specified");
		return 0;
	}

	if (!ast_strlen_zero(name)) {
		c = ast_get_channel_by_name_locked(name);
		if (!c) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}

	pbx_builtin_setvar_helper(c, varname, varval);

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

static int action_getvar(struct mansession *s, struct message *m)
{
	struct ast_channel *c = NULL;
	char *name = astman_get_header(m, "Channel");
	char *varname = astman_get_header(m, "Variable");
	char *varval;
	char workspace[1024];

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
		ast_func_read(c, varname, workspace, sizeof(workspace));
	} else {
		pbx_retrieve_variable(c, varname, &varval, workspace, sizeof(workspace), NULL);
	}

	if (c)
		ast_channel_unlock(c);
	astman_start_ack(s, m);
	astman_append(s, "Variable: %s\r\nValue: %s\r\n\r\n", varname, varval);

	return 0;
}


/*! \brief Manager "status" command to show channels */
/* Needs documentation... */
static int action_status(struct mansession *s, struct message *m)
{
    	char *name = astman_get_header(m,"Channel");
	struct ast_channel *c;
	char bridge[256];
	struct timeval now = ast_tvnow();
	long elapsed_seconds = 0;
	int all = ast_strlen_zero(name); /* set if we want all channels */
	char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	astman_send_ack(s, m, "Channel status will follow");
	if (all)
		c = ast_channel_walk_locked(NULL);
	else {
		c = ast_get_channel_by_name_locked(name);
		if (!c) {
			astman_send_error(s, m, "No such channel");
			return 0;
		}
	}
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
			S_OR(c->cid.cid_name, "<unknown>"),
			c->accountcode,
			ast_state2str(c->_state), c->context,
			c->exten, c->priority, (long)elapsed_seconds, bridge, c->uniqueid, idText);
		} else {
			astman_append(s,
			"Event: Status\r\n"
			"Privilege: Call\r\n"
			"Channel: %s\r\n"
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

static char mandescr_sendtext[] =
"Description: Sends A Text Message while in a call.\n"
"Variables: (Names marked with * are required)\n"
"       *Channel: Channel to send message to\n"
"       *Message: Message to send\n"
"       ActionID: Optional Action id for message matching.\n";

static int action_sendtext(struct mansession *s, struct message *m)
{
	struct ast_channel *c = NULL;
	char *name = astman_get_header(m, "Channel");
	char *textmsg = astman_get_header(m, "Message");
	int res = 0;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return 0;
	}

	if (ast_strlen_zero(textmsg)) {
		astman_send_error(s, m, "No Message specified");
		return 0;
	}

	c = ast_get_channel_by_name_locked(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return 0;
	}

	res = ast_sendtext(c, textmsg);
	ast_mutex_unlock(&c->lock);
	
	if (res > 0)
		astman_send_ack(s, m, "Success");
	else
		astman_send_error(s, m, "Failure");
	
	return res;
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
static int action_redirect(struct mansession *s, struct message *m)
{
	char *name = astman_get_header(m, "Channel");
	char *name2 = astman_get_header(m, "ExtraChannel");
	char *exten = astman_get_header(m, "Exten");
	char *context = astman_get_header(m, "Context");
	char *priority = astman_get_header(m, "Priority");
	struct ast_channel *chan, *chan2 = NULL;
	int pi = 0;
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}
	if (!ast_strlen_zero(priority) && (sscanf(priority, "%d", &pi) != 1)) {
		if ((pi = ast_findlabel_extension(NULL, context, exten, priority, NULL)) < 1) {
			astman_send_error(s, m, "Invalid priority\n");
			return 0;
		}
	}
	/* XXX watch out, possible deadlock - we are trying to get two channels!!! */
	chan = ast_get_channel_by_name_locked(name);
	if (!chan) {
		char buf[BUFSIZ];
		snprintf(buf, sizeof(buf), "Channel does not exist: %s", name);
		astman_send_error(s, m, buf);
		return 0;
	}
	if (!ast_strlen_zero(name2))
		chan2 = ast_get_channel_by_name_locked(name2);
	res = ast_async_goto(chan, context, exten, pi);
	if (!res) {
		if (!ast_strlen_zero(name2)) {
			if (chan2)
				res = ast_async_goto(chan2, context, exten, pi);
			else
				res = -1;
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

static char mandescr_command[] =
"Description: Run a CLI command.\n"
"Variables: (Names marked with * are required)\n"
"	*Command: Asterisk CLI command to run\n"
"	ActionID: Optional Action id for message matching.\n";

/*! \brief  Manager command "command" - execute CLI command */
static int action_command(struct mansession *s, struct message *m)
{
	char *cmd = astman_get_header(m, "Command");
	char *id = astman_get_header(m, "ActionID");
	astman_append(s, "Response: Follows\r\nPrivilege: Command\r\n");
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n", id);
	/* FIXME: Wedge a ActionID response in here, waiting for later changes */
	ast_cli_command(s->fd, cmd);
	astman_append(s, "--END COMMAND--\r\n\r\n");
	return 0;
}

/* helper function for originate */
struct fast_originate_helper {
	char tech[AST_MAX_MANHEADER_LEN];
	char data[AST_MAX_MANHEADER_LEN];
	int timeout;
	char app[AST_MAX_APP];
	char appdata[AST_MAX_MANHEADER_LEN];
	char cid_name[AST_MAX_MANHEADER_LEN];
	char cid_num[AST_MAX_MANHEADER_LEN];
	char context[AST_MAX_CONTEXT];
	char exten[AST_MAX_EXTENSION];
	char idtext[AST_MAX_MANHEADER_LEN];
	char account[AST_MAX_ACCOUNT_CODE];
	int priority;
	struct ast_variable *vars;
};

static void *fast_originate(void *data)
{
	struct fast_originate_helper *in = data;
	int res;
	int reason = 0;
	struct ast_channel *chan = NULL;

	if (!ast_strlen_zero(in->app)) {
		res = ast_pbx_outgoing_app(in->tech, AST_FORMAT_SLINEAR, in->data, in->timeout, in->app, in->appdata, &reason, 1,
			S_OR(in->cid_num, NULL),
			S_OR(in->cid_name, NULL),
			in->vars, in->account, &chan);
	} else {
		res = ast_pbx_outgoing_exten(in->tech, AST_FORMAT_SLINEAR, in->data, in->timeout, in->context, in->exten, in->priority, &reason, 1,
			S_OR(in->cid_num, NULL),
			S_OR(in->cid_name, NULL),
			in->vars, in->account, &chan);
	}

	/* Tell the manager what happened with the channel */
	manager_event(EVENT_FLAG_CALL,
		res ? "OriginateFailure" : "OriginateSuccess",
		"%s"
		"Channel: %s/%s\r\n"
		"Context: %s\r\n"
		"Exten: %s\r\n"
		"Reason: %d\r\n"
		"Uniqueid: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n",
		in->idtext, in->tech, in->data, in->context, in->exten, reason,
		chan ? chan->uniqueid : "<null>",
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
"	Timeout: How long to wait for call to be answered (in ms)\n"
"	CallerID: Caller ID to be set on the outgoing channel\n"
"	Variable: Channel variable to set, multiple Variable: headers are allowed\n"
"	Account: Account code\n"
"	Async: Set to 'true' for fast origination\n";

static int action_originate(struct mansession *s, struct message *m)
{
	char *name = astman_get_header(m, "Channel");
	char *exten = astman_get_header(m, "Exten");
	char *context = astman_get_header(m, "Context");
	char *priority = astman_get_header(m, "Priority");
	char *timeout = astman_get_header(m, "Timeout");
	char *callerid = astman_get_header(m, "CallerID");
	char *account = astman_get_header(m, "Account");
	char *app = astman_get_header(m, "Application");
	char *appdata = astman_get_header(m, "Data");
	char *async = astman_get_header(m, "Async");
	char *id = astman_get_header(m, "ActionID");
	struct ast_variable *vars = astman_get_variables(m);
	char *tech, *data;
	char *l = NULL, *n = NULL;
	int pi = 0;
	int res;
	int to = 30000;
	int reason = 0;
	char tmp[256];
	char tmp2[256];

	pthread_t th;
	pthread_attr_t attr;
	if (!name) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}
	if (!ast_strlen_zero(priority) && (sscanf(priority, "%d", &pi) != 1)) {
		if ((pi = ast_findlabel_extension(NULL, context, exten, priority, NULL)) < 1) {
			astman_send_error(s, m, "Invalid priority\n");
			return 0;
		}
	}
	if (!ast_strlen_zero(timeout) && (sscanf(timeout, "%d", &to) != 1)) {
		astman_send_error(s, m, "Invalid timeout\n");
		return 0;
	}
	ast_copy_string(tmp, name, sizeof(tmp));
	tech = tmp;
	data = strchr(tmp, '/');
	if (!data) {
		astman_send_error(s, m, "Invalid channel\n");
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
	if (ast_true(async)) {
		struct fast_originate_helper *fast = ast_calloc(1, sizeof(*fast));
		if (!fast) {
			res = -1;
		} else {
			if (!ast_strlen_zero(id))
				snprintf(fast->idtext, sizeof(fast->idtext), "ActionID: %s\r\n", id);
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
			fast->timeout = to;
			fast->priority = pi;
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			if (ast_pthread_create(&th, &attr, fast_originate, fast)) {
				res = -1;
			} else {
				res = 0;
			}
		}
	} else if (!ast_strlen_zero(app)) {
        	res = ast_pbx_outgoing_app(tech, AST_FORMAT_SLINEAR, data, to, app, appdata, &reason, 1, l, n, vars, account, NULL);
    	} else {
		if (exten && context && pi)
	        	res = ast_pbx_outgoing_exten(tech, AST_FORMAT_SLINEAR, data, to, context, exten, pi, &reason, 1, l, n, vars, account, NULL);
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

static int action_mailboxstatus(struct mansession *s, struct message *m)
{
	char *mailbox = astman_get_header(m, "Mailbox");
	int ret;

	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	ret = ast_app_has_voicemail(mailbox, NULL);
	astman_start_ack(s, m);
	astman_append(s, "Message: Mailbox Status\r\n"
			 "Mailbox: %s\r\n"
			 "Waiting: %d\r\n\r\n", mailbox, ret);
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
static int action_mailboxcount(struct mansession *s, struct message *m)
{
	char *mailbox = astman_get_header(m, "Mailbox");
	int newmsgs = 0, oldmsgs = 0;

	if (ast_strlen_zero(mailbox)) {
		astman_send_error(s, m, "Mailbox not specified");
		return 0;
	}
	ast_app_inboxcount(mailbox, &newmsgs, &oldmsgs);
	astman_start_ack(s, m);
	astman_append(s,   "Message: Mailbox Message Count\r\n"
			   "Mailbox: %s\r\n"
			   "NewMessages: %d\r\n"
			   "OldMessages: %d\r\n"
			   "\r\n",
			   mailbox, newmsgs, oldmsgs);
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

static int action_extensionstate(struct mansession *s, struct message *m)
{
	char *exten = astman_get_header(m, "Exten");
	char *context = astman_get_header(m, "Context");
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
	astman_start_ack(s, m);
	astman_append(s,   "Message: Extension Status\r\n"
			   "Exten: %s\r\n"
			   "Context: %s\r\n"
			   "Hint: %s\r\n"
			   "Status: %d\r\n\r\n",
			   exten, context, hint, status);
	return 0;
}

static char mandescr_timeout[] =
"Description: Hangup a channel after a certain time.\n"
"Variables: (Names marked with * are required)\n"
"	*Channel: Channel name to hangup\n"
"	*Timeout: Maximum duration of the call (sec)\n"
"Acknowledges set time with 'Timeout Set' message\n";

static int action_timeout(struct mansession *s, struct message *m)
{
	struct ast_channel *c;
	char *name = astman_get_header(m, "Channel");
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

/*!
 * Send any applicable events to the client listening on this socket
 */
static int process_events(struct mansession *s)
{
	int ret = 0;

	ast_mutex_lock(&s->__lock);
	if (s->fd > -1) {
		struct eventqent *eqe;

		while ( (eqe = NEW_EVENT(s)) ) {
			ref_event(eqe);
			if ((s->authenticated && (s->readperm & eqe->category) == eqe->category) &&
			    ((s->send_events & eqe->category) == eqe->category)) {
				if (!ret && ast_carefulwrite(s->fd, eqe->eventdata,
						strlen(eqe->eventdata), s->writetimeout) < 0)
					ret = -1;
			}
			s->last_ev = unref_event(s->last_ev);
		}
	}
	ast_mutex_unlock(&s->__lock);
	return ret;
}

static char mandescr_userevent[] =
"Description: Send an event to manager sessions.\n"
"Variables: (Names marked with * are required)\n"
"       *UserEvent: EventStringToSend\n"
"       Header1: Content1\n"
"       HeaderN: ContentN\n";

static int action_userevent(struct mansession *s, struct message *m)
{
	char *event = astman_get_header(m, "UserEvent");
	char body[2048] = "";
	int x, bodylen = 0;
	for (x = 0; x < m->hdrcount; x++) {
		if (strncasecmp("UserEvent:", m->headers[x], strlen("UserEvent:"))) {
			ast_copy_string(body + bodylen, m->headers[x], sizeof(body) - bodylen - 3);
			bodylen += strlen(m->headers[x]);
			ast_copy_string(body + bodylen, "\r\n", 3);
			bodylen += 2;
		}
	}

	manager_event(EVENT_FLAG_USER, "UserEvent", "UserEvent: %s\r\n%s", event, body);
	return 0;
}

/*
 * Done with the action handlers here, we start with the code in charge
 * of accepting connections and serving them.
 * accept_thread() forks a new thread for each connection, session_do(),
 * which in turn calls get_input() repeatedly until a full message has
 * been accumulated, and then invokes process_message() to pass it to
 * the appropriate handler.
 */

/*
 * Process an AMI message, performing desired action.
 * Return 0 on success, -1 on error that require the session to be destroyed.
 */
static int process_message(struct mansession *s, struct message *m)
{
	char action[80] = "";
	int ret = 0;
	struct manager_action *tmp;

	ast_copy_string(action, astman_get_header(m, "Action"), sizeof(action));
	if (option_debug)
		ast_log(LOG_DEBUG, "Manager received command '%s'\n", action);

	if (ast_strlen_zero(action)) {
		astman_send_error(s, m, "Missing action in request");
		return 0;
	}

	if (!s->authenticated && strcasecmp(action, "Login") && strcasecmp(action, "Logoff") && strcasecmp(action, "Challenge")) {
		astman_send_error(s, m, "Permission denied");
		return 0;
	}
	/* XXX should we protect the list navigation ? */
	for (tmp = first_action ; tmp; tmp = tmp->next) {
		if (!strcasecmp(action, tmp->action)) {
			if ((s->writeperm & tmp->authority) == tmp->authority) {
				if (tmp->func(s, m))	/* error */
					return -1;
			} else {
				astman_send_error(s, m, "Permission denied");
			}
			break;
		}
	}
	if (!tmp)
		astman_send_error(s, m, "Invalid/unknown command");
	if (ret)
		return ret;
	/* Once done with our message, deliver any pending events */
	return process_events(s);
}

/*!
 * Read one full line (including crlf) from the manager socket.
 * \r\n is the only valid terminator for the line.
 * (Note that, later, '\0' will be considered as the end-of-line marker,
 * so everything between the '\0' and the '\r\n' will not be used).
 * Also note that we assume output to have at least "maxlen" space.
 */
static int get_input(struct mansession *s, char *output)
{
	struct pollfd fds[1];
	int res, x;
	int maxlen = sizeof(s->inbuf) - 1;
	char *src = s->inbuf;

	/*
	 * Look for \r\n within the buffer. If found, copy to the output
	 * buffer and return, trimming the \r\n (not used afterwards).
	 */
	for (x = 1; x < s->inlen; x++) {
		if (src[x] != '\n' || src[x-1] != '\r')
			continue;
		x++;	/* Found. Update length to include \r\n */
		memmove(output, src, x-2);	/*... but trim \r\n */
		output[x-2] = '\0';		/* terminate the string */
		s->inlen -= x;			/* remaining size */
		memmove(src, src + x, s->inlen); /* remove used bytes */
		return 1;
	}
	if (s->inlen >= maxlen) {
		/* no crlf found, and buffer full - sorry, too long for us */
		ast_log(LOG_WARNING, "Dumping long line with no return from %s: %s\n", ast_inet_ntoa(s->sin.sin_addr), src);
		s->inlen = 0;
	}
	fds[0].fd = s->fd;
	fds[0].events = POLLIN;
	res = 0;
	while (res == 0) {
		/* XXX do we really need this locking ? */
		ast_mutex_lock(&s->__lock);
		s->waiting_thread = pthread_self();
		ast_mutex_unlock(&s->__lock);

		res = poll(fds, 1, -1);	/* return 0 on timeout ? */

		ast_mutex_lock(&s->__lock);
		s->waiting_thread = AST_PTHREADT_NULL;
		ast_mutex_unlock(&s->__lock);
	}
	if (res < 0) {
		/* If we get a signal from some other thread (typically because
		 * there are new events queued), return 0 to notify the caller.
		 */
		if (errno == EINTR)
			return 0;
		ast_log(LOG_WARNING, "poll() returned error: %s\n", strerror(errno));
		return -1;
	}
	ast_mutex_lock(&s->__lock);
	res = read(s->fd, src + s->inlen, maxlen - s->inlen);
	if (res < 1)
		res = -1;	/* error return */
	else {
		s->inlen += res;
		src[s->inlen] = '\0';
		res = 0;
	}
	ast_mutex_unlock(&s->__lock);
	return res;
}

/*! \brief The body of the individual manager session.
 * Call get_input() to read one line at a time
 * (or be woken up on new events), collect the lines in a
 * message until found an empty line, and execute the request.
 * In any case, deliver events asynchronously through process_events()
 * (called from here if no line is available, or at the end of
 * process_message(). )
 */
static void *session_do(void *data)
{
	struct mansession *s = data;
	struct message m;	/* XXX watch out, this is 20k of memory! */

	ast_mutex_lock(&s->__lock);
	astman_append(s, "Asterisk Call Manager/1.0\r\n");	/* welcome prompt */
	ast_mutex_unlock(&s->__lock);
	memset(&m, 0, sizeof(m));
	for (;;) {
		char *buf = m.headers[m.hdrcount];
		int res = get_input(s, buf);
		if (res < 0)	/* error */
			break;
		if (res > 0) {	/* got one line */
			if (ast_strlen_zero(buf)) {	/* empty line, terminator */
				if (process_message(s, &m))
					break;
				memset(&m, 0, sizeof(m));
			} else if (m.hdrcount < AST_MAX_MANHEADERS - 1)
				m.hdrcount++;
		} else if (process_events(s))
			break;
	}
	/* session is over, explain why and terminate */
	if (s->authenticated) {
		if (option_verbose > 1) {
			if (displayconnects)
				ast_verbose(VERBOSE_PREFIX_2 "Manager '%s' logged off from %s\n", s->username, ast_inet_ntoa(s->sin.sin_addr));
		}
		ast_log(LOG_EVENT, "Manager '%s' logged off from %s\n", s->username, ast_inet_ntoa(s->sin.sin_addr));
	} else {
		if (option_verbose > 1) {
			if (displayconnects)
				ast_verbose(VERBOSE_PREFIX_2 "Connect attempt from '%s' unable to authenticate\n", ast_inet_ntoa(s->sin.sin_addr));
		}
		ast_log(LOG_EVENT, "Failed attempt from %s\n", ast_inet_ntoa(s->sin.sin_addr));
	}
	destroy_session(s);
	return NULL;
}

/*! \brief The thread accepting connections on the manager interface port.
 * As a side effect, it purges stale sessions, one per each iteration,
 * which is at least every 5 seconds.
 */
static void *accept_thread(void *ignore)
{
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	for (;;) {
		struct mansession *s;
		time_t now = time(NULL);
		int as;
		struct sockaddr_in sin;
		socklen_t sinlen;
		struct protoent *p;
		int flags;
		struct pollfd pfds[1];

		AST_LIST_LOCK(&sessions);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&sessions, s, list) {
			if (s->sessiontimeout && (now > s->sessiontimeout) && !s->inuse) {
				ast_verbose("destroy session[2] %lx now %lu to %lu\n",
					s->managerid, (unsigned long)now, (unsigned long)s->sessiontimeout);
				AST_LIST_REMOVE_CURRENT(&sessions, list);
				ast_atomic_fetchadd_int(&num_sessions, -1);
				if (s->authenticated && (option_verbose > 1) && displayconnects) {
					ast_verbose(VERBOSE_PREFIX_2 "HTTP Manager '%s' timed out from %s\n",
						s->username, ast_inet_ntoa(s->sin.sin_addr));
				}
				free_session(s);	/* XXX outside ? */
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END
		AST_LIST_UNLOCK(&sessions);
		purge_unused();

		sinlen = sizeof(sin);
		pfds[0].fd = asock;
		pfds[0].events = POLLIN;
		/* Wait for something to happen, but timeout every few seconds so
		   we can ditch any old manager sessions */
		if (poll(pfds, 1, 5000) < 1)
			continue;
		as = accept(asock, (struct sockaddr *)&sin, &sinlen);
		if (as < 0) {
			ast_log(LOG_NOTICE, "Accept returned -1: %s\n", strerror(errno));
			continue;
		}
		p = getprotobyname("tcp");
		if (p) {
			int arg = 1;
			if( setsockopt(as, p->p_proto, TCP_NODELAY, (char *)&arg, sizeof(arg) ) < 0 ) {
				ast_log(LOG_WARNING, "Failed to set manager tcp connection to TCP_NODELAY mode: %s\n", strerror(errno));
			}
		}
		s = ast_calloc(1, sizeof(*s));	/* allocate a new record */
		if (!s) {
			close(as);
			continue;
		}


		s->sin = sin;
		s->writetimeout = 100;
		s->waiting_thread = AST_PTHREADT_NULL;

		flags = fcntl(as, F_GETFL);
		if (!block_sockets) /* For safety, make sure socket is non-blocking */
			flags |= O_NONBLOCK;
		else
			flags &= ~O_NONBLOCK;
		fcntl(as, F_SETFL, flags);

		ast_mutex_init(&s->__lock);
		s->fd = as;
		s->send_events = -1;

		ast_atomic_fetchadd_int(&num_sessions, 1);
		AST_LIST_LOCK(&sessions);
		AST_LIST_INSERT_HEAD(&sessions, s, list);
		AST_LIST_UNLOCK(&sessions);
		/* Hook to the tail of the event queue */
		s->last_ev = grab_last();
		if (ast_pthread_create_background(&s->ms_t, &attr, session_do, s))
			destroy_session(s);
	}
	pthread_attr_destroy(&attr);
	return NULL;
}

/*
 * events are appended to a queue from where they
 * can be dispatched to clients.
 */
static int append_event(const char *str, int category)
{
	struct eventqent *tmp = ast_malloc(sizeof(*tmp) + strlen(str));
	static int seq;	/* sequence number */

	if (!tmp)
		return -1;

	/* need to init all fields, because ast_malloc() does not */
	tmp->usecount = 0;
	tmp->category = category;
	tmp->seq = ast_atomic_fetchadd_int(&seq, 1);
	AST_LIST_NEXT(tmp, eq_next) = NULL;
	strcpy(tmp->eventdata, str);

	AST_LIST_LOCK(&all_events);
	AST_LIST_INSERT_TAIL(&all_events, tmp, eq_next);
	AST_LIST_UNLOCK(&all_events);

	return 0;
}

/*! \brief  manager_event: Send AMI event to client */
int __manager_event(int category, const char *event,
	const char *file, int line, const char *func, const char *fmt, ...)
{
	struct mansession *s;
	struct manager_custom_hook *hook;
	char auth[80];
	char tmp[4096] = "";
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
	if (manager_debug) {
		static int seq;
		ast_dynamic_str_thread_append(&buf, 0, &manager_event_buf,
				"SequenceNumber: %d\r\n",
				 ast_atomic_fetchadd_int(&seq, 1));
		ast_dynamic_str_thread_append(&buf, 0, &manager_event_buf,
				"File: %s\r\nLine: %d\r\nFunc: %s\r\n", file, line, func);
	}

	va_start(ap, fmt);
	ast_dynamic_str_thread_append_va(&buf, 0, &manager_event_buf, fmt, ap);
	va_end(ap);

	ast_dynamic_str_thread_append(&buf, 0, &manager_event_buf, "\r\n");

	append_event(buf->str, category);

	/* Wake up any sleeping sessions */
	AST_LIST_LOCK(&sessions);
	AST_LIST_TRAVERSE(&sessions, s, list) {
		ast_mutex_lock(&s->__lock);
		if (s->waiting_thread != AST_PTHREADT_NULL)
			pthread_kill(s->waiting_thread, SIGURG);
		ast_mutex_unlock(&s->__lock);
	}
	AST_LIST_UNLOCK(&sessions);

	AST_RWLIST_RDLOCK(&manager_hooks);
	if (!AST_RWLIST_EMPTY(&manager_hooks)) {
		char *p;
		int len;
		snprintf(tmp, sizeof(tmp), "event: %s\r\nprivilege: %s\r\n", event, authority_to_str(category, tmp, sizeof(tmp)));
                len = strlen(tmp);
                p = tmp + len;
                va_start(ap, fmt);
                vsnprintf(p, sizeof(tmp) - len, fmt, ap);
                va_end(ap);
		AST_RWLIST_TRAVERSE(&manager_hooks, hook, list) {
			hook->helper(category, event, tmp);
		}
	}
	AST_RWLIST_UNLOCK(&manager_hooks);

	return 0;
}

/*
 * support functions to register/unregister AMI action handlers,
 */
int ast_manager_unregister(char *action)
{
	struct manager_action *cur = first_action, *prev = first_action;

	ast_mutex_lock(&actionlock);
	for (cur = first_action, prev = NULL; cur; prev = cur, cur = cur->next) {
		if (!strcasecmp(action, cur->action)) {
			if (prev)
				prev->next = cur->next;
			else
				first_action = cur->next;
			free(cur);
			if (option_verbose > 1)
				ast_verbose(VERBOSE_PREFIX_2 "Manager unregistered action %s\n", action);
			break;
		}
	}
	ast_mutex_unlock(&actionlock);
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

	ast_mutex_lock(&actionlock);
	for (cur = first_action; cur; prev = cur, cur = cur->next) {
		ret = strcasecmp(cur->action, act->action);
		if (ret == 0) {
			ast_log(LOG_WARNING, "Manager: Action '%s' already registered\n", act->action);
			ast_mutex_unlock(&actionlock);
			return -1;
		}
		if (ret > 0)	/* Insert these alphabetically */
			break;
	}
	if (prev)
		prev->next = act;
	else
		first_action = act;
	act->next = cur;

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Manager registered action %s\n", act->action);
	ast_mutex_unlock(&actionlock);
	return 0;
}

/*! \brief register a new command with manager, including online help. This is
	the preferred way to register a manager command */
int ast_manager_register2(const char *action, int auth, int (*func)(struct mansession *s, struct message *m), const char *synopsis, const char *description)
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

	ast_manager_register_struct(cur);

	return 0;
}
/*! @}
 END Doxygen group */

/*
 * The following are support functions for AMI-over-http.
 * The common entry point is generic_http_callback(),
 * which extracts HTTP header and URI fields and reformats
 * them into AMI messages, locates a proper session
 * (using the mansession_id Cookie or GET variable),
 * and calls process_message() as for regular AMI clients.
 * When done, the output (which goes to a temporary file)
 * is read back into a buffer and reformatted as desired,
 * then fed back to the client over the original socket.
 */

enum output_format {
	FORMAT_RAW,
	FORMAT_HTML,
	FORMAT_XML,
};

static char *contenttype[] = {
	[FORMAT_RAW] = "plain",
	[FORMAT_HTML] = "html",
	[FORMAT_XML] =  "xml",
};

/*!
 * locate an http session in the list. The search key (ident) is
 * the value of the mansession_id cookie (0 is not valid and means
 * a session on the AMI socket).
 */
static struct mansession *find_session(unsigned long ident)
{
	struct mansession *s;

	if (ident == 0)
		return NULL;

	AST_LIST_LOCK(&sessions);
	AST_LIST_TRAVERSE(&sessions, s, list) {
		ast_mutex_lock(&s->__lock);
		if (s->managerid == ident && !s->needdestroy) {
			ast_atomic_fetchadd_int(&s->inuse, 1);
			break;
		}
		ast_mutex_unlock(&s->__lock);
	}
	AST_LIST_UNLOCK(&sessions);

	return s;
}

static void vars2msg(struct message *m, struct ast_variable *vars)
{
	int x;
	for (x = 0; vars && (x < AST_MAX_MANHEADERS); x++, vars = vars->next) {
		if (!vars)
			break;
		m->hdrcount = x + 1;
		snprintf(m->headers[x], sizeof(m->headers[x]), "%s: %s", vars->name, vars->value);
	}
}

/*
 * convert to xml with various conversion:
 * mode & 1	-> lowercase;
 * mode & 2	-> replace non-alphanumeric chars with underscore
 */
static void xml_copy_escape(char **dst, size_t *maxlen, const char *src, int mode)
{
	for ( ; *src && *maxlen > 6; src++) {
		if ( (mode & 2) && !isalnum(*src)) {
			*(*dst)++ = '_';
			(*maxlen)--;
			continue;
		}
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
			*(*dst)++ = mode ? tolower(*src) : *src;
			(*maxlen)--;
		}
	}
}

/*! \brief Convert the input into XML or HTML.
 * The input is supposed to be a sequence of lines of the form
 *	Name: value
 * optionally followed by a blob of unformatted text.
 * A blank line is a section separator. Basically, this is a
 * mixture of the format of Manager Interface and CLI commands.
 * The unformatted text is considered as a single value of a field
 * named 'Opaque-data'.
 *
 * At the moment the output format is the following (but it may
 * change depending on future requirements so don't count too
 * much on it when writing applications):
 *
 * General: the unformatted text is used as a value of
 * XML output:  to be completed
 *   Each section is within <response type="object" id="xxx">
 *   where xxx is taken from ajaxdest variable or defaults to unknown
 *   Each row is reported as an attribute Name="value" of an XML
 *   entity named from the variable ajaxobjtype, default to "generic"
 *
 * HTML output:
 *   each Name-value pair is output as a single row of a two-column table.
 *   Sections (blank lines in the input) are separated by a <HR>
 *
 */
static char *xml_translate(char *in, struct ast_variable *vars, enum output_format format)
{
	struct ast_variable *v;
	char *dest = NULL;
	char *out, *tmp, *var, *val;
	char *objtype = NULL;
	int colons = 0;
	int breaks = 0;
	size_t len;
	int in_data = 0;	/* parsing data */
	int escaped = 0;
	int inobj = 0;
	int x;
	int xml = (format == FORMAT_XML);

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

	/* determine how large is the response.
	 * This is a heuristic - counting colons (for headers),
	 * newlines (for extra arguments), and escaped chars.
	 * XXX needs to be checked carefully for overflows.
	 * Even better, use some code that allows extensible strings.
	 */
	for (x = 0; in[x]; x++) {
		if (in[x] == ':')
			colons++;
		else if (in[x] == '\n')
			breaks++;
		else if (strchr("&\"<>", in[x]))
			escaped++;
	}
	len = (size_t) (strlen(in) + colons * 5 + breaks * (40 + strlen(dest) + strlen(objtype)) + escaped * 10); /* foo="bar", "<response type=\"object\" id=\"dest\"", "&amp;" */
	out = ast_malloc(len);
	if (!out)
		return NULL;
	tmp = out;
	/* we want to stop when we find an empty line */
	while (in && *in) {
		val = strsep(&in, "\r\n");	/* mark start and end of line */
		if (in && *in == '\n')		/* remove trailing \n if any */
			in++;
		ast_trim_blanks(val);
		if (0)
			ast_verbose("inobj %d in_data %d line <%s>\n", inobj, in_data, val);
		if (ast_strlen_zero(val)) {
			if (in_data) { /* close data */
				ast_build_string(&tmp, &len, xml ? "'" : "</td></tr>\n");
				in_data = 0;
			}
			ast_build_string(&tmp, &len, xml ? " /></response>\n" :
				"<tr><td colspan=\"2\"><hr></td></tr>\r\n");
			inobj = 0;
			continue;
		}
		/* we expect Name: value lines */
		if (in_data) {
			var = NULL;
		} else {
			var = strsep(&val, ":");
			if (val) {	/* found the field name */
				val = ast_skip_blanks(val);
				ast_trim_blanks(var);
			} else {		/* field name not found, move to opaque mode */
				val = var;
				var = "Opaque-data";
			}
		}
		if (!inobj) {
			if (xml)
				ast_build_string(&tmp, &len, "<response type='object' id='%s'><%s", dest, objtype);
			else
				ast_build_string(&tmp, &len, "<body>\n");
			inobj = 1;
		}
		if (!in_data) {	/* build appropriate line start */
			ast_build_string(&tmp, &len, xml ? " " : "<tr><td>");
			xml_copy_escape(&tmp, &len, var, xml ? 1 | 2 : 0);
			ast_build_string(&tmp, &len, xml ? "='" : "</td><td>");
			if (!strcmp(var, "Opaque-data"))
				in_data = 1;
		}
		xml_copy_escape(&tmp, &len, val, 0);	/* data field */
		if (!in_data)
			ast_build_string(&tmp, &len, xml ? "'" : "</td></tr>\n");
		else
			ast_build_string(&tmp, &len, xml ? "\n" : "<br>\n");
	}
	if (inobj)
		ast_build_string(&tmp, &len, xml ? " /></response>\n" :
			"<tr><td colspan=\"2\"><hr></td></tr>\r\n");
	return out;
}

static char *generic_http_callback(enum output_format format,
	struct sockaddr_in *requestor, const char *uri,
	struct ast_variable *params, int *status,
	char **title, int *contentlength)
{
	struct mansession *s = NULL;
	unsigned long ident = 0; /* invalid, so find_session will fail if not set through the cookie */
	char workspace[1024];
	size_t len = sizeof(workspace);
	int blastaway = 0;
	char *c = workspace;
	char *retval = NULL;
	struct message m;
	struct ast_variable *v;
	char template[] = "/tmp/ast-http-XXXXXX";	/* template for temporary file */

	for (v = params; v; v = v->next) {
		if (!strcasecmp(v->name, "mansession_id")) {
			sscanf(v->value, "%lx", &ident);
			break;
		}
	}

	if (!(s = find_session(ident))) {
		/* Create new session.
		 * While it is not in the list we don't need any locking
		 */
		if (!(s = ast_calloc(1, sizeof(*s)))) {
			*status = 500;
			goto generic_callback_out;
		}
		s->sin = *requestor;
		s->fd = -1;
		s->waiting_thread = AST_PTHREADT_NULL;
		s->send_events = 0;
		ast_mutex_init(&s->__lock);
		ast_mutex_lock(&s->__lock);
		s->inuse = 1;
		s->managerid = rand() | 1;	/* make sure it is non-zero */
		s->last_ev = grab_last();
		AST_LIST_LOCK(&sessions);
		AST_LIST_INSERT_HEAD(&sessions, s, list);
		AST_LIST_UNLOCK(&sessions);
		ast_atomic_fetchadd_int(&num_sessions, 1);
	}

	ast_mutex_unlock(&s->__lock);
	memset(&m, 0, sizeof(m));
	{
		char tmp[80];
		char cookie[128];

		ast_build_string(&c, &len, "Content-type: text/%s\r\n", contenttype[format]);
		ast_build_string(&c, &len, "Cache-Control: no-cache;\r\n");
		sprintf(tmp, "%08lx", s->managerid);
		ast_build_string(&c, &len, "%s\r\n", ast_http_setcookie("mansession_id", tmp, httptimeout, cookie, sizeof(cookie)));
	}

	if (format == FORMAT_HTML)
		ast_build_string(&c, &len, "<title>Asterisk&trade; Manager Test Interface</title>");
	vars2msg(&m, params);

	if (format == FORMAT_XML) {
		ast_build_string(&c, &len, "<ajax-response>\n");
	} else if (format == FORMAT_HTML) {

#define ROW_FMT	"<tr><td colspan=\"2\" bgcolor=\"#f1f1ff\">%s</td></tr>\r\n"
#define TEST_STRING \
	"<form action=\"manager\">action: <input name=\"action\"> cmd <input name=\"command\"><br> \
	user <input name=\"username\"> pass <input type=\"password\" name=\"secret\"><br> \
	<input type=\"submit\"></form>"

		ast_build_string(&c, &len, "<body bgcolor=\"#ffffff\"><table align=center bgcolor=\"#f1f1f1\" width=\"500\">\r\n");
		ast_build_string(&c, &len, ROW_FMT, "<h1>Manager Tester</h1>");
		ast_build_string(&c, &len, ROW_FMT, TEST_STRING);
	}

	s->fd = mkstemp(template);	/* create a temporary file for command output */

	if (process_message(s, &m)) {
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
	if (s->fd > -1) {	/* have temporary output */
		char *buf;
		off_t len = lseek(s->fd, 0, SEEK_END);	/* how many chars available */

		if (len > 0 && (buf = ast_calloc(1, len+1))) {
			if (!s->outputstr)
				s->outputstr = ast_calloc(1, sizeof(*s->outputstr));
			if (s->outputstr) {
				lseek(s->fd, 0, SEEK_SET);
				read(s->fd, buf, len);
				if (0)
					ast_verbose("--- fd %d has %d bytes ---\n%s\n---\n", s->fd, (int)len, buf);
				ast_dynamic_str_append(&s->outputstr, 0, "%s", buf);
			}
			free(buf);
		}
		close(s->fd);
		s->fd = -1;
		unlink(template);
	}

	if (s->outputstr) {
		char *tmp;
		if (format == FORMAT_XML || format == FORMAT_HTML)
			tmp = xml_translate(s->outputstr->str, params, format);
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
	/* Still okay because c would safely be pointing to workspace even
	   if retval failed to allocate above */
	if (format == FORMAT_XML) {
		ast_build_string(&c, &len, "</ajax-response>\n");
	} else if (format == FORMAT_HTML)
		ast_build_string(&c, &len, "</table></body>\r\n");

	ast_mutex_lock(&s->__lock);
	/* Reset HTTP timeout.  If we're not authenticated, keep it extremely short */
	s->sessiontimeout = time(NULL) + ((s->authenticated || httptimeout < 5) ? httptimeout : 5);
	if (0)
		ast_verbose("die in %d seconds\n",
			(int)(s->sessiontimeout - time(NULL)) );
	if (s->needdestroy) {
		if (s->inuse == 1) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Need destroy, doing it now!\n");
			blastaway = 1;
		} else {
			if (option_debug)
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
	struct ast_config *cfg = NULL;
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
		ast_manager_register2("Login", 0, action_login, "Login Manager", NULL);
		ast_manager_register2("Challenge", 0, action_challenge, "Generate Challenge for MD5 Auth", NULL);
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
		ast_manager_register2("SendText", EVENT_FLAG_CALL, action_sendtext, "Send text message to channel", mandescr_sendtext);
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

	if ((val = ast_variable_retrieve(cfg, "general", "debug")))
		manager_debug = ast_true(val);

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

	while ((cat = ast_category_browse(cfg, cat))) {
		struct ast_variable *var = NULL;

		if (!strcasecmp(cat, "general"))
			continue;

		/* Look for an existing entry, if none found - create one and add it to the list */
		if (!(user = get_manager_by_name_locked(cat))) {
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
			else {
				if (option_debug)
					ast_log(LOG_DEBUG, "%s is an unknown option.\n", var->name);
			}
			var = var->next;
		}
	}

	/* Perform cleanup - essentially prune out old users that no longer exist */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&users, user, list) {
		if (user->keep) {	/* valid record. clear flag for the next round */
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
		ast_pthread_create_background(&accept_thread_ptr, NULL, accept_thread, NULL);
	}
	return 0;
}

int reload_manager(void)
{
	manager_event(EVENT_FLAG_SYSTEM, "Reload", "Message: Reload Requested\r\n");
	return init_manager();
}
