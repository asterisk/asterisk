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

#include "minimime/mm.h"

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

struct ast_http_server_instance {
	FILE *f;
	int fd;
	struct sockaddr_in requestor;
	ast_http_callback callback;
};

AST_RWLOCK_DEFINE_STATIC(uris_lock);
static struct ast_http_uri *uris;

struct ast_http_post_mapping {
	AST_RWLIST_ENTRY(ast_http_post_mapping) entry;
	char *from;
	char *to;
};

static AST_RWLIST_HEAD_STATIC(post_mappings, ast_http_post_mapping);

static int httpfd = -1;
static pthread_t master = AST_PTHREADT_NULL;
static char prefix[MAX_PREFIX];
static int prefix_len;
static struct sockaddr_in oldsin;
static int enablestatic;

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
	{ "svg", "image/svg+xml" },
	{ "gif", "image/gif" },
};

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

static char *static_callback(struct sockaddr_in *req, const char *uri, struct ast_variable *vars, int *status, char **title, int *contentlength)
{
	char result[4096];
	char *c=result;
	char *path;
	char *ftype, *mtype;
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
	return ast_http_error(404, "Not Found", NULL, "Nothing to see here.  Move along.");

out403:
	*status = 403;
	*title = strdup("Access Denied");
	return ast_http_error(403, "Access Denied", NULL, "Sorry, I cannot let you do that, Dave.");
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

/*! \note This assumes that the post_mappings list is locked */
static struct ast_http_post_mapping *find_post_mapping(const char *uri)
{
	struct ast_http_post_mapping *post_map;

	if (!ast_strlen_zero(prefix) && strncmp(prefix, uri, strlen(prefix))) {
		ast_log(LOG_DEBUG, "URI %s does not have prefix %s\n", uri, prefix);
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

static int get_filename(struct mm_mimepart *part, char *fn, size_t fn_len)
{
	const char *filename;

	filename = mm_content_getdispositionparambyname(part->type, "filename");

	if (ast_strlen_zero(filename))
		return -1;

	ast_copy_string(fn, filename, fn_len);

	return 0;
}

static void post_raw(struct mm_mimepart *part, const char *post_dir, const char *fn)
{
	char filename[PATH_MAX];
	FILE *f;
	const char *body;
	size_t body_len;

	snprintf(filename, sizeof(filename), "%s/%s", post_dir, fn);

	if (option_debug)
		ast_log(LOG_DEBUG, "Posting raw data to %s\n", filename);

	if (!(f = fopen(filename, "w"))) {
		ast_log(LOG_WARNING, "Unable to open %s for writing file from a POST!\n", filename);
		return;
	}

	if (!(body = mm_mimepart_getbody(part, 0))) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Couldn't get the mimepart body\n");
		fclose(f);
		return;
	}
	body_len = mm_mimepart_getlength(part);

	if (option_debug)
		ast_log(LOG_DEBUG, "Body length is %ld\n", (long int)body_len);

	fwrite(body, 1, body_len, f);

	fclose(f);
}

static char *handle_post(struct ast_http_server_instance *ser, char *uri, 
	int *status, char **title, int *contentlength, struct ast_variable *headers,
	struct ast_variable *cookies)
{
	char buf;
	FILE *f;
	size_t res;
	struct ast_variable *var;
	int content_len = 0;
	MM_CTX *ctx;
	int mm_res, i;
	struct ast_http_post_mapping *post_map;
	const char *post_dir;
	unsigned long ident = 0;

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
			if (option_debug)
				ast_log(LOG_DEBUG, "Got a Content-Length of %d\n", content_len);
		} else if (!strcasecmp(var->name, "Content-Type"))
			fprintf(f, "Content-Type: %s\r\n\r\n", var->value);
	}

	while ((res = fread(&buf, 1, 1, ser->f))) {
		fwrite(&buf, 1, 1, f);
		content_len--;
		if (!content_len)
			break;
	}

	if (fseek(f, SEEK_SET, 0)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Failed to seek temp file back to beginning.\n");
		fclose(f);
		return NULL;
	}

	AST_RWLIST_RDLOCK(&post_mappings);
	if (!(post_map = find_post_mapping(uri))) {
		if (option_debug)
			ast_log(LOG_DEBUG, "%s is not a valid URI for POST\n", uri);
		AST_RWLIST_UNLOCK(&post_mappings);
		fclose(f);
		*status = 404;
		*title = ast_strdup("Not Found");
		return ast_http_error(404, "Not Found", NULL, "The requested URL was not found on this server.");
	}
	post_dir = ast_strdupa(post_map->to);
	post_map = NULL;
	AST_RWLIST_UNLOCK(&post_mappings);

	if (option_debug)
		ast_log(LOG_DEBUG, "Going to post files to dir %s\n", post_dir);

	if (!(ctx = mm_context_new())) {
		fclose(f);
		return NULL;
	}

	mm_res = mm_parse_fileptr(ctx, f, MM_PARSE_LOOSE, 0);
	fclose(f);
	if (mm_res == -1) {
		ast_log(LOG_ERROR, "Error parsing MIME data\n");
		mm_context_free(ctx);
		*status = 400;
		*title = ast_strdup("Bad Request");
		return ast_http_error(400, "Bad Request", NULL, "The was an error parsing the request.");
	}

	mm_res = mm_context_countparts(ctx);
	if (!mm_res) {
		ast_log(LOG_ERROR, "Invalid MIME data, found no parts!\n");
		mm_context_free(ctx);
		*status = 400;
		*title = ast_strdup("Bad Request");
		return ast_http_error(400, "Bad Request", NULL, "The was an error parsing the request.");
	}

	if (option_debug) {
		if (mm_context_iscomposite(ctx))
			ast_log(LOG_DEBUG, "Found %d MIME parts\n", mm_res - 1);
		else
			ast_log(LOG_DEBUG, "We have a flat (not multi-part) message\n");
	}

	for (i = 1; i < mm_res; i++) {
		struct mm_mimepart *part;
		char fn[PATH_MAX];

		if (!(part = mm_context_getpart(ctx, i))) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Failed to get mime part num %d\n", i);
			continue;
		}

		if (get_filename(part, fn, sizeof(fn))) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Failed to retrieve a filename for part num %d\n", i);
			continue;
		}
	
		if (!part->type) {
			if (option_debug)
				ast_log(LOG_DEBUG, "This part has no content struct?\n");
			continue;
		}

		/* XXX This assumes the MIME part body is not encoded! */
		post_raw(part, post_dir, fn);
	}

	mm_context_free(ctx);

	*status = 200;
	*title = ast_strdup("OK");
	return ast_strdup("");
}

static char *handle_uri(struct sockaddr_in *sin, char *uri, int *status, char **title, int *contentlength, struct ast_variable **cookies)
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
		c = urih->callback(sin, uri, vars, status, title, contentlength);
		ast_rwlock_unlock(&uris_lock);
	} else if (ast_strlen_zero(uri) && ast_strlen_zero(prefix)) {
		/* Special case: If no prefix, and no URI, send to /static/index.html */
		c = ast_http_error(302, "Moved Temporarily", "Location: /static/index.html\r\n", "This is not the page you are looking for...");
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

static void *ast_httpd_helper_thread(void *data)
{
	char buf[4096];
	char cookie[4096];
	char timebuf[256];
	struct ast_http_server_instance *ser = data;
	struct ast_variable *var, *prev=NULL, *vars=NULL, *headers = NULL;
	char *uri, *c, *title=NULL;
	char *vname, *vval;
	int status = 200, contentlength = 0;
	time_t t;

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
                                vname = cookie + 8;
				
				/* If we got an IE cookie string, we need to skip to    
				    past the version to get to the name                 */
				if (*vname == '$') {
					vname = strchr(vname, ';');
					if (vname) { 
						vname++;
						if (*vname == ' ')
							vname++;
					}
				}
				
				if (vname) {
					vval = strchr(vname, '=');
					if (vval) {
						/* Ditch the = and the quotes */
						*vval++ = '\0';
						if (*vval)
							vval++;
						if (strlen(vval))
							vval[strlen(vval) - 1] = '\0';
						var = ast_variable_new(vname, vval);
						if (var) {
							if (prev)
								prev->next = var;
							else
								vars = var;
							prev = var;
						}
					}
				}
			} else {
				char *name, *value;

				value = ast_strdupa(cookie);
				name = strsep(&value, ":");
				if (!value)
					continue;
				value = ast_skip_blanks(value);
				if (ast_strlen_zero(value))
					continue;
				var = ast_variable_new(name, value);
				if (!var)
					continue;
				var->next = headers;
				headers = var;
			}
		}

		if (*uri) {
			if (!strcasecmp(buf, "get")) 
				c = handle_uri(&ser->requestor, uri, &status, &title, &contentlength, &vars);
			else if (!strcasecmp(buf, "post")) 
				c = handle_post(ser, uri, &status, &title, &contentlength, headers, vars);
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
			if (contentlength) {
				char *tmp;
				tmp = strstr(c, "\r\n\r\n");
				if (tmp) {
					ast_cli(ser->fd, "Content-length: %d\r\n", contentlength);
					write(ser->fd, c, (tmp + 4 - c));
					write(ser->fd, tmp + 4, contentlength);
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
		ser = ast_calloc(1, sizeof(*ser));
		if (!ser) {
			ast_log(LOG_WARNING, "No memory for new session: %s\n", strerror(errno));
			close(fd);
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
			}
			pthread_attr_destroy(&attr);
		} else {
			ast_log(LOG_WARNING, "fdopen failed!\n");
			close(ser->fd);
			free(ser);
		}
	}
	return NULL;
}

char *ast_http_setcookie(const char *var, const char *val, int expires, char *buf, size_t buflen)
{
	char *c;
	c = buf;
	ast_build_string(&c, &buflen, "Set-Cookie: %s=\"%s\"; Version=\"1\"", var, val);
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

static void destroy_post_mapping(struct ast_http_post_mapping *post_map)
{
	if (post_map->from)
		free(post_map->from);
	if (post_map->to)
		free(post_map->to);
	free(post_map);
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

	destroy_post_mappings();

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
					
			}
			v = v->next;
		}

		for (v = ast_variable_browse(cfg, "post_mappings"); v; v = v->next)
			add_post_mapping(v->name, v->value);

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
	struct ast_http_post_mapping *post_map;

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

	ast_cli(fd, "\nPOST mappings:\n");
	AST_RWLIST_RDLOCK(&post_mappings);
	AST_LIST_TRAVERSE(&post_mappings, post_map, entry)
		ast_cli(fd, "%s/%s => %s\n", prefix, post_map->from, post_map->to);
	ast_cli(fd, "%s\n", AST_LIST_EMPTY(&post_mappings) ? "None.\n" : "");
	AST_RWLIST_UNLOCK(&post_mappings);

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
	mm_library_init();
	mm_codec_registerdefaultcodecs();

	ast_http_uri_link(&statusuri);
	ast_http_uri_link(&staticuri);
	ast_cli_register_multiple(cli_http, sizeof(cli_http) / sizeof(struct ast_cli_entry));

	return __ast_http_load(0);
}
