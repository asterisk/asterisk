/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * David Vossel <dvossel@digium.com>
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


#ifndef _CONFBRIDGE_H
#define _CONFBRIDGE_H

#include "asterisk.h"
#include "asterisk/app.h"
#include "asterisk/logger.h"
#include "asterisk/linkedlists.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_features.h"
#include "asterisk/stasis_bridges.h"
#include "conf_state.h"

/*! Maximum length of a conference bridge name */
#define MAX_CONF_NAME AST_MAX_EXTENSION
/*! Maximum length of a conference pin */
#define MAX_PIN     80
/*! Maximum length of bridge/user/menu profile names */
#define MAX_PROFILE_NAME 128

#define DEFAULT_USER_PROFILE "default_user"
#define DEFAULT_BRIDGE_PROFILE "default_bridge"
#define DEFAULT_MENU_PROFILE "default_menu"

/*! Default minimum average magnitude threshold to determine talking by the DSP. */
#define DEFAULT_TALKING_THRESHOLD 160

/*! Default time in ms of silence necessary to declare talking stopped by the bridge. */
#define DEFAULT_SILENCE_THRESHOLD 2500

enum user_profile_flags {
	USER_OPT_ADMIN =        (1 << 0), /*!< Set if the caller is an administrator */
	USER_OPT_NOONLYPERSON = (1 << 1), /*!< Set if the "you are currently the only person in this conference" sound file should not be played */
	USER_OPT_MARKEDUSER =   (1 << 2), /*!< Set if the caller is a marked user */
	USER_OPT_STARTMUTED =   (1 << 3), /*!< Set if the caller should be initially set muted */
	USER_OPT_MUSICONHOLD =  (1 << 4), /*!< Set if music on hold should be played if nobody else is in the conference bridge */
	USER_OPT_QUIET =        (1 << 5), /*!< Set if no audio prompts should be played */
	USER_OPT_ANNOUNCEUSERCOUNT = (1 << 6), /*!< Set if the number of users should be announced to the caller */
	USER_OPT_WAITMARKED =   (1 << 7), /*!< Set if the user must wait for a marked user before starting */
	USER_OPT_ENDMARKED =    (1 << 8), /*!< Set if the user should be kicked after the last Marked user exits */
	USER_OPT_DENOISE =      (1 << 9), /*!< Sets if denoise filter should be used on audio before mixing. */
	USER_OPT_ANNOUNCE_JOIN_LEAVE = (1 << 10), /*!< Sets if the user's name should be recorded and announced on join and leave. */
	USER_OPT_TALKER_DETECT = (1 << 11), /*!< Sets if start and stop talking events should generated for this user over AMI. */
	USER_OPT_DROP_SILENCE =  (1 << 12), /*!< Sets if silence should be dropped from the mix or not. */
	USER_OPT_DTMF_PASS    =  (1 << 13), /*!< Sets if dtmf should be passed into the conference or not */
	USER_OPT_ANNOUNCEUSERCOUNTALL = (1 << 14), /*!< Sets if the number of users should be announced to everyone. */
	USER_OPT_JITTERBUFFER =  (1 << 15), /*!< Places a jitterbuffer on the user. */
	USER_OPT_ANNOUNCE_JOIN_LEAVE_REVIEW = (1 << 16), /*!< modifies ANNOUNCE_JOIN_LEAVE - user reviews the recording before continuing */
	USER_OPT_SEND_EVENTS = (1 << 17), /*!< Send text message events to users */
	USER_OPT_ECHO_EVENTS = (1 << 18), /*!< Send events only to the admin(s) */
	USER_OPT_TEXT_MESSAGING = (1 << 19), /*!< Send text messages to the user */
	USER_OPT_ANSWER_CHANNEL = (1 << 20), /*!< Sets if the channel should be answered if currently unanswered */
	USER_OPT_HEAR_OWN_JOIN_SOUND  = (1 << 21), /*!< Set if the caller should hear the join sound */
	USER_OPT_ENDMARKEDANY = (1 << 22), /*!< Set if the user should be kicked after any marked user exits */
};

enum bridge_profile_flags {
	BRIDGE_OPT_RECORD_CONFERENCE = (1 << 0), /*!< Set if the conference should be recorded */
	BRIDGE_OPT_VIDEO_SRC_LAST_MARKED = (1 << 1), /*!< Set if conference should feed video of last marked user to all participants. */
	BRIDGE_OPT_VIDEO_SRC_FIRST_MARKED = (1 << 2), /*!< Set if conference should feed video of first marked user to all participants. */
	BRIDGE_OPT_VIDEO_SRC_FOLLOW_TALKER = (1 << 3), /*!< Set if conference set the video feed to follow the loudest talker.  */
	BRIDGE_OPT_RECORD_FILE_APPEND = (1 << 4), /*!< Set if the record file should be appended to between start/stops.  */
	BRIDGE_OPT_RECORD_FILE_TIMESTAMP = (1 << 5), /*!< Set if the record file should have a timestamp appended */
	BRIDGE_OPT_BINAURAL_ACTIVE = (1 << 6), /*!< Set if binaural convolution is activated */
	BRIDGE_OPT_VIDEO_SRC_SFU = (1 << 7), /*!< Selective forwarding unit */
	BRIDGE_OPT_REMB_BEHAVIOR_AVERAGE = (1 << 8), /*!< The average of all REMB reports is sent to the sender */
	BRIDGE_OPT_REMB_BEHAVIOR_LOWEST = (1 << 9), /*!< The lowest estimated maximum bitrate is sent to the sender */
	BRIDGE_OPT_REMB_BEHAVIOR_HIGHEST = (1 << 10), /*!< The highest estimated maximum bitrate is sent to the sender */
	BRIDGE_OPT_ENABLE_EVENTS = (1 << 11), /*!< Enable sending events to participants */
	BRIDGE_OPT_REMB_BEHAVIOR_AVERAGE_ALL = (1 << 12), /*!< The average of all REMB reports in the entire bridge is sent to each sender */
	BRIDGE_OPT_REMB_BEHAVIOR_LOWEST_ALL = (1 << 13), /*!< The lowest estimated maximum bitrate from all receivers is sent to each sender */
	BRIDGE_OPT_REMB_BEHAVIOR_HIGHEST_ALL = (1 << 14), /*!< The highest estimated maximum bitrate from all receivers is sent to each sender */
	BRIDGE_OPT_REMB_BEHAVIOR_FORCE = (1 << 15), /*!< Force the REMB estimated bitrate to that specifiec in remb_estimated_bitrate */
};

enum conf_menu_action_id {
	MENU_ACTION_TOGGLE_MUTE = 1,
	MENU_ACTION_PLAYBACK,
	MENU_ACTION_PLAYBACK_AND_CONTINUE,
	MENU_ACTION_INCREASE_LISTENING,
	MENU_ACTION_DECREASE_LISTENING,
	MENU_ACTION_RESET_LISTENING,
	MENU_ACTION_RESET_TALKING,
	MENU_ACTION_INCREASE_TALKING,
	MENU_ACTION_DECREASE_TALKING,
	MENU_ACTION_DIALPLAN_EXEC,
	MENU_ACTION_ADMIN_TOGGLE_LOCK,
	MENU_ACTION_ADMIN_KICK_LAST,
	MENU_ACTION_LEAVE,
	MENU_ACTION_NOOP,
	MENU_ACTION_SET_SINGLE_VIDEO_SRC,
	MENU_ACTION_RELEASE_SINGLE_VIDEO_SRC,
	MENU_ACTION_PARTICIPANT_COUNT,
	MENU_ACTION_ADMIN_TOGGLE_MUTE_PARTICIPANTS,
	MENU_ACTION_TOGGLE_BINAURAL,
};

/*! The conference menu action contains both
 *  the action id that represents the action that
 *  must take place, along with any data associated
 *  with that action. */
struct conf_menu_action {
	enum conf_menu_action_id id;
	union {
		char playback_file[PATH_MAX];
		struct {
			char context[AST_MAX_CONTEXT];
			char exten[AST_MAX_EXTENSION];
			int priority;
		} dialplan_args;
	} data;
	AST_LIST_ENTRY(conf_menu_action) action;
};

/*! Conference menu entries contain the DTMF sequence
 *  and the list of actions that are associated with that
 *  sequence. */
struct conf_menu_entry {
	/*! the DTMF sequence that triggers the actions */
	char dtmf[MAXIMUM_DTMF_FEATURE_STRING];
	/*! The actions associated with this menu entry. */
	AST_LIST_HEAD_NOLOCK(, conf_menu_action) actions;
	AST_LIST_ENTRY(conf_menu_entry) entry;
};

/*! Conference menu structure.  Contains a list
 * of DTMF sequences coupled with the actions those
 * sequences invoke.*/
struct conf_menu {
	char name[MAX_PROFILE_NAME];
	AST_LIST_HEAD_NOLOCK(, conf_menu_entry) entries;
};

struct user_profile {
	char name[MAX_PROFILE_NAME];
	char pin[MAX_PIN];
	char moh_class[128];
	char announcement[PATH_MAX];
	unsigned int flags;
	unsigned int announce_user_count_all_after;
	/*! Minimum average magnitude threshold to determine talking by the DSP. */
	unsigned int talking_threshold;
	/*! Time in ms of silence necessary to declare talking stopped by the bridge. */
	unsigned int silence_threshold;
	/*! The time in ms the user may stay in the confbridge */
	unsigned int timeout;
};

enum conf_sounds {
	CONF_SOUND_HAS_JOINED,
	CONF_SOUND_HAS_LEFT,
	CONF_SOUND_KICKED,
	CONF_SOUND_MUTED,
	CONF_SOUND_UNMUTED,
	CONF_SOUND_ONLY_ONE,
	CONF_SOUND_THERE_ARE,
	CONF_SOUND_OTHER_IN_PARTY,
	CONF_SOUND_PLACE_IN_CONF,
	CONF_SOUND_WAIT_FOR_LEADER,
	CONF_SOUND_LEADER_HAS_LEFT,
	CONF_SOUND_GET_PIN,
	CONF_SOUND_INVALID_PIN,
	CONF_SOUND_ONLY_PERSON,
	CONF_SOUND_LOCKED,
	CONF_SOUND_LOCKED_NOW,
	CONF_SOUND_UNLOCKED_NOW,
	CONF_SOUND_ERROR_MENU,
	CONF_SOUND_JOIN,
	CONF_SOUND_LEAVE,
	CONF_SOUND_PARTICIPANTS_MUTED,
	CONF_SOUND_PARTICIPANTS_UNMUTED,
	CONF_SOUND_BEGIN,
	CONF_SOUND_BINAURAL_ON,
	CONF_SOUND_BINAURAL_OFF,
};

struct bridge_profile_sounds {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(hasjoin);
		AST_STRING_FIELD(hasleft);
		AST_STRING_FIELD(kicked);
		AST_STRING_FIELD(muted);
		AST_STRING_FIELD(unmuted);
		AST_STRING_FIELD(onlyone);
		AST_STRING_FIELD(thereare);
		AST_STRING_FIELD(otherinparty);
		AST_STRING_FIELD(placeintoconf);
		AST_STRING_FIELD(waitforleader);
		AST_STRING_FIELD(leaderhasleft);
		AST_STRING_FIELD(getpin);
		AST_STRING_FIELD(invalidpin);
		AST_STRING_FIELD(onlyperson);
		AST_STRING_FIELD(locked);
		AST_STRING_FIELD(lockednow);
		AST_STRING_FIELD(unlockednow);
		AST_STRING_FIELD(errormenu);
		AST_STRING_FIELD(leave);
		AST_STRING_FIELD(join);
		AST_STRING_FIELD(participantsmuted);
		AST_STRING_FIELD(participantsunmuted);
		AST_STRING_FIELD(begin);
		AST_STRING_FIELD(binauralon);
		AST_STRING_FIELD(binauraloff);
	);
};

struct bridge_profile {
	char name[MAX_PROFILE_NAME];
	char language[MAX_LANGUAGE];		  /*!< Language used for playback_chan */
	char rec_file[PATH_MAX];
	char rec_options[128];
	char rec_command[128];
	unsigned int flags;
	unsigned int max_members;          /*!< The maximum number of participants allowed in the conference */
	unsigned int internal_sample_rate; /*!< The internal sample rate of the bridge. 0 when set to auto adjust mode. */
	unsigned int maximum_sample_rate; /*!< The maximum sample rate of the bridge. 0 when set to no maximum. */
	unsigned int mix_interval;  /*!< The internal mixing interval used by the bridge. When set to 0 the bridgewill use a default interval. */
	struct bridge_profile_sounds *sounds;
	char regcontext[AST_MAX_CONTEXT];
	unsigned int video_update_discard; /*!< Amount of time after sending a video update request that subsequent requests should be discarded */
	unsigned int remb_send_interval; /*!< Interval at which a combined REMB frame is sent to video sources */
	unsigned int remb_estimated_bitrate; /*!< Bitrate sent when BRIDGE_OPT_REMB_BEHAVIOR_FORCE is set */
};

/*! \brief The structure that represents a conference bridge */
struct confbridge_conference {
	char name[MAX_CONF_NAME];                                         /*!< Name of the conference bridge */
	struct confbridge_state *state;                                   /*!< Conference state information */
	struct ast_bridge *bridge;                                        /*!< Bridge structure doing the mixing */
	struct bridge_profile b_profile;                                  /*!< The Bridge Configuration Profile */
	unsigned int activeusers;                                         /*!< Number of active users present */
	unsigned int markedusers;                                         /*!< Number of marked users present */
	unsigned int waitingusers;                                        /*!< Number of waiting users present */
	unsigned int locked:1;                                            /*!< Is this conference bridge locked? */
	unsigned int muted:1;                                             /*!< Is this conference bridge muted? */
	struct ast_channel *playback_chan;                                /*!< Channel used for playback into the conference bridge */
	struct ast_channel *record_chan;                                  /*!< Channel used for recording the conference */
	struct ast_str *record_filename;                                  /*!< Recording filename. */
	struct ast_str *orig_rec_file;                                    /*!< Previous b_profile.rec_file. */
	AST_LIST_HEAD_NOLOCK(, confbridge_user) active_list;              /*!< List of users participating in the conference bridge */
	AST_LIST_HEAD_NOLOCK(, confbridge_user) waiting_list;             /*!< List of users waiting to join the conference bridge */
	struct ast_taskprocessor *playback_queue;                         /*!< Queue for playing back bridge announcements and managing the announcer channel */
};

extern struct ao2_container *conference_bridges;

struct post_join_action {
	int (*func)(struct confbridge_user *user);
	AST_LIST_ENTRY(post_join_action) list;
};

/*! \brief The structure that represents a conference bridge user */
struct confbridge_user {
	struct confbridge_conference *conference;    /*!< Conference bridge they are participating in */
	struct bridge_profile b_profile;             /*!< The Bridge Configuration Profile */
	struct user_profile u_profile;               /*!< The User Configuration Profile */
	char menu_name[MAX_PROFILE_NAME];            /*!< The name of the DTMF menu assigned to this user */
	char name_rec_location[PATH_MAX];            /*!< Location of the User's name recorded file if it exists */
	struct ast_channel *chan;                    /*!< Asterisk channel participating */
	struct ast_bridge_features features;         /*!< Bridge features structure */
	struct ast_bridge_tech_optimizations tech_args; /*!< Bridge technology optimizations for talk detection */
	unsigned int suspended_moh;                  /*!< Count of active suspended MOH actions. */
	unsigned int muted:1;                        /*!< Has the user requested to be muted? */
	unsigned int kicked:1;                       /*!< User has been kicked from the conference */
	unsigned int playing_moh:1;                  /*!< MOH is currently being played to the user */
	unsigned int talking:1;                      /*!< User is currently talking */
	AST_LIST_HEAD_NOLOCK(, post_join_action) post_join_list; /*!< List of sounds to play after joining */;
	AST_LIST_ENTRY(confbridge_user) list;        /*!< Linked list information */
};

/*! \brief load confbridge.conf file */
int conf_load_config(void);

/*! \brief reload confbridge.conf file */
int conf_reload_config(void);

/*! \brief destroy the information loaded from the confbridge.conf file*/
void conf_destroy_config(void);

/*!
 * \brief find a user profile given a user profile's name and store
 * that profile in result structure.
 *
 * \param chan channel the user profile is requested for
 * \param user_profile_name name of the profile requested (optional)
 * \param result data contained by the user profile will be copied to this struct pointer
 *
 * \details If user_profile_name is not provided, this function will
 * check for the presence of a user profile set by the CONFBRIDGE
 * function on a channel datastore. If that doesn't exist, the
 * default_user profile is used.
 *
 * \retval user profile on success
 * \retval NULL on failure
 */
const struct user_profile *conf_find_user_profile(struct ast_channel *chan, const char *user_profile_name, struct user_profile *result);

/*!
 * \brief Find a bridge profile given a bridge profile's name and store
 * that profile in result structure.
 *
 * \param chan channel the bridge profile is requested for
 * \param bridge_profile_name name of the profile requested (optional)
 * \param result data contained by the bridge profile will be copied to this struct pointer
 *
 * \details If bridge_profile_name is not provided, this function will
 * check for the presence of a bridge profile set by the CONFBRIDGE
 * function on a channel datastore. If that doesn't exist, the
 * default_bridge profile is used.
 *
 * \retval bridge profile on success
 * \retval NULL on failure
 */
const struct bridge_profile *conf_find_bridge_profile(struct ast_channel *chan, const char *bridge_profile_name, struct bridge_profile *result);

/*!
 * \brief find a menu profile given a menu profile's name and apply
 * the menu in DTMF hooks.
 *
 * \param chan channel the menu profile is requested for
 * \param user user profile the menu is being applied to
 * \param menu_profile_name name of the profile requested (optional)
 *
 * \details If menu_profile_name is not provided, this function will
 * check for the presence of a menu profile set by the CONFBRIDGE
 * function on a channel datastore. If that doesn't exist, the
 * default_menu profile is used.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int conf_set_menu_to_user(struct ast_channel *chan, struct confbridge_user *user, const char *menu_profile_name);

/*!
 * \brief Destroy a bridge profile found by 'conf_find_bridge_profile'
 */
void conf_bridge_profile_destroy(struct bridge_profile *b_profile);

/*!
 * \brief copies a bridge profile
 * \note conf_bridge_profile_destroy must be called on the dst structure
 */
void conf_bridge_profile_copy(struct bridge_profile *dst, struct bridge_profile *src);

/*!
 * \brief Finds a menu_entry in a menu structure matched by DTMF sequence.
 *
 * \note the menu entry found must be destroyed using conf_menu_entry_destroy()
 *
 * \retval 1 success, entry is found and stored in result
 * \retval 0 failure, no entry found for given DTMF sequence
 */
int conf_find_menu_entry_by_sequence(const char *dtmf_sequence, struct conf_menu *menu, struct conf_menu_entry *result);

/*!
 * \brief Destroys and frees all the actions stored in a menu_entry structure
 */
void conf_menu_entry_destroy(struct conf_menu_entry *menu_entry);

/*!
 * \brief Once a DTMF sequence matches a sequence in the user's DTMF menu, this function will get
 * called to perform the menu action.
 *
 * \param bridge_channel Bridged channel this is involving
 * \param user the conference user to perform the action on.
 * \param menu_entry the menu entry that invoked this callback to occur.
 * \param menu an AO2 referenced pointer to the entire menu structure the menu_entry
 *        derived from.
 *
 * \note The menu_entry is a deep copy of the entry found in the menu structure.  This allows
 * for the menu_entry to be accessed without requiring the menu lock.  If the menu must
 * be accessed, the menu lock must be held.  Reference counting of the menu structure is
 * handled outside of the scope of this function.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int conf_handle_dtmf(
	struct ast_bridge_channel *bridge_channel,
	struct confbridge_user *user,
	struct conf_menu_entry *menu_entry,
	struct conf_menu *menu);


/*! \brief Looks to see if sound file is stored in bridge profile sounds, if not
 *  default sound is provided.*/
const char *conf_get_sound(enum conf_sounds sound, struct bridge_profile_sounds *custom_sounds);

int func_confbridge_helper(struct ast_channel *chan, const char *cmd, char *data, const char *value);

/*!
 * \brief Play sound file into conference bridge
 *
 * \param conference The conference bridge to play sound file into
 * \param filename Sound file to play
 *
 * \retval 0 success
 * \retval -1 failure
 */
int play_sound_file(struct confbridge_conference *conference, const char *filename);

/*!
 * \brief Play sound file into conference bridge asynchronously
 *
 * If the initiator parameter is non-NULL, then the playback will wait for
 * that initiator channel to get back in the bridge before playing the sound
 * file. This way, the initiator has no danger of hearing a "clipped" file.
 *
 * \param conference The conference bridge to play sound file into
 * \param filename Sound file to play
 * \param initiator Channel that initiated playback.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int async_play_sound_file(struct confbridge_conference *conference, const char *filename,
	struct ast_channel *initiator);

/*!
 * \brief Indicate the initiator of an async sound file is ready for it to play.
 *
 * When playing an async sound file, the initiator is typically either out of the bridge
 * or not in a position to hear the queued announcement. This function lets the announcement
 * thread know that the initiator is now ready for the sound to play.
 *
 * If an async announcement was queued and no initiator channel was provided, then this is
 * a no-op
 *
 * \param chan The channel that initiated the async announcement
 */
void async_play_sound_ready(struct ast_channel *chan);

/*! \brief Callback to be called when the conference has become empty
 * \param conference The conference bridge
 */
void conf_ended(struct confbridge_conference *conference);

/*!
 * \brief Update the actual mute status of the user and set it on the bridge.
 *
 * \param user User to update the mute status.
 */
void conf_update_user_mute(struct confbridge_user *user);

/*!
 * \brief Stop MOH for the conference user.
 *
 * \param user Conference user to stop MOH on.
 */
void conf_moh_stop(struct confbridge_user *user);

/*!
 * \brief Start MOH for the conference user.
 *
 * \param user Conference user to start MOH on.
 */
void conf_moh_start(struct confbridge_user *user);

/*! \brief Attempt to mute/play MOH to the only user in the conference if they require it
 * \param conference A conference bridge containing a single user
 */
void conf_mute_only_active(struct confbridge_conference *conference);

/*! \brief Callback to execute any time we transition from zero to one active users
 * \param conference The conference bridge with a single active user joined
 */
void conf_handle_first_join(struct confbridge_conference *conference);

/*! \brief Handle actions every time a waitmarked user joins w/o a marked user present
 * \param user The waitmarked user
 * \retval 0 success
 * \retval -1 failure
 */
int conf_handle_inactive_waitmarked(struct confbridge_user *user);

/*! \brief Handle actions whenever an user joins an empty conference
 *
 * \param user The user
 */
int conf_handle_only_person(struct confbridge_user *user);

/*! \brief Handle when a conference moves to having more than one active participant
 * \param conference The conference bridge with more than one active participant
 */
void conf_handle_second_active(struct confbridge_conference *conference);

/*! \brief Add a conference bridge user as an unmarked active user of the conference
 * \param conference The conference bridge to add the user to
 * \param user The conference bridge user to add to the conference
 */
void conf_add_user_active(struct confbridge_conference *conference, struct confbridge_user *user);

/*! \brief Add a conference bridge user as a marked active user of the conference
 * \param conference The conference bridge to add the user to
 * \param user The conference bridge user to add to the conference
 */
void conf_add_user_marked(struct confbridge_conference *conference, struct confbridge_user *user);

/*! \brief Add a conference bridge user as an waiting user of the conference
 * \param conference The conference bridge to add the user to
 * \param user The conference bridge user to add to the conference
 */
void conf_add_user_waiting(struct confbridge_conference *conference, struct confbridge_user *user);

/*! \brief Remove a conference bridge user from the unmarked active conference users in the conference
 * \param conference The conference bridge to remove the user from
 * \param user The conference bridge user to remove from the conference
 */
void conf_remove_user_active(struct confbridge_conference *conference, struct confbridge_user *user);

/*! \brief Remove a conference bridge user from the marked active conference users in the conference
 * \param conference The conference bridge to remove the user from
 * \param user The conference bridge user to remove from the conference
 */
void conf_remove_user_marked(struct confbridge_conference *conference, struct confbridge_user *user);

/*! \brief Remove a conference bridge user from the waiting conference users in the conference
 * \param conference The conference bridge to remove the user from
 * \param user The conference bridge user to remove from the conference
 */
void conf_remove_user_waiting(struct confbridge_conference *conference, struct confbridge_user *user);

/*! \brief Queue a function to run with the given conference bridge user as an argument once the state transition is complete
 * \param user The conference bridge user to pass to the function
 * \param func The function to queue
 * \retval 0 success
 * \retval non-zero failure
 */
int conf_add_post_join_action(struct confbridge_user *user, int (*func)(struct confbridge_user *user));

/*!
 * \since 12.0
 * \brief get the confbridge start stasis message type
 *
 * \retval stasis message type for confbridge start messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_start_type(void);

/*!
 * \since 12.0
 * \brief get the confbridge end stasis message type
 *
 * \retval stasis message type for confbridge end messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_end_type(void);

/*!
 * \since 12.0
 * \brief get the confbridge join stasis message type
 *
 * \retval stasis message type for confbridge join messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_join_type(void);

/*!
 * \since 12.0
 * \brief get the confbridge leave stasis message type
 *
 * \retval stasis message type for confbridge leave messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_leave_type(void);

/*!
 * \since 12.0
 * \brief get the confbridge start_record stasis message type
 *
 * \retval stasis message type for confbridge start_record messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_start_record_type(void);

/*!
 * \since 12.0
 * \brief get the confbridge stop_record stasis message type
 *
 * \retval stasis message type for confbridge stop_record messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_stop_record_type(void);

/*!
 * \since 12.0
 * \brief get the confbridge mute stasis message type
 *
 * \retval stasis message type for confbridge mute messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_mute_type(void);

/*!
 * \since 12.0
 * \brief get the confbridge unmute stasis message type
 *
 * \retval stasis message type for confbridge unmute messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_unmute_type(void);

/*!
 * \since 12.0
 * \brief get the confbridge talking stasis message type
 *
 * \retval stasis message type for confbridge talking messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_talking_type(void);

/*!
 * \since 15.5
 * \brief get the confbridge welcome stasis message type
 *
 * \retval stasis message type for confbridge welcome messages if it's available
 * \retval NULL if it isn't
 */
struct stasis_message_type *confbridge_welcome_type(void);

/*!
 * \since 15.5
 * \brief Get the string representation of a confbridge stasis message type
 *
 * \param event_type The confbridge event type such as 'confbridge_welcome_type()'
 *
 * \retval The string representation of the message type
 * \retval "unknown" if not found
 */
const char *confbridge_event_type_to_string(struct stasis_message_type *event_type);

/*!
 * \since 12.0
 * \brief register stasis message routers to handle manager events for confbridge messages
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int manager_confbridge_init(void);

/*!
 * \since 12.0
 * \brief unregister stasis message routers to handle manager events for confbridge messages
 */
void manager_confbridge_shutdown(void);

/*!
 * \brief Get ConfBridge record channel technology struct.
 * \since 12.0.0
 *
 * \return ConfBridge record channel technology.
 */
struct ast_channel_tech *conf_record_get_tech(void);

/*!
 * \brief Get ConfBridge announce channel technology struct.
 * \since 12.0.0
 *
 * \return ConfBridge announce channel technology.
 */
struct ast_channel_tech *conf_announce_get_tech(void);

/*!
 * \brief Push the announcer channel into the conference.
 * \since 12.0.0
 *
 * \param ast Either channel in the announcer channel pair.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
int conf_announce_channel_push(struct ast_channel *ast);

/*!
 * \brief Find a confbridge by name.
 * \since 13.22.0
 * \since 15.5.0
 *
 * \param conference_name The name to search for
 *
 * \return ConfBridge (which must be unreffed) or NULL.
 */
struct confbridge_conference *conf_find_bridge(const char *conference_name);

/*!
 * \brief Send events to bridge participants.
 * \since 15.7
 * \since 16.1
 *
 * \param conference The conference bridge
 * \param chan The channel triggering the action
 * \param msg The stasis message describing the event
 */
void conf_send_event_to_participants(struct confbridge_conference *conference,
	struct ast_channel *chan, struct stasis_message *msg);

/*!
 * \brief Create join/leave events for attended transfers
 * \since 13.28
 * \since 16.5
 *
 * \param msg The attended transfer stasis message
 *
 */
void confbridge_handle_atxfer(struct ast_attended_transfer_message *msg);

#endif
