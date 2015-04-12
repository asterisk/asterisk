/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

/*!
 * \file \brief Test ARI API.
 * \author\verbatim David M. Lee, II <dlee@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_ari</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/ari.h"

/*!@{*/

/*!
 * \internal
 * The following code defines a simple RESTful API for unit testing. The
 * response encodes the inputs of the invocation. The invocation_count
 * counter is also incremented.
 *
 *  - /foo (GET)
 *  - /foo/bar (GET, POST)
 *  - /foo/{bam} (GET)
 *  - /foo/{bam}/bang (GET, POST, DE1LETE)
 */

static int invocation_count;

/*!
 * \internal
 * Shared code for all handlers
 */
static void handler(const char *name,
		    int response_code,
		    struct ast_variable *get_params,
		    struct ast_variable *path_vars,
		    struct ast_variable *headers,
		    struct ast_ari_response *response)
{
	struct ast_json *message = ast_json_pack("{s: s, s: {}, s: {}, s: {}}",
						 "name", name,
						 "get_params",
						 "path_vars",
						 "headers");
	struct ast_json *get_params_obj = ast_json_object_get(message, "get_params");
	struct ast_json *path_vars_obj = ast_json_object_get(message, "path_vars");
	struct ast_json *headers_obj = ast_json_object_get(message, "headers");

	for (; get_params != NULL; get_params = get_params->next) {
		ast_json_object_set(get_params_obj, get_params->name, ast_json_string_create(get_params->value));
	}

	for (; path_vars != NULL; path_vars = path_vars->next) {
		ast_json_object_set(path_vars_obj, path_vars->name, ast_json_string_create(path_vars->value));
	}

	for (; headers != NULL; headers = headers->next) {
		ast_json_object_set(headers_obj, headers->name, ast_json_string_create(headers->value));
	}

	++invocation_count;
	response->response_code = response_code;
	response->message = message;
}

/*!
 * \internal
 * Macro to reduce the handler definition boiler-plate.
 */
#define HANDLER(name, response_code)					\
	static void name(struct ast_tcptls_session_instance *ser,	\
		struct ast_variable *get_params,			\
		struct ast_variable *path_vars,				\
		struct ast_variable *headers,				\
		struct ast_ari_response *response)			\
	{								\
		handler(#name, response_code, get_params, path_vars, headers, response); \
	}

HANDLER(bang_get, 200)
HANDLER(bang_post, 200)
HANDLER(bang_delete, 204)
HANDLER(bar_get, 200)
HANDLER(bar_post, 200)
HANDLER(bam_get, 200)
HANDLER(foo_get, 200)

static struct stasis_rest_handlers bang = {
	.path_segment = "bang",
	.callbacks = {
		[AST_HTTP_GET] = bang_get,
		[AST_HTTP_POST] = bang_post,
		[AST_HTTP_DELETE] = bang_delete,
	},
	.num_children = 0
};
static struct stasis_rest_handlers bar = {
	.path_segment = "bar",
	.callbacks = {
		[AST_HTTP_GET] = bar_get,
		[AST_HTTP_POST] = bar_post,
	},
	.num_children = 0
};
static struct stasis_rest_handlers bam = {
	.path_segment = "bam",
	.is_wildcard = 1,
	.callbacks = {
		[AST_HTTP_GET] = bam_get,
	},
	.num_children = 1,
	.children = { &bang }
};
static struct stasis_rest_handlers test_root = {
	.path_segment = "foo",
	.callbacks = {
		[AST_HTTP_GET] = foo_get,
	},
	.num_children = 3,
	.children = { &bar, &bam, &bang }
};
/*!@}*/

/*!
 * \internal
 * \c ast_ari_response constructor.
 */
static struct ast_ari_response *response_alloc(void)
{
	struct ast_ari_response *resp = ast_calloc(1, sizeof(struct ast_ari_response));
	resp->headers = ast_str_create(24);
	return resp;
}

/*!
 * \internal
 * \c ast_ari_response destructor.
 */
static void response_free(struct ast_ari_response *resp)
{
	if (!resp) {
		return;
	}
	ast_free(resp->headers);
	ast_json_unref(resp->message);
	ast_free(resp);
}

/*!
 * \ internal
 * Setup test fixture for invocation tests.
 */
static void *setup_invocation_test(void) {
	int r;
	invocation_count = 0;
	r = ast_ari_add_handler(&test_root);
	ast_assert(r == 0);
	return &invocation_count;
}

/*!
 * \ internal
 * Tear down test fixture for invocation tests.
 */
static void tear_down_invocation_test(void *ignore) {
	if (!ignore) {
		return;
	}
	ast_ari_remove_handler(&test_root);
}


AST_TEST_DEFINE(get_docs)
{
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	RAII_VAR(struct ast_variable *, headers, NULL, ast_variables_destroy);
	struct ast_json *basePathJson;
	const char *basePath;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test simple API get.";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	response = response_alloc();
	headers = ast_variable_new("Host", "stasis.asterisk.org", __FILE__);
	ast_ari_get_docs("resources.json", headers, response);
	ast_test_validate(test, 200 == response->response_code);

	/* basePath should be relative to the Host header */
	basePathJson = ast_json_object_get(response->message, "basePath");
	ast_test_validate(test, NULL != basePathJson);
	basePath = ast_json_string_get(basePathJson);
	ast_test_validate(test, 0 == strcmp("http://stasis.asterisk.org/ari", basePath));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(get_docs_nohost)
{
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	struct ast_variable *headers = NULL;
	struct ast_json *basePathJson;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test API get without a Host header";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	response = response_alloc();
	ast_ari_get_docs("resources.json", headers, response);
	ast_test_validate(test, 200 == response->response_code);

	/* basePath should be relative to the Host header */
	basePathJson = ast_json_object_get(response->message, "basePath");
	ast_test_validate(test, NULL == basePathJson);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(get_docs_notfound)
{
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	struct ast_variable *headers = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test API get for invalid resource";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	response = response_alloc();
	ast_ari_get_docs("i-am-not-a-resource.json", headers, response);
	ast_test_validate(test, 404 == response->response_code);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(get_docs_hackerz)
{
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	struct ast_variable *headers = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test API get for a file outside the rest-api path";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	response = response_alloc();
	ast_ari_get_docs("../../../../sbin/asterisk", headers, response);
	ast_test_validate(test, 404 == response->response_code);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(invoke_get)
{
	RAII_VAR(void *, fixture, NULL, tear_down_invocation_test);
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	struct ast_variable *get_params = NULL;
	struct ast_variable *headers = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test simple GET of an HTTP resource.";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	fixture = setup_invocation_test();
	response = response_alloc();
	get_params = ast_variable_new("get1", "get-one", __FILE__);
	ast_assert(get_params != NULL);
	get_params->next = ast_variable_new("get2", "get-two", __FILE__);
	ast_assert(get_params->next != NULL);

	headers = ast_variable_new("head1", "head-one", __FILE__);
	ast_assert(headers != NULL);
	headers->next = ast_variable_new("head2", "head-two", __FILE__);
	ast_assert(headers->next != NULL);

	expected = ast_json_pack("{s: s, s: {s: s, s: s}, s: {s: s, s: s}, s: {}}",
				 "name", "foo_get",
				 "get_params",
				 "get1", "get-one",
				 "get2", "get-two",
				 "headers",
				 "head1", "head-one",
				 "head2", "head-two",
				 "path_vars");

	ast_ari_invoke(NULL, "foo", AST_HTTP_GET, get_params, headers, response);

	ast_test_validate(test, 1 == invocation_count);
	ast_test_validate(test, 200 == response->response_code);
	ast_test_validate(test, ast_json_equal(expected, response->message));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(invoke_wildcard)
{
	RAII_VAR(void *, fixture, NULL, tear_down_invocation_test);
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	struct ast_variable *get_params = NULL;
	struct ast_variable *headers = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test GET of a wildcard resource.";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	fixture = setup_invocation_test();
	response = response_alloc();
	expected = ast_json_pack("{s: s, s: {}, s: {}, s: {s: s}}",
				 "name", "bam_get",
				 "get_params",
				 "headers",
				 "path_vars",
				 "bam", "foshizzle");

	ast_ari_invoke(NULL, "foo/foshizzle", AST_HTTP_GET, get_params, headers, response);

	ast_test_validate(test, 1 == invocation_count);
	ast_test_validate(test, 200 == response->response_code);
	ast_test_validate(test, ast_json_equal(expected, response->message));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(invoke_delete)
{
	RAII_VAR(void *, fixture, NULL, tear_down_invocation_test);
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	struct ast_variable *get_params = NULL;
	struct ast_variable *headers = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test DELETE of an HTTP resource.";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	fixture = setup_invocation_test();
	response = response_alloc();
	expected = ast_json_pack("{s: s, s: {}, s: {}, s: {s: s}}",
				 "name", "bang_delete",
				 "get_params",
				 "headers",
				 "path_vars",
				 "bam", "foshizzle");

	ast_ari_invoke(NULL, "foo/foshizzle/bang", AST_HTTP_DELETE, get_params, headers, response);

	ast_test_validate(test, 1 == invocation_count);
	ast_test_validate(test, 204 == response->response_code);
	ast_test_validate(test, ast_json_equal(expected, response->message));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(invoke_post)
{
	RAII_VAR(void *, fixture, NULL, tear_down_invocation_test);
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	RAII_VAR(struct ast_json *, expected, NULL, ast_json_unref);
	struct ast_variable *get_params = NULL;
	struct ast_variable *headers = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test POST of an HTTP resource.";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	fixture = setup_invocation_test();
	response = response_alloc();
	get_params = ast_variable_new("get1", "get-one", __FILE__);
	ast_assert(get_params != NULL);
	get_params->next = ast_variable_new("get2", "get-two", __FILE__);
	ast_assert(get_params->next != NULL);

	headers = ast_variable_new("head1", "head-one", __FILE__);
	ast_assert(headers != NULL);
	headers->next = ast_variable_new("head2", "head-two", __FILE__);
	ast_assert(headers->next != NULL);

	expected = ast_json_pack("{s: s, s: {s: s, s: s}, s: {s: s, s: s}, s: {}}",
				 "name", "bar_post",
				 "get_params",
				 "get1", "get-one",
				 "get2", "get-two",
				 "headers",
				 "head1", "head-one",
				 "head2", "head-two",
				 "path_vars");

	ast_ari_invoke(NULL, "foo/bar", AST_HTTP_POST, get_params, headers, response);

	ast_test_validate(test, 1 == invocation_count);
	ast_test_validate(test, 200 == response->response_code);
	ast_test_validate(test, ast_json_equal(expected, response->message));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(invoke_bad_post)
{
	RAII_VAR(void *, fixture, NULL, tear_down_invocation_test);
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	struct ast_variable *get_params = NULL;
	struct ast_variable *headers = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test POST on a resource that doesn't support it.";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	fixture = setup_invocation_test();
	response = response_alloc();
	ast_ari_invoke(NULL, "foo", AST_HTTP_POST, get_params, headers, response);

	ast_test_validate(test, 0 == invocation_count);
	ast_test_validate(test, 405 == response->response_code);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(invoke_not_found)
{
	RAII_VAR(void *, fixture, NULL, tear_down_invocation_test);
	RAII_VAR(struct ast_ari_response *, response, NULL, response_free);
	struct ast_variable *get_params = NULL;
	struct ast_variable *headers = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = "/res/ari/";
		info->summary = "Test GET on a resource that does not exist.";
		info->description = "Test ARI binding logic.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	fixture = setup_invocation_test();
	response = response_alloc();
	ast_ari_invoke(NULL, "foo/fizzle/i-am-not-a-resource", AST_HTTP_GET, get_params, headers, response);

	ast_test_validate(test, 0 == invocation_count);
	ast_test_validate(test, 404 == response->response_code);

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(get_docs);
	AST_TEST_UNREGISTER(get_docs_nohost);
	AST_TEST_UNREGISTER(get_docs_notfound);
	AST_TEST_UNREGISTER(get_docs_hackerz);
	AST_TEST_UNREGISTER(invoke_get);
	AST_TEST_UNREGISTER(invoke_wildcard);
	AST_TEST_UNREGISTER(invoke_delete);
	AST_TEST_UNREGISTER(invoke_post);
	AST_TEST_UNREGISTER(invoke_bad_post);
	AST_TEST_UNREGISTER(invoke_not_found);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(get_docs);
	AST_TEST_REGISTER(get_docs_nohost);
	AST_TEST_REGISTER(get_docs_notfound);
	AST_TEST_REGISTER(get_docs_hackerz);
	AST_TEST_REGISTER(invoke_get);
	AST_TEST_REGISTER(invoke_wildcard);
	AST_TEST_REGISTER(invoke_delete);
	AST_TEST_REGISTER(invoke_post);
	AST_TEST_REGISTER(invoke_bad_post);
	AST_TEST_REGISTER(invoke_not_found);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "ARI testing",
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_ari",
	);
