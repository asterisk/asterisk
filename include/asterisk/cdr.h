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

/*!
 * \file
 * \brief Call Detail Record API
 *
 * \author Mark Spencer <markster@digium.com>
 */

#ifndef _ASTERISK_CDR_H
#define _ASTERISK_CDR_H

#include "asterisk/channel.h"

/*! \file
 *
 * \since 12
 *
 * \brief Call Detail Record Engine.
 *
 * \page CDR Call Detail Record Engine
 *
 * \par Intro
 *
 * The Call Detail Record (CDR) engine uses the \ref stasis Stasis Message Bus
 * to build records for the channels in Asterisk. As the state of a channel and
 * the bridges it participates in changes, notifications are sent over the
 * Stasis Message Bus. The CDR engine consumes these notifications and builds
 * records that reflect that state. Over the lifetime of a channel, many CDRs
 * may be generated for that channel or that involve that channel.
 *
 * CDRs have a lifecycle that is a subset of the channel that they reflect. A
 * single CDR for a channel represents a path of communication between the
 * endpoint behind a channel and Asterisk, or between two endpoints. When a
 * channel establishes a new path of communication, a new CDR is created for the
 * channel. Likewise, when a path of communication is terminated, a CDR is
 * finalized. Finally, when a channel is no longer present in Asterisk, all CDRs
 * for that channel are dispatched for recording.
 *
 * Dispatching of CDRs occurs to registered CDR backends. CDR backends register
 * through \ref ast_cdr_register and are responsible for taking the produced
 * CDRs and storing them in permanent storage.
 *
 * \par CDR attributes
 *
 * While a CDR can have many attributes, all CDRs have two parties: a Party A
 * and a Party B. The Party A is \em always the channel that owns the CDR. A CDR
 * may or may not have a Party B, depending on its state.
 *
 * For the most part, attributes on a CDR are reflective of those same
 * attributes on the channel at the time when the CDR was finalized. Specific
 * CDR attributes include:
 * \li \c start The time when the CDR was created
 * \li \c answer The time when the Party A was answered, or when the path of
 * communication between Party A and Party B was established
 * \li \c end The time when the CDR was finalized
 * \li \c duration \c end - \c start. If \c end is not known, the current time
 * is used
 * \li \c billsec \c end - \c answer. If \c end is not known, the current time
 * is used
 * \li \c userfield User set data on some party in the CDR
 *
 * Note that \c accountcode and \c amaflags are actually properties of a
 * channel, not the CDR.
 *
 * \par CDR States
 *
 * CDRs go through various states during their lifetime. State transitions occur
 * due to messages received over the \ref stasis Stasis Message Bus. The
 * following describes the possible states a CDR can be in, and how it
 * transitions through the states.
 *
 * \par Single
 *
 * When a CDR is created, it is put into the Single state. The Single state
 * represents a CDR for a channel that has no Party B. CDRs can be unanswered
 * or answered while in the Single state.
 *
 * The following transitions can occur while in the Single state:
 * \li If a \ref ast_channel_dial_type indicating a Dial Begin is received, the
 * state transitions to Dial
 * \li If a \ref ast_channel_snapshot is received indicating that the channel
 * has hung up, the state transitions to Finalized
 * \li If a \ref ast_bridge_blob_type is received indicating a Bridge Enter, the
 * state transitions to Bridge
 * \li If a \ref ast_bridge_blob_type message indicating an entrance to a
 * holding bridge with a subclass type of "parking" is received, the CDR is
 * transitioned to the Parked state.
 *
 * \par Dial
 *
 * This state represents a dial that is occurring within Asterisk. The Party A
 * can either be the caller for a two party dial, or it can be the dialed party
 * if the calling party is Asterisk (that is, an Originated channel). In the
 * first case, the Party B is \em always the dialed channel; in the second case,
 * the channel is not considered to be a "dialed" channel as it is alone in the
 * dialed operation.
 *
 * While in the Dial state, multiple CDRs can be created for the Party A if a
 * parallel dial occurs. Each dialed party receives its own CDR with Party A.
 *
 * The following transitions can occur while in the Dial state:
 * \li If a \ref ast_channel_dial_type indicating a Dial End is received where
 * the \ref dial_status is not ANSWER, the state transitions to Finalized
 * \li If a \ref ast_channel_snapshot is received indicating that the channel
 * has hung up, the state transitions to Finalized
 * \li If a \ref ast_channel_dial_type indicating a Dial End is received where
 * the \ref dial_status is ANSWER, the state transitions to DialedPending
 * \li If a \ref ast_bridge_blob_type is received indicating a Bridge Enter, the
 * state transitions to Bridge
 *
 * \par DialedPending
 *
 * Technically, after being dialed, a CDR does not have to transition to the
 * Bridge state. If the channel being dialed was originated, the channel may
 * being executing dialplan. Strangely enough, it is also valid to have both
 * Party A and Party B - after a dial - to not be bridged and instead execute
 * dialplan. DialedPending handles the state where we figure out if the CDR
 * showing the dial needs to move to the Bridge state; if the CDR should show
 * that we started executing dialplan; of if we need a new CDR.
 *
 * The following transition can occur while in the DialedPending state:
 * \li If a \ref ast_channel_snapshot is received that indicates that the
 * channel has begun executing dialplan, we transition to the Finalized state
 * if we have a Party B. Otherwise, we transition to the Single state.
 * \li If a \ref ast_bridge_blob_type is received indicating a Bridge Enter, the
 * state transitions to Bridge (through the Dial state)
 * \li If a \ref ast_bridge_blob_type message indicating an entrance to a
 * holding bridge with a subclass type of "parking" is received, the CDR is
 * transitioned to the Parked state.
 *
 * \par Bridge
 *
 * The Bridge state represents a path of communication between Party A and one
 * or more other parties. When a CDR enters into the Bridge state, the following
 * occurs:
 * \li The CDR attempts to find a Party B. If the CDR has a Party B, it looks
 * for that channel in the bridge and updates itself accordingly. If the CDR
 * does not yet have a Party B, it attempts to find a channel that can be its
 * Party B. If it finds one, it updates itself; otherwise, the CDR is
 * temporarily finalized.
 * \li Once the CDR has a Party B or it is determined that it cannot have a
 * Party B, new CDRs are created for each pairing of channels with the CDR's
 * Party A.
 *
 * As an example, consider the following:
 * \li A Dials B - both answer
 * \li B joins a bridge. Since no one is in the bridge and it was a dialed
 * channel, it cannot have a Party B.
 * \li A joins the bridge. Since A's Party B is B, A updates itself with B.
 * \li Now say an Originated channel, C, joins the bridge. The bridge becomes
 * a multi-party bridge.
 * \li C attempts to get a Party B. A cannot be C's Party B, as it was created
 * before it. B is a dialed channel and can thus be C's Party B, so C's CDR
 * updates its Party B to B.
 * \li New CDRs are now generated. A gets a new CDR for A -> C. B is dialed, and
 * hence cannot get any CDR.
 * \li Now say another Originated channel, D, joins the bridge. Say D has the
 * \ref party_a flag set on it, such that it is always the preferred Party A.
 * As such, it takes A as its Party B.
 * \li New CDRs are generated. D gets new CDRs for D -> B and D -> C.
 *
 * The following transitions can occur while in the Bridge state:
 * \li If a \ref ast_bridge_blob_type message indicating a leave is received,
 * the state transitions to the Finalized state.
 *
 * \par Parked
 *
 * Parking is technically just another bridge in the Asterisk bridging
 * framework. Unlike other bridges, however there are several key distinctions:
 * \li With normal bridges, you want to show paths of communication between
 * the participants. In parking, however, each participant is independent.
 * From the perspective of a CDR, a call in parking should look like a dialplan
 * application just executed.
 * \li Holding bridges are typically items using in more complex applications,
 * and so we usually don't want to show them. However, with Park, there is no
 * application execution - often, a channel will be put directly into the
 * holding bridge, bypassing the dialplan. This occurs when a call is blind
 * transferred to a parking extension.
 *
 * As such, if a channel enters a bridge and that happens to be a holding bridge
 * with a subclass type of "parking", we transition the CDR into the Parked
 * state. The parking Stasis message updates the application name and data to
 * reflect that the channel is in parking. When this occurs, a special flag is
 * set on the CDR that prevents the application name from being updates by
 * subsequent channel snapshot updates.
 *
 * The following transitions can occur while in the Parked state:
 * \li If a \ref ast_bridge_blob_type message indicating a leave is received,
 * the state transitions to the Finalized state
 *
 * \par Finalized
 *
 * Once a CDR enters the finalized state, it is finished. No further updates
 * can be made to the party information, and the CDR cannot be changed.
 *
 * One exception to this occurs during linkedid propagation, in which the CDRs
 * linkedids are updated based on who the channel is bridged with. In general,
 * however, a finalized CDR is waiting for dispatch to the CDR backends.
 */

/*! \brief CDR engine settings */
enum ast_cdr_settings {
	CDR_ENABLED = 1 << 0,               /*!< Enable CDRs */
	CDR_BATCHMODE = 1 << 1,             /*!< Whether or not we should dispatch CDRs in batches */
	CDR_UNANSWERED = 1 << 2,            /*!< Log unanswered CDRs */
	CDR_CONGESTION = 1 << 3,            /*!< Treat congestion as if it were a failed call */
	CDR_END_BEFORE_H_EXTEN = 1 << 4,    /*!< End the CDR before the 'h' extension runs */
	CDR_INITIATED_SECONDS = 1 << 5,     /*!< Include microseconds into the billing time */
	CDR_DEBUG = 1 << 6,                 /*!< Enables extra debug statements */
};

/*! \brief CDR Batch Mode settings */
enum ast_cdr_batch_mode_settings {
	BATCH_MODE_SCHEDULER_ONLY = 1 << 0, /*!< Don't spawn a thread to handle the batches - do it on the scheduler */
	BATCH_MODE_SAFE_SHUTDOWN = 1 << 1,  /*!< During safe shutdown, submit the batched CDRs */
};

/*!
 * \brief CDR manipulation options. Certain function calls will manipulate the
 * state of a CDR object based on these flags.
 */
enum ast_cdr_options {
	AST_CDR_FLAG_KEEP_VARS = (1 << 0),   /*!< Copy variables during the operation */
	AST_CDR_FLAG_DISABLE = (1 << 1),     /*!< Disable the current CDR */
	AST_CDR_FLAG_DISABLE_ALL = (3 << 1), /*!< Disable the CDR and all future CDRs */
	AST_CDR_FLAG_PARTY_A = (1 << 3),     /*!< Set the channel as party A */
	AST_CDR_FLAG_FINALIZE = (1 << 4),    /*!< Finalize the current CDRs */
	AST_CDR_FLAG_SET_ANSWER = (1 << 5),  /*!< If the channel is answered, set the answer time to now */
	AST_CDR_FLAG_RESET = (1 << 6),       /*!< If set, set the start and answer time to now */
	AST_CDR_LOCK_APP = (1 << 7),         /*!< Prevent any further changes to the application field/data field for this CDR */
};

/*!
 * \brief CDR Flags - Disposition
 */
enum ast_cdr_disposition {
	AST_CDR_NOANSWER   = 0,
	AST_CDR_NULL       = (1 << 0),
	AST_CDR_FAILED     = (1 << 1),
	AST_CDR_BUSY       = (1 << 2),
	AST_CDR_ANSWERED   = (1 << 3),
	AST_CDR_CONGESTION = (1 << 4),
};


/*! \brief The global options available for CDRs */
struct ast_cdr_config {
	struct ast_flags settings;			/*!< CDR settings */
	struct batch_settings {
		unsigned int time;				/*!< Time between batches */
		unsigned int size;				/*!< Size to trigger a batch */
		struct ast_flags settings;		/*!< Settings for batches */
	} batch_settings;
};

/*!
 * \brief Responsible for call detail data
 */
struct ast_cdr {
	/*! Caller*ID with text */
	char clid[AST_MAX_EXTENSION];
	/*! Caller*ID number */
	char src[AST_MAX_EXTENSION];
	/*! Destination extension */
	char dst[AST_MAX_EXTENSION];
	/*! Destination context */
	char dcontext[AST_MAX_EXTENSION];

	char channel[AST_MAX_EXTENSION];
	/*! Destination channel if appropriate */
	char dstchannel[AST_MAX_EXTENSION];
	/*! Last application if appropriate */
	char lastapp[AST_MAX_EXTENSION];
	/*! Last application data */
	char lastdata[AST_MAX_EXTENSION];

	struct timeval start;

	struct timeval answer;

	struct timeval end;
	/*! Total time in system, in seconds */
	long int duration;
	/*! Total time call is up, in seconds */
	long int billsec;
	/*! What happened to the call */
	long int disposition;
	/*! What flags to use */
	long int amaflags;
	/*! What account number to use */
	char accountcode[AST_MAX_ACCOUNT_CODE];
	/*! Account number of the last person we talked to */
	char peeraccount[AST_MAX_ACCOUNT_CODE];
	/*! flags */
	unsigned int flags;
	/*! Unique Channel Identifier */
	char uniqueid[AST_MAX_UNIQUEID];
	/*! Linked group Identifier */
	char linkedid[AST_MAX_UNIQUEID];
	/*! User field */
	char userfield[AST_MAX_USER_FIELD];
	/*! Sequence field */
	int sequence;

	/*! A linked list for variables */
	struct varshead varshead;

	struct ast_cdr *next;
};

/*!
 * \since 12
 * \brief Obtain the current CDR configuration
 *
 * The configuration is a ref counted object. The caller of this function must
 * decrement the ref count when finished with the configuration.
 *
 * \retval NULL on error
 * \retval The current CDR configuration
 */
struct ast_cdr_config *ast_cdr_get_config(void);

/*!
 * \since 12
 * \brief Set the current CDR configuration
 *
 * \param config The new CDR configuration
 */
void ast_cdr_set_config(struct ast_cdr_config *config);

/*!
 * \since 12
 * \brief Format a CDR variable from an already posted CDR
 *
 * \param cdr The dispatched CDR to process
 * \param name The name of the variable
 * \param ret Pointer to the formatted buffer
 * \param workspace A pointer to the buffer to use to format the variable
 * \param workspacelen The size of \ref workspace
 * \param raw If non-zero and a date/time is extraced, provide epoch seconds. Otherwise format as a date/time stamp
 */
void ast_cdr_format_var(struct ast_cdr *cdr, const char *name, char **ret, char *workspace, int workspacelen, int raw);

/*!
 * \since 12
 * \brief Retrieve a CDR variable from a channel's current CDR
 *
 * \param channel_name The name of the party A channel that the CDR is associated with
 * \param name The name of the variable to retrieve
 * \param value Buffer to hold the value
 * \param length The size of the buffer
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int ast_cdr_getvar(const char *channel_name, const char *name, char *value, size_t length);

/*!
 * \since 12
 * \brief Set a variable on a CDR
 *
 * \param channel_name The channel to set the variable on
 * \param name The name of the variable to set
 * \param value The value of the variable to set
 *
 * \retval 0 on success
 * \retval non-zero on failure
 */
int ast_cdr_setvar(const char *channel_name, const char *name, const char *value);

/*!
 * \since 12
 * \brief Fork a CDR
 *
 * \param channel_name The name of the channel whose CDR should be forked
 * \param options Options to control how the fork occurs.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_cdr_fork(const char *channel_name, struct ast_flags *options);

/*!
 * \since 12
 * \brief Set a property on a CDR for a channel
 *
 * This function sets specific administrative properties on a CDR for a channel.
 * This includes properties like preventing a CDR from being dispatched, to
 * setting the channel as the preferred Party A in future CDRs. See
 * \ref enum ast_cdr_options for more information.
 *
 * \param channel_name The CDR's channel
 * \param option Option to apply to the CDR
 *
 * \retval 0 on success
 * \retval 1 on error
 */
int ast_cdr_set_property(const char *channel_name, enum ast_cdr_options option);

/*!
 * \since 12
 * \brief Clear a property on a CDR for a channel
 *
 * Clears a flag previously set by \ref ast_cdr_set_property
 *
 * \param channel_name The CDR's channel
 * \param option Option to clear from the CDR
 *
 * \retval 0 on success
 * \retval 1 on error
 */
int ast_cdr_clear_property(const char *channel_name, enum ast_cdr_options option);

/*!
 * \brief Reset the detail record
 * \param channel_name The channel that the CDR is associated with
 * \param keep_variables Keep the variables during the reset. If zero,
 *        variables are discarded during the reset.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int ast_cdr_reset(const char *channel_name, int keep_variables);

/*!
 * \brief Serializes all the data and variables for a current CDR record
 * \param channel_name The channel to get the CDR for
 * \param buf A buffer to use for formatting the data
 * \param delim A delimeter to use to separate variable keys/values
 * \param sep A separator to use between nestings
 * \retval the total number of serialized variables
 */
int ast_cdr_serialize_variables(const char *channel_name, struct ast_str **buf, char delim, char sep);

/*!
 * \brief CDR backend callback
 * \warning CDR backends should NOT attempt to access the channel associated
 * with a CDR record.  This channel is not guaranteed to exist when the CDR
 * backend is invoked.
 */
typedef int (*ast_cdrbe)(struct ast_cdr *cdr);

/*! \brief Return TRUE if CDR subsystem is enabled */
int ast_cdr_is_enabled(void);

/*!
 * \brief Allocate a CDR record
 * \retval a malloc'd ast_cdr structure
 * \retval NULL on error (malloc failure)
 */
struct ast_cdr *ast_cdr_alloc(void);

struct stasis_message_router;

/*!
 * \brief Return the message router for the CDR engine
 *
 * This returns the \ref stasis_message_router that the CDR engine
 * uses for dispatching \ref stasis messages. The reference on the
 * message router is bumped and must be released by the caller of
 * this function.
 *
 * \retval NULL if the CDR engine is disabled or unavailable
 * \retval the \ref stasis_message_router otherwise
 */
struct stasis_message_router *ast_cdr_message_router(void);

/*!
 * \brief Duplicate a public CDR
 * \param cdr the record to duplicate
 *
 * \retval a malloc'd ast_cdr structure,
 * \retval NULL on error (malloc failure)
 */
struct ast_cdr *ast_cdr_dup(struct ast_cdr *cdr);

/*!
 * \brief Free a CDR record
 * \param cdr ast_cdr structure to free
 * Returns nothing
 */
void ast_cdr_free(struct ast_cdr *cdr);

/*!
 * \brief Register a CDR handling engine
 * \param name name associated with the particular CDR handler
 * \param desc description of the CDR handler
 * \param be function pointer to a CDR handler
 * Used to register a Call Detail Record handler.
 * \retval 0 on success.
 * \retval -1 on error
 */
int ast_cdr_register(const char *name, const char *desc, ast_cdrbe be);

/*!
 * \brief Unregister a CDR handling engine
 * \param name name of CDR handler to unregister
 * Unregisters a CDR by it's name
 *
 * \retval 0 The backend unregistered successfully
 * \retval -1 The backend could not be unregistered at this time
 */
int ast_cdr_unregister(const char *name);

/*!
 * \brief Suspend a CDR backend temporarily
 *
  * \retval 0 The backend is suspdended
  * \retval -1 The backend could not be suspended
  */
int ast_cdr_backend_suspend(const char *name);

/*!
 * \brief Unsuspend a CDR backend
 *
 * \retval 0 The backend was unsuspended
 * \retval -1 The back could not be unsuspended
 */
int ast_cdr_backend_unsuspend(const char *name);

/*!
 * \brief Register a CDR modifier
 * \param name name associated with the particular CDR modifier
 * \param desc description of the CDR modifier
 * \param be function pointer to a CDR modifier
 *
 * Used to register a Call Detail Record modifier.
 *
 * This gives modules a chance to modify CDR fields before they are dispatched
 * to registered backends (odbc, syslog, etc).
 *
 * \note The *modified* CDR will be passed to **all** registered backends for
 * logging. For instance, if cdr_manager changes the CDR data, cdr_adaptive_odbc
 * will also get the modified CDR.
 *
 * \retval 0 on success.
 * \retval -1 on error
 */
int ast_cdr_modifier_register(const char *name, const char *desc, ast_cdrbe be);

/*!
 * \brief Unregister a CDR modifier
 * \param name name of CDR modifier to unregister
 * Unregisters a CDR modifier by its name
 *
 * \retval 0 The modifier unregistered successfully
 * \retval -1 The modifier could not be unregistered at this time
 */
int ast_cdr_modifier_unregister(const char *name);

/*!
 * \brief Disposition to a string
 * \param disposition input binary form
 * Converts the binary form of a disposition to string form.
 * \return a pointer to the string form
 */
const char *ast_cdr_disp2str(int disposition);

/*!
 * \brief Set CDR user field for channel (stored in CDR)
 *
 * \param channel_name The name of the channel that owns the CDR
 * \param userfield The user field to set
 */
void ast_cdr_setuserfield(const char *channel_name, const char *userfield);

/*! Submit any remaining CDRs and prepare for shutdown */
void ast_cdr_engine_term(void);

#endif /* _ASTERISK_CDR_H */
