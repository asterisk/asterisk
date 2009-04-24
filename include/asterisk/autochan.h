/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \file
 * \brief "smart" channels that update automatically if a channel is masqueraded
 *
 * \author Mark Michelson <mmichelson@digium.com>
 */

#include "asterisk.h"
#include "asterisk/linkedlists.h"

#ifndef _ASTERISK_AUTOCHAN_H
#define _ASTERISK_AUTOCHAN_H

struct ast_autochan {
	struct ast_channel *chan;
	AST_LIST_ENTRY(ast_autochan) list;
};

/*! 
 * \par Just what the $!@# is an autochan?
 *
 * An ast_autochan is a structure which contains an ast_channel. The pointer
 * inside an autochan has the ability to update itself if the channel it points
 * to is masqueraded into a different channel.
 *
 * This has a great benefit for any application or service which creates a thread
 * outside of the channel's main operating thread which keeps a pointer to said
 * channel. when a masquerade occurs on the channel, the autochan's chan pointer
 * will automatically update to point to the new channel.
 *
 * Some rules for autochans
 *
 * 1. If you are going to use an autochan, then be sure to always refer to the
 * channel using the chan pointer inside the autochan if possible, since this is
 * the pointer that will be updated during a masquerade.
 *
 * 2. If you are going to save off a pointer to the autochan's chan, then be sure
 * to save off the pointer using ast_channel_ref and to unref the channel when you
 * are finished with the pointer. If you do not do this and a masquerade occurs on
 * the channel, then it is possible that your saved pointer will become invalid.
 */

/*!
 * \brief set up a new ast_autochan structure
 *
 * \details
 * Allocates and initializes an ast_autochan, sets the
 * autochan's chan pointer to point to the chan parameter, and
 * adds the autochan to the global list of autochans. The newly-
 * created autochan is returned to the caller. This function will
 * cause the refcount of chan to increase by 1.
 *
 * \param chan The channel our new autochan will point to
 *
 * \note autochans must be freed using ast_autochan_destroy
 *
 * \retval NULL Failure
 * \retval non-NULL success
 */
struct ast_autochan *ast_autochan_setup(struct ast_channel *chan);

/*!
 * \brief destroy an ast_autochan structure
 *
 * \details
 * Removes the passed-in autochan from the list of autochans and
 * unrefs the channel that is pointed to. Also frees the autochan
 * struct itself. This function will unref the channel reference
 * which was made in ast_autochan_setup
 *
 * \param autochan The autochan that you wish to destroy
 *
 * \retval void
 */
void ast_autochan_destroy(struct ast_autochan *autochan);

/*!
 * \brief Switch what channel autochans point to
 *
 * \details
 * Traverses the list of autochans. All autochans which point to
 * old_chan will be updated to point to new_chan instead. Currently
 * this is only called from ast_do_masquerade in channel.c.
 * 
 * \pre Both channels must be locked before calling this function.
 *
 * \param old_chan The channel that autochans may currently point to
 * \param new_chan The channel that we want to point those autochans to now
 *
 * \retval void
 */
void ast_autochan_new_channel(struct ast_channel *old_chan, struct ast_channel *new_chan);

#endif /* _ASTERISK_AUTOCHAN_H */
