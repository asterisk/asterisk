/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Copyright (C) 2005-2006, Digium, Inc.
 *
 * Matthew A. Nicholson <mnicholson@digium.com>
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

/*! \brief SMDI message desk message queue. */
struct ast_smdi_md_queue {
	ASTOBJ_CONTAINER_COMPONENTS(struct ast_smdi_md_message);
};

/*! \brief SMDI message waiting indicator message queue. */
struct ast_smdi_mwi_queue {
	ASTOBJ_CONTAINER_COMPONENTS(struct ast_smdi_mwi_message);
};

/*! 
 * \brief SMDI interface structure.
 *
 * The ast_smdi_interface structure holds information on a serial port that
 * should be monitored for SMDI activity.  The structure contains a message
 * queue of messages that have been recieved on the interface.
 */
struct ast_smdi_interface {
	ASTOBJ_COMPONENTS_FULL(struct ast_smdi_interface, SMDI_MAX_FILENAME_LEN, 1);
	struct ast_smdi_md_queue md_q;
	struct ast_smdi_mwi_queue mwi_q;
	FILE *file;
	int fd;
	pthread_t thread;
	struct termios mode;
	int msdstrip;
	long msg_expiry;
};


/* MD message queue functions */
struct ast_smdi_md_message *ast_smdi_md_message_pop(struct ast_smdi_interface *iface);
struct ast_smdi_md_message *ast_smdi_md_message_wait(struct ast_smdi_interface *iface, int timeout);
void ast_smdi_md_message_putback(struct ast_smdi_interface *iface, struct ast_smdi_md_message *msg);

/* MWI message queue functions */
struct ast_smdi_mwi_message *ast_smdi_mwi_message_pop(struct ast_smdi_interface *iface);
struct ast_smdi_mwi_message *ast_smdi_mwi_message_wait(struct ast_smdi_interface *iface, int timeout);
void ast_smdi_mwi_message_putback(struct ast_smdi_interface *iface, struct ast_smdi_mwi_message *msg);

struct ast_smdi_interface *ast_smdi_interface_find(const char *iface_name);

/* MWI functions */
int ast_smdi_mwi_set(struct ast_smdi_interface *iface, const char *mailbox);
int ast_smdi_mwi_unset(struct ast_smdi_interface *iface, const char *mailbox);

void ast_smdi_md_message_destroy(struct ast_smdi_md_message *msg);
void ast_smdi_mwi_message_destroy(struct ast_smdi_mwi_message *msg);

void ast_smdi_interface_destroy(struct ast_smdi_interface *iface);

#endif /* !ASTERISK_SMDI_H */
