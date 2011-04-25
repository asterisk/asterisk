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

/*!
 * \file 
 * \brief http server for AMI access
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * This program implements a tiny http server
 * and was inspired by micro-httpd by Jef Poskanzer 
 * 
 * \ref AstHTTP - AMI over the http protocol
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>

#include "asterisk/cli.h"
#include "asterisk/http.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/version.h"
#include "asterisk/manager.h"

#define MAX_PREFIX 80
#define DEFAULT_PREFIX "/asterisk"
#define DEFAULT_SESSION_LIMIT 100

struct ast_http_server_instance {
	FILE *f;
	int fd;
	struct sockaddr_in requestor;
	ast_http_callback callback;
};

AST_RWLOCK_DEFINE_STATIC(uris_lock);
static struct ast_http_uri *uris;

static int httpfd = -1;
static pthread_t master = AST_PTHREADT_NULL;
static char prefix[MAX_PREFIX];
static int prefix_len;
static struct sockaddr_in oldsin;
static int enablestatic;
static int session_limit = DEFAULT_SESSION_LIMIT;
static int session_count = 0;

/*! \brief Limit the kinds of files we're willing to serve up */
static struct {
	const char *ext;
	const char *mtype;
} mimetypes[] = {
	{ "png", "image/png" },
	{ "jpg", "image/jpeg" },
	{ "js", "application/x-javascript" },
	{ "wav", "audio/x-wav" },
	{ "mp3", "audio/mpeg" },
	{ "svg", "image/svg+xml" },
	{ "svgz", "image/svg+xml" },
	{ "gif", "image/gif" },
};

static const char *ftype2mtype(const char *ftype, char *wkspace, int wkspacelen)
{
	int x;
	if (ftype) {
		for (x=0;x<sizeof(mimetypes) / sizeof(mimetypes[0]); x++) {
			if (!strcasecmp(ftype, mimetypes[x].ext))
				return mimetypes[x].mtype;
		}
	}
	snprintf(wkspace, wkspacelen, "text/%s", ftype ? ftype : "plain");
	return wkspace;
}

static char *static_callback(struct sockaddr_in *req, const char *uri, struct ast_variable *vars, int *status, char **title, int *contentlength)
{
	char result[4096];
	char *c=result;
	char *path;
	char *ftype;
	const char *mtype;
	char wkspace[80];
	struct stat st;
	int len;
	int fd;
	void *blob;

	/* Yuck.  I'm not really sold on this, but if you don't deliver static content it makes your configuration 
	   substantially more challenging, but this seems like a rather irritating feature creep on Asterisk. */
	if (!enablestatic || ast_strlen_zero(uri))
		goto out403;
	/* Disallow any funny filenames at all */
	if ((uri[0] < 33) || strchr("./|~@#$%^&*() \t", uri[0]))
		goto out403;
	if (strstr(uri, "/.."))
		goto out403;
		
	if ((ftype = strrchr(uri, '.')))
		ftype++;
	mtype = ftype2mtype(ftype, wkspace, sizeof(wkspace));
	
	/* Cap maximum length */
	len = strlen(uri) + strlen(ast_config_AST_DATA_DIR) + strlen("/static-http/") + 5;
	if (len > 1024)
		goto out403;
		
	path = alloca(len);
	sprintf(path, "%s/static-http/%s", ast_config_AST_DATA_DIR, uri);
	if (stat(path, &st))
		goto out404;
	if (S_ISDIR(st.st_mode))
		goto out404;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		goto out403;
	
	len = st.st_size + strlen(mtype) + 40;
	
	blob = malloc(len);
	if (blob) {
		c = blob;
		sprintf(c, "Content-type: %s\r\n\r\n", mtype);
		c += strlen(c);
		*contentlength = read(fd, c, st.st_size);
		if (*contentlength < 0) {
			close(fd);
			free(blob);
			goto out403;
		}
	}
	close(fd);
	return blob;

out404:
	*status = 404;
	*title = strdup("Not Found");
	return ast_http_error(404, "Not Found", NULL, "The requested URL was not found on this server.");

out403:
	*status = 403;
	*title = strdup("Access Denied");
	return ast_http_error(403, "Access Denied", NULL, "You do not have permission to access the requested URL.");
}


static char *httpstatus_callback(struct sockaddr_in *req, const char *uri, struct ast_variable *vars, int *status, char **title, int *contentlength)
{
	char result[4096];
	size_t reslen = sizeof(result);
	char *c=result;
	struct ast_variable *v;

	ast_build_string(&c, &reslen,
		"\r\n"
		"<title>Asterisk HTTP Status</title>\r\n"
		"<body bgcolor=\"#ffffff\">\r\n"
		"<table bgcolor=\"#f1f1f1\" align=\"center\"><tr><td bgcolor=\"#e0e0ff\" colspan=\"2\" width=\"500\">\r\n"
		"<h2>&nbsp;&nbsp;Asterisk&trade; HTTP Status</h2></td></tr>\r\n");

	ast_build_string(&c, &reslen, "<tr><td><i>Prefix</i></td><td><b>%s</b></td></tr>\r\n", prefix);
	ast_build_string(&c, &reslen, "<tr><td><i>Bind Address</i></td><td><b>%s</b></td></tr>\r\n",
			ast_inet_ntoa(oldsin.sin_addr));
	ast_build_string(&c, &reslen, "<tr><td><i>Bind Port</i></td><td><b>%d</b></td></tr>\r\n",
			ntohs(oldsin.sin_port));
	ast_build_string(&c, &reslen, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");
	v = vars;
	while(v) {
		if (strncasecmp(v->name, "cookie_", 7))
			ast_build_string(&c, &reslen, "<tr><td><i>Submitted Variable '%s'</i></td><td>%s</td></tr>\r\n", v->name, v->value);
		v = v->next;
	}
	ast_build_string(&c, &reslen, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");
	v = vars;
	while(v) {
		if (!strncasecmp(v->name, "cookie_", 7))
			ast_build_string(&c, &reslen, "<tr><td><i>Cookie '%s'</i></td><td>%s</td></tr>\r\n", v->name, v->value);
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
	
static struct ast_http_uri staticuri = {
	.callback = static_callback,
	.description = "Asterisk HTTP Static Delivery",
	.uri = "static",
	.has_subtree = 1,
	.static_content = 1,
};
	
char *ast_http_error(int status, const char *title, const char *extra_header, const char *text)
{
	char *c = NULL;
	if (asprintf(&c,
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
		     (extra_header ? extra_header : ""), status, title, title, text) < 0) {
		ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
		c = NULL;
	}
	return c;
}

int ast_http_uri_link(struct ast_http_uri *urih)
{
	struct ast_http_uri *prev;

	ast_rwlock_wrlock(&uris_lock);
	prev = uris;
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
	ast_rwlock_unlock(&uris_lock);

	return 0;
}	

void ast_http_uri_unlink(struct ast_http_uri *urih)
{
	struct ast_http_uri *prev;

	ast_rwlock_wrlock(&uris_lock);
	if (!uris) {
		ast_rwlock_unlock(&uris_lock);
		return;
	}
	prev = uris;
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
	ast_rwlock_unlock(&uris_lock);
}

static char *handle_uri(struct sockaddr_in *sin, char *uri, int *status, 
	char **title, int *contentlength, struct ast_variable **cookies, 
	unsigned int *static_content)
{
	char *c;
	char *turi;
	char *params;
	char *var;
	char *val;
	struct ast_http_uri *urih=NULL;
	int len;
	struct ast_variable *vars=NULL, *v, *prev = NULL;
	
	
	params = strchr(uri, '?');
	if (params) {
		*params = '\0';
		params++;
		while ((var = strsep(&params, "&"))) {
			val = strchr(var, '=');
			if (val) {
				*val = '\0';
				val++;
				ast_uri_decode(val);
			} else 
				val = "";
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
	if (prev)
		prev->next = *cookies;
	else
		vars = *cookies;
	*cookies = NULL;
	ast_uri_decode(uri);
	if (!strncasecmp(uri, prefix, prefix_len)) {
		uri += prefix_len;
		if (!*uri || (*uri == '/')) {
			if (*uri == '/')
				uri++;
			ast_rwlock_rdlock(&uris_lock);
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
			if (!urih)
				ast_rwlock_unlock(&uris_lock);
		}
	}
	if (urih) {
		if (urih->static_content)
			*static_content = 1;
		c = urih->callback(sin, uri, vars, status, title, contentlength);
		ast_rwlock_unlock(&uris_lock);
	} else if (ast_strlen_zero(uri) && ast_strlen_zero(prefix)) {
		/* Special case: If no prefix, and no URI, send to /static/index.html */
		c = ast_http_error(302, "Moved Temporarily", "Location: /static/index.html\r\n", "Redirecting to /static/index.html.");
		*status = 302;
		*title = strdup("Moved Temporarily");
	} else {
		c = ast_http_error(404, "Not Found", NULL, "The requested URL was not found on this server.");
		*status = 404;
		*title = strdup("Not Found");
	}
	ast_variables_destroy(vars);
	return c;
}

static struct ast_variable *parse_cookies(char *cookies)
{
	char *cur;
	struct ast_variable *vars = NULL, *var;

	/* Skip Cookie: */
	cookies += 8;

	while ((cur = strsep(&cookies, ";"))) {
		char *name, *val;
		
		name = val = cur;
		strsep(&val, "=");

		if (ast_strlen_zero(name) || ast_strlen_zero(val)) {
			continue;
		}

		name = ast_strip(name);
		val = ast_strip_quoted(val, "\"", "\"");

		if (ast_strlen_zero(name) || ast_strlen_zero(val)) {
			continue;
		}

		if (option_debug) {
			ast_log(LOG_DEBUG, "mmm ... cookie!  Name: '%s'  Value: '%s'\n", name, val);
		}

		var = ast_variable_new(name, val);
		var->next = vars;
		vars = var;
	}

	return vars;
}

static void *ast_httpd_helper_thread(void *data)
{
	char buf[4096];
	char cookie[4096];
	char timebuf[256];
	struct ast_http_server_instance *ser = data;
	struct ast_variable *vars = NULL;
	char *uri, *c, *title=NULL;
	int status = 200, contentlength = 0;
	time_t t;
	unsigned int static_content = 0;

	if (fgets(buf, sizeof(buf), ser->f)) {
		/* Skip method */
		uri = buf;
		while(*uri && (*uri > 32))
			uri++;
		if (*uri) {
			*uri = '\0';
			uri++;
		}

		/* Skip white space */
		while (*uri && (*uri < 33))
			uri++;

		if (*uri) {
			c = uri;
			while (*c && (*c > 32))
				 c++;
			if (*c) {
				*c = '\0';
			}
		}

		while (fgets(cookie, sizeof(cookie), ser->f)) {
			/* Trim trailing characters */
			while(!ast_strlen_zero(cookie) && (cookie[strlen(cookie) - 1] < 33)) {
				cookie[strlen(cookie) - 1] = '\0';
			}
			if (ast_strlen_zero(cookie))
				break;
			if (!strncasecmp(cookie, "Cookie: ", 8)) {
				vars = parse_cookies(cookie);
			}
		}

		if (*uri) {
			if (!strcasecmp(buf, "get")) 
				c = handle_uri(&ser->requestor, uri, &status, &title, &contentlength, &vars, &static_content);
			else 
				c = ast_http_error(501, "Not Implemented", NULL, "Attempt to use unimplemented / unsupported method");\
		} else 
			c = ast_http_error(400, "Bad Request", NULL, "Invalid Request");

		/* If they aren't mopped up already, clean up the cookies */
		if (vars)
			ast_variables_destroy(vars);

		if (!c)
			c = ast_http_error(500, "Internal Error", NULL, "Internal Server Error");
		if (c) {
			time(&t);
			strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&t));
			ast_cli(ser->fd, "HTTP/1.1 %d %s\r\n", status, title ? title : "OK");
			ast_cli(ser->fd, "Server: Asterisk/%s\r\n", ASTERISK_VERSION);
			ast_cli(ser->fd, "Date: %s\r\n", timebuf);
			ast_cli(ser->fd, "Connection: close\r\n");
			if (!static_content)
				ast_cli(ser->fd, "Cache-Control: no-cache, no-store\r\n");
				/* We set the no-cache headers only for dynamic content.
				* If you want to make sure the static file you requested is not from cache,
				* append a random variable to your GET request.  Ex: 'something.html?r=109987734'
				*/

			if (contentlength) {
				char *tmp;
				tmp = strstr(c, "\r\n\r\n");
				if (tmp) {
					ast_cli(ser->fd, "Content-length: %d\r\n", contentlength);
					if (write(ser->fd, c, (tmp + 4 - c)) < 0) {
						ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
					}
					if (write(ser->fd, tmp + 4, contentlength) < 0) {
						ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
					}
				}
			} else
				ast_cli(ser->fd, "%s", c);
			free(c);
		}
		if (title)
			free(title);
	}
	fclose(ser->f);
	free(ser);
	ast_atomic_fetchadd_int(&session_count, -1);
	return NULL;
}

static void *http_root(void *data)
{
	int fd;
	struct sockaddr_in sin;
	socklen_t sinlen;
	struct ast_http_server_instance *ser;
	pthread_t launched;
	pthread_attr_t attr;
	
	for (;;) {
		int flags;

		ast_wait_for_input(httpfd, -1);
		sinlen = sizeof(sin);
		fd = accept(httpfd, (struct sockaddr *)&sin, &sinlen);

		if (fd < 0) {
			if ((errno != EAGAIN) && (errno != EINTR))
				ast_log(LOG_WARNING, "Accept failed: %s\n", strerror(errno));
			continue;
		}

		if (ast_atomic_fetchadd_int(&session_count, +1) >= session_limit) {
			close(fd);
			continue;
		}

		ser = ast_calloc(1, sizeof(*ser));
		if (!ser) {
			ast_log(LOG_WARNING, "No memory for new session: %s\n", strerror(errno));
			close(fd);
			ast_atomic_fetchadd_int(&session_count, -1);
			continue;
		}
		flags = fcntl(fd, F_GETFL);
		fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
		ser->fd = fd;
		memcpy(&ser->requestor, &sin, sizeof(ser->requestor));
		if ((ser->f = fdopen(ser->fd, "w+"))) {
			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			
			if (ast_pthread_create_background(&launched, &attr, ast_httpd_helper_thread, ser)) {
				ast_log(LOG_WARNING, "Unable to launch helper thread: %s\n", strerror(errno));
				fclose(ser->f);
				free(ser);
				ast_atomic_fetchadd_int(&session_count, -1);
			}
			pthread_attr_destroy(&attr);
		} else {
			ast_log(LOG_WARNING, "fdopen failed!\n");
			close(ser->fd);
			free(ser);
			ast_atomic_fetchadd_int(&session_count, -1);
		}
	}
	return NULL;
}

char *ast_http_setcookie(const char *var, const char *val, int expires, char *buf, size_t buflen)
{
	char *c;
	c = buf;
	ast_build_string(&c, &buflen, "Set-Cookie: %s=\"%s\"; Version=1", var, val);
	if (expires)
		ast_build_string(&c, &buflen, "; Max-Age=%d", expires);
	ast_build_string(&c, &buflen, "\r\n");
	return buf;
}


static void http_server_start(struct sockaddr_in *sin)
{
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
			ast_inet_ntoa(sin->sin_addr), ntohs(sin->sin_port),
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
	if (ast_pthread_create_background(&master, NULL, http_root, NULL)) {
		ast_log(LOG_NOTICE, "Unable to launch http server on %s:%d: %s\n",
				ast_inet_ntoa(sin->sin_addr), ntohs(sin->sin_port),
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
	int newenablestatic=0;
	struct sockaddr_in sin;
	struct hostent *hp;
	struct ast_hostent ahp;
	char newprefix[MAX_PREFIX];

	memset(&sin, 0, sizeof(sin));
	sin.sin_port = htons(8088);

	strcpy(newprefix, DEFAULT_PREFIX);

	cfg = ast_config_load("http.conf");
	if (cfg) {
		v = ast_variable_browse(cfg, "general");
		while(v) {
			if (!strcasecmp(v->name, "enabled"))
				enabled = ast_true(v->value);
			else if (!strcasecmp(v->name, "enablestatic"))
				newenablestatic = ast_true(v->value);
			else if (!strcasecmp(v->name, "bindport"))
				sin.sin_port = ntohs(atoi(v->value));
			else if (!strcasecmp(v->name, "bindaddr")) {
				if ((hp = ast_gethostbyname(v->value, &ahp))) {
					memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
				} else {
					ast_log(LOG_WARNING, "Invalid bind address '%s'\n", v->value);
				}
			} else if (!strcasecmp(v->name, "prefix")) {
				if (!ast_strlen_zero(v->value)) {
					newprefix[0] = '/';
					ast_copy_string(newprefix + 1, v->value, sizeof(newprefix) - 1);
				} else {
					newprefix[0] = '\0';
				}
			} else if (!strcasecmp(v->name, "sessionlimit")) {
				int limit = atoi(v->value);

				if (limit < 1) {
					ast_log(LOG_WARNING, "Invalid sessionlimit value '%s', using default value\n", v->value);
					session_limit = DEFAULT_SESSION_LIMIT;
				} else {
					session_limit = limit;
				}
			}

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
	enablestatic = newenablestatic;

	http_server_start(&sin);


	return 0;
}

static int handle_show_http(int fd, int argc, char *argv[])
{
	struct ast_http_uri *urih;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	ast_cli(fd, "HTTP Server Status:\n");
	ast_cli(fd, "Prefix: %s\n", prefix);
	if (oldsin.sin_family)
		ast_cli(fd, "Server Enabled and Bound to %s:%d\n\n",
			ast_inet_ntoa(oldsin.sin_addr),
			ntohs(oldsin.sin_port));
	else
		ast_cli(fd, "Server Disabled\n\n");
	ast_cli(fd, "Enabled URI's:\n");
	ast_rwlock_rdlock(&uris_lock);
	urih = uris;
	while(urih){
		ast_cli(fd, "%s/%s%s => %s\n", prefix, urih->uri, (urih->has_subtree ? "/..." : "" ), urih->description);
		urih = urih->next;
	}
	if (!uris)
		ast_cli(fd, "None.\n");
	ast_rwlock_unlock(&uris_lock);

	return RESULT_SUCCESS;
}

int ast_http_reload(void)
{
	return __ast_http_load(1);
}

static char show_http_help[] =
"Usage: http show status\n"
"       Lists status of internal HTTP engine\n";

static struct ast_cli_entry cli_http[] = {
	{ { "http", "show", "status", NULL },
	handle_show_http, "Display HTTP server status",
	show_http_help },
};

int ast_http_init(void)
{
	ast_http_uri_link(&statusuri);
	ast_http_uri_link(&staticuri);
	ast_cli_register_multiple(cli_http, sizeof(cli_http) / sizeof(struct ast_cli_entry));

	return __ast_http_load(0);
}
