/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) <Year>, <Your Name Here>
 *
 * <Your Name Here> <<Your Email Here>>
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
 *
 * Please follow coding guidelines
 * https://wiki.asterisk.org/wiki/display/AST/Coding+Guidelines
 */

/*! \file
 *
 * \brief Skeleton application
 *
 * \author\verbatim <Your Name Here> <<Your Email Here>> \endverbatim
 *
 * This is a skeleton for development of an Asterisk application
 * \ingroup applications
 */

/*! \li \ref app_skel.c uses configuration file \ref app_skel.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page app_skel.conf app_skel.conf
 * \verbinclude app_skel.conf.sample
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <math.h> /* log10 */
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/say.h"
#include "asterisk/astobj2.h"
#include "asterisk/acl.h"
#include "asterisk/netsock2.h"
#include "asterisk/strings.h"
#include "asterisk/cli.h"

/*** DOCUMENTATION
	<application name="SkelGuessNumber" language="en_US">
		<synopsis>
			An example number guessing game
		</synopsis>
		<syntax>
			<parameter name="level" required="true"/>
			<parameter name="options">
				<optionlist>
					<option name="c">
						<para>The computer should cheat</para>
					</option>
					<option name="n">
						<para>How many games to play before hanging up</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
		<para>This simple number guessing application is a template to build other applications
		from. It shows you the basic structure to create your own Asterisk applications.</para>
		</description>
	</application>

	<configInfo name="app_skel" language="en_US">
		<configFile name="app_skel.conf">
			<configObject name="globals">
				<synopsis>Options that apply globally to app_skel</synopsis>
				<configOption name="games">
					<synopsis>The number of games a single execution of SkelGuessNumber will play</synopsis>
				</configOption>
				<configOption name="cheat">
					<synopsis>Should the computer cheat?</synopsis>
					<description><para>If enabled, the computer will ignore winning guesses.</para></description>
				</configOption>
			</configObject>
			<configObject name="sounds">
				<synopsis>Prompts for SkelGuessNumber to play</synopsis>
				<configOption name="prompt" default="please-enter-your&amp;number&amp;queue-less-than">
					<synopsis>A prompt directing the user to enter a number less than the max number</synopsis>
				</configOption>
				<configOption name="wrong_guess" default="vm-pls-try-again">
					<synopsis>The sound file to play when a wrong guess is made</synopsis>
				</configOption>
				<configOption name="right_guess" default="auth-thankyou">
					<synopsis>The sound file to play when a correct guess is made</synopsis>
				</configOption>
				<configOption name="too_low">
					<synopsis>The sound file to play when a guess is too low</synopsis>
				</configOption>
				<configOption name="too_high">
					<synopsis>The sound file to play when a guess is too high</synopsis>
				</configOption>
				<configOption name="lose" default="vm-goodbye">
					<synopsis>The sound file to play when a player loses</synopsis>
				</configOption>
			</configObject>
			<configObject name="level">
				<synopsis>Defined levels for the SkelGuessNumber game</synopsis>
				<configOption name="max_number">
					<synopsis>The maximum in the range of numbers to guess (1 is the implied minimum)</synopsis>
				</configOption>
				<configOption name="max_guesses">
					<synopsis>The maximum number of guesses before a game is considered lost</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

static char *app = "SkelGuessNumber";

enum option_flags {
	OPTION_CHEAT    = (1 << 0),
	OPTION_NUMGAMES = (1 << 1),
};

enum option_args {
	OPTION_ARG_NUMGAMES,
	/* This *must* be the last value in this enum! */
	OPTION_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(app_opts,{
	AST_APP_OPTION('c', OPTION_CHEAT),
	AST_APP_OPTION_ARG('n', OPTION_NUMGAMES, OPTION_ARG_NUMGAMES),
});

/*! \brief A structure to hold global configuration-related options */
struct skel_global_config {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(prompt); /*!< The comma-separated list of sounds to prompt to enter a number */
		AST_STRING_FIELD(wrong);  /*!< The comma-separated list of sounds to indicate a wrong guess */
		AST_STRING_FIELD(right);  /*!< The comma-separated list of sounds to indicate a right guess */
		AST_STRING_FIELD(high);   /*!< The comma-separated list of sounds to indicate a high guess */
		AST_STRING_FIELD(low);    /*!< The comma-separated list of sounds to indicate a low guess */
		AST_STRING_FIELD(lose);  /*!< The comma-separated list of sounds to indicate a lost game */
	);
	uint32_t num_games;    /*!< The number of games to play before hanging up */
	unsigned char cheat:1; /*!< Whether the computer can cheat or not */
};

/*! \brief A structure to maintain level state across reloads */
struct skel_level_state {
	uint32_t wins;      /*!< How many wins for this level */
	uint32_t losses;    /*!< How many losses for this level */
	double avg_guesses; /*!< The average number of guesses to win for this level */
};

/*! \brief Object to hold level config information.
 * \note This object should hold a reference to an an object that holds state across reloads.
 * The other fields are just examples of the kind of data that might be stored in an level.
 */
struct skel_level {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);      /*!< The name of the level */
	);
	uint32_t max_num;                /*!< The upper value on th range of numbers to guess */
	uint32_t max_guesses;            /*!< The maximum number of guesses before losing */
	struct skel_level_state *state;  /*!< A pointer to level state that must exist across all reloads */
};

/*! \brief Information about a currently running set of games
 * \note Because we want to be able to show true running information about the games
 * regardless of whether or not a reload has modified what the level looks like, it
 * is important to either copy the information we need from the level to the
 * current_game struct, or as we do here, store a reference to the level as it is for
 * the running game.
 */
struct skel_current_game {
	uint32_t total_games;          /*! The total number of games for this call to to the app */
	uint32_t games_left;           /*! How many games are left to play in this set */
	uint32_t cheat;                /*! Whether or not cheating was enabled for the game */
	struct skel_level *level_info; /*! The level information for the running game */
};

/* Treat the levels as an array--there won't be many and this will maintain the order */
#define LEVEL_BUCKETS 1

/*! \brief A container that holds all config-related information
 * \note This object should contain a pointer to structs for global data and containers for
 * any levels that are configured. Objects of this type will be swapped out on reload. If an
 * level needs to maintain state across reloads, it needs to allocate a refcounted object to
 * hold that state and ensure that a reference is passed to that state when creating a new
 * level for reload. */
struct skel_config {
	struct skel_global_config *global;
	struct ao2_container *levels;
};

/* Config Options API callbacks */

/*! \brief Allocate a skel_config to hold a snapshot of the complete results of parsing a config
 * \internal
 * \returns A void pointer to a newly allocated skel_config
 */
static void *skel_config_alloc(void);

/*! \brief Allocate a skel_level based on a category in a configuration file
 * \param cat The category to base the level on
 * \returns A void pointer to a newly allocated skel_level
 */
static void *skel_level_alloc(const char *cat);

/*! \brief Find a skel level in the specified container
 * \note This function *does not* look for a skel_level in the active container. It is used
 * internally by the Config Options code to check if an level has already been added to the
 * container that will be swapped for the live container on a successul reload.
 *
 * \param tmp_container A non-active container to search for a level
 * \param category The category associated with the level to check for
 * \retval non-NULL The level from the container
 * \retval NULL The level does not exist in the container
 */
static void *skel_level_find(struct ao2_container *tmp_container, const char *category);

/*! \brief An aco_type structure to link the "general" category to the skel_global_config type */
static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "globals",
	.item_offset = offsetof(struct skel_config, global),
	.category_match = ACO_WHITELIST,
	.category = "^general$",
};

struct aco_type *global_options[] = ACO_TYPES(&global_option);

/*! \brief An aco_type structure to link the "sounds" category to the skel_global_config type */
static struct aco_type sound_option = {
	.type = ACO_GLOBAL,
	.name = "sounds",
	.item_offset = offsetof(struct skel_config, global),
	.category_match = ACO_WHITELIST,
	.category = "^sounds$",
};

struct aco_type *sound_options[] = ACO_TYPES(&sound_option);

/*! \brief An aco_type structure to link the everything but the "general" and "sounds" categories to the skel_level type */
static struct aco_type level_option = {
	.type = ACO_ITEM,
	.name = "level",
	.category_match = ACO_BLACKLIST,
	.category = "^(general|sounds)$",
	.item_alloc = skel_level_alloc,
	.item_find = skel_level_find,
	.item_offset = offsetof(struct skel_config, levels),
};

struct aco_type *level_options[] = ACO_TYPES(&level_option);

struct aco_file app_skel_conf = {
	.filename = "app_skel.conf",
	.types = ACO_TYPES(&global_option, &sound_option, &level_option),
};

/*! \brief A global object container that will contain the skel_config that gets swapped out on reloads */
static AO2_GLOBAL_OBJ_STATIC(globals);

/*! \brief The container of active games */
static struct ao2_container *games;

/*! \brief Register information about the configs being processed by this module */
CONFIG_INFO_STANDARD(cfg_info, globals, skel_config_alloc,
	.files = ACO_FILES(&app_skel_conf),
);

static void skel_global_config_destructor(void *obj)
{
	struct skel_global_config *global = obj;
	ast_string_field_free_memory(global);
}

static void skel_game_destructor(void *obj)
{
	struct skel_current_game *game = obj;
	ao2_cleanup(game->level_info);
}

static void skel_state_destructor(void *obj)
{
	return;
}

static struct skel_current_game *skel_game_alloc(struct skel_level *level)
{
	struct skel_current_game *game;
	if (!(game = ao2_alloc(sizeof(struct skel_current_game), skel_game_destructor))) {
		return NULL;
	}
	ao2_ref(level, +1);
	game->level_info = level;
	return game;
}

static void skel_level_destructor(void *obj)
{
	struct skel_level *level = obj;
	ast_string_field_free_memory(level);
	ao2_cleanup(level->state);
}

static int skel_level_hash(const void *obj, const int flags)
{
	const struct skel_level *level = obj;
	const char *name = (flags & OBJ_KEY) ? obj : level->name;
	return ast_str_case_hash(name);
}

static int skel_level_cmp(void *obj, void *arg, int flags)
{
	struct skel_level *one = obj, *two = arg;
	const char *match = (flags & OBJ_KEY) ? arg : two->name;
	return strcasecmp(one->name, match) ? 0 : (CMP_MATCH | CMP_STOP);
}

/*! \brief A custom bitfield handler
 * \internal
 * \note It is not possible to take the address of a bitfield, therefor all
 * bitfields in the config struct will have to use a custom handler
 * \param opt The opaque config option
 * \param var The ast_variable containing the option name and value
 * \param obj The object registerd for this option type
 * \retval 0 Success
 * \retval non-zero Failure
 */
static int custom_bitfield_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct skel_global_config *global = obj;

	if (!strcasecmp(var->name, "cheat")) {
		global->cheat = ast_true(var->value);
	} else {
		return -1;
	}

	return 0;
}

static void play_files_helper(struct ast_channel *chan, const char *prompts)
{
	char *prompt, *rest = ast_strdupa(prompts);

	ast_stopstream(chan);
	while ((prompt = strsep(&rest, "&")) && !ast_stream_and_wait(chan, prompt, "")) {
		ast_stopstream(chan);
	}
}

static int app_exec(struct ast_channel *chan, const char *data)
{
	int win = 0;
	uint32_t guesses;
	RAII_VAR(struct skel_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct skel_level *, level, NULL, ao2_cleanup);
	RAII_VAR(struct skel_current_game *, game, NULL, ao2_cleanup);
	char *parse, *opts[OPTION_ARG_ARRAY_SIZE];
	struct ast_flags flags;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(level);
		AST_APP_ARG(options);
	);

	if (!cfg) {
		ast_log(LOG_ERROR, "Couldn't access configuratino data!\n");
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (level[,options])\n", app);
		return -1;
	}

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc == 2) {
		ast_app_parse_options(app_opts, &flags, opts, args.options);
	}

	if (ast_strlen_zero(args.level)) {
		ast_log(LOG_ERROR, "%s requires a level argument\n", app);
		return -1;
	}

	if (!(level = ao2_find(cfg->levels, args.level, OBJ_KEY))) {
		ast_log(LOG_ERROR, "Unknown level: %s\n", args.level);
		return -1;
	}

	if (!(game = skel_game_alloc(level))) {
		return -1;
	}

	ao2_link(games, game);

	/* Use app-specified values, or the options specified in [general] if they aren't passed to the app */
	if (!ast_test_flag(&flags, OPTION_NUMGAMES) ||
			ast_strlen_zero(opts[OPTION_ARG_NUMGAMES]) ||
			ast_parse_arg(opts[OPTION_ARG_NUMGAMES], PARSE_UINT32, &game->total_games)) {
		game->total_games = cfg->global->num_games;
	}
	game->games_left = game->total_games;
	game->cheat = ast_test_flag(&flags, OPTION_CHEAT) || cfg->global->cheat;

	for (game->games_left = game->total_games; game->games_left; game->games_left--) {
		uint32_t num = ast_random() % level->max_num; /* random number between 0 and level->max_num */

		ast_debug(1, "They should totally should guess %u\n", num);

		/* Play the prompt */
		play_files_helper(chan, cfg->global->prompt);
		ast_say_number(chan, level->max_num, "", ast_channel_language(chan), "");

		for (guesses = 0; guesses < level->max_guesses; guesses++) {
			size_t buflen = log10(level->max_num) + 1;
			char buf[buflen];
			int guess;
			buf[buflen] = '\0';

			/* Read the number pressed */
			ast_readstring(chan, buf, buflen - 1, 2000, 10000, "");
			if (ast_parse_arg(buf, PARSE_INT32 | PARSE_IN_RANGE, &guess, 0, level->max_num)) {
				if (guesses < level->max_guesses - 1) {
					play_files_helper(chan, cfg->global->wrong);
				}
				continue;
			}

			/* Inform whether the guess was right, low, or high */
			if (guess == num && !game->cheat) {
				/* win */
				win = 1;
				play_files_helper(chan, cfg->global->right);
				guesses++;
				break;
			} else if (guess < num) {
				play_files_helper(chan, cfg->global->low);
			} else {
				play_files_helper(chan, cfg->global->high);
			}

			if (guesses < level->max_guesses - 1) {
				play_files_helper(chan, cfg->global->wrong);
			}
		}

		/* Process game stats */
		ao2_lock(level->state);
		if (win) {
			++level->state->wins;
			level->state->avg_guesses = ((level->state->wins - 1) * level->state->avg_guesses + guesses) / level->state->wins;
		} else {
			/* lose */
			level->state->losses++;
			play_files_helper(chan, cfg->global->lose);
		}
		ao2_unlock(level->state);
	}

	ao2_unlink(games, game);

	return 0;
}

static struct skel_level *skel_state_alloc(const char *name)
{
	struct skel_level *level;

	if (!(level = ao2_alloc(sizeof(*level), skel_state_destructor))) {
		return NULL;
	}

	return level;
}

static void *skel_level_find(struct ao2_container *tmp_container, const char *category)
{
	return ao2_find(tmp_container, category, OBJ_KEY);
}

/*! \brief Look up an existing state object, or create a new one
 * \internal
 * \note Since the reload code will create a new level from scratch, it
 * is important for any state that must persist between reloads to be
 * in a separate refcounted object. This function allows the level alloc
 * function to get a ref to an existing state object if it exists,
 * otherwise it will return a reference to a newly allocated state object.
 */
static void *skel_find_or_create_state(const char *category)
{
	RAII_VAR(struct skel_config *, cfg, ao2_global_obj_ref(globals), ao2_cleanup);
	RAII_VAR(struct skel_level *, level, NULL, ao2_cleanup);
	if (!cfg || !cfg->levels || !(level = ao2_find(cfg->levels, category, OBJ_KEY))) {
		return skel_state_alloc(category);
	}
	ao2_ref(level->state, +1);
	return level->state;
}

static void *skel_level_alloc(const char *cat)
{
	struct skel_level *level;

	if (!(level = ao2_alloc(sizeof(*level), skel_level_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(level, 128)) {
		ao2_ref(level, -1);
		return NULL;
	}

	/* Since the level has state information that needs to persist between reloads,
	 * it is important to handle that here in the level's allocation function.
	 * If not separated out into its own object, the data would be destroyed on
	 * reload. */
	if (!(level->state = skel_find_or_create_state(cat))) {
		ao2_ref(level, -1);
		return NULL;
	}

	ast_string_field_set(level, name, cat);

	return level;
}

static void skel_config_destructor(void *obj)
{
	struct skel_config *cfg = obj;
	ao2_cleanup(cfg->global);
	ao2_cleanup(cfg->levels);
}

static void *skel_config_alloc(void)
{
	struct skel_config *cfg;

	if (!(cfg = ao2_alloc(sizeof(*cfg), skel_config_destructor))) {
		return NULL;
	}

	/* Allocate/initialize memory */
	if (!(cfg->global = ao2_alloc(sizeof(*cfg->global), skel_global_config_destructor))) {
		goto error;
	}

	if (ast_string_field_init(cfg->global, 128)) {
		goto error;
	}

	if (!(cfg->levels = ao2_container_alloc(LEVEL_BUCKETS, skel_level_hash, skel_level_cmp))) {
		goto error;
	}

	return cfg;
error:
	ao2_ref(cfg, -1);
	return NULL;
}

static char *handle_skel_show_config(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct skel_config *, cfg, NULL, ao2_cleanup);

	switch(cmd) {
	case CLI_INIT:
		e->command = "skel show config";
		e->usage =
			"Usage: skel show config\n"
			"       List app_skel global config\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!(cfg = ao2_global_obj_ref(globals)) || !cfg->global) {
		return NULL;
	}

	ast_cli(a->fd, "games per call:  %u\n", cfg->global->num_games);
	ast_cli(a->fd, "computer cheats: %s\n", AST_CLI_YESNO(cfg->global->cheat));
	ast_cli(a->fd, "\n");
	ast_cli(a->fd, "Sounds\n");
	ast_cli(a->fd, "  prompt:      %s\n", cfg->global->prompt);
	ast_cli(a->fd, "  wrong guess: %s\n", cfg->global->wrong);
	ast_cli(a->fd, "  right guess: %s\n", cfg->global->right);

	return CLI_SUCCESS;
}

static char *handle_skel_show_games(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator iter;
	struct skel_current_game *game;

	switch(cmd) {
	case CLI_INIT:
		e->command = "skel show games";
		e->usage =
			"Usage: skel show games\n"
			"       List app_skel active games\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

#define SKEL_FORMAT "%-15.15s %-15.15s %-15.15s\n"
#define SKEL_FORMAT1 "%-15.15s %-15u %-15u\n"
	ast_cli(a->fd, SKEL_FORMAT, "Level", "Total Games", "Games Left");
	iter = ao2_iterator_init(games, 0);
	while ((game = ao2_iterator_next(&iter))) {
		ast_cli(a->fd, SKEL_FORMAT1, game->level_info->name, game->total_games, game->games_left);
		ao2_ref(game, -1);
	}
	ao2_iterator_destroy(&iter);
#undef SKEL_FORMAT
#undef SKEL_FORMAT1
	return CLI_SUCCESS;
}

static char *handle_skel_show_levels(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct skel_config *, cfg, NULL, ao2_cleanup);
	struct ao2_iterator iter;
	struct skel_level *level;

	switch(cmd) {
	case CLI_INIT:
		e->command = "skel show levels";
		e->usage =
			"Usage: skel show levels\n"
			"       List the app_skel levels\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (!(cfg = ao2_global_obj_ref(globals)) || !cfg->levels) {
		return NULL;
	}

#define SKEL_FORMAT "%-15.15s %-11.11s %-12.12s %-8.8s %-8.8s %-12.12s\n"
#define SKEL_FORMAT1 "%-15.15s %-11u %-12u %-8u %-8u %-8f\n"
	ast_cli(a->fd, SKEL_FORMAT, "Name", "Max number", "Max Guesses", "Wins", "Losses", "Avg Guesses");
	iter = ao2_iterator_init(cfg->levels, 0);
	while ((level = ao2_iterator_next(&iter))) {
		ast_cli(a->fd, SKEL_FORMAT1, level->name, level->max_num, level->max_guesses, level->state->wins, level->state->losses, level->state->avg_guesses);
		ao2_ref(level, -1);
	}
	ao2_iterator_destroy(&iter);
#undef SKEL_FORMAT
#undef SKEL_FORMAT1

	return CLI_SUCCESS;
}

static struct ast_cli_entry skel_cli[] = {
	AST_CLI_DEFINE(handle_skel_show_config, "Show app_skel global config options"),
	AST_CLI_DEFINE(handle_skel_show_levels, "Show app_skel levels"),
	AST_CLI_DEFINE(handle_skel_show_games, "Show app_skel active games"),
};

static int reload_module(void)
{
	if (aco_process_config(&cfg_info, 1) == ACO_PROCESS_ERROR) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return 0;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(skel_cli, ARRAY_LEN(skel_cli));
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(globals);
	ao2_cleanup(games);
	return ast_unregister_application(app);
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if (aco_info_init(&cfg_info)) {
		goto error;
	}
	if (!(games = ao2_container_alloc(1, NULL, NULL))) {
		goto error;
	}

	/* Global options */
	aco_option_register(&cfg_info, "games", ACO_EXACT, global_options, "3", OPT_UINT_T, 0, FLDSET(struct skel_global_config, num_games));
	aco_option_register_custom(&cfg_info, "cheat", ACO_EXACT, global_options, "no", custom_bitfield_handler, 0);

	/* Sound options */
	aco_option_register(&cfg_info, "prompt", ACO_EXACT, sound_options, "please-enter-your&number&queue-less-than", OPT_STRINGFIELD_T, 0, STRFLDSET(struct skel_global_config, prompt));
	aco_option_register(&cfg_info, "wrong_guess", ACO_EXACT, sound_options, "vm-pls-try-again", OPT_STRINGFIELD_T, 0, STRFLDSET(struct skel_global_config, wrong));
	aco_option_register(&cfg_info, "right_guess", ACO_EXACT, sound_options, "auth-thankyou", OPT_STRINGFIELD_T, 0, STRFLDSET(struct skel_global_config, right));
	aco_option_register(&cfg_info, "too_high", ACO_EXACT, sound_options, "high", OPT_STRINGFIELD_T, 0, STRFLDSET(struct skel_global_config, high));
	aco_option_register(&cfg_info, "too_low", ACO_EXACT, sound_options, "low", OPT_STRINGFIELD_T, 0, STRFLDSET(struct skel_global_config, low));
	aco_option_register(&cfg_info, "lose", ACO_EXACT, sound_options, "vm-goodbye", OPT_STRINGFIELD_T, 0, STRFLDSET(struct skel_global_config, lose));

	/* Level options */
	aco_option_register(&cfg_info, "max_number", ACO_EXACT, level_options, NULL, OPT_UINT_T, 0, FLDSET(struct skel_level, max_num));
	aco_option_register(&cfg_info, "max_guesses", ACO_EXACT, level_options, NULL, OPT_UINT_T, 1, FLDSET(struct skel_level, max_guesses));

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		goto error;
	}

	ast_cli_register_multiple(skel_cli, ARRAY_LEN(skel_cli));
	if (ast_register_application_xml(app, app_exec)) {
		goto error;
	}
	return AST_MODULE_LOAD_SUCCESS;

error:
	aco_info_destroy(&cfg_info);
	ao2_cleanup(games);
	return AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Skeleton (sample) Application",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_DEFAULT,
);
