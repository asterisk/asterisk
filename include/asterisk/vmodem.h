/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Voice Modem Definitions
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_VMODEM_H
#define _ASTERISK_VMODEM_H

#include <asterisk/frame.h>
#include <asterisk/channel.h>

#define CHAR_DLE		0x10
#define CHAR_ETX		0x03
#define CHAR_DC4		0x14

#define MODEM_DEV_TELCO		0
#define MODEM_DEV_TELCO_SPK	4
#define MODEM_DEV_SPKRPHONE	6
#define MODEM_DEV_HANDSET	9

/* Thirty millisecond sections */
#define MODEM_MAX_LEN 30
#define MODEM_MAX_BUF MODEM_MAX_LEN * 16

#define AST_MAX_INIT_STR	256

struct ast_modem_pvt;

struct ast_modem_driver {
	char *name;
	char **idents;
	int formats;
	int fullduplex;
	void (*incusecnt)();
	void (*decusecnt)();
	char * (*identify)(struct ast_modem_pvt *);
	int (*init)(struct ast_modem_pvt *);
	int (*setdev)(struct ast_modem_pvt *, int dev);
	struct ast_frame * (*read)(struct ast_modem_pvt *);
	int (*write)(struct ast_modem_pvt *, struct ast_frame *fr);
	int (*dial)(struct ast_modem_pvt *, char *);
	int (*answer)(struct ast_modem_pvt *);
	int (*hangup)(struct ast_modem_pvt *);
	int (*startrec)(struct ast_modem_pvt *);
	int (*stoprec)(struct ast_modem_pvt *);
	int (*startpb)(struct ast_modem_pvt *);
	int (*stoppb)(struct ast_modem_pvt *);
	int (*setsilence)(struct ast_modem_pvt *, int onoff);
	int (*dialdigit)(struct ast_modem_pvt *, char digit);
	struct ast_modem_driver *next;
};

#define MODEM_MODE_IMMEDIATE 		0
#define MODEM_MODE_WAIT_RING		1
#define MODEM_MODE_WAIT_ANSWER		2

struct ast_modem_pvt {
	int fd;							/* Raw file descriptor for this device */
	FILE *f;						/* FILE * representation of device */
	struct ast_channel *owner;		/* Channel we belong to, possibly NULL */
	char dev[256];					/* Device name */
	struct ast_frame fr;			/* Frame */
	char offset[AST_FRIENDLY_OFFSET];
	char obuf[MODEM_MAX_BUF];		/* Outgoing buffer */
	int tail;
	char dialtype;					/* Pulse or tone dialling */
	char dialtimeout;				/* Time to wait for dial timeout */
	int obuflen;
	int mode;						/* Immediate, or wait for an answer */
	int ministate;					/* State of modem in miniature */
	int stripmsd;					/* Digits to strip on outgoing numbers */
	int escape;					/* Is the last thing we saw an escape */
	int gotclid;				/* flag to say if has caller*id yet*/
	int ringt;				/* ringer timeout */
	time_t lastring;			/* actual time of last ring */
	char dtmfrx;				/* dtmf receive state/data */
	char context[AST_MAX_EXTENSION];
	char msn[AST_MAX_EXTENSION];	/* Multiple Subscriber Number */
	char cid[AST_MAX_EXTENSION];	/* Caller ID if available */
	char dnid[AST_MAX_EXTENSION];	/* Dialed Number if available */
	char initstr[AST_MAX_INIT_STR];	/* Modem initialization String */
	char language[MAX_LANGUAGE];	/* default language */
	char response[256];				/* Static response buffer */
	struct ast_modem_driver *mc;	/* Modem Capability */
	struct ast_modem_pvt *next;			/* Next channel in list */
};

/* Register a driver */
extern int ast_register_modem_driver(struct ast_modem_driver *mc);

/* Unregister a driver */
extern int ast_unregister_modem_driver(struct ast_modem_driver *mc);

/* Send the command cmd (length len, or 0 if pure ascii) on modem */
extern int ast_modem_send(struct ast_modem_pvt *p, char *cmd, int len);

/* Wait for result to occur.  Return non-zero if times out or error, last
   response is stored in p->response  */
extern int ast_modem_expect(struct ast_modem_pvt *p,  char *result, int timeout);

/* Wait for result to occur.    response is stored in p->response  */
extern int ast_modem_read_response(struct ast_modem_pvt *p,  int timeout);

/* Used by modem drivers to start up the PBX on a RING */
extern struct ast_channel *ast_modem_new(struct ast_modem_pvt *i, int state);

/* Trim off trailing mess */
extern void ast_modem_trim(char *s);
#endif
