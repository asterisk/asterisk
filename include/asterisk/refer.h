/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Commend International
 *
 * Maximilian Fridrich <m.fridrich@commend.com>
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
 *
 * \brief Out-of-call refer support
 *
 * \author Maximilian Fridrich <m.fridrich@commend.com>
 *
 * The purpose of this API is to provide support for refers that
 * are not session based. The refers are passed into the Asterisk core
 * to be routed through the dialplan or another interface and potentially
 * sent back out through a refer technology that has been registered
 * through this API.
 */

#ifndef __AST_REFER_H__
#define __AST_REFER_H__

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*!
 * \brief A refer structure.
 *
 * This is an opaque type that represents a refer.
 */
struct ast_refer;

/*!
 * \brief A refer technology
 *
 * A refer technology is capable of transmitting text refers.
 */
struct ast_refer_tech {
	/*!
	 * \brief Name of this refer technology
	 *
	 * This is the name that comes at the beginning of a URI for refers
	 * that should be sent to this refer technology implementation.
	 * For example, refers sent to "pjsip:m.fridrich@commend.com" would be
	 * passed to the ast_refer_tech with a name of "pjsip".
	 */
	const char * const name;
	/*!
	 * \brief Send a refer.
	 *
	 * \param refer The refer to send
	 *
	 * The fields of the ast_refer are guaranteed not to change during the
	 * duration of this function call.
	 *
	 * \retval 0 success
	 * \retval non-zero failure
	 */
	int (* const refer_send)(const struct ast_refer *refer);
};

/*!
 * \brief Register a refer technology
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_refer_tech_register(const struct ast_refer_tech *tech);

/*!
 * \brief Unregister a refer technology.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_refer_tech_unregister(const struct ast_refer_tech *tech);

/*!
 * \brief Allocate a refer.
 *
 * Allocate a refer for the purposes of passing it into the Asterisk core
 * to be routed through the dialplan. This refer must be destroyed using
 * ast_refer_destroy().
 *
 * \return A refer object. This function will return NULL if an allocation
 *         error occurs.
 */
struct ast_refer *ast_refer_alloc(void);

/*!
 * \brief Destroy an ast_refer
 *
 * \retval NULL always.
 */
struct ast_refer *ast_refer_destroy(struct ast_refer *refer);

/*!
 * \brief Bump a refer's ref count
 */
struct ast_refer *ast_refer_ref(struct ast_refer *refer);

/*!
 * \brief Set the 'to' URI of a refer
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_refer_set_to(struct ast_refer *refer, const char *fmt, ...);

/*!
 * \brief Set the 'from' URI of a refer
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_refer_set_from(struct ast_refer *refer, const char *fmt, ...);

/*!
 * \brief Set the 'refer_to' URI of a refer
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_refer_set_refer_to(struct ast_refer *refer, const char *fmt, ...);

/*!
 * \brief Set the 'to_self' value of a refer
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_refer_set_to_self(struct ast_refer *refer, int val);

/*!
 * \brief Set the technology associated with this refer
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_refer_set_tech(struct ast_refer *refer, const char *fmt, ...);

/*!
 * \brief Set the technology's endpoint associated with this refer
 *
 * \retval 0 success
 * \retval -1 failure
 */
int __attribute__((format(printf, 2, 3)))
		ast_refer_set_endpoint(struct ast_refer *refer, const char *fmt, ...);

/*!
 * \brief Set a variable on the refer being sent to a refer tech directly.
 * \note Setting a variable that already exists overwrites the existing variable value
 *
 * \param refer
 * \param name Name of variable to set
 * \param value Value of variable to set
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_refer_set_var_outbound(struct ast_refer *refer, const char *name, const char *value);

/*!
 * \brief Get the specified variable on the refer and unlink it from the container of variables
 * \note The return value must be freed by the caller.
 *
 * \param refer
 * \param name Name of variable to get
 *
 * \return The value associated with variable "name". NULL if variable not found.
 */
char *ast_refer_get_var_and_unlink(struct ast_refer *refer, const char *name);

/*!
 * \brief Get the specified variable on the refer
 * \note The return value is valid only as long as the ast_refer is valid. Hold a reference
 *       to the refer if you plan on storing the return value. It is possible to re-set the
 *       same refer var name (with ast_refer_set_var_outbound passing the variable name)
 *       while holding a pointer to the result of this function.
 *
 * \param refer
 * \param name Name of variable to get
 *
 * \return The value associated with variable "name". NULL if variable not found.
 */
const char *ast_refer_get_var(struct ast_refer *refer, const char *name);

/*!
 * \brief Get the "refer-to" value of a refer.
 * \note The return value is valid only as long as the ast_refer is valid. Hold a reference
 *       to the refer if you plan on storing the return value.
 *
 * \param refer The refer to get the "refer-to" value from
 *
 * \return The "refer-to" value of the refer, encoded in UTF-8.
 */
const char *ast_refer_get_refer_to(const struct ast_refer *refer);

/*!
 * \brief Retrieve the source of this refer
 *
 * \param refer The refer to get the soure from
 *
 * \return The source of the refer
 * \retval NULL or empty string if the refer has no source
 */
const char *ast_refer_get_from(const struct ast_refer *refer);

/*!
 * \brief Retrieve the destination of this refer
 *
 * \param refer The refer to get the destination from
 *
 * \return The destination of the refer
 * \retval NULL or empty string if the refer has no destination
 */
const char *ast_refer_get_to(const struct ast_refer *refer);

/*!
 * \brief Retrieve the "to_self" value of this refer
 *
 * \param refer The refer to get the destination from
 *
 * \return The to_self value of the refer
 */
int ast_refer_get_to_self(const struct ast_refer *refer);

/*!
 * \brief Retrieve the technology associated with this refer
 *
 * \param refer The refer to get the technology from
 *
 * \return The technology of the refer
 * \retval NULL or empty string if the refer has no associated technology
 */
const char *ast_refer_get_tech(const struct ast_refer *refer);

/*!
 * \brief Retrieve the endpoint associated with this refer
 *
 * \param refer The refer to get the endpoint from
 *
 * \return The endpoint associated with the refer
 * \retval NULL or empty string if the refer has no associated endpoint
 */
const char *ast_refer_get_endpoint(const struct ast_refer *refer);

/*!
 * \brief Send a refer directly to an endpoint.
 *
 * Regardless of the return value of this function, this function will take
 * care of ensuring that the refer object is properly destroyed when needed.
 *
 * \retval 0 refer successfully queued to be sent out
 * \retval non-zero failure, refer not get sent out.
 */
int ast_refer_send(struct ast_refer *refer);

/*!
 * \brief Opaque iterator for refer variables
 */
struct ast_refer_var_iterator;

/*!
 * \brief Create a new refer variable iterator
 * \param refer A refer whose variables are to be iterated over
 *
 * \return An opaque pointer to the new iterator
 */
struct ast_refer_var_iterator *ast_refer_var_iterator_init(const struct ast_refer *refer);

/*!
 * \brief Get the next variable name and value
 *
 * \param iter An iterator created with ast_refer_var_iterator_init
 * \param name A pointer to the name result pointer
 * \param value A pointer to the value result pointer
 *
 * \note The refcount to iter->current_used must be decremented by the caller
 *       by calling ast_refer_var_unref_current.
 *
 * \retval 0 No more entries
 * \retval 1 Valid entry
 */
int ast_refer_var_iterator_next(struct ast_refer_var_iterator *iter, const char **name, const char **value);

/*!
 * \brief Destroy a refer variable iterator
 * \param iter Iterator to be destroyed
 */
void ast_refer_var_iterator_destroy(struct ast_refer_var_iterator *iter);

/*!
 * \brief Unref a refer var from inside an iterator loop
 */
void ast_refer_var_unref_current(struct ast_refer_var_iterator *iter);

/*!
 *  @}
 */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* __AST_REFER_H__ */
