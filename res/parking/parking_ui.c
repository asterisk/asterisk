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
 * \brief Call Parking CLI commands
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "res_parking.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"
#include "asterisk/features.h"
#include "asterisk/manager.h"

static void display_parked_call(struct parked_user *user, int fd)
{
	ast_cli(fd, "  Space               :  %d\n", user->parking_space);
	ast_cli(fd, "  Channel             :  %s\n", ast_channel_name(user->chan));
	ast_cli(fd, "  Parker Dial String  :  %s\n", user->parker_dial_string);
	ast_cli(fd, "\n");
}

static int display_parked_users_cb(void *obj, void *arg, int flags)
{
	int *fd = arg;
	struct parked_user *user = obj;
	display_parked_call(user, *fd);
	return 0;
}

static void display_parking_lot(struct parking_lot *lot, int fd)
{
	ast_cli(fd, "Parking Lot: %s\n--------------------------------------------------------------------------\n", lot->name);
	ast_cli(fd, "Parking Extension   :  %s\n", lot->cfg->parkext);
	ast_cli(fd, "Parking Context     :  %s\n", lot->cfg->parking_con);
	ast_cli(fd, "Parking Spaces      :  %d-%d\n", lot->cfg->parking_start, lot->cfg->parking_stop);
	ast_cli(fd, "Parking Time        :  %u sec\n", lot->cfg->parkingtime);
	ast_cli(fd, "Comeback to Origin  :  %s\n", lot->cfg->comebacktoorigin ? "yes" : "no");
	ast_cli(fd, "Comeback Context    :  %s%s\n", lot->cfg->comebackcontext, lot->cfg->comebacktoorigin ? " (comebacktoorigin=yes, not used)" : "");
	ast_cli(fd, "Comeback Dial Time  :  %u sec\n", lot->cfg->comebackdialtime);
	ast_cli(fd, "MusicOnHold Class   :  %s\n", lot->cfg->mohclass);
	ast_cli(fd, "Enabled             :  %s\n", (lot->mode == PARKINGLOT_DISABLED) ? "no" : "yes");
	ast_cli(fd, "Dynamic             :  %s\n", (lot->mode == PARKINGLOT_DYNAMIC) ? "yes" : "no");
	ast_cli(fd, "\n");
}

static int display_parking_lot_cb(void *obj, void *arg, int flags)
{
	int *fd = arg;
	struct parking_lot *lot = obj;
	display_parking_lot(lot, *fd);
	return 0;
}

static void cli_display_parking_lot(int fd, const char *name)
{
	RAII_VAR(struct parking_lot *, lot, NULL, ao2_cleanup);
	lot = parking_lot_find_by_name(name);

	/* If the parking lot couldn't be found with the search, also abort. */
	if (!lot) {
		ast_cli(fd, "Could not find parking lot '%s'\n\n", name);
		return;
	}

	display_parking_lot(lot, fd);

	ast_cli(fd, "Parked Calls\n------------\n");

	if (!ao2_container_count(lot->parked_users)) {
		ast_cli(fd, "  (none)\n");
		ast_cli(fd, "\n\n");
		return;
	}

	ao2_callback(lot->parked_users, OBJ_MULTIPLE | OBJ_NODATA, display_parked_users_cb, &fd);
	ast_cli(fd, "\n");
}

static void cli_display_parking_global(int fd)
{
	ast_cli(fd, "Parking General Options\n"
	            "-----------------------\n");
	ast_cli(fd, "Dynamic Parking     :  %s\n", parking_dynamic_lots_enabled() ? "yes" : "no");
	ast_cli(fd, "\n");
}

static void cli_display_parking_lot_list(int fd)
{
	struct ao2_container *lot_container;

	lot_container = get_parking_lot_container();

	if (!lot_container) {
		ast_cli(fd, "Failed to obtain parking lot list.\n\n");
		return;
	}

	ao2_callback(lot_container, OBJ_MULTIPLE | OBJ_NODATA, display_parking_lot_cb, &fd);
	ast_cli(fd, "\n");
}

struct parking_lot_complete {
	int seeking;    /*! Nth match to return. */
	int which;      /*! Which match currently on. */
};

static int complete_parking_lot_search(void *obj, void *arg, void *data, int flags)
{
	struct parking_lot_complete *search = data;
	if (++search->which > search->seeking) {
		return CMP_MATCH;
	}
	return 0;
}

static char *complete_parking_lot(const char *word, int seeking)
{
	char *ret = NULL;
	struct parking_lot *lot;
	struct ao2_container *global_lots = get_parking_lot_container();
	struct parking_lot_complete search = {
		.seeking = seeking,
		};

	lot = ao2_callback_data(global_lots, ast_strlen_zero(word) ? 0 : OBJ_PARTIAL_KEY,
		complete_parking_lot_search, (char *) word, &search);

	if (!lot) {
		return NULL;
	}

	ret = ast_strdup(lot->name);
	ao2_ref(lot, -1);
	return ret;
}

/* \brief command parking show <name> */
static char *handle_show_parking_lot_cmd(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "parking show";
		e->usage =
			"Usage: parking show [name]\n"
			"	Shows a list of parking lots or details of a specific parking lot.";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_parking_lot(a->word, a->n);
		}
		return NULL;
	}

	ast_cli(a->fd, "\n");

	if (a->argc == 2) {
		cli_display_parking_global(a->fd);
		cli_display_parking_lot_list(a->fd);
		return CLI_SUCCESS;
	}

	if (a->argc == 3) {
		cli_display_parking_lot(a->fd, a->argv[2]);
		return CLI_SUCCESS;
	}

	return CLI_SHOWUSAGE;
}

static struct ast_cli_entry cli_parking_lot[] = {
	AST_CLI_DEFINE(handle_show_parking_lot_cmd, "Show a parking lot or a list of all parking lots."),
};

int load_parking_ui(const struct ast_module_info *ast_module_info)
{
	return ast_cli_register_multiple(cli_parking_lot, ARRAY_LEN(cli_parking_lot));
}

void unload_parking_ui(void)
{
	ast_cli_unregister_multiple(cli_parking_lot, ARRAY_LEN(cli_parking_lot));
}
