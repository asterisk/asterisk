/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Matt Jordan
 *
 * Matt Jordan <mjordan@digium.com>
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

/*!
 * \file
 * \brief An in-memory media cache
 *
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <sys/stat.h>
#include "asterisk/config.h"
#include "asterisk/bucket.h"
#include "asterisk/astdb.h"
#include "asterisk/media_cache.h"

/*! The name of the AstDB family holding items in the cache. */
#define AST_DB_FAMILY "MediaCache"

/*! Length of 'MediaCache' + 2 '/' characters */
#define AST_DB_FAMILY_LEN 12

/*! Number of buckets in the ao2 container holding our media items */
#define AO2_BUCKETS 61

/*! Our one and only container holding media items */
static struct ao2_container *media_cache;

/*!
 * \internal
 * \brief Hashing function for file metadata
 */
static int media_cache_hash(const void *obj, const int flags)
{
	const struct ast_bucket_file *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = ast_sorcery_object_get_id(object);
		break;
	default:
		/* Hash can only work on something with a full key */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*!
 * \internal
 * \brief Comparison function for file metadata
 */
static int media_cache_cmp(void *obj, void *arg, int flags)
{
	struct ast_bucket_file *left = obj;
	struct ast_bucket_file *right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = ast_sorcery_object_get_id(right);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(ast_sorcery_object_get_id(left), right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(ast_sorcery_object_get_id(left), right_key, strlen(right_key));
		break;
	default:
		ast_assert(0);
		cmp = 0;
		break;
	}

	return cmp ? 0 : CMP_MATCH | CMP_STOP;
}


int ast_media_cache_exists(const char *uri)
{
	struct ast_bucket_file *bucket_file;

	if (ast_strlen_zero(uri)) {
		return 0;
	}

	bucket_file = ao2_find(media_cache, uri, OBJ_SEARCH_KEY);
	if (bucket_file) {
		ao2_ref(bucket_file, -1);
		return 1;
	}

	/* Check to see if any bucket implementation could return this item */
	bucket_file = ast_bucket_file_retrieve(uri);
	if (bucket_file) {
		ao2_ref(bucket_file, -1);
		return 1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Sync \c bucket_file metadata to the AstDB
 * \param bucket_file The bucket holding the metadata
 * \param hash The hash family key in the AstDB to associate the entry with
 * \param name The name of the metadata to sync to the AstDB
 */
static void metadata_sync_to_astdb(struct ast_bucket_file *bucket_file,
	const char *hash, const char *name)
{
	struct ast_bucket_metadata *metadata;

	metadata = ast_bucket_file_metadata_get(bucket_file, name);
	if (metadata) {
		ast_db_put(hash, metadata->name, metadata->value);
		ao2_ref(metadata, -1);
	}
}

/*!
 * \internal
 * \brief Sync a media cache item to the AstDB
 * \param bucket_file The \c ast_bucket_file media cache item to sync
 */
static void media_cache_item_sync_to_astdb(struct ast_bucket_file *bucket_file)
{
	char hash[41]; /* 40 character SHA1 hash */

	ast_sha1_hash(hash, ast_sorcery_object_get_id(bucket_file));
	if (ast_db_put(AST_DB_FAMILY, ast_sorcery_object_get_id(bucket_file), hash)) {
		return;
	}

	ast_db_put(hash, "path", bucket_file->path);
	metadata_sync_to_astdb(bucket_file, hash, "size");
	metadata_sync_to_astdb(bucket_file, hash, "ext");
	metadata_sync_to_astdb(bucket_file, hash, "accessed");
}

/*!
 * \internal
 * \brief Delete a media cache item from the AstDB
 * \param bucket_file The \c ast_bucket_file media cache item to delete
 */
static void media_cache_item_del_from_astdb(struct ast_bucket_file *bucket_file)
{
	char *hash_value;

	if (ast_db_get_allocated(AST_DB_FAMILY, ast_sorcery_object_get_id(bucket_file), &hash_value)) {
		return;
	}

	ast_db_deltree(hash_value, NULL);
	ast_db_del(AST_DB_FAMILY, hash_value);
	ast_free(hash_value);
}

/*!
 * \internal
 * \brief Update the name of the file backing a \c bucket_file
 * \param preferred_file_name The preferred name of the backing file
 */
static void bucket_file_update_path(struct ast_bucket_file *bucket_file,
	const char *preferred_file_name)
{
	rename(bucket_file->path, preferred_file_name);
	ast_copy_string(bucket_file->path, preferred_file_name,
		sizeof(bucket_file->path));
}

int ast_media_cache_retrieve(const char *uri, const char *preferred_file_name,
	char *file_path, size_t len)
{
	struct ast_bucket_file *bucket_file;
	SCOPED_AO2LOCK(media_lock, media_cache);

	if (ast_strlen_zero(uri)) {
		return -1;
	}

	bucket_file = ao2_find(media_cache, uri, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (bucket_file) {
		ao2_lock(bucket_file);
		ast_bucket_file_update(bucket_file);
		bucket_file_update_path(bucket_file, preferred_file_name);
		ast_copy_string(file_path, bucket_file->path, len);
		media_cache_item_sync_to_astdb(bucket_file);
		ao2_unlock(bucket_file);
		ao2_ref(bucket_file, -1);
		return 0;
	}

	bucket_file = ast_bucket_file_retrieve(uri);
	if (!bucket_file) {
		bucket_file = ast_bucket_file_alloc(uri);
		if (!bucket_file) {
			ast_log(LOG_WARNING, "Failed to create storage for %s\n", uri);
			return -1;
		}

		if (!ast_strlen_zero(preferred_file_name)) {
			ast_copy_string(bucket_file->path, preferred_file_name,
				sizeof(bucket_file->path));
		} else if (ast_bucket_file_temporary_create(bucket_file)) {
			ast_log(LOG_WARNING, "Failed to create temp storage for %s\n", uri);
			ao2_ref(bucket_file, -1);
			return -1;
		}

		if (ast_bucket_file_create(bucket_file)) {
			ast_log(LOG_WARNING, "Failed to obtain media at %s\n", uri);
			ao2_ref(bucket_file, -1);
			return -1;
		}
	} else if (!ast_strlen_zero(preferred_file_name)) {
		ao2_lock(bucket_file);
		bucket_file_update_path(bucket_file, preferred_file_name);
		ao2_unlock(bucket_file);
	}
	ast_copy_string(file_path, bucket_file->path, len);
	media_cache_item_sync_to_astdb(bucket_file);
	ao2_link_flags(media_cache, bucket_file, OBJ_NOLOCK);

	ao2_ref(bucket_file, -1);

	return 0;
}

int ast_media_cache_retrieve_metadata(const char *uri, const char *key,
	char *value, size_t len)
{
	struct ast_bucket_file *bucket_file;
	struct ast_bucket_metadata *metadata;

	if (ast_strlen_zero(uri) || ast_strlen_zero(key) || !value) {
		return -1;
	}

	bucket_file = ao2_find(media_cache, uri, OBJ_SEARCH_KEY);
	if (!bucket_file) {
		return -1;
	}

	metadata = ao2_find(bucket_file->metadata, key, OBJ_SEARCH_KEY);
	if (!metadata) {
		ao2_ref(bucket_file, -1);
		return -1;
	}
	ast_copy_string(value, metadata->value, len);

	ao2_ref(metadata, -1);
	ao2_ref(bucket_file, -1);
	return 0;
}

int ast_media_cache_create_or_update(const char *uri, const char *file_path,
	struct ast_variable *metadata)
{
	struct ast_bucket_file *bucket_file;
	struct stat st;
	char tmp[128];
	char *ext;
	char *file_path_ptr;
	struct ast_variable *it_metadata;
	SCOPED_AO2LOCK(media_lock, media_cache);

	if (ast_strlen_zero(file_path) || ast_strlen_zero(uri)) {
		return -1;
	}
	file_path_ptr = ast_strdupa(file_path);

	if (stat(file_path, &st)) {
		ast_log(LOG_WARNING, "Unable to obtain information for file %s for URI %s\n",
			file_path, uri);
		return -1;
	}

	bucket_file = ao2_find(media_cache, uri, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (!bucket_file) {
		bucket_file = ast_bucket_file_retrieve(uri);
		if (!bucket_file) {
			bucket_file = ast_bucket_file_alloc(uri);
			if (!bucket_file) {
				ast_log(LOG_WARNING, "Failed to create file storage for %s and %s\n",
					uri, file_path);
				return -1;
			}
		}
		ao2_link_flags(media_cache, bucket_file, OBJ_NOLOCK);
	}

	ao2_lock(bucket_file);
	strcpy(bucket_file->path, file_path);
	bucket_file->created.tv_sec = st.st_ctime;
	bucket_file->modified.tv_sec = st.st_mtime;

	snprintf(tmp, sizeof(tmp), "%ld", (long)st.st_atime);
	ast_bucket_file_metadata_set(bucket_file, "accessed", tmp);

	snprintf(tmp, sizeof(tmp), "%zu", st.st_size);
	ast_bucket_file_metadata_set(bucket_file, "size", tmp);

	ext = strrchr(file_path_ptr, '.');
	if (ext) {
		ast_bucket_file_metadata_set(bucket_file, "ext", ext + 1);
	}

	for (it_metadata = metadata; it_metadata; it_metadata = it_metadata->next) {
		ast_bucket_file_metadata_set(bucket_file, it_metadata->name, it_metadata->value);
	}

	if (ast_bucket_file_create(bucket_file)) {
		ao2_unlock(bucket_file);
		ast_log(LOG_WARNING, "Failed to create media for %s\n", uri);
		ao2_unlink_flags(media_cache, bucket_file, OBJ_NOLOCK);
		ao2_ref(bucket_file, -1);
		return -1;
	}
	media_cache_item_sync_to_astdb(bucket_file);
	ao2_unlock(bucket_file);

	ao2_ref(bucket_file, -1);
	return 0;
}

int ast_media_cache_delete(const char *uri)
{
	struct ast_bucket_file *bucket_file;
	int res;

	if (ast_strlen_zero(uri)) {
		return -1;
	}

	bucket_file = ao2_find(media_cache, uri, OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (!bucket_file) {
		return -1;
	}

	res = ast_bucket_file_delete(bucket_file);
	media_cache_item_del_from_astdb(bucket_file);

	ao2_ref(bucket_file, -1);

	return res;
}

/*!
 * \internal
 * \brief Shutdown the media cache
 */
static void media_cache_shutdown(void)
{
	ao2_ref(media_cache, -1);
	media_cache = NULL;
}

/*!
 * \internal
 * \brief Remove a media cache item from the AstDB
 * \param uri The unique URI that represents the item in the cache
 * \param hash The hash key for the item in the AstDB
 */
static void media_cache_remove_from_astdb(const char *uri, const char *hash)
{
	ast_db_del(AST_DB_FAMILY, uri + AST_DB_FAMILY_LEN);
	ast_db_deltree(hash, NULL);
}

/*!
 * \internal
 * \brief Create an item in the media cache from entries in the AstDB
 * \param uri The unique URI that represents the item in the cache
 * \param hash The hash key for the item in the AstDB
 * \retval 0 success
 * \retval -1 failure
 */
static int media_cache_item_populate_from_astdb(const char *uri, const char *hash)
{
	char path[PATH_MAX];

	if (ast_db_get(hash, "path", path, sizeof(path))) {
		ast_log(LOG_WARNING, "Failed to restore media cache item for '%s' from AstDB: no 'path' specified\n",
			uri);
		return -1;
	}

	return ast_media_cache_create_or_update(uri + AST_DB_FAMILY_LEN, path, NULL);
}

/*!
 * \internal
 * \brief Populate the media cache from entries in the AstDB
 */
static void media_cache_populate_from_astdb(void)
{
	struct ast_db_entry *db_entry;
	struct ast_db_entry *db_tree;

	db_tree = ast_db_gettree(AST_DB_FAMILY, NULL);
	for (db_entry = db_tree; db_entry; db_entry = db_entry->next) {
		if (media_cache_item_populate_from_astdb(db_entry->key, db_entry->data)) {
			media_cache_remove_from_astdb(db_entry->key, db_entry->data);
		}
	}
	ast_db_freetree(db_tree);
}

int ast_media_cache_init(void)
{
	ast_register_atexit(media_cache_shutdown);

	media_cache = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_RWLOCK, AO2_BUCKETS,
		media_cache_hash, media_cache_cmp);
	if (!media_cache) {
		return -1;
	}

	media_cache_populate_from_astdb();

	return 0;
}
