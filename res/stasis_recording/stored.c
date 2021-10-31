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
 * \brief Stored file operations for Stasis
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/paths.h"
#include "asterisk/stasis_app_recording.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct stasis_app_stored_recording {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);	/*!< Recording's name */
		AST_STRING_FIELD(file);	/*!< Absolute filename, without extension; for use with streamfile */
		AST_STRING_FIELD(file_with_ext);	/*!< Absolute filename, with extension; for use with everything else */
		);

	const char *format;	/*!< Format name (i.e. filename extension) */
};

static void stored_recording_dtor(void *obj)
{
	struct stasis_app_stored_recording *recording = obj;

	ast_string_field_free_memory(recording);
}

const char *stasis_app_stored_recording_get_file(
	struct stasis_app_stored_recording *recording)
{
	if (!recording) {
		return NULL;
	}
	return recording->file;
}

const char *stasis_app_stored_recording_get_filename(
	struct stasis_app_stored_recording *recording)
{
	if (!recording) {
		return NULL;
	}
	return recording->file_with_ext;
}

const char *stasis_app_stored_recording_get_extension(
	struct stasis_app_stored_recording *recording)
{
	if (!recording) {
		return NULL;
	}
	return recording->format;
}

/*!
 * \brief Split a path into directory and file, resolving canonical directory.
 *
 * The path is resolved relative to the recording directory. Both dir and file
 * are allocated strings, which you must ast_free().
 *
 * \param path Path to split.
 * \param[out] dir Output parameter for directory portion.
 * \param[out] fail Output parameter for the file portion.
 * \return 0 on success.
 * \return Non-zero on error.
 */
static int split_path(const char *path, char **dir, char **file)
{
	RAII_VAR(char *, relative_dir, NULL, ast_free);
	RAII_VAR(char *, absolute_dir, NULL, ast_free);
	RAII_VAR(char *, real_dir, NULL, ast_std_free);
	char *last_slash;
	const char *file_portion;

	relative_dir = ast_strdup(path);
	if (!relative_dir) {
		return -1;
	}

	last_slash = strrchr(relative_dir, '/');
	if (last_slash) {
		*last_slash = '\0';
		file_portion = last_slash + 1;
		ast_asprintf(&absolute_dir, "%s/%s",
			ast_config_AST_RECORDING_DIR, relative_dir);
	} else {
		/* There is no directory portion */
		file_portion = path;
		*relative_dir = '\0';
		absolute_dir = ast_strdup(ast_config_AST_RECORDING_DIR);
	}
	if (!absolute_dir) {
		return -1;
	}

	real_dir = realpath(absolute_dir, NULL);
	if (!real_dir) {
		return -1;
	}

	*dir = ast_strdup(real_dir); /* Dupe so we can ast_free() */
	*file = ast_strdup(file_portion);
	return (*dir && *file) ? 0 : -1;
}

struct match_recording_data {
	const char *file;
	char *file_with_ext;
};

static int is_recording(const char *filename)
{
	const char *ext = strrchr(filename, '.');

	if (!ext) {
		/* No file extension; not us */
		return 0;
	}
	++ext;

	if (!ast_get_format_for_file_ext(ext)) {
		ast_debug(5, "Recording %s: unrecognized format %s\n",
			filename, ext);
		/* Keep looking */
		return 0;
	}

	/* Return the index to the .ext */
	return ext - filename - 1;
}

static int handle_find_recording(const char *dir_name, const char *filename, void *obj)
{
	struct match_recording_data *data = obj;
	int num;

	/* If not a recording or the names do not match the keep searching */
	if (!(num = is_recording(filename)) || strncmp(data->file, filename, num)) {
		return 0;
	}

	if (ast_asprintf(&data->file_with_ext, "%s/%s", dir_name, filename) < 0) {
		return -1;
	}

	return 1;
}

/*!
 * \brief Finds a recording in the given directory.
 *
 * This function searches for a file with the given file name, with a registered
 * format that matches its extension.
 *
 * \param dir_name Directory to search (absolute path).
 * \param file File name, without extension.
 * \return Absolute path of the recording file.
 * \return \c NULL if recording is not found.
 */
static char *find_recording(const char *dir_name, const char *file)
{
	struct match_recording_data data = {
		.file = file,
		.file_with_ext = NULL
	};

	ast_file_read_dir(dir_name, handle_find_recording, &data);

	/* Note, string potentially allocated in handle_file_recording */
	return data.file_with_ext;
}

/*!
 * \brief Allocate a recording object.
 */
static struct stasis_app_stored_recording *recording_alloc(void)
{
	RAII_VAR(struct stasis_app_stored_recording *, recording, NULL,
		ao2_cleanup);
	int res;

	recording = ao2_alloc(sizeof(*recording), stored_recording_dtor);
	if (!recording) {
		return NULL;
	}

	res = ast_string_field_init(recording, 255);
	if (res != 0) {
		return NULL;
	}

	ao2_ref(recording, +1);
	return recording;
}

static int recording_sort(const void *obj_left, const void *obj_right, int flags)
{
	const struct stasis_app_stored_recording *object_left = obj_left;
	const struct stasis_app_stored_recording *object_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_POINTER:
		right_key = object_right->name;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(object_left->name, right_key);
		break;
	case OBJ_PARTIAL_KEY:
		/*
		 * We could also use a partial key struct containing a length
		 * so strlen() does not get called for every comparison instead.
		 */
		cmp = strncmp(object_left->name, right_key, strlen(right_key));
		break;
	default:
		/* Sort can only work on something with a full or partial key. */
		ast_assert(0);
		cmp = 0;
		break;
	}
	return cmp;
}

static int handle_scan_file(const char *dir_name, const char *filename, void *obj)
{
	struct ao2_container *recordings = obj;
	struct stasis_app_stored_recording *recording;
	char *dot, *filepath;

	/* Skip if it is not a recording */
	if (!is_recording(filename)) {
		return 0;
	}

	if (ast_asprintf(&filepath, "%s/%s", dir_name, filename) < 0) {
		return -1;
	}

	recording = recording_alloc();
	if (!recording) {
		ast_free(filepath);
		return -1;
	}

	ast_string_field_set(recording, file_with_ext, filepath);
	/* Build file and format from full path */
	ast_string_field_set(recording, file, filepath);

	ast_free(filepath);

	dot = strrchr(recording->file, '.');
	*dot = '\0';
	recording->format = dot + 1;

	/* Removed the recording dir from the file for the name. */
	ast_string_field_set(recording, name,
		recording->file + strlen(ast_config_AST_RECORDING_DIR) + 1);

	/* Add it to the recordings container */
	ao2_link(recordings, recording);
	ao2_ref(recording, -1);

	return 0;
}

struct ao2_container *stasis_app_stored_recording_find_all(void)
{
	struct ao2_container *recordings;
	int res;

	recordings = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, recording_sort, NULL);
	if (!recordings) {
		return NULL;
	}

	res = ast_file_read_dirs(ast_config_AST_RECORDING_DIR,
				 handle_scan_file, recordings, -1);
	if (res) {
		ao2_ref(recordings, -1);
		return NULL;
	}

	return recordings;
}

struct stasis_app_stored_recording *stasis_app_stored_recording_find_by_name(
	const char *name)
{
	RAII_VAR(struct stasis_app_stored_recording *, recording, NULL,
		ao2_cleanup);
	RAII_VAR(char *, dir, NULL, ast_free);
	RAII_VAR(char *, file, NULL, ast_free);
	RAII_VAR(char *, file_with_ext, NULL, ast_free);
	int res;
	struct stat file_stat;
	int prefix_len = strlen(ast_config_AST_RECORDING_DIR);

	errno = 0;

	if (!name) {
		errno = EINVAL;
		return NULL;
	}

	recording = recording_alloc();
	if (!recording) {
		return NULL;
	}

	res = split_path(name, &dir, &file);
	if (res != 0) {
		return NULL;
	}
	ast_string_field_build(recording, file, "%s/%s", dir, file);

	if (!ast_begins_with(dir, ast_config_AST_RECORDING_DIR)) {
		/* It's possible that one or more component of the recording path is
		 * a symbolic link, this would prevent dir from ever matching. */
		char *real_basedir = realpath(ast_config_AST_RECORDING_DIR, NULL);

		if (!real_basedir || !ast_begins_with(dir, real_basedir)) {
			/* Attempt to escape the recording directory */
			ast_log(LOG_WARNING, "Attempt to access invalid recording directory %s\n",
				dir);
			ast_std_free(real_basedir);
			errno = EACCES;

			return NULL;
		}

		prefix_len = strlen(real_basedir);
		ast_std_free(real_basedir);
	}

	/* The actual name of the recording is file with the config dir
	 * prefix removed.
	 */
	ast_string_field_set(recording, name, recording->file + prefix_len + 1);

	file_with_ext = find_recording(dir, file);
	if (!file_with_ext) {
		return NULL;
	}
	ast_string_field_set(recording, file_with_ext, file_with_ext);
	recording->format = strrchr(recording->file_with_ext, '.');
	if (!recording->format) {
		return NULL;
	}
	++(recording->format);

	res = stat(file_with_ext, &file_stat);
	if (res != 0) {
		return NULL;
	}

	if (!S_ISREG(file_stat.st_mode)) {
		/* Let's not play if it's not a regular file */
		errno = EACCES;
		return NULL;
	}

	ao2_ref(recording, +1);
	return recording;
}

int stasis_app_stored_recording_copy(struct stasis_app_stored_recording *src_recording, const char *dst,
	struct stasis_app_stored_recording **dst_recording)
{
	RAII_VAR(char *, full_path, NULL, ast_free);
	char *dst_file = ast_strdupa(dst);
	char *format;
	char *last_slash;
	int res;

	/* Drop the extension if specified, core will do this for us */
	format = strrchr(dst_file, '.');
	if (format) {
		*format = '\0';
	}

	/* See if any intermediary directories need to be made */
	last_slash = strrchr(dst_file, '/');
	if (last_slash) {
		RAII_VAR(char *, tmp_path, NULL, ast_free);

		*last_slash = '\0';
		if (ast_asprintf(&tmp_path, "%s/%s", ast_config_AST_RECORDING_DIR, dst_file) < 0) {
			return -1;
		}
		if (ast_safe_mkdir(ast_config_AST_RECORDING_DIR,
				tmp_path, 0777) != 0) {
			/* errno set by ast_mkdir */
			return -1;
		}
		*last_slash = '/';
		if (ast_asprintf(&full_path, "%s/%s", ast_config_AST_RECORDING_DIR, dst_file) < 0) {
			return -1;
		}
	} else {
		/* There is no directory portion */
		if (ast_asprintf(&full_path, "%s/%s", ast_config_AST_RECORDING_DIR, dst_file) < 0) {
			return -1;
		}
	}

	ast_verb(4, "Copying recording %s to %s (format %s)\n", src_recording->file,
		full_path, src_recording->format);
	res = ast_filecopy(src_recording->file, full_path, src_recording->format);
	if (!res) {
		*dst_recording = stasis_app_stored_recording_find_by_name(dst_file);
	}

	return res;
}

int stasis_app_stored_recording_delete(
	struct stasis_app_stored_recording *recording)
{
	/* Path was validated when the recording object was created */
	return unlink(recording->file_with_ext);
}

struct ast_json *stasis_app_stored_recording_to_json(
	struct stasis_app_stored_recording *recording)
{
	if (!recording) {
		return NULL;
	}

	return ast_json_pack("{ s: s, s: s }",
		"name", recording->name,
		"format", recording->format);
}
