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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/paths.h"
#include "asterisk/stasis_app_recording.h"

#include <dirent.h>
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

#if defined(__AST_DEBUG_MALLOC)
	*dir = ast_strdup(real_dir); /* Dupe so we can ast_free() */
#else
	/*
	 * ast_std_free() and ast_free() are the same thing at this time
	 * so we don't need to dupe.
	 */
	*dir = real_dir;
	real_dir = NULL;
#endif	/* defined(__AST_DEBUG_MALLOC) */
	*file = ast_strdup(file_portion);
	return 0;
}

static void safe_closedir(DIR *dirp)
{
	if (!dirp) {
		return;
	}
	closedir(dirp);
}

/*!
 * \brief Finds a recording in the given directory.
 *
 * This function searchs for a file with the given file name, with a registered
 * format that matches its extension.
 *
 * \param dir_name Directory to search (absolute path).
 * \param file File name, without extension.
 * \return Absolute path of the recording file.
 * \return \c NULL if recording is not found.
 */
static char *find_recording(const char *dir_name, const char *file)
{
	RAII_VAR(DIR *, dir, NULL, safe_closedir);
	struct dirent entry;
	struct dirent *result = NULL;
	char *ext = NULL;
	char *file_with_ext = NULL;

	dir = opendir(dir_name);
	if (!dir) {
		return NULL;
	}

	while (readdir_r(dir, &entry, &result) == 0 && result != NULL) {
		ext = strrchr(result->d_name, '.');

		if (!ext) {
			/* No file extension; not us */
			continue;
		}
		*ext++ = '\0';

		if (strcmp(file, result->d_name) == 0) {
			if (!ast_get_format_for_file_ext(ext)) {
				ast_log(LOG_WARNING,
					"Recording %s: unrecognized format %s\n",
					result->d_name,
					ext);
				/* Keep looking */
				continue;
			}
			/* We have a winner! */
			break;
		}
	}

	if (!result) {
		return NULL;
	}

	ast_asprintf(&file_with_ext, "%s/%s.%s", dir_name, file, ext);
	return file_with_ext;
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

static int scan(struct ao2_container *recordings,
	const char *base_dir, const char *subdir, struct dirent *entry);

static int scan_file(struct ao2_container *recordings,
	const char *base_dir, const char *subdir, const char *filename,
	const char *path)
{
	RAII_VAR(struct stasis_app_stored_recording *, recording, NULL,
		ao2_cleanup);
	const char *ext;
	char *dot;

	ext = strrchr(filename, '.');

	if (!ext) {
		ast_verb(4, "  Ignore file without extension: %s\n",
			filename);
		/* No file extension; not us */
		return 0;
	}
	++ext;

	if (!ast_get_format_for_file_ext(ext)) {
		ast_verb(4, "  Not a media file: %s\n", filename);
		/* Not a media file */
		return 0;
	}

	recording = recording_alloc();
	if (!recording) {
		return -1;
	}

	ast_string_field_set(recording, file_with_ext, path);

	/* Build file and format from full path */
	ast_string_field_set(recording, file, path);
	dot = strrchr(recording->file, '.');
	*dot = '\0';
	recording->format = dot + 1;

	/* Removed the recording dir from the file for the name. */
	ast_string_field_set(recording, name,
		recording->file + strlen(ast_config_AST_RECORDING_DIR) + 1);

	/* Add it to the recordings container */
	ao2_link(recordings, recording);

	return 0;
}

static int scan_dir(struct ao2_container *recordings,
	const char *base_dir, const char *subdir, const char *dirname,
	const char *path)
{
	RAII_VAR(DIR *, dir, NULL, safe_closedir);
	RAII_VAR(struct ast_str *, rel_dirname, NULL, ast_free);
	struct dirent entry;
	struct dirent *result = NULL;

	if (strcmp(dirname, ".") == 0 ||
		strcmp(dirname, "..") == 0) {
		ast_verb(4, "  Ignoring self/parent dir\n");
		return 0;
	}

	/* Build relative dirname */
	rel_dirname = ast_str_create(80);
	if (!rel_dirname) {
		return -1;
	}
	if (!ast_strlen_zero(subdir)) {
		ast_str_append(&rel_dirname, 0, "%s/", subdir);
	}
	if (!ast_strlen_zero(dirname)) {
		ast_str_append(&rel_dirname, 0, "%s", dirname);
	}

	/* Read the directory */
	dir = opendir(path);
	if (!dir) {
		ast_log(LOG_WARNING, "Error reading dir '%s'\n", path);
		return -1;
	}
	while (readdir_r(dir, &entry, &result) == 0 && result != NULL) {
		scan(recordings, base_dir, ast_str_buffer(rel_dirname), result);
	}

	return 0;
}

static int scan(struct ao2_container *recordings,
	const char *base_dir, const char *subdir, struct dirent *entry)
{
	RAII_VAR(struct ast_str *, path, NULL, ast_free);

	path = ast_str_create(255);
	if (!path) {
		return -1;
	}

	/* Build file path */
	ast_str_append(&path, 0, "%s", base_dir);
	if (!ast_strlen_zero(subdir)) {
		ast_str_append(&path, 0, "/%s", subdir);
	}
	if (entry) {
		ast_str_append(&path, 0, "/%s", entry->d_name);
	}
	ast_verb(4, "Scanning '%s'\n", ast_str_buffer(path));

	/* Handle this file */
	switch (entry->d_type) {
	case DT_REG:
		scan_file(recordings, base_dir, subdir, entry->d_name,
			ast_str_buffer(path));
		break;
	case DT_DIR:
		scan_dir(recordings, base_dir, subdir, entry->d_name,
			ast_str_buffer(path));
		break;
	default:
		ast_log(LOG_WARNING, "Skipping %s: not a regular file\n",
			ast_str_buffer(path));
		break;
	}

	return 0;
}

struct ao2_container *stasis_app_stored_recording_find_all(void)
{
	RAII_VAR(struct ao2_container *, recordings, NULL, ao2_cleanup);
	int res;

	recordings = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, recording_sort, NULL);
	if (!recordings) {
		return NULL;
	}

	res = scan_dir(recordings, ast_config_AST_RECORDING_DIR, "", "",
		ast_config_AST_RECORDING_DIR);
	if (res != 0) {
		return NULL;
	}

	ao2_ref(recordings, +1);
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
		/* Attempt to escape the recording directory */
		ast_log(LOG_WARNING, "Attempt to access invalid recording %s\n",
			name);
		errno = EACCES;
		return NULL;
	}

	/* The actual name of the recording is file with the config dir
	 * prefix removed.
	 */
	ast_string_field_set(recording, name,
		recording->file + strlen(ast_config_AST_RECORDING_DIR) + 1);

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
