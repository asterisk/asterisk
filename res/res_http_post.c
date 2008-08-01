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

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 111213 $")

#include <sys/stat.h>
#include <fcntl.h>
#include <gmime/gmime.h>

#include "asterisk/linkedlists.h"
#include "asterisk/http.h"
#include "asterisk/paths.h"	/* use ast_config_AST_DATA_DIR */
#include "asterisk/tcptls.h"
#include "asterisk/manager.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/ast_version.h"

#define MAX_PREFIX 80

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
		l = GMIME_MULTIPART(part)->subparts;
		while (l) {
			process_message_callback(l->data, cbinfo);
			l = l->next;
		}
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

	g_mime_message_foreach_part(message, process_message_callback, &cbinfo);

	return cbinfo.count;
}

static struct ast_str *http_post_callback(struct ast_tcptls_session_instance *ser, const struct ast_http_uri *urih, const char *uri, enum ast_http_method method, struct ast_variable *vars, struct ast_variable *headers, int *status, char **title, int *contentlength)
{
	struct ast_variable *var;
	unsigned long ident = 0;
	char buf[4096];
	FILE *f;
	size_t res;
	int content_len = 0;
	struct ast_str *post_dir;
	GMimeMessage *message;
	int message_count = 0;

	if (!urih) {
		return ast_http_error((*status = 400),
			   (*title = ast_strdup("Missing URI handle")),
			   NULL, "There was an error parsing the request");
	}

	for (var = vars; var; var = var->next) {
		if (strcasecmp(var->name, "mansession_id")) {
			continue;
		}

		if (sscanf(var->value, "%lx", &ident) != 1) {
			return ast_http_error((*status = 400),
					      (*title = ast_strdup("Bad Request")),
					      NULL, "The was an error parsing the request.");
		}

		if (!astman_verify_session_writepermissions(ident, EVENT_FLAG_CONFIG)) {
			return ast_http_error((*status = 401),
					      (*title = ast_strdup("Unauthorized")),
					      NULL, "You are not authorized to make this request.");
		}

		break;
	}

	if (!var) {
		return ast_http_error((*status = 401),
				      (*title = ast_strdup("Unauthorized")),
				      NULL, "You are not authorized to make this request.");
	}

	if (!(f = tmpfile())) {
		ast_log(LOG_ERROR, "Could not create temp file.\n");
		return NULL;
	}

	for (var = headers; var; var = var->next) {
		fprintf(f, "%s: %s\r\n", var->name, var->value);

		if (!strcasecmp(var->name, "Content-Length")) {
			if ((sscanf(var->value, "%u", &content_len)) != 1) {
				ast_log(LOG_ERROR, "Invalid Content-Length in POST request!\n");
				fclose(f);

				return NULL;
			}
			ast_debug(1, "Got a Content-Length of %d\n", content_len);
		}
	}

	fprintf(f, "\r\n");

	for (res = sizeof(buf); content_len; content_len -= res) {
		if (content_len < res) {
			res = content_len;
		}
		fread(buf, 1, res, ser->f);
		fwrite(buf, 1, res, f);
	}

	if (fseek(f, SEEK_SET, 0)) {
		ast_log(LOG_ERROR, "Failed to seek temp file back to beginning.\n");
		fclose(f);

		return NULL;
	}

	post_dir = urih->data;

	message = parse_message(f); /* Takes ownership and will close f */

	if (!message) {
		ast_log(LOG_ERROR, "Error parsing MIME data\n");

		return ast_http_error((*status = 400),
				      (*title = ast_strdup("Bad Request")),
				      NULL, "The was an error parsing the request.");
	}

	if (!(message_count = process_message(message, post_dir->str))) {
		ast_log(LOG_ERROR, "Invalid MIME data, found no parts!\n");
		g_object_unref(message);
		return ast_http_error((*status = 400),
				      (*title = ast_strdup("Bad Request")),
				      NULL, "The was an error parsing the request.");
	}

	g_object_unref(message);

	return ast_http_error((*status = 200),
			      (*title = ast_strdup("OK")),
			      NULL, "File successfully uploaded.");
}

static int __ast_http_post_load(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	if ((cfg = ast_config_load2("http.conf", "http", config_flags)) == CONFIG_STATUS_FILEUNCHANGED) {
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
				return -1;
			}

			if (!(ds = ast_str_create(32)))
				return -1;


			urih->description = ast_strdup("HTTP POST mapping");
			urih->uri = ast_strdup(v->name);
			ast_str_set(&ds, 0, "%s/%s", prefix, v->value);
			urih->data = ds;
			urih->has_subtree = 0;
			urih->supports_get = 0;
			urih->supports_post = 1;
			urih->callback = http_post_callback;
			urih->key = __FILE__;

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
