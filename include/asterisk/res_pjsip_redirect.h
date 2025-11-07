/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2025, Commend International
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

#ifndef _RES_PJSIP_REDIRECT_H
#define _RES_PJSIP_REDIRECT_H

#include <pjsip.h>

/*!
 * \brief Maximum number of redirect hops allowed
 */
#define AST_SIP_MAX_REDIRECT_HOPS 5

/*!
 * \brief Maximum number of redirect contacts to process
 */
#define AST_SIP_MAX_REDIRECT_CONTACTS 20

/*!
 * \brief Opaque structure for redirect state
 *
 * This structure encapsulates all state needed for handling
 * SIP 3xx redirects, including visited URIs for loop detection,
 * pending contacts for retry logic, and hop counting.
 */
struct ast_sip_redirect_state;

/*!
 * \brief Create a new redirect state
 *
 * \param endpoint The SIP endpoint
 * \param initial_uri The initial URI being contacted (for loop detection)
 *
 * \retval NULL on failure
 * \retval A new redirect state on success
 *
 * \note The caller must call ast_sip_redirect_state_destroy() when done
 */
struct ast_sip_redirect_state *ast_sip_redirect_state_create(
	struct ast_sip_endpoint *endpoint,
	const char *initial_uri);

/*!
 * \brief Check if redirect should be followed based on endpoint configuration
 *
 * \param endpoint The SIP endpoint
 * \param rdata The redirect response data containing the 3xx response
 *
 * \retval 0 if redirect should not be followed
 * \retval 1 if redirect should be followed
 *
 * \note This checks if the status code is 3xx and if the SIP method
 *       (extracted from the CSeq header) is allowed to follow redirects
 *       based on the endpoint's follow_redirect_methods configuration
 */
int ast_sip_should_redirect(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata);

/*!
 * \brief Parse a 3xx redirect response and extract contacts
 *
 * This function parses all Contact headers from a 3xx response,
 * extracts q-values, sorts contacts by priority (highest q-value first),
 * and filters out URIs that would create loops.
 *
 * \param rdata The redirect response data
 * \param state The redirect state
 *
 * \retval -1 on failure (hop limit reached, no valid contacts, etc.)
 * \retval 0 on success (at least one valid contact available)
 *
 * \note After calling this, use ast_sip_redirect_get_next_uri() to retrieve URIs
 */
int ast_sip_redirect_parse_3xx(pjsip_rx_data *rdata, struct ast_sip_redirect_state *state);

/*!
 * \brief Get the next redirect URI to try
 *
 * This function returns the next contact URI from the redirect response,
 * ordered by q-value (highest first). It also marks the URI as visited
 * to prevent loops on subsequent redirects.
 *
 * \param state The redirect state
 * \param uri_out Pointer to store the URI string (caller must free)
 *
 * \retval -1 if no more URIs available
 * \retval 0 on success
 *
 * \note The caller must ast_free() the returned URI string
 */
int ast_sip_redirect_get_next_uri(struct ast_sip_redirect_state *state, char **uri_out);

/*!
 * \brief Check if a URI would create a redirect loop
 *
 * \param state The redirect state
 * \param uri The URI to check
 *
 * \retval 0 if URI is safe (not visited)
 * \retval 1 if URI would create a loop (already visited)
 */
int ast_sip_redirect_check_loop(const struct ast_sip_redirect_state *state, const char *uri);

/*!
 * \brief Get the current hop count
 *
 * \param state The redirect state
 *
 * \return The current hop count
 */
int ast_sip_redirect_get_hop_count(const struct ast_sip_redirect_state *state);

/*!
 * \brief Get the endpoint from the redirect state
 *
 * \param state The redirect state
 *
 * \return The endpoint (borrowed reference, do not cleanup)
 */
struct ast_sip_endpoint *ast_sip_redirect_get_endpoint(const struct ast_sip_redirect_state *state);

/*!
 * \brief Destroy a redirect state and free all resources
 *
 * \param state The redirect state to destroy
 */
void ast_sip_redirect_state_destroy(struct ast_sip_redirect_state *state);

#endif /* _RES_PJSIP_REDIRECT_H */
