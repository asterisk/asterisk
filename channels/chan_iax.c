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

#include <asterisk/frame.h> 
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/logger.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/sched.h>
#include <asterisk/io.h>
#include <asterisk/config.h>
#include <asterisk/options.h>
#include <asterisk/cli.h>
#include <asterisk/translate.h>
#include <asterisk/md5.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>

#include "iax.h"

#define DEFAULT_RETRY_TIME 1000
#define MEMORY_SIZE 100
#define DEFAULT_DROP 3

/* If you want to use the simulator, then define IAX_SIMULATOR.  */

/*
#define IAX_SIMULATOR
*/
static char *desc = "Inter Asterisk eXchange";
static char *tdesc = "Inter Asterisk eXchange Drver";
static char *type = "IAX";
static char *config = "iax.conf";

static char context[80] = "default";

static int max_retries = 4;
static int ping_time = 20;
static int lagrq_time = 10;
static int nextcallno = 0;
static int maxjitterbuffer=4000;

static int netsocket = -1;

static int usecnt;
static pthread_mutex_t usecnt_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t iaxs_lock = PTHREAD_MUTEX_INITIALIZER;

/* Ethernet, etc */
#define IAX_CAPABILITY_FULLBANDWIDTH 	0x7FFFFFFF
/* T1, maybe ISDN */
#define IAX_CAPABILITY_MEDBANDWIDTH 	(IAX_CAPABILITY_FULLBANDWIDTH & \
									~AST_FORMAT_SLINEAR & \
									~AST_FORMAT_ULAW & \
									~AST_FORMAT_ALAW) 
/* A modem */
#define IAX_CAPABILITY_LOWBANDWIDTH		(IAX_CAPABILITY_MEDBANDWIDTH & \
									~AST_FORMAT_MP3 & \
									~AST_FORMAT_ADPCM)

#define IAX_CAPABILITY_LOWFREE		(IAX_CAPABILITY_LOWBANDWIDTH & \
									 ~AST_FORMAT_G723_1)

static	struct io_context *io;
static	struct sched_context *sched;

static int iax_capability = IAX_CAPABILITY_FULLBANDWIDTH;

static int iax_dropcount = DEFAULT_DROP;

static int use_jitterbuffer = 1;

static pthread_t netthreadid;

#define IAX_STATE_STARTED		(1 << 0)
#define IAX_STATE_AUTHENTICATED (1 << 1)

#define IAX_SENSE_DENY			0
#define IAX_SENSE_ALLOW			1

struct iax_ha {
	/* Host access rule */
	struct in_addr netaddr;
	struct in_addr netmask;
	int sense;
	struct iax_ha *next;
};

struct iax_context {
	char context[AST_MAX_EXTENSION];
	struct iax_context *next;
};

struct iax_user {
	char name[80];
	char secret[80];
	char methods[80];
	struct iax_ha *ha;
	struct iax_context *contexts;
	struct iax_user *next;
};

struct iax_peer {
	char name[80];
	char username[80];
	char secret[80];
	struct sockaddr_in addr;
	int formats;
	struct in_addr mask;

	/* Dynamic Registration fields */
	int dynamic;
	struct sockaddr_in defaddr;
	char regsecret[80];
	char methods[80];
	struct timeval nexpire;
	int expire;
	struct iax_ha *ha;
	struct iax_peer *next;
};
	
/* Don't retry more frequently than every 10 ms, or less frequently than every 5 seconds */
#define MIN_RETRY_TIME	10
#define MAX_RETRY_TIME  10000
#define MAX_JITTER_BUFFER 50

/* If we have more than this much excess real jitter buffer, srhink it. */
static int max_jitter_buffer = MAX_JITTER_BUFFER;

struct chan_iax_pvt {
	/* Pipes for communication.  pipe[1] belongs to the
	   network thread (write), and pipe[0] belongs to the individual 
	   channel (read) */
	int pipe[2];
	/* Last received voice format */
	int voiceformat;
	/* Last sent voice format */
	int svoiceformat;
	/* Last received timestamp */
	unsigned int last;
	/* Last sent timestamp - never send the same timestamp twice in a single call */
	unsigned int lastsent;
	/* Ping time */
	unsigned int pingtime;
	/* Peer Address */
	struct sockaddr_in addr;
	/* Our call number */
	int callno;
	/* Peer callno */
	int peercallno;
	/* Peer supported formats */
	int peerformats;
	/* timeval that we base our transmission on */
	struct timeval offset;
	/* timeval that we base our delivery on */
	struct timeval rxcore;
	/* Historical delivery time */
	int history[MEMORY_SIZE];
	/* Current base jitterbuffer */
	int jitterbuffer;
	/* Current jitter measure */
	int jitter;
	/* LAG */
	int lag;
	/* Error, as discovered by the manager */
	int error;
	/* Onwer if we have one */
	struct ast_channel *owner;
	/* What's our state? */
	int state;
	/* Next outgoing sequence number */
	unsigned short oseqno;
	/* Next incoming sequence number */
	unsigned short iseqno;
	/* Peer name */
	char peer[80];
	/* Default Context */
	char context[80];
	/* Caller ID if available */
	char callerid[80];
	/* DNID */
	char dnid[80];
	/* Requested Extension */
	char exten[AST_MAX_EXTENSION];
	/* Expected Username */
	char username[80];
	/* Expected Secret */
	char secret[80];
	/* permitted authentication methods */
	char methods[80];
	/* MD5 challenge */
	char challenge[10];
};

struct ast_iax_frame {
	/* Actual, isolated frame */
	struct ast_frame *f;
	/* /Our/ call number */
	short callno;
	/* Start of raw frame (outgoing only) */
	void *data;
	/* Length of frame (outgoing only) */
	int datalen;
	/* How many retries so far? */
	int retries;
	/* Outgoing relative timestamp (ms) */
	unsigned int ts;
	/* How long to wait before retrying */
	int retrytime;
	/* Are we received out of order?  */
	int outoforder;
	/* Have we been sent at all yet? */
	int sentyet;
	/* Packet sequence number */
	int seqno;
	/* Easy linking */
	struct ast_iax_frame *next;
	struct ast_iax_frame *prev;
};

static struct ast_iax_queue {
	struct ast_iax_frame *head;
	struct ast_iax_frame *tail;
	int count;
	pthread_mutex_t lock;
} iaxq;

static struct ast_user_list {
	struct iax_user *users;
	pthread_mutex_t lock;
} userl;

static struct ast_peer_list {
	struct iax_peer *peers;
	pthread_mutex_t lock;
} peerl;

/* XXX We probably should use a mutex when working with this XXX */
static struct chan_iax_pvt *iaxs[AST_IAX_MAX_CALLS];

static int send_command(struct chan_iax_pvt *, char, int, unsigned int, char *, int, int);

static unsigned int calc_timestamp(struct chan_iax_pvt *p, unsigned int ts);

static int send_ping(void *data)
{
	int callno = (long)data;
	if (iaxs[callno]) {
		send_command(iaxs[callno], AST_FRAME_IAX, AST_IAX_COMMAND_PING, 0, NULL, 0, -1);
		return 1;
	} else
		return 0;
}

static int send_lagrq(void *data)
{
	int callno = (long)data;
	if (iaxs[callno]) {
		send_command(iaxs[callno], AST_FRAME_IAX, AST_IAX_COMMAND_LAGRQ, 0, NULL, 0, -1);
		return 1;
	} else
		return 0;
}

static unsigned char compress_subclass(int subclass)
{
	int x;
	int power=-1;
	/* If it's 128 or smaller, just return it */
	if (subclass < AST_FLAG_SC_LOG)
		return subclass;
	/* Otherwise find its power */
	for (x = 0; x < AST_MAX_SHIFT; x++) {
		if (subclass & (1 << x)) {
			if (power > -1) {
				ast_log(LOG_WARNING, "Can't compress subclass %d\n", subclass);
				return 0;
			} else
				power = x;
		}
	}
	return power | AST_FLAG_SC_LOG;
}

static int uncompress_subclass(unsigned char csub)
{
	/* If the SC_LOG flag is set, return 2^csub otherwise csub */
	if (csub & AST_FLAG_SC_LOG)
		return 1 << (csub & ~AST_FLAG_SC_LOG & AST_MAX_SHIFT);
	else
		return csub;
}

static struct chan_iax_pvt *new_iax(void)
{
	struct chan_iax_pvt *tmp;
	tmp = malloc(sizeof(struct chan_iax_pvt));
	if (tmp) {
		memset(tmp, 0, sizeof(struct chan_iax_pvt));
		/* On my linux system, pipe's are more than 2x as fast as socketpairs */
		if (pipe(tmp->pipe)) {
			ast_log(LOG_WARNING, "Unable to create pipe: %s\n", strerror(errno));
			free(tmp);
			return NULL;
		}
		tmp->callno = -1;
		tmp->peercallno = -1;
		/* strncpy(tmp->context, context, sizeof(tmp->context)); */
		strncpy(tmp->exten, "s", sizeof(tmp->exten));
	}
	return tmp;
}

static int get_timelen(struct ast_frame *f)
{
	int timelen=0;
	switch(f->subclass) {
	case AST_FORMAT_G723_1:
		timelen = 30;
		break;
	case AST_FORMAT_GSM:
		timelen = 20;
		break;
	case AST_FORMAT_SLINEAR:
		timelen = f->datalen / 8;
		break;
	case AST_FORMAT_LPC10:
		timelen = 22;
		timelen += ((char *)(f->data))[7] & 0x1;
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to calculate timelen on %d packets\n", f->subclass);
	}
	return timelen;
}

#if 0
static struct ast_iax_frame *iaxfrdup(struct ast_iax_frame *fr)
{
	/* Malloc() a copy of a frame */
	struct ast_iax_frame *new = malloc(sizeof(struct ast_iax_frame));
	if (new) 
		memcpy(new, fr, sizeof(struct ast_iax_frame));	
	return new;
}
#endif

static struct ast_iax_frame *iaxfrdup2(struct ast_iax_frame *fr, int ch)
{
	/* Malloc() a copy of a frame */
	struct ast_iax_frame *new = malloc(sizeof(struct ast_iax_frame));
	if (new) {
		memcpy(new, fr, sizeof(struct ast_iax_frame));	
		new->f = ast_frdup(fr->f);
		/* Copy full header */
		if (ch) {
			memcpy(new->f->data - sizeof(struct ast_iax_full_hdr),
					fr->f->data - sizeof(struct ast_iax_full_hdr), 
						sizeof(struct ast_iax_full_hdr));
			/* Grab new data pointer */
			new->data = new->f->data - (fr->f->data - fr->data);
		} else {
			new->data = NULL;
			new->datalen = 0;
		}
	}
	return new;
}

#define NEW_PREVENT 0
#define NEW_ALLOW 	1
#define NEW_FORCE 	2

static int find_callno(short callno, short dcallno ,struct sockaddr_in *sin, int new)
{
	int res = -1;
	int x;
	int start;
	if (new <= NEW_ALLOW) {
		/* Look for an existing connection first */
		for (x=0;x<AST_IAX_MAX_CALLS;x++) {
			if (iaxs[x]) {
				/* Look for an exact match */
				if ((sin->sin_port == iaxs[x]->addr.sin_port) &&
				    (sin->sin_addr.s_addr == iaxs[x]->addr.sin_addr.s_addr) &&
					((callno == iaxs[x]->peercallno) || /* Our expected source call number is the same */
					 ((dcallno == x) && (iaxs[x]->peercallno = -1)) 
					 /* We have no expected source number, and the destination is right */
					 )) {
					res = x;
					break;
				}
			}
		}
	}
	if ((res < 0) && (new >= NEW_ALLOW)) {
		/* Create a new one */
		start = nextcallno;
		for (x = nextcallno + 1; iaxs[x] && (x != start); x = (x + 1) % AST_IAX_MAX_CALLS) 
		if (x == start) {
			ast_log(LOG_WARNING, "Unable to accept more calls\n");
			return -1;
		}
		iaxs[x] = new_iax();
		if (iaxs[x]) {
			if (option_debug)
				ast_log(LOG_DEBUG, "Creating new call structure %d\n", x);
			iaxs[x]->addr.sin_port = sin->sin_port;
			iaxs[x]->addr.sin_family = sin->sin_family;
			iaxs[x]->addr.sin_addr.s_addr = sin->sin_addr.s_addr;
			iaxs[x]->peercallno = callno;
			iaxs[x]->callno = x;
			iaxs[x]->pingtime = DEFAULT_RETRY_TIME;
			ast_sched_add(sched, ping_time * 1000, send_ping, (void *)x);
			ast_sched_add(sched, lagrq_time * 1000, send_lagrq, (void *)x);
		} else
			ast_log(LOG_DEBUG, "Out of memory\n");
		res = x;
		nextcallno = x;
	}
	return res;
}

static int iax_send(struct chan_iax_pvt *pvt, struct ast_frame *f, unsigned int ts, int seqno);

static int do_deliver(void *data)
{
	/* Just deliver the packet by writing it to half of the pipe. */
	struct ast_iax_frame *fr = data;
	unsigned int ts;
	if (iaxs[fr->callno]) {
		if (fr->f->frametype == AST_FRAME_IAX) {
			/* We have to treat some of these packets specially because
			   they're LAG measurement packets */
			if (fr->f->subclass == AST_IAX_COMMAND_LAGRQ) {
				/* If we got a queued request, build a reply and send it */
				fr->f->subclass = AST_IAX_COMMAND_LAGRP;
				iax_send(iaxs[fr->callno], fr->f, fr->ts, -1);
			} else if (fr->f->subclass == AST_IAX_COMMAND_LAGRP) {
				/* This is a reply we've been given, actually measure the difference */
				ts = calc_timestamp(iaxs[fr->callno], 0);
				iaxs[fr->callno]->lag = ts - fr->ts;
			}
		} else {
			ast_fr_fdwrite(iaxs[fr->callno]->pipe[1], fr->f);
		}
	}
	/* Free the packet */
	ast_frfree(fr->f);
	/* And our iax frame */
	free(fr);
	/* And don't run again */
	return 0;
}

static int handle_error()
{
	/* XXX Ideally we should figure out why an error occured and then abort those
	   rather than continuing to try.  Unfortunately, the published interface does
	   not seem to work XXX */
#if 0
	struct sockaddr_in *sin;
	int res;
	struct msghdr m;
	struct sock_extended_err e;
	m.msg_name = NULL;
	m.msg_namelen = 0;
	m.msg_iov = NULL;
	m.msg_control = &e;
	m.msg_controllen = sizeof(e);
	m.msg_flags = 0;
	res = recvmsg(netsocket, &m, MSG_ERRQUEUE);
	if (res < 0)
		ast_log(LOG_WARNING, "Error detected, but unable to read error: %s\n", strerror(errno));
	else {
		if (m.msg_controllen) {
			sin = (struct sockaddr_in *)SO_EE_OFFENDER(&e);
			if (sin) 
				ast_log(LOG_WARNING, "Receive error from %s\n", inet_ntoa(sin->sin_addr));
			else
				ast_log(LOG_WARNING, "No address detected??\n");
		} else {
			ast_log(LOG_WARNING, "Local error: %s\n", strerror(e.ee_errno));
		}
	}
#endif
	return 0;
}

#ifdef IAX_SIMULATOR
static int __send_packet(struct ast_iax_frame *f)
#else
static int send_packet(struct ast_iax_frame *f)
#endif
{
	int res;
	if (option_debug)
		ast_log(LOG_DEBUG, "Sending %d on %d/%d to %s:%d\n", f->ts, f->callno, iaxs[f->callno]->peercallno, inet_ntoa(iaxs[f->callno]->addr.sin_addr), ntohs(iaxs[f->callno]->addr.sin_port));
	/* Don't send if there was an error, but return error instead */
	if (f->callno < 0) {
		ast_log(LOG_WARNING, "Call number = %d\n", f->callno);
		return -1;
	}
	if (iaxs[f->callno]->error)
		return -1;
	res = sendto(netsocket, f->data, f->datalen, 0, &iaxs[f->callno]->addr, 
					sizeof(iaxs[f->callno]->addr));
	if (res < 0) { 
		ast_log(LOG_WARNING, "Received error: %s\n", strerror(errno));
		handle_error();
	} else 
		res = 0;
	return res;
}

#ifdef IAX_SIMULATOR

/* Average amount of delay in the connection */
static int average_delay = 0;
/* Permitted deviation either side of the average delay */
static int delay_deviation = 0;
/* Percent chance that a packet arrives O.K. */
static int reliability = 100;

static int iax_sim_calc_delay()
{
	int ms;
	ms = average_delay - delay_deviation;
	ms += ((float)(delay_deviation * 2)) * rand() / (RAND_MAX + 1.0);
	if (ms < 0)
		ms = 0;
	if ((float)rand()/(RAND_MAX + 1.0) < ((float)reliability)/100)
		return ms;
	else
		return -1;
}

static int d_send_packet(void *v)
{
	struct ast_iax_frame *f = (struct ast_iax_frame *)v;
	if (iaxs[f->callno])
		__send_packet(f);
	ast_frfree(f->f);
	free(f);
	return 0;
}

static int send_packet(struct ast_iax_frame *f)
{
	struct ast_iax_frame *fn;
	int ms;
	ms = iax_sim_calc_delay();
	if (ms == 0)
		return __send_packet(f);
	else if (ms > 0) {
		/* Make a completely independent frame, in case the other
		   is destroyed -- still doesn't make things like hangups
		   arrive if the main channel is destroyed, but close enough */
		fn = iaxfrdup2(f, 1);
		ast_sched_add(sched, ms, d_send_packet, fn);
	} /* else we drop the packet */
	return 0;
}

static int iax_sim_set(int fd, int argc, char *argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	if (!strcasecmp(argv[2], "delay")) 
		average_delay = atoi(argv[3]);
	else if (!strcasecmp(argv[2], "deviation"))
		delay_deviation = atoi(argv[3]);
	else if (!strcasecmp(argv[2], "reliability"))
		reliability = atoi(argv[3]);
	else 
		return RESULT_SHOWUSAGE;
	if (reliability > 100)
		reliability = 100;
	if (reliability < 0)
		reliability = 0;
	if (delay_deviation > average_delay)
		delay_deviation = average_delay;
	return RESULT_SUCCESS;
}

static char delay_usage[] = 
"Usage: sim set delay <value>\n"
"       Configure the IAX network simulator to generate average\n"
"       delays equal to the specified value (in milliseconds).\n";

static char deviation_usage[] = 
"Usage: sim set deviation <value>\n"
"       Configures the IAX network simulator's deviation value.\n"
"       The delays generated by the simulator will always be within\n"
"       this value of milliseconds (postive or negative) of the \n"
"       average delay.\n";

static char reliability_usage[] = 
"Usage: sim set reliability <value>\n"
"       Configure the probability that a packet will be delivered.\n"
"       The value specified is a percentage from 0 to 100\n";

static int iax_sim_show(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	ast_cli(fd, "Average Delay:   %d ms\n", average_delay);
	ast_cli(fd, "Delay Deviation: %d ms\n", delay_deviation);
	ast_cli(fd, "Reliability:     %d %\n", reliability);
	return RESULT_SUCCESS;
}

static char sim_show_usage[] = 
"Usage: sim show\n"
"       Displays average delay, deviation, and reliability\n"
"       used by the network simulator.\n";

static struct ast_cli_entry delay_cli = 
{ { "sim", "set", "delay", NULL }, iax_sim_set, "Sets simulated average delay", delay_usage };
static struct ast_cli_entry deviation_cli = 
{ { "sim", "set", "deviation", NULL }, iax_sim_set, "Sets simulated delay deviation", deviation_usage };
static struct ast_cli_entry reliability_cli = 
{ { "sim", "set", "reliability", NULL }, iax_sim_set, "Sets simulated reliability", reliability_usage };
static struct ast_cli_entry sim_show_cli = 
{ { "sim", "show", NULL }, iax_sim_show, "Displays simulation parameters", sim_show_usage };

#endif

static int attempt_transmit(void *data)
{
	/* Attempt to transmit the frame to the remote peer */
	char zero = 0;
	struct ast_iax_frame *f = data;
	int res = 0;
	/* Make sure this call is still active */
	if (iaxs[f->callno]) {
		if ((f->retries == -1) /* Already ACK'd */ ||
		    (f->retries >= max_retries) /* Too many attempts */) {
				/* Record an error if we've transmitted too many times */
				if (f->retries >= max_retries) {
					ast_log(LOG_WARNING, "Max retries exceeded to host %s (type = %d, subclass = %d, ts=%d, seqno=%d)\n", inet_ntoa(iaxs[f->callno]->addr.sin_addr), f->f->frametype, f->f->subclass, f->ts, f->seqno);
					iaxs[f->callno]->error = ETIMEDOUT;
					/* Send a bogus frame to wake up the waiting process */
					write(iaxs[f->callno]->pipe[1], &zero, 1);
				}
				/* Don't attempt delivery, just remove it from the queue */
				pthread_mutex_lock(&iaxq.lock);
				if (f->prev) 
					f->prev->next = f->next;
				else
					iaxq.head = f->next;
				if (f->next)
					f->next->prev = f->prev;
				else
					iaxq.tail = f->prev;
				iaxq.count--;
				pthread_mutex_unlock(&iaxq.lock);
		} else {
			/* Attempt transmission */
			send_packet(f);
			f->retries++;
			/* Try again later after 10 times as long */
			f->retrytime *= 10;
			if (f->retrytime > MAX_RETRY_TIME)
				f->retrytime = MAX_RETRY_TIME;
			ast_sched_add(sched, f->retrytime, attempt_transmit, f);
			res=0;
		}
	}
	/* Do not try again */
	return res;
}

static int iax_set_jitter(int fd, int argc, char *argv[])
{
	if ((argc != 4) && (argc != 5))
		return RESULT_SHOWUSAGE;
	if (argc == 4) {
		max_jitter_buffer = atoi(argv[3]);
		if (max_jitter_buffer < 0)
			max_jitter_buffer = 0;
	} else {
		if (argc == 5) {
			pthread_mutex_lock(&iaxs_lock);
			if ((atoi(argv[3]) >= 0) && (atoi(argv[3]) < AST_IAX_MAX_CALLS)) {
				if (iaxs[atoi(argv[3])]) {
					iaxs[atoi(argv[3])]->jitterbuffer = atoi(argv[4]);
					if (iaxs[atoi(argv[3])]->jitterbuffer < 0)
						iaxs[atoi(argv[3])]->jitterbuffer = 0;
				} else
					ast_cli(fd, "No such call '%d'\n", atoi(argv[3]));
			} else
				ast_cli(fd, "%d is not a valid call number\n", atoi(argv[3]));
			pthread_mutex_unlock(&iaxs_lock);
		}
	}
	return RESULT_SUCCESS;
}

static char jitter_usage[] = 
"Usage: iax set jitter [callid] <value>\n"
"       If used with a callid, it sets the jitter buffer to the given static\n"
"value (until its next calculation).  If used without a callid, the value is used\n"
"to establish the maximum excess jitter buffer that is permitted before the jitter\n"
"buffer size is reduced.";

static struct ast_cli_entry cli_set_jitter = 
{ { "iax", "set", "jitter", NULL }, iax_set_jitter, "Sets IAX jitter buffer", jitter_usage };

static unsigned int calc_rxstamp(struct chan_iax_pvt *p);

static int schedule_delivery(struct ast_iax_frame *fr)
{
	/* XXX FIXME: I should delay delivery with a sliding jitter buffer XXX */
	int ms,x;
	int drops[MEMORY_SIZE];
	int min, max=0, maxone=0,y,z, match;
	/* ms is a measure of the "lateness" of the packet relative to the first
	   packet we received, which always has a lateness of 1.  */
	ms = calc_rxstamp(iaxs[fr->callno]) - fr->ts;
	
	/* Rotate our history queue of "lateness".  Don't worry about those initial
	   zeros because the first entry will always be zero */
	for (x=0;x<MEMORY_SIZE - 1;x++) 
		iaxs[fr->callno]->history[x] = iaxs[fr->callno]->history[x+1];
	/* Add a history entry for this one */
	iaxs[fr->callno]->history[x] = ms;

	/* Initialize the minimum to reasonable values.  It's too much
	   work to do the same for the maximum, repeatedly */
	min=iaxs[fr->callno]->history[0];
	for (z=0;z < iax_dropcount + 1;z++) {
		/* Start very pessimistic ;-) */
		max=-999999999;
		for (x=0;x<MEMORY_SIZE;x++) {
			if (max < iaxs[fr->callno]->history[x]) {
				/* We have a candidate new maximum value.  Make
				   sure it's not in our drop list */
				match = 0;
				for (y=0;!match && (y<z);y++)
					match |= (drops[y] == x);
				if (!match) {
					/* It's not in our list, use it as the new maximum */
					max = iaxs[fr->callno]->history[x];
					maxone = x;
				}
				
			}
			if (!z) {
				/* On our first pass, find the minimum too */
				if (min > iaxs[fr->callno]->history[x])
					min = iaxs[fr->callno]->history[x];
			}
		}
#if 1
		drops[z] = maxone;
#endif
	}
	/* Just for reference, keep the "jitter" value, the difference between the
	   earliest and the latest. */
	iaxs[fr->callno]->jitter = max - min;	
	
	/* If our jitter buffer is too big (by a significant margin), then we slowly
	   shrink it by about 1 ms each time to avoid letting the change be perceived */
	if (max < iaxs[fr->callno]->jitterbuffer - max_jitter_buffer)
		iaxs[fr->callno]->jitterbuffer -= 2;

#if 1
	/* Constrain our maximum jitter buffer appropriately */
	if (max > min + maxjitterbuffer) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Constraining buffer from %d to %d + %d\n", max, min , maxjitterbuffer);
		max = min + maxjitterbuffer;
	}
#endif

	/* If our jitter buffer is smaller than our maximum delay, grow the jitter
	   buffer immediately to accomodate it (and a little more).  */
	if (max > iaxs[fr->callno]->jitterbuffer)
		iaxs[fr->callno]->jitterbuffer = max 
			/* + ((float)iaxs[fr->callno]->jitter) * 0.1 */;
		

	if (option_debug)
		ast_log(LOG_DEBUG, "min = %d, max = %d, jb = %d, lateness = %d\n", min, max, iaxs[fr->callno]->jitterbuffer, ms);
	
	/* Subtract the lateness from our jitter buffer to know how long to wait
	   before sending our packet.  */
	ms = iaxs[fr->callno]->jitterbuffer - ms;
	
	if (!use_jitterbuffer)
		ms = 0;

	if (ms < 1) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Calculated ms is %d\n", ms);
		/* Don't deliver it more than 4 ms late */
		if ((ms > -4) || (fr->f->frametype != AST_FRAME_VOICE)) {
			do_deliver(fr);
		}
		else {
			/* Free the packet */
			ast_frfree(fr->f);
			/* And our iax frame */
			free(fr);
		}
	} else {
		if (option_debug)
			ast_log(LOG_DEBUG, "Scheduling delivery in %d ms\n", ms);
		ast_sched_add(sched, ms, do_deliver, fr);
	}
	return 0;
}

static int iax_transmit(struct ast_iax_frame *fr)
{
	/* Lock the queue and place this packet at the end */
	fr->next = NULL;
	fr->prev = NULL;
	/* By setting this to 0, the network thread will send it for us, and
	   queue retransmission if necessary */
	fr->sentyet = 0;
	pthread_mutex_lock(&iaxq.lock);
	if (!iaxq.head) {
		/* Empty queue */
		iaxq.head = fr;
		iaxq.tail = fr;
	} else {
		/* Double link */
		iaxq.tail->next = fr;
		fr->prev = iaxq.tail;
		iaxq.tail = fr;
	}
	iaxq.count++;
	pthread_mutex_unlock(&iaxq.lock);
	/* Wake up the network thread */
	pthread_kill(netthreadid, SIGURG);
	return 0;
}



static int iax_digit(struct ast_channel *c, char digit)
{
	return send_command(c->pvt->pvt, AST_FRAME_DTMF, digit, 0, NULL, 0, -1);
}

static int iax_sendtext(struct ast_channel *c, char *text)
{
	
	return send_command(c->pvt->pvt, AST_FRAME_TEXT,
		0, 0, text, strlen(text) + 1, -1);
}

static int create_addr(struct sockaddr_in *sin, char *peer)
{
	struct hostent *hp;
	struct iax_peer *p;
	sin->sin_family = AF_INET;
	pthread_mutex_lock(&peerl.lock);
	p = peerl.peers;
	while(p) {
		if (!strcasecmp(p->name, peer)) {
			sin->sin_addr = p->addr.sin_addr;
			sin->sin_port = p->addr.sin_port;
			break;
		}
		p = p->next;
	}
	pthread_mutex_unlock(&peerl.lock);
	if (!p) {
		hp = gethostbyname(peer);
		if (hp) {
			memcpy(&sin->sin_addr, hp->h_addr, sizeof(sin->sin_addr));
			sin->sin_port = htons(AST_DEFAULT_IAX_PORTNO);
			return 0;
		} else
			return -1;
	} else
		return 0;
}
static int iax_call(struct ast_channel *c, char *dest, int timeout)
{
	struct sockaddr_in sin;
	char host[256];
	char *rdest;
	char *rcontext;
	char *username;
	char *hname;
	char requeststr[256] = "";
	char myrdest [5] = "s";
	char *portno = NULL;
	if ((c->state != AST_STATE_DOWN) && (c->state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "Line is already in use (%s)?\n", c->name);
		return -1;
	}
	strncpy(host, dest, sizeof(host));
	strtok(host, "/");
	/* If no destination extension specified, use 's' */
	rdest = strtok(NULL, "/");
	if (!rdest) 
		rdest = myrdest;
	strtok(rdest, "@");
	rcontext = strtok(NULL, "@");
	strtok(host, "@");
	username = strtok(NULL, "@");
	if (username) {
		/* Really the second argument is the host, not the username */
		hname = username;
		username = host;
	} else {
		hname = host;
	}
	if (strtok(hname, ":")) {
		strtok(hname, ":");
		portno = strtok(hname, ":");
	}
	if (create_addr(&sin, hname)) {
		ast_log(LOG_WARNING, "No address associated with '%s'\n", hname);
		return -1;
	}
	if (portno) {
		sin.sin_port = htons(atoi(portno));
	}
	/* Now we build our request string */
#define MYSNPRINTF snprintf(requeststr + strlen(requeststr), sizeof(requeststr) - strlen(requeststr), 
	MYSNPRINTF "exten=%s;", rdest);
	if (c->callerid)
		MYSNPRINTF "callerid=%s;", c->callerid);
	if (c->dnid)
		MYSNPRINTF "dnid=%s;", c->dnid);
	if (rcontext)
		MYSNPRINTF "context=%s;", rcontext);
	if (username)
		MYSNPRINTF "username=%s;", username);
	MYSNPRINTF "formats=%d;", c->format);
	MYSNPRINTF "version=%d;", AST_IAX_PROTO_VERSION);
	/* Trim the trailing ";" */
	if (strlen(requeststr))
		requeststr[strlen(requeststr) - 1] = '\0';
	/* Transmit the string in a "NEW" request */
	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Calling using options '%s'\n", requeststr);
	send_command((struct chan_iax_pvt *)c->pvt->pvt, AST_FRAME_IAX,
		AST_IAX_COMMAND_NEW, 0, requeststr, strlen(requeststr) + 1, -1);
	c->state = AST_STATE_RINGING;
	return 0;
}

static void iax_destroy(int callno)
{
	char zero=0;
	struct chan_iax_pvt *pvt = iaxs[callno];
	if (pvt) {
		if (pvt->owner) {
			/* If there's an owner, prod it to give up */
			write(pvt->pipe[1], &zero, 1);
			return;
		}
		iaxs[callno] = NULL;
		close(pvt->pipe[0]);
		close(pvt->pipe[1]);
		free(pvt);
	}
}

static int iax_hangup(struct ast_channel *c) {
	struct chan_iax_pvt *pvt = c->pvt->pvt;
	/* Send the hangup unless we have had a transmission error */
	if (!pvt->error) {
		send_command(pvt, AST_FRAME_IAX, AST_IAX_COMMAND_HANGUP, 0, NULL, 0, -1);
		/* Wait for the network thread to transmit our command -- of course, if
		   it doesn't, that's okay too -- the other end will find out
		   soon enough, but it's a nicity if it can know now.  */
		sleep(1);
	}
	pthread_mutex_lock(&iaxs_lock);
	c->pvt->pvt = NULL;
	pvt->owner = NULL;
	pthread_mutex_lock(&usecnt_lock);
	usecnt--;
	if (usecnt < 0) 
		ast_log(LOG_WARNING, "Usecnt < 0???\n");
	pthread_mutex_unlock(&usecnt_lock);
	ast_update_use_count();
	if (option_verbose > 2) 
		ast_verbose( VERBOSE_PREFIX_3 "Hungup '%s'\n", c->name);
	iax_destroy(pvt->callno);
	pthread_mutex_unlock(&iaxs_lock);
	return 0;
}

static struct ast_frame *iax_read(struct ast_channel *c) 
{
	struct chan_iax_pvt *pvt = c->pvt->pvt;
	struct ast_frame *f;
	if (pvt->error) {
		ast_log(LOG_DEBUG, "Connection closed, error: %s\n", strerror(pvt->error));
		return NULL;
	}
	f = ast_fr_fdread(pvt->pipe[0]);
	if (f) {
		if ((f->frametype == AST_FRAME_CONTROL) &&
		    (f->subclass == AST_CONTROL_ANSWER))
				c->state = AST_STATE_UP;
	}
	return f;
}

static int iax_answer(struct ast_channel *c)
{
	struct chan_iax_pvt *pvt = c->pvt->pvt;
	if (option_debug)
		ast_log(LOG_DEBUG, "Answering\n");
	return send_command(pvt, AST_FRAME_CONTROL, AST_CONTROL_ANSWER, 0, NULL, 0, -1);
}

static int iax_write(struct ast_channel *c, struct ast_frame *f);

static struct ast_channel *ast_iax_new(struct chan_iax_pvt *i, int state)
{
	struct ast_channel *tmp;
	tmp = ast_channel_alloc();
	if (tmp) {
		snprintf(tmp->name, sizeof(tmp->name), "IAX[%s:%d]/%d", inet_ntoa(i->addr.sin_addr), ntohs(i->addr.sin_port), i->callno);
		tmp->type = type;
		tmp->fd = i->pipe[0];
		/* We can support any format by default, until we get restricted */
		tmp->format = iax_capability;
		tmp->pvt->pvt = i;
		tmp->pvt->send_digit = iax_digit;
		tmp->pvt->send_text = iax_sendtext;
		tmp->pvt->call = iax_call;
		tmp->pvt->hangup = iax_hangup;
		tmp->pvt->answer = iax_answer;
		tmp->pvt->read = iax_read;
		tmp->pvt->write = iax_write;
		if (strlen(i->callerid))
			tmp->callerid = strdup(i->callerid);
		if (strlen(i->dnid))
			tmp->dnid = strdup(i->dnid);
		strncpy(tmp->context, i->context, sizeof(tmp->context));
		strncpy(tmp->exten, i->exten, sizeof(tmp->exten));
		i->owner = tmp;
		tmp->state = state;
		pthread_mutex_lock(&usecnt_lock);
		usecnt++;
		pthread_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(tmp)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
				ast_hangup(tmp);
				tmp = NULL;
			}
		}
	}
	return tmp;
}

static unsigned int calc_timestamp(struct chan_iax_pvt *p, unsigned int ts)
{
	struct timeval tv;
	unsigned int ms;
	if (!p->offset.tv_sec && !p->offset.tv_usec)
		gettimeofday(&p->offset, NULL);
	/* If the timestamp is specified, just send it as is */
	if (ts)
		return ts;
	gettimeofday(&tv, NULL);
	ms = (tv.tv_sec - p->offset.tv_sec) * 1000 + (tv.tv_usec - p->offset.tv_usec) / 1000;
	/* We never send the same timestamp twice, so fudge a little if we must */
	if (ms <= p->lastsent)
		ms = p->lastsent + 1;
	p->lastsent = ms;
	return ms;
}

static unsigned int calc_rxstamp(struct chan_iax_pvt *p)
{
	/* Returns where in "receive time" we are */
	struct timeval tv;
	unsigned int ms;
	if (!p->rxcore.tv_sec && !p->rxcore.tv_usec)
		gettimeofday(&p->rxcore, NULL);
	gettimeofday(&tv, NULL);
	ms = (tv.tv_sec - p->offset.tv_sec) * 1000 + (tv.tv_usec - p->offset.tv_usec) / 1000;
	return ms;
}
static int iax_send(struct chan_iax_pvt *pvt, struct ast_frame *f, unsigned int ts, int seqno)
{
	/* Queue a packet for delivery on a given private structure.  Use "ts" for
	   timestamp, or calculate if ts is 0 */
	struct ast_iax_full_hdr *fh;
	struct ast_iax_mini_hdr *mh;
	struct ast_iax_frame *fr;
	int res;
	unsigned int lastsent;
	/* Allocate an ast_iax_frame */
	fr = malloc(sizeof(struct ast_iax_frame));
	if (!fr) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}
	if (!pvt) {
		ast_log(LOG_WARNING, "No private structure for packet (%d)?\n", fr->callno);
		free(fr);
		return -1;
	}
	/* Isolate our frame for transmission */
	fr->f = ast_frdup(f);
	if (!fr->f) {
		ast_log(LOG_WARNING, "Out of memory\n");
		free(fr);
		return -1;
	}
	if (fr->f->offset < sizeof(struct ast_iax_full_hdr)) {
		ast_log(LOG_WARNING, "Packet from '%s' not friendly\n", fr->f->src);
		free(fr);
		return -1;
	}
	lastsent = pvt->lastsent;
	fr->ts = calc_timestamp(pvt, ts);
	if (!fr->ts) {
		ast_log(LOG_WARNING, "timestamp is 0?\n");
		return -1;
	}
	fr->callno = pvt->callno;
	if (((fr->ts & 0xFFFF0000L) != (lastsent & 0xFFFF0000L))
		/* High two bits of timestamp differ */ ||
	    (fr->f->frametype != AST_FRAME_VOICE) 
		/* or not a voice frame */ || 
		(fr->f->subclass != pvt->svoiceformat) 
		/* or new voice format */ ) {
		/* We need a full frame */
		if (seqno > -1)
			fr->seqno = seqno;
		else
			fr->seqno = pvt->oseqno++;
		fh = (struct ast_iax_full_hdr *)(fr->f->data - sizeof(struct ast_iax_full_hdr));
		fh->callno = htons(fr->callno | AST_FLAG_FULL);
		fh->ts = htonl(fr->ts);
		fh->seqno = htons(fr->seqno);
		fh->type = fr->f->frametype & 0xFF;
		fh->csub = compress_subclass(fr->f->subclass);
#if 0
		fh->subclasshigh = (fr->f->subclass & 0xFF0000) >> 16;
		fh->subclasslow = htons(fr->f->subclass & 0xFFFF);
#endif
		fh->dcallno = htons(pvt->peercallno);
		fr->datalen = fr->f->datalen + sizeof(struct ast_iax_full_hdr);
		fr->data = fh;
		fr->retries = 0;
		/* Retry after 2x the ping time has passed */
		fr->retrytime = pvt->pingtime * 2;
		if (fr->retrytime < MIN_RETRY_TIME)
			fr->retrytime = MIN_RETRY_TIME;
		if (fr->retrytime > MAX_RETRY_TIME)
			fr->retrytime = MAX_RETRY_TIME;
		/* Acks' don't get retried */
		if ((f->frametype == AST_FRAME_IAX) && (f->subclass == AST_IAX_COMMAND_ACK))
			fr->retries = -1;
		if (f->frametype == AST_FRAME_VOICE) {
			pvt->svoiceformat = f->subclass;
		}
		res = iax_transmit(fr);
	} else {
		/* Mini-frames have no sequence number */
		fr->seqno = -1;
		/* Mini frame will do */
		mh = (struct ast_iax_mini_hdr *)(fr->f->data - sizeof(struct ast_iax_mini_hdr));
		mh->callno = htons(fr->callno);
		mh->ts = htons(fr->ts & 0xFFFF);
		fr->datalen = fr->f->datalen + sizeof(struct ast_iax_mini_hdr);
		fr->data = mh;
		fr->retries = -1;
		res = iax_transmit(fr);
	}
	return res;
}



static int iax_show_users(int fd, int argc, char *argv[])
{
#define FORMAT "%-15.15s  %-15.15s  %-15.15s  %-15.15s  %-5.5s\n"
	struct iax_user *user;
	if (argc != 3) 
		return RESULT_SHOWUSAGE;
	pthread_mutex_lock(&userl.lock);
	ast_cli(fd, FORMAT, "Username", "Secret", "Authen", "Def.Context", "A/C");
	for(user=userl.users;user;user=user->next) {
		ast_cli(fd, FORMAT, user->name, user->secret, user->methods, 
				user->contexts ? user->contexts->context : context,
				user->ha ? "Yes" : "No");
	}
	pthread_mutex_unlock(&userl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
}

static int iax_show_peers(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-15.15s  %-15.15s  %-15.15s  %-15.15s  %s\n"
#define FORMAT "%-15.15s  %-15.15s  %-15.15s  %-15.15s  %d\n"
	struct iax_peer *peer;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	pthread_mutex_lock(&peerl.lock);
	ast_cli(fd, FORMAT2, "Name", "Username", "Host", "Mask", "Port");
	for (peer = peerl.peers;peer;peer = peer->next) {
		char nm[20];
		strncpy(nm, inet_ntoa(peer->mask), sizeof(nm));
		ast_cli(fd, FORMAT, peer->name, 
					peer->username ? peer->username : "(Any)",
					peer->addr.sin_addr.s_addr ? inet_ntoa(peer->addr.sin_addr) : "(Any)",
					nm,
					ntohs(peer->addr.sin_port));
	}
	pthread_mutex_unlock(&peerl.lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int iax_show_channels(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-15.15s  %-10.10s  %-11.11s  %-11.11s  %-7.7s  %-6.6s  %s\n"
#define FORMAT  "%-15.15s  %-10.10s  %5.5d/%5.5d  %5.5d/%5.5d  %-5.5dms  %-4.4dms  %d\n"
	int x;
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	pthread_mutex_lock(&iaxs_lock);
	ast_cli(fd, FORMAT2, "Peer", "Username", "ID (Lo/Rem)", "Seq (Tx/Rx)", "Lag", "Jitter", "Format");
	for (x=0;x<AST_IAX_MAX_CALLS;x++)
		if (iaxs[x]) 
			ast_cli(fd, FORMAT, inet_ntoa(iaxs[x]->addr.sin_addr), 
						strlen(iaxs[x]->username) ? iaxs[x]->username : "(None)", 
						iaxs[x]->callno, iaxs[x]->peercallno, 
						iaxs[x]->oseqno, iaxs[x]->iseqno, 
						iaxs[x]->lag,
						iaxs[x]->jitter,
						iaxs[x]->voiceformat);
	pthread_mutex_unlock(&iaxs_lock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static char show_users_usage[] = 
"Usage: iax show users\n"
"       Lists all users known to the IAX (Inter-Asterisk eXchange) subsystem.\n";

static char show_channels_usage[] = 
"Usage: iax show channels\n"
"       Lists all currently active IAX channels.\n";

static char show_peers_usage[] = 
"Usage: iax show peers\n"
"       Lists all known IAX peers.\n";

static struct ast_cli_entry  cli_show_users = 
	{ { "iax", "show", "users", NULL }, iax_show_users, "Show defined IAX users", show_users_usage };
static struct ast_cli_entry  cli_show_channels =
	{ { "iax", "show", "channels", NULL }, iax_show_channels, "Show active IAX channels", show_channels_usage };
static struct ast_cli_entry  cli_show_peers =
	{ { "iax", "show", "peers", NULL }, iax_show_peers, "Show defined IAX peers", show_peers_usage };

static int iax_write(struct ast_channel *c, struct ast_frame *f)
{
	struct chan_iax_pvt *i = c->pvt->pvt;
	/* If there's an outstanding error, return failure now */
	if (i->error) {
		ast_log(LOG_DEBUG, "Write error: %s\n", strerror(errno));
		return -1;
	}
	/* Don't waste bandwidth sending null frames */
	if (f->frametype == AST_FRAME_NULL)
		return 0;
	/* Simple, just queue for transmission */
	return iax_send(i, f, 0, -1);
}

static int send_command(struct chan_iax_pvt *i, char type, int command, unsigned int ts, char *data, int datalen, int seqno)
{
	struct ast_frame f;
	f.frametype = type;
	f.subclass = command;
	f.datalen = datalen;
	f.timelen = 0;
	f.mallocd = 0;
	f.offset = 0;
	f.src = __FUNCTION__;
	f.data = data;
	return iax_send(i, &f, ts, seqno);
}

static int apply_context(struct iax_context *con, char *context)
{
	while(con) {
		if (!strcmp(con->context, context))
			return -1;
		con = con->next;
	}
	return 0;
}

static int apply_ha(struct iax_ha *ha, struct sockaddr_in *sin)
{
	/* Start optimistic */
	int res = IAX_SENSE_ALLOW;
	while(ha) {
		/* For each rule, if this address and the netmask = the net address
		   apply the current rule */
		if ((sin->sin_addr.s_addr & ha->netmask.s_addr) == (ha->netaddr.s_addr))
			res = ha->sense;
		ha = ha->next;
	}
	return res;
}

static int iax_getformats(int callno, char *orequest)
{
	char *var, *value;
	char request[256];
	strncpy(request, orequest, sizeof(request));
	var = strtok(request, ";");
	while(var) {
		value = strchr(var, '=');
		if (value) {
			*value='\0';
			value++;
			if (!strcmp(var, "formats")) {
				iaxs[callno]->peerformats = atoi(value);
			} else 
				ast_log(LOG_WARNING, "Unknown variable '%s' with value '%s'\n", var, value);
		}
		var = strtok(NULL, ";");
	}
	return 0;
}

static int check_access(int callno, struct sockaddr_in *sin, char *orequest, int requestl)
{
	/* Start pessimistic */
	int res = -1;
	int version = 1;
	char *var, *value;
	struct iax_user *user;
	char request[256];
	strncpy(request, orequest, sizeof(request));
	if (!iaxs[callno])
		return res;
	var = strtok(request, ";");
	while(var) {
		value = strchr(var, '=');
		if (value) { 
			*value='\0';
			value++;
			if (!strcmp(var, "exten")) 
				strncpy(iaxs[callno]->exten, value, sizeof(iaxs[callno]->exten));
			else if (!strcmp(var, "callerid"))
				strncpy(iaxs[callno]->callerid, value, sizeof(iaxs[callno]->callerid));
			else if (!strcmp(var, "dnid"))
				strncpy(iaxs[callno]->dnid, value, sizeof(iaxs[callno]->dnid));
			else if (!strcmp(var, "context"))
				strncpy(iaxs[callno]->context, value, sizeof(iaxs[callno]->context));
			else if (!strcmp(var, "username"))
				strncpy(iaxs[callno]->username, value, sizeof(iaxs[callno]->username));
			else if (!strcmp(var, "formats"))
				iaxs[callno]->peerformats = atoi(value);
			else if (!strcmp(var, "version"))
				version = atoi(value);
			else 
				ast_log(LOG_WARNING, "Unknown variable '%s' with value '%s'\n", var, value);
		}
		var = strtok(NULL, ";");
	}
	if (version > AST_IAX_PROTO_VERSION) {
		ast_log(LOG_WARNING, "Peer '%s' has too new a protocol version (%d) for me\n", 
			inet_ntoa(sin->sin_addr), version);
		return res;
	}
	pthread_mutex_lock(&userl.lock);
	/* Search the userlist for a compatible entry, and fill in the rest */
	user = userl.users;
	while(user) {
		if ((!strlen(iaxs[callno]->username) ||				/* No username specified */
			!strcmp(iaxs[callno]->username, user->name))	/* Or this username specified */
			&& (apply_ha(user->ha, sin) == IAX_SENSE_ALLOW)	/* Access is permitted from this IP */
			&& (!strlen(iaxs[callno]->context) ||			/* No context specified */
			     apply_context(user->contexts, iaxs[callno]->context))) {			/* Context is permitted */
			/* We found our match (use the first) */
			
			/* Store the requested username if not specified */
			if (!strlen(iaxs[callno]->username))
				strncpy(iaxs[callno]->username, user->name, sizeof(iaxs[callno]->username));
			/* And use the default context */
			if (!strlen(iaxs[callno]->context)) {
				if (user->contexts)
					strncpy(iaxs[callno]->context, user->contexts->context, sizeof(iaxs[callno]->context));
				else
					strncpy(iaxs[callno]->context, context, sizeof(iaxs[callno]->context));
			}
			/* Copy the secret */
			strncpy(iaxs[callno]->secret, user->secret, sizeof(iaxs[callno]->secret));
			/* And the permitted authentication methods */
			strncpy(iaxs[callno]->methods, user->methods, sizeof(iaxs[callno]->methods));
			res = 0;
			break;
		}
		user = user->next;	
	}
	pthread_mutex_unlock(&userl.lock);
	return res;
}

static int raw_hangup(struct sockaddr_in *sin, short src, short dst)
{
	struct ast_iax_full_hdr fh;
	fh.callno = htons(src | AST_FLAG_FULL);
	fh.dcallno = htons(dst);
	fh.ts = 0;
	fh.seqno = 0;
	fh.type = AST_FRAME_IAX;
	fh.csub = compress_subclass(AST_IAX_COMMAND_INVAL);
	if (option_debug)
		ast_log(LOG_DEBUG, "Raw Hangup\n");
	return sendto(netsocket, &fh, sizeof(fh), 0, sin, sizeof(*sin));
}

static int authenticate_request(struct chan_iax_pvt *p)
{
	char requeststr[256] = "";
	MYSNPRINTF "methods=%s;", p->methods);
	if (strstr(p->methods, "md5")) {
		/* Build the challenge */
		srand(time(NULL));
		snprintf(p->challenge, sizeof(p->challenge), "%d", rand());
		MYSNPRINTF "challenge=%s;", p->challenge);
	}
	MYSNPRINTF "username=%s;", p->username);
	if (strlen(requeststr))
		requeststr[strlen(requeststr) - 1] = '\0';
	return send_command(p, AST_FRAME_IAX, AST_IAX_COMMAND_AUTHREQ, 0, requeststr, strlen(requeststr) + 1, -1);
}

static int authenticate_verify(struct chan_iax_pvt *p, char *orequest)
{
	char requeststr[256] = "";
	char *var, *value, request[256];
	char md5secret[256] = "";
	char secret[256] = "";
	int res = -1; 
	int x;
	
	if (!(p->state & IAX_STATE_AUTHENTICATED))
		return res;
	strncpy(request, orequest, sizeof(request));
	var = strtok(request, ";");
	while(var) {
		value = strchr(var, '=');
		if (value) { 
			*value='\0';
			value++;
			if (!strcmp(var, "secret")) 
				strncpy(secret, value, sizeof(secret));
			else if (!strcmp(var, "md5secret"))
				strncpy(md5secret, value, sizeof(md5secret));
			else 
				ast_log(LOG_WARNING, "Unknown variable '%s' with value '%s'\n", var, value);
		}
		var = strtok(NULL, ";");
	}
	if (strstr(p->methods, "md5")) {
		struct MD5Context md5;
		unsigned char digest[16];
		MD5Init(&md5);
		MD5Update(&md5, p->challenge, strlen(p->challenge));
		MD5Update(&md5, p->secret, strlen(p->secret));
		MD5Final(digest, &md5);
		/* If they support md5, authenticate with it.  */
		for (x=0;x<16;x++)
			MYSNPRINTF "%2.2x", digest[x]);
		if (!strcasecmp(requeststr, md5secret))
			res = 0;
	} else if (strstr(p->methods, "plaintext")) {
		if (!strcmp(secret, p->secret))
			res = 0;
	}
	return res;
}

static int authenticate_reply(struct chan_iax_pvt *p, struct sockaddr_in *sin, char *orequest)
{
	struct iax_peer *peer;
	/* Start pessimistic */
	int res = -1;
	char request[256];
	char methods[80] = "";
	char requeststr[256] = "";
	char *var, *value;
	int x;
	strncpy(request, orequest, sizeof(request));
	var = strtok(request, ";");
	while(var) {
		value = strchr(var, '=');
		if (value) { 
			*value='\0';
			value++;
			if (!strcmp(var, "username")) 
				strncpy(p->username, value, sizeof(p->username));
			else if (!strcmp(var, "challenge"))
				strncpy(p->challenge, value, sizeof(p->challenge));
			else if (!strcmp(var, "methods"))
				strncpy(methods, value, sizeof(methods));
			else 
				ast_log(LOG_WARNING, "Unknown variable '%s' with value '%s'\n", var, value);
		}
		var = strtok(NULL, ";");
	}
	pthread_mutex_lock(&peerl.lock);
	peer = peerl.peers;
	while(peer) {
		if ((!strlen(p->peer) || !strcmp(p->peer, peer->name)) 
								/* No peer specified at our end, or this is the peer */
			 && (!strlen(peer->username) || (!strcmp(peer->username, p->username)))
			 					/* No username specified in peer rule, or this is the right username */
			 && (!peer->addr.sin_addr.s_addr || ((sin->sin_addr.s_addr & peer->mask.s_addr) == (peer->addr.sin_addr.s_addr & peer->mask.s_addr)))
			 					/* No specified host, or this is our host */
			) {
			/* We have a match, authenticate it. */
			res = 0;
			if (strstr(methods, "md5")) {
				struct MD5Context md5;
				unsigned char digest[16];
				MD5Init(&md5);
				MD5Update(&md5, p->challenge, strlen(p->challenge));
				MD5Update(&md5, peer->secret, strlen(peer->secret));
				MD5Final(digest, &md5);
				/* If they support md5, authenticate with it.  */
				MYSNPRINTF "md5secret=");
				for (x=0;x<16;x++)
					MYSNPRINTF "%2.2x", digest[x]);
				MYSNPRINTF ";");
			} else if (strstr(methods, "plaintext")) {
				MYSNPRINTF "secret=%s;", peer->secret);
			} else 
				res = -1;
			if (strlen(requeststr))
				requeststr[strlen(requeststr)-1] = '\0';
			if (!res)
				res = send_command(p, AST_FRAME_IAX, AST_IAX_COMMAND_AUTHREP, 0, requeststr, strlen(requeststr) + 1, -1);
			break;	
		}
		peer = peer->next;
	}
	pthread_mutex_unlock(&peerl.lock);
	return res;
}

static int socket_read(int *id, int fd, short events, void *cbdata)
{
	struct sockaddr_in sin;
	int res;
	int new = NEW_PREVENT;
	char buf[4096];
	char src[80];
	int len = sizeof(sin);
	int dcallno = -1;
	struct ast_iax_full_hdr *fh = (struct ast_iax_full_hdr *)buf;
	struct ast_iax_mini_hdr *mh = (struct ast_iax_mini_hdr *)buf;
	struct ast_iax_frame fr, *cur;
	struct ast_frame f;
	struct ast_channel *c;
	res = recvfrom(netsocket, buf, sizeof(buf), 0, &sin, &len);
	if (res < 0) {
		ast_log(LOG_WARNING, "Error: %s\n", strerror(errno));
		handle_error();
		return 1;
	}
	if (res < sizeof(struct ast_iax_mini_hdr)) {
		ast_log(LOG_WARNING, "midget packet received (%d of %d min)\n", res, sizeof(struct ast_iax_mini_hdr));
		return 1;
	}
	if (ntohs(mh->callno) & AST_FLAG_FULL) {
		/* Get the destination call number */
		dcallno = ntohs(fh->dcallno);
		/* Retrieve the type and subclass */
		f.frametype = fh->type;
		f.subclass = uncompress_subclass(fh->csub);
#if 0
		f.subclass = fh->subclasshigh << 16;
		f.subclass += ntohs(fh->subclasslow);
#endif
		if ((f.frametype == AST_FRAME_IAX) && (f.subclass == AST_IAX_COMMAND_NEW))
			new = NEW_ALLOW;
	}
	pthread_mutex_lock(&iaxs_lock);
	fr.callno = find_callno(ntohs(mh->callno) & ~AST_FLAG_FULL, dcallno, &sin, new);
	if ((fr.callno < 0) || !iaxs[fr.callno]) {
		/* A call arrived for a non-existant destination.  Unless it's an "inval"
		   frame, reply with an inval */
		if (ntohs(mh->callno) & AST_FLAG_FULL) {
			/* We can only raw hangup control frames */
			if ((f.subclass != AST_IAX_COMMAND_INVAL) || (f.frametype != AST_FRAME_IAX))
				raw_hangup(&sin, ntohs(fh->dcallno), ntohs(mh->callno));
		}
		pthread_mutex_unlock(&iaxs_lock);
		return 1;
	}
	iaxs[fr.callno]->peercallno = ntohs(mh->callno) & ~AST_FLAG_FULL;
	if (ntohs(mh->callno) & AST_FLAG_FULL) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Received packet %d, (%d, %d)\n", ntohs(fh->seqno), f.frametype, f.subclass);
		/* Check if it's out of order (and not an ACK or INVAL) */
		fr.seqno = ntohs(fh->seqno);
		if (iaxs[fr.callno]->iseqno != fr.seqno) {
			if (
			 ((f.subclass != AST_IAX_COMMAND_ACK) && (f.subclass != AST_IAX_COMMAND_INVAL)) ||
			 (f.frametype != AST_FRAME_IAX)) {
			 	/* If it's not an ACK packet, it's out of order. */
				if (option_debug)
					ast_log(LOG_DEBUG, "Packet arrived out of order (expecting %d, got %d) (frametype = %d, subclass = %d)\n", 
					iaxs[fr.callno]->iseqno, fr.seqno, f.frametype, f.subclass);
				if (iaxs[fr.callno]->iseqno > fr.seqno) {
					/* If we've already seen it, ack it XXX There's a border condition here XXX */
					if ((f.frametype != AST_FRAME_IAX) || 
							((f.subclass != AST_IAX_COMMAND_ACK) && (f.subclass != AST_IAX_COMMAND_INVAL))) {
						if (option_debug)
							ast_log(LOG_DEBUG, "Acking anyway\n");
						send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX_COMMAND_ACK, fr.ts, NULL, 0,fr.seqno);
					}
				}
				pthread_mutex_unlock(&iaxs_lock);
				return 1;
			}
		} else {
			/* Increment unless it's an ACK */
			if ((f.subclass != AST_IAX_COMMAND_ACK) ||
			    (f.frametype != AST_FRAME_IAX))
				iaxs[fr.callno]->iseqno++;
		}
		/* A full frame */
		if (res < sizeof(struct ast_iax_full_hdr)) {
			ast_log(LOG_WARNING, "midget packet received (%d of %d min)\n", res, sizeof(struct ast_iax_full_hdr));
			pthread_mutex_unlock(&iaxs_lock);
			return 1;
		}
		f.datalen = res - sizeof(struct ast_iax_full_hdr);
		if (f.datalen)
			f.data = buf + sizeof(struct ast_iax_full_hdr);
		else
			f.data = NULL;
		fr.ts = ntohl(fh->ts);
		/* Unless this is an ACK or INVAL frame, ack it */
		if ((f.frametype != AST_FRAME_IAX) || 
			 ((f.subclass != AST_IAX_COMMAND_ACK) && (f.subclass != AST_IAX_COMMAND_INVAL))) 
			send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX_COMMAND_ACK, fr.ts, NULL, 0,fr.seqno);
		if (f.frametype == AST_FRAME_VOICE)
			iaxs[fr.callno]->voiceformat = f.subclass;
		if (f.frametype == AST_FRAME_IAX) {
			/* Handle the IAX pseudo frame itself */
			if (option_debug)
				ast_log(LOG_DEBUG, "IAX subclass %d received\n", f.subclass);
			switch(f.subclass) {
			case AST_IAX_COMMAND_ACK:
				/* Ack the packet with the given timestamp */
				pthread_mutex_lock(&iaxq.lock);
				for (cur = iaxq.head; cur ; cur = cur->next) {
					/* If it's our call, and our timestamp, mark -1 retries */
					if ((fr.callno == cur->callno) && (fr.seqno == cur->seqno))
						cur->retries = -1;
				}
				pthread_mutex_unlock(&iaxq.lock);
				break;
			case AST_IAX_COMMAND_NEW:
				((char *)f.data)[f.datalen] = '\0';
				if (check_access(fr.callno, &sin, f.data, f.datalen)) {
					/* They're not allowed on */
					send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX_COMMAND_REJECT, 0, "No authority found", strlen("No authority found"), -1);
					ast_log(LOG_NOTICE, "Rejected connect attempt from %s, request '%s'\n", inet_ntoa(sin.sin_addr), f.data);
					/* XXX Not guaranteed to work, but probably does XXX */
					pthread_mutex_lock(&iaxq.lock);
					send_packet(iaxq.tail);
					pthread_mutex_unlock(&iaxq.lock);
					iax_destroy(fr.callno);
					break;
				}
				if (!strlen(iaxs[fr.callno]->secret)) {
					/* No authentication required, let them in */
					send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX_COMMAND_ACCEPT, 0, NULL, 0, -1);
					iaxs[fr.callno]->state |= IAX_STATE_STARTED;
					if(!(c = ast_iax_new(iaxs[fr.callno], AST_STATE_RING)))
						iax_destroy(fr.callno);
					else
						c->format = iaxs[fr.callno]->peerformats;
					break;
				}
				authenticate_request(iaxs[fr.callno]);
				iaxs[fr.callno]->state |= IAX_STATE_AUTHENTICATED;
				break;
			case AST_IAX_COMMAND_HANGUP:
#if 0
				iaxs[fr.callno]->error = ENOTCONN;
#endif
				iax_destroy(fr.callno);
				break;
			case AST_IAX_COMMAND_REJECT:
				if (f.data)
					((char *)f.data)[f.datalen] = '\0';
				ast_log(LOG_WARNING, "Call rejected by %s: %s\n", inet_ntoa(iaxs[fr.callno]->addr.sin_addr), f.data);
				iaxs[fr.callno]->error = EPERM;
				iax_destroy(fr.callno);
				break;
			case AST_IAX_COMMAND_ACCEPT:
				if (f.data) {
					((char *)f.data)[f.datalen]='\0';
					iax_getformats(fr.callno, (char *)f.data);
				} else {
					iaxs[fr.callno]->peerformats = iax_capability;
				}
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Call accepted by %s\n", inet_ntoa(iaxs[fr.callno]->addr.sin_addr));
				iaxs[fr.callno]->state |= IAX_STATE_STARTED;
				if (iaxs[fr.callno]->owner) {
					/* Switch us to use a compatible format */
					iaxs[fr.callno]->owner->format &= iaxs[fr.callno]->peerformats;

					if (!iaxs[fr.callno]->owner->format) 
						iaxs[fr.callno]->owner->format = iaxs[fr.callno]->peerformats & iax_capability;
					if (!iaxs[fr.callno]->owner->format) {
						ast_log(LOG_WARNING, "Unable to negotiate a common format with the peer.");
						iaxs[fr.callno]->error = EBADE;
						iax_destroy(fr.callno);
					} else {
						if (option_verbose > 2)
							ast_verbose(VERBOSE_PREFIX_3 "Format for call is %d\n", iaxs[fr.callno]->owner->format);
					}
						
				}
				break;
			case AST_IAX_COMMAND_PING:
				/* Send back a pong packet with the original timestamp */
				send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX_COMMAND_PONG, fr.ts, NULL, 0, -1);
				break;
			case AST_IAX_COMMAND_PONG:
				iaxs[fr.callno]->pingtime =  calc_timestamp(iaxs[fr.callno], 0) - fr.ts;
				break;
			case AST_IAX_COMMAND_LAGRQ:
			case AST_IAX_COMMAND_LAGRP:
				/* A little strange -- We have to actually go through the motions of
				   delivering the packet.  In the very last step, it will be properly
				   handled by do_deliver */
				snprintf(src, sizeof(src), "LAGRQ-IAX/%s/%d", inet_ntoa(sin.sin_addr),fr.callno);
				f.src = src;
				f.mallocd = 0;
				f.offset = 0;
				fr.f = &f;
				f.timelen = 0;
				schedule_delivery(iaxfrdup2(&fr, 0));
				break;
			case AST_IAX_COMMAND_AUTHREQ:
				((char *)f.data)[f.datalen] = '\0';
				if (authenticate_reply(iaxs[fr.callno], &iaxs[fr.callno]->addr, (char *)f.data)) {
					ast_log(LOG_WARNING, 
						"I don't know how to authenticate %s to %s\n", 
						f.data, inet_ntoa(iaxs[fr.callno]->addr.sin_addr));
					iax_destroy(fr.callno);
				}
				break;
			case AST_IAX_COMMAND_AUTHREP:
				((char *)f.data)[f.datalen] = '\0';
				if (authenticate_verify(iaxs[fr.callno], (char *)f.data)) {
					ast_log(LOG_NOTICE, "Host %s failed to authenticate as %s\n", inet_ntoa(iaxs[fr.callno]->addr.sin_addr), iaxs[fr.callno]->username);
					send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX_COMMAND_REJECT, 0, "No authority found", strlen("No authority found"), -1);
					/* XXX Not guaranteed to work, but probably does XXX */
					pthread_mutex_lock(&iaxq.lock);
					send_packet(iaxq.tail);
					pthread_mutex_unlock(&iaxq.lock);
					iax_destroy(fr.callno);
					break;
				}
				/* Authentication is fine, go ahead */
				send_command(iaxs[fr.callno], AST_FRAME_IAX, AST_IAX_COMMAND_ACCEPT, 0, NULL, 0, -1);
				iaxs[fr.callno]->state |= IAX_STATE_STARTED;
				if(!(c = ast_iax_new(iaxs[fr.callno], AST_STATE_RING)))
					iax_destroy(fr.callno);
				else
					c->format = iaxs[fr.callno]->peerformats;
				break;
			case AST_IAX_COMMAND_INVAL:
				iaxs[fr.callno]->error = ENOTCONN;
				iax_destroy(fr.callno);
				if (option_debug)
					ast_log(LOG_DEBUG, "Destroying call %d\n", fr.callno);
				break;
			default:
				ast_log(LOG_DEBUG, "Unknown IAX command %d on %d/%d\n", f.subclass, fr.callno, iaxs[fr.callno]->peercallno);
			}
			/* Don't actually pass these frames along */
			pthread_mutex_unlock(&iaxs_lock);
			return 1;
		}
	} else {
		/* A mini frame */
		f.frametype = AST_FRAME_VOICE;
		if (iaxs[fr.callno]->voiceformat > 0)
			f.subclass = iaxs[fr.callno]->voiceformat;
		else {
			ast_log(LOG_WARNING, "Received mini frame before first full voice frame\n ");
			pthread_mutex_unlock(&iaxs_lock);
			return 1;
		}
		f.datalen = res - sizeof(struct ast_iax_mini_hdr);
		if (f.datalen < 0) {
			ast_log(LOG_WARNING, "Datalen < 0?\n");
			pthread_mutex_unlock(&iaxs_lock);
			return 1;
		}
		if (f.datalen)
			f.data = buf + sizeof(struct ast_iax_mini_hdr);
		else
			f.data = NULL;
		fr.ts = (iaxs[fr.callno]->last & 0xFFFF0000L) | ntohs(mh->ts);
	}
	/* Don't pass any packets until we're started */
	if (!(iaxs[fr.callno]->state & IAX_STATE_STARTED)) {
		pthread_mutex_unlock(&iaxs_lock);
		return 1;
	}
	/* Common things */
	snprintf(src, sizeof(src), "IAX/%s/%d", inet_ntoa(sin.sin_addr),fr.callno);
	f.src = src;
	f.mallocd = 0;
	f.offset = 0;
	fr.f = &f;
	if (f.datalen && (f.frametype == AST_FRAME_VOICE)) 
		f.timelen = get_timelen(&f);
	else
		f.timelen = 0;

	/* If this is our most recent packet, use it as our basis for timestamping */
	if (iaxs[fr.callno]->last < fr.ts) {
		iaxs[fr.callno]->last = fr.ts;
		fr.outoforder = 0;
	} else {
		ast_log(LOG_DEBUG, "Received out of order packet... (type=%d, subclass %d, ts = %d, last = %d)\n", f.frametype, f.subclass, fr.ts, iaxs[fr.callno]->last);
		fr.outoforder = -1;
	}
	schedule_delivery(iaxfrdup2(&fr, 0));
	/* Always run again */
	pthread_mutex_unlock(&iaxs_lock);
	return 1;
}

static void free_ha(struct iax_ha *ha)
{
	struct iax_ha *hal;
	while(ha) {
		hal = ha;
		ha = ha->next;
		free(hal);
	}
}

static void free_context(struct iax_context *con)
{
	struct iax_context *conl;
	while(con) {
		conl = con;
		con = con->next;
		free(conl);
	}
}

static struct ast_channel *iax_request(char *type, int format, void *data)
{
	int callno;
	struct sockaddr_in sin;
	char s[256];
	char *st;
	struct ast_channel *c;
	strncpy(s, (char *)data, sizeof(s));
	strtok(s, "/");
	strtok(s, "@");
	st = strtok(NULL, "@");
	if (!st)
		st = s;
	/* Populate our address from the given */
	if (create_addr(&sin, st)) {
		ast_log(LOG_WARNING, "Unable to assign address\n");
		return NULL;
	}
	pthread_mutex_lock(&iaxs_lock);
	callno = find_callno(-1, -1, &sin, NEW_FORCE);
	if (callno < 0) {
		ast_log(LOG_WARNING, "Unable to create call\n");
		return NULL;
	}
	c = ast_iax_new(iaxs[callno], AST_STATE_DOWN);
	if (c) {
		/* Choose a format we can live with */
		if (c->format & format)
			c->format &= format;
		else 
			c->format = ast_translator_best_choice(format, c->format);
	}
	pthread_mutex_unlock(&iaxs_lock);
	return c;
}

static void *network_thread(void *ignore)
{
	/* Our job is simple: Send queued messages, retrying if necessary.  Read frames 
	   from the network, and queue them for delivery to the channels */
	int res;
	struct ast_iax_frame *f, *freeme;
	/* Establish I/O callback for socket read */
	ast_io_add(io, netsocket, socket_read, AST_IO_IN, NULL);
	pthread_mutex_lock(&iaxs_lock);
	for(;;) {
		/* Go through the queue, sending messages which have not yet been
		   sent, and scheduling retransmissions if appropriate */
		pthread_mutex_lock(&iaxq.lock);
		f = iaxq.head;
		while(f) {
			freeme = NULL;
			if (!f->sentyet) {
				f->sentyet++;
				/* Send a copy immediately */
				if (iaxs[f->callno]) {
					send_packet(f);
				} 
				if (f->retries < 0) {
					/* This is not supposed to be retransmitted */
					if (f->prev) 
						f->prev->next = f->next;
					else
						iaxq.head = f->next;
					if (f->next)
						f->next->prev = f->prev;
					else
						iaxq.tail = f->prev;
					iaxq.count--;
					/* Free the frame */
					ast_frfree(f->f);
					/* Free the iax frame */
					freeme = f;
				} else {
					/* We need reliable delivery.  Schedule a retransmission */
					f->retries++;
					ast_sched_add(sched, f->retrytime, attempt_transmit, f);
				}
			}
			f = f->next;
			if (freeme)
				free(freeme);
		}
		pthread_mutex_unlock(&iaxq.lock);
		pthread_mutex_unlock(&iaxs_lock);
		res = ast_sched_wait(sched);
		res = ast_io_wait(io, res);
		pthread_mutex_lock(&iaxs_lock);
		if (res >= 0) {
			ast_sched_runq(sched);
		}
	}
}

static int start_network_thread()
{
	return pthread_create(&netthreadid, NULL, network_thread, NULL);
}

static struct iax_context *build_context(char *context)
{
	struct iax_context *con = malloc(sizeof(struct iax_context));
	if (con) {
		strncpy(con->context, context, sizeof(con->context));
		con->next = NULL;
	}
	return con;
}

static struct iax_ha *build_ha(char *sense, char *stuff)
{
	struct iax_ha *ha = malloc(sizeof(struct iax_ha));
	char *nm;
	if (ha) {
		strtok(stuff, "/");
		nm = strtok(NULL, "/");
		if (!nm)
			nm = "255.255.255.255";
		if (!inet_aton(stuff, &ha->netaddr)) {
			ast_log(LOG_WARNING, "%s not a valid IP\n", stuff);
			free(ha);
			return NULL;
		}
		if (!inet_aton(nm, &ha->netmask)) {
			ast_log(LOG_WARNING, "%s not a valid netmask\n", nm);
			free(ha);
			return NULL;
		}
		ha->netaddr.s_addr &= ha->netmask.s_addr;
		if (!strcasecmp(sense, "a")) {
			ha->sense = IAX_SENSE_ALLOW;
		} else {
			ha->sense = IAX_SENSE_DENY;
		}
		ha->next = NULL;
	}
	return ha;
}

static struct iax_peer *build_peer(char *name, struct ast_variable *v)
{
	struct iax_peer *peer;
	int maskfound=0;
	struct hostent *hp;
	peer = malloc(sizeof(struct iax_peer));
	if (peer) {
		memset(peer, 0, sizeof(struct iax_peer));
		strncpy(peer->name, name, sizeof(peer->name));
		peer->addr.sin_port = htons(AST_DEFAULT_IAX_PORTNO);
		while(v) {
			if (!strcasecmp(v->name, "secret")) 
				strncpy(peer->secret, v->value, sizeof(peer->secret));
			else if (!strcasecmp(v->name, "host")) {
				if (!strcasecmp(v->value, "dynamic")) {
					/* They'll register with us */
					peer->dynamic = 1;
					memset(&peer->addr.sin_addr, 0, 4);
				} else {
					peer->dynamic = 0;
					hp = gethostbyname(v->value);
					if (hp) {
						memcpy(&peer->addr.sin_addr, hp->h_addr, sizeof(peer->addr.sin_addr));
					} else {
						ast_log(LOG_WARNING, "Unable to lookup '%s'\n", v->value);
						free(peer);
						return NULL;
					}
					if (!maskfound)
						inet_aton("255.255.255.255", &peer->mask);
				}
			}
			else if (!strcasecmp(v->name, "mask")) {
				maskfound++;
				inet_aton(v->value, &peer->mask);
			} else if (!strcasecmp(v->name, "port"))
				peer->addr.sin_port = htons(atoi(v->value));
			else if (!strcasecmp(v->name, "username"))
				strncpy(peer->username, v->value, sizeof(peer->username));
			v=v->next;
		}
	}
	return peer;
}

static struct iax_user *build_user(char *name, struct ast_variable *v)
{
	struct iax_user *user;
	struct iax_context *con, *conl = NULL;
	struct iax_ha *ha, *hal = NULL;
	user = (struct iax_user *)malloc(sizeof(struct iax_user));
	if (user) {
		memset(user, 0, sizeof(struct iax_user));
		strncpy(user->name, name, sizeof(user->name));
		while(v) {
			if (!strcasecmp(v->name, "context")) {
				con = build_context(v->value);
				if (con) {
					if (conl)
						conl->next = con;
					else
						user->contexts = con;
					conl = con;
				}
			} else if (!strcasecmp(v->name, "allow") ||
					   !strcasecmp(v->name, "deny")) {
				ha = build_ha(v->name, v->value);
				if (ha) {
					if (hal)
						hal->next = ha;
					else
						user->ha = ha;
					hal = ha;
				}
			} else if (!strcasecmp(v->name, "auth")) {
				strncpy(user->methods, v->value, sizeof(user->methods));
			} else if (!strcasecmp(v->name, "secret")) {
				strncpy(user->secret, v->value, sizeof(user->secret));
			}
			v = v->next;
		}
	}
	return user;
}

int load_module()
{
	int res = 0;
	struct ast_config *cfg;
	struct ast_variable *v;
	struct iax_user *user;
	struct iax_peer *peer;
	char *cat;
	char *utype;
	int format;
	
	struct sockaddr_in sin;
	
	sin.sin_family = AF_INET;
	sin.sin_port = ntohs(AST_DEFAULT_IAX_PORTNO);
	sin.sin_addr.s_addr = INADDR_ANY;
	
	io = io_context_create();
	sched = sched_context_create();
	
	if (!io || !sched) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}

	pthread_mutex_init(&iaxq.lock, NULL);
	pthread_mutex_init(&userl.lock, NULL);

	ast_cli_register(&cli_show_users);
	ast_cli_register(&cli_show_channels);
	ast_cli_register(&cli_show_peers);
	ast_cli_register(&cli_set_jitter);
#ifdef IAX_SIMULATOR
	ast_cli_register(&delay_cli);
	ast_cli_register(&deviation_cli);
	ast_cli_register(&reliability_cli);
	ast_cli_register(&sim_show_cli);
#endif
	cfg = ast_load(config);
	
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config);
		return -1;
	}
	v = ast_variable_browse(cfg, "general");
	while(v) {
		if (!strcasecmp(v->name, "port")) 
			sin.sin_port = ntohs(atoi(v->value));
		else if (!strcasecmp(v->name, "pingtime")) 
			ping_time = atoi(v->value);
		else if (!strcasecmp(v->name, "maxjitterbuffer")) 
			maxjitterbuffer = atoi(v->value);
		else if (!strcasecmp(v->name, "maxexcessbuffer")) 
			max_jitter_buffer = atoi(v->value);
		else if (!strcasecmp(v->name, "lagrqtime")) 
			lagrq_time = atoi(v->value);
		else if (!strcasecmp(v->name, "dropcount")) 
			iax_dropcount = atoi(v->value);
		else if (!strcasecmp(v->name, "bindaddr"))
			inet_aton(v->value, &sin.sin_addr);
		else if (!strcasecmp(v->name, "jitterbuffer"))
			use_jitterbuffer = ast_true(v->value);
		else if (!strcasecmp(v->name, "bandwidth")) {
			if (!strcasecmp(v->value, "low")) {
				iax_capability = IAX_CAPABILITY_LOWBANDWIDTH;
			} else if (!strcasecmp(v->value, "medium")) {
				iax_capability = IAX_CAPABILITY_MEDBANDWIDTH;
			} else if (!strcasecmp(v->value, "high")) {
				iax_capability = IAX_CAPABILITY_FULLBANDWIDTH;
			} else
				ast_log(LOG_WARNING, "bandwidth must be either low, medium, or high\n");
		} else if (!strcasecmp(v->name, "allow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot allow unknown format '%s'\n", v->value);
			else
				iax_capability |= format;
		} else if (!strcasecmp(v->name, "disallow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot disallow unknown format '%s'\n", v->value);
			else
				iax_capability &= ~format;
		}
		v = v->next;
	}
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			utype = ast_variable_retrieve(cfg, cat, "type");
			if (utype) {
				if (!strcasecmp(utype, "user")) {
					user = build_user(cat, ast_variable_browse(cfg, cat));
					if (user) {
						pthread_mutex_lock(&userl.lock);
						user->next = userl.users;
						userl.users = user;
						pthread_mutex_unlock(&userl.lock);
					}
				} else if (!strcasecmp(utype, "peer")) {
					peer = build_peer(cat, ast_variable_browse(cfg, cat));
					if (peer) {
						pthread_mutex_lock(&peerl.lock);
						peer->next = peerl.peers;
						peerl.peers = peer;
						pthread_mutex_unlock(&peerl.lock);
					}
				} else {
					ast_log(LOG_WARNING, "Unknown type '%s' for '%s' in %s\n", utype, cat, config);
				}
			} else
				ast_log(LOG_WARNING, "Section '%s' lacks type\n", cat);
		}
		cat = ast_category_browse(cfg, cat);
	}
	ast_destroy(cfg);
	if (ast_channel_register(type, tdesc, iax_capability, iax_request)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		unload_module();
		return -1;
	}
	
	/* Make a UDP socket */
	netsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
		
	if (netsocket < 0) {
		ast_log(LOG_ERROR, "Unable to create network socket: %s\n", strerror(errno));
		return -1;
	}
	if (bind(netsocket, &sin, sizeof(sin))) {
		ast_log(LOG_ERROR, "Unable to bind to %s port %d\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
		return -1;
	}

	if (!res) {
		res = start_network_thread();
		if (option_verbose > 1) 
			ast_verbose(VERBOSE_PREFIX_2 "IAX Ready and Listening on %s port %d\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
	} else {
		ast_log(LOG_ERROR, "Unable to start network thread\n");
		close(netsocket);
	}
	return res;
}

char *description()
{
	return desc;
}

int unload_module()
{
	struct iax_user *user, *userlast;
	struct iax_peer *peer, *peerlast;
	int x;
	/* Cancel the network thread, close the net socket */
	pthread_cancel(netthreadid);
	pthread_join(netthreadid, NULL);
	close(netsocket);
	for (x=0;x<AST_IAX_MAX_CALLS;x++)
		if (iaxs[x])
			iax_destroy(x);
	ast_cli_unregister(&cli_show_users);
	ast_cli_unregister(&cli_show_channels);
	ast_cli_unregister(&cli_show_peers);
	ast_cli_unregister(&cli_set_jitter);
#ifdef IAX_SIMULATOR
	ast_cli_unregister(&delay_cli);
	ast_cli_unregister(&deviation_cli);
	ast_cli_unregister(&reliability_cli);
	ast_cli_unregister(&sim_show_cli);
#endif
	for (user=userl.users;user;) {
		free_ha(user->ha);
		free_context(user->contexts);
		userlast = user;
		user=user->next;
		free(userlast);
	}
	for (peer=peerl.peers;peer;) {
		peerlast = peer;
		peer=peer->next;
		free(peerlast);
	}
	return 0;
}

int usecount()
{
	int res;
	pthread_mutex_lock(&usecnt_lock);
	res = usecnt;
	pthread_mutex_unlock(&usecnt_lock);
	return res;
}

