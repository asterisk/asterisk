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
 * To subscribe to device state changes, use the stasis_subscribe
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

#include "asterisk/channelstate.h"
#include "asterisk/utils.h"

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
	AST_DEVICE_TOTAL,        /*!< Total num of device states, used for testing */
};

/*! \brief Device State Cachability
 *  \note This is used to define the cacheability of a device state when set.
 */
enum ast_devstate_cache {
	AST_DEVSTATE_NOT_CACHABLE,  /*!< This device state is not cachable */
	AST_DEVSTATE_CACHABLE,      /*!< This device state is cachable */
};

/*! \brief Devicestate provider call back */
typedef enum ast_device_state (*ast_devstate_prov_cb_type)(const char *data);

/*!
 * \brief Convert channel state to devicestate
 *
 * \param chanstate Current channel state
 * \since 1.6.1
 */
enum ast_device_state ast_state_chan2dev(enum ast_channel_state chanstate);

/*!
 * \brief Convert device state to text string for output
 *
 * \param devstate Current device state
 */
const char *ast_devstate2str(enum ast_device_state devstate) attribute_pure;

/*!
 * \brief Convert device state to text string that is easier to parse
 *
 * \param devstate Current device state
 */
const char *ast_devstate_str(enum ast_device_state devstate) attribute_pure;

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
 */
enum ast_device_state ast_device_state(const char *device);

/*!
 * \brief Tells Asterisk the State for Device is changed
 *
 * \param state the new state of the device
 * \param cachable whether this device state is cachable
 * \param fmt device name like a dial string with format parameters
 *
 * The new state of the device will be sent off to any subscribers
 * of device states.  It will also be stored in the internal event
 * cache.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_devstate_changed(enum ast_device_state state, enum ast_devstate_cache cachable, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

/*!
 * \brief Tells Asterisk the State for Device is changed
 *
 * \param state the new state of the device
 * \param cachable whether this device state is cachable
 * \param device device name like a dial string with format parameters
 *
 * The new state of the device will be sent off to any subscribers
 * of device states.  It will also be stored in the internal event
 * cache.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_devstate_changed_literal(enum ast_device_state state, enum ast_devstate_cache cachable, const char *device);

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
 * \since 1.6.1
 */
void ast_devstate_aggregate_init(struct ast_devstate_aggregate *agg);

/*!
 * \brief Add a device state to the aggregate device state
 *
 * \param[in] agg the state object
 * \param[in] state the state to add
 *
 * \return nothing
 * \since 1.6.1
 */
void ast_devstate_aggregate_add(struct ast_devstate_aggregate *agg, enum ast_device_state state);

/*!
 * \brief Get the aggregate device state result
 *
 * \param[in] agg the state object
 *
 * \return the aggregate device state after adding some number of device states.
 * \since 1.6.1
 */
enum ast_device_state ast_devstate_aggregate_result(struct ast_devstate_aggregate *agg);

/*!
 * \brief You shouldn't care about the contents of this struct
 *
 * This struct is only here so that it can be easily declared on the stack.
 */
struct ast_devstate_aggregate {
	unsigned int ringing:1;
	unsigned int inuse:1;
	enum ast_device_state state;
};

/*!
 * \brief The structure that contains device state
 * \since 12
 */
struct ast_device_state_message {
	/*! The name of the device */
	const char *device;
	/*!
	 * \brief The EID of the server where this message originated.
	 *
	 * \note A NULL EID means aggregate state.
	 */
	const struct ast_eid *eid;
	/*! The state of the device */
	enum ast_device_state state;
	/*! Flag designating the cacheability of this device state */
	enum ast_devstate_cache cachable;
	/*! The device and eid data is stuffed here when the struct is allocated. */
	struct ast_eid stuff[0];
};

/*!
 * \brief Get the Stasis topic for device state messages
 * \retval The topic for device state messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_topic *ast_device_state_topic_all(void);

/*!
 * \brief Get the Stasis topic for device state messages for a specific device
 * \param uniqueid The device for which to get the topic
 * \retval The topic structure for MWI messages for a given device
 * \retval NULL if it failed to be found or allocated
 * \since 12
 */
struct stasis_topic *ast_device_state_topic(const char *device);

/*!
 * \brief Get the Stasis caching topic for device state messages
 * \retval The caching topic for device state messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_topic *ast_device_state_topic_cached(void);

/*!
 * \brief Backend cache for ast_device_state_topic_cached()
 * \retval Cache of \ref ast_device_state_message.
 * \since 12
 */
struct stasis_cache *ast_device_state_cache(void);

/*!
 * \brief Get the Stasis message type for device state messages
 * \retval The message type for device state messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_message_type *ast_device_state_message_type(void);

/*!
 * \brief Clear the device from the stasis cache.
 * \param The device to clear
 * \retval 0 if successful
 * \retval -1 nothing to clear
 * \since 12
 */
int ast_device_state_clear_cache(const char *device);

/*!
 * \brief Initialize the device state core
 * \retval 0 Success
 * \retval -1 Failure
 * \since 12
 */
int devstate_init(void);

/*!
 * \brief Publish a device state update
 * \param[in] device The device name
 * \param[in] state The state of the device
 * \param[in] cachable Whether the device state can be cached
 * \retval 0 Success
 * \retval -1 Failure
 * \since 12
 */
#define ast_publish_device_state(device, state, cachable) \
	ast_publish_device_state_full(device, state, cachable, &ast_eid_default)

/*!
 * \brief Publish a device state update with EID
 * \param[in] device The device name
 * \param[in] state The state of the device
 * \param[in] cachable Whether the device state can be cached
 * \param[in] eid The EID of the server that originally published the message
 * \retval 0 Success
 * \retval -1 Failure
 * \since 12
 */
int ast_publish_device_state_full(
			const char *device,
			enum ast_device_state state,
			enum ast_devstate_cache cachable,
			struct ast_eid *eid);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DEVICESTATE_H */
