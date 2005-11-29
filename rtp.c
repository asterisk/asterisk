/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Real-time Protocol Support
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include <asterisk/rtp.h>
#include <asterisk/logger.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>

static int dtmftimeout = 300;	/* 300 samples */

struct ast_rtp {
	int s;
	char resp;
	struct ast_frame f;
	unsigned char rawdata[1024 + AST_FRIENDLY_OFFSET];
	unsigned int ssrc;
	unsigned int lastts;
	unsigned int lastrxts;
	int dtmfcount;
	struct sockaddr_in us;
	struct sockaddr_in them;
	struct timeval rxcore;
	struct timeval txcore;
	int *ioid;
	unsigned short seqno;
	struct sched_context *sched;
	struct io_context *io;
	void *data;
	ast_rtp_callback callback;
};


void ast_rtp_set_data(struct ast_rtp *rtp, void *data)
{
	rtp->data = data;
}

void ast_rtp_set_callback(struct ast_rtp *rtp, ast_rtp_callback callback)
{
	rtp->callback = callback;
}

static void send_dtmf(struct ast_rtp *rtp)
{
	printf("Sending dtmf: %d (%c)\n", rtp->resp, rtp->resp);
	rtp->f.frametype = AST_FRAME_DTMF;
	rtp->f.subclass = rtp->resp;
	rtp->f.datalen = 0;
	rtp->f.timelen = 0;
	rtp->f.mallocd = 0;
	rtp->f.src = "RTP";
	rtp->resp = 0;
	if (rtp->callback)
		rtp->callback(rtp, &rtp->f, rtp->data);
	
}

static void process_rfc2833(struct ast_rtp *rtp, unsigned char *data, int len)
{
	unsigned int event;
	char resp = 0;
	event = ntohl(*((unsigned int *)(data)));
	event >>= 24;
#if 0
	printf("Event: %08x (len = %d)\n", event, len);
#endif	
	if (event < 10) {
		resp = '0' + event;
	} else if (event < 11) {
		resp = '*';
	} else if (event < 12) {
		resp = '#';
	} else if (event < 16) {
		resp = 'A' + (event - 12);
	}
	if (rtp->resp && (rtp->resp != resp)) {
		send_dtmf(rtp);
	}
	rtp->resp = resp;
	rtp->dtmfcount = dtmftimeout;
}

static int rtpread(int *id, int fd, short events, void *cbdata)
{
	struct ast_rtp *rtp = cbdata;
	int res;
	struct sockaddr_in sin;
	int len;
	unsigned int seqno;
	int payloadtype;
	int hdrlen = 12;
	unsigned int timestamp;
	
	unsigned int *rtpheader;
	
	len = sizeof(sin);
	
	res = recvfrom(rtp->s, rtp->rawdata + AST_FRIENDLY_OFFSET, sizeof(rtp->rawdata) - AST_FRIENDLY_OFFSET,
					0, (struct sockaddr *)&sin, &len);

	rtpheader = (unsigned int *)(rtp->rawdata + AST_FRIENDLY_OFFSET);
	if (res < 0) {
		ast_log(LOG_WARNING, "RTP Read error: %s\n", strerror(errno));
		if (errno == EBADF)
			CRASH;
		return 1;
	}
	if (res < hdrlen) {
		ast_log(LOG_WARNING, "RTP Read too short\n");
		return 1;
	}
	/* Get fields */
	seqno = ntohl(rtpheader[0]);
	payloadtype = (seqno & 0x7f0000) >> 16;
	seqno &= 0xffff;
	timestamp = ntohl(rtpheader[1]);
#if 0
	printf("Got RTP packet from %s:%d (type %d, seq %d, ts %d, len = %d)\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), payloadtype, seqno, timestamp,res - hdrlen);
#endif	
	rtp->f.frametype = AST_FRAME_VOICE;
	rtp->f.subclass = rtp2ast(payloadtype);
	if (rtp->f.subclass < 0) {
		if (payloadtype == 101) {
			/* It's special -- rfc2833 process it */
			process_rfc2833(rtp, rtp->rawdata + AST_FRIENDLY_OFFSET + hdrlen, res - hdrlen);
		} else
			ast_log(LOG_NOTICE, "Unknown RTP codec %d received\n", payloadtype);
		return 1;
	}

	if (!rtp->lastrxts)
		rtp->lastrxts = timestamp;

	if (rtp->dtmfcount) {
#if 0
		printf("dtmfcount was %d\n", rtp->dtmfcount);
#endif		
		rtp->dtmfcount -= (timestamp - rtp->lastrxts);
		if (rtp->dtmfcount < 0)
			rtp->dtmfcount = 0;
#if 0
		if (dtmftimeout != rtp->dtmfcount)
			printf("dtmfcount is %d\n", rtp->dtmfcount);
#endif
	}
	rtp->lastrxts = timestamp;

	/* Send any pending DTMF */
	if (rtp->resp && !rtp->dtmfcount) {
		send_dtmf(rtp);
		/* Setup the voice frame again */
		rtp->f.frametype = AST_FRAME_VOICE;
		rtp->f.subclass = rtp2ast(payloadtype);
	}
	rtp->f.mallocd = 0;
	rtp->f.datalen = res - hdrlen;
	rtp->f.data = rtp->rawdata + hdrlen + AST_FRIENDLY_OFFSET;
	rtp->f.offset = hdrlen + AST_FRIENDLY_OFFSET;
	switch(rtp->f.subclass) {
	case AST_FORMAT_ULAW:
	case AST_FORMAT_ALAW:
		rtp->f.timelen = rtp->f.datalen / 8;
		break;
	case AST_FORMAT_SLINEAR:
		rtp->f.timelen = rtp->f.datalen / 16;
		break;
	case AST_FORMAT_GSM:
		rtp->f.timelen = 20 * (rtp->f.datalen / 33);
		break;
	case AST_FORMAT_ADPCM:
		rtp->f.timelen = rtp->f.datalen / 8;
		break;
	default:
		ast_log(LOG_NOTICE, "Unable to calculate timelen for format %d\n", rtp->f.subclass);
		break;
	}
	rtp->f.src = "RTP";
	if (rtp->callback)
		rtp->callback(rtp, &rtp->f, rtp->data);
	return 1;
}

static struct {
	int rtp;
	int ast;
} cmap[] = {
	{ 0, AST_FORMAT_ULAW },
	{ 3, AST_FORMAT_GSM },
	{ 4, AST_FORMAT_G723_1 },
	{ 5, AST_FORMAT_ADPCM },
	{ 8, AST_FORMAT_ALAW },
	{ 18, AST_FORMAT_G729A },
};

int rtp2ast(int id)
{
	int x;
	for (x=0;x<sizeof(cmap) / sizeof(cmap[0]); x++) {
		if (cmap[x].rtp == id)
			return cmap[x].ast;
	}
	return -1;
}

int ast2rtp(int id)
{
	int x;
	for (x=0;x<sizeof(cmap) / sizeof(cmap[0]); x++) {
		if (cmap[x].ast == id)
			return cmap[x].rtp;
	}
	return -1;
}

struct ast_rtp *ast_rtp_new(struct sched_context *sched, struct io_context *io)
{
	struct ast_rtp *rtp;
	int x;
	int flags;
	rtp = malloc(sizeof(struct ast_rtp));
	if (!rtp)
		return NULL;
	memset(rtp, 0, sizeof(struct ast_rtp));
	rtp->them.sin_family = AF_INET;
	rtp->us.sin_family = AF_INET;
	rtp->s = socket(AF_INET, SOCK_DGRAM, 0);
	rtp->ssrc = rand();
	rtp->seqno = rand() & 0xffff;
	if (rtp->s < 0) {
		free(rtp);
		ast_log(LOG_WARNING, "Unable to allocate socket: %s\n", strerror(errno));
		return NULL;
	}
	flags = fcntl(rtp->s, F_GETFL);
	fcntl(rtp->s, F_SETFL, flags | O_NONBLOCK);
	for (;;) {
		/* Find us a place */
		x = (rand() % (65000-1025)) + 1025;
		/* Must be an even port number by RTP spec */
		x = x & ~1;
		rtp->us.sin_port = htons(x);
		if (!bind(rtp->s, &rtp->us, sizeof(rtp->us)))
			break;
		if (errno != EADDRINUSE) {
			ast_log(LOG_WARNING, "Unexpected bind error: %s\n", strerror(errno));
			close(rtp->s);
			free(rtp);
			return NULL;
		}
	}
	rtp->io = io;
	rtp->sched = sched;
	rtp->ioid = ast_io_add(rtp->io, rtp->s, rtpread, AST_IO_IN, rtp);
	return rtp;
}

void ast_rtp_set_peer(struct ast_rtp *rtp, struct sockaddr_in *them)
{
	rtp->them.sin_port = them->sin_port;
	rtp->them.sin_addr = them->sin_addr;
}

void ast_rtp_get_us(struct ast_rtp *rtp, struct sockaddr_in *us)
{
	memcpy(us, &rtp->us, sizeof(rtp->us));
}

void ast_rtp_destroy(struct ast_rtp *rtp)
{
	if (rtp->ioid)
		ast_io_remove(rtp->io, rtp->ioid);
	if (rtp->s > -1)
		close(rtp->s);
	free(rtp);
}

static unsigned int calc_txstamp(struct ast_rtp *rtp)
{
	struct timeval now;
	unsigned int ms;
	if (!rtp->txcore.tv_sec && !rtp->txcore.tv_usec) {
		gettimeofday(&rtp->txcore, NULL);
	}
	gettimeofday(&now, NULL);
	ms = (now.tv_sec - rtp->txcore.tv_sec) * 1000;
	ms += (now.tv_usec - rtp->txcore.tv_usec);
	return ms;
}

int ast_rtp_write(struct ast_rtp *rtp, struct ast_frame *_f)
{
	int hdrlen = 12;
	struct ast_frame *f;
	int codec;
	int res;
	unsigned int ms;
	unsigned int *rtpheader;
	
	/* Make sure we have enough space for RTP header */
	
	if (_f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "RTP can only send voice\n");
		return -1;
	}

	codec = ast2rtp(_f->subclass);
	if (codec < 0) {
		ast_log(LOG_WARNING, "Don't know how to send format %d packets with RTP\n", _f->subclass);
		return -1;
	}

	if (_f->offset < hdrlen) {
		f = ast_frdup(_f);
	} else
		f = _f;
		
	ms = calc_txstamp(rtp) * 8;
	switch(f->subclass) {
	case AST_FORMAT_ULAW:
	case AST_FORMAT_ALAW:
		break;
	default:
		ast_log(LOG_WARNING, "Not sure about timestamp format for codec format %d\n", f->subclass);
	}
	rtp->lastts += f->datalen;
	/* Get a pointer to the header */
	rtpheader = (unsigned int *)(f->data - hdrlen);
	rtpheader[0] = htonl((2 << 30) | (codec << 16) | (rtp->seqno++));
	rtpheader[1] = htonl(rtp->lastts);
	rtpheader[2] = htonl(rtp->ssrc); 
	if (rtp->them.sin_port && rtp->them.sin_addr.s_addr) {
		res = sendto(rtp->s, (void *)rtpheader, f->datalen + hdrlen, 0, &rtp->them, sizeof(rtp->them));
		if (res <0) 
			ast_log(LOG_NOTICE, "RTP Transmission error to %s:%d: %s\n", inet_ntoa(rtp->them.sin_addr), ntohs(rtp->them.sin_port), strerror(errno));
#if 0
		printf("Sent %d bytes of RTP data to %s:%d\n", res, inet_ntoa(rtp->them.sin_addr), ntohs(rtp->them.sin_port));
#endif		
	}
	return 0;
}
