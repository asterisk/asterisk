/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008 - 2009, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
 * Russell Bryant <russell@digium.com>
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

/*!
	\file timing.h
	\brief Timing source management
	\author Kevin P. Fleming <kpfleming@digium.com>
	\author Russell Bryant <russell@digium.com>

	Portions of Asterisk require a timing source, a periodic trigger
	for media handling activities. The functions in this file allow
	a loadable module to provide a timing source for Asterisk and its
	modules, so that those modules can request a 'timing handle' when
	they require one. These handles are file descriptors, which can be
	used with select() or poll().

	The timing source used by Asterisk must provide the following
	features:

	1) Periodic triggers, with a configurable interval (specified as
		 number of triggers per second).

	2) Multiple outstanding triggers, each of which must be 'acked'
		 to clear it. Triggers must also be 'ackable' in quantity.

	3) Continuous trigger mode, which when enabled causes every call
		 to poll() on the timer handle to immediately return.

	4) Multiple 'event types', so that the code using the timer can
		 know whether the wakeup it received was due to a periodic trigger
		 or a continuous trigger.
 */

#ifndef _ASTERISK_TIMING_H
#define _ASTERISK_TIMING_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

enum ast_timer_event {
	AST_TIMING_EVENT_EXPIRED = 1,
	AST_TIMING_EVENT_CONTINUOUS = 2,
};

/*!
 * \brief Timing module interface
 *
 * The public API calls for the timing API directly map to this interface.
 * So, the behavior of these calls should match the documentation of the
 * public API calls.
 */
struct ast_timing_interface {
	const char *name;
	/*! This handles the case where multiple timing modules are loaded.
	 *  The highest priority timing interface available will be used. */
	unsigned int priority;
	void *(*timer_open)(void);
	void (*timer_close)(void *data);
	int (*timer_set_rate)(void *data, unsigned int rate);
	int (*timer_ack)(void *data, unsigned int quantity);
	int (*timer_enable_continuous)(void *data);
	int (*timer_disable_continuous)(void *data);
	enum ast_timer_event (*timer_get_event)(void *data);
	unsigned int (*timer_get_max_rate)(void *data);
	int (*timer_fd)(void *data);
};

/*!
 * \brief Register a set of timing functions.
 *
 * \param i An instance of the \c ast_timing_interfaces structure with pointers
 *        to the functions provided by the timing implementation.
 *
 * \retval NULL failure 
 * \retval non-Null handle to be passed to ast_unregister_timing_interface() on success
 * \since 1.6.1
 */
#define ast_timing_interface_register(i) __ast_timing_interface_register(i, AST_MODULE_SELF)
int __ast_timing_interface_register(struct ast_timing_interface *funcs, struct ast_module *mod);

struct ast_timer;

/*!
 * \brief Open a timer
 *
 * \retval NULL on error, with errno set
 * \retval non-NULL timer handle on success
 * \since 1.6.1
 */
struct ast_timer *ast_timer_open(void);

/*!
 * \brief Close an opened timing handle
 *
 * \param handle timer handle returned from timer_open()
 *
 * \return nothing
 * \since 1.6.1
 */
void ast_timer_close(struct ast_timer *handle);

/*!
 * \brief Get a poll()-able file descriptor for a timer
 *
 * \param handle timer handle returned from timer_open()
 *
 * \return file descriptor which can be used with poll() to wait for events
 * \since 1.6.1
 */
int ast_timer_fd(const struct ast_timer *handle);

/*!
 * \brief Set the timing tick rate
 *
 * \param handle timer handle returned from timer_open()
 * \param rate ticks per second, 0 turns the ticks off if needed
 *
 * Use this function if you want the timer to show input at a certain
 * rate.  The other alternative use of a timer is the continuous
 * mode.
 *
 * \retval -1 error, with errno set
 * \retval 0 success
 * \since 1.6.1
 */
int ast_timer_set_rate(const struct ast_timer *handle, unsigned int rate);

/*!
 * \brief Acknowledge a timer event
 *
 * \param handle timer handle returned from timer_open()
 * \param quantity number of timer events to acknowledge
 *
 * \note This function should only be called if timer_get_event()
 *       returned AST_TIMING_EVENT_EXPIRED.
 *
 * \retval -1 failure, with errno set
 * \retval 0 success
 * \since 10.5.2
 */
int ast_timer_ack(const struct ast_timer *handle, unsigned int quantity);

/*!
 * \brief Enable continuous mode
 *
 * \param handle timer handle returned from timer_open()
 *
 * Continuous mode causes poll() on the timer's fd to immediately return
 * always until continuous mode is disabled.
 *
 * \retval -1 failure, with errno set
 * \retval 0 success
 * \since 1.6.1
 */
int ast_timer_enable_continuous(const struct ast_timer *handle);

/*!
 * \brief Disable continuous mode
 *
 * \param handle timer handle returned from timer_close()
 *
 * \retval -1 failure, with errno set
 * \retval 0 success
 * \since 1.6.1
 */
int ast_timer_disable_continuous(const struct ast_timer *handle);

/*!
 * \brief Retrieve timing event
 *
 * \param handle timer handle returned by timer_open()
 *
 * After poll() indicates that there is input on the timer's fd, this will
 * be called to find out what triggered it.
 *
 * \return which event triggered the timer
 * \since 1.6.1
 */
enum ast_timer_event ast_timer_get_event(const struct ast_timer *handle);

/*!
 * \brief Get maximum rate supported for a timer
 *
 * \param handle timer handle returned by timer_open()
 *
 * \return maximum rate supported by timer
 * \since 1.6.1
 */
unsigned int ast_timer_get_max_rate(const struct ast_timer *handle);

/*!
 * \brief Get name of timer in use
 *
 * \param handle timer handle returned by timer_open()
 *
 * \return name of timer
 * \since 1.6.2
 */
const char *ast_timer_get_name(const struct ast_timer *handle);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_TIMING_H */
