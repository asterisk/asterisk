/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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
 * \brief /api-docs/deviceStates.{format} implementation- Device state resources
 *
 * \author Kevin Harwell <kharwell@digium.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis_device_state</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "resource_device_states.h"
#include "asterisk/stasis_app_device_state.h"

void ast_ari_device_states_list(
	struct ast_variable *headers,
	struct ast_ari_device_states_list_args *args,
	struct ast_ari_response *response)
{
	struct ast_json *json;

	if (!(json = stasis_app_device_states_to_json())) {
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Error building response");
		return;
	}

	ast_ari_response_ok(response, json);
}

void ast_ari_device_states_get(struct ast_variable *headers,
	struct ast_ari_device_states_get_args *args,
	struct ast_ari_response *response)
{
	struct ast_json *json;

	if (!(json = stasis_app_device_state_to_json(
		      args->device_name, ast_device_state(args->device_name)))) {
		ast_ari_response_error(response, 500,
			"Internal Server Error", "Error building response");
		return;
	}

	ast_ari_response_ok(response, json);
}

void ast_ari_device_states_update(struct ast_variable *headers,
	struct ast_ari_device_states_update_args *args,
	struct ast_ari_response *response)
{
	switch (stasis_app_device_state_update(
			args->device_name, args->device_state)) {
	case STASIS_DEVICE_STATE_NOT_CONTROLLED:
		ast_ari_response_error(response, 409,
			"Conflict", "Uncontrolled device specified");
		return;
	case STASIS_DEVICE_STATE_MISSING:
		ast_ari_response_error(response, 404,
			"Not Found", "Device name is missing");
		return;
	case STASIS_DEVICE_STATE_UNKNOWN:
		ast_ari_response_error(response, 500, "Internal Server Error",
				       "Unknown device");
		return;
	case STASIS_DEVICE_STATE_OK:
	case STASIS_DEVICE_STATE_SUBSCRIBERS: /* shouldn't be returned for update */
		ast_ari_response_no_content(response);
	}
}

void ast_ari_device_states_delete(struct ast_variable *headers,
	struct ast_ari_device_states_delete_args *args,
	struct ast_ari_response *response)
{
	switch (stasis_app_device_state_delete(args->device_name)) {
	case STASIS_DEVICE_STATE_NOT_CONTROLLED:
		ast_ari_response_error(response, 409,
			"Conflict", "Uncontrolled device specified");
		return;
	case STASIS_DEVICE_STATE_MISSING:
		ast_ari_response_error(response, 404,
			"Not Found", "Device name is missing");
		return;
	case STASIS_DEVICE_STATE_SUBSCRIBERS:
		ast_ari_response_error(response, 500,
			"Internal Server Error",
			"Cannot delete device with subscribers");
		return;
	case STASIS_DEVICE_STATE_OK:
	case STASIS_DEVICE_STATE_UNKNOWN:
		ast_ari_response_no_content(response);
	}
}
