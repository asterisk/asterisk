/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kinsey Moore <markster@digium.com>
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
 * \brief Sound file format and description index.
 */

#include "asterisk.h"

#include <dirent.h>
#include <sys/stat.h>

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/paths.h"	/* use ast_config_AST_DATA_DIR */
#include "asterisk/media_index.h"
#include "asterisk/sounds_index.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/_private.h"
#include "asterisk/stasis_message_router.h"

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*! \brief The number of buckets to be used for storing language-keyed objects */
#define LANGUAGE_BUCKETS 7

static struct ast_media_index *sounds_index;

static struct stasis_message_router *sounds_system_router;

/*! \brief Get the languages in which sound files are available */
static struct ao2_container *get_languages(void)
{
	RAII_VAR(struct ao2_container *, lang_dirs, NULL, ao2_cleanup);
	struct dirent* dent;
	DIR* srcdir;
	RAII_VAR(struct ast_str *, media_dir, ast_str_create(64), ast_free);
	RAII_VAR(struct ast_str *, variant_dir, ast_str_create(64), ast_free);

	lang_dirs = ast_str_container_alloc(LANGUAGE_BUCKETS);
	if (!media_dir || !lang_dirs) {
		return NULL;
	}

	ast_str_set(&media_dir, 0, "%s/sounds", ast_config_AST_DATA_DIR);

	srcdir = opendir(ast_str_buffer(media_dir));

	if (srcdir == NULL) {
		ast_log(LOG_ERROR, "Failed to open %s\n", ast_str_buffer(media_dir));
		return NULL;
	}

	while((dent = readdir(srcdir)) != NULL) {
		struct stat st;

		if(!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")) {
			continue;
		}

		ast_str_reset(variant_dir);
		ast_str_set(&variant_dir, 0, "%s/%s", ast_str_buffer(media_dir), dent->d_name);

		if (stat(ast_str_buffer(variant_dir), &st) < 0) {
			ast_log(LOG_ERROR, "Failed to stat %s\n", ast_str_buffer(variant_dir));
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			ast_str_container_add(lang_dirs, dent->d_name);
		}
	}

	closedir(srcdir);
	ao2_ref(lang_dirs, +1);
	return lang_dirs;
}

/*! \brief Callback to process an individual language directory or subdirectory */
static int update_index_cb(void *obj, void *arg, int flags)
{
	char *lang = obj;
	struct ast_media_index *index = arg;

	if (ast_media_index_update(index, lang)) {
		return CMP_MATCH;
	}
	return 0;
}

AST_MUTEX_DEFINE_STATIC(reload_lock);

int ast_sounds_reindex(void)
{
	RAII_VAR(struct ast_str *, sounds_dir, NULL, ast_free);
	RAII_VAR(struct ao2_container *, languages, NULL, ao2_cleanup);
	RAII_VAR(char *, failed_index, NULL, ao2_cleanup);
	RAII_VAR(struct ast_media_index *, new_index, NULL, ao2_cleanup);
	struct ast_media_index *old_index;

	SCOPED_MUTEX(lock, &reload_lock);

	old_index = sounds_index;
	languages = get_languages();
	sounds_dir = ast_str_create(64);

	if (!languages || !sounds_dir) {
		return -1;
	}

	ast_str_set(&sounds_dir, 0, "%s/sounds", ast_config_AST_DATA_DIR);
	new_index = ast_media_index_create(ast_str_buffer(sounds_dir));
	if (!new_index) {
		return -1;
	}

	failed_index = ao2_callback(languages, 0, update_index_cb, new_index);
	if (failed_index) {
		return -1;
	}

	ao2_ref(new_index, +1);
	sounds_index = new_index;
	ao2_cleanup(old_index);
	return 0;
}

static int show_sounds_cb(void *obj, void *arg, int flags)
{
	char *name = obj;
	struct ast_cli_args *a = arg;
	ast_cli(a->fd, "%s\n", name);
	return 0;
}

static int show_sound_info_cb(void *obj, void *arg, int flags)
{
	char *language = obj;
	struct ast_cli_args *a = arg;
        struct ast_format format;
	int formats_shown = 0;
	RAII_VAR(struct ast_media_index *, local_index, ast_sounds_get_index(), ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, cap, NULL, ast_format_cap_destroy);
	const char *description = ast_media_get_description(local_index, a->argv[2], language);

	ast_cli(a->fd, "  Language %s:\n", language);
	if (!ast_strlen_zero(description)) {
		ast_cli(a->fd, "    Description: %s\n", description);
	}

	cap = ast_media_get_format_cap(local_index, a->argv[2], language);
        ast_format_cap_iter_start(cap);
        while (!ast_format_cap_iter_next(cap, &format)) {
		ast_cli(a->fd, "    Format: %s\n", ast_getformatname(&format));
		formats_shown = 1;
        }
        ast_format_cap_iter_end(cap);

	if (!formats_shown) {
		ast_cli(a->fd, "    No Formats Available\n");
	}

	return 0;
}

/*! \brief Allow for reloading of sounds via the command line */
static char *handle_cli_sounds_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "sounds reload";
		e->usage =
			"Usage: sounds reload\n"
			"       Reloads the index of sound files and their descriptions.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2) {
		return CLI_SHOWUSAGE;
	}

	if (ast_sounds_reindex()) {
		ast_cli(a->fd, "Sound re-indexing failed.\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Sound files re-indexed.\n");
	return CLI_SUCCESS;
}

/*! \brief Allow for reloading of sounds via the command line */
static char *handle_cli_sounds_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "sounds show";
		e->usage =
			"Usage: sounds show [soundid]\n"
			"       Shows a listing of sound files or information about the specified sound.\n";
		return NULL;
	case CLI_GENERATE:
	{
                int length = strlen(a->word);
                int which = 0;
                struct ao2_iterator it_sounds;
		char *match = NULL;
		char *filename;
		RAII_VAR(struct ao2_container *, sound_files, ast_media_get_media(sounds_index), ao2_cleanup);
		if (!sound_files) {
			return NULL;
		}

		it_sounds = ao2_iterator_init(sound_files, 0);
                while ((filename = ao2_iterator_next(&it_sounds))) {
                        if (!strncasecmp(a->word, filename, length) && ++which > a->n) {
                                match = ast_strdup(filename);
                                ao2_ref(filename, -1);
                                break;
                        }
                        ao2_ref(filename, -1);
                }
                ao2_iterator_destroy(&it_sounds);
                return match;
	}
	}

	if (a->argc == 2) {
		RAII_VAR(struct ao2_container *, sound_files, ast_media_get_media(sounds_index), ao2_cleanup);
		if (!sound_files) {
			return CLI_FAILURE;
		}

		ast_cli(a->fd, "Available audio files:\n");
		ao2_callback(sound_files, OBJ_MULTIPLE | OBJ_NODATA, show_sounds_cb, a);
		return CLI_SUCCESS;
	}

	if (a->argc == 3) {
		RAII_VAR(struct ao2_container *, variants, ast_media_get_variants(sounds_index, a->argv[2]), ao2_cleanup);
		if (!variants || !ao2_container_count(variants)) {
			ast_cli(a->fd, "ERROR: File %s not found in index\n", a->argv[2]);
			return CLI_FAILURE;
		}

		ast_cli(a->fd, "Indexed Information for %s:\n", a->argv[2]);
		ao2_callback(variants, OBJ_MULTIPLE | OBJ_NODATA, show_sound_info_cb, a);
		return CLI_SUCCESS;
	}

	return CLI_SHOWUSAGE;
}

/*! \brief Struct for registering CLI commands */
static struct ast_cli_entry cli_sounds[] = {
	AST_CLI_DEFINE(handle_cli_sounds_show, "Shows available sounds"),
	AST_CLI_DEFINE(handle_cli_sounds_reload, "Reload sounds index"),
};

static void sounds_cleanup(void)
{
	stasis_message_router_unsubscribe_and_join(sounds_system_router);
	sounds_system_router = NULL;
	ast_cli_unregister_multiple(cli_sounds, ARRAY_LEN(cli_sounds));
	ao2_cleanup(sounds_index);
	sounds_index = NULL;
}

static void format_update_cb(void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic, struct stasis_message *message)
{
	ast_sounds_reindex();
}

int ast_sounds_index_init(void)
{
	int res = 0;
	sounds_index = NULL;
	if (ast_sounds_reindex()) {
		return -1;
	}
	res |= ast_cli_register_multiple(cli_sounds, ARRAY_LEN(cli_sounds));

	sounds_system_router = stasis_message_router_create(ast_system_topic());
	if (!sounds_system_router) {
		return -1;
	}

	res |= stasis_message_router_add(
		sounds_system_router,
		ast_format_register_type(),
		format_update_cb,
		NULL);

	res |= stasis_message_router_add(
		sounds_system_router,
		ast_format_unregister_type(),
		format_update_cb,
		NULL);

	if (res) {
		return -1;
	}

	ast_register_atexit(sounds_cleanup);
	return 0;
}

struct ast_media_index *ast_sounds_get_index(void)
{
	ao2_ref(sounds_index, +1);
	return sounds_index;
}
