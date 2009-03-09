/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007-2008, Dwayne M. Hubbard 
 *
 * Dwayne M. Hubbard <dhubbard@digium.com>
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
#include "asterisk.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"

#ifndef __taskprocessor_h__
#define __taskprocessor_h__

/*!
 * \file taskprocessor.h
 * \brief An API for managing task processing threads that can be shared across modules
 *
 * \author Dwayne M. Hubbard <dhubbard@digium.com>
 *
 * \note A taskprocessor is a named singleton containing a processing thread and
 * a task queue that serializes tasks pushed into it by [a] module(s) that reference the taskprocessor.  
 * A taskprocessor is created the first time its name is requested via the ast_taskprocessor_get()
 * function and destroyed when the taskprocessor reference count reaches zero.
 *
 * Modules that obtain a reference to a taskprocessor can queue tasks into the taskprocessor
 * to be processed by the singleton processing thread when the task is popped off the front 
 * of the queue.  A task is a wrapper around a task-handling function pointer and a data
 * pointer.  It is the responsibility of the task handling function to free memory allocated for
 * the task data pointer.  A task is pushed into a taskprocessor queue using the 
 * ast_taskprocessor_push(taskprocessor, taskhandler, taskdata) function and freed by the
 * taskprocessor after the task handling function returns.  A module releases its reference to a
 * taskprocessor using the ast_taskprocessor_unreference() function which may result in the
 * destruction of the taskprocessor if the taskprocessor's reference count reaches zero.  Tasks waiting
 * to be processed in the taskprocessor queue when the taskprocessor reference count reaches zero
 * will be purged and released from the taskprocessor queue without being processed.
 */
struct ast_taskprocessor;

/*! \brief ast_tps_options for specification of taskprocessor options
 *
 * Specify whether a taskprocessor should be created via ast_taskprocessor_get() if the taskprocessor 
 * does not already exist.  The default behavior is to create a taskprocessor if it does not already exist 
 * and provide its reference to the calling function.  To only return a reference to a taskprocessor if 
 * and only if it exists, use the TPS_REF_IF_EXISTS option in ast_taskprocessor_get(). */
enum ast_tps_options {
	/*! \brief return a reference to a taskprocessor, create one if it does not exist */
	TPS_REF_DEFAULT = 0,
	/*! \brief return a reference to a taskprocessor ONLY if it already exists */
	TPS_REF_IF_EXISTS = (1 << 0),
};

/*! \brief Get a reference to a taskprocessor with the specified name and create the taskprocessor if necessary
 *
 * The default behavior of instantiating a taskprocessor if one does not already exist can be
 * disabled by specifying the TPS_REF_IF_EXISTS ast_tps_options as the second argument to ast_taskprocessor_get().
 * \param name The name of the taskprocessor
 * \param create Use 0 by default or specify TPS_REF_IF_EXISTS to return NULL if the taskprocessor does 
 * not already exist
 * return A pointer to a reference counted taskprocessor under normal conditions, or NULL if the
 * TPS_REF_IF_EXISTS reference type is specified and the taskprocessor does not exist
 * \since 1.6.1
 */
struct ast_taskprocessor *ast_taskprocessor_get(char *name, enum ast_tps_options create);

/*! \brief Unreference the specified taskprocessor and its reference count will decrement.
 *
 * Taskprocessors use astobj2 and will unlink from the taskprocessor singleton container and destroy
 * themself when the taskprocessor reference count reaches zero.
 * \param tps taskprocessor to unreference
 * \return NULL
 * \since 1.6.1
 */
void *ast_taskprocessor_unreference(struct ast_taskprocessor *tps);

/*! \brief Push a task into the specified taskprocessor queue and signal the taskprocessor thread
 * \param tps The taskprocessor structure
 * \param task_exe The task handling function to push into the taskprocessor queue
 * \param datap The data to be used by the task handling function
 * \return zero on success, -1 on failure
 * \since 1.6.1
 */
int ast_taskprocessor_push(struct ast_taskprocessor *tps, int (*task_exe)(void *datap), void *datap);

/*! \brief Return the name of the taskprocessor singleton
 * \since 1.6.1
 */
const char *ast_taskprocessor_name(struct ast_taskprocessor *tps);
#endif

