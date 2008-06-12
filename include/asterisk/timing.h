/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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

enum ast_timing_event {
	AST_TIMING_EVENT_EXPIRED = 1,
	AST_TIMING_EVENT_CONTINUOUS = 2,
};

struct ast_timing_functions {
	int (*timer_open)(unsigned int rate);
	void (*timer_close)(int handle);
	void (*timer_ack)(int handle, unsigned int quantity);
	int (*timer_enable_continuous)(int handle);
	int (*timer_disable_continuous)(int handle);
	enum ast_timing_event (*timer_get_event)(int handle);
};

/*!
  \brief Install a set of timing functions.
  \param funcs An instance of the \c ast_timing_functions structure with pointers
  to the functions provided by the timing implementation.
  \retval NULL on failure, or a handle to be passed to
  ast_uninstall_timing_functions() on success
 */
void *ast_install_timing_functions(struct ast_timing_functions *funcs);

/*!
  \brief Uninstall a previously-installed set of timing functions.
  \param handle The handle returned from a prior successful call to
  ast_install_timing_functions().
  \retval none
 */
void ast_uninstall_timing_functions(void *handle);

/*!
  \brief Open a timer handle.
  \param rate The rate at which the timer should trigger.
  \retval -1 on failure, or a positive integer on success
 */
int ast_timer_open(unsigned int rate);

/*!
  \brief Close a previously-opened timer handle.
  \param handle The timer handle to close.
  \retval none
 */
void ast_timer_close(int handle);

void ast_timer_ack(int handle, unsigned int quantity);

int ast_timer_enable_continuous(int handle);

int ast_timer_disable_continous(int handle);

enum ast_timing_event ast_timer_get_event(int handle);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_TIMING_H */
