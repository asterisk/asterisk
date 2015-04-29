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
 * \brief Call Parking Device State Management
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"
#include "asterisk/logger.h"
#include "res_parking.h"
#include "asterisk/devicestate.h"

struct parking_lot_extension_inuse_search {
	char *context;
	int exten;
};

static int retrieve_parked_user_targeted(void *obj, void *arg, int flags)
{
	int *target = arg;
	struct parked_user *user = obj;
	if (user->parking_space == *target) {
		return CMP_MATCH;
	}

	return 0;
}

static int parking_lot_search_context_extension_inuse(void *obj, void *arg, int flags)
{
	struct parking_lot *lot = obj;
	struct parking_lot_extension_inuse_search *search = arg;
	RAII_VAR(struct parked_user *, user, NULL, ao2_cleanup);

	if (strcmp(lot->cfg->parking_con, search->context)) {
		return 0;
	}

	if ((search->exten < lot->cfg->parking_start) || (search->exten > lot->cfg->parking_stop)) {
		return 0;
	}

	user = ao2_callback(lot->parked_users, 0, retrieve_parked_user_targeted, &search->exten);
	if (!user) {
		return 0;
	}

	ao2_lock(user);
	if (user->resolution != PARK_UNSET) {
		/* The parked user isn't in an answerable state. */
		ao2_unlock(user);
		return 0;
	}
	ao2_unlock(user);

	return CMP_MATCH;
}

static enum ast_device_state metermaidstate(const char *data)
{
	struct ao2_container *global_lots = get_parking_lot_container();
	RAII_VAR(struct parking_lot *, lot, NULL, ao2_cleanup);
	char *context;
	char *exten;
	struct parking_lot_extension_inuse_search search = {};

	context = ast_strdupa(data);

	exten = strsep(&context, "@");

	if (ast_strlen_zero(context) || ast_strlen_zero(exten)) {
		return AST_DEVICE_INVALID;
	}

	search.context = context;
	if (sscanf(exten, "%d", &search.exten) != 1) {
		return AST_DEVICE_INVALID;
	}

	ast_debug(4, "Checking state of exten %d in context %s\n", search.exten, context);

	lot = ao2_callback(global_lots, 0, parking_lot_search_context_extension_inuse, &data);
	if (!lot) {
		return AST_DEVICE_NOT_INUSE;
	}

	return AST_DEVICE_INUSE;
}

void parking_notify_metermaids(int exten, const char *context, enum ast_device_state state)
{
	ast_debug(4, "Notification of state change to metermaids %d@%s\n to state '%s'\n",
		exten, context, ast_devstate2str(state));

	ast_devstate_changed(state, AST_DEVSTATE_CACHABLE, "park:%d@%s", exten, context);
}

int load_parking_devstate(void)
{
	return ast_devstate_prov_add("Park", metermaidstate);
}
