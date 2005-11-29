/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Implementation of Inter-Asterisk eXchange
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#ifndef _ASTERISK_IAX_H
#define _ASTERISK_IAX_H

/* Max version of IAX protocol we support */
#define AST_IAX_PROTO_VERSION 1

#define AST_IAX_MAX_CALLS 32768

#define AST_FLAG_FULL		0x8000

#define AST_FLAG_SC_LOG		0x80

#define AST_MAX_SHIFT		0x1F

/* Subclass for AST_FRAME_IAX */
#define AST_IAX_COMMAND_NEW		1
#define AST_IAX_COMMAND_PING	2
#define AST_IAX_COMMAND_PONG	3
#define AST_IAX_COMMAND_ACK		4
#define AST_IAX_COMMAND_HANGUP	5
#define AST_IAX_COMMAND_REJECT	6
#define AST_IAX_COMMAND_ACCEPT	7
#define AST_IAX_COMMAND_AUTHREQ	8
#define AST_IAX_COMMAND_AUTHREP	9
#define AST_IAX_COMMAND_INVAL	10
#define AST_IAX_COMMAND_LAGRQ	11
#define AST_IAX_COMMAND_LAGRP	12
#define AST_IAX_COMMAND_REGREQ	13	/* Registration request */
#define AST_IAX_COMMAND_REGAUTH	14	/* Registration authentication required */
#define AST_IAX_COMMAND_REGACK	15	/* Registration accepted */
#define AST_IAX_COMMAND_REGREJ	16	/* Registration rejected */
#define AST_IAX_COMMAND_REGREL	17	/* Force release of registration */
#define AST_IAX_COMMAND_VNAK	18	/* If we receive voice before valid first voice frame, send this */
#define AST_IAX_COMMAND_DPREQ	19	/* Request status of a dialplan entry */
#define AST_IAX_COMMAND_DPREP	20	/* Request status of a dialplan entry */
#define AST_IAX_COMMAND_DIAL	21	/* Request a dial on channel brought up TBD */
#define AST_IAX_COMMAND_TXREQ	22	/* Transfer Request */
#define AST_IAX_COMMAND_TXCNT	23	/* Transfer Connect */
#define AST_IAX_COMMAND_TXACC	24	/* Transfer Accepted */
#define AST_IAX_COMMAND_TXREADY	25	/* Transfer ready */
#define AST_IAX_COMMAND_TXREL	26	/* Transfer release */
#define AST_IAX_COMMAND_TXREJ	27	/* Transfer reject */
#define AST_IAX_COMMAND_QUELCH	28	/* Stop audio/video transmission */
#define AST_IAX_COMMAND_UNQUELCH 29	/* Resume audio/video transmission */

#define AST_DEFAULT_REG_EXPIRE  60	/* By default require re-registration once per minute */

#define AST_DEFAULT_IAX_PORTNO	5036

/* Full frames are always delivered reliably */
struct ast_iax_full_hdr {
	short callno;			/* Source call number -- high bit must be 1 */
	short dcallno;			/* Destination call number */
	unsigned int ts;		/* 32-bit timestamp in milliseconds */
	unsigned short seqno;	/* Packet number */
	char type;				/* Frame type */
	unsigned char csub;		/* Compressed subclass */
	char data[0];
};

/* Mini header is used only for voice frames -- delivered unreliably */
struct ast_iax_mini_hdr {
	short callno;			/* Source call number -- high bit must be 0 */
	unsigned short ts;		/* 16-bit Timestamp (high 16 bits from last ast_iax_full_hdr) */
							/* Frametype implicitly VOICE_FRAME */
							/* subclass implicit from last ast_iax_full_hdr */
	char data[0];
};

#endif
