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
 * This program implements a tiny http server supporting the "get" method
 * only and was inspired by micro-httpd by Jef Poskanzer 
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

#include "asterisk/cli.h"
#include "asterisk/http.h"
#include "asterisk/utils.h"
#include "asterisk/strings.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/stringfields.h"

#define MAX_PREFIX 80
#define DEFAULT_PREFIX "/asterisk"

/*!
 * In order to have TLS/SSL support, we need the openssl libraries.
 * Still we can decide whether or not to use them by commenting
 * in or out the DO_SSL macro.
 * TLS/SSL support is basically implemented by reading from a config file
 * (currently http.conf) the names of the certificate and cipher to use,
 * and then run ssl_setup() to create an appropriate SSL_CTX (ssl_ctx)
 * If we support multiple domains, presumably we need to read multiple
 * certificates.
 * When we are requested to open a TLS socket, we run make_file_from_fd()
 * on the socket, to do the necessary setup. At the moment the context's name
 * is hardwired in the function, but we can certainly make it into an extra
 * parameter to the function.
 *
 * We declare most of ssl support variables unconditionally,
 * because their number is small and this simplifies the code.
 */

#if defined(HAVE_OPENSSL) && (defined(HAVE_FUNOPEN) || defined(HAVE_FOPENCOOKIE))
#define	DO_SSL	/* comment in/out if you want to support ssl */
#endif

static struct tls_config http_tls_cfg;

static void *httpd_helper_thread(void *arg);

/*!
 * we have up to two accepting threads, one for http, one for https
 */
static struct server_args http_desc = {
	.accept_fd = -1,
	.master = AST_PTHREADT_NULL,
	.tls_cfg = NULL,
	.poll_timeout = -1,
	.name = "http server",
	.accept_fn = server_root,
	.worker_fn = httpd_helper_thread,
};

static struct server_args https_desc = {
	.accept_fd = -1,
	.master = AST_PTHREADT_NULL,
	.tls_cfg = &http_tls_cfg,
	.poll_timeout = -1,
	.name = "https server",
	.accept_fn = server_root,
	.worker_fn = httpd_helper_thread,
};

static AST_LIST_HEAD_STATIC(uris, ast_http_uri);	/*!< list of supported handlers */

/* all valid URIs must be prepended by the string in prefix. */
static char prefix[MAX_PREFIX];
static int enablestatic=0;

/*! \brief Limit the kinds of files we're willing to serve up */
static struct {
	char *ext;
	char *mtype;
} mimetypes[] = {
	{ "png", "image/png" },
	{ "jpg", "image/jpeg" },
	{ "js", "application/x-javascript" },
	{ "wav", "audio/x-wav" },
	{ "mp3", "audio/mpeg" },
};

struct http_uri_redirect {
	AST_LIST_ENTRY(http_uri_redirect) entry;
	char *dest;
	char target[0];
};

static AST_LIST_HEAD_STATIC(uri_redirects, http_uri_redirect);

static char *ftype2mtype(const char *ftype, char *wkspace, int wkspacelen)
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

/* like ast_uri_decode, but replace '+' with ' ' */
static char *uri_decode(char *buf)
{
	char *c;
	ast_uri_decode(buf);
	for (c = buf; *c; c++) {
		if (*c == '+')
			*c = ' ';
	}
	return buf;
}
static struct ast_str *static_callback(struct sockaddr_in *req, const char *uri, struct ast_variable *vars, int *status, char **title, int *contentlength)
{
	struct ast_str *result;
	char *path;
	char *ftype, *mtype;
	char wkspace[80];
	struct stat st;
	int len;
	int fd;

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
	mtype=ftype2mtype(ftype, wkspace, sizeof(wkspace));
	
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
	result = ast_str_create(len);
	if (result == NULL)	/* XXX not really but... */
		goto out403;

	ast_str_append(&result, 0, "Content-type: %s\r\n\r\n", mtype);
	*contentlength = read(fd, result->str + result->used, st.st_size);
	if (*contentlength < 0) {
		close(fd);
		free(result);
		goto out403;
	}
	result->used += *contentlength;
	close(fd);
	return result;

out404:
	*status = 404;
	*title = strdup("Not Found");
	return ast_http_error(404, "Not Found", NULL, "Nothing to see here.  Move along.");

out403:
	*status = 403;
	*title = strdup("Access Denied");
	return ast_http_error(403, "Access Denied", NULL, "Sorry, I cannot let you do that, Dave.");
}


static struct ast_str *httpstatus_callback(struct sockaddr_in *req, const char *uri, struct ast_variable *vars, int *status, char **title, int *contentlength)
{
	struct ast_str *out = ast_str_create(512);
	struct ast_variable *v;

	if (out == NULL)
		return out;

	ast_str_append(&out, 0,
		"\r\n"
		"<title>Asterisk HTTP Status</title>\r\n"
		"<body bgcolor=\"#ffffff\">\r\n"
		"<table bgcolor=\"#f1f1f1\" align=\"center\"><tr><td bgcolor=\"#e0e0ff\" colspan=\"2\" width=\"500\">\r\n"
		"<h2>&nbsp;&nbsp;Asterisk&trade; HTTP Status</h2></td></tr>\r\n");

	ast_str_append(&out, 0, "<tr><td><i>Prefix</i></td><td><b>%s</b></td></tr>\r\n", prefix);
	ast_str_append(&out, 0, "<tr><td><i>Bind Address</i></td><td><b>%s</b></td></tr>\r\n",
			ast_inet_ntoa(http_desc.oldsin.sin_addr));
	ast_str_append(&out, 0, "<tr><td><i>Bind Port</i></td><td><b>%d</b></td></tr>\r\n",
			ntohs(http_desc.oldsin.sin_port));
	if (http_tls_cfg.enabled)
		ast_str_append(&out, 0, "<tr><td><i>SSL Bind Port</i></td><td><b>%d</b></td></tr>\r\n",
			ntohs(https_desc.oldsin.sin_port));
	ast_str_append(&out, 0, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");
	for (v = vars; v; v = v->next) {
		if (strncasecmp(v->name, "cookie_", 7))
			ast_str_append(&out, 0, "<tr><td><i>Submitted Variable '%s'</i></td><td>%s</td></tr>\r\n", v->name, v->value);
	}
	ast_str_append(&out, 0, "<tr><td colspan=\"2\"><hr></td></tr>\r\n");
	for (v = vars; v; v = v->next) {
		if (!strncasecmp(v->name, "cookie_", 7))
			ast_str_append(&out, 0, "<tr><td><i>Cookie '%s'</i></td><td>%s</td></tr>\r\n", v->name, v->value);
	}
	ast_str_append(&out, 0, "</table><center><font size=\"-1\"><i>Asterisk and Digium are registered trademarks of Digium, Inc.</i></font></center></body>\r\n");
	return out;
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
};
	
struct ast_str *ast_http_error(int status, const char *title, const char *extra_header, const char *text)
{
	struct ast_str *out = ast_str_create(512);
	if (out == NULL)
		return out;
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
 * Link the new uri into the list. They are sorted by length of
 * the string, not alphabetically. Duplicate entries are not replaced,
 * but the insertion order (using <= and not just <) makes sure that
 * more recent insertions hide older ones.
 * On a lookup, we just scan the list and stop at the first matching entry.
 */
int ast_http_uri_link(struct ast_http_uri *urih)
{
	struct ast_http_uri *uri;
	int len = strlen(urih->uri);

	AST_LIST_LOCK(&uris);

	if ( AST_LIST_EMPTY(&uris) || strlen(AST_LIST_FIRST(&uris)->uri) <= len ) {
		AST_LIST_INSERT_HEAD(&uris, urih, entry);
		AST_LIST_UNLOCK(&uris);
		return 0;
	}

	AST_LIST_TRAVERSE(&uris, uri, entry) {
		if ( AST_LIST_NEXT(uri, entry) 
			&& strlen(AST_LIST_NEXT(uri, entry)->uri) <= len ) {
			AST_LIST_INSERT_AFTER(&uris, uri, urih, entry);
			AST_LIST_UNLOCK(&uris); 
			return 0;
		}
	}

	AST_LIST_INSERT_TAIL(&uris, urih, entry);

	AST_LIST_UNLOCK(&uris);
	
	return 0;
}	

void ast_http_uri_unlink(struct ast_http_uri *urih)
{
	AST_LIST_LOCK(&uris);
	AST_LIST_REMOVE(&uris, urih, entry);
	AST_LIST_UNLOCK(&uris);
}

static struct ast_str *handle_uri(struct sockaddr_in *sin, char *uri, int *status, char **title, int *contentlength, struct ast_variable **cookies)
{
	char *c;
	struct ast_str *out = NULL;
	char *params = uri;
	struct ast_http_uri *urih=NULL;
	int l;
	struct ast_variable *vars=NULL, *v, *prev = NULL;
	struct http_uri_redirect *redirect;

	strsep(&params, "?");
	/* Extract arguments from the request and store them in variables. */
	if (params) {
		char *var, *val;

		while ((val = strsep(&params, "&"))) {
			var = strsep(&val, "=");
			if (val)
				uri_decode(val);
			else 
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
	/*
	 * Append the cookies to the variables (the only reason to have them
	 * at the end is to avoid another pass of the cookies list to find
	 * the tail).
	 */
	if (prev)
		prev->next = *cookies;
	else
		vars = *cookies;
	*cookies = NULL;
	ast_uri_decode(uri);

	AST_LIST_LOCK(&uri_redirects);
	AST_LIST_TRAVERSE(&uri_redirects, redirect, entry) {
		if (!strcasecmp(uri, redirect->target)) {
			char buf[512];
			snprintf(buf, sizeof(buf), "Location: %s\r\n", redirect->dest);
			out = ast_http_error(302, "Moved Temporarily", buf,
				"There is no spoon...");
			*status = 302;
			*title = strdup("Moved Temporarily");
			break;
		}
	}
	AST_LIST_UNLOCK(&uri_redirects);
	if (redirect)
		goto cleanup;

	/* We want requests to start with the prefix and '/' */
	l = strlen(prefix);
	if (l && !strncasecmp(uri, prefix, l) && uri[l] == '/') {
		uri += l + 1;
		/* scan registered uris to see if we match one. */
		AST_LIST_LOCK(&uris);
		AST_LIST_TRAVERSE(&uris, urih, entry) {
			l = strlen(urih->uri);
			c = uri + l;	/* candidate */
			if (strncasecmp(urih->uri, uri, l) /* no match */
			    || (*c && *c != '/')) /* substring */
				continue;
			if (*c == '/')
				c++;
			if (!*c || urih->has_subtree) {
				uri = c;
				break;
			}
		}
		if (!urih)
			AST_LIST_UNLOCK(&uris);
	}
	if (urih) {
		out = urih->callback(sin, uri, vars, status, title, contentlength);
		AST_LIST_UNLOCK(&uris);
	} else {
		out = ast_http_error(404, "Not Found", NULL,
			"The requested URL was not found on this server.");
		*status = 404;
		*title = strdup("Not Found");
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
static HOOK_T ssl_read(void *cookie, char *buf, LEN_T len)
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
}
#endif	/* DO_SSL */

/*!
 * creates a FILE * from the fd passed by the accept thread.
 * This operation is potentially expensive (certificate verification),
 * so we do it in the child thread context.
 */
static void *make_file_from_fd(void *data)
{
	struct server_instance *ser = data;

	/*
	 * open a FILE * as appropriate.
	 */
	if (!ser->parent->tls_cfg)
		ser->f = fdopen(ser->fd, "w+");
#ifdef DO_SSL
	else if ( (ser->ssl = SSL_new(ser->parent->tls_cfg->ssl_ctx)) ) {
		SSL_set_fd(ser->ssl, ser->fd);
		if (SSL_accept(ser->ssl) == 0)
			ast_verbose(" error setting up ssl connection");
		else {
#if defined(HAVE_FUNOPEN)	/* the BSD interface */
			ser->f = funopen(ser->ssl, ssl_read, ssl_write, NULL, ssl_close);

#elif defined(HAVE_FOPENCOOKIE)	/* the glibc/linux interface */
			static const cookie_io_functions_t cookie_funcs = {
				ssl_read, ssl_write, NULL, ssl_close
			};
			ser->f = fopencookie(ser->ssl, "w+", cookie_funcs);
#else
			/* could add other methods here */
#endif
		}
		if (!ser->f)	/* no success opening descriptor stacking */
			SSL_free(ser->ssl);
	}
#endif /* DO_SSL */

	if (!ser->f) {
		close(ser->fd);
		ast_log(LOG_WARNING, "FILE * open failed!\n");
		free(ser);
		return NULL;
	}
	return ser->parent->worker_fn(ser);
}

static void *httpd_helper_thread(void *data)
{
	char buf[4096];
	char cookie[4096];
	struct server_instance *ser = data;
	struct ast_variable *var, *prev=NULL, *vars=NULL;
	char *uri, *title=NULL;
	int status = 200, contentlength = 0;
	struct ast_str *out = NULL;

	if (!fgets(buf, sizeof(buf), ser->f))
		goto done;

	uri = ast_skip_nonblanks(buf);	/* Skip method */
	if (*uri)
		*uri++ = '\0';

	uri = ast_skip_blanks(uri);	/* Skip white space */

	if (*uri) {			/* terminate at the first blank */
		char *c = ast_skip_nonblanks(uri);
		if (*c)
			*c = '\0';
	}

	/* process "Cookie: " lines */
	while (fgets(cookie, sizeof(cookie), ser->f)) {
		char *vname, *vval;
		int l;

		/* Trim trailing characters */
		ast_trim_blanks(cookie);
		if (ast_strlen_zero(cookie))
			break;
		if (strncasecmp(cookie, "Cookie: ", 8))
			continue;

		/* TODO - The cookie parsing code below seems to work   
		   in IE6 and FireFox 1.5.  However, it is not entirely 
		   correct, and therefore may not work in all           
		   circumstances.		                        
		      For more details see RFC 2109 and RFC 2965        */
	
		/* FireFox cookie strings look like:                    
		     Cookie: mansession_id="********"                   
		   InternetExplorer's look like:                        
		     Cookie: $Version="1"; mansession_id="********"     */
		
		/* If we got a FireFox cookie string, the name's right  
		    after "Cookie: "                                    */
		vname = ast_skip_blanks(cookie + 8);
			
		/* If we got an IE cookie string, we need to skip to    
		    past the version to get to the name                 */
		if (*vname == '$') {
			strsep(&vname, ";");
			if (!vname)	/* no name ? */
				continue;
			vname = ast_skip_blanks(vname);
		}
		vval = strchr(vname, '=');
		if (!vval)
			continue;
		/* Ditch the = and the quotes */
		*vval++ = '\0';
		if (*vval)
			vval++;
		if ( (l = strlen(vval)) )
			vval[l - 1] = '\0';	/* trim trailing quote */
		var = ast_variable_new(vname, vval);
		if (var) {
			if (prev)
				prev->next = var;
			else
				vars = var;
			prev = var;
		}
	}

	if (!*uri)
		out = ast_http_error(400, "Bad Request", NULL, "Invalid Request");
	else if (strcasecmp(buf, "get")) 
		out = ast_http_error(501, "Not Implemented", NULL,
			"Attempt to use unimplemented / unsupported method");
	else	/* try to serve it */
		out = handle_uri(&ser->requestor, uri, &status, &title, &contentlength, &vars);

	/* If they aren't mopped up already, clean up the cookies */
	if (vars)
		ast_variables_destroy(vars);

	if (out == NULL)
		out = ast_http_error(500, "Internal Error", NULL, "Internal Server Error");
	if (out) {
		time_t t = time(NULL);
		char timebuf[256];

		strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&t));
		fprintf(ser->f, "HTTP/1.1 %d %s\r\n"
				"Server: Asterisk\r\n"
				"Date: %s\r\n"
				"Connection: close\r\n",
			status, title ? title : "OK", timebuf);
		if (!contentlength) {	/* opaque body ? just dump it hoping it is properly formatted */
			fprintf(ser->f, "%s", out->str);
		} else {
			char *tmp = strstr(out->str, "\r\n\r\n");

			if (tmp) {
				fprintf(ser->f, "Content-length: %d\r\n", contentlength);
				/* first write the header, then the body */
				fwrite(out->str, 1, (tmp + 4 - out->str), ser->f);
				fwrite(tmp + 4, 1, contentlength, ser->f);
			}
		}
		free(out);
	}
	if (title)
		free(title);

done:
	if (ser->f)
		fclose(ser->f);
	free(ser);
	return NULL;
}

void *server_root(void *data)
{
	struct server_args *desc = data;
	int fd;
	struct sockaddr_in sin;
	socklen_t sinlen;
	struct server_instance *ser;
	pthread_t launched;
	pthread_attr_t attr;
	
	for (;;) {
		int i, flags;

		if (desc->periodic_fn)
			desc->periodic_fn(desc);
		i = ast_wait_for_input(desc->accept_fd, desc->poll_timeout);
		if (i <= 0)
			continue;
		sinlen = sizeof(sin);
		fd = accept(desc->accept_fd, (struct sockaddr *)&sin, &sinlen);
		if (fd < 0) {
			if ((errno != EAGAIN) && (errno != EINTR))
				ast_log(LOG_WARNING, "Accept failed: %s\n", strerror(errno));
			continue;
		}
		ser = ast_calloc(1, sizeof(*ser));
		if (!ser) {
			ast_log(LOG_WARNING, "No memory for new session: %s\n", strerror(errno));
			close(fd);
			continue;
		}
		flags = fcntl(fd, F_GETFL);
		fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
		ser->fd = fd;
		ser->parent = desc;
		memcpy(&ser->requestor, &sin, sizeof(ser->requestor));

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			
		if (ast_pthread_create_background(&launched, &attr, make_file_from_fd, ser)) {
			ast_log(LOG_WARNING, "Unable to launch helper thread: %s\n", strerror(errno));
			close(ser->fd);
			free(ser);
		}
	}
	return NULL;
}

int ssl_setup(struct tls_config *cfg)
{
#ifndef DO_SSL
	cfg->enabled = 0;
	return 0;
#else
	if (!cfg->enabled)
		return 0;
	SSL_load_error_strings();
	SSLeay_add_ssl_algorithms();
	cfg->ssl_ctx = SSL_CTX_new( SSLv23_server_method() );
	if (!ast_strlen_zero(cfg->certfile)) {
		if (SSL_CTX_use_certificate_file(cfg->ssl_ctx, cfg->certfile, SSL_FILETYPE_PEM) == 0 ||
		    SSL_CTX_use_PrivateKey_file(cfg->ssl_ctx, cfg->certfile, SSL_FILETYPE_PEM) == 0 ||
		    SSL_CTX_check_private_key(cfg->ssl_ctx) == 0 ) {
			ast_verbose("ssl cert error <%s>", cfg->certfile);
			sleep(2);
			cfg->enabled = 0;
			return 0;
		}
	}
	if (!ast_strlen_zero(cfg->cipher)) {
		if (SSL_CTX_set_cipher_list(cfg->ssl_ctx, cfg->cipher) == 0 ) {
			ast_verbose("ssl cipher error <%s>", cfg->cipher);
			sleep(2);
			cfg->enabled = 0;
			return 0;
		}
	}
	ast_verbose("ssl cert ok");
	return 1;
#endif
}

/*!
 * This is a generic (re)start routine for a TCP server,
 * which does the socket/bind/listen and starts a thread for handling
 * accept().
 */
void server_start(struct server_args *desc)
{
	int flags;
	int x = 1;
	
	/* Do nothing if nothing has changed */
	if (!memcmp(&desc->oldsin, &desc->sin, sizeof(desc->oldsin))) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Nothing changed in %s\n", desc->name);
		return;
	}
	
	desc->oldsin = desc->sin;
	
	/* Shutdown a running server if there is one */
	if (desc->master != AST_PTHREADT_NULL) {
		pthread_cancel(desc->master);
		pthread_kill(desc->master, SIGURG);
		pthread_join(desc->master, NULL);
	}
	
	if (desc->accept_fd != -1)
		close(desc->accept_fd);

	/* If there's no new server, stop here */
	if (desc->sin.sin_family == 0)
		return;

	desc->accept_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (desc->accept_fd < 0) {
		ast_log(LOG_WARNING, "Unable to allocate socket for %s: %s\n",
			desc->name, strerror(errno));
		return;
	}
	
	setsockopt(desc->accept_fd, SOL_SOCKET, SO_REUSEADDR, &x, sizeof(x));
	if (bind(desc->accept_fd, (struct sockaddr *)&desc->sin, sizeof(desc->sin))) {
		ast_log(LOG_NOTICE, "Unable to bind %s to %s:%d: %s\n",
			desc->name,
			ast_inet_ntoa(desc->sin.sin_addr), ntohs(desc->sin.sin_port),
			strerror(errno));
		goto error;
	}
	if (listen(desc->accept_fd, 10)) {
		ast_log(LOG_NOTICE, "Unable to listen for %s!\n", desc->name);
		goto error;
	}
	flags = fcntl(desc->accept_fd, F_GETFL);
	fcntl(desc->accept_fd, F_SETFL, flags | O_NONBLOCK);
	if (ast_pthread_create_background(&desc->master, NULL, desc->accept_fn, desc)) {
		ast_log(LOG_NOTICE, "Unable to launch %s on %s:%d: %s\n",
			desc->name,
			ast_inet_ntoa(desc->sin.sin_addr), ntohs(desc->sin.sin_port),
			strerror(errno));
		goto error;
	}
	return;

error:
	close(desc->accept_fd);
	desc->accept_fd = -1;
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

	if (!(redirect = ast_calloc(1, total_len)))
		return;

	redirect->dest = redirect->target + target_len;
	strcpy(redirect->target, target);
	strcpy(redirect->dest, dest);

	AST_LIST_LOCK(&uri_redirects);

	target_len--; /* So we can compare directly with strlen() */
	if ( AST_LIST_EMPTY(&uri_redirects) 
		|| strlen(AST_LIST_FIRST(&uri_redirects)->target) <= target_len ) {
		AST_LIST_INSERT_HEAD(&uri_redirects, redirect, entry);
		AST_LIST_UNLOCK(&uri_redirects);
		return;
	}

	AST_LIST_TRAVERSE(&uri_redirects, cur, entry) {
		if ( AST_LIST_NEXT(cur, entry) 
			&& strlen(AST_LIST_NEXT(cur, entry)->target) <= target_len ) {
			AST_LIST_INSERT_AFTER(&uri_redirects, cur, redirect, entry);
			AST_LIST_UNLOCK(&uri_redirects); 
			return;
		}
	}

	AST_LIST_INSERT_TAIL(&uri_redirects, redirect, entry);

	AST_LIST_UNLOCK(&uri_redirects);
}

static int __ast_http_load(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	int enabled=0;
	int newenablestatic=0;
	struct hostent *hp;
	struct ast_hostent ahp;
	char newprefix[MAX_PREFIX];
	int have_sslbindaddr = 0;
	struct http_uri_redirect *redirect;

	/* default values */
	memset(&http_desc.sin, 0, sizeof(http_desc.sin));
	http_desc.sin.sin_port = htons(8088);

	memset(&https_desc.sin, 0, sizeof(https_desc.sin));
	https_desc.sin.sin_port = htons(8089);
	strcpy(newprefix, DEFAULT_PREFIX);

	http_tls_cfg.enabled = 0;
	if (http_tls_cfg.certfile)
		free(http_tls_cfg.certfile);
	http_tls_cfg.certfile = ast_strdup(AST_CERTFILE);
	if (http_tls_cfg.cipher)
		free(http_tls_cfg.cipher);
	http_tls_cfg.cipher = ast_strdup("");

	AST_LIST_LOCK(&uri_redirects);
	while ((redirect = AST_LIST_REMOVE_HEAD(&uri_redirects, entry)))
		free(redirect);
	AST_LIST_UNLOCK(&uri_redirects);

	cfg = ast_config_load("http.conf");
	if (cfg) {
		v = ast_variable_browse(cfg, "general");
		for (; v; v = v->next) {
			if (!strcasecmp(v->name, "enabled"))
				enabled = ast_true(v->value);
			else if (!strcasecmp(v->name, "sslenable"))
				http_tls_cfg.enabled = ast_true(v->value);
			else if (!strcasecmp(v->name, "sslbindport"))
				https_desc.sin.sin_port = htons(atoi(v->value));
			else if (!strcasecmp(v->name, "sslcert")) {
				free(http_tls_cfg.certfile);
				http_tls_cfg.certfile = ast_strdup(v->value);
			} else if (!strcasecmp(v->name, "sslcipher")) {
				free(http_tls_cfg.cipher);
				http_tls_cfg.cipher = ast_strdup(v->value);
			}
			else if (!strcasecmp(v->name, "enablestatic"))
				newenablestatic = ast_true(v->value);
			else if (!strcasecmp(v->name, "bindport"))
				http_desc.sin.sin_port = htons(atoi(v->value));
			else if (!strcasecmp(v->name, "sslbindaddr")) {
				if ((hp = ast_gethostbyname(v->value, &ahp))) {
					memcpy(&https_desc.sin.sin_addr, hp->h_addr, sizeof(https_desc.sin.sin_addr));
					have_sslbindaddr = 1;
				} else {
					ast_log(LOG_WARNING, "Invalid bind address '%s'\n", v->value);
				}
			} else if (!strcasecmp(v->name, "bindaddr")) {
				if ((hp = ast_gethostbyname(v->value, &ahp))) {
					memcpy(&http_desc.sin.sin_addr, hp->h_addr, sizeof(http_desc.sin.sin_addr));
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
			} else {
				ast_log(LOG_WARNING, "Ignoring unknown option '%s' in http.conf\n", v->name);
			}
		}
		ast_config_destroy(cfg);
	}
	if (!have_sslbindaddr)
		https_desc.sin.sin_addr = http_desc.sin.sin_addr;
	if (enabled)
		http_desc.sin.sin_family = https_desc.sin.sin_family = AF_INET;
	if (strcmp(prefix, newprefix))
		ast_copy_string(prefix, newprefix, sizeof(prefix));
	enablestatic = newenablestatic;
	server_start(&http_desc);
	if (ssl_setup(https_desc.tls_cfg))
		server_start(&https_desc);
	return 0;
}

static int handle_show_http(int fd, int argc, char *argv[])
{
	struct ast_http_uri *urih;
	struct http_uri_redirect *redirect;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	ast_cli(fd, "HTTP Server Status:\n");
	ast_cli(fd, "Prefix: %s\n", prefix);
	if (!http_desc.oldsin.sin_family)
		ast_cli(fd, "Server Disabled\n\n");
	else {
		ast_cli(fd, "Server Enabled and Bound to %s:%d\n\n",
			ast_inet_ntoa(http_desc.oldsin.sin_addr),
			ntohs(http_desc.oldsin.sin_port));
		if (http_tls_cfg.enabled)
			ast_cli(fd, "HTTPS Server Enabled and Bound to %s:%d\n\n",
				ast_inet_ntoa(https_desc.oldsin.sin_addr),
				ntohs(https_desc.oldsin.sin_port));
	}

	ast_cli(fd, "Enabled URI's:\n");
	AST_LIST_LOCK(&uris);
	AST_LIST_TRAVERSE(&uris, urih, entry)
		ast_cli(fd, "%s/%s%s => %s\n", prefix, urih->uri, (urih->has_subtree ? "/..." : "" ), urih->description);
	if (AST_LIST_EMPTY(&uris))
		ast_cli(fd, "None.\n");
	AST_LIST_UNLOCK(&uris);

	ast_cli(fd, "\nEnabled Redirects:\n");
	AST_LIST_LOCK(&uri_redirects);
	AST_LIST_TRAVERSE(&uri_redirects, redirect, entry)
		ast_cli(fd, "  %s => %s\n", redirect->target, redirect->dest);
	if (AST_LIST_EMPTY(&uri_redirects))
		ast_cli(fd, "  None.\n");
	AST_LIST_UNLOCK(&uri_redirects);

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
