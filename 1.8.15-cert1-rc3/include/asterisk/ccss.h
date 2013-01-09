/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \brief Call Completion Supplementary Services API
 * \author Mark Michelson <mmichelson@digium.com>
 */

#ifndef _ASTERISK_CCSS_H
#define _ASTERISK_CCSS_H

#include "asterisk.h"

#include "asterisk/linkedlists.h"
#include "asterisk/devicestate.h"

enum ast_cc_service_type {
	/* No Service available/requested */
	AST_CC_NONE,
	/* Call Completion Busy Subscriber */
	AST_CC_CCBS,
	/* Call Completion No Response */
	AST_CC_CCNR,
	/* Call Completion Not Logged In (currently SIP only) */
	AST_CC_CCNL,
};

/*!
 * \since 1.8
 * \brief The various possibilities for cc_agent_policy values
 */
enum ast_cc_agent_policies {
	/*! Never offer CCSS to the caller */
	AST_CC_AGENT_NEVER,
	/*! Offer CCSS using native signaling */
	AST_CC_AGENT_NATIVE,
	/*! Use generic agent for caller */
	AST_CC_AGENT_GENERIC,
};

/*!
 * \brief agent flags that can alter core behavior
 */
enum ast_cc_agent_flags {
	/* Some agent types allow for a caller to
	 * request CC without reaching the CC_CALLER_OFFERED
	 * state. In other words, the caller can request
	 * CC while he is still on the phone from the failed
	 * call. The generic agent is an agent which allows
	 * for this behavior.
	 */
	AST_CC_AGENT_SKIP_OFFER = (1 << 0),
};

/*!
 * \since 1.8
 * \brief The various possibilities for cc_monitor_policy values
 */
enum ast_cc_monitor_policies {
	/*! Never accept CCSS offers from callee */
	AST_CC_MONITOR_NEVER,
	/* CCSS only available if callee offers it through signaling */
	AST_CC_MONITOR_NATIVE,
	/*! Always use CCSS generic monitor for callee
	 * Note that if callee offers CCSS natively, we still
	 * will use a generic CCSS monitor if this is set
	 */
	AST_CC_MONITOR_GENERIC,
	/*! Accept native CCSS offers, but if no offer is present,
	 * use a generic CCSS monitor
	 */
	AST_CC_MONITOR_ALWAYS,
};

/* Forward declaration. Struct is in main/ccss.c */
struct ast_cc_config_params;

/*!
 * \since 1.8
 * \brief Queue an AST_CONTROL_CC frame
 *
 * \note
 * Since this function calls ast_queue_frame, the channel will be
 * locked during the course of this function.
 *
 * \param chan The channel onto which to queue the frame
 * \param monitor_type The type of monitor to use when CC is requested
 * \param dialstring The dial string used to call the device
 * \param service The type of CC service the device is willing to offer
 * \param private_data If a native monitor is being used, and some channel-driver-specific private
 * data has been allocated, then this parameter should contain a pointer to that data. If using a generic
 * monitor, this parameter should remain NULL. Note that if this function should fail at some point,
 * it is the responsibility of the caller to free the private data upon return.
 * \retval 0 Success
 * \retval -1 Error
 */
int ast_queue_cc_frame(struct ast_channel *chan, const char * const monitor_type,
		const char * const dialstring, enum ast_cc_service_type service, void *private_data);

/*!
 * \brief Allocate and initialize an ast_cc_config_params structure
 *
 * \note
 * Reasonable default values are chosen for the parameters upon allocation.
 *
 * \retval NULL Unable to allocate the structure
 * \retval non-NULL A pointer to the newly allocated and initialized structure
 */
struct ast_cc_config_params *__ast_cc_config_params_init(const char *file, int line, const char *function);

/*!
 * \brief Allocate and initialize an ast_cc_config_params structure
 *
 * \note
 * Reasonable default values are chosen for the parameters upon allocation.
 *
 * \retval NULL Unable to allocate the structure
 * \retval non-NULL A pointer to the newly allocated and initialized structure
 */
#define ast_cc_config_params_init() __ast_cc_config_params_init(__FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief Free memory from CCSS configuration params
 *
 * \note
 * Just a call to ast_free for now...
 *
 * \param params Pointer to structure whose memory we need to free
 * \retval void
 */
void ast_cc_config_params_destroy(struct ast_cc_config_params *params);

/*!
 * \brief set a CCSS configuration parameter, given its name
 *
 * \note
 * Useful when parsing config files when used in conjunction
 * with ast_ccss_is_cc_config_param.
 *
 * \param params The parameter structure to set the value on
 * \param name The name of the cc parameter
 * \param value The value of the parameter
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_cc_set_param(struct ast_cc_config_params *params, const char * const name,
		const char * value);

/*!
 * \brief get a CCSS configuration parameter, given its name
 *
 * \note
 * Useful when reading input as a string, like from dialplan or
 * manager.
 *
 * \param params The CCSS configuration from which to get the value
 * \param name The name of the CCSS parameter we want
 * \param buf A preallocated buffer to hold the value
 * \param buf_len The size of buf
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_cc_get_param(struct ast_cc_config_params *params, const char * const name,
		char *buf, size_t buf_len);

/*!
 * \since 1.8
 * \brief Is this a CCSS configuration parameter?
 * \param name Name of configuration option being parsed.
 * \retval 1 Yes, this is a CCSS configuration parameter.
 * \retval 0 No, this is not a CCSS configuration parameter.
 */
int ast_cc_is_config_param(const char * const name);

/*!
 * \since 1.8
 * \brief Set the specified CC config params to default values.
 *
 * \details
 * This is just like ast_cc_copy_config_params() and could be used in place
 * of it if you need to set the config params to defaults instead.
 * You are simply "copying" defaults into the destination.
 *
 * \param params CC config params to set to default values.
 *
 * \return Nothing
 */
void ast_cc_default_config_params(struct ast_cc_config_params *params);

/*!
 * \since 1.8
 * \brief copy CCSS configuration parameters from one structure to another
 *
 * \details
 * For now, this is a simple memcpy, but this function is necessary since
 * the size of an ast_cc_config_params structure is unknown outside of
 * main/ccss.c. Also, this allows for easier expansion of the function in
 * case it becomes more complex than just a memcpy.
 *
 * \param src The structure from which data is copied
 * \param dest The structure to which data is copied
 *
 * \return Nothing
 */
void ast_cc_copy_config_params(struct ast_cc_config_params *dest, const struct ast_cc_config_params *src);

/*!
 * \since 1.8
 * \brief Get the cc_agent_policy
 * \param config The configuration to retrieve the policy from
 * \return The current cc_agent_policy for this configuration
 */
enum ast_cc_agent_policies ast_get_cc_agent_policy(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the cc_agent_policy
 * \param config The configuration to set the cc_agent_policy on
 * \param value The new cc_agent_policy we want to change to
 * \retval 0 Success
 * \retval -1 Failure (likely due to bad input)
 */
int ast_set_cc_agent_policy(struct ast_cc_config_params *config, enum ast_cc_agent_policies value);

/*!
 * \since 1.8
 * \brief Get the cc_monitor_policy
 * \param config The configuration to retrieve the cc_monitor_policy from
 * \return The cc_monitor_policy retrieved from the configuration
 */
enum ast_cc_monitor_policies ast_get_cc_monitor_policy(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the cc_monitor_policy
 * \param config The configuration to set the cc_monitor_policy on
 * \param value The new cc_monitor_policy we want to change to
 * \retval 0 Success
 * \retval -1 Failure (likely due to bad input)
 */
int ast_set_cc_monitor_policy(struct ast_cc_config_params *config, enum ast_cc_monitor_policies value);

/*!
 * \since 1.8
 * \brief Get the cc_offer_timer
 * \param config The configuration to retrieve the cc_offer_timer from
 * \return The cc_offer_timer from this configuration
 */
unsigned int ast_get_cc_offer_timer(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the cc_offer_timer
 * \param config The configuration to set the cc_offer_timer on
 * \param value The new cc_offer_timer we want to change to
 * \retval void
 */
void ast_set_cc_offer_timer(struct ast_cc_config_params *config, unsigned int value);

/*!
 * \since 1.8
 * \brief Get the ccnr_available_timer
 * \param config The configuration to retrieve the ccnr_available_timer from
 * \return The ccnr_available_timer from this configuration
 */
unsigned int ast_get_ccnr_available_timer(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the ccnr_available_timer
 * \param config The configuration to set the ccnr_available_timer on
 * \param value The new ccnr_available_timer we want to change to
 * \retval void
 */
void ast_set_ccnr_available_timer(struct ast_cc_config_params *config, unsigned int value);

/*!
 * \since 1.8
 * \brief Get the cc_recall_timer
 * \param config The configuration to retrieve the cc_recall_timer from
 * \return The cc_recall_timer from this configuration
 */
unsigned int ast_get_cc_recall_timer(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the cc_recall_timer
 * \param config The configuration to set the cc_recall_timer on
 * \param value The new cc_recall_timer we want to change to
 * \retval void
 */
void ast_set_cc_recall_timer(struct ast_cc_config_params *config, unsigned int value);

/*!
 * \since 1.8
 * \brief Get the ccbs_available_timer
 * \param config The configuration to retrieve the ccbs_available_timer from
 * \return The ccbs_available_timer from this configuration
 */
unsigned int ast_get_ccbs_available_timer(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the ccbs_available_timer
 * \param config The configuration to set the ccbs_available_timer on
 * \param value The new ccbs_available_timer we want to change to
 * \retval void
 */
void ast_set_ccbs_available_timer(struct ast_cc_config_params *config, unsigned int value);

/*!
 * \since 1.8
 * \brief Get the cc_agent_dialstring
 * \param config The configuration to retrieve the cc_agent_dialstring from
 * \return The cc_agent_dialstring from this configuration
 */
const char *ast_get_cc_agent_dialstring(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the cc_agent_dialstring
 * \param config The configuration to set the cc_agent_dialstring on
 * \param value The new cc_agent_dialstring we want to change to
 * \retval void
 */
void ast_set_cc_agent_dialstring(struct ast_cc_config_params *config, const char *const value);

/*!
 * \since 1.8
 * \brief Get the cc_max_agents
 * \param config The configuration to retrieve the cc_max_agents from
 * \return The cc_max_agents from this configuration
 */
unsigned int ast_get_cc_max_agents(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the cc_max_agents
 * \param config The configuration to set the cc_max_agents on
 * \param value The new cc_max_agents we want to change to
 * \retval void
 */
void ast_set_cc_max_agents(struct ast_cc_config_params *config, unsigned int value);

/*!
 * \since 1.8
 * \brief Get the cc_max_monitors
 * \param config The configuration to retrieve the cc_max_monitors from
 * \return The cc_max_monitors from this configuration
 */
unsigned int ast_get_cc_max_monitors(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the cc_max_monitors
 * \param config The configuration to set the cc_max_monitors on
 * \param value The new cc_max_monitors we want to change to
 * \retval void
 */
void ast_set_cc_max_monitors(struct ast_cc_config_params *config, unsigned int value);

/*!
 * \since 1.8
 * \brief Get the name of the callback_macro
 * \param config The configuration to retrieve the callback_macro from
 * \return The callback_macro name
 */
const char *ast_get_cc_callback_macro(struct ast_cc_config_params *config);

/*!
 * \since 1.8
 * \brief Set the callback_macro name
 * \param config The configuration to set the callback_macro on
 * \param value The new callback macro  we want to change to
 * \retval void
 */
void ast_set_cc_callback_macro(struct ast_cc_config_params *config, const char * const value);

/* END CONFIGURATION FUNCTIONS */

/* BEGIN AGENT/MONITOR REGISTRATION API */

struct ast_cc_monitor_callbacks;

/*!
 * \since 1.8
 * \brief Register a set of monitor callbacks with the core
 *
 * \details
 * This is made so that at monitor creation time, the proper callbacks
 * may be installed and the proper .init callback may be called for the
 * monitor to establish private data.
 *
 * \param callbacks The callbacks used by the monitor implementation
 * \retval 0 Successfully registered
 * \retval -1 Failure to register
 */
int ast_cc_monitor_register(const struct ast_cc_monitor_callbacks *callbacks);

/*!
 * \since 1.8
 * \brief Unregister a set of monitor callbacks with the core
 *
 * \details
 * If a module which makes use of a CC monitor is unloaded, then it may
 * unregister its monitor callbacks with the core.
 *
 * \param callbacks The callbacks used by the monitor implementation
 * \retval 0 Successfully unregistered
 * \retval -1 Failure to unregister
 */
void ast_cc_monitor_unregister(const struct ast_cc_monitor_callbacks *callbacks);

struct ast_cc_agent_callbacks;

/*!
 * \since 1.8
 * \brief Register a set of agent callbacks with the core
 *
 * \details
 * This is made so that at agent creation time, the proper callbacks
 * may be installed and the proper .init callback may be called for the
 * monitor to establish private data.
 *
 * \param callbacks The callbacks used by the agent implementation
 * \retval 0 Successfully registered
 * \retval -1 Failure to register
 */
int ast_cc_agent_register(const struct ast_cc_agent_callbacks *callbacks);

/*!
 * \since 1.8
 * \brief Unregister a set of agent callbacks with the core
 *
 * \details
 * If a module which makes use of a CC agent is unloaded, then it may
 * unregister its agent callbacks with the core.
 *
 * \param callbacks The callbacks used by the agent implementation
 * \retval 0 Successfully unregistered
 * \retval -1 Failure to unregister
 */
void ast_cc_agent_unregister(const struct ast_cc_agent_callbacks *callbacks);

/* END AGENT/MONITOR REGISTRATION API */

/* BEGIN SECTION ON MONITORS AND MONITOR CALLBACKS */

/*!
 * It is recommended that monitors use a pointer to
 * an ast_cc_monitor_callbacks::type when creating
 * an AST_CONTROL_CC frame. Since the generic monitor
 * callbacks are opaque and channel drivers will wish
 * to use that, this string is made globally available
 * for all to use
 */
#define AST_CC_GENERIC_MONITOR_TYPE "generic"

/*!
 * Used to determine which type
 * of monitor an ast_cc_device_monitor
 * is.
 */
enum ast_cc_monitor_class {
	AST_CC_DEVICE_MONITOR,
	AST_CC_EXTENSION_MONITOR,
};

/*!
 * \internal
 * \brief An item in a CC interface tree.
 *
 * These are the individual items in an interface tree.
 * The key difference between this structure and the ast_cc_interface
 * is that this structure contains data which is intrinsic to the item's
 * placement in the tree, such as who its parent is.
 */
struct ast_cc_monitor {
	/*!
	 * Information regarding the interface.
	 */
	struct ast_cc_interface *interface;
	/*!
	 * Every interface has an id that uniquely identifies it. It is
	 * formed by incrementing a counter.
	 */
	unsigned int id;
	/*!
	 * The ID of this monitor's parent. If this monitor is at the
	 * top of the tree, then his parent will be 0.
	 */
	unsigned int parent_id;
	/*!
	 * The instance of the CC core to which this monitor belongs
	 */
	int core_id;
	/*!
	 * The type of call completion service offered by a device.
	 */
	enum ast_cc_service_type service_offered;
	/*!
	 * \brief Name that should be used to recall specified interface
	 *
	 * \details
	 * When issuing a CC recall, some technologies will require
	 * that a name other than the device name is dialed. For instance,
	 * with SIP, a specific URI will be used which chan_sip will be able
	 * to recognize as being a CC recall. Similarly, ISDN will need a specific
	 * dial string to know that the call is a recall.
	 */
	char *dialstring;
	/*!
	 * The ID of the available timer used by the current monitor
	 */
	int available_timer_id;
	/*!
	 * Monitor callbacks
	 */
	const struct ast_cc_monitor_callbacks *callbacks;
	/*!
	 * \brief Data that is private to a monitor technology
	 *
	 * Most channel drivers that implement CC monitors will have to
	 * allocate data that the CC core does not care about but which
	 * is vital to the operation of the monitor. This data is stored
	 * in this pointer so that the channel driver may use it as
	 * needed
	 */
	void *private_data;
	AST_LIST_ENTRY(ast_cc_monitor) next;
};

/*!
 * \brief Callbacks defined by CC monitors
 *
 * \note
 * Every callback is called with the list of monitors locked. There
 * are several public API calls that also will try to lock this lock.
 * These public functions have a note in their doxygen stating so.
 * As such, pay attention to the lock order you establish in these callbacks
 * to ensure that you do not violate the lock order when calling
 * the functions in this file with lock order notices.
 */
struct ast_cc_monitor_callbacks {
	/*!
	 * \brief Type of monitor the callbacks belong to.
	 *
	 * \note
	 * Examples include "generic" and "SIP"
	 */
	const char *type;
	/*!
	 * \brief Request CCSS.
	 *
	 * \param monitor CC core monitor control.
	 * \param available_timer_id The scheduler ID for the available timer.
	 * Will never be NULL for a device monitor.
	 *
	 * \details
	 * Perform whatever steps are necessary in order to request CC.
	 * In addition, the monitor implementation is responsible for
	 * starting the available timer in this callback.
	 *
	 * \retval 0 on success
	 * \retval -1 on failure.
	 */
	int (*request_cc)(struct ast_cc_monitor *monitor, int *available_timer_id);
	/*!
	 * \brief Suspend monitoring.
	 *
	 * \param monitor CC core monitor control.
	 *
	 * \details
	 * Implementers must perform the necessary steps to suspend
	 * monitoring.
	 *
	 * \retval 0 on success
	 * \retval -1 on failure.
	 */
	int (*suspend)(struct ast_cc_monitor *monitor);
	/*!
	 * \brief Status response to an ast_cc_monitor_status_request().
	 *
	 * \param monitor CC core monitor control.
	 * \param devstate Current status of a Party A device.
	 *
	 * \details
	 * Alert a monitor as to the status of the agent for which
	 * the monitor had previously requested a status request.
	 *
	 * \note Zero or more responses may come as a result.
	 *
	 * \retval 0 on success
	 * \retval -1 on failure.
	 */
	int (*status_response)(struct ast_cc_monitor *monitor, enum ast_device_state devstate);
	/*!
	 * \brief Unsuspend monitoring.
	 *
	 * \param monitor CC core monitor control.
	 *
	 * \details
	 * Perform the necessary steps to unsuspend monitoring.
	 *
	 * \retval 0 on success
	 * \retval -1 on failure.
	 */
	int (*unsuspend)(struct ast_cc_monitor *monitor);
	/*!
	 * \brief Cancel the running available timer.
	 *
	 * \param monitor CC core monitor control.
	 * \param sched_id Available timer scheduler id to cancel.
	 * Will never be NULL for a device monitor.
	 *
	 * \details
	 * In most cases, this function will likely consist of just a
	 * call to AST_SCHED_DEL. It might have been possible to do this
	 * within the core, but unfortunately the mixture of sched_thread
	 * and sched usage in Asterisk prevents such usage.
	 *
	 * \retval 0 on success
	 * \retval -1 on failure.
	 */
	int (*cancel_available_timer)(struct ast_cc_monitor *monitor, int *sched_id);
	/*!
	 * \brief Destroy private data on the monitor.
	 *
	 * \param private_data The private data pointer from the monitor.
	 *
	 * \details
	 * Implementers of this callback are responsible for destroying
	 * all heap-allocated data in the monitor's private_data pointer, including
	 * the private_data itself.
	 */
	void (*destructor)(void *private_data);
};

/*!
 * \since 1.8
 * \brief Scheduler callback for available timer expiration
 *
 * \note
 * When arming the available timer from within a device monitor, you MUST
 * use this function as the callback for the scheduler.
 *
 * \param data A reference to the CC monitor on which the timer was running.
 */
int ast_cc_available_timer_expire(const void *data);

/* END SECTION ON MONITORS AND MONITOR CALLBACKS */

/* BEGIN API FOR IN-CALL CC HANDLING */

/*!
 * \since 1.8
 *
 * \brief Mark the channel to ignore further CC activity.
 *
 * \details
 * When a CC-capable application, such as Dial, has finished
 * with all CC processing for a channel and knows that any further
 * CC processing should be ignored, this function should be called.
 *
 * \param chan The channel for which further CC processing should be ignored.
 * \retval void
 */
void ast_ignore_cc(struct ast_channel *chan);

/*!
 * \since 1.8
 *
 * \brief Properly react to a CC control frame.
 *
 * \details
 * When a CC-capable application, such as Dial, receives a frame
 * of type AST_CONTROL_CC, then it may call this function in order
 * to have the device which sent the frame added to the tree of interfaces
 * which is kept on the inbound channel.
 *
 * \param inbound The inbound channel
 * \param outbound The outbound channel (The one from which the CC frame was read)
 * \param frame_data The ast_frame's data.ptr field.
 * \retval void
 */
void ast_handle_cc_control_frame(struct ast_channel *inbound, struct ast_channel *outbound, void *frame_data);

/*!
 * \since 1.8
 *
 * \brief Start the CC process on a call.
 *
 * \details
 * Whenever a CC-capable application, such as Dial, wishes to
 * engage in CC activity, it initiates the process by calling this
 * function. If the CC core should discover that a previous application
 * has called ast_ignore_cc on this channel or a "parent" channel, then
 * the value of the ignore_cc integer passed in will be set nonzero.
 *
 * The ignore_cc parameter is a convenience parameter. It can save an
 * application the trouble of trying to call CC APIs when it knows that
 * it should just ignore further attempts at CC actions.
 *
 * \param chan The inbound channel calling the CC-capable application.
 * \param[out] ignore_cc Will be set non-zero if no further CC actions need to be taken
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_cc_call_init(struct ast_channel *chan, int *ignore_cc);

/*!
 * \since 1.8
 *
 * \brief Add a child dialstring to an extension monitor
 *
 * Whenever we request a channel, the parent extension monitor needs
 * to store the dialstring of the device requested. The reason is so
 * that we can call the device back during the recall even if we are
 * not monitoring the device.
 *
 * \param incoming The caller's channel
 * \param dialstring The dialstring used when requesting the outbound channel
 * \param device_name The device name associated with the requested outbound channel
 * \retval void
 */
void ast_cc_extension_monitor_add_dialstring(struct ast_channel *incoming, const char * const dialstring, const char * const device_name);

/*!
 * \since 1.8
 * \brief Check if the incoming CC request is within the bounds
 * set by the cc_max_requests configuration option
 *
 * \details
 * It is recommended that an entity which receives an incoming
 * CC request calls this function before calling
 * ast_cc_agent_accept_request. This way, immediate feedback can be
 * given to the caller about why his request was rejected.
 *
 * If this is not called and a state change to CC_CALLER_REQUESTED
 * is made, then the core will still not allow for the request
 * to succeed. However, if done this way, it may not be obvious
 * to the requestor why the request failed.
 *
 * \retval 0 Not within the limits. Fail.
 * \retval non-zero Within the limits. Success.
 */
int ast_cc_request_is_within_limits(void);

/*!
 * \since 1.8
 * \brief Get the core id for the current call
 *
 * \details
 * The main use of this function is for channel drivers
 * who queue an AST_CONTROL_CC frame. A channel driver may
 * call this function in order to get the core_id for what
 * may become a CC request. This way, when monitor functions
 * are called which use a core_id as a means of identification,
 * the channel driver will have saved this information.
 *
 * The channel given to this function may be an inbound or outbound
 * channel. Both will have the necessary info on it.
 *
 * \param chan The channel from which to get the core_id.
 * \retval core_id on success
 * \retval -1 Failure
 */
int ast_cc_get_current_core_id(struct ast_channel *chan);

/* END API FOR IN-CALL CC HANDLING */

/*!
 * \brief Structure with information about an outbound interface
 *
 * \details
 * This structure is first created when an outbound interface indicates that
 * it is capable of accepting a CC request. It is stored in a "tree" on a datastore on
 * the caller's channel. Once an agent structure is created, the agent gains
 * a reference to the tree of interfaces. If CC is requested, then the
 * interface tree on the agent is converted into a tree of monitors. Each
 * monitor will contain a pointer to an individual ast_cc_interface. Finally,
 * the tree of interfaces is also present on a second datastore during a
 * CC recall so that the CC_INTERFACES channel variable may be properly
 * populated.
 */
struct ast_cc_interface {
	/* What class of monitor is being offered here
	 */
	enum ast_cc_monitor_class monitor_class;
	/*!
	 * \brief The type of monitor that should be used for this interface
	 *
	 * \details
	 * This will be something like "extension" "generic" or "SIP".
	 * This should point to a static const char *, so there is
	 * no reason to make a new copy.
	 */
	const char *monitor_type;
	/*!
	 * The configuration parameters used for this interface
	 */
	struct ast_cc_config_params *config_params;
	/* The name of the interface/extension. local channels will
	 * have 'exten@context' for a name. Other channel types will
	 * have 'tech/device' for a name.
	 */
	char device_name[1];
};

/* BEGIN STRUCTURES FOR AGENTS */

struct ast_cc_agent {
	/*!
	 * Which instance of the core state machine does this
	 * agent pertain to?
	 */
	unsigned int core_id;
	/*!
	 * Callback functions needed for specific agent
	 * implementations
	 */
	const struct ast_cc_agent_callbacks *callbacks;
	/*!
	 * Configuration parameters that affect this
	 * agent's operation.
	 */
	struct ast_cc_config_params *cc_params;
	/*!
	 * \brief Flags for agent operation
	 *
	 * \details
	 * There are some attributes of certain agent types
	 * that can alter the behavior of certain CC functions.
	 * For a list of these flags, see the ast_cc_agent_flags
	 * enum
	 */
	unsigned int flags;
	/*! Data specific to agent implementation */
	void *private_data;
	/*! The name of the device which this agent
	 * represents/communicates with
	 */
	char device_name[1];
};

enum ast_cc_agent_response_reason {
	/*! CC request accepted */
	AST_CC_AGENT_RESPONSE_SUCCESS,
	/*! CC request not allowed at this time. Invalid state transition. */
	AST_CC_AGENT_RESPONSE_FAILURE_INVALID,
	/*! Too many CC requests in the system. */
	AST_CC_AGENT_RESPONSE_FAILURE_TOO_MANY,
};

struct ast_cc_agent_callbacks {
	/*!
	 * \brief Type of agent the callbacks belong to.
	 *
	 * \note
	 * Examples are "SIP" "ISDN" and "generic"
	 */
	const char *type;
	/*!
	 * \brief CC agent initialization.
	 *
	 * \param agent CC core agent control.
	 * \param chan Original channel the agent will attempt to recall.
	 *
	 * \details
	 * This callback is called when the CC core
	 * is initialized. Agents should allocate
	 * any private data necessary for the
	 * call and assign it to the private_data
	 * on the agent. Additionally, if any ast_cc_agent_flags
	 * are pertinent to the specific agent type, they should
	 * be set in this function as well.
	 *
	 * \retval 0 on success.
	 * \retval -1 on error.
	 */
	int (*init)(struct ast_cc_agent *agent, struct ast_channel *chan);
	/*!
	 * \brief Start the offer timer.
	 *
	 * \param agent CC core agent control.
	 *
	 * \details
	 * This is called by the core when the caller hangs up after
	 * a call for which CC may be requested. The agent should
	 * begin the timer as configured.
	 *
	 * The primary reason why this functionality is left to
	 * the specific agent implementations is due to the differing
	 * use of schedulers throughout the code. Some channel drivers
	 * may already have a scheduler context they wish to use, and
	 * amongst those, some may use the ast_sched API while others
	 * may use the ast_sched_thread API, which are incompatible.
	 *
	 * \retval 0 on success.
	 * \retval -1 on error.
	 */
	int (*start_offer_timer)(struct ast_cc_agent *agent);
	/*!
	 * \brief Stop the offer timer.
	 *
	 * \param agent CC core agent control.
	 *
	 * \details
	 * This callback is called by the CC core when the caller
	 * has requested CC.
	 *
	 * \retval 0 on success.
	 * \retval -1 on error.
	 */
	int (*stop_offer_timer)(struct ast_cc_agent *agent);
	/*!
	 * \brief Respond to a CC request.
	 *
	 * \param agent CC core agent control.
	 * \param reason CC request response status.
	 *
	 * \details
	 * When the core receives knowledge that a called
	 * party has accepted a CC request, it will call
	 * this callback. The core may also call this
	 * if there is some error when attempting to process
	 * the incoming CC request.
	 *
	 * The duty of this is to issue a propper response to a
	 * CC request from the caller by acknowledging receipt
	 * of that request or rejecting it.
	 */
	void (*respond)(struct ast_cc_agent *agent, enum ast_cc_agent_response_reason reason);
	/*!
	 * \brief Request the status of the agent's device.
	 *
	 * \param agent CC core agent control.
	 *
	 * \details
	 * Asynchronous request for the status of any caller
	 * which may be a valid caller for the CC transaction.
	 * Status responses should be made using the
	 * ast_cc_status_response function.
	 *
	 * \retval 0 on success.
	 * \retval -1 on error.
	 */
	int (*status_request)(struct ast_cc_agent *agent);
	/*!
	 * \brief Request for an agent's phone to stop ringing.
	 *
	 * \param agent CC core agent control.
	 *
	 * \details
	 * The usefulness of this is quite limited. The only specific
	 * known case for this is if Asterisk requests CC over an ISDN
	 * PTMP link as the TE side. If other phones are in the same
	 * recall group as the Asterisk server, and one of those phones
	 * picks up the recall notice, then Asterisk will receive a
	 * "stop ringing" notification from the NT side of the PTMP
	 * link. This indication needs to be passed to the phone
	 * on the other side of the Asterisk server which originally
	 * placed the call so that it will stop ringing. Since the
	 * phone may be of any type, it is necessary to have a callback
	 * that the core can know about.
	 *
	 * \retval 0 on success.
	 * \retval -1 on error.
	 */
	int (*stop_ringing)(struct ast_cc_agent *agent);
	/*!
	 * \brief Let the caller know that the callee has become free
	 * but that the caller cannot attempt to call back because
	 * he is either busy or there is congestion on his line.
	 *
	 * \param agent CC core agent control.
	 *
	 * \details
	 * This is something that really only affects a scenario where
	 * a phone places a call over ISDN PTMP to Asterisk, who then
	 * connects over PTMP again to the ISDN network. For most agent
	 * types, there is no need to implement this callback at all
	 * because they don't really need to actually do anything in
	 * this situation. If you're having trouble understanding what
	 * the purpose of this callback is, then you can be safe simply
	 * not implementing it.
	 *
	 * \retval 0 on success.
	 * \retval -1 on error.
	 */
	int (*party_b_free)(struct ast_cc_agent *agent);
	/*!
	 * \brief Begin monitoring a busy device.
	 *
	 * \param agent CC core agent control.
	 *
	 * \details
	 * The core will call this callback if the callee becomes
	 * available but the caller has reported that he is busy.
	 * The agent should begin monitoring the caller's device.
	 * When the caller becomes available again, the agent should
	 * call ast_cc_agent_caller_available.
	 *
	 * \retval 0 on success.
	 * \retval -1 on error.
	 */
	int (*start_monitoring)(struct ast_cc_agent *agent);
	/*!
	 * \brief Alert the caller that it is time to try recalling.
	 *
	 * \param agent CC core agent control.
	 *
	 * \details
	 * The core will call this function when it receives notice
	 * that a monitored party has become available.
	 *
	 * The agent's job is to send a message to the caller to
	 * notify it of such a change. If the agent is able to
	 * discern that the caller is currently unavailable, then
	 * the agent should react by calling the ast_cc_caller_unavailable
	 * function.
	 *
	 * \retval 0 on success.
	 * \retval -1 on error.
	 */
	int (*callee_available)(struct ast_cc_agent *agent);
	/*!
	 * \brief Destroy private data on the agent.
	 *
	 * \param agent CC core agent control.
	 *
	 * \details
	 * The core will call this function upon completion
	 * or failure of CC.
	 *
	 * \note
	 * The agent private_data pointer may be NULL if the agent
	 * constructor failed.
	 */
	void (*destructor)(struct ast_cc_agent *agent);
};

/*!
 * \brief Call a callback on all agents of a specific type
 *
 * \details
 * Since the container of CC core instances is private, and so
 * are the items which the container contains, we have to provide
 * an ao2_callback-like method so that a specific agent may be
 * found or so that an operation can be made on all agents of
 * a particular type. The first three arguments should be familiar
 * to anyone who has used ao2_callback. The final argument is the
 * type of agent you wish to have the callback called on.
 *
 * \note Since agents are refcounted, and this function returns
 * a reference to the agent, it is imperative that you decrement
 * the refcount of the agent once you have finished using it.
 *
 * \param flags astobj2 search flags
 * \param function an ao2 callback function to call
 * \param arg the argument to the callback function
 * \param type The type of agents to call the callback on
 */
struct ast_cc_agent *ast_cc_agent_callback(int flags, ao2_callback_fn *function, void *arg, const char * const type);

/* END STRUCTURES FOR AGENTS */

/* BEGIN STATE CHANGE API */

/*!
 * \since 1.8
 * \brief Offer CC to a caller
 *
 * \details
 * This function is called from ast_hangup if the caller is
 * eligible to be offered call completion service.
 *
 * \param caller_chan The calling channel
 * \retval -1 Error
 * \retval 0 Success
 */
int ast_cc_offer(struct ast_channel *caller_chan);

/*!
 * \since 1.8
 * \brief Accept inbound CC request
 *
 * \details
 * When a caller requests CC, this function should be called to let
 * the core know that the request has been accepted.
 *
 * \param core_id core_id of the CC transaction
 * \param debug optional string to print for debugging purposes
 * \retval 0 Success
 * \retval -1 Failure
 */
int __attribute__((format(printf, 2, 3))) ast_cc_agent_accept_request(int core_id, const char * const debug, ...);

/*!
 * \since 1.8
 * \brief Indicate that an outbound entity has accepted our CC request
 *
 * \details
 * When we receive confirmation that an outbound device has accepted the
 * CC request we sent it, this function must be called.
 *
 * \param core_id core_id of the CC transaction
 * \param debug optional string to print for debugging purposes
 * \retval 0 Success
 * \retval -1 Failure
 */
int __attribute__((format(printf, 2, 3))) ast_cc_monitor_request_acked(int core_id, const char * const debug, ...);

/*!
 * \since 1.8
 * \brief Indicate that the caller is busy
 *
 * \details
 * When the callee makes it known that he is available, the core
 * will let the caller's channel driver know that it may attempt
 * to let the caller know to attempt a recall. If the channel
 * driver can detect, though, that the caller is busy, then
 * the channel driver should call this function to let the CC
 * core know.
 *
 * \param core_id core_id of the CC transaction
 * \param debug optional string to print for debugging purposes
 * \retval 0 Success
 * \retval -1 Failure
 */
int __attribute__((format(printf, 2, 3))) ast_cc_agent_caller_busy(int core_id, const char * const debug, ...);

/*!
 * \since 1.8
 * \brief Indicate that a previously unavailable caller has become available
 *
 * \details
 * If a monitor is suspended due to a caller becoming unavailable, then this
 * function should be called to indicate that the caller has become available.
 *
 * \param core_id core_id of the CC transaction
 * \param debug optional string to print for debugging purposes
 * \retval 0 Success
 * \retval -1 Failure
 */
int __attribute__((format(printf, 2, 3))) ast_cc_agent_caller_available(int core_id, const char * const debug, ...);

/*!
 * \since 1.8
 * \brief Tell the CC core that a caller is currently recalling
 *
 * \details
 * The main purpose of this is so that the core can alert the monitor
 * to stop its available timer since the caller has begun its recall
 * phase.
 *
 * \param core_id core_id of the CC transaction
 * \param debug optional string to print for debugging purposes
 * \retval 0 Success
 * \retval -1 Failure
 */
int __attribute__((format(printf, 2, 3))) ast_cc_agent_recalling(int core_id, const char * const debug, ...);

/*!
 * \since 1.8
 * \brief Indicate recall has been acknowledged
 *
 * \details
 * When we receive confirmation that an endpoint has responded to our
 * CC recall, we call this function.
 *
 * \param chan The inbound channel making the CC recall
 * \param debug optional string to print for debugging purposes
 * \retval 0 Success
 * \retval -1 Failure
 */
int __attribute__((format(printf, 2, 3))) ast_cc_completed(struct ast_channel *chan, const char * const debug, ...);

/*!
 * \since 1.8
 * \brief Indicate failure has occurred
 *
 * \details
 * If at any point a failure occurs, this is the function to call
 * so that the core can initiate cleanup procedures.
 *
 * \param core_id core_id of the CC transaction
 * \param debug optional string to print for debugging purposes
 * \retval 0 Success
 * \retval -1 Failure
 */
int __attribute__((format(printf, 2, 3))) ast_cc_failed(int core_id, const char * const debug, ...);

/*!
 * \since 1.8
 * \brief Indicate that a failure has occurred on a specific monitor
 *
 * \details
 * If a monitor should detect that a failure has occurred when communicating
 * with its endpoint, then ast_cc_monitor_failed should be called. The big
 * difference between ast_cc_monitor_failed and ast_cc_failed is that ast_cc_failed
 * indicates a global failure for a CC transaction, where as ast_cc_monitor_failed
 * is localized to a particular monitor. When ast_cc_failed is called, the entire
 * CC transaction is torn down. When ast_cc_monitor_failed is called, only the
 * monitor on which the failure occurred is pruned from the tree of monitors.
 *
 * If there are no more devices left to monitor when this function is called,
 * then the core will fail the CC transaction globally.
 *
 * \param core_id The core ID for the CC transaction
 * \param monitor_name The name of the monitor on which the failure occurred
 * \param debug A debug message to print to the CC log
 * \return void
 */
int __attribute__((format(printf, 3, 4))) ast_cc_monitor_failed(int core_id, const char * const monitor_name, const char * const debug, ...);

/* END STATE CHANGE API */

/*!
 * The following are all functions which are required due to the unique
 * case where Asterisk is acting as the NT side of an ISDN PTMP
 * connection to the caller and as the TE side of an ISDN PTMP connection
 * to the callee. In such a case, there are several times where the
 * PTMP monitor needs information from the agent in order to formulate
 * the appropriate messages to send.
 */

/*!
 * \brief Request the status of a caller or callers.
 *
 * \details
 * When an ISDN PTMP monitor senses that the callee has become
 * available, it needs to know the current status of the caller
 * in order to determine the appropriate response to send to
 * the caller. In order to do this, the monitor calls this function.
 * Responses will arrive asynchronously.
 *
 * \note Zero or more responses may come as a result.
 *
 * \param core_id The core ID of the CC transaction
 *
 * \retval 0 Successfully requested status
 * \retval -1 Failed to request status
 */
int ast_cc_monitor_status_request(int core_id);

/*!
 * \brief Response with a caller's current status
 *
 * \details
 * When an ISDN PTMP monitor requests the caller's status, the
 * agent must respond to the request using this function. For
 * simplicity it is recommended that the devstate parameter
 * be one of AST_DEVICE_INUSE or AST_DEVICE_NOT_INUSE.
 *
 * \param core_id The core ID of the CC transaction
 * \param devstate The current state of the caller to which the agent pertains
 * \retval 0 Successfully responded with our status
 * \retval -1 Failed to respond with our status
 */
int ast_cc_agent_status_response(int core_id, enum ast_device_state devstate);

/*!
 * \brief Alert a caller to stop ringing
 *
 * \details
 * When an ISDN PTMP monitor becomes available, it is assumed
 * that the agent will then cause the caller's phone to ring. In
 * some cases, this is literally what happens. In other cases, it may
 * be that the caller gets a visible indication on his phone that he
 * may attempt to recall the callee. If multiple callers are recalled
 * (since it may be possible to have a group of callers configured as
 * a single party A), and one of those callers picks up his phone, then
 * the ISDN PTMP monitor will alert the other callers to stop ringing.
 * The agent's stop_ringing callback will be called, and it is up to the
 * agent's driver to send an appropriate message to make his caller
 * stop ringing.
 *
 * \param core_id The core ID of the CC transaction
 * \retval 0 Successfully requested for the phone to stop ringing
 * \retval -1 Could not request for the phone to stop ringing
 */
int ast_cc_monitor_stop_ringing(int core_id);

/*!
 * \brief Alert a caller that though the callee has become free, the caller
 * himself is not and may not call back.
 *
 * \details
 * When an ISDN PTMP monitor senses that his monitored party has become
 * available, he will request the status of the called party. If he determines
 * that the caller is currently not available, then he will call this function
 * so that an appropriate message is sent to the caller.
 *
 * Yes, you just read that correctly. The callee asks the caller what his
 * current status is, and if the caller is currently unavailable, the monitor
 * must send him a message anyway. WTF?
 *
 * This function results in the agent's party_b_free callback being called.
 * It is most likely that you will not need to actually implement the
 * party_b_free callback in an agent because it is not likely that you will
 * need to or even want to send a caller a message indicating the callee's
 * status if the caller himself is not also free.
 *
 * \param core_id The core ID of the CC transaction
 * \retval 0 Successfully alerted the core that party B is free
 * \retval -1 Could not alert the core that party B is free
 */
int ast_cc_monitor_party_b_free(int core_id);

/* BEGIN API FOR USE WITH/BY MONITORS */

/*!
 * \since 1.8
 * \brief Return the number of outstanding CC requests to a specific device
 *
 * \note
 * This function will lock the list of monitors stored on every instance of
 * the CC core. Callers of this function should be aware of this and avoid
 * any potential lock ordering problems.
 *
 * \param name The name of the monitored device
 * \param type The type of the monitored device (e.g. "generic")
 * \return The number of CC requests for the monitor
 */
int ast_cc_monitor_count(const char * const name, const char * const type);

/*!
 * \since 1.8
 * \brief Alert the core that a device being monitored has become available.
 *
 * \note
 * The code in the core will take care of making sure that the information gets passed
 * up the ladder correctly.
 *
 * \param core_id The core ID of the corresponding CC transaction
 * \param debug
 * \retval 0 Request successfully queued
 * \retval -1 Request could not be queued
 */
int __attribute__((format(printf, 2, 3))) ast_cc_monitor_callee_available(const int core_id, const char * const debug, ...);

/* END API FOR USE WITH/BY MONITORS */

/* BEGIN API TO BE USED ON CC RECALL */

/*!
 * \since 1.8
 * \brief Set up a CC recall datastore on a channel
 *
 * \details
 * Implementers of protocol-specific CC agents will need to call this
 * function in order for the channel to have the necessary interfaces
 * to recall.
 *
 * This function must be called by the implementer once it has been detected
 * that an inbound call is a cc_recall. After allocating the channel, call this
 * function, followed by ast_cc_set_cc_interfaces_chanvar. While it would be nice to
 * be able to have the core do this automatically, it just cannot be done given
 * the current architecture.
 */
int ast_setup_cc_recall_datastore(struct ast_channel *chan, const int core_id);

/*!
 * \since 1.8
 * \brief Decide if a call to a particular channel is a CC recall
 *
 * \details
 * When a CC recall happens, it is important on the called side to
 * know that the call is a CC recall and not a normal call. This function
 * will determine first if the call in question is a CC recall. Then it
 * will determine based on the chan parameter if the channel is being
 * called is being recalled.
 *
 * As a quick example, let's say a call is placed to SIP/1000 and SIP/1000
 * is currently on the phone. The caller requests CCBS. SIP/1000 finishes
 * his call, and so the caller attempts to recall. Now, the dialplan
 * administrator has set up this second call so that not only is SIP/1000
 * called, but also SIP/2000 is called. If SIP/1000's channel were passed
 * to this function, the return value would be non-zero, but if SIP/2000's
 * channel were passed into this function, then the return would be 0 since
 * SIP/2000 was not one of the original devices dialed.
 *
 * \note
 * This function may be called on a calling channel as well to
 * determine if it is part of a CC recall.
 *
 * \note
 * This function will lock the channel as well as the list of monitors
 * on the channel datastore, though the locks are not held at the same time. Be
 * sure that you have no potential lock order issues here.
 *
 * \param chan The channel to check
 * \param[out] core_id If this is a valid CC recall, the core_id of the failed call
 * will be placed in this output parameter
 * \param monitor_type Clarify which type of monitor type we are looking for if this
 * is happening on a called channel. For incoming channels, this parameter is not used.
 * \retval 0 Either this is not a recall or it is but this channel is not part of the recall
 * \retval non-zero This is a recall and the channel in question is directly involved.
 */
int ast_cc_is_recall(struct ast_channel *chan, int *core_id, const char * const monitor_type);

/*!
 * \since 1.8
 * \brief Get the associated monitor given the device name and core_id
 *
 * \details
 * The function ast_cc_is_recall is helpful for determining if a call to
 * a specific channel is a recall. However, once you have determined that
 * this is a recall, you will most likely need access to the private data
 * within the associated monitor. This function is what one uses to get
 * that monitor.
 *
 * \note
 * This function locks the list of monitors that correspond to the core_id
 * passed in. Be sure that you have no potential lock order issues when
 * calling this function.
 *
 * \param core_id The core ID to which this recall corresponds. This likely will
 * have been obtained using the ast_cc_is_recall function
 * \param device_name Which device to find the monitor for.
 *
 * \retval NULL Appropriate monitor does not exist
 * \retval non-NULL The monitor to use for this recall
 */
struct ast_cc_monitor *ast_cc_get_monitor_by_recall_core_id(const int core_id, const char * const device_name);

/*!
 * \since 1.8
 * \brief Set the first level CC_INTERFACES channel variable for a channel.
 *
 * \note
 * Implementers of protocol-specific CC agents should call this function after
 * calling ast_setup_cc_recall_datastore.
 *
 * \note
 * This function will lock the channel as well as the list of monitors stored
 * on the channel's CC recall datastore, though neither are held at the same
 * time. Callers of this function should be aware of potential lock ordering
 * problems that may arise.
 *
 * \details
 * The CC_INTERFACES channel variable will have the interfaces that should be
 * called back for a specific PBX instance.
 *
 * \param chan The channel to set the CC_INTERFACES variable on
 */
int ast_cc_agent_set_interfaces_chanvar(struct ast_channel *chan);

/*!
 * \since 1.8
 * \brief Set the CC_INTERFACES channel variable for a channel using an
 * extension@context as a starting point
 *
 * \details
 * The CC_INTERFACES channel variable will have the interfaces that should be
 * called back for a specific PBX instance. This version of the function is used
 * mainly by chan_local, wherein we need to set CC_INTERFACES based on an extension
 * and context that appear in the middle of the tree of dialed interfaces
 *
 * \note
 * This function will lock the channel as well as the list of monitors stored
 * on the channel's CC recall datastore, though neither are held at the same
 * time. Callers of this function should be aware of potential lock ordering
 * problems that may arise.
 *
 * \param chan The channel to set the CC_INTERFACES variable on
 * \param extension The name of the extension for which we're setting the variable.
 * This should be in the form of "exten@context"
 */
int ast_set_cc_interfaces_chanvar(struct ast_channel *chan, const char * const extension);

/*!
 * \since 1.8
 * \brief Make CCBS available in the case that ast_call fails
 *
 * In some situations, notably if a call-limit is reached in SIP, ast_call will fail
 * due to Asterisk's knowing that the desired device is currently busy. In such a situation,
 * CCBS should be made available to the caller.
 *
 * One caveat is that this may only be used if generic monitoring is being used. The reason
 * is that since Asterisk determined that the device was busy without actually placing a call to it,
 * the far end will have no idea what call we are requesting call completion for if we were to send
 * a call completion request.
 */
void ast_cc_call_failed(struct ast_channel *incoming, struct ast_channel *outgoing, const char * const dialstring);

/*!
 * \since 1.8
 * \brief Callback made from ast_cc_callback for certain channel types
 *
 * \param inbound Incoming asterisk channel.
 * \param cc_params The CC configuration parameters for the outbound target
 * \param monitor_type The type of monitor to use when CC is requested
 * \param device_name The name of the outbound target device.
 * \param dialstring The dial string used when calling this specific interface
 * \param private_data If a native monitor is being used, and some channel-driver-specific private
 * data has been allocated, then this parameter should contain a pointer to that data. If using a generic
 * monitor, this parameter should remain NULL. Note that if this function should fail at some point,
 * it is the responsibility of the caller to free the private data upon return.
 *
 * \details
 * For channel types that fail ast_request when the device is busy, we call into the
 * channel driver with ast_cc_callback. This is the callback that is called in that
 * case for each device found which could have been returned by ast_request.
 *
 * This function creates a CC control frame payload, simulating the act of reading
 * it from the nonexistent outgoing channel's frame queue. We then handle this
 * simulated frame just as we would a normal CC frame which had actually been queued
 * by the channel driver.
 */
void ast_cc_busy_interface(struct ast_channel *inbound, struct ast_cc_config_params *cc_params,
	const char *monitor_type, const char * const device_name, const char * const dialstring, void *private_data);

/*!
 * \since 1.8
 * \brief Create a CC Control frame
 *
 * \details
 * chan_dahdi is weird. It doesn't seem to actually queue frames when it needs to tell
 * an application something. Instead it wakes up, tells the application that it has data
 * ready, and then based on set flags, creates the proper frame type. For chan_dahdi, we
 * provide this function. It provides us the data we need, and we'll make its frame for it.
 *
 * \param chan A channel involved in the call. What we want is on a datastore on both incoming 
 * and outgoing so either may be provided
 * \param cc_params The CC configuration parameters for the outbound target
 * \param monitor_type The type of monitor to use when CC is requested
 * \param device_name The name of the outbound target device.
 * \param dialstring The dial string used when calling this specific interface
 * \param service What kind of CC service is being offered. (CCBS/CCNR/etc...)
 * \param private_data If a native monitor is being used, and some channel-driver-specific private
 * data has been allocated, then this parameter should contain a pointer to that data. If using a generic
 * monitor, this parameter should remain NULL. Note that if this function should fail at some point,
 * it is the responsibility of the caller to free the private data upon return.
 * \param[out] frame The frame we will be returning to the caller. It is vital that ast_frame_free be 
 * called on this frame since the payload will be allocated on the heap.
 * \retval -1 Failure. At some point there was a failure. Do not attempt to use the frame in this case.
 * \retval 0 Success
 */
int ast_cc_build_frame(struct ast_channel *chan, struct ast_cc_config_params *cc_params,
	const char *monitor_type, const char * const device_name,
	const char * const dialstring, enum ast_cc_service_type service, void *private_data,
	struct ast_frame *frame);


/*!
 * \brief Callback made from ast_cc_callback for certain channel types
 * \since 1.8
 *
 * \param chan A channel involved in the call. What we want is on a datastore on both incoming and outgoing so either may be provided
 * \param cc_params The CC configuration parameters for the outbound target
 * \param monitor_type The type of monitor to use when CC is requested
 * \param device_name The name of the outbound target device.
 * \param dialstring The dial string used when calling this specific interface
 * \param private_data If a native monitor is being used, and some channel-driver-specific private
 * data has been allocated, then this parameter should contain a pointer to that data. If using a generic
 * monitor, this parameter should remain NULL. Note that if this function should fail at some point,
 * it is the responsibility of the caller to free the private data upon return.
 *
 * \details
 * For channel types that fail ast_request when the device is busy, we call into the
 * channel driver with ast_cc_callback. This is the callback that is called in that
 * case for each device found which could have been returned by ast_request.
 *
 * \return Nothing
 */
typedef void (*ast_cc_callback_fn)(struct ast_channel *chan, struct ast_cc_config_params *cc_params,
	const char *monitor_type, const char * const device_name, const char * const dialstring, void *private_data);

/*!
 * \since 1.8
 * \brief Run a callback for potential matching destinations.
 *
 * \note
 * See the explanation in ast_channel_tech::cc_callback for more
 * details.
 *
 * \param inbound
 * \param tech Channel technology to use
 * \param dest Channel/group/peer or whatever the specific technology uses
 * \param callback Function to call when a target is reached
 * \retval Always 0, I guess.
 */
int ast_cc_callback(struct ast_channel *inbound, const char * const tech, const char * const dest, ast_cc_callback_fn callback);

/*!
 * \since 1.8
 * \brief Initialize CCSS
 *
 * Performs startup routines necessary for CC operation.
 *
 * \retval 0 Success
 * \retval nonzero Failure
 */
int ast_cc_init(void);

#endif /* _ASTERISK_CCSS_H */
