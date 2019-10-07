/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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
 * \brief PJSIP dialplan functions header file
 */

#ifndef _PJSIP_DIALPLAN_FUNCTIONS
#define _PJSIP_DIALPLAN_FUNCTIONS

/*!
 * \brief CHANNEL function read callback
 * \param chan The channel the function is called on
 * \param cmd The name of the function
 * \param data Arguments passed to the function
 * \param buf Out buffer that should be populated with the data
 * \param len Size of the buffer
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_channel_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len);

/*!
 * \brief PJSIP_MEDIA_OFFER function write callback
 * \param chan The channel the function is called on
 * \param cmd The name of the function
 * \param data Arguments passed to the function
 * \param value Value to be set by the function
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_media_offer_write(struct ast_channel *chan, const char *cmd, char *data, const char *value);

/*!
 * \brief PJSIP_DTMF_MODE function read callback
 * \param chan The channel the function is called on
 * \param cmd The name of the function
 * \param data Arguments passed to the function
 * \param buf Out buffer that should be populated with the data
 * \param len Size of the buffer
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_dtmf_mode_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len);

/*!
 * \brief PJSIP_DTMF_MODE function write callback
 * \param chan The channel the function is called on
 * \param cmd The name of the function
 * \param data Arguments passed to the function
 * \param value Value to be set by the function
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_dtmf_mode_write(struct ast_channel *chan, const char *cmd, char *data, const char *value);

/*!
 * \brief PJSIP_MOH_PASSTHROUGH function read callback
 * \param chan The channel the function is called on
 * \param cmd The name of the function
 * \param data Arguments passed to the function
 * \param buf Out buffer that should be populated with the data
 * \param len Size of the buffer
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_moh_passthrough_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len);

/*!
 * \brief PJSIP_MOH_PASSTHROUGH function write callback
 * \param chan The channel the function is called on
 * \param cmd The name of the function
 * \param data Arguments passed to the function
 * \param value Value to be set by the function
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_moh_passthrough_write(struct ast_channel *chan, const char *cmd, char *data, const char *value);

/*!
 * \brief PJSIP_MEDIA_OFFER function read callback
 * \param chan The channel the function is called on
 * \param cmd The name of the function
 * \param data Arguments passed to the function
 * \param buf Out buffer that should be populated with the data
 * \param len Size of the buffer
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_media_offer_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len);

/*!
 * \brief PJSIP_SEND_SESSION_REFRESH function write callback
 * \param chan The channel the function is called on
 * \param cmd the Name of the function
 * \param data Arguments passed to the function
 * \param value Value to be set by the function
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_session_refresh_write(struct ast_channel *chan, const char *cmd, char *data, const char *value);

/*!
 * \brief PJSIP_DIAL_CONTACTS function read callback
 * \param chan The channel the function is called on
 * \param cmd The name of the function
 * \param data Arguments passed to the function
 * \param buf Out buffer that should be populated with the data
 * \param len Size of the buffer
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_dial_contacts_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len);

/*!
 * \brief PJSIP_PARSE_URI function read callback
 * \param chan The channel the function is called on
 * \param cmd The name of the function
 * \param data Arguments passed to the function
 * \param buf Out buffer that should be populated with the data
 * \param len Size of the buffer
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int pjsip_acf_parse_uri_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len);

#endif /* _PJSIP_DIALPLAN_FUNCTIONS */