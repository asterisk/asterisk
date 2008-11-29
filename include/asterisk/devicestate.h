/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief Device state management
 *
 * To subscribe to device state changes, use the generic ast_event_subscribe
 * method.  For an example, see apps/app_queue.c.
 *
 * \todo Currently, when the state of a device changes, the device state provider
 * calls one of the functions defined here to queue an object to say that the 
 * state of a device has changed.  However, this does not include the new state.
 * Another thread processes these device state change objects and calls the
 * device state provider's callback to figure out what the new state is.  It
 * would make a lot more sense for the new state to be included in the original
 * function call that says the state of a device has changed.  However, it
 * will take a lot of work to change this.
 *
 * \arg See \ref AstExtState
 */

#ifndef _ASTERISK_DEVICESTATE_H
#define _ASTERISK_DEVICESTATE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief Device States 
 *  \note The order of these states may not change because they are included
 *        in Asterisk events which may be transmitted across the network to
 *        other servers.
 */
enum ast_device_state {
	AST_DEVICE_UNKNOWN,      /*!< Device is valid but channel didn't know state */
	AST_DEVICE_NOT_INUSE,    /*!< Device is not used */
	AST_DEVICE_INUSE,        /*!< Device is in use */
	AST_DEVICE_BUSY,         /*!< Device is busy */
	AST_DEVICE_INVALID,      /*!< Device is invalid */
	AST_DEVICE_UNAVAILABLE,  /*!< Device is unavailable */
	AST_DEVICE_RINGING,      /*!< Device is ringing */
	AST_DEVICE_RINGINUSE,    /*!< Device is ringing *and* in use */
	AST_DEVICE_ONHOLD,       /*!< Device is on hold */
};

/*! \brief Devicestate provider call back */
typedef enum ast_device_state (*ast_devstate_prov_cb_type)(const char *data);

/*! 
 * \brief Convert device state to text string for output 
 *
 * \param devstate Current device state 
 */
const char *devstate2str(enum ast_device_state devstate);

/*! 
 * \brief Convert device state to text string that is easier to parse 
 *
 * \param devstate Current device state 
 */
const char *ast_devstate_str(enum ast_device_state devstate);

/*! 
 * \brief Convert device state from text to integer value
 *
 * \param val The text representing the device state.  Valid values are anything
 *        that comes after AST_DEVICE_ in one of the defined values.
 *
 * \return The AST_DEVICE_ integer value
 */
enum ast_device_state ast_devstate_val(const char *val);

/*! 
 * \brief Search the Channels by Name
 *
 * \param device like a dial string
 *
 * Search the Device in active channels by compare the channel name against 
 * the device name. Compared are only the first chars to the first '-' char.
 *
 * \retval AST_DEVICE_UNKNOWN if no channel found
 * \retval AST_DEVICE_INUSE if a channel is found
 */
enum ast_device_state ast_parse_device_state(const char *device);

/*! 
 * \brief Asks a channel for device state
 *
 * \param device like a dial string
 *
 * Asks a channel for device state, data is normally a number from a dial string
 * used by the low level module
 * Tries the channel device state callback if not supported search in the
 * active channels list for the device.
 *
 * \retval an AST_DEVICE_??? state 
 * \retval -1 on failure
 */
enum ast_device_state ast_device_state(const char *device);

/*! 
 * \brief Tells Asterisk the State for Device is changed
 *
 * \param state the new state of the device
 * \param fmt device name like a dial string with format parameters
 *
 * The new state of the device will be sent off to any subscribers
 * of device states.  It will also be stored in the internal event
 * cache.
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_devstate_changed(enum ast_device_state state, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

/*! 
 * \brief Tells Asterisk the State for Device is changed
 *
 * \param state the new state of the device
 * \param device device name like a dial string with format parameters
 *
 * The new state of the device will be sent off to any subscribers
 * of device states.  It will also be stored in the internal event
 * cache.
 *
 * \retval 0 on success 
 * \retval -1 on failure
 */
int ast_devstate_changed_literal(enum ast_device_state state, const char *device);

/*! 
 * \brief Tells Asterisk the State for Device is changed
 *
 * \param fmt device name like a dial string with format parameters
 *
 * Asterisk polls the new extension states and calls the registered
 * callbacks for the changed extensions
 *
 * \retval 0 on success 
 * \retval -1 on failure
 *
 * \note This is deprecated in favor of ast_devstate_changed()
 */
int ast_device_state_changed(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));

/*! 
 * \brief Tells Asterisk the State for Device is changed 
 *
 * \param device device name like a dial string
 *
 * Asterisk polls the new extension states and calls the registered
 * callbacks for the changed extensions
 *
 * \retval 0 on success 
 * \retval -1 on failure
 *
 * \note This is deprecated in favor of ast_devstate_changed_literal()
 */
int ast_device_state_changed_literal(const char *device);

/*! 
 * \brief Add device state provider 
 *
 * \param label to use in hint, like label:object
 * \param callback Callback
 *
 * \retval 0 success
 * \retval -1 failure
 */ 
int ast_devstate_prov_add(const char *label, ast_devstate_prov_cb_type callback);

/*! 
 * \brief Remove device state provider 
 *
 * \param label to use in hint, like label:object
 *
 * \retval -1 on failure 
 * \retval 0 on success
 */ 
int ast_devstate_prov_del(const char *label);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DEVICESTATE_H */
