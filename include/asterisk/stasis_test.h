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

#ifndef _ASTERISK_STASIS_TEST_H
#define _ASTERISK_STASIS_TEST_H

/*!
 * \file
 * \brief Test infrastructure for dealing with Stasis.
 *
 * \author David M. Lee, II <dlee@digium.com>
 *
 * This file contains some helpful utilities for testing Stasis related topics
 * and messages. The \ref stasis_message_sink is something you can subscribe to
 * a topic which will receive all of the messages from the topic. This messages
 * are accumulated in its \c messages field.
 *
 * There are a set of wait functions (stasis_message_sink_wait_for_count(),
 * stasis_message_sink_wait_for(), etc.) which will block waiting for conditions
 * to be met in the \ref stasis_message_sink.
 */

#include "asterisk/lock.h"
#include "asterisk/stasis.h"

#define STASIS_SINK_DEFAULT_WAIT 5000

/*! \brief Structure that collects messages from a topic */
struct stasis_message_sink {
	/*! Condition mutex. */
	ast_mutex_t lock;
	/*! Condition to signal state changes */
	ast_cond_t cond;
	/*! Maximum number of messages messages field can hold without
	 * realloc */
	size_t max_messages;
	/*! Current number of messages in messages field. */
	size_t num_messages;
	/*! Boolean flag to be set when unsubscribe is received */
	int is_done:1;
	/*! Ordered array of messages received */
	struct stasis_message **messages;
};

/*!
 * \brief Create a message sink.
 *
 * This is an AO2 managed object, which you ao2_cleanup() when done. The
 * destructor waits for an unsubscribe message to be received, to ensure the
 * object isn't disposed of before the topic is finished.
 */
struct stasis_message_sink *stasis_message_sink_create(void);

/*!
 * \brief Topic callback to receive messages.
 *
 * We return a function pointer instead of simply exposing the function because
 * of the vagaries of dlopen(), \c RTLD_LAZY, and function pointers. See the
 * comment on the implementation for details why.
 *
 * \return Function pointer to \ref stasis_message_sink's message handling
 *         function
 */
stasis_subscription_cb stasis_message_sink_cb(void);

/*!
 * \brief Wait for a sink's num_messages field to reach a certain level.
 *
 * The optional timeout prevents complete deadlock in a test.
 *
 * \param sink Sink to wait on.
 * \param num_messages sink->num_messages value to wait for.
 * \param timeout_millis Number of milliseconds to wait. -1 to wait forever.
 * \return Actual sink->num_messages value at return.
 *         If this is < \a num_messages, then the timeout expired.
 */
int stasis_message_sink_wait_for_count(struct stasis_message_sink *sink,
	int num_messages, int timeout_millis);

typedef int (*stasis_wait_cb)(struct stasis_message *msg, const void *data);

/*!
 * \brief Wait for a message that matches the given criteria.
 *
 * \param sink Sink to wait on.
 * \param start Index of message to start with.
 * \param cmp_cb comparison function. This returns true (non-zero) on match
 *               and false (zero) on match.
 * \param data
 * \param timeout_millis Number of milliseconds to wait.
 * \return Index of the matching message.
 * \return Negative for no match.
 */
int stasis_message_sink_wait_for(struct stasis_message_sink *sink, int start,
	stasis_wait_cb cmp_cb, const void *data, int timeout_millis);

/*!
 * \brief Ensures that no new messages are received.
 *
 * The optional timeout prevents complete deadlock in a test.
 *
 * \param sink Sink to wait on.
 * \param num_messages expecte \a sink->num_messages.
 * \param timeout_millis Number of milliseconds to wait for.
 * \return Actual sink->num_messages value at return.
 *         If this is < \a num_messages, then the timeout expired.
 */
int stasis_message_sink_should_stay(struct stasis_message_sink *sink,
	int num_messages, int timeout_millis);

/*! \addtogroup StasisTopicsAndMessages
 * @{
 */

/*!
 * \brief Creates a test message.
 */
struct stasis_message *stasis_test_message_create(void);

/*!
 * \brief Gets the type of messages created by stasis_test_message_create().
 */
struct stasis_message_type *stasis_test_message_type(void);

/*!
 * @}
 */

#endif /* _ASTERISK_STASIS_TEST_H */
