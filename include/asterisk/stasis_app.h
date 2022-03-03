/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

#ifndef _ASTERISK_STASIS_APP_H
#define _ASTERISK_STASIS_APP_H

/*! \file
 *
 * \brief Stasis Application API. See \ref res_stasis "Stasis Application API"
 * for detailed documentation.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 *
 * \page res_stasis Stasis Application API
 *
 * This is the API that binds the Stasis dialplan application to external
 * Stasis applications, such as \c res_stasis_websocket.
 *
 * The associated \c res_stasis module registers a dialplan function named \c
 * Stasis, which uses \c res_stasis to put a channel into the named Stasis
 * app. As a channel enters and leaves the Stasis diaplan application, the
 * Stasis app receives a \c 'stasis-start' and \c 'stasis-end' events.
 *
 * Stasis apps register themselves using the \ref stasis_app_register and
 * stasis_app_unregister functions. Messages are sent to an appliction using
 * \ref stasis_app_send.
 *
 * Finally, Stasis apps control channels through the use of the \ref
 * stasis_app_control object, and the family of \c stasis_app_control_*
 * functions.
 *
 * Since module unload order is based on reference counting, any module that
 * uses the API defined in this file must list "res_stasis" in the requires
 * field.
 */

#include "asterisk/channel.h"

/*! @{ */

/*!
 * \brief Callback for Stasis application handler.
 *
 * The message given to the handler is a borrowed copy. If you want to keep a
 * reference to it, you should use \c ao2_ref() to keep it around.
 *
 * \param data Data ptr given when registered.
 * \param app_name Name of the application being dispatched to.
 * \param message Message to handle. (borrowed copy)
 */
typedef void (*stasis_app_cb)(void *data, const char *app_name,
	struct ast_json *message);

/*!
 * \brief Gets the names of all registered Stasis applications.
 *
 * \return \c ast_str_container of container names.
 * \retval NULL on error.
 */
struct ao2_container *stasis_app_get_all(void);

/*!
 * \brief Retrieve a handle to a Stasis application by its name
 *
 * \param name The name of the registered Stasis application
 *
 * \return \c stasis_app on success.
 * \retval NULL on error.
 */
struct stasis_app *stasis_app_get_by_name(const char *name);

/*!
 * \brief Register a new Stasis application.
 *
 * If an application is already registered with the given name, the old
 * application is sent a 'replaced' message and unregistered.
 *
 * \param app_name Name of this application.
 * \param handler Callback for application messages.
 * \param data Data blob to pass to the callback. Must be AO2 managed.
 *
 * \return 0 for success
 * \return -1 for error.
 */
int stasis_app_register(const char *app_name, stasis_app_cb handler, void *data);

/*!
 * \brief Register a new Stasis application that receives all Asterisk events.
 *
 * If an application is already registered with the given name, the old
 * application is sent a 'replaced' message and unregistered.
 *
 * \param app_name Name of this application.
 * \param handler Callback for application messages.
 * \param data Data blob to pass to the callback. Must be AO2 managed.
 *
 * \return 0 for success
 * \return -1 for error.
 */
int stasis_app_register_all(const char *app_name, stasis_app_cb handler, void *data);

/*!
 * \brief Unregister a Stasis application.
 * \param app_name Name of the application to unregister.
 */
void stasis_app_unregister(const char *app_name);

/*!
 * \brief Send a message to the given Stasis application.
 *
 * The message given to the handler is a borrowed copy. If you want to keep a
 * reference to it, you should use \c ao2_ref() to keep it around.
 *
 * \param app_name Name of the application to invoke.
 * \param message Message to send (borrowed reference)
 *
 * \return 0 for success.
 * \return -1 for error.
 */
int stasis_app_send(const char *app_name, struct ast_json *message);

/*! \brief Forward declare app */
struct stasis_app;

/*!
 * \brief Retrieve an application's name
 *
 * \param app An application
 *
 * \return The name of the application.
 */
const char *stasis_app_name(const struct stasis_app *app);

/*!
 * \brief Return the JSON representation of a Stasis application.
 * \since 16.3.0
 *
 * \param app The application.
 *
 * \return JSON representation of app with given name.
 * \retval NULL on error.
 */
struct ast_json *stasis_app_object_to_json(struct stasis_app *app);

/*!
 * \brief Return the JSON representation of a Stasis application.
 *
 * \param app_name Name of the application.
 *
 * \return JSON representation of app with given name.
 * \retval NULL on error.
 */
struct ast_json *stasis_app_to_json(const char *app_name);

/*!
 * \brief Event source information and callbacks.
 */
struct stasis_app_event_source {
	/*! \brief The scheme to match against on [un]subscribes */
	const char *scheme;

	/*!
	 * \brief Find an event source data object by the given id/name.
	 *
	 * \param app Application
	 * \param id A unique identifier to search on
	 *
	 * \return The data object associated with the id/name.
	 */
	void *(*find)(const struct stasis_app *app, const char *id);

	/*!
	 * \brief Subscribe an application to an event source.
	 *
	 * \param app Application
	 * \param obj an event source data object
	 *
	 * \return 0 on success, failure code otherwise
	 */
	int (*subscribe)(struct stasis_app *app, void *obj);

	/*!
	 * \brief Cancel the subscription an app has to an event source.
	 *
	 * \param app Application
	 * \param id a previously subscribed object id
	 *
	 * \return 0 on success, failure code otherwise
	 */
	int (*unsubscribe)(struct stasis_app *app, const char *id);

	/*!
	 * \brief Find an event source by the given id/name.
	 *
	 * \param app Application
	 * \param id A unique identifier to check
	 *
	 * \return true if id is subscribed, false otherwise.
	 */
	int (*is_subscribed)(struct stasis_app *app, const char *id);

	/*!
	 * \brief Convert event source data to json
	 *
	 * \param app Application
	 * \param id json object to fill
	 */
	void (*to_json)(const struct stasis_app *app, struct ast_json *json);

	/*! Next item in the list */
	AST_LIST_ENTRY(stasis_app_event_source) next;
};

/*!
 * \brief Register an application event source.
 *
 * \param obj the event source to register
 */
void stasis_app_register_event_source(struct stasis_app_event_source *obj);

/*!
 * \brief Register core event sources.
 */
void stasis_app_register_event_sources(void);

/*!
 * \brief Unregister an application event source.
 *
 * \param obj the event source to unregister
 */
void stasis_app_unregister_event_source(struct stasis_app_event_source *obj);

/*!
 * \brief Unregister core event sources.
 */
void stasis_app_unregister_event_sources(void);

/*! \brief Return code for stasis_app_user_event */
enum stasis_app_user_event_res {
	STASIS_APP_USER_OK,
	STASIS_APP_USER_APP_NOT_FOUND,
	STASIS_APP_USER_EVENT_SOURCE_NOT_FOUND,
	STASIS_APP_USER_EVENT_SOURCE_BAD_SCHEME,
	STASIS_APP_USER_USEREVENT_INVALID,
	STASIS_APP_USER_INTERNAL_ERROR,
};

/*!
 * \brief Generate a Userevent for stasis app (echo to AMI)
 *
 * \param app_name Name of the application to generate event for/to.
 * \param event_name Name of the Userevent.
 * \param source_uris URIs for the source objects to attach to event.
 * \param sources_count Array size of source_uris.
 * \param json_variables event blob variables.
 *
 * \return \ref stasis_app_user_event_res return code.
 */
enum stasis_app_user_event_res stasis_app_user_event(const char *app_name,
	const char *event_name,
	const char **source_uris, int sources_count,
	struct ast_json *json_variables);


/*! \brief Return code for stasis_app_[un]subscribe */
enum stasis_app_subscribe_res {
	STASIS_ASR_OK,
	STASIS_ASR_APP_NOT_FOUND,
	STASIS_ASR_EVENT_SOURCE_NOT_FOUND,
	STASIS_ASR_EVENT_SOURCE_BAD_SCHEME,
	STASIS_ASR_INTERNAL_ERROR,
};

/*!
 * \brief Subscribes an application to a list of event sources.
 *
 * \param app_name Name of the application to subscribe.
 * \param event_source_uris URIs for the event sources to subscribe to.
 * \param event_sources_count Array size of event_source_uris.
 * \param json Optional output pointer for JSON representation of the app
 *             after adding the subscription.
 *
 * \return \ref stasis_app_subscribe_res return code.
 *
 * \note Do not hold any channel locks if subscribing to a channel.
 */
enum stasis_app_subscribe_res stasis_app_subscribe(const char *app_name,
	const char **event_source_uris, int event_sources_count,
	struct ast_json **json);

/*!
 * \brief Unsubscribes an application from a list of event sources.
 *
 * \param app_name Name of the application to subscribe.
 * \param event_source_uris URIs for the event sources to subscribe to.
 * \param event_sources_count Array size of event_source_uris.
 * \param json Optional output pointer for JSON representation of the app
 *             after adding the subscription.
 *
 * \return \ref stasis_app_subscribe_res return code.
 */
enum stasis_app_subscribe_res stasis_app_unsubscribe(const char *app_name,
	const char **event_source_uris, int event_sources_count,
	struct ast_json **json);

/*!
 * \brief Directly subscribe an application to a channel
 *
 * \param app_name Name of the application to subscribe.
 * \param chan The channel to subscribe to
 *
 * \return \ref stasis_app_subscribe_res return code.
 *
 * \note This method can be used when you already hold a channel and its
 *       lock. This bypasses the channel lookup that would normally be
 *       performed by \ref stasis_app_subscribe.
 */
enum stasis_app_subscribe_res stasis_app_subscribe_channel(const char *app_name,
	struct ast_channel *chan);

/*! @} */

/*! @{ */

/*! \brief Handler for controlling a channel that's in a Stasis application */
struct stasis_app_control;

/*! \brief Rule to check to see if an operation is allowed */
struct stasis_app_control_rule {
	/*!
	 * \brief Checks to see if an operation is allowed on the control
	 *
	 * \param control Control object to check
	 * \return 0 on success, otherwise a failure code
	 */
	enum stasis_app_control_channel_result (*check_rule)(
		const struct stasis_app_control *control);
	/*! Next item in the list */
	AST_LIST_ENTRY(stasis_app_control_rule) next;
};

/*!
 * \brief Registers an add channel to bridge rule.
 *
 * \param control Control object
 * \param rule The rule to register
 */
void stasis_app_control_register_add_rule(
	struct stasis_app_control *control,
	struct stasis_app_control_rule *rule);

/*!
 * \brief UnRegister an add channel to bridge rule.
 *
 * \param control Control object
 * \param rule The rule to unregister
 */
void stasis_app_control_unregister_add_rule(
	struct stasis_app_control *control,
	struct stasis_app_control_rule *rule);

/*!
 * \brief Registers a remove channel from bridge rule.
 *
 * \param control Control object
 * \param rule The rule to register
 */
void stasis_app_control_register_remove_rule(
	struct stasis_app_control *control,
	struct stasis_app_control_rule *rule);

/*!
 * \brief Unregisters a remove channel from bridge rule.
 *
 * \param control Control object
 * \param rule The rule to unregister
 */
void stasis_app_control_unregister_remove_rule(
	struct stasis_app_control *control,
	struct stasis_app_control_rule *rule);

/*!
 * \brief Returns the handler for the given channel.
 * \param chan Channel to handle.
 *
 * \return NULL channel not in Stasis application.
 * \return Pointer to \c res_stasis handler.
 */
struct stasis_app_control *stasis_app_control_find_by_channel(
	const struct ast_channel *chan);

/*!
 * \brief Returns the handler for the channel with the given id.
 * \param channel_id Uniqueid of the channel.
 *
 * \return NULL channel not in Stasis application, or channel does not exist.
 * \return Pointer to \c res_stasis handler.
 */
struct stasis_app_control *stasis_app_control_find_by_channel_id(
	const char *channel_id);

/*!
 * \brief Creates a control handler for a channel that isn't in a stasis app.
 * \since 12.0.0
 *
 * \param chan Channel to create controller handle for
 *
 * \return NULL on failure to create the handle
 * \return Pointer to \c res_stasis handler.
 */
struct stasis_app_control *stasis_app_control_create(
	struct ast_channel *chan);

/*!
 * \brief Act on a stasis app control queue until it is empty
 * \since 12.0.0
 *
 * \param chan Channel to handle
 * \param control Control object to execute
 */
void stasis_app_control_execute_until_exhausted(
	struct ast_channel *chan,
	struct stasis_app_control *control);

/*!
 * \brief Check if a control is marked as done
 * \since 12.2.0
 *
 * \param control Which control object is being evaluated
 */
int stasis_app_control_is_done(
	struct stasis_app_control *control);

/*!
 * \brief Flush the control command queue.
 * \since 13.9.0
 *
 * \param control Control object to flush command queue.
 */
void stasis_app_control_flush_queue(struct stasis_app_control *control);

/*!
 * \brief Returns the uniqueid of the channel associated with this control
 *
 * \param control Control object.
 *
 * \return Uniqueid of the associate channel.
 * \retval NULL if \a control is \c NULL.
 */
const char *stasis_app_control_get_channel_id(
	const struct stasis_app_control *control);

/*!
 * \brief Apply a bridge role to a channel controlled by a stasis app control
 *
 * \param control Control for \c res_stasis
 * \param role Role to apply
 *
 * \return 0 for success
 * \return -1 for error.
 */
int stasis_app_control_add_role(struct stasis_app_control *control, const char *role);

/*!
 * \brief Clear bridge roles currently applied to a channel controlled by a stasis app control
 *
 * \param control Control for \c res_stasis
 */
void stasis_app_control_clear_roles(struct stasis_app_control *control);

/*!
 * \brief Exit \c res_stasis and continue execution in the dialplan.
 *
 * If the channel is no longer in \c res_stasis, this function does nothing.
 *
 * \param control Control for \c res_stasis
 * \param context An optional context to continue to
 * \param extension An optional extension to continue to
 * \param priority An optional priority to continue to
 *
 * \return 0 for success
 * \return -1 for error.
 */
int stasis_app_control_continue(struct stasis_app_control *control, const char *context, const char *extension, int priority);

/*!
 * \brief Exit \c res_stasis and move to another Stasis application.
 *
 * If the channel is no longer in \c res_stasis, this function does nothing.
 *
 * \param control Control for \c res_stasis
 * \param app_name The name of the application to switch to
 * \param app_args The list of arguments to pass to the application
 *
 * \return 0 for success
 * \return -1 for error
 */
int stasis_app_control_move(struct stasis_app_control *control, const char *app_name, const char *app_args);

/*!
 * \brief Redirect a channel in \c res_stasis to a particular endpoint
 *
 * \param control Control for \c res_stasis
 * \param endpoint The endpoint transfer string where the channel should be sent to
 *
 * \return 0 for success
 * \return -1 for error
 */
int stasis_app_control_redirect(struct stasis_app_control *control, const char *endpoint);

/*!
 * \brief Indicate ringing to the channel associated with this control.
 *
 * \param control Control for \c res_stasis.
 *
 * \return 0 for success.
 * \return -1 for error.
 */
int stasis_app_control_ring(struct stasis_app_control *control);

/*!
 * \brief Stop locally generated ringing on the channel associated with this control.
 *
 * \param control Control for \c res_stasis.
 *
 * \return 0 for success.
 * \return -1 for error.
 */
int stasis_app_control_ring_stop(struct stasis_app_control *control);

/*!
 * \brief Send DTMF to the channel associated with this control.
 *
 * \param control Control for \c res_stasis.
 * \param dtmf DTMF string.
 * \param before Amount of time to wait before sending DTMF digits.
 * \param between Amount of time between each DTMF digit.
 * \param duration Amount of time each DTMF digit lasts for.
 * \param after Amount of time to wait after sending DTMF digits.
 *
 * \return 0 for success.
 * \return -1 for error.
 */
int stasis_app_control_dtmf(struct stasis_app_control *control, const char *dtmf, int before, int between, unsigned int duration, int after);

/*!
 * \brief Mute the channel associated with this control.
 *
 * \param control Control for \c res_stasis.
 * \param direction The direction in which the audio should be muted.
 * \param frametype The type of stream that should be muted.
 *
 * \return 0 for success
 * \return -1 for error.
 */
int stasis_app_control_mute(struct stasis_app_control *control, unsigned int direction, enum ast_frame_type frametype);

/*!
 * \brief Unmute the channel associated with this control.
 *
 * \param control Control for \c res_stasis.
 * \param direction The direction in which the audio should be unmuted.
 * \param frametype The type of stream that should be unmuted.
 *
 * \return 0 for success
 * \return -1 for error.
 */
int stasis_app_control_unmute(struct stasis_app_control *control, unsigned int direction, enum ast_frame_type frametype);

/*!
 * \brief Answer the channel associated with this control.
 * \param control Control for \c res_stasis.
 * \return 0 for success.
 * \return Non-zero for error.
 */
int stasis_app_control_answer(struct stasis_app_control *control);

/*!
 * \brief Set a variable on the channel associated with this control to value.
 * \param control Control for \c res_stasis.
 * \param variable The name of the variable
 * \param value The value to set the variable to
 *
 * \return 0 for success.
 * \return -1 for error.
 */
int stasis_app_control_set_channel_var(struct stasis_app_control *control, const char *variable, const char *value);

/*!
 * \brief Place the channel associated with the control on hold.
 * \param control Control for \c res_stasis.
 */
void stasis_app_control_hold(struct stasis_app_control *control);

/*!
 * \brief Remove the channel associated with the control from hold.
 * \param control Control for \c res_stasis.
 */
void stasis_app_control_unhold(struct stasis_app_control *control);

/*!
 * \brief Play music on hold to a channel (does not affect hold status)
 * \param control Control for \c res_stasis.
 * \param moh_class class of music on hold to play (NULL allowed)
 */
void stasis_app_control_moh_start(struct stasis_app_control *control, const char *moh_class);

/*!
 * \brief Stop playing music on hold to a channel (does not affect hold status)
 * \param control Control for \c res_stasis.
 */
void stasis_app_control_moh_stop(struct stasis_app_control *control);

/*!
 * \brief Start playing silence to a channel.
 * \param control Control for \c res_stasis.
 */
void stasis_app_control_silence_start(struct stasis_app_control *control);

/*!
 * \brief Stop playing silence to a channel.
 * \param control Control for \c res_stasis.
 */
void stasis_app_control_silence_stop(struct stasis_app_control *control);

/*!
 * \brief Returns the most recent snapshot for the associated channel.
 *
 * The returned pointer is AO2 managed, so ao2_cleanup() when you're done.
 *
 * \param control Control for \c res_stasis.
 *
 * \return Most recent snapshot. ao2_cleanup() when done.
 * \retval NULL if channel isn't in cache.
 */
struct ast_channel_snapshot *stasis_app_control_get_snapshot(
	const struct stasis_app_control *control);

/*!
 * \brief Publish a message to the \a control's channel's topic.
 *
 * \param control Control to publish to
 * \param message Message to publish
 */
void stasis_app_control_publish(
	struct stasis_app_control *control, struct stasis_message *message);

/*!
 * \brief Returns the stasis topic for an app
 *
 * \param app Stasis app to get topic of
 */
struct stasis_topic *ast_app_get_topic(struct stasis_app *app);

/*!
 * \brief Queue a control frame without payload.
 *
 * \param control Control to publish to.
 * \param frame_type type of control frame.
 *
 * \return zero on success
 * \return non-zero on failure
 */
int stasis_app_control_queue_control(struct stasis_app_control *control,
	enum ast_control_frame_type frame_type);

/*!
 * \brief Create a bridge of the specified type.
 *
 * \param type The type of bridge to be created
 * \param name Optional name to give to the bridge
 * \param id Optional Unique ID to give to the bridge
 *
 * \return New bridge.
 * \retval NULL on error.
 */
struct ast_bridge *stasis_app_bridge_create(const char *type, const char *name, const char *id);

/*!
 * \brief Create an invisible bridge of the specified type.
 *
 * \param type The type of bridge to be created
 * \param name Optional name to give to the bridge
 * \param id Optional Unique ID to give to the bridge
 *
 * \return New bridge.
 * \retval NULL on error.
 */
struct ast_bridge *stasis_app_bridge_create_invisible(const char *type, const char *name, const char *id);

/*!
 * \brief Returns the bridge with the given id.
 * \param bridge_id Uniqueid of the bridge.
 *
 * \return NULL bridge not created by a Stasis application, or bridge does not exist.
 * \return Pointer to bridge.
 */
struct ast_bridge *stasis_app_bridge_find_by_id(
	const char *bridge_id);

/*!
 * \brief Finds or creates an announcer channel in a bridge that can play music on hold.
 *
 * \param bridge Bridge we want an MOH channel for
 *
 * \return NULL if the music on hold channel fails to be created or join the bridge for any reason.
 * \return Pointer to the ;1 end of the announcer channel chain.
 */
struct ast_channel *stasis_app_bridge_moh_channel(
	struct ast_bridge *bridge);

/*!
 * \brief Breaks down MOH channels playing on the bridge created by stasis_app_bridge_moh_channel
 *
 * \param bridge Bridge we want to stop the MOH on
 *
 * \return -1 if no moh channel could be found and stopped
 * \return 0 on success
 */
int stasis_app_bridge_moh_stop(
	struct ast_bridge *bridge);

/*!
 * \brief Finds an existing ARI playback channel in a bridge
 *
 * \param bridge Bridge we want to find the playback channel for
 *
 * \return NULL if the playback channel can not be found for any reason.
 * \return Pointer to the ;1 end of the playback channel chain.
 */
struct ast_channel *stasis_app_bridge_playback_channel_find(
	struct ast_bridge *bridge);

/*!
 * \brief Adds a channel to the list of ARI playback channels for bridges.
 *
 * \param bridge Bridge we are adding the playback channel for
 * \param chan Channel being added as a playback channel (must be ;1)
 * \param control The app control structure for the playback channel
 *
 * \retval -1 failed to add channel for any reason
 * \retval 0 on success
 */
int stasis_app_bridge_playback_channel_add(struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct stasis_app_control *control);

/*!
 * \brief remove channel from list of ARI playback channels for bridges.
 *
 * \param bridge_id The unique ID of the bridge the playback channel is in.
 * \param control The app control structure for the playback channel
 */
void stasis_app_bridge_playback_channel_remove(char *bridge_id,
	struct stasis_app_control *control);

/*!
 * \brief Result codes used when adding/removing channels to/from bridges.
 */
enum stasis_app_control_channel_result {
	/*! The channel is okay to be added/removed */
	STASIS_APP_CHANNEL_OKAY = 0,
	/*! The channel is currently recording */
	STASIS_APP_CHANNEL_RECORDING
};

/*!
 * \brief Add a channel to the bridge.
 *
 * \param control Control whose channel should be added to the bridge
 * \param bridge Pointer to the bridge
 *
 * \return non-zero on failure
 * \return zero on success
 */
int stasis_app_control_add_channel_to_bridge(
	struct stasis_app_control *control, struct ast_bridge *bridge);

/*!
 * \brief Remove a channel from the bridge.
 *
 * \param control Control whose channel should be removed from the bridge
 * \param bridge Pointer to the bridge
 *
 * \return non-zero on failure
 * \return zero on success
 */
int stasis_app_control_remove_channel_from_bridge(
	struct stasis_app_control *control, struct ast_bridge *bridge);

/*!
 * \brief Initialize bridge features into a channel control
 *
 * \note Bridge features on a control are destroyed after each bridge session,
 *       so new features need to be initialized before each bridge add.
 *
 * \param control Control in which to store the features
 *
 * \return non-zero on failure
 * \return zero on success
 */
int stasis_app_control_bridge_features_init(
	struct stasis_app_control *control);

/*!
 * \brief Set whether DTMF from the channel is absorbed instead of passing through to the bridge
 *
 * \param control Control whose channel should have its DTMF absorbed when bridged
 * \param absorb Whether DTMF should be absorbed (1) instead of passed through (0).
 */
void stasis_app_control_absorb_dtmf_in_bridge(
	struct stasis_app_control *control, int absorb);

/*!
 * \brief Set whether audio from the channel is muted instead of passing through to the bridge
 *
 * \param control Control whose channel should have its audio muted when bridged
 * \param mute Whether audio should be muted (1) instead of passed through (0).
 */
void stasis_app_control_mute_in_bridge(
	struct stasis_app_control *control, int mute);

/*!
 * \since 18
 * \brief Set whether COLP frames should be generated when joining the bridge
 *
 * \param control Control whose channel should have its COLP frames inhibited when bridged
 * \param inhibit_colp Whether COLP frames should be generated (0) or not (1).
 */
void stasis_app_control_inhibit_colp_in_bridge(
	struct stasis_app_control *control, int inhibit_colp);

/*!
 * \since 12
 * \brief Gets the bridge currently associated with a control object.
 *
 * \note If the bridge returned by this function is to be held for any
 *       length of time, its refcount should be incremented until the
 *       caller is finished with it.
 *
 * \param control Control object for the channel to query.
 *
 * \return Associated \ref ast_bridge.
 * \retval NULL if not associated with a bridge.
 */
struct ast_bridge *stasis_app_get_bridge(struct stasis_app_control *control);

/*!
 * \brief Destroy the bridge.
 *
 * \param bridge_id Uniqueid of bridge to be destroyed
 */
void stasis_app_bridge_destroy(const char *bridge_id);

/*!
 * \brief Get the Stasis message sanitizer for app_stasis applications
 *
 * \retval The stasis message sanitizer
 */
struct stasis_message_sanitizer *stasis_app_get_sanitizer(void);

/*!
 * \brief Indicate that this channel has had a StasisEnd published for it
 *
 * \param chan The channel that is exiting Stasis.
 */
void stasis_app_channel_set_stasis_end_published(struct ast_channel *chan);

/*!
 * \brief Has this channel had a StasisEnd published on it?
 *
 * \param chan The channel upon which the query rests.
 *
 * \retval 0 No
 * \retval 1 Yes
 */
int stasis_app_channel_is_stasis_end_published(struct ast_channel *chan);

/*!
 * \brief Is this channel internal to Stasis?
 *
 * \param chan The channel to check.
 *
 * \retval 0 No
 * \retval 1 Yes
 */
int stasis_app_channel_is_internal(struct ast_channel *chan);

/*!
 * \brief Mark this unreal channel and it's other half as being internal to Stasis.
 *
 * \param chan The channel to mark.
 *
 * \retval zero Success
 * \retval non-zero Failure
 */
int stasis_app_channel_unreal_set_internal(struct ast_channel *chan);

/*!
 * \brief Mark this channel as being internal to Stasis.
 *
 * \param chan The channel to mark.
 *
 * \retval zero Success
 * \retval non-zero Failure
 */
int stasis_app_channel_set_internal(struct ast_channel *chan);

/*!
 * \brief Dial a channel
 * \param control Control for \c res_stasis.
 * \param dialstring The dialstring to pass to the channel driver
 * \param timeout Optional timeout in milliseconds
 */
int stasis_app_control_dial(struct stasis_app_control *control,
		const char *dialstring, unsigned int timeout);

/*!
 * \brief Let Stasis app internals shut down
 *
 * This is called when res_stasis is unloaded. It ensures that
 * the Stasis app internals can free any resources they may have
 * allocated during the time that res_stasis was loaded.
 */
void stasis_app_control_shutdown(void);

/*!
 * \brief Enable/disable request/response and event logging on an application
 *
 * \param app The app to debug
 * \param debug If non-zero, enable debugging. If zero, disable.
 */
void stasis_app_set_debug(struct stasis_app *app, int debug);

/*!
 * \brief Enable/disable request/response and event logging on an application
 *
 * \param app_name The app name to debug
 * \param debug If non-zero, enable debugging. If zero, disable.
 */
void stasis_app_set_debug_by_name(const char *app_name, int debug);

/*!
 * \brief Get debug status of an application
 *
 * \param app The app to check
 * \return The debug flag for the app || the global debug flag
 */
int stasis_app_get_debug(struct stasis_app *app);

/*!
 * \brief Get debug status of an application
 *
 * \param app_name The app_name to check
 * \return The debug flag for the app || the global debug flag
 */
int stasis_app_get_debug_by_name(const char *app_name);

/*!
 * \brief Enable/disable request/response and event logging on all applications
 *
 * \param debug If non-zero, enable debugging. If zero, disable.
 */
void stasis_app_set_global_debug(int debug);

struct ast_cli_args;

/*!
 * \brief Dump properties of a \c stasis_app to the CLI
 *
 * \param app The application
 * \param a The CLI arguments
 */
void stasis_app_to_cli(const struct stasis_app *app, struct ast_cli_args *a);

/*!
 * \brief Convert and add the app's event type filter(s) to the given json object.
 *
 * \param app The application
 * \param json The json object to add the filter data to
 *
 * \return The given json object
 */
struct ast_json *stasis_app_event_filter_to_json(struct stasis_app *app, struct ast_json *json);

/*!
 * \brief Set the application's event type filter
 *
 * \param app The application
 * \param filter The allowed and/or disallowed event filter
 *
 * \return 0 if successfully set
 */
int stasis_app_event_filter_set(struct stasis_app *app, struct ast_json *filter);

/*!
 * \brief Check if the given event should be filtered.
 *
 * Attempts first to find the event in the application's disallowed events list.
 * If found then the event won't be sent to the remote. If not found in the
 * disallowed list then a search is done to see if it can be found in the allowed
 * list. If found the event message is sent, otherwise it is not sent.
 *
 * \param app_name The application name
 * \param event The event to check
 *
 * \return True if allowed, false otherwise
 */
int stasis_app_event_allowed(const char *app_name, struct ast_json *event);

/*! @} */

#endif /* _ASTERISK_STASIS_APP_H */
