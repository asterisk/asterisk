/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

/*! \file
 *
 * \brief HTTP binding for the Stasis API
 * \author David M. Lee, II <dlee@digium.com>
 *
 * The API itself is documented using <a
 * href="https://developers.helloreverb.com/swagger/">Swagger</a>, a lightweight
 * mechanism for documenting RESTful API's using JSON. This allows us to use <a
 * href="https://github.com/wordnik/swagger-ui">swagger-ui</a> to provide
 * executable documentation for the API, generate client bindings in different
 * <a href="https://github.com/asterisk/asterisk_rest_libraries">languages</a>,
 * and generate a lot of the boilerplate code for implementing the RESTful
 * bindings. The API docs live in the \c rest-api/ directory.
 *
 * The RESTful bindings are generated from the Swagger API docs using a set of
 * <a href="http://mustache.github.io/mustache.5.html">Mustache</a> templates.
 * The code generator is written in Python, and uses the Python implementation
 * <a href="https://github.com/defunkt/pystache">pystache</a>. Pystache has no
 * dependencies, and be installed easily using \c pip. Code generation code
 * lives in \c rest-api-templates/.
 *
 * The generated code reduces a lot of boilerplate when it comes to handling
 * HTTP requests. It also helps us have greater consistency in the REST API.
 *
 * The structure of the generated code is:
 *
 *  - res/ari/resource_{resource}.h
 *    - For each operation in the resource, a generated argument structure
 *      (holding the parsed arguments from the request) and function
 *      declarations (to implement in res/ari/resource_{resource}.c)
 *  - res_ari_{resource}.c
 *    - A set of \ref stasis_rest_callback functions, which glue the two
 *      together. They parse out path variables and request parameters to
 *      populate a specific \c *_args which is passed to the specific request
 *      handler (in res/ari/resource_{resource}.c)
 *    - A tree of \ref stasis_rest_handlers for routing requests to its
 *      \ref stasis_rest_callback
 *
 * The basic flow of an HTTP request is:
 *
 *  - ast_ari_callback()
 *    1. Initial request validation
 *    2. Routes as either a doc request (ast_ari_get_docs) or API
 *       request (ast_ari_invoke)
 *       - ast_ari_invoke()
 *         1. Further request validation
 *         2. Routes the request through the tree of generated
 *            \ref stasis_rest_handlers.
 *         3. Dispatch to the generated callback
 *            - \c ast_ari_*_cb
 *              1. Populate \c *_args struct with path and get params
 *              2. Invoke the request handler
 *    3. Validates and sends response
 */

/*** MODULEINFO
	<depend type="module">res_http_websocket</depend>
	<depend type="module">res_stasis</depend>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_ari" language="en_US">
		<synopsis>HTTP binding for the Stasis API</synopsis>
		<configFile name="ari.conf">
			<configObject name="general">
				<synopsis>General configuration settings</synopsis>
				<configOption name="enabled">
					<synopsis>Enable/disable the ARI module</synopsis>
					<description>
						<para>This option enables or disables the ARI module.</para>
						<note>
							<para>ARI uses Asterisk's HTTP server, which must also be enabled in <filename>http.conf</filename>.</para>
						</note>
					</description>
					<see-also>
						<ref type="filename">http.conf</ref>
						<ref type="link">https://wiki.asterisk.org/wiki/display/AST/Asterisk+Builtin+mini-HTTP+Server</ref>
					</see-also>
				</configOption>
				<configOption name="websocket_write_timeout">
					<synopsis>The timeout (in milliseconds) to set on WebSocket connections.</synopsis>
					<description>
						<para>If a websocket connection accepts input slowly, the timeout
						for writes to it can be increased to keep it from being disconnected.
						Value is in milliseconds; default is 100 ms.</para>
					</description>
				</configOption>
				<configOption name="pretty">
					<synopsis>Responses from ARI are formatted to be human readable</synopsis>
				</configOption>
				<configOption name="auth_realm">
					<synopsis>Realm to use for authentication. Defaults to Asterisk REST Interface.</synopsis>
				</configOption>
				<configOption name="allowed_origins">
					<synopsis>Comma separated list of allowed origins, for Cross-Origin Resource Sharing. May be set to * to allow all origins.</synopsis>
				</configOption>
				<configOption name="channelvars">
					<synopsis>Comma separated list of channel variables to display in channel json.</synopsis>
				</configOption>
			</configObject>

			<configObject name="user">
				<synopsis>Per-user configuration settings</synopsis>
				<configOption name="type">
					<synopsis>Define this configuration section as a user.</synopsis>
					<description>
						<enumlist>
							<enum name="user"><para>Configure this section as a <replaceable>user</replaceable></para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="read_only">
					<synopsis>When set to yes, user is only authorized for read-only requests</synopsis>
				</configOption>
				<configOption name="password">
					<synopsis>Crypted or plaintext password (see password_format)</synopsis>
				</configOption>
				<configOption name="password_format">
					<synopsis>password_format may be set to plain (the default) or crypt. When set to crypt, crypt(3) is used to validate the password. A crypted password can be generated using mkpasswd -m sha-512. When set to plain, the password is in plaintext</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

#include "asterisk.h"

#include "ari/internal.h"
#include "asterisk/ari.h"
#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"
#include "asterisk/stasis_app.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*! \brief Helper function to check if module is enabled. */
static int is_enabled(void)
{
	RAII_VAR(struct ast_ari_conf *, cfg, ast_ari_config_get(), ao2_cleanup);
	return cfg && cfg->general && cfg->general->enabled;
}

/*! Lock for \ref root_handler */
static ast_mutex_t root_handler_lock;

/*! Handler for root RESTful resource. */
static struct stasis_rest_handlers *root_handler;

/*! Pre-defined message for allocation failures. */
static struct ast_json *oom_json;

struct ast_json *ast_ari_oom_json(void)
{
	return oom_json;
}

int ast_ari_add_handler(struct stasis_rest_handlers *handler)
{
	RAII_VAR(struct stasis_rest_handlers *, new_handler, NULL, ao2_cleanup);
	size_t old_size, new_size;

	SCOPED_MUTEX(lock, &root_handler_lock);

	old_size = sizeof(*new_handler) + root_handler->num_children * sizeof(handler);
	new_size = old_size + sizeof(handler);

	new_handler = ao2_alloc(new_size, NULL);
	if (!new_handler) {
		return -1;
	}
	memcpy(new_handler, root_handler, old_size);
	new_handler->children[new_handler->num_children++] = handler;

	ao2_cleanup(root_handler);
	ao2_ref(new_handler, +1);
	root_handler = new_handler;
	return 0;
}

int ast_ari_remove_handler(struct stasis_rest_handlers *handler)
{
	struct stasis_rest_handlers *new_handler;
	size_t size;
	size_t i;
	size_t j;

	ast_assert(root_handler != NULL);

	ast_mutex_lock(&root_handler_lock);
	size = sizeof(*new_handler) + root_handler->num_children * sizeof(handler);

	new_handler = ao2_alloc(size, NULL);
	if (!new_handler) {
		ast_mutex_unlock(&root_handler_lock);
		return -1;
	}

	/* Create replacement root_handler less the handler to remove. */
	memcpy(new_handler, root_handler, sizeof(*new_handler));
	for (i = 0, j = 0; i < root_handler->num_children; ++i) {
		if (root_handler->children[i] == handler) {
			continue;
		}
		new_handler->children[j++] = root_handler->children[i];
	}
	new_handler->num_children = j;

	/* Replace the old root_handler with the new. */
	ao2_cleanup(root_handler);
	root_handler = new_handler;

	ast_mutex_unlock(&root_handler_lock);
	return 0;
}

static struct stasis_rest_handlers *get_root_handler(void)
{
	SCOPED_MUTEX(lock, &root_handler_lock);
	ao2_ref(root_handler, +1);
	return root_handler;
}

static struct stasis_rest_handlers *root_handler_create(void)
{
	RAII_VAR(struct stasis_rest_handlers *, handler, NULL, ao2_cleanup);

	handler = ao2_alloc(sizeof(*handler), NULL);
	if (!handler) {
		return NULL;
	}
	handler->path_segment = "ari";

	ao2_ref(handler, +1);
	return handler;
}

void ast_ari_response_error(struct ast_ari_response *response,
				int response_code,
				const char *response_text,
				const char *message_fmt, ...)
{
	RAII_VAR(struct ast_json *, message, NULL, ast_json_unref);
	va_list ap;

	va_start(ap, message_fmt);
	message = ast_json_vstringf(message_fmt, ap);
	va_end(ap);
	response->message = ast_json_pack("{s: o}",
					  "message", ast_json_ref(message));
	response->response_code = response_code;
	response->response_text = response_text;
}

void ast_ari_response_ok(struct ast_ari_response *response,
			     struct ast_json *message)
{
	response->message = message;
	response->response_code = 200;
	response->response_text = "OK";
}

void ast_ari_response_no_content(struct ast_ari_response *response)
{
	response->message = ast_json_null();
	response->response_code = 204;
	response->response_text = "No Content";
}

void ast_ari_response_accepted(struct ast_ari_response *response)
{
	response->message = ast_json_null();
	response->response_code = 202;
	response->response_text = "Accepted";
}

void ast_ari_response_alloc_failed(struct ast_ari_response *response)
{
	response->message = ast_json_ref(oom_json);
	response->response_code = 500;
	response->response_text = "Internal Server Error";
}

void ast_ari_response_created(struct ast_ari_response *response,
	const char *url, struct ast_json *message)
{
	RAII_VAR(struct stasis_rest_handlers *, root, get_root_handler(), ao2_cleanup);
	response->message = message;
	response->response_code = 201;
	response->response_text = "Created";
	ast_str_append(&response->headers, 0, "Location: /%s%s\r\n", root->path_segment, url);
}

static void add_allow_header(struct stasis_rest_handlers *handler,
			     struct ast_ari_response *response)
{
	enum ast_http_method m;
	ast_str_append(&response->headers, 0,
		       "Allow: OPTIONS");
	for (m = 0; m < AST_HTTP_MAX_METHOD; ++m) {
		if (handler->callbacks[m] != NULL) {
			ast_str_append(&response->headers, 0,
				       ",%s", ast_get_http_method(m));
		}
	}
	ast_str_append(&response->headers, 0, "\r\n");
}

static int origin_allowed(const char *origin)
{
	RAII_VAR(struct ast_ari_conf *, cfg, ast_ari_config_get(), ao2_cleanup);

	char *allowed = ast_strdupa(cfg->general->allowed_origins);
	char *current;

	while ((current = strsep(&allowed, ","))) {
		if (!strcmp(current, "*")) {
			return 1;
		}

		if (!strcmp(current, origin)) {
			return 1;
		}
	}

	return 0;
}

#define ACR_METHOD "Access-Control-Request-Method"
#define ACR_HEADERS "Access-Control-Request-Headers"
#define ACA_METHODS "Access-Control-Allow-Methods"
#define ACA_HEADERS "Access-Control-Allow-Headers"

/*!
 * \brief Handle OPTIONS request, mainly for CORS preflight requests.
 *
 * Some browsers will send this prior to non-simple methods (i.e. DELETE).
 * See http://www.w3.org/TR/cors/ for the spec. Especially section 6.2.
 */
static void handle_options(struct stasis_rest_handlers *handler,
			   struct ast_variable *headers,
			   struct ast_ari_response *response)
{
	struct ast_variable *header;
	char const *acr_method = NULL;
	char const *acr_headers = NULL;
	char const *origin = NULL;

	RAII_VAR(struct ast_str *, allow, NULL, ast_free);
	enum ast_http_method m;
	int allowed = 0;

	/* Regular OPTIONS response */
	add_allow_header(handler, response);
	ast_ari_response_no_content(response);

	/* Parse CORS headers */
	for (header = headers; header != NULL; header = header->next) {
		if (strcmp(ACR_METHOD, header->name) == 0) {
			acr_method = header->value;
		} else if (strcmp(ACR_HEADERS, header->name) == 0) {
			acr_headers = header->value;
		} else if (strcmp("Origin", header->name) == 0) {
			origin = header->value;
		}
	}

	/* CORS 6.2, #1 - "If the Origin header is not present terminate this
	 * set of steps."
	 */
	if (origin == NULL) {
		return;
	}

	/* CORS 6.2, #2 - "If the value of the Origin header is not a
	 * case-sensitive match for any of the values in list of origins do not
	 * set any additional headers and terminate this set of steps.
	 *
	 * Always matching is acceptable since the list of origins can be
	 * unbounded.
	 *
	 * The Origin header can only contain a single origin as the user agent
	 * will not follow redirects."
	 */
	if (!origin_allowed(origin)) {
		ast_log(LOG_NOTICE, "Origin header '%s' does not match an allowed origin.\n", origin);
		return;
	}

	/* CORS 6.2, #3 - "If there is no Access-Control-Request-Method header
	 * or if parsing failed, do not set any additional headers and terminate
	 * this set of steps."
	 */
	if (acr_method == NULL) {
		return;
	}

	/* CORS 6.2, #4 - "If there are no Access-Control-Request-Headers
	 * headers let header field-names be the empty list."
	 */
	if (acr_headers == NULL) {
		acr_headers = "";
	}

	/* CORS 6.2, #5 - "If method is not a case-sensitive match for any of
	 * the values in list of methods do not set any additional headers and
	 * terminate this set of steps."
	 */
	allow = ast_str_create(20);

	if (!allow) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	/* Go ahead and build the ACA_METHODS header at the same time */
	for (m = 0; m < AST_HTTP_MAX_METHOD; ++m) {
		if (handler->callbacks[m] != NULL) {
			char const *m_str = ast_get_http_method(m);
			if (strcmp(m_str, acr_method) == 0) {
				allowed = 1;
			}
			ast_str_append(&allow, 0, ",%s", m_str);
		}
	}

	if (!allowed) {
		return;
	}

	/* CORS 6.2 #6 - "If any of the header field-names is not a ASCII
	 * case-insensitive match for any of the values in list of headers do
	 * not set any additional headers and terminate this set of steps.
	 *
	 * Note: Always matching is acceptable since the list of headers can be
	 * unbounded."
	 */

	/* CORS 6.2 #7 - "If the resource supports credentials add a single
	 * Access-Control-Allow-Origin header, with the value of the Origin
	 * header as value, and add a single Access-Control-Allow-Credentials
	 * header with the case-sensitive string "true" as value."
	 *
	 * Added by process_cors_request() earlier in the request.
	 */

	/* CORS 6.2 #8 - "Optionally add a single Access-Control-Max-Age
	 * header..."
	 */

	/* CORS 6.2 #9 - "Add one or more Access-Control-Allow-Methods headers
	 * consisting of (a subset of) the list of methods."
	 */
	ast_str_append(&response->headers, 0, "%s: OPTIONS%s\r\n",
		       ACA_METHODS, ast_str_buffer(allow));


	/* CORS 6.2, #10 - "Add one or more Access-Control-Allow-Headers headers
	 * consisting of (a subset of) the list of headers.
	 *
	 * Since the list of headers can be unbounded simply returning headers
	 * can be enough."
	 */
	if (!ast_strlen_zero(acr_headers)) {
		ast_str_append(&response->headers, 0, "%s: %s\r\n",
			       ACA_HEADERS, acr_headers);
	}
}

void ast_ari_invoke(struct ast_tcptls_session_instance *ser,
	const char *uri, enum ast_http_method method,
	struct ast_variable *get_params, struct ast_variable *headers,
	struct ast_json *body, struct ast_ari_response *response)
{
	RAII_VAR(struct stasis_rest_handlers *, root, NULL, ao2_cleanup);
	struct stasis_rest_handlers *handler;
	struct stasis_rest_handlers *wildcard_handler = NULL;
	RAII_VAR(struct ast_variable *, path_vars, NULL, ast_variables_destroy);
	char *path = ast_strdupa(uri);
	char *path_segment;
	stasis_rest_callback callback;

	root = handler = get_root_handler();
	ast_assert(root != NULL);

	ast_debug(3, "Finding handler for %s\n", path);

	while ((path_segment = strsep(&path, "/")) && (strlen(path_segment) > 0)) {
		struct stasis_rest_handlers *found_handler = NULL;
		int i;

		ast_uri_decode(path_segment, ast_uri_http_legacy);
		ast_debug(3, "  Finding handler for %s\n", path_segment);

		for (i = 0; found_handler == NULL && i < handler->num_children; ++i) {
			struct stasis_rest_handlers *child = handler->children[i];

			if (child->is_wildcard) {
				/* Record the path variable */
				struct ast_variable *path_var = ast_variable_new(child->path_segment, path_segment, __FILE__);
				path_var->next = path_vars;
				path_vars = path_var;
				wildcard_handler = child;
				ast_debug(3, "        Checking %s %s:  Matched wildcard.\n", handler->path_segment, child->path_segment);

			} else if (strcmp(child->path_segment, path_segment) == 0) {
				found_handler = child;
				ast_debug(3, "        Checking %s %s:  Explicit match with %s\n", handler->path_segment, child->path_segment, path_segment);
			} else {
				ast_debug(3, "        Checking %s %s:  Didn't match %s\n", handler->path_segment, child->path_segment, path_segment);
			}
		}

		if (!found_handler && wildcard_handler) {
			ast_debug(3, "  No explicit handler found for %s.  Using wildcard %s.\n",
				path_segment, wildcard_handler->path_segment);
			found_handler = wildcard_handler;
			wildcard_handler = NULL;
		}

		if (found_handler == NULL) {
			/* resource not found */
			ast_debug(3, "  Handler not found for %s\n", path_segment);
			ast_ari_response_error(
				response, 404, "Not Found",
				"Resource not found");
			return;
		} else {
			handler = found_handler;
		}
	}

	ast_assert(handler != NULL);
	if (method == AST_HTTP_OPTIONS) {
		handle_options(handler, headers, response);
		return;
	}

	if (method < 0 || method >= AST_HTTP_MAX_METHOD) {
		add_allow_header(handler, response);
		ast_ari_response_error(
			response, 405, "Method Not Allowed",
			"Invalid method");
		return;
	}

	if (handler->ws_server && method == AST_HTTP_GET) {
		/* WebSocket! */
		ari_handle_websocket(handler->ws_server, ser, uri, method,
			get_params, headers);
		/* Since the WebSocket code handles the connection, we shouldn't
		 * do anything else; setting no_response */
		response->no_response = 1;
		return;
	}

	callback = handler->callbacks[method];
	if (callback == NULL) {
		add_allow_header(handler, response);
		ast_ari_response_error(
			response, 405, "Method Not Allowed",
			"Invalid method");
		return;
	}

	callback(ser, get_params, path_vars, headers, body, response);
	if (response->message == NULL && response->response_code == 0) {
		/* Really should not happen */
		ast_log(LOG_ERROR, "ARI %s %s not implemented\n",
			ast_get_http_method(method), uri);
		ast_ari_response_error(
			response, 501, "Not Implemented",
			"Method not implemented");
	}
}

void ast_ari_get_docs(const char *uri, const char *prefix, struct ast_variable *headers,
			  struct ast_ari_response *response)
{
	RAII_VAR(struct ast_str *, absolute_path_builder, NULL, ast_free);
	RAII_VAR(char *, absolute_api_dirname, NULL, ast_std_free);
	RAII_VAR(char *, absolute_filename, NULL, ast_std_free);
	struct ast_json *obj = NULL;
	struct ast_variable *host = NULL;
	struct ast_json_error error = {};
	struct stat file_stat;

	ast_debug(3, "%s(%s)\n", __func__, uri);

	absolute_path_builder = ast_str_create(80);
	if (absolute_path_builder == NULL) {
		ast_ari_response_alloc_failed(response);
		return;
	}

	/* absolute path to the rest-api directory */
	ast_str_append(&absolute_path_builder, 0, "%s", ast_config_AST_DATA_DIR);
	ast_str_append(&absolute_path_builder, 0, "/rest-api/");
	absolute_api_dirname = realpath(ast_str_buffer(absolute_path_builder), NULL);
	if (absolute_api_dirname == NULL) {
		ast_log(LOG_ERROR, "Error determining real directory for rest-api\n");
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Cannot find rest-api directory");
		return;
	}

	/* absolute path to the requested file */
	ast_str_append(&absolute_path_builder, 0, "%s", uri);
	absolute_filename = realpath(ast_str_buffer(absolute_path_builder), NULL);
	if (absolute_filename == NULL) {
		switch (errno) {
		case ENAMETOOLONG:
		case ENOENT:
		case ENOTDIR:
			ast_ari_response_error(
				response, 404, "Not Found",
				"Resource not found");
			break;
		case EACCES:
			ast_ari_response_error(
				response, 403, "Forbidden",
				"Permission denied");
			break;
		default:
			ast_log(LOG_ERROR,
				"Error determining real path for uri '%s': %s\n",
				uri, strerror(errno));
			ast_ari_response_error(
				response, 500, "Internal Server Error",
				"Cannot find file");
			break;
		}
		return;
	}

	if (!ast_begins_with(absolute_filename, absolute_api_dirname)) {
		/* HACKERZ! */
		ast_log(LOG_ERROR,
			"Invalid attempt to access '%s' (not in %s)\n",
			absolute_filename, absolute_api_dirname);
		ast_ari_response_error(
			response, 404, "Not Found",
			"Resource not found");
		return;
	}

	if (stat(absolute_filename, &file_stat) == 0) {
		if (!(file_stat.st_mode & S_IFREG)) {
			/* Not a file */
			ast_ari_response_error(
				response, 403, "Forbidden",
				"Invalid access");
			return;
		}
	} else {
		/* Does not exist */
		ast_ari_response_error(
			response, 404, "Not Found",
			"Resource not found");
		return;
	}

	/* Load resource object from file */
	obj = ast_json_load_new_file(absolute_filename, &error);
	if (obj == NULL) {
		ast_log(LOG_ERROR, "Error parsing resource file: %s:%d(%d) %s\n",
			error.source, error.line, error.column, error.text);
		ast_ari_response_error(
			response, 500, "Internal Server Error",
			"Yikes! Cannot parse resource");
		return;
	}

	/* Update the basePath properly */
	if (ast_json_object_get(obj, "basePath") != NULL) {
		for (host = headers; host; host = host->next) {
			if (strcasecmp(host->name, "Host") == 0) {
				break;
			}
		}
		if (host != NULL) {
			if (prefix != NULL && strlen(prefix) > 0) {
				ast_json_object_set(
					obj, "basePath",
					ast_json_stringf("http://%s%s/ari", host->value,prefix));
			} else {
				ast_json_object_set(
					obj, "basePath",
					ast_json_stringf("http://%s/ari", host->value));
			}
		} else {
			/* Without the host, we don't have the basePath */
			ast_json_object_del(obj, "basePath");
		}
	}

	ast_ari_response_ok(response, obj);
}

static void remove_trailing_slash(const char *uri,
				  struct ast_ari_response *response)
{
	char *slashless = ast_strdupa(uri);
	slashless[strlen(slashless) - 1] = '\0';

	/* While it's tempting to redirect the client to the slashless URL,
	 * that is problematic. A 302 Found is the most appropriate response,
	 * but most clients issue a GET on the location you give them,
	 * regardless of the method of the original request.
	 *
	 * While there are some ways around this, it gets into a lot of client
	 * specific behavior and corner cases in the HTTP standard. There's also
	 * very little practical benefit of redirecting; only GET and HEAD can
	 * be redirected automagically; all other requests "MUST NOT
	 * automatically redirect the request unless it can be confirmed by the
	 * user, since this might change the conditions under which the request
	 * was issued."
	 *
	 * Given all of that, a 404 with a nice message telling them what to do
	 * is probably our best bet.
	 */
	ast_ari_response_error(response, 404, "Not Found",
		"ARI URLs do not end with a slash. Try /ari/%s", slashless);
}

/*!
 * \brief Handle CORS headers for simple requests.
 *
 * See http://www.w3.org/TR/cors/ for the spec. Especially section 6.1.
 */
static void process_cors_request(struct ast_variable *headers,
				 struct ast_ari_response *response)
{
	char const *origin = NULL;
	struct ast_variable *header;

	/* Parse CORS headers */
	for (header = headers; header != NULL; header = header->next) {
		if (strcmp("Origin", header->name) == 0) {
			origin = header->value;
		}
	}

	/* CORS 6.1, #1 - "If the Origin header is not present terminate this
	 * set of steps."
	 */
	if (origin == NULL) {
		return;
	}

	/* CORS 6.1, #2 - "If the value of the Origin header is not a
	 * case-sensitive match for any of the values in list of origins, do not
	 * set any additional headers and terminate this set of steps.
	 *
	 * Note: Always matching is acceptable since the list of origins can be
	 * unbounded."
	 */
	if (!origin_allowed(origin)) {
		ast_log(LOG_NOTICE, "Origin header '%s' does not match an allowed origin.\n", origin);
		return;
	}

	/* CORS 6.1, #3 - "If the resource supports credentials add a single
	 * Access-Control-Allow-Origin header, with the value of the Origin
	 * header as value, and add a single Access-Control-Allow-Credentials
	 * header with the case-sensitive string "true" as value.
	 *
	 * Otherwise, add a single Access-Control-Allow-Origin header, with
	 * either the value of the Origin header or the string "*" as value."
	 */
	ast_str_append(&response->headers, 0,
		       "Access-Control-Allow-Origin: %s\r\n", origin);
	ast_str_append(&response->headers, 0,
		       "Access-Control-Allow-Credentials: true\r\n");

	/* CORS 6.1, #4 - "If the list of exposed headers is not empty add one
	 * or more Access-Control-Expose-Headers headers, with as values the
	 * header field names given in the list of exposed headers."
	 *
	 * No exposed headers; skipping
	 */
}

enum ast_json_encoding_format ast_ari_json_format(void)
{
	RAII_VAR(struct ast_ari_conf *, cfg, NULL, ao2_cleanup);
	cfg = ast_ari_config_get();
	return cfg->general->format;
}

/*!
 * \brief Authenticate a <code>?api_key=userid:password</code>
 *
 * \param api_key API key query parameter
 * \return User object for the authenticated user.
 * \return \c NULL if authentication failed.
 */
static struct ast_ari_conf_user *authenticate_api_key(const char *api_key)
{
	RAII_VAR(char *, copy, NULL, ast_free);
	char *username;
	char *password;

	password = copy = ast_strdup(api_key);
	if (!copy) {
		return NULL;
	}

	username = strsep(&password, ":");
	if (!password) {
		ast_log(LOG_WARNING, "Invalid api_key\n");
		return NULL;
	}

	return ast_ari_config_validate_user(username, password);
}

/*!
 * \brief Authenticate an HTTP request.
 *
 * \param get_params GET parameters of the request.
 * \param header HTTP headers.
 * \return User object for the authenticated user.
 * \return \c NULL if authentication failed.
 */
static struct ast_ari_conf_user *authenticate_user(struct ast_variable *get_params,
	struct ast_variable *headers)
{
	RAII_VAR(struct ast_http_auth *, http_auth, NULL, ao2_cleanup);
	struct ast_variable *v;

	/* HTTP Basic authentication */
	http_auth = ast_http_get_auth(headers);
	if (http_auth) {
		return ast_ari_config_validate_user(http_auth->userid,
			http_auth->password);
	}

	/* ?api_key authentication */
	for (v = get_params; v; v = v->next) {
		if (strcasecmp("api_key", v->name) == 0) {
			return authenticate_api_key(v->value);
		}
	}

	return NULL;
}

/*!
 * \internal
 * \brief ARI HTTP handler.
 *
 * This handler takes the HTTP request and turns it into the appropriate
 * RESTful request (conversion to JSON, routing, etc.)
 *
 * \param ser TCP session.
 * \param urih URI handler.
 * \param uri URI requested.
 * \param method HTTP method.
 * \param get_params HTTP \c GET params.
 * \param headers HTTP headers.
 */
static int ast_ari_callback(struct ast_tcptls_session_instance *ser,
				const struct ast_http_uri *urih,
				const char *uri,
				enum ast_http_method method,
				struct ast_variable *get_params,
				struct ast_variable *headers)
{
	RAII_VAR(struct ast_ari_conf *, conf, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, response_body, ast_str_create(256), ast_free);
	RAII_VAR(struct ast_ari_conf_user *, user, NULL, ao2_cleanup);
	struct ast_ari_response response = { .fd = -1, 0 };
	RAII_VAR(struct ast_variable *, post_vars, NULL, ast_variables_destroy);
	struct ast_variable *var;
	const char *app_name = NULL;
	RAII_VAR(struct ast_json *, body, ast_json_null(), ast_json_unref);
	int debug_app = 0;

	if (!response_body) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Out of memory");
		return 0;
	}

	response.headers = ast_str_create(40);
	if (!response.headers) {
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "Out of memory");
		return 0;
	}

	conf = ast_ari_config_get();
	if (!conf || !conf->general) {
		ast_free(response.headers);
		ast_http_request_close_on_completion(ser);
		ast_http_error(ser, 500, "Server Error", "URI handler config missing");
		return 0;
	}

	process_cors_request(headers, &response);

	/* Process form data from a POST. It could be mixed with query
	 * parameters, which seems a bit odd. But it's allowed, so that's okay
	 * with us.
	 */
	post_vars = ast_http_get_post_vars(ser, headers);
	if (!post_vars) {
		switch (errno) {
		case EFBIG:
			ast_ari_response_error(&response, 413,
				"Request Entity Too Large",
				"Request body too large");
			goto request_failed;
		case ENOMEM:
			ast_http_request_close_on_completion(ser);
			ast_ari_response_error(&response, 500,
				"Internal Server Error",
				"Out of memory");
			goto request_failed;
		case EIO:
			ast_ari_response_error(&response, 400,
				"Bad Request", "Error parsing request body");
			goto request_failed;
		}

		/* Look for a JSON request entity only if there were no post_vars.
		 * If there were post_vars, then the request body would already have
		 * been consumed and can not be read again.
		 */
		body = ast_http_get_json(ser, headers);
		if (!body) {
			switch (errno) {
			case EFBIG:
				ast_ari_response_error(&response, 413, "Request Entity Too Large", "Request body too large");
				goto request_failed;
			case ENOMEM:
				ast_ari_response_error(&response, 500, "Internal Server Error", "Error processing request");
				goto request_failed;
			case EIO:
				ast_ari_response_error(&response, 400, "Bad Request", "Error parsing request body");
				goto request_failed;
			}
		}
	}
	if (get_params == NULL) {
		get_params = post_vars;
	} else if (get_params && post_vars) {
		/* Has both post_vars and get_params */
		struct ast_variable *last_var = post_vars;
		while (last_var->next) {
			last_var = last_var->next;
		}
		/* The duped get_params will get freed when post_vars gets
		 * ast_variables_destroyed.
		 */
		last_var->next = ast_variables_dup(get_params);
		get_params = post_vars;
	}

	/* At this point, get_params will contain post_vars (if any) */
	app_name = ast_variable_find_in_list(get_params, "app");
	if (!app_name) {
		struct ast_json *app = ast_json_object_get(body, "app");

		app_name = (app ? ast_json_string_get(app) : NULL);
	}

	/* stasis_app_get_debug_by_name returns an "||" of the app's debug flag
	 * and the global debug flag.
	 */
	debug_app = stasis_app_get_debug_by_name(app_name);
	if (debug_app) {
		struct ast_str *buf = ast_str_create(512);
		char *str = ast_json_dump_string_format(body, ast_ari_json_format());

		if (!buf || (body && !str)) {
			ast_http_request_close_on_completion(ser);
			ast_ari_response_error(&response, 500, "Server Error", "Out of memory");
			ast_json_free(str);
			ast_free(buf);
			goto request_failed;
		}

		ast_str_append(&buf, 0, "<--- ARI request received from: %s --->\n",
			ast_sockaddr_stringify(&ser->remote_address));
		for (var = headers; var; var = var->next) {
			ast_str_append(&buf, 0, "%s: %s\n", var->name, var->value);
		}
		for (var = get_params; var; var = var->next) {
			ast_str_append(&buf, 0, "%s: %s\n", var->name, var->value);
		}
		ast_verbose("%sbody:\n%s\n\n", ast_str_buffer(buf), S_OR(str, ""));
		ast_json_free(str);
		ast_free(buf);
	}

	user = authenticate_user(get_params, headers);
	if (response.response_code > 0) {
		/* POST parameter processing error. Do nothing. */
	} else if (!user) {
		/* Per RFC 2617, section 1.2: The 401 (Unauthorized) response
		 * message is used by an origin server to challenge the
		 * authorization of a user agent. This response MUST include a
		 * WWW-Authenticate header field containing at least one
		 * challenge applicable to the requested resource.
		 */
		ast_ari_response_error(&response, 401, "Unauthorized", "Authentication required");

		/* Section 1.2:
		 *   realm       = "realm" "=" realm-value
		 *   realm-value = quoted-string
		 * Section 2:
		 *   challenge   = "Basic" realm
		 */
		ast_str_append(&response.headers, 0,
			"WWW-Authenticate: Basic realm=\"%s\"\r\n",
			conf->general->auth_realm);
	} else if (!ast_fully_booted) {
		ast_http_request_close_on_completion(ser);
		ast_ari_response_error(&response, 503, "Service Unavailable", "Asterisk not booted");
	} else if (user->read_only && method != AST_HTTP_GET && method != AST_HTTP_OPTIONS) {
		ast_ari_response_error(&response, 403, "Forbidden", "Write access denied");
	} else if (ast_ends_with(uri, "/")) {
		remove_trailing_slash(uri, &response);
	} else if (ast_begins_with(uri, "api-docs/")) {
		/* Serving up API docs */
		if (method != AST_HTTP_GET) {
			ast_ari_response_error(&response, 405, "Method Not Allowed", "Unsupported method");
		} else {
			/* Skip the api-docs prefix */
			ast_ari_get_docs(strchr(uri, '/') + 1, urih->prefix, headers, &response);
		}
	} else {
		/* Other RESTful resources */
		ast_ari_invoke(ser, uri, method, get_params, headers, body,
			&response);
	}

	if (response.no_response) {
		/* The handler indicates no further response is necessary.
		 * Probably because it already handled it */
		ast_free(response.headers);
		return 0;
	}

request_failed:

	/* If you explicitly want to have no content, set message to
	 * ast_json_null().
	 */
	ast_assert(response.message != NULL);
	ast_assert(response.response_code > 0);

	/* response.message could be NULL, in which case the empty response_body
	 * is correct
	 */
	if (response.message && !ast_json_is_null(response.message)) {
		ast_str_append(&response.headers, 0,
			       "Content-type: application/json\r\n");
		if (ast_json_dump_str_format(response.message, &response_body,
				conf->general->format) != 0) {
			/* Error encoding response */
			response.response_code = 500;
			response.response_text = "Internal Server Error";
			ast_str_set(&response_body, 0, "%s", "");
			ast_str_set(&response.headers, 0, "%s", "");
		}
	}

	if (debug_app) {
		ast_verbose("<--- Sending ARI response to %s --->\n%d %s\n%s%s\n\n",
			ast_sockaddr_stringify(&ser->remote_address), response.response_code,
			response.response_text, ast_str_buffer(response.headers),
			ast_str_buffer(response_body));
	}

	ast_http_send(ser, method, response.response_code,
		      response.response_text, response.headers, response_body,
		      response.fd != -1 ? response.fd : 0, 0);
	/* ast_http_send takes ownership, so we don't have to free them */
	response_body = NULL;

	ast_json_unref(response.message);
	if (response.fd >= 0) {
		close(response.fd);
	}
	return 0;
}

static struct ast_http_uri http_uri = {
	.callback = ast_ari_callback,
	.description = "Asterisk RESTful API",
	.uri = "ari",
	.has_subtree = 1,
	.data = NULL,
	.key = __FILE__,
	.no_decode_uri = 1,
};

static int unload_module(void)
{
	ast_ari_cli_unregister();

	if (is_enabled()) {
		ast_debug(3, "Disabling ARI\n");
		ast_http_uri_unlink(&http_uri);
	}

	ast_ari_config_destroy();

	ao2_cleanup(root_handler);
	root_handler = NULL;
	ast_mutex_destroy(&root_handler_lock);

	ast_json_unref(oom_json);
	oom_json = NULL;

	return 0;
}

static int load_module(void)
{
	ast_mutex_init(&root_handler_lock);

	/* root_handler may have been built during a declined load */
	if (!root_handler) {
		root_handler = root_handler_create();
	}
	if (!root_handler) {
		return AST_MODULE_LOAD_DECLINE;
	}

	/* oom_json may have been built during a declined load */
	if (!oom_json) {
		oom_json = ast_json_pack(
			"{s: s}", "error", "Allocation failed");
	}
	if (!oom_json) {
		/* Ironic */
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_ari_config_init() != 0) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	if (is_enabled()) {
		ast_debug(3, "ARI enabled\n");
		ast_http_uri_link(&http_uri);
	} else {
		ast_debug(3, "ARI disabled\n");
	}

	if (ast_ari_cli_register() != 0) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	char was_enabled = is_enabled();

	if (ast_ari_config_reload() != 0) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (was_enabled && !is_enabled()) {
		ast_debug(3, "Disabling ARI\n");
		ast_http_uri_unlink(&http_uri);
	} else if (!was_enabled && is_enabled()) {
		ast_debug(3, "Enabling ARI\n");
		ast_http_uri_link(&http_uri);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Asterisk RESTful Interface",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.optional_modules = "res_http_websocket",
	.requires = "http,res_stasis",
	.load_pri = AST_MODPRI_APP_DEPEND,
);
