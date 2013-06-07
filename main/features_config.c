/*
* Asterisk -- An open source telephony toolkit.
*
* Copyright (C) 2013, Digium, Inc.
*
* Mark Michelson <mmichelson@digium.com>
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

#include "asterisk.h"

#include "asterisk/features_config.h"
#include "asterisk/config_options.h"
#include "asterisk/datastore.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/cli.h"

/* BUGBUG XML Documentation is still needed for configuration options */
/*** DOCUMENTATION
	<function name="FEATURE" language="en_US">
		<synopsis>
			Get or set a feature option on a channel.
		</synopsis>
		<syntax>
			<parameter name="option_name" required="true">
				<para>The allowed values are:</para>
				<enumlist>
					<enum name="inherit"><para>Inherit feature settings made in FEATURE or FEATUREMAP to child channels.</para></enum>
					<enum name="featuredigittimeout"><para>Milliseconds allowed between digits when entering a feature code.</para></enum>
					<enum name="transferdigittimeout"><para>Milliseconds allowed between digits when dialing a transfer destination</para></enum>
					<enum name="atxfernoanswertimeout"><para>Milliseconds to wait for transfer destination to answer</para></enum>
					<enum name="atxferdropcall"><para>Hang up the call entirely if the attended transfer fails</para></enum>
					<enum name="atxferloopdelay"><para>Milliseconds to wait between attempts to re-dial transfer destination</para></enum>
					<enum name="atxfercallbackretries"><para>Number of times to re-attempt dialing a transfer destination</para></enum>
					<enum name="xfersound"><para>Sound to play to a transferee when a transfer completes</para></enum>
					<enum name="xferfailsound"><para>Sound to play to a transferee when a transfer fails</para></enum>
					<enum name="atxferabort"><para>Digits to dial to abort an attended transfer attempt</para></enum>
					<enum name="atxfercomplete"><para>Digits to dial to complete an attended transfer</para></enum>
					<enum name="atxferthreeway"><para>Digits to dial to change an attended transfer into a three-way call</para></enum>
					<enum name="pickupexten"><para>Digits used for picking up ringing calls</para></enum>
					<enum name="pickupsound"><para>Sound to play to picker when a call is picked up</para></enum>
					<enum name="pickupfailsound"><para>Sound to play to picker when a call cannot be picked up</para></enum>
					<enum name="courtesytone"><para>Sound to play when automon or automixmon is activated</para></enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>When this function is used as a read, it will get the current
			value of the specified feature option for this channel.  It will be
			the value of this option configured in features.conf if a channel specific
			value has not been set.  This function can also be used to set a channel
			specific value for the supported feature options.</para>
		</description>
		<see-also>
			<ref type="function">FEATUREMAP</ref>
		</see-also>
	</function>
	<function name="FEATUREMAP" language="en_US">
		<synopsis>
			Get or set a feature map to a given value on a specific channel.
		</synopsis>
		<syntax>
			<parameter name="feature_name" required="true">
				<para>The allowed values are:</para>
				<enumlist>
					<enum name="atxfer"><para>Attended Transfer</para></enum>
					<enum name="blindxfer"><para>Blind Transfer</para></enum>
					<enum name="automon"><para>Auto Monitor</para></enum>
					<enum name="disconnect"><para>Call Disconnect</para></enum>
					<enum name="parkcall"><para>Park Call</para></enum>
					<enum name="automixmon"><para>Auto MixMonitor</para></enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>When this function is used as a read, it will get the current
			digit sequence mapped to the specified feature for this channel.  This
			value will be the one configured in features.conf if a channel specific
			value has not been set.  This function can also be used to set a channel
			specific value for a feature mapping.</para>
		</description>
		<see-also>
			<ref type="function">FEATURE</ref>
		</see-also>
	</function>
 ***/
/*! Default general options */
#define DEFAULT_FEATURE_DIGIT_TIMEOUT               1000

/*! Default xfer options */
#define DEFAULT_TRANSFER_DIGIT_TIMEOUT              3000
#define DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER  15000
#define DEFAULT_ATXFER_DROP_CALL                    0
#define DEFAULT_ATXFER_LOOP_DELAY                   10000
#define DEFAULT_ATXFER_CALLBACK_RETRIES             2
#define DEFAULT_XFERSOUND                           "beep"
#define DEFAULT_XFERFAILSOUND                       "beeperr"
#define DEFAULT_ATXFER_ABORT                        "*1"
#define DEFAULT_ATXFER_COMPLETE                     "*2"
#define DEFAULT_ATXFER_THREEWAY                     "*3"

/*! Default pickup options */
#define DEFAULT_PICKUPEXTEN                         "*8"
#define DEFAULT_PICKUPSOUND                         ""
#define DEFAULT_PICKUPFAILSOUND                     ""

/*! Default featuremap options */
#define DEFAULT_FEATUREMAP_BLINDXFER                "#"
#define DEFAULT_FEATUREMAP_DISCONNECT               "*"
#define DEFAULT_FEATUREMAP_AUTOMON                  ""
#define DEFAULT_FEATUREMAP_ATXFER                   ""
#define DEFAULT_FEATUREMAP_PARKCALL                 ""
#define DEFAULT_FEATUREMAP_AUTOMIXMON               ""

/*!
 * \brief Configuration from the "general" section of features.conf
 */
struct features_global_config {
	struct ast_features_general_config *general;
	struct ast_features_xfer_config *xfer;
	struct ast_features_pickup_config *pickup;
};

static void ast_applicationmap_item_destructor(void *obj)
{
	struct ast_applicationmap_item *item = obj;

	ast_string_field_free_memory(item);
}

static int applicationmap_sort(const void *obj, const void *arg, int flags)
{
	const struct ast_applicationmap_item *item1 = obj;
	const struct ast_applicationmap_item *item2;
	const char *key2;

	switch(flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key2 = arg;
		return strcasecmp(item1->name, key2);
	case OBJ_PARTIAL_KEY:
		key2 = arg;
		return strncasecmp(item1->name, key2, strlen(key2));
	default:
	case OBJ_POINTER:
		item2 = arg;
		return strcasecmp(item1->name, item2->name);
	}
}

/*!
 * \brief Entry in the container of featuregroups
 */
struct featuregroup_item {
	AST_DECLARE_STRING_FIELDS(
		/*! The name of the applicationmap item that we are referring to */
		AST_STRING_FIELD(appmap_item_name);
		/*! Custom DTMF override to use instead of the default for the applicationmap item */
		AST_STRING_FIELD(dtmf_override);
	);
	/*! The applicationmap item that is being referred to */
	struct ast_applicationmap_item *appmap_item;
};

static void featuregroup_item_destructor(void *obj)
{
	struct featuregroup_item *item = obj;

	ast_string_field_free_memory(item);
	ao2_cleanup(item->appmap_item);
}

static int group_item_sort(const void *obj, const void *arg, int flags)
{
	const struct featuregroup_item *item1 = obj;
	const struct featuregroup_item *item2;
	const char *key2;

	switch(flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key2 = arg;
		return strcasecmp(item1->appmap_item_name, key2);
	case OBJ_PARTIAL_KEY:
		key2 = arg;
		return strncasecmp(item1->appmap_item_name, key2, strlen(key2));
	case OBJ_POINTER:
		item2 = arg;
		return strcasecmp(item1->appmap_item_name, item2->appmap_item_name);
	default:
		return CMP_STOP;
	}
}

/*!
 * \brief Featuregroup representation
 */
struct featuregroup {
	/*! The name of the featuregroup */
	const char *name;
	/*! A container of featuregroup_items */
	struct ao2_container *items;
};

static int featuregroup_hash(const void *obj, int flags)
{
	const struct featuregroup *group;
	const char *key;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key = obj;
		return ast_str_case_hash(key);
	case OBJ_PARTIAL_KEY:
		ast_assert(0);
		return 0;
	case OBJ_POINTER:
	default:
		group = obj;
		return ast_str_case_hash(group->name);
	}
}

static int featuregroup_cmp(void *obj, void *arg, int flags)
{
	struct featuregroup *group1 = obj;
	struct featuregroup *group2;
	const char *key2;

	switch(flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key2 = arg;
		return strcasecmp(group1->name, key2) ? 0 : CMP_MATCH;
	case OBJ_PARTIAL_KEY:
		key2 = arg;
		return strncasecmp(group1->name, key2, strlen(key2)) ? 0 : CMP_MATCH;
	case OBJ_POINTER:
		group2 = arg;
		return strcasecmp(group1->name, group2->name) ? 0 : CMP_MATCH;
	default:
		return CMP_STOP;
	}
}

static void *featuregroup_find(struct ao2_container *group_container, const char *category)
{
	return ao2_find(group_container, category, OBJ_KEY);
}

static void featuregroup_destructor(void *obj)
{
	struct featuregroup *group = obj;

	ast_free((char *) group->name);
	ao2_cleanup(group->items);
}

static void *featuregroup_alloc(const char *cat)
{
	struct featuregroup *group;

	group = ao2_alloc(sizeof(*group), featuregroup_destructor);
	if (!group) {
		return NULL;
	}

	group->name = ast_strdup(cat);
	if (!group->name) {
		ao2_cleanup(group);
		return NULL;
	}

	group->items = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, group_item_sort, NULL);
	if (!group->items) {
		ao2_cleanup(group);
		return NULL;
	}

	return group;
}

struct features_config {
	struct features_global_config *global;
	struct ast_featuremap_config *featuremap;
	struct ao2_container *applicationmap;
	struct ao2_container *featuregroups;
};

static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "globals",
	.category_match = ACO_WHITELIST,
	.category = "^general$",
	.item_offset = offsetof(struct features_config, global),
};

static struct aco_type featuremap_option = {
	.type = ACO_GLOBAL,
	.name = "featuremap",
	.category_match = ACO_WHITELIST,
	.category = "^featuremap$",
	.item_offset = offsetof(struct features_config, featuremap),
};

static struct aco_type applicationmap_option = {
	.type = ACO_GLOBAL,
	.name = "applicationmap",
	.category_match = ACO_WHITELIST,
	.category = "^applicationmap$",
	.item_offset = offsetof(struct features_config, applicationmap),
};

static struct aco_type featuregroup_option = {
	.type = ACO_ITEM,
	.name = "featuregroup",
	.category_match = ACO_BLACKLIST,
	.category = "^(general|featuremap|applicationmap|parkinglot_.*)$",
	.item_offset = offsetof(struct features_config, featuregroups),
	.item_alloc = featuregroup_alloc,
	.item_find = featuregroup_find,
};

static struct aco_type *global_options[] = ACO_TYPES(&global_option);
static struct aco_type *featuremap_options[] = ACO_TYPES(&featuremap_option);
static struct aco_type *applicationmap_options[] = ACO_TYPES(&applicationmap_option);
static struct aco_type *featuregroup_options[] = ACO_TYPES(&featuregroup_option);

static struct aco_file features_conf = {
	.filename = "features.conf",
	.types = ACO_TYPES(&global_option, &featuremap_option, &applicationmap_option, &featuregroup_option),
};

AO2_GLOBAL_OBJ_STATIC(globals);

static void features_config_destructor(void *obj)
{
	struct features_config *cfg = obj;

	ao2_cleanup(cfg->global);
	ao2_cleanup(cfg->featuremap);
	ao2_cleanup(cfg->applicationmap);
	ao2_cleanup(cfg->featuregroups);
}

static void featuremap_config_destructor(void *obj)
{
	struct ast_featuremap_config *cfg = obj;

	ast_string_field_free_memory(cfg);
}

static void global_config_destructor(void *obj)
{
	struct features_global_config *cfg = obj;

	ao2_cleanup(cfg->general);
	ao2_cleanup(cfg->xfer);
	ao2_cleanup(cfg->pickup);
}

static void general_destructor(void *obj)
{
	struct ast_features_general_config *cfg = obj;

	ast_string_field_free_memory(cfg);
}

static void xfer_destructor(void *obj)
{
	struct ast_features_xfer_config *cfg = obj;

	ast_string_field_free_memory(cfg);
}

static void pickup_destructor(void *obj)
{
	struct ast_features_pickup_config *cfg = obj;

	ast_string_field_free_memory(cfg);
}

static struct features_global_config *global_config_alloc(void)
{
	RAII_VAR(struct features_global_config *, cfg, NULL, ao2_cleanup);

	cfg = ao2_alloc(sizeof(*cfg), global_config_destructor);
	if (!cfg) {
		return NULL;
	}

	cfg->general = ao2_alloc(sizeof(*cfg->general), general_destructor);
	if (!cfg->general || ast_string_field_init(cfg->general, 32)) {
		return NULL;
	}

	cfg->xfer = ao2_alloc(sizeof(*cfg->xfer), xfer_destructor);
	if (!cfg->xfer || ast_string_field_init(cfg->xfer, 32)) {
		return NULL;
	}

	cfg->pickup = ao2_alloc(sizeof(*cfg->pickup), pickup_destructor);
	if (!cfg->pickup || ast_string_field_init(cfg->pickup, 32)) {
		return NULL;
	}

	ao2_ref(cfg, +1);
	return cfg;
}

static struct ao2_container *applicationmap_alloc(int replace_duplicates)
{
	return ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK,
		replace_duplicates ? AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE : AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW,
		applicationmap_sort, NULL);
}

/*!
 * \internal
 * \brief Allocate the major configuration structure
 *
 * The parameter is used to determine if the applicationmap and featuregroup
 * structures should be allocated. We only want to allocate these structures for
 * the global features_config structure. For the datastores on channels, we don't
 * need to allocate these structures because they are not used.
 *
 * \param allocate_applicationmap See previous explanation
 * \retval NULL Failed to alloate configuration
 * \retval non-NULL Allocated configuration
 */
static struct features_config *__features_config_alloc(int allocate_applicationmap)
{
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);

	cfg = ao2_alloc(sizeof(*cfg), features_config_destructor);
	if (!cfg) {
		return NULL;
	}

	cfg->global = global_config_alloc();;
	if (!cfg->global) {
		return NULL;
	}

	cfg->featuremap = ao2_alloc(sizeof(*cfg->featuremap), featuremap_config_destructor);
	if (!cfg->featuremap || ast_string_field_init(cfg->featuremap, 32)) {
		return NULL;
	}

	if (allocate_applicationmap) {
		cfg->applicationmap = applicationmap_alloc(1);
		if (!cfg->applicationmap) {
			return NULL;
		}

		cfg->featuregroups = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 11, featuregroup_hash,
			featuregroup_cmp);
		if (!cfg->featuregroups) {
			return NULL;
		}
	}

	ao2_ref(cfg, +1);
	return cfg;

}

static void *features_config_alloc(void)
{
	return __features_config_alloc(1);
}

static void general_copy(struct ast_features_general_config *dest, const struct ast_features_general_config *src)
{
	ast_string_fields_copy(dest, src);
	dest->featuredigittimeout = src->featuredigittimeout;
}

static void xfer_copy(struct ast_features_xfer_config *dest, const struct ast_features_xfer_config *src)
{
	ast_string_fields_copy(dest, src);
	dest->transferdigittimeout = src->transferdigittimeout;
	dest->atxfernoanswertimeout = src->atxfernoanswertimeout;
	dest->atxferloopdelay = src->atxferloopdelay;
	dest->atxfercallbackretries = src->atxfercallbackretries;
	dest->atxferdropcall = src->atxferdropcall;
}

static void pickup_copy(struct ast_features_pickup_config *dest, const struct ast_features_pickup_config *src)
{
	ast_string_fields_copy(dest, src);
}

static void global_copy(struct features_global_config *dest, const struct features_global_config *src)
{
	general_copy(dest->general, src->general);
	xfer_copy(dest->xfer, src->xfer);
	pickup_copy(dest->pickup, src->pickup);
}

static void featuremap_copy(struct ast_featuremap_config *dest, const struct ast_featuremap_config *src)
{
	ast_string_fields_copy(dest, src);
}

static void features_copy(struct features_config *dest, const struct features_config *src)
{
	global_copy(dest->global, src->global);
	featuremap_copy(dest->featuremap, src->featuremap);

	/* applicationmap and featuregroups are purposely not copied. A channel's applicationmap
	 * is produced on the fly when ast_get_chan_applicationmap() is called
	 */
}

static struct features_config *features_config_dup(const struct features_config *orig)
{
	struct features_config *dup;

	dup = __features_config_alloc(0);
	if (!dup) {
		return NULL;
	}

	features_copy(dup, orig);

	return dup;
}

static int general_set(struct ast_features_general_config *general, const char *name,
		const char *value)
{
	int res = 0;

	if (!strcasecmp(name, "featuredigittimeout")) {
		res = ast_parse_arg(value, PARSE_INT32, &general->featuredigittimeout);
	} else if (!strcasecmp(name, "courtesytone")) {
		ast_string_field_set(general, courtesytone, value);
	} else {
		/* Unrecognized option */
		res = -1;
	}

	return res;
}

static int general_get(struct ast_features_general_config *general, const char *field,
		char *buf, size_t len)
{
	int res = 0;

	if (!strcasecmp(field, "featuredigittimeout")) {
		snprintf(buf, len, "%u", general->featuredigittimeout);
	} else if (!strcasecmp(field, "courtesytone")) {
		ast_copy_string(buf, general->courtesytone, len);
	} else {
		/* Unrecognized option */
		res = -1;
	}

	return res;
}

static int xfer_set(struct ast_features_xfer_config *xfer, const char *name,
		const char *value)
{
	int res = 0;

	if (!strcasecmp(name, "transferdigittimeout")) {
		res = ast_parse_arg(value, PARSE_INT32, &xfer->transferdigittimeout);
	} else if (!strcasecmp(name, "atxfernoanswertimeout")) {
		res = ast_parse_arg(value, PARSE_INT32, &xfer->atxfernoanswertimeout);
	} else if (!strcasecmp(name, "atxferloopdelay")) {
		res = ast_parse_arg(value, PARSE_INT32, &xfer->atxferloopdelay);
	} else if (!strcasecmp(name, "atxfercallbackretries")) {
		res = ast_parse_arg(value, PARSE_INT32, &xfer->atxfercallbackretries);
	} else if (!strcasecmp(name, "atxferdropcall")) {
		xfer->atxferdropcall = ast_true(value);
	} else if (!strcasecmp(name, "xfersound")) {
		ast_string_field_set(xfer, xfersound, value);
	} else if (!strcasecmp(name, "xferfailsound")) {
		ast_string_field_set(xfer, xferfailsound, value);
	} else if (!strcasecmp(name, "atxferabort")) {
		ast_string_field_set(xfer, atxferabort, value);
	} else if (!strcasecmp(name, "atxfercomplete")) {
		ast_string_field_set(xfer, atxfercomplete, value);
	} else if (!strcasecmp(name, "atxferthreeway")) {
		ast_string_field_set(xfer, atxferthreeway, value);
	} else {
		/* Unrecognized option */
		res = -1;
	}

	return res;
}

static int xfer_get(struct ast_features_xfer_config *xfer, const char *field,
		char *buf, size_t len)
{
	int res = 0;

	if (!strcasecmp(field, "transferdigittimeout")) {
		snprintf(buf, len, "%u", xfer->transferdigittimeout);
	} else if (!strcasecmp(field, "atxfernoanswertimeout")) {
		snprintf(buf, len, "%u", xfer->atxfernoanswertimeout);
	} else if (!strcasecmp(field, "atxferloopdelay")) {
		snprintf(buf, len, "%u", xfer->atxferloopdelay);
	} else if (!strcasecmp(field, "atxfercallbackretries")) {
		snprintf(buf, len, "%u", xfer->atxfercallbackretries);
	} else if (!strcasecmp(field, "atxferdropcall")) {
		snprintf(buf, len, "%u", xfer->atxferdropcall);
	} else if (!strcasecmp(field, "xfersound")) {
		ast_copy_string(buf, xfer->xfersound, len);
	} else if (!strcasecmp(field, "xferfailsound")) {
		ast_copy_string(buf, xfer->xferfailsound, len);
	} else if (!strcasecmp(field, "atxferabort")) {
		ast_copy_string(buf, xfer->atxferabort, len);
	} else if (!strcasecmp(field, "atxfercomplete")) {
		ast_copy_string(buf, xfer->atxfercomplete, len);
	} else if (!strcasecmp(field, "atxferthreeway")) {
		ast_copy_string(buf, xfer->atxferthreeway, len);
	} else {
		/* Unrecognized option */
		res = -1;
	}

	return res;
}

static int pickup_set(struct ast_features_pickup_config *pickup, const char *name,
		const char *value)
{
	int res = 0;

	if (!strcasecmp(name, "pickupsound")) {
		ast_string_field_set(pickup, pickupsound, value);
	} else if (!strcasecmp(name, "pickupfailsound")) {
		ast_string_field_set(pickup, pickupfailsound, value);
	} else if (!strcasecmp(name, "pickupexten")) {
		ast_string_field_set(pickup, pickupexten, value);
	} else {
		/* Unrecognized option */
		res = -1;
	}

	return res;
}

static int pickup_get(struct ast_features_pickup_config *pickup, const char *field,
		char *buf, size_t len)
{
	int res = 0;

	if (!strcasecmp(field, "pickupsound")) {
		ast_copy_string(buf, pickup->pickupsound, len);
	} else if (!strcasecmp(field, "pickupfailsound")) {
		ast_copy_string(buf, pickup->pickupfailsound, len);
	} else if (!strcasecmp(field, "pickupexten")) {
		ast_copy_string(buf, pickup->pickupexten, len);
	} else {
		/* Unrecognized option */
		res = -1;
	}

	return res;
}

static int featuremap_set(struct ast_featuremap_config *featuremap, const char *name,
		const char *value)
{
	int res = 0;

	if (!strcasecmp(name, "blindxfer")) {
		ast_string_field_set(featuremap, blindxfer, value);
	} else if (!strcasecmp(name, "disconnect")) {
		ast_string_field_set(featuremap, disconnect, value);
	} else if (!strcasecmp(name, "automon")) {
		ast_string_field_set(featuremap, automon, value);
	} else if (!strcasecmp(name, "atxfer")) {
		ast_string_field_set(featuremap, atxfer, value);
	} else if (!strcasecmp(name, "automixmon")) {
		ast_string_field_set(featuremap, automixmon, value);
	} else if (!strcasecmp(name, "parkcall")) {
		ast_string_field_set(featuremap, parkcall, value);
	} else {
		/* Unrecognized option */
		res = -1;
	}

	return res;
}

static int featuremap_get(struct ast_featuremap_config *featuremap, const char *field,
		char *buf, size_t len)
{
	int res = 0;

	if (!strcasecmp(field, "blindxfer")) {
		ast_copy_string(buf, featuremap->blindxfer, len);
	} else if (!strcasecmp(field, "disconnect")) {
		ast_copy_string(buf, featuremap->disconnect, len);
	} else if (!strcasecmp(field, "automon")) {
		ast_copy_string(buf, featuremap->automon, len);
	} else if (!strcasecmp(field, "atxfer")) {
		ast_copy_string(buf, featuremap->atxfer, len);
	} else if (!strcasecmp(field, "automixmon")) {
		ast_copy_string(buf, featuremap->automixmon, len);
	} else if (!strcasecmp(field, "parkcall")) {
		ast_copy_string(buf, featuremap->parkcall, len);
	} else {
		/* Unrecognized option */
		res = -1;
	}

	return res;
}

static void feature_ds_destroy(void *data)
{
	struct features_config *cfg = data;
	ao2_cleanup(cfg);
}

static void *feature_ds_duplicate(void *data)
{
	struct features_config *old_cfg = data;

	return features_config_dup(old_cfg);
}

static const struct ast_datastore_info feature_ds_info = {
	.type = "FEATURE",
	.destroy = feature_ds_destroy,
	.duplicate = feature_ds_duplicate,
};

/*!
 * \internal
 * \brief Find or create feature datastore on a channel
 *
 * \pre chan is locked
 *
 * \return the data on the FEATURE datastore, or NULL on error
 */
static struct features_config *get_feature_ds(struct ast_channel *chan)
{
	RAII_VAR(struct features_config *, orig, NULL, ao2_cleanup);
	struct features_config *cfg;
	struct ast_datastore *ds;

	if ((ds = ast_channel_datastore_find(chan, &feature_ds_info, NULL))) {
		cfg = ds->data;
		ao2_ref(cfg, +1);
		return cfg;
	}

	orig = ao2_global_obj_ref(globals);
	if (!orig) {
		return NULL;
	}

	cfg = features_config_dup(orig);
	if (!cfg) {
		return NULL;
	}

	if (!(ds = ast_datastore_alloc(&feature_ds_info, NULL))) {
		ao2_cleanup(cfg);
		return NULL;
	}

	/* Give the datastore a reference to the config */
	ao2_ref(cfg, +1);
	ds->data = cfg;

	ast_channel_datastore_add(chan, ds);

	return cfg;
}

static struct ast_datastore *get_feature_chan_ds(struct ast_channel *chan)
{
	struct ast_datastore *ds;

	if (!(ds = ast_channel_datastore_find(chan, &feature_ds_info, NULL))) {
		/* Hasn't been created yet.  Trigger creation. */
		RAII_VAR(struct features_config *, cfg, get_feature_ds(chan), ao2_cleanup);
		ds = ast_channel_datastore_find(chan, &feature_ds_info, NULL);
	}

	return ds;
}

struct ast_features_general_config *ast_get_chan_features_general_config(struct ast_channel *chan)
{
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);

	if (chan) {
		cfg = get_feature_ds(chan);
	} else {
		cfg = ao2_global_obj_ref(globals);
	}

	if (!cfg) {
		return NULL;
	}

	ast_assert(cfg->global && cfg->global->general);

	ao2_ref(cfg->global->general, +1);
	return cfg->global->general;
}

struct ast_features_xfer_config *ast_get_chan_features_xfer_config(struct ast_channel *chan)
{
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);

	if (chan) {
		cfg = get_feature_ds(chan);
	} else {
		cfg = ao2_global_obj_ref(globals);
	}

	if (!cfg) {
		return NULL;
	}

	ast_assert(cfg->global && cfg->global->xfer);

	ao2_ref(cfg->global->xfer, +1);
	return cfg->global->xfer;
}

struct ast_features_pickup_config *ast_get_chan_features_pickup_config(struct ast_channel *chan)
{
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);

	if (chan) {
		cfg = get_feature_ds(chan);
	} else {
		cfg = ao2_global_obj_ref(globals);
	}

	if (!cfg) {
		return NULL;
	}

	ast_assert(cfg->global && cfg->global->pickup);

	ao2_ref(cfg->global->pickup, +1);
	return cfg->global->pickup;
}

struct ast_featuremap_config *ast_get_chan_featuremap_config(struct ast_channel *chan)
{
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);

	if (chan) {
		cfg = get_feature_ds(chan);
	} else {
		cfg = ao2_global_obj_ref(globals);
	}

	if (!cfg) {
		return NULL;
	}

	ast_assert(cfg->featuremap != NULL);

	ao2_ref(cfg->featuremap, +1);
	return cfg->featuremap;
}

int ast_get_builtin_feature(struct ast_channel *chan, const char *feature, char *buf, size_t len)
{
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);

	if (chan) {
		cfg = get_feature_ds(chan);
	} else {
		cfg = ao2_global_obj_ref(globals);
	}

	if (!cfg) {
		return -1;
	}

	return featuremap_get(cfg->featuremap, feature, buf, len);
}

int ast_get_feature(struct ast_channel *chan, const char *feature, char *buf, size_t len)
{
	RAII_VAR(struct ao2_container *, applicationmap, NULL, ao2_cleanup);
	RAII_VAR(struct ast_applicationmap_item *, item, NULL, ao2_cleanup);

	if (!ast_get_builtin_feature(chan, feature, buf, len)) {
		return 0;
	}

	/* Dang, must be in the application map */
	applicationmap = ast_get_chan_applicationmap(chan);
	if (!applicationmap) {
		return -1;
	}

	item = ao2_find(applicationmap, feature, OBJ_KEY);
	if (!item) {
		return -1;
	}

	ast_copy_string(buf, item->dtmf, len);
	return 0;
}

static struct ast_applicationmap_item *applicationmap_item_alloc(const char *name,
		const char *app, const char *app_data, const char *moh_class, const char *dtmf,
		unsigned int activate_on_self)
{
	struct ast_applicationmap_item *item;

	item = ao2_alloc(sizeof(*item), ast_applicationmap_item_destructor);

	if (!item || ast_string_field_init(item, 64)) {
		return NULL;
	}

	ast_string_field_set(item, name, name);
	ast_string_field_set(item, app, app);
	ast_string_field_set(item, app_data, app_data);
	ast_string_field_set(item, moh_class, moh_class);
	ast_copy_string(item->dtmf, dtmf, sizeof(item->dtmf));
	item->activate_on_self = activate_on_self;

	return item;
}

static int add_item(void *obj, void *arg, int flags)
{
	struct featuregroup_item *fg_item = obj;
	struct ao2_container *applicationmap = arg;
	RAII_VAR(struct ast_applicationmap_item *, appmap_item, NULL, ao2_cleanup);

	/* If there's no DTMF override, then we can just link
	 * the applicationmap item directly. Otherwise, we need
	 * to create a copy with the DTMF override in place and
	 * link that instead
	 */
	if (ast_strlen_zero(fg_item->dtmf_override)) {
		ao2_ref(fg_item->appmap_item, +1);
		appmap_item = fg_item->appmap_item;
	} else {
		appmap_item = applicationmap_item_alloc(fg_item->appmap_item_name,
				fg_item->appmap_item->app, fg_item->appmap_item->app_data,
				fg_item->appmap_item->moh_class, fg_item->dtmf_override,
				fg_item->appmap_item->activate_on_self);
	}

	if (!appmap_item) {
		return 0;
	}

	ao2_link(applicationmap, appmap_item);
	return 0;
}

struct ao2_container *ast_get_chan_applicationmap(struct ast_channel *chan)
{
	RAII_VAR(struct features_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	struct ao2_container *applicationmap;
	char *group_names;
	char *name;

	if (!cfg) {
		return NULL;
	}

	if (!chan) {
		if (!cfg->applicationmap || ao2_container_count(cfg->applicationmap) == 0) {
			return NULL;
		}
		ao2_ref(cfg->applicationmap, +1);
		return cfg->applicationmap;
	}

	group_names = ast_strdupa(S_OR(pbx_builtin_getvar_helper(chan, "DYNAMIC_FEATURES"), ""));
	if (ast_strlen_zero(group_names)) {
		return NULL;
	}

	applicationmap = applicationmap_alloc(0);
	if (!applicationmap) {
		return NULL;
	}

	while ((name = strsep(&group_names, "#"))) {
		RAII_VAR(struct featuregroup *, group, ao2_find(cfg->featuregroups, name, OBJ_KEY), ao2_cleanup);

		if (!group) {
			RAII_VAR(struct ast_applicationmap_item *, item, ao2_find(cfg->applicationmap, name, OBJ_KEY), ao2_cleanup);

			if (item) {
				ao2_link(applicationmap, item);
			} else {
				ast_log(LOG_WARNING, "Unknown DYNAMIC_FEATURES item '%s' on channel %s.\n",
					name, ast_channel_name(chan));
			}
		} else {
			ao2_callback(group->items, 0, add_item, applicationmap);
		}
	}

	if (ao2_container_count(applicationmap) == 0) {
		ao2_cleanup(applicationmap);
		return NULL;
	}

	return applicationmap;
}

static int applicationmap_handler(const struct aco_option *opt,
		struct ast_variable *var, void *obj)
{
	RAII_VAR(struct ast_applicationmap_item *, item, NULL, ao2_cleanup);
	struct ao2_container *applicationmap = obj;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(dtmf);
		AST_APP_ARG(activate_on);
		AST_APP_ARG(app);
		AST_APP_ARG(app_data);
		AST_APP_ARG(moh_class);
	);
	char *parse = ast_strdupa(var->value);
	char *slash;
	char *paren;
	unsigned int activate_on_self;

	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.dtmf) ||
			ast_strlen_zero(args.activate_on) ||
			ast_strlen_zero(args.app)) {
		ast_log(LOG_WARNING, "Invalid applicationmap syntax for '%s'. Missing required argument\n", var->name);
		return -1;
	}

	/* features.conf used to have an "activated_by" portion
	 * in addition to activate_on. Get rid of whatever may be
	 * there
	 */
	slash = strchr(args.activate_on, '/');
	if (slash) {
		*slash = '\0';
	}

	/* Two syntaxes allowed for applicationmap:
	 * Old: foo = *1,self,NoOp,Boo!,default
	 * New: foo = *1,self,NoOp(Boo!),default
	 *
	 * We need to handle both
	 */
	paren = strchr(args.app, '(');
	if (paren) {
		/* New syntax */
		char *close_paren;

		args.moh_class = args.app_data;
		*paren++ = '\0';
		close_paren = strrchr(paren, ')');
		if (close_paren) {
			*close_paren = '\0';
		}
		args.app_data = paren;

		/* Re-check that the application is not empty */
		if (ast_strlen_zero(args.app)) {
			ast_log(LOG_WARNING, "Applicationmap item '%s' does not contain an application name.\n", var->name);
			return -1;
		}
	} else if (strchr(args.app_data, '"')) {
		args.app_data = ast_strip_quoted(args.app_data, "\"", "\"");
	}

	/* Allow caller and callee to be specified for backwards compatibility */
	if (!strcasecmp(args.activate_on, "self") || !strcasecmp(args.activate_on, "caller")) {
		activate_on_self = 1;
	} else if (!strcasecmp(args.activate_on, "peer") || !strcasecmp(args.activate_on, "callee")) {
		activate_on_self = 0;
	} else {
		ast_log(LOG_WARNING, "Invalid 'activate_on' value %s for applicationmap item %s\n",
			args.activate_on, var->name);
		return -1;
	}

	ast_debug(1, "Allocating applicationmap item: dtmf = %s, app = %s, app_data = %s, moh_class = %s\n",
			args.dtmf, args.app, args.app_data, args.moh_class);

	item = applicationmap_item_alloc(var->name, args.app, args.app_data,
			args.moh_class, args.dtmf, activate_on_self);

	if (!item) {
		return -1;
	}

	if (!ao2_link(applicationmap, item)) {
		return -1;
	}

	return 0;
}

static int featuregroup_handler(const struct aco_option *opt,
		struct ast_variable *var, void *obj)
{
	RAII_VAR(struct featuregroup_item *, item, NULL, ao2_cleanup);
	struct featuregroup *group = obj;

	item = ao2_alloc(sizeof(*item), featuregroup_item_destructor);
	if (!item || ast_string_field_init(item, 32)) {
		return -1;
	}

	ast_string_field_set(item, appmap_item_name, var->name);
	ast_string_field_set(item, dtmf_override, var->value);

	if (!ao2_link(group->items, item)) {
		return -1;
	}

	/* We wait to look up the application map item in the preapply callback */

	return 0;
}

static int general_handler(const struct aco_option *opt,
		struct ast_variable *var, void *obj)
{
	struct features_global_config *global = obj;
	struct ast_features_general_config *general = global->general;

	return general_set(general, var->name, var->value);
}

static int xfer_handler(const struct aco_option *opt,
		struct ast_variable *var, void *obj)
{
	struct features_global_config *global = obj;
	struct ast_features_xfer_config *xfer = global->xfer;

	return xfer_set(xfer, var->name, var->value);
}

static int pickup_handler(const struct aco_option *opt,
		struct ast_variable *var, void *obj)
{
	struct features_global_config *global = obj;
	struct ast_features_pickup_config *pickup = global->pickup;

	return pickup_set(pickup, var->name, var->value);
}

static int featuremap_handler(const struct aco_option *opt,
		struct ast_variable *var, void *obj)
{
	struct ast_featuremap_config *featuremap = obj;

	return featuremap_set(featuremap, var->name, var->value);
}

static int check_featuregroup_item(void *obj, void *arg, void *data, int flags)
{
	struct ast_applicationmap_item *appmap_item;
	struct featuregroup_item *fg_item = obj;
	int *err = arg;
	struct ao2_container *applicationmap = data;

	appmap_item = ao2_find(applicationmap, fg_item->appmap_item_name, OBJ_KEY);
	if (!appmap_item) {
		*err = 1;
		return CMP_STOP;
	}

	fg_item->appmap_item = appmap_item;

	return 0;
}

static int check_featuregroup(void *obj, void *arg, void *data, int flags)
{
	struct featuregroup *group = obj;
	int *err = arg;

	ao2_callback_data(group->items, 0, check_featuregroup_item, arg, data);

	if (*err) {
		ast_log(LOG_WARNING, "Featuregroup %s refers to non-existent applicationmap item\n",
				group->name);
	}

	return *err ? CMP_STOP : 0;
}

static int features_pre_apply_config(void);

CONFIG_INFO_CORE("features", cfg_info, globals, features_config_alloc,
	.files = ACO_FILES(&features_conf),
	.pre_apply_config = features_pre_apply_config,
);

static int features_pre_apply_config(void)
{
	struct features_config *cfg = aco_pending_config(&cfg_info);
	int err = 0;

	/* Now that the entire config has been processed, we can check that the featuregroup
	 * items refer to actual applicationmap items.
	 */

	ao2_callback_data(cfg->featuregroups, 0, check_featuregroup, &err, cfg->applicationmap);

	return err;
}

static int feature_read(struct ast_channel *chan, const char *cmd, char *data,
	       char *buf, size_t len)
{
	int res;
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);
	SCOPED_CHANNELLOCK(lock, chan);

	if (!strcasecmp(data, "inherit")) {
		struct ast_datastore *ds = get_feature_chan_ds(chan);
		unsigned int inherit = ds ? ds->inheritance : 0;

		snprintf(buf, len, "%s", inherit ? "yes" : "no");
		return 0;
	}

	cfg = get_feature_ds(chan);
	if (!cfg) {
		return -1;
	}

	res = general_get(cfg->global->general, data, buf, len) &&
		xfer_get(cfg->global->xfer, data, buf, len) &&
		pickup_get(cfg->global->pickup, data, buf, len);

	if (res) {
		ast_log(LOG_WARNING, "Invalid argument '%s' to FEATURE()\n", data);
	}

	return res;
}

static int feature_write(struct ast_channel *chan, const char *cmd, char *data,
		const char *value)
{
	int res;
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);
	SCOPED_CHANNELLOCK(lock, chan);

	if (!strcasecmp(data, "inherit")) {
		struct ast_datastore *ds = get_feature_chan_ds(chan);
		if (ds) {
			ds->inheritance = ast_true(value) ? DATASTORE_INHERIT_FOREVER : 0;
		}
		return 0;
	}

	if (!(cfg = get_feature_ds(chan))) {
		return -1;
	}

	res = general_set(cfg->global->general, data, value) &&
		xfer_set(cfg->global->xfer, data, value) &&
		pickup_set(cfg->global->pickup, data, value);

	if (res) {
		ast_log(LOG_WARNING, "Invalid argument '%s' to FEATURE()\n", data);
	}

	return res;
}

static int featuremap_read(struct ast_channel *chan, const char *cmd, char *data,
	       char *buf, size_t len)
{
	int res;
	SCOPED_CHANNELLOCK(lock, chan);

	res = ast_get_builtin_feature(chan, data, buf, len);

	if (res) {
		ast_log(LOG_WARNING, "Invalid argument '%s' to FEATUREMAP()\n", data);
	}

	return res;
}

static int featuremap_write(struct ast_channel *chan, const char *cmd, char *data,
		const char *value)
{
	int res;
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);
	SCOPED_CHANNELLOCK(lock, chan);

	if (!(cfg = get_feature_ds(chan))) {
		return -1;
	}

	res = featuremap_set(cfg->featuremap, data, value);
	if (res) {
		ast_log(LOG_WARNING, "Invalid argument '%s' to FEATUREMAP()\n", data);
		return -1;
	}

	return 0;
}

static struct ast_custom_function feature_function = {
	.name = "FEATURE",
	.read = feature_read,
	.write = feature_write
};

static struct ast_custom_function featuremap_function = {
	.name = "FEATUREMAP",
	.read = featuremap_read,
	.write = featuremap_write
};

static int load_config(int reload)
{
	if (!reload && aco_info_init(&cfg_info)) {
		ast_log(LOG_ERROR, "Unable to initialize configuration info for features\n");
		return -1;
	}

	aco_option_register_custom(&cfg_info, "featuredigittimeout", ACO_EXACT, global_options,
			__stringify(DEFAULT_FEATURE_DIGIT_TIMEOUT), general_handler, 0);
	aco_option_register_custom(&cfg_info, "courtesytone", ACO_EXACT, global_options,
			__stringify(DEFAULT_COURTESY_TONE), general_handler, 0);

	aco_option_register_custom(&cfg_info, "transferdigittimeout", ACO_EXACT, global_options,
			__stringify(DEFAULT_TRANSFER_DIGIT_TIMEOUT), xfer_handler, 0)
	aco_option_register_custom(&cfg_info, "atxfernoanswertimeout", ACO_EXACT, global_options,
			__stringify(DEFAULT_NOANSWER_TIMEOUT_ATTENDED_TRANSFER), xfer_handler, 0);
	aco_option_register_custom(&cfg_info, "atxferdropcall", ACO_EXACT, global_options,
			__stringify(DEFAULT_ATXFER_DROP_CALL), xfer_handler, 0);
	aco_option_register_custom(&cfg_info, "atxferloopdelay", ACO_EXACT, global_options,
			__stringify(DEFAULT_ATXFER_LOOP_DELAY), xfer_handler, 0);
	aco_option_register_custom(&cfg_info, "atxfercallbackretries", ACO_EXACT, global_options,
			__stringify(DEFAULT_ATXFER_CALLBACK_RETRIES), xfer_handler, 0);
	aco_option_register_custom(&cfg_info, "xfersound", ACO_EXACT, global_options,
			DEFAULT_XFERSOUND, xfer_handler, 0);
	aco_option_register_custom(&cfg_info, "xferfailsound", ACO_EXACT, global_options,
			DEFAULT_XFERFAILSOUND, xfer_handler, 0);
	aco_option_register_custom(&cfg_info, "atxferabort", ACO_EXACT, global_options,
			DEFAULT_ATXFER_ABORT, xfer_handler, 0);
	aco_option_register_custom(&cfg_info, "atxfercomplete", ACO_EXACT, global_options,
			DEFAULT_ATXFER_COMPLETE, xfer_handler, 0);
	aco_option_register_custom(&cfg_info, "atxferthreeway", ACO_EXACT, global_options,
			DEFAULT_ATXFER_THREEWAY, xfer_handler, 0);

	aco_option_register_custom(&cfg_info, "pickupexten", ACO_EXACT, global_options,
			DEFAULT_PICKUPEXTEN, pickup_handler, 0);
	aco_option_register_custom(&cfg_info, "pickupsound", ACO_EXACT, global_options,
			DEFAULT_PICKUPSOUND, pickup_handler, 0);
	aco_option_register_custom(&cfg_info, "pickupfailsound", ACO_EXACT, global_options,
			DEFAULT_PICKUPFAILSOUND, pickup_handler, 0);

	aco_option_register_custom(&cfg_info, "blindxfer", ACO_EXACT, featuremap_options,
			DEFAULT_FEATUREMAP_BLINDXFER, featuremap_handler, 0);
	aco_option_register_custom(&cfg_info, "disconnect", ACO_EXACT, featuremap_options,
			DEFAULT_FEATUREMAP_DISCONNECT, featuremap_handler, 0);
	aco_option_register_custom(&cfg_info, "automon", ACO_EXACT, featuremap_options,
			DEFAULT_FEATUREMAP_AUTOMON, featuremap_handler, 0);
	aco_option_register_custom(&cfg_info, "atxfer", ACO_EXACT, featuremap_options,
			DEFAULT_FEATUREMAP_ATXFER, featuremap_handler, 0);
	aco_option_register_custom(&cfg_info, "parkcall", ACO_EXACT, featuremap_options,
			DEFAULT_FEATUREMAP_PARKCALL, featuremap_handler, 0);
	aco_option_register_custom(&cfg_info, "automixmon", ACO_EXACT, featuremap_options,
			DEFAULT_FEATUREMAP_AUTOMIXMON, featuremap_handler, 0);

	aco_option_register_custom(&cfg_info, "^.*$", ACO_REGEX, applicationmap_options,
			"", applicationmap_handler, 0);

	aco_option_register_custom(&cfg_info, "^.*$", ACO_REGEX, featuregroup_options,
			"", featuregroup_handler, 0);

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		ast_log(LOG_ERROR, "Failed to process features.conf configuration!\n");
		if (!reload) {
			aco_info_destroy(&cfg_info);
			ao2_global_obj_release(globals);
		}
		return -1;
	}

	return 0;
}

static int print_featuregroup(void *obj, void *arg, int flags)
{
	struct featuregroup_item *item = obj;
	struct ast_cli_args *a = arg;

	ast_cli(a->fd, "===> --> %s (%s)\n", item->appmap_item_name,
			S_OR(item->dtmf_override, item->appmap_item->dtmf));

	return 0;
}

static int print_featuregroups(void *obj, void *arg, int flags)
{
	struct featuregroup *group = obj;
	struct ast_cli_args *a = arg;

	ast_cli(a->fd, "===> Group: %s\n", group->name);

	ao2_callback(group->items, 0, print_featuregroup, a);
	return 0;
}

#define HFS_FORMAT "%-25s %-7s %-7s\n"

static int print_applicationmap(void *obj, void *arg, int flags)
{
	struct ast_applicationmap_item *item = obj;
	struct ast_cli_args *a = arg;

	ast_cli(a->fd, HFS_FORMAT, item->name, "no def", item->dtmf);
	return 0;
}

/*!
 * \brief CLI command to list configured features
 * \param e
 * \param cmd
 * \param a
 *
 * \retval CLI_SUCCESS on success.
 * \retval NULL when tab completion is used.
 */
static char *handle_feature_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct features_config *, cfg, NULL, ao2_cleanup);

	switch (cmd) {

	case CLI_INIT:
		e->command = "features show";
		e->usage =
			"Usage: features show\n"
			"       Lists configured features\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	cfg = ao2_global_obj_ref(globals);

	ast_cli(a->fd, HFS_FORMAT, "Builtin Feature", "Default", "Current");
	ast_cli(a->fd, HFS_FORMAT, "---------------", "-------", "-------");

	ast_cli(a->fd, HFS_FORMAT, "Pickup", DEFAULT_PICKUPEXTEN, cfg->global->pickup->pickupexten);
	ast_cli(a->fd, HFS_FORMAT, "Blind Transfer", DEFAULT_FEATUREMAP_BLINDXFER, cfg->featuremap->blindxfer);
	ast_cli(a->fd, HFS_FORMAT, "Attended Transfer", DEFAULT_FEATUREMAP_ATXFER, cfg->featuremap->atxfer);
	ast_cli(a->fd, HFS_FORMAT, "One Touch Monitor", DEFAULT_FEATUREMAP_AUTOMON, cfg->featuremap->automon);
	ast_cli(a->fd, HFS_FORMAT, "Disconnect Call", DEFAULT_FEATUREMAP_DISCONNECT, cfg->featuremap->disconnect);
	ast_cli(a->fd, HFS_FORMAT, "Park Call", DEFAULT_FEATUREMAP_PARKCALL, cfg->featuremap->parkcall);
	ast_cli(a->fd, HFS_FORMAT, "One Touch MixMonitor", DEFAULT_FEATUREMAP_AUTOMIXMON, cfg->featuremap->automixmon);

	ast_cli(a->fd, "\n");
	ast_cli(a->fd, HFS_FORMAT, "Dynamic Feature", "Default", "Current");
	ast_cli(a->fd, HFS_FORMAT, "---------------", "-------", "-------");
	if (!cfg->applicationmap || ao2_container_count(cfg->applicationmap) == 0) {
		ast_cli(a->fd, "(none)\n");
	} else {
		ao2_callback(cfg->applicationmap, 0, print_applicationmap, a);
	}

	ast_cli(a->fd, "\nFeature Groups:\n");
	ast_cli(a->fd, "---------------\n");
	if (!cfg->featuregroups || ao2_container_count(cfg->featuregroups) == 0) {
		ast_cli(a->fd, "(none)\n");
	} else {
		ao2_callback(cfg->featuregroups, 0, print_featuregroups, a);
	}

	ast_cli(a->fd, "\n");

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_features_config[] = {
	AST_CLI_DEFINE(handle_feature_show, "Lists configured features"),
};

void ast_features_config_shutdown(void)
{
	ast_custom_function_unregister(&featuremap_function);
	ast_custom_function_unregister(&feature_function);
	ast_cli_unregister_multiple(cli_features_config, ARRAY_LEN(cli_features_config));
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(globals);
}

int ast_features_config_reload(void)
{
	return load_config(1);
}

int ast_features_config_init(void)
{
	int res;

	res = load_config(0);
	res |= __ast_custom_function_register(&feature_function, NULL);
	res |= __ast_custom_function_register(&featuremap_function, NULL);
	res |= ast_cli_register_multiple(cli_features_config, ARRAY_LEN(cli_features_config));

	if (res) {
		ast_features_config_shutdown();
	}

	return res;
}
