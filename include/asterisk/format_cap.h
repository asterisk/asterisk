/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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

/*!
 * \file
 * \brief Format Capability API
 *
 * \author David Vossel <dvossel@digium.com>
 */

#ifndef _AST_FORMATCAP_H_
#define _AST_FORMATCAP_H_

/*! Capabilities are represented by an opaque structure statically defined in format_capability.c */
struct ast_format_cap;

/*!
 * \brief Allocate a new ast_format_cap structure.
 *
 * \note Allocation of this object assumes locking
 * is already occuring and that the point of contention
 * is above this capabilities structure.  For example,
 * a tech_pvt object referencing a capabilities structure
 * can use this function as long as it always holds the
 * tech_pvt lock while accessing its capabilities.
 *
 * \retval ast_format_cap object on success.
 * \retval NULL on failure.
 */
struct ast_format_cap *ast_format_cap_alloc_nolock(void);

/*!
 * \brief Allocate a new ast_format_cap structure with locking
 *
 * \note If no other form of locking is taking place, use this function.
 * This function makes most sense for globally accessible capabilities structures
 * that have no other means of locking.
 *
 * \retval ast_format_cap object on success.
 * \retval NULL on failure.
 */
struct ast_format_cap *ast_format_cap_alloc(void);

/*!
 * \brief Destroy an ast_format_cap structure.
 *
 * \return NULL
 */
void *ast_format_cap_destroy(struct ast_format_cap *cap);

/*!
 * \brief Add format capability to capabilities structure.
 *
 * \note A copy of the input format is made and that copy is
 * what is placed in the ast_format_cap structure.  The actual
 * input format ptr is not stored.
 */
void ast_format_cap_add(struct ast_format_cap *cap, const struct ast_format *format);

/*!
 * \brief Add all formats Asterisk knows about for a specific type to
 * the capabilities structure.  Formats with attributes are set, but their
 * attributes are initilized to 0's.  An attribute structure of 0's should
 * indicate to the format attribute interface that the format has full
 * capabilities.
 *
 * \note A copy of the input format is made and that copy is
 * what is placed in the ast_format_cap structure.  The actual
 * input format ptr is not stored.
 */
void ast_format_cap_add_all_by_type(struct ast_format_cap *cap, enum ast_format_type type);

/*!
 * \brief Add all known formats to the capabilities structure using default format attribute. */
void ast_format_cap_add_all(struct ast_format_cap *cap);

/*!
 * \brief Append the formats in src to dst
 */
void ast_format_cap_append(struct ast_format_cap *dst, const struct ast_format_cap *src);

/*!
 * \brief Copy all items in src to dst.
 * \note any items in dst will be removed before copying
 */
void ast_format_cap_copy(struct ast_format_cap *dst, const struct ast_format_cap *src);

/*!
 * \brief create a deep copy of an ast_format_cap structure
 *
 * \retval cap on success
 * \retval NULL on failure
 */
struct ast_format_cap *ast_format_cap_dup(const struct ast_format_cap *src);

/*!
 * \brief determine if a capabilities structure is empty or not
 *
 * \retval 1, true is empty
 * \retval 0, false, not empty
 */
int ast_format_cap_is_empty(const struct ast_format_cap *cap);

/*!
 * \brief Remove format capability from capability structure.
 *
 * \Note format must match Exactly to format in ast_format_cap object in order
 * to be removed.
 *
 * \retval 0, remove was successful
 * \retval -1, remove failed. Could not find format to remove
 */
int ast_format_cap_remove(struct ast_format_cap *cap, struct ast_format *format);

/*!
 * \brief Remove all format capabilities from capability
 * structure for a specific format id.
 *
 * \Note This will remove _ALL_ formats matching the format id from the
 * capabilities structure.
 *
 * \retval 0, remove was successful
 * \retval -1, remove failed. Could not find formats to remove
 */
int ast_format_cap_remove_byid(struct ast_format_cap *cap, enum ast_format_id id);

/*!
 * \brief Remove all formats matching a specific format type.
 */
void ast_format_cap_remove_bytype(struct ast_format_cap *cap, enum ast_format_type type);

/*!
 * \brief Remove all format capabilities from capability structure
 */
void ast_format_cap_remove_all(struct ast_format_cap *cap);

/*!
 * \brief Remove all previous formats and set a single new format.
 */
void ast_format_cap_set(struct ast_format_cap *cap, struct ast_format *format);

/*!
 * \brief Find if input ast_format is within the capabilities of the ast_format_cap object
 * then return the compatible format from the capabilities structure in the result.
 *
 * \retval 1 format is compatible with formats held in ast_format_cap object.
 * \retval 0 format is not compatible with any formats in ast_format_cap object.
 */
int ast_format_cap_get_compatible_format(const struct ast_format_cap *cap, const struct ast_format *format, struct ast_format *result);

/*!
 * \brief Find if ast_format is within the capabilities of the ast_format_cap object.
 *
 * retval 1 format is compatible with formats held in ast_format_cap object.
 * retval 0 format is not compatible with any formats in ast_format_cap object.
 */
int ast_format_cap_iscompatible(const struct ast_format_cap *cap, const struct ast_format *format);

/*!
 * \brief Finds the best quality audio format for a given format id and returns it in result.
 *
 * \retval 1 format found and set to result structure.
 * \retval 0 no format found, result structure is cleared.
 */
int ast_format_cap_best_byid(const struct ast_format_cap *cap, enum ast_format_id, struct ast_format *result);

/*!
 * \brief is cap1 identical to cap2
 *
 * retval 1 true, identical
 * retval 0 false, not identical
 */
int ast_format_cap_identical(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2);

/*!
 * \brief Get joint capability structure.
 *
 * \note returns an ast_format_cap object containing the joint capabilities on success.  This new
 * capabilities structure is allocated with _NO_ locking enabled.  If a joint structure requires
 * locking, allocate it and use the ast_format_cap_joint_copy function to fill it with the joint
 * capabilities.
 *
 * \retval !NULL success, joint capabilties structure with _NO_ locking enabled.
 * \retval NULL failure
 */
struct ast_format_cap *ast_format_cap_joint(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2);

/*!
 * \brief Get joint capability structure, copy into result capabilities structure
 *
 * \retval 1, joint capabilities exist
 * \retval 0, joint capabilities do not exist
 */
int ast_format_cap_joint_copy(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2, struct ast_format_cap *result);

/*!
 * \brief Get joint capability structure, append into result capabilities structure
 *
 * \retval 1, joint capabilities exist
 * \retval 0, joint capabilities do not exist
 */
int ast_format_cap_joint_append(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2, struct ast_format_cap *result);

/*!
 * \brief Find out if capability structures have any joint capabilities without
 * returning those capabilities.
 *
 * \retval 1 true, has joint capabilities
 * \retval 0 false, failure
 */
int ast_format_cap_has_joint(const struct ast_format_cap *cap1, const struct ast_format_cap *cap2);

/*!
 * \brief Get all capabilities for a specific media type
 *
 * \retval !NULL success, new capabilities structure with _NO_ locking enabled on the new structure.
 * \retval NULL failure
 */
struct ast_format_cap *ast_format_cap_get_type(const struct ast_format_cap *cap, enum ast_format_type ftype);

/*!
 * \brief Find out if the capabilities structure has any formats
 * of a specific type.
 *
 * \retval 1 true
 * \retval 0 false, no formats of specific type.
 */
int ast_format_cap_has_type(const struct ast_format_cap *cap, enum ast_format_type type);

/*! \brief Start iterating formats */
void ast_format_cap_iter_start(struct ast_format_cap *cap);

/*!
 * \brief Next format in interation
 *
 * \details
 * Here is how to use the ast_format_cap iterator.
 *
 * 1. call ast_format_cap_iter_start
 * 2. call ast_format_cap_iter_next in a loop until it returns -1
 * 3. call ast_format_cap_iter_end to terminate the iterator.
 *
 * example:
 *
 * ast_format_cap_iter_start(cap);
 * while (!ast_format_cap_iter_next(cap, &format)) {
 *
 * }
 * ast_format_cap_iter_end(Cap);
 *
 * \Note Unless the container was alloced using no_lock, the container
 * will be locked during the entire iteration until ast_format_cap_iter_end
 * is called. XXX Remember this, and do not attempt to lock any containers
 * within this iteration that will violate locking order.
 *
 * \retval 0 on success, new format is copied into input format struct
 * \retval -1, no more formats are present.
 */
int ast_format_cap_iter_next(struct ast_format_cap *cap, struct ast_format *format);

/*!
 * \brief Ends ast_format_cap iteration.
 * \note this must be call after every ast_format_cap_iter_start
 */
void ast_format_cap_iter_end(struct ast_format_cap *cap);

/*!
 * \brief ast_format_cap to old bitfield format represenatation
 *
 * \note This is only to be used for IAX2 compatibility 
 *
 * \retval old bitfield representation of ast_format_cap
 * \retval 0, if no old bitfield capabilities are present in ast_format_cap
 */
uint64_t ast_format_cap_to_old_bitfield(const struct ast_format_cap *cap);

/*!
 * \brief convert old bitfield format to ast_format_cap represenatation
 * \note This is only to be used for IAX2 compatibility 
 */
void ast_format_cap_from_old_bitfield(struct ast_format_cap *dst, uint64_t src);

/*! \brief Get the names of a set of formats
 * \param buf a buffer for the output string
 * \param size size of buf (bytes)
 * \param format the format (combined IDs of codecs)
 * Prints a list of readable codec names corresponding to "format".
 * ex: for format=AST_FORMAT_GSM|AST_FORMAT_SPEEX|AST_FORMAT_ILBC it will return "0x602 (GSM|SPEEX|ILBC)"
 * \return The return value is buf.
 */
char* ast_getformatname_multiple(char *buf, size_t size, struct ast_format_cap *cap);

#endif /* _AST_FORMATCAP_H */
