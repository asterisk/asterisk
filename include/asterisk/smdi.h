/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 2005-2008, Digium, Inc.
 *
 * Matthew A. Nicholson <mnicholson@digium.com>
 * Russell Bryant <russell@digium.com>
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
 * \brief SMDI support for Asterisk.
 * \author Matthew A. Nicholson <mnicholson@digium.com>
 * \author Russell Bryant <russell@digium.com>
 */


/* C is simply a ego booster for those who want to do objects the hard way. */


#ifndef ASTERISK_SMDI_H
#define ASTERISK_SMDI_H

#include <termios.h>
#include <time.h>

#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/astobj.h"

#define SMDI_MESG_DESK_NUM_LEN 3
#define SMDI_MESG_DESK_TERM_LEN 4
#define SMDI_MWI_FAIL_CAUSE_LEN 3
#define SMDI_MAX_STATION_NUM_LEN 10
#define SMDI_MAX_FILENAME_LEN 256

/*!
 * \brief An SMDI message waiting indicator message.
 *
 * The ast_smdi_mwi_message structure contains the parsed out parts of an smdi
 * message.  Each ast_smdi_interface structure has a message queue consisting
 * ast_smdi_mwi_message structures. 
 */
struct ast_smdi_mwi_message {
	ASTOBJ_COMPONENTS(struct ast_smdi_mwi_message);
	char fwd_st[SMDI_MAX_STATION_NUM_LEN + 1];		/* forwarding station number */
	char cause[SMDI_MWI_FAIL_CAUSE_LEN + 1];		/* the type of failure */
	struct timeval timestamp;				/* a timestamp for the message */
};

/*!
 * \brief An SMDI message desk message.
 *
 * The ast_smdi_md_message structure contains the parsed out parts of an smdi
 * message.  Each ast_smdi_interface structure has a message queue consisting
 * ast_smdi_md_message structures. 
 */
struct ast_smdi_md_message {
	ASTOBJ_COMPONENTS(struct ast_smdi_md_message);
	char mesg_desk_num[SMDI_MESG_DESK_NUM_LEN + 1];		/* message desk number */
	char mesg_desk_term[SMDI_MESG_DESK_TERM_LEN + 1];	/* message desk terminal */
	char fwd_st[SMDI_MAX_STATION_NUM_LEN + 1];		/* forwarding station number */
	char calling_st[SMDI_MAX_STATION_NUM_LEN + 1];		/* calling station number */
	char type;						/* the type of the call */
	struct timeval timestamp;				/* a timestamp for the message */
};

/*! 
 * \brief SMDI interface structure.
 *
 * The ast_smdi_interface structure holds information on a serial port that
 * should be monitored for SMDI activity.  The structure contains a message
 * queue of messages that have been received on the interface.
 */
struct ast_smdi_interface;

void ast_smdi_interface_unref(struct ast_smdi_interface *iface) attribute_weak;

/*! 
 * \brief Get the next SMDI message from the queue.
 * \param iface a pointer to the interface to use.
 *
 * This function pulls the first unexpired message from the SMDI message queue
 * on the specified interface.  It will purge all expired SMDI messages before
 * returning.
 *
 * \return the next SMDI message, or NULL if there were no pending messages.
 */
struct ast_smdi_md_message *ast_smdi_md_message_pop(struct ast_smdi_interface *iface) attribute_weak;

/*!
 * \brief Get the next SMDI message from the queue.
 * \param iface a pointer to the interface to use.
 * \param timeout the time to wait before returning in milliseconds.
 *
 * This function pulls a message from the SMDI message queue on the specified
 * interface.  If no message is available this function will wait the specified
 * amount of time before returning.
 *
 * \return the next SMDI message, or NULL if there were no pending messages and
 * the timeout has expired.
 */
struct ast_smdi_md_message *ast_smdi_md_message_wait(struct ast_smdi_interface *iface, int timeout) attribute_weak;

/*!
 * \brief Put an SMDI message back in the front of the queue.
 * \param iface a pointer to the interface to use.
 * \param md_msg a pointer to the message to use.
 *
 * This function puts a message back in the front of the specified queue.  It
 * should be used if a message was popped but is not going to be processed for
 * some reason, and the message needs to be returned to the queue.
 */
void ast_smdi_md_message_putback(struct ast_smdi_interface *iface, struct ast_smdi_md_message *msg) attribute_weak;

/*!
 * \brief Get the next SMDI message from the queue.
 * \param iface a pointer to the interface to use.
 *
 * This function pulls the first unexpired message from the SMDI message queue
 * on the specified interface.  It will purge all expired SMDI messages before
 * returning.
 *
 * \return the next SMDI message, or NULL if there were no pending messages.
 */
struct ast_smdi_mwi_message *ast_smdi_mwi_message_pop(struct ast_smdi_interface *iface) attribute_weak;

/*!
 * \brief Get the next SMDI message from the queue.
 * \param iface a pointer to the interface to use.
 * \param timeout the time to wait before returning in milliseconds.
 *
 * This function pulls a message from the SMDI message queue on the specified
 * interface.  If no message is available this function will wait the specified
 * amount of time before returning.
 *
 * \return the next SMDI message, or NULL if there were no pending messages and
 * the timeout has expired.
 */
struct ast_smdi_mwi_message *ast_smdi_mwi_message_wait(struct ast_smdi_interface *iface, int timeout) attribute_weak;
struct ast_smdi_mwi_message *ast_smdi_mwi_message_wait_station(struct ast_smdi_interface *iface, int
	timeout, const char *station) attribute_weak;

/*!
 * \brief Put an SMDI message back in the front of the queue.
 * \param iface a pointer to the interface to use.
 * \param mwi_msg a pointer to the message to use.
 *
 * This function puts a message back in the front of the specified queue.  It
 * should be used if a message was popped but is not going to be processed for
 * some reason, and the message needs to be returned to the queue.
 */
void ast_smdi_mwi_message_putback(struct ast_smdi_interface *iface, struct ast_smdi_mwi_message *msg) attribute_weak;

/*!
 * \brief Find an SMDI interface with the specified name.
 * \param iface_name the name/port of the interface to search for.
 *
 * \return a pointer to the interface located or NULL if none was found.  This
 * actually returns an ASTOBJ reference and should be released using
 * #ASTOBJ_UNREF(iface, ast_smdi_interface_destroy).
 */
struct ast_smdi_interface *ast_smdi_interface_find(const char *iface_name) attribute_weak;

/*!
 * \brief Set the MWI indicator for a mailbox.
 * \param iface the interface to use.
 * \param mailbox the mailbox to use.
 */
int ast_smdi_mwi_set(struct ast_smdi_interface *iface, const char *mailbox) attribute_weak;

/*! 
 * \brief Unset the MWI indicator for a mailbox.
 * \param iface the interface to use.
 * \param mailbox the mailbox to use.
 */
int ast_smdi_mwi_unset(struct ast_smdi_interface *iface, const char *mailbox) attribute_weak;

/*! \brief ast_smdi_md_message destructor. */
void ast_smdi_md_message_destroy(struct ast_smdi_md_message *msg) attribute_weak;

/*! \brief ast_smdi_mwi_message destructor. */
void ast_smdi_mwi_message_destroy(struct ast_smdi_mwi_message *msg) attribute_weak;

#endif /* !ASTERISK_SMDI_H */
