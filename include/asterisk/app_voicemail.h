/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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
 * \brief Voice Mail API
 * \author David Vossel <dvossel@digium.com>
 */

#ifndef _ASTERISK_VM_H
#define _ASTERISK_VM_H

#include "asterisk/stringfields.h"
#include "asterisk/linkedlists.h"

#define AST_VM_FOLDER_NUMBER 12

enum ast_vm_snapshot_sort_val {
	AST_VM_SNAPSHOT_SORT_BY_ID = 0,
	AST_VM_SNAPSHOT_SORT_BY_TIME,
};

struct ast_vm_msg_snapshot {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(msg_id);
		AST_STRING_FIELD(callerid);
		AST_STRING_FIELD(callerchan);
		AST_STRING_FIELD(exten);
		AST_STRING_FIELD(origdate);
		AST_STRING_FIELD(origtime);
		AST_STRING_FIELD(duration);
		AST_STRING_FIELD(folder_name);
		AST_STRING_FIELD(flag);
	);
	unsigned int msg_number;

	AST_LIST_ENTRY(ast_vm_msg_snapshot) msg;
};

struct ast_vm_mailbox_snapshot {
	int total_msg_num;
	AST_LIST_HEAD_NOLOCK(, ast_vm_msg_snapshot) snapshots[AST_VM_FOLDER_NUMBER];
};

/*
 * \brief Create a snapshot of a mailbox which contains information about every msg.
 *
 * \param mailbox, the mailbox to look for
 * \param context, the context to look for the mailbox in
 * \param folder, OPTIONAL.  When not NULL only msgs from the specified folder will be included.
 * \param desending, list the msgs in descending order rather than ascending order.
 * \param combine_INBOX_and_OLD, When this argument is set, The OLD folder will be represented
 *        in the INBOX folder of the snapshot. This allows the snapshot to represent the
 *        OLD and INBOX messages in sorted order merged together.
 *
 * \retval snapshot on success
 * \retval NULL on failure
 */
struct ast_vm_mailbox_snapshot *ast_vm_mailbox_snapshot_create(const char *mailbox,
	const char *context,
	const char *folder,
	int descending,
	enum ast_vm_snapshot_sort_val sort_val,
	int combine_INBOX_and_OLD);

/*
 * \brief destroy a snapshot
 *
 * \param mailbox_snapshot The snapshot to destroy.
 * \retval NULL
 */
struct ast_vm_mailbox_snapshot *ast_vm_mailbox_snapshot_destroy(struct ast_vm_mailbox_snapshot *mailbox_snapshot);

/*!
 * \brief Move messages from one folder to another
 *
 * \param mailbox The mailbox to which the folders belong
 * \param context The voicemail context for the mailbox
 * \param num_msgs The number of messages to move
 * \param oldfolder The folder from where messages should be moved
 * \param old_msg_nums The message IDs of the messages to move
 * \param newfolder The folder to which messages should be moved
 * \param new_msg_ids[out] An array of message IDs for the messages as they are in the
 * new folder. This array must be num_msgs sized.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
int ast_vm_msg_move(const char *mailbox,
	const char *context,
	size_t num_msgs,
	const char *oldfolder,
	const char *old_msg_ids [],
	const char *newfolder);

/*!
 * \brief Remove/delete messages from a mailbox folder.
 *
 * \param mailbox The mailbox from which to delete messages
 * \param context The voicemail context for the mailbox
 * \param num_msgs The number of messages to delete
 * \param folder The folder from which to remove messages
 * \param msgs The message IDs of the messages to delete
 * 
 * \retval -1 Failure
 * \retval 0 Success
 */
int ast_vm_msg_remove(const char *mailbox,
	const char *context,
	size_t num_msgs,
	const char *folder,
	const char *msgs []);

/*!
 * \brief forward a message from one mailbox to another.
 *
 * \brief from_mailbox The original mailbox the message is being forwarded from
 * \brief from_context The voicemail context of the from_mailbox
 * \brief from_folder The folder from which the message is being forwarded
 * \brief to_mailbox The mailbox to forward the message to
 * \brief to_context The voicemail context of the to_mailbox
 * \brief to_folder The voicemail folder to forward the message to
 * \brief num_msgs The number of messages being forwarded
 * \brief msg_ids The message IDs of the messages in from_mailbox to forward
 * \brief delete_old If non-zero, the forwarded messages are also deleted from from_mailbox.
 * Otherwise, the messages will remain in the from_mailbox.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
int ast_vm_msg_forward(const char *from_mailbox,
	const char *from_context,
	const char *from_folder,
	const char *to_mailbox,
	const char *to_context,
	const char *to_folder,
	size_t num_msgs,
	const char *msg_ids [],
	int delete_old);

/*!
 * \brief Voicemail playback callback function definition
 *
 * \param channel to play the file back on.
 * \param location of file on disk
 * \param duration of file in seconds. This will be zero if msg is very short or
 * has an unknown duration.
 */
typedef void (ast_vm_msg_play_cb)(struct ast_channel *chan, const char *playfile, int duration);

/*!
 * \brief Play a voicemail msg back on a channel.
 *
 * \param mailbox msg is in.
 * \param context of mailbox.
 * \param voicemail folder to look in.
 * \param message number in the voicemailbox to playback to the channel.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_vm_msg_play(struct ast_channel *chan,
	const char *mailbox,
	const char *context,
	const char *folder,
	const char *msg_id,
	ast_vm_msg_play_cb cb);

/*!
 * \brief Get the name of a folder given its numeric index
 *
 * \param index The integer value of the mailbox.
 * \retval "" Invalid index provided
 * \retval other The name of the mailbox
 */
const char *ast_vm_index_to_foldername(unsigned int index);

#ifdef TEST_FRAMEWORK
/*!
 * \brief Add a user to the voicemail system for test purposes
 * \param context The context of the mailbox
 * \param mailbox The mailbox for the user
 * \retval 0 success
 * \retval other failure
 */
int ast_vm_test_create_user(const char *context, const char *mailbox);

/*!
 * \brief Dispose of a user.  This should be used to destroy a user that was
 * previously created using ast_vm_test_create_user
 * \param context The context of the mailbox
 * \param mailbox The mailbox for the user to destroy
 */
int ast_vm_test_destroy_user(const char *context, const char *mailbox);

#endif

#endif
