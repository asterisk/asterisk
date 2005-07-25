/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Call Detail Record API 
 * 
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License.
 *
 * Includes code and algorithms from the Zapata library.
 *
 */

#ifndef _ASTERISK_CDR_H
#define _ASTERISK_CDR_H

#include <sys/time.h>
#define AST_CDR_FLAG_KEEP_VARS			(1 << 0)
#define AST_CDR_FLAG_POSTED			(1 << 1)
#define AST_CDR_FLAG_LOCKED			(1 << 2)
#define AST_CDR_FLAG_CHILD			(1 << 3)
#define AST_CDR_FLAG_POST_DISABLED		(1 << 4)

#define AST_CDR_NOANSWER			(1 << 0)
#define AST_CDR_BUSY				(1 << 1)
#define AST_CDR_ANSWERED			(1 << 2)
#define AST_CDR_FAILED				(1 << 3)

/*! AMA Flags */
#define AST_CDR_OMIT				(1)
#define AST_CDR_BILLING				(2)
#define AST_CDR_DOCUMENTATION			(3)

#define AST_MAX_USER_FIELD			256
#define AST_MAX_ACCOUNT_CODE			20

/* Include channel.h after relevant declarations it will need */
#include "asterisk/channel.h"

struct ast_channel;
AST_LIST_HEAD(varshead,ast_var_t);

/*! Responsible for call detail data */
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
	int duration;				
	/*! Total time call is up, in seconds */
	int billsec;				
	/*! What happened to the call */
	int disposition;			
	/*! What flags to use */
	int amaflags;				
	/*! What account number to use */
	char accountcode[AST_MAX_ACCOUNT_CODE];			
	/*! flags */
	unsigned int flags;				
	/* Unique Channel Identifier */
	char uniqueid[32];
	/* User field */
	char userfield[AST_MAX_USER_FIELD];

	/* A linked list for variables */
	struct varshead varshead;

	struct ast_cdr *next;
};

extern void ast_cdr_getvar(struct ast_cdr *cdr, const char *name, char **ret, char *workspace, int workspacelen, int recur);
extern int ast_cdr_setvar(struct ast_cdr *cdr, const char *name, const char *value, int recur);
extern int ast_cdr_serialize_variables(struct ast_cdr *cdr, char *buf, size_t size, char delim, char sep, int recur);
extern void ast_cdr_free_vars(struct ast_cdr *cdr, int recur);
extern int ast_cdr_copy_vars(struct ast_cdr *to_cdr, struct ast_cdr *from_cdr);

typedef int (*ast_cdrbe)(struct ast_cdr *cdr);

/*! Allocate a record */
/*! 
 * Returns a malloc'd ast_cdr structure, returns NULL on error (malloc failure)
 */
extern struct ast_cdr *ast_cdr_alloc(void);

/*! Duplicate a record */
/*! 
 * Returns a malloc'd ast_cdr structure, returns NULL on error (malloc failure)
 */
extern struct ast_cdr *ast_cdr_dup(struct ast_cdr *cdr);

/*! Free a record */
/* \param cdr ast_cdr structure to free
 * Returns nothing important
 */
extern void ast_cdr_free(struct ast_cdr *cdr);

/*! Initialize based on a channel */
/*! 
 * \param cdr Call Detail Record to use for channel
 * \param chan Channel to bind CDR with
 * Initializes a CDR and associates it with a particular channel
 * Return is negligible.  (returns 0 by default)
 */
extern int ast_cdr_init(struct ast_cdr *cdr, struct ast_channel *chan);

/*! Initialize based on a channel */
/*! 
 * \param cdr Call Detail Record to use for channel
 * \param chan Channel to bind CDR with
 * Initializes a CDR and associates it with a particular channel
 * Return is negligible.  (returns 0 by default)
 */
extern int ast_cdr_setcid(struct ast_cdr *cdr, struct ast_channel *chan);

/*! Register a CDR handling engine */
/*!
 * \param name name associated with the particular CDR handler
 * \param desc description of the CDR handler
 * \param be function pointer to a CDR handler
 * Used to register a Call Detail Record handler.
 * Returns -1 on error, 0 on success.
 */
extern int ast_cdr_register(char *name, char *desc, ast_cdrbe be);

/*! Unregister a CDR handling engine */
/*!
 * \param name name of CDR handler to unregister
 * Unregisters a CDR by it's name
 */
extern void ast_cdr_unregister(char *name);

/*! Start a call */
/*!
 * \param cdr the cdr you wish to associate with the call
 * Starts all CDR stuff necessary for monitoring a call
 * Returns nothing important
 */
extern void ast_cdr_start(struct ast_cdr *cdr);

/*! Answer a call */
/*!
 * \param cdr the cdr you wish to associate with the call
 * Starts all CDR stuff necessary for doing CDR when answering a call
 */
extern void ast_cdr_answer(struct ast_cdr *cdr);

/*! Busy a call */
/*!
 * \param cdr the cdr you wish to associate with the call
 * Returns nothing important
 */
extern void ast_cdr_busy(struct ast_cdr *cdr);

/*! Fail a call */
/*!
 * \param cdr the cdr you wish to associate with the call
 * Returns nothing important
 */
extern void ast_cdr_failed(struct ast_cdr *cdr);

/*! Save the result of the call based on the AST_CAUSE_* */
/*!
 * \param cdr the cdr you wish to associate with the call
 * Returns nothing important
 * \param cause the AST_CAUSE_*
 */
extern int ast_cdr_disposition(struct ast_cdr *cdr, int cause);
	
/*! End a call */
/*!
 * \param cdr the cdr you have associated the call with
 * Registers the end of call time in the cdr structure.
 * Returns nothing important
 */
extern void ast_cdr_end(struct ast_cdr *cdr);

/*! Detaches the detail record for posting (and freeing) either now or at a
 * later time in bulk with other records during batch mode operation */
/*! 
 * \param cdr Which CDR to detach from the channel thread
 * Prevents the channel thread from blocking on the CDR handling
 * Returns nothing
 */
extern void ast_cdr_detach(struct ast_cdr *cdr);

/*! Spawns (possibly) a new thread to submit a batch of CDRs to the backend engines */
/*!
 * \param shutdown Whether or not we are shutting down
 * Blocks the asterisk shutdown procedures until the CDR data is submitted.
 * Returns nothing
 */
extern void ast_cdr_submit_batch(int shutdown);

/*! Set the destination channel, if there was one */
/*!
 * \param cdr Which cdr it's applied to
 * Sets the destination channel the CDR is applied to
 * Returns nothing
 */
extern void ast_cdr_setdestchan(struct ast_cdr *cdr, char *chan);

/*! Set the last executed application */
/*!
 * \param cdr which cdr to act upon
 * \param app the name of the app you wish to change it to
 * \param data the data you want in the data field of app you set it to
 * Changes the value of the last executed app
 * Returns nothing
 */
extern void ast_cdr_setapp(struct ast_cdr *cdr, char *app, char *data);

/*! Convert a string to a detail record AMA flag */
/*!
 * \param flag string form of flag
 * Converts the string form of the flag to the binary form.
 * Returns the binary form of the flag
 */
extern int ast_cdr_amaflags2int(const char *flag);

/*! Disposition to a string */
/*!
 * \param flag input binary form
 * Converts the binary form of a disposition to string form.
 * Returns a pointer to the string form
 */
extern char *ast_cdr_disp2str(int disposition);

/*! Reset the detail record, optionally posting it first */
/*!
 * \param cdr which cdr to act upon
 * \param flags |AST_CDR_FLAG_POSTED whether or not to post the cdr first before resetting it
 *              |AST_CDR_FLAG_LOCKED whether or not to reset locked CDR's
 */
extern void ast_cdr_reset(struct ast_cdr *cdr, int flags);

/*! Flags to a string */
/*!
 * \param flags binary flag
 * Converts binary flags to string flags
 * Returns string with flag name
 */
extern char *ast_cdr_flags2str(int flags);

extern int ast_cdr_setaccount(struct ast_channel *chan, const char *account);
extern int ast_cdr_setamaflags(struct ast_channel *chan, const char *amaflags);


extern int ast_cdr_setuserfield(struct ast_channel *chan, const char *userfield);
extern int ast_cdr_appenduserfield(struct ast_channel *chan, const char *userfield);


/* Update CDR on a channel */
extern int ast_cdr_update(struct ast_channel *chan);


extern int ast_default_amaflags;

extern char ast_default_accountcode[AST_MAX_ACCOUNT_CODE];

extern struct ast_cdr *ast_cdr_append(struct ast_cdr *cdr, struct ast_cdr *newcdr);

/*! Reload the configuration file cdr.conf and start/stop CDR scheduling thread */
extern void ast_cdr_engine_reload(void);

/*! Load the configuration file cdr.conf and possibly start the CDR scheduling thread */
extern int ast_cdr_engine_init(void);

/*! Submit any remaining CDRs and prepare for shutdown */
extern void ast_cdr_engine_term(void);

#endif /* _ASTERISK_CDR_H */
