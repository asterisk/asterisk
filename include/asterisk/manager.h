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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <asterisk/lock.h>

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
#define EVENT_FLAG_USER                 (1 << 6) /* Ability to read/set user info */
#define EVENT_FLAG_CUSTOM1                 (1 << 7) /* Ability to read/set user info */
#define EVENT_FLAG_CUSTOM2                 (1 << 8) /* Ability to read/set user info */
#define EVENT_FLAG_CUSTOM3                 (1 << 9) /* Ability to read/set user info */

/* Export manager structures */
#define MAX_HEADERS 80
#define MAX_LEN 256

struct mansession {
	pthread_t t;
	ast_mutex_t lock;
	struct sockaddr_in sin;
	int fd;
	int blocking;
	char username[80];
	char challenge[10];
	int authenticated;
	int readperm;
	int writeperm;
	char inbuf[MAX_LEN];
	int inlen;
	int send_events;
	struct mansession *next;
};


struct message {
	int hdrcount;
	char headers[MAX_HEADERS][MAX_LEN];
};

struct manager_action {
	/*! Name of the action */
	char *action;
	/*! Short description of the action */
	char *synopsis;
	/*! Detailed description of the action */
	char *description;
	/*! Permission required for action.  EVENT_FLAG_* */
	int authority;
	/*! Function to be called */
	int (*func)(struct mansession *s, struct message *m);
	/*! For easy linking */
	struct manager_action *next;
};

int ast_carefulwrite(int fd, char *s, int len, int timeoutms);

/* External routines may register/unregister manager callbacks this way */
#define ast_manager_register(a, b, c, d) ast_manager_register2(a, b, c, d, NULL)
int ast_manager_register2( char *action, int authority, 
					 int (*func)(struct mansession *s, struct message *m), char *synopsis, char *description);
int ast_manager_unregister( char *action );

/* External routines may send asterisk manager events this way */
extern int manager_event(int category, char *event, char *contents, ...)
	__attribute__ ((format (printf, 3,4)));

extern char *astman_get_header(struct message *m, char *var);
extern void astman_send_error(struct mansession *s, struct message *m, char *error);
extern void astman_send_response(struct mansession *s, struct message *m, char *resp, char *msg);
extern void astman_send_ack(struct mansession *s, struct message *m, char *msg);

/* Called by Asterisk initialization */
extern int init_manager(void);
extern int reload_manager(void);
#endif
