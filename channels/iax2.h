/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Inter-Asterisk eXchange
 * 
 * Copyright (C) 2003, Digium
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#ifndef _ASTERISK_IAX2_H
#define _ASTERISK_IAX2_H

/* Max version of IAX protocol we support */
#define AST_IAX2_PROTO_VERSION 2

#define AST_IAX2_MAX_CALLS 32768

#define AST_FLAG_FULL		0x8000

#define AST_FLAG_RETRANS	0x8000

#define AST_FLAG_SC_LOG		0x80

#define AST_MAX_SHIFT		0x1F

#define AST_IAX2_WINDOW		256

/* Subclass for AST_FRAME_IAX */
#define AST_IAX2_COMMAND_NEW		1
#define AST_IAX2_COMMAND_PING	2
#define AST_IAX2_COMMAND_PONG	3
#define AST_IAX2_COMMAND_ACK		4
#define AST_IAX2_COMMAND_HANGUP	5
#define AST_IAX2_COMMAND_REJECT	6
#define AST_IAX2_COMMAND_ACCEPT	7
#define AST_IAX2_COMMAND_AUTHREQ	8
#define AST_IAX2_COMMAND_AUTHREP	9
#define AST_IAX2_COMMAND_INVAL	10
#define AST_IAX2_COMMAND_LAGRQ	11
#define AST_IAX2_COMMAND_LAGRP	12
#define AST_IAX2_COMMAND_REGREQ	13	/* Registration request */
#define AST_IAX2_COMMAND_REGAUTH	14	/* Registration authentication required */
#define AST_IAX2_COMMAND_REGACK	15	/* Registration accepted */
#define AST_IAX2_COMMAND_REGREJ	16	/* Registration rejected */
#define AST_IAX2_COMMAND_REGREL	17	/* Force release of registration */
#define AST_IAX2_COMMAND_VNAK	18	/* If we receive voice before valid first voice frame, send this */
#define AST_IAX2_COMMAND_DPREQ	19	/* Request status of a dialplan entry */
#define AST_IAX2_COMMAND_DPREP	20	/* Request status of a dialplan entry */
#define AST_IAX2_COMMAND_DIAL	21	/* Request a dial on channel brought up TBD */
#define AST_IAX2_COMMAND_TXREQ	22	/* Transfer Request */
#define AST_IAX2_COMMAND_TXCNT	23	/* Transfer Connect */
#define AST_IAX2_COMMAND_TXACC	24	/* Transfer Accepted */
#define AST_IAX2_COMMAND_TXREADY	25	/* Transfer ready */
#define AST_IAX2_COMMAND_TXREL	26	/* Transfer release */
#define AST_IAX2_COMMAND_TXREJ	27	/* Transfer reject */
#define AST_IAX2_COMMAND_QUELCH	28	/* Stop audio/video transmission */
#define AST_IAX2_COMMAND_UNQUELCH 29	/* Resume audio/video transmission */
#define AST_IAX2_COMMAND_POKE    30  /* Like ping, but does not require an open connection */
#define AST_IAX2_COMMAND_PAGE	31	/* Paging description */
#define AST_IAX2_COMMAND_MWI	32	/* Stand-alone message waiting indicator */
#define AST_IAX2_COMMAND_UNSUPPORT	33	/* Unsupported message received */

#define AST_DEFAULT_REG_EXPIRE  60	/* By default require re-registration once per minute */

#define AST_DEFAULT_IAX_PORTNO	4569

/* IAX Information elements */
#define IAX_IE_CALLED_NUMBER		1		/* Number/extension being called - string */
#define IAX_IE_CALLING_NUMBER		2		/* Calling number - string */
#define IAX_IE_CALLING_ANI			3		/* Calling number ANI for billing  - string */
#define IAX_IE_CALLING_NAME			4		/* Name of caller - string */
#define IAX_IE_CALLED_CONTEXT		5		/* Context for number - string */
#define IAX_IE_USERNAME				6		/* Username (peer or user) for authentication - string */
#define IAX_IE_PASSWORD				7		/* Password for authentication - string */
#define IAX_IE_CAPABILITY			8		/* Actual codec capability - unsigned int */
#define IAX_IE_FORMAT				9		/* Desired codec format - unsigned int */
#define IAX_IE_LANGUAGE				10		/* Desired language - string */
#define IAX_IE_VERSION				11		/* Protocol version - short */
#define IAX_IE_ADSICPE				12		/* CPE ADSI capability - int */
#define IAX_IE_DNID					13		/* Originally dialed DNID - string */
#define IAX_IE_AUTHMETHODS			14		/* Authentication method(s) - short */
#define IAX_IE_CHALLENGE			15		/* Challenge data for MD5/RSA - string */
#define IAX_IE_MD5_RESULT			16		/* MD5 challenge result - string */
#define IAX_IE_RSA_RESULT			17		/* RSA challenge result - string */
#define IAX_IE_APPARENT_ADDR		18		/* Apparent address of peer - struct sockaddr_in */
#define IAX_IE_REFRESH				19		/* When to refresh registration - short */
#define IAX_IE_DPSTATUS				20		/* Dialplan status - short */
#define IAX_IE_CALLNO				21		/* Call number of peer - short */
#define IAX_IE_CAUSE				22		/* Cause - string */

#define IAX_AUTH_PLAINTEXT			(1 << 0)
#define IAX_AUTH_MD5				(1 << 1)
#define IAX_AUTH_RSA				(1 << 2)

#define IAX_DPSTATUS_EXISTS			(1 << 0)
#define IAX_DPSTATUS_CANEXIST		(1 << 1)
#define IAX_DPSTATUS_NONEXISTANT	(1 << 2)
#define IAX_DPSTATUS_IGNOREPAT		(1 << 14)
#define IAX_DPSTATUS_MATCHMORE		(1 << 15)

/* Full frames are always delivered reliably */
struct ast_iax2_full_hdr {
	unsigned short scallno;	/* Source call number -- high bit must be 1 */
	unsigned short dcallno;	/* Destination call number -- high bit is 1 if retransmission */
	unsigned int ts;		/* 32-bit timestamp in milliseconds (from 1st transmission) */
	unsigned char oseqno;	/* Packet number (outgoing) */
	unsigned char iseqno;	/* Packet number (next incoming expected) */
	char type;				/* Frame type */
	unsigned char csub;		/* Compressed subclass */
	unsigned char iedata[0];
} __attribute__ ((__packed__));

/* Mini header is used only for voice frames -- delivered unreliably */
struct ast_iax2_mini_hdr {
	short callno;			/* Source call number -- high bit must be 0 */
	unsigned short ts;		/* 16-bit Timestamp (high 16 bits from last ast_iax2_full_hdr) */
							/* Frametype implicitly VOICE_FRAME */
							/* subclass implicit from last ast_iax2_full_hdr */
	unsigned char iedata[0];
} __attribute__ ((__packed__));

#endif
