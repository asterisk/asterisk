/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Call Parking and Pickup API 
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Includes code and algorithms from the Zapata library.
 *
 */

#ifndef _AST_FEATURES_H
#define _AST_FEATURES_H

#define FEATURE_MAX_LEN		11
#define FEATURE_APP_LEN		64
#define FEATURE_APP_ARGS_LEN	256
#define FEATURE_SNAME_LEN	32
#define FEATURE_EXTEN_LEN	32

/* main call feature structure */
struct ast_call_feature {
	int feature_mask;
	char *fname;
	char sname[FEATURE_SNAME_LEN];
	char exten[FEATURE_MAX_LEN];
	char default_exten[FEATURE_MAX_LEN];
	int (*operation)(struct ast_channel *chan, struct ast_channel *peer, struct ast_bridge_config *config, char *code, int sense);
	unsigned int flags;
	char app[FEATURE_APP_LEN];		
	char app_args[FEATURE_APP_ARGS_LEN];
	AST_LIST_ENTRY(ast_call_feature) feature_entry;
};



/*! Park a call and read back parked location */
/*! \param chan the channel to actually be parked
    \param host the channel which will have the parked location read to
	Park the channel chan, and read back the parked location to the
	host.  If the call is not picked up within a specified period of
	time, then the call will return to the last step that it was in 
	(in terms of exten, priority and context)
	\param timeout is a timeout in milliseconds
	\param extout is a parameter to an int that will hold the parked location, or NULL if you want
*/
extern int ast_park_call(struct ast_channel *chan, struct ast_channel *host, int timeout, int *extout);
/*! Park a call via a masqueraded channel */
/*! \param rchan the real channel to be parked
    \param host the channel to have the parking read to
	Masquerade the channel rchan into a new, empty channel which is then
	parked with ast_park_call
	\param timeout is a timeout in milliseconds
	\param extout is a parameter to an int that will hold the parked location, or NULL if you want
*/
extern int ast_masq_park_call(struct ast_channel *rchan, struct ast_channel *host, int timeout, int *extout);

/*! Determine system parking extension */
/*! Returns the call parking extension for drivers that provide special
    call parking help */
extern char *ast_parking_ext(void);
extern char *ast_pickup_ext(void);

/*! Bridge a call, optionally allowing redirection */

extern int ast_bridge_call(struct ast_channel *chan, struct ast_channel *peer,struct ast_bridge_config *config);

extern int ast_pickup_call(struct ast_channel *chan);

/*! register new feature into feature_set 
   \param feature an ast_call_feature object which contains a keysequence
   and a callback function which is called when this keysequence is pressed
   during a call. */
extern void ast_register_feature(struct ast_call_feature *feature);

/*! unregister feature from feature_set
    \param feature the ast_call_feature object which was registered before*/
extern void ast_unregister_feature(struct ast_call_feature *feature);

     
#endif /* _AST_FEATURES_H */
