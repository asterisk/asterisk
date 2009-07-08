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
 */

#ifndef _ASTERISK_DEVICESTATE_H
#define _ASTERISK_DEVICESTATE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

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
	AST_DEVICE_TOTAL,        /*!< Total num of device states, used for testing */
};

/*! \brief Devicestate watcher call back */
typedef int (*ast_devstate_cb_type)(const char *dev, int state, void *data);

/*!  \brief Devicestate provider call back */
typedef int (*ast_devstate_prov_cb_type)(const char *data);

/*! \brief Convert device state to text string for output 
 * \param devstate Current device state 
 */
const char *devstate2str(enum ast_device_state devstate);

/*! \brief Search the Channels by Name
 * \param device like a dialstring
 * Search the Device in active channels by compare the channelname against 
 * the devicename. Compared are only the first chars to the first '-' char.
 * Returns an AST_DEVICE_UNKNOWN if no channel found or
 * AST_DEVICE_INUSE if a channel is found
 */
int ast_parse_device_state(const char *device);

/*! \brief Asks a channel for device state
 * \param device like a dialstring
 * Asks a channel for device state, data is  normaly a number from dialstring
 * used by the low level module
 * Trys the channel devicestate callback if not supported search in the
 * active channels list for the device.
 * Returns an AST_DEVICE_??? state -1 on failure
 */
int ast_device_state(const char *device);

/*! \brief Tells Asterisk the State for Device is changed
 * \param fmt devicename like a dialstring with format parameters
 * Asterisk polls the new extensionstates and calls the registered
 * callbacks for the changed extensions
 * Returns 0 on success, -1 on failure
 */
int ast_device_state_changed(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));


/*! \brief Tells Asterisk the State for Device is changed 
 * \param device devicename like a dialstring
 * Asterisk polls the new extensionstates and calls the registered
 * callbacks for the changed extensions
 * Returns 0 on success, -1 on failure
 */
int ast_device_state_changed_literal(const char *device);

/*! \brief Registers a device state change callback 
 * \param callback Callback
 * \param data to pass to callback
 * The callback is called if the state for extension is changed
 * Return -1 on failure, ID on success
 */ 
int ast_devstate_add(ast_devstate_cb_type callback, void *data);

/*! \brief Unregisters a device state change callback 
 * \param callback Callback
 * \param data to pass to callback
 * The callback is called if the state for extension is changed
 * Return -1 on failure, ID on success
 */ 
void ast_devstate_del(ast_devstate_cb_type callback, void *data);

/*! \brief Add device state provider 
 * \param label to use in hint, like label:object
 * \param callback Callback
 * \retval -1 failure
 * \retval 0 success
 */ 
int ast_devstate_prov_add(const char *label, ast_devstate_prov_cb_type callback);

/*! \brief Remove device state provider 
 * \param label to use in hint, like label:object
 * \return nothing
 */ 
void ast_devstate_prov_del(const char *label);

/*!
 * \brief An object to hold state when calculating aggregate device state
 */
struct ast_devstate_aggregate;

/*!
 * \brief Initialize aggregate device state
 *
 * \param[in] agg the state object
 *
 * \return nothing
 */
void ast_devstate_aggregate_init(struct ast_devstate_aggregate *agg);

/*!
 * \brief Add a device state to the aggregate device state
 *
 * \param[in] agg the state object
 * \param[in] state the state to add
 *
 * \return nothing
 */
void ast_devstate_aggregate_add(struct ast_devstate_aggregate *agg, enum ast_device_state state);

/*!
 * \brief Get the aggregate device state result
 *
 * \param[in] agg the state object
 *
 * \return the aggregate device state after adding some number of device states.
 */
enum ast_device_state ast_devstate_aggregate_result(struct ast_devstate_aggregate *agg);

/*!
 * \brief You shouldn't care about the contents of this struct
 *
 * This struct is only here so that it can be easily declared on the stack.
 */
struct ast_devstate_aggregate {
	unsigned int all_unavail:1;
	unsigned int all_busy:1;
	unsigned int all_free:1;
	unsigned int all_unknown:1;
	unsigned int on_hold:1;
	unsigned int busy:1;
	unsigned int in_use:1;
	unsigned int ring:1;
};

int ast_device_state_engine_init(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DEVICESTATE_H */
