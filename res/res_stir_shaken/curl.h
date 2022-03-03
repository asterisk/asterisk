/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Ben Ford <bford@sangoma.com>
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
#ifndef _STIR_SHAKEN_CURL_H
#define _STIR_SHAKEN_CURL_H

/* Forward declaration for CURL callback data */
struct curl_cb_data;

/*!
 * \brief Allocate memory for a curl_cb_data struct
 *
 * \note This will need to be freed by the consumer using curl_cb_data_free
 *
 * \retval NULL on failure
 * \retval curl_cb_struct on success
 */
struct curl_cb_data *curl_cb_data_create(void);

/*!
 * \brief Free a curl_cb_data struct
 *
 * \param data The curl_cb_data struct to free
 */
void curl_cb_data_free(struct curl_cb_data *data);

/*!
 * \brief Get the cache_control field from a curl_cb_data struct
 *
 * \param data The curl_cb_data
 *
 * \retval cache_control on success
 * \retval NULL otherwise
 */
char *curl_cb_data_get_cache_control(const struct curl_cb_data *data);

/*!
 * \brief Get the expires field from a curl_cb_data struct
 *
 * \param data The curl_cb_data
 *
 * \retval expires on success
 * \retval NULL otherwise
 */
char *curl_cb_data_get_expires(const struct curl_cb_data *data);

/*!
 * \brief CURL the public key from the provided URL to the specified path
 *
 * \note The returned string will need to be freed by the caller
 *
 * \param public_cert_url The public cert URL
 * \param path The path to download the file to
 * \param data The curl_cb_data
 *
 * \retval NULL on failure
 * \retval full path filename on success
 */
char *curl_public_key(const char *public_cert_url, const char *path, struct curl_cb_data *data);

#endif /* _STIR_SHAKEN_CURL_H */
