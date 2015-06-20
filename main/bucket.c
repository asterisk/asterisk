/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Bucket File API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<use type="external">uriparser</use>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
        <configInfo name="core" language="en_US">
                <synopsis>Bucket file API</synopsis>
                <configFile name="bucket">
                        <configObject name="bucket">
                                <configOption name="scheme">
                                        <synopsis>Scheme in use for bucket</synopsis>
                                </configOption>
                                <configOption name="created">
                                        <synopsis>Time at which the bucket was created</synopsis>
                                </configOption>
                                <configOption name="modified">
                                        <synopsis>Time at which the bucket was last modified</synopsis>
                                </configOption>
                        </configObject>
                        <configObject name="file">
                                <configOption name="scheme">
                                        <synopsis>Scheme in use for file</synopsis>
                                </configOption>
                                <configOption name="created">
                                        <synopsis>Time at which the file was created</synopsis>
                                </configOption>
                                <configOption name="modified">
                                        <synopsis>Time at which the file was last modified</synopsis>
                                </configOption>
                        </configObject>
                </configFile>
        </configInfo>
***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#ifdef HAVE_URIPARSER
#include <uriparser/Uri.h>
#endif

#include "asterisk/logger.h"
#include "asterisk/sorcery.h"
#include "asterisk/bucket.h"
#include "asterisk/config_options.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/json.h"
#include "asterisk/file.h"
#include "asterisk/module.h"

/*! \brief Number of buckets for the container of schemes */
#define SCHEME_BUCKETS 53

/*! \brief Number of buckets for the container of metadata in a file */
#define METADATA_BUCKETS 53

/*! \brief Sorcery instance for all bucket operations */
static struct ast_sorcery *bucket_sorcery;

/*! \brief Container of registered schemes */
static struct ao2_container *schemes;

/*! \brief Structure for available schemes */
struct ast_bucket_scheme {
	/*! \brief Wizard for buckets */
	struct ast_sorcery_wizard *bucket;
	/*! \brief Wizard for files */
	struct ast_sorcery_wizard *file;
	/*! \brief Pointer to the file snapshot creation callback */
	bucket_file_create_cb create;
	/*! \brief Pointer to the file snapshot destruction callback */
	bucket_file_destroy_cb destroy;
	/*! \brief Name of the scheme */
	char name[0];
};

/*! \brief Callback function for creating a bucket */
static int bucket_wizard_create(const struct ast_sorcery *sorcery, void *data, void *object)
{
	struct ast_bucket *bucket = object;

	return bucket->scheme_impl->bucket->create(sorcery, data, object);
}

/*! \brief Callback function for retrieving a bucket */
static void *bucket_wizard_retrieve(const struct ast_sorcery *sorcery, void *data, const char *type,
	const char *id)
{
#ifdef HAVE_URIPARSER
	UriParserStateA state;
	UriUriA uri;
	size_t len;
#else
	char *tmp = ast_strdupa(id);
#endif
	SCOPED_AO2RDLOCK(lock, schemes);
	char *uri_scheme;
	RAII_VAR(struct ast_bucket_scheme *, scheme, NULL, ao2_cleanup);

#ifdef HAVE_URIPARSER
	state.uri = &uri;
	if (uriParseUriA(&state, id) != URI_SUCCESS ||
		!uri.scheme.first || !uri.scheme.afterLast) {
		uriFreeUriMembersA(&uri);
		return NULL;
	}

	len = (uri.scheme.afterLast - uri.scheme.first) + 1;
	uri_scheme = ast_alloca(len);
	ast_copy_string(uri_scheme, uri.scheme.first, len);

	uriFreeUriMembersA(&uri);
#else
	uri_scheme = tmp;
	if (!(tmp = strchr(uri_scheme, ':'))) {
		return NULL;
	}
	*tmp = '\0';
#endif

	scheme = ao2_find(schemes, uri_scheme, OBJ_KEY | OBJ_NOLOCK);

	if (!scheme) {
		return NULL;
	}

	return scheme->bucket->retrieve_id(sorcery, data, type, id);
}

/*! \brief Callback function for deleting a bucket */
static int bucket_wizard_delete(const struct ast_sorcery *sorcery, void *data, void *object)
{
	struct ast_bucket *bucket = object;

	return bucket->scheme_impl->bucket->delete(sorcery, data, object);
}

/*! \brief Intermediary bucket wizard */
static struct ast_sorcery_wizard bucket_wizard = {
	.name = "bucket",
	.create = bucket_wizard_create,
	.retrieve_id = bucket_wizard_retrieve,
	.delete = bucket_wizard_delete,
};

/*! \brief Callback function for creating a bucket file */
static int bucket_file_wizard_create(const struct ast_sorcery *sorcery, void *data, void *object)
{
	struct ast_bucket_file *file = object;

	return file->scheme_impl->file->create(sorcery, data, object);
}

/*! \brief Callback function for retrieving a bucket file */
static void *bucket_file_wizard_retrieve(const struct ast_sorcery *sorcery, void *data, const char *type,
	const char *id)
{
#ifdef HAVE_URIPARSER
	UriParserStateA state;
	UriUriA uri;
	size_t len;
#else
	char *tmp = ast_strdupa(id);
#endif
	char *uri_scheme;
	SCOPED_AO2RDLOCK(lock, schemes);
	RAII_VAR(struct ast_bucket_scheme *, scheme, NULL, ao2_cleanup);

#ifdef HAVE_URIPARSER
	state.uri = &uri;
	if (uriParseUriA(&state, id) != URI_SUCCESS ||
		!uri.scheme.first || !uri.scheme.afterLast) {
		uriFreeUriMembersA(&uri);
		return NULL;
	}

	len = (uri.scheme.afterLast - uri.scheme.first) + 1;
	uri_scheme = ast_alloca(len);
	ast_copy_string(uri_scheme, uri.scheme.first, len);

	uriFreeUriMembersA(&uri);
#else
	uri_scheme = tmp;
	if (!(tmp = strchr(uri_scheme, ':'))) {
		return NULL;
	}
	*tmp = '\0';
#endif

	scheme = ao2_find(schemes, uri_scheme, OBJ_KEY | OBJ_NOLOCK);

	if (!scheme) {
		return NULL;
	}

	return scheme->file->retrieve_id(sorcery, data, type, id);
}

/*! \brief Callback function for updating a bucket file */
static int bucket_file_wizard_update(const struct ast_sorcery *sorcery, void *data, void *object)
{
	struct ast_bucket_file *file = object;

	return file->scheme_impl->file->update(sorcery, data, object);
}

/*! \brief Callback function for deleting a bucket file */
static int bucket_file_wizard_delete(const struct ast_sorcery *sorcery, void *data, void *object)
{
	struct ast_bucket_file *file = object;

	return file->scheme_impl->file->delete(sorcery, data, object);
}

/*! \brief Intermediary file wizard */
static struct ast_sorcery_wizard bucket_file_wizard = {
	.name = "bucket_file",
	.create = bucket_file_wizard_create,
	.retrieve_id = bucket_file_wizard_retrieve,
	.update = bucket_file_wizard_update,
	.delete = bucket_file_wizard_delete,
};

int __ast_bucket_scheme_register(const char *name, struct ast_sorcery_wizard *bucket,
	struct ast_sorcery_wizard *file, bucket_file_create_cb create_cb,
	bucket_file_destroy_cb destroy_cb, struct ast_module *module)
{
	SCOPED_AO2WRLOCK(lock, schemes);
	RAII_VAR(struct ast_bucket_scheme *, scheme, NULL, ao2_cleanup);

	if (ast_strlen_zero(name) || !bucket || !file ||
	    !bucket->create || !bucket->delete || !bucket->retrieve_id ||
	    !create_cb) {
		return -1;
	}

	scheme = ao2_find(schemes, name, OBJ_KEY | OBJ_NOLOCK);
	if (scheme) {
		return -1;
	}

	scheme = ao2_alloc(sizeof(*scheme) + strlen(name) + 1, NULL);
	if (!scheme) {
		return -1;
	}

	strcpy(scheme->name, name);
	scheme->bucket = bucket;
	scheme->file = file;
	scheme->create = create_cb;
	scheme->destroy = destroy_cb;

	ao2_link_flags(schemes, scheme, OBJ_NOLOCK);

	ast_verb(2, "Registered bucket scheme '%s'\n", name);

	ast_module_shutdown_ref(module);

	return 0;
}

/*! \brief Allocator for metadata attributes */
static struct ast_bucket_metadata *bucket_metadata_alloc(const char *name, const char *value)
{
	int name_len = strlen(name) + 1, value_len = strlen(value) + 1;
	struct ast_bucket_metadata *metadata = ao2_alloc(sizeof(*metadata) + name_len + value_len, NULL);
	char *dst;

	if (!metadata) {
		return NULL;
	}

	dst = metadata->data;
	metadata->name = strcpy(dst, name);
	dst += name_len;
	metadata->value = strcpy(dst, value);

	return metadata;
}

int ast_bucket_file_metadata_set(struct ast_bucket_file *file, const char *name, const char *value)
{
	RAII_VAR(struct ast_bucket_metadata *, metadata, bucket_metadata_alloc(name, value), ao2_cleanup);

	if (!metadata) {
		return -1;
	}

	ao2_find(file->metadata, name, OBJ_NODATA | OBJ_UNLINK | OBJ_KEY);
	ao2_link(file->metadata, metadata);

	return 0;
}

int ast_bucket_file_metadata_unset(struct ast_bucket_file *file, const char *name)
{
	RAII_VAR(struct ast_bucket_metadata *, metadata, ao2_find(file->metadata, name, OBJ_UNLINK | OBJ_KEY), ao2_cleanup);

	if (!metadata) {
		return -1;
	}

	return 0;
}

struct ast_bucket_metadata *ast_bucket_file_metadata_get(struct ast_bucket_file *file, const char *name)
{
	return ao2_find(file->metadata, name, OBJ_KEY);
}

/*! \brief Destructor for buckets */
static void bucket_destroy(void *obj)
{
	struct ast_bucket *bucket = obj;

	ao2_cleanup(bucket->scheme_impl);
	ast_string_field_free_memory(bucket);
	ao2_cleanup(bucket->buckets);
	ao2_cleanup(bucket->files);
}

/*! \brief Sorting function for red black tree string container */
static int bucket_rbtree_str_sort_cmp(const void *obj_left, const void *obj_right, int flags)
{
	const char *str_left = obj_left;
	const char *str_right = obj_right;
	int cmp = 0;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
	case OBJ_KEY:
		cmp = strcmp(str_left, str_right);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncmp(str_left, str_right, strlen(str_right));
		break;
	}
	return cmp;
}

/*! \brief Allocator for buckets */
static void *bucket_alloc(const char *name)
{
	RAII_VAR(struct ast_bucket *, bucket, NULL, ao2_cleanup);

	bucket = ast_sorcery_generic_alloc(sizeof(*bucket), bucket_destroy);
	if (!bucket) {
		return NULL;
	}

	if (ast_string_field_init(bucket, 128)) {
		return NULL;
	}

	bucket->buckets = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, bucket_rbtree_str_sort_cmp, NULL);
	if (!bucket->buckets) {
		return NULL;
	}

	bucket->files = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, bucket_rbtree_str_sort_cmp, NULL);
	if (!bucket->files) {
		return NULL;
	}

	ao2_ref(bucket, +1);
	return bucket;
}

struct ast_bucket *ast_bucket_alloc(const char *uri)
{
#ifdef HAVE_URIPARSER
	UriParserStateA state;
	UriUriA full_uri;
	size_t len;
#else
	char *tmp = ast_strdupa(uri);
#endif
	char *uri_scheme;
	RAII_VAR(struct ast_bucket_scheme *, scheme, NULL, ao2_cleanup);
	struct ast_bucket *bucket;

	if (ast_strlen_zero(uri)) {
		return NULL;
	}

#ifdef HAVE_URIPARSER
	state.uri = &full_uri;
	if (uriParseUriA(&state, uri) != URI_SUCCESS ||
		!full_uri.scheme.first || !full_uri.scheme.afterLast ||
		!full_uri.pathTail) {
		uriFreeUriMembersA(&full_uri);
		return NULL;
	}

	len = (full_uri.scheme.afterLast - full_uri.scheme.first) + 1;
	uri_scheme = ast_alloca(len);
	ast_copy_string(uri_scheme, full_uri.scheme.first, len);

	uriFreeUriMembersA(&full_uri);
#else
	uri_scheme = tmp;
	if (!(tmp = strchr(uri_scheme, ':'))) {
		return NULL;
	}
	*tmp = '\0';
#endif

	scheme = ao2_find(schemes, uri_scheme, OBJ_KEY);
	if (!scheme) {
		return NULL;
	}

	bucket = ast_sorcery_alloc(bucket_sorcery, "bucket", uri);
	if (!bucket) {
		return NULL;
	}

	ao2_ref(scheme, +1);
	bucket->scheme_impl = scheme;

	ast_string_field_set(bucket, scheme, uri_scheme);

	return bucket;
}

int ast_bucket_create(struct ast_bucket *bucket)
{
	return ast_sorcery_create(bucket_sorcery, bucket);
}

/*!
 * \internal
 * \brief Sorcery object type copy handler for \c ast_bucket
 */
static int bucket_copy_handler(const void *src, void *dst)
{
	const struct ast_bucket *src_bucket = src;
	struct ast_bucket *dst_bucket = dst;

	dst_bucket->scheme_impl = ao2_bump(src_bucket->scheme_impl);
	ast_string_field_set(dst_bucket, scheme, src_bucket->scheme);
	dst_bucket->created = src_bucket->created;
	dst_bucket->modified = src_bucket->modified;

	return 0;
}

struct ast_bucket *ast_bucket_clone(struct ast_bucket *bucket)
{
	return ast_sorcery_copy(bucket_sorcery, bucket);
}

struct ast_bucket *ast_bucket_retrieve(const char *uri)
{
	if (ast_strlen_zero(uri)) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(bucket_sorcery, "bucket", uri);
}

int ast_bucket_observer_add(const struct ast_sorcery_observer *callbacks)
{
	return ast_sorcery_observer_add(bucket_sorcery, "bucket", callbacks);
}

void ast_bucket_observer_remove(const struct ast_sorcery_observer *callbacks)
{
	ast_sorcery_observer_remove(bucket_sorcery, "bucket", callbacks);
}

int ast_bucket_delete(struct ast_bucket *bucket)
{
	return ast_sorcery_delete(bucket_sorcery, bucket);
}

struct ast_json *ast_bucket_json(const struct ast_bucket *bucket)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ast_json *id, *files, *buckets;
	struct ao2_iterator i;
	char *uri;
	int res = 0;

	json = ast_sorcery_objectset_json_create(bucket_sorcery, bucket);
	if (!json) {
		return NULL;
	}

	id = ast_json_string_create(ast_sorcery_object_get_id(bucket));
	if (!id) {
		return NULL;
	}

	if (ast_json_object_set(json, "id", id)) {
		return NULL;
	}

	buckets = ast_json_array_create();
	if (!buckets) {
		return NULL;
	}

	if (ast_json_object_set(json, "buckets", buckets)) {
		return NULL;
	}

	i = ao2_iterator_init(bucket->buckets, 0);
	for (; (uri = ao2_iterator_next(&i)); ao2_ref(uri, -1)) {
		struct ast_json *bucket_uri = ast_json_string_create(uri);

		if (!bucket_uri || ast_json_array_append(buckets, bucket_uri)) {
			res = -1;
			ao2_ref(uri, -1);
			break;
		}
	}
	ao2_iterator_destroy(&i);

	if (res) {
		return NULL;
	}

	files = ast_json_array_create();
	if (!files) {
		return NULL;
	}

	if (ast_json_object_set(json, "files", files)) {
		return NULL;
	}

	i = ao2_iterator_init(bucket->files, 0);
	for (; (uri = ao2_iterator_next(&i)); ao2_ref(uri, -1)) {
		struct ast_json *file_uri = ast_json_string_create(uri);

		if (!file_uri || ast_json_array_append(files, file_uri)) {
			res = -1;
			ao2_ref(uri, -1);
			break;
		}
	}
	ao2_iterator_destroy(&i);

	if (res) {
		return NULL;
	}

	ast_json_ref(json);
	return json;
}

/*! \brief Hashing function for file metadata */
static int bucket_file_metadata_hash(const void *obj, const int flags)
{
	const struct ast_bucket_metadata *object;
	const char *key;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key = obj;
		return ast_str_hash(key);
	case OBJ_POINTER:
		object = obj;
		return ast_str_hash(object->name);
	default:
		/* Hash can only work on something with a full key */
		ast_assert(0);
		return 0;
	}
}

/*! \brief Comparison function for file metadata */
static int bucket_file_metadata_cmp(void *obj, void *arg, int flags)
{
	struct ast_bucket_metadata *metadata1 = obj, *metadata2 = arg;
	const char *name = arg;

	return !strcmp(metadata1->name, flags & OBJ_KEY ? name : metadata2->name) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Destructor for bucket files */
static void bucket_file_destroy(void *obj)
{
	struct ast_bucket_file *file = obj;

	if (file->scheme_impl->destroy) {
		file->scheme_impl->destroy(file);
	}

	ao2_cleanup(file->scheme_impl);
	ao2_cleanup(file->metadata);
}

/*! \brief Allocator for bucket files */
static void *bucket_file_alloc(const char *name)
{
	RAII_VAR(struct ast_bucket_file *, file, NULL, ao2_cleanup);

	file = ast_sorcery_generic_alloc(sizeof(*file), bucket_file_destroy);
	if (!file) {
		return NULL;
	}

	if (ast_string_field_init(file, 128)) {
		return NULL;
	}

	file->metadata = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, METADATA_BUCKETS,
		bucket_file_metadata_hash, bucket_file_metadata_cmp);
	if (!file->metadata) {
		return NULL;
	}

	ao2_ref(file, +1);
	return file;
}

struct ast_bucket_file *ast_bucket_file_alloc(const char *uri)
{
#ifdef HAVE_URIPARSER
	UriParserStateA state;
	UriUriA full_uri;
	size_t len;
#else
	char *tmp = ast_strdupa(uri);
#endif
	char *uri_scheme;
	RAII_VAR(struct ast_bucket_scheme *, scheme, NULL, ao2_cleanup);
	struct ast_bucket_file *file;

	if (ast_strlen_zero(uri)) {
		return NULL;
	}

#ifdef HAVE_URIPARSER
	state.uri = &full_uri;
	if (uriParseUriA(&state, uri) != URI_SUCCESS ||
		!full_uri.scheme.first || !full_uri.scheme.afterLast ||
		!full_uri.pathTail) {
		uriFreeUriMembersA(&full_uri);
		return NULL;
	}

	len = (full_uri.scheme.afterLast - full_uri.scheme.first) + 1;
	uri_scheme = ast_alloca(len);
	ast_copy_string(uri_scheme, full_uri.scheme.first, len);

	uriFreeUriMembersA(&full_uri);
#else
	uri_scheme = tmp;
	if (!(tmp = strchr(uri_scheme, ':'))) {
		return NULL;
	}
	*tmp = '\0';
#endif

	scheme = ao2_find(schemes, uri_scheme, OBJ_KEY);
	if (!scheme) {
		return NULL;
	}

	file = ast_sorcery_alloc(bucket_sorcery, "file", uri);
	if (!file) {
		return NULL;
	}

	ao2_ref(scheme, +1);
	file->scheme_impl = scheme;

	ast_string_field_set(file, scheme, uri_scheme);

	if (scheme->create(file)) {
		ao2_ref(file, -1);
		return NULL;
	}

	return file;
}

int ast_bucket_file_create(struct ast_bucket_file *file)
{
	return ast_sorcery_create(bucket_sorcery, file);
}

/*! \brief Copy a file, shamelessly taken from file.c */
static int bucket_copy(const char *infile, const char *outfile)
{
	int ifd, ofd, len;
	char buf[4096];	/* XXX make it lerger. */

	if ((ifd = open(infile, O_RDONLY)) < 0) {
		ast_log(LOG_WARNING, "Unable to open %s in read-only mode, error: %s\n", infile, strerror(errno));
		return -1;
	}
	if ((ofd = open(outfile, O_WRONLY | O_TRUNC | O_CREAT, AST_FILE_MODE)) < 0) {
		ast_log(LOG_WARNING, "Unable to open %s in write-only mode, error: %s\n", outfile, strerror(errno));
		close(ifd);
		return -1;
	}
	while ( (len = read(ifd, buf, sizeof(buf)) ) ) {
		int res;
		if (len < 0) {
			ast_log(LOG_WARNING, "Read failed on %s: %s\n", infile, strerror(errno));
			break;
		}
		/* XXX handle partial writes */
		res = write(ofd, buf, len);
		if (res != len) {
			ast_log(LOG_WARNING, "Write failed on %s (%d of %d): %s\n", outfile, res, len, strerror(errno));
			len = -1; /* error marker */
			break;
		}
	}
	close(ifd);
	close(ofd);
	if (len < 0) {
		unlink(outfile);
		return -1; /* error */
	}
	return 0;	/* success */
}

/*!
 * \internal
 * \brief Sorcery object type copy handler for \c ast_bucket_file
 */
static int bucket_file_copy_handler(const void *src, void *dst)
{
	const struct ast_bucket_file *src_file = src;
	struct ast_bucket_file *dst_file = dst;

	dst_file->scheme_impl = ao2_bump(src_file->scheme_impl);
	ast_string_field_set(dst_file, scheme, src_file->scheme);
	dst_file->created = src_file->created;
	dst_file->modified = src_file->modified;
	strcpy(dst_file->path, src_file->path); /* safe */

	dst_file->metadata = ao2_container_clone(src_file->metadata, 0);
	if (!dst_file->metadata) {
		return -1;
	}

	return 0;
}

struct ast_bucket_file *ast_bucket_file_copy(struct ast_bucket_file *file, const char *uri)
{
	RAII_VAR(struct ast_bucket_file *, copy, ast_bucket_file_alloc(uri), ao2_cleanup);

	if (!copy) {
		return NULL;
	}

	ao2_cleanup(copy->metadata);
	copy->metadata = ao2_container_clone(file->metadata, 0);
	if (!copy->metadata ||
		bucket_copy(file->path, copy->path)) {
		return NULL;
	}

	ao2_ref(copy, +1);
	return copy;
}

struct ast_bucket_file *ast_bucket_file_clone(struct ast_bucket_file *file)
{
	return ast_sorcery_copy(bucket_sorcery, file);
}

struct ast_bucket_file *ast_bucket_file_retrieve(const char *uri)
{
	if (ast_strlen_zero(uri)) {
		return NULL;
	}

	return ast_sorcery_retrieve_by_id(bucket_sorcery, "file", uri);
}

int ast_bucket_file_observer_add(const struct ast_sorcery_observer *callbacks)
{
	return ast_sorcery_observer_add(bucket_sorcery, "file", callbacks);
}

void ast_bucket_file_observer_remove(const struct ast_sorcery_observer *callbacks)
{
	ast_sorcery_observer_remove(bucket_sorcery, "file", callbacks);
}

int ast_bucket_file_update(struct ast_bucket_file *file)
{
	return ast_sorcery_update(bucket_sorcery, file);
}

int ast_bucket_file_delete(struct ast_bucket_file *file)
{
	return ast_sorcery_delete(bucket_sorcery, file);
}

struct ast_json *ast_bucket_file_json(const struct ast_bucket_file *file)
{
	RAII_VAR(struct ast_json *, json, NULL, ast_json_unref);
	struct ast_json *id, *metadata;
	struct ao2_iterator i;
	struct ast_bucket_metadata *attribute;
	int res = 0;

	json = ast_sorcery_objectset_json_create(bucket_sorcery, file);
	if (!json) {
		return NULL;
	}

	id = ast_json_string_create(ast_sorcery_object_get_id(file));
	if (!id) {
		return NULL;
	}

	if (ast_json_object_set(json, "id", id)) {
		return NULL;
	}

	metadata = ast_json_object_create();
	if (!metadata) {
		return NULL;
	}

	if (ast_json_object_set(json, "metadata", metadata)) {
		return NULL;
	}

	i = ao2_iterator_init(file->metadata, 0);
	for (; (attribute = ao2_iterator_next(&i)); ao2_ref(attribute, -1)) {
		struct ast_json *value = ast_json_string_create(attribute->value);

		if (!value || ast_json_object_set(metadata, attribute->name, value)) {
			res = -1;
			break;
		}
	}
	ao2_iterator_destroy(&i);

	if (res) {
		return NULL;
	}

	ast_json_ref(json);
	return json;
}

int ast_bucket_file_temporary_create(struct ast_bucket_file *file)
{
	int fd;

	ast_copy_string(file->path, "/tmp/bucket-XXXXXX", sizeof(file->path));

	fd = mkstemp(file->path);
	if (fd < 0) {
		return -1;
	}

	close(fd);
	return 0;
}

void ast_bucket_file_temporary_destroy(struct ast_bucket_file *file)
{
	if (!ast_strlen_zero(file->path)) {
		unlink(file->path);
	}
}

/*! \brief Hashing function for scheme container */
static int bucket_scheme_hash(const void *obj, const int flags)
{
	const struct ast_bucket_scheme *object;
	const char *key;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	case OBJ_KEY:
		key = obj;
		return ast_str_hash(key);
	case OBJ_POINTER:
		object = obj;
		return ast_str_hash(object->name);
	default:
		/* Hash can only work on something with a full key */
		ast_assert(0);
		return 0;
	}
}

/*! \brief Comparison function for scheme container */
static int bucket_scheme_cmp(void *obj, void *arg, int flags)
{
	struct ast_bucket_scheme *scheme1 = obj, *scheme2 = arg;
	const char *name = arg;

	return !strcmp(scheme1->name, flags & OBJ_KEY ? name : scheme2->name) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Cleanup function for graceful shutdowns */
static void bucket_cleanup(void)
{
	ast_sorcery_unref(bucket_sorcery);
	bucket_sorcery = NULL;

	ast_sorcery_wizard_unregister(&bucket_wizard);
	ast_sorcery_wizard_unregister(&bucket_file_wizard);

	ao2_cleanup(schemes);
}

/*! \brief Custom handler for translating from a string timeval to actual structure */
static int timeval_str2struct(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct timeval *field = (struct timeval *)(obj + aco_option_get_argument(opt, 0));
	return ast_get_timeval(var->value, field, ast_tv(0, 0), NULL);
}

/*! \brief Custom handler for translating from an actual structure timeval to string */
static int timeval_struct2str(const void *obj, const intptr_t *args, char **buf)
{
	struct timeval *field = (struct timeval *)(obj + args[0]);
	return (ast_asprintf(buf, "%lu.%06lu", (unsigned long)field->tv_sec, (unsigned long)field->tv_usec) < 0) ? -1 : 0;
}

/*! \brief Initialize bucket support */
int ast_bucket_init(void)
{
	ast_register_cleanup(&bucket_cleanup);

	schemes = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_RWLOCK, SCHEME_BUCKETS, bucket_scheme_hash,
		bucket_scheme_cmp);
	if (!schemes) {
		ast_log(LOG_ERROR, "Failed to create container for Bucket schemes\n");
		return -1;
	}

	if (__ast_sorcery_wizard_register(&bucket_wizard, NULL)) {
		ast_log(LOG_ERROR, "Failed to register sorcery wizard for 'bucket' intermediary\n");
		return -1;
	}

	if (__ast_sorcery_wizard_register(&bucket_file_wizard, NULL)) {
		ast_log(LOG_ERROR, "Failed to register sorcery wizard for 'file' intermediary\n");
		return -1;
	}

	if (!(bucket_sorcery = ast_sorcery_open())) {
		ast_log(LOG_ERROR, "Failed to create sorcery instance for Bucket support\n");
		return -1;
	}

	if (ast_sorcery_apply_default(bucket_sorcery, "bucket", "bucket", NULL) == AST_SORCERY_APPLY_FAIL) {
		ast_log(LOG_ERROR, "Failed to apply intermediary for 'bucket' object type in Bucket sorcery\n");
		return -1;
	}

	if (ast_sorcery_object_register(bucket_sorcery, "bucket", bucket_alloc, NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register 'bucket' object type in Bucket sorcery\n");
		return -1;
	}

	ast_sorcery_object_field_register(bucket_sorcery, "bucket", "scheme", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_bucket, scheme));
	ast_sorcery_object_field_register_custom(bucket_sorcery, "bucket", "created", "", timeval_str2struct, timeval_struct2str, NULL, 0, FLDSET(struct ast_bucket, created));
	ast_sorcery_object_field_register_custom(bucket_sorcery, "bucket", "modified", "", timeval_str2struct, timeval_struct2str, NULL, 0, FLDSET(struct ast_bucket, modified));
	ast_sorcery_object_set_copy_handler(bucket_sorcery, "bucket", bucket_copy_handler);

	if (ast_sorcery_apply_default(bucket_sorcery, "file", "bucket_file", NULL) == AST_SORCERY_APPLY_FAIL) {
		ast_log(LOG_ERROR, "Failed to apply intermediary for 'file' object type in Bucket sorcery\n");
		return -1;
	}

	if (ast_sorcery_object_register(bucket_sorcery, "file", bucket_file_alloc, NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to register 'file' object type in Bucket sorcery\n");
		return -1;
	}

	ast_sorcery_object_field_register(bucket_sorcery, "file", "scheme", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_bucket_file, scheme));
	ast_sorcery_object_field_register_custom(bucket_sorcery, "file", "created", "", timeval_str2struct, timeval_struct2str, NULL, 0, FLDSET(struct ast_bucket_file, created));
	ast_sorcery_object_field_register_custom(bucket_sorcery, "file", "modified", "", timeval_str2struct, timeval_struct2str, NULL, 0, FLDSET(struct ast_bucket_file, modified));
	ast_sorcery_object_set_copy_handler(bucket_sorcery, "file", bucket_file_copy_handler);

	return 0;
}
