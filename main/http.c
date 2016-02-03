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
 * \extref GMime http://spruce.sourceforge.net/gmime/
 *
 * \ref AstHTTP - AMI over the http protocol
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <fcntl.h>

#include "asterisk/paths.h"	/* use ast_config_AST_DATA_DIR */
#include "asterisk/cli.h"
#include "asterisk/tcptls.h"
#include "asterisk/http.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/config.h"
#include "asterisk/stringfields.h"
#include "asterisk/ast_version.h"
#include "asterisk/manager.h"
#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "asterisk/netsock2.h"

#define MAX_PREFIX 80
#define DEFAULT_PORT 8088
#define DEFAULT_TLS_PORT 8089
#define DEFAULT_SESSION_LIMIT 100
#define DEFAULT_SESSION_INACTIVITY 30000	/* (ms) Idle time waiting for data. */

/* See http.h for more information about the SSL implementation */
#if defined(HAVE_OPENSSL) && (defined(HAVE_FUNOPEN) || defined(HAVE_FOPENCOOKIE))
#define	DO_SSL	/* comment in/out if you want to support ssl */
#endif

static int session_limit = DEFAULT_SESSION_LIMIT;
static int session_inactivity = DEFAULT_SESSION_INACTIVITY;
static int session_count = 0;

static struct ast_tls_config http_tls_cfg;

static void *httpd_helper_thread(void *arg);

/*!
 * we have up to two accepting threads, one for http, one for https
 */
static struct ast_tcptls_session_args http_desc = {
	.accept_fd = -1,
	.master = AST_PTHREADT_NULL,
	.tls_cfg = NULL,
	.poll_timeout = -1,
	.name = "http server",
	.accept_fn = ast_tcptls_server_root,
	.worker_fn = httpd_helper_thread,
};

static struct ast_tcptls_session_args https_desc = {
	.accept_fd = -1,
	.master = AST_PTHREADT_NULL,
	.tls_cfg = &http_tls_cfg,
	.poll_timeout = -1,
	.name = "https server",
	.accept_fn = ast_tcptls_server_root,
	.worker_fn = httpd_helper_thread,
};

static AST_RWLIST_HEAD_STATIC(uris, ast_http_uri);	/*!< list of supported handlers */

/* all valid URIs must be prepended by the string in prefix. */
static char prefix[MAX_PREFIX];
static int enablestatic;

/*! \brief Limit the kinds of files we're willing to serve up */
static struct {
	const char *ext;
	const char *mtype;
} mimetypes[] = {
	{ "png", "image/png" },
	{ "xml", "text/xml" },
	{ "jpg", "image/jpeg" },
	{ "js", "application/x-javascript" },
	{ "wav", "audio/x-wav" },
	{ "mp3", "audio/mpeg" },
	{ "svg", "image/svg+xml" },
	{ "svgz", "image/svg+xml" },
	{ "gif", "image/gif" },
	{ "html", "text/html" },
	{ "htm", "text/html" },
	{ "css", "text/css" },
	{ "cnf", "text/plain" },
	{ "cfg", "text/plain" },
	{ "bin", "application/octet-stream" },
	{ "sbn", "application/octet-stream" },
	{ "ld", "application/octet-stream" },
};

struct http_uri_redirect {
	AST_LIST_ENTRY(http_uri_redirect) entry;
	char *dest;
	char target[0];
};

static AST_RWLIST_HEAD_STATIC(uri_redirects, http_uri_redirect);

static const struct ast_cfhttp_methods_text {
	enum ast_http_method method;
	const char *text;
} ast_http_methods_text[] = {
	{ AST_HTTP_UNKNOWN,     "UNKNOWN" },
	{ AST_HTTP_GET,         "GET" },
	{ AST_HTTP_POST,        "POST" },
	{ AST_HTTP_HEAD,        "HEAD" },
	{ AST_HTTP_PUT,         "PUT" },
};

const char *ast_get_http_method(enum ast_http_method method)
{
	int x;

	for (x = 0; x < ARRAY_LEN(ast_http_methods_text); x++) {
		if (ast_http_methods_text[x].method == method) {
			return ast_http_methods_text[x].text;
		}
	}

	return NULL;
}

const char *ast_http_ftype2mtype(const char *ftype)
{
	int x;

	if (ftype) {
		for (x = 0; x < ARRAY_LEN(mimetypes); x++) {
			if (!strcasecmp(ftype, mimetypes[x].ext)) {
				return mimetypes[x].mtype;
			}
		}
	}
	return NULL;
}

uint32_t ast_http_manid_from_vars(struct ast_variable *headers)
{
	uint32_t mngid = 0;
	struct ast_variable *v, *cookies;

	cookies = ast_http_get_cookies(headers);
	for (v = cookies; v; v = v->next) {
		if (!strcasecmp(v->name, "mansession_id")) {
			sscanf(v->value, "%30x", &mngid);
			break;
		}
	}
	ast_variables_destroy(cookies);
	return mngid;
}

void ast_http_prefix(char *buf, int len)
{
	if (buf) {
		ast_copy_string(buf, prefix, len);
	}
}

static int static_callback(struct ast_tcptls_session_instance *ser,
	const struct ast_http_uri *urih, const char *uri,
	enum ast_http_method method, struct ast_variable *get_vars,
	struct ast_variable *headers)
{
	char *path;
	const char *ftype;
	const char *mtype;
	char wkspace[80];
	struct stat st;
	int len;
	int fd;
	struct ast_str *http_header;
	struct timeval tv;
	struct ast_tm tm;
	char timebuf[80], etag[23];
	struct ast_variable *v;
	int not_modified = 0;

	if (method != AST_HTTP_GET && method != AST_HTTP_HEAD) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return -1;
	}

	/* Yuck.  I'm not really sold on this, but if you don't deliver static content it makes your configuration
	   substantially more challenging, but this seems like a rather irritating feature creep on Asterisk. */
	if (!enablestatic || ast_strlen_zero(uri)) {
		goto out403;
	}

	/* Disallow any funny filenames at all (checking first character only??) */
	if ((uri[0] < 33) || strchr("./|~@#$%^&*() \t", uri[0])) {
		goto out403;
	}

	if (strstr(uri, "/..")) {
		goto out403;
	}

	if ((ftype = strrchr(uri, '.'))) {
		ftype++;
	}

	if (!(mtype = ast_http_ftype2mtype(ftype))) {
		snprintf(wkspace, sizeof(wkspace), "text/%s", S_OR(ftype, "plain"));
		mtype = wkspace;
	}

	/* Cap maximum length */
	if ((len = strlen(uri) + strlen(ast_config_AST_DATA_DIR) + strlen("/static-http/") + 5) > 1024) {
		goto out403;
	}

	path = ast_alloca(len);
	sprintf(path, "%s/static-http/%s", ast_config_AST_DATA_DIR, uri);
	if (stat(path, &st)) {
		goto out404;
	}

	if (S_ISDIR(st.st_mode)) {
		goto out404;
	}

	if (strstr(path, "/private/") && !astman_is_authed(ast_http_manid_from_vars(headers))) {
		goto out403;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		goto out403;
	}

	/* make "Etag:" http header value */
	snprintf(etag, sizeof(etag), "\"%ld\"", (long)st.st_mtime);

	/* make "Last-Modified:" http header value */
	tv.tv_sec = st.st_mtime;
	tv.tv_usec = 0;
	ast_strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", ast_localtime(&tv, &tm, "GMT"));

	/* check received "If-None-Match" request header and Etag value for file */
	for (v = headers; v; v = v->next) {
		if (!strcasecmp(v->name, "If-None-Match")) {
			if (!strcasecmp(v->value, etag)) {
				not_modified = 1;
			}
			break;
		}
	}

	if ( (http_header = ast_str_create(255)) == NULL) {
		close(fd);
		return -1;
	}

	ast_str_set(&http_header, 0, "Content-type: %s\r\n"
		"ETag: %s\r\n"
		"Last-Modified: %s\r\n",
		mtype,
		etag,
		timebuf);

	/* ast_http_send() frees http_header, so we don't need to do it before returning */
	if (not_modified) {
		ast_http_send(ser, method, 304, "Not Modified", http_header, NULL, 0, 1);
	} else {
		ast_http_send(ser, method, 200, NULL, http_header, NULL, fd, 1); /* static content flag is set */
	}
	close(fd);
	return 0;

out404:
	ast_http_error(ser, 404, "Not Found", "The requested URL was not found on this server.");
	return -1;

out403:
	ast_http_error(ser, 403, "Access Denied", "You do not have permission to access the requested URL.");
	return -1;
}

static int httpstatus_callback(struct ast_tcptls_session_instance *ser,
	const struct ast_http_uri *urih, const char *uri,
	enum ast_http_method method, struct ast_variable *get_vars,
	struct ast_variable *headers)
{
	struct ast_str *out;
	struct ast_variable *v, *cookies = NULL;

	if (method != AST_HTTP_GET && method != AST_HTTP_HEAD) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return -1;
	}

	if ( (out = ast_str_create(512)) == NULL) {
		return -1;
	}

	ast_str_append(&out, 0,
		"<html><title>Asterisk HTTP Status</title>\r\n"
		"<body bgcolor=\"#ffffff\">\r\n"
		"<table bgcolor=\"#f1f1f1\" align=\"center\"><tr><td bgcolor=\"#e0e0ff\" colspan=\"2\" width=\"500\">\r\n"
		"<h2>&nbsp;&nbsp;Asterisk&trade; HTTP Status</h2></td></tr>\r\n");

	ast_str_append(&out, 0, "<tr><td><i>Prefix</i></td><td><b>%s</b></td></tr>\r\n", prefix);
	ast_str_append(&out, 0, "<tr><td><i>Bind Address</i></td><td><b>%s</b></td></tr>\r\n",
		       ast_sockaddr_stringify_addr(&http_desc.old_address));
	ast_str_append(&out, 0, "<tr><td><i>Bind Port</i></td><td><b>%s</b></td></tr>\r\n",
		       ast_sockaddr_stringify_port(&http_desc.old_address));
	if (http_tls_cfg.enabled) {
		ast_str_append(&out, 0, "<tr><td><i>SSL Bind Port</i></td><td><b>%s</b></td></tr>\r\n",
			       ast_sockaddr_stringify_port(&https_desc.old_address));
	}
	ast_str_append(&out, 0, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");
	for (v = get_vars; v; v = v->next) {
		ast_str_append(&out, 0, "<tr><td><i>Submitted GET Variable '%s'</i></td><td>%s</td></tr>\r\n", v->name, v->value);
	}
	ast_str_append(&out, 0, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");

	cookies = ast_http_get_cookies(headers);
	for (v = cookies; v; v = v->next) {
		ast_str_append(&out, 0, "<tr><td><i>Cookie '%s'</i></td><td>%s</td></tr>\r\n", v->name, v->value);
	}
	ast_variables_destroy(cookies);

	ast_str_append(&out, 0, "</table><center><font size=\"-1\"><i>Asterisk and Digium are registered trademarks of Digium, Inc.</i></font></center></body></html>\r\n");
	ast_http_send(ser, method, 200, NULL, NULL, out, 0, 0);
	return 0;
}

static struct ast_http_uri statusuri = {
	.callback = httpstatus_callback,
	.description = "Asterisk HTTP General Status",
	.uri = "httpstatus",
	.has_subtree = 0,
	.data = NULL,
	.key = __FILE__,
};

static struct ast_http_uri staticuri = {
	.callback = static_callback,
	.description = "Asterisk HTTP Static Delivery",
	.uri = "static",
	.has_subtree = 1,
	.data = NULL,
	.key= __FILE__,
};


/* send http/1.1 response */
/* free content variable and close socket*/
void ast_http_send(struct ast_tcptls_session_instance *ser,
	enum ast_http_method method, int status_code, const char *status_title,
	struct ast_str *http_header, struct ast_str *out, const int fd,
	unsigned int static_content)
{
	struct timeval now = ast_tvnow();
	struct ast_tm tm;
	char timebuf[80];
	int content_length = 0;

	if (!ser || 0 == ser->f) {
		return;
	}

	ast_strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", ast_localtime(&now, &tm, "GMT"));

	/* calc content length */
	if (out) {
		content_length += ast_str_strlen(out);
	}

	if (fd) {
		content_length += lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
	}

	/* send http header */
	fprintf(ser->f, "HTTP/1.1 %d %s\r\n"
		"Server: Asterisk/%s\r\n"
		"Date: %s\r\n"
		"Connection: close\r\n"
		"%s"
		"Content-Length: %d\r\n"
		"%s"
		"\r\n",
		status_code, status_title ? status_title : "OK",
		ast_get_version(),
		timebuf,
		static_content ? "" : "Cache-Control: no-cache, no-store\r\n",
		content_length,
		http_header ? ast_str_buffer(http_header) : ""
		);

	/* send content */
	if (method != AST_HTTP_HEAD || status_code >= 400) {
		if (out && ast_str_strlen(out)) {
			if (fwrite(ast_str_buffer(out), ast_str_strlen(out), 1, ser->f) != 1) {
				ast_log(LOG_ERROR, "fwrite() failed: %s\n", strerror(errno));
			}
		}

		if (fd) {
			char buf[256];
			int len;
			while ((len = read(fd, buf, sizeof(buf))) > 0) {
				if (fwrite(buf, len, 1, ser->f) != 1) {
					ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
					break;
				}
			}
		}
	}

	if (http_header) {
		ast_free(http_header);
	}
	if (out) {
		ast_free(out);
	}

	ast_tcptls_close_session_file(ser);
	return;
}

/* Send http "401 Unauthorized" responce and close socket*/
void ast_http_auth(struct ast_tcptls_session_instance *ser, const char *realm,
	const unsigned long nonce, const unsigned long opaque, int stale,
	const char *text)
{
	struct ast_str *http_headers = ast_str_create(128);
	struct ast_str *out = ast_str_create(512);

	if (!http_headers || !out) {
		ast_free(http_headers);
		ast_free(out);
		return;
	}

	ast_str_set(&http_headers, 0,
		"WWW-authenticate: Digest algorithm=MD5, realm=\"%s\", nonce=\"%08lx\", qop=\"auth\", opaque=\"%08lx\"%s\r\n"
		"Content-type: text/html\r\n",
		realm ? realm : "Asterisk",
		nonce,
		opaque,
		stale ? ", stale=true" : "");

	ast_str_set(&out, 0,
		"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<html><head>\r\n"
		"<title>401 Unauthorized</title>\r\n"
		"</head><body>\r\n"
		"<h1>401 Unauthorized</h1>\r\n"
		"<p>%s</p>\r\n"
		"<hr />\r\n"
		"<address>Asterisk Server</address>\r\n"
		"</body></html>\r\n",
		text ? text : "");

	ast_http_send(ser, AST_HTTP_UNKNOWN, 401, "Unauthorized", http_headers, out, 0, 0);
	return;
}

/* send http error response and close socket*/
void ast_http_error(struct ast_tcptls_session_instance *ser, int status_code, const char *status_title, const char *text)
{
	struct ast_str *http_headers = ast_str_create(40);
	struct ast_str *out = ast_str_create(256);

	if (!http_headers || !out) {
		ast_free(http_headers);
		ast_free(out);
		return;
	}

	ast_str_set(&http_headers, 0, "Content-type: text/html\r\n");

	ast_str_set(&out, 0,
		"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		"<html><head>\r\n"
		"<title>%d %s</title>\r\n"
		"</head><body>\r\n"
		"<h1>%s</h1>\r\n"
		"<p>%s</p>\r\n"
		"<hr />\r\n"
		"<address>Asterisk Server</address>\r\n"
		"</body></html>\r\n",
			status_code, status_title, status_title, text);

	ast_http_send(ser, AST_HTTP_UNKNOWN, status_code, status_title, http_headers, out, 0, 0);
	return;
}

/*! \brief
 * Link the new uri into the list.
 *
 * They are sorted by length of
 * the string, not alphabetically. Duplicate entries are not replaced,
 * but the insertion order (using <= and not just <) makes sure that
 * more recent insertions hide older ones.
 * On a lookup, we just scan the list and stop at the first matching entry.
 */
int ast_http_uri_link(struct ast_http_uri *urih)
{
	struct ast_http_uri *uri;
	int len = strlen(urih->uri);

	AST_RWLIST_WRLOCK(&uris);

	if ( AST_RWLIST_EMPTY(&uris) || strlen(AST_RWLIST_FIRST(&uris)->uri) <= len ) {
		AST_RWLIST_INSERT_HEAD(&uris, urih, entry);
		AST_RWLIST_UNLOCK(&uris);
		return 0;
	}

	AST_RWLIST_TRAVERSE(&uris, uri, entry) {
		if (AST_RWLIST_NEXT(uri, entry) &&
			strlen(AST_RWLIST_NEXT(uri, entry)->uri) <= len) {
			AST_RWLIST_INSERT_AFTER(&uris, uri, urih, entry);
			AST_RWLIST_UNLOCK(&uris);

			return 0;
		}
	}

	AST_RWLIST_INSERT_TAIL(&uris, urih, entry);

	AST_RWLIST_UNLOCK(&uris);

	return 0;
}

void ast_http_uri_unlink(struct ast_http_uri *urih)
{
	AST_RWLIST_WRLOCK(&uris);
	AST_RWLIST_REMOVE(&uris, urih, entry);
	AST_RWLIST_UNLOCK(&uris);
}

void ast_http_uri_unlink_all_with_key(const char *key)
{
	struct ast_http_uri *urih;
	AST_RWLIST_WRLOCK(&uris);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&uris, urih, entry) {
		if (!strcmp(urih->key, key)) {
			AST_RWLIST_REMOVE_CURRENT(entry);
			if (urih->dmallocd) {
				ast_free(urih->data);
			}
			if (urih->mallocd) {
				ast_free(urih);
			}
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&uris);
}

#define MAX_POST_CONTENT 1025

/*
 * get post variables from client Request Entity-Body, if content type is
 * application/x-www-form-urlencoded
 */
struct ast_variable *ast_http_get_post_vars(
	struct ast_tcptls_session_instance *ser, struct ast_variable *headers)
{
	int content_length = 0;
	struct ast_variable *v, *post_vars=NULL, *prev = NULL;
	char *buf, *var, *val;
	int res;

	for (v = headers; v; v = v->next) {
		if (!strcasecmp(v->name, "Content-Type")) {
			if (strcasecmp(v->value, "application/x-www-form-urlencoded")) {
				return NULL;
			}
			break;
		}
	}

	for (v = headers; v; v = v->next) {
		if (!strcasecmp(v->name, "Content-Length")) {
			content_length = atoi(v->value);
			break;
		}
	}

	if (content_length <= 0) {
		return NULL;
	}

	if (content_length > MAX_POST_CONTENT - 1) {
		ast_log(LOG_WARNING, "Excessively long HTTP content. %d is greater than our max of %d\n",
				content_length, MAX_POST_CONTENT);
		ast_http_send(ser, AST_HTTP_POST, 413, "Request Entity Too Large", NULL, NULL, 0, 0);
		return NULL;
	}

	buf = ast_malloc(content_length + 1);
	if (!buf) {
		return NULL;
	}

	res = fread(buf, 1, content_length, ser->f);
	if (res < content_length) {
		/* Error, distinguishable by ferror() or feof(), but neither
		 * is good. */
		goto done;
	}
	buf[content_length] = '\0';

	while ((val = strsep(&buf, "&"))) {
		var = strsep(&val, "=");
		if (val) {
			ast_uri_decode(val, ast_uri_http_legacy);
		} else  {
			val = "";
		}
		ast_uri_decode(var, ast_uri_http_legacy);
		if ((v = ast_variable_new(var, val, ""))) {
			if (post_vars) {
				prev->next = v;
			} else {
				post_vars = v;
			}
			prev = v;
		}
	}

done:
	ast_free(buf);
	return post_vars;
}

static int handle_uri(struct ast_tcptls_session_instance *ser, char *uri,
	enum ast_http_method method, struct ast_variable *headers)
{
	char *c;
	int res = -1;
	char *params = uri;
	struct ast_http_uri *urih = NULL;
	int l;
	struct ast_variable *get_vars = NULL, *v, *prev = NULL;
	struct http_uri_redirect *redirect;

	ast_debug(2, "HTTP Request URI is %s \n", uri);

	strsep(&params, "?");
	/* Extract arguments from the request and store them in variables. */
	if (params) {
		char *var, *val;

		while ((val = strsep(&params, "&"))) {
			var = strsep(&val, "=");
			if (val) {
				ast_uri_decode(val, ast_uri_http_legacy);
			} else  {
				val = "";
			}
			ast_uri_decode(var, ast_uri_http_legacy);
			if ((v = ast_variable_new(var, val, ""))) {
				if (get_vars) {
					prev->next = v;
				} else {
					get_vars = v;
				}
				prev = v;
			}
		}
	}
	ast_uri_decode(uri, ast_uri_http_legacy);

	AST_RWLIST_RDLOCK(&uri_redirects);
	AST_RWLIST_TRAVERSE(&uri_redirects, redirect, entry) {
		if (!strcasecmp(uri, redirect->target)) {
			struct ast_str *http_header = ast_str_create(128);
			ast_str_set(&http_header, 0, "Location: %s\r\n", redirect->dest);
			ast_http_send(ser, method, 302, "Moved Temporarily", http_header, NULL, 0, 0);

			break;
		}
	}
	AST_RWLIST_UNLOCK(&uri_redirects);
	if (redirect) {
		goto cleanup;
	}

	/* We want requests to start with the (optional) prefix and '/' */
	l = strlen(prefix);
	if (!strncasecmp(uri, prefix, l) && uri[l] == '/') {
		uri += l + 1;
		/* scan registered uris to see if we match one. */
		AST_RWLIST_RDLOCK(&uris);
		AST_RWLIST_TRAVERSE(&uris, urih, entry) {
			ast_debug(2, "match request [%s] with handler [%s] len %d\n", uri, urih->uri, l);
			l = strlen(urih->uri);
			c = uri + l;	/* candidate */
			if (strncasecmp(urih->uri, uri, l) /* no match */
			    || (*c && *c != '/')) { /* substring */
				continue;
			}
			if (*c == '/') {
				c++;
			}
			if (!*c || urih->has_subtree) {
				uri = c;
				break;
			}
		}
		AST_RWLIST_UNLOCK(&uris);
	}
	if (urih) {
		res = urih->callback(ser, urih, uri, method, get_vars, headers);
	} else {
		ast_http_error(ser, 404, "Not Found", "The requested URL was not found on this server.");
	}

cleanup:
	ast_variables_destroy(get_vars);
	return res;
}

#ifdef DO_SSL
#if defined(HAVE_FUNOPEN)
#define HOOK_T int
#define LEN_T int
#else
#define HOOK_T ssize_t
#define LEN_T size_t
#endif

/*!
 * replacement read/write functions for SSL support.
 * We use wrappers rather than SSL_read/SSL_write directly so
 * we can put in some debugging.
 */
/*static HOOK_T ssl_read(void *cookie, char *buf, LEN_T len)
{
	int i = SSL_read(cookie, buf, len-1);
#if 0
	if (i >= 0)
		buf[i] = '\0';
	ast_verbose("ssl read size %d returns %d <%s>\n", (int)len, i, buf);
#endif
	return i;
}

static HOOK_T ssl_write(void *cookie, const char *buf, LEN_T len)
{
#if 0
	char *s = ast_alloca(len+1);
	strncpy(s, buf, len);
	s[len] = '\0';
	ast_verbose("ssl write size %d <%s>\n", (int)len, s);
#endif
	return SSL_write(cookie, buf, len);
}

static int ssl_close(void *cookie)
{
	close(SSL_get_fd(cookie));
	SSL_shutdown(cookie);
	SSL_free(cookie);
	return 0;
}*/
#endif	/* DO_SSL */

static struct ast_variable *parse_cookies(const char *cookies)
{
	char *parse = ast_strdupa(cookies);
	char *cur;
	struct ast_variable *vars = NULL, *var;

	while ((cur = strsep(&parse, ";"))) {
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

		ast_debug(1, "HTTP Cookie, Name: '%s'  Value: '%s'\n", name, val);

		var = ast_variable_new(name, val, __FILE__);
		var->next = vars;
		vars = var;
	}

	return vars;
}

/* get cookie from Request headers */
struct ast_variable *ast_http_get_cookies(struct ast_variable *headers)
{
	struct ast_variable *v, *cookies = NULL;

	for (v = headers; v; v = v->next) {
		if (!strcasecmp(v->name, "Cookie")) {
			ast_variables_destroy(cookies);
			cookies = parse_cookies(v->value);
		}
	}
	return cookies;
}

/*! Limit the number of request headers in case the sender is being ridiculous. */
#define MAX_HTTP_REQUEST_HEADERS	100

static void *httpd_helper_thread(void *data)
{
	char buf[4096];
	char header_line[4096];
	struct ast_tcptls_session_instance *ser = data;
	struct ast_variable *headers = NULL;
	struct ast_variable *tail = headers;
	char *uri, *method;
	enum ast_http_method http_method = AST_HTTP_UNKNOWN;
	int remaining_headers;
	int flags;
	struct protoent *p;

	if (ast_atomic_fetchadd_int(&session_count, +1) >= session_limit) {
		goto done;
	}

	/* here we set TCP_NODELAY on the socket to disable Nagle's algorithm.
	 * This is necessary to prevent delays (caused by buffering) as we
	 * write to the socket in bits and pieces. */
	p = getprotobyname("tcp");
	if (p) {
		int arg = 1;
		if( setsockopt(ser->fd, p->p_proto, TCP_NODELAY, (char *)&arg, sizeof(arg) ) < 0 ) {
			ast_log(LOG_WARNING, "Failed to set TCP_NODELAY on HTTP connection: %s\n", strerror(errno));
			ast_log(LOG_WARNING, "Some HTTP requests may be slow to respond.\n");
		}
	} else {
		ast_log(LOG_WARNING, "Failed to set TCP_NODELAY on HTTP connection, getprotobyname(\"tcp\") failed\n");
		ast_log(LOG_WARNING, "Some HTTP requests may be slow to respond.\n");
	}

	/* make sure socket is non-blocking */
	flags = fcntl(ser->fd, F_GETFL);
	flags |= O_NONBLOCK;
	fcntl(ser->fd, F_SETFL, flags);

	/* We can let the stream wait for data to arrive. */
	ast_tcptls_stream_set_exclusive_input(ser->stream_cookie, 1);

	ast_tcptls_stream_set_timeout_inactivity(ser->stream_cookie, session_inactivity);

	if (!fgets(buf, sizeof(buf), ser->f) || feof(ser->f)) {
		goto done;
	}

	/* Get method */
	method = ast_skip_blanks(buf);
	uri = ast_skip_nonblanks(method);
	if (*uri) {
		*uri++ = '\0';
	}

	if (!strcasecmp(method,"GET")) {
		http_method = AST_HTTP_GET;
	} else if (!strcasecmp(method,"POST")) {
		http_method = AST_HTTP_POST;
	} else if (!strcasecmp(method,"HEAD")) {
		http_method = AST_HTTP_HEAD;
	} else if (!strcasecmp(method,"PUT")) {
		http_method = AST_HTTP_PUT;
	}

	uri = ast_skip_blanks(uri);	/* Skip white space */

	if (*uri) {			/* terminate at the first blank */
		char *c = ast_skip_nonblanks(uri);

		if (*c) {
			*c = '\0';
		}
	} else {
		ast_http_error(ser, 400, "Bad Request", "Invalid Request");
		goto done;
	}

	/* process "Request Headers" lines */
	remaining_headers = MAX_HTTP_REQUEST_HEADERS;
	for (;;) {
		char *name;
		char *value;

		if (!fgets(header_line, sizeof(header_line), ser->f) || feof(ser->f)) {
			ast_http_error(ser, 400, "Bad Request", "Timeout");
			goto done;
		}

		/* Trim trailing characters */
		ast_trim_blanks(header_line);
		if (ast_strlen_zero(header_line)) {
			/* A blank line ends the request header section. */
			break;
		}

		value = header_line;
		name = strsep(&value, ":");
		if (!value) {
			continue;
		}

		value = ast_skip_blanks(value);
		if (ast_strlen_zero(value) || ast_strlen_zero(name)) {
			continue;
		}

		ast_trim_blanks(name);

		if (!remaining_headers--) {
			/* Too many headers. */
			ast_http_error(ser, 413, "Request Entity Too Large", "Too many headers");
			goto done;
		}
		if (!headers) {
			headers = ast_variable_new(name, value, __FILE__);
			tail = headers;
		} else {
			tail->next = ast_variable_new(name, value, __FILE__);
			tail = tail->next;
		}
		if (!tail) {
			/*
			 * Variable allocation failure.
			 * Try to make some room.
			 */
			ast_variables_destroy(headers);
			headers = NULL;

			ast_http_error(ser, 500, "Server Error", "Out of memory");
			goto done;
		}
	}

	handle_uri(ser, uri, http_method, headers);

done:
	ast_atomic_fetchadd_int(&session_count, -1);

	/* clean up all the header information */
	ast_variables_destroy(headers);

	if (ser->f) {
		ast_tcptls_close_session_file(ser);
	}
	ao2_ref(ser, -1);
	ser = NULL;
	return NULL;
}

/*!
 * \brief Add a new URI redirect
 * The entries in the redirect list are sorted by length, just like the list
 * of URI handlers.
 */
static void add_redirect(const char *value)
{
	char *target, *dest;
	struct http_uri_redirect *redirect, *cur;
	unsigned int target_len;
	unsigned int total_len;

	dest = ast_strdupa(value);
	dest = ast_skip_blanks(dest);
	target = strsep(&dest, " ");
	target = ast_skip_blanks(target);
	target = strsep(&target, " "); /* trim trailing whitespace */

	if (!dest) {
		ast_log(LOG_WARNING, "Invalid redirect '%s'\n", value);
		return;
	}

	target_len = strlen(target) + 1;
	total_len = sizeof(*redirect) + target_len + strlen(dest) + 1;

	if (!(redirect = ast_calloc(1, total_len))) {
		return;
	}
	redirect->dest = redirect->target + target_len;
	strcpy(redirect->target, target);
	strcpy(redirect->dest, dest);

	AST_RWLIST_WRLOCK(&uri_redirects);

	target_len--; /* So we can compare directly with strlen() */
	if (AST_RWLIST_EMPTY(&uri_redirects)
		|| strlen(AST_RWLIST_FIRST(&uri_redirects)->target) <= target_len ) {
		AST_RWLIST_INSERT_HEAD(&uri_redirects, redirect, entry);
		AST_RWLIST_UNLOCK(&uri_redirects);

		return;
	}

	AST_RWLIST_TRAVERSE(&uri_redirects, cur, entry) {
		if (AST_RWLIST_NEXT(cur, entry)
			&& strlen(AST_RWLIST_NEXT(cur, entry)->target) <= target_len ) {
			AST_RWLIST_INSERT_AFTER(&uri_redirects, cur, redirect, entry);
			AST_RWLIST_UNLOCK(&uri_redirects);
			return;
		}
	}

	AST_RWLIST_INSERT_TAIL(&uri_redirects, redirect, entry);

	AST_RWLIST_UNLOCK(&uri_redirects);
}

static int __ast_http_load(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	int enabled=0;
	int newenablestatic=0;
	char newprefix[MAX_PREFIX] = "";
	struct http_uri_redirect *redirect;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	uint32_t bindport = DEFAULT_PORT;
	RAII_VAR(struct ast_sockaddr *, addrs, NULL, ast_free);
	int num_addrs = 0;
	int http_tls_was_enabled = 0;

	cfg = ast_config_load2("http.conf", "http", config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
		return 0;
	}

	http_tls_was_enabled = (reload && http_tls_cfg.enabled);

	http_tls_cfg.enabled = 0;
	if (http_tls_cfg.certfile) {
		ast_free(http_tls_cfg.certfile);
	}
	http_tls_cfg.certfile = ast_strdup(AST_CERTFILE);

	if (http_tls_cfg.pvtfile) {
		ast_free(http_tls_cfg.pvtfile);
	}
	http_tls_cfg.pvtfile = ast_strdup("");

	/* Apply modern intermediate settings according to the Mozilla OpSec team as of July 30th, 2015 but disable TLSv1 */
	ast_set_flag(&http_tls_cfg.flags, AST_SSL_DISABLE_TLSV1 | AST_SSL_SERVER_CIPHER_ORDER);

	if (http_tls_cfg.cipher) {
		ast_free(http_tls_cfg.cipher);
	}
	http_tls_cfg.cipher = ast_strdup("ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA:AES:CAMELLIA:DES-CBC3-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA");

	AST_RWLIST_WRLOCK(&uri_redirects);
	while ((redirect = AST_RWLIST_REMOVE_HEAD(&uri_redirects, entry))) {
		ast_free(redirect);
	}
	AST_RWLIST_UNLOCK(&uri_redirects);

	ast_sockaddr_setnull(&https_desc.local_address);

	session_limit = DEFAULT_SESSION_LIMIT;
	session_inactivity = DEFAULT_SESSION_INACTIVITY;

	if (cfg) {
		v = ast_variable_browse(cfg, "general");
		for (; v; v = v->next) {

			/* read tls config options while preventing unsupported options from being set */
			if (strcasecmp(v->name, "tlscafile")
				&& strcasecmp(v->name, "tlscapath")
				&& strcasecmp(v->name, "tlscadir")
				&& strcasecmp(v->name, "tlsverifyclient")
				&& strcasecmp(v->name, "tlsdontverifyserver")
				&& strcasecmp(v->name, "tlsclientmethod")
				&& strcasecmp(v->name, "sslclientmethod")
				&& !ast_tls_read_conf(&http_tls_cfg, &https_desc, v->name, v->value)) {
				continue;
			}

			if (!strcasecmp(v->name, "enabled")) {
				enabled = ast_true(v->value);
			} else if (!strcasecmp(v->name, "enablestatic")) {
				newenablestatic = ast_true(v->value);
			} else if (!strcasecmp(v->name, "bindport")) {
				if (ast_parse_arg(v->value, PARSE_UINT32 | PARSE_IN_RANGE | PARSE_DEFAULT, &bindport, DEFAULT_PORT, 0, 65535)) {
					ast_log(LOG_WARNING, "Invalid port %s specified. Using default port %"PRId32, v->value, DEFAULT_PORT);
				}
			} else if (!strcasecmp(v->name, "bindaddr")) {
				if (!(num_addrs = ast_sockaddr_resolve(&addrs, v->value, 0, AST_AF_UNSPEC))) {
					ast_log(LOG_WARNING, "Invalid bind address %s\n", v->value);
				}
			} else if (!strcasecmp(v->name, "prefix")) {
				if (!ast_strlen_zero(v->value)) {
					newprefix[0] = '/';
					ast_copy_string(newprefix + 1, v->value, sizeof(newprefix) - 1);
				} else {
					newprefix[0] = '\0';
				}
			} else if (!strcasecmp(v->name, "redirect")) {
				add_redirect(v->value);
			} else if (!strcasecmp(v->name, "sessionlimit")) {
				if (ast_parse_arg(v->value, PARSE_INT32|PARSE_DEFAULT|PARSE_IN_RANGE,
							&session_limit, DEFAULT_SESSION_LIMIT, 1, INT_MAX)) {
					ast_log(LOG_WARNING, "Invalid %s '%s' at line %d of http.conf\n",
							v->name, v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "session_inactivity")) {
				if (ast_parse_arg(v->value, PARSE_INT32 |PARSE_DEFAULT | PARSE_IN_RANGE,
					&session_inactivity, DEFAULT_SESSION_INACTIVITY, 1, INT_MAX)) {
					ast_log(LOG_WARNING, "Invalid %s '%s' at line %d of http.conf\n",
						v->name, v->value, v->lineno);
				}
			} else {
				ast_log(LOG_WARNING, "Ignoring unknown option '%s' in http.conf\n", v->name);
			}
		}

		ast_config_destroy(cfg);
	}

	if (strcmp(prefix, newprefix)) {
		ast_copy_string(prefix, newprefix, sizeof(prefix));
	}
	enablestatic = newenablestatic;

	if (num_addrs && enabled) {
		int i;
		for (i = 0; i < num_addrs; ++i) {
			ast_sockaddr_copy(&http_desc.local_address, &addrs[i]);
			if (!ast_sockaddr_port(&http_desc.local_address)) {
				ast_sockaddr_set_port(&http_desc.local_address, bindport);
			}
			ast_tcptls_server_start(&http_desc);
			if (http_desc.accept_fd == -1) {
				ast_log(LOG_WARNING, "Failed to start HTTP server for address %s\n", ast_sockaddr_stringify(&addrs[i]));
				ast_sockaddr_setnull(&http_desc.local_address);
			} else {
				ast_verb(1, "Bound HTTP server to address %s\n", ast_sockaddr_stringify(&addrs[i]));
				break;
			}
		}
		/* When no specific TLS bindaddr is specified, we just use
		 * the non-TLS bindaddress here.
		 */
		if (ast_sockaddr_isnull(&https_desc.local_address) && http_desc.accept_fd != -1) {
			ast_sockaddr_copy(&https_desc.local_address, &https_desc.local_address);
			/* Of course, we can't use the same port though.
			 * Since no bind address was specified, we just use the
			 * default TLS port
			 */
			ast_sockaddr_set_port(&https_desc.local_address, DEFAULT_TLS_PORT);
		}
	}
	if (http_tls_was_enabled && !http_tls_cfg.enabled) {
		ast_tcptls_server_stop(&https_desc);
	} else if (http_tls_cfg.enabled && !ast_sockaddr_isnull(&https_desc.local_address)) {
		/* We can get here either because a TLS-specific address was specified
		 * or because we copied the non-TLS address here. In the case where
		 * we read an explicit address from the config, there may have been
		 * no port specified, so we'll just use the default TLS port.
		 */
		if (!ast_sockaddr_port(&https_desc.local_address)) {
			ast_sockaddr_set_port(&https_desc.local_address, DEFAULT_TLS_PORT);
		}
		if (ast_ssl_setup(https_desc.tls_cfg)) {
			ast_tcptls_server_start(&https_desc);
		}
	}

	return 0;
}

static char *handle_show_http(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_http_uri *urih;
	struct http_uri_redirect *redirect;

	switch (cmd) {
	case CLI_INIT:
		e->command = "http show status";
		e->usage =
			"Usage: http show status\n"
			"       Lists status of internal HTTP engine\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}
	ast_cli(a->fd, "HTTP Server Status:\n");
	ast_cli(a->fd, "Prefix: %s\n", prefix);
	if (ast_sockaddr_isnull(&http_desc.old_address)) {
		ast_cli(a->fd, "Server Disabled\n\n");
	} else {
		ast_cli(a->fd, "Server Enabled and Bound to %s\n\n",
			ast_sockaddr_stringify(&http_desc.old_address));
		if (http_tls_cfg.enabled) {
			ast_cli(a->fd, "HTTPS Server Enabled and Bound to %s\n\n",
				ast_sockaddr_stringify(&https_desc.old_address));
		}
	}

	ast_cli(a->fd, "Enabled URI's:\n");
	AST_RWLIST_RDLOCK(&uris);
	if (AST_RWLIST_EMPTY(&uris)) {
		ast_cli(a->fd, "None.\n");
	} else {
		AST_RWLIST_TRAVERSE(&uris, urih, entry)
			ast_cli(a->fd, "%s/%s%s => %s\n", prefix, urih->uri, (urih->has_subtree ? "/..." : "" ), urih->description);
	}
	AST_RWLIST_UNLOCK(&uris);

	ast_cli(a->fd, "\nEnabled Redirects:\n");
	AST_RWLIST_RDLOCK(&uri_redirects);
	AST_RWLIST_TRAVERSE(&uri_redirects, redirect, entry)
		ast_cli(a->fd, "  %s => %s\n", redirect->target, redirect->dest);
	if (AST_RWLIST_EMPTY(&uri_redirects)) {
		ast_cli(a->fd, "  None.\n");
	}
	AST_RWLIST_UNLOCK(&uri_redirects);

	return CLI_SUCCESS;
}

int ast_http_reload(void)
{
	return __ast_http_load(1);
}

static struct ast_cli_entry cli_http[] = {
	AST_CLI_DEFINE(handle_show_http, "Display HTTP server status"),
};

static void http_shutdown(void)
{
	struct http_uri_redirect *redirect;
	ast_cli_unregister_multiple(cli_http, ARRAY_LEN(cli_http));

	ast_tcptls_server_stop(&http_desc);
	if (http_tls_cfg.enabled) {
		ast_tcptls_server_stop(&https_desc);
	}
	ast_free(http_tls_cfg.certfile);
	ast_free(http_tls_cfg.pvtfile);
	ast_free(http_tls_cfg.cipher);

	ast_http_uri_unlink(&statusuri);
	ast_http_uri_unlink(&staticuri);

	AST_RWLIST_WRLOCK(&uri_redirects);
	while ((redirect = AST_RWLIST_REMOVE_HEAD(&uri_redirects, entry))) {
		ast_free(redirect);
	}
	AST_RWLIST_UNLOCK(&uri_redirects);
}

int ast_http_init(void)
{
	ast_http_uri_link(&statusuri);
	ast_http_uri_link(&staticuri);
	ast_cli_register_multiple(cli_http, ARRAY_LEN(cli_http));
	ast_register_cleanup(http_shutdown);

	return __ast_http_load(0);
}
