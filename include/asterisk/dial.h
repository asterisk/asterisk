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
 * \brief Dialing API
 */

#ifndef _ASTERISK_DIAL_H
#define _ASTERISK_DIAL_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief Main dialing structure. Contains global options, channels being dialed, and more! */
struct ast_dial;

/*! \brief Dialing channel structure. Contains per-channel dialing options, asterisk channel, and more! */
struct ast_dial_channel;

/*! \brief Forward declaration for format capabilities, used in prerun */
struct ast_format_cap;

typedef void (*ast_dial_state_callback)(struct ast_dial *);

/*! \brief List of options that are applicable either globally or per dialed channel */
enum ast_dial_option {
	AST_DIAL_OPTION_RINGING,                 /*!< Always indicate ringing to caller */
	AST_DIAL_OPTION_ANSWER_EXEC,             /*!< Execute application upon answer in async mode */
	AST_DIAL_OPTION_MUSIC,                   /*!< Play music on hold instead of ringing to the calling channel */
	AST_DIAL_OPTION_DISABLE_CALL_FORWARDING, /*!< Disable call forwarding on channels */
	AST_DIAL_OPTION_PREDIAL,                 /*!< Execute a predial subroutine before dialing */
	AST_DIAL_OPTION_DIAL_REPLACES_SELF,      /*!< The dial operation is a replacement for the requester */
	AST_DIAL_OPTION_SELF_DESTROY,            /*!< Destroy self at end of ast_dial_run */
	AST_DIAL_OPTION_MAX,                     /*!< End terminator -- must always remain last */
};

/*! \brief List of return codes for dial run API calls */
enum ast_dial_result {
	AST_DIAL_RESULT_INVALID,     /*!< Invalid options were passed to run function */
	AST_DIAL_RESULT_FAILED,      /*!< Attempts to dial failed before reaching critical state */
	AST_DIAL_RESULT_TRYING,      /*!< Currently trying to dial */
	AST_DIAL_RESULT_RINGING,     /*!< Dial is presently ringing */
	AST_DIAL_RESULT_PROGRESS,    /*!< Dial is presently progressing */
	AST_DIAL_RESULT_PROCEEDING,  /*!< Dial is presently proceeding */
	AST_DIAL_RESULT_ANSWERED,    /*!< A channel was answered */
	AST_DIAL_RESULT_TIMEOUT,     /*!< Timeout was tripped, nobody answered */
	AST_DIAL_RESULT_HANGUP,      /*!< Caller hung up */
	AST_DIAL_RESULT_UNANSWERED,  /*!< Nobody answered */
};

/*! \brief New dialing structure
 * \note Create a dialing structure
 * \return Returns a calloc'd ast_dial structure, NULL on failure
 */
struct ast_dial *ast_dial_create(void);

/*! \brief Append a channel
 * \note Appends a channel to a dialing structure
 * \return Returns channel reference number on success, -1 on failure
 */
int ast_dial_append(struct ast_dial *dial, const char *tech, const char *device, const struct ast_assigned_ids *assignedids);

/*!
 * \brief Append a channel using an actual channel object
 *
 * \param dial The ast_dial to add the channel to
 * \param chan The channel to add to the dial
 * \retval -1 Failure
 * \retval non-zero The position of the channel in the list of dialed channels
 *
 * \note The chan ref is stolen with a successful return.
 */
int ast_dial_append_channel(struct ast_dial *dial, struct ast_channel *chan);

/*! \brief Request all appended channels, but do not dial
 * \param dial Dialing structure
 * \param chan Optional dialing channel
 * \param cap Optional requested capabilities
 * \retval -1 failure
 * \reval 0 success
 */
int ast_dial_prerun(struct ast_dial *dial, struct ast_channel *chan, struct ast_format_cap *cap);

/*! \brief Execute dialing synchronously or asynchronously
 * \note Dials channels in a dial structure.
 * \return Returns dial result code. (TRYING/INVALID/FAILED/ANSWERED/TIMEOUT/UNANSWERED).
 */
enum ast_dial_result ast_dial_run(struct ast_dial *dial, struct ast_channel *chan, int async);

/*! \brief Return channel that answered
 * \note Returns the Asterisk channel that answered
 * \param dial Dialing structure
 */
struct ast_channel *ast_dial_answered(struct ast_dial *dial);

/*! \brief Steal the channel that answered
 * \note Returns the Asterisk channel that answered and removes it from the dialing structure
 * \param dial Dialing structure
 */
struct ast_channel *ast_dial_answered_steal(struct ast_dial *dial);

/*! \brief Return state of dial
 * \note Returns the state of the dial attempt
 * \param dial Dialing structure
 */
enum ast_dial_result ast_dial_state(struct ast_dial *dial);

/*! \brief Cancel async thread
 * \note Cancel a running async thread
 * \param dial Dialing structure
 */
enum ast_dial_result ast_dial_join(struct ast_dial *dial);

/*! \brief Hangup channels
 * \note Hangup all active channels
 * \param dial Dialing structure
 */
void ast_dial_hangup(struct ast_dial *dial);

/*! \brief Destroys a dialing structure
 * \note Cancels dialing and destroys (free's) the given ast_dial structure
 * \param dial Dialing structure to free
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_destroy(struct ast_dial *dial);

/*! \brief Enables an option globally
 * \param dial Dial structure to enable option on
 * \param option Option to enable
 * \param data Data to pass to this option (not always needed)
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_option_global_enable(struct ast_dial *dial, enum ast_dial_option option, void *data);

/*! \brief Enables an option per channel
 * \param dial Dial structure
 * \param num Channel number to enable option on
 * \param option Option to enable
 * \param data Data to pass to this option (not always needed)
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_option_enable(struct ast_dial *dial, int num, enum ast_dial_option option, void *data);

/*! \brief Disables an option globally
 * \param dial Dial structure to disable option on
 * \param option Option to disable
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_option_global_disable(struct ast_dial *dial, enum ast_dial_option option);

/*! \brief Disables an option per channel
 * \param dial Dial structure
 * \param num Channel number to disable option on
 * \param option Option to disable
 * \return Returns 0 on success, -1 on failure
 */
int ast_dial_option_disable(struct ast_dial *dial, int num, enum ast_dial_option option);

/*! \brief Get the reason an outgoing channel has failed
 * \param dial Dial structure
 * \param num Channel number to get the reason from
 * \return Numerical cause code
 */
int ast_dial_reason(struct ast_dial *dial, int num);

/*! \brief Get the dialing channel, if prerun has been executed
 * \param dial Dial structure
 * \param num Channel number to get channel of
 * \return Pointer to channel, without reference
 */
struct ast_channel *ast_dial_get_channel(struct ast_dial *dial, int num);

/*! \brief Set a callback for state changes
 * \param dial The dial structure to watch for state changes
 * \param callback the callback
 * \return nothing
 */
void ast_dial_set_state_callback(struct ast_dial *dial, ast_dial_state_callback callback);

/*! \brief Set user data on a dial structure
 * \param dial The dial structure to set a user data pointer on
 * \param user_data The user data pointer
 * \return nothing
 */
void ast_dial_set_user_data(struct ast_dial *dial, void *user_data);

/*! \brief Return the user data on a dial structure
 * \param dial The dial structure
 * \return A pointer to the user data
 */
void *ast_dial_get_user_data(struct ast_dial *dial);

/*! \brief Set the maximum time (globally) allowed for trying to ring phones
 * \param dial The dial structure to apply the time limit to
 * \param timeout Maximum time allowed in milliseconds
 * \return nothing
 */
void ast_dial_set_global_timeout(struct ast_dial *dial, int timeout);

/*! \brief Set the maximum time (per channel) allowed for trying to ring the phone
 * \param dial The dial structure the channel belongs to
 * \param num Channel number to set timeout on
 * \param timeout Maximum time allowed in milliseconds
 * \return nothing
 */
void ast_dial_set_timeout(struct ast_dial *dial, int num, int timeout);

/*! \since 12
 * \brief Convert a hangup cause to a publishable dial status
 */
const char *ast_hangup_cause_to_dial_status(int hangup_cause);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DIAL_H */
