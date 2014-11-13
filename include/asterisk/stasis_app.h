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
 * uses the API defined in this file must call stasis_app_ref() when loaded,
 * and stasis_app_unref() when unloaded.
 */

#include "asterisk/channel.h"
#include "asterisk/json.h"

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
 * \return \c NULL on error.
 */
struct ao2_container *stasis_app_get_all(void);

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
 *
 * \param app_name Name of the application.
 *
 * \return JSON representation of app with given name.
 * \return \c NULL on error.
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
 * \brief Checks to see if the given object is a core event source
 *
 * \note core event sources are currently only endpoint, bridge, and channel.
 *
 * \param obj event source object to check
 *
 * \return non-zero if core event source, otherwise 0 (false)

 */
int stasis_app_is_core_event_source(struct stasis_app_event_source *obj);

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
 * \param userevent_data Custom parameters for the user event
 * \param userevents_count Array size of userevent_data
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
 * \brief Returns the uniqueid of the channel associated with this control
 *
 * \param control Control object.
 *
 * \return Uniqueid of the associate channel.
 * \return \c NULL if \a control is \c NULL.
 */
const char *stasis_app_control_get_channel_id(
	const struct stasis_app_control *control);

/*!
 * \brief Dial an endpoint and bridge it to a channel in \c res_stasis
 *
 * If the channel is no longer in \c res_stasis, this function does nothing.
 *
 * \param control Control for \c res_stasis
 * \param endpoint The endpoint to dial.
 * \param exten Extension to dial if no endpoint specified.
 * \param context Context to use with extension.
 * \param timeout The amount of time to wait for answer, before giving up.
 *
 * \return 0 for success
 * \return -1 for error.
 */
int stasis_app_control_dial(struct stasis_app_control *control, const char *endpoint, const char *exten,
                            const char *context, int timeout);

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
 * \brief Get the value of a variable on the channel associated with this control.
 * \param control Control for \c res_stasis.
 * \param variable The name of the variable.
 *
 * \return The value of the variable.  The returned variable must be freed.
 */
char *stasis_app_control_get_channel_var(struct stasis_app_control *control, const char *variable);

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
 * \return \c NULL if channel isn't in cache.
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
 * \return \c NULL on error.
 */
struct ast_bridge *stasis_app_bridge_create(const char *type, const char *name, const char *id);

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
 *
 * \retval -1 failed to add channel for any reason
 * \retval 0 on success
 */
int stasis_app_bridge_playback_channel_add(struct ast_bridge *bridge,
	struct ast_channel *chan,
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
 * \return \c NULL if not associated with a bridge.
 */
struct ast_bridge *stasis_app_get_bridge(struct stasis_app_control *control);

/*!
 * \brief Destroy the bridge.
 *
 * \param bridge_id Uniqueid of bridge to be destroyed
 *
 * \retval non-zero on failure
 * \retval zero on success
 */
void stasis_app_bridge_destroy(const char *bridge_id);

/*!
 * \brief Increment the res_stasis reference count.
 *
 * This ensures graceful shutdown happens in the proper order.
 */
void stasis_app_ref(void);

/*!
 * \brief Decrement the res_stasis reference count.
 *
 * This ensures graceful shutdown happens in the proper order.
 */
void stasis_app_unref(void);

/*!
 * \brief Get the Stasis message sanitizer for app_stasis applications
 *
 * \retval The stasis message sanitizer
 */
struct stasis_message_sanitizer *stasis_app_get_sanitizer(void);

/*!
 * \brief Indicate that this channel has had a StasisEnd published for it
 *
 * \param The channel that is exiting Stasis.
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

/*! @} */

#endif /* _ASTERISK_STASIS_APP_H */
