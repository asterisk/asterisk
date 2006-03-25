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

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <asterisk/cli.h>
#include <asterisk/http.h>
#include <asterisk/utils.h>
#include <asterisk/strings.h>

#define MAX_PREFIX 80
#define DEFAULT_PREFIX "asterisk"

/* This program implements a tiny http server supporting the "get" method
   only and was inspired by micro-httpd by Jef Poskanzer */

struct ast_http_server_instance {
	FILE *f;
	int fd;
	struct sockaddr_in requestor;
	ast_http_callback callback;
};

static struct ast_http_uri *uris;

static int httpfd = -1;
static pthread_t master = AST_PTHREADT_NULL;
static char prefix[MAX_PREFIX];
static int prefix_len = 0;
static struct sockaddr_in oldsin;


static char *httpstatus_callback(struct sockaddr_in *req, const char *uri, struct ast_variable *vars, int *status, char **title, int *contentlength)
{
	char result[4096];
	int reslen = sizeof(result);
	char *c=result;
	struct ast_variable *v;
	char iabuf[INET_ADDRSTRLEN];

	ast_build_string(&c, &reslen,
		"\r\n"
		"<title>Asterisk HTTP Status</title>\r\n"
		"<body bgcolor=\"#ffffff\">\r\n"
		"<table bgcolor=\"#f1f1f1\" align=\"center\"><tr><td bgcolor=\"#e0e0ff\" colspan=\"2\" width=\"500\">\r\n"
		"<h2>&nbsp;&nbsp;Asterisk&trade; HTTP Status</h2></td></tr>\r\n");

	ast_build_string(&c, &reslen, "<tr><td><i>Prefix</i></td><td><b>%s</b></td></tr>\r\n", prefix);
	ast_build_string(&c, &reslen, "<tr><td><i>Bind Address</i></td><td><b>%s</b></td></tr>\r\n",
			ast_inet_ntoa(iabuf, sizeof(iabuf), oldsin.sin_addr));
	ast_build_string(&c, &reslen, "<tr><td><i>Bind Port</i></td><td><b>%d</b></td></tr>\r\n",
			ntohs(oldsin.sin_port));
	ast_build_string(&c, &reslen, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");
	v = vars;
	while(v) {
		ast_build_string(&c, &reslen, "<tr><td><i>Submitted Variable '%s'</i></td><td>%s</td></tr>\r\n", v->name, v->value);
		v = v->next;
	}
	ast_build_string(&c, &reslen, "</table><center><font size=\"-1\"><i>Asterisk and Digium are registered trademarks of Digium, Inc.</i></font></center></body>\r\n");
	return strdup(result);
}

static struct ast_http_uri statusuri = {
	.callback = httpstatus_callback,
	.description = "Asterisk HTTP General Status",
	.uri = "httpstatus",
	.has_subtree = 0,
};
	
char *ast_http_error(int status, const char *title, const char *extra_header, const char *text)
{
	char *c = NULL;
	asprintf(&c,
		"Content-type: text/html\r\n"
		"%s"
		"\r\n"
		"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<html><head>\r\n"
		"<title>%d %s</title>\r\n"
		"</head><body>\r\n"
		"<h1>%s</h1>\r\n"
		"<p>%s</p>\r\n"
		"<hr />\r\n"
		"<address>Asterisk Server</address>\r\n"
		"</body></html>\r\n",
			(extra_header ? extra_header : ""), status, title, title, text);
	return c;
}

int ast_http_uri_link(struct ast_http_uri *urih)
{
	struct ast_http_uri *prev=uris;
	if (!uris || strlen(uris->uri) <= strlen(urih->uri)) {
		urih->next = uris;
		uris = urih;
	} else {
		while (prev->next && (strlen(prev->next->uri) > strlen(urih->uri)))
			prev = prev->next;
		/* Insert it here */
		urih->next = prev->next;
		prev->next = urih;
	}
	return 0;
}	

void ast_http_uri_unlink(struct ast_http_uri *urih)
{
	struct ast_http_uri *prev = uris;
	if (!uris)
		return;
	if (uris == urih) {
		uris = uris->next;
	}
	while(prev->next) {
		if (prev->next == urih) {
			prev->next = urih->next;
			break;
		}
		prev = prev->next;
	}
}

static char *handle_uri(struct sockaddr_in *sin, char *uri, int *status, char **title, int *contentlength)
{
	char *c;
	char *turi;
	char *params;
	char *var;
	char *val;
	struct ast_http_uri *urih=NULL;
	int len;
	struct ast_variable *vars=NULL, *v, *prev = NULL;
	
	
	if (*uri == '/')
		uri++;
	params = strchr(uri, '?');
	if (params) {
		*params = '\0';
		params++;
		while ((var = strsep(&params, "&"))) {
			val = strchr(var, '=');
			if (val) {
				*val = '\0';
				val++;
			} else 
				val = "";
			ast_uri_decode(val);
			ast_uri_decode(var);
			if ((v = ast_variable_new(var, val))) {
				if (vars)
					prev->next = v;
				else
					vars = v;
				prev = v;
			}
		}
	}
	ast_uri_decode(uri);
	if (!strncasecmp(uri, prefix, prefix_len)) {
		uri += prefix_len;
		if (!*uri || (*uri == '/')) {
			if (*uri == '/')
				uri++;
			urih = uris;
			while(urih) {
				len = strlen(urih->uri);
				if (!strncasecmp(urih->uri, uri, len)) {
					if (!uri[len] || uri[len] == '/') {
						turi = uri + len;
						if (*turi == '/')
							turi++;
						if (!*turi || urih->has_subtree) {
							uri = turi;
							break;
						}
					}
				}
				urih = urih->next;
			}
		}
	}
	if (urih) {
		c = urih->callback(sin, uri, vars, status, title, contentlength);
		ast_variables_destroy(vars);
	} else {
		c = ast_http_error(404, "Not Found", NULL, "The requested URL was not found on this serer.");
		*status = 404;
		*title = strdup("Not Found");
	}
	return c;
}

static void *ast_httpd_helper_thread(void *data)
{
	char buf[4096];
	char timebuf[256];
	struct ast_http_server_instance *ser = data;
	char *uri, *c, *title=NULL;
	int status = 200, contentlength = 0;
	time_t t;

	if (fgets(buf, sizeof(buf), ser->f)) {
		/* Skip method */
		uri = buf;
		while(*uri && (*uri > 32)) uri++;
		if (*uri) {
			*uri = '\0';
			uri++;
		}

		/* Skip white space */
		while (*uri && (*uri < 33)) uri++;

		if (*uri) {
			c = uri;
			while (*c && (*c > 32)) c++;
			if (*c) {
				*c = '\0';
			}
		}
		if (*uri) {
			if (!strcasecmp(buf, "get")) 
				c = handle_uri(&ser->requestor, uri, &status, &title, &contentlength);
			else 
				c = ast_http_error(501, "Not Implemented", NULL, "Attempt to use unimplemented / unsupported method");\
		} else 
			c = ast_http_error(400, "Bad Request", NULL, "Invalid Request");
		if (!c)
			c = ast_http_error(500, "Internal Error", NULL, "Internal Server Error");
		if (c) {
			time(&t);
			strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&t));
			ast_cli(ser->fd, "HTTP/1.1 GET %d %s\r\n", status, title ? title : "OK");
			ast_cli(ser->fd, "Server: Asterisk\r\n");
			ast_cli(ser->fd, "Date: %s\r\n", timebuf);
			if (contentlength)
				ast_cli(ser->fd, "Content-length: %d\r\n", contentlength);
			ast_cli(ser->fd, "Connection: close\r\n");
			ast_cli(ser->fd, "%s", c);
			free(c);
		}
		if (title)
			free(title);
	}
	fclose(ser->f);
	free(ser);
	return NULL;
}

static void *http_root(void *data)
{
	int fd;
	struct sockaddr_in sin;
	int sinlen;
	struct ast_http_server_instance *ser;
	pthread_t launched;
	for (;;) {
		ast_wait_for_input(httpfd, -1);
		sinlen = sizeof(sin);
		fd = accept(httpfd, (struct sockaddr *)&sin, &sinlen);
		if (fd < 0) {
			if ((errno != EAGAIN) && (errno != EINTR))
				ast_log(LOG_WARNING, "Accept failed: %s\n", strerror(errno));
			continue;
		}
		ser = calloc(1, sizeof(*ser));
		if (ser) {
			ser->fd = fd;
			if ((ser->f = fdopen(ser->fd, "w+"))) {
				if (ast_pthread_create(&launched, NULL, ast_httpd_helper_thread, ser)) {
					ast_log(LOG_WARNING, "Unable to launch helper thread: %s\n", strerror(errno));
					fclose(ser->f);
					free(ser);
				}
			} else {
				ast_log(LOG_WARNING, "fdopen failed!\n");
				close(ser->fd);
				free(ser);
			}
		} else {
			ast_log(LOG_WARNING, "Out of memory!\n");
			close(fd);
		}
	}
	return NULL;
}

static void http_server_start(struct sockaddr_in *sin)
{
	char iabuf[INET_ADDRSTRLEN];
	int flags;
	int x = 1;
	
	/* Do nothing if nothing has changed */
	if (!memcmp(&oldsin, sin, sizeof(oldsin))) {
		ast_log(LOG_DEBUG, "Nothing changed in http\n");
		return;
	}
	
	memcpy(&oldsin, sin, sizeof(oldsin));
	
	/* Shutdown a running server if there is one */
	if (master != AST_PTHREADT_NULL) {
		pthread_cancel(master);
		pthread_kill(master, SIGURG);
		pthread_join(master, NULL);
	}
	
	if (httpfd != -1)
		close(httpfd);

	/* If there's no new server, stop here */
	if (!sin->sin_family)
		return;
	
	
	httpfd = socket(AF_INET, SOCK_STREAM, 0);
	if (httpfd < 0) {
		ast_log(LOG_WARNING, "Unable to allocate socket: %s\n", strerror(errno));
		return;
	}
	
	setsockopt(httpfd, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
	if (bind(httpfd, (struct sockaddr *)sin, sizeof(*sin))) {
		ast_log(LOG_NOTICE, "Unable to bind http server to %s:%d: %s\n",
			ast_inet_ntoa(iabuf, sizeof(iabuf), sin->sin_addr), ntohs(sin->sin_port),
			strerror(errno));
		close(httpfd);
		httpfd = -1;
		return;
	}
	if (listen(httpfd, 10)) {
		ast_log(LOG_NOTICE, "Unable to listen!\n");
		close(httpfd);
		httpfd = -1;
		return;
	}
	flags = fcntl(httpfd, F_GETFL);
	fcntl(httpfd, F_SETFL, flags | O_NONBLOCK);
	if (ast_pthread_create(&master, NULL, http_root, NULL)) {
		ast_log(LOG_NOTICE, "Unable to launch http server on %s:%d: %s\n",
				ast_inet_ntoa(iabuf, sizeof(iabuf), sin->sin_addr), ntohs(sin->sin_port),
				strerror(errno));
		close(httpfd);
		httpfd = -1;
	}
}

static int __ast_http_load(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	int enabled=0;
	struct sockaddr_in sin;
	struct hostent *hp;
	struct ast_hostent ahp;
	char newprefix[MAX_PREFIX];
	memset(&sin, 0, sizeof(sin));
	sin.sin_port = 8088;
	strcpy(newprefix, DEFAULT_PREFIX);
	cfg = ast_config_load("http.conf");
	if (cfg) {
		v = ast_variable_browse(cfg, "general");
		while(v) {
			if (!strcasecmp(v->name, "enabled"))
				enabled = ast_true(v->value);
			else if (!strcasecmp(v->name, "bindport"))
				sin.sin_port = ntohs(atoi(v->value));
			else if (!strcasecmp(v->name, "bindaddr")) {
				if ((hp = ast_gethostbyname(v->value, &ahp))) {
					memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
				} else {
					ast_log(LOG_WARNING, "Invalid bind address '%s'\n", v->value);
				}
			} else if (!strcasecmp(v->name, "prefix"))
				ast_copy_string(newprefix, v->value, sizeof(newprefix));
			v = v->next;
		}
		ast_config_destroy(cfg);
	}
	if (enabled)
		sin.sin_family = AF_INET;
	if (strcmp(prefix, newprefix)) {
		ast_copy_string(prefix, newprefix, sizeof(prefix));
		prefix_len = strlen(prefix);
	}
	http_server_start(&sin);
	return 0;
}

static int handle_show_http(int fd, int argc, char *argv[])
{
	char iabuf[INET_ADDRSTRLEN];
	struct ast_http_uri *urih;
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, "HTTP Server Status:\n");
	ast_cli(fd, "Prefix: %s\n", prefix);
	if (oldsin.sin_family)
		ast_cli(fd, "Server Enabled and Bound to %s:%d\n\n",
			ast_inet_ntoa(iabuf, sizeof(iabuf), oldsin.sin_addr),
			ntohs(oldsin.sin_port));
	else
		ast_cli(fd, "Server Disabled\n\n");
	ast_cli(fd, "Enabled URI's:\n");
	urih = uris;
	while(urih){
		ast_cli(fd, "/%s/%s%s => %s\n", prefix, urih->uri, (urih->has_subtree ? "/..." : "" ), urih->description);
		urih = urih->next;
	}
	if (!uris)
		ast_cli(fd, "None.\n");
	return RESULT_SUCCESS;
}

int ast_http_reload(void)
{
	return __ast_http_load(1);
}

static char show_http_help[] =
"Usage: show http\n"
"       Shows status of internal HTTP engine\n";

static struct ast_cli_entry http_cli[] = {
	{ { "show", "http", NULL }, handle_show_http,
	  "Display HTTP status", show_http_help },
};

int ast_http_init(void)
{
	ast_http_uri_link(&statusuri);
	ast_cli_register_multiple(http_cli, sizeof(http_cli) / sizeof(http_cli[0]));
	return __ast_http_load(0);
}
