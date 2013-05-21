/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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
 * \brief Parking Core
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "asterisk/pbx.h"
#include "asterisk/bridging.h"
#include "asterisk/parking.h"
#include "asterisk/channel.h"

/*! \brief Message type for parked calls */
static struct stasis_message_type *parked_call_type;

/*! \brief Topic for parking lots */
static struct stasis_topic *parking_topic;

/*! \brief Function Callback for handling blind transfers to park applications */
static ast_park_blind_xfer_fn ast_park_blind_xfer_func = NULL;

/*! \brief Function Callback for handling a bridge channel trying to park itself */
static ast_bridge_channel_park_fn ast_bridge_channel_park_func = NULL;

void ast_parking_stasis_init(void)
{
	parked_call_type = stasis_message_type_create("ast_parked_call");
	parking_topic = stasis_topic_create("ast_parking");
}

void ast_parking_stasis_disable(void)
{
	ao2_cleanup(parked_call_type);
	ao2_cleanup(parking_topic);
	parked_call_type = NULL;
	parking_topic = NULL;
}

struct stasis_topic *ast_parking_topic(void)
{
	return parking_topic;
}

struct stasis_message_type *ast_parked_call_type(void)
{
	return parked_call_type;
}

/*! \brief Destructor for parked_call_payload objects */
static void parked_call_payload_destructor(void *obj)
{
	struct ast_parked_call_payload *park_obj = obj;

	ao2_cleanup(park_obj->parkee);
	ao2_cleanup(park_obj->parker);
	ast_string_field_free_memory(park_obj);
}

struct ast_parked_call_payload *ast_parked_call_payload_create(enum ast_parked_call_event_type event_type,
		struct ast_channel_snapshot *parkee_snapshot, struct ast_channel_snapshot *parker_snapshot,
		struct ast_channel_snapshot *retriever_snapshot, const char *parkinglot,
		unsigned int parkingspace, unsigned long int timeout,
		unsigned long int duration)
{
	RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);

	payload = ao2_alloc(sizeof(*payload), parked_call_payload_destructor);
	if (!payload) {
		return NULL;
	}

	if (ast_string_field_init(payload, 32)) {
		return NULL;
	}

	payload->event_type = event_type;

	ao2_ref(parkee_snapshot, +1);
	payload->parkee = parkee_snapshot;

	if (parker_snapshot) {
		ao2_ref(parker_snapshot, +1);
		payload->parker = parker_snapshot;
	}

	if (retriever_snapshot) {
		ao2_ref(retriever_snapshot, +1);
		payload->retriever = retriever_snapshot;
	}

	if (parkinglot) {
		ast_string_field_set(payload, parkinglot, parkinglot);
	}

	payload->parkingspace = parkingspace;
	payload->timeout = timeout;
	payload->duration = duration;

	/* Bump the ref count by one since RAII_VAR is going to eat one when we leave. */
	ao2_ref(payload, +1);
	return payload;
}

void ast_install_park_blind_xfer_func(ast_park_blind_xfer_fn park_blind_xfer_func)
{
	ast_park_blind_xfer_func = park_blind_xfer_func;
}

void ast_install_bridge_channel_park_func(ast_bridge_channel_park_fn bridge_channel_park_func)
{
	ast_bridge_channel_park_func = bridge_channel_park_func;
}

void ast_uninstall_park_blind_xfer_func(void)
{
	ast_park_blind_xfer_func = NULL;
}

void ast_uninstall_bridge_channel_park_func(void)
{
	ast_bridge_channel_park_func = NULL;
}

int ast_park_blind_xfer(struct ast_bridge *bridge, struct ast_bridge_channel *parker,
		struct ast_exten *park_exten)
{
	static int warned = 0;
	if (ast_park_blind_xfer_func) {
		return ast_park_blind_xfer_func(bridge, parker, park_exten);
	}

	if (warned++ % 10 == 0) {
		ast_verb(3, "%s attempted to blind transfer to a parking extension, but no parking blind transfer function is loaded.\n",
			ast_channel_name(parker->chan));
	}

	return -1;
}

struct ast_exten *ast_get_parking_exten(const char *exten_str, struct ast_channel *chan, const char *context)
{
	struct ast_exten *exten;
	struct pbx_find_info q = { .stacklen = 0 }; /* the rest is reset in pbx_find_extension */
	const char *app_at_exten;

	ast_debug(4, "Checking if %s@%s is a parking exten\n", exten_str, context);
	exten = pbx_find_extension(chan, NULL, &q, context, exten_str, 1, NULL, NULL,
		E_MATCH);
	if (!exten) {
		return NULL;
	}

	app_at_exten = ast_get_extension_app(exten);
	if (!app_at_exten || strcasecmp(PARK_APPLICATION, app_at_exten)) {
		return NULL;
	}

	return exten;
}

void ast_bridge_channel_park(struct ast_bridge_channel *bridge_channel, const char *parkee_uuid, const char *parker_uuid, const char *app_data)
{
	/* Run installable function */
	if (ast_bridge_channel_park_func) {
		return ast_bridge_channel_park_func(bridge_channel, parkee_uuid, parker_uuid, app_data);
	}
}
