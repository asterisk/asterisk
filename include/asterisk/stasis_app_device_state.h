/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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

#ifndef _ASTERISK_STASIS_APP_DEVICE_STATE_H
#define _ASTERISK_STASIS_APP_DEVICE_STATE_H

/*! \file
 *
 * \brief Stasis Application Device State API. See \ref res_stasis "Stasis
 * Application API" for detailed documentation.
 *
 * \author Kevin Harwell <kharwell@digium.com>
 * \since 12
 */

#include "asterisk/app.h"
#include "asterisk/stasis_app.h"

/*! @{ */

/*!
 * \brief Convert device state to json.
 *
 * \param name the name of the device
 * \param state the device state
 * \return JSON representation.
 * \retval NULL on error.
 */
struct ast_json *stasis_app_device_state_to_json(
	const char *name, enum ast_device_state state);

/*!
 * \brief Convert device states to json array.
 *
 * \return JSON representation.
 * \retval NULL on error.
 */
struct ast_json *stasis_app_device_states_to_json(void);

/*! Stasis device state application result codes */
enum stasis_device_state_result {
	/*! Application controlled device state is okay */
	STASIS_DEVICE_STATE_OK,
	/*! The device name is not application controlled */
	STASIS_DEVICE_STATE_NOT_CONTROLLED,
	/*! The application controlled device name is missing */
	STASIS_DEVICE_STATE_MISSING,
	/*! The application controlled device is unknown */
	STASIS_DEVICE_STATE_UNKNOWN,
	/*! The application controlled device has subscribers */
	STASIS_DEVICE_STATE_SUBSCRIBERS
};

/*!
 * \brief Changes the state of a device controlled by ARI.
 *
 * \note The controlled device must be prefixed with 'Stasis:'.
 * \note Implicitly creates the device state.
 *
 * \param name the name of the ARI controlled device
 * \param value a valid device state value
 *
 * \return a stasis device state application result.
 */
enum stasis_device_state_result stasis_app_device_state_update(
	const char *name, const char *value);

/*!
 * \brief Delete a device controlled by ARI.
 *
 * \param name the name of the ARI controlled device
 *
 * \return stasis device state application result.
 */
enum stasis_device_state_result stasis_app_device_state_delete(
	const char *name);

/*! @} */

#endif /* _ASTERISK_STASIS_APP_DEVICE_STATE_H */
