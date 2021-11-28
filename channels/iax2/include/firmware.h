/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief IAX Firmware Support header file
 */

#ifndef _IAX2_FIRMWARE_H
#define _IAX2_FIRMWARE_H

#include "parser.h"

/*!
 * \internal
 * \brief Reload the list of available firmware.
 *
 * Searches the IAX firmware directory, adding new firmware that is available
 * and removing firmware that is no longer available.
 */
void iax_firmware_reload(void);

/*!
 * \internal
 * \brief Unload all of the currently loaded firmware.
 */
void iax_firmware_unload(void);

/*!
 * \internal
 * \brief Determine the version number of the specified firmware.
 *
 * \param      device_name The device name of the firmware for which we want the
 *                         version.
 * \param[out] version     Pointer to a variable where the version number is
 *                         stored upon return.
 *
 * \retval 1 on success
 * \retval 0 on failure
 */
int iax_firmware_get_version(const char *device_name,
	uint16_t *version);

/*!
 * \internal
 * \brief Add firmware related IEs to an IAX2 IE buffer.
 *
 * \param ie_data     The IE buffer being appended to.
 * \param device_name The name of the requested firmware.
 * \param block_desc  The requested firmware block identification.
 *
 * Search the list of loaded firmware for \c device_name and, if found, add the
 * appropriate FWBLOCKDESC and FWBLOCKDATA IEs to the specified \c ie_data
 * list.
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int iax_firmware_append(struct iax_ie_data *ie_data,
	const char *device_name, unsigned int block_desc);

/*!
 * \internal
 * \brief Iterate over the list of currently loaded IAX firmware.
 *
 * \param prefix    The prefix of the device to filter for, or \c NULL to visit
 *                  all firmware records.
 * \param callback  A pointer to a function to call for each firmware record
 *                  that is visited.
 * \param user_data A pointer to user supplied data that will be passed to the
 *                  \c callback function each time it is invoked.
 *
 * This function visits each of the elements in the IAX firmware list, calling
 * the specfied \c callback for each element. Iteration continues until the end
 * of the list is reached, or the \c callback returns non-zero.
 *
 * The \c callback function receives a pointer to the firmware header and the
 * value of the \c user_data argument that was passed in, which may be \c NULL.
 */
void iax_firmware_traverse(const char *prefix,
	int (*callback)(struct ast_iax2_firmware_header *header, void *user_data),
	void *user_data);

#endif
