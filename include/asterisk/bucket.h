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
 * \brief Bucket File API
 * \author Joshua Colp <jcolp@digium.com>
 * \ref AstBucket
 */

/*!
 * \page bucket AstBucket Bucket File API
 *
 * Bucket is an API which provides directory and file access in a generic fashion. It is
 * implemented as a thin wrapper over the sorcery data access layer API and is written in
 * a pluggable fashion to allow different backend storage mechanisms.
 *
 */

#ifndef _ASTERISK_BUCKET_H
#define _ASTERISK_BUCKET_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/sorcery.h"

/*! \brief Opaque structure for internal details about a scheme */
struct ast_bucket_scheme;

/*! \brief Bucket metadata structure, AO2 key value pair */
struct ast_bucket_metadata {
	/*! \brief Name of the attribute */
	const char *name;
	/*! \brief Value of the attribute */
	const char *value;
	/*! \brief Storage for the above name and value */
	char data[0];
};

/*! \brief Bucket structure, contains other buckets and files */
struct ast_bucket {
	/*! \brief Sorcery object information */
	SORCERY_OBJECT(details);
	/*! \brief Scheme implementation in use */
	struct ast_bucket_scheme *scheme_impl;
	/*! \brief Stringfields */
	AST_DECLARE_STRING_FIELDS(
		/*! \brief Name of scheme in use */
		AST_STRING_FIELD(scheme);
	);
	/*! \brief When this bucket was created */
	struct timeval created;
	/*! \brief When this bucket was last modified */
	struct timeval modified;
	/*! \brief Container of string URIs of buckets within this bucket */
	struct ao2_container *buckets;
	/*! \brief Container of string URIs of files within this bucket */
	struct ao2_container *files;
};

/*! \brief Bucket file structure, contains reference to file and information about it */
struct ast_bucket_file {
	/*! \brief Sorcery object information */
	SORCERY_OBJECT(details);
	/*! \brief Scheme implementation in use */
	struct ast_bucket_scheme *scheme_impl;
	/*! \brief Stringfields */
	AST_DECLARE_STRING_FIELDS(
		/*! \brief Name of scheme in use */
		AST_STRING_FIELD(scheme);
	);
	/*! \brief When this file was created */
	struct timeval created;
	/*! \brief When this file was last modified */
	struct timeval modified;
	/*! \brief Container of metadata attributes about file */
	struct ao2_container *metadata;
	/*! \brief Local path to this file */
	char path[PATH_MAX];
};

/*!
 * \brief A callback function invoked when creating a file snapshot
 *
 * \param file Pointer to the file snapshot
 *
 * \retval 0 success
 * \retval -1 failure
 */
typedef int (*bucket_file_create_cb)(struct ast_bucket_file *file);

/*!
 * \brief A callback function invoked when destroying a file snapshot
 *
 * \param file Pointer to the file snapshot
 */
typedef void (*bucket_file_destroy_cb)(struct ast_bucket_file *file);

/*!
 * \brief Initialize bucket support
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_bucket_init(void);

/*!
 * \brief Register support for a specific scheme
 *
 * \param name Name of the scheme, used to find based on scheme in URIs
 * \param bucket Sorcery wizard used for buckets
 * \param file Sorcery wizard used for files
 * \param create_cb Required file snapshot creation callback
 * \param destroy_cb Optional file snapshot destruction callback
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note Once a scheme has been registered it can not be unregistered
 */
#define ast_bucket_scheme_register(name, bucket, file, create_cb, destroy_cb) __ast_bucket_scheme_register(name, bucket, file, create_cb, destroy_cb, AST_MODULE_SELF)

/*!
 * \brief Register support for a specific scheme
 *
 * \param name Name of the scheme, used to find based on scheme in URIs
 * \param bucket Sorcery wizard used for buckets
 * \param file Sorcery wizard used for files
 * \param create_cb Required file snapshot creation callback
 * \param destroy_cb Optional file snapshot destruction callback
 * \param module The module which implements this scheme
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note Once a scheme has been registered it can not be unregistered
 */
int __ast_bucket_scheme_register(const char *name, struct ast_sorcery_wizard *bucket,
	struct ast_sorcery_wizard *file, bucket_file_create_cb create_cb,
	bucket_file_destroy_cb destroy_cb, struct ast_module *module);

/*!
 * \brief Set a metadata attribute on a file to a specific value
 *
 * \param file The bucket file
 * \param name Name of the attribute
 * \param value Value of the attribute
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note This function will overwrite an existing attribute of the same name, unless an error
 * occurs. If an error occurs the existing attribute is left alone.
 */
int ast_bucket_file_metadata_set(struct ast_bucket_file *file, const char *name, const char *value);

/*!
 * \brief Unset a specific metadata attribute on a file
 *
 * \param file The bucket file
 * \param name Name of the attribute
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_bucket_file_metadata_unset(struct ast_bucket_file *file, const char *name);

/*!
 * \brief Retrieve a metadata attribute from a file
 *
 * \param file The bucket file
 * \param name Name of the attribute
 *
 * \retval non-NULL if found
 * \retval NULL if not found
 *
 * \note The object is returned with reference count increased
 */
struct ast_bucket_metadata *ast_bucket_file_metadata_get(struct ast_bucket_file *file, const char *name);

/*!
 * \brief Execute a callback function on the metadata associated with a file
 * \since 14.0.0
 *
 * \param file The bucket file
 * \param cb An ao2 callback function that will be called with each \c ast_bucket_metadata
 *           associated with \c file
 * \param arg An optional argument to pass to \c cb
 */
void ast_bucket_file_metadata_callback(struct ast_bucket_file *file, ao2_callback_fn cb, void *arg);

/*!
 * \brief Allocate a new bucket
 *
 * \param uri Complete URI for the bucket
 *
 * \param non-NULL success
 * \param NULL failure
 *
 * \note This only creates a local bucket object, to persist in backend storage you must call
 * ast_bucket_create
 */
struct ast_bucket *ast_bucket_alloc(const char *uri);

/*!
 * \brief Create a new bucket in backend storage
 *
 * \param bucket The bucket
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_bucket_create(struct ast_bucket *bucket);

/*!
 * \brief Clone a bucket
 *
 * This will create a copy of the passed in \c ast_bucket structure. While
 * all properties of the \c ast_bucket structure are copied, any metadata
 * in the original structure simply has its reference count increased.
 *
 * \param file The bucket to clone
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note This operation should be called prior to updating a bucket
 * object, as \c ast_bucket instances are immutable
 */
struct ast_bucket *ast_bucket_clone(struct ast_bucket *bucket);

/*!
 * \brief Delete a bucket from backend storage
 *
 * \param bucket The bucket
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_bucket_delete(struct ast_bucket *bucket);

/*!
 * \brief Retrieve information about a bucket
 *
 * \param uri Complete URI of the bucket
 *
 * \retval non-NULL if found
 * \retval NULL if not found
 *
 * \note The object is returned with reference count increased
 */
struct ast_bucket *ast_bucket_retrieve(const char *uri);

/*!
 * \brief Retrieve whether or not the backing datastore views the bucket as stale
 * \since 14.0.0
 *
 * This function will ask whatever data storage backs the bucket's schema
 * type if the current instance of the object is stale. It will not
 * update the bucket object itself, as said objects are immutable. If the
 * caller of this function would like to update the object, it should perform
 * a retrieve operation.
 *
 * \param bucket The bucket object to check
 *
 * \retval 0 if \c bucket is not stale
 * \retval 1 if \c bucket is stale
 */
int ast_bucket_is_stale(struct ast_bucket *bucket);

/*!
 * \brief Add an observer for bucket creation and deletion operations
 *
 * \param callbacks Implementation of the sorcery observer interface
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note You must be ready to accept observer invocations before this function is called
 */
int ast_bucket_observer_add(const struct ast_sorcery_observer *callbacks);

/*!
 * \brief Remove an observer from bucket creation and deletion
 *
 * \param callbacks Implementation of the sorcery observer interface
 */
void ast_bucket_observer_remove(const struct ast_sorcery_observer *callbacks);

/*!
 * \brief Get a JSON representation of a bucket
 *
 * \param bucket The specific bucket
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The returned ast_json object must be unreferenced using ast_json_unref
 */
struct ast_json *ast_bucket_json(const struct ast_bucket *bucket);

/*!
 * \brief Allocate a new bucket file
 *
 * \param uri Complete URI for the bucket file
 *
 * \param non-NULL success
 * \param NULL failure
 *
 * \note This only creates a local bucket file object, to persist in backend storage you must call
 * ast_bucket_file_create
 */
struct ast_bucket_file *ast_bucket_file_alloc(const char *uri);

/*!
 * \brief Create a new bucket file in backend storage
 *
 * \param file The bucket file
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_bucket_file_create(struct ast_bucket_file *file);

/*!
 * \brief Copy a bucket file to a new URI
 *
 * \param file The source bucket file
 * \param uri The new URI
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note This operation stages things locally, you must call ast_bucket_file_create on the file
 * that is returned to commit the copy to backend storage
 *
 */
struct ast_bucket_file *ast_bucket_file_copy(struct ast_bucket_file *file, const char *uri);

/*!
 * \brief Clone a bucket file
 *
 * This will create a copy of the passed in \c ast_bucket_file structure. While
 * all properties of the \c ast_bucket_file structure are copied, any metadata
 * in the original structure simply has its reference count increased. Note that
 * this copies the structure, not the underlying file.
 *
 * \param file The bucket file to clone
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note This operation should be called prior to updating a bucket file
 * object, as \c ast_bucket_file instances are immutable
 */
struct ast_bucket_file *ast_bucket_file_clone(struct ast_bucket_file *file);

/*!
 * \brief Update an existing bucket file in backend storage
 *
 * \param file The bucket file
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note This operation will update both the actual content of the file and the metadata associated with it
 */
int ast_bucket_file_update(struct ast_bucket_file *file);

/*!
 * \brief Delete a bucket file from backend storage
 *
 * \param file The bucket file
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_bucket_file_delete(struct ast_bucket_file *file);

/*!
 * \brief Retrieve a bucket file
 *
 * \param uri Complete URI of the bucket file
 *
 * \retval non-NULL if found
 * \retval NULL if not found
 *
 * \note The object is returned with reference count increased
 */
struct ast_bucket_file *ast_bucket_file_retrieve(const char *uri);

/*!
 * \brief Retrieve whether or not the backing datastore views the bucket file as stale
 * \since 14.0.0
 *
 * This function will ask whatever data storage backs the bucket file's schema
 * type if the current instance of the object is stale. It will not
 * update the bucket file object itself, as said objects are immutable. If the
 * caller of this function would like to update the object, it should perform
 * a retrieve operation.
 *
 * \param bucket_file The bucket file object to check
 *
 * \retval 0 if \c bucket_file is not stale
 * \retval 1 if \c bucket_file is stale
 */
int ast_bucket_file_is_stale(struct ast_bucket_file *file);

/*!
 * \brief Add an observer for bucket file creation and deletion operations
 *
 * \param callbacks Implementation of the sorcery observer interface
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note You must be ready to accept observer invocations before this function is called
 */
int ast_bucket_file_observer_add(const struct ast_sorcery_observer *callbacks);

/*!
 * \brief Remove an observer from bucket file creation and deletion
 *
 * \param callbacks Implementation of the sorcery observer interface
 */
void ast_bucket_file_observer_remove(const struct ast_sorcery_observer *callbacks);

/*!
 * \brief Get a JSON representation of a bucket file
 *
 * \param file The specific bucket file
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The returned ast_json object must be unreferenced using ast_json_unref
 */
struct ast_json *ast_bucket_file_json(const struct ast_bucket_file *file);

/*!
 * \brief Common file snapshot creation callback for creating a temporary file
 *
 * \param file Pointer to the file snapshot
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_bucket_file_temporary_create(struct ast_bucket_file *file);

/*!
 * \brief Common file snapshot destruction callback for deleting a temporary file
 *
 * \param file Pointer to the file snapshot
 */
void ast_bucket_file_temporary_destroy(struct ast_bucket_file *file);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_BUCKET_H */
