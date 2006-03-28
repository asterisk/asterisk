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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#include <sys/time.h>
#include <time.h>
#include <ctype.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/smdi.h"
#include "asterisk/config.h"
#include "asterisk/astobj.h"
#include "asterisk/io.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"

/* Message expiry time in milliseconds */
#define SMDI_MSG_EXPIRY_TIME	30000 /* 30 seconds */

static const char tdesc[] = "Asterisk Simplified Message Desk Interface (SMDI) Module";
static const char config_file[] = "smdi.conf";

static void ast_smdi_md_message_push(struct ast_smdi_interface *iface, struct ast_smdi_md_message *msg);
static void ast_smdi_mwi_message_push(struct ast_smdi_interface *iface, struct ast_smdi_mwi_message *msg);

static void *smdi_read(void *iface_p);
static int smdi_load(int reload);

/* Use count stuff */

LOCAL_USER_DECL;

/*! \brief SMDI interface container. */
struct ast_smdi_interface_container {
	ASTOBJ_CONTAINER_COMPONENTS(struct ast_smdi_interface);
} smdi_ifaces;

/*! 
 * \internal
 * \brief Push an SMDI message to the back of an interface's message queue.
 * \param iface a pointer to the interface to use.
 * \param md_msg a pointer to the message to use.
 */
static void ast_smdi_md_message_push(struct ast_smdi_interface *iface, struct ast_smdi_md_message *md_msg)
{
	ASTOBJ_CONTAINER_LINK_END(&iface->md_q, md_msg);
}

/*!
 * \internal
 * \brief Push an SMDI message to the back of an interface's message queue.
 * \param iface a pointer to the interface to use.
 * \param mwi_msg a pointer to the message to use.
 */
static void ast_smdi_mwi_message_push(struct ast_smdi_interface *iface, struct ast_smdi_mwi_message *mwi_msg)
{
	ASTOBJ_CONTAINER_LINK_END(&iface->mwi_q, mwi_msg);
}

/*!
 * \brief Set the MWI indicator for a mailbox.
 * \param iface the interface to use.
 * \param mailbox the mailbox to use.
 */
int ast_smdi_mwi_set(struct ast_smdi_interface *iface, const char *mailbox)
{
	FILE *file;
	int i;
	
	file = fopen(iface->name, "w");
	if(!file) {
		ast_log(LOG_ERROR, "Error opening SMDI interface %s (%s) for writing\n", iface->name, strerror(errno));
		return 1;
	}	

	ASTOBJ_WRLOCK(iface);

	fprintf(file, "OP:MWI ");

	for(i = 0; i < iface->msdstrip; i++)
	   fprintf(file, "0");

	fprintf(file, "%s!\x04", mailbox);
	fclose(file);

	ASTOBJ_UNLOCK(iface);
	ast_log(LOG_DEBUG, "Sent MWI set message for %s on %s\n", mailbox, iface->name);
	return 0;
}

/*! 
 * \brief Unset the MWI indicator for a mailbox.
 * \param iface the interface to use.
 * \param mailbox the mailbox to use.
 */
int ast_smdi_mwi_unset(struct ast_smdi_interface *iface, const char *mailbox)
{
	FILE *file;
	int i;
	
	file = fopen(iface->name, "w");
	if(!file) {
		ast_log(LOG_ERROR, "Error opening SMDI interface %s (%s) for writing\n", iface->name, strerror(errno));
		return 1;
	}	

	ASTOBJ_WRLOCK(iface);

	fprintf(file, "RMV:MWI ");

	for(i = 0; i < iface->msdstrip; i++)
	   fprintf(file, "0");

	fprintf(file, "%s!\x04", mailbox);
	fclose(file);

	ASTOBJ_UNLOCK(iface);
	ast_log(LOG_DEBUG, "Sent MWI unset message for %s on %s", mailbox, iface->name);
	return 0;
}

/*!
 * \brief Put an SMDI message back in the front of the queue.
 * \param iface a pointer to the interface to use.
 * \param msg a pointer to the message to use.
 *
 * This function puts a message back in the front of the specified queue.  It
 * should be used if a message was popped but is not going to be processed for
 * some reason, and the message needs to be returned to the queue.
 */
void ast_smdi_md_message_putback(struct ast_smdi_interface *iface, struct ast_smdi_md_message *md_msg)
{
	ASTOBJ_CONTAINER_LINK_START(&iface->md_q, md_msg);
}

/*!
 * \brief Put an SMDI message back in the front of the queue.
 * \param iface a pointer to the interface to use.
 * \param msg a pointer to the message to use.
 *
 * This function puts a message back in the front of the specified queue.  It
 * should be used if a message was popped but is not going to be processed for
 * some reason, and the message needs to be returned to the queue.
 */
void ast_smdi_mwi_message_putback(struct ast_smdi_interface *iface, struct ast_smdi_mwi_message *mwi_msg)
{
	ASTOBJ_CONTAINER_LINK_START(&iface->mwi_q, mwi_msg);
}

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
struct ast_smdi_md_message *ast_smdi_md_message_pop(struct ast_smdi_interface *iface)
{
	struct ast_smdi_md_message *md_msg = ASTOBJ_CONTAINER_UNLINK_START(&iface->md_q);
	struct timeval now;
	long elapsed = 0;

	/* purge old messages */
	now = ast_tvnow();
	while (md_msg) {
		elapsed = ast_tvdiff_ms(now, md_msg->timestamp);

		if (elapsed > iface->msg_expiry) {
			/* found an expired message */
			ASTOBJ_UNREF(md_msg, ast_smdi_md_message_destroy);
			ast_log(LOG_NOTICE, "Purged expired message from %s SMDI MD message queue.  Message was %ld milliseconds too old.",
				iface->name, elapsed - iface->msg_expiry);
			md_msg = ASTOBJ_CONTAINER_UNLINK_START(&iface->md_q);
		}
		else {
			/* good message, return it */
			break;
		}
	}

	return md_msg;
}

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
extern struct ast_smdi_md_message *ast_smdi_md_message_wait(struct ast_smdi_interface *iface, int timeout)
{
	struct timeval start;
	long diff = 0;
	struct ast_smdi_md_message *msg;

	start = ast_tvnow();
	while (diff < timeout) {

		if ((msg = ast_smdi_md_message_pop(iface)))
			return msg;

		/* check timeout */
		diff = ast_tvdiff_ms(ast_tvnow(), start);
	}

	return (ast_smdi_md_message_pop(iface));
}

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
extern struct ast_smdi_mwi_message *ast_smdi_mwi_message_pop(struct ast_smdi_interface *iface)
{
	struct ast_smdi_mwi_message *mwi_msg = ASTOBJ_CONTAINER_UNLINK_START(&iface->mwi_q);
	struct timeval now;
	long elapsed = 0;

	/* purge old messages */
	now = ast_tvnow();
	while (mwi_msg)	{
		elapsed = ast_tvdiff_ms(now, mwi_msg->timestamp);

		if (elapsed > iface->msg_expiry) {
			/* found an expired message */
			ASTOBJ_UNREF(mwi_msg, ast_smdi_mwi_message_destroy);
			ast_log(LOG_NOTICE, "Purged expired message from %s SMDI MWI message queue.  Message was %ld milliseconds too old.",
				iface->name, elapsed - iface->msg_expiry);
			mwi_msg = ASTOBJ_CONTAINER_UNLINK_START(&iface->mwi_q);
		}
		else {
			/* good message, return it */
			break;
		}
	}

	return mwi_msg;
}

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
extern struct ast_smdi_mwi_message *ast_smdi_mwi_message_wait(struct ast_smdi_interface *iface, int timeout)
{
	struct timeval start;
	long diff = 0;
	struct ast_smdi_mwi_message *msg;

	start = ast_tvnow();
	while (diff < timeout) {

		if ((msg = ast_smdi_mwi_message_pop(iface)))
			return msg;

		/* check timeout */
		diff = ast_tvdiff_ms(ast_tvnow(), start);
	}

	return (ast_smdi_mwi_message_pop(iface));
}

/*!
 * \brief Find an SMDI interface with the specified name.
 * \param iface_name the name/port of the interface to search for.
 *
 * \return a pointer to the interface located or NULL if none was found.  This
 * actually returns an ASTOBJ reference and should be released using
 * #ASTOBJ_UNREF(iface, ast_smdi_interface_destroy).
 */
extern struct ast_smdi_interface *ast_smdi_interface_find(const char *iface_name)
{
	return (ASTOBJ_CONTAINER_FIND(&smdi_ifaces, iface_name));
}

/*! \brief Read an SMDI message.
 *
 * \param iface the SMDI interface to read from.
 *
 * This function loops and reads from and SMDI interface.  It must be stopped
 * using pthread_cancel().
 */
static void *smdi_read(void *iface_p)
{
	struct ast_smdi_interface *iface = iface_p;
	struct ast_smdi_md_message *md_msg;
	struct ast_smdi_mwi_message *mwi_msg;
	char c = '\0';
	char *cp = NULL;
	int i;
	int start = 0;
		
	/* read an smdi message */
	while ((c = fgetc(iface->file))) {

		/* check if this is the start of a message */
		if (!start) {
			if (c == 'M')
				start = 1;
		}
		else { /* Determine if this is a MD or MWI message */
			if(c == 'D') { /* MD message */
				start = 0;

				if (!(md_msg = ast_calloc(1, sizeof(*md_msg)))) {
					ASTOBJ_UNREF(iface,ast_smdi_interface_destroy);
					return NULL;
				}
				
				ASTOBJ_INIT(md_msg);

				/* read the message desk number */
				for(i = 0; i < SMDI_MESG_DESK_NUM_LEN; i++)
					md_msg->mesg_desk_num[i] = fgetc(iface->file);

				md_msg->mesg_desk_num[SMDI_MESG_DESK_NUM_LEN] = '\0';

				/* read the message desk terminal number */
				for(i = 0; i < SMDI_MESG_DESK_TERM_LEN; i++)
					md_msg->mesg_desk_term[i] = fgetc(iface->file);

				md_msg->mesg_desk_term[SMDI_MESG_DESK_TERM_LEN] = '\0';

				/* read the message type */
				md_msg->type = fgetc(iface->file);
			   
				/* read the forwarding station number (may be blank) */
				cp = &md_msg->fwd_st[0];
				for (i = 0; i < SMDI_MAX_STATION_NUM_LEN + 1; i++) {
					if((c = fgetc(iface->file)) == ' ') {
						*cp = '\0';
						break;
					}

					/* store c in md_msg->fwd_st */
					if( i >= iface->msdstrip)
						*cp++ = c;
				}

				/* make sure the value is null terminated, even if this truncates it */
				md_msg->fwd_st[SMDI_MAX_STATION_NUM_LEN] = '\0';
				cp = NULL;
				
				/* read the calling station number (may be blank) */
				cp = &md_msg->calling_st[0];
				for (i = 0; i < SMDI_MAX_STATION_NUM_LEN + 1; i++) {
					if (!isdigit((c = fgetc(iface->file)))) {
						*cp = '\0';
						break;
					}

					/* store c in md_msg->calling_st */
					if (i >= iface->msdstrip)
						*cp++ = c;
				}

				/* make sure the value is null terminated, even if this truncates it */
				md_msg->calling_st[SMDI_MAX_STATION_NUM_LEN] = '\0';
				cp = NULL;

				/* add the message to the message queue */
				md_msg->timestamp = ast_tvnow();
				ast_smdi_md_message_push(iface, md_msg);
				ast_log(LOG_DEBUG, "Recieved SMDI MD message on %s\n", iface->name);
				
				ASTOBJ_UNREF(md_msg, ast_smdi_md_message_destroy);

			} else if(c == 'W') { /* MWI message */
				start = 0;

				if (!(mwi_msg = ast_calloc(1, sizeof(*mwi_msg)))) {
					ASTOBJ_UNREF(iface,ast_smdi_interface_destroy);
					return NULL;
				}

				ASTOBJ_INIT(mwi_msg);

				/* discard the 'I' (from 'MWI') */
				fgetc(iface->file);
				
				/* read the forwarding station number (may be blank) */
				cp = &mwi_msg->fwd_st[0];
				for (i = 0; i < SMDI_MAX_STATION_NUM_LEN + 1; i++) {
					if ((c = fgetc(iface->file)) == ' ') {
						*cp = '\0';
						break;
					}

					/* store c in md_msg->fwd_st */
					if (i >= iface->msdstrip)
						*cp++ = c;
				}

				/* make sure the station number is null terminated, even if this will truncate it */
				mwi_msg->fwd_st[SMDI_MAX_STATION_NUM_LEN] = '\0';
				cp = NULL;
				
				/* read the mwi failure cause */
				for (i = 0; i < SMDI_MWI_FAIL_CAUSE_LEN; i++)
					mwi_msg->cause[i] = fgetc(iface->file);

				mwi_msg->cause[SMDI_MWI_FAIL_CAUSE_LEN] = '\0';

				/* add the message to the message queue */
				mwi_msg->timestamp = ast_tvnow();
				ast_smdi_mwi_message_push(iface, mwi_msg);
				ast_log(LOG_DEBUG, "Recieved SMDI MWI message on %s\n", iface->name);
				
				ASTOBJ_UNREF(mwi_msg, ast_smdi_mwi_message_destroy);
			} else {
				ast_log(LOG_ERROR, "Unknown SMDI message type recieved on %s (M%c).\n", iface->name, c);
				start = 0;
			}
		}
	}

	ast_log(LOG_ERROR, "Error reading from SMDI interface %s, stopping listener thread\n", iface->name);
	ASTOBJ_UNREF(iface,ast_smdi_interface_destroy);
	return NULL;
}

/*! \brief ast_smdi_md_message destructor. */
void ast_smdi_md_message_destroy(struct ast_smdi_md_message *msg)
{
	free(msg);
}

/*! \brief ast_smdi_mwi_message destructor. */
void ast_smdi_mwi_message_destroy(struct ast_smdi_mwi_message *msg)
{
	free(msg);
}

/*! \brief ast_smdi_interface destructor. */
void ast_smdi_interface_destroy(struct ast_smdi_interface *iface)
{
	if (iface->thread != AST_PTHREADT_NULL && iface->thread != AST_PTHREADT_STOP) {
		pthread_cancel(iface->thread);
		pthread_join(iface->thread, NULL);
	}
	
	iface->thread = AST_PTHREADT_STOP;
	
	if(iface->file) 
		fclose(iface->file);
	
	ASTOBJ_CONTAINER_DESTROYALL(&iface->md_q, ast_smdi_md_message_destroy);
	ASTOBJ_CONTAINER_DESTROYALL(&iface->mwi_q, ast_smdi_mwi_message_destroy);
	ASTOBJ_CONTAINER_DESTROY(&iface->md_q);
	ASTOBJ_CONTAINER_DESTROY(&iface->mwi_q);
	free(iface);

	STANDARD_DECREMENT_USECOUNT;
}

/*!
 * \internal
 * \brief Load and reload SMDI configuration.
 * \param reload this should be 1 if we are reloading and 0 if not.
 *
 * This function loads/reloads the SMDI configuration and starts and stops
 * interfaces accordingly.
 *
 * \return zero on success, -1 on failure, and 1 if no smdi interfaces were started.
 */
static int smdi_load(int reload)
{
	struct ast_config *conf;
	struct ast_variable *v;
	struct ast_smdi_interface *iface = NULL;
	int res = 0;

	/* Config options */
	speed_t baud_rate = B9600;     /* 9600 baud rate */
	tcflag_t paritybit = PARENB;   /* even parity checking */
	tcflag_t charsize = CS7;       /* seven bit characters */
	int stopbits = 0;              /* One stop bit */
	
	int msdstrip = 0;              /* strip zero digits */
	long msg_expiry = SMDI_MSG_EXPIRY_TIME;
	
	conf = ast_config_load(config_file);

	if (!conf) {
		if (reload)
			ast_log(LOG_NOTICE, "Unable to reload config %s: SMDI untouched\n", config_file);
		else
			ast_log(LOG_NOTICE, "Unable to load config %s: SMDI disabled\n", config_file);
		return 1;
	}

	/* Mark all interfaces that we are listening on.  We will unmark them
	 * as we find them in the config file, this way we know any interfaces
	 * still marked after we have finished parsing the config file should
	 * be stopped.
	 */
	if (reload)
		ASTOBJ_CONTAINER_MARKALL(&smdi_ifaces);

	for (v = ast_variable_browse(conf, "interfaces"); v; v = v->next) {
		if (!strcasecmp(v->name, "baudrate")) {
			if (!strcasecmp(v->value, "9600"))
				baud_rate = B9600;
			else if(!strcasecmp(v->value, "4800"))
				baud_rate = B4800;
			else if(!strcasecmp(v->value, "2400"))
				baud_rate = B2400;
			else if(!strcasecmp(v->value, "1200"))
				baud_rate = B1200;
			else {
				ast_log(LOG_NOTICE, "Invalid baud rate '%s' specified in %s (line %d), using default\n", v->value, config_file, v->lineno);
				baud_rate = B9600;
			}
		} else if (!strcasecmp(v->name, "msdstrip")) {
			if (!sscanf(v->value, "%d", &msdstrip)) {
				ast_log(LOG_NOTICE, "Invalid msdstrip value in %s (line %d), using default\n", config_file, v->lineno);
				msdstrip = 0;
			} else if (0 > msdstrip || msdstrip > 9) {
				ast_log(LOG_NOTICE, "Invalid msdstrip value in %s (line %d), using default\n", config_file, v->lineno);
				msdstrip = 0;
			}
		} else if (!strcasecmp(v->name, "msgexpirytime")) {
			if (!sscanf(v->value, "%ld", &msg_expiry)) {
				ast_log(LOG_NOTICE, "Invalid msgexpirytime value in %s (line %d), using default\n", config_file, v->lineno);
				msg_expiry = SMDI_MSG_EXPIRY_TIME;
			}
		} else if (!strcasecmp(v->name, "paritybit")) {
			if (!strcasecmp(v->value, "even"))
				paritybit = PARENB;
			else if (!strcasecmp(v->value, "odd"))
				paritybit = PARENB | PARODD;
			else if (!strcasecmp(v->value, "none"))
				paritybit = ~PARENB;
			else {
				ast_log(LOG_NOTICE, "Invalid parity bit setting in %s (line %d), using default\n", config_file, v->lineno);
				paritybit = PARENB;
			}
		} else if (!strcasecmp(v->name, "charsize")) {
			if (!strcasecmp(v->value, "7"))
				charsize = CS7;
			else if (!strcasecmp(v->value, "8"))
				charsize = CS8;
			else {
				ast_log(LOG_NOTICE, "Invalid character size setting in %s (line %d), using default\n", config_file, v->lineno);
				charsize = CS7;
			}
		} else if (!strcasecmp(v->name, "twostopbits")) {
			stopbits = ast_true(v->name);
		} else if (!strcasecmp(v->name, "smdiport")) {
			if (reload) {
				/* we are reloading, check if we are already
				 * monitoring this interface, if we are we do
				 * not want to start it again.  This also has
				 * the side effect of not updating different
				 * setting for the serial port, but it should
				 * be trivial to rewrite this section so that
				 * options on the port are changed without
				 * restarting the interface.  Or the interface
				 * could be restarted with out emptying the
				 * queue. */
				if ((iface = ASTOBJ_CONTAINER_FIND(&smdi_ifaces, v->value))) {
					ast_log(LOG_NOTICE, "SMDI interface %s already running, not restarting\n", iface->name);
					ASTOBJ_UNMARK(iface);
					ASTOBJ_UNREF(iface, ast_smdi_interface_destroy);
					continue;
				}
			}
							
			iface = ast_calloc(1, sizeof(*iface));

			ASTOBJ_INIT(iface);
			ASTOBJ_CONTAINER_INIT(&iface->md_q);
			ASTOBJ_CONTAINER_INIT(&iface->mwi_q);

			ast_copy_string(iface->name, v->value, sizeof(iface->name));

			if (!(iface->file = fopen(iface->name, "r"))) {
				ast_log(LOG_ERROR, "Error opening SMDI interface %s (%s)\n", iface->name, strerror(errno));
				ASTOBJ_UNREF(iface, ast_smdi_interface_destroy);
				continue;
			}

			iface->fd = fileno(iface->file);

			/* Set the proper attributes for our serial port. */

			/* get the current attributes from the port */
			if (tcgetattr(iface->fd, &iface->mode)) {
				ast_log(LOG_ERROR, "Error getting atributes of %s (%s)\n", iface->name, strerror(errno));
				ASTOBJ_UNREF(iface, ast_smdi_interface_destroy);
				continue;
			}

			/* set the desired speed */
			if (cfsetispeed(&iface->mode, baud_rate) || cfsetospeed(&iface->mode, baud_rate)) {
				ast_log(LOG_ERROR, "Error setting baud rate on %s (%s)\n", iface->name, strerror(errno));
				ASTOBJ_UNREF(iface, ast_smdi_interface_destroy);
				continue;
			}
			
			/* set the stop bits */
			if (stopbits)
			   iface->mode.c_cflag = iface->mode.c_cflag | CSTOPB;   /* set two stop bits */
			else
			   iface->mode.c_cflag = iface->mode.c_cflag & ~CSTOPB;  /* set one stop bit */

			/* set the parity */
			iface->mode.c_cflag = (iface->mode.c_cflag & ~PARENB & ~PARODD) | paritybit;

			/* set the character size */
			iface->mode.c_cflag = (iface->mode.c_cflag & ~CSIZE) | charsize;
			
			/* commit the desired attributes */
			if (tcsetattr(iface->fd, TCSAFLUSH, &iface->mode)) {
				ast_log(LOG_ERROR, "Error setting attributes on %s (%s)\n", iface->name, strerror(errno));
				ASTOBJ_UNREF(iface, ast_smdi_interface_destroy);
				continue;
			}

			/* set the msdstrip */
			iface->msdstrip = msdstrip;

			/* set the message expiry time */
			iface->msg_expiry = msg_expiry;

                        /* start the listner thread */
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Starting SMDI monitor thread for %s\n", iface->name);
			if (ast_pthread_create(&iface->thread, NULL, smdi_read, iface)) {
				ast_log(LOG_ERROR, "Error starting SMDI monitor thread for %s\n", iface->name);
				ASTOBJ_UNREF(iface, ast_smdi_interface_destroy);
				continue;
			}

			ASTOBJ_CONTAINER_LINK(&smdi_ifaces, iface);
			ASTOBJ_UNREF(iface, ast_smdi_interface_destroy);
			STANDARD_INCREMENT_USECOUNT;
		} else {
			ast_log(LOG_NOTICE, "Ignoring unknown option %s in %s\n", v->name, config_file);
		}
		v = v->next;
	}
	ast_config_destroy(conf);

	/* Prune any interfaces we should no longer monitor. */
	if (reload)
		ASTOBJ_CONTAINER_PRUNE_MARKED(&smdi_ifaces, ast_smdi_interface_destroy);
	
	ASTOBJ_CONTAINER_RDLOCK(&smdi_ifaces);
	/* TODO: this is bad, we need an ASTOBJ method for this! */
	if (!smdi_ifaces.head)
		res = 1;
	ASTOBJ_CONTAINER_UNLOCK(&smdi_ifaces);
			
	return res;
}


char *description(void)
{
	return (char *) tdesc;
}

int load_module(void)
{
	int res;

	/* initialize our containers */
	memset(&smdi_ifaces, 0, sizeof(smdi_ifaces));
	ASTOBJ_CONTAINER_INIT(&smdi_ifaces);

	/* load the config and start the listener threads*/
	res = smdi_load(0);
	if (res < 0) {
		return res;
	} else if (res == 1) {
		ast_log(LOG_WARNING, "No SMDI interfaces are available to listen on, not starting SDMI listener.\n");
		return 0;
	} else
		return 0;
}

int unload_module(void)
{
	/* this destructor stops any running smdi_read threads */
	ASTOBJ_CONTAINER_DESTROYALL(&smdi_ifaces, ast_smdi_interface_destroy);
	ASTOBJ_CONTAINER_DESTROY(&smdi_ifaces);

	/*
	 * localusers = NULL; is just to silence the compiler warning
	 * about an unused variable. It will be removed soon, when the
	 * LOCALUSER-related functions are rewritten.
	 */
	localusers = NULL;
	return 0;
}

int reload(void)
{
	int res;

	res = smdi_load(1);

	if (res < 0) {
		return res;
	} else if (res == 1) {
		ast_log(LOG_WARNING, "No SMDI interfaces were specified to listen on, not starting SDMI listener.\n");
		return 0;
	} else
		return 0;
}

int usecount(void)
{
	int res;

	STANDARD_USECOUNT(res);

	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
