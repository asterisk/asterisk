/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2009, 2013 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Bridging API
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 * \author Joshua Colp <jcolp@digium.com>
 * \ref AstBridging
 *
 * See Also:
 * \arg \ref AstCREDITS
 */

/*!
 * \page AstBridging Bridging API
 *
 * The purpose of this API is to provide an easy and flexible way to bridge
 * channels of different technologies with different features.
 *
 * Bridging technologies provide the mechanism that do the actual handling
 * of frames between channels. They provide capability information, codec information,
 * and preference value to assist the bridging core in choosing a bridging technology when
 * creating a bridge. Different bridges may use different bridging technologies based on needs
 * but once chosen they all operate under the same premise; they receive frames and send frames.
 *
 * Bridges are a combination of bridging technology, channels, and features. A
 * developer creates a new bridge based on what they are currently expecting to do
 * with it or what they will do with it in the future. The bridging core determines what
 * available bridging technology will best fit the requirements and creates a new bridge.
 * Once created, channels can be added to the bridge in a blocking or non-blocking fashion.
 *
 * Features are such things as channel muting or DTMF based features such as attended transfer,
 * blind transfer, and hangup. Feature information must be set at the most granular level, on
 * the channel. While you can use features on a global scope the presence of a feature structure
 * on the channel will override the global scope. An example would be having the bridge muted
 * at global scope and attended transfer enabled on a channel. Since the channel itself is not muted
 * it would be able to speak.
 *
 * Feature hooks allow a developer to tell the bridging core that when a DTMF string
 * is received from a channel a callback should be called in their application. For
 * example, a conference bridge application may want to provide an IVR to control various
 * settings on the conference bridge. This can be accomplished by attaching a feature hook
 * that calls an IVR function when a DTMF string is entered.
 *
 */

#ifndef _ASTERISK_BRIDGING_H
#define _ASTERISK_BRIDGING_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include "asterisk/bridge_features.h"
#include "asterisk/bridge_channel.h"
#include "asterisk/bridge_roles.h"
#include "asterisk/dsp.h"
#include "asterisk/uuid.h"

struct ast_bridge_technology;
struct ast_bridge;
struct ast_bridge_tech_optimizations;

/*! \brief Capabilities for a bridge technology */
enum ast_bridge_capability {
	/*! Bridge technology can service calls on hold. */
	AST_BRIDGE_CAPABILITY_HOLDING = (1 << 0),
	/*! Bridge waits for channel to answer.  Passes early media. (XXX Not supported yet) */
	AST_BRIDGE_CAPABILITY_EARLY = (1 << 1),
	/*! Bridge is capable of natively bridging two channels. (Smart bridge only) */
	AST_BRIDGE_CAPABILITY_NATIVE = (1 << 2),
	/*! Bridge is capable of mixing at most two channels. (Smart bridgeable) */
	AST_BRIDGE_CAPABILITY_1TO1MIX = (1 << 3),
	/*! Bridge is capable of mixing an arbitrary number of channels. (Smart bridgeable) */
	AST_BRIDGE_CAPABILITY_MULTIMIX = (1 << 4),
};

/*! \brief Video source modes */
enum ast_bridge_video_mode_type {
	/*! Video is not allowed in the bridge */
	AST_BRIDGE_VIDEO_MODE_NONE = 0,
	/*! A single user is picked as the only distributed of video across the bridge */
	AST_BRIDGE_VIDEO_MODE_SINGLE_SRC,
	/*! A single user's video feed is distributed to all bridge channels, but
	 *  that feed is automatically picked based on who is talking the most. */
	AST_BRIDGE_VIDEO_MODE_TALKER_SRC,
};

/*! \brief This is used for both SINGLE_SRC mode to set what channel
 *  should be the current single video feed */
struct ast_bridge_video_single_src_data {
	/*! Only accept video coming from this channel */
	struct ast_channel *chan_vsrc;
};

/*! \brief This is used for both SINGLE_SRC_TALKER mode to set what channel
 *  should be the current single video feed */
struct ast_bridge_video_talker_src_data {
	/*! Only accept video coming from this channel */
	struct ast_channel *chan_vsrc;
	int average_talking_energy;

	/*! Current talker see's this person */
	struct ast_channel *chan_old_vsrc;
};

/*! \brief Data structure that defines a video source mode */
struct ast_bridge_video_mode {
	enum ast_bridge_video_mode_type mode;
	/* Add data for all the video modes here. */
	union {
		struct ast_bridge_video_single_src_data single_src_data;
		struct ast_bridge_video_talker_src_data talker_src_data;
	} mode_data;
};

/*!
 * \brief Destroy the bridge.
 *
 * \param self Bridge to operate upon.
 *
 * \return Nothing
 */
typedef void (*ast_bridge_destructor_fn)(struct ast_bridge *self);

/*!
 * \brief The bridge is being dissolved.
 *
 * \param self Bridge to operate upon.
 *
 * \details
 * The bridge is being dissolved.  Remove any external
 * references to the bridge so it can be destroyed.
 *
 * \note On entry, self must NOT be locked.
 *
 * \return Nothing
 */
typedef void (*ast_bridge_dissolving_fn)(struct ast_bridge *self);

/*!
 * \brief Push this channel into the bridge.
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to push.
 * \param swap Bridge channel to swap places with if not NULL.
 *
 * \details
 * Setup any channel hooks controlled by the bridge.  Allocate
 * bridge_channel->bridge_pvt and initialize any resources put
 * in bridge_channel->bridge_pvt if needed.  If there is a swap
 * channel, use it as a guide to setting up the bridge_channel.
 *
 * \note On entry, self is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.  The channel did not get pushed.
 */
typedef int (*ast_bridge_push_channel_fn)(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap);

/*!
 * \brief Pull this channel from the bridge.
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to pull.
 *
 * \details
 * Remove any channel hooks controlled by the bridge.  Release
 * any resources held by bridge_channel->bridge_pvt and release
 * bridge_channel->bridge_pvt.
 *
 * \note On entry, self is already locked.
 *
 * \return Nothing
 */
typedef void (*ast_bridge_pull_channel_fn)(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel);

/*!
 * \brief Notify the bridge that this channel was just masqueraded.
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel that was masqueraded.
 *
 * \details
 * A masquerade just happened to this channel.  The bridge needs
 * to re-evaluate this a channel in the bridge.
 *
 * \note On entry, self is already locked.
 *
 * \return Nothing
 */
typedef void (*ast_bridge_notify_masquerade_fn)(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel);

/*!
 * \brief Get the merge priority of this bridge.
 *
 * \param self Bridge to operate upon.
 *
 * \note On entry, self is already locked.
 *
 * \return Merge priority
 */
typedef int (*ast_bridge_merge_priority_fn)(struct ast_bridge *self);

/*!
 * \brief Bridge virtual methods table definition.
 *
 * \note Any changes to this struct must be reflected in
 * bridge_alloc() validity checking.
 */
struct ast_bridge_methods {
	/*! Bridge class name for log messages. */
	const char *name;
	/*! Destroy the bridge. */
	ast_bridge_destructor_fn destroy;
	/*! The bridge is being dissolved.  Remove any references to the bridge. */
	ast_bridge_dissolving_fn dissolving;
	/*! Push the bridge channel into the bridge. */
	ast_bridge_push_channel_fn push;
	/*! Pull the bridge channel from the bridge. */
	ast_bridge_pull_channel_fn pull;
	/*! Notify the bridge of a masquerade with the channel. */
	ast_bridge_notify_masquerade_fn notify_masquerade;
	/*! Get the bridge merge priority. */
	ast_bridge_merge_priority_fn get_merge_priority;
	/*! Peek at swap channel before it can hang up, prior to push. */
	ast_bridge_push_channel_fn push_peek;
};

/*! Softmix technology parameters. */
struct ast_bridge_softmix {
	/*! The video mode softmix is using */
	struct ast_bridge_video_mode video_mode;
	/*!
	 * \brief The internal sample rate softmix uses to mix channels.
	 *
	 * \note If this value is 0, the sofmix may auto adjust the mixing rate.
	 */
	unsigned int internal_sample_rate;
	/*!
	 * \brief The mixing interval indicates how quickly softmix
	 * mixing should occur to mix audio.
	 *
	 * \note When set to 0, softmix must choose a default interval
	 * for itself.
	 */
	unsigned int internal_mixing_interval;
};

/*!
 * \brief Structure that contains information about a bridge
 */
struct ast_bridge {
	/*! Bridge virtual method table. */
	const struct ast_bridge_methods *v_table;
	/*! "Personality" currently exhibited by bridge subclass */
	void *personality;
	/*! Bridge technology that is handling the bridge */
	struct ast_bridge_technology *technology;
	/*! Private information unique to the bridge technology */
	void *tech_pvt;
	/*! Per-bridge topics */
	struct stasis_cp_single *topics;
	/*! Call ID associated with the bridge */
	struct ast_callid *callid;
	/*! Linked list of channels participating in the bridge */
	AST_LIST_HEAD_NOLOCK(, ast_bridge_channel) channels;
	/*! Queue of actions to perform on the bridge. */
	AST_LIST_HEAD_NOLOCK(, ast_frame) action_queue;
	/*! Softmix technology parameters. */
	struct ast_bridge_softmix softmix;
	/*! Bridge flags to tweak behavior */
	struct ast_flags feature_flags;
	/*! Allowed bridge technology capabilities when AST_BRIDGE_FLAG_SMART enabled. */
	uint32_t allowed_capabilities;
	/*! Number of channels participating in the bridge */
	unsigned int num_channels;
	/*! Number of active channels in the bridge. */
	unsigned int num_active;
	/*! Number of channels with AST_BRIDGE_CHANNEL_FLAG_LONELY in the bridge. */
	unsigned int num_lonely;
	/*!
	 * \brief Count of the active temporary requests to inhibit bridge merges.
	 * Zero if merges are allowed.
	 *
	 * \note Temporary as in try again in a moment.
	 */
	unsigned int inhibit_merge;
	/*! Cause code of the dissolved bridge. */
	int cause;
	/*! TRUE if the bridge was reconfigured. */
	unsigned int reconfigured:1;
	/*! TRUE if the bridge has been dissolved.  Any channel that now tries to join is immediately ejected. */
	unsigned int dissolved:1;
	/*! TRUE if the bridge construction was completed. */
	unsigned int construction_completed:1;

	AST_DECLARE_STRING_FIELDS(
		/*! Immutable name of the creator for the bridge */
		AST_STRING_FIELD(creator);
		/*! Immutable name given to the bridge by its creator */
		AST_STRING_FIELD(name);
		/*! Immutable bridge UUID. */
		AST_STRING_FIELD(uniqueid);
	);
};

/*! \brief Bridge base class virtual method table. */
extern struct ast_bridge_methods ast_bridge_base_v_table;

/*!
 * \brief Create a new base class bridge
 *
 * \param capabilities The capabilities that we require to be used on the bridge
 * \param flags Flags that will alter the behavior of the bridge
 * \param creator Entity that created the bridge (optional)
 * \param name Name given to the bridge by its creator (optional, requires named creator)
 * \param id Unique ID given to the bridge by its creator (optional)
 *
 * \retval a pointer to a new bridge on success
 * \retval NULL on failure
 *
 * Example usage:
 *
 * \code
 * struct ast_bridge *bridge;
 * bridge = ast_bridge_base_new(AST_BRIDGE_CAPABILITY_1TO1MIX, AST_BRIDGE_FLAG_DISSOLVE_HANGUP);
 * \endcode
 *
 * This creates a no frills two party bridge that will be
 * destroyed once one of the channels hangs up.
 */
struct ast_bridge *ast_bridge_base_new(uint32_t capabilities, unsigned int flags, const char *creator, const char *name, const char *id);

/*!
 * \brief Try locking the bridge.
 *
 * \param bridge Bridge to try locking
 *
 * \retval 0 on success.
 * \retval non-zero on error.
 */
#define ast_bridge_trylock(bridge)	_ast_bridge_trylock(bridge, __FILE__, __PRETTY_FUNCTION__, __LINE__, #bridge)
static inline int _ast_bridge_trylock(struct ast_bridge *bridge, const char *file, const char *function, int line, const char *var)
{
	return __ao2_trylock(bridge, AO2_LOCK_REQ_MUTEX, file, function, line, var);
}

/*!
 * \brief Lock the bridge.
 *
 * \param bridge Bridge to lock
 *
 * \return Nothing
 */
#define ast_bridge_lock(bridge)	_ast_bridge_lock(bridge, __FILE__, __PRETTY_FUNCTION__, __LINE__, #bridge)
static inline void _ast_bridge_lock(struct ast_bridge *bridge, const char *file, const char *function, int line, const char *var)
{
	__ao2_lock(bridge, AO2_LOCK_REQ_MUTEX, file, function, line, var);
}

/*!
 * \brief Unlock the bridge.
 *
 * \param bridge Bridge to unlock
 *
 * \return Nothing
 */
#define ast_bridge_unlock(bridge)	_ast_bridge_unlock(bridge, __FILE__, __PRETTY_FUNCTION__, __LINE__, #bridge)
static inline void _ast_bridge_unlock(struct ast_bridge *bridge, const char *file, const char *function, int line, const char *var)
{
	__ao2_unlock(bridge, file, function, line, var);
}

/*! \brief Lock two bridges. */
#define ast_bridge_lock_both(bridge1, bridge2)		\
	do {											\
		for (;;) {									\
			ast_bridge_lock(bridge1);				\
			if (!ast_bridge_trylock(bridge2)) {		\
				break;								\
			}										\
			ast_bridge_unlock(bridge1);				\
			sched_yield();							\
		}											\
	} while (0)

/*!
 * \brief Destroy a bridge
 *
 * \param bridge Bridge to destroy
 * \param cause Cause of bridge being destroyed.  (If cause <= 0 then use AST_CAUSE_NORMAL_CLEARING)
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_destroy(bridge, AST_CAUSE_NORMAL_CLEARING);
 * \endcode
 *
 * This destroys a bridge that was previously created.
 *
 * \note
 * While this function will kick all channels out of the bridge, channels that
 * were added to the bridge using ast_bridge_impart() with the flag
 * AST_BRIDGE_IMPART_CHAN_DEPARTABLE set must have ast_bridge_depart() called
 * on them.
 */
int ast_bridge_destroy(struct ast_bridge *bridge, int cause);

/*!
 * \brief Notify bridging that this channel was just masqueraded.
 * \since 12.0.0
 *
 * \param chan Channel just involved in a masquerade
 *
 * \return Nothing
 */
void ast_bridge_notify_masquerade(struct ast_channel *chan);

enum ast_bridge_join_flags {
	/*! The bridge reference is being passed by the caller. */
	AST_BRIDGE_JOIN_PASS_REFERENCE = (1 << 0),
	/*! The initial bridge join does not cause a COLP exchange. */
	AST_BRIDGE_JOIN_INHIBIT_JOIN_COLP = (1 << 1),
};

/*!
 * \brief Join a channel to a bridge (blocking)
 *
 * \param bridge Bridge to join
 * \param chan Channel to join
 * \param swap Channel to swap out if swapping (A channel reference is stolen.)
 * \param features Bridge features structure
 * \param tech_args Optional Bridging tech optimization parameters for this channel.
 * \param flags defined by enum ast_bridge_join_flags.
 *
 * \note The passed in swap channel is always unreffed on return.  It is not a
 * good idea to access the swap channel on return or for the caller to keep a
 * reference to it.
 *
 * \note Absolutely _NO_ locks should be held before calling
 * this function since it blocks.
 *
 * \retval 0 if the channel successfully joined the bridge before it exited.
 * \retval -1 if the channel failed to join the bridge
 *
 * Example usage:
 *
 * \code
 * ast_bridge_join(bridge, chan, NULL, NULL, NULL, AST_BRIDGE_JOIN_PASS_REFERENCE);
 * \endcode
 *
 * This adds a channel pointed to by the chan pointer to the bridge pointed to by
 * the bridge pointer. This function will not return until the channel has been
 * removed from the bridge, swapped out for another channel, or has hung up.
 *
 * If this channel will be replacing another channel the other channel can be specified
 * in the swap parameter. The other channel will be thrown out of the bridge in an
 * atomic fashion.
 *
 * If channel specific features are enabled a pointer to the features structure
 * can be specified in the features parameter.
 */
int ast_bridge_join(struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_channel *swap,
	struct ast_bridge_features *features,
	struct ast_bridge_tech_optimizations *tech_args,
	enum ast_bridge_join_flags flags);

enum ast_bridge_impart_flags {
	/*! Field describing what the caller can do with the channel after it is imparted. */
	AST_BRIDGE_IMPART_CHAN_MASK = (1 << 0),
	/*! The caller wants to reclaim the channel using ast_bridge_depart(). */
	AST_BRIDGE_IMPART_CHAN_DEPARTABLE = (0 << 0),
	/*! The caller is passing channel control entirely to the bridging system. */
	AST_BRIDGE_IMPART_CHAN_INDEPENDENT = (1 << 0),
	/*! The initial bridge join does not cause a COLP exchange. */
	AST_BRIDGE_IMPART_INHIBIT_JOIN_COLP = (1 << 1),
};

/*!
 * \brief Impart a channel to a bridge (non-blocking)
 *
 * \param bridge Bridge to impart on
 * \param chan Channel to impart (The channel reference is stolen if impart successful.)
 * \param swap Channel to swap out if swapping.  NULL if not swapping.
 * \param features Bridge features structure.
 * \param flags defined by enum ast_bridge_impart_flags.
 *
 * \note The features parameter must be NULL or obtained by
 * ast_bridge_features_new().  You must not dereference features
 * after calling even if the call fails.
 *
 * \note chan is locked by this function.
 *
 * \retval 0 on success
 * \retval -1 on failure (Caller still has ownership of chan)
 *
 * Example usage:
 *
 * \code
 * ast_bridge_impart(bridge, chan, NULL, NULL, AST_BRIDGE_IMPART_CHAN_INDEPENDENT);
 * \endcode
 *
 * \details
 * This adds a channel pointed to by the chan pointer to the
 * bridge pointed to by the bridge pointer.  This function will
 * return immediately and will not wait until the channel is no
 * longer part of the bridge.
 *
 * If this channel will be replacing another channel the other
 * channel can be specified in the swap parameter.  The other
 * channel will be thrown out of the bridge in an atomic
 * fashion.
 *
 * If channel specific features are enabled, a pointer to the
 * features structure can be specified in the features
 * parameter.
 *
 * \note If you impart a channel with
 * AST_BRIDGE_IMPART_CHAN_DEPARTABLE you MUST
 * ast_bridge_depart() the channel if this call succeeds.  The
 * bridge channel thread is created join-able.  The implication
 * is that the channel is special and will not behave like a
 * normal channel.
 *
 * \note If you impart a channel with
 * AST_BRIDGE_IMPART_CHAN_INDEPENDENT you must not
 * ast_bridge_depart() the channel.  The bridge channel thread
 * is created non-join-able.  The channel must be treated as if
 * it were placed into the bridge by ast_bridge_join().
 * Channels placed into a bridge by ast_bridge_join() are
 * removed by a third party using ast_bridge_remove().
 */
int ast_bridge_impart(struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_channel *swap,
	struct ast_bridge_features *features,
	enum ast_bridge_impart_flags flags) attribute_warn_unused_result;

/*!
 * \brief Depart a channel from a bridge
 *
 * \param chan Channel to depart
 *
 * \note chan is locked by this function.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_depart(chan);
 * \endcode
 *
 * This removes the channel pointed to by the chan pointer from any bridge
 * it may be in and gives control to the calling thread.
 * This does not hang up the channel.
 *
 * \note This API call can only be used on channels that were added to the bridge
 *       using the ast_bridge_impart API call with the AST_BRIDGE_IMPART_CHAN_DEPARTABLE
 *       flag.
 */
int ast_bridge_depart(struct ast_channel *chan);

/*!
 * \brief Remove a channel from a bridge
 *
 * \param bridge Bridge that the channel is to be removed from
 * \param chan Channel to remove
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_remove(bridge, chan);
 * \endcode
 *
 * This removes the channel pointed to by the chan pointer from the bridge
 * pointed to by the bridge pointer and requests that it be hung up. Control
 * over the channel will NOT be given to the calling thread.
 *
 * \note This API call can be used on channels that were added to the bridge
 *       using both ast_bridge_join and ast_bridge_impart.
 */
int ast_bridge_remove(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Kick a channel from a bridge
 *
 * \param bridge Bridge that the channel is to be kicked from
 * \param chan Channel to kick
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_kick(bridge, chan);
 * \endcode
 *
 * \details
 * This kicks the channel pointed to by the chan pointer from
 * the bridge pointed to by the bridge pointer and requests that
 * it be hung up.  Control over the channel will NOT be given to
 * the calling thread.
 *
 * \note The functional difference between ast_bridge_kick() and
 * ast_bridge_remove() is that the bridge may dissolve as a
 * result of the channel being kicked.
 *
 * \note This API call can be used on channels that were added
 * to the bridge using both ast_bridge_join and
 * ast_bridge_impart.
 */
int ast_bridge_kick(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Merge two bridges together
 *
 * \param dst_bridge Destination bridge of merge.
 * \param src_bridge Source bridge of merge.
 * \param merge_best_direction TRUE if don't care about which bridge merges into the other.
 * \param kick_me Array of channels to kick from the bridges.
 * \param num_kick Number of channels in the kick_me array.
 *
 * \note Absolutely _NO_ bridge or channel locks should be held
 * before calling this function.
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_merge(dst_bridge, src_bridge, 0, NULL, 0);
 * \endcode
 *
 * This moves the channels in src_bridge into the bridge pointed
 * to by dst_bridge.
 */
int ast_bridge_merge(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, int merge_best_direction, struct ast_channel **kick_me, unsigned int num_kick);

/*!
 * \brief Move a channel from one bridge to another.
 * \since 12.0.0
 *
 * \param dst_bridge Destination bridge of bridge channel move.
 * \param src_bridge Source bridge of bridge channel move.
 * \param chan Channel to move.
 * \param swap Channel to replace in dst_bridge.
 * \param attempt_recovery TRUE if failure attempts to push channel back into original bridge.
 *
 * \note Absolutely _NO_ bridge or channel locks should be held
 * before calling this function.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
int ast_bridge_move(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, struct ast_channel *chan, struct ast_channel *swap, int attempt_recovery);

/*!
 * \brief Adjust the bridge merge inhibit request count.
 * \since 12.0.0
 *
 * \param bridge What to operate on.
 * \param request Inhibit request increment.
 *     (Positive to add requests.  Negative to remove requests.)
 *
 * \return Nothing
 */
void ast_bridge_merge_inhibit(struct ast_bridge *bridge, int request);

/*!
 * \brief Suspend a channel temporarily from a bridge
 *
 * \param bridge Bridge to suspend the channel from
 * \param chan Channel to suspend
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_suspend(bridge, chan);
 * \endcode
 *
 * This suspends the channel pointed to by chan from the bridge pointed to by bridge temporarily.
 * Control of the channel is given to the calling thread. This differs from ast_bridge_depart as
 * the channel will not be removed from the bridge.
 *
 * \note This API call can be used on channels that were added to the bridge
 *       using both ast_bridge_join and ast_bridge_impart.
 */
int ast_bridge_suspend(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Unsuspend a channel from a bridge
 *
 * \param bridge Bridge to unsuspend the channel from
 * \param chan Channel to unsuspend
 *
 * \retval 0 on success
 * \retval -1 on failure
 *
 * Example usage:
 *
 * \code
 * ast_bridge_unsuspend(bridge, chan);
 * \endcode
 *
 * This unsuspends the channel pointed to by chan from the bridge pointed to by bridge.
 * The bridge will go back to handling the channel once this function returns.
 *
 * \note You must not mess with the channel once this function returns.
 *       Doing so may result in bad things happening.
 */
int ast_bridge_unsuspend(struct ast_bridge *bridge, struct ast_channel *chan);

struct ast_unreal_pvt;

/*!
 * \brief Check and optimize out the unreal channels between bridges.
 * \since 12.0.0
 *
 * \param chan Unreal channel writing a frame into the channel driver.
 * \param peer Other unreal channel in the pair.
 * \param pvt Private data provided by an implementation of the unreal driver that
 * contains the callbacks that should be called when optimization begins/ends
 *
 * \note It is assumed that chan is already locked.
 *
 * \retval 0 if unreal channels were not optimized out.
 * \retval non-zero if unreal channels were optimized out.
 */
int ast_bridge_unreal_optimize_out(struct ast_channel *chan, struct ast_channel *peer, struct ast_unreal_pvt *pvt);

/*!
 * \brief Tells, if optimization is allowed, how the optimization would be performed
 */
enum ast_bridge_optimization {
	/*! Optimization would swap peer into the chan_bridge */
	AST_BRIDGE_OPTIMIZE_SWAP_TO_CHAN_BRIDGE,
	/*! Optimization would swap chan into the peer_bridge */
	AST_BRIDGE_OPTIMIZE_SWAP_TO_PEER_BRIDGE,
	/*! Optimization would merge peer_bridge into chan_bridge */
	AST_BRIDGE_OPTIMIZE_MERGE_TO_CHAN_BRIDGE,
	/*! Optimization would merge chan_bridge into peer_bridge */
	AST_BRIDGE_OPTIMIZE_MERGE_TO_PEER_BRIDGE,
	/*! Optimization is not permitted on one or both bridges */
	AST_BRIDGE_OPTIMIZE_PROHIBITED,
};

/*!
 * \brief Determine if bridges allow for optimization to occur betweem them
 * \since 12.0.0
 *
 * \param chan_bridge First bridge being tested
 * \param peer_bridge Second bridge being tested
 *
 * This determines if two bridges allow for unreal channel optimization
 * to occur between them. The function does not require for unreal channels
 * to already be in the bridges when called.
 *
 * \note It is assumed that both bridges are locked prior to calling this function
 *
 * \note A return other than AST_BRIDGE_OPTIMIZE_PROHIBITED does not guarantee
 * that an optimization attempt will succeed. However, a return of
 * AST_BRIDGE_OPTIMIZE_PROHIBITED guarantees that an optimization attempt will
 * never succeed.
 *
 * \returns Optimization allowability for the bridges
 */
enum ast_bridge_optimization ast_bridges_allow_optimization(struct ast_bridge *chan_bridge,
		struct ast_bridge *peer_bridge);

/*!
 * \brief Put an action onto the specified bridge.
 * \since 12.0.0
 *
 * \param bridge What to queue the action on.
 * \param action What to do.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 *
 * \note This API call is meant for internal bridging operations.
 */
int ast_bridge_queue_action(struct ast_bridge *bridge, struct ast_frame *action);

/*!
 * \brief Queue the given frame to everyone else.
 * \since 12.0.0
 *
 * \param bridge What bridge to distribute frame.
 * \param bridge_channel Channel to optionally not pass frame to. (NULL to pass to everyone)
 * \param frame Frame to pass.
 *
 * \note This is intended to be called by bridge hooks and
 * bridge technologies.
 *
 * \retval 0 Frame written to at least one channel.
 * \retval -1 Frame written to no channels.
 */
int ast_bridge_queue_everyone_else(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame);

/*!
 * \brief Adjust the internal mixing sample rate of a bridge
 * used during multimix mode.
 *
 * \param bridge Channel to change the sample rate on.
 * \param sample_rate the sample rate to change to. If a
 *        value of 0 is passed here, the bridge will be free to pick
 *        what ever sample rate it chooses.
 *
 */
void ast_bridge_set_internal_sample_rate(struct ast_bridge *bridge, unsigned int sample_rate);

/*!
 * \brief Adjust the internal mixing interval of a bridge used
 * during multimix mode.
 *
 * \param bridge Channel to change the sample rate on.
 * \param mixing_interval the sample rate to change to.  If 0 is set
 * the bridge tech is free to choose any mixing interval it uses by default.
 */
void ast_bridge_set_mixing_interval(struct ast_bridge *bridge, unsigned int mixing_interval);

/*!
 * \brief Set a bridge to feed a single video source to all participants.
 */
void ast_bridge_set_single_src_video_mode(struct ast_bridge *bridge, struct ast_channel *video_src_chan);

/*!
 * \brief Set the bridge to pick the strongest talker supporting
 * video as the single source video feed
 */
void ast_bridge_set_talker_src_video_mode(struct ast_bridge *bridge);

/*!
 * \brief Update information about talker energy for talker src video mode.
 */
void ast_bridge_update_talker_src_video_mode(struct ast_bridge *bridge, struct ast_channel *chan, int talker_energy, int is_keyfame);

/*!
 * \brief Returns the number of video sources currently active in the bridge
 */
int ast_bridge_number_video_src(struct ast_bridge *bridge);

/*!
 * \brief Determine if a channel is a video src for the bridge
 *
 * \retval 0 Not a current video source of the bridge.
 * \retval None 0, is a video source of the bridge, The number
 *         returned represents the priority this video stream has
 *         on the bridge where 1 is the highest priority.
 */
int ast_bridge_is_video_src(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief remove a channel as a source of video for the bridge.
 */
void ast_bridge_remove_video_src(struct ast_bridge *bridge, struct ast_channel *chan);

enum ast_transfer_result {
	/*! The transfer completed successfully */
	AST_BRIDGE_TRANSFER_SUCCESS,
	/*! A bridge involved does not permit transferring */
	AST_BRIDGE_TRANSFER_NOT_PERMITTED,
	/*! The current bridge setup makes transferring an invalid operation */
	AST_BRIDGE_TRANSFER_INVALID,
	/*! The transfer operation failed for a miscellaneous reason */
	AST_BRIDGE_TRANSFER_FAIL,
};

enum ast_transfer_type {
	/*! Transfer of a single party */
	AST_BRIDGE_TRANSFER_SINGLE_PARTY,
	/*! Transfer of multiple parties */
	AST_BRIDGE_TRANSFER_MULTI_PARTY,
};

/*!
 * \brief AO2 object that wraps data for transfer_channel_cb
 */
struct transfer_channel_data {
	void *data;    /*! Data to be used by the transfer_channel_cb -- note that this
	                *  pointer is going to be pointing to something on the stack, so
	                *  it must not be used at any point after returning from the
	                *  transfer_channel_cb. */
	int completed; /*! Initially 0, This will be set to 1 by either the transfer
	                *  code or by transfer code hooks (e.g. parking) when the
	                *  transfer is completed and any remaining actions have taken
	                *  place (e.g. parking announcements). It will never be reset
	                *  to 0. This is used for deferring progress for channel
	                *  drivers that support deferred progress. */
};

/*!
 * \brief Callback function type called during blind transfers
 *
 * A caller of ast_bridge_transfer_blind() may wish to set data on
 * the channel that ends up running dialplan. For instance, it may
 * be useful to set channel variables on the channel.
 *
 * \param chan The involved channel
 * \param user_data User-provided data needed in the callback
 * \param transfer_type The type of transfer being completed
 */
typedef void (*transfer_channel_cb)(struct ast_channel *chan, struct transfer_channel_data *user_data,
		enum ast_transfer_type transfer_type);

/*!
 * \brief Blind transfer target to the extension and context provided
 *
 * The channel given is bridged to one or multiple channels. Depending on
 * the bridge and the number of participants, the entire bridge could be
 * transferred to the given destination, or a single channel may be redirected.
 *
 * Callers may also provide a callback to be called on the channel that will
 * be running dialplan. The user data passed into ast_bridge_transfer_blind
 * will be given as the argument to the callback to be interpreted as desired.
 * This callback is guaranteed to be called in the same thread as
 * ast_bridge_transfer_blind() and before ast_bridge_transfer_blind() returns.
 *
 * \note Absolutely _NO_ channel locks should be held before
 * calling this function.
 *
 * \param is_external Indicates that transfer was initiated externally
 * \param transferer The channel performing the blind transfer
 * \param exten The dialplan extension to send the call to
 * \param context The dialplan context to send the call to
 * \param new_channel_cb A callback to be called on the channel that will
 *        be executing dialplan
 * \param user_data Argument for new_channel_cb
 * \return The success or failure result of the blind transfer
 */
enum ast_transfer_result ast_bridge_transfer_blind(int is_external,
		struct ast_channel *transferer, const char *exten, const char *context,
		transfer_channel_cb new_channel_cb, void *user_data);

/*!
 * \brief Attended transfer
 *
 * The two channels are both transferer channels. The first is the channel
 * that is bridged to the transferee (or if unbridged, the 'first' call of
 * the transfer). The second is the channel that is bridged to the transfer
 * target (or if unbridged, the 'second' call of the transfer).
 *
 * \note Absolutely _NO_ channel locks should be held before
 * calling this function.
 *
 * \param to_transferee Transferer channel on initial call (presumably bridged to transferee)
 * \param to_transfer_target Transferer channel on consultation call (presumably bridged to transfer target)
 * \return The success or failure of the attended transfer
 */
enum ast_transfer_result ast_bridge_transfer_attended(struct ast_channel *to_transferee,
		struct ast_channel *to_transfer_target);

/*!
 * \brief Set the relevant transfer variables for a single channel
 *
 * Sets either the ATTENDEDTRANSFER or BLINDTRANSFER variable for a channel while clearing
 * the opposite.
 *
 * \param chan Channel the variable is being set for
 * \param value Value the variable is being set to
 * \param is_attended false  set BLINDTRANSFER and unset ATTENDEDTRANSFER
 *                    true   set ATTENDEDTRANSFER and unset BLINDTRANSFER
 */
void ast_bridge_set_transfer_variables(struct ast_channel *chan, const char *value, int is_attended);

/*!
 * \brief Get a container of all channels in the bridge
 * \since 12.0.0
 *
 * \param bridge The bridge which is already locked.
 *
 * \retval NULL Failed to create container
 * \retval non-NULL Container of channels in the bridge
 */
struct ao2_container *ast_bridge_peers_nolock(struct ast_bridge *bridge);

/*!
 * \brief Get a container of all channels in the bridge
 * \since 12.0.0
 *
 * \param bridge The bridge
 *
 * \note The returned container is a snapshot of channels in the
 * bridge when called.
 *
 * \retval NULL Failed to create container
 * \retval non-NULL Container of channels in the bridge
 */
struct ao2_container *ast_bridge_peers(struct ast_bridge *bridge);

/*!
 * \brief Get the channel's bridge peer only if the bridge is two-party.
 * \since 12.0.0
 *
 * \param bridge The bridge which is already locked.
 * \param chan Channel desiring the bridge peer channel.
 *
 * \note The returned peer channel is the current peer in the
 * bridge when called.
 *
 * \retval NULL Channel not in a bridge or the bridge is not two-party.
 * \retval non-NULL Reffed peer channel at time of calling.
 */
struct ast_channel *ast_bridge_peer_nolock(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Get the channel's bridge peer only if the bridge is two-party.
 * \since 12.0.0
 *
 * \param bridge The bridge
 * \param chan Channel desiring the bridge peer channel.
 *
 * \note The returned peer channel is the current peer in the
 * bridge when called.
 *
 * \retval NULL Channel not in a bridge or the bridge is not two-party.
 * \retval non-NULL Reffed peer channel at time of calling.
 */
struct ast_channel *ast_bridge_peer(struct ast_bridge *bridge, struct ast_channel *chan);

/*!
 * \brief Remove marked bridge channel feature hooks.
 * \since 12.0.0
 *
 * \param features Bridge features structure
 * \param flags Determinator for whether hook is removed.
 *
 * \return Nothing
 */
void ast_bridge_features_remove(struct ast_bridge_features *features, enum ast_bridge_hook_remove_flags flags);

/*!
 * \brief Find bridge by id
 * \since 12.0.0
 *
 * \param bridge_id Bridge identifier
 *
 * \return NULL bridge not found
 * \return non-NULL reference to bridge
 */
struct ast_bridge *ast_bridge_find_by_id(const char *bridge_id);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif	/* _ASTERISK_BRIDGING_H */
