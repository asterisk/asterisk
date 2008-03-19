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

#ifdef ENABLE_UPLOADS
#include <gmime/gmime.h>
#endif /* ENABLE_UPLOADS */

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

#define MAX_PREFIX 80

/* See http.h for more information about the SSL implementation */
#if defined(HAVE_OPENSSL) && (defined(HAVE_FUNOPEN) || defined(HAVE_FOPENCOOKIE))
#define	DO_SSL	/* comment in/out if you want to support ssl */
#endif

static struct ast_tls_config http_tls_cfg;

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
	.accept_fn = ast_tcptls_server_root,
	.worker_fn = httpd_helper_thread,
};

static struct server_args https_desc = {
	.accept_fd = -1,
	.master = AST_PTHREADT_NULL,
	.tls_cfg = &http_tls_cfg,
	.poll_timeout = -1,
	.name = "https server",
	.accept_fn = ast_tcptls_server_root,
	.worker_fn = httpd_helper_thread,
};

static AST_RWLIST_HEAD_STATIC(uris, ast_http_uri);	/*!< list of supported handlers */

#ifdef ENABLE_UPLOADS
struct ast_http_post_mapping {
	AST_RWLIST_ENTRY(ast_http_post_mapping) entry;
	char *from;
	char *to;
};

static AST_RWLIST_HEAD_STATIC(post_mappings, ast_http_post_mapping);

struct mime_cbinfo {
	int count;
	const char *post_dir;
};
#endif /* ENABLE_UPLOADS */

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
			if (!strcasecmp(ftype, mimetypes[x].ext))
				return mimetypes[x].mtype;
		}
	}

	snprintf(wkspace, wkspacelen, "text/%s", S_OR(ftype, "plain"));

	return wkspace;
}

static struct ast_str *static_callback(struct ast_tcptls_session_instance *ser, const char *uri, enum ast_http_method method,
				       struct ast_variable *vars, int *status, char **title, int *contentlength)
{
	char *path;
	char *ftype;
	const char *mtype;
	char wkspace[80];
	struct stat st;
	int len;
	int fd;
	struct timeval tv = ast_tvnow();
	char buf[256];
	struct ast_tm tm;

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

	ast_strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %Z", ast_localtime(&tv, &tm, "GMT"));
	fprintf(ser->f, "HTTP/1.1 200 OK\r\n"
		"Server: Asterisk/%s\r\n"
		"Date: %s\r\n"
		"Connection: close\r\n"
		"Cache-Control: no-cache, no-store\r\n"
		"Content-Length: %d\r\n"
		"Content-type: %s\r\n\r\n",
		ast_get_version(), buf, (int) st.st_size, mtype);

	while ((len = read(fd, buf, sizeof(buf))) > 0)
		fwrite(buf, 1, len, ser->f);

	close(fd);
	return NULL;

out404:
	*status = 404;
	*title = ast_strdup("Not Found");
	return ast_http_error(404, "Not Found", NULL, "Nothing to see here.  Move along.");

out403:
	*status = 403;
	*title = ast_strdup("Access Denied");
	return ast_http_error(403, "Access Denied", NULL, "Sorry, I cannot let you do that, Dave.");
}


static struct ast_str *httpstatus_callback(struct ast_tcptls_session_instance *ser, const char *uri, enum ast_http_method method,
					   struct ast_variable *vars, int *status, char **title, int *contentlength)
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
	.static_content = 1,
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
		if ( AST_RWLIST_NEXT(uri, entry) 
			&& strlen(AST_RWLIST_NEXT(uri, entry)->uri) <= len ) {
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

#ifdef ENABLE_UPLOADS
/*! \note This assumes that the post_mappings list is locked */
static struct ast_http_post_mapping *find_post_mapping(const char *uri)
{
	struct ast_http_post_mapping *post_map;

	if (!ast_strlen_zero(prefix) && strncmp(prefix, uri, strlen(prefix))) {
		ast_debug(1, "URI %s does not have prefix %s\n", uri, prefix);
		return NULL;
	}

	uri += strlen(prefix);
	if (*uri == '/')
		uri++;
	
	AST_RWLIST_TRAVERSE(&post_mappings, post_map, entry) {
		if (!strcmp(uri, post_map->from))
			return post_map;
	}

	return NULL;
}

static void post_raw(GMimePart *part, const char *post_dir, const char *fn)
{
	char filename[PATH_MAX];
	GMimeDataWrapper *content;
	GMimeStream *stream;
	int fd;

	snprintf(filename, sizeof(filename), "%s/%s", post_dir, fn);

	ast_debug(1, "Posting raw data to %s\n", filename);

	if ((fd = open(filename, O_CREAT | O_WRONLY, 0666)) == -1) {
		ast_log(LOG_WARNING, "Unable to open %s for writing file from a POST!\n", filename);
		return;
	}

	stream = g_mime_stream_fs_new(fd);

	content = g_mime_part_get_content_object(part);
	g_mime_data_wrapper_write_to_stream(content, stream);
	g_mime_stream_flush(stream);

	g_object_unref(content);
	g_object_unref(stream);
}

static GMimeMessage *parse_message(FILE *f)
{
	GMimeMessage *message;
	GMimeParser *parser;
	GMimeStream *stream;

	stream = g_mime_stream_file_new(f);

	parser = g_mime_parser_new_with_stream(stream);
	g_mime_parser_set_respect_content_length(parser, 1);
	
	g_object_unref(stream);

	message = g_mime_parser_construct_message(parser);

	g_object_unref(parser);

	return message;
}

static void process_message_callback(GMimeObject *part, gpointer user_data)
{
	struct mime_cbinfo *cbinfo = user_data;

	cbinfo->count++;

	/* We strip off the headers before we get here, so should only see GMIME_IS_PART */
	if (GMIME_IS_MESSAGE_PART(part)) {
		ast_log(LOG_WARNING, "Got unexpected GMIME_IS_MESSAGE_PART\n");
		return;
	} else if (GMIME_IS_MESSAGE_PARTIAL(part)) {
		ast_log(LOG_WARNING, "Got unexpected GMIME_IS_MESSAGE_PARTIAL\n");
		return;
	} else if (GMIME_IS_MULTIPART(part)) {
		GList *l;
		
		ast_log(LOG_WARNING, "Got unexpected GMIME_IS_MULTIPART, trying to process subparts\n");
		l = GMIME_MULTIPART (part)->subparts;
		while (l) {
			process_message_callback(l->data, cbinfo);
			l = l->next;
		}
	} else if (GMIME_IS_PART(part)) {
		const char *filename;

		ast_debug(3, "Got mime part\n");
		if (ast_strlen_zero(filename = g_mime_part_get_filename(GMIME_PART(part)))) {
			ast_debug(1, "Skipping part with no filename\n");
			return;
		}

		post_raw(GMIME_PART(part), cbinfo->post_dir, filename);
	} else {
		ast_log(LOG_ERROR, "Encountered unknown MIME part. This should never happen!\n");
	}
}

static int process_message(GMimeMessage *message, const char *post_dir)
{
	struct mime_cbinfo cbinfo = {
		.count = 0,
		.post_dir = post_dir,
	};

	g_mime_message_foreach_part(message, process_message_callback, &cbinfo);

	return cbinfo.count;
}

static struct ast_str *handle_post(struct ast_tcptls_session_instance *ser, char *uri, 
	int *status, char **title, int *contentlength, struct ast_variable *headers,
	struct ast_variable *cookies)
{
	char buf[4096];
	FILE *f;
	size_t res;
	struct ast_variable *var;
	int content_len = 0;
	struct ast_http_post_mapping *post_map;
	const char *post_dir;
	unsigned long ident = 0;
	GMimeMessage *message;
	int message_count = 0;

	for (var = cookies; var; var = var->next) {
		if (strcasecmp(var->name, "mansession_id"))
			continue;

		if (sscanf(var->value, "%lx", &ident) != 1) {
			*status = 400;
			*title = ast_strdup("Bad Request");
			return ast_http_error(400, "Bad Request", NULL, "The was an error parsing the request.");
		}

		if (!astman_verify_session_writepermissions(ident, EVENT_FLAG_CONFIG)) {
			*status = 401;
			*title = ast_strdup("Unauthorized");
			return ast_http_error(401, "Unauthorized", NULL, "You are not authorized to make this request.");
		}

		break;
	}
	if (!var) {
		*status = 401;
		*title = ast_strdup("Unauthorized");
		return ast_http_error(401, "Unauthorized", NULL, "You are not authorized to make this request.");
	}

	if (!(f = tmpfile()))
		return NULL;

	for (var = headers; var; var = var->next) {
		if (!strcasecmp(var->name, "Content-Length")) {
			if ((sscanf(var->value, "%u", &content_len)) != 1) {
				ast_log(LOG_ERROR, "Invalid Content-Length in POST request!\n");
				fclose(f);
				return NULL;
			}
			ast_debug(1, "Got a Content-Length of %d\n", content_len);
		} else if (!strcasecmp(var->name, "Content-Type"))
			fprintf(f, "Content-Type: %s\r\n\r\n", var->value);
	}

	for(res = sizeof(buf);content_len;content_len -= res) {
		if (content_len < res)
			res = content_len;
		fread(buf, 1, res, ser->f);
		fwrite(buf, 1, res, f);
	}

	if (fseek(f, SEEK_SET, 0)) {
		ast_debug(1, "Failed to seek temp file back to beginning.\n");
		fclose(f);
		return NULL;
	}

	AST_RWLIST_RDLOCK(&post_mappings);
	if (!(post_map = find_post_mapping(uri))) {
		ast_debug(1, "%s is not a valid URI for POST\n", uri);
		AST_RWLIST_UNLOCK(&post_mappings);
		*status = 404;
		*title = ast_strdup("Not Found");
		return ast_http_error(404, "Not Found", NULL, "The requested URL was not found on this server.");
	}
	post_dir = ast_strdupa(post_map->to);
	post_map = NULL;
	AST_RWLIST_UNLOCK(&post_mappings);

	ast_debug(1, "Going to post files to dir %s\n", post_dir);

	message = parse_message(f); /* Takes ownership and will close f */

	if (!message) {
		ast_log(LOG_ERROR, "Error parsing MIME data\n");
		*status = 400;
		*title = ast_strdup("Bad Request");
		return ast_http_error(400, "Bad Request", NULL, "The was an error parsing the request.");
	}

	if (!(message_count = process_message(message, post_dir))) {
		ast_log(LOG_ERROR, "Invalid MIME data, found no parts!\n");
		*status = 400;
		*title = ast_strdup("Bad Request");
		return ast_http_error(400, "Bad Request", NULL, "The was an error parsing the request.");
	}

	*status = 200;
	*title = ast_strdup("OK");
	return ast_http_error(200, "OK", NULL, "File successfully uploaded.");
}
#endif /* ENABLE_UPLOADS */

static struct ast_str *handle_uri(struct ast_tcptls_session_instance *ser, char *uri, int *status, 
	char **title, int *contentlength, struct ast_variable **cookies, 
	unsigned int *static_content)
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
				ast_uri_decode(val);
			else 
				val = "";
			ast_uri_decode(var);
			if ((v = ast_variable_new(var, val, ""))) {
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

	AST_RWLIST_RDLOCK(&uri_redirects);
	AST_RWLIST_TRAVERSE(&uri_redirects, redirect, entry) {
		if (!strcasecmp(uri, redirect->target)) {
			char buf[512];
			snprintf(buf, sizeof(buf), "Location: %s\r\n", redirect->dest);
			out = ast_http_error(302, "Moved Temporarily", buf,
				"There is no spoon...");
			*status = 302;
			*title = ast_strdup("Moved Temporarily");
			break;
		}
	}
	AST_RWLIST_UNLOCK(&uri_redirects);
	if (redirect)
		goto cleanup;

	/* We want requests to start with the prefix and '/' */
	l = strlen(prefix);
	if (l && !strncasecmp(uri, prefix, l) && uri[l] == '/') {
		uri += l + 1;
		/* scan registered uris to see if we match one. */
		AST_RWLIST_RDLOCK(&uris);
		AST_RWLIST_TRAVERSE(&uris, urih, entry) {
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
			AST_RWLIST_UNLOCK(&uris);
	}
	if (urih) {
		if (urih->static_content)
			*static_content = 1;
		out = urih->callback(ser, uri, AST_HTTP_GET, vars, status, title, contentlength);
		AST_RWLIST_UNLOCK(&uris);
	} else {
		out = ast_http_error(404, "Not Found", NULL,
			"The requested URL was not found on this server.");
		*status = 404;
		*title = ast_strdup("Not Found");
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

static void *httpd_helper_thread(void *data)
{
	char buf[4096];
	char cookie[4096];
	struct ast_tcptls_session_instance *ser = data;
	struct ast_variable *var, *prev=NULL, *vars=NULL, *headers = NULL;
	char *uri, *title=NULL;
	int status = 200, contentlength = 0;
	struct ast_str *out = NULL;
	unsigned int static_content = 0;

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
		if (strncasecmp(cookie, "Cookie: ", 8)) {
			char *name, *value;

			value = ast_strdupa(cookie);
			name = strsep(&value, ":");
			if (!value)
				continue;
			value = ast_skip_blanks(value);
			if (ast_strlen_zero(value))
				continue;
			var = ast_variable_new(name, value, "");
			if (!var)
				continue;
			var->next = headers;
			headers = var;
			continue;
		}

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
		var = ast_variable_new(vname, vval, "");
		if (var) {
			if (prev)
				prev->next = var;
			else
				vars = var;
			prev = var;
		}
	}

	if (!*uri) {
		out = ast_http_error(400, "Bad Request", NULL, "Invalid Request");
	} else if (!strcasecmp(buf, "post")) {
#ifdef ENABLE_UPLOADS
		out = handle_post(ser, uri, &status, &title, &contentlength, headers, vars);
#else
		out = ast_http_error(501, "Not Implemented", NULL,
			"Attempt to use unimplemented / unsupported method");
#endif /* ENABLE_UPLOADS */
	} else if (strcasecmp(buf, "get")) {
		out = ast_http_error(501, "Not Implemented", NULL,
			"Attempt to use unimplemented / unsupported method");
	} else {	/* try to serve it */
		out = handle_uri(ser, uri, &status, &title, &contentlength, &vars, &static_content);
	}

	/* If they aren't mopped up already, clean up the cookies */
	if (vars)
		ast_variables_destroy(vars);
	/* Clean up all the header information pulled as well */
	if (headers)
		ast_variables_destroy(headers);

	if (out) {
		struct timeval tv = ast_tvnow();
		char timebuf[256];
		struct ast_tm tm;

		ast_strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S %Z", ast_localtime(&tv, &tm, "GMT"));
		fprintf(ser->f, "HTTP/1.1 %d %s\r\n"
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
		ast_free(out);
	}
	if (title)
		ast_free(title);

done:
	fclose(ser->f);
	ser = ast_tcptls_session_instance_destroy(ser);
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

	if (!(redirect = ast_calloc(1, total_len)))
		return;

	redirect->dest = redirect->target + target_len;
	strcpy(redirect->target, target);
	strcpy(redirect->dest, dest);

	AST_RWLIST_WRLOCK(&uri_redirects);

	target_len--; /* So we can compare directly with strlen() */
	if ( AST_RWLIST_EMPTY(&uri_redirects) 
		|| strlen(AST_RWLIST_FIRST(&uri_redirects)->target) <= target_len ) {
		AST_RWLIST_INSERT_HEAD(&uri_redirects, redirect, entry);
		AST_RWLIST_UNLOCK(&uri_redirects);
		return;
	}

	AST_RWLIST_TRAVERSE(&uri_redirects, cur, entry) {
		if ( AST_RWLIST_NEXT(cur, entry) 
			&& strlen(AST_RWLIST_NEXT(cur, entry)->target) <= target_len ) {
			AST_RWLIST_INSERT_AFTER(&uri_redirects, cur, redirect, entry);
			AST_RWLIST_UNLOCK(&uri_redirects); 
			return;
		}
	}

	AST_RWLIST_INSERT_TAIL(&uri_redirects, redirect, entry);

	AST_RWLIST_UNLOCK(&uri_redirects);
}

#ifdef ENABLE_UPLOADS
static void destroy_post_mapping(struct ast_http_post_mapping *post_map)
{
	if (post_map->from)
		ast_free(post_map->from);
	if (post_map->to)
		ast_free(post_map->to);
	ast_free(post_map);
}

static void destroy_post_mappings(void)
{
	struct ast_http_post_mapping *post_map;

	AST_RWLIST_WRLOCK(&post_mappings);
	while ((post_map = AST_RWLIST_REMOVE_HEAD(&post_mappings, entry)))
		destroy_post_mapping(post_map);
	AST_RWLIST_UNLOCK(&post_mappings);
}

static void add_post_mapping(const char *from, const char *to)
{
	struct ast_http_post_mapping *post_map;

	if (!(post_map = ast_calloc(1, sizeof(*post_map))))
		return;

	if (!(post_map->from = ast_strdup(from))) {
		destroy_post_mapping(post_map);
		return;
	}

	if (!(post_map->to = ast_strdup(to))) {
		destroy_post_mapping(post_map);
		return;
	}

	AST_RWLIST_WRLOCK(&post_mappings);
	AST_RWLIST_INSERT_TAIL(&post_mappings, post_map, entry);
	AST_RWLIST_UNLOCK(&post_mappings);
}
#endif /* ENABLE_UPLOADS */

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

	if ((cfg = ast_config_load("http.conf", config_flags)) == CONFIG_STATUS_FILEUNCHANGED)
		return 0;

	/* default values */
	memset(&http_desc.sin, 0, sizeof(http_desc.sin));
	http_desc.sin.sin_port = htons(8088);

	memset(&https_desc.sin, 0, sizeof(https_desc.sin));
	https_desc.sin.sin_port = htons(8089);

	http_tls_cfg.enabled = 0;
	if (http_tls_cfg.certfile)
		ast_free(http_tls_cfg.certfile);
	http_tls_cfg.certfile = ast_strdup(AST_CERTFILE);
	if (http_tls_cfg.cipher)
		ast_free(http_tls_cfg.cipher);
	http_tls_cfg.cipher = ast_strdup("");

	AST_RWLIST_WRLOCK(&uri_redirects);
	while ((redirect = AST_RWLIST_REMOVE_HEAD(&uri_redirects, entry)))
		ast_free(redirect);
	AST_RWLIST_UNLOCK(&uri_redirects);

#ifdef ENABLE_UPLOADS
	destroy_post_mappings();
#endif /* ENABLE_UPLOADS */

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
				ast_free(http_tls_cfg.certfile);
				http_tls_cfg.certfile = ast_strdup(v->value);
			} else if (!strcasecmp(v->name, "sslcipher")) {
				ast_free(http_tls_cfg.cipher);
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

#ifdef ENABLE_UPLOADS
		for (v = ast_variable_browse(cfg, "post_mappings"); v; v = v->next)
			add_post_mapping(v->name, v->value);
#endif /* ENABLE_UPLOADS */

		ast_config_destroy(cfg);
	}
	if (!have_sslbindaddr)
		https_desc.sin.sin_addr = http_desc.sin.sin_addr;
	if (enabled)
		http_desc.sin.sin_family = https_desc.sin.sin_family = AF_INET;
	if (strcmp(prefix, newprefix))
		ast_copy_string(prefix, newprefix, sizeof(prefix));
	enablestatic = newenablestatic;
	ast_tcptls_server_start(&http_desc);
	if (ast_ssl_setup(https_desc.tls_cfg))
		ast_tcptls_server_start(&https_desc);

	return 0;
}

static char *handle_show_http(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_http_uri *urih;
	struct http_uri_redirect *redirect;

#ifdef ENABLE_UPLOADS
	struct ast_http_post_mapping *post_map;
#endif /* ENABLE_UPLOADS */

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
	
	if (a->argc != 3)
		return CLI_SHOWUSAGE;
	ast_cli(a->fd, "HTTP Server Status:\n");
	ast_cli(a->fd, "Prefix: %s\n", prefix);
	if (!http_desc.oldsin.sin_family)
		ast_cli(a->fd, "Server Disabled\n\n");
	else {
		ast_cli(a->fd, "Server Enabled and Bound to %s:%d\n\n",
			ast_inet_ntoa(http_desc.oldsin.sin_addr),
			ntohs(http_desc.oldsin.sin_port));
		if (http_tls_cfg.enabled)
			ast_cli(a->fd, "HTTPS Server Enabled and Bound to %s:%d\n\n",
				ast_inet_ntoa(https_desc.oldsin.sin_addr),
				ntohs(https_desc.oldsin.sin_port));
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
	if (AST_RWLIST_EMPTY(&uri_redirects))
		ast_cli(a->fd, "  None.\n");
	AST_RWLIST_UNLOCK(&uri_redirects);


#ifdef ENABLE_UPLOADS
	ast_cli(a->fd, "\nPOST mappings:\n");
	AST_RWLIST_RDLOCK(&post_mappings);
	AST_LIST_TRAVERSE(&post_mappings, post_map, entry) {
		ast_cli(a->fd, "%s/%s => %s\n", prefix, post_map->from, post_map->to);
	}
	ast_cli(a->fd, "%s\n", AST_LIST_EMPTY(&post_mappings) ? "None.\n" : "");
	AST_RWLIST_UNLOCK(&post_mappings);
#endif /* ENABLE_UPLOADS */

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
#ifdef ENABLE_UPLOADS
	g_mime_init(0);
#endif /* ENABLE_UPLOADS */

	ast_http_uri_link(&statusuri);
	ast_http_uri_link(&staticuri);
	ast_cli_register_multiple(cli_http, sizeof(cli_http) / sizeof(struct ast_cli_entry));

	return __ast_http_load(0);
}
