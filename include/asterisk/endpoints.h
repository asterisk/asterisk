/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

#ifndef _ASTERISK_ENDPOINTS_H
#define _ASTERISK_ENDPOINTS_H

/*! \file
 *
 * \brief Endpoint abstractions.
 *
 * \author David M. Lee, II <dlee@digium.com>
 * \since 12
 *
 * An endpoint is an external device/system that may offer/accept channels
 * to/from Asterisk. While this is a very useful concept for end users, it is
 * surprisingly \a not a core concept within Asterisk itself.
 *
 * This file defines \ref ast_endpoint as a seperate object, which channel
 * drivers may use to expose their concept of an endpoint. As the channel driver
 * creates channels, it can use ast_endpoint_add_channel() to associate channels
 * to the endpoint. This updates the endpoint appropriately, and forwards all of
 * the channel's events to the endpoint's topic.
 *
 * In order to avoid excessive locking on the endpoint object itself, the
 * mutable state is not accessible via getters. Instead, you can create a
 * snapshot using ast_endpoint_snapshot_create() to get a consistent snapshot of
 * the internal state.
 */

#include "asterisk/json.h"

/*!
 * \brief Valid states for an endpoint.
 * \since 12
 */
enum ast_endpoint_state {
	/*! The endpoint state is not known. */
	AST_ENDPOINT_UNKNOWN,
	/*! The endpoint is not available. */
	AST_ENDPOINT_OFFLINE,
	/*! The endpoint is available. */
	AST_ENDPOINT_ONLINE,
};

/*!
 * \brief Returns a string representation of the given endpoint state.
 *
 * \param state Endpoint state.
 * \return String representation of \a state.
 * \return \c "?" if \a state isn't in \ref ast_endpoint_state.
 */
const char *ast_endpoint_state_to_string(enum ast_endpoint_state state);

/*!
 * \brief Opaque struct representing an endpoint.
 *
 * An endpoint is an external device/system that may offer/accept channels
 * to/from Asterisk.
 *
 * \since 12
 */
struct ast_endpoint;

/*!
 * \brief Finds the endpoint with the given tech[/resource] id.
 *
 * Endpoints are refcounted, so ao2_cleanup() when you're done.
 *
 * \note The resource portion of an ID is optional. If not provided,
 *       an aggregate endpoint for the entire technology is returned.
 *       These endpoints must not be modified, but can be subscribed
 *       to in order to receive updates for all endpoints of a given
 *       technology.
 *
 * \param id Tech[/resource] id to look for.
 * \return Associated endpoint.
 * \return \c NULL if not found.
 *
 * \since 12
 */
struct ast_endpoint *ast_endpoint_find_by_id(const char *id);

/*!
 * \brief Create an endpoint struct.
 *
 * The endpoint is created with a state of UNKNOWN and max_channels of -1
 * (unlimited). While \ref ast_endpoint is AO2 managed, you have to
 * shut it down with ast_endpoint_shutdown() to clean up references from
 * subscriptions.
 *
 * \param tech Technology for this endpoint.
 * \param resource Name of this endpoint.
 * \return Newly created endpoint.
 * \return \c NULL on error.
 * \since 12
 */
struct ast_endpoint *ast_endpoint_create(const char *tech, const char *resource);

/*!
 * \brief Shutsdown an \ref ast_endpoint.
 *
 * \param endpoint Endpoint to shut down.
 * \since 12
 */
void ast_endpoint_shutdown(struct ast_endpoint *endpoint);

/*!
 * \brief Gets the technology of the given endpoint.
 *
 * This is an immutable string describing the channel provider technology
 * (SIP, IAX2, etc.).
 *
 * \param endpoint The endpoint.
 * \return Tec of the endpoint.
 * \return \c NULL if endpoint is \c NULL.
 * \since 12
 */
const char *ast_endpoint_get_tech(const struct ast_endpoint *endpoint);

/*!
 * \brief Gets the resource name of the given endpoint.
 *
 * This is unique for the endpoint's technology, and immutable.
 *
 * \note If the endpoint being queried is a technology aggregate
 *       endpoint, this will be an empty string.
 *
 * \param endpoint The endpoint.
 * \return Resource name of the endpoint.
 * \return \c NULL if endpoint is \c NULL.
 * \since 12
 */
const char *ast_endpoint_get_resource(const struct ast_endpoint *endpoint);

/*!
 * \brief Gets the tech/resource id of the given endpoint.
 *
 * This is unique across all endpoints, and immutable.
 *
 * \param endpoint The endpoint.
 * \return Tech/resource id of the endpoint.
 * \return \c NULL if endpoint is \c NULL.
 * \since 12
 */
const char *ast_endpoint_get_id(const struct ast_endpoint *endpoint);

/*!
 * \brief Gets the state of the given endpoint.
 *
 * \param endpoint The endpoint.
 * \return state.
 * \return \c AST_ENDPOINT_UNKNOWN if endpoint is \c NULL.
 * \since 13.4
 */
enum ast_endpoint_state ast_endpoint_get_state(const struct ast_endpoint *endpoint);

/*!
 * \brief Updates the state of the given endpoint.
 *
 * \param endpoint Endpoint to modify.
 * \param state New state.
 * \since 12
 */
void ast_endpoint_set_state(struct ast_endpoint *endpoint,
	enum ast_endpoint_state state);

/*!
 * \brief Updates the maximum number of channels an endpoint supports.
 *
 * Set to -1 for unlimited channels.
 *
 * \param endpoint Endpoint to modify.
 * \param max_channels Maximum number of concurrent channels this endpoint
 *        supports.
 */
void ast_endpoint_set_max_channels(struct ast_endpoint *endpoint,
	int max_channels);


/*!
 * \since 12
 * \brief Adds a channel to the given endpoint.
 *
 * This updates the endpoint's statistics, as well as forwarding all of the
 * channel's messages to the endpoint's topic.
 *
 * The channel is automagically removed from the endpoint when it is disposed
 * of.
 *
 * \param endpoint
 * \param chan Channel.
 * \retval 0 on success.
 * \retval Non-zero on error.
 */
int ast_endpoint_add_channel(struct ast_endpoint *endpoint,
	struct ast_channel *chan);


#endif /* _ASTERISK_ENDPOINTS_H */
