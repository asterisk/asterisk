/*
 * ASTerisk MANager
 * Copyright (C) 2002, Linux Support Services, Inc.
 *
 * Distributed under the terms of the GNU General Public License
 */
 
#include <newt.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

#include <asterisk/md5.h>
#include <asterisk/manager.h>

#undef gethostbyname

#define MAX_HEADERS 80
#define MAX_LEN 256

static struct ast_mansession {
	struct sockaddr_in sin;
	int fd;
	char inbuf[MAX_LEN];
	int inlen;
} session;

static struct ast_chan {
	char name[80];
	char exten[20];
	char context[20];
	char priority[20];
	char callerid[40];
	char state[10];
	struct ast_chan *next;
} *chans;

static struct ast_chan *find_chan(char *name)
{
	struct ast_chan *prev = NULL, *chan = chans;
	while(chan) {
		if (!strcmp(name, chan->name))
			return chan;
		prev = chan;
		chan = chan->next;
	}
	chan = malloc(sizeof(struct ast_chan));
	if (chan) {
		memset(chan, 0, sizeof(struct ast_chan));
		strncpy(chan->name, name, sizeof(chan->name) - 1);
		if (prev) 
			prev->next = chan;
		else
			chans = chan;
	}
	return chan;
}

static void del_chan(char *name)
{
	struct ast_chan *prev = NULL, *chan = chans;
	while(chan) {
		if (!strcmp(name, chan->name)) {
			if (prev)
				prev->next = chan->next;
			else
				chans = chan->next;
			return;
		}
		prev = chan;
		chan = chan->next;
	}
}

static void fdprintf(int fd, char *fmt, ...)
{
	char stuff[4096];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(stuff, sizeof(stuff), fmt, ap);
	va_end(ap);
	write(fd, stuff, strlen(stuff));
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

static int event_newstate(struct ast_mansession *s, struct message *m)
{
	struct ast_chan *chan;
	chan = find_chan(get_header(m, "Channel"));
	strncpy(chan->state, get_header(m, "State"), sizeof(chan->state) - 1);
	return 0;
}

static int event_newexten(struct ast_mansession *s, struct message *m)
{
	struct ast_chan *chan;
	chan = find_chan(get_header(m, "Channel"));
	strncpy(chan->exten, get_header(m, "Extension"), sizeof(chan->exten) - 1);
	strncpy(chan->context, get_header(m, "Context"), sizeof(chan->context) - 1);
	strncpy(chan->priority, get_header(m, "Priority"), sizeof(chan->priority) - 1);
	return 0;
}

static int event_newchannel(struct ast_mansession *s, struct message *m)
{
	struct ast_chan *chan;
	chan = find_chan(get_header(m, "Channel"));
	strncpy(chan->state, get_header(m, "State"), sizeof(chan->state) - 1);
	strncpy(chan->callerid, get_header(m, "Callerid"), sizeof(chan->callerid) - 1);
	return 0;
}

static int event_status(struct ast_mansession *s, struct message *m)
{
	struct ast_chan *chan;
	chan = find_chan(get_header(m, "Channel"));
	strncpy(chan->state, get_header(m, "State"), sizeof(chan->state) - 1);
	strncpy(chan->callerid, get_header(m, "Callerid"), sizeof(chan->callerid) - 1);
	strncpy(chan->exten, get_header(m, "Extension"), sizeof(chan->exten) - 1);
	strncpy(chan->context, get_header(m, "Context"), sizeof(chan->context) - 1);
	strncpy(chan->priority, get_header(m, "Priority"), sizeof(chan->priority) - 1);
	return 0;
}

static int event_hangup(struct ast_mansession *s, struct message *m)
{
	del_chan(get_header(m, "Channel"));
	return 0;
}

static int event_ignore(struct ast_mansession *s, struct message *m)
{
	return 0;
}

static int event_rename(struct ast_mansession *s, struct message *m)
{
	struct ast_chan *chan;
	chan = find_chan(get_header(m, "Oldname"));
	strncpy(chan->name, get_header(m, "Newname"), sizeof(chan->name) - 1);
	return 0;
}
static struct event {
	char *event;
	int (*func)(struct ast_mansession *s, struct message *m);
} events[] = {
	{ "Newstate", event_newstate },
	{ "Newchannel", event_newchannel },
	{ "Newexten", event_newexten },
	{ "Hangup", event_hangup },
	{ "Rename", event_rename },
	{ "Status", event_status },
	{ "Link", event_ignore },
	{ "Unlink", event_ignore },
	{ "StatusComplete", event_ignore }
};

static int process_message(struct ast_mansession *s, struct message *m)
{
	int x;
	char event[80];
	strncpy(event, get_header(m, "Event"), sizeof(event));
	if (!strlen(event)) {
		fprintf(stderr, "Missing event in request");
		return 0;
	}
	for (x=0;x<sizeof(events) / sizeof(events[0]);x++) {
		if (!strcasecmp(event, events[x].event)) {
			if (events[x].func(s, m))
				return -1;
			break;
		}
	}
	if (x >= sizeof(events) / sizeof(events[0]))
		fprintf(stderr, "Ignoring unknown event '%s'", event);
#if 0
	for (x=0;x<m->hdrcount;x++) {
		printf("Header: %s\n", m->headers[x]);
	}
#endif	
	return 0;
}

static void rebuild_channels(newtComponent c)
{
	void *prev = NULL;
	struct ast_chan *chan;
	char tmpn[42];
	char tmp[256];
	int x=0;
	prev = newtListboxGetCurrent(c);
	newtListboxClear(c);
	chan = chans;
	while(chan) {
		snprintf(tmpn, sizeof(tmpn), "%s (%s)", chan->name, chan->callerid);
		if (strlen(chan->exten)) 
			snprintf(tmp, sizeof(tmp), "%-30s %8s -> %s@%s:%s", 
				tmpn, chan->state,
				chan->exten, chan->context, chan->priority);
		else
			snprintf(tmp, sizeof(tmp), "%-30s %8s",
				tmpn, chan->state);
		newtListboxAppendEntry(c, tmp, chan);
		x++;
		chan = chan->next;
	}
	if (!x)
		newtListboxAppendEntry(c, " << No Active Channels >> ", NULL);
	newtListboxSetCurrentByKey(c, prev);
}

static int has_input(struct ast_mansession *s)
{
	int x;
	for (x=1;x<s->inlen;x++) 
		if ((s->inbuf[x] == '\n') && (s->inbuf[x-1] == '\r')) 
			return 1;
	return 0;
}

static int get_input(struct ast_mansession *s, char *output)
{
	/* output must have at least sizeof(s->inbuf) space */
	int res;
	int x;
	struct timeval tv = {0, 0};
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
		fprintf(stderr, "Dumping long line with no return from %s: %s\n", inet_ntoa(s->sin.sin_addr), s->inbuf);
		s->inlen = 0;
	}
	FD_ZERO(&fds);
	FD_SET(s->fd, &fds);
	res = select(s->fd + 1, &fds, NULL, NULL, &tv);
	if (res < 0) {
		fprintf(stderr, "Select returned error: %s\n", strerror(errno));
	} else if (res > 0) {
		res = read(s->fd, s->inbuf + s->inlen, sizeof(s->inbuf) - 1 - s->inlen);
		if (res < 1)
			return -1;
		s->inlen += res;
		s->inbuf[s->inlen] = '\0';
	} else {
		return 2;
	}
	return 0;
}

static int input_check(struct ast_mansession *s, struct message **mout)
{
	static struct message m;
	int res;

	if (mout)
		*mout = NULL;

	for(;;) {
		res = get_input(s, m.headers[m.hdrcount]);
		if (res == 1) {
#if 0
			fprintf(stderr, "Got header: %s", m.headers[m.hdrcount]);
			fgetc(stdin);
#endif
			/* Strip trailing \r\n */
			if (strlen(m.headers[m.hdrcount]) < 2)
				continue;
			m.headers[m.hdrcount][strlen(m.headers[m.hdrcount]) - 2] = '\0';
			if (!strlen(m.headers[m.hdrcount])) {
				if (mout && strlen(get_header(&m, "Response"))) {
					*mout = &m;
					return 0;
				}
				if (process_message(s, &m))
					break;
				memset(&m, 0, sizeof(&m));
			} else if (m.hdrcount < MAX_HEADERS - 1)
				m.hdrcount++;
		} else if (res < 0) {
			return -1;
		} else if (res == 2)
			return 0;
	}
	return -1;
}

static struct message *wait_for_response(int timeout)
{
	struct message *m;
	struct timeval tv;
	int res;
	fd_set fds;
	for (;;) {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout % 1000) * 1000;
		FD_SET(session.fd, &fds);
		res = select(session.fd + 1, &fds, NULL, NULL, &tv);
		if (res < 1)
			break;
		if (input_check(&session, &m) < 0) {
			return NULL;
		}
		if (m)
			return m;
	}
	return NULL;
}

static int manager_action(char *action, char *fmt, ...)
{
	struct ast_mansession *s;
	char tmp[4096];
	va_list ap;

	s = &session;
	fdprintf(s->fd, "Action: %s\r\n", action);
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	write(s->fd, tmp, strlen(tmp));
	fdprintf(s->fd, "\r\n");
	return 0;
}

static int show_message(char *title, char *msg)
{
	newtComponent form;
	newtComponent label;
	newtComponent ok;
	struct newtExitStruct es;

	newtCenteredWindow(60,7, title);

	label = newtLabel(4,1,msg);
	ok = newtButton(27, 3, "OK");
	form = newtForm(NULL, NULL, 0);
	newtFormAddComponents(form, label, ok, NULL);
	newtFormRun(form, &es);
	newtPopWindow();
	newtFormDestroy(form);
	return 0;
}

static newtComponent showform;
static int show_doing(char *title, char *tmp)
{
	struct newtExitStruct es;
	newtComponent label;
	showform = newtForm(NULL, NULL, 0);
	newtCenteredWindow(70,4, title);
	label = newtLabel(3,1,tmp);
	newtFormAddComponents(showform,label, NULL);
	newtFormSetTimer(showform, 200);
	newtFormRun(showform, &es);
	return 0;
}

static int hide_doing(void)
{
	newtPopWindow();
	newtFormDestroy(showform);
	return 0;
}

static void try_status(void)
{
	struct message *m;
	manager_action("Status", "");
	m = wait_for_response(10000);
	if (!m) {
		show_message("Status Failed", "Timeout waiting for response");
	} else if (strcasecmp(get_header(m, "Response"), "Success"))  {
		show_message("Status Failed Failed", get_header(m, "Message"));
	}
}
	

static void try_hangup(newtComponent c)
{
	struct ast_chan *chan;
	struct message *m;

	chan = newtListboxGetCurrent(c);
	if (chan) {
		manager_action("Hangup", "Channel: %s\r\n", chan->name);
		m = wait_for_response(10000);
		if (!m) {
			show_message("Hangup Failed", "Timeout waiting for response");
		} else if (strcasecmp(get_header(m, "Response"), "Success"))  {
			show_message("Hangup Failed", get_header(m, "Message"));
		}
	}
	
}

static int get_user_input(char *msg, char *buf, int buflen)
{
	newtComponent form;
	newtComponent ok;
	newtComponent cancel;
	newtComponent inpfield;
	const char *input;
	int res = -1;
	struct newtExitStruct es;

	newtCenteredWindow(60,7, msg);

	inpfield = newtEntry(5, 2, "", 50, &input, 0);
	ok = newtButton(22, 3, "OK");
	cancel = newtButton(32, 3, "Cancel");
	form = newtForm(NULL, NULL, 0);
	newtFormAddComponents(form, inpfield, ok, cancel, NULL);
	newtFormRun(form, &es);
	strncpy(buf, input, buflen - 1);
	if (es.u.co == ok) 
		res = 0;
	else
		res = -1;
	newtPopWindow();
	newtFormDestroy(form);
	return res;
}

static void try_redirect(newtComponent c)
{
	struct ast_chan *chan;
	char dest[256];
	struct message *m;
	char channame[256];
	char tmp[80];
	char *context;

	chan = newtListboxGetCurrent(c);
	if (chan) {
		strncpy(channame, chan->name, sizeof(channame) - 1);
		snprintf(tmp, sizeof(tmp), "Enter new extension for %s", channame);
		if (get_user_input(tmp, dest, sizeof(dest))) 
			return;
		if ((context = strchr(dest, '@'))) {
			*context = '\0';
			context++;
			manager_action("Redirect", "Channel: %s\r\nContext: %s\r\nExten: %s\r\nPriority: 1\r\n", chan->name,context,dest);
		} else {
			manager_action("Redirect", "Channel: %s\r\nExten: %s\r\nPriority: 1\r\n", chan->name, dest);
		}
		m = wait_for_response(10000);
		if (!m) {
			show_message("Hangup Failed", "Timeout waiting for response");
		} else if (strcasecmp(get_header(m, "Response"), "Success"))  {
			show_message("Hangup Failed", get_header(m, "Message"));
		}
	}
	
}

static int manage_calls(char *host)
{
	newtComponent form;
	newtComponent quit;
	newtComponent hangup;
	newtComponent redirect;
	newtComponent channels;
	struct newtExitStruct es;
	char tmp[80];

	/* If there's one thing you learn from this code, it is this...
	   Never, ever fly Air France.  Their customer service is absolutely
	   the worst.  I've never heard the words "That's not my problem" as 
	   many times as I have from their staff -- It should, without doubt
	   be their corporate motto if it isn't already.  Don't bother giving 
	   them business because you're just a pain in their side and they
	   will be sure to let you know the first time you speak to them.
	   
	   If you ever want to make me happy just tell me that you, too, will
	   never fly Air France again either (in spite of their excellent
	   cuisine). */
	snprintf(tmp, sizeof(tmp), "Asterisk Manager at %s", host);
	newtCenteredWindow(74, 20, tmp);
	form = newtForm(NULL, NULL, 0);
	newtFormWatchFd(form, session.fd, NEWT_FD_READ);
	newtFormSetTimer(form, 100);
	quit = newtButton(62, 16, "Quit");
	redirect = newtButton(35, 16, "Redirect");
	hangup = newtButton(50, 16, "Hangup");
	channels = newtListbox(1,1,14, NEWT_FLAG_SCROLL);
	newtFormAddComponents(form, channels, redirect, hangup, quit, NULL);
	newtListboxSetWidth(channels, 72);
	
	show_doing("Getting Status", "Retrieving system status...");
	try_status();
	hide_doing();

	for(;;) {
		newtFormRun(form, &es);
		if (has_input(&session) || (es.reason == NEWT_EXIT_FDREADY)) {
			if (input_check(&session, NULL)) {
				show_message("Disconnected", "Disconnected from remote host");
				break;
			}
		} else if (es.reason == NEWT_EXIT_COMPONENT) {
			if (es.u.co == quit)
				break;
			if (es.u.co == hangup) {
				try_hangup(channels);
			} else if (es.u.co == redirect) {
				try_redirect(channels);
			}
		}
		rebuild_channels(channels);
	}
	newtFormDestroy(form);
	return 0;
}

static int login(char *hostname)
{
	newtComponent form;
	newtComponent cancel;
	newtComponent login;
	newtComponent username;
	newtComponent password;
	newtComponent label;
	newtComponent ulabel;
	newtComponent plabel;
	const char *user;
	const char *pass;
	struct message *m;
	struct newtExitStruct es;
	char tmp[55];
	struct hostent *hp;
	int res = -1;
	
	session.fd = socket(AF_INET, SOCK_STREAM, 0);
	if (session.fd < 0) {
		snprintf(tmp, sizeof(tmp), "socket() failed: %s\n", strerror(errno));
		show_message("Socket failed", tmp);
		return -1;
	}
	
	snprintf(tmp, sizeof(tmp), "Looking up %s\n", hostname);
	show_doing("Connecting....", tmp);
	
	
	hp = gethostbyname(hostname);
	if (!hp) {
		snprintf(tmp, sizeof(tmp), "No such address: %s\n", hostname);
		show_message("Host lookup failed", tmp);
		return -1;
	}
	hide_doing();
	snprintf(tmp, sizeof(tmp), "Connecting to %s", hostname);
	show_doing("Connecting...", tmp);

	session.sin.sin_family = AF_INET;
	session.sin.sin_port = htons(DEFAULT_MANAGER_PORT);
	memcpy(&session.sin.sin_addr, hp->h_addr, sizeof(session.sin.sin_addr));

	if (connect(session.fd,(struct sockaddr*)&session.sin, sizeof(session.sin))) {
		snprintf(tmp, sizeof(tmp), "%s failed: %s\n", hostname, strerror(errno));
		show_message("Connect Failed", tmp);
		return -1;
	}
	
	hide_doing();
	
	login = newtButton(5, 6, "Login");
	cancel = newtButton(25, 6, "Cancel");
	newtCenteredWindow(40, 10, "Asterisk Manager Login");
	snprintf(tmp, sizeof(tmp), "Host:     %s", hostname);
	label = newtLabel(4,1, tmp);
	
	ulabel = newtLabel(4,2,"Username:");
	plabel = newtLabel(4,3,"Password:");
	
	username = newtEntry(14, 2, "", 20, &user, 0);
	password = newtEntry(14, 3, "", 20, &pass, NEWT_FLAG_HIDDEN);
	
	form = newtForm(NULL, NULL, 0);
	newtFormAddComponents(form, username, password, login, cancel, label, ulabel, plabel,NULL);
	newtFormRun(form, &es);
	if (es.reason == NEWT_EXIT_COMPONENT) {
		if (es.u.co == login) {
			snprintf(tmp, sizeof(tmp), "Logging in '%s'...", user);
			show_doing("Logging in", tmp);
			/* Check to see if the remote host supports MD5 Authentication */
			manager_action("Challenge", "AuthType: MD5\r\n");
			m = wait_for_response(10000);
			if (m && !strcasecmp(get_header(m, "Response"), "Success")) {
				char *challenge = get_header(m, "Challenge");
				int x;
				int len = 0;
				char md5key[256] = "";
				struct MD5Context md5;
				unsigned char digest[16];
				MD5Init(&md5);
				MD5Update(&md5, challenge, strlen(challenge));
				MD5Update(&md5, pass, strlen(pass));
				MD5Final(digest, &md5);
				for (x=0; x<16; x++)
					len += sprintf(md5key + len, "%2.2x", digest[x]);
				manager_action("Login",
						"AuthType: MD5\r\n"
						"Username: %s\r\n"
						"Key: %s\r\n",
						user, md5key);
				m = wait_for_response(10000);
				hide_doing();
				if (!strcasecmp(get_header(m, "Response"), "Success")) {
					res = 0;
				} else {
					show_message("Login Failed", get_header(m, "Message"));
				}
			} else {
				memset(m, 0, sizeof(m));
				manager_action("Login", 
					"Username: %s\r\n"
					"Secret: %s\r\n",
						user, pass);
				m = wait_for_response(10000);
				hide_doing();
				if (m) {
					if (!strcasecmp(get_header(m, "Response"), "Success")) {
						res = 0;
					} else {
						show_message("Login Failed", get_header(m, "Message"));
					}
				}
			}
		}
	}
	newtFormDestroy(form);
	return res;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		fprintf(stderr, "Usage: astman <host>\n");
		exit(1);
	}
	newtInit();
	newtCls();
	newtDrawRootText(0, 0, "Asterisk Manager (C)2002, Linux Support Services, Inc.");
	newtPushHelpLine("Welcome to the Asterisk Manager!");
	if (login(argv[1])) {
		newtFinished();
		exit(1);
	}
	manage_calls(argv[1]);
	newtFinished();
	return 0;
}
