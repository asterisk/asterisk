/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Asterisk PBX channel spy definitions
 */

#ifndef _ASTERISK_CHANSPY_H
#define _ASTERISK_CHANSPY_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/linkedlists.h"

enum chanspy_states {
	CHANSPY_NEW = 0,		/*!< spy not yet operating */
	CHANSPY_RUNNING = 1,		/*!< normal operation, spy is still operating */
	CHANSPY_DONE = 2,		/*!< spy is stopped and already removed from channel */
	CHANSPY_STOP = 3,		/*!< spy requested to stop, still attached to channel */
};

enum chanspy_flags {
	CHANSPY_MIXAUDIO = (1 << 0),
	CHANSPY_READ_VOLADJUST = (1 << 1),
	CHANSPY_WRITE_VOLADJUST = (1 << 2),
	CHANSPY_FORMAT_AUDIO = (1 << 3),
	CHANSPY_TRIGGER_MODE = (3 << 4),
	CHANSPY_TRIGGER_READ = (1 << 4),
	CHANSPY_TRIGGER_WRITE = (2 << 4),
	CHANSPY_TRIGGER_NONE = (3 << 4),
	CHANSPY_TRIGGER_FLUSH = (1 << 6),
};

struct ast_channel_spy_queue {
	struct ast_frame *head;
	unsigned int samples;
	unsigned int format;
};

struct ast_channel_spy {
	AST_LIST_ENTRY(ast_channel_spy) list;
	ast_mutex_t lock;
	ast_cond_t trigger;
	struct ast_channel *chan;
	struct ast_channel_spy_queue read_queue;
	struct ast_channel_spy_queue write_queue;
	unsigned int flags;
	enum chanspy_states status;
	const char *type;
	/* The volume adjustment values are very straightforward:
	   positive values cause the samples to be multiplied by that amount
	   negative values cause the samples to be divided by the absolute value of that amount
	*/
	int read_vol_adjustment;
	int write_vol_adjustment;
};

/*!
  \brief Adds a spy to a channel, to begin receiving copies of the channel's audio frames.
  \param chan The channel to add the spy to.
  \param spy A pointer to ast_channel_spy structure describing how the spy is to be used.
  \return 0 for success, non-zero for failure

  Note: This function performs no locking; you must hold the channel's lock before
  calling this function.
 */
int ast_channel_spy_add(struct ast_channel *chan, struct ast_channel_spy *spy);

/*!
  \brief Remove a spy from a channel.
  \param chan The channel to remove the spy from
  \param spy The spy to be removed
  \return nothing

  Note: This function performs no locking; you must hold the channel's lock before
  calling this function.
 */
void ast_channel_spy_remove(struct ast_channel *chan, struct ast_channel_spy *spy);

/*!
  \brief Free a spy.
  \param spy The spy to free
  \return nothing

  Note: This function MUST NOT be called with the spy locked.
*/
void ast_channel_spy_free(struct ast_channel_spy *spy);

/*!
  \brief Find all spies of a particular type on a channel and stop them.
  \param chan The channel to operate on
  \param type A character string identifying the type of spies to be stopped
  \return nothing

  Note: This function performs no locking; you must hold the channel's lock before
  calling this function.
 */
void ast_channel_spy_stop_by_type(struct ast_channel *chan, const char *type);

/*!
  \brief Read one (or more) frames of audio from a channel being spied upon.
  \param spy The spy to operate on
  \param samples The number of audio samples to read
  \return NULL for failure, one ast_frame pointer, or a chain of ast_frame pointers

  This function can return multiple frames if the spy structure needs to be 'flushed'
  due to mismatched queue lengths, or if the spy structure is configured to return
  unmixed audio (in which case each call to this function will return a frame of audio
  from each side of channel).

  Note: This function performs no locking; you must hold the spy's lock before calling
  this function. You must <b>not</b> hold the channel's lock at the same time.
 */
struct ast_frame *ast_channel_spy_read_frame(struct ast_channel_spy *spy, unsigned int samples);

/*!
  \brief Efficiently wait until audio is available for a spy, or an exception occurs.
  \param spy The spy to wait on
  \return nothing

  Note: The locking rules for this function are non-obvious... first, you must <b>not</b>
  hold the channel's lock when calling this function. Second, you must hold the spy's lock
  before making the function call; while the function runs the lock will be released, and
  when the trigger event occurs, the lock will be re-obtained. This means that when control
  returns to your code, you will again hold the spy's lock.
 */
void ast_channel_spy_trigger_wait(struct ast_channel_spy *spy);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_CHANSPY_H */
