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
 * \brief Call Parking Resource
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

/*** MODULEINFO
	<depend>bridge_holding</depend>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_parking" language="en_US">
		<configFile name="res_parking.conf">
			<configObject name="globals">
				<synopsis>Options that apply to every parking lot</synopsis>
			</configObject>
			<configObject name="parking_lot">
				<synopsis>Defined parking lots for res_parking to use to park calls on</synopsis>
				<configOption name="context" default="parkedcalls">
					<synopsis>The name of the context where calls are parked and picked up from.</synopsis>
					<description><para>This option is only used if parkext is set.</para></description>
				</configOption>
				<configOption name="parkext">
					<synopsis>Extension to park calls to this parking lot.</synopsis>
					<description><para>If this option is used, this extension will automatically be created to place calls into
                        parking lots. In addition, if parkext_exclusive is set for this parking lot, the name of the parking lot
                        will be included in the application's arguments so that it only parks to this parking lot. The extension
                        will be created in <literal>context</literal>. Using this option also creates extensions for retrieving
                        parked calls from the parking spaces in the same context.</para></description>
				</configOption>
				<configOption name="parkext_exclusive" default="no">
					<synopsis>If yes, the extension registered as parkext will park exclusively to this parking lot.</synopsis>
				</configOption>
				<configOption name="parkpos" default="701-750">
					<synopsis>Numerical range of parking spaces which can be used to retrieve parked calls.</synopsis>
					<description><para>If parkext is set, these extensions will automatically be mapped in <literal>context</literal>
						in order to pick up calls parked to these parking spaces.</para></description>
				</configOption>
				<configOption name="parkinghints" default="no">
					<synopsis>If yes, this parking lot will add hints automatically for parking spaces.</synopsis>
				</configOption>
				<configOption name="parkingtime" default="45">
					<synopsis>Amount of time a call will remain parked before giving up (in seconds).</synopsis>
				</configOption>
				<configOption name="parkedmusicclass">
					<synopsis>Which music class to use for parked calls. They will use the default if unspecified.</synopsis>
				</configOption>
				<configOption name="comebacktoorigin" default="yes">
					<synopsis>Determines what should be done with the parked channel if no one picks it up before it times out.</synopsis>
					<description><para>Valid Options:</para>
						<enumlist>
							<enum name="yes">
								<para>Automatically have the parked channel dial the device that parked the call with dial
									timeout set by the <literal>parkingtime</literal> option. When the call times out an extension
									to dial the PARKER will automatically be created in the <literal>park-dial</literal> context with
									an extension of the flattened parker device name. If the call is not answered, the parked channel
									that is timing out will continue in the dial plan at that point if there are more priorities in
									the extension (which won't be the case unless the dialplan deliberately includes such priorities
									in the <literal>park-dial</literal> context through pattern matching or deliberately written
									flattened peer extensions).</para>
							</enum>
							<enum name="no">
								<para>Place the call into the PBX at <literal>comebackcontext</literal> instead. The extension will
									still be set as the flattened peer name. If an extension the flattened peer name isn't available
									then it will fall back to the <literal>s</literal> extension. If that also is unavailable it will
									attempt to fall back to <literal>s@default</literal>. The normal dial extension will still be
									created in the <literal>park-dial</literal> context with the extension also being the flattened
									peer name.</para>
							</enum>
						</enumlist>
						<note><para>Flattened Peer Names - Extensions can not include slash characters since those are used for pattern
							matching. When a peer name is flattened, slashes become underscores. For example if the parker of a call
							is called <literal>SIP/0004F2040001</literal> then flattened peer name and therefor the extensions created
							and used on timeouts will be <literal>SIP_0004F204001</literal>.</para></note>
						<note><para>When parking times out and the channel returns to the dial plan, the following variables are set:
						</para></note>
						<variablelist>
							<variable name="PARKINGSLOT">
								<para>extension that the call was parked in prior to timing out.</para>
							</variable>
							<variable name="PARKEDLOT">
								<para>name of the lot that the call was parked in prior to timing out.</para>
							</variable>
							<variable name="PARKER">
								<para>The device that parked the call</para>
							</variable>
						</variablelist>
					</description>
				</configOption>
				<configOption name="comebackdialtime" default="30">
					<synopsis>Timeout for the Dial extension created to call back the parker when a parked call times out.</synopsis>
				</configOption>
				<configOption name="comebackcontext" default="parkedcallstimeout">
					<synopsis>Context where parked calls will enter the PBX on timeout when comebacktoorigin=no</synopsis>
					<description><para>The extension the call enters will prioritize the flattened peer name in this context.
						If the flattened peer name extension is unavailable, then the 's' extension in this context will be
						used. If that also is unavailable, the 's' extension in the 'default' context will be used.</para>
					</description>
				</configOption>
				<configOption name="courtesytone">
					<synopsis>If the name of a sound file is provided, use this as the courtesy tone</synopsis>
					<description><para>By default, this tone is only played to the caller of a parked call. Who receives the tone
						can be changed using the <literal>parkedplay</literal> option.</para>
					</description>
				</configOption>
				<configOption name="parkedplay" default="caller">
					<synopsis>Who we should play the courtesytone to on the pickup of a parked call from this lot</synopsis>
					<description>
						<enumlist>
							<enum name="no"><para>Apply to neither side.</para></enum>
							<enum name="caller"><para>Apply to only to the caller picking up the parked call.</para></enum>
							<enum name="callee"><para>Apply to only to the parked call being picked up.</para></enum>
							<enum name="both"><para>Apply to both the caller and the callee.</para></enum>
						</enumlist>
						<note><para>If courtesy tone is not specified then this option will be ignored.</para></note>
					</description>
				</configOption>
				<configOption name="parkedcalltransfers" default="no">
					<synopsis>Apply the DTMF transfer features to the caller and/or callee when parked calls are picked up.</synopsis>
					<description>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='res_parking']/configFile[@name='res_parking.conf']/configObject[@name='parking_lot']/configOption[@name='parkedplay']/description/enumlist)" />
					</description>
				</configOption>
				<configOption name="parkedcallreparking" default="no">
					<synopsis>Apply the DTMF parking feature to the caller and/or callee when parked calls are picked up.</synopsis>
					<description>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='res_parking']/configFile[@name='res_parking.conf']/configObject[@name='parking_lot']/configOption[@name='parkedplay']/description/enumlist)" />
					</description>
				</configOption>
				<configOption name="parkedcallhangup" default="no">
					<synopsis>Apply the DTMF Hangup feature to the caller and/or callee when parked calls are picked up.</synopsis>
					<description>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='res_parking']/configFile[@name='res_parking.conf']/configObject[@name='parking_lot']/configOption[@name='parkedplay']/description/enumlist)" />
					</description>
				</configOption>
				<configOption name="parkedcallrecording" default="no">
					<synopsis>Apply the DTMF recording features to the caller and/or callee when parked calls are picked up</synopsis>
					<description>
						<xi:include xpointer="xpointer(/docs/configInfo[@name='res_parking']/configFile[@name='res_parking.conf']/configObject[@name='parking_lot']/configOption[@name='parkedplay']/description/enumlist)" />
					</description>
				</configOption>
				<configOption name="findslot" default="first">
					<synopsis>Rule to use when trying to figure out which parking space a call should be parked with.</synopsis>
					<description>
						<enumlist>
							<enum name="first"><para>Always try to place in the lowest available space in the parking lot</para></enum>
							<enum name="next"><para>Track the last parking space used and always attempt to use the one immediately after.
							</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="courtesytone">
					<synopsis>If set, the sound set will be played to whomever is set by parkedplay</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "parking/res_parking.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/event.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"
#include "asterisk/features.h"
#include "asterisk/manager.h"

#define PARKED_CALL_APPLICATION "ParkedCall"
#define PARK_AND_ANNOUNCE_APPLICATION "ParkAndAnnounce"

/* TODO Add unit tests for parking */

static int parking_lot_sort_fn(const void *obj_left, const void *obj_right, int flags)
{
	const struct parking_lot *left = obj_left;
	const struct parking_lot *right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_key = right->name;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(left->name, right_key);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncmp(left->name, right_key, strlen(right_key));
	}
	return cmp;
}

/*! All parking lots that are currently alive in some fashion can be obtained from here */
static struct ao2_container *parking_lot_container;

static void *parking_config_alloc(void);

static void *parking_lot_cfg_alloc(const char *cat);
static void *named_item_find(struct ao2_container *container, const char *name); /* XXX This is really just a generic string find. Move to astobj2.c? */

static int config_parking_preapply(void);
static void link_configured_disable_marked_lots(void);

struct parking_global_config {
	/* TODO Implement dynamic parking lots. Entirely. */
	int parkeddynamic;
};

struct parking_config {
	struct parking_global_config *global;
	struct ao2_container *parking_lots;
};

static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "globals",
	.item_offset = offsetof(struct parking_config, global),
	.category_match = ACO_WHITELIST,
	.category = "^general$",
};

struct aco_type *global_options[] = ACO_TYPES(&global_option);

static struct aco_type parking_lot_type = {
	.type = ACO_ITEM,
	.name = "parking_lot",
	.category_match = ACO_BLACKLIST,
	.category = "^(general)$",
	.item_alloc = parking_lot_cfg_alloc,
	.item_find = named_item_find,
	.item_offset = offsetof(struct parking_config, parking_lots),
};

struct aco_type *parking_lot_types[] = ACO_TYPES(&parking_lot_type);

struct aco_file parking_lot_conf = {
	.filename = "res_parking.conf",
	.types = ACO_TYPES(&global_option, &parking_lot_type),
};

static AO2_GLOBAL_OBJ_STATIC(globals);

CONFIG_INFO_STANDARD(cfg_info, globals, parking_config_alloc,
	.files = ACO_FILES(&parking_lot_conf),
	.pre_apply_config = config_parking_preapply,
	.post_apply_config = link_configured_disable_marked_lots,
);

static int parking_lot_cfg_hash_fn(const void *obj, const int flags)
{
	const struct parking_lot_cfg *entry;
	const char *key;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key = obj;
		return ast_str_hash(key);
	case OBJ_PARTIAL_KEY:
		ast_assert(0);
		return 0;
	default:
		entry = obj;
		return ast_str_hash(entry->name);
	}
}

static int parking_lot_cfg_cmp_fn(void *obj, void *arg, const int flags)
{
	struct parking_lot_cfg *entry1 = obj;

	char *key;
	size_t key_size;
	struct parking_lot_cfg *entry2;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key = arg;
		return (!strcmp(entry1->name, key)) ? CMP_MATCH : 0;
	case OBJ_PARTIAL_KEY:
		key = arg;
		key_size = strlen(key);
		return (!strncmp(entry1->name, key, key_size)) ? CMP_MATCH : 0;
	case OBJ_POINTER:
		entry2 = arg;
		return (!strcmp(entry1->name, entry2->name)) ? CMP_MATCH : 0;
	default:
		return CMP_STOP;
	}
}

/*! \brief destructor for parking_config */
static void parking_config_destructor(void *obj)
{
	struct parking_config *cfg = obj;
	ao2_cleanup(cfg->parking_lots);
	ao2_cleanup(cfg->global);
}

/*! \brief destructor for parking_global_config */
static void parking_global_config_destructor(void *obj)
{
	/* For now, do nothing. */
}

/*! \brief allocator callback for parking_config. Notice it returns void * since it is only used by the backend config code */
static void *parking_config_alloc(void)
{
	RAII_VAR(struct parking_config *, cfg, NULL, ao2_cleanup);

	if (!(cfg = ao2_alloc(sizeof(*cfg), parking_config_destructor))) {
		return NULL;
	}

	if (!(cfg->parking_lots = ao2_container_alloc(37, parking_lot_cfg_hash_fn, parking_lot_cfg_cmp_fn))) {
		return NULL;
	}

	if (!(cfg->global = ao2_alloc(sizeof(*cfg->global), parking_global_config_destructor))) {
		return NULL;
	}

	/* Bump the ref count since RAII_VAR is going to eat one */
	ao2_ref(cfg, +1);
	return cfg;
}

void parking_lot_remove_if_unused(struct parking_lot *lot)
{

	if (lot->mode != PARKINGLOT_DISABLED) {
		return;
	}


	if (!ao2_container_count(lot->parked_users)) {
		ao2_unlink(parking_lot_container, lot);
	}
}

static void parking_lot_disable(struct parking_lot *lot)
{
	lot->mode = PARKINGLOT_DISABLED;
	parking_lot_remove_if_unused(lot);
}

/*! \brief Destroy a parking lot cfg object */
static void parking_lot_cfg_destructor(void *obj)
{
	struct parking_lot_cfg *lot_cfg = obj;

	ast_string_field_free_memory(lot_cfg);
}

/* The arg just needs to have the parking space with it */
static int parked_user_cmp_fn(void *obj, void *arg, int flags)
{
	int *search_space = arg;
	struct parked_user *user = obj;
	int object_space = user->parking_space;

	if (*search_space == object_space) {
		return CMP_MATCH;
	}
	return 0;
}

static int parked_user_sort_fn(const void *obj_left, const void *obj_right, int flags)
{
	const struct parked_user *left = obj_left;
	const struct parked_user *right = obj_right;

	return left->parking_space - right->parking_space;
}

/*!
 * \brief create a parking lot structure
 * \param cat name given to the parking lot
 * \retval NULL failure
 * \retval non-NULL successfully allocated parking lot
 */
static void *parking_lot_cfg_alloc(const char *cat)
{
	struct parking_lot_cfg *lot_cfg;

	lot_cfg = ao2_alloc(sizeof(*lot_cfg), parking_lot_cfg_destructor);
	if (!lot_cfg) {
		return NULL;
	}

	if (ast_string_field_init(lot_cfg, 32)) {
		ao2_cleanup(lot_cfg);
		return NULL;
	}

	ast_string_field_set(lot_cfg, name, cat);

	return lot_cfg;
}

/*!
 * XXX This is actually incredibly generic and might be better placed in something like astobj2 if there isn't already an equivalent
 * \brief find an item in a container by its name
 *
 * \param container ao2container where we want the item from
 * \param key name of the item wanted to be found
 *
 * \retval pointer to the parking lot if available. NULL if not found.
 */
static void *named_item_find(struct ao2_container *container, const char *name)
{
	return ao2_find(container, name, OBJ_KEY);
}

/*!
 * \brief Custom field handler for parking positions
 */
static int option_handler_parkpos(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct parking_lot_cfg *lot_cfg = obj;
	int low;
	int high;

	if (sscanf(var->value, "%30d-%30d", &low, &high) != 2) {
		ast_log(LOG_WARNING, "Format for parking positions is a-b, where a and b are numbers\n");
	} else if (high < low || low <= 0 || high <= 0) {
		ast_log(LOG_WARNING, "Format for parking positions is a-b, where a <= b\n");
	} else {
		lot_cfg->parking_start = low;
		lot_cfg->parking_stop = high;
		return 0;
	}
	return -1;
}

/*!
 * \brief Custom field handler for the findslot option
 */
static int option_handler_findslot(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct parking_lot_cfg *lot_cfg = obj;

	if (!strcmp(var->value, "first")) {
		lot_cfg->parkfindnext = 0;
	} else if (!strcmp(var->value, "next")) {
		lot_cfg->parkfindnext = 1;
	} else {
		ast_log(LOG_WARNING, "value '%s' is not valid for findslot option.\n", var->value);
		return -1;
	}

	return 0;
}

/*!
 * \brief Maps string values for option_handler_parkedfeature to their ENUM values
 */
static int parking_feature_flag_cfg(int *param, const char *var)
{
	if (ast_false(var)) {
		*param = 0;
	} else if (!strcasecmp(var, "both")) {
		*param = AST_FEATURE_FLAG_BYBOTH;
	} else if (!strcasecmp(var, "caller")) {
		*param = AST_FEATURE_FLAG_BYCALLER;
	} else if (!strcasecmp(var, "callee")) {
		*param = AST_FEATURE_FLAG_BYCALLEE;
	} else {
		return -1;
	}

	return 0;
}

/*!
 * \brief Custom field handler for feature mapping on parked call pickup options
 */
static int option_handler_parkedfeature(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct parking_lot_cfg *cfg = obj;
	enum parked_call_feature_options option = aco_option_get_flags(opt);
	int *parameter = NULL;

	switch (option) {
	case OPT_PARKEDPLAY:
		parameter = &cfg->parkedplay;
		break;
	case OPT_PARKEDTRANSFERS:
		parameter = &cfg->parkedcalltransfers;
		break;
	case OPT_PARKEDREPARKING:
		parameter = &cfg->parkedcallreparking;
		break;
	case OPT_PARKEDHANGUP:
		parameter = &cfg->parkedcallhangup;
		break;
	case OPT_PARKEDRECORDING:
		parameter = &cfg->parkedcallrecording;
		break;
	}

	if (!parameter) {
		ast_log(LOG_ERROR, "Unable to handle option '%s'\n", var->name);
		return -1;
	}

	if (parking_feature_flag_cfg(parameter, var->value)) {
		ast_log(LOG_ERROR, "'%s' is not a valid value for parking lot option '%s'\n", var->value, var->name);
		return -1;
	}

	return 0;
}

struct ao2_container *get_parking_lot_container(void)
{
	return parking_lot_container;
}

struct parking_lot *parking_lot_find_by_name(const char *lot_name)
{
	struct parking_lot *lot = named_item_find(parking_lot_container, lot_name);
	return lot;
}

const char *find_channel_parking_lot_name(struct ast_channel *chan)
{
	const char *name;

	/* The channel variable overrides everything */
	name = pbx_builtin_getvar_helper(chan, "PARKINGLOT");
	if (ast_strlen_zero(name) && !ast_strlen_zero(ast_channel_parkinglot(chan))) {
		/* Use the channel's parking lot. */
		name = ast_channel_parkinglot(chan);
	}

	/* If the name couldn't be pulled from that either, use the default parking lot name. */
	if (ast_strlen_zero(name)) {
		name = DEFAULT_PARKING_LOT;
	}

	return name;
}

static void parking_lot_destructor(void *obj)
{
	struct parking_lot *lot = obj;

	if (lot->parking_bridge) {
		ast_bridge_destroy(lot->parking_bridge);
	}
	ao2_cleanup(lot->parked_users);
	ao2_cleanup(lot->cfg);
	ast_string_field_free_memory(lot);
}

static struct parking_lot *alloc_new_parking_lot(struct parking_lot_cfg *lot_cfg)
{
	struct parking_lot *lot;
	if (!(lot = ao2_alloc(sizeof(*lot), parking_lot_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(lot, 32)) {
		return NULL;
	}

	/* Create parked user ordered list */
	lot->parked_users = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_RWLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT,
		parked_user_sort_fn,
		parked_user_cmp_fn);

	if (!lot->parked_users) {
		ao2_cleanup(lot);
		return NULL;
	}

	ast_string_field_set(lot, name, lot_cfg->name);
	return lot;
}

struct parking_lot *parking_lot_build_or_update(struct parking_lot_cfg *lot_cfg)
{
	struct parking_lot *lot;
	struct parking_lot_cfg *replaced_cfg = NULL;
	int found = 0;

	/* Start by trying to find it. If that works we can skip the rest. */
	lot = named_item_find(parking_lot_container, lot_cfg->name);
	if (!lot) {
		lot = alloc_new_parking_lot(lot_cfg);

		/* If we still don't have a lot, we failed to alloc one. */
		if (!lot) {
			return NULL;
		}
	} else {
		found = 1;
	}

	/* Set the configuration reference. Unref the one currently in the lot if it's there. */
	if (lot->cfg) {
		replaced_cfg = lot->cfg;
	}

	ao2_ref(lot_cfg, +1);
	lot->cfg = lot_cfg;

	ao2_cleanup(replaced_cfg);

	/* Set the operating mode to normal since the parking lot has a configuration. */
	lot->disable_mark = 0;
	lot->mode = PARKINGLOT_NORMAL;

	if (!found) {
		/* Link after configuration is set since a lot without configuration will cause all kinds of trouble. */
		ao2_link(parking_lot_container, lot);
	};

	return lot;
}

static void generate_or_link_lots_to_configs(void)
{
	RAII_VAR(struct parking_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	struct parking_lot_cfg *lot_cfg;
	struct ao2_iterator iter;

	for (iter = ao2_iterator_init(cfg->parking_lots, 0); (lot_cfg = ao2_iterator_next(&iter)); ao2_ref(lot_cfg, -1)) {
		RAII_VAR(struct parking_lot *, lot, NULL, ao2_cleanup);
		lot = parking_lot_build_or_update(lot_cfg);
	}

	ao2_iterator_destroy(&iter);
}

/* Preapply */

static int verify_default_parking_lot(void)
{
	struct parking_config *cfg = aco_pending_config(&cfg_info);
	RAII_VAR(struct parking_lot_cfg *, lot_cfg, NULL, ao2_cleanup);

	if (!cfg) {
		return 0;
	}

	lot_cfg = ao2_find(cfg->parking_lots, DEFAULT_PARKING_LOT, OBJ_KEY);
	if (!lot_cfg) {
		lot_cfg = parking_lot_cfg_alloc(DEFAULT_PARKING_LOT);
		if (!lot_cfg) {
			return -1;
		}
		ast_log(AST_LOG_NOTICE, "Adding %s profile to res_parking\n", DEFAULT_PARKING_LOT);
		aco_set_defaults(&parking_lot_type, DEFAULT_PARKING_LOT, lot_cfg);
		ast_string_field_set(lot_cfg, parkext, DEFAULT_PARKING_EXTEN);
		ao2_link(cfg->parking_lots, lot_cfg);
	}

	return 0;
}

static void mark_lots_as_disabled(void)
{
	struct ao2_iterator iter;
	struct parking_lot *lot;

	for (iter = ao2_iterator_init(parking_lot_container, 0); (lot = ao2_iterator_next(&iter)); ao2_ref(lot, -1)) {
		/* We aren't concerned with dynamic lots */
		if (lot->mode == PARKINGLOT_DYNAMIC) {
			continue;
		}

		lot->disable_mark = 1;
	}

	ao2_iterator_destroy(&iter);
}

static int config_parking_preapply(void)
{
	mark_lots_as_disabled();
	return verify_default_parking_lot();
}

static void disable_marked_lots(void)
{
	struct ao2_iterator iter;
	struct parking_lot *lot;

	for (iter = ao2_iterator_init(parking_lot_container, 0); (lot = ao2_iterator_next(&iter)); ao2_ref(lot, -1)) {
		if (lot->disable_mark) {
			parking_lot_disable(lot);
		}
	}

	ao2_iterator_destroy(&iter);
}

static void link_configured_disable_marked_lots(void)
{
	generate_or_link_lots_to_configs();
	disable_marked_lots();
}

static int load_module(void)
{
	if (aco_info_init(&cfg_info)) {
		goto error;
	}

	parking_lot_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_RWLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT,
		parking_lot_sort_fn,
		NULL);

	if (!parking_lot_container) {
		goto error;
	}

	/* Global options */

	/* Register the per parking lot options. */
	aco_option_register(&cfg_info, "parkext", ACO_EXACT, parking_lot_types, "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct parking_lot_cfg, parkext));
	aco_option_register(&cfg_info, "context", ACO_EXACT, parking_lot_types, "parkedcalls", OPT_STRINGFIELD_T, 0, STRFLDSET(struct parking_lot_cfg, parking_con));
	aco_option_register(&cfg_info, "parkingtime", ACO_EXACT, parking_lot_types, "45", OPT_UINT_T, 0, FLDSET(struct parking_lot_cfg, parkingtime));
	aco_option_register(&cfg_info, "comebacktoorigin", ACO_EXACT, parking_lot_types, "yes", OPT_BOOL_T, 1, FLDSET(struct parking_lot_cfg, comebacktoorigin));
	aco_option_register(&cfg_info, "comebackcontext", ACO_EXACT, parking_lot_types, "parkedcallstimeout", OPT_STRINGFIELD_T, 0, STRFLDSET(struct parking_lot_cfg, comebackcontext));
	aco_option_register(&cfg_info, "comebackdialtime", ACO_EXACT, parking_lot_types, "30", OPT_UINT_T, 0, FLDSET(struct parking_lot_cfg, comebackdialtime));
	aco_option_register(&cfg_info, "parkedmusicclass", ACO_EXACT, parking_lot_types, "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct parking_lot_cfg, mohclass));
	aco_option_register(&cfg_info, "parkext_exclusive", ACO_EXACT, parking_lot_types, "no", OPT_BOOL_T, 1, FLDSET(struct parking_lot_cfg, parkext_exclusive));
	aco_option_register(&cfg_info, "parkinghints", ACO_EXACT, parking_lot_types, "no", OPT_BOOL_T, 1, FLDSET(struct parking_lot_cfg, parkaddhints));
	aco_option_register(&cfg_info, "courtesytone", ACO_EXACT, parking_lot_types, "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct parking_lot_cfg, courtesytone));

	/* More complicated parking lot options that require special handling */
	aco_option_register_custom(&cfg_info, "parkpos", ACO_EXACT, parking_lot_types, "701-750", option_handler_parkpos, 0);
	aco_option_register_custom(&cfg_info, "findslot", ACO_EXACT, parking_lot_types, "first", option_handler_findslot, 0);
	aco_option_register_custom(&cfg_info, "parkedplay", ACO_EXACT, parking_lot_types, "caller", option_handler_parkedfeature, OPT_PARKEDPLAY);
	aco_option_register_custom(&cfg_info, "parkedcalltransfers", ACO_EXACT, parking_lot_types, "no", option_handler_parkedfeature, OPT_PARKEDTRANSFERS);
	aco_option_register_custom(&cfg_info, "parkedcallreparking", ACO_EXACT, parking_lot_types, "no", option_handler_parkedfeature, OPT_PARKEDREPARKING);
	aco_option_register_custom(&cfg_info, "parkedcallhangup", ACO_EXACT, parking_lot_types, "no", option_handler_parkedfeature, OPT_PARKEDHANGUP);
	aco_option_register_custom(&cfg_info, "parkedcallrecording", ACO_EXACT, parking_lot_types, "no", option_handler_parkedfeature, OPT_PARKEDRECORDING);

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		goto error;
	}

	if (ast_register_application_xml(PARK_APPLICATION, park_app_exec)) {
		goto error;
	}

	if (ast_register_application_xml(PARKED_CALL_APPLICATION, parked_call_app_exec)) {
		goto error;
	}

	if (ast_register_application_xml(PARK_AND_ANNOUNCE_APPLICATION, park_and_announce_app_exec)) {
		goto error;
	}

	if (load_parking_ui()) {
		goto error;
	}

	if (load_parking_manager()) {
		goto error;
	}

	if (load_parking_bridge_features()) {
		goto error;
	}

	/* TODO Dialplan generation for parking lots that set parkext */
	/* TODO Generate hints for parking lots that set parkext and have hints enabled */

	return AST_MODULE_LOAD_SUCCESS;

error:
	aco_info_destroy(&cfg_info);
	return AST_MODULE_LOAD_DECLINE;
}

static int reload_module(void)
{
	if (aco_process_config(&cfg_info, 1) == ACO_PROCESS_ERROR) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return 0;
}

static int unload_module(void)
{
	/* XXX Parking is currently unloadable due to the fact that it loads features which could cause
	 *     significant problems if they disappeared while a channel still had access to them.
	 */
	return -1;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Call Parking Resource",
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
);
