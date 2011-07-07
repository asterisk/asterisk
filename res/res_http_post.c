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
 * \brief HTTP POST upload support for Asterisk HTTP server
 *
 * \author Terry Wilson <twilson@digium.com
 *
 * \ref AstHTTP - AMI over the http protocol
 */

/*** MODULEINFO
	<depend>gmime</depend>
 ***/


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>
#include <fcntl.h>
#include <gmime/gmime.h>
#if defined (__OpenBSD__) || defined(__FreeBSD__) || defined(__Darwin__)
#include <libgen.h>
#endif

#include "asterisk/linkedlists.h"
#include "asterisk/http.h"
#include "asterisk/paths.h"	/* use ast_config_AST_DATA_DIR */
#include "asterisk/tcptls.h"
#include "asterisk/manager.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/ast_version.h"

#define MAX_PREFIX 80

/* gmime 2.4 provides a newer interface. */
#ifdef GMIME_TYPE_CONTENT_TYPE
#define AST_GMIME_VER_24
#endif

/* just a little structure to hold callback info for gmime */
struct mime_cbinfo {
	int count;
	const char *post_dir;
};

/* all valid URIs must be prepended by the string in prefix. */
static char prefix[MAX_PREFIX];

static void post_raw(GMimePart *part, const char *post_dir, const char *fn)
{
	char filename[PATH_MAX];
	GMimeDataWrapper *content;
	GMimeStream *stream;
	int fd;

	snprintf(filename, sizeof(filename), "%s/%s", post_dir, fn);

	ast_debug(1, "Posting raw data to %s\n", filename);

	if ((fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666)) == -1) {
		ast_log(LOG_WARNING, "Unable to open %s for writing file from a POST!\n", filename);

		return;
	}

	stream = g_mime_stream_fs_new(fd);

	content = g_mime_part_get_content_object(part);
	g_mime_data_wrapper_write_to_stream(content, stream);
	g_mime_stream_flush(stream);

#ifndef AST_GMIME_VER_24
	g_object_unref(content);
#endif
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

#ifdef AST_GMIME_VER_24
static void process_message_callback(GMimeObject *parent, GMimeObject *part, gpointer user_data)
#else
static void process_message_callback(GMimeObject *part, gpointer user_data)
#endif
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
#ifndef AST_GMIME_VER_24
		GList *l;

		ast_log(LOG_WARNING, "Got unexpected GMIME_IS_MULTIPART, trying to process subparts\n");
		l = GMIME_MULTIPART(part)->subparts;
		while (l) {
			process_message_callback(l->data, cbinfo);
			l = l->next;
		}
#else
		ast_log(LOG_WARNING, "Got unexpected MIME subpart.\n");
#endif
	} else if (GMIME_IS_PART(part)) {
		const char *filename;

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

#ifdef AST_GMIME_VER_24
	g_mime_message_foreach(message, process_message_callback, &cbinfo);
#else
	g_mime_message_foreach_part(message, process_message_callback, &cbinfo);
#endif

	return cbinfo.count;
}

/* Find a sequence of bytes within a binary array. */
static int find_sequence(char * inbuf, int inlen, char * matchbuf, int matchlen)
{
	int current;
	int comp;
	int found = 0;

	for (current = 0; current < inlen-matchlen; current++, inbuf++) {
		if (*inbuf == *matchbuf) {
			found=1;
			for (comp = 1; comp < matchlen; comp++) {
				if (inbuf[comp] != matchbuf[comp]) {
					found = 0;
					break;
				}
			}
			if (found) {
				break;
			}
		}
	}
	if (found) {
		return current;
	} else {
		return -1;
	}
}

/*
* The following is a work around to deal with how IE7 embeds the local file name
* within the Mime header using full WINDOWS file path with backslash directory delimiters.
* This section of code attempts to isolate the directory path and remove it
* from what is written into the output file.  In addition, it changes
* esc chars (i.e. backslashes) to forward slashes.
* This function has two modes.  The first to find a boundary marker.  The
* second is to find the filename immediately after the boundary.
*/
static int readmimefile(FILE * fin, FILE * fout, char * boundary, int contentlen)
{
	int find_filename = 0;
	char buf[4096];
	int marker;
	int x;
	int char_in_buf = 0;
	int num_to_read;
	int boundary_len;
	char * path_end, * path_start, * filespec;

	if (NULL == fin || NULL == fout || NULL == boundary || 0 >= contentlen) {
		return -1;
	}

	boundary_len = strlen(boundary);
	while (0 < contentlen || 0 < char_in_buf) {
		/* determine how much I will read into the buffer */
		if (contentlen > sizeof(buf) - char_in_buf) {
			num_to_read = sizeof(buf)- char_in_buf;
		} else {
			num_to_read = contentlen;
		}

		if (0 < num_to_read) {
			if (fread(&(buf[char_in_buf]), 1, num_to_read, fin) < num_to_read) {
				ast_log(LOG_WARNING, "fread() failed: %s\n", strerror(errno));
				num_to_read = 0;
			}
			contentlen -= num_to_read;
			char_in_buf += num_to_read;
		}
		/* If I am looking for the filename spec */
		if (find_filename) {
			path_end = filespec = NULL;
			x = strlen("filename=\"");
			marker = find_sequence(buf, char_in_buf, "filename=\"", x );
			if (0 <= marker) {
				marker += x;  /* Index beyond the filename marker */
				path_start = &buf[marker];
				for (path_end = path_start, x = 0; x < char_in_buf-marker; x++, path_end++) {
					if ('\\' == *path_end) {	/* convert backslashses to forward slashes */
						*path_end = '/';
					}
					if ('\"' == *path_end) {	/* If at the end of the file name spec */
						*path_end = '\0';		/* temporarily null terminate the file spec for basename */
						filespec = basename(path_start);
						*path_end = '\"';
						break;
					}
				}
			}
			if (filespec) {	/* If the file name path was found in the header */
				if (fwrite(buf, 1, marker, fout) != marker) {
					ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
				}
				x = (int)(path_end+1 - filespec);
				if (fwrite(filespec, 1, x, fout) != x) {
					ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
				}
				x = (int)(path_end+1 - buf);
				memmove(buf, &(buf[x]), char_in_buf-x);
				char_in_buf -= x;
			}
			find_filename = 0;
		} else { /* I am looking for the boundary marker */
			marker = find_sequence(buf, char_in_buf, boundary, boundary_len);
			if (0 > marker) {
				if (char_in_buf < (boundary_len)) {
					/*no possibility to find the boundary, write all you have */
					if (fwrite(buf, 1, char_in_buf, fout) != char_in_buf) {
						ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
					}
					char_in_buf = 0;
				} else {
					/* write all except for area where the boundary marker could be */
					if (fwrite(buf, 1, char_in_buf -(boundary_len -1), fout) != char_in_buf - (boundary_len - 1)) {
						ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
					}
					x = char_in_buf -(boundary_len -1);
					memmove(buf, &(buf[x]), char_in_buf-x);
					char_in_buf = (boundary_len -1);
				}
			} else {
				/* write up through the boundary, then look for filename in the rest */
				if (fwrite(buf, 1, marker + boundary_len, fout) != marker + boundary_len) {
					ast_log(LOG_WARNING, "fwrite() failed: %s\n", strerror(errno));
				}
				x = marker + boundary_len;
				memmove(buf, &(buf[x]), char_in_buf-x);
				char_in_buf -= marker + boundary_len;
				find_filename =1;
			}
		}
	}
	return 0;
}

static int http_post_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *get_vars, struct ast_variable *headers)
{
	struct ast_variable *var, *cookies;
	unsigned long ident = 0;
	FILE *f;
	int content_len = 0;
	struct ast_str *post_dir;
	GMimeMessage *message;
	int message_count = 0;
	char * boundary_marker = NULL;

	if (method != AST_HTTP_POST) {
		ast_http_error(ser, 501, "Not Implemented", "Attempt to use unimplemented / unsupported method");
		return -1;
	}

	if (!astman_is_authed(ast_http_manid_from_vars(headers))) {
		ast_http_error(ser, 403, "Access Denied", "Sorry, I cannot let you do that, Dave.");
		return -1;
	}

	if (!urih) {
		ast_http_error(ser, 400, "Missing URI handle", "There was an error parsing the request");
	        return -1;
	}

	cookies = ast_http_get_cookies(headers);
	for (var = cookies; var; var = var->next) {
		if (!strcasecmp(var->name, "mansession_id")) {
			sscanf(var->value, "%30lx", &ident);
			break;
		}
	}
	if (cookies) {
		ast_variables_destroy(cookies);
	}

	if (ident == 0) {
		ast_http_error(ser, 401, "Unauthorized", "You are not authorized to make this request.");
		return -1;
	}
	if (!astman_verify_session_writepermissions(ident, EVENT_FLAG_CONFIG)) {
		ast_http_error(ser, 401, "Unauthorized", "You are not authorized to make this request.");
		return -1;
	}

	if (!(f = tmpfile())) {
		ast_log(LOG_ERROR, "Could not create temp file.\n");
		ast_http_error(ser, 500, "Internal server error", "Could not create temp file.");
		return -1;
	}

	for (var = headers; var; var = var->next) {
		fprintf(f, "%s: %s\r\n", var->name, var->value);

		if (!strcasecmp(var->name, "Content-Length")) {
			if ((sscanf(var->value, "%30u", &content_len)) != 1) {
				ast_log(LOG_ERROR, "Invalid Content-Length in POST request!\n");
				fclose(f);
				ast_http_error(ser, 500, "Internal server error", "Invalid Content-Length in POST request!");
				return -1;
			}
			ast_debug(1, "Got a Content-Length of %d\n", content_len);
		} else if (!strcasecmp(var->name, "Content-Type")) {
			boundary_marker = strstr(var->value, "boundary=");
			if (boundary_marker) {
				boundary_marker += strlen("boundary=");
			}
		}
	}

	fprintf(f, "\r\n");

	if (0 > readmimefile(ser->f, f, boundary_marker, content_len)) {
		ast_debug(1, "Cannot find boundary marker in POST request.\n");
		fclose(f);

		return -1;
	}

	if (fseek(f, SEEK_SET, 0)) {
		ast_log(LOG_ERROR, "Failed to seek temp file back to beginning.\n");
		fclose(f);
		ast_http_error(ser, 500, "Internal server error", "Failed to seek temp file back to beginning.");
		return -1;
	}

	post_dir = urih->data;

	message = parse_message(f); /* Takes ownership and will close f */

	if (!message) {
		ast_log(LOG_ERROR, "Error parsing MIME data\n");

		ast_http_error(ser, 400, "Bad Request", "The was an error parsing the request.");
		return -1;
	}

	if (!(message_count = process_message(message, ast_str_buffer(post_dir)))) {
		ast_log(LOG_ERROR, "Invalid MIME data, found no parts!\n");
		g_object_unref(message);
		ast_http_error(ser, 400, "Bad Request", "The was an error parsing the request.");
		return -1;
	}
	g_object_unref(message);

	ast_http_error(ser, 200, "OK", "File successfully uploaded.");
	return 0;
}

static int __ast_http_post_load(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	cfg = ast_config_load2("http.conf", "http", config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEUNCHANGED || cfg == CONFIG_STATUS_FILEINVALID) {
		return 0;
	}

	if (reload) {
		ast_http_uri_unlink_all_with_key(__FILE__);
	}

	if (cfg) {
		for (v = ast_variable_browse(cfg, "general"); v; v = v->next) {
			if (!strcasecmp(v->name, "prefix")) {
				ast_copy_string(prefix, v->value, sizeof(prefix));
				if (prefix[strlen(prefix)] == '/') {
					prefix[strlen(prefix)] = '\0';
				}
			}
		}

		for (v = ast_variable_browse(cfg, "post_mappings"); v; v = v->next) {
			struct ast_http_uri *urih;
			struct ast_str *ds;

			if (!(urih = ast_calloc(sizeof(*urih), 1))) {
				ast_config_destroy(cfg);
				return -1;
			}

			if (!(ds = ast_str_create(32))) {
				ast_free(urih);
				ast_config_destroy(cfg);
				return -1;
			}

			urih->description = ast_strdup("HTTP POST mapping");
			urih->uri = ast_strdup(v->name);
			ast_str_set(&ds, 0, "%s", v->value);
			urih->data = ds;
			urih->has_subtree = 0;
			urih->callback = http_post_callback;
			urih->key = __FILE__;
			urih->mallocd = urih->dmallocd = 1;

			ast_http_uri_link(urih);
		}

		ast_config_destroy(cfg);
	}
	return 0;
}

static int unload_module(void)
{
	ast_http_uri_unlink_all_with_key(__FILE__);

	return 0;
}

static int reload(void)
{
	__ast_http_post_load(1);

	return AST_MODULE_LOAD_SUCCESS;
}

static int load_module(void)
{
	g_mime_init(0);

	__ast_http_post_load(0);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "HTTP POST support",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);
