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
 * GMime http://spruce.sourceforge.net/gmime/
 *
 * \ref AstHTTP - AMI over the http protocol
 */

/*! \li \ref http.c uses the configuration file \ref http.conf
 * \addtogroup configuration_file
 */

/*! \page http.conf http.conf
 * \verbinclude http.conf.sample
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
#include "asterisk/json.h"

#define MAX_PREFIX 80
#define DEFAULT_PORT 8088
#define DEFAULT_TLS_PORT 8089
#define DEFAULT_SESSION_LIMIT 100
/*! (ms) Idle time waiting for data. */
#define DEFAULT_SESSION_INACTIVITY 30000
/*! (ms) Min timeout for initial HTTP request to start coming in. */
#define MIN_INITIAL_REQUEST_TIMEOUT	10000
/*! (ms) Idle time between HTTP requests */
#define DEFAULT_SESSION_KEEP_ALIVE 15000
/*! Max size for the http server name */
#define	MAX_SERVER_NAME_LENGTH 128
/*! Max size for the http response header */
#define	DEFAULT_RESPONSE_HEADER_LENGTH 512

/*! Maximum application/json or application/x-www-form-urlencoded body content length. */
#if !defined(LOW_MEMORY)
#define MAX_CONTENT_LENGTH 4096
#else
#define MAX_CONTENT_LENGTH 1024
#endif	/* !defined(LOW_MEMORY) */

/*! Maximum line length for HTTP requests. */
#if !defined(LOW_MEMORY)
#define MAX_HTTP_LINE_LENGTH 4096
#else
#define MAX_HTTP_LINE_LENGTH 1024
#endif	/* !defined(LOW_MEMORY) */

static char http_server_name[MAX_SERVER_NAME_LENGTH];

static int session_limit = DEFAULT_SESSION_LIMIT;
static int session_inactivity = DEFAULT_SESSION_INACTIVITY;
static int session_keep_alive = DEFAULT_SESSION_KEEP_ALIVE;
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
	{ AST_HTTP_DELETE,      "DELETE" },
	{ AST_HTTP_OPTIONS,     "OPTIONS" },
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
		return 0;
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

	http_header = ast_str_create(255);
	if (!http_header) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Out of memory");
		close(fd);
		return 0;
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
	return 0;

out403:
	ast_http_request_close_on_completion(ser);
	ast_http_error(ser, 403, "Access Denied", "You do not have permission to access the requested URL.");
	return 0;
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
		return 0;
	}

	out = ast_str_create(512);
	if (!out) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Out of memory");
		return 0;
	}

	ast_str_append(&out, 0,
		"<title>Asterisk HTTP Status</title>\r\n"
		"<body bgcolor=\"#ffffff\">\r\n"
		"<table bgcolor=\"#f1f1f1\" align=\"center\"><tr><td bgcolor=\"#e0e0ff\" colspan=\"2\" width=\"500\">\r\n"
		"<h2>&nbsp;&nbsp;Asterisk&trade; HTTP Status</h2></td></tr>\r\n");

	ast_str_append(&out, 0, "<tr><td><i>Server</i></td><td><b>%s</b></td></tr>\r\n", http_server_name);
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

	ast_str_append(&out, 0, "</table><center><font size=\"-1\"><i>Asterisk and Digium are registered trademarks of Digium, Inc.</i></font></center></body>\r\n");
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

enum http_private_flags {
	/*! TRUE if the HTTP request has a body. */
	HTTP_FLAG_HAS_BODY = (1 << 0),
	/*! TRUE if the HTTP request body has been read. */
	HTTP_FLAG_BODY_READ = (1 << 1),
	/*! TRUE if the HTTP request must close when completed. */
	HTTP_FLAG_CLOSE_ON_COMPLETION = (1 << 2),
};

/*! HTTP tcptls worker_fn private data. */
struct http_worker_private_data {
	/*! Body length or -1 if chunked.  Valid if HTTP_FLAG_HAS_BODY is TRUE. */
	int body_length;
	/*! HTTP body tracking flags */
	struct ast_flags flags;
};

void ast_http_send(struct ast_tcptls_session_instance *ser,
	enum ast_http_method method, int status_code, const char *status_title,
	struct ast_str *http_header, struct ast_str *out, int fd,
	unsigned int static_content)
{
	struct timeval now = ast_tvnow();
	struct ast_tm tm;
	char timebuf[80];
	int content_length = 0;
	int close_connection;
	struct ast_str *server_header_field = ast_str_create(MAX_SERVER_NAME_LENGTH);

	if (!ser || !ser->f || !server_header_field) {
		/* The connection is not open. */
		ast_free(http_header);
		ast_free(out);
		ast_free(server_header_field);
		return;
	}

	if(!ast_strlen_zero(http_server_name)) {
		ast_str_set(&server_header_field,
	                0,
	                "Server: %s\r\n",
	                http_server_name);
	}

	/*
	 * We shouldn't be sending non-final status codes to this
	 * function because we may close the connection before
	 * returning.
	 */
	ast_assert(200 <= status_code);

	if (session_keep_alive <= 0) {
		close_connection = 1;
	} else {
		struct http_worker_private_data *request;

		request = ser->private_data;
		if (!request
			|| ast_test_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION)
			|| ast_http_body_discard(ser)) {
			close_connection = 1;
		} else {
			close_connection = 0;
		}
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
	fprintf(ser->f,
		"HTTP/1.1 %d %s\r\n"
		"%s"
		"Date: %s\r\n"
		"%s"
		"%s"
		"%s"
		"Content-Length: %d\r\n"
		"\r\n",
		status_code, status_title ? status_title : "OK",
		ast_str_buffer(server_header_field),
		timebuf,
		close_connection ? "Connection: close\r\n" : "",
		static_content ? "" : "Cache-Control: no-cache, no-store\r\n",
		http_header ? ast_str_buffer(http_header) : "",
		content_length
		);

	/* send content */
	if (method != AST_HTTP_HEAD || status_code >= 400) {
		if (out && ast_str_strlen(out)) {
			if (fwrite(ast_str_buffer(out), ast_str_strlen(out), 1, ser->f) != 1) {
				ast_log(LOG_ERROR, "fwrite() failed: %s\n", strerror(errno));
				close_connection = 1;
			}
		}

		if (fd) {
			char buf[256];
			int len;

			while ((len = read(fd, buf, sizeof(buf))) > 0) {
				if (fwrite(buf, len, 1, ser->f) != 1) {
					ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
					close_connection = 1;
					break;
				}
			}
		}
	}

	ast_free(http_header);
	ast_free(out);
	ast_free(server_header_field);

	if (close_connection) {
		ast_debug(1, "HTTP closing session.  status_code:%d\n", status_code);
		ast_tcptls_close_session_file(ser);
	} else {
		ast_debug(1, "HTTP keeping session open.  status_code:%d\n", status_code);
	}
}

void ast_http_create_response(struct ast_tcptls_session_instance *ser, int status_code,
	const char *status_title, struct ast_str *http_header_data, const char *text)
{
	char server_name[MAX_SERVER_NAME_LENGTH];
	struct ast_str *server_address = ast_str_create(MAX_SERVER_NAME_LENGTH);
	struct ast_str *out = ast_str_create(MAX_CONTENT_LENGTH);

	if (!http_header_data || !server_address || !out) {
		ast_free(http_header_data);
		ast_free(server_address);
		ast_free(out);
		if (ser && ser->f) {
			ast_debug(1, "HTTP closing session. OOM.\n");
			ast_tcptls_close_session_file(ser);
		}
		return;
	}

	if(!ast_strlen_zero(http_server_name)) {
		ast_xml_escape(http_server_name, server_name, sizeof(server_name));
		ast_str_set(&server_address,
	                0,
	                "<address>%s</address>\r\n",
	                server_name);
	}

	ast_str_set(&out,
	            0,
	            "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
	            "<html><head>\r\n"
	            "<title>%d %s</title>\r\n"
	            "</head><body>\r\n"
	            "<h1>%s</h1>\r\n"
	            "<p>%s</p>\r\n"
	            "<hr />\r\n"
	            "%s"
	            "</body></html>\r\n",
	            status_code,
	            status_title,
	            status_title,
	            text ? text : "",
	            ast_str_buffer(server_address));

	ast_free(server_address);

	ast_http_send(ser,
	              AST_HTTP_UNKNOWN,
	              status_code,
	              status_title,
	              http_header_data,
	              out,
	              0,
	              0);
}

void ast_http_auth(struct ast_tcptls_session_instance *ser, const char *realm,
	const unsigned long nonce, const unsigned long opaque, int stale,
	const char *text)
{
	int status_code = 401;
	char *status_title = "Unauthorized";
	struct ast_str *http_header_data = ast_str_create(DEFAULT_RESPONSE_HEADER_LENGTH);

	if (http_header_data) {
		ast_str_set(&http_header_data,
		            0,
		            "WWW-authenticate: Digest algorithm=MD5, realm=\"%s\", nonce=\"%08lx\", qop=\"auth\", opaque=\"%08lx\"%s\r\n"
		            "Content-type: text/html\r\n",
		            realm ? realm : "Asterisk",
		            nonce,
		            opaque,
		            stale ? ", stale=true" : "");
	}

	ast_http_create_response(ser,
	                         status_code,
	                         status_title,
	                         http_header_data,
	                         text);
}

void ast_http_error(struct ast_tcptls_session_instance *ser, int status_code,
	const char *status_title, const char *text)
{
	struct ast_str *http_header_data = ast_str_create(DEFAULT_RESPONSE_HEADER_LENGTH);

	if (http_header_data) {
		ast_str_set(&http_header_data, 0, "Content-type: text/html\r\n");
	}

	ast_http_create_response(ser,
	                         status_code,
	                         status_title,
	                         http_header_data,
	                         text);
}

/*!
 * \brief Link the new uri into the list.
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

/*!
 * \brief Retrieves the header with the given field name.
 *
 * \param headers Headers to search.
 * \param field_name Name of the header to find.
 * \return Associated header value.
 * \return \c NULL if header is not present.
 */
static const char *get_header(struct ast_variable *headers, const char *field_name)
{
	struct ast_variable *v;

	for (v = headers; v; v = v->next) {
		if (!strcasecmp(v->name, field_name)) {
			return v->value;
		}
	}
	return NULL;
}

/*!
 * \brief Retrieves the content type specified in the "Content-Type" header.
 *
 * This function only returns the "type/subtype" and any trailing parameter is
 * not included.
 *
 * \note the return value is an allocated string that needs to be freed.
 *
 * \retval the content type/subtype or NULL if the header is not found.
 */
static char *get_content_type(struct ast_variable *headers)
{
	const char *content_type = get_header(headers, "Content-Type");
	const char *param;
	size_t size;

	if (!content_type) {
		return NULL;
	}

	param = strchr(content_type, ';');
	size = param ? param - content_type : strlen(content_type);

	return ast_strndup(content_type, size);
}

/*!
 * \brief Returns the value of the Content-Length header.
 *
 * \param headers HTTP headers.
 *
 * \retval length Value of the Content-Length header.
 * \retval 0 if header is not present.
 * \retval -1 if header is invalid.
 */
static int get_content_length(struct ast_variable *headers)
{
	const char *content_length = get_header(headers, "Content-Length");
	int length;

	if (!content_length) {
		/* Missing content length; assume zero */
		return 0;
	}

	length = 0;
	if (sscanf(content_length, "%30d", &length) != 1) {
		/* Invalid Content-Length value */
		length = -1;
	}
	return length;
}

/*!
 * \brief Returns the value of the Transfer-Encoding header.
 *
 * \param headers HTTP headers.
 * \retval string Value of the Transfer-Encoding header.
 * \retval NULL if header is not present.
 */
static const char *get_transfer_encoding(struct ast_variable *headers)
{
	return get_header(headers, "Transfer-Encoding");
}

/*!
 * \internal
 * \brief Determine if the HTTP peer wants the connection closed.
 *
 * \param headers List of HTTP headers
 *
 * \retval 0 keep connection open.
 * \retval -1 close connection.
 */
static int http_check_connection_close(struct ast_variable *headers)
{
	const char *connection = get_header(headers, "Connection");
	int close_connection = 0;

	if (connection && !strcasecmp(connection, "close")) {
		close_connection = -1;
	}
	return close_connection;
}

void ast_http_request_close_on_completion(struct ast_tcptls_session_instance *ser)
{
	struct http_worker_private_data *request = ser->private_data;

	ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
}

/*!
 * \internal
 * \brief Initialize the request tracking information in case of early failure.
 * \since 12.4.0
 *
 * \param request Request tracking information.
 *
 * \return Nothing
 */
static void http_request_tracking_init(struct http_worker_private_data *request)
{
	ast_set_flags_to(&request->flags,
		HTTP_FLAG_HAS_BODY | HTTP_FLAG_BODY_READ | HTTP_FLAG_CLOSE_ON_COMPLETION,
		/* Assume close in case request fails early */
		HTTP_FLAG_CLOSE_ON_COMPLETION);
}

/*!
 * \internal
 * \brief Setup the HTTP request tracking information.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 * \param headers List of HTTP headers.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int http_request_tracking_setup(struct ast_tcptls_session_instance *ser, struct ast_variable *headers)
{
	struct http_worker_private_data *request = ser->private_data;
	const char *transfer_encoding;

	ast_set_flags_to(&request->flags,
		HTTP_FLAG_HAS_BODY | HTTP_FLAG_BODY_READ | HTTP_FLAG_CLOSE_ON_COMPLETION,
		http_check_connection_close(headers) ? HTTP_FLAG_CLOSE_ON_COMPLETION : 0);

	transfer_encoding = get_transfer_encoding(headers);
	if (transfer_encoding && !strcasecmp(transfer_encoding, "chunked")) {
		request->body_length = -1;
		ast_set_flag(&request->flags, HTTP_FLAG_HAS_BODY);
		return 0;
	}

	request->body_length = get_content_length(headers);
	if (0 < request->body_length) {
		ast_set_flag(&request->flags, HTTP_FLAG_HAS_BODY);
	} else if (request->body_length < 0) {
		/* Invalid Content-Length */
		ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
		ast_http_error(ser, 400, "Bad Request", "Invalid Content-Length in request!");
		return -1;
	}
	return 0;
}

void ast_http_body_read_status(struct ast_tcptls_session_instance *ser, int read_success)
{
	struct http_worker_private_data *request;

	request = ser->private_data;
	if (!ast_test_flag(&request->flags, HTTP_FLAG_HAS_BODY)
		|| ast_test_flag(&request->flags, HTTP_FLAG_BODY_READ)) {
		/* No body to read. */
		return;
	}
	ast_set_flag(&request->flags, HTTP_FLAG_BODY_READ);
	if (!read_success) {
		ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
	}
}

/*!
 * \internal
 * \brief Read the next length bytes from the HTTP body.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 * \param buf Where to put the contents reading.
 * \param length How much contents to read.
 * \param what_getting Name of the contents reading.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int http_body_read_contents(struct ast_tcptls_session_instance *ser, char *buf, int length, const char *what_getting)
{
	int res;

	/* Stay in fread until get all the expected data or timeout. */
	res = fread(buf, length, 1, ser->f);
	if (res < 1) {
		ast_log(LOG_WARNING, "Short HTTP request %s (Wanted %d)\n",
			what_getting, length);
		return -1;
	}
	return 0;
}

/*!
 * \internal
 * \brief Read and discard the next length bytes from the HTTP body.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 * \param length How much contents to discard
 * \param what_getting Name of the contents discarding.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int http_body_discard_contents(struct ast_tcptls_session_instance *ser, int length, const char *what_getting)
{
	int res;
	char buf[MAX_HTTP_LINE_LENGTH];/* Discard buffer */

	/* Stay in fread until get all the expected data or timeout. */
	while (sizeof(buf) < length) {
		res = fread(buf, sizeof(buf), 1, ser->f);
		if (res < 1) {
			ast_log(LOG_WARNING, "Short HTTP request %s (Wanted %zu of remaining %d)\n",
				what_getting, sizeof(buf), length);
			return -1;
		}
		length -= sizeof(buf);
	}
	res = fread(buf, length, 1, ser->f);
	if (res < 1) {
		ast_log(LOG_WARNING, "Short HTTP request %s (Wanted %d of remaining %d)\n",
			what_getting, length, length);
		return -1;
	}
	return 0;
}

/*!
 * \internal
 * \brief decode chunked mode hexadecimal value
 *
 * \param s string to decode
 * \param len length of string
 *
 * \retval length on success.
 * \retval -1 on error.
 */
static int chunked_atoh(const char *s, int len)
{
	int value = 0;
	char c;

	if (*s < '0') {
		/* zero value must be 0\n not just \n */
		return -1;
	}

	while (len--) {
		c = *s++;
		if (c == '\x0D') {
			return value;
		}
		if (c == ';') {
			/* We have a chunk-extension that we don't care about. */
			while (len--) {
				if (*s++ == '\x0D') {
					return value;
				}
			}
			break;
		}
		value <<= 4;
		if (c >= '0' && c <= '9') {
			value += c - '0';
			continue;
		}
		if (c >= 'a' && c <= 'f') {
			value += 10 + c - 'a';
			continue;
		}
		if (c >= 'A' && c <= 'F') {
			value += 10 + c - 'A';
			continue;
		}
		/* invalid character */
		return -1;
	}
	/* end of string */
	return -1;
}

/*!
 * \internal
 * \brief Read and convert the chunked body header length.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 *
 * \retval length Size of chunk to expect.
 * \retval -1 on error.
 */
static int http_body_get_chunk_length(struct ast_tcptls_session_instance *ser)
{
	int length;
	char header_line[MAX_HTTP_LINE_LENGTH];

	/* get the line of hexadecimal giving chunk-size w/ optional chunk-extension */
	if (!fgets(header_line, sizeof(header_line), ser->f)) {
		ast_log(LOG_WARNING, "Short HTTP read of chunked header\n");
		return -1;
	}
	length = chunked_atoh(header_line, strlen(header_line));
	if (length < 0) {
		ast_log(LOG_WARNING, "Invalid HTTP chunk size\n");
		return -1;
	}
	return length;
}

/*!
 * \internal
 * \brief Read and check the chunk contents line termination.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int http_body_check_chunk_sync(struct ast_tcptls_session_instance *ser)
{
	int res;
	char chunk_sync[2];

	/* Stay in fread until get the expected CRLF or timeout. */
	res = fread(chunk_sync, sizeof(chunk_sync), 1, ser->f);
	if (res < 1) {
		ast_log(LOG_WARNING, "Short HTTP chunk sync read (Wanted %zu)\n",
			sizeof(chunk_sync));
		return -1;
	}
	if (chunk_sync[0] != 0x0D || chunk_sync[1] != 0x0A) {
		ast_log(LOG_WARNING, "HTTP chunk sync bytes wrong (0x%02hhX, 0x%02hhX)\n",
			(unsigned char) chunk_sync[0], (unsigned char) chunk_sync[1]);
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Read and discard any chunked trailer entity-header lines.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int http_body_discard_chunk_trailer_headers(struct ast_tcptls_session_instance *ser)
{
	char header_line[MAX_HTTP_LINE_LENGTH];

	for (;;) {
		if (!fgets(header_line, sizeof(header_line), ser->f)) {
			ast_log(LOG_WARNING, "Short HTTP read of chunked trailer header\n");
			return -1;
		}

		/* Trim trailing whitespace */
		ast_trim_blanks(header_line);
		if (ast_strlen_zero(header_line)) {
			/* A blank line ends the chunked-body */
			break;
		}
	}
	return 0;
}

int ast_http_body_discard(struct ast_tcptls_session_instance *ser)
{
	struct http_worker_private_data *request;

	request = ser->private_data;
	if (!ast_test_flag(&request->flags, HTTP_FLAG_HAS_BODY)
		|| ast_test_flag(&request->flags, HTTP_FLAG_BODY_READ)) {
		/* No body to read or it has already been read. */
		return 0;
	}
	ast_set_flag(&request->flags, HTTP_FLAG_BODY_READ);

	ast_debug(1, "HTTP discarding unused request body\n");

	ast_assert(request->body_length != 0);
	if (0 < request->body_length) {
		if (http_body_discard_contents(ser, request->body_length, "body")) {
			ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
			return -1;
		}
		return 0;
	}

	/* parse chunked-body */
	for (;;) {
		int length;

		length = http_body_get_chunk_length(ser);
		if (length < 0) {
			ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
			return -1;
		}
		if (length == 0) {
			/* parsed last-chunk */
			break;
		}

		if (http_body_discard_contents(ser, length, "chunk-data")
			|| http_body_check_chunk_sync(ser)) {
			ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
			return -1;
		}
	}

	/* Read and discard any trailer entity-header lines. */
	if (http_body_discard_chunk_trailer_headers(ser)) {
		ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
		return -1;
	}
	return 0;
}

/*!
 * \brief Returns the contents (body) of the HTTP request
 *
 * \param return_length ptr to int that returns content length
 * \param ser HTTP TCP/TLS session object
 * \param headers List of HTTP headers
 * \return ptr to content (zero terminated) or NULL on failure
 * \note Since returned ptr is malloc'd, it should be free'd by caller
 */
static char *ast_http_get_contents(int *return_length,
	struct ast_tcptls_session_instance *ser, struct ast_variable *headers)
{
	struct http_worker_private_data *request;
	int content_length;
	int bufsize;
	char *buf;

	request = ser->private_data;
	if (!ast_test_flag(&request->flags, HTTP_FLAG_HAS_BODY)) {
		/* no content - not an error */
		return NULL;
	}
	if (ast_test_flag(&request->flags, HTTP_FLAG_BODY_READ)) {
		/* Already read the body.  Cannot read again.  Assume no content. */
		ast_assert(0);
		return NULL;
	}
	ast_set_flag(&request->flags, HTTP_FLAG_BODY_READ);

	ast_debug(2, "HTTP consuming request body\n");

	ast_assert(request->body_length != 0);
	if (0 < request->body_length) {
		/* handle regular non-chunked content */
		content_length = request->body_length;
		if (content_length > MAX_CONTENT_LENGTH) {
			ast_log(LOG_WARNING, "Excessively long HTTP content. (%d > %d)\n",
				content_length, MAX_CONTENT_LENGTH);
			ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
			errno = EFBIG;
			return NULL;
		}
		buf = ast_malloc(content_length + 1);
		if (!buf) {
			/* Malloc sets ENOMEM */
			ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
			return NULL;
		}

		if (http_body_read_contents(ser, buf, content_length, "body")) {
			ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
			errno = EIO;
			ast_free(buf);
			return NULL;
		}

		buf[content_length] = 0;
		*return_length = content_length;
		return buf;
	}

	/* pre-allocate buffer */
	bufsize = 250;
	buf = ast_malloc(bufsize);
	if (!buf) {
		ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
		return NULL;
	}

	/* parse chunked-body */
	content_length = 0;
	for (;;) {
		int chunk_length;

		chunk_length = http_body_get_chunk_length(ser);
		if (chunk_length < 0) {
			ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
			errno = EIO;
			ast_free(buf);
			return NULL;
		}
		if (chunk_length == 0) {
			/* parsed last-chunk */
			break;
		}
		if (content_length + chunk_length > MAX_CONTENT_LENGTH) {
			ast_log(LOG_WARNING,
				"Excessively long HTTP accumulated chunked body. (%d + %d > %d)\n",
				content_length, chunk_length, MAX_CONTENT_LENGTH);
			ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
			errno = EFBIG;
			ast_free(buf);
			return NULL;
		}

		/* insure buffer is large enough +1 */
		if (content_length + chunk_length >= bufsize) {
			char *new_buf;

			/* Increase bufsize until it can handle the expected data. */
			do {
				bufsize *= 2;
			} while (content_length + chunk_length >= bufsize);

			new_buf = ast_realloc(buf, bufsize);
			if (!new_buf) {
				ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
				ast_free(buf);
				return NULL;
			}
			buf = new_buf;
		}

		if (http_body_read_contents(ser, buf + content_length, chunk_length, "chunk-data")
			|| http_body_check_chunk_sync(ser)) {
			ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
			errno = EIO;
			ast_free(buf);
			return NULL;
		}
		content_length += chunk_length;
	}

	/*
	 * Read and discard any trailer entity-header lines
	 * which we don't care about.
	 *
	 * XXX In the future we may need to add the trailer headers
	 * to the passed in headers list rather than discarding them.
	 */
	if (http_body_discard_chunk_trailer_headers(ser)) {
		ast_set_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION);
		errno = EIO;
		ast_free(buf);
		return NULL;
	}

	buf[content_length] = 0;
	*return_length = content_length;
	return buf;
}

struct ast_json *ast_http_get_json(
	struct ast_tcptls_session_instance *ser, struct ast_variable *headers)
{
	int content_length = 0;
	struct ast_json *body;
	RAII_VAR(char *, buf, NULL, ast_free);
	RAII_VAR(char *, type, get_content_type(headers), ast_free);

	/* Use errno to distinguish errors from no body */
	errno = 0;

	if (ast_strlen_zero(type) || strcasecmp(type, "application/json")) {
		/* Content type is not JSON.  Don't read the body. */
		return NULL;
	}

	buf = ast_http_get_contents(&content_length, ser, headers);
	if (!buf || !content_length) {
		/*
		 * errno already set
		 * or it is not an error to have zero content
		 */
		return NULL;
	}

	body = ast_json_load_buf(buf, content_length, NULL);
	if (!body) {
		/* Failed to parse JSON; treat as an I/O error */
		errno = EIO;
		return NULL;
	}

	return body;
}

/*
 * get post variables from client Request Entity-Body, if content type is
 * application/x-www-form-urlencoded
 */
struct ast_variable *ast_http_get_post_vars(
	struct ast_tcptls_session_instance *ser, struct ast_variable *headers)
{
	int content_length = 0;
	struct ast_variable *v, *post_vars=NULL, *prev = NULL;
	char *var, *val;
	RAII_VAR(char *, buf, NULL, ast_free);
	RAII_VAR(char *, type, get_content_type(headers), ast_free);

	/* Use errno to distinguish errors from no params */
	errno = 0;

	if (ast_strlen_zero(type) ||
	    strcasecmp(type, "application/x-www-form-urlencoded")) {
		/* Content type is not form data.  Don't read the body. */
		return NULL;
	}

	buf = ast_http_get_contents(&content_length, ser, headers);
	if (!buf || !content_length) {
		/*
		 * errno already set
		 * or it is not an error to have zero content
		 */
		return NULL;
	}

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

	return post_vars;
}

static int handle_uri(struct ast_tcptls_session_instance *ser, char *uri,
	enum ast_http_method method, struct ast_variable *headers)
{
	char *c;
	int res = 0;
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

	AST_RWLIST_RDLOCK(&uri_redirects);
	AST_RWLIST_TRAVERSE(&uri_redirects, redirect, entry) {
		if (!strcasecmp(uri, redirect->target)) {
			struct ast_str *http_header = ast_str_create(128);

			if (!http_header) {
				ast_http_request_close_on_completion(ser);
				ast_http_error(ser, 500, "Server Error", "Out of memory");
				break;
			}
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
			l = strlen(urih->uri);
			c = uri + l;	/* candidate */
			ast_debug(2, "match request [%s] with handler [%s] len %d\n", uri, urih->uri, l);
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
		ast_debug(1, "Match made with [%s]\n", urih->uri);
		if (!urih->no_decode_uri) {
			ast_uri_decode(uri, ast_uri_http_legacy);
		}
		res = urih->callback(ser, urih, uri, method, get_vars, headers);
	} else {
		ast_debug(1, "Requested URI [%s] has no handler\n", uri);
		ast_http_error(ser, 404, "Not Found", "The requested URL was not found on this server.");
	}

cleanup:
	ast_variables_destroy(get_vars);
	return res;
}

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

static struct ast_http_auth *auth_create(const char *userid, const char *password)
{
	struct ast_http_auth *auth;
	size_t userid_len;
	size_t password_len;

	if (!userid || !password) {
		ast_log(LOG_ERROR, "Invalid userid/password\n");
		return NULL;
	}

	userid_len = strlen(userid) + 1;
	password_len = strlen(password) + 1;

	/* Allocate enough room to store everything in one memory block */
	auth = ao2_alloc(sizeof(*auth) + userid_len + password_len, NULL);
	if (!auth) {
		return NULL;
	}

	/* Put the userid right after the struct */
	auth->userid = (char *)(auth + 1);
	strcpy(auth->userid, userid);

	/* Put the password right after the userid */
	auth->password = auth->userid + userid_len;
	strcpy(auth->password, password);

	return auth;
}

#define BASIC_PREFIX "Basic "
#define BASIC_LEN 6 /*!< strlen(BASIC_PREFIX) */

struct ast_http_auth *ast_http_get_auth(struct ast_variable *headers)
{
	struct ast_variable *v;

	for (v = headers; v; v = v->next) {
		const char *base64;
		char decoded[256] = {};
		char *username;
		char *password;
#ifdef AST_DEVMODE
		int cnt;
#endif /* AST_DEVMODE */

		if (strcasecmp("Authorization", v->name) != 0) {
			continue;
		}

		if (!ast_begins_with(v->value, BASIC_PREFIX)) {
			ast_log(LOG_DEBUG,
				"Unsupported Authorization scheme\n");
			continue;
		}

		/* Basic auth header parsing. RFC 2617, section 2.
		 *   credentials = "Basic" basic-credentials
		 *   basic-credentials = base64-user-pass
		 *   base64-user-pass  = <base64 encoding of user-pass,
		 *                        except not limited to 76 char/line>
		 *   user-pass   = userid ":" password
		 */

		base64 = v->value + BASIC_LEN;

		/* This will truncate "userid:password" lines to
		 * sizeof(decoded). The array is long enough that this shouldn't
		 * be a problem */
#ifdef AST_DEVMODE
		cnt =
#endif /* AST_DEVMODE */
		ast_base64decode((unsigned char*)decoded, base64,
			sizeof(decoded) - 1);
		ast_assert(cnt < sizeof(decoded));

		/* Split the string at the colon */
		password = decoded;
		username = strsep(&password, ":");
		if (!password) {
			ast_log(LOG_WARNING, "Invalid Authorization header\n");
			return NULL;
		}

		return auth_create(username, password);
	}

	return NULL;
}

int ast_http_response_status_line(const char *buf, const char *version, int code)
{
	int status_code;
	size_t size = strlen(version);

	if (strncmp(buf, version, size) || buf[size] != ' ') {
		ast_log(LOG_ERROR, "HTTP version not supported - "
			"expected %s\n", version);
		return -1;
	}

	/* skip to status code (version + space) */
	buf += size + 1;

	if (sscanf(buf, "%d", &status_code) != 1) {
		ast_log(LOG_ERROR, "Could not read HTTP status code - "
			"%s\n", buf);
		return -1;
	}

	return status_code;
}

static void remove_excess_lws(char *s)
{
	char *p, *res = s;
	char *buf = ast_malloc(strlen(s) + 1);
	char *buf_end;

	if (!buf) {
		return;
	}

	buf_end = buf;

	while (*s && *(s = ast_skip_blanks(s))) {
		p = s;
		s = ast_skip_nonblanks(s);

		if (buf_end != buf) {
			*buf_end++ = ' ';
		}

		memcpy(buf_end, p, s - p);
		buf_end += s - p;
	}
	*buf_end = '\0';
	/* safe since buf will always be less than or equal to res */
	strcpy(res, buf);
	ast_free(buf);
}

int ast_http_header_parse(char *buf, char **name, char **value)
{
	ast_trim_blanks(buf);
	if (ast_strlen_zero(buf)) {
		return -1;
	}

	*value = buf;
	*name = strsep(value, ":");
	if (!*value) {
		return 1;
	}

	*value = ast_skip_blanks(*value);
	if (ast_strlen_zero(*value) || ast_strlen_zero(*name)) {
		return 1;
	}

	remove_excess_lws(*value);
	return 0;
}

int ast_http_header_match(const char *name, const char *expected_name,
			  const char *value, const char *expected_value)
{
	if (strcasecmp(name, expected_name)) {
		/* no value to validate if names don't match */
		return 0;
	}

	if (strcasecmp(value, expected_value)) {
		ast_log(LOG_ERROR, "Invalid header value - expected %s "
			"received %s", value, expected_value);
		return -1;
	}
	return 1;
}

int ast_http_header_match_in(const char *name, const char *expected_name,
			     const char *value, const char *expected_value)
{
	if (strcasecmp(name, expected_name)) {
		/* no value to validate if names don't match */
		return 0;
	}

	if (!strcasestr(expected_value, value)) {
		ast_log(LOG_ERROR, "Header '%s' - could not locate '%s' "
			"in '%s'\n", name, value, expected_value);
		return -1;

	}
	return 1;
}

/*! Limit the number of request headers in case the sender is being ridiculous. */
#define MAX_HTTP_REQUEST_HEADERS	100

/*!
 * \internal
 * \brief Read the request headers.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 * \param headers Where to put the request headers list pointer.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int http_request_headers_get(struct ast_tcptls_session_instance *ser, struct ast_variable **headers)
{
	struct ast_variable *tail = *headers;
	int remaining_headers;
	char header_line[MAX_HTTP_LINE_LENGTH];

	remaining_headers = MAX_HTTP_REQUEST_HEADERS;
	for (;;) {
		char *name;
		char *value;

		if (!fgets(header_line, sizeof(header_line), ser->f)) {
			ast_http_error(ser, 400, "Bad Request", "Timeout");
			return -1;
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
			return -1;
		}
		if (!*headers) {
			*headers = ast_variable_new(name, value, __FILE__);
			tail = *headers;
		} else {
			tail->next = ast_variable_new(name, value, __FILE__);
			tail = tail->next;
		}
		if (!tail) {
			/*
			 * Variable allocation failure.
			 * Try to make some room.
			 */
			ast_variables_destroy(*headers);
			*headers = NULL;

			ast_http_error(ser, 500, "Server Error", "Out of memory");
			return -1;
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief Process a HTTP request.
 * \since 12.4.0
 *
 * \param ser HTTP TCP/TLS session object.
 *
 * \retval 0 Continue and process the next HTTP request.
 * \retval -1 Fatal HTTP connection error.  Force the HTTP connection closed.
 */
static int httpd_process_request(struct ast_tcptls_session_instance *ser)
{
	RAII_VAR(struct ast_variable *, headers, NULL, ast_variables_destroy);
	char *uri;
	char *method;
	const char *transfer_encoding;
	struct http_worker_private_data *request;
	enum ast_http_method http_method = AST_HTTP_UNKNOWN;
	int res;
	char request_line[MAX_HTTP_LINE_LENGTH];

	if (!fgets(request_line, sizeof(request_line), ser->f)) {
		return -1;
	}

	/* Re-initialize the request body tracking data. */
	request = ser->private_data;
	http_request_tracking_init(request);

	/* Get method */
	method = ast_skip_blanks(request_line);
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
	} else if (!strcasecmp(method,"DELETE")) {
		http_method = AST_HTTP_DELETE;
	} else if (!strcasecmp(method,"OPTIONS")) {
		http_method = AST_HTTP_OPTIONS;
	}

	uri = ast_skip_blanks(uri);	/* Skip white space */
	if (*uri) {			/* terminate at the first blank */
		char *c = ast_skip_nonblanks(uri);

		if (*c) {
			*c = '\0';
		}
	} else {
		ast_http_error(ser, 400, "Bad Request", "Invalid Request");
		return -1;
	}

	if (ast_shutdown_final()) {
		ast_http_error(ser, 503, "Service Unavailable", "Shutdown in progress");
		return -1;
	}

	/* process "Request Headers" lines */
	if (http_request_headers_get(ser, &headers)) {
		return -1;
	}

	transfer_encoding = get_transfer_encoding(headers);
	/* Transfer encoding defaults to identity */
	if (!transfer_encoding) {
		transfer_encoding = "identity";
	}

	/*
	 * RFC 2616, section 3.6, we should respond with a 501 for any transfer-
	 * codings we don't understand.
	 */
	if (strcasecmp(transfer_encoding, "identity") != 0 &&
		strcasecmp(transfer_encoding, "chunked") != 0) {
		/* Transfer encodings not supported */
		ast_http_error(ser, 501, "Unimplemented", "Unsupported Transfer-Encoding.");
		return -1;
	}

	if (http_request_tracking_setup(ser, headers)
		|| handle_uri(ser, uri, http_method, headers)
		|| ast_test_flag(&request->flags, HTTP_FLAG_CLOSE_ON_COMPLETION)) {
		res = -1;
	} else {
		res = 0;
	}
	return res;
}

static void *httpd_helper_thread(void *data)
{
	struct ast_tcptls_session_instance *ser = data;
	struct protoent *p;
	int flags;
	int timeout;

	if (!ser || !ser->f) {
		ao2_cleanup(ser);
		return NULL;
	}

	if (ast_atomic_fetchadd_int(&session_count, +1) >= session_limit) {
		ast_log(LOG_WARNING, "HTTP session count exceeded %d sessions.\n",
			session_limit);
		goto done;
	}
	ast_debug(1, "HTTP opening session.  Top level\n");

	/*
	 * Here we set TCP_NODELAY on the socket to disable Nagle's algorithm.
	 * This is necessary to prevent delays (caused by buffering) as we
	 * write to the socket in bits and pieces.
	 */
	p = getprotobyname("tcp");
	if (p) {
		int arg = 1;

		if (setsockopt(ser->fd, p->p_proto, TCP_NODELAY, (char *) &arg, sizeof(arg) ) < 0) {
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

	/* Setup HTTP worker private data to keep track of request body reading. */
	ao2_cleanup(ser->private_data);
	ser->private_data = ao2_alloc_options(sizeof(struct http_worker_private_data), NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!ser->private_data) {
		ast_http_error(ser, 500, "Server Error", "Out of memory");
		goto done;
	}
	http_request_tracking_init(ser->private_data);

	/* Determine initial HTTP request wait timeout. */
	timeout = session_keep_alive;
	if (timeout <= 0) {
		/* Persistent connections not enabled. */
		timeout = session_inactivity;
	}
	if (timeout < MIN_INITIAL_REQUEST_TIMEOUT) {
		timeout = MIN_INITIAL_REQUEST_TIMEOUT;
	}

	/* We can let the stream wait for data to arrive. */
	ast_tcptls_stream_set_exclusive_input(ser->stream_cookie, 1);

	for (;;) {
		int ch;

		/* Wait for next potential HTTP request message. */
		ast_tcptls_stream_set_timeout_inactivity(ser->stream_cookie, timeout);
		ch = fgetc(ser->f);
		if (ch == EOF || ungetc(ch, ser->f) == EOF) {
			/* Between request idle timeout */
			ast_debug(1, "HTTP idle timeout or peer closed connection.\n");
			break;
		}

		ast_tcptls_stream_set_timeout_inactivity(ser->stream_cookie, session_inactivity);
		if (httpd_process_request(ser) || !ser->f || feof(ser->f)) {
			/* Break the connection or the connection closed */
			break;
		}

		timeout = session_keep_alive;
		if (timeout <= 0) {
			/* Persistent connections not enabled. */
			break;
		}
	}

done:
	ast_atomic_fetchadd_int(&session_count, -1);

	if (ser->f) {
		ast_debug(1, "HTTP closing session.  Top level\n");
		ast_tcptls_close_session_file(ser);
	}
	ao2_ref(ser, -1);
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
	char server_name[MAX_SERVER_NAME_LENGTH];
	struct http_uri_redirect *redirect;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	uint32_t bindport = DEFAULT_PORT;
	RAII_VAR(struct ast_sockaddr *, addrs, NULL, ast_free);
	int num_addrs = 0;
	int http_tls_was_enabled = 0;

	cfg = ast_config_load2("http.conf", "http", config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
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

	if (http_tls_cfg.cipher) {
		ast_free(http_tls_cfg.cipher);
	}
	http_tls_cfg.cipher = ast_strdup("");

	AST_RWLIST_WRLOCK(&uri_redirects);
	while ((redirect = AST_RWLIST_REMOVE_HEAD(&uri_redirects, entry))) {
		ast_free(redirect);
	}
	AST_RWLIST_UNLOCK(&uri_redirects);

	ast_sockaddr_setnull(&https_desc.local_address);

	session_limit = DEFAULT_SESSION_LIMIT;
	session_inactivity = DEFAULT_SESSION_INACTIVITY;
	session_keep_alive = DEFAULT_SESSION_KEEP_ALIVE;

	snprintf(server_name, sizeof(server_name), "Asterisk/%s", ast_get_version());

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
			&& strcasecmp(v->name, "tlscipher")
			&& strcasecmp(v->name, "sslcipher")
			&& !ast_tls_read_conf(&http_tls_cfg, &https_desc, v->name, v->value)) {
			continue;
		}

		if (!strcasecmp(v->name, "servername")) {
			if (!ast_strlen_zero(v->value)) {
				ast_copy_string(server_name, v->value, sizeof(server_name));
			} else {
				server_name[0] = '\0';
			}
		} else if (!strcasecmp(v->name, "enabled")) {
			enabled = ast_true(v->value);
		} else if (!strcasecmp(v->name, "enablestatic")) {
			newenablestatic = ast_true(v->value);
		} else if (!strcasecmp(v->name, "bindport")) {
			if (ast_parse_arg(v->value, PARSE_UINT32 | PARSE_IN_RANGE | PARSE_DEFAULT,
				&bindport, DEFAULT_PORT, 0, 65535)) {
				ast_log(LOG_WARNING, "Invalid port %s specified. Using default port %" PRId32 "\n",
					v->value, DEFAULT_PORT);
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
			if (ast_parse_arg(v->value, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE,
				&session_limit, DEFAULT_SESSION_LIMIT, 1, INT_MAX)) {
				ast_log(LOG_WARNING, "Invalid %s '%s' at line %d of http.conf\n",
					v->name, v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "session_inactivity")) {
			if (ast_parse_arg(v->value, PARSE_INT32 | PARSE_DEFAULT | PARSE_IN_RANGE,
				&session_inactivity, DEFAULT_SESSION_INACTIVITY, 1, INT_MAX)) {
				ast_log(LOG_WARNING, "Invalid %s '%s' at line %d of http.conf\n",
					v->name, v->value, v->lineno);
			}
		} else if (!strcasecmp(v->name, "session_keep_alive")) {
			if (sscanf(v->value, "%30d", &session_keep_alive) != 1
				|| session_keep_alive < 0) {
				session_keep_alive = DEFAULT_SESSION_KEEP_ALIVE;
				ast_log(LOG_WARNING, "Invalid %s '%s' at line %d of http.conf\n",
					v->name, v->value, v->lineno);
			}
		} else {
			ast_log(LOG_WARNING, "Ignoring unknown option '%s' in http.conf\n", v->name);
		}
	}

	ast_config_destroy(cfg);

	if (strcmp(prefix, newprefix)) {
		ast_copy_string(prefix, newprefix, sizeof(prefix));
	}

	ast_copy_string(http_server_name, server_name, sizeof(http_server_name));
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
	ast_cli(a->fd, "Server: %s\n", http_server_name);
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
	ast_register_atexit(http_shutdown);

	return __ast_http_load(0);
}
