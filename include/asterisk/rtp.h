/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Real-time Transport Protocol support
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_RTP_H
#define _ASTERISK_RTP_H

#include <asterisk/frame.h>
#include <asterisk/io.h>
#include <asterisk/sched.h>
#include <asterisk/channel.h>

#include <netinet/in.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/* Codes for RTP-specific data - not defined by our AST_FORMAT codes */
/*! DTMF (RFC2833) */
#define AST_RTP_DTMF            (1 << 0)
/*! 'Comfort Noise' (RFC3389) */
#define AST_RTP_CN              (1 << 1)
/*! DTMF (Cisco Proprietary) */
#define AST_RTP_CISCO_DTMF      (1 << 2)
/*! Maximum RTP-specific code */
#define AST_RTP_MAX             AST_RTP_CISCO_DTMF

struct ast_rtp_protocol {
	struct ast_rtp *(*get_rtp_info)(struct ast_channel *chan);				/* Get RTP struct, or NULL if unwilling to transfer */
	struct ast_rtp *(*get_vrtp_info)(struct ast_channel *chan);				/* Get RTP struct, or NULL if unwilling to transfer */
	int (*set_rtp_peer)(struct ast_channel *chan, struct ast_rtp *peer, struct ast_rtp *vpeer, int codecs);	/* Set RTP peer */
	int (*get_codec)(struct ast_channel *chan);
	char *type;
	struct ast_rtp_protocol *next;
};

struct ast_rtp;

typedef int (*ast_rtp_callback)(struct ast_rtp *rtp, struct ast_frame *f, void *data);

struct ast_rtp *ast_rtp_new(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode);

struct ast_rtp *ast_rtp_new_with_bindaddr(struct sched_context *sched, struct io_context *io, int rtcpenable, int callbackmode, struct in_addr in);

void ast_rtp_set_peer(struct ast_rtp *rtp, struct sockaddr_in *them);

void ast_rtp_get_peer(struct ast_rtp *rtp, struct sockaddr_in *them);

void ast_rtp_get_us(struct ast_rtp *rtp, struct sockaddr_in *us);

void ast_rtp_destroy(struct ast_rtp *rtp);

void ast_rtp_set_callback(struct ast_rtp *rtp, ast_rtp_callback callback);

void ast_rtp_set_data(struct ast_rtp *rtp, void *data);

int ast_rtp_write(struct ast_rtp *rtp, struct ast_frame *f);

struct ast_frame *ast_rtp_read(struct ast_rtp *rtp);

struct ast_frame *ast_rtcp_read(struct ast_rtp *rtp);

int ast_rtp_fd(struct ast_rtp *rtp);

int ast_rtcp_fd(struct ast_rtp *rtp);

int ast_rtp_senddigit(struct ast_rtp *rtp, char digit);

int ast_rtp_settos(struct ast_rtp *rtp, int tos);

// Setting RTP payload types from lines in a SDP description:
void ast_rtp_pt_clear(struct ast_rtp* rtp);
/* Set payload types to defaults */
void ast_rtp_pt_default(struct ast_rtp* rtp);
void ast_rtp_set_m_type(struct ast_rtp* rtp, int pt);
void ast_rtp_set_rtpmap_type(struct ast_rtp* rtp, int pt,
			 char* mimeType, char* mimeSubtype);

// Mapping between RTP payload format codes and Asterisk codes:
struct rtpPayloadType ast_rtp_lookup_pt(struct ast_rtp* rtp, int pt);
int ast_rtp_lookup_code(struct ast_rtp* rtp, int isAstFormat, int code);
void ast_rtp_offered_from_local(struct ast_rtp* rtp, int local);

void ast_rtp_get_current_formats(struct ast_rtp* rtp,
			     int* astFormats, int* nonAstFormats);

// Mapping an Asterisk code into a MIME subtype (string):
char* ast_rtp_lookup_mime_subtype(int isAstFormat, int code);

void ast_rtp_setnat(struct ast_rtp *rtp, int nat);

int ast_rtp_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc);

int ast_rtp_proto_register(struct ast_rtp_protocol *proto);

void ast_rtp_proto_unregister(struct ast_rtp_protocol *proto);

void ast_rtp_stop(struct ast_rtp *rtp);

void ast_rtp_init(void);

void ast_rtp_reload(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
