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

#ifndef RES_AEAP_TRANSPORT_H
#define RES_AEAP_TRANSPORT_H

#include <stdint.h>

#include "asterisk/res_aeap.h"

struct aeap_transport;

/*!
 * \brief Asterisk external application transport virtual table
 *
 * Callbacks to be implemented by "derived" transports
 */
struct aeap_transport_vtable {
	/*!
	 * \brief Connect a transport
	 *
	 * \param self The transport object
	 * \param url The URL to connect to
	 * \param protocol The connection protocol to use if applicable
	 * \param timeout How long (in milliseconds) to attempt to connect (-1 equals infinite)
	 *
	 * \returns 0 on success, or -1 on error
	 */
	int (*connect)(struct aeap_transport *self, const char *url, const char *protocol, int timeout);

	/*!
	 * \brief Disconnect a transport
	 *
	 * \param self The transport object
	 *
	 * \returns 0 on success, or -1 on error
	 */
	int (*disconnect)(struct aeap_transport *self);

	/*!
	 * \brief Destroy a transport
	 *
	 * \param self The transport object
	 */
	void (*destroy)(struct aeap_transport *self);

	/*!
	 * \brief Read data from a transport
	 *
	 * \param self The transport object
	 * \param buf The buffer data is read read into
	 * \param size The size of the given data buffer
	 * \param rtype [out] The type of data read
	 *
	 * \returns Total number of bytes read, or less than zero on error
	 */
	intmax_t (*read)(struct aeap_transport *self, void *buf, intmax_t size,
		enum AST_AEAP_DATA_TYPE *rtype);

	/*!
	 * \brief Write data to a transport
	 *
	 * \param self The transport object
	 * \param buf The data to write
	 * \param size The size of data to write
	 * \param wtype The type of data to write
	 *
	 * \returns Total number of bytes written, or less than zero on error
	 */
	intmax_t (*write)(struct aeap_transport *self, const void *buf, intmax_t size,
		enum AST_AEAP_DATA_TYPE wtype);
};

/*!
 * \brief Asterisk external application transport structure to be
 *        "derived" by specific transport implementation types
 *
 * Transports are assumed to support simultaneous reading and writing,
 * thus separate read and write locks. A transport type not supporting
 * such can simply apply the opposing lock during a read or write, i.e.
 * lock the write lock during a read and vice versa.
 */
struct aeap_transport {
	/*! Transport virtual table */
	struct aeap_transport_vtable *vtable;
	/*! Whether or not the transport is connected */
	unsigned int connected;
	/*! Lock used when reading */
	ast_mutex_t read_lock;
	/*! Lock used when writing */
	ast_mutex_t write_lock;
};

/*!
 * \brief Create an Asterisk external application transport
 *
 * \param type The type of transport to create
 *
 * \returns An Asterisk external application transport, or NULL on error
 */
struct aeap_transport *aeap_transport_create(const char *type);

/*!
 * \brief Connect a transport
 *
 * \param transport The transport to connect
 * \param url The URL to connect to
 * \param protocol The connection protocol to use if applicable
 * \param timeout How long (in milliseconds) to attempt to connect (-1 equals infinite)
 *
 * \returns 0 on success, or -1 on error
 */
int aeap_transport_connect(struct aeap_transport *transport, const char *url,
	const char *protocol, int timeout);

/*!
 * \brief Create an Asterisk external application transport, and connect it
 *
 * \param type The type of transport to create
 * \param url The URL to connect to
 * \param protocol The connection protocol to use if applicable
 * \param timeout How long (in milliseconds) to attempt to connect (-1 equals infinite)
 *
 * \returns An Asterisk external application transport, or NULL on error
 */
struct aeap_transport *aeap_transport_create_and_connect(const char* type,
	const char *url, const char *protocol, int timeout);

/*!
 * \brief Disconnect a transport
 *
 * \note Locks both the transport's read and write locks before calling transport
 *       instance's disconnect, and unlocks both before returning.
 *
 * \param transport The transport to disconnect
 *
 * \returns 0 on success, or -1 on error
 */
int aeap_transport_disconnect(struct aeap_transport *transport);

/*!
 * \brief Whether or not the transport is in a connected state
 *
 * \param transport The transport object
 *
 * \returns True if connected, false otherwise
 */
int aeap_transport_is_connected(struct aeap_transport *transport);

/*!
 * \brief Destroy a transport
 *
 * \param transport The transport to destroy
 *
 * \returns 0 on success, or -1 on error
 */
void aeap_transport_destroy(struct aeap_transport *transport);

/*!
 * \brief Read data from the transport
 *
 * This is a blocking read, and will not return until the transport
 * implementation returns.
 *
 * \note Locks transport's read lock before calling transport instance's
 *       read, and unlocks it before returning.
 *
 * \param transport The transport to read from
 * \param buf The buffer data is read into
 * \param size The size of data given data buffer
 * \param rtype [out] The type of data read
 *
 * \returns Total number of bytes read, or less than zero on error
 */
intmax_t aeap_transport_read(struct aeap_transport *transport, void *buf, intmax_t size,
	enum AST_AEAP_DATA_TYPE *rtype);

/*!
 * \brief Write data to the transport
 *
 * \note Locks transport's write lock before calling transport instance's
 *       write, and unlocks it before returning.
 *
 * \param transport The transport to write to
 * \param buf The data to write
 * \param size The size of data to write
 * \param wtype The type of data to write
 *
 * \returns Total number of bytes written, or less than zero on error
 */
intmax_t aeap_transport_write(struct aeap_transport *transport, const void *buf, intmax_t size,
	enum AST_AEAP_DATA_TYPE wtype);

#endif /* RES_AEAP_TRANSPORT_H */
