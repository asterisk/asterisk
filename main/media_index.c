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
 * \brief Sound file format and description indexer.
 */

#include "asterisk.h"

#include <dirent.h>
#include <sys/stat.h>

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/media_index.h"
#include "asterisk/file.h"

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

/*! \brief The number of buckets to be used for storing variant-keyed objects */
#define VARIANT_BUCKETS 7

/*! \brief The number of buckets to be used for storing media filename-keyed objects */
#define INDEX_BUCKETS 157

/*! \brief Structure to hold a list of the format variations for a media file for a specific variant */
struct media_variant {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(description);	/*!< The description of the media */
	);
	struct ast_format_cap *formats;	/*!< The formats this media is available in for this variant */
	char variant[0];				/*!< The variant this media is available in */
};

static void media_variant_destroy(void *obj)
{
	struct media_variant *variant = obj;

	ast_string_field_free_memory(variant);
	ao2_cleanup(variant->formats);
}

static struct media_variant *media_variant_alloc(const char *variant_str)
{
	size_t str_sz = strlen(variant_str) + 1;
	struct media_variant *variant;

	variant = ao2_alloc_options(sizeof(*variant) + str_sz, media_variant_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!variant) {
		return NULL;
	}

	memcpy(variant->variant, variant_str, str_sz);

	variant->formats = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!variant->formats || ast_string_field_init(variant, 8)) {
		ao2_ref(variant, -1);

		return NULL;
	}

	return variant;
}

static int media_variant_hash(const void *obj, const int flags)
{
	const char *variant = (flags & OBJ_KEY) ? obj : ((struct media_variant*) obj)->variant;
	return ast_str_case_hash(variant);
}

static int media_variant_cmp(void *obj, void *arg, int flags)
{
	struct media_variant *opt1 = obj, *opt2 = arg;
	const char *variant = (flags & OBJ_KEY) ? arg : opt2->variant;
	return strcasecmp(opt1->variant, variant) ? 0 : CMP_MATCH | CMP_STOP;
}

/*! \brief Structure to hold information about a media file */
struct media_info {
	struct ao2_container *variants;  /*!< The variants for which this media is available */
	char name[0];                    /*!< The file name of the media */
};

static void media_info_destroy(void *obj)
{
	struct media_info *info = obj;

	ao2_cleanup(info->variants);
}

static struct media_info *media_info_alloc(const char *name)
{
	size_t name_sz = strlen(name) + 1;
	struct media_info *info;

	info = ao2_alloc_options(sizeof(*info) + name_sz, media_info_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!info) {
		return NULL;
	}

	memcpy(info->name, name, name_sz);

	info->variants = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		VARIANT_BUCKETS, media_variant_hash, NULL, media_variant_cmp);
	if (!info->variants) {
		ao2_ref(info, -1);

		return NULL;
	}

	return info;
}

static int media_info_hash(const void *obj, const int flags)
{
	const char *name = (flags & OBJ_KEY) ? obj : ((struct media_info*) obj)->name;
	return ast_str_case_hash(name);
}

static int media_info_cmp(void *obj, void *arg, int flags)
{
	struct media_info *opt1 = obj, *opt2 = arg;
	const char *name = (flags & OBJ_KEY) ? arg : opt2->name;
	return strcasecmp(opt1->name, name) ? 0 : CMP_MATCH | CMP_STOP;
}

struct ast_media_index {
	struct ao2_container *index;            /*!< The index of media that has requested */
	struct ao2_container *media_list_cache; /*!< Cache of filenames to prevent them from being regenerated so often */
	char base_dir[0];                       /*!< Base directory for indexing */
};

static void media_index_dtor(void *obj)
{
	struct ast_media_index *index = obj;

	ao2_cleanup(index->index);
	ao2_cleanup(index->media_list_cache);
}

struct ast_media_index *ast_media_index_create(const char *base_dir)
{
	size_t base_dir_sz = strlen(base_dir) + 1;
	struct ast_media_index *index = ao2_alloc(sizeof(*index) + base_dir_sz, media_index_dtor);

	if (!index) {
		return NULL;
	}

	memcpy(index->base_dir, base_dir, base_dir_sz);

	index->index = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, INDEX_BUCKETS,
		media_info_hash, NULL, media_info_cmp);
	if (!index->index) {
		ao2_ref(index, -1);

		return NULL;
	}

	return index;
}

static struct media_variant *find_variant(struct ast_media_index *index, const char *filename, const char *variant)
{
	RAII_VAR(struct media_info *, info, NULL, ao2_cleanup);

	info = ao2_find(index->index, filename, OBJ_KEY);
	if (!info) {
		return NULL;
	}

	return ao2_find(info->variants, variant, OBJ_KEY);
}

/*! \brief create the appropriate media_variant and any necessary structures */
static struct media_variant *alloc_variant(struct ast_media_index *index, const char *filename, const char *variant_str)
{
	struct media_info *info;
	struct media_variant *variant = NULL;

	info = ao2_find(index->index, filename, OBJ_KEY);
	if (!info) {
		/* This is the first time the index has seen this filename,
		 * allocate and link */
		info = media_info_alloc(filename);
		if (!info) {
			return NULL;
		}

		ao2_link(index->index, info);
	} else {
		variant = ao2_find(info->variants, variant_str, OBJ_KEY);
	}

	if (!variant) {
		/* This is the first time the index has seen this variant for
		 * this filename, allocate and link */
		variant = media_variant_alloc(variant_str);
		if (variant) {
			ao2_link(info->variants, variant);
		}
	}

	ao2_ref(info, -1);

	return variant;
}

const char *ast_media_get_description(struct ast_media_index *index, const char *filename, const char *variant_str)
{
	RAII_VAR(struct media_variant *, variant, NULL, ao2_cleanup);
	if (ast_strlen_zero(filename) || ast_strlen_zero(variant_str)) {
		return NULL;
	}

	variant = find_variant(index, filename, variant_str);
	if (!variant) {
		return NULL;
	}

	return variant->description;
}

struct ast_format_cap *ast_media_get_format_cap(struct ast_media_index *index, const char *filename, const char *variant_str)
{
	struct ast_format_cap *dupcap;
	RAII_VAR(struct media_variant *, variant, NULL, ao2_cleanup);
	if (ast_strlen_zero(filename) || ast_strlen_zero(variant_str)) {
		return NULL;
	}

	variant = find_variant(index, filename, variant_str);
	if (!variant) {
		return NULL;
	}

	dupcap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (dupcap) {
		ast_format_cap_append_from_cap(dupcap, variant->formats, AST_MEDIA_TYPE_UNKNOWN);
	}
	return dupcap;
}

/*! \brief Add the variant to the list of variants requested */
static int add_variant_cb(void *obj, void *arg, int flags)
{
	struct media_variant *variant = obj;
	struct ao2_container *variants= arg;
	ast_str_container_add(variants, variant->variant);
	return 0;
}

struct ao2_container *ast_media_get_variants(struct ast_media_index *index, const char *filename)
{
	RAII_VAR(struct media_info *, info, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, variants, NULL, ao2_cleanup);
	if (!filename) {
		return NULL;
	}

	variants = ast_str_container_alloc(VARIANT_BUCKETS);
	if (!variants) {
		return NULL;
	}

	info = ao2_find(index->index, filename, OBJ_KEY);
	if (!info) {
		return NULL;
	}

	ao2_callback(info->variants, OBJ_NODATA, add_variant_cb, variants);

	ao2_ref(variants, +1);
	return variants;
}

/*! \brief Add the media_info's filename to the container of filenames requested */
static int add_media_cb(void *obj, void *arg, int flags)
{
	struct media_info *info = obj;
	struct ao2_container *media = arg;
	ast_str_container_add(media, info->name);
	return 0;
}

struct ao2_container *ast_media_get_media(struct ast_media_index *index)
{
	RAII_VAR(struct ao2_container *, media, NULL, ao2_cleanup);

	if (!index->media_list_cache) {
		media = ast_str_container_alloc(INDEX_BUCKETS);
		if (!media) {
			return NULL;
		}

		ao2_callback(index->index, OBJ_NODATA, add_media_cb, media);

		/* Ref to the cache */
		ao2_ref(media, +1);
		index->media_list_cache = media;
	}

	/* Ref to the caller */
	ao2_ref(index->media_list_cache, +1);
	return index->media_list_cache;
}

/*! \brief Update an index with new format/variant information */
static int update_file_format_info(struct ast_media_index *index, const char *filename, const char *variant_str, struct ast_format *file_format)
{
	struct media_variant *variant;

	variant = alloc_variant(index, filename, variant_str);
	if (!variant) {
		return -1;
	}

	ast_format_cap_append(variant->formats, file_format, 0);
	ao2_ref(variant, -1);

	return 0;
}

/*! \brief Process a media file into the index */
static int process_media_file(struct ast_media_index *index, const char *variant, const char *subdir, const char *filename_stripped, const char *ext)
{
	struct ast_format *file_format;
	const char *file_identifier = filename_stripped;
	char *file_id_str = NULL;
	int res;

	file_format = ast_get_format_for_file_ext(ext);
	if (!file_format) {
		/* extension not registered */
		return 0;
	}

	/* handle updating the file information */
	if (subdir) {
		if (ast_asprintf(&file_id_str, "%s/%s", subdir, filename_stripped) == -1) {
			return -1;
		}

		file_identifier = file_id_str;
	}

	res = update_file_format_info(index, file_identifier, variant, file_format);
	ast_free(file_id_str);

	return res;
}

/*!
 * \brief Process a media description text file
 *
 * This currently processes core-sounds-*.txt and extra-sounds-*.txt, but will
 * process others if present.
 */
static int process_description_file(struct ast_media_index *index,
	const char *subdir,
	const char *variant_str,
	const char *filename)
{
	RAII_VAR(struct ast_str *, description_file_path, ast_str_create(64), ast_free);
	RAII_VAR(struct ast_str *, cumulative_description, ast_str_create(64), ast_free);
	char *file_id_persist = NULL;
	int res = 0;
	FILE *f = NULL;
#if defined(LOW_MEMORY)
	char buf[256];
#else
	char buf[2048];
#endif

	if (!description_file_path || !cumulative_description) {
		return -1;
	}

	if (ast_strlen_zero(subdir)) {
		ast_str_set(&description_file_path, 0, "%s/%s/%s", index->base_dir, variant_str, filename);
	} else {
		ast_str_set(&description_file_path, 0, "%s/%s/%s/%s", index->base_dir, variant_str, subdir, filename);
	}
	f = fopen(ast_str_buffer(description_file_path), "r");
	if (!f) {
		ast_log(LOG_WARNING, "Could not open media description file '%s': %s\n", ast_str_buffer(description_file_path), strerror(errno));
		return -1;
	}

	while (!feof(f)) {
		char *file_identifier, *description;
		if (!fgets(buf, sizeof(buf), f)) {
			if (ferror(f)) {
				ast_log(LOG_ERROR, "Error reading from file %s: %s\n", ast_str_buffer(description_file_path), strerror(errno));
			}
			continue;
		}

		/* Skip lines that are too long */
		if (strlen(buf) == sizeof(buf) - 1 && buf[sizeof(buf) - 1] != '\n') {
			ast_log(LOG_WARNING, "Line too long, skipping. It begins with: %.32s...\n", buf);
			while (fgets(buf, sizeof(buf), f)) {
				if (strlen(buf) != sizeof(buf) - 1 || buf[sizeof(buf) - 1] == '\n') {
					break;
				}
			}
			if (ferror(f)) {
				ast_log(LOG_ERROR, "Error reading from file %s: %s\n", ast_str_buffer(description_file_path), strerror(errno));
			}
			continue;
		}

		if (buf[0] == ';') {
			/* ignore comments */
			continue;
		}

		ast_trim_blanks(buf);
		description = buf;
		file_identifier = strsep(&description, ":");
		if (!description) {
			/* no ':' means this is a continuation */
			if (file_id_persist) {
				ast_str_append(&cumulative_description, 0, "\n%s", file_identifier);
			}
			continue;
		} else {
			/* if there's text in cumulative_description, archive it and start anew */
			if (file_id_persist && !ast_strlen_zero(ast_str_buffer(cumulative_description))) {
				struct media_variant *variant;

				variant = alloc_variant(index, file_id_persist, variant_str);
				if (!variant) {
					res = -1;
					break;
				}

				ast_string_field_set(variant, description, ast_str_buffer(cumulative_description));

				ast_str_reset(cumulative_description);
				ao2_ref(variant, -1);
			}

			ast_free(file_id_persist);
			file_id_persist = ast_strdup(file_identifier);
			description = ast_skip_blanks(description);
			ast_str_set(&cumulative_description, 0, "%s", description);
		}
	}

	/* handle the last one */
	if (file_id_persist && !ast_strlen_zero(ast_str_buffer(cumulative_description))) {
		struct media_variant *variant;

		variant = alloc_variant(index, file_id_persist, variant_str);
		if (variant) {
			ast_string_field_set(variant, description, ast_str_buffer(cumulative_description));
			ao2_ref(variant, -1);
		} else {
			res = -1;
		}
	}

	ast_free(file_id_persist);
	fclose(f);
	return res;
}

/*! \brief process an individual file listing */
static int process_file(struct ast_media_index *index, const char *variant_str, const char *subdir, const char *filename)
{
	RAII_VAR(char *, filename_stripped, ast_strdup(filename), ast_free);
	char *ext;

	if (!filename_stripped) {
		return -1;
	}

	ext = strrchr(filename_stripped, '.');
	if (!ext) {
		/* file has no extension */
		return 0;
	}

	*ext++ = '\0';
	if (!strcmp(ext, "txt")) {
		if (process_description_file(index, subdir, variant_str, filename)) {
			return -1;
		}
	} else {
		if (process_media_file(index, variant_str, subdir, filename_stripped, ext)) {
			return -1;
		}
	}
	return 0;
}

/*! \brief internal function for updating the index, recursive */
static int media_index_update(struct ast_media_index *index,
	const char *variant,
	const char *subdir)
{
	struct dirent* dent;
	DIR* srcdir;
	RAII_VAR(struct ast_str *, index_dir, ast_str_create(64), ast_free);
	RAII_VAR(struct ast_str *, statfile, ast_str_create(64), ast_free);
	int res = 0;

	if (!index_dir) {
		return 0;
	}

	ast_str_set(&index_dir, 0, "%s", index->base_dir);
	if (!ast_strlen_zero(variant)) {
		ast_str_append(&index_dir, 0, "/%s", variant);
	}
	if (!ast_strlen_zero(subdir)) {
		ast_str_append(&index_dir, 0, "/%s", subdir);
	}

	srcdir = opendir(ast_str_buffer(index_dir));
	if (srcdir == NULL) {
		ast_log(LOG_ERROR, "Failed to open %s: %s\n", ast_str_buffer(index_dir), strerror(errno));
		return -1;
	}

	while((dent = readdir(srcdir)) != NULL) {
		struct stat st;

		if(!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, "..")) {
			continue;
		}

		ast_str_reset(statfile);
		ast_str_set(&statfile, 0, "%s/%s", ast_str_buffer(index_dir), dent->d_name);

		if (stat(ast_str_buffer(statfile), &st) < 0) {
			ast_log(LOG_WARNING, "Failed to stat %s: %s\n", ast_str_buffer(statfile), strerror(errno));
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			if (ast_strlen_zero(subdir)) {
				res = media_index_update(index, variant, dent->d_name);
			} else {
				RAII_VAR(struct ast_str *, new_subdir, ast_str_create(64), ast_free);
				ast_str_set(&new_subdir, 0, "%s/%s", subdir, dent->d_name);
				res = media_index_update(index, variant, ast_str_buffer(new_subdir));
			}

			if (res) {
				break;
			}
			continue;
		}

		if (!S_ISREG(st.st_mode)) {
			continue;
		}

		if (process_file(index, variant, subdir, dent->d_name)) {
			res = -1;
			break;
		}
	}

	closedir(srcdir);
	return res;
}

int ast_media_index_update(struct ast_media_index *index,
	const char *variant)
{
	return media_index_update(index, variant, NULL);
}
