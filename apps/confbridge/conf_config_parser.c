/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief ConfBridge config parser
 *
 * \author David Vossel <dvossel@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")
#include "asterisk/logger.h"
#include "asterisk/config.h"
#include "include/confbridge.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/bridging_features.h"
#include "asterisk/stringfields.h"
#include "asterisk/pbx.h"

#define CONFBRIDGE_CONFIG "confbridge.conf"

static struct ao2_container *user_profiles;
static struct ao2_container *bridge_profiles;
static struct ao2_container *menus;

/*! bridge profile container functions */
static int bridge_cmp_cb(void *obj, void *arg, int flags)
{
	const struct bridge_profile *entry1 = obj;
	const struct bridge_profile *entry2 = arg;
	return (!strcasecmp(entry1->name, entry2->name)) ? CMP_MATCH | CMP_STOP : 0;
}
static int bridge_hash_cb(const void *obj, const int flags)
{
	const struct bridge_profile *b_profile = obj;
	return ast_str_case_hash(b_profile->name);
}
static int bridge_mark_delme_cb(void *obj, void *arg, int flag)
{
	struct bridge_profile *entry = obj;
	entry->delme = 1;
	return 0;
}
static int match_bridge_delme_cb(void *obj, void *arg, int flag)
{
	const struct bridge_profile *entry = obj;
	return entry->delme ? CMP_MATCH : 0;
}

/*! menu container functions */
static int menu_cmp_cb(void *obj, void *arg, int flags)
{
	const struct conf_menu *entry1 = obj;
	const struct conf_menu *entry2 = arg;
	return (!strcasecmp(entry1->name, entry2->name)) ? CMP_MATCH | CMP_STOP : 0;
}
static int menu_hash_cb(const void *obj, const int flags)
{
	const struct conf_menu *menu = obj;
	return ast_str_case_hash(menu->name);
}
static int menu_mark_delme_cb(void *obj, void *arg, int flag)
{
	struct conf_menu *entry = obj;
	entry->delme = 1;
	return 0;
}
static int match_menu_delme_cb(void *obj, void *arg, int flag)
{
	const struct conf_menu *entry = obj;
	return entry->delme ? CMP_MATCH : 0;
}
static void menu_destructor(void *obj)
{
	struct conf_menu *menu = obj;
	struct conf_menu_entry *entry = NULL;

	while ((entry = AST_LIST_REMOVE_HEAD(&menu->entries, entry))) {
		conf_menu_entry_destroy(entry);
		ast_free(entry);
	}
}

/*! User profile container functions */
static int user_cmp_cb(void *obj, void *arg, int flags)
{
	const struct user_profile *entry1 = obj;
	const struct user_profile *entry2 = arg;
	return (!strcasecmp(entry1->name, entry2->name)) ? CMP_MATCH | CMP_STOP : 0;
}
static int user_hash_cb(const void *obj, const int flags)
{
	const struct user_profile *u_profile = obj;
	return ast_str_case_hash(u_profile->name);
}
static int user_mark_delme_cb(void *obj, void *arg, int flag)
{
	struct user_profile *entry = obj;
	entry->delme = 1;
	return 0;
}
static int match_user_delme_cb(void *obj, void *arg, int flag)
{
	const struct user_profile *entry = obj;
	return entry->delme ? CMP_MATCH : 0;
}

/*! Bridge Profile Sounds functions */
static void bridge_profile_sounds_destroy_cb(void *obj)
{
	struct bridge_profile_sounds *sounds = obj;
	ast_string_field_free_memory(sounds);
}

static struct bridge_profile_sounds *bridge_profile_sounds_alloc(void)
{
	struct bridge_profile_sounds *sounds = ao2_alloc(sizeof(*sounds), bridge_profile_sounds_destroy_cb);

	if (!sounds) {
		return NULL;
	}
	if (ast_string_field_init(sounds, 512)) {
		ao2_ref(sounds, -1);
		return NULL;
	}

	return sounds;
}

static int set_user_option(const char *name, const char *value, struct user_profile *u_profile)
{
	if (!strcasecmp(name, "admin")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_ADMIN);
	} else if (!strcasecmp(name, "marked")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_MARKEDUSER);
	} else if (!strcasecmp(name, "startmuted")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_STARTMUTED);
	} else if (!strcasecmp(name, "music_on_hold_when_empty")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_MUSICONHOLD);
	} else if (!strcasecmp(name, "quiet")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_QUIET);
	} else if (!strcasecmp(name, "announce_user_count_all")) {
		if (ast_true(value)) {
			u_profile->flags = u_profile->flags | USER_OPT_ANNOUNCEUSERCOUNTALL;
		} else if (ast_false(value)) {
			u_profile->flags = u_profile->flags & ~USER_OPT_ANNOUNCEUSERCOUNTALL;
		} else if (sscanf(value, "%30u", &u_profile->announce_user_count_all_after) == 1) {
			u_profile->flags = u_profile->flags | USER_OPT_ANNOUNCEUSERCOUNTALL;
		} else {
			return -1;
		}
	} else if (!strcasecmp(name, "announce_user_count")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_ANNOUNCEUSERCOUNT);
	} else if (!strcasecmp(name, "announce_only_user")) {
		u_profile->flags = ast_true(value) ?
			u_profile->flags & ~USER_OPT_NOONLYPERSON :
			u_profile->flags | USER_OPT_NOONLYPERSON;
	} else if (!strcasecmp(name, "wait_marked")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_WAITMARKED);
	} else if (!strcasecmp(name, "end_marked")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_ENDMARKED);
	} else if (!strcasecmp(name, "talk_detection_events")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_TALKER_DETECT);
	} else if (!strcasecmp(name, "dtmf_passthrough")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_DTMF_PASS);
	} else if (!strcasecmp(name, "announce_join_leave")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_ANNOUNCE_JOIN_LEAVE);
	} else if (!strcasecmp(name, "pin")) {
		ast_copy_string(u_profile->pin, value, sizeof(u_profile->pin));
	} else if (!strcasecmp(name, "music_on_hold_class")) {
		ast_copy_string(u_profile->moh_class, value, sizeof(u_profile->moh_class));
	} else if (!strcasecmp(name, "denoise")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_DENOISE);
	} else if (!strcasecmp(name, "dsp_talking_threshold")) {
		if (sscanf(value, "%30u", &u_profile->talking_threshold) != 1) {
			return -1;
		}
	} else if (!strcasecmp(name, "dsp_silence_threshold")) {
		if (sscanf(value, "%30u", &u_profile->silence_threshold) != 1) {
			return -1;
		}
	} else if (!strcasecmp(name, "dsp_drop_silence")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_DROP_SILENCE);
	} else if (!strcasecmp(name, "template")) {
		if (!(conf_find_user_profile(NULL, value, u_profile))) {
			return -1;
		}
	} else if (!strcasecmp(name, "jitterbuffer")) {
		ast_set2_flag(u_profile, ast_true(value), USER_OPT_JITTERBUFFER);
	} else {
		return -1;
	}
	return 0;
}

static int set_sound(const char *sound_name, const char *sound_file, struct bridge_profile_sounds *sounds)
{
	if (ast_strlen_zero(sound_file)) {
		return -1;
	}

	if (!strcasecmp(sound_name, "sound_only_person")) {
		ast_string_field_set(sounds, onlyperson, sound_file);
	} else if (!strcasecmp(sound_name, "sound_has_joined")) {
		ast_string_field_set(sounds, hasjoin, sound_file);
	} else if (!strcasecmp(sound_name, "sound_has_left")) {
		ast_string_field_set(sounds, hasleft, sound_file);
	} else if (!strcasecmp(sound_name, "sound_kicked")) {
		ast_string_field_set(sounds, kicked, sound_file);
	} else if (!strcasecmp(sound_name, "sound_muted")) {
		ast_string_field_set(sounds, muted, sound_file);
	} else if (!strcasecmp(sound_name, "sound_unmuted")) {
		ast_string_field_set(sounds, unmuted, sound_file);
	} else if (!strcasecmp(sound_name, "sound_there_are")) {
		ast_string_field_set(sounds, thereare, sound_file);
	} else if (!strcasecmp(sound_name, "sound_other_in_party")) {
		ast_string_field_set(sounds, otherinparty, sound_file);
	} else if (!strcasecmp(sound_name, "sound_place_into_conference")) {
		ast_string_field_set(sounds, placeintoconf, sound_file);
	} else if (!strcasecmp(sound_name, "sound_wait_for_leader")) {
		ast_string_field_set(sounds, waitforleader, sound_file);
	} else if (!strcasecmp(sound_name, "sound_get_pin")) {
		ast_string_field_set(sounds, getpin, sound_file);
	} else if (!strcasecmp(sound_name, "sound_invalid_pin")) {
		ast_string_field_set(sounds, invalidpin, sound_file);
	} else if (!strcasecmp(sound_name, "sound_locked")) {
		ast_string_field_set(sounds, locked, sound_file);
	} else if (!strcasecmp(sound_name, "sound_unlocked_now")) {
		ast_string_field_set(sounds, unlockednow, sound_file);
	} else if (!strcasecmp(sound_name, "sound_locked_now")) {
		ast_string_field_set(sounds, lockednow, sound_file);
	} else if (!strcasecmp(sound_name, "sound_error_menu")) {
		ast_string_field_set(sounds, errormenu, sound_file);
	} else if (!strcasecmp(sound_name, "sound_join")) {
		ast_string_field_set(sounds, join, sound_file);
	} else if (!strcasecmp(sound_name, "sound_leave")) {
		ast_string_field_set(sounds, leave, sound_file);
	} else {
		return -1;
	}

	return 0;
}
static int set_bridge_option(const char *name, const char *value, struct bridge_profile *b_profile)
{
	if (!strcasecmp(name, "internal_sample_rate")) {
		if (!strcasecmp(value, "auto")) {
			b_profile->internal_sample_rate = 0;
		} else if (sscanf(value, "%30u", &b_profile->internal_sample_rate) != 1) {
			return -1;
		}
	} else if (!strcasecmp(name, "mixing_interval")) {
		if (sscanf(value, "%30u", &b_profile->mix_interval) != 1) {
			return -1;
		}
		switch (b_profile->mix_interval) {
		case 10:
		case 20:
		case 40:
		case 80:
			break;
		default:
			ast_log(LOG_WARNING, "invalid mixing interval %u\n", b_profile->mix_interval);
			b_profile->mix_interval = 0;
			return -1;
		}
	} else if (!strcasecmp(name, "record_conference")) {
		ast_set2_flag(b_profile, ast_true(value), BRIDGE_OPT_RECORD_CONFERENCE);
	} else if (!strcasecmp(name, "max_members")) {
		if (sscanf(value, "%30u", &b_profile->max_members) != 1) {
			return -1;
		}
	} else if (!strcasecmp(name, "record_file")) {
		ast_copy_string(b_profile->rec_file, value, sizeof(b_profile->rec_file));
	} else if (strlen(name) >= 5 && !strncasecmp(name, "sound", 5)) {
		if (set_sound(name, value, b_profile->sounds)) {
			return -1;
		}
	} else if (!strcasecmp(name, "template")) { /* Only documented for use in CONFBRIDGE dialplan function */
		struct bridge_profile *tmp = b_profile;
		struct bridge_profile_sounds *sounds = bridge_profile_sounds_alloc();
		struct bridge_profile_sounds *oldsounds = b_profile->sounds;
		if (!sounds) {
			return -1;
		}
		if (!(conf_find_bridge_profile(NULL, value, tmp))) {
			ao2_ref(sounds, -1);
			return -1;
		}
		/* Using a bridge profile as a template is a little complicated due to the sounds. Since the sounds
		 * structure of a dynamic profile will need to be altered, a completely new sounds structure must be
		 * create instead of simply holding a reference to the one built by the config file. */
		ast_string_field_set(sounds, onlyperson, tmp->sounds->onlyperson);
		ast_string_field_set(sounds, hasjoin, tmp->sounds->hasjoin);
		ast_string_field_set(sounds, hasleft, tmp->sounds->hasleft);
		ast_string_field_set(sounds, kicked, tmp->sounds->kicked);
		ast_string_field_set(sounds, muted, tmp->sounds->muted);
		ast_string_field_set(sounds, unmuted, tmp->sounds->unmuted);
		ast_string_field_set(sounds, thereare, tmp->sounds->thereare);
		ast_string_field_set(sounds, otherinparty, tmp->sounds->otherinparty);
		ast_string_field_set(sounds, placeintoconf, tmp->sounds->placeintoconf);
		ast_string_field_set(sounds, waitforleader, tmp->sounds->waitforleader);
		ast_string_field_set(sounds, getpin, tmp->sounds->getpin);
		ast_string_field_set(sounds, invalidpin, tmp->sounds->invalidpin);
		ast_string_field_set(sounds, locked, tmp->sounds->locked);
		ast_string_field_set(sounds, unlockednow, tmp->sounds->unlockednow);
		ast_string_field_set(sounds, lockednow, tmp->sounds->lockednow);
		ast_string_field_set(sounds, errormenu, tmp->sounds->errormenu);

		ao2_ref(tmp->sounds, -1); /* sounds struct copied over to it from the template by reference only. */
		ao2_ref(oldsounds,-1);    /* original sounds struct we don't need anymore */
		tmp->sounds = sounds;     /* the new sounds struct that is a deep copy of the one from the template. */
	} else {
		return -1;
	}

	return 0;
}

/*! CONFBRIDGE dialplan function functions and channel datastore. */
struct func_confbridge_data {
	struct bridge_profile b_profile;
	struct user_profile u_profile;
	unsigned int b_usable:1; /*!< Tells if bridge profile is usable or not */
	unsigned int u_usable:1; /*!< Tells if user profile is usable or not */
};
static void func_confbridge_destroy_cb(void *data)
{
	struct func_confbridge_data *b_data = data;
	conf_bridge_profile_destroy(&b_data->b_profile);
	ast_free(b_data);
};
static const struct ast_datastore_info confbridge_datastore = {
	.type = "confbridge",
	.destroy = func_confbridge_destroy_cb
};
int func_confbridge_helper(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *datastore = NULL;
	struct func_confbridge_data *b_data = NULL;
	char *parse = NULL;
	int new = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(type);
		AST_APP_ARG(option);
	);

	/* parse all the required arguments and make sure they exist. */
	if (ast_strlen_zero(data) || ast_strlen_zero(value)) {
		return -1;
	}
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (ast_strlen_zero(args.type) || ast_strlen_zero(args.option)) {
		return -1;
	}

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &confbridge_datastore, NULL))) {
		ast_channel_unlock(chan);

		if (!(datastore = ast_datastore_alloc(&confbridge_datastore, NULL))) {
			return 0;
		}
		if (!(b_data = ast_calloc(1, sizeof(*b_data)))) {
			ast_datastore_free(datastore);
			return 0;
		}
		if (!(b_data->b_profile.sounds = bridge_profile_sounds_alloc())) {
			ast_datastore_free(datastore);
			ast_free(b_data);
			return 0;
		}
		datastore->data = b_data;
		new = 1;
	} else {
		ast_channel_unlock(chan);
		b_data = datastore->data;
	}

	/* SET(CONFBRIDGE(type,option)=value) */
	if (!strcasecmp(args.type, "bridge") && !set_bridge_option(args.option, value, &b_data->b_profile)) {
		b_data->b_usable = 1;
	} else if (!strcasecmp(args.type, "user") && !set_user_option(args.option, value, &b_data->u_profile)) {
		b_data->u_usable = 1;
	} else {
		ast_log(LOG_WARNING, "Profile type \"%s\" can not be set in CONFBRIDGE function with option \"%s\" and value \"%s\"\n",
			args.type, args.option, value);
		goto cleanup_error;
	}
	if (new) {
		ast_channel_lock(chan);
		ast_channel_datastore_add(chan, datastore);
		ast_channel_unlock(chan);
	}
	return 0;

cleanup_error:
	ast_log(LOG_ERROR, "Invalid argument provided to the %s function\n", cmd);
	if (new) {
		ast_datastore_free(datastore);
	}
	return -1;
}

/*!
 * \brief Parse the bridge profile options
 */
static int parse_bridge(const char *cat, struct ast_config *cfg)
{
	struct ast_variable *var;
	struct bridge_profile tmp;
	struct bridge_profile *b_profile;

	ast_copy_string(tmp.name, cat, sizeof(tmp.name));
	if ((b_profile = ao2_find(bridge_profiles, &tmp, OBJ_POINTER))) {
		b_profile->delme = 0;
	} else if ((b_profile = ao2_alloc(sizeof(*b_profile), NULL))) {
		ast_copy_string(b_profile->name, cat, sizeof(b_profile->name));
		ao2_link(bridge_profiles, b_profile);
	} else {
		return -1;
	}

	ao2_lock(b_profile);
	/* set defaults */
	b_profile->internal_sample_rate = 0;
	b_profile->flags = 0;
	b_profile->max_members = 0;
	b_profile->mix_interval = 0;
	memset(b_profile->rec_file, 0, sizeof(b_profile->rec_file));
	if (b_profile->sounds) {
		ao2_ref(b_profile->sounds, -1); /* sounds is read only.  Once it has been created
		                                 * it can never be altered. This prevents having to
		                                 * do any locking after it is built from the config. */
		b_profile->sounds = NULL;
	}

	if (!(b_profile->sounds = bridge_profile_sounds_alloc())) {
		ao2_unlock(b_profile);
		ao2_ref(b_profile, -1);
		ao2_unlink(bridge_profiles, b_profile);
		return -1;
	}

	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "type")) {
			continue;
		} else if (set_bridge_option(var->name, var->value, b_profile)) {
			ast_log(LOG_WARNING, "Invalid: '%s' at line %d of %s is not supported.\n",
				var->name, var->lineno, CONFBRIDGE_CONFIG);
		}
	}
	ao2_unlock(b_profile);

	ao2_ref(b_profile, -1);
	return 0;
}

static int parse_user(const char *cat, struct ast_config *cfg)
{
	struct ast_variable *var;
	struct user_profile tmp;
	struct user_profile *u_profile;

	ast_copy_string(tmp.name, cat, sizeof(tmp.name));
	if ((u_profile = ao2_find(user_profiles, &tmp, OBJ_POINTER))) {
		u_profile->delme = 0;
	} else if ((u_profile = ao2_alloc(sizeof(*u_profile), NULL))) {
		ast_copy_string(u_profile->name, cat, sizeof(u_profile->name));
		ao2_link(user_profiles, u_profile);
	} else {
		return -1;
	}

	ao2_lock(u_profile);
	/* set defaults */
	u_profile->flags = 0;
	u_profile->announce_user_count_all_after = 0;
	u_profile->silence_threshold = DEFAULT_SILENCE_THRESHOLD;
	u_profile->talking_threshold = DEFAULT_TALKING_THRESHOLD;
	memset(u_profile->pin, 0, sizeof(u_profile->pin));
	memset(u_profile->moh_class, 0, sizeof(u_profile->moh_class));
	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "type")) {
			continue;
		} else if (set_user_option(var->name, var->value, u_profile)) {
			ast_log(LOG_WARNING, "Invalid option '%s' at line %d of %s is not supported.\n",
				var->name, var->lineno, CONFBRIDGE_CONFIG);
		}
	}
	ao2_unlock(u_profile);

	ao2_ref(u_profile, -1);
	return 0;
}

static int add_action_to_menu_entry(struct conf_menu_entry *menu_entry, enum conf_menu_action_id id, char *databuf)
{
	struct conf_menu_action *menu_action = ast_calloc(1, sizeof(*menu_action));

	if (!menu_action) {
		return -1;
	}
	menu_action->id = id;

	switch (id) {
	case MENU_ACTION_NOOP:
	case MENU_ACTION_TOGGLE_MUTE:
	case MENU_ACTION_INCREASE_LISTENING:
	case MENU_ACTION_DECREASE_LISTENING:
	case MENU_ACTION_INCREASE_TALKING:
	case MENU_ACTION_DECREASE_TALKING:
	case MENU_ACTION_RESET_LISTENING:
	case MENU_ACTION_RESET_TALKING:
	case MENU_ACTION_ADMIN_TOGGLE_LOCK:
	case MENU_ACTION_ADMIN_KICK_LAST:
	case MENU_ACTION_LEAVE:
		break;
	case MENU_ACTION_PLAYBACK:
	case MENU_ACTION_PLAYBACK_AND_CONTINUE:
		if (!(ast_strlen_zero(databuf))) {
			ast_copy_string(menu_action->data.playback_file, databuf, sizeof(menu_action->data.playback_file));
		} else {
			ast_free(menu_action);
			return -1;
		}
		break;
	case MENU_ACTION_DIALPLAN_EXEC:
		if (!(ast_strlen_zero(databuf))) {
			AST_DECLARE_APP_ARGS(args,
				AST_APP_ARG(context);
				AST_APP_ARG(exten);
				AST_APP_ARG(priority);
			);
			AST_STANDARD_APP_ARGS(args, databuf);
			if (!ast_strlen_zero(args.context)) {
				ast_copy_string(menu_action->data.dialplan_args.context,
					args.context,
					sizeof(menu_action->data.dialplan_args.context));
			}
			if (!ast_strlen_zero(args.exten)) {
				ast_copy_string(menu_action->data.dialplan_args.exten,
					args.exten,
					sizeof(menu_action->data.dialplan_args.exten));
			}
			menu_action->data.dialplan_args.priority = 1; /* 1 by default */
			if (!ast_strlen_zero(args.priority) &&
				(sscanf(args.priority, "%30u", &menu_action->data.dialplan_args.priority) != 1)) {
				/* invalid priority */
				ast_free(menu_action);
				return -1;
			}
		} else {
			ast_free(menu_action);
			return -1;
		}
	};

	AST_LIST_INSERT_TAIL(&menu_entry->actions, menu_action, action);

	return 0;
}

static int add_menu_entry(struct conf_menu *menu, const char *dtmf, const char *action_names)
{
	struct conf_menu_entry *menu_entry = NULL, *cur = NULL;
	int res = 0;
	char *tmp_action_names = ast_strdupa(action_names);
	char *action = NULL;
	char *action_args;
	char *tmp;
	char buf[PATH_MAX];
	char *delimiter = ",";

	if (!(menu_entry = ast_calloc(1, sizeof(*menu_entry)))) {
		return -1;
	}

	for (;;) {
		char *comma;
		char *startbrace;
		char *endbrace;
		unsigned int action_len;

		if (ast_strlen_zero(tmp_action_names)) {
			break;
		}
		startbrace = strchr(tmp_action_names, '(');
		endbrace = strchr(tmp_action_names, ')');
		comma = strchr(tmp_action_names, ',');

		/* If the next action has brackets with comma delimited arguments in it,
		 * make the delimeter ')' instead of a comma to preserve the argments */
		if (startbrace && endbrace && comma && (comma > startbrace && comma < endbrace)) {
			delimiter = ")";
		} else {
			delimiter = ",";
		}

		if (!(action = strsep(&tmp_action_names, delimiter))) {
			break;
		}

		action = ast_strip(action);
		if (ast_strlen_zero(action)) {
			continue;
		}

		action_len = strlen(action);
		ast_copy_string(menu_entry->dtmf, dtmf, sizeof(menu_entry->dtmf));
		if (!strcasecmp(action, "toggle_mute")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_TOGGLE_MUTE, NULL);
		} else if (!strcasecmp(action, "no_op")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_NOOP, NULL);
		} else if (!strcasecmp(action, "increase_listening_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_INCREASE_LISTENING, NULL);
		} else if (!strcasecmp(action, "decrease_listening_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_DECREASE_LISTENING, NULL);
		} else if (!strcasecmp(action, "increase_talking_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_INCREASE_TALKING, NULL);
		} else if (!strcasecmp(action, "reset_listening_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_RESET_LISTENING, NULL);
		} else if (!strcasecmp(action, "reset_talking_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_RESET_TALKING, NULL);
		} else if (!strcasecmp(action, "decrease_talking_volume")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_DECREASE_TALKING, NULL);
		} else if (!strcasecmp(action, "admin_toggle_conference_lock")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_ADMIN_TOGGLE_LOCK, NULL);
		} else if (!strcasecmp(action, "admin_kick_last")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_ADMIN_KICK_LAST, NULL);
		} else if (!strcasecmp(action, "leave_conference")) {
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_LEAVE, NULL);
		} else if (!strncasecmp(action, "dialplan_exec(", 14)) {
			ast_copy_string(buf, action, sizeof(buf));
			action_args = buf;
			if ((action_args = strchr(action, '('))) {
				action_args++;
			}
			/* it is possible that this argument may or may not
			 * have a closing brace at this point, it all depends on if
			 * comma delimited arguments were provided */
			if ((tmp = strchr(action, ')'))) {
				*tmp = '\0';
			}
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_DIALPLAN_EXEC, action_args);
		} else if (action_len >= 21 && !strncasecmp(action, "playback_and_continue(", 22)) {
			ast_copy_string(buf, action, sizeof(buf));
			action_args = buf;
			if ((action_args = strchr(action, '(')) && (tmp = strrchr(action_args, ')'))) {
				*tmp = '\0';
				action_args++;
			}
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_PLAYBACK_AND_CONTINUE, action_args);
		} else if (action_len >= 8 && !strncasecmp(action, "playback(", 9)) {
			ast_copy_string(buf, action, sizeof(buf));
			action_args = buf;
			if ((action_args = strchr(action, '(')) && (tmp = strrchr(action_args, ')'))) {
				*tmp = '\0';
				action_args++;
			}
			res |= add_action_to_menu_entry(menu_entry, MENU_ACTION_PLAYBACK, action_args);
		}
	}

	/* if adding any of the actions failed, bail */
	if (res) {
		struct conf_menu_action *action;
		while ((action = AST_LIST_REMOVE_HEAD(&menu_entry->actions, action))) {
			ast_free(action);
		}
		ast_free(menu_entry);
		return -1;
	}

	/* remove any list entry with an identical DTMF sequence for overrides */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&menu->entries, cur, entry) {
		if (!strcasecmp(cur->dtmf, menu_entry->dtmf)) {
			AST_LIST_REMOVE_CURRENT(entry);
			ast_free(cur);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	AST_LIST_INSERT_TAIL(&menu->entries, menu_entry, entry);

	return 0;
}
static int parse_menu(const char *cat, struct ast_config *cfg)
{
	struct ast_variable *var;
	struct conf_menu tmp;
	struct conf_menu *menu;

	ast_copy_string(tmp.name, cat, sizeof(tmp.name));
	if ((menu = ao2_find(menus, &tmp, OBJ_POINTER))) {
		menu->delme = 0;
	} else if ((menu = ao2_alloc(sizeof(*menu), menu_destructor))) {
		ast_copy_string(menu->name, cat, sizeof(menu->name));
		ao2_link(menus, menu);
	} else {
		return -1;
	}

	ao2_lock(menu);
	/* this isn't freeing the menu, just destroying the menu list so it can be rebuilt.*/
	menu_destructor(menu);
	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "type")) {
			continue;
		} else if (add_menu_entry(menu, var->name, var->value)) {
			ast_log(LOG_WARNING, "Unknown option '%s' at line %d of %s is not supported.\n",
				var->name, var->lineno, CONFBRIDGE_CONFIG);
		}
	}
	ao2_unlock(menu);

	ao2_ref(menu, -1);
	return 0;
}

static char *complete_user_profile_name(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	char *res = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator i;
	struct user_profile *u_profile = NULL;

	i = ao2_iterator_init(user_profiles, 0);
	while ((u_profile = ao2_iterator_next(&i))) {
		if (!strncasecmp(u_profile->name, word, wordlen) && ++which > state) {
			res = ast_strdup(u_profile->name);
			ao2_ref(u_profile, -1);
			break;
		}
		ao2_ref(u_profile, -1);
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *handle_cli_confbridge_show_user_profiles(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator it;
	struct user_profile *u_profile;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show profile users";
		e->usage =
			"Usage confbridge show profile users\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd,"--------- User Profiles -----------\n");
	ao2_lock(user_profiles);
	it = ao2_iterator_init(user_profiles, 0);
	while ((u_profile = ao2_iterator_next(&it))) {
		ast_cli(a->fd,"%s\n", u_profile->name);
		ao2_ref(u_profile, -1);
	}
	ao2_iterator_destroy(&it);
	ao2_unlock(user_profiles);

	return CLI_SUCCESS;
}
static char *handle_cli_confbridge_show_user_profile(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct user_profile u_profile;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show profile user";
		e->usage =
			"Usage confbridge show profile user [<profile name>]\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return complete_user_profile_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (!(conf_find_user_profile(NULL, a->argv[4], &u_profile))) {
		ast_cli(a->fd, "No conference user profile named '%s' found!\n", a->argv[4]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd,"--------------------------------------------\n");
	ast_cli(a->fd,"Name:                    %s\n",
		u_profile.name);
	ast_cli(a->fd,"Admin:                   %s\n",
		u_profile.flags & USER_OPT_ADMIN ?
		"true" : "false");
	ast_cli(a->fd,"Marked User:             %s\n",
		u_profile.flags & USER_OPT_MARKEDUSER ?
		"true" : "false");
	ast_cli(a->fd,"Start Muted:             %s\n",
		u_profile.flags & USER_OPT_STARTMUTED?
		"true" : "false");
	ast_cli(a->fd,"MOH When Empty:          %s\n",
		u_profile.flags & USER_OPT_MUSICONHOLD ?
		"enabled" : "disabled");
	ast_cli(a->fd,"MOH Class:               %s\n",
		ast_strlen_zero(u_profile.moh_class) ?
		"default" : u_profile.moh_class);
	ast_cli(a->fd,"Quiet:                   %s\n",
		u_profile.flags & USER_OPT_QUIET ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Wait Marked:             %s\n",
		u_profile.flags & USER_OPT_WAITMARKED ?
		"enabled" : "disabled");
	ast_cli(a->fd,"END Marked:              %s\n",
		u_profile.flags & USER_OPT_ENDMARKED ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Drop_silence:            %s\n",
		u_profile.flags & USER_OPT_DROP_SILENCE ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Silence Threshold:       %dms\n",
		u_profile.silence_threshold);
	ast_cli(a->fd,"Talking Threshold:       %dms\n",
		u_profile.talking_threshold);
	ast_cli(a->fd,"Denoise:                 %s\n",
		u_profile.flags & USER_OPT_DENOISE ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Jitterbuffer:            %s\n",
		u_profile.flags & USER_OPT_JITTERBUFFER ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Talk Detect Events:      %s\n",
		u_profile.flags & USER_OPT_TALKER_DETECT ?
		"enabled" : "disabled");
	ast_cli(a->fd,"DTMF Pass Through:       %s\n",
		u_profile.flags & USER_OPT_DTMF_PASS ?
		"enabled" : "disabled");
	ast_cli(a->fd,"PIN:                     %s\n",
		ast_strlen_zero(u_profile.pin) ?
		"None" : u_profile.pin);
	ast_cli(a->fd,"Announce User Count:     %s\n",
		u_profile.flags & USER_OPT_ANNOUNCEUSERCOUNT ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Announce join/leave:     %s\n",
		u_profile.flags & USER_OPT_ANNOUNCE_JOIN_LEAVE ?
		"enabled" : "disabled");
	ast_cli(a->fd,"Announce User Count all: %s\n",
		u_profile.flags & USER_OPT_ANNOUNCEUSERCOUNTALL ?
		"enabled" : "disabled");
		ast_cli(a->fd,"\n");

	return CLI_SUCCESS;
}

static char *complete_bridge_profile_name(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	char *res = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator i;
	struct bridge_profile *b_profile = NULL;

	i = ao2_iterator_init(bridge_profiles, 0);
	while ((b_profile = ao2_iterator_next(&i))) {
		if (!strncasecmp(b_profile->name, word, wordlen) && ++which > state) {
			res = ast_strdup(b_profile->name);
			ao2_ref(b_profile, -1);
			break;
		}
		ao2_ref(b_profile, -1);
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *handle_cli_confbridge_show_bridge_profiles(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator it;
	struct bridge_profile *b_profile;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show profile bridges";
		e->usage =
			"Usage confbridge show profile bridges\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd,"--------- Bridge Profiles -----------\n");
	ao2_lock(bridge_profiles);
	it = ao2_iterator_init(bridge_profiles, 0);
	while ((b_profile = ao2_iterator_next(&it))) {
		ast_cli(a->fd,"%s\n", b_profile->name);
		ao2_ref(b_profile, -1);
	}
	ao2_iterator_destroy(&it);
	ao2_unlock(bridge_profiles);

	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_show_bridge_profile(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct bridge_profile b_profile;
	char tmp[64];

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show profile bridge";
		e->usage =
			"Usage confbridge show profile bridge <profile name>\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 4) {
			return complete_bridge_profile_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (!(conf_find_bridge_profile(NULL, a->argv[4], &b_profile))) {
		ast_cli(a->fd, "No conference bridge profile named '%s' found!\n", a->argv[4]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd,"--------------------------------------------\n");
	ast_cli(a->fd,"Name:                 %s\n", b_profile.name);

	if (b_profile.internal_sample_rate) {
		snprintf(tmp, sizeof(tmp), "%d", b_profile.internal_sample_rate);
	} else {
		ast_copy_string(tmp, "auto", sizeof(tmp));
	}
	ast_cli(a->fd,"Internal Sample Rate: %s\n", tmp);

	if (b_profile.mix_interval) {
		ast_cli(a->fd,"Mixing Interval:      %d\n", b_profile.mix_interval);
	} else {
		ast_cli(a->fd,"Mixing Interval:      Default 20ms\n");
	}

	ast_cli(a->fd,"Record Conference:    %s\n",
		b_profile.flags & BRIDGE_OPT_RECORD_CONFERENCE ?
		"yes" : "no");

	ast_cli(a->fd,"Record File:          %s\n",
		ast_strlen_zero(b_profile.rec_file) ? "Auto Generated" :
		b_profile.rec_file);

	if (b_profile.max_members) {
		ast_cli(a->fd,"Max Members:          %d\n", b_profile.max_members);
	} else {
		ast_cli(a->fd,"Max Members:          No Limit\n");
	}

	ast_cli(a->fd,"sound_join:           %s\n", conf_get_sound(CONF_SOUND_JOIN, b_profile.sounds));
	ast_cli(a->fd,"sound_leave:          %s\n", conf_get_sound(CONF_SOUND_LEAVE, b_profile.sounds));
	ast_cli(a->fd,"sound_only_person:    %s\n", conf_get_sound(CONF_SOUND_ONLY_PERSON, b_profile.sounds));
	ast_cli(a->fd,"sound_has_joined:     %s\n", conf_get_sound(CONF_SOUND_HAS_JOINED, b_profile.sounds));
	ast_cli(a->fd,"sound_has_left:       %s\n", conf_get_sound(CONF_SOUND_HAS_LEFT, b_profile.sounds));
	ast_cli(a->fd,"sound_kicked:         %s\n", conf_get_sound(CONF_SOUND_KICKED, b_profile.sounds));
	ast_cli(a->fd,"sound_muted:          %s\n", conf_get_sound(CONF_SOUND_MUTED, b_profile.sounds));
	ast_cli(a->fd,"sound_unmuted:        %s\n", conf_get_sound(CONF_SOUND_UNMUTED, b_profile.sounds));
	ast_cli(a->fd,"sound_there_are:      %s\n", conf_get_sound(CONF_SOUND_THERE_ARE, b_profile.sounds));
	ast_cli(a->fd,"sound_other_in_party: %s\n", conf_get_sound(CONF_SOUND_OTHER_IN_PARTY, b_profile.sounds));
	ast_cli(a->fd,"sound_place_into_conference: %s\n", conf_get_sound(CONF_SOUND_PLACE_IN_CONF, b_profile.sounds));
	ast_cli(a->fd,"sound_wait_for_leader:       %s\n", conf_get_sound(CONF_SOUND_WAIT_FOR_LEADER, b_profile.sounds));
	ast_cli(a->fd,"sound_get_pin:        %s\n", conf_get_sound(CONF_SOUND_GET_PIN, b_profile.sounds));
	ast_cli(a->fd,"sound_invalid_pin:    %s\n", conf_get_sound(CONF_SOUND_INVALID_PIN, b_profile.sounds));
	ast_cli(a->fd,"sound_locked:         %s\n", conf_get_sound(CONF_SOUND_LOCKED, b_profile.sounds));
	ast_cli(a->fd,"sound_unlocked_now:   %s\n", conf_get_sound(CONF_SOUND_UNLOCKED_NOW, b_profile.sounds));
	ast_cli(a->fd,"sound_lockednow:      %s\n", conf_get_sound(CONF_SOUND_LOCKED_NOW, b_profile.sounds));
	ast_cli(a->fd,"sound_error_menu:     %s\n", conf_get_sound(CONF_SOUND_ERROR_MENU, b_profile.sounds));
	ast_cli(a->fd,"\n");

	conf_bridge_profile_destroy(&b_profile);
	return CLI_SUCCESS;
}

static char *complete_menu_name(const char *line, const char *word, int pos, int state)
{
	int which = 0;
	char *res = NULL;
	int wordlen = strlen(word);
	struct ao2_iterator i;
	struct conf_menu *menu = NULL;

	i = ao2_iterator_init(menus, 0);
	while ((menu = ao2_iterator_next(&i))) {
		if (!strncasecmp(menu->name, word, wordlen) && ++which > state) {
			res = ast_strdup(menu->name);
			ao2_ref(menu, -1);
			break;
		}
		ao2_ref(menu, -1);
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *handle_cli_confbridge_show_menus(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator it;
	struct conf_menu *menu;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show menus";
		e->usage =
			"Usage confbridge show profile menus\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd,"--------- Menus -----------\n");
	ao2_lock(menus);
	it = ao2_iterator_init(menus, 0);
	while ((menu = ao2_iterator_next(&it))) {
		ast_cli(a->fd,"%s\n", menu->name);
		ao2_ref(menu, -1);
	}
	ao2_iterator_destroy(&it);
	ao2_unlock(menus);

	return CLI_SUCCESS;
}

static char *handle_cli_confbridge_show_menu(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct conf_menu tmp;
	struct conf_menu *menu;
	struct conf_menu_entry *menu_entry = NULL;
	struct conf_menu_action *menu_action = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "confbridge show menu";
		e->usage =
			"Usage confbridge show menu [<menu name>]\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return complete_menu_name(a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	ast_copy_string(tmp.name, a->argv[3], sizeof(tmp.name));
	if (!(menu = ao2_find(menus, &tmp, OBJ_POINTER))) {
		ast_cli(a->fd, "No conference menu named '%s' found!\n", a->argv[3]);
		return CLI_SUCCESS;
	}
	ao2_lock(menu);

	ast_cli(a->fd,"Name: %s\n", menu->name);
	AST_LIST_TRAVERSE(&menu->entries, menu_entry, entry) {
		int action_num = 0;
		ast_cli(a->fd, "%s=", menu_entry->dtmf);
		AST_LIST_TRAVERSE(&menu_entry->actions, menu_action, action) {
			if (action_num) {
				ast_cli(a->fd, ", ");
			}
			switch (menu_action->id) {
			case MENU_ACTION_TOGGLE_MUTE:
				ast_cli(a->fd, "toggle_mute");
				break;
			case MENU_ACTION_NOOP:
				ast_cli(a->fd, "no_op");
				break;
			case MENU_ACTION_INCREASE_LISTENING:
				ast_cli(a->fd, "increase_listening_volume");
				break;
			case MENU_ACTION_DECREASE_LISTENING:
				ast_cli(a->fd, "decrease_listening_volume");
				break;
			case MENU_ACTION_RESET_LISTENING:
				ast_cli(a->fd, "reset_listening_volume");
				break;
			case MENU_ACTION_RESET_TALKING:
				ast_cli(a->fd, "reset_talking_volume");
				break;
			case MENU_ACTION_INCREASE_TALKING:
				ast_cli(a->fd, "increase_talking_volume");
				break;
			case MENU_ACTION_DECREASE_TALKING:
				ast_cli(a->fd, "decrease_talking_volume");
				break;
			case MENU_ACTION_PLAYBACK:
				ast_cli(a->fd, "playback(%s)", menu_action->data.playback_file);
				break;
			case MENU_ACTION_PLAYBACK_AND_CONTINUE:
				ast_cli(a->fd, "playback_and_continue(%s)", menu_action->data.playback_file);
				break;
			case MENU_ACTION_DIALPLAN_EXEC:
				ast_cli(a->fd, "dialplan_exec(%s,%s,%d)",
					menu_action->data.dialplan_args.context,
					menu_action->data.dialplan_args.exten,
					menu_action->data.dialplan_args.priority);
				break;
			case MENU_ACTION_ADMIN_TOGGLE_LOCK:
				ast_cli(a->fd, "admin_toggle_conference_lock");
				break;
			case MENU_ACTION_ADMIN_KICK_LAST:
				ast_cli(a->fd, "admin_kick_last");
				break;
			case MENU_ACTION_LEAVE:
				ast_cli(a->fd, "leave_conference");
				break;
			}
			action_num++;
		}
		ast_cli(a->fd,"\n");
	}


	ao2_unlock(menu);
	ao2_ref(menu, -1);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_confbridge_parser[] = {
	AST_CLI_DEFINE(handle_cli_confbridge_show_user_profile, "Show a conference user profile."),
	AST_CLI_DEFINE(handle_cli_confbridge_show_bridge_profile, "Show a conference bridge profile."),
	AST_CLI_DEFINE(handle_cli_confbridge_show_menu, "Show a conference menu"),
	AST_CLI_DEFINE(handle_cli_confbridge_show_user_profiles, "Show a list of conference user profiles."),
	AST_CLI_DEFINE(handle_cli_confbridge_show_bridge_profiles, "Show a list of conference bridge profiles."),
	AST_CLI_DEFINE(handle_cli_confbridge_show_menus, "Show a list of conference menus"),

};

static int conf_parse_init(void)
{
	if (!(user_profiles = ao2_container_alloc(283, user_hash_cb, user_cmp_cb))) {
		conf_destroy_config();
		return -1;
	}

	if (!(bridge_profiles = ao2_container_alloc(283, bridge_hash_cb, bridge_cmp_cb))) {
		conf_destroy_config();
		return -1;
	}

	if (!(menus = ao2_container_alloc(283, menu_hash_cb, menu_cmp_cb))) {
		conf_destroy_config();
		return -1;
	}

	ast_cli_register_multiple(cli_confbridge_parser, ARRAY_LEN(cli_confbridge_parser));

	return 0;
}

void conf_destroy_config()
{
	if (user_profiles) {
		ao2_ref(user_profiles, -1);
		user_profiles = NULL;
	}
	if (bridge_profiles) {
		ao2_ref(bridge_profiles, -1);
		bridge_profiles = NULL;
	}

	if (menus) {
		ao2_ref(menus, -1);
		menus = NULL;
	}
	ast_cli_unregister_multiple(cli_confbridge_parser, sizeof(cli_confbridge_parser) / sizeof(struct ast_cli_entry));
}

static void remove_all_delme(void)
{
	ao2_callback(user_profiles, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, match_user_delme_cb, NULL);
	ao2_callback(bridge_profiles, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, match_bridge_delme_cb, NULL);
	ao2_callback(menus, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, match_menu_delme_cb, NULL);
}

static void mark_all_delme(void)
{
	ao2_callback(user_profiles, OBJ_NODATA | OBJ_MULTIPLE, user_mark_delme_cb, NULL);
	ao2_callback(bridge_profiles, OBJ_NODATA | OBJ_MULTIPLE, bridge_mark_delme_cb, NULL);
	ao2_callback(menus, OBJ_NODATA | OBJ_MULTIPLE, menu_mark_delme_cb, NULL);
}

int conf_load_config(int reload)
{
	struct ast_flags config_flags = { 0, };
	struct ast_config *cfg = ast_config_load(CONFBRIDGE_CONFIG, config_flags);
	const char *type = NULL;
	char *cat = NULL;

	if (!reload) {
		conf_parse_init();
	}

	if (!cfg || cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		return 0;
	}

	mark_all_delme();

	while ((cat = ast_category_browse(cfg, cat))) {
		if (!(type = (ast_variable_retrieve(cfg, cat, "type")))) {
			if (strcasecmp(cat, "general")) {
				ast_log(LOG_WARNING, "Section '%s' lacks type\n", cat);
			}
			continue;
		}
		if (!strcasecmp(type, "bridge")) {
			parse_bridge(cat, cfg);
		} else if (!strcasecmp(type, "user")) {
			parse_user(cat, cfg);
		} else if (!strcasecmp(type, "menu")) {
			parse_menu(cat, cfg);
		} else {
			continue;
		}
	}

	remove_all_delme();

	return 0;
}

static void conf_user_profile_copy(struct user_profile *dst, struct user_profile *src)
{
	memcpy(dst, src, sizeof(*dst));
}

const struct user_profile *conf_find_user_profile(struct ast_channel *chan, const char *user_profile_name, struct user_profile *result)
{
	struct user_profile tmp;
	struct user_profile *tmp2;
	struct ast_datastore *datastore = NULL;
	struct func_confbridge_data *b_data = NULL;
	ast_copy_string(tmp.name, user_profile_name, sizeof(tmp.name));

	if (chan) {
		ast_channel_lock(chan);
		if ((datastore = ast_channel_datastore_find(chan, &confbridge_datastore, NULL))) {
			ast_channel_unlock(chan);
			b_data = datastore->data;
			if (b_data->u_usable) {
				conf_user_profile_copy(result, &b_data->u_profile);
				return result;
			}
		}
		ast_channel_unlock(chan);
	}

	if (ast_strlen_zero(user_profile_name)) {
		user_profile_name = DEFAULT_USER_PROFILE;
	}
	if (!(tmp2 = ao2_find(user_profiles, &tmp, OBJ_POINTER))) {
		return NULL;
	}
	ao2_lock(tmp2);
	conf_user_profile_copy(result, tmp2);
	ao2_unlock(tmp2);
	ao2_ref(tmp2, -1);

	return result;
}

void conf_bridge_profile_copy(struct bridge_profile *dst, struct bridge_profile *src)
{
	memcpy(dst, src, sizeof(*dst));
	if (src->sounds) {
		ao2_ref(src->sounds, +1);
	}
}

void conf_bridge_profile_destroy(struct bridge_profile *b_profile)
{
	if (b_profile->sounds) {
		ao2_ref(b_profile->sounds, -1);
		b_profile->sounds = NULL;
	}
}

const struct bridge_profile *conf_find_bridge_profile(struct ast_channel *chan, const char *bridge_profile_name, struct bridge_profile *result)
{
	struct bridge_profile tmp;
	struct bridge_profile *tmp2;
	struct ast_datastore *datastore = NULL;
	struct func_confbridge_data *b_data = NULL;

	if (chan) {
		ast_channel_lock(chan);
		if ((datastore = ast_channel_datastore_find(chan, &confbridge_datastore, NULL))) {
			ast_channel_unlock(chan);
			b_data = datastore->data;
			if (b_data->b_usable) {
				conf_bridge_profile_copy(result, &b_data->b_profile);
				return result;
			}
		}
		ast_channel_unlock(chan);
	}
	if (ast_strlen_zero(bridge_profile_name)) {
		bridge_profile_name = DEFAULT_BRIDGE_PROFILE;
	}
	ast_copy_string(tmp.name, bridge_profile_name, sizeof(tmp.name));
	if (!(tmp2 = ao2_find(bridge_profiles, &tmp, OBJ_POINTER))) {
		return NULL;
	}
	ao2_lock(tmp2);
	conf_bridge_profile_copy(result, tmp2);
	ao2_unlock(tmp2);
	ao2_ref(tmp2, -1);

	return result;
}

struct dtmf_menu_hook_pvt {
	struct conference_bridge_user *conference_bridge_user;
	struct conf_menu_entry menu_entry;
	struct conf_menu *menu;
};

static void menu_hook_destroy(void *hook_pvt)
{
	struct dtmf_menu_hook_pvt *pvt = hook_pvt;
	struct conf_menu_action *action = NULL;

	ao2_ref(pvt->menu, -1);

	while ((action = AST_LIST_REMOVE_HEAD(&pvt->menu_entry.actions, action))) {
		ast_free(action);
	}
	ast_free(pvt);
}

static int menu_hook_callback(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct dtmf_menu_hook_pvt *pvt = hook_pvt;
	return conf_handle_dtmf(bridge_channel, pvt->conference_bridge_user, &pvt->menu_entry, pvt->menu);
}

static int copy_menu_entry(struct conf_menu_entry *dst, struct conf_menu_entry *src)
{
	struct conf_menu_action *menu_action = NULL;
	struct conf_menu_action *new_menu_action = NULL;

	memcpy(dst, src, sizeof(*dst));
	AST_LIST_HEAD_INIT_NOLOCK(&dst->actions);

	AST_LIST_TRAVERSE(&src->actions, menu_action, action) {
		if (!(new_menu_action = ast_calloc(1, sizeof(*new_menu_action)))) {
			return -1;
		}
		memcpy(new_menu_action, menu_action, sizeof(*new_menu_action));
		AST_LIST_INSERT_HEAD(&dst->actions, new_menu_action, action);
	}
	return 0;
}

void conf_menu_entry_destroy(struct conf_menu_entry *menu_entry)
{
	struct conf_menu_action *menu_action = NULL;
	while ((menu_action = AST_LIST_REMOVE_HEAD(&menu_entry->actions, action))) {
		ast_free(menu_action);
	}
}

int conf_find_menu_entry_by_sequence(const char *dtmf_sequence, struct conf_menu *menu, struct conf_menu_entry *result)
{
	struct conf_menu_entry *menu_entry = NULL;

	ao2_lock(menu);
	AST_LIST_TRAVERSE(&menu->entries, menu_entry, entry) {
		if (!strcasecmp(menu_entry->dtmf, dtmf_sequence)) {
			copy_menu_entry(result, menu_entry);
			ao2_unlock(menu);
			return 1;
		}
	}
	ao2_unlock(menu);

	return 0;
}

int conf_set_menu_to_user(const char *menu_name, struct conference_bridge_user *conference_bridge_user)
{
	struct conf_menu tmp;
	struct conf_menu *menu;
	struct conf_menu_entry *menu_entry = NULL;
	ast_copy_string(tmp.name, menu_name, sizeof(tmp.name));

	if (!(menu = ao2_find(menus, &tmp, OBJ_POINTER))) {
		return -1;
	}
	ao2_lock(menu);
	AST_LIST_TRAVERSE(&menu->entries, menu_entry, entry) {
		struct dtmf_menu_hook_pvt *pvt;
		if (!(pvt = ast_calloc(1, sizeof(*pvt)))) {
			ao2_unlock(menu);
			ao2_ref(menu, -1);
			return -1;
		}
		if (copy_menu_entry(&pvt->menu_entry, menu_entry)) {
			ast_free(pvt);
			ao2_unlock(menu);
			ao2_ref(menu, -1);
			return -1;
		}
		pvt->conference_bridge_user = conference_bridge_user;
		ao2_ref(menu, +1);
		pvt->menu = menu;

		ast_bridge_features_hook(&conference_bridge_user->features, pvt->menu_entry.dtmf, menu_hook_callback, pvt, menu_hook_destroy);
	}

	ao2_unlock(menu);
	ao2_ref(menu, -1);

	return 0;
}
