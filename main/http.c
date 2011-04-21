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

#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/signal.h>
#include <fcntl.h>

#include "asterisk/paths.h"	/* use ast_config_AST_DATA_DIR */
#include "asterisk/network.h"
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

#define MAX_PREFIX 80
#define DEFAULT_SESSION_LIMIT 100

/* See http.h for more information about the SSL implementation */
#if defined(HAVE_OPENSSL) && (defined(HAVE_FUNOPEN) || defined(HAVE_FOPENCOOKIE))
#define	DO_SSL	/* comment in/out if you want to support ssl */
#endif

static int session_limit = DEFAULT_SESSION_LIMIT;
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
	{ "jpg", "image/jpeg" },
	{ "js", "application/x-javascript" },
	{ "wav", "audio/x-wav" },
	{ "mp3", "audio/mpeg" },
	{ "svg", "image/svg+xml" },
	{ "svgz", "image/svg+xml" },
	{ "gif", "image/gif" },
};

struct http_uri_redirect {
	AST_LIST_ENTRY(http_uri_redirect) entry;
	char *dest;
	char target[0];
};

static AST_RWLIST_HEAD_STATIC(uri_redirects, http_uri_redirect);

static const char *ftype2mtype(const char *ftype, char *wkspace, int wkspacelen)
{
	int x;

	if (ftype) {
		for (x = 0; x < ARRAY_LEN(mimetypes); x++) {
			if (!strcasecmp(ftype, mimetypes[x].ext)) {
				return mimetypes[x].mtype;
			}
		}
	}

	snprintf(wkspace, wkspacelen, "text/%s", S_OR(ftype, "plain"));

	return wkspace;
}

static uint32_t manid_from_vars(struct ast_variable *sid) {
	uint32_t mngid;

	while (sid && strcmp(sid->name, "mansession_id"))
		sid = sid->next;

	if (!sid || sscanf(sid->value, "%30x", &mngid) != 1)
		return 0;

	return mngid;
}

void ast_http_prefix(char *buf, int len)
{
	if (buf) {
		ast_copy_string(buf, prefix, len);
	}
}

static struct ast_str *static_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *vars, struct ast_variable *headers, int *status, char **title, int *contentlength)
{
	char *path;
	char *ftype;
	const char *mtype;
	char wkspace[80];
	struct stat st;
	int len;
	int fd;
	struct timeval now = ast_tvnow();
	char buf[256];
	struct ast_tm tm;

	/* Yuck.  I'm not really sold on this, but if you don't deliver static content it makes your configuration 
	   substantially more challenging, but this seems like a rather irritating feature creep on Asterisk. */
	if (!enablestatic || ast_strlen_zero(uri)) {
		goto out403;
	}

	/* Disallow any funny filenames at all */
	if ((uri[0] < 33) || strchr("./|~@#$%^&*() \t", uri[0])) {
		goto out403;
	}

	if (strstr(uri, "/..")) {
		goto out403;
	}
		
	if ((ftype = strrchr(uri, '.'))) {
		ftype++;
	}

	mtype = ftype2mtype(ftype, wkspace, sizeof(wkspace));
	
	/* Cap maximum length */
	if ((len = strlen(uri) + strlen(ast_config_AST_DATA_DIR) + strlen("/static-http/") + 5) > 1024) {
		goto out403;
	}
		
	path = alloca(len);
	sprintf(path, "%s/static-http/%s", ast_config_AST_DATA_DIR, uri);
	if (stat(path, &st)) {
		goto out404;
	}

	if (S_ISDIR(st.st_mode)) {
		goto out404;
	}	

	if ((fd = open(path, O_RDONLY)) < 0) {
		goto out403;
	}

	if (strstr(path, "/private/") && !astman_is_authed(manid_from_vars(vars))) {
		goto out403;
	}

	ast_strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %Z", ast_localtime(&now, &tm, "GMT"));
	fprintf(ser->f, "HTTP/1.1 200 OK\r\n"
		"Server: Asterisk/%s\r\n"
		"Date: %s\r\n"
		"Connection: close\r\n"
		"Cache-Control: private\r\n"
		"Content-Length: %d\r\n"
		"Content-type: %s\r\n\r\n",
		ast_get_version(), buf, (int) st.st_size, mtype);

	while ((len = read(fd, buf, sizeof(buf))) > 0) {
		if (fwrite(buf, 1, len, ser->f) != len) {
			ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
		}
	}

	close(fd);

	return NULL;

out404:
	return ast_http_error((*status = 404),
			      (*title = ast_strdup("Not Found")),
			       NULL, "The requested URL was not found on this server.");

out403:
	return ast_http_error((*status = 403),
			      (*title = ast_strdup("Access Denied")),
			      NULL, "You do not have permission to access the requested URL.");
}


static struct ast_str *httpstatus_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *vars, struct ast_variable *headers, int *status, char **title, int *contentlength)
{
	struct ast_str *out = ast_str_create(512);
	struct ast_variable *v;

	if (out == NULL) {
		return out;
	}

	ast_str_append(&out, 0,
		       "\r\n"
		       "<title>Asterisk HTTP Status</title>\r\n"
		       "<body bgcolor=\"#ffffff\">\r\n"
		       "<table bgcolor=\"#f1f1f1\" align=\"center\"><tr><td bgcolor=\"#e0e0ff\" colspan=\"2\" width=\"500\">\r\n"
		       "<h2>&nbsp;&nbsp;Asterisk&trade; HTTP Status</h2></td></tr>\r\n");
	ast_str_append(&out, 0, "<tr><td><i>Prefix</i></td><td><b>%s</b></td></tr>\r\n", prefix);
	ast_str_append(&out, 0, "<tr><td><i>Bind Address</i></td><td><b>%s</b></td></tr>\r\n",
		       ast_inet_ntoa(http_desc.old_address.sin_addr));
	ast_str_append(&out, 0, "<tr><td><i>Bind Port</i></td><td><b>%d</b></td></tr>\r\n",
		       ntohs(http_desc.old_address.sin_port));

	if (http_tls_cfg.enabled) {
		ast_str_append(&out, 0, "<tr><td><i>SSL Bind Port</i></td><td><b>%d</b></td></tr>\r\n",
			       ntohs(https_desc.old_address.sin_port));
	}

	ast_str_append(&out, 0, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");

	for (v = vars; v; v = v->next) {
		if (strncasecmp(v->name, "cookie_", 7)) {
			ast_str_append(&out, 0, "<tr><td><i>Submitted Variable '%s'</i></td><td>%s</td></tr>\r\n", v->name, v->value);
		}
	}

	ast_str_append(&out, 0, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");

	for (v = vars; v; v = v->next) {
		if (!strncasecmp(v->name, "cookie_", 7)) {
			ast_str_append(&out, 0, "<tr><td><i>Cookie '%s'</i></td><td>%s</td></tr>\r\n", v->name, v->value);
		}
	}

	ast_str_append(&out, 0, "</table><center><font size=\"-1\"><i>Asterisk and Digium are registered trademarks of Digium, Inc.</i></font></center></body>\r\n");
	return out;
}

static struct ast_http_uri statusuri = {
	.callback = httpstatus_callback,
	.description = "Asterisk HTTP General Status",
	.uri = "httpstatus",
	.supports_get = 1,
	.data = NULL,
	.key = __FILE__,
};
	
static struct ast_http_uri staticuri = {
	.callback = static_callback,
	.description = "Asterisk HTTP Static Delivery",
	.uri = "static",
	.has_subtree = 1,
	.static_content = 1,
	.supports_get = 1,
	.data = NULL,
	.key= __FILE__,
};
	
struct ast_str *ast_http_error(int status, const char *title, const char *extra_header, const char *text)
{
	struct ast_str *out = ast_str_create(512);

	if (out == NULL) {
		return out;
	}

	ast_str_set(&out, 0,
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

	return out;
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

	if (!(urih->supports_get || urih->supports_post)) {
		ast_log(LOG_WARNING, "URI handler does not provide either GET or POST method: %s (%s)\n", urih->uri, urih->description);
		return -1;
	}

	AST_RWLIST_WRLOCK(&uris);

	if (AST_RWLIST_EMPTY(&uris) || strlen(AST_RWLIST_FIRST(&uris)->uri) <= len) {
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
		}
		if (urih->dmallocd) {
			ast_free(urih->data);
		}
		if (urih->mallocd) {
			ast_free(urih);
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&uris);
}

/*
 * Decode special characters in http uri.
 * We have ast_uri_decode to handle %XX sequences, but spaces
 * are encoded as a '+' so we need to replace them beforehand.
 */
static void http_decode(char *s)
{
	char *t;
	
	for (t = s; *t; t++) {
		if (*t == '+')
			*t = ' ';
	}

	ast_uri_decode(s);
}

static struct ast_str *handle_uri(struct ast_tcptls_session_instance *ser, char *uri, enum ast_http_method method,
				  int *status, char **title, int *contentlength, struct ast_variable **cookies, struct ast_variable *headers, 
				  unsigned int *static_content)
{
	char *c;
	struct ast_str *out = NULL;
	char *params = uri;
	struct ast_http_uri *urih = NULL;
	int l;
	struct ast_variable *vars = NULL, *v, *prev = NULL;
	struct http_uri_redirect *redirect;
	int saw_method = 0;

	/* preserve previous behavior of only support URI parameters on GET requests */
	if (method == AST_HTTP_GET) {
		strsep(&params, "?");
		
		/* Extract arguments from the request and store them in variables.
		 * Note that a request can have multiple arguments with the same
		 * name, and we store them all in the list of variables.
		 * It is up to the application to handle multiple values.
		 */
		if (params) {
			char *var, *val;
			
			while ((val = strsep(&params, "&"))) {
				var = strsep(&val, "=");
				if (val) {
					http_decode(val);
				} else {
					val = "";
				}
				http_decode(var);
				if ((v = ast_variable_new(var, val, ""))) {
					if (vars) {
						prev->next = v;
					} else {
						vars = v;
					}
					prev = v;
				}
			}
		}
	}

	/*
	 * Append the cookies to the list of variables.
	 * This saves a pass in the cookies list, but has the side effect
	 * that a variable might mask a cookie with the same name if the
	 * application stops at the first match.
	 * Note that this is the same behaviour as $_REQUEST variables in PHP.
	 */
	if (prev) {
		prev->next = *cookies;
	} else {
		vars = *cookies;
	}
	*cookies = NULL;

	http_decode(uri);

	AST_RWLIST_RDLOCK(&uri_redirects);
	AST_RWLIST_TRAVERSE(&uri_redirects, redirect, entry) {
		if (!strcasecmp(uri, redirect->target)) {
			char buf[512];

			snprintf(buf, sizeof(buf), "Location: %s\r\n", redirect->dest);
			out = ast_http_error((*status = 302),
					     (*title = ast_strdup("Moved Temporarily")),
					     buf, "Redirecting...");

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
			if (!saw_method) {
				switch (method) {
				case AST_HTTP_GET:
					if (urih->supports_get) {
						saw_method = 1;
					}
					break;
				case AST_HTTP_POST:
					if (urih->supports_post) {
						saw_method = 1;
					}
					break;
				}
			}

			l = strlen(urih->uri);
			c = uri + l;	/* candidate */

			if (strncasecmp(urih->uri, uri, l) || /* no match */
			    (*c && *c != '/')) { /* substring */
				continue;
			}

			if (*c == '/') {
				c++;
			}

			if (!*c || urih->has_subtree) {
				if (((method == AST_HTTP_GET) && urih->supports_get) ||
				    ((method == AST_HTTP_POST) && urih->supports_post)) {
					uri = c;

					break;
				}
			}
		}

		if (!urih) {
			AST_RWLIST_UNLOCK(&uris);
		}
	}

	if (method == AST_HTTP_POST && !astman_is_authed(manid_from_vars(vars))) {
		out = ast_http_error((*status = 403),
			      (*title = ast_strdup("Access Denied")),
			      NULL, "You do not have permission to access the requested URL.");
	} else if (urih) {
		*static_content = urih->static_content;
		out = urih->callback(ser, urih, uri, method, vars, headers, status, title, contentlength);
		AST_RWLIST_UNLOCK(&uris);
	} else if (saw_method) {
		out = ast_http_error((*status = 404),
				     (*title = ast_strdup("Not Found")), NULL,
				     "The requested URL was not found on this server.");
	} else {
		out = ast_http_error((*status = 501),
				     (*title = ast_strdup("Not Implemented")), NULL,
				     "Attempt to use unimplemented / unsupported method");
	}

cleanup:
	ast_variables_destroy(vars);

	return out;
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
	char *s = alloca(len+1);
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

		var = ast_variable_new(name, val, __FILE__);
		var->next = vars;
		vars = var;
	}

	return vars;
}

static void *httpd_helper_thread(void *data)
{
	char buf[4096];
	char cookie[4096];
	struct ast_tcptls_session_instance *ser = data;
	struct ast_variable *vars=NULL, *headers = NULL;
	char *uri, *title=NULL;
	int status = 200, contentlength = 0;
	struct ast_str *out = NULL;
	unsigned int static_content = 0;
	struct ast_variable *tail = headers;

	if (ast_atomic_fetchadd_int(&session_count, +1) >= session_limit) {
		goto done;
	}

	if (!fgets(buf, sizeof(buf), ser->f)) {
		goto done;
	}

	uri = ast_skip_nonblanks(buf);	/* Skip method */
	if (*uri) {
		*uri++ = '\0';
	}

	uri = ast_skip_blanks(uri);	/* Skip white space */

	if (*uri) {			/* terminate at the first blank */
		char *c = ast_skip_nonblanks(uri);

		if (*c) {
			*c = '\0';
		}
	}

	/* process "Cookie: " lines */
	while (fgets(cookie, sizeof(cookie), ser->f)) {
		/* Trim trailing characters */
		ast_trim_blanks(cookie);
		if (ast_strlen_zero(cookie)) {
			break;
		}
		if (!strncasecmp(cookie, "Cookie: ", 8)) {
			vars = parse_cookies(cookie);
		} else {
			char *name, *val;

			val = cookie;
			name = strsep(&val, ":");
			if (ast_strlen_zero(name) || ast_strlen_zero(val)) {
				continue;
			}
			ast_trim_blanks(name);
			val = ast_skip_blanks(val);

			if (!headers) {
				headers = ast_variable_new(name, val, __FILE__);
				tail = headers;
			} else {
				tail->next = ast_variable_new(name, val, __FILE__);
				tail = tail->next;
			}
		}
	}

	if (!*uri) {
		out = ast_http_error(400, "Bad Request", NULL, "Invalid Request");
	} else if (strcasecmp(buf, "post") && strcasecmp(buf, "get")) {
		out = ast_http_error(501, "Not Implemented", NULL,
				     "Attempt to use unimplemented / unsupported method");
	} else {	/* try to serve it */
		out = handle_uri(ser, uri, (!strcasecmp(buf, "get")) ? AST_HTTP_GET : AST_HTTP_POST,
				 &status, &title, &contentlength, &vars, headers, &static_content);
	}

	/* If they aren't mopped up already, clean up the cookies */
	if (vars) {
		ast_variables_destroy(vars);
	}
	/* Clean up all the header information pulled as well */
	if (headers) {
		ast_variables_destroy(headers);
	}

	if (out) {
		struct timeval now = ast_tvnow();
		char timebuf[256];
		struct ast_tm tm;

		ast_strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S %Z", ast_localtime(&now, &tm, "GMT"));
		fprintf(ser->f,
			"HTTP/1.1 %d %s\r\n"
			"Server: Asterisk/%s\r\n"
			"Date: %s\r\n"
			"Connection: close\r\n"
			"%s",
			status, title ? title : "OK", ast_get_version(), timebuf,
			static_content ? "" : "Cache-Control: no-cache, no-store\r\n");
			/* We set the no-cache headers only for dynamic content.
			* If you want to make sure the static file you requested is not from cache,
			* append a random variable to your GET request.  Ex: 'something.html?r=109987734'
			*/
		if (!contentlength) {	/* opaque body ? just dump it hoping it is properly formatted */
			fprintf(ser->f, "%s", ast_str_buffer(out));
		} else {
			char *tmp = strstr(ast_str_buffer(out), "\r\n\r\n");

			if (tmp) {
				fprintf(ser->f, "Content-length: %d\r\n", contentlength);
				/* first write the header, then the body */
				if (fwrite(ast_str_buffer(out), 1, (tmp + 4 - ast_str_buffer(out)), ser->f) != tmp + 4 - ast_str_buffer(out)) {
					ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
				}
				if (fwrite(tmp + 4, 1, contentlength, ser->f) != contentlength ) {
					ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
				}
			}
		}
		ast_free(out);
	}

	if (title) {
		ast_free(title);
	}

done:
	ast_atomic_fetchadd_int(&session_count, -1);
	fclose(ser->f);
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
	    || strlen(AST_RWLIST_FIRST(&uri_redirects)->target) <= target_len) {
		AST_RWLIST_INSERT_HEAD(&uri_redirects, redirect, entry);
		AST_RWLIST_UNLOCK(&uri_redirects);

		return;
	}

	AST_RWLIST_TRAVERSE(&uri_redirects, cur, entry) {
		if (AST_RWLIST_NEXT(cur, entry) 
		    && strlen(AST_RWLIST_NEXT(cur, entry)->target) <= target_len) {
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
	struct hostent *hp;
	struct ast_hostent ahp;
	char newprefix[MAX_PREFIX] = "";
	int have_sslbindaddr = 0;
	struct http_uri_redirect *redirect;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	cfg = ast_config_load2("http.conf", "http", config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
		return 0;
	}

	/* default values */
	memset(&http_desc.local_address, 0, sizeof(http_desc.local_address));
	http_desc.local_address.sin_port = htons(8088);

	memset(&https_desc.local_address, 0, sizeof(https_desc.local_address));
	https_desc.local_address.sin_port = htons(8089);

	http_tls_cfg.enabled = 0;
	if (http_tls_cfg.certfile) {
		ast_free(http_tls_cfg.certfile);
	}
	http_tls_cfg.certfile = ast_strdup(AST_CERTFILE);
	if (http_tls_cfg.cipher) {
		ast_free(http_tls_cfg.cipher);
	}
	http_tls_cfg.cipher = ast_strdup("");

	AST_RWLIST_WRLOCK(&uri_redirects);
	while ((redirect = AST_RWLIST_REMOVE_HEAD(&uri_redirects, entry))) {
		ast_free(redirect);
	}
	AST_RWLIST_UNLOCK(&uri_redirects);

	if (cfg) {
		v = ast_variable_browse(cfg, "general");
		for (; v; v = v->next) {
			if (!strcasecmp(v->name, "enabled")) {
				enabled = ast_true(v->value);
			} else if (!strcasecmp(v->name, "sslenable")) {
				http_tls_cfg.enabled = ast_true(v->value);
			} else if (!strcasecmp(v->name, "sslbindport")) {
				https_desc.local_address.sin_port = htons(atoi(v->value));
			} else if (!strcasecmp(v->name, "sslcert")) {
				ast_free(http_tls_cfg.certfile);
				http_tls_cfg.certfile = ast_strdup(v->value);
			} else if (!strcasecmp(v->name, "sslcipher")) {
				ast_free(http_tls_cfg.cipher);
				http_tls_cfg.cipher = ast_strdup(v->value);
			} else if (!strcasecmp(v->name, "enablestatic")) {
				newenablestatic = ast_true(v->value);
			} else if (!strcasecmp(v->name, "bindport")) {
				http_desc.local_address.sin_port = htons(atoi(v->value));
			} else if (!strcasecmp(v->name, "sslbindaddr")) {
				if ((hp = ast_gethostbyname(v->value, &ahp))) {
					memcpy(&https_desc.local_address.sin_addr, hp->h_addr, sizeof(https_desc.local_address.sin_addr));
					have_sslbindaddr = 1;
				} else {
					ast_log(LOG_WARNING, "Invalid bind address '%s'\n", v->value);
				}
			} else if (!strcasecmp(v->name, "bindaddr")) {
				if ((hp = ast_gethostbyname(v->value, &ahp))) {
					memcpy(&http_desc.local_address.sin_addr, hp->h_addr, sizeof(http_desc.local_address.sin_addr));
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
			} else if (!strcasecmp(v->name, "redirect")) {
				add_redirect(v->value);
			} else if (!strcasecmp(v->name, "sessionlimit")) {
				if (ast_parse_arg(v->value, PARSE_INT32|PARSE_DEFAULT|PARSE_IN_RANGE,
							&session_limit, DEFAULT_SESSION_LIMIT, 1, INT_MAX)) {
					ast_log(LOG_WARNING, "Invalid %s '%s' at line %d of http.conf\n",
							v->name, v->value, v->lineno);
				}
			} else {
				ast_log(LOG_WARNING, "Ignoring unknown option '%s' in http.conf\n", v->name);
			}
		}

		ast_config_destroy(cfg);
	}

	if (!have_sslbindaddr) {
		https_desc.local_address.sin_addr = http_desc.local_address.sin_addr;
	}
	if (enabled) {
		http_desc.local_address.sin_family = https_desc.local_address.sin_family = AF_INET;
	}
	if (strcmp(prefix, newprefix)) {
		ast_copy_string(prefix, newprefix, sizeof(prefix));
	}
	enablestatic = newenablestatic;
	ast_tcptls_server_start(&http_desc);
	if (ast_ssl_setup(https_desc.tls_cfg)) {
		ast_tcptls_server_start(&https_desc);
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
	if (!http_desc.old_address.sin_family) {
		ast_cli(a->fd, "Server Disabled\n\n");
	} else {
		ast_cli(a->fd, "Server Enabled and Bound to %s:%d\n\n",
			ast_inet_ntoa(http_desc.old_address.sin_addr),
			ntohs(http_desc.old_address.sin_port));
		if (http_tls_cfg.enabled) {
			ast_cli(a->fd, "HTTPS Server Enabled and Bound to %s:%d\n\n",
				ast_inet_ntoa(https_desc.old_address.sin_addr),
				ntohs(https_desc.old_address.sin_port));
		}
	}

	ast_cli(a->fd, "Enabled URI's:\n");
	AST_RWLIST_RDLOCK(&uris);
	if (AST_RWLIST_EMPTY(&uris)) {
		ast_cli(a->fd, "None.\n");
	} else {
		AST_RWLIST_TRAVERSE(&uris, urih, entry) {
			ast_cli(a->fd, "%s/%s%s => %s\n", prefix, urih->uri, (urih->has_subtree ? "/..." : ""), urih->description);
		}
	}
	AST_RWLIST_UNLOCK(&uris);

	ast_cli(a->fd, "\nEnabled Redirects:\n");
	AST_RWLIST_RDLOCK(&uri_redirects);
	AST_RWLIST_TRAVERSE(&uri_redirects, redirect, entry) {
		ast_cli(a->fd, "  %s => %s\n", redirect->target, redirect->dest);
	}
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

int ast_http_init(void)
{
	ast_http_uri_link(&statusuri);
	ast_http_uri_link(&staticuri);
	ast_cli_register_multiple(cli_http, ARRAY_LEN(cli_http));

	return __ast_http_load(0);
}
