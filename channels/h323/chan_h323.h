/*
 * chan_h323.h
 *
 * OpenH323 Channel Driver for ASTERISK PBX.
 *			By Jeremy McNamara
 * 			For The NuFone Network
 *	
 * This code has been derived from code created by 
 *		Michael Manousos and Mark Spencer
 *
 * This file is part of the chan_h323 driver for Asterisk
 *
 * chan_h323 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. 
 *
 * chan_h323 is distributed WITHOUT ANY WARRANTY; without even 
 * the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE. See the GNU General Public License for more details. 
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * Version Info: $Id$
 */

#include <arpa/inet.h>

/** call_option struct holds various bits
 *         of information for each call */
typedef struct call_options {
	char            cid_num[80];
	char            cid_name[80];
	int             noFastStart;
	int             noH245Tunneling;
	int             noSilenceSuppression;
	unsigned int    port;
	int             progress_setup;
	int             progress_alert;
	int             progress_audio;
	int		dtmfcodec;
} call_options_t;

/* structure to hold the valid asterisk users */
struct oh323_user {
	char name[80];
	char context[80];
	char secret[80];
	char callerid[80];
	char accountcode[20];
	int amaflags;
	int bridge;
	int nat;
	int dtmfmode;
	int host;
	call_options_t options;
	struct ast_ha *ha;
	struct sockaddr_in addr;
	struct oh323_user *next;
};

/* structure to hold the valid asterisk peers 
   All peers are registered to a GK if there is one */
struct oh323_peer {
	char name[80];
	char mailbox[80];
	int capability;
	int bridge;
	int nat;
	int dtmfmode;
	int delme;
	struct sockaddr_in addr;
	struct ast_ha *ha;
	call_options_t options;
	struct oh323_peer *next;
};

/* structure to hold the H.323 aliases which get registered to 
   the H.323 endpoint and gatekeeper */
struct oh323_alias {
	char name[80];
	char e164[20];				/* tells a GK to route this E.164 to this alias */
	char prefix[500];			/* tells a GK this alias supports these prefixes */
	char secret[20];			/* the H.235 password to send to the GK for authentication */
	char context[80];
	struct oh323_alias *next;	
};

/** call_details struct call detail records 
	to asterisk for processing and used for matching up 
	asterisk channels to acutal h.323 connections */
typedef struct call_details {	
	unsigned int call_reference;
	char *call_token;				
	char *call_source_aliases;
	char *call_dest_alias;
	char *call_source_name;
	char *call_source_e164;
	char *call_dest_e164;
	int presentation;
	int screening;
	char *sourceIp;
} call_details_t;

typedef struct rtp_info {
	const char *addr;
	unsigned int port;
} rtp_info_t;

/* This is a callback prototype function, called pass
   DTMF down the RTP. */
typedef int (*send_digit_cb)(unsigned, char, const char *);
extern send_digit_cb on_send_digit; 

/* This is a callback prototype function, called to collect
   the external RTP port from Asterisk. */
typedef rtp_info_t *(*on_rtp_cb)(unsigned, const char *);
extern on_rtp_cb on_external_rtp_create; 

/* This is a callback prototype function, called to send
   the remote IP and RTP port from H.323 to Asterisk */ 
typedef void (*start_rtp_cb)(unsigned int, const char *, int, const char *, int);
extern start_rtp_cb on_start_rtp_channel; 

/* This is a callback that happens when call progress is
 * made, and handles inband progress */
typedef int (*progress_cb)(unsigned, const char *, int);
extern progress_cb on_progress;

/* This is a callback prototype function, called upon
   an incoming call happens. */
typedef call_options_t *(*setup_incoming_cb)(call_details_t);
extern setup_incoming_cb on_incoming_call;

/* This is a callback prototype function, called upon
   an outbound call. */
typedef int (*setup_outbound_cb)(call_details_t);
extern setup_outbound_cb on_outgoing_call; 

/* This is a callback prototype function, called when
   OnAlerting is invoked */
typedef void (*chan_ringing_cb)(unsigned, const char *);
extern chan_ringing_cb on_chan_ringing;

/* This is a callback protoype function, called when
   OnConnectionEstablished is inovked */
typedef void (*con_established_cb)(unsigned, const char *);
extern con_established_cb on_connection_established;

/* This is a callback prototype function, called when
   OnConnectionCleared callback is invoked */
typedef void (*clear_con_cb)(call_details_t);
extern clear_con_cb on_connection_cleared;

/* This is a callback prototype function, called when
    an H.323 call is answered */
typedef int (*answer_call_cb)(unsigned, const char *);
extern answer_call_cb on_answer_call;

/* This is a callback prototype function, called when
   we know which RTP payload type RFC2833 will be
   transmitted */
typedef void (*rfc2833_cb)(unsigned, const char *, int);
extern rfc2833_cb on_set_rfc2833_payload;

typedef void (*hangup_cb)(unsigned, const char *, int);
extern hangup_cb on_hangup;

/* debug flag */
extern int h323debug;

#define H323_DTMF_RFC2833	(1 << 0)
#define H323_DTMF_INBAND	(1 << 1)

#ifndef BOOL
#define BOOL int
#endif

#ifdef __cplusplus
extern "C" {
#endif   
    
	void h323_gk_urq(void);
	void h323_end_point_create(void);
	void h323_end_process(void);
	int  h323_end_point_exist(void);
    
	void h323_debug(int, unsigned);

	/* callback function handler*/
	void h323_callback_register(setup_incoming_cb,  
				    setup_outbound_cb,
 				    on_rtp_cb,
				    start_rtp_cb,
 				    clear_con_cb,
 				    chan_ringing_cb,
				    con_established_cb,
 				    send_digit_cb,
 				    answer_call_cb,
				    progress_cb,
				    rfc2833_cb,
				    hangup_cb);
	int h323_set_capability(int, int);
	int h323_set_alias(struct oh323_alias *);
	int h323_set_gk(int, char *, char *);
	void h323_set_id(char *);
	void h323_show_tokens(void);

	/* H323 listener related funcions */
	int h323_start_listener(int, struct sockaddr_in);

	void h323_native_bridge(const char *, const char *, char *);

	/* Send a DTMF tone to remote endpoint */
	void h323_send_tone(const char *call_token, char tone);

	/* H323 create and destroy sessions */
	int h323_make_call(char *dest, call_details_t *cd, call_options_t *);
	int h323_clear_call(const char *, int cause);
	
	/* H.323 alerting and progress */
	int h323_send_alerting(const char *token);
	int h323_send_progress(const char *token);
	int h323_answering_call(const char *token, int);
	int h323_soft_hangup(const char *data);	
	int h323_show_codec(int fd, int argc, char *argv[]);

#ifdef __cplusplus
}
#endif



