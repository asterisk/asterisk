/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Application convenience functions, designed to give consistent
 *        look and feel to Asterisk apps.
 */

#ifndef _ASTERISK_APP_H
#define _ASTERISK_APP_H

#include "asterisk/stringfields.h"
#include "asterisk/strings.h"
#include "asterisk/threadstorage.h"
#include "asterisk/file.h"
#include "asterisk/linkedlists.h"
#include "asterisk/utils.h"
#include "asterisk/stasis.h"

struct ast_flags64;

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

AST_THREADSTORAGE_EXTERNAL(ast_str_thread_global_buf);

/* IVR stuff */

/*! \brief Callback function for IVR
    \return returns 0 on completion, -1 on hangup or digit if interrupted
  */
typedef int (ast_ivr_callback)(struct ast_channel *chan, char *option, void *cbdata);

typedef enum {
	AST_ACTION_UPONE,	/*!< adata is unused */
	AST_ACTION_EXIT,	/*!< adata is the return value for ast_ivr_menu_run if channel was not hungup */
	AST_ACTION_CALLBACK,	/*!< adata is an ast_ivr_callback */
	AST_ACTION_PLAYBACK,	/*!< adata is file to play */
	AST_ACTION_BACKGROUND,	/*!< adata is file to play */
	AST_ACTION_PLAYLIST,	/*!< adata is list of files, separated by ; to play */
	AST_ACTION_MENU,	/*!< adata is a pointer to an ast_ivr_menu */
	AST_ACTION_REPEAT,	/*!< adata is max # of repeats, cast to a pointer */
	AST_ACTION_RESTART,	/*!< adata is like repeat, but resets repeats to 0 */
	AST_ACTION_TRANSFER,	/*!< adata is a string with exten\verbatim[@context]\endverbatim */
	AST_ACTION_WAITOPTION,	/*!< adata is a timeout, or 0 for defaults */
	AST_ACTION_NOOP,	/*!< adata is unused */
	AST_ACTION_BACKLIST,	/*!< adata is list of files separated by ; allows interruption */
} ast_ivr_action;

/*!
    Special "options" are:
   \arg "s" - "start here (one time greeting)"
   \arg "g" - "greeting/instructions"
   \arg "t" - "timeout"
   \arg "h" - "hangup"
   \arg "i" - "invalid selection"

*/
struct ast_ivr_option {
	char *option;
	ast_ivr_action action;
	void *adata;
};

struct ast_ivr_menu {
	char *title;		/*!< Title of menu */
	unsigned int flags;	/*!< Flags */
	struct ast_ivr_option *options;	/*!< All options */
};

/*!
 * \brief Structure used for ast_copy_recording_to_vm in order to cleanly supply
 * data needed for making the recording from the recorded file.
 */
struct ast_vm_recording_data {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(context);
		AST_STRING_FIELD(mailbox);
		AST_STRING_FIELD(folder);
		AST_STRING_FIELD(recording_file);
		AST_STRING_FIELD(recording_ext);

		AST_STRING_FIELD(call_context);
		AST_STRING_FIELD(call_macrocontext);
		AST_STRING_FIELD(call_extension);
		AST_STRING_FIELD(call_callerchan);
		AST_STRING_FIELD(call_callerid);
		);
	int call_priority;
};

#define AST_IVR_FLAG_AUTORESTART (1 << 0)

#define AST_IVR_DECLARE_MENU(holder, title, flags, foo...) \
	static struct ast_ivr_option __options_##holder[] = foo;\
	static struct ast_ivr_menu holder = { title, flags, __options_##holder }

enum ast_timelen {
	TIMELEN_HOURS,
	TIMELEN_MINUTES,
	TIMELEN_SECONDS,
	TIMELEN_MILLISECONDS,
};

/*!	\brief Runs an IVR menu
	\return returns 0 on successful completion, -1 on hangup, or -2 on user error in menu */
int ast_ivr_menu_run(struct ast_channel *c, struct ast_ivr_menu *menu, void *cbdata);

/*! \brief Plays a stream and gets DTMF data from a channel
 * \param c Which channel one is interacting with
 * \param prompt File to pass to ast_streamfile (the one that you wish to play).
 *        It is also valid for this to be multiple files concatenated by "&".
 *        For example, "file1&file2&file3".
 * \param s The location where the DTMF data will be stored
 * \param maxlen Max Length of the data
 * \param timeout Timeout length waiting for data(in milliseconds).  Set to 0 for standard timeout(six seconds), or -1 for no time out.
 *
 *  This function was designed for application programmers for situations where they need
 *  to play a message and then get some DTMF data in response to the message.  If a digit
 *  is pressed during playback, it will immediately break out of the message and continue
 *  execution of your code.
 */
int ast_app_getdata(struct ast_channel *c, const char *prompt, char *s, int maxlen, int timeout);

/*! \brief Full version with audiofd and controlfd.  NOTE: returns '2' on ctrlfd available, not '1' like other full functions */
int ast_app_getdata_full(struct ast_channel *c, const char *prompt, char *s, int maxlen, int timeout, int audiofd, int ctrlfd);

/*!
 * \brief Run a macro on a channel, placing an optional second channel into autoservice.
 * \since 11.0
 *
 * \details
 * This is a shorthand method that makes it very easy to run a
 * macro on any given channel.  It is perfectly reasonable to
 * supply a NULL autoservice_chan here in case there is no
 * channel to place into autoservice.
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 *
 * \param autoservice_chan A channel to place into autoservice while the macro is run
 * \param macro_chan Channel to execute macro on.
 * \param macro_args Macro application argument string.
 *
 * \retval 0 success
 * \retval -1 on error
 */
int ast_app_exec_macro(struct ast_channel *autoservice_chan, struct ast_channel *macro_chan, const char *macro_args);

/*!
 * \since 1.8
 * \brief Run a macro on a channel, placing an optional second channel into autoservice.
 *
 * \details
 * This is a shorthand method that makes it very easy to run a
 * macro on any given channel.  It is perfectly reasonable to
 * supply a NULL autoservice_chan here in case there is no
 * channel to place into autoservice.
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 *
 * \param autoservice_chan A channel to place into autoservice while the macro is run
 * \param macro_chan Channel to execute macro on.
 * \param macro_name The name of the macro to run.
 * \param macro_args The arguments to pass to the macro.
 *
 * \retval 0 success
 * \retval -1 on error
 */
int ast_app_run_macro(struct ast_channel *autoservice_chan,
	struct ast_channel *macro_chan, const char *macro_name, const char *macro_args);

/*!
 * \brief Stack applications callback functions.
 */
struct ast_app_stack_funcs {
	/*!
	 * Module reference pointer so the module will stick around
	 * while a callback is active.
	 */
	void *module;

	/*!
	 * \brief Callback for the routine to run a subroutine on a channel.
	 *
	 * \note Absolutely _NO_ channel locks should be held before calling this function.
	 *
	 * \param chan Channel to execute subroutine on.
	 * \param args Gosub application argument string.
	 * \param ignore_hangup TRUE if a hangup does not stop execution of the routine.
	 *
	 * \retval 0 success
	 * \retval -1 on error
	 */
	int (*run_sub)(struct ast_channel *chan, const char *args, int ignore_hangup);

	/*!
	 * \brief Add missing context/exten to Gosub application argument string.
	 *
	 * \param chan Channel to obtain context/exten.
	 * \param args Gosub application argument string.
	 *
	 * \details
	 * Fills in the optional context and exten from the given channel.
	 *
	 * \retval New-args Gosub argument string on success.  Must be freed.
	 * \retval NULL on error.
	 */
	const char *(*expand_sub_args)(struct ast_channel *chan, const char *args);

	/* Add new API calls to the end here. */
};

/*!
 * \since 11
 * \brief Set stack application function callbacks
 * \param funcs Stack applications callback functions.
 */
void ast_install_stack_functions(const struct ast_app_stack_funcs *funcs);

/*!
 * \brief Add missing context/exten to subroutine argument string.
 *
 * \param chan Channel to obtain context/exten.
 * \param args Gosub application argument string.
 *
 * \details
 * Fills in the optional context and exten from the given channel.
 *
 * \retval New-args Gosub argument string on success.  Must be freed.
 * \retval NULL on error.
 */
const char *ast_app_expand_sub_args(struct ast_channel *chan, const char *args);

/*!
 * \since 11
 * \brief Run a subroutine on a channel, placing an optional second channel into autoservice.
 *
 * \details
 * This is a shorthand method that makes it very easy to run a
 * subroutine on any given channel.  It is perfectly reasonable
 * to supply a NULL autoservice_chan here in case there is no
 * channel to place into autoservice.
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 *
 * \param autoservice_chan A channel to place into autoservice while the subroutine is run
 * \param sub_chan Channel to execute subroutine on.
 * \param sub_args Gosub application argument string.
 * \param ignore_hangup TRUE if a hangup does not stop execution of the routine.
 *
 * \retval 0 success
 * \retval -1 on error
 */
int ast_app_exec_sub(struct ast_channel *autoservice_chan, struct ast_channel *sub_chan, const char *sub_args, int ignore_hangup);

/*!
 * \since 11
 * \brief Run a subroutine on a channel, placing an optional second channel into autoservice.
 *
 * \details
 * This is a shorthand method that makes it very easy to run a
 * subroutine on any given channel.  It is perfectly reasonable
 * to supply a NULL autoservice_chan here in case there is no
 * channel to place into autoservice.
 *
 * \note Absolutely _NO_ channel locks should be held before calling this function.
 *
 * \param autoservice_chan A channel to place into autoservice while the subroutine is run
 * \param sub_chan Channel to execute subroutine on.
 * \param sub_location The location of the subroutine to run.
 * \param sub_args The arguments to pass to the subroutine.
 * \param ignore_hangup TRUE if a hangup does not stop execution of the routine.
 *
 * \retval 0 success
 * \retval -1 on error
 */
int ast_app_run_sub(struct ast_channel *autoservice_chan,
	struct ast_channel *sub_chan, const char *sub_location, const char *sub_args, int ignore_hangup);

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
	int folders;
	/* Things are not quite as they seem here.  This points to an allocated array of lists. */
	AST_LIST_HEAD_NOLOCK(, ast_vm_msg_snapshot) *snapshots;
};

/*!
 * \brief Voicemail playback callback function definition
 *
 * \param chan Channel to play the file back on.
 * \param playfile Location of file on disk
 * \param duration of file in seconds. This will be zero if msg is very short or
 * has an unknown duration.
 */
typedef void (ast_vm_msg_play_cb)(struct ast_channel *chan, const char *playfile, int duration);

/*!
 * \brief Determines if the given folder has messages.
 *
 * \param mailboxes Comma or & delimited list of mailboxes (user@context).
 *          If no context is found, uses 'default' for the context.
 * \param folder The folder to look in.  Default is INBOX if not provided.
 *
 * \retval 1 if the folder has one or more messages.
 * \retval 0 otherwise.
 */
typedef int (ast_has_voicemail_fn)(const char *mailboxes, const char *folder);

/*!
 * \brief Gets the number of messages that exist for the mailbox list.
 *
 * \param mailboxes Comma or space delimited list of mailboxes (user@context).
 *          If no context is found, uses 'default' for the context.
 * \param newmsgs Where to put the count of new messages. (Can be NULL)
 * \param oldmsgs Where to put the count of old messages. (Can be NULL)
 *
 * \details
 * Simultaneously determines the count of new + urgent and old
 * messages.  The total messages would then be the sum of these.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
typedef int (ast_inboxcount_fn)(const char *mailboxes, int *newmsgs, int *oldmsgs);

/*!
 * \brief Gets the number of messages that exist for the mailbox list.
 *
 * \param mailboxes Comma or space delimited list of mailboxes (user@context).
 *          If no context is found, uses 'default' for the context.
 * \param urgentmsgs Where to put the count of urgent messages. (Can be NULL)
 * \param newmsgs Where to put the count of new messages. (Can be NULL)
 * \param oldmsgs Where to put the count of old messages. (Can be NULL)
 *
 * \details
 * Simultaneously determines the count of new, old, and urgent
 * messages.  The total messages would then be the sum of these
 * three.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
typedef int (ast_inboxcount2_fn)(const char *mailboxes, int *urgentmsgs, int *newmsgs, int *oldmsgs);

/*!
 * \brief Gets the number of messages that exist in a mailbox folder.
 *
 * \param mailbox_id The mailbox name.
 * \param folder The folder to look in.  Default is INBOX if not provided.
 *
 * \note If requesting INBOX then the returned count is INBOX + Urgent.
 *
 * \return The number of messages in the mailbox folder (zero or more).
 */
typedef int (ast_messagecount_fn)(const char *mailbox_id, const char *folder);

/*!
 * \brief Play a recorded user name for the mailbox to the specified channel.
 *
 * \param chan Where to play the recorded name file.
 * \param mailbox_id The mailbox name.
 *
 * \retval 0 Name played without interruption
 * \retval dtmf ASCII value of the DTMF which interrupted playback.
 * \retval -1 Unable to locate mailbox or hangup occurred.
 */
typedef int (ast_sayname_fn)(struct ast_channel *chan, const char *mailbox_id);

/*!
 * \brief Creates a voicemail based on a specified file to a mailbox.
 *
 * \param vm_rec_data A record containing filename and voicemail txt info.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
typedef int (ast_copy_recording_to_vm_fn)(struct ast_vm_recording_data *vm_rec_data);

/*!
 * \brief Convert the mailbox folder id to a folder name.
 *
 * \param id Mailbox folder id to convert.
 *
 * \deprecated Nothing calls it and nothing ever should.
 *
 * \return The folder name associated with the id.
 */
typedef const char *(ast_vm_index_to_foldername_fn)(int id);

/*!
 * \brief Create a snapshot of a mailbox which contains information about every msg.
 *
 * \param user The user part of user@context.
 * \param context The context part of user@context.  Must be explicit.
 * \param folder When not NULL only msgs from the specified folder will be included.
 * \param descending list the msgs in descending order rather than ascending order.
 * \param sort_val What to sort in the snapshot.
 * \param combine_INBOX_and_OLD When this argument is set, The OLD folder will be represented
 *        in the INBOX folder of the snapshot. This allows the snapshot to represent the
 *        OLD and INBOX messages in sorted order merged together.
 *
 * \note Only used by voicemail unit tests.
 *
 * \retval snapshot on success
 * \retval NULL on failure
 */
typedef struct ast_vm_mailbox_snapshot *(ast_vm_mailbox_snapshot_create_fn)(const char *user,
	const char *context, const char *folder, int descending,
	enum ast_vm_snapshot_sort_val sort_val, int combine_INBOX_and_OLD);

/*!
 * \brief destroy a snapshot
 *
 * \param mailbox_snapshot The snapshot to destroy.
 *
 * \note Only used by voicemail unit tests.
 *
 * \retval NULL
 */
typedef struct ast_vm_mailbox_snapshot *(ast_vm_mailbox_snapshot_destroy_fn)(struct ast_vm_mailbox_snapshot *mailbox_snapshot);

/*!
 * \brief Move messages from one folder to another
 *
 * \param mailbox The mailbox to which the folders belong
 * \param context The voicemail context for the mailbox
 * \param num_msgs The number of messages to move
 * \param oldfolder The folder from where messages should be moved
 * \param old_msg_ids The message IDs of the messages to move
 * \param newfolder The folder to which messages should be moved
 *    new folder. This array must be num_msgs sized.
 *
 * \note Only used by voicemail unit tests.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
typedef int (ast_vm_msg_move_fn)(const char *mailbox, const char *context, size_t num_msgs,
	const char *oldfolder, const char *old_msg_ids[], const char *newfolder);

/*!
 * \brief Remove/delete messages from a mailbox folder.
 *
 * \param mailbox The mailbox from which to delete messages
 * \param context The voicemail context for the mailbox
 * \param num_msgs The number of messages to delete
 * \param folder The folder from which to remove messages
 * \param msgs The message IDs of the messages to delete
 *
 * \note Only used by voicemail unit tests.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
typedef int (ast_vm_msg_remove_fn)(const char *mailbox, const char *context, size_t num_msgs,
	const char *folder, const char *msgs[]);

/*!
 * \brief forward a message from one mailbox to another.
 *
 * \brief from_mailbox The original mailbox the message is being forwarded from
 * \brief from_context The voicemail context of the from_mailbox
 * \brief from_folder The folder from which the message is being forwarded
 * \brief to_mailbox The mailbox to forward the message to
 * \brief to_context The voicemail context of the to_mailbox
 * \brief to_folder The folder to which the message is being forwarded
 * \brief num_msgs The number of messages being forwarded
 * \brief msg_ids The message IDs of the messages in from_mailbox to forward
 * \brief delete_old If non-zero, the forwarded messages are also deleted from from_mailbox.
 * Otherwise, the messages will remain in the from_mailbox.
 *
 * \note Only used by voicemail unit tests.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
typedef int (ast_vm_msg_forward_fn)(const char *from_mailbox, const char *from_context,
	const char *from_folder, const char *to_mailbox, const char *to_context,
	const char *to_folder, size_t num_msgs, const char *msg_ids[], int delete_old);

/*!
 * \brief Play a voicemail msg back on a channel.
 *
 * \param chan
 * \param mailbox msg is in.
 * \param context of mailbox.
 * \param folder voicemail folder to look in.
 * \param msg_num message number in the voicemailbox to playback to the channel.
 * \param cb
 *
 * \note Only used by voicemail unit tests.
 *
 * \retval 0 success
 * \retval -1 failure
 */
typedef int (ast_vm_msg_play_fn)(struct ast_channel *chan, const char *mailbox,
	const char *context, const char *folder, const char *msg_num, ast_vm_msg_play_cb *cb);

#define VM_MODULE_VERSION 2

/*! \brief Voicemail function table definition. */
struct ast_vm_functions {
	/*!
	 * \brief The version of this function table.
	 *
	 * \note If the ABI for this table changes, the module version
	 * (\ref VM_MODULE_VERSION) should be incremented.
	 */
	unsigned int module_version;
	/*! \brief The name of the module that provides the voicemail functionality */
	const char *module_name;
	/*! \brief The module for the voicemail provider */
	struct ast_module *module;

	ast_has_voicemail_fn *has_voicemail;
	ast_inboxcount_fn *inboxcount;
	ast_inboxcount2_fn *inboxcount2;
	ast_messagecount_fn *messagecount;
	ast_copy_recording_to_vm_fn *copy_recording_to_vm;
	ast_vm_index_to_foldername_fn *index_to_foldername;
	ast_vm_mailbox_snapshot_create_fn *mailbox_snapshot_create;
	ast_vm_mailbox_snapshot_destroy_fn *mailbox_snapshot_destroy;
	ast_vm_msg_move_fn *msg_move;
	ast_vm_msg_remove_fn *msg_remove;
	ast_vm_msg_forward_fn *msg_forward;
	ast_vm_msg_play_fn *msg_play;
};

/*!
 * \brief Determine if a voicemail provider is registered.
 * \since 12.0.0
 *
 * \retval 0 if no provider registered.
 * \retval 1 if a provider is registered.
 */
int ast_vm_is_registered(void);

/*!
 * \brief Set voicemail function callbacks
 *
 * \param vm_table Voicemail function table to install.
 * \param module Pointer to the module implementing the interface
 *
 * \retval 0 on success.
 * \retval -1 on error.
 * \retval AST_MODULE_LOAD_DECLINE if there's already another provider registered.
 */
int __ast_vm_register(const struct ast_vm_functions *vm_table, struct ast_module *module);

/*! \brief See \ref __ast_vm_register() */
#define ast_vm_register(vm_table) __ast_vm_register(vm_table, AST_MODULE_SELF)

/*!
 * \brief Unregister the specified voicemail provider
 *
 * \param The module name of the provider to unregister
 *
 * \return Nothing
 */
void ast_vm_unregister(const char *module_name);

#ifdef TEST_FRAMEWORK
/*!
 * \brief Swap out existing voicemail functions with a temporary set of functions for use with unit tests
 *
 * \param vm_table function table to use for testing
 *
 * \note ast_vm_test_swap_table_out should be called to restore the original set before testing concludes
 */
void ast_vm_test_swap_table_in(const struct ast_vm_functions *vm_table);

/*!
 * \brief Used after ast_vm_test_swap_table_in to restore the original set of voicemail functions
 */
void ast_vm_test_swap_table_out(void);
#endif

#define VM_GREETER_MODULE_VERSION 1

/*! \brief Voicemail greeter function table definition. */
struct ast_vm_greeter_functions {
	/*!
	 * \brief The version of this function table.
	 *
	 * \note If the ABI for this table changes, the module version
	 * (\ref VM_GREETER_MODULE_VERSION) should be incremented.
	 */
	unsigned int module_version;
	/*! \brief The name of the module that provides the voicemail greeter functionality */
	const char *module_name;
	/*! \brief The module for the voicemail greeter provider */
	struct ast_module *module;

	ast_sayname_fn *sayname;
};

/*!
 * \brief Determine if a voicemail greeter provider is registered.
 * \since 13.0.0
 *
 * \retval 0 if no provider registered.
 * \retval 1 if a provider is registered.
 */
int ast_vm_greeter_is_registered(void);

/*!
 * \brief Set voicemail greeter function callbacks
 * \since 13.0.0
 *
 * \param vm_table Voicemail greeter function table to install.
 * \param module Pointer to the module implementing the interface
 *
 * \retval 0 on success.
 * \retval -1 on error.
 * \retval AST_MODULE_LOAD_DECLINE if there's already another greeter registered.
 */
int __ast_vm_greeter_register(const struct ast_vm_greeter_functions *vm_table, struct ast_module *module);

/*! \brief See \ref __ast_vm_greeter_register() */
#define ast_vm_greeter_register(vm_table) __ast_vm_greeter_register(vm_table, AST_MODULE_SELF)

/*!
 * \brief Unregister the specified voicemail greeter provider
 * \since 13.0.0
 *
 * \param The module name of the provider to unregister
 *
 * \return Nothing
 */
void ast_vm_greeter_unregister(const char *module_name);

#ifdef TEST_FRAMEWORK
typedef int (ast_vm_test_create_user_fn)(const char *context, const char *user);
typedef int (ast_vm_test_destroy_user_fn)(const char *context, const char *user);

void ast_install_vm_test_functions(ast_vm_test_create_user_fn *vm_test_create_user_func,
	ast_vm_test_destroy_user_fn *vm_test_destroy_user_func);

void ast_uninstall_vm_test_functions(void);
#endif

/*!
 * \brief
 * param[in] vm_rec_data Contains data needed to make the recording.
 * retval 0 voicemail successfully created from recording.
 * retval -1 Failure
 */
int ast_app_copy_recording_to_vm(struct ast_vm_recording_data *vm_rec_data);

/*!
 * \brief Determine if a given mailbox has any voicemail
 * If folder is NULL, defaults to "INBOX".  If folder is "INBOX", includes the
 * number of messages in the "Urgent" folder.
 * \retval 1 Mailbox has voicemail
 * \retval 0 No new voicemail in specified mailbox
 * \retval -1 Failure
 * \since 1.0
 */
int ast_app_has_voicemail(const char *mailboxes, const char *folder);

/*!
 * \brief Determine number of new/old messages in a mailbox
 * \since 1.0
 * \param[in] mailboxes Mailbox specification in the format
 * 	/code
 *	 mbox[\@context][&mbox2[\@context2]][...]
 *	/code
 * \param[out] newmsgs Number of messages in the "INBOX" folder.  Includes number of messages in the "Urgent" folder, if any.
 * \param[out] oldmsgs Number of messages in the "Old" folder.
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_app_inboxcount(const char *mailboxes, int *newmsgs, int *oldmsgs);

/*!
 * \brief Determine number of urgent/new/old messages in a mailbox
 * \param[in] mailboxes the mailbox context to use
 * \param[out] urgentmsgs the urgent message count
 * \param[out] newmsgs the new message count
 * \param[out] oldmsgs the old message count
 * \return Returns 0 for success, negative upon error
 * \since 1.6.1
 */
int ast_app_inboxcount2(const char *mailboxes, int *urgentmsgs, int *newmsgs, int *oldmsgs);

/*!
 * \brief Play a recorded user name for the mailbox to the specified channel.
 *
 * \param chan Where to play the recorded name file.
 * \param mailbox_id The mailbox name.
 *
 * \retval 0 Name played without interruption
 * \retval dtmf ASCII value of the DTMF which interrupted playback.
 * \retval -1 Unable to locate mailbox or hangup occurred.
 */
int ast_app_sayname(struct ast_channel *chan, const char *mailbox_id);

/*!
 * \brief Get the number of messages in a given mailbox folder
 *
 * \param[in] mailbox_id Mailbox name
 * \param[in] folder The folder to look in.  Default is INBOX if not provided.
 *
 * \note If requesting INBOX then the returned count is INBOX + Urgent.
 *
 * \return The number of messages in the mailbox folder (zero or more).
 */
int ast_app_messagecount(const char *mailbox_id, const char *folder);

/*!
 * \brief Return name of folder, given an id
 * \param[in] id Folder id
 * \return Name of folder
 */
const char *ast_vm_index_to_foldername(int id);

/*!
 * \brief Create a snapshot of a mailbox which contains information about every msg.
 *
 * \param mailbox, the mailbox to look for
 * \param context, the context to look for the mailbox in
 * \param folder, OPTIONAL.  When not NULL only msgs from the specified folder will be included.
 * \param descending, list the msgs in descending order rather than ascending order.
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

/*!
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
 * \param old_msg_ids The message IDs of the messages to move
 * \param newfolder The folder to which messages should be moved
 * new folder. This array must be num_msgs sized.
 *
 * \retval -1 Failure
 * \retval 0 Success
 */
int ast_vm_msg_move(const char *mailbox,
	const char *context,
	size_t num_msgs,
	const char *oldfolder,
	const char *old_msg_ids[],
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
	const char *msgs[]);

/*!
 * \brief forward a message from one mailbox to another.
 *
 * \brief from_mailbox The original mailbox the message is being forwarded from
 * \brief from_context The voicemail context of the from_mailbox
 * \brief from_folder The folder from which the message is being forwarded
 * \brief to_mailbox The mailbox to forward the message to
 * \brief to_context The voicemail context of the to_mailbox
 * \brief to_folder The folder to which the message is being forwarded
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
	const char *msg_ids[],
	int delete_old);

/*!
 * \brief Play a voicemail msg back on a channel.
 *
 * \param chan
 * \param mailbox msg is in.
 * \param context of mailbox.
 * \param folder voicemail folder to look in.
 * \param msg_num message number in the voicemailbox to playback to the channel.
 * \param cb
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_vm_msg_play(struct ast_channel *chan,
	const char *mailbox,
	const char *context,
	const char *folder,
	const char *msg_num,
	ast_vm_msg_play_cb *cb);

#ifdef TEST_FRAMEWORK
int ast_vm_test_destroy_user(const char *context, const char *mailbox);
int ast_vm_test_create_user(const char *context, const char *mailbox);
#endif

/*!
 * \brief Safely spawn an external program while closing file descriptors
 *
 * \note This replaces the \b execvp call in all Asterisk modules
 *
 * \param dualfork Non-zero to simulate running the program in the
 * background by forking twice.  The option provides similar
 * functionality to the '&' in the OS shell command "cmd &".  The
 * option allows Asterisk to run a reaper loop to watch the first fork
 * which immediately exits after spaning the second fork.  The actual
 * program is run in the second fork.
 * \param file execvp(file, argv) file parameter
 * \param argv execvp(file, argv) argv parameter
 */
int ast_safe_execvp(int dualfork, const char *file, char *const argv[]);

/*!
 * \brief Safely spawn an OS shell command while closing file descriptors
 *
 * \note This replaces the \b system call in all Asterisk modules
 *
 * \param s - OS shell command string to execute.
 *
 * \warning Command injection can happen using this call if the passed
 * in string is created using untrusted data from an external source.
 * It is best not to use untrusted data.  However, the caller could
 * filter out dangerous characters to avoid command injection.
 */
int ast_safe_system(const char *s);

/*!
 * \brief Replace the SIGCHLD handler
 *
 * Normally, Asterisk has a SIGCHLD handler that is cleaning up all zombie
 * processes from forking elsewhere in Asterisk.  However, if you want to
 * wait*() on the process to retrieve information about it's exit status,
 * then this signal handler needs to be temporarily replaced.
 *
 * Code that executes this function *must* call ast_unreplace_sigchld()
 * after it is finished doing the wait*().
 */
void ast_replace_sigchld(void);

/*!
 * \brief Restore the SIGCHLD handler
 *
 * This function is called after a call to ast_replace_sigchld.  It restores
 * the SIGCHLD handler that cleans up any zombie processes.
 */
void ast_unreplace_sigchld(void);

/*!
 * \brief Send a string of DTMF digits to a channel
 *
 * \param chan    The channel that will receive the DTMF frames
 * \param peer    (optional) Peer channel that will be autoserviced while the
 *                primary channel is receiving DTMF
 * \param digits  This is a string of characters representing the DTMF digits
 *                to be sent to the channel.  Valid characters are
 *                "0123456789*#abcdABCD".  Note: You can pass arguments 'f' or
 *                'F', if you want to Flash the channel (if supported by the
 *                channel), or 'w' to add a 500 millisecond pause to the DTMF
 *                sequence.
 * \param between This is the number of milliseconds to wait in between each
 *                DTMF digit.  If zero milliseconds is specified, then the
 *                default value of 100 will be used.
 * \param duration This is the duration that each DTMF digit should have.
 *
 * \pre This must only be called by the channel's media handler thread.
 *
 * \retval 0 on success.
 * \retval -1 on failure or a channel hung up.
 */
int ast_dtmf_stream(struct ast_channel *chan, struct ast_channel *peer, const char *digits, int between, unsigned int duration);

/*!
 * \brief Send a string of DTMF digits to a channel from an external thread.
 *
 * \param chan    The channel that will receive the DTMF frames
 * \param digits  This is a string of characters representing the DTMF digits
 *                to be sent to the channel.  Valid characters are
 *                "0123456789*#abcdABCD".  Note: You can pass arguments 'f' or
 *                'F', if you want to Flash the channel (if supported by the
 *                channel), or 'w' to add a 500 millisecond pause to the DTMF
 *                sequence.
 * \param between This is the number of milliseconds to wait in between each
 *                DTMF digit.  If zero milliseconds is specified, then the
 *                default value of 100 will be used.
 * \param duration This is the duration that each DTMF digit should have.
 *
 * \pre This must only be called by threads that are not the channel's
 * media handler thread.
 *
 * \return Nothing
 */
void ast_dtmf_stream_external(struct ast_channel *chan, const char *digits, int between, unsigned int duration);

/*! \brief Stream a filename (or file descriptor) as a generator. */
int ast_linear_stream(struct ast_channel *chan, const char *filename, int fd, int allowoverride);

/*!
 * \brief Stream a file with fast forward, pause, reverse, restart.
 * \param chan Channel
 * \param file File to play.
 * \param fwd, rev, stop, pause, restart DTMF keys for media control
 * \param skipms Number of milliseconds to skip for fwd/rev.
 * \param offsetms Number of milliseconds to skip when starting the media.
 *
 * Before calling this function, set this to be the number
 * of ms to start from the beginning of the file.  When the function
 * returns, it will be the number of ms from the beginning where the
 * playback stopped.  Pass NULL if you don't care.
 *
 * \retval 0 on success
 * \retval Non-zero on failure
 */
int ast_control_streamfile(struct ast_channel *chan, const char *file, const char *fwd, const char *rev, const char *stop, const char *pause, const char *restart, int skipms, long *offsetms);

/*!
 * \brief Version of ast_control_streamfile() which allows the language of the
 * media file to be specified.
 *
 * \retval 0 on success
 * \retval Non-zero on failure
 */
int ast_control_streamfile_lang(struct ast_channel *chan, const char *file,
	const char *fwd, const char *rev, const char *stop, const char *suspend,
	const char *restart, int skipms, const char *lang, long *offsetms);

/*!
 * \brief Controls playback of a tone
 *
 * \retval 0 on success
 * \retval Non-zero on failure
 */
int ast_control_tone(struct ast_channel *chan, const char *tone);

/*!
 * \brief Stream a file with fast forward, pause, reverse, restart.
 * \param chan
 * \param file filename
 * \param fwd, rev, stop, pause, restart, skipms, offsetms
 * \param cb waitstream callback to invoke when fastforward or rewind occurrs.
 *
 * Before calling this function, set this to be the number
 * of ms to start from the beginning of the file.  When the function
 * returns, it will be the number of ms from the beginning where the
 * playback stopped.  Pass NULL if you don't care.
 */
int ast_control_streamfile_w_cb(struct ast_channel *chan,
	const char *file,
	const char *fwd,
	const char *rev,
	const char *stop,
	const char *pause,
	const char *restart,
	int skipms,
	long *offsetms,
	ast_waitstream_fr_cb cb);

/*! \brief Play a stream and wait for a digit, returning the digit that was pressed */
int ast_play_and_wait(struct ast_channel *chan, const char *fn);

/*!
 * Possible actions to take if a recording already exists
 * \since 12
 */
enum ast_record_if_exists {
	/*! Return an Error State for IF_Exists */
	AST_RECORD_IF_EXISTS_ERROR = -1,
	/*! Fail the recording. */
	AST_RECORD_IF_EXISTS_FAIL,
	/*! Overwrite the existing recording. */
	AST_RECORD_IF_EXISTS_OVERWRITE,
	/*! Append to the existing recording. */
	AST_RECORD_IF_EXISTS_APPEND,
};

/*!
 * \brief Record a file based on input from a channel
 *        This function will play "auth-thankyou" upon successful recording if
 *        skip_confirmation_sound is false.
 *
 * \param chan the channel being recorded
 * \param playfile Filename of sound to play before recording begins. A beep is also played when playfile completes, before the recording begins.
 * \param recordfile Filename to save the recording
 * \param maxtime_sec Longest possible message length in seconds
 * \param fmt string containing all formats to be recorded delimited by '|'
 * \param duration pointer to integer for storing length of the recording
 * \param beep If true, play a beep before recording begins (and doesn't play \a playfile)
 * \param sound_duration pointer to integer for storing length of the recording minus all silence
 * \param silencethreshold tolerance of noise levels that can be considered silence for the purpose of silence timeout, -1 for default
 * \param maxsilence_ms Length of time in milliseconds which will trigger a timeout from silence, -1 for default
 * \param path Optional filesystem path to unlock
 * \param acceptdtmf Character of DTMF to end and accept the recording
 * \param canceldtmf Character of DTMF to end and cancel the recording
 * \param skip_confirmation_sound If true, don't play auth-thankyou at end. Nice for custom recording prompts in apps.
 * \param if_exists Action to take if recording already exists.
 *
 * \retval -1 failure or hangup
 * \retval 'S' Recording ended from silence timeout
 * \retval 't' Recording ended from the message exceeding the maximum duration
 * \retval dtmfchar Recording ended via the return value's DTMF character for either cancel or accept.
 */
int ast_play_and_record_full(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime_sec, const char *fmt, int *duration, int *sound_duration, int beep, int silencethreshold, int maxsilence_ms, const char *path, const char *acceptdtmf, const char *canceldtmf, int skip_confirmation_sound, enum ast_record_if_exists if_exists);

/*!
 * \brief Record a file based on input from a channel. Use default accept and cancel DTMF.
 *        This function will play "auth-thankyou" upon successful recording.
 *
 * \param chan the channel being recorded
 * \param playfile Filename of sound to play before recording begins
 * \param recordfile Filename to save the recording
 * \param maxtime_sec Longest possible message length in seconds
 * \param fmt string containing all formats to be recorded delimited by '|'
 * \param duration pointer to integer for storing length of the recording
 * \param sound_duration pointer to integer for storing length of the recording minus all silence
 * \param silencethreshold tolerance of noise levels that can be considered silence for the purpose of silence timeout, -1 for default
 * \param maxsilence_ms length of time in milliseconds which will trigger a timeout from silence, -1 for default
 * \param path Optional filesystem path to unlock
 *
 * \retval -1 failure or hangup
 * \retval 'S' Recording ended from silence timeout
 * \retval 't' Recording ended from the message exceeding the maximum duration
 * \retval dtmfchar Recording ended via the return value's DTMF character for either cancel or accept.
 */
int ast_play_and_record(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime_sec, const char *fmt, int *duration, int *sound_duration, int silencethreshold, int maxsilence_ms, const char *path);

/*!
 * \brief Record a file based on input frm a channel. Recording is performed in 'prepend' mode which works a little differently from normal recordings
 *        This function will not play a success message due to post-recording control in the application this was added for.
 *
 * \param chan the channel being recorded
 * \param playfile Filename of sound to play before recording begins
 * \param recordfile Filename to save the recording
 * \param maxtime_sec Longest possible message length in seconds
 * \param fmt string containing all formats to be recorded delimited by '|'
 * \param duration pointer to integer for storing length of the recording
 * \param sound_duration pointer to integer for storing length of the recording minus all silence
 * \param beep whether to play a beep to prompt the recording
 * \param silencethreshold tolerance of noise levels that can be considered silence for the purpose of silence timeout, -1 for default
 * \param maxsilence_ms length of time in milliseconds which will trigger a timeout from silence, -1 for default.
 *
 * \retval -1 failure or hangup
 * \retval 'S' Recording ended from silence timeout
 * \retval 't' Recording either exceeded maximum duration or the call was ended via DTMF
 */
int ast_play_and_prepend(struct ast_channel *chan, char *playfile, char *recordfile, int maxtime_sec, char *fmt, int *duration, int *sound_duration, int beep, int silencethreshold, int maxsilence_ms);

enum ast_getdata_result {
	AST_GETDATA_FAILED = -1,
	AST_GETDATA_COMPLETE = 0,
	AST_GETDATA_TIMEOUT = 1,
	AST_GETDATA_INTERRUPTED = 2,
	/*! indicates a user terminated empty string rather than an empty string resulting
	 * from a timeout or other factors */
	AST_GETDATA_EMPTY_END_TERMINATED = 3,
};

enum AST_LOCK_RESULT {
	AST_LOCK_SUCCESS = 0,
	AST_LOCK_TIMEOUT = -1,
	AST_LOCK_PATH_NOT_FOUND = -2,
	AST_LOCK_FAILURE = -3,
};

/*! \brief Type of locking to use in ast_lock_path / ast_unlock_path */
enum AST_LOCK_TYPE {
	AST_LOCK_TYPE_LOCKFILE = 0,
	AST_LOCK_TYPE_FLOCK = 1,
};

/*!
 * \brief Set the type of locks used by ast_lock_path()
 * \param type the locking type to use
 */
void ast_set_lock_type(enum AST_LOCK_TYPE type);

/*!
 * \brief Lock a filesystem path.
 * \param path the path to be locked
 * \return one of \ref AST_LOCK_RESULT values
 */
enum AST_LOCK_RESULT ast_lock_path(const char *path);

/*! \brief Unlock a path */
int ast_unlock_path(const char *path);

/*! \brief Read a file into asterisk*/
char *ast_read_textfile(const char *file);

struct ast_group_info;

/*! \brief Split a group string into group and category, returning a default category if none is provided. */
int ast_app_group_split_group(const char *data, char *group, int group_max, char *category, int category_max);

/*! \brief Set the group for a channel, splitting the provided data into group and category, if specified. */
int ast_app_group_set_channel(struct ast_channel *chan, const char *data);

/*! \brief Get the current channel count of the specified group and category. */
int ast_app_group_get_count(const char *group, const char *category);

/*! \brief Get the current channel count of all groups that match the specified pattern and category. */
int ast_app_group_match_get_count(const char *groupmatch, const char *category);

/*! \brief Discard all group counting for a channel */
int ast_app_group_discard(struct ast_channel *chan);

/*! \brief Update all group counting for a channel to a new one */
int ast_app_group_update(struct ast_channel *oldchan, struct ast_channel *newchan);

/*! \brief Write Lock the group count list */
int ast_app_group_list_wrlock(void);

/*! \brief Read Lock the group count list */
int ast_app_group_list_rdlock(void);

/*! \brief Get the head of the group count list */
struct ast_group_info *ast_app_group_list_head(void);

/*! \brief Unlock the group count list */
int ast_app_group_list_unlock(void);

/*!
  \brief Define an application argument
  \param name The name of the argument
*/
#define AST_APP_ARG(name) char *name

/*!
  \brief Declare a structure to hold an application's arguments.
  \param name The name of the structure
  \param arglist The list of arguments, defined using AST_APP_ARG

  This macro declares a structure intended to be used in a call
  to ast_app_separate_args(). The structure includes all the
  arguments specified, plus an argv array that overlays them and an
  argc argument counter. The arguments must be declared using AST_APP_ARG,
  and they will all be character pointers (strings).

  \note The structure is <b>not</b> initialized, as the call to
  ast_app_separate_args() will perform that function before parsing
  the arguments.
 */
#define AST_DECLARE_APP_ARGS(name, arglist) AST_DEFINE_APP_ARGS_TYPE(, arglist) name = { 0, }

/*!
  \brief Define a structure type to hold an application's arguments.
  \param type The name of the structure type
  \param arglist The list of arguments, defined using AST_APP_ARG

  This macro defines a structure type intended to be used in a call
  to ast_app_separate_args(). The structure includes all the
  arguments specified, plus an argv array that overlays them and an
  argc argument counter. The arguments must be declared using AST_APP_ARG,
  and they will all be character pointers (strings).

  \note This defines a structure type, but does not declare an instance
  of the structure. That must be done separately.
 */
#define AST_DEFINE_APP_ARGS_TYPE(type, arglist) \
	struct type { \
		unsigned int argc; \
		char *argv[0]; \
		arglist \
	}

/*!
  \brief Performs the 'standard' argument separation process for an application.
  \param args An argument structure defined using AST_DECLARE_APP_ARGS
  \param parse A modifiable buffer containing the input to be parsed

  This function will separate the input string using the standard argument
  separator character ',' and fill in the provided structure, including
  the argc argument counter field.
 */
#define AST_STANDARD_APP_ARGS(args, parse) \
	args.argc = __ast_app_separate_args(parse, ',', 1, args.argv, ((sizeof(args) - offsetof(typeof(args), argv)) / sizeof(args.argv[0])))
#define AST_STANDARD_RAW_ARGS(args, parse) \
	args.argc = __ast_app_separate_args(parse, ',', 0, args.argv, ((sizeof(args) - offsetof(typeof(args), argv)) / sizeof(args.argv[0])))

/*!
  \brief Performs the 'nonstandard' argument separation process for an application.
  \param args An argument structure defined using AST_DECLARE_APP_ARGS
  \param parse A modifiable buffer containing the input to be parsed
  \param sep A nonstandard separator character

  This function will separate the input string using the nonstandard argument
  separator character and fill in the provided structure, including
  the argc argument counter field.
 */
#define AST_NONSTANDARD_APP_ARGS(args, parse, sep) \
	args.argc = __ast_app_separate_args(parse, sep, 1, args.argv, ((sizeof(args) - offsetof(typeof(args), argv)) / sizeof(args.argv[0])))
#define AST_NONSTANDARD_RAW_ARGS(args, parse, sep) \
	args.argc = __ast_app_separate_args(parse, sep, 0, args.argv, ((sizeof(args) - offsetof(typeof(args), argv)) / sizeof(args.argv[0])))

/*!
  \brief Separate a string into arguments in an array
  \param buf The string to be parsed (this must be a writable copy, as it will be modified)
  \param delim The character to be used to delimit arguments
  \param remove_chars Remove backslashes and quote characters, while parsing
  \param array An array of 'char *' to be filled in with pointers to the found arguments
  \param arraylen The number of elements in the array (i.e. the number of arguments you will accept)

  Note: if there are more arguments in the string than the array will hold, the last element of
  the array will contain the remaining arguments, not separated.

  The array will be completely zeroed by this function before it populates any entries.

  \return The number of arguments found, or zero if the function arguments are not valid.
*/
unsigned int __ast_app_separate_args(char *buf, char delim, int remove_chars, char **array, int arraylen);
#define ast_app_separate_args(a,b,c,d)	__ast_app_separate_args(a,b,1,c,d)

/*!
  \brief A structure to hold the description of an application 'option'.

  Application 'options' are single-character flags that can be supplied
  to the application to affect its behavior; they can also optionally
  accept arguments enclosed in parenthesis.

  These structures are used by the ast_app_parse_options function, uses
  this data to fill in a flags structure (to indicate which options were
  supplied) and array of argument pointers (for those options that had
  arguments supplied).
 */
struct ast_app_option {
	/*! \brief The flag bit that represents this option. */
	uint64_t flag;
	/*! \brief The index of the entry in the arguments array
	  that should be used for this option's argument. */
	unsigned int arg_index;
};

#define BEGIN_OPTIONS {
#define END_OPTIONS }

/*!
  \brief Declares an array of options for an application.
  \param holder The name of the array to be created
  \param options The actual options to be placed into the array
  \sa ast_app_parse_options

  This macro declares a 'static const' array of \c struct \c ast_option
  elements to hold the list of available options for an application.
  Each option must be declared using either the AST_APP_OPTION()
  or AST_APP_OPTION_ARG() macros.

  Example usage:
  \code
  enum my_app_option_flags {
        OPT_JUMP = (1 << 0),
        OPT_BLAH = (1 << 1),
        OPT_BLORT = (1 << 2),
  };

  enum my_app_option_args {
        OPT_ARG_BLAH = 0,
        OPT_ARG_BLORT,
        !! this entry tells how many possible arguments there are,
           and must be the last entry in the list
        OPT_ARG_ARRAY_SIZE,
  };

  AST_APP_OPTIONS(my_app_options, {
        AST_APP_OPTION('j', OPT_JUMP),
        AST_APP_OPTION_ARG('b', OPT_BLAH, OPT_ARG_BLAH),
        AST_APP_OPTION_BLORT('B', OPT_BLORT, OPT_ARG_BLORT),
  });

  static int my_app_exec(struct ast_channel *chan, void *data)
  {
  	char *options;
	struct ast_flags opts = { 0, };
	char *opt_args[OPT_ARG_ARRAY_SIZE];

  	... do any argument parsing here ...

	if (ast_app_parse_options(my_app_options, &opts, opt_args, options)) {
		return -1;
	}
  }
  \endcode
 */
#define AST_APP_OPTIONS(holder, options...) \
	static const struct ast_app_option holder[128] = options

/*!
  \brief Declares an application option that does not accept an argument.
  \param option The single character representing the option
  \param flagno The flag index to be set if this option is present
  \sa AST_APP_OPTIONS, ast_app_parse_options
 */
#define AST_APP_OPTION(option, flagno) \
	[option] = { .flag = flagno }

/*!
  \brief Declares an application option that accepts an argument.
  \param option The single character representing the option
  \param flagno The flag index to be set if this option is present
  \param argno The index into the argument array where the argument should
  be placed
  \sa AST_APP_OPTIONS, ast_app_parse_options
 */
#define AST_APP_OPTION_ARG(option, flagno, argno) \
	[option] = { .flag = flagno, .arg_index = argno + 1 }

/*!
  \brief Parses a string containing application options and sets flags/arguments.
  \param options The array of possible options declared with AST_APP_OPTIONS
  \param flags The flag structure to have option flags set
  \param args The array of argument pointers to hold arguments found
  \param optstr The string containing the options to be parsed
  \return zero for success, non-zero if an error occurs
  \sa AST_APP_OPTIONS
 */
int ast_app_parse_options(const struct ast_app_option *options, struct ast_flags *flags, char **args, char *optstr);

	/*!
  \brief Parses a string containing application options and sets flags/arguments.
  \param options The array of possible options declared with AST_APP_OPTIONS
  \param flags The 64-bit flag structure to have option flags set
  \param args The array of argument pointers to hold arguments found
  \param optstr The string containing the options to be parsed
  \return zero for success, non-zero if an error occurs
  \sa AST_APP_OPTIONS
 */
int ast_app_parse_options64(const struct ast_app_option *options, struct ast_flags64 *flags, char **args, char *optstr);

/*! \brief Given a list of options array, return an option string based on passed flags
	\param options The array of possible options declared with AST_APP_OPTIONS
	\param flags The flags of the options that you wish to populate the buffer with
	\param buf The buffer to fill with the string of options
	\param len The maximum length of buf
*/
void ast_app_options2str64(const struct ast_app_option *options, struct ast_flags64 *flags, char *buf, size_t len);

/*! \brief Present a dialtone and collect a certain length extension.
    \return Returns 1 on valid extension entered, -1 on hangup, or 0 on invalid extension.
\note Note that if 'collect' holds digits already, new digits will be appended, so be sure it's initialized properly */
int ast_app_dtget(struct ast_channel *chan, const char *context, char *collect, size_t size, int maxlen, int timeout);

/*! \brief Allow to record message and have a review option */
int ast_record_review(struct ast_channel *chan, const char *playfile, const char *recordfile, int maxtime, const char *fmt, int *duration, const char *path);

/*!
 * \brief Decode an encoded control or extended ASCII character
 * \param[in] stream String to decode
 * \param[out] result Decoded character
 * \param[out] consumed Number of characters used in stream to encode the character
 * \retval -1 Stream is of zero length
 * \retval 0 Success
 */
int ast_get_encoded_char(const char *stream, char *result, size_t *consumed);

/*!
 * \brief Decode a stream of encoded control or extended ASCII characters
 * \param[in] stream Encoded string
 * \param[out] result Decoded string
 * \param[in] result_len Maximum size of the result buffer
 * \return A pointer to the result string
 */
char *ast_get_encoded_str(const char *stream, char *result, size_t result_len);

/*! \brief Decode a stream of encoded control or extended ASCII characters */
int ast_str_get_encoded_str(struct ast_str **str, int maxlen, const char *stream);

/*!
 * \brief Common routine for child processes, to close all fds prior to exec(2)
 * \param[in] n starting file descriptor number for closing all higher file descriptors
 * \since 1.6.1
 */
void ast_close_fds_above_n(int n);

/*!
 * \brief Common routine to safely fork without a chance of a signal handler firing badly in the child
 * \param[in] stop_reaper flag to determine if sigchld handler is replaced or not
 * \since 1.6.1
 */
int ast_safe_fork(int stop_reaper);

/*!
 * \brief Common routine to cleanup after fork'ed process is complete (if reaping was stopped)
 * \since 1.6.1
 */
void ast_safe_fork_cleanup(void);

/*!
 * \brief Common routine to parse time lengths, with optional time unit specifier
 * \param[in] timestr String to parse
 * \param[in] defunit Default unit type
 * \param[out] result Resulting value, specified in milliseconds
 * \retval 0 Success
 * \retval -1 Failure
 * \since 1.8
 */
int ast_app_parse_timelen(const char *timestr, int *result, enum ast_timelen defunit);

/*!
 * \since 12
 * \brief Publish a MWI state update via stasis
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define ast_publish_mwi_state(mailbox, context, new_msgs, old_msgs) \
	ast_publish_mwi_state_full(mailbox, context, new_msgs, old_msgs, NULL, NULL)

/*!
 * \since 12
 * \brief Publish a MWI state update associated with some channel
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 * \param[in] channel_id A unique identifier for a channel associated with this
 * change in mailbox state
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define ast_publish_mwi_state_channel(mailbox, context, new_msgs, old_msgs, channel_id) \
	ast_publish_mwi_state_full(mailbox, context, new_msgs, old_msgs, channel_id, NULL)

/*!
 * \since 12
 * \brief Publish a MWI state update via stasis with all parameters
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] new_msgs The number of new messages in this mailbox
 * \param[in] old_msgs The number of old messages in this mailbox
 * \param[in] channel_id A unique identifier for a channel associated with this
 * change in mailbox state
 * \param[in] eid The EID of the server that originally published the message
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_publish_mwi_state_full(
	const char *mailbox,
	const char *context,
	int new_msgs,
	int old_msgs,
	const char *channel_id,
	struct ast_eid *eid);

/*!
 * \since 12.2.0
 * \brief Delete MWI state cached by stasis
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
#define ast_delete_mwi_state(mailbox, context) \
	ast_delete_mwi_state_full(mailbox, context, NULL)

/*!
 * \since 12.2.0
 * \brief Delete MWI state cached by stasis with all parameters
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 * \param[in] eid The EID of the server that originally published the message
 *
 * \retval 0 Success
 * \retval -1 Failure
 */
int ast_delete_mwi_state_full(const char *mailbox, const char *context, struct ast_eid *eid);

/*! \addtogroup StasisTopicsAndMessages
 * @{
 */

/*!
 * \brief The structure that contains MWI state
 * \since 12
 */
struct ast_mwi_state {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(uniqueid);  /*!< Unique identifier for this mailbox */
	);
	int new_msgs;                    /*!< The current number of new messages for this mailbox */
	int old_msgs;                    /*!< The current number of old messages for this mailbox */
	/*! If applicable, a snapshot of the channel that caused this MWI change */
	struct ast_channel_snapshot *snapshot;
	struct ast_eid eid;              /*!< The EID of the server where this message originated */
};

/*!
 * \brief Object that represents an MWI update with some additional application
 * defined data
 */
struct ast_mwi_blob {
	struct ast_mwi_state *mwi_state;    /*!< MWI state */
	struct ast_json *blob;              /*!< JSON blob of data */
};

/*!
 * \since 12
 * \brief Create a \ref ast_mwi_state object
 *
 * \param[in] mailbox The mailbox identifier string.
 * \param[in] context The context this mailbox resides in (NULL or "" if only using mailbox)
 *
 * \retval \ref ast_mwi_state object on success
 * \retval NULL on error
 */
struct ast_mwi_state *ast_mwi_create(const char *mailbox, const char *context);

/*!
 * \since 12
 * \brief Creates a \ref ast_mwi_blob message.
 *
 * The \a blob JSON object requires a \c "type" field describing the blob. It
 * should also be treated as immutable and not modified after it is put into the
 * message.
 *
 * \param mwi_state MWI state associated with the update
 * \param message_type The type of message to create
 * \param blob JSON object representing the data.
 * \return \ref ast_mwi_blob message.
 * \return \c NULL on error
 */
struct stasis_message *ast_mwi_blob_create(struct ast_mwi_state *mwi_state,
					   struct stasis_message_type *message_type,
					   struct ast_json *blob);

/*!
 * \brief Get the \ref stasis topic for MWI messages
 * \retval The topic structure for MWI messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_topic *ast_mwi_topic_all(void);

/*!
 * \brief Get the \ref stasis topic for MWI messages on a unique ID
 * \param uniqueid The unique id for which to get the topic
 * \retval The topic structure for MWI messages for a given uniqueid
 * \retval NULL if it failed to be found or allocated
 * \since 12
 */
struct stasis_topic *ast_mwi_topic(const char *uniqueid);

/*!
 * \brief Get the \ref stasis caching topic for MWI messages
 * \retval The caching topic structure for MWI messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_topic *ast_mwi_topic_cached(void);

/*!
 * \brief Backend cache for ast_mwi_topic_cached().
 * \retval Cache of \ref ast_mwi_state.
 */
struct stasis_cache *ast_mwi_state_cache(void);

/*!
 * \brief Get the \ref stasis message type for MWI messages
 * \retval The message type structure for MWI messages
 * \retval NULL on error
 * \since 12
 */
struct stasis_message_type *ast_mwi_state_type(void);

/*!
 * \brief Get the \ref stasis message type for voicemail application specific messages
 *
 * This message type exists for those messages a voicemail application may wish to send
 * that have no logical relationship with other voicemail applications. Voicemail apps
 * that use this message type must pass a \ref ast_mwi_blob. Any extraneous information
 * in the JSON blob must be packed as key/value pair tuples of strings.
 *
 * At least one key/value tuple must have a key value of "Event".
 *
 * \retval The \ref stasis_message_type for voicemail application specific messages
 * \retval NULL on error
 * \since 12
 */
struct stasis_message_type *ast_mwi_vm_app_type(void);

/*!
 * \brief Get the \ref stasis topic for queue messages
 * \retval The topic structure for queue messages
 * \retval NULL if it has not been allocated
 * \since 12
 */
struct stasis_topic *ast_queue_topic_all(void);

/*!
 * \brief Get the \ref stasis topic for queue messages for a particular queue name
 * \param queuename The name for which to get the topic
 * \retval The topic structure for queue messages for a given name
 * \retval NULL if it failed to be found or allocated
 * \since 12
 */
struct stasis_topic *ast_queue_topic(const char *queuename);
/*! @} */

/*!
 * \brief Initialize the application core
 * \retval 0 Success
 * \retval -1 Failure
 * \since 12
 */
int app_init(void);

#define AST_MAX_MAILBOX_UNIQUEID (AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2)
#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_APP_H */
