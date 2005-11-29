/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * External call management support 
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

#ifndef _ASTERISK_MANAGER_H
#define _ASTERISK_MANAGER_H

#include <stdarg.h>

/* 
 * Call management packages are text fields of the form a: b.  There is
 * always exactly one space after the colon.
 *
 * The first header type is the "Event" header.  Other headers vary from
 * event to event.  Headers end with standard \r\n termination.
 *
 * Some standard headers:
 *
 * Action: <action>			-- request or notification of a particular action
 * Response: <response>		-- response code, like "200 OK"
 *
 */
 
#define DEFAULT_MANAGER_PORT 5038	/* Default port for Asterisk management via TCP */

#define EVENT_FLAG_SYSTEM 		(1 << 0) /* System events such as module load/unload */
#define EVENT_FLAG_CALL			(1 << 1) /* Call event, such as state change, etc */
#define EVENT_FLAG_LOG			(1 << 2) /* Log events */
#define EVENT_FLAG_VERBOSE		(1 << 3) /* Verbose messages */
#define EVENT_FLAG_COMMAND		(1 << 4) /* Ability to read/set commands */
#define EVENT_FLAG_AGENT		(1 << 5) /* Ability to read/set agent info */

/* External routines may send asterisk manager events this way */
extern int manager_event(int category, char *event, char *contents, ...)
	__attribute__ ((format (printf, 3,4)));

/* Called by Asterisk initialization */
extern int init_manager(void);
extern int reload_manager(void);
#endif
