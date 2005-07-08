/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Device state management
 * 
 * Copyright (C) 2005, Digium, Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_DEVICESTATE_H
#define _ASTERISK_DEVICESTATE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! Device is valid but channel didn't know state */
#define AST_DEVICE_UNKNOWN	0
/*! Device is not used */
#define AST_DEVICE_NOT_INUSE	1
/*! Device is in use */
#define AST_DEVICE_INUSE	2
/*! Device is busy */
#define AST_DEVICE_BUSY		3
/*! Device is invalid */
#define AST_DEVICE_INVALID	4
/*! Device is unavailable */
#define AST_DEVICE_UNAVAILABLE	5
/*! Device is ringing */
#define AST_DEVICE_RINGING	6

typedef int (*ast_devstate_cb_type)(const char *dev, int state, void *data);

/*! Search the Channels by Name */
/*!
 * \param device like a dialstring
 * Search the Device in active channels by compare the channelname against 
 * the devicename. Compared are only the first chars to the first '-' char.
 * Returns an AST_DEVICE_UNKNOWN if no channel found or
 * AST_DEVICE_INUSE if a channel is found
 */
int ast_parse_device_state(const char *device);

/*! Asks a channel for device state */
/*!
 * \param device like a dialstring
 * Asks a channel for device state, data is  normaly a number from dialstring
 * used by the low level module
 * Trys the channel devicestate callback if not supported search in the
 * active channels list for the device.
 * Returns an AST_DEVICE_??? state -1 on failure
 */
int ast_device_state(const char *device);

/*! Tells Asterisk the State for Device is changed */
/*!
 * \param fmt devicename like a dialstring with format parameters
 * Asterisk polls the new extensionstates and calls the registered
 * callbacks for the changed extensions
 * Returns 0 on success, -1 on failure
 */
int ast_device_state_changed(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

/*! Registers a device state change callback */
/*!
 * \param data to pass to callback
 * The callback is called if the state for extension is changed
 * Return -1 on failure, ID on success
 */ 
int ast_devstate_add(ast_devstate_cb_type callback, void *data);
void ast_devstate_del(ast_devstate_cb_type callback, void *data);

int ast_device_state_engine_init(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DEVICESTATE_H */
