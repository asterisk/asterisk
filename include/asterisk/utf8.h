/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sean Bright
 *
 * Sean Bright <sean.bright@gmail.com>
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
 * \brief UTF-8 information and validation functions
 */

#ifndef ASTERISK_UTF8_H
#define ASTERISK_UTF8_H

/*!
 * \brief Check if a zero-terminated string is valid UTF-8
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * \param str The zero-terminated string to check
 *
 * \retval 0 if the string is not valid UTF-8
 * \retval Non-zero if the string is valid UTF-8
 */
int ast_utf8_is_valid(const char *str);

/*!
 * \brief Check if the first \a size bytes of a string are valid UTF-8
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * Similar to \a ast_utf8_is_valid() but checks the first \a size bytes or until
 * a zero byte is reached, whichever comes first.
 *
 * \param str The string to check
 * \param size The number of bytes to evaluate
 *
 * \retval 0 if the string is not valid UTF-8
 * \retval Non-zero if the string is valid UTF-8
 */
int ast_utf8_is_validn(const char *str, size_t size);

/*!
 * \brief Copy a string safely ensuring valid UTF-8
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * This is similar to \ref ast_copy_string, but it will only copy valid UTF-8
 * sequences from the source string into the destination buffer. If an invalid
 * UTF-8 sequence is encountered, or the available space in the destination
 * buffer is exhausted in the middle of an otherwise valid UTF-8 sequence, the
 * destination buffer will be truncated to ensure that it only contains valid
 * UTF-8.
 *
 * \param dst The destination buffer.
 * \param src The source string
 * \param size The size of the destination buffer
 */
void ast_utf8_copy_string(char *dst, const char *src, size_t size);

enum ast_utf8_validation_result {
	/*! \brief The consumed sequence is valid UTF-8
	 *
	 * The bytes consumed thus far by the validator represent a valid sequence of
	 * UTF-8 bytes. If additional bytes are fed into the validator, it can
	 * transition into either \a AST_UTF8_INVALID or \a AST_UTF8_UNKNOWN
	 */
	AST_UTF8_VALID,

	/*! \brief The consumed sequence is invalid UTF-8
	 *
	 * The bytes consumed thus far by the validator represent an invalid sequence
	 * of UTF-8 bytes. Feeding additional bytes into the validator will not
	 * change its state.
	 */
	AST_UTF8_INVALID,

	/*! \brief The validator is in an intermediate state
	 *
	 * The validator is in the process of validating a multibyte UTF-8 sequence
	 * and requires additional data to be fed into it to determine validity. If
	 * additional bytes are fed into the validator, it can transition into either
	 * \a AST_UTF8_VALID or \a AST_UTF8_INVALID. If you have no additional data
	 * to feed into the validator the UTF-8 sequence is invalid.
	 */
	AST_UTF8_UNKNOWN,
};

/*!
 * \brief Opaque type for UTF-8 validator state.
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 */
struct ast_utf8_validator;

/*!
 * \brief Create a new UTF-8 validator
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * \param[out] validator The validator instance
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_utf8_validator_new(struct ast_utf8_validator **validator);

/*!
 * \brief Feed a zero-terminated string into the UTF-8 validator
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * \param validator The validator instance
 * \param data The zero-terminated string to feed into the validator
 *
 * \return The \ref ast_utf8_validation_result indicating the current state of
 *         the validator.
 */
enum ast_utf8_validation_result ast_utf8_validator_feed(
	struct ast_utf8_validator *validator, const char *data);

/*!
 * \brief Feed a string into the UTF-8 validator
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * Similar to \a ast_utf8_validator_feed but will stop feeding in data if a zero
 * byte is encountered or \a size bytes have been read.
 *
 * \param validator The validator instance
 * \param data The string to feed into the validator
 * \param size The number of bytes to feed into the validator
 *
 * \return The \ref ast_utf8_validation_result indicating the current state of
 *         the validator.
 */
enum ast_utf8_validation_result ast_utf8_validator_feedn(
	struct ast_utf8_validator *validator, const char *data, size_t size);

/*!
 * \brief Get the current UTF-8 validator state
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * \param validator The validator instance
 *
 * \return The \ref ast_utf8_validation_result indicating the current state of
 *         the validator.
 */
enum ast_utf8_validation_result ast_utf8_validator_state(
	struct ast_utf8_validator *validator);

/*!
 * \brief Reset the state of a UTF-8 validator
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * Resets the provided UTF-8 validator to its initial state so that it can be
 * reused.
 *
 * \param validator The validator instance to reset
 */
void ast_utf8_validator_reset(
	struct ast_utf8_validator *validator);

/*!
 * \brief Destroy a UTF-8 validator
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * \param validator The validator instance to destroy
 */
void ast_utf8_validator_destroy(struct ast_utf8_validator *validator);

/*!
 * \brief Register UTF-8 tests
 * \since 13.36.0, 16.13.0, 17.7.0, 18.0.0
 *
 * Does nothing unless TEST_FRAMEWORK is defined.
 *
 * \retval 0 Always
 */
int ast_utf8_init(void);

#endif /* ASTERISK_UTF8_H */
