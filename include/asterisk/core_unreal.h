/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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
 * \brief Unreal channel derivative framework.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

#ifndef _ASTERISK_CORE_UNREAL_H
#define _ASTERISK_CORE_UNREAL_H

#include "asterisk/astobj2.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/abstract_jb.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Forward declare some struct names */
struct ast_format_cap;

/* ------------------------------------------------------------------- */

struct ast_unreal_pvt;

enum ast_unreal_channel_indicator {
	AST_UNREAL_OWNER,
	AST_UNREAL_CHAN,
};

/*!
 * \brief Callbacks that can be provided by concrete implementations of the unreal
 * channel driver that will be called when events occur in the unreal layer
 */
struct ast_unreal_pvt_callbacks {
	/*!
	 * \brief Called when an optimization attempt has started
	 * \note p is locked when this callback is called
	 * \param p The \ref ast_unreal_pvt object
	 * \param source The channel that is optimizing into an unreal_pvt channel's bridge.
	 * If NULL, the optimization is being accomplished via a bridge merge.
	 * \param dest Indicator of which channel's bridge in the unreal_pvt will survive the
	 * optimization
	 * \param id Unique identifier for this optimization operation.
	 */
	void (* const optimization_started)(struct ast_unreal_pvt *p, struct ast_channel *source,
			enum ast_unreal_channel_indicator dest, unsigned int id);

	/*!
	 * \brief Called when an optimization attempt completed successfully
	 * \note p is locked when this callback is called
	 * \param p The \ref ast_unreal_pvt object
	 * \param success Non-zero if the optimization succeeded, zero if the optimization
	 * met with fatal and permanent error
	 * \param id Unique identifier for this optimization. Same as the one from the optimization_started
	 * call
	 */
	void (* const optimization_finished)(struct ast_unreal_pvt *p, int success, unsigned int id);
};

/*!
 * \brief The base pvt structure for local channel derivatives.
 *
 * The unreal pvt has two ast_chan objects - the "owner" and the "next channel", the outbound channel
 *
 * ast_chan owner -> ast_unreal_pvt -> ast_chan chan
 */
struct ast_unreal_pvt {
	struct ast_unreal_pvt_callbacks *callbacks; /*!< Event callbacks */
	struct ast_channel *owner;                  /*!< Master Channel - ;1 side */
	struct ast_channel *chan;                   /*!< Outbound channel - ;2 side */
	struct ast_format_cap *reqcap;              /*!< Requested format capabilities */
	struct ast_jb_conf jb_conf;                 /*!< jitterbuffer configuration */
	unsigned int flags;                         /*!< Private option flags */
	/*! Base name of the unreal channels.  exten@context or other name. */
	char name[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2];
};

#define AST_UNREAL_IS_OUTBOUND(a, b) ((a) == (b)->chan ? 1 : 0)

#define AST_UNREAL_CARETAKER_THREAD (1 << 0) /*!< The ;2 side launched a PBX, was pushed into a bridge, or was masqueraded into an application. */
#define AST_UNREAL_NO_OPTIMIZATION  (1 << 1) /*!< Do not optimize out the unreal channels */
#define AST_UNREAL_MOH_INTERCEPT    (1 << 2) /*!< Intercept and act on hold/unhold control frames */
#define AST_UNREAL_OPTIMIZE_BEGUN   (1 << 3) /*!< Indicates that an optimization attempt has been started */

/*!
 * \brief Send an unreal pvt in with no locks held and get all locks
 *
 * \note NO locks should be held prior to calling this function
 * \note The pvt must have a ref held before calling this function
 * \note if outchan or outowner is set != NULL after calling this function
 *       those channels are locked and reffed.
 * \note Batman.
 */
void ast_unreal_lock_all(struct ast_unreal_pvt *p, struct ast_channel **outchan, struct ast_channel **outowner);

/*!
 * \brief Hangup one end (maybe both ends) of an unreal channel derivative.
 * \since 12.0.0
 *
 * \param p Private channel struct (reffed)
 * \param ast Channel being hung up.  (locked)
 *
 * \note Common hangup code for unreal channels.  Derived
 * channels will need to deal with any additional resources.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_unreal_hangup(struct ast_unreal_pvt *p, struct ast_channel *ast);

/*! Unreal channel framework struct ast_channel_tech.send_digit_begin callback */
int ast_unreal_digit_begin(struct ast_channel *ast, char digit);

/*! Unreal channel framework struct ast_channel_tech.send_digit_end callback */
int ast_unreal_digit_end(struct ast_channel *ast, char digit, unsigned int duration);

/*! Unreal channel framework struct ast_channel_tech.answer callback */
int ast_unreal_answer(struct ast_channel *ast);

/*! Unreal channel framework struct ast_channel_tech.read and struct ast_channel_tech.exception callback */
struct ast_frame *ast_unreal_read(struct ast_channel *ast);

/*! Unreal channel framework struct ast_channel_tech.write callback */
int ast_unreal_write(struct ast_channel *ast, struct ast_frame *f);

/*! Unreal channel framework struct ast_channel_tech.indicate callback */
int ast_unreal_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);

/*! Unreal channel framework struct ast_channel_tech.fixup callback */
int ast_unreal_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

/*! Unreal channel framework struct ast_channel_tech.send_html callback */
int ast_unreal_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen);

/*! Unreal channel framework struct ast_channel_tech.send_text callback */
int ast_unreal_sendtext(struct ast_channel *ast, const char *text);

/*! Unreal channel framework struct ast_channel_tech.queryoption callback */
int ast_unreal_queryoption(struct ast_channel *ast, int option, void *data, int *datalen);

/*! Unreal channel framework struct ast_channel_tech.setoption callback */
int ast_unreal_setoption(struct ast_channel *chan, int option, void *data, int datalen);

/*!
 * \brief struct ast_unreal_pvt destructor.
 * \since 12.0.0
 *
 * \param vdoomed Object to destroy.
 *
 * \return Nothing
 */
void ast_unreal_destructor(void *vdoomed);

/*!
 * \brief Allocate the base unreal struct for a derivative.
 * \since 12.0.0
 *
 * \param size Size of the unreal struct to allocate.
 * \param destructor Destructor callback.
 * \param cap Format capabilities to give the unreal private struct.
 *
 * \retval pvt on success.
 * \retval NULL on error.
 */
struct ast_unreal_pvt *ast_unreal_alloc(size_t size, ao2_destructor_fn destructor, struct ast_format_cap *cap);

/*!
 * \brief Create the semi1 and semi2 unreal channels.
 * \since 12.0.0
 *
 * \param p Unreal channel private struct.
 * \param tech Channel technology to use.
 * \param semi1_state State to start the semi1(owner) channel in.
 * \param semi2_state State to start the semi2(outgoing chan) channel in.
 * \param exten Exten to start the chennels in. (NULL if s)
 * \param context Context to start the channels in. (NULL if default)
 * \param requestor Channel requesting creation. (NULL if none)
 * \param callid Thread callid to use.
 *
 * \retval semi1_channel on success.
 * \retval NULL on error.
 */
struct ast_channel *ast_unreal_new_channels(struct ast_unreal_pvt *p,
	const struct ast_channel_tech *tech, int semi1_state, int semi2_state,
	const char *exten, const char *context, const struct ast_assigned_ids *assignedids, 
	const struct ast_channel *requestor, ast_callid callid);

/*!
 * \brief Setup unreal owner and chan channels before initiating call.
 * \since 12.0.0
 *
 * \param semi1 Owner channel of unreal channel pair.
 * \param semi2 Outgoing channel of unreal channel pair.
 *
 * \note On entry, the semi1 and semi2 channels are already locked.
 *
 * \return Nothing
 */
void ast_unreal_call_setup(struct ast_channel *semi1, struct ast_channel *semi2);

/*!
 * \brief Push the semi2 unreal channel into a bridge from either member of the unreal pair
 * \since 12.0.0
 *
 * \param ast A member of the unreal channel being pushed
 * \param bridge Which bridge we want to push the channel to
 * \param flags Feature flags to be set on the bridge channel.
 *
 * \retval 0 if the channel is successfully imparted onto the bridge
 * \retval -1 on failure
 *
 * \note This is equivalent to ast_call() on unreal based channel drivers that are designed to use it instead.
 */
int ast_unreal_channel_push_to_bridge(struct ast_channel *ast, struct ast_bridge *bridge, unsigned int flags);

/* ------------------------------------------------------------------- */

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_CORE_UNREAL_H */
