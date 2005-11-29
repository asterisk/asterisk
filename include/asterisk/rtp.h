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

#include <netinet/in.h>

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_rtp;

typedef int (*ast_rtp_callback)(struct ast_rtp *rtp, struct ast_frame *f, void *data);

struct ast_rtp *ast_rtp_new(struct sched_context *sched, struct io_context *io);

void ast_rtp_set_peer(struct ast_rtp *rtp, struct sockaddr_in *them);

void ast_rtp_get_peer(struct ast_rtp *rpt, struct sockaddr_in *them);

void ast_rtp_get_us(struct ast_rtp *rtp, struct sockaddr_in *us);

void ast_rtp_destroy(struct ast_rtp *rtp);

void ast_rtp_set_callback(struct ast_rtp *rtp, ast_rtp_callback callback);

void ast_rtp_set_data(struct ast_rtp *rtp, void *data);

int ast_rtp_write(struct ast_rtp *rtp, struct ast_frame *f);

int ast_rtp_senddigit(struct ast_rtp *rtp, char digit);

int ast_rtp_settos(struct ast_rtp *rtp, int tos);

int ast2rtp(int id);

int rtp2ast(int id);

char *ast2rtpn(int id);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
