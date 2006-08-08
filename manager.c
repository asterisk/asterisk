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

struct eventqent {
	int usecount;
	int category;
	ast_mutex_t lock;
	struct eventqent *next;
	char eventdata[1];
};

static int enabled = 0;
static int portno = DEFAULT_MANAGER_PORT;
static int asock = -1;
static int displayconnects = 1;
static int timestampevents = 0;
static int httptimeout = 60;

static pthread_t t;
AST_MUTEX_DEFINE_STATIC(sessionlock);
static int block_sockets = 0;
static int num_sessions = 0;
struct eventqent *master_eventq = NULL;

AST_THREADSTORAGE(manager_event_buf, manager_event_buf_init);
#define MANAGER_EVENT_BUF_INITSIZE   256

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

static struct mansession {
	/*! Execution thread */
	pthread_t t;
	/*! Thread lock -- don't use in action callbacks, it's already taken care of  */
	ast_mutex_t __lock;
	/*! socket address */
	struct sockaddr_in sin;
	/*! TCP socket */
	int fd;
	/*! Whether or not we're busy doing an action */
	int busy;
	/*! Whether or not we're "dead" */
	int dead;
	/*! Whether an HTTP manager is in use */
	int inuse;
	/*! Whether an HTTP session should be destroyed */
	int needdestroy;
	/*! Whether an HTTP session has someone waiting on events */
	pthread_t waiting_thread;
	/*! Unique manager identifer */
	unsigned long managerid;
	/*! Session timeout if HTTP */
	time_t sessiontimeout;
	/*! Output from manager interface */
	char *outputstr;
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
	char inbuf[AST_MAX_MANHEADER_LEN];
	int inlen;
	int send_events;
	int displaysystemname;		/*!< Add system name to manager responses and events */
	/* Queued events that we've not had the ability to send yet */
	struct eventqent *eventq;
	/* Timeout for ast_carefulwrite() */
	int writetimeout;
	struct mansession *next;
} *sessions = NULL;

static struct manager_action *first_action = NULL;
AST_MUTEX_DEFINE_STATIC(actionlock);

/*! \brief Convert authority code to string with serveral options */
static char *authority_to_str(int authority, char *res, int reslen)
{
	int running_total = 0, i;
	memset(res, 0, reslen);
	for (i=0; i<sizeof(perms) / sizeof(perms[0]) - 1; i++) {
		if (authority & perms[i].num) {
			if (*res) {
				strncat(res, ",", (reslen > running_total) ? reslen - running_total : 0);
				running_total++;
			}
			strncat(res, perms[i].label, (reslen > running_total) ? reslen - running_total : 0);
			running_total += strlen(perms[i].label);
		}
	}
	if (ast_strlen_zero(res)) {
		ast_copy_string(res, "<none>", reslen);
	}
	return res;
}

static char *complete_show_mancmd(const char *line, const char *word, int pos, int state)
{
	struct manager_action *cur;
	int which = 0;
	char *ret = NULL;

	ast_mutex_lock(&actionlock);
	for (cur = first_action; cur; cur = cur->next) { /* Walk the list of actions */
		if (!strncasecmp(word, cur->action, strlen(word)) && ++which > state) {
			ret = ast_strdup(cur->action);
			break;	/* make sure we exit even if ast_strdup() returns NULL */
		}
	}
	ast_mutex_unlock(&actionlock);
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
	v = vars;

	while (v) {
		if (!dest && !strcasecmp(v->name, "ajaxdest"))
			dest = v->value;
		else if (!objtype && !strcasecmp(v->name, "ajaxobjtype")) 
			objtype = v->value;
		v = v->next;
	}
	if (!dest)
		dest = "unknown";
	if (!objtype)
		objtype = "generic";
	for (x=0; in[x]; x++) {
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
					ast_build_string(&tmp, &len, "<response type='object' id='%s'><%s", dest, objtype);
					inobj = 1;
				}
				ast_build_string(&tmp, &len, " ");				
				xml_copy_escape(&tmp, &len, var, 1);
				ast_build_string(&tmp, &len, "='");
				xml_copy_escape(&tmp, &len, val, 0);
				ast_build_string(&tmp, &len, "'");
			}
		}
	}
	if (inobj)
		ast_build_string(&tmp, &len, " /></response>\n");
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

void astman_append(struct mansession *s, const char *fmt, ...)
{
	char *stuff;
	int res;
	va_list ap;
	char *tmp;

	va_start(ap, fmt);
	res = vasprintf(&stuff, fmt, ap);
	va_end(ap);
	if (res == -1) {
		ast_log(LOG_ERROR, "Memory allocation failure\n");
		return;
	} 
	if (s->fd > -1)
		ast_carefulwrite(s->fd, stuff, strlen(stuff), s->writetimeout);
	else {
		tmp = realloc(s->outputstr, (s->outputstr ? strlen(s->outputstr) : 0) + strlen(stuff) + 1);
		if (tmp) {
			if (!s->outputstr)
				tmp[0] = '\0';
			s->outputstr = tmp;
			strcat(s->outputstr, stuff);
		}
	}
	free(stuff);
}

static int handle_showmancmd(int fd, int argc, char *argv[])
{
	struct manager_action *cur = first_action;
	char authority[80];
	int num;

	if (argc != 4)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&actionlock);
	while (cur) { /* Walk the list of actions */
		for (num = 3; num < argc; num++) {
			if (!strcasecmp(cur->action, argv[num])) {
				ast_cli(fd, "Action: %s\nSynopsis: %s\nPrivilege: %s\n%s\n", cur->action, cur->synopsis, authority_to_str(cur->authority, authority, sizeof(authority) -1), cur->description ? cur->description : "");
			}
		}
		cur = cur->next;
	}

	ast_mutex_unlock(&actionlock);
	return RESULT_SUCCESS;
}

/*! \brief  CLI command 
	Should change to "manager show commands" */
static int handle_showmancmds(int fd, int argc, char *argv[])
{
	struct manager_action *cur = first_action;
	char authority[80];
	char *format = "  %-15.15s  %-15.15s  %-55.55s\n";

	ast_mutex_lock(&actionlock);
	ast_cli(fd, format, "Action", "Privilege", "Synopsis");
	ast_cli(fd, format, "------", "---------", "--------");
	while (cur) { /* Walk the list of actions */
		ast_cli(fd, format, cur->action, authority_to_str(cur->authority, authority, sizeof(authority) -1), cur->synopsis);
		cur = cur->next;
	}

	ast_mutex_unlock(&actionlock);
	return RESULT_SUCCESS;
}

/*! \brief CLI command show manager connected */
/* Should change to "manager show connected" */
static int handle_showmanconn(int fd, int argc, char *argv[])
{
	struct mansession *s;
	char *format = "  %-15.15s  %-15.15s\n";
	ast_mutex_lock(&sessionlock);
	s = sessions;
	ast_cli(fd, format, "Username", "IP Address");
	while (s) {
		ast_cli(fd, format,s->username, ast_inet_ntoa(s->sin.sin_addr));
		s = s->next;
	}

	ast_mutex_unlock(&sessionlock);
	return RESULT_SUCCESS;
}

/*! \brief CLI command show manager connected */
/* Should change to "manager show connected" */
static int handle_showmaneventq(int fd, int argc, char *argv[])
{
	struct eventqent *s;
	ast_mutex_lock(&sessionlock);
	s = master_eventq;
	while (s) {
		ast_cli(fd, "Usecount: %d\n",s->usecount);
		ast_cli(fd, "Category: %d\n", s->category);
		ast_cli(fd, "Event:\n%s", s->eventdata);
		s = s->next;
	}
	ast_mutex_unlock(&sessionlock);
	return RESULT_SUCCESS;
}

static char showmancmd_help[] = 
"Usage: show manager command <actionname>\n"
"	Shows the detailed description for a specific Asterisk manager interface command.\n";

static char showmancmds_help[] = 
"Usage: show manager commands\n"
"	Prints a listing of all the available Asterisk manager interface commands.\n";

static char showmanconn_help[] = 
"Usage: show manager connected\n"
"	Prints a listing of the users that are currently connected to the\n"
"Asterisk manager interface.\n";

static char showmaneventq_help[] = 
"Usage: show manager eventq\n"
"	Prints a listing of all events pending in the Asterisk manger\n"
"event queue.\n";

static struct ast_cli_entry show_mancmd_cli =
	{ { "show", "manager", "command", NULL },
	handle_showmancmd, "Show a manager interface command", showmancmd_help, complete_show_mancmd };

static struct ast_cli_entry show_mancmds_cli =
	{ { "show", "manager", "commands", NULL },
	handle_showmancmds, "List manager interface commands", showmancmds_help };

static struct ast_cli_entry show_manconn_cli =
	{ { "show", "manager", "connected", NULL },
	handle_showmanconn, "Show connected manager interface users", showmanconn_help };

static struct ast_cli_entry show_maneventq_cli =
	{ { "show", "manager", "eventq", NULL },
	handle_showmaneventq, "Show manager interface queued events", showmaneventq_help };

static void unuse_eventqent(struct eventqent *e)
{
	/* XXX Need to atomically decrement the users.  Change this to atomic_dec
	       one day when we have such a beast XXX */
	int val;
	ast_mutex_lock(&e->lock);
	e->usecount--;
	val = !e->usecount && e->next;
	ast_mutex_unlock(&e->lock);
	/* Wake up sleeping beauty */
	if (val)
		pthread_kill(t, SIGURG);
}

static void free_session(struct mansession *s)
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

static void destroy_session(struct mansession *s)
{
	struct mansession *cur, *prev = NULL;
	ast_mutex_lock(&sessionlock);
	cur = sessions;
	while (cur) {
		if (cur == s)
			break;
		prev = cur;
		cur = cur->next;
	}
	if (cur) {
		if (prev)
			prev->next = cur->next;
		else
			sessions = cur->next;
		free_session(s);
		num_sessions--;
	} else
		ast_log(LOG_WARNING, "Trying to delete nonexistent session %p?\n", s);
	ast_mutex_unlock(&sessionlock);
}

char *astman_get_header(struct message *m, char *var)
{
	char cmp[80];
	int x;
	snprintf(cmp, sizeof(cmp), "%s: ", var);
	for (x=0; x<m->hdrcount; x++)
		if (!strncasecmp(cmp, m->headers[x], strlen(cmp)))
			return m->headers[x] + strlen(cmp);
	return "";
}

struct ast_variable *astman_get_variables(struct message *m)
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
void astman_send_error(struct mansession *s, struct message *m, char *error)
{
	char *id = astman_get_header(m,"ActionID");

	astman_append(s, "Response: Error\r\n");
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n", id);
	astman_append(s, "Message: %s\r\n\r\n", error);
}

void astman_send_response(struct mansession *s, struct message *m, char *resp, char *msg)
{
	char *id = astman_get_header(m,"ActionID");

	astman_append(s, "Response: %s\r\n", resp);
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n", id);
	if (msg)
		astman_append(s, "Message: %s\r\n\r\n", msg);
	else
		astman_append(s, "\r\n");
}

void astman_send_ack(struct mansession *s, struct message *m, char *msg)
{
	astman_send_response(s, m, "Success", msg);
}

/*! Tells you if smallstr exists inside bigstr
   which is delim by delim and uses no buf or stringsep
   ast_instring("this|that|more","this",',') == 1;

   feel free to move this to app.c -anthm */
static int ast_instring(char *bigstr, char *smallstr, char delim) 
{
	char *val = bigstr, *next;

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

static int get_perm(char *instr)
{
	int x = 0, ret = 0;

	if (!instr)
		return 0;

	for (x=0; x<sizeof(perms) / sizeof(perms[0]); x++)
		if (ast_instring(instr, perms[x].label, ','))
			ret |= perms[x].num;
	
	return ret;
}

static int ast_is_number(char *string) 
{
	int ret = 1, x = 0;

	if (!string)
		return 0;

	for (x=0; x < strlen(string); x++) {
		if (!(string[x] >= 48 && string[x] <= 57)) {
			ret = 0;
			break;
		}
	}
	
	return ret ? atoi(string) : 0;
}

static int ast_strings_to_mask(char *string) 
{
	int x, ret = -1;
	
	x = ast_is_number(string);

	if (x) {
		ret = x;
	} else if (ast_strlen_zero(string)) {
		ret = -1;
	} else if (ast_false(string)) {
		ret = 0;
	} else if (ast_true(string)) {
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
static int set_eventmask(struct mansession *s, char *eventmask)
{
	int maskint = ast_strings_to_mask(eventmask);

	ast_mutex_lock(&s->__lock);
	if (maskint >= 0)	
		s->send_events = maskint;
	ast_mutex_unlock(&s->__lock);
	
	return maskint;
}

static int authenticate(struct mansession *s, struct message *m)
{
	struct ast_config *cfg;
	char *cat;
	char *user = astman_get_header(m, "Username");
	char *pass = astman_get_header(m, "Secret");
	char *authtype = astman_get_header(m, "AuthType");
	char *key = astman_get_header(m, "Key");
	char *events = astman_get_header(m, "Events");
	
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
				v = ast_variable_browse(cfg, cat);
				while (v) {
					if (!strcasecmp(v->name, "secret")) {
						password = v->value;
					} else if (!strcasecmp(v->name, "displaysystemname")) {
						if (ast_true(v->value)) {
							if (ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
								s->displaysystemname = 1;
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
							s->writetimeout = val;
					}
				    		
					v = v->next;
				}
				if (ha && !ast_apply_ha(ha, &(s->sin))) {
					ast_log(LOG_NOTICE, "%s failed to pass IP ACL as '%s'\n", ast_inet_ntoa(s->sin.sin_addr), user);
					ast_free_ha(ha);
					ast_config_destroy(cfg);
					return -1;
				} else if (ha)
					ast_free_ha(ha);
				if (!strcasecmp(authtype, "MD5")) {
					if (!ast_strlen_zero(key) && s->challenge) {
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
							break;
						else {
							ast_config_destroy(cfg);
							return -1;
						}
					}
				} else if (password && !strcmp(password, pass)) {
					break;
				} else {
					ast_log(LOG_NOTICE, "%s failed to authenticate as '%s'\n", ast_inet_ntoa(s->sin.sin_addr), user);
					ast_config_destroy(cfg);
					return -1;
				}	
			}
		}
		cat = ast_category_browse(cfg, cat);
	}
	if (cat) {
		ast_copy_string(s->username, cat, sizeof(s->username));
		s->readperm = get_perm(ast_variable_retrieve(cfg, cat, "read"));
		s->writeperm = get_perm(ast_variable_retrieve(cfg, cat, "write"));
		ast_config_destroy(cfg);
		if (events)
			set_eventmask(s, events);
		return 0;
	}
	ast_log(LOG_NOTICE, "%s tried to authenticate with nonexistent user '%s'\n", ast_inet_ntoa(s->sin.sin_addr), user);
	ast_config_destroy(cfg);
	return -1;
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
	char idText[256] = "";
	char *id = astman_get_header(m, "ActionID");

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	if (ast_strlen_zero(fn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}
	if (!(cfg = ast_config_load(fn))) {
		astman_send_error(s, m, "Config file not found");
		return 0;
	}
	astman_append(s, "Response: Success\r\n%s", idText);
	while ((category = ast_category_browse(cfg, category))) {
		lineno = 0;
		astman_append(s, "Category-%06d: %s\r\n", catcount, category);
		v = ast_variable_browse(cfg, category);
		while (v) {
			astman_append(s, "Line-%06d-%06d: %s=%s\r\n", catcount, lineno++, v->name, v->value);
			v = v->next;
		}
		catcount++;
	}
	ast_config_destroy(cfg);
	astman_append(s, "\r\n");
	return 0;
}


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
	char idText[256] = "";
	char *id = astman_get_header(m, "ActionID");

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);

	if (ast_strlen_zero(sfn) || ast_strlen_zero(dfn)) {
		astman_send_error(s, m, "Filename not specified");
		return 0;
	}
	if (!(cfg = ast_config_load(sfn))) {
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
	struct eventqent *eqe;
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
		if (s->busy == 1)
			s->busy = 2;
	}
	ast_mutex_unlock(&s->__lock);
	s->waiting_thread = pthread_self();
	if (option_debug)
		ast_log(LOG_DEBUG, "Starting waiting for an event!\n");
	for (x=0; ((x < timeout) || (timeout < 0)); x++) {
		ast_mutex_lock(&s->__lock);
		if (s->eventq && s->eventq->next)
			needexit = 1;
		if (s->waiting_thread != pthread_self())
			needexit = 1;
		if (s->needdestroy)
			needexit = 1;
		ast_mutex_unlock(&s->__lock);
		if (needexit)
			break;
		if (s->fd > 0) {
			if (ast_wait_for_input(s->fd, 1000))
				break;
		} else {
			sleep(1);
		}
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "Finished waiting for an event!\n");
	ast_mutex_lock(&s->__lock);
	if (s->waiting_thread == pthread_self()) {
		astman_send_response(s, m, "Success", "Waiting for Event...");
		/* Only show events if we're the most recent waiter */
		while(s->eventq->next) {
			eqe = s->eventq->next;
			if (((s->readperm & eqe->category) == eqe->category) &&
			    ((s->send_events & eqe->category) == eqe->category)) {
				astman_append(s, "%s", eqe->eventdata);
			}
			unuse_eventqent(s->eventq);
			s->eventq = eqe;
		}
		astman_append(s,
			"Event: WaitEventComplete\r\n"
			"%s"
			"\r\n", idText);
		s->waiting_thread = AST_PTHREADT_NULL;
	} else {
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
	struct manager_action *cur = first_action;
	char idText[256] = "";
	char temp[BUFSIZ];
	char *id = astman_get_header(m,"ActionID");

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	astman_append(s, "Response: Success\r\n%s", idText);
	ast_mutex_lock(&actionlock);
	while (cur) { /* Walk the list of actions */
		if ((s->writeperm & cur->authority) == cur->authority)
			astman_append(s, "%s: %s (Priv: %s)\r\n", cur->action, cur->synopsis, authority_to_str(cur->authority, temp, sizeof(temp)));
		cur = cur->next;
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
	char *id = astman_get_header(m,"ActionID");
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
	astman_append(s, "Response: Success\r\n"
		"Variable: %s\r\nValue: %s\r\n", varname, varval);
	if (!ast_strlen_zero(id))
		astman_append(s, "ActionID: %s\r\n",id);
	astman_append(s, "\r\n");

	return 0;
}


/*! \brief Manager "status" command to show channels */
/* Needs documentation... */
static int action_status(struct mansession *s, struct message *m)
{
	char *id = astman_get_header(m,"ActionID");
    	char *name = astman_get_header(m,"Channel");
	char idText[256] = "";
	struct ast_channel *c;
	char bridge[256];
	struct timeval now = ast_tvnow();
	long elapsed_seconds = 0;
	int all = ast_strlen_zero(name); /* set if we want all channels */

	astman_send_ack(s, m, "Channel status will follow");
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
	/* XXX watch out, possible deadlock!!! */
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

/*! \brief  action_command: Manager command "command" - execute CLI command */
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
		"CallerID: %s\r\n"		/* This parameter is deprecated and will be removed post-1.4 */
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n",
		in->idtext, in->tech, in->data, in->context, in->exten, reason, 
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
	char *id = astman_get_header(m,"ActionID");
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
static int action_mailboxcount(struct mansession *s, struct message *m)
{
	char *mailbox = astman_get_header(m, "Mailbox");
	char *id = astman_get_header(m,"ActionID");
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

static int action_extensionstate(struct mansession *s, struct message *m)
{
	char *exten = astman_get_header(m, "Exten");
	char *context = astman_get_header(m, "Context");
	char *id = astman_get_header(m,"ActionID");
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

static int action_timeout(struct mansession *s, struct message *m)
{
	struct ast_channel *c = NULL;
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

static int process_events(struct mansession *s)
{
	struct eventqent *eqe;
	int ret = 0;
	ast_mutex_lock(&s->__lock);
	if (s->fd > -1) {
		s->busy--;
		if (!s->eventq)
			s->eventq = master_eventq;
		while(s->eventq->next) {
			eqe = s->eventq->next;
			if ((s->authenticated && (s->readperm & eqe->category) == eqe->category) &&
			    ((s->send_events & eqe->category) == eqe->category)) {
				if (!ret && ast_carefulwrite(s->fd, eqe->eventdata, strlen(eqe->eventdata), s->writetimeout) < 0)
					ret = -1;
			}
			unuse_eventqent(s->eventq);
			s->eventq = eqe;
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

static int process_message(struct mansession *s, struct message *m)
{
	char action[80] = "";
	struct manager_action *tmp = first_action;
	char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	int ret = 0;

	ast_copy_string(action, astman_get_header(m, "Action"), sizeof(action));
	ast_log( LOG_DEBUG, "Manager received command '%s'\n", action );

	if (ast_strlen_zero(action)) {
		astman_send_error(s, m, "Missing action in request");
		return 0;
	}
	if (!ast_strlen_zero(id)) {
		snprintf(idText, sizeof(idText), "ActionID: %s\r\n", id);
	}
	if (!s->authenticated) {
		if (!strcasecmp(action, "Challenge")) {
			char *authtype;
			authtype = astman_get_header(m, "AuthType");
			if (!strcasecmp(authtype, "MD5")) {
				if (ast_strlen_zero(s->challenge))
					snprintf(s->challenge, sizeof(s->challenge), "%ld", ast_random());
				ast_mutex_lock(&s->__lock);
				astman_append(s, "Response: Success\r\n"
						"%s"
						"Challenge: %s\r\n\r\n",
						idText, s->challenge);
				ast_mutex_unlock(&s->__lock);
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
				s->authenticated = 1;
				if (option_verbose > 1) {
					if (displayconnects) {
						ast_verbose(VERBOSE_PREFIX_2 "%sManager '%s' logged on from %s\n", (s->sessiontimeout ? "HTTP " : ""), s->username, ast_inet_ntoa(s->sin.sin_addr));
					}
				}
				ast_log(LOG_EVENT, "%sManager '%s' logged on from %s\n", (s->sessiontimeout ? "HTTP " : ""), s->username, ast_inet_ntoa(s->sin.sin_addr));
				astman_send_ack(s, m, "Authentication accepted");
			}
		} else if (!strcasecmp(action, "Logoff")) {
			astman_send_ack(s, m, "See ya");
			return -1;
		} else
			astman_send_error(s, m, "Authentication Required");
	} else {
		ast_mutex_lock(&s->__lock);
		s->busy++;
		ast_mutex_unlock(&s->__lock);
		while (tmp) { 		
			if (!strcasecmp(action, tmp->action)) {
				if ((s->writeperm & tmp->authority) == tmp->authority) {
					if (tmp->func(s, m))
						ret = -1;
				} else {
					astman_send_error(s, m, "Permission denied");
				}
				break;
			}
			tmp = tmp->next;
		}
		if (!tmp)
			astman_send_error(s, m, "Invalid/unknown command");
	}
	if (ret)
		return ret;
	return process_events(s);
}

static int get_input(struct mansession *s, char *output)
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
		s->waiting_thread = pthread_self();
		ast_mutex_unlock(&s->__lock);

		res = poll(fds, 1, -1);

		ast_mutex_lock(&s->__lock);
		s->waiting_thread = AST_PTHREADT_NULL;
		ast_mutex_unlock(&s->__lock);
		if (res < 0) {
			if (errno == EINTR) {
				if (s->dead)
					return -1;
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

static void *session_do(void *data)
{
	struct mansession *s = data;
	struct message m;
	int res;
	
	ast_mutex_lock(&s->__lock);
	astman_append(s, "Asterisk Call Manager/1.0\r\n");
	ast_mutex_unlock(&s->__lock);
	memset(&m, 0, sizeof(m));
	for (;;) {
		res = get_input(s, m.headers[m.hdrcount]);
		if (res > 0) {
			/* Strip trailing \r\n */
			if (strlen(m.headers[m.hdrcount]) < 2)
				continue;
			m.headers[m.hdrcount][strlen(m.headers[m.hdrcount]) - 2] = '\0';
			if (ast_strlen_zero(m.headers[m.hdrcount])) {
				if (process_message(s, &m))
					break;
				memset(&m, 0, sizeof(m));
			} else if (m.hdrcount < AST_MAX_MANHEADERS - 1)
				m.hdrcount++;
		} else if (res < 0) {
			break;
		} else if (s->eventq->next) {
			if (process_events(s))
				break;
		}
	}
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

static void *accept_thread(void *ignore)
{
	int as;
	struct sockaddr_in sin;
	socklen_t sinlen;
	struct eventqent *eqe;
	struct mansession *s, *prev = NULL, *next;
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
		ast_mutex_lock(&sessionlock);
		prev = NULL;
		s = sessions;
		while (s) {
			next = s->next;
			if (s->sessiontimeout && (now > s->sessiontimeout) && !s->inuse) {
				num_sessions--;
				if (prev)
					prev->next = next;
				else
					sessions = next;
				if (s->authenticated && (option_verbose > 1) && displayconnects) {
					ast_verbose(VERBOSE_PREFIX_2 "HTTP Manager '%s' timed out from %s\n",
						s->username, ast_inet_ntoa(s->sin.sin_addr));
				}
				free_session(s);
			} else
				prev = s;
			s = next;
		}
		/* Purge master event queue of old, unused events, but make sure we
		   always keep at least one in the queue */
		eqe = master_eventq;
		while (master_eventq->next && !master_eventq->usecount) {
			eqe = master_eventq;
			master_eventq = master_eventq->next;
			free(eqe);
		}
		ast_mutex_unlock(&sessionlock);

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
		}
		ast_mutex_init(&s->__lock);
		s->fd = as;
		s->send_events = -1;
		ast_mutex_lock(&sessionlock);
		num_sessions++;
		s->next = sessions;
		sessions = s;
		/* Find the last place in the master event queue and hook ourselves
		   in there */
		s->eventq = master_eventq;
		while(s->eventq->next)
			s->eventq = s->eventq->next;
		ast_mutex_lock(&s->eventq->lock);
		s->eventq->usecount++;
		ast_mutex_unlock(&s->eventq->lock);
		ast_mutex_unlock(&sessionlock);
		if (ast_pthread_create(&s->t, &attr, session_do, s))
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

	ast_mutex_init(&tmp->lock);
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
	struct mansession *s;
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
	
	ast_mutex_lock(&sessionlock);
	/* Append even to master list and wake up any sleeping sessions */
	append_event(buf->str, category);
	for (s = sessions; s; s = s->next) {
		ast_mutex_lock(&s->__lock);
		if (s->waiting_thread != AST_PTHREADT_NULL)
			pthread_kill(s->waiting_thread, SIGURG);
		ast_mutex_unlock(&s->__lock);
	}
	ast_mutex_unlock(&sessionlock);

	return 0;
}

int ast_manager_unregister( char *action ) 
{
	struct manager_action *cur = first_action, *prev = first_action;

	ast_mutex_lock(&actionlock);
	while (cur) {
		if (!strcasecmp(action, cur->action)) {
			prev->next = cur->next;
			free(cur);
			if (option_verbose > 1) 
				ast_verbose(VERBOSE_PREFIX_2 "Manager unregistered action %s\n", action);
			ast_mutex_unlock(&actionlock);
			return 0;
		}
		prev = cur;
		cur = cur->next;
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
	struct manager_action *cur = first_action, *prev = NULL;
	int ret;

	ast_mutex_lock(&actionlock);
	while (cur) { /* Walk the list of actions */
		ret = strcasecmp(cur->action, act->action);
		if (ret == 0) {
			ast_log(LOG_WARNING, "Manager: Action '%s' already registered\n", act->action);
			ast_mutex_unlock(&actionlock);
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

static struct mansession *find_session(unsigned long ident)
{
	struct mansession *s;
	ast_mutex_lock(&sessionlock);
	s = sessions;
	while (s) {
		ast_mutex_lock(&s->__lock);
		if (s->sessiontimeout && (s->managerid == ident) && !s->needdestroy) {
			s->inuse++;
			break;
		}
		ast_mutex_unlock(&s->__lock);
		s = s->next;
	}
	ast_mutex_unlock(&sessionlock);
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

#define FORMAT_RAW	0
#define FORMAT_HTML	1
#define FORMAT_XML	2

static char *contenttype[] = { "plain", "html", "xml" };

static char *generic_http_callback(int format, struct sockaddr_in *requestor, const char *uri, struct ast_variable *params, int *status, char **title, int *contentlength)
{
	struct mansession *s = NULL;
	unsigned long ident = 0;
	char workspace[512];
	char cookie[128];
	size_t len = sizeof(workspace);
	int blastaway = 0;
	char *c = workspace;
	char *retval = NULL;
	struct message m;
	struct ast_variable *v;

	v = params;
	while (v) {
		if (!strcasecmp(v->name, "mansession_id")) {
			sscanf(v->value, "%lx", &ident);
			break;
		}
		v = v->next;
	}
	s = find_session(ident);

	if (!s) {
		/* Create new session */
		s = ast_calloc(1, sizeof(struct mansession));
		if (!s) {
			*status = 500;
			goto generic_callback_out;
		}
		memcpy(&s->sin, requestor, sizeof(s->sin));
		s->fd = -1;
		s->waiting_thread = AST_PTHREADT_NULL;
		s->send_events = 0;
		ast_mutex_init(&s->__lock);
		ast_mutex_lock(&s->__lock);
		ast_mutex_lock(&sessionlock);
		s->inuse = 1;
		s->managerid = rand() | (unsigned long)s;
		s->next = sessions;
		sessions = s;
		num_sessions++;
		/* Hook into the last spot in the event queue */
		s->eventq = master_eventq;
		while (s->eventq->next)
			s->eventq = s->eventq->next;
		ast_mutex_lock(&s->eventq->lock);
		s->eventq->usecount++;
		ast_mutex_unlock(&s->eventq->lock);
		ast_mutex_unlock(&sessionlock);
	}

	/* Reset HTTP timeout.  If we're not yet authenticated, keep it extremely short */
	time(&s->sessiontimeout);
	if (!s->authenticated && (httptimeout > 5))
		s->sessiontimeout += 5;
	else
		s->sessiontimeout += httptimeout;
	ast_mutex_unlock(&s->__lock);
	
	memset(&m, 0, sizeof(m));
	if (s) {
		char tmp[80];
		ast_build_string(&c, &len, "Content-type: text/%s\r\n", contenttype[format]);
		sprintf(tmp, "%08lx", s->managerid);
		ast_build_string(&c, &len, "%s\r\n", ast_http_setcookie("mansession_id", tmp, httptimeout, cookie, sizeof(cookie)));
		if (format == FORMAT_HTML)
			ast_build_string(&c, &len, "<title>Asterisk&trade; Manager Test Interface</title>");
		vars2msg(&m, params);
		if (format == FORMAT_XML) {
			ast_build_string(&c, &len, "<ajax-response>\n");
		} else if (format == FORMAT_HTML) {
			ast_build_string(&c, &len, "<body bgcolor=\"#ffffff\"><table align=center bgcolor=\"#f1f1f1\" width=\"500\">\r\n");
			ast_build_string(&c, &len, "<tr><td colspan=\"2\" bgcolor=\"#f1f1ff\"><h1>&nbsp;&nbsp;Manager Tester</h1></td></tr>\r\n");
		}
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
		if (s->outputstr) {
			char *tmp;
			if (format == FORMAT_XML)
				tmp = xml_translate(s->outputstr, params);
			else if (format == FORMAT_HTML)
				tmp = html_translate(s->outputstr);
			else
				tmp = s->outputstr;
			if (tmp) {
				retval = malloc(strlen(workspace) + strlen(tmp) + 128);
				if (retval) {
					strcpy(retval, workspace);
					strcpy(retval + strlen(retval), tmp);
					c = retval + strlen(retval);
					len = 120;
				}
				free(tmp);
			}
			if (tmp != s->outputstr)
				free(s->outputstr);
			s->outputstr = NULL;
		}
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
	struct ast_config *cfg;
	char *val;
	int oldportno = portno;
	static struct sockaddr_in ba;
	int x = 1;
	int flags;
	int webenabled = 0;
	int newhttptimeout = 60;
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

		ast_cli_register(&show_mancmd_cli);
		ast_cli_register(&show_mancmds_cli);
		ast_cli_register(&show_manconn_cli);
		ast_cli_register(&show_maneventq_cli);
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
		ast_pthread_create(&t, NULL, accept_thread, NULL);
	}
	return 0;
}

int reload_manager(void)
{
	manager_event(EVENT_FLAG_SYSTEM, "Reload", "Message: Reload Requested\r\n");
	return init_manager();
}
