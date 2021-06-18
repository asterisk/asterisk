/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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

#ifndef RES_AEAP_TRANSPORT_WEBSOCKET_H
#define RES_AEAP_TRANSPORT_WEBSOCKET_H

/*!
 * \brief Asterisk external application protocol websocket transport
 */
struct aeap_transport_websocket;

/*!
 * \brief Creates (heap allocated), and initializes a transport websocket
 *
 * \returns A transport websocket object, or NULL on error
 */
struct aeap_transport_websocket *aeap_transport_websocket_create(void);

#endif /* RES_AEAP_TRANSPORT_WEBSOCKET_H */
