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

/*! \file
 *
 * \brief /api-docs/applications.{format} implementation - Stasis application
 * resources
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/stasis_app.h"
#include "resource_applications.h"

static int append_json(void *obj, void *arg, int flags)
{
	const char *app = obj;
	struct ast_json *array = arg;

	ast_json_array_append(array, stasis_app_to_json(app));

	return 0;
}

void ast_ari_applications_list(struct ast_variable *headers,
	struct ast_ari_applications_list_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ao2_container *, apps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	size_t count;

	apps = stasis_app_get_all();
	json = ast_json_array_create();
	if (!apps || !json) {
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Allocation failed");
		return;
	}

	ao2_lock(apps);
	count = ao2_container_count(apps);
	ao2_callback(apps, OBJ_NOLOCK | OBJ_NODATA, append_json, json);
	ao2_lock(apps);

	if (count != ast_json_array_size(json)) {
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Allocation failed");
		return;
	}

	ast_ari_response_ok(response, ast_json_ref(json));
}

void ast_ari_applications_get(struct ast_variable *headers,
	struct ast_ari_applications_get_args *args,
	struct ast_ari_response *response)
{
	struct ast_json *json;

	json = stasis_app_to_json(args->application_name);

	if (!json) {
		ast_ari_response_error(response, 404, "Not Found",
			"Application not found");
		return;
	}

	ast_ari_response_ok(response, json);
}

void ast_ari_applications_subscribe(struct ast_variable *headers,
	struct ast_ari_applications_subscribe_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	enum stasis_app_subscribe_res res;

	if (args->event_source_count <= 0) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Missing parameter eventSource");
		return;
	}

	if (ast_strlen_zero(args->application_name)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Missing parameter applicationName");
		return;
	}

	res = stasis_app_subscribe(args->application_name, args->event_source,
		args->event_source_count, &json);

	switch (res) {
	case STASIS_ASR_OK:
		ast_ari_response_ok(response, ast_json_ref(json));
		break;
	case STASIS_ASR_APP_NOT_FOUND:
		ast_ari_response_error(response, 404, "Not Found",
			"Application not found");
		break;
	case STASIS_ASR_EVENT_SOURCE_NOT_FOUND:
		ast_ari_response_error(response, 422, "Unprocessable Entity",
			"Event source does not exist");
		break;
	case STASIS_ASR_EVENT_SOURCE_BAD_SCHEME:
		ast_ari_response_error(response, 400, "Bad Request",
			"Invalid event source URI scheme");
		break;
	case STASIS_ASR_INTERNAL_ERROR:
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Error processing request");
		break;
	}
}

void ast_ari_applications_unsubscribe(struct ast_variable *headers,
	struct ast_ari_applications_unsubscribe_args *args,
	struct ast_ari_response *response)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	enum stasis_app_subscribe_res res;

	if (args->event_source_count == 0) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Missing parameter eventSource");
		return;
	}

	res = stasis_app_unsubscribe(args->application_name, args->event_source,
		args->event_source_count, &json);

	switch (res) {
	case STASIS_ASR_OK:
		ast_ari_response_ok(response, ast_json_ref(json));
		break;
	case STASIS_ASR_APP_NOT_FOUND:
		ast_ari_response_error(response, 404, "Not Found",
			"Application not found");
		break;
	case STASIS_ASR_EVENT_SOURCE_NOT_FOUND:
		ast_ari_response_error(response, 422, "Unprocessable Entity",
			"Event source was not subscribed to");
		break;
	case STASIS_ASR_EVENT_SOURCE_BAD_SCHEME:
		ast_ari_response_error(response, 400, "Bad Request",
			"Invalid event source URI scheme");
		break;
	case STASIS_ASR_INTERNAL_ERROR:
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Error processing request");
	}
}
