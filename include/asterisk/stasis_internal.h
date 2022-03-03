/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 *
 * \brief Internal Stasis APIs.
 *
 * This header file is used to define functions that are shared between files that make
 * up \ref stasis. Functions declared here should not be used by any module outside of
 * Stasis.
 *
 * If you find yourself needing to call one of these functions directly, something has
 * probably gone horribly wrong.
 *
 * \author Matt Jordan <mjordan@digium.com>
 */

#include "asterisk/stasis.h"

#ifndef STASIS_INTERNAL_H_
#define STASIS_INTERNAL_H_

/*!
 * \brief Create a subscription.
 *
 * In addition to being AO2 managed memory (requiring an ao2_cleanup() to free
 * up this reference), the subscription must be explicitly unsubscribed from its
 * topic using stasis_unsubscribe().
 *
 * The invocations of the callback are serialized, but may not always occur on
 * the same thread. The invocation order of different subscriptions is
 * unspecified.
 *
 * Note: modules outside of Stasis should use \ref stasis_subscribe.
 *
 * \param topic Topic to subscribe to.
 * \param callback Callback function for subscription messages.
 * \param data Data to be passed to the callback, in addition to the message.
 * \param needs_mailbox Determines whether or not the subscription requires a mailbox.
 *  Subscriptions with mailboxes will be delivered on some non-publisher thread;
 *  subscriptions without mailboxes will be delivered on the publisher thread.
 * \param use_thread_pool Use the thread pool for the subscription. This is only
 *  relevant if \c needs_mailbox is non-zero.
 * \param file, lineno, func
 * \return New \ref stasis_subscription object.
 * \retval NULL on error.
 * \since 12
 */
struct stasis_subscription *internal_stasis_subscribe(
	struct stasis_topic *topic,
	stasis_subscription_cb callback,
	void *data,
	int needs_mailbox,
	int use_thread_pool,
	const char *file,
	int lineno,
	const char *func);

#endif /* STASIS_INTERNAL_H_ */
