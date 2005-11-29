/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel Management and more
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <asterisk/channel.h>
#include <asterisk/file.h>
#include <asterisk/manager.h>
#include <asterisk/config.h>
#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/pbx.h>

static int enabled = 0;
static int portno = DEFAULT_MANAGER_PORT;
static int asock = -1;
static pthread_t t;
static pthread_mutex_t sessionlock = AST_MUTEX_INITIALIZER;

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
	{ -1, "all" },
};

static struct mansession *sessions = NULL;
static struct manager_action *first_action = NULL;
static pthread_mutex_t actionlock = AST_MUTEX_INITIALIZER;

static int handle_showmancmds(int fd, int argc, char *argv[])
{
	struct manager_action *cur = first_action;

	ast_pthread_mutex_lock(&actionlock);
	while(cur) { /* Walk the list of actions */
		ast_cli(fd, "\t%s  %s\r\n",cur->action, cur->synopsis);
		cur = cur->next;
	}

	ast_pthread_mutex_unlock(&actionlock);
	return RESULT_SUCCESS;
}

static char showmancmds_help[] = 
"Usage: show manager commands\n"
"	Prints a listing of all the available manager commands.\n";

static struct ast_cli_entry show_mancmds_cli =
	{ { "show", "manager", "commands", NULL },
	handle_showmancmds, "Show manager commands", showmancmds_help };

static void destroy_session(struct mansession *s)
{
	struct mansession *cur, *prev = NULL;
	ast_pthread_mutex_lock(&sessionlock);
	cur = sessions;
	while(cur) {
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
		if (s->fd > -1)
			close(s->fd);
		free(s);
	} else
		ast_log(LOG_WARNING, "Trying to delete non-existant session %p?\n", s);
	ast_pthread_mutex_unlock(&sessionlock);
	
}

static char *get_header(struct message *m, char *var)
{
	char cmp[80];
	int x;
	snprintf(cmp, sizeof(cmp), "%s: ", var);
	for (x=0;x<m->hdrcount;x++)
		if (!strncasecmp(cmp, m->headers[x], strlen(cmp)))
			return m->headers[x] + strlen(cmp);
	return "";
}

static void send_error(struct mansession *s, char *error)
{
	ast_pthread_mutex_lock(&s->lock);
	ast_cli(s->fd, "Response: Error\r\n");
	ast_cli(s->fd, "Message: %s\r\n\r\n", error);
	ast_pthread_mutex_unlock(&s->lock);
}

static void send_response(struct mansession *s, char *resp, char *msg)
{
	ast_pthread_mutex_lock(&s->lock);
	ast_cli(s->fd, "Response: %s\r\n", resp);
	if (msg)
		ast_cli(s->fd, "Message: %s\r\n\r\n", msg);
	else
		ast_cli(s->fd, "\r\n");
	ast_pthread_mutex_unlock(&s->lock);
}

static void send_ack(struct mansession *s, char *msg)
{
	send_response(s, "Success", msg);
}

static int get_perm(char *instr)
{
	char tmp[256];
	char *c;
	int x;
	int ret = 0;
	char *stringp=NULL;
	if (!instr)
		return 0;
	strncpy(tmp, instr, sizeof(tmp) - 1);
	stringp=tmp;
	c = strsep(&stringp, ",");
	while(c) {
		for (x=0;x<sizeof(perms) / sizeof(perms[0]);x++) {
			if (!strcasecmp(perms[x].label, c)) 
				ret |= perms[x].num;
		}
		c = strsep(&stringp, ",");
	}
	return ret;
}

static int authenticate(struct mansession *s, struct message *m)
{
	struct ast_config *cfg;
	char *cat;
	char *user = get_header(m, "Username");
	char *pass = get_header(m, "Secret");
	cfg = ast_load("manager.conf");
	if (!cfg)
		return -1;
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			/* This is a user */
			if (!strcasecmp(cat, user)) {
				char *password = ast_variable_retrieve(cfg, cat, "secret");
				if (password && !strcasecmp(password, pass)) {
					break;
				} else {
					ast_log(LOG_NOTICE, "%s failed to authenticate as '%s'\n", inet_ntoa(s->sin.sin_addr), user);
					ast_destroy(cfg);
					return -1;
				}	
			}
		}
		cat = ast_category_browse(cfg, cat);
	}
	if (cat) {
		strncpy(s->username, cat, sizeof(s->username) - 1);
		s->readperm = get_perm(ast_variable_retrieve(cfg, cat, "read"));
		s->writeperm = get_perm(ast_variable_retrieve(cfg, cat, "write"));
		ast_destroy(cfg);
		return 0;
	}
	ast_log(LOG_NOTICE, "%s tried to authenticate with non-existant user '%s'\n", inet_ntoa(s->sin.sin_addr), user);
	ast_destroy(cfg);
	return -1;
}

static int action_ping(struct mansession *s, struct message *m)
{
	send_response(s, "Pong", NULL);
	return 0;
}

static int action_logoff(struct mansession *s, struct message *m)
{
	send_response(s, "Goodbye", "Thanks for all the fish.");
	return -1;
}

static int action_hangup(struct mansession *s, struct message *m)
{
	struct ast_channel *c = NULL;
	char *name = get_header(m, "Channel");
	if (!strlen(name)) {
		send_error(s, "No channel specified");
		return 0;
	}
	c = ast_channel_walk(NULL);
	while(c) {
		if (!strcasecmp(c->name, name)) {
			break;
		}
		c = ast_channel_walk(c);
	}
	if (!c) {
		send_error(s, "No such channel");
		return 0;
	}
	ast_softhangup(c, AST_SOFTHANGUP_EXPLICIT);
	send_ack(s, "Channel Hungup");
	return 0;
}

static int action_status(struct mansession *s, struct message *m)
{
	struct ast_channel *c;
	char bridge[256];
	send_ack(s, "Channel status will follow");
	c = ast_channel_walk(NULL);
	while(c) {
		if (c->bridge)
			snprintf(bridge, sizeof(bridge), "Link: %s\r\n", c->bridge->name);
		else
			strcpy(bridge, "");
		if (c->pbx) {
			ast_cli(s->fd,
			"Event: Status\r\n"
			"Channel: %s\r\n"
			"CallerID: %s\r\n"
			"State: %s\r\n"
			"Context: %s\r\n"
			"Extension: %s\r\n"
			"Priority: %d\r\n"
			"%s"
			"\r\n",
			c->name, c->callerid ? c->callerid : "<unknown>", 
			ast_state2str(c->_state), c->context,
			c->exten, c->priority, bridge);
		} else {
			ast_cli(s->fd,
			"Event: Status\r\n"
			"Channel: %s\r\n"
			"CallerID: %s\r\n"
			"State: %s\r\n"
			"%s"
			"\r\n",
			c->name, c->callerid ? c->callerid : "<unknown>", 
			ast_state2str(c->_state), bridge);
		}
		c = ast_channel_walk(c);
	}
	return 0;
}

static int action_redirect(struct mansession *s, struct message *m)
{
	char *name = get_header(m, "Channel");
	char *name2 = get_header(m, "ExtraChannel");
	char *exten = get_header(m, "Exten");
	char *context = get_header(m, "Context");
	char *priority = get_header(m, "Priority");
	int pi = 0;
	int res;
	if (!name || !strlen(name)) {
		send_error(s, "Channel not specified");
		return 0;
	}
	if (strlen(priority) && (sscanf(priority, "%d", &pi) != 1)) {
		send_error(s, "Invalid priority\n");
		return 0;
	}
	res = ast_async_goto_by_name(name, context, exten, pi);
	if (!res) {
		if (strlen(name2)) {
			res = ast_async_goto_by_name(name2, context, exten, pi);
			if (!res)
				send_ack(s, "Dual Redirect successful");
			else
				send_error(s, "Secondary redirect failed");
		} else
			send_ack(s, "Redirect successful");
	} else
		send_error(s, "Redirect failed");
	return 0;
}

static int action_command(struct mansession *s, struct message *m)
{
	char *cmd = get_header(m, "Command");
	ast_pthread_mutex_lock(&s->lock);
	s->blocking = 1;
	ast_pthread_mutex_unlock(&s->lock);
	ast_cli(s->fd, "Response: Follows\r\n");
	ast_cli_command(s->fd, cmd);
	ast_cli(s->fd, "--END COMMAND--\r\n\r\n");
	ast_pthread_mutex_lock(&s->lock);
	s->blocking = 0;
	ast_pthread_mutex_unlock(&s->lock);
	return 0;
}

static int action_originate(struct mansession *s, struct message *m)
{
	char *name = get_header(m, "Channel");
	char *exten = get_header(m, "Exten");
	char *context = get_header(m, "Context");
	char *priority = get_header(m, "Priority");
	char *timeout = get_header(m, "Timeout");
	char *callerid = get_header(m, "CallerID");
	char *tech, *data;
	int pi = 0;
	int res;
	int to = 30000;
	int reason = 0;
	char tmp[256];
	if (!name) {
		send_error(s, "Channel not specified");
		return 0;
	}
	if (strlen(priority) && (sscanf(priority, "%d", &pi) != 1)) {
		send_error(s, "Invalid priority\n");
		return 0;
	}
	if (strlen(timeout) && (sscanf(timeout, "%d", &to) != 1)) {
		send_error(s, "Invalid timeout\n");
		return 0;
	}
	strncpy(tmp, name, sizeof(tmp) - 1);
	tech = tmp;
	data = strchr(tmp, '/');
	if (!data) {
		send_error(s, "Invalid channel\n");
		return 0;
	}
	*data = '\0';
	data++;
	res = ast_pbx_outgoing_exten(tech, AST_FORMAT_SLINEAR, data, to, context, exten, pi, &reason, 0, strlen(callerid) ? callerid : NULL, NULL );
	if (!res)
		send_ack(s, "Originate successfully queued");
	else
		send_error(s, "Originate failed");
	return 0;
}

static int process_message(struct mansession *s, struct message *m)
{
	char action[80];
	struct manager_action *tmp = first_action;

	strncpy(action, get_header(m, "Action"), sizeof(action));

	if (!strlen(action)) {
		send_error(s, "Missing action in request");
		return 0;
	}
	if (!s->authenticated) {
		if (!strcasecmp(action, "Login")) {
			if (authenticate(s, m)) {
				sleep(1);
				send_error(s, "Authentication failed");
				return -1;
			} else {
				s->authenticated = 1;
				if (option_verbose > 1) 
					ast_verbose(VERBOSE_PREFIX_2 "Manager '%s' logged on from %s\n", s->username, inet_ntoa(s->sin.sin_addr));
				ast_log(LOG_EVENT, "Manager '%s' logged on from %s\n", s->username, inet_ntoa(s->sin.sin_addr));
				send_ack(s, "Authentication accepted");
			}
		} else 
			send_error(s, "Authentication Required");
	} else {
		while( tmp ) { 		
			if (!strcasecmp(action, tmp->action)) {
				if ((s->writeperm & tmp->authority) == tmp->authority) {
					if (tmp->func(s, m))
						return -1;
				} else {
					send_error(s, "Permission denied");
				}
				return 0;
			}
			tmp = tmp->next;
		}
		send_error(s, "Invalid/unknown command");
	}
	return 0;
}

static int get_input(struct mansession *s, char *output)
{
	/* output must have at least sizeof(s->inbuf) space */
	int res;
	int x;
	fd_set fds;
	for (x=1;x<s->inlen;x++) {
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
		ast_log(LOG_WARNING, "Dumping long line with no return from %s: %s\n", inet_ntoa(s->sin.sin_addr), s->inbuf);
		s->inlen = 0;
	}
	FD_ZERO(&fds);
	FD_SET(s->fd, &fds);
	res = select(s->fd + 1, &fds, NULL, NULL, NULL);
	if (res < 0) {
		ast_log(LOG_WARNING, "Select returned error: %s\n", strerror(errno));
	} else if (res > 0) {
		ast_pthread_mutex_lock(&s->lock);
		res = read(s->fd, s->inbuf + s->inlen, sizeof(s->inbuf) - 1 - s->inlen);
		ast_pthread_mutex_unlock(&s->lock);
		if (res < 1)
			return -1;
	}
	s->inlen += res;
	s->inbuf[s->inlen] = '\0';
	return 0;
}

static void *session_do(void *data)
{
	struct mansession *s = data;
	struct message m;
	int res;
	
	ast_pthread_mutex_lock(&s->lock);
	ast_cli(s->fd, "Asterisk Call Manager/1.0\r\n");
	ast_pthread_mutex_unlock(&s->lock);
	memset(&m, 0, sizeof(&m));
	for (;;) {
		res = get_input(s, m.headers[m.hdrcount]);
		if (res > 0) {
			/* Strip trailing \r\n */
			if (strlen(m.headers[m.hdrcount]) < 2)
				continue;
			m.headers[m.hdrcount][strlen(m.headers[m.hdrcount]) - 2] = '\0';
			if (!strlen(m.headers[m.hdrcount])) {
				if (process_message(s, &m))
					break;
				memset(&m, 0, sizeof(&m));
			} else if (m.hdrcount < MAX_HEADERS - 1)
				m.hdrcount++;
		} else if (res < 0)
			break;
	}
	if (s->authenticated) {
		if (option_verbose > 1) 
			ast_verbose(VERBOSE_PREFIX_2 "Manager '%s' logged off from %s\n", s->username, inet_ntoa(s->sin.sin_addr));
		ast_log(LOG_EVENT, "Manager '%s' logged off from %s\n", s->username, inet_ntoa(s->sin.sin_addr));
	} else {
		if (option_verbose > 1)
			ast_verbose(VERBOSE_PREFIX_2 "Connect attempt from '%s' unable to authenticate\n", inet_ntoa(s->sin.sin_addr));
		ast_log(LOG_EVENT, "Failed attempt from %s\n", inet_ntoa(s->sin.sin_addr));
	}
	destroy_session(s);
	return NULL;
}

static void *accept_thread(void *ignore)
{
	int as;
	struct sockaddr_in sin;
	int sinlen;
	struct mansession *s;
	for (;;) {
		sinlen = sizeof(sin);
		as = accept(asock, &sin, &sinlen);
		if (as < 0) {
			ast_log(LOG_NOTICE, "Accept returned -1: %s\n", strerror(errno));
			continue;
		}
		s = malloc(sizeof(struct mansession));
		if (!s) {
			ast_log(LOG_WARNING, "Failed to allocate management session: %s\n", strerror(errno));
			continue;
		} 
		memset(s, 0, sizeof(struct mansession));
		memcpy(&s->sin, &sin, sizeof(sin));
		ast_pthread_mutex_init(&s->lock);
		s->fd = as;
		ast_pthread_mutex_lock(&sessionlock);
		s->next = sessions;
		sessions = s;
		ast_pthread_mutex_unlock(&sessionlock);
		if (pthread_create(&t, NULL, session_do, s))
			destroy_session(s);
	}
	return NULL;
}

int manager_event(int category, char *event, char *fmt, ...)
{
	struct mansession *s;
	char tmp[4096];
	va_list ap;

	ast_pthread_mutex_lock(&sessionlock);
	s = sessions;
	while(s) {
		if ((s->readperm & category) == category) {
			ast_pthread_mutex_lock(&s->lock);
			if (!s->blocking) {
				ast_cli(s->fd, "Event: %s\r\n", event);
				va_start(ap, fmt);
				vsnprintf(tmp, sizeof(tmp), fmt, ap);
				va_end(ap);
				write(s->fd, tmp, strlen(tmp));
				ast_cli(s->fd, "\r\n");
			}
			ast_pthread_mutex_unlock(&s->lock);
		}
		s = s->next;
	}
	ast_pthread_mutex_unlock(&sessionlock);
	return 0;
}

int ast_manager_unregister( char *action ) {
	struct manager_action *cur = first_action, *prev = first_action;

	ast_pthread_mutex_lock(&actionlock);
	while( cur ) { 		
		if (!strcasecmp(action, cur->action)) {
			prev->next = cur->next;
			free(cur);
			if (option_verbose > 1) 
				ast_verbose(VERBOSE_PREFIX_2 "Manager unregistered action %s\n", action);
			ast_pthread_mutex_unlock(&actionlock);
			return 0;
		}
		prev = cur;
		cur = cur->next;
	}
	ast_pthread_mutex_unlock(&actionlock);
	return 0;
}

int ast_manager_register( char *action, int auth, 
	int (*func)(struct mansession *s, struct message *m), char *synopsis)
{
	struct manager_action *cur = first_action, *prev = NULL;

	ast_pthread_mutex_lock(&actionlock);
	while(cur) { /* Walk the list of actions */
		prev = cur; 
		cur = cur->next;
	}
	cur = malloc( sizeof(struct manager_action) );
	if( !cur ) {
		ast_log(LOG_WARNING, "Manager: out of memory trying to register action\n");
		ast_pthread_mutex_unlock(&actionlock);
		return -1;
	}
	strncpy( cur->action, action, 255 );
	cur->authority = auth;
	cur->func = func;
	cur->synopsis = synopsis;
	cur->next = NULL;

	if( prev ) prev->next = cur;
	else first_action = cur;

	if (option_verbose > 1) 
		ast_verbose(VERBOSE_PREFIX_2 "Manager registered action %s\n", action);
	ast_pthread_mutex_unlock(&actionlock);
	return 0;
}

static int registered = 0;

int init_manager(void)
{
	struct ast_config *cfg;
	char *val;
	int oldportno = portno;
	static struct sockaddr_in ba;
	int x = 1;
	if (!registered) {
		/* Register default actions */
		ast_manager_register( "Ping", 0, action_ping, "Ping" );
		ast_manager_register( "Logoff", 0, action_logoff, "Logoff Manager" );
		ast_manager_register( "Hangup", EVENT_FLAG_CALL, action_hangup, "Hangup Channel" );
		ast_manager_register( "Status", EVENT_FLAG_CALL, action_status, "Status" );
		ast_manager_register( "Redirect", EVENT_FLAG_CALL, action_redirect, "Redirect" );
		ast_manager_register( "Originate", EVENT_FLAG_CALL, action_originate, "Originate Call" );
		ast_manager_register( "Command", EVENT_FLAG_COMMAND, action_command, "Execute Command" );

		ast_cli_register(&show_mancmds_cli);
		registered = 1;
	}
	portno = DEFAULT_MANAGER_PORT;
	cfg = ast_load("manager.conf");
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to open management configuration manager.conf.  Call management disabled.\n");
		return 0;
	}
	memset(&ba, 0, sizeof(ba));
	val = ast_variable_retrieve(cfg, "general", "enabled");
	if (val)
		enabled = ast_true(val);

	if ((val = ast_variable_retrieve(cfg, "general", "portno"))) {
		if (sscanf(val, "%d", &portno) != 1) {
			ast_log(LOG_WARNING, "Invalid port number '%s'\n", val);
			portno = DEFAULT_MANAGER_PORT;
		}
	}
	
	ba.sin_family = AF_INET;
	ba.sin_port = htons(portno);
	memset(&ba.sin_addr, 0, sizeof(ba.sin_addr));
	
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
		if (bind(asock, &ba, sizeof(ba))) {
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
		if (option_verbose)
			ast_verbose("Asterisk Management interface listening on port %d\n", portno);
		pthread_create(&t, NULL, accept_thread, NULL);
	}
	ast_destroy(cfg);
	return 0;
}

int reload_manager(void)
{
	return init_manager();
}
