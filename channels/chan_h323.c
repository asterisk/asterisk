/*
 * chan_h323.c
 *
 * OpenH323 Channel Driver for ASTERISK PBX.
 *			By Jeremy McNamara
 *                      For The NuFone Network 
 *
 * This code has been derived from code created by
 *              Michael Manousos and Mark Spencer
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


#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/param.h>
#if defined(BSD)
#ifndef IPTOS_MINCOST
#define IPTOS_MINCOST 0x02
#endif
#endif
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#ifdef __cplusplus
extern "C" {
#endif   
#include <asterisk/lock.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
#include <asterisk/channel_pvt.h>
#include <asterisk/config.h>
#include <asterisk/module.h>
#include <asterisk/pbx.h>
#include <asterisk/options.h>
#include <asterisk/utils.h>
#include <asterisk/lock.h>
#include <asterisk/sched.h>
#include <asterisk/io.h>
#include <asterisk/rtp.h>
#include <asterisk/acl.h>
#include <asterisk/callerid.h>
#include <asterisk/cli.h>
#include <asterisk/dsp.h>
#ifdef __cplusplus
}
#endif
#include "h323/chan_h323.h"

send_digit_cb		on_send_digit; 
on_connection_cb	on_create_connection; 
setup_incoming_cb	on_incoming_call;
setup_outbound_cb	on_outgoing_call; 
start_logchan_cb	on_start_logical_channel; 
chan_ringing_cb		on_chan_ringing;
con_established_cb	on_connection_established;
clear_con_cb		on_connection_cleared;
answer_call_cb		on_answer_call;

int h323debug;

/** String variables required by ASTERISK */
static char *type	= "H323";
static char *desc	= "The NuFone Network's Open H.323 Channel Driver";
static char *tdesc	= "The NuFone Network's Open H.323 Channel Driver";
static char *config = "h323.conf";

static char default_context[AST_MAX_EXTENSION];

/** H.323 configuration values */
static char gatekeeper[100];
static int  gatekeeper_disable = 1;
static int  gatekeeper_discover = 0;
static int  usingGk;
static int  port = 1720;
static int  gkroute = 0;

static int noFastStart = 1;
static int noH245Tunneling = 0;

/* to find user by alias is default, alternative is the incomming call's source IP address*/
static int  userbyalias = 1;

static int  bridge_default = 1;

/* Just about everybody seems to support ulaw, so make it a nice default */
static int capability = AST_FORMAT_ULAW;

/* TOS flag */
static int tos = 0;

static int dtmfmode = H323_DTMF_RFC2833;

static char secret[50];

/** Private structure of a OpenH323 channel */
struct oh323_pvt {
	ast_mutex_t lock;					/* Channel private lock */
	call_options_t calloptions;				/* Options to be used during call setup */
	int alreadygone;					/* Whether or not we've already been destroyed by our peer */
	int needdestroy;					/* if we need to be destroyed */
	call_details_t cd;					/* Call details */
	struct ast_channel *owner;				/* Who owns us */
	int capability;						/* audio capability */
	int nonCodecCapability;					/* non-audio capability */
	int outgoing;						/* Outgoing or incoming call? */
	int nat;						/* Are we talking to a NAT EP?*/
	int bridge;						/* Determine of we should native bridge or not*/
	char exten[AST_MAX_EXTENSION];				/* Requested extension */
	char context[AST_MAX_EXTENSION];			/* Context where to start */
	char username[81];					/* H.323 alias using this channel */
	char accountcode[256];					/* Account code */
	int amaflags;						/* AMA Flags */
	char callerid[80];					/* Caller*ID if available */
	struct ast_rtp *rtp;					/* RTP Session */
	int dtmfmode;						/* What DTMF Mode is being used */
	struct ast_dsp *vad;					/* Used for in-band DTMF detection */
	struct oh323_pvt *next;					/* Next channel in list */
} *iflist = NULL;

static struct ast_user_list {
	struct oh323_user *users;
	ast_mutex_t lock;
} userl;

static struct ast_peer_list {
	struct oh323_peer *peers;
	ast_mutex_t lock;
} peerl;

static struct ast_alias_list {
	struct oh323_alias *aliases;
	ast_mutex_t lock;
} aliasl;

/** Asterisk RTP stuff*/
static struct sched_context *sched;
static struct io_context *io;

/** Protect the interface list (of oh323_pvt's) */
AST_MUTEX_DEFINE_STATIC(iflock);

/** Usage counter and associated lock */
static int usecnt =0;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

/* Protect the monitoring thread, so only one process can kill or start it, and not
   when it's doing something critical. */
AST_MUTEX_DEFINE_STATIC(monlock);

/* Avoid two chan to pass capabilities simultaneaously to the h323 stack. */
AST_MUTEX_DEFINE_STATIC(caplock);

/* This is the thread for the monitor which checks for input on the channels
   which are not currently in use.  */
static pthread_t monitor_thread = AST_PTHREADT_NULL;

static int restart_monitor(void);

static void __oh323_destroy(struct oh323_pvt *p)
{
	struct oh323_pvt *cur, *prev = NULL;
	
	if (p->rtp) {
		ast_rtp_destroy(p->rtp);
	}
	
	/* Unlink us from the owner if we have one */
	if (p->owner) {
		ast_mutex_lock(&p->owner->lock);
		ast_log(LOG_DEBUG, "Detaching from %s\n", p->owner->name);
		p->owner->pvt->pvt = NULL;
		ast_mutex_unlock(&p->owner->lock);
	}
	cur = iflist;
	while(cur) {
		if (cur == p) {
			if (prev)
				prev->next = cur->next;
			else
				iflist = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	if (!cur) {
		ast_log(LOG_WARNING, "%p is not in list?!?! \n", cur);
	} else {
                ast_mutex_destroy(&p->lock);
		free(p);
        }
}

static void oh323_destroy(struct oh323_pvt *p)
{
	ast_mutex_lock(&iflock);
	__oh323_destroy(p);
	ast_mutex_unlock(&iflock);
}

static struct oh323_alias *build_alias(char *name, struct ast_variable *v)
{
	struct oh323_alias *alias;

	alias = (struct oh323_alias *)malloc(sizeof(struct oh323_alias));

	if (alias) {
		memset(alias, 0, sizeof(struct oh323_alias));
		strncpy(alias->name, name, sizeof(alias->name)-1);

		while (v) {
			if (!strcasecmp(v->name, "e164")) {
				strncpy(alias->e164,  v->value, sizeof(alias->e164)-1);
			} else if (!strcasecmp(v->name, "prefix")) {
				strncpy(alias->prefix,  v->value, sizeof(alias->prefix)-1);
			} else if (!strcasecmp(v->name, "context")) {
				strncpy(alias->context,  v->value, sizeof(alias->context)-1);
			} else if (!strcasecmp(v->name, "secret")) {
				strncpy(alias->secret,  v->value, sizeof(alias->secret)-1);
			} else {
				if (strcasecmp(v->value, "h323")) { 	
					ast_log(LOG_WARNING, "Keyword %s does not make sense in type=h323\n", v->value);
				}
			}
			v = v->next;
		}
	}
	return alias;
}

static struct oh323_user *build_user(char *name, struct ast_variable *v)
{
	struct oh323_user *user;
	int format;
	
	user = (struct oh323_user *)malloc(sizeof(struct oh323_user));
	if (user) {
		memset(user, 0, sizeof(struct oh323_user));
		strncpy(user->name, name, sizeof(user->name)-1);
		
		/* set the usage flag to a sane starting value*/
		user->inUse = 0;
		/* Assume we can native bridge */
		user->bridge = bridge_default; 

		while(v) {
			if (!strcasecmp(v->name, "context")) {
				strncpy(user->context, v->value, sizeof(user->context)-1);
			} else if (!strcasecmp(v->name, "bridge")) {
				user->bridge = ast_true(v->value);
                      } else if (!strcasecmp(v->name, "nat")) {
                              user->nat = ast_true(v->value);
			} else if (!strcasecmp(v->name, "noFastStart")) {
				user->noFastStart = ast_true(v->value);
			} else if (!strcasecmp(v->name, "noH245Tunneling")) {
				user->noH245Tunneling = ast_true(v->value);
			} else if (!strcasecmp(v->name, "noSilenceSuppression")) {
				user->noSilenceSuppression = ast_true(v->value);
			} else if (!strcasecmp(v->name, "secret")) {
				strncpy(user->secret, v->value, sizeof(user->secret)-1);
			} else if (!strcasecmp(v->name, "callerid")) {
				strncpy(user->callerid, v->value, sizeof(user->callerid)-1);
			} else if (!strcasecmp(v->name, "accountcode")) {
				strncpy(user->accountcode, v->value, sizeof(user->accountcode)-1);
			} else if (!strcasecmp(v->name, "incominglimit")) {
				user->incominglimit = atoi(v->value);
				if (user->incominglimit < 0)
					user->incominglimit = 0;
			} else if (!strcasecmp(v->name, "host")) {
				if (!strcasecmp(v->value, "dynamic")) {
					ast_log(LOG_ERROR, "Dynamic host configuration not implemented, yet!\n");
					free(user);
					return NULL;
				} else if (ast_get_ip(&user->addr, v->value)) {
					free(user);
					return NULL;
				} 
				/* Let us know we need to use ip authentication */
				user->host = 1;
			} else if (!strcasecmp(v->name, "amaflags")) {
				format = ast_cdr_amaflags2int(v->value);
				if (format < 0) {
					ast_log(LOG_WARNING, "Invalid AMA Flags: %s at line %d\n", v->value, v->lineno);
				} else {
					user->amaflags = format;
				}
			}
			v = v->next;
		}
	}
	return user;
}


static struct oh323_peer *build_peer(char *name, struct ast_variable *v)
{
	struct oh323_peer *peer;
	struct oh323_peer *prev;
	int found=0;
	
	prev = NULL;
	ast_mutex_lock(&peerl.lock);
	peer = peerl.peers;

	while(peer) {
		if (!strcasecmp(peer->name, name)) {	
			break;
		}
		prev = peer;
		peer = peer->next;
	}

	if (peer) {
		found++;
		/* Already in the list, remove it and it will be added back (or FREE'd) */
		if (prev) {
			prev->next = peer->next;
		} else {
			peerl.peers = peer->next;
		}
		ast_mutex_unlock(&peerl.lock);
 	} else {
		ast_mutex_unlock(&peerl.lock);
		peer = (struct oh323_peer*)malloc(sizeof(struct oh323_peer));
		memset(peer, 0, sizeof(struct oh323_peer));
	}
	if (peer) {
		if (!found) {
			strncpy(peer->name, name, sizeof(peer->name)-1);
		}
		
		/* set the usage flag to a sane starting value*/
		peer->inUse = 0;

		while(v) {
			if (!strcasecmp(v->name, "context")) {
				strncpy(peer->context, v->value, sizeof(peer->context)-1);
			}  else if (!strcasecmp(v->name, "bridge")) {
				peer->bridge = ast_true(v->value);
			} else if (!strcasecmp(v->name, "noFastStart")) {
				peer->noFastStart = ast_true(v->value);
			} else if (!strcasecmp(v->name, "noH245Tunneling")) {
				peer->noH245Tunneling = ast_true(v->value);
			} else if (!strcasecmp(v->name, "noSilenceSuppression")) {
				peer->noSilenceSuppression = ast_true(v->value);
			} else if (!strcasecmp(v->name, "outgoinglimit")) {
				peer->outgoinglimit = atoi(v->value);
				if (peer->outgoinglimit > 0)
					peer->outgoinglimit = 0;
			} else if (!strcasecmp(v->name, "host")) {
				if (!strcasecmp(v->value, "dynamic")) {
					ast_log(LOG_ERROR, "Dynamic host configuration not implemented, yet!\n");
					free(peer);
					return NULL;
				}
				if (ast_get_ip(&peer->addr, v->value)) {
						free(peer);
						return NULL;
				}
			} 
			v=v->next;
		}
	}
	return peer;
}



/**
 * Send (play) the specified digit to the channel.
 * 
 */
static int oh323_digit(struct ast_channel *c, char digit)
{
	struct oh323_pvt *p = (struct oh323_pvt *) c->pvt->pvt;
	if (p && p->rtp && (p->dtmfmode & H323_DTMF_RFC2833)) {
		ast_rtp_senddigit(p->rtp, digit);
	}
	/* If in-band DTMF is desired, send that */
	if (p->dtmfmode & H323_DTMF_INBAND)
		h323_send_tone(p->cd.call_token, digit);
	return 0;
}


/**
 * Make a call over the specified channel to the specified 
 * destination. This function will parse the destination string
 * and determine the address-number to call.
 * Return -1 on error, 0 on success.
 */
static int oh323_call(struct ast_channel *c, char *dest, int timeout)
{
	int res;
	struct oh323_pvt *p = (struct oh323_pvt *) c->pvt->pvt;
	char called_addr[256];
	char *tmp, *cid, *cidname, oldcid[256];

	strtok_r(dest, "/", &(tmp));

	ast_log(LOG_DEBUG, "dest=%s, timeout=%d.\n", dest, timeout);

	if (strlen(dest) > sizeof(called_addr) - 1) {
		ast_log(LOG_DEBUG, "Destination is too long (%d)\n", strlen(dest));
		return -1;
	}

	if ((c->_state != AST_STATE_DOWN) && (c->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "Line is already in use (%s)\n", c->name);
		return -1;
	}
	
	/* outgoing call */
	p->outgoing = 1;

	/* Clear the call token */
	if ((p->cd).call_token == NULL)
		(p->cd).call_token = (char *)malloc(128);

	memset((char *)(p->cd).call_token, 0, 128);
	
	if (p->cd.call_token == NULL) {
		ast_log(LOG_ERROR, "Not enough memory.\n");
		return -1;
	}

	/* Build the address to call */
	memset(called_addr, 0, sizeof(called_addr));
	memcpy(called_addr, dest, strlen(dest));

	/* Copy callerid, if there is any */
	if (c->callerid) {
                memset(oldcid, 0, sizeof(oldcid));
                memcpy(oldcid, c->callerid, strlen(c->callerid));
                oldcid[sizeof(oldcid)-1] = '\0';
                ast_callerid_parse(oldcid, &cidname, &cid);
                if (p->calloptions.callerid) {
                        free(p->calloptions.callerid);
                        p->calloptions.callerid = NULL;
                }
                if (p->calloptions.callername) {
                        free(p->calloptions.callername);
                        p->calloptions.callername = NULL;
                }
                p->calloptions.callerid = (char*)malloc(256);
                if (p->calloptions.callerid == NULL) {
                        ast_log(LOG_ERROR, "Not enough memory.\n");
                        return(-1);
                }
                memset(p->calloptions.callerid, 0, 256);
                if ((cid != NULL)&&(strlen(cid) > 0))
                        strncpy(p->calloptions.callerid, cid, 255);

                p->calloptions.callername = (char*)malloc(256);
                if (p->calloptions.callername == NULL) {
                        ast_log(LOG_ERROR, "Not enough memory.\n");
                        return(-1);
                }
                memset(p->calloptions.callername, 0, 256);
                if ((cidname != NULL)&&(strlen(cidname) > 0))
                        strncpy(p->calloptions.callername, cidname, 255);

        } else {
                if (p->calloptions.callerid) {
                        free(p->calloptions.callerid);
                        p->calloptions.callerid = NULL;
                }
                if (p->calloptions.callername) {
                        free(p->calloptions.callername);
                        p->calloptions.callername = NULL;
                }
        }

	p->calloptions.noFastStart = noFastStart;
	p->calloptions.noH245Tunneling = noH245Tunneling;

	res = h323_make_call(called_addr, &(p->cd), p->calloptions);

	if (res) {
		ast_log(LOG_NOTICE, "h323_make_call failed(%s)\n", c->name);
		return -1;
	}
	return 0;
}

static int oh323_answer(struct ast_channel *c)
{
	int res;

	struct oh323_pvt *p = (struct oh323_pvt *) c->pvt->pvt;

	res = h323_answering_call(p->cd.call_token, 0);
	
	if (c->_state != AST_STATE_UP)
		ast_setstate(c, AST_STATE_UP);

	return res;
}

static int oh323_hangup(struct ast_channel *c)
{
	struct oh323_pvt *p = (struct oh323_pvt *) c->pvt->pvt;
	int needcancel = 0;
	if (h323debug)
		ast_log(LOG_DEBUG, "oh323_hangup(%s)\n", c->name);
	if (!c->pvt->pvt) {
		ast_log(LOG_DEBUG, "Asked to hangup channel not connected\n");
		return 0;
	}
	ast_mutex_lock(&p->lock);
	/* Determine how to disconnect */
	if (p->owner != c) {
		ast_log(LOG_WARNING, "Huh?  We aren't the owner?\n");
		ast_mutex_unlock(&p->lock);
		return 0;
	}
	if (!c || (c->_state != AST_STATE_UP))
		needcancel = 1;
	/* Disconnect */
	p = (struct oh323_pvt *) c->pvt->pvt;
	
	/* Free dsp used for in-band DTMF detection */
	if (p->vad) {
		ast_dsp_free(p->vad);
	}

	p->owner = NULL;
	c->pvt->pvt = NULL;

	/* Start the process if it's not already started */
	if (!p->alreadygone) {
		if (h323_clear_call((p->cd).call_token)) { 
			ast_log(LOG_DEBUG, "ClearCall failed.\n");
		}
		p->needdestroy = 1;
	} 

	/* Update usage counter */
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	if (usecnt < 0)
		ast_log(LOG_WARNING, "Usecnt < 0\n");
	ast_mutex_unlock(&usecnt_lock);
	ast_update_use_count();

	ast_mutex_unlock(&p->lock);
	return 0;
}

static struct ast_frame *oh323_rtp_read(struct oh323_pvt *p)
{
	/* Retrieve audio/etc from channel.  Assumes p->lock is already held. */
	struct ast_frame *f;
	static struct ast_frame null_frame = { AST_FRAME_NULL, };

      /* Only apply it for the first packet, we just need the correct ip/port */
      if(p->nat)
      {
              ast_rtp_setnat(p->rtp,p->nat);
              p->nat = 0;
      }

	f = ast_rtp_read(p->rtp);
	/* Don't send RFC2833 if we're not supposed to */
	if (f && (f->frametype == AST_FRAME_DTMF) && !(p->dtmfmode & H323_DTMF_RFC2833))
		return &null_frame;
	if (p->owner) {
		/* We already hold the channel lock */
		if (f->frametype == AST_FRAME_VOICE) {
			if (f->subclass != p->owner->nativeformats) {
				ast_log(LOG_DEBUG, "Oooh, format changed to %d\n", f->subclass);
				p->owner->nativeformats = f->subclass;
				ast_set_read_format(p->owner, p->owner->readformat);
				ast_set_write_format(p->owner, p->owner->writeformat);
			}
		
			/* Do in-band DTMF detection */
			if (p->dtmfmode & H323_DTMF_INBAND) {
                   f = ast_dsp_process(p->owner,p->vad,f);
				   if (f->frametype == AST_FRAME_DTMF)
					ast_log(LOG_DEBUG, "Got in-band digit %c.\n", f->subclass);
            }
			
			
		}
	}
	return f;
}


static struct ast_frame  *oh323_read(struct ast_channel *c)
{
	struct ast_frame *fr;
	struct oh323_pvt *p = (struct oh323_pvt *) c->pvt->pvt;
	ast_mutex_lock(&p->lock);
	fr = oh323_rtp_read(p);
	ast_mutex_unlock(&p->lock);
	return fr;
}

static int oh323_write(struct ast_channel *c, struct ast_frame *frame)
{
	struct oh323_pvt *p = (struct oh323_pvt *) c->pvt->pvt;
	int res = 0;
	if (frame->frametype != AST_FRAME_VOICE) {
		if (frame->frametype == AST_FRAME_IMAGE)
			return 0;
		else {
			ast_log(LOG_WARNING, "Can't send %d type frames with H323 write\n", frame->frametype);
			return 0;
		}
	} else {
		if (!(frame->subclass & c->nativeformats)) {
			ast_log(LOG_WARNING, "Asked to transmit frame type %d, while native formats is %d (read/write = %d/%d)\n",
				frame->subclass, c->nativeformats, c->readformat, c->writeformat);
			return -1;
		}
	}
	if (p) {
		ast_mutex_lock(&p->lock);
		if (p->rtp) {
			res =  ast_rtp_write(p->rtp, frame);
		}
		ast_mutex_unlock(&p->lock);
	}
	return res;
}

/** FIXME: Can I acutally use this or does Open H.323 take care of everything? */
static int oh323_indicate(struct ast_channel *c, int condition)
{

	struct oh323_pvt *p = (struct oh323_pvt *) c->pvt->pvt;
	
	switch(condition) {
	case AST_CONTROL_RINGING:
		if (c->_state == AST_STATE_RING || c->_state == AST_STATE_RINGING) {
			h323_send_alerting(p->cd.call_token);
 			break;
 		}		
		return -1;
	case AST_CONTROL_PROGRESS:
		if (c->_state != AST_STATE_UP) {
			h323_send_progress(p->cd.call_token);
			break;
		}
		return -1;

	case AST_CONTROL_BUSY:
		if (c->_state != AST_STATE_UP) {
			h323_answering_call(p->cd.call_token, 1);
 			p->alreadygone = 1;
			ast_softhangup_nolock(c, AST_SOFTHANGUP_DEV);			
			break;
		}
		return -1;
	case AST_CONTROL_CONGESTION:
		if (c->_state != AST_STATE_UP) {
			h323_answering_call(p->cd.call_token, 1);
			p->alreadygone = 1;
			ast_softhangup_nolock(c, AST_SOFTHANGUP_DEV);
			break;
		}
		return -1;
	case AST_CONTROL_PROCEEDING:
	case -1:
		return -1;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", condition);
		return -1;
	}
	return 0;
}

// FIXME: WTF is this? Do I need this???
static int oh323_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct oh323_pvt *p = (struct oh323_pvt *) newchan->pvt->pvt;

	ast_mutex_lock(&p->lock);
	if (p->owner != oldchan) {
		ast_log(LOG_WARNING, "old channel wasn't %p but was %p\n", oldchan, p->owner);
		return -1;
	}
	p->owner = newchan;
	ast_mutex_unlock(&p->lock);
	return 0;
}

static struct ast_channel *oh323_new(struct oh323_pvt *i, int state, const char *host)
{
	struct ast_channel *ch;
	int fmt;
	ch = ast_channel_alloc(1);
	
	if (ch) {
		
		snprintf(ch->name, sizeof(ch->name), "H323/%s", host);
		ch->nativeformats = i->capability;
		if (!ch->nativeformats)
			ch->nativeformats = capability;
		fmt = ast_best_codec(ch->nativeformats);
		ch->type = type;
		ch->fds[0] = ast_rtp_fd(i->rtp);
		
		if (state == AST_STATE_RING)
			ch->rings = 1;
		
		ch->writeformat = fmt;
		ch->pvt->rawwriteformat = fmt;
		ch->readformat = fmt;
		ch->pvt->rawreadformat = fmt;
		
		/* Allocate dsp for in-band DTMF support */
		if (i->dtmfmode & H323_DTMF_INBAND) {
			i->vad = ast_dsp_new();
			ast_dsp_set_features(i->vad, DSP_FEATURE_DTMF_DETECT);
        	}

		/* Register the OpenH323 channel's functions. */
		ch->pvt->pvt = i;
		ch->pvt->send_digit = oh323_digit;
		ch->pvt->call = oh323_call;
		ch->pvt->hangup = oh323_hangup;
		ch->pvt->answer = oh323_answer;
		ch->pvt->read = oh323_read;
		ch->pvt->write = oh323_write;
		ch->pvt->indicate = oh323_indicate;
		ch->pvt->fixup = oh323_fixup;
	     /*	ch->pvt->bridge = ast_rtp_bridge; */

		/*  Set the owner of this channel */
		i->owner = ch;
		
		ast_mutex_lock(&usecnt_lock);
		usecnt++;
		ast_mutex_unlock(&usecnt_lock);
		ast_update_use_count();
		strncpy(ch->context, i->context, sizeof(ch->context)-1);
		strncpy(ch->exten, i->exten, sizeof(ch->exten)-1);		
		ch->priority = 1;
		if (!ast_strlen_zero(i->callerid))
			ch->callerid = strdup(i->callerid);
		if (!ast_strlen_zero(i->accountcode))
			strncpy(ch->accountcode, i->accountcode, sizeof(ch->accountcode)-1);
		if (i->amaflags)
			ch->amaflags = i->amaflags;
		ast_setstate(ch, state);
		if (state != AST_STATE_DOWN) {
			if (ast_pbx_start(ch)) {
				ast_log(LOG_WARNING, "Unable to start PBX on %s\n", ch->name);
				ast_hangup(ch);
				ch = NULL;
			}
		}
	} else
		ast_log(LOG_WARNING, "Unable to allocate channel structure\n");
	return ch;
}

static struct oh323_pvt *oh323_alloc(int callid)
{
	struct oh323_pvt *p;

	p = (struct oh323_pvt *) malloc(sizeof(struct oh323_pvt));
	if (!p) {
		ast_log(LOG_ERROR, "Couldn't allocate private structure. This is bad\n");
		return NULL;
	}

	/* Keep track of stuff */
	memset(p, 0, sizeof(struct oh323_pvt));
	p->rtp = ast_rtp_new(sched, io, 1, 0);

	if (!p->rtp) {
		ast_log(LOG_WARNING, "Unable to create RTP session: %s\n", strerror(errno));
		free(p);
		return NULL;
	}
	ast_rtp_settos(p->rtp, tos);
	ast_mutex_init(&p->lock);
	
	p->cd.call_reference = callid;
	p->bridge = bridge_default;
	
	p->dtmfmode = dtmfmode;
	if (p->dtmfmode & H323_DTMF_RFC2833)
		p->nonCodecCapability |= AST_RTP_DTMF;

	/* Add to interface list */
	ast_mutex_lock(&iflock);
	p->next = iflist;
	iflist = p;
	ast_mutex_unlock(&iflock);
	return p;
}

static struct oh323_pvt *find_call(int call_reference)
{  
        struct oh323_pvt *p;

		ast_mutex_lock(&iflock);
        p = iflist; 

        while(p) {
                if ((signed int)p->cd.call_reference == call_reference) {
                        /* Found the call */						
						ast_mutex_unlock(&iflock);
						return p;
                }
                p = p->next; 
        }
        ast_mutex_unlock(&iflock);
		return NULL;
        
}

static struct ast_channel *oh323_request(char *type, int format, void *data)
{

	int oldformat;
	struct oh323_pvt *p;
	struct ast_channel *tmpc = NULL;
	char *dest = (char *) data;
	char *ext, *host;
	char *h323id = NULL;
	char tmp[256];

	
	ast_log(LOG_DEBUG, "type=%s, format=%d, data=%s.\n", type, format, (char *)data);

	oldformat = format;
	format &= capability;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%d'\n", format);
		return NULL;
	}
	
	strncpy(tmp, dest, sizeof(tmp) - 1);
		
	host = strchr(tmp, '@');
	if (host) {
		*host = '\0';
		host++;
		ext = tmp;
	} else {
		host = tmp;
		ext = NULL;
	}

	strtok_r(host, "/", &(h323id));
		
	if (h323id && !ast_strlen_zero(h323id)) {
		h323_set_id(h323id);
	}
		
	p = oh323_alloc(0);

	if (!p) {
		ast_log(LOG_WARNING, "Unable to build pvt data for '%s'\n", (char *)data);
		return NULL;
	}

	/* Assign a default capability */
	p->capability = capability;
	
	if (p->dtmfmode) {
		if (p->dtmfmode & H323_DTMF_RFC2833) {
			p->nonCodecCapability |= AST_RTP_DTMF;
		} else {
			p->nonCodecCapability &= ~AST_RTP_DTMF;
		}
	}
	/* pass on our preferred codec to the H.323 stack */
	ast_mutex_lock(&caplock);
	h323_set_capability(format, dtmfmode);
	ast_mutex_unlock(&caplock);

	if (ext) {
		strncpy(p->username, ext, sizeof(p->username) - 1);
	}
	ast_log(LOG_DEBUG, "Host: %s\tUsername: %s\n", host, p->username);

	tmpc = oh323_new(p, AST_STATE_DOWN, host);
	if (!tmpc)
		oh323_destroy(p);
	
	restart_monitor();
	
	return tmpc;
}

struct oh323_alias *find_alias(const char *source_aliases)
{
	struct oh323_alias *a;

	a = aliasl.aliases;

	while(a) {

		if (!strcasecmp(a->name, source_aliases)) {
			break;
		}
		a = a->next;
	}
	return a;
}

struct oh323_user *find_user(const call_details_t cd)
{
	struct oh323_user *u;
	char iabuf[INET_ADDRSTRLEN];
	u = userl.users;
	if(userbyalias == 1){
		while(u) {
			if (!strcasecmp(u->name, cd.call_source_aliases)) {
				break;
			}
			u = u->next;
		}

	} else {
		while(u) {
			if (!strcasecmp(cd.sourceIp, ast_inet_ntoa(iabuf, sizeof(iabuf), u->addr.sin_addr))) {
				break;
			}
			u = u->next;
		}

	
	}
	return u;

}

struct oh323_peer *find_peer(char *dest_peer)
{
	struct oh323_peer *p;

	p = peerl.peers;

	while(p) {
		if (!strcasecmp(p->name, dest_peer)) {
			break;
		}
		p = p->next;
	}
	return p;

}

/**
  * Callback for sending digits from H.323 up to asterisk
  *
  */
int send_digit(unsigned call_reference, char digit)
{
	struct oh323_pvt *p;
	struct ast_frame f;

	ast_log(LOG_DEBUG, "Recieved Digit: %c\n", digit);
	p = find_call(call_reference);
	
	if (!p) {
		ast_log(LOG_ERROR, "Private structure not found in send_digit.\n");
		return -1;
	}
	memset(&f, 0, sizeof(f));
	f.frametype = AST_FRAME_DTMF;
    f.subclass = digit;
    f.datalen = 0;
    f.samples = 800;
    f.offset = 0;
    f.data = NULL;
    f.mallocd = 0;
    f.src = "SEND_DIGIT";
   	
	return ast_queue_frame(p->owner, &f);	
}

/**
  * Call-back function that gets called when any H.323 connection is made
  *
  * Returns the local RTP information
  */
struct rtp_info *create_connection(unsigned call_reference)
{	
	struct oh323_pvt *p;
	struct sockaddr_in us;
	struct sockaddr_in them;
	struct rtp_info *info;
	/* XXX This is sooooo bugus.  inet_ntoa is not reentrant
	   but this function wants to return a static variable so
	   the only way to do this will be to declare iabuf within
	   the oh323_pvt structure XXX */
	static char iabuf[INET_ADDRSTRLEN];

	info = (struct rtp_info *) malloc(sizeof(struct rtp_info));

	p = find_call(call_reference);

	if (!p) {
		ast_log(LOG_ERROR, "Unable to allocate private structure, this is very bad.\n");
		return NULL;
	}

	/* figure out our local RTP port and tell the H.323 stack about it*/
	ast_rtp_get_us(p->rtp, &us);
	ast_rtp_get_peer(p->rtp, &them);

	info->addr = ast_inet_ntoa(iabuf, sizeof(iabuf), us.sin_addr);
	info->port = ntohs(us.sin_port);

	return info;
}

/**
 *  Call-back function for incoming calls
 *
 *  Returns 1 on success
 */

int setup_incoming_call(call_details_t cd)
{
	
	struct oh323_pvt *p = NULL;
/*	struct ast_channel *c = NULL; */
	struct oh323_user *user = NULL;
	struct oh323_alias *alias = NULL;
	char iabuf[INET_ADDRSTRLEN];

	/* allocate the call*/
	p = oh323_alloc(cd.call_reference);

	if (!p) {
		ast_log(LOG_ERROR, "Unable to allocate private structure, this is bad.\n");
		return 0;
	}

	/* Populate the call details in the private structure */
	p->cd.call_token = strdup(cd.call_token);
	p->cd.call_source_aliases = strdup(cd.call_source_aliases);
	p->cd.call_dest_alias = strdup(cd.call_dest_alias);
	p->cd.call_source_name = strdup(cd.call_source_name);
	p->cd.call_source_e164 = strdup(cd.call_source_e164);
	p->cd.call_dest_e164 = strdup(cd.call_dest_e164);

	if (h323debug) {
		ast_verbose(VERBOSE_PREFIX_3 "Setting up Call\n");
		ast_verbose(VERBOSE_PREFIX_3 "	   Call token:  [%s]\n", p->cd.call_token);
		ast_verbose(VERBOSE_PREFIX_3 "	   Calling party name:  [%s]\n", p->cd.call_source_name);
		ast_verbose(VERBOSE_PREFIX_3 "	   Calling party number:  [%s]\n", p->cd.call_source_e164);
		ast_verbose(VERBOSE_PREFIX_3 "	   Called  party name:  [%s]\n", p->cd.call_dest_alias);
		ast_verbose(VERBOSE_PREFIX_3 "	   Called  party number:  [%s]\n", p->cd.call_dest_e164);
	}

	/* Decide if we are allowing Gatekeeper routed calls*/
	if ((!strcasecmp(cd.sourceIp, gatekeeper)) && (gkroute == -1) && (usingGk == 1)) {
		
		if (!ast_strlen_zero(cd.call_dest_e164)) {
			strncpy(p->exten, cd.call_dest_e164, sizeof(p->exten)-1);
			strncpy(p->context, default_context, sizeof(p->context)-1); 
		} else {
			alias = find_alias(cd.call_dest_alias);
		
			if (!alias) {
				ast_log(LOG_ERROR, "Call for %s rejected, alias not found\n", cd.call_dest_alias);
				return 0;
			}
			strncpy(p->exten, alias->name, sizeof(p->exten)-1);
			strncpy(p->context, alias->context, sizeof(p->context)-1);
		}
		snprintf(p->callerid, sizeof(p->callerid), "%s <%s>", p->cd.call_source_name, p->cd.call_source_e164);
	} else { 
		/* Either this call is not from the Gatekeeper 
		   or we are not allowing gk routed calls */
		user  = find_user(cd);

		if (!user) {
			snprintf(p->callerid, sizeof(p->callerid), "%s <%s>", p->cd.call_source_name, p->cd.call_source_e164);
			if (!ast_strlen_zero(p->cd.call_dest_e164)) {
				strncpy(p->exten, cd.call_dest_e164, sizeof(p->exten)-1);
			} else {
				strncpy(p->exten, cd.call_dest_alias, sizeof(p->exten)-1);		
			}
			if (ast_strlen_zero(default_context)) {
				ast_log(LOG_ERROR, "Call from '%s' rejected due to no default context\n", p->cd.call_source_aliases);
				return 0;
			}
			strncpy(p->context, default_context, sizeof(p->context)-1);
			ast_log(LOG_DEBUG, "Sending %s to context [%s]\n", cd.call_source_aliases, p->context);
		} else {					
			if (user->host) {
				if (strcasecmp(cd.sourceIp, ast_inet_ntoa(iabuf, sizeof(iabuf), user->addr.sin_addr))){					
					if (ast_strlen_zero(user->context)) {
						if (ast_strlen_zero(default_context)) {					
							ast_log(LOG_ERROR, "Call from '%s' rejected due to non-matching IP address (%s) and no default context\n", user->name, cd.sourceIp);
                					return 0;
						}
						strncpy(p->context, default_context, sizeof(p->context)-1);
					} else {
						strncpy(p->context, user->context, sizeof(p->context)-1);
					}
					p->exten[0] = 'i';
					p->exten[1] = '\0';
					ast_log(LOG_ERROR, "Call from '%s' rejected due to non-matching IP address (%s)s\n", user->name, cd.sourceIp);
					goto exit;					
				}
			}
			if (user->incominglimit > 0) {
				if (user->inUse >= user->incominglimit) {
					ast_log(LOG_ERROR, "Call from user '%s' rejected due to usage limit of %d\n", user->name, user->incominglimit);
					return 0;
				}
			}
			strncpy(p->context, user->context, sizeof(p->context)-1);
			p->bridge = user->bridge;
                      	p->nat = user->nat;

			if (!ast_strlen_zero(user->callerid)) {
				strncpy(p->callerid, user->callerid, sizeof(p->callerid) - 1);
			} else {
				 snprintf(p->callerid, sizeof(p->callerid), "%s <%s>", p->cd.call_source_name, p->cd.call_source_e164); 
			}
			if (!ast_strlen_zero(p->cd.call_dest_e164)) {
				strncpy(p->exten, cd.call_dest_e164, sizeof(p->exten)-1);
			} else {
				strncpy(p->exten, cd.call_dest_alias, sizeof(p->exten)-1);		
			}
			if (!ast_strlen_zero(user->accountcode)) {
				strncpy(p->accountcode, user->accountcode, sizeof(p->accountcode)-1);
			} 

			
			/* Increment the usage counter */
			user->inUse++;
		} 
	}

exit:
#if 0
	/* allocate a channel and tell asterisk about it */
	c = oh323_new(p, AST_STATE_RINGING, cd.call_token);
	if (!c) {
		ast_log(LOG_ERROR, "Couldn't create channel. This is bad\n");
		return 0;
	}
#endif
	return 1;
}

/**
 * Call-back function to start PBX when OpenH323 ready to serve incoming call
 *
 * Returns 1 on success
 */
static int answer_call(unsigned call_reference)
{
	struct oh323_pvt *p = NULL;
	struct ast_channel *c = NULL;
	
	/* Find the call or allocate a private structure if call not found */
	p = find_call(call_reference);
	
	if (!p) {
		ast_log(LOG_ERROR, "Something is wrong: answer_call\n");
		return 0;
	}
	
	/* allocate a channel and tell asterisk about it */
	c = oh323_new(p, AST_STATE_RINGING, p->cd.call_token);
	if (!c) {
		ast_log(LOG_ERROR, "Couldn't create channel. This is bad\n");
		return 0;
	}
	return 1;
}

/**
 * Call-back function to establish an outgoing H.323 call
 * 
 * Returns 1 on success 
 */
int setup_outgoing_call(call_details_t cd)
{	
	return 1;
}

#if 0
if (p->inUse >= p->outgoinglimit) {
	ast_log(LOG_ERROR, "Call to %s rejected due to usage limit of %d outgoing channels\n", p->name, p->inUse);
	return 0;
}

if (!p) {
	ast_log(LOG_ERROR, "Rejecting call: peer %s not found\n", dest_peer);
	return 0;
}
#endif

/**
  * Call-back function that gets called for each rtp channel opened 
  *
  * Returns nothing 
  */
void setup_rtp_connection(unsigned call_reference, const char *remoteIp, int remotePort)
{
	struct oh323_pvt *p = NULL;
	struct sockaddr_in them;

	/* Find the call or allocate a private structure if call not found */
	p = find_call(call_reference);

	if (!p) {
		ast_log(LOG_ERROR, "Something is wrong: rtp\n");
		return;
	}

	them.sin_family = AF_INET;
	them.sin_addr.s_addr = inet_addr(remoteIp); // only works for IPv4
	them.sin_port = htons(remotePort);
	ast_rtp_set_peer(p->rtp, &them);

	return;
}

/**  
  *	Call-back function to signal asterisk that the channel has been answered 
  * Returns nothing
  */
void connection_made(unsigned call_reference)
{
	struct ast_channel *c = NULL;
	struct oh323_pvt *p = NULL;
	
	p = find_call(call_reference);
	
	if (!p) {
		ast_log(LOG_ERROR, "Something is wrong: connection\n");
		return;
	}

	if (!p->owner) {
		ast_log(LOG_ERROR, "Channel has no owner\n");
		return;
	}
	c = p->owner;	

	ast_setstate(c, AST_STATE_UP);
	ast_queue_control(c, AST_CONTROL_ANSWER);
	return;
}

/**
  *  Call-back function to signal asterisk that the channel is ringing
  *  Returns nothing
  */
void chan_ringing(unsigned call_reference)
{
        struct ast_channel *c = NULL;
        struct oh323_pvt *p = NULL;

        p = find_call(call_reference);

        if (!p) {
                ast_log(LOG_ERROR, "Something is wrong: ringing\n");
	}

        if (!p->owner) {
                ast_log(LOG_ERROR, "Channel has no owner\n");
                return;
        }
        c = p->owner;
        ast_setstate(c, AST_STATE_RINGING);
        ast_queue_control(c, AST_CONTROL_RINGING);
        return;
}


void cleanup_call_details(call_details_t cd) 
{
        if (cd.call_token) {
                free(cd.call_token);
        }
        if (cd.call_source_aliases) {
                free(cd.call_source_aliases);
        }
        if (cd.call_dest_alias) {
                free(cd.call_dest_alias);
	}
        if (cd.call_source_name) { 
                free(cd.call_source_name);
        }
        if (cd.call_source_e164) {
                free(cd.call_source_e164);
        }
        if (cd.call_dest_e164) {
                free(cd.call_dest_e164);
        }
        if (cd.sourceIp) {
                free(cd.sourceIp);
        }
}

/**
  * Call-back function to cleanup communication
  * Returns nothing,
  */
void cleanup_connection(call_details_t cd)
{	
	struct oh323_pvt *p = NULL;
/*	struct oh323_peer *peer = NULL; */
	struct oh323_user *user = NULL;
	struct ast_rtp *rtp = NULL;
	
	p = find_call(cd.call_reference);

	if (!p) {
		return;
	}
	ast_mutex_lock(&p->lock);

	/* Decrement usage counter */
	if (!p->outgoing) {
		user = find_user(cd);
		
		if(user) {
			user->inUse--;
		}
	}

#if 0
	if (p->outgoing) {
		peer = find_peer(cd.call_dest_alias);
		peer->inUse--;
	} else {
		user = find_user(cd);
		user->inUse--;
	}
#endif
	
	if (p->rtp) {
		rtp = p->rtp;
		p->rtp = NULL;
		/* Immediately stop RTP */
		ast_rtp_destroy(rtp);
	}

	cleanup_call_details(p->cd);
	
	p->alreadygone = 1;
	
	/* Send hangup */	
	if (p->owner) {
		ast_queue_hangup(p->owner);
	} 

	ast_mutex_unlock(&p->lock);
	return;	
}

static void *do_monitor(void *data)
{
	int res;
	struct oh323_pvt *oh323 = NULL;
	
		for(;;) {
		/* Check for interfaces needing to be killed */
		ast_mutex_lock(&iflock);
restartsearch:		
		oh323 = iflist;
		while(oh323) {
			if (oh323->needdestroy) {
				__oh323_destroy(oh323);
				goto restartsearch;
			}
			oh323 = oh323->next;
		}
		ast_mutex_unlock(&iflock);

		/* Wait for sched or io */
		res = ast_sched_wait(sched);
		if ((res < 0) || (res > 1000))
			res = 1000;
		res = ast_io_wait(io, res);

		pthread_testcancel();

		ast_mutex_lock(&monlock);
		if (res >= 0) 
			ast_sched_runq(sched);
		ast_mutex_unlock(&monlock);
	}
	/* Never reached */
	return NULL;
	
}

static int restart_monitor(void)
{
	/* If we're supposed to be stopped -- stay stopped */
	if (monitor_thread == AST_PTHREADT_STOP)
		return 0;
	if (ast_mutex_lock(&monlock)) {
		ast_log(LOG_WARNING, "Unable to lock monitor\n");
		return -1;
	}
	if (monitor_thread == pthread_self()) {
		ast_mutex_unlock(&monlock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (monitor_thread && (monitor_thread != AST_PTHREADT_NULL)) {
		/* Wake up the thread */
		pthread_kill(monitor_thread, SIGURG);
	} else {
		/* Start a new monitor */
		if (pthread_create(&monitor_thread, NULL, do_monitor, NULL) < 0) {
			ast_mutex_unlock(&monlock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&monlock);
	return 0;
}

static int h323_do_trace(int fd, int argc, char *argv[])
{
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}
	h323_debug(1, atoi(argv[2]));
	ast_cli(fd, "H.323 trace set to level %s\n", argv[2]);
	return RESULT_SUCCESS;
}

static int h323_no_trace(int fd, int argc, char *argv[])
{
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}
	h323_debug(0,0);
	ast_cli(fd, "H.323 trace disabled\n");
	return RESULT_SUCCESS;
}

static int h323_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2) {
		return RESULT_SHOWUSAGE;
	}
	h323debug = 1;
	ast_cli(fd, "H323 debug enabled\n");
	return RESULT_SUCCESS;
}

static int h323_no_debug(int fd, int argc, char *argv[])
{
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}
	h323debug = 0;
	ast_cli(fd, "H323 Debug disabled\n");
	return RESULT_SUCCESS;
}

static int h323_gk_cycle(int fd, int argc, char *argv[])
{
	return RESULT_SUCCESS;
#if 0
	if (argc != 3) {
		return RESULT_SHOWUSAGE;
	}	
	h323_gk_urq();
	
	/* Possibly register with a GK */
	if (!gatekeeper_disable) {
		if (h323_set_gk(gatekeeper_discover, gatekeeper, secret)) {
			ast_log(LOG_ERROR, "Gatekeeper registration failed.\n");
		}
	}
	return RESULT_SUCCESS;
#endif
}

static int h323_ep_hangup(int fd, int argc, char *argv[])
{

        if (argc != 3) {
                return RESULT_SHOWUSAGE;
	}

	if (h323_soft_hangup(argv[2])) {
		ast_verbose(VERBOSE_PREFIX_3 "Hangup succeeded on %s\n", argv[2]);
	} else { 
		ast_verbose(VERBOSE_PREFIX_3 "Hangup failed for %s\n", argv[2]);
	}

	return RESULT_SUCCESS;
}

static int h323_tokens_show(int fd, int argc, char *argv[])
{

        if (argc != 3) {
                return RESULT_SHOWUSAGE;
	}
	h323_show_tokens();

	return RESULT_SUCCESS;
}


static char trace_usage[] = 
"Usage: h.323 trace <level num>\n"
"       Enables H.323 stack tracing for debugging purposes\n";

static char no_trace_usage[] = 
"Usage: h.323 no trace\n"
"       Disables H.323 stack tracing for debugging purposes\n";

static char debug_usage[] = 
"Usage: h.323 debug\n"
"       Enables chan_h323 debug output\n";

static char no_debug_usage[] = 
"Usage: h.323 no debug\n"
"       Disables chan_h323 debug output\n";

static char show_codec_usage[] = 
"Usage: h.323 show codec\n"
"       Shows all enabled codecs\n";

static char show_cycle_usage[] = 
"Usage: h.323 gk cycle\n"
"       Manually re-register with the Gatekeper\n";

static char show_hangup_usage[] = 
"Usage: h.323 hangup <token>\n"
"       Manually try to hang up call identified by <token>\n";

static char show_tokens_usage[] = 
"Usage: h.323 show tokens\n"
"       Print out all active call tokens\n";

static struct ast_cli_entry  cli_trace =
	{ { "h.323", "trace", NULL }, h323_do_trace, "Enable H.323 Stack Tracing", trace_usage };
static struct ast_cli_entry  cli_no_trace =
	{ { "h.323", "no", "trace", NULL }, h323_no_trace, "Disable H.323 Stack Tracing", no_trace_usage };
static struct ast_cli_entry  cli_debug =
	{ { "h.323", "debug", NULL }, h323_do_debug, "Enable chan_h323 debug", debug_usage };
static struct ast_cli_entry  cli_no_debug =
	{ { "h.323", "no", "debug", NULL }, h323_no_debug, "Disable chan_h323 debug", no_debug_usage };
static struct ast_cli_entry  cli_show_codecs =
	{ { "h.323", "show", "codecs", NULL }, h323_show_codec, "Show enabled codecs", show_codec_usage };
static struct ast_cli_entry  cli_gk_cycle =
	{ { "h.323", "gk", "cycle", NULL }, h323_gk_cycle, "Manually re-register with the Gatekeper", show_cycle_usage };
static struct ast_cli_entry  cli_hangup_call =
	{ { "h.323", "hangup", NULL }, h323_ep_hangup, "Show all active call tokens", show_hangup_usage };
static struct ast_cli_entry  cli_show_tokens =
	{ { "h.323", "show", "tokens", NULL }, h323_tokens_show, "Manually try to hang up a call", show_tokens_usage };



int reload_config(void)
{
	
	int format;
	struct ast_config *cfg;
	struct ast_variable *v;
	struct oh323_peer *peer   = NULL;
	struct oh323_user *user   = NULL;
	struct oh323_alias *alias = NULL;
	struct ast_hostent ahp; struct hostent *hp;
	char *cat;
    	char *utype;
	
	cfg = ast_load(config);

	/* We *must* have a config file otherwise stop immediately */
	if (!cfg) {
		ast_log(LOG_NOTICE, "Unable to load config %s, H.323 disabled\n", config);
		return 1;
	}
	
       /* fire up the H.323 Endpoint */       
	if (!h323_end_point_exist()) {
	       h323_end_point_create(noFastStart,noH245Tunneling);        
	}
	h323debug=0;
	dtmfmode = H323_DTMF_RFC2833;

	memset(&bindaddr, 0, sizeof(bindaddr));

	v = ast_variable_browse(cfg, "general");
	while(v) {
		/* Create the interface list */
		if (!strcasecmp(v->name, "port")) {
			port = (int)strtol(v->value, NULL, 10);
		} else if (!strcasecmp(v->name, "bindaddr")) {
			if (!(hp = ast_gethostbyname(v->value, &ahp))) {
				ast_log(LOG_WARNING, "Invalid address: %s\n", v->value);
			} else {
				memcpy(&bindaddr.sin_addr, hp->h_addr, sizeof(bindaddr.sin_addr));
			}
		} else if (!strcasecmp(v->name, "allow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot allow unknown format '%s'\n", v->value);
			else
				capability |= format;
		} else if (!strcasecmp(v->name, "disallow")) {
			format = ast_getformatbyname(v->value);
			if (format < 1) 
				ast_log(LOG_WARNING, "Cannot disallow unknown format '%s'\n", v->value);
			else
				capability &= ~format;
		} else if (!strcasecmp(v->name, "tos")) {
			if (sscanf(v->value, "%i", &format) == 1)
				tos = format & 0xff;
			else if (!strcasecmp(v->value, "lowdelay"))
				tos = IPTOS_LOWDELAY;
			else if (!strcasecmp(v->value, "throughput"))
				tos = IPTOS_THROUGHPUT;
			else if (!strcasecmp(v->value, "reliability"))
				tos = IPTOS_RELIABILITY;
			else if (!strcasecmp(v->value, "mincost"))
				tos = IPTOS_MINCOST;
			else if (!strcasecmp(v->value, "none"))
				tos = 0;
			else
				ast_log(LOG_WARNING, "Invalid tos value at line %d, should be 'lowdelay', 'throughput', 'reliability', 'mincost', or 'none'\n", v->lineno);
		} else if (!strcasecmp(v->name, "gatekeeper")) {
			if (!strcasecmp(v->value, "DISABLE")) {
				gatekeeper_disable = 1;
				usingGk = 0;
			} else if (!strcasecmp(v->value, "DISCOVER")) {
				gatekeeper_disable = 0;
				gatekeeper_discover = 1;
				usingGk = 1;
			} else {
				gatekeeper_disable = 0;
				usingGk = 1;
				strncpy(gatekeeper, v->value, sizeof(gatekeeper)-1);
			}
		} else if (!strcasecmp(v->name, "secret")) {
				strncpy(secret, v->value, sizeof(secret)-1);
		} else if (!strcasecmp(v->name, "AllowGKRouted")) {
				gkroute = ast_true(v->value);
		} else if (!strcasecmp(v->name, "context")) {
			strncpy(default_context, v->value, sizeof(default_context)-1);
			ast_verbose(VERBOSE_PREFIX_3 "  == Setting default context to %s\n", default_context);	
		} else if (!strcasecmp(v->name, "dtmfmode")) {
			if (!strcasecmp(v->value, "inband"))
				dtmfmode=H323_DTMF_INBAND;
			else if (!strcasecmp(v->value, "rfc2833"))
				dtmfmode = H323_DTMF_RFC2833;
			else {
				ast_log(LOG_WARNING, "Unknown dtmf mode '%s', using rfc2833\n", v->value);
				dtmfmode = H323_DTMF_RFC2833;
			}
		} else if (!strcasecmp(v->name, "UserByAlias")) {
                        userbyalias = ast_true(v->value);
                } else if (!strcasecmp(v->name, "bridge")) {
                        bridge_default = ast_true(v->value);
                } else if (!strcasecmp(v->name, "noFastStart")) {
                                noFastStart = ast_true(v->value);
                } else if (!strcasecmp(v->name, "noH245Tunneling")) {
                                noH245Tunneling = ast_true(v->value);
		}
		v = v->next;	
	}
	
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general")) {
			utype = ast_variable_retrieve(cfg, cat, "type");
			if (utype) {
				if (!strcasecmp(utype, "user") || !strcasecmp(utype, "friend")) {
					user = build_user(cat, ast_variable_browse(cfg, cat));
					if (user) {
						ast_mutex_lock(&userl.lock);
						user->next = userl.users;
						userl.users = user;
						ast_mutex_unlock(&userl.lock);
					}
				}  else if (!strcasecmp(utype, "peer") || !strcasecmp(utype, "friend")) {
					peer = build_peer(cat, ast_variable_browse(cfg, cat));
					if (peer) {
						ast_mutex_lock(&peerl.lock);
						peer->next = peerl.peers;
						peerl.peers = peer;
						ast_mutex_unlock(&peerl.lock);
					}
				}  else if (!strcasecmp(utype, "h323")) {			
					alias = build_alias(cat, ast_variable_browse(cfg, cat));
					if (alias) {
						ast_mutex_lock(&aliasl.lock);
						alias->next = aliasl.aliases;
						aliasl.aliases = alias;
						ast_mutex_unlock(&aliasl.lock);
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

	/* Register our H.323 aliases if any*/
	while (alias) {		
		if (h323_set_alias(alias)) {
			ast_log(LOG_ERROR, "Alias %s rejected by endpoint\n", alias->name);
			return -1;
		}	
		alias = alias->next;
	}

	/* Add some capabilities */
	ast_mutex_lock(&caplock);
	if(h323_set_capability(capability, dtmfmode)) {
		ast_log(LOG_ERROR, "Capabilities failure, this is bad.\n");
		ast_mutex_unlock(&caplock);
		return -1;
	}
	ast_mutex_unlock(&caplock);

	return 0;
}

void delete_users(void)
{
	struct oh323_user *user, *userlast;
	struct oh323_peer *peer;
	
	/* Delete all users */
	ast_mutex_lock(&userl.lock);
	for (user=userl.users;user;) {
		userlast = user;
		user=user->next;
		free(userlast);
	}
	userl.users=NULL;
	ast_mutex_unlock(&userl.lock);
	ast_mutex_lock(&peerl.lock);
	for (peer=peerl.peers;peer;) {
		/* Assume all will be deleted, and we'll find out for sure later */
		peer->delme = 1;
		peer = peer->next;
	}
	ast_mutex_unlock(&peerl.lock);
}

void delete_aliases(void)
{
	struct oh323_alias *alias, *aliaslast;
		
	/* Delete all users */
	ast_mutex_lock(&aliasl.lock);
	for (alias=aliasl.aliases;alias;) {
		aliaslast = alias;
		alias=alias->next;
		free(aliaslast);
	}
	aliasl.aliases=NULL;
	ast_mutex_unlock(&aliasl.lock);
}

void prune_peers(void)
{
	/* Prune peers who still are supposed to be deleted */
	struct oh323_peer *peer, *peerlast, *peernext;
	ast_mutex_lock(&peerl.lock);
	peerlast = NULL;
	for (peer=peerl.peers;peer;) {
		peernext = peer->next;
		if (peer->delme) {
			free(peer);
			if (peerlast)
				peerlast->next = peernext;
			else
				peerl.peers = peernext;
		} else
			peerlast = peer;
		peer=peernext;
	}
	ast_mutex_unlock(&peerl.lock);
}

int reload(void)
{
	delete_users();
	delete_aliases();
	prune_peers();

#if 0
	if (!ast_strlen_zero(gatekeeper)) {
		h323_gk_urq();
	}
#endif

	reload_config();

#if 0
	/* Possibly register with a GK */
	if (gatekeeper_disable == 0) {
		if (h323_set_gk(gatekeeper_discover, gatekeeper, secret)) {
			ast_log(LOG_ERROR, "Gatekeeper registration failed.\n");
			h323_end_process();
			return -1;
		}
	}
#endif
	restart_monitor();
	return 0;
}


static struct ast_rtp *oh323_get_rtp_peer(struct ast_channel *chan)
{
	struct oh323_pvt *p;
	p = (struct oh323_pvt *) chan->pvt->pvt;
	if (p && p->rtp && p->bridge) {
		return p->rtp;
	}
	return NULL;
}

static struct ast_rtp *oh323_get_vrtp_peer(struct ast_channel *chan)
{
	return NULL;
}

static char *convertcap(int cap)
{
	switch (cap) {
	case AST_FORMAT_G723_1:
		return "G.723";
	case AST_FORMAT_GSM:
		return "GSM";
	case AST_FORMAT_ULAW:
		return "ULAW";
	case AST_FORMAT_ALAW:
		return "ALAW";
	case AST_FORMAT_ADPCM:
		return "G.728";
	case AST_FORMAT_G729A:
		return "G.729";
	case AST_FORMAT_SPEEX:
		return "SPEEX";
	case AST_FORMAT_ILBC:
		return "ILBC";
	default:
		ast_log(LOG_NOTICE, "Don't know how to deal with mode %d\n", cap);
		return NULL;
	}

}

static int oh323_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp, struct ast_rtp *vrtp, int codecs)
{
	/* XXX Deal with Video */
	struct oh323_pvt *p;
	struct sockaddr_in them;
	struct sockaddr_in us;
	char *mode;
	char iabuf[INET_ADDRSTRLEN];

	mode = convertcap(chan->writeformat); 

	if (!rtp) {
		return 0;
	}

	p = (struct oh323_pvt *) chan->pvt->pvt;
	if (!p) {
		ast_log(LOG_ERROR, "No Private Structure, this is bad\n");
		return -1;
	}

	ast_rtp_get_peer(rtp, &them);	
	ast_rtp_get_us(rtp, &us);

	h323_native_bridge(p->cd.call_token, ast_inet_ntoa(iabuf, sizeof(iabuf), them.sin_addr), mode);
	
	return 0;
	
}

static struct ast_rtp_protocol oh323_rtp = {
	get_rtp_info: oh323_get_rtp_peer,
	get_vrtp_info: oh323_get_vrtp_peer,
	set_rtp_peer: oh323_set_rtp_peer,
};

int load_module()
{
	int res;

        ast_mutex_init(&userl.lock);
        ast_mutex_init(&peerl.lock);
        ast_mutex_init(&aliasl.lock);

	res = reload_config();

	if (res) {
		return 0;
	} else {
		/* Make sure we can register our channel type */
		if (ast_channel_register(type, tdesc, capability, oh323_request)) {
			ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
			h323_end_process();
			return -1;
		}
		ast_cli_register(&cli_debug);
		ast_cli_register(&cli_no_debug);
		ast_cli_register(&cli_trace);
		ast_cli_register(&cli_no_trace);
		ast_cli_register(&cli_show_codecs);
		ast_cli_register(&cli_gk_cycle);
		ast_cli_register(&cli_hangup_call);
		ast_cli_register(&cli_show_tokens);

		oh323_rtp.type = type;
		ast_rtp_proto_register(&oh323_rtp);

		sched = sched_context_create();
		if (!sched) {
			ast_log(LOG_WARNING, "Unable to create schedule context\n");
		}
		io = io_context_create();
		if (!io) {
			ast_log(LOG_WARNING, "Unable to create I/O context\n");
		}
		
		/* Register our callback functions */
		h323_callback_register(setup_incoming_call, 
			               setup_outgoing_call,							 
	 			       create_connection, 
				       setup_rtp_connection, 
				       cleanup_connection, 
				       chan_ringing,
				       connection_made, 
				       send_digit,
				       answer_call);
	

		/* start the h.323 listener */
		if (h323_start_listener(port, bindaddr)) {
			ast_log(LOG_ERROR, "Unable to create H323 listener.\n");
			return -1;
		}

		/* Possibly register with a GK */
		if (gatekeeper_disable == 0) {
			if (h323_set_gk(gatekeeper_discover, gatekeeper, secret)) {
				ast_log(LOG_ERROR, "Gatekeeper registration failed.\n");
				return 0;
			}
		}
		/* And start the monitor for the first time */
		restart_monitor();
	}
	return res;
}


int unload_module() 
{
	struct oh323_pvt *p, *pl;
		
	if (!ast_mutex_lock(&iflock)) {
	/* hangup all interfaces if they have an owner */
	p = iflist;
	while(p) {
		if (p->owner)
			ast_softhangup(p->owner, AST_SOFTHANGUP_APPUNLOAD);
		p = p->next;
	}
	iflist = NULL;
	ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		return -1;
	}

	if (!ast_mutex_lock(&monlock)) {
                if (monitor_thread && (monitor_thread != AST_PTHREADT_STOP)) {
                        pthread_cancel(monitor_thread);
                        pthread_kill(monitor_thread, SIGURG);
                        pthread_join(monitor_thread, NULL);
                 }
                monitor_thread = AST_PTHREADT_STOP;
                ast_mutex_unlock(&monlock);
        } else {
                ast_log(LOG_WARNING, "Unable to lock the monitor\n");
                return -1;
        }
		
	if (!ast_mutex_lock(&iflock)) {
		/* destroy all the interfaces and free their memory */
		p = iflist;
		while(p) {
			pl = p;
			p = p->next;
			/* free associated memory */
			ast_mutex_destroy(&pl->lock);
			free(pl);
		}
		iflist = NULL;
		ast_mutex_unlock(&iflock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the interface list\n");
		return -1;
	}
	h323_gk_urq();
	h323_end_process();

	/* unregister rtp */
	ast_rtp_proto_unregister(&oh323_rtp);
	
	/* unregister commands */
        ast_cli_unregister(&cli_debug);
        ast_cli_unregister(&cli_no_debug);
        ast_cli_unregister(&cli_trace);
        ast_cli_unregister(&cli_no_trace);   
        ast_cli_unregister(&cli_show_codecs);
        ast_cli_unregister(&cli_gk_cycle);
        ast_cli_unregister(&cli_hangup_call);
        ast_cli_unregister(&cli_show_tokens);
                        
	/* unregister channel type */
	ast_channel_unregister(type);

	return 0; 
} 

int usecount()
{
	int res;
	ast_mutex_lock(&usecnt_lock);
	res = usecnt;
	ast_mutex_unlock(&usecnt_lock);
	return res;
}

char *description()
{
	return desc;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}




