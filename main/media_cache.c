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

#include <sys/stat.h>
#include "asterisk/config.h"
#include "asterisk/bucket.h"
#include "asterisk/astdb.h"
#include "asterisk/cli.h"
#include "asterisk/file.h"
#include "asterisk/media_cache.h"

/*! The name of the AstDB family holding items in the cache. */
#define AST_DB_FAMILY "MediaCache"

/*! Length of 'MediaCache' + 2 '/' characters */
#define AST_DB_FAMILY_LEN 12

/*! Number of buckets in the ao2 container holding our media items */
#define AO2_BUCKETS 61

/*! Our one and only container holding media items */
static struct ao2_container *media_cache;

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
 */
static int metadata_sync_to_astdb(void *obj, void *arg, int flags)
{
	struct ast_bucket_metadata *metadata = obj;
	const char *hash = arg;

	ast_db_put(hash, metadata->name, metadata->value);

	return 0;
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
	ast_bucket_file_metadata_callback(bucket_file, metadata_sync_to_astdb, hash);
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
	ast_db_del(AST_DB_FAMILY, ast_sorcery_object_get_id(bucket_file));
	ast_free(hash_value);
}

/*!
 * \internal
 * \brief Normalize the value of a Content-Type header
 *
 * This will trim off any optional parameters after the type/subtype.
 */
static void normalize_content_type_header(char *content_type)
{
	char *params = strchr(content_type, ';');

	if (params) {
		*params-- = 0;
		while (params > content_type && (*params == ' ' || *params == '\t')) {
			*params-- = 0;
		}
	}
}

/*!
 * \internal
 * \brief Update the name of the file backing a \c bucket_file
 * \param preferred_file_name The preferred name of the backing file
 */
static void bucket_file_update_path(struct ast_bucket_file *bucket_file,
	const char *preferred_file_name)
{
	char *ext;

	if (!ast_strlen_zero(preferred_file_name) && strcmp(bucket_file->path, preferred_file_name)) {
		/* Use the preferred file name if available */

		rename(bucket_file->path, preferred_file_name);
		ast_copy_string(bucket_file->path, preferred_file_name,
			sizeof(bucket_file->path));
	} else if (!strchr(bucket_file->path, '.') && (ext = strrchr(ast_sorcery_object_get_id(bucket_file), '.'))) {
		/* If we don't have a file extension and were provided one in the URI, use it */
		char new_path[PATH_MAX];
		char found_ext[PATH_MAX];

		ast_bucket_file_metadata_set(bucket_file, "ext", ext);

		/* Don't pass '.' while checking for supported extension */
		if (!ast_get_format_for_file_ext(ext + 1)) {
			/* If the file extension passed in the URI isn't supported check for the
			 * extension based on the MIME type passed in the Content-Type header before
			 * giving up.
			 * If a match is found then retrieve the extension from the supported list
			 * corresponding to the mime-type and use that to rename the file */
			struct ast_bucket_metadata *header = ast_bucket_file_metadata_get(bucket_file, "content-type");
			if (header) {
				char *mime_type = ast_strdup(header->value);
				if (mime_type) {
					normalize_content_type_header(mime_type);
					if (!ast_strlen_zero(mime_type)) {
						if (ast_get_extension_for_mime_type(mime_type, found_ext, sizeof(found_ext))) {
							ext = found_ext;
						}
					}
					ast_free(mime_type);
				}
			}
		}

		snprintf(new_path, sizeof(new_path), "%s%s", bucket_file->path, ext);
		rename(bucket_file->path, new_path);
		ast_copy_string(bucket_file->path, new_path, sizeof(bucket_file->path));
	}
}

int ast_media_cache_retrieve(const char *uri, const char *preferred_file_name,
	char *file_path, size_t len)
{
	struct ast_bucket_file *bucket_file;
	char *ext;
	SCOPED_AO2LOCK(media_lock, media_cache);

	if (ast_strlen_zero(uri)) {
		return -1;
	}

	/* First, retrieve from the ao2 cache here. If we find a bucket_file
	 * matching the requested URI, ask the appropriate backend if it is
	 * stale. If not; return it.
	 */
	bucket_file = ao2_find(media_cache, uri, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (bucket_file) {
		if (!ast_bucket_file_is_stale(bucket_file)
			&& ast_file_is_readable(bucket_file->path)) {
			ast_copy_string(file_path, bucket_file->path, len);
			if ((ext = strrchr(file_path, '.'))) {
				*ext = '\0';
			}
			ao2_ref(bucket_file, -1);

			ast_debug(5, "Returning media at local file: %s\n", file_path);
			return 0;
		}

		/* Stale! Remove the item completely, as we're going to replace it next */
		ao2_unlink_flags(media_cache, bucket_file, OBJ_NOLOCK);
		ast_bucket_file_delete(bucket_file);
		ao2_ref(bucket_file, -1);
	}

	/* Either this is new or the resource is stale; do a full retrieve
	 * from the appropriate bucket_file backend
	 */
	bucket_file = ast_bucket_file_retrieve(uri);
	if (!bucket_file) {
		ast_debug(2, "Failed to obtain media at '%s'\n", uri);
		return -1;
	}

	/* We can manipulate the 'immutable' bucket_file here, as we haven't
	 * let anyone know of its existence yet
	 */
	bucket_file_update_path(bucket_file, preferred_file_name);
	media_cache_item_sync_to_astdb(bucket_file);
	ast_copy_string(file_path, bucket_file->path, len);
	if ((ext = strrchr(file_path, '.'))) {
		*ext = '\0';
	}
	ao2_link_flags(media_cache, bucket_file, OBJ_NOLOCK);
	ao2_ref(bucket_file, -1);

	ast_debug(5, "Returning media at local file: %s\n", file_path);

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
	struct ast_variable *it_metadata;
	struct stat st;
	char tmp[128];
	char *ext;
	char *file_path_ptr;
	int created = 0;
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
	if (bucket_file) {
		struct ast_bucket_file *clone;

		clone = ast_bucket_file_clone(bucket_file);
		if (!clone) {
			ao2_ref(bucket_file, -1);
			return -1;
		}

		/* Remove the old bucket_file. We'll replace it if we succeed below. */
		ao2_unlink_flags(media_cache, bucket_file, OBJ_NOLOCK);
		ao2_ref(bucket_file, -1);

		bucket_file = clone;
	} else {
		bucket_file = ast_bucket_file_alloc(uri);
		if (!bucket_file) {
			ast_log(LOG_WARNING, "Failed to create file storage for %s and %s\n",
				uri, file_path);
			return -1;
		}
		created = 1;
	}

	strcpy(bucket_file->path, file_path);
	bucket_file->created.tv_sec = st.st_ctime;
	bucket_file->modified.tv_sec = st.st_mtime;

	snprintf(tmp, sizeof(tmp), "%ld", (long)st.st_atime);
	ast_bucket_file_metadata_set(bucket_file, "accessed", tmp);

	snprintf(tmp, sizeof(tmp), "%jd", (intmax_t)st.st_size);
	ast_bucket_file_metadata_set(bucket_file, "size", tmp);

	ext = strrchr(file_path_ptr, '.');
	if (ext) {
		ast_bucket_file_metadata_set(bucket_file, "ext", ext + 1);
	}

	for (it_metadata = metadata; it_metadata; it_metadata = it_metadata->next) {
		ast_bucket_file_metadata_set(bucket_file, it_metadata->name, it_metadata->value);
	}

	if (created && ast_bucket_file_create(bucket_file)) {
		ast_log(LOG_WARNING, "Failed to create media for %s\n", uri);
		ao2_ref(bucket_file, -1);
		return -1;
	}
	media_cache_item_sync_to_astdb(bucket_file);

	ao2_link_flags(media_cache, bucket_file, OBJ_NOLOCK);
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
	struct ast_bucket_file *bucket_file;
	struct ast_db_entry *db_tree;
	struct ast_db_entry *db_entry;
	struct stat st;

	bucket_file = ast_bucket_file_alloc(uri);
	if (!bucket_file) {
		return -1;
	}

	db_tree = ast_db_gettree(hash, NULL);
	for (db_entry = db_tree; db_entry; db_entry = db_entry->next) {
		const char *key = strchr(db_entry->key + 1, '/');

		if (ast_strlen_zero(key)) {
			continue;
		}
		key++;

		if (!strcasecmp(key, "path")) {
			strcpy(bucket_file->path, db_entry->data);

			if (stat(bucket_file->path, &st)) {
				ast_log(LOG_WARNING, "Unable to obtain information for file %s for URI %s\n",
					bucket_file->path, uri);
				ao2_ref(bucket_file, -1);
				ast_db_freetree(db_tree);
				return -1;
			}
		} else {
			ast_bucket_file_metadata_set(bucket_file, key, db_entry->data);
		}
	}
	ast_db_freetree(db_tree);

	if (ast_strlen_zero(bucket_file->path)) {
		ao2_ref(bucket_file, -1);
		ast_log(LOG_WARNING, "Failed to restore media cache item for '%s' from AstDB: no 'path' specified\n",
			uri);
		return -1;
	}

	ao2_link(media_cache, bucket_file);
	ao2_ref(bucket_file, -1);

	return 0;
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
		if (media_cache_item_populate_from_astdb(db_entry->key + AST_DB_FAMILY_LEN, db_entry->data)) {
			media_cache_remove_from_astdb(db_entry->key, db_entry->data);
		}
	}
	ast_db_freetree(db_tree);
}

/*!
 * \internal
 * \brief ao2 callback function for \ref media_cache_handle_show_all
 */
static int media_cache_prnt_summary(void *obj, void *arg, int flags)
{
#define FORMAT_ROW "%-40s\n\t%-40s\n"
	struct ast_bucket_file *bucket_file = obj;
	struct ast_cli_args *a = arg;

	ast_cli(a->fd, FORMAT_ROW, ast_sorcery_object_get_id(bucket_file), bucket_file->path);

#undef FORMAT_ROW
	return CMP_MATCH;
}

static char *media_cache_handle_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "media cache show all";
		e->usage =
			"Usage: media cache show all\n"
			"       Display a summary of all current items\n"
			"       in the media cache.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "URI\n\tLocal File\n");
	ast_cli(a->fd, "---------------\n");
	ao2_callback(media_cache, OBJ_NODATA | OBJ_MULTIPLE, media_cache_prnt_summary, a);

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief CLI tab completion function for URIs
 */
static char *cli_complete_uri(const char *word, int state)
{
	struct ast_bucket_file *bucket_file;
	struct ao2_iterator it_media_items;
	int wordlen = strlen(word);
	int which = 0;
	char *result = NULL;

	it_media_items = ao2_iterator_init(media_cache, 0);
	while ((bucket_file = ao2_iterator_next(&it_media_items))) {
		if (!strncasecmp(word, ast_sorcery_object_get_id(bucket_file), wordlen)
			&& ++which > state) {
			result = ast_strdup(ast_sorcery_object_get_id(bucket_file));
		}
		ao2_ref(bucket_file, -1);
		if (result) {
			break;
		}
	}
	ao2_iterator_destroy(&it_media_items);
	return result;
}

static char *media_cache_handle_show_item(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT_ROW "\t%20s: %-40.40s\n"
	struct ast_bucket_file *bucket_file;
	struct ao2_iterator it_metadata;
	struct ast_bucket_metadata *metadata;

	switch (cmd) {
	case CLI_INIT:
		e->command = "media cache show";
		e->usage =
			"Usage: media cache show <uri>\n"
			"       Display all information about a particular\n"
			"       item in the media cache.\n";
		return NULL;
	case CLI_GENERATE:
		return cli_complete_uri(a->word, a->n);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	bucket_file = ao2_find(media_cache, a->argv[3], OBJ_SEARCH_KEY);
	if (!bucket_file) {
		ast_cli(a->fd, "Unable to find '%s' in the media cache\n", a->argv[3]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "URI: %s\n", ast_sorcery_object_get_id(bucket_file));
	ast_cli(a->fd, "%s\n", "----------------------------------------");
	ast_cli(a->fd, FORMAT_ROW, "Path", bucket_file->path);

	it_metadata = ao2_iterator_init(bucket_file->metadata, 0);
	while ((metadata = ao2_iterator_next(&it_metadata))) {
		ast_cli(a->fd, FORMAT_ROW, metadata->name, metadata->value);
		ao2_ref(metadata, -1);
	}
	ao2_iterator_destroy(&it_metadata);

	ao2_ref(bucket_file, -1);
#undef FORMAT_ROW
	return CLI_SUCCESS;
}

static char *media_cache_handle_delete_item(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "media cache delete";
		e->usage =
			"Usage: media cache delete <uri>\n"
			"       Delete an item from the media cache.\n"
			"       Note that this will also remove any local\n"
			"       storage of the media associated with the URI,\n"
			"       and will inform the backend supporting the URI\n"
			"       scheme that it should remove the item.\n";
		return NULL;
	case CLI_GENERATE:
		return cli_complete_uri(a->word, a->n);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (ast_media_cache_delete(a->argv[3])) {
		ast_cli(a->fd, "Unable to delete '%s'\n", a->argv[3]);
	} else {
		ast_cli(a->fd, "Deleted '%s' from the media cache\n", a->argv[3]);
	}

	return CLI_SUCCESS;
}

static char *media_cache_handle_refresh_item(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char file_path[PATH_MAX];

	switch (cmd) {
	case CLI_INIT:
		e->command = "media cache refresh";
		e->usage =
			"Usage: media cache refresh <uri>\n"
			"       Ask for a refresh of a particular URI.\n"
			"       If the item does not already exist in the\n"
			"       media cache, the item will be populated from\n"
			"       the backend supporting the URI scheme.\n";
		return NULL;
	case CLI_GENERATE:
		return cli_complete_uri(a->word, a->n);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (ast_media_cache_retrieve(a->argv[3], NULL, file_path, sizeof(file_path))) {
		ast_cli(a->fd, "Unable to refresh '%s'\n", a->argv[3]);
	} else {
		ast_cli(a->fd, "Refreshed '%s' to local storage '%s'\n", a->argv[3], file_path);
	}

	return CLI_SUCCESS;
}

static char *media_cache_handle_create_item(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "media cache create";
		e->usage =
			"Usage: media cache create <uri> <file>\n"
			"       Create an item in the media cache by associating\n"
			"       a local media file with some URI.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (ast_media_cache_create_or_update(a->argv[3], a->argv[4], NULL)) {
		ast_cli(a->fd, "Unable to create '%s' associated with local file '%s'\n",
			a->argv[3], a->argv[4]);
	} else {
		ast_cli(a->fd, "Created '%s' for '%s' in the media cache\n",
			a->argv[3], a->argv[4]);
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_media_cache[] = {
	AST_CLI_DEFINE(media_cache_handle_show_all, "Show all items in the media cache"),
	AST_CLI_DEFINE(media_cache_handle_show_item, "Show a single item in the media cache"),
	AST_CLI_DEFINE(media_cache_handle_delete_item, "Remove an item from the media cache"),
	AST_CLI_DEFINE(media_cache_handle_refresh_item, "Refresh an item in the media cache"),
	AST_CLI_DEFINE(media_cache_handle_create_item, "Create an item in the media cache"),
};

/*!
 * \internal
 * \brief Shutdown the media cache
 */
static void media_cache_shutdown(void)
{
	ao2_cleanup(media_cache);
	media_cache = NULL;

	ast_cli_unregister_multiple(cli_media_cache, ARRAY_LEN(cli_media_cache));
}

int ast_media_cache_init(void)
{
	ast_register_cleanup(media_cache_shutdown);

	media_cache = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, AO2_BUCKETS,
		ast_sorcery_object_id_hash, NULL, ast_sorcery_object_id_compare);
	if (!media_cache) {
		return -1;
	}

	if (ast_cli_register_multiple(cli_media_cache, ARRAY_LEN(cli_media_cache))) {
		return -1;
	}

	media_cache_populate_from_astdb();

	return 0;
}
