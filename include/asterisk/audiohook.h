/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Audiohooks Architecture
 */

#ifndef _ASTERISK_AUDIOHOOK_H
#define _ASTERISK_AUDIOHOOK_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* these two are used in struct ast_audiohook */
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/slinfactory.h"

enum ast_audiohook_type {
	AST_AUDIOHOOK_TYPE_SPY = 0,    /*!< Audiohook wants to receive audio */
	AST_AUDIOHOOK_TYPE_WHISPER,    /*!< Audiohook wants to provide audio to be mixed with existing audio */
	AST_AUDIOHOOK_TYPE_MANIPULATE, /*!< Audiohook wants to manipulate the audio */
};

enum ast_audiohook_status {
	AST_AUDIOHOOK_STATUS_NEW = 0,  /*!< Audiohook was just created, not in use yet */
	AST_AUDIOHOOK_STATUS_RUNNING,  /*!< Audiohook is running on a channel */
	AST_AUDIOHOOK_STATUS_SHUTDOWN, /*!< Audiohook is being shutdown */
	AST_AUDIOHOOK_STATUS_DONE,     /*!< Audiohook has shutdown and is not running on a channel any longer */
};

enum ast_audiohook_direction {
	AST_AUDIOHOOK_DIRECTION_READ = 0, /*!< Reading audio in */
	AST_AUDIOHOOK_DIRECTION_WRITE,    /*!< Writing audio out */
	AST_AUDIOHOOK_DIRECTION_BOTH,     /*!< Both reading audio in and writing audio out */
};

enum ast_audiohook_flags {
	AST_AUDIOHOOK_TRIGGER_MODE = (3 << 0),  /*!< When audiohook should be triggered to do something */
	AST_AUDIOHOOK_TRIGGER_READ = (1 << 0),  /*!< Audiohook wants to be triggered when reading audio in */
	AST_AUDIOHOOK_TRIGGER_WRITE = (2 << 0), /*!< Audiohook wants to be triggered when writing audio out */
	AST_AUDIOHOOK_WANTS_DTMF = (1 << 1),    /*!< Audiohook also wants to receive DTMF frames */
	AST_AUDIOHOOK_TRIGGER_SYNC = (1 << 2),  /*!< Audiohook wants to be triggered when both sides have combined audio available */
	/*! Audiohooks with this flag set will not allow for a large amount of samples to build up on its
	 * slinfactories. We will flush the factories if they contain too many samples.
	 */
	AST_AUDIOHOOK_SMALL_QUEUE = (1 << 3),
	AST_AUDIOHOOK_MUTE_READ = (1 << 4),     /*!< audiohook should be mute frames read */
	AST_AUDIOHOOK_MUTE_WRITE = (1 << 5),    /*!< audiohook should be mute frames written */
};

#define AST_AUDIOHOOK_SYNC_TOLERANCE 100 /*< Tolerance in milliseconds for audiohooks synchronization */

struct ast_audiohook;

/*! \brief Callback function for manipulate audiohook type
 * \param audiohook Audiohook structure
 * \param chan Channel
 * \param frame Frame of audio to manipulate
 * \param direction Direction frame came from
 * \return Returns 0 on success, -1 on failure.
 * \note An audiohook does not have any reference to a private data structure for manipulate
 *       types. It is up to the manipulate callback to store this data via it's own method.
 *       An example would be datastores.
 * \note The input frame should never be freed or corrupted during a manipulate callback.
 *       If the callback has the potential to corrupt the frame's data during manipulation,
 *       local data should be used for the manipulation and only copied to the frame on
 *       success.
 * \note A failure return value indicates that the frame was not manipulated and that
 *       is being returned in its original state.
 */
typedef int (*ast_audiohook_manipulate_callback)(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *frame, enum ast_audiohook_direction direction);

struct ast_audiohook_options {
	int read_volume;  /*!< Volume adjustment on frames read from the channel the hook is on */
	int write_volume; /*!< Volume adjustment on frames written to the channel the hook is on */
};

struct ast_audiohook {
	ast_mutex_t lock;                                      /*!< Lock that protects the audiohook structure */
	ast_cond_t trigger;                                    /*!< Trigger condition (if enabled) */
	enum ast_audiohook_type type;                          /*!< Type of audiohook */
	enum ast_audiohook_status status;                      /*!< Status of the audiohook */
	const char *source;                                    /*!< Who this audiohook ultimately belongs to */
	unsigned int flags;                                    /*!< Flags on the audiohook */
	struct ast_slinfactory read_factory;                   /*!< Factory where frames read from the channel, or read from the whisper source will go through */
	struct ast_slinfactory write_factory;                  /*!< Factory where frames written to the channel will go through */
	struct timeval read_time;                              /*!< Last time read factory was fed */
	struct timeval write_time;                             /*!< Last time write factory was fed */
	struct ast_format format;                              /*!< Format translation path is setup as */
	struct ast_trans_pvt *trans_pvt;                       /*!< Translation path for reading frames */
	ast_audiohook_manipulate_callback manipulate_callback; /*!< Manipulation callback */
	struct ast_audiohook_options options;                  /*!< Applicable options */
	AST_LIST_ENTRY(ast_audiohook) list;                    /*!< Linked list information */
};

struct ast_audiohook_list;

/*! \brief Initialize an audiohook structure
 * \param audiohook Audiohook structure
 * \param type Type of audiohook to initialize this as
 * \param source Who is initializing this audiohook
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_init(struct ast_audiohook *audiohook, enum ast_audiohook_type type, const char *source);

/*! \brief Destroys an audiohook structure
 * \param audiohook Audiohook structure
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_destroy(struct ast_audiohook *audiohook);

/*! \brief Writes a frame into the audiohook structure
 * \param audiohook Audiohook structure
 * \param direction Direction the audio frame came from
 * \param frame Frame to write in
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_write_frame(struct ast_audiohook *audiohook, enum ast_audiohook_direction direction, struct ast_frame *frame);

/*! \brief Reads a frame in from the audiohook structure
 * \param audiohook Audiohook structure
 * \param samples Number of samples wanted
 * \param direction Direction the audio frame came from
 * \param format Format of frame remote side wants back
 * \return Returns frame on success, NULL on failure
 */
struct ast_frame *ast_audiohook_read_frame(struct ast_audiohook *audiohook, size_t samples, enum ast_audiohook_direction direction, struct ast_format *format);

/*! \brief Attach audiohook to channel
 * \param chan Channel
 * \param audiohook Audiohook structure
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_attach(struct ast_channel *chan, struct ast_audiohook *audiohook);

/*! \brief Detach audiohook from channel
 * \param audiohook Audiohook structure
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_detach(struct ast_audiohook *audiohook);

/*! \brief Detach audiohooks from list and destroy said list
 * \param audiohook_list List of audiohooks
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_detach_list(struct ast_audiohook_list *audiohook_list);

/*! \brief Move an audiohook from one channel to a new one
 *
 * \todo Currently only the first audiohook of a specific source found will be moved.
 * We should add the capability to move multiple audiohooks from a single source as well.
 *
 * \note It is required that both old_chan and new_chan are locked prior to calling
 * this function. Besides needing to protect the data within the channels, not locking
 * these channels can lead to a potential deadlock
 *
 * \param old_chan The source of the audiohook to move
 * \param new_chan The destination to which we want the audiohook to move
 * \param source The source of the audiohook we want to move
 */
void ast_audiohook_move_by_source(struct ast_channel *old_chan, struct ast_channel *new_chan, const char *source);

/*!
 * \brief Detach specified source audiohook from channel
 *
 * \param chan Channel to detach from
 * \param source Name of source to detach
 *
 * \return Returns 0 on success, -1 on failure
 *
 * \note The channel does not need to be locked before calling this function.
 */
int ast_audiohook_detach_source(struct ast_channel *chan, const char *source);

/*!
 * \brief Remove an audiohook from a specified channel
 *
 * \param chan Channel to remove from
 * \param audiohook Audiohook to remove
 *
 * \return Returns 0 on success, -1 on failure
 *
 * \note The channel does not need to be locked before calling this function
 */
int ast_audiohook_remove(struct ast_channel *chan, struct ast_audiohook *audiohook);

/*!
 * \brief determines if a audiohook_list is empty or not.
 *
 * retval 0 false, 1 true
 */
int ast_audiohook_write_list_empty(struct ast_audiohook_list *audiohook_list);

/*! \brief Pass a frame off to be handled by the audiohook core
 * \param chan Channel that the list is coming off of
 * \param audiohook_list List of audiohooks
 * \param direction Direction frame is coming in from
 * \param frame The frame itself
 * \return Return frame on success, NULL on failure
 */
struct ast_frame *ast_audiohook_write_list(struct ast_channel *chan, struct ast_audiohook_list *audiohook_list, enum ast_audiohook_direction direction, struct ast_frame *frame);

/*! \brief Update audiohook's status
 * \param audiohook Audiohook structure
 * \param audiohook status enum
 *
 * \note once status is updated to DONE, this function can not be used to set the
 * status back to any other setting.  Setting DONE effectively locks the status as such.
 */
void ast_audiohook_update_status(struct ast_audiohook *audiohook, enum ast_audiohook_status status);

/*! \brief Wait for audiohook trigger to be triggered
 * \param audiohook Audiohook to wait on
 */
void ast_audiohook_trigger_wait(struct ast_audiohook *audiohook);

/*!
  \brief Find out how many audiohooks from  a certain source exist on a given channel, regardless of status.
  \param chan The channel on which to find the spies
  \param source The audiohook's source
  \param type The type of audiohook
  \return Return the number of audiohooks which are from the source specified

  Note: Function performs nlocking.
*/
int ast_channel_audiohook_count_by_source(struct ast_channel *chan, const char *source, enum ast_audiohook_type type);

/*!
  \brief Find out how many spies of a certain type exist on a given channel, and are in state running.
  \param chan The channel on which to find the spies
  \param source The source of the audiohook
  \param type The type of spy to look for
  \return Return the number of running audiohooks which are from the source specified

  Note: Function performs no locking.
*/
int ast_channel_audiohook_count_by_source_running(struct ast_channel *chan, const char *source, enum ast_audiohook_type type);

/*! \brief Lock an audiohook
 * \param ah Audiohook structure
 */
#define ast_audiohook_lock(ah) ast_mutex_lock(&(ah)->lock)

/*! \brief Unlock an audiohook
 * \param ah Audiohook structure
 */
#define ast_audiohook_unlock(ah) ast_mutex_unlock(&(ah)->lock)

/*!
 * \brief Adjust the volume on frames read from or written to a channel
 * \param chan Channel to muck with
 * \param direction Direction to set on
 * \param volume Value to adjust the volume by
 * \return Returns 0 on success, -1 on failure
 * \since 1.6.1
 */
int ast_audiohook_volume_set(struct ast_channel *chan, enum ast_audiohook_direction direction, int volume);

/*!
 * \brief Retrieve the volume adjustment value on frames read from or written to a channel
 * \param chan Channel to retrieve volume adjustment from
 * \param direction Direction to retrieve
 * \return Returns adjustment value
 * \since 1.6.1
 */
int ast_audiohook_volume_get(struct ast_channel *chan, enum ast_audiohook_direction direction);

/*!
 * \brief Adjust the volume on frames read from or written to a channel
 * \param chan Channel to muck with
 * \param direction Direction to increase
 * \param volume Value to adjust the adjustment by
 * \return Returns 0 on success, -1 on failure
 * \since 1.6.1
 */
int ast_audiohook_volume_adjust(struct ast_channel *chan, enum ast_audiohook_direction direction, int volume);

/*! \brief Mute frames read from or written to a channel
 * \param chan Channel to muck with
 * \param source Type of audiohook
 * \param flag which direction to set / clear
 * \param clear set or clear muted frames on direction based on flag parameter
 * \retval 0 success
 * \retval -1 failure
 */
int ast_audiohook_set_mute(struct ast_channel *chan, const char *source, enum ast_audiohook_flags flag, int clear);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_AUDIOHOOK_H */
