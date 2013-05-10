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
 *  - res/stasis_http/resource_{resource}.h
 *    - For each operation in the resouce, a generated argument structure
 *      (holding the parsed arguments from the request) and function
 *      declarations (to implement in res/stasis_http/resource_{resource}.c)
 *  - res_stasis_http_{resource}.c
 *    - A set of \ref stasis_rest_callback functions, which glue the two
 *      together. They parse out path variables and request parameters to
 *      populate a specific \c *_args which is passed to the specific request
 *      handler (in res/stasis_http/resource_{resource}.c)
 *    - A tree of \ref stasis_rest_handlers for routing requests to its
 *      \ref stasis_rest_callback
 *
 * The basic flow of an HTTP request is:
 *
 *  - stasis_http_callback()
 *    1. Initial request validation
 *    2. Routes as either a doc request (stasis_http_get_docs) or API
 *       request (stasis_http_invoke)
 *       - stasis_http_invoke()
 *         1. Further request validation
 *         2. Routes the request through the tree of generated
 *            \ref stasis_rest_handlers.
 *         3. Dispatch to the generated callback
 *            - \c stasis_http_*_cb
 *              1. Populate \c *_args struct with path and get params
 *              2. Invoke the request handler
 *    3. Validates and sends response
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_stasis_http" language="en_US">
		<synopsis>HTTP binding for the Stasis API</synopsis>
		<configFile name="stasis_http.conf">
			<configObject name="global">
				<synopsis>Global configuration settings</synopsis>
				<configOption name="enabled">
					<synopsis>Enable/disable the stasis-http module</synopsis>
				</configOption>
				<configOption name="pretty">
					<synopsis>Responses from stasis-http are formatted to be human readable</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/paths.h"
#include "asterisk/stasis_http.h"
#include "asterisk/config_options.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*! \brief Global configuration options for stasis http. */
struct conf_global_options {
	/*! Enabled by default, disabled if false. */
	int enabled:1;
	/*! Encoding format used during output (default compact). */
	enum ast_json_encoding_format format;
};

/*! \brief All configuration options for stasis http. */
struct conf {
	/*! The general section configuration options. */
	struct conf_global_options *global;
};

/*! \brief Locking container for safe configuration access. */
static AO2_GLOBAL_OBJ_STATIC(confs);

/*! \brief Mapping of the stasis http conf struct's globals to the
 *         general context in the config file. */
static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "global",
	.item_offset = offsetof(struct conf, global),
	.category = "^general$",
	.category_match = ACO_WHITELIST
};

static struct aco_type *global_options[] = ACO_TYPES(&global_option);

/*! \brief Disposes of the stasis http conf object */
static void conf_destructor(void *obj)
{
    struct conf *cfg = obj;
    ao2_cleanup(cfg->global);
}

/*! \brief Creates the statis http conf object. */
static void *conf_alloc(void)
{
    struct conf *cfg;

    if (!(cfg = ao2_alloc(sizeof(*cfg), conf_destructor))) {
        return NULL;
    }

    if (!(cfg->global = ao2_alloc(sizeof(*cfg->global), NULL))) {
        ao2_ref(cfg, -1);
        return NULL;
    }
    return cfg;
}

/*! \brief The conf file that's processed for the module. */
static struct aco_file conf_file = {
	/*! The config file name. */
	.filename = "stasis_http.conf",
	/*! The mapping object types to be processed. */
	.types = ACO_TYPES(&global_option),
};

CONFIG_INFO_STANDARD(cfg_info, confs, conf_alloc,
		     .files = ACO_FILES(&conf_file));

/*! \brief Bitfield handler since it is not possible to take address. */
static int conf_bitfield_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct conf_global_options *global = obj;

	if (!strcasecmp(var->name, "enabled")) {
		global->enabled = ast_true(var->value);
	} else {
		return -1;
	}

	return 0;
}

/*! \brief Encoding format handler converts from boolean to enum. */
static int encoding_format_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct conf_global_options *global = obj;

	if (!strcasecmp(var->name, "pretty")) {
		global->format = ast_true(var->value) ? AST_JSON_PRETTY : AST_JSON_COMPACT;
	} else {
		return -1;
	}

	return 0;
}

/*! \brief Helper function to check if module is enabled. */
static char is_enabled(void)
{
	RAII_VAR(struct conf *, cfg, ao2_global_obj_ref(confs), ao2_cleanup);

	return cfg->global->enabled;
}

/*! Lock for \ref root_handler */
static ast_mutex_t root_handler_lock;

/*! Handler for root RESTful resource. */
static struct stasis_rest_handlers *root_handler;

/*! Pre-defined message for allocation failures. */
static struct ast_json *alloc_failed_message;

int stasis_http_add_handler(struct stasis_rest_handlers *handler)
{
	RAII_VAR(struct stasis_rest_handlers *, new_handler, NULL, ao2_cleanup);
	size_t old_size, new_size;

	SCOPED_MUTEX(lock, &root_handler_lock);

	old_size = sizeof(*new_handler) +
		root_handler->num_children * sizeof(handler);
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
	ast_module_ref(ast_module_info->self);
	return 0;
}

int stasis_http_remove_handler(struct stasis_rest_handlers *handler)
{
	RAII_VAR(struct stasis_rest_handlers *, new_handler, NULL, ao2_cleanup);
	size_t size, i, j;

	ast_assert(root_handler != NULL);

	ast_mutex_lock(&root_handler_lock);
	size = sizeof(*new_handler) +
		root_handler->num_children * sizeof(handler);

	new_handler = ao2_alloc(size, NULL);
	if (!new_handler) {
		return -1;
	}
	memcpy(new_handler, root_handler, sizeof(*new_handler));

	for (i = 0, j = 0; i < root_handler->num_children; ++i) {
		if (root_handler->children[i] == handler) {
			ast_module_unref(ast_module_info->self);
			continue;
		}
		new_handler->children[j++] = root_handler->children[i];
	}
	new_handler->num_children = j;

	ao2_cleanup(root_handler);
	ao2_ref(new_handler, +1);
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
	handler->path_segment = "stasis";

	ao2_ref(handler, +1);
	return handler;
}

void stasis_http_response_error(struct stasis_http_response *response,
				int response_code,
				const char *response_text,
				const char *message_fmt, ...)
{
	RAII_VAR(struct ast_json *, message, NULL, ast_json_unref);
	va_list ap;

	va_start(ap, message_fmt);
	message = ast_json_vstringf(message_fmt, ap);
	response->message = ast_json_pack("{s: o}",
					  "message", ast_json_ref(message));
	response->response_code = response_code;
	response->response_text = response_text;
}

void stasis_http_response_ok(struct stasis_http_response *response,
			     struct ast_json *message)
{
	response->message = message;
	response->response_code = 200;
	response->response_text = "OK";
}

void stasis_http_response_no_content(struct stasis_http_response *response)
{
	response->message = NULL;
	response->response_code = 204;
	response->response_text = "No Content";
}

void stasis_http_response_alloc_failed(struct stasis_http_response *response)
{
	response->message = ast_json_ref(alloc_failed_message);
	response->response_code = 500;
	response->response_text = "Internal Server Error";
}

static void add_allow_header(struct stasis_rest_handlers *handler,
			     struct stasis_http_response *response)
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
			   struct stasis_http_response *response)
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
	response->response_code = 204;
	response->response_text = "No Content";
	response->message = NULL;

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
	 * set of steps.
	 */
	if (origin == NULL) {
		return;
	}

	/* CORS 6.2, #2 - "If the value of the Origin header is not a
	 * case-sensitive match for any of the values in list of origins do not
	 * set any additional headers and terminate this set of steps.
	 *
	 * "Always matching is acceptable since the list of origins can be
	 * unbounded.
	 *
	 * "The Origin header can only contain a single origin as the user agent
	 * will not follow redirects.
	 *
	 * TODO - pull list of allowed origins from config
	 */

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
		stasis_http_response_alloc_failed(response);
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
	 * "Note: Always matching is acceptable since the list of headers can be
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
	ast_str_append(&response->headers, 0, "%s: OPTIONS,%s\r\n",
		       ACA_METHODS, ast_str_buffer(allow));


	/* CORS 6.2, #10 - "Add one or more Access-Control-Allow-Headers headers
	 * consisting of (a subset of) the list of headers.
	 *
	 * "Since the list of headers can be unbounded simply returning headers
	 * can be enough."
	 */
	if (!ast_strlen_zero(acr_headers)) {
		ast_str_append(&response->headers, 0, "%s: %s\r\n",
			       ACA_HEADERS, acr_headers);
	}
}

void stasis_http_invoke(const char *uri,
			enum ast_http_method method,
			struct ast_variable *get_params,
			struct ast_variable *headers,
			struct stasis_http_response *response)
{
	RAII_VAR(char *, response_text, NULL, ast_free);
	RAII_VAR(struct stasis_rest_handlers *, root, NULL, ao2_cleanup);
	struct stasis_rest_handlers *handler;
	struct ast_variable *path_vars = NULL;
	char *path = ast_strdupa(uri);
	const char *path_segment;
	stasis_rest_callback callback;

	root = handler = get_root_handler();
	ast_assert(root != NULL);

	while ((path_segment = strsep(&path, "/")) && (strlen(path_segment) > 0)) {
		struct stasis_rest_handlers *found_handler = NULL;
		int i;
		ast_debug(3, "Finding handler for %s\n", path_segment);
		for (i = 0; found_handler == NULL && i < handler->num_children; ++i) {
			struct stasis_rest_handlers *child = handler->children[i];

			ast_debug(3, "  Checking %s\n", child->path_segment);
			if (child->is_wildcard) {
				/* Record the path variable */
				struct ast_variable *path_var = ast_variable_new(child->path_segment, path_segment, __FILE__);
				path_var->next = path_vars;
				path_vars = path_var;
				found_handler = child;
			} else if (strcmp(child->path_segment, path_segment) == 0) {
				found_handler = child;
			}
		}

		if (found_handler == NULL) {
			/* resource not found */
			ast_debug(3, "  Handler not found\n");
			stasis_http_response_error(
				response, 404, "Not Found",
				"Resource not found");
			return;
		} else {
			ast_debug(3, "  Got it!\n");
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
		stasis_http_response_error(
			response, 405, "Method Not Allowed",
			"Invalid method");
		return;
	}

	callback = handler->callbacks[method];
	if (callback == NULL) {
		add_allow_header(handler, response);
		stasis_http_response_error(
			response, 405, "Method Not Allowed",
			"Invalid method");
		return;
	}

	callback(get_params, path_vars, headers, response);
	if (response->message == NULL && response->response_code == 0) {
		/* Really should not happen */
		ast_assert(0);
		stasis_http_response_error(
			response, 418, "I'm a teapot",
			"Method not implemented");
	}
}

void stasis_http_get_docs(const char *uri, struct ast_variable *headers,
			  struct stasis_http_response *response)
{
	RAII_VAR(struct ast_str *, absolute_path_builder, NULL, ast_free);
	RAII_VAR(char *, absolute_api_dirname, NULL, free);
	RAII_VAR(char *, absolute_filename, NULL, free);
	struct ast_json *obj = NULL;
	struct ast_variable *host = NULL;
	struct ast_json_error error = {};
	struct stat file_stat;

	ast_debug(3, "%s(%s)\n", __func__, uri);

	absolute_path_builder = ast_str_create(80);
	if (absolute_path_builder == NULL) {
		stasis_http_response_alloc_failed(response);
		return;
	}

	/* absolute path to the rest-api directory */
	ast_str_append(&absolute_path_builder, 0, "%s", ast_config_AST_DATA_DIR);
	ast_str_append(&absolute_path_builder, 0, "/rest-api/");
	absolute_api_dirname = realpath(ast_str_buffer(absolute_path_builder), NULL);
	if (absolute_api_dirname == NULL) {
		ast_log(LOG_ERROR, "Error determining real directory for rest-api\n");
		stasis_http_response_error(
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
			stasis_http_response_error(
				response, 404, "Not Found",
				"Resource not found");
			break;
		case EACCES:
			stasis_http_response_error(
				response, 403, "Forbidden",
				"Permission denied");
			break;
		default:
			ast_log(LOG_ERROR,
				"Error determining real path for uri '%s': %s\n",
				uri, strerror(errno));
			stasis_http_response_error(
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
		stasis_http_response_error(
			response, 404, "Not Found",
			"Resource not found");
		return;
	}

	if (stat(absolute_filename, &file_stat) == 0) {
		if (!(file_stat.st_mode & S_IFREG)) {
			/* Not a file */
			stasis_http_response_error(
				response, 403, "Forbidden",
				"Invalid access");
			return;
		}
	} else {
		/* Does not exist */
		stasis_http_response_error(
			response, 404, "Not Found",
			"Resource not found");
		return;
	}

	/* Load resource object from file */
	obj = ast_json_load_new_file(absolute_filename, &error);
	if (obj == NULL) {
		ast_log(LOG_ERROR, "Error parsing resource file: %s:%d(%d) %s\n",
			error.source, error.line, error.column, error.text);
		stasis_http_response_error(
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
			ast_json_object_set(
				obj, "basePath",
				ast_json_stringf("http://%s/stasis", host->value));
		} else {
			/* Without the host, we don't have the basePath */
			ast_json_object_del(obj, "basePath");
		}
	}

	stasis_http_response_ok(response, obj);
}

static void remove_trailing_slash(const char *uri,
				  struct stasis_http_response *response)
{
	char *slashless = ast_strdupa(uri);
	slashless[strlen(slashless) - 1] = '\0';

	ast_str_append(&response->headers, 0,
		       "Location: /stasis/%s\r\n", slashless);
	stasis_http_response_error(response, 302, "Found",
				   "Redirecting to %s", slashless);
}

/*!
 * \brief Handle CORS headers for simple requests.
 *
 * See http://www.w3.org/TR/cors/ for the spec. Especially section 6.1.
 */
static void process_cors_request(struct ast_variable *headers,
				 struct stasis_http_response *response)
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
	 * "Note: Always matching is acceptable since the list of origins can be
	 * unbounded."
	 *
	 * TODO - pull list of allowed origins from config
	 */

	/* CORS 6.1, #3 - "If the resource supports credentials add a single
	 * Access-Control-Allow-Origin header, with the value of the Origin
	 * header as value, and add a single Access-Control-Allow-Credentials
	 * header with the case-sensitive string "true" as value.
	 *
	 * "Otherwise, add a single Access-Control-Allow-Origin header, with
	 * either the value of the Origin header or the string "*" as value."
	 *
	 * TODO - when we add authentication, this will change to
	 * Access-Control-Allow-Credentials.
	 */
	ast_str_append(&response->headers, 0,
		       "Access-Control-Allow-Origin: %s\r\n", origin);

	/* CORS 6.1, #4 - "If the list of exposed headers is not empty add one
	 * or more Access-Control-Expose-Headers headers, with as values the
	 * header field names given in the list of exposed headers."
	 *
	 * No exposed headers; skipping
	 */
}


/*!
 * \internal
 * \brief Stasis HTTP handler.
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
static int stasis_http_callback(struct ast_tcptls_session_instance *ser,
				const struct ast_http_uri *urih,
				const char *uri,
				enum ast_http_method method,
				struct ast_variable *get_params,
				struct ast_variable *headers)
{
	RAII_VAR(struct conf *, cfg, ao2_global_obj_ref(confs), ao2_cleanup);
	RAII_VAR(struct ast_str *, response_headers, ast_str_create(40), ast_free);
	RAII_VAR(struct ast_str *, response_body, ast_str_create(256), ast_free);
	struct stasis_http_response response = {};
	int ret = 0;

	if (!response_headers || !response_body) {
		return -1;
	}

	response.headers = ast_str_create(40);

	process_cors_request(headers, &response);

	if (ast_ends_with(uri, "/")) {
		remove_trailing_slash(uri, &response);
	} else if (ast_begins_with(uri, "api-docs/")) {
		/* Serving up API docs */
		if (method != AST_HTTP_GET) {
			response.message =
				ast_json_pack("{s: s}",
					      "message", "Unsupported method");
			response.response_code = 405;
			response.response_text = "Method Not Allowed";
		} else {
			/* Skip the api-docs prefix */
			stasis_http_get_docs(strchr(uri, '/') + 1, headers, &response);
		}
	} else {
		/* Other RESTful resources */
		stasis_http_invoke(uri, method, get_params, headers, &response);
	}

	/* Leaving message unset is only allowed for 204 (No Content).
	 * If you explicitly want to have no content for a different return
	 * code, set message to ast_json_null().
	 */
	ast_assert(response.response_code == 204 || response.message != NULL);
	ast_assert(response.response_code > 0);

	ast_str_append(&response_headers, 0, "%s", ast_str_buffer(response.headers));

	/* response.message could be NULL, in which case the empty response_body
	 * is correct
	 */
	if (response.message && !ast_json_is_null(response.message)) {
		ast_str_append(&response_headers, 0,
			       "Content-type: application/json\r\n");
		if (ast_json_dump_str_format(response.message, &response_body, cfg->global->format) != 0) {
			/* Error encoding response */
			response.response_code = 500;
			response.response_text = "Internal Server Error";
			ast_str_set(&response_body, 0, "%s", "");
			ast_str_set(&response_headers, 0, "%s", "");
			ret = -1;
		}
	}

	ast_http_send(ser, method, response.response_code,
		      response.response_text, response_headers, response_body,
		      0, 0);
	/* ast_http_send takes ownership, so we don't have to free them */
	response_headers = NULL;
	response_body = NULL;

	ast_json_unref(response.message);
	return ret;
}

static struct ast_http_uri http_uri = {
	.callback = stasis_http_callback,
	.description = "Asterisk RESTful API",
	.uri = "stasis",

	.has_subtree = 1,
	.data = NULL,
	.key = __FILE__,
};

static int load_module(void)
{
	ast_mutex_init(&root_handler_lock);

	root_handler = root_handler_create();
	if (!root_handler) {
		return AST_MODULE_LOAD_FAILURE;
	}

	if (aco_info_init(&cfg_info)) {
		aco_info_destroy(&cfg_info);
		return AST_MODULE_LOAD_DECLINE;
	}

	aco_option_register_custom(&cfg_info, "enabled", ACO_EXACT, global_options,
				   "yes", conf_bitfield_handler, 0);
	aco_option_register_custom(&cfg_info, "pretty", ACO_EXACT, global_options,
				   "no",  encoding_format_handler, 0);

	if (aco_process_config(&cfg_info, 0)) {
		aco_info_destroy(&cfg_info);
		return AST_MODULE_LOAD_DECLINE;
	}

	alloc_failed_message = ast_json_pack(
		"{s: s}", "message", "Allocation failed");

	if (is_enabled()) {
		ast_http_uri_link(&http_uri);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_json_unref(alloc_failed_message);
	alloc_failed_message = NULL;

	if (is_enabled()) {
		ast_http_uri_unlink(&http_uri);
	}

	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(confs);

	ao2_cleanup(root_handler);
	root_handler = NULL;
	ast_mutex_destroy(&root_handler_lock);

	return 0;
}

static int reload_module(void)
{
	char was_enabled = is_enabled();

	if (aco_process_config(&cfg_info, 1)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (was_enabled && !is_enabled()) {
		ast_http_uri_unlink(&http_uri);
	} else if (!was_enabled && is_enabled()) {
		ast_http_uri_link(&http_uri);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY,
	AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER,
	"Stasis HTTP bindings",
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	);
