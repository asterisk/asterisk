/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#ifndef _ASTERISK_SDP_OPTIONS_H
#define _ASTERISK_SDP_OPTIONS_H

struct ast_sdp_options;

/*!
 * \since 15.0.0
 * \brief Allocate a new SDP options structure.
 *
 * This will heap-allocate an SDP options structure and
 * initialize it to a set of default values.
 *
 * \retval NULL Allocation failure
 * \retval non-NULL Newly allocated SDP options
 */
struct ast_sdp_options *ast_sdp_options_alloc(void);

/*!
 * \since 15.0.0
 * \brief Free an SDP options structure.
 *
 * \note This only needs to be called if an error occurs between
 *       options allocation and a call to ast_sdp_state_alloc()
 *       Otherwise, the SDP state will take care of freeing the
 *       options for you.
 *
 * \param options The options to free
 */
void ast_sdp_options_free(struct ast_sdp_options *options);

/*!
 * \brief ICE options
 *
 * This is an enum because it is predicted that this eventually
 * support a TRICKLE-ICE option.
 */
enum ast_sdp_options_ice {
	/*! ICE is not enabled on this session */
	AST_SDP_ICE_DISABLED = 0,
	/*! Standard ICE is enabled on this session */
	AST_SDP_ICE_ENABLED_STANDARD,
};

/*!
 * \since 15.0.0
 * \brief Set ICE options
 *
 * The default is AST_SDP_ICE_DISABLED
 */
int ast_sdp_options_set_ice(struct ast_sdp_options *options,
	enum ast_sdp_options_ice ice_setting);

/*!
 * \since 15.0.0
 * \brief Retrieve ICE options
 */
enum ast_sdp_options_ice ast_sdp_options_get_ice(const struct ast_sdp_options *options);

/*!
 * \since 15.0.0
 * \brief Enable or disable telephone events.
 *
 * A non-zero value indicates telephone events are enabled.
 * A zero value indicates telephone events are disabled.
 *
 * The default is 0
 */
int ast_sdp_options_set_telephone_event(struct ast_sdp_options *options,
	int telephone_event_enabled);

/*!
 * \since 15.0.0
 * \brief Retrieve telephone event setting.
 *
 * \retval 0 Telephone events are currently disabled.
 * \retval non-zero Telephone events are currently enabled.
 */
int ast_sdp_options_get_telephone_event(const struct ast_sdp_options *options);

/*!
 * \brief Representation of the SDP
 *
 * Users of the SDP API set the representation based on what they
 * natively handle. This indicates the type of SDP that the API expects
 * when being given an SDP, and it indicates the type of SDP that the API
 * returns when asked for one.
 */
enum ast_sdp_options_repr {
	/*! SDP is represented as a string */
	AST_SDP_REPR_STRING = 0,
	/*! SDP is represented as a pjmedia_sdp_session */
	AST_SDP_REPR_PJMEDIA,
};

/*!
 * \since 15.0.0
 * \brief Set the SDP representation
 *
 * The default is AST_SDP_REPR_STRING
 */
int ast_sdp_options_set_repr(struct ast_sdp_options *options,
	enum ast_sdp_options_repr repr);

/*!
 * \since 15.0.0
 * \brief Get the SDP representation
 */
enum ast_sdp_options_repr ast_sdp_options_get_repr(const struct ast_sdp_options *options);

/*!
 * \brief SDP encryption options
 */
enum ast_sdp_options_encryption {
	/*! No encryption */
	AST_SDP_ENCRYPTION_DISABLED = 0,
	/*! SRTP SDES encryption */
	AST_SDP_ENCRYPTION_SRTP_SDES,
	/*! DTLS encryption */
	AST_SDP_ENCRYPTION_DTLS,
};

/*!
 * \since 15.0.0
 * \brief Set the SDP encryption
 *
 * The default is AST_SDP_ENCRYPTION_DISABLED
 */
int ast_sdp_options_set_encryption(struct ast_sdp_options *options,
	enum ast_sdp_options_encryption encryption);

/*!
 * \since 15.0.0
 * \brief Get the SDP encryption
 */
enum ast_sdp_options_encryption ast_sdp_options_get_encryption(const struct ast_sdp_options *options);

#endif /* _ASTERISK_SDP_OPTIONS_H */
