/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Attila Domjan
 *
 * Attila Domjan <attila.domjan.hu@gmail.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!\file
 *
 * \brief PacketCable COPS
 * 
 * \author Attila Domjan <attila.domjan.hu@gmail.com>
 *
 * \note 
 * This module is an add-on to chan_mgcp. It adds support for the
 * PacketCable MGCP variation called NCS. Res_pktccops implements COPS
 * (RFC 2748), a protocol used to manage dynamic bandwith allocation in
 * CMTS's (HFC gateways). When you use NCS, you need to talk COPS with
 * the CMTS to complete the calls.
 */

/*** MODULEINFO
	<export_globals/>
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <signal.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#define AST_API_MODULE
#include "asterisk/pktccops.h"

#define DEFAULT_COPS_PORT "2126"

#define COPS_HEADER_SIZE 8
#define COPS_OBJECT_HEADER_SIZE 4
#define GATE_SET_OBJ_SIZE 144
#define GATEID_OBJ_SIZE 8
#define GATE_INFO_OBJ_SIZE 24

#define PKTCCOPS_SCOMMAND_GATE_ALLOC 1
#define PKTCCOPS_SCOMMAND_GATE_ALLOC_ACK 2
#define PKTCCOPS_SCOMMAND_GATE_ALLOC_ERR 3
#define PKTCCOPS_SCOMMAND_GATE_SET 4
#define PKTCCOPS_SCOMMAND_GATE_SET_ACK 5
#define PKTCCOPS_SCOMMAND_GATE_SET_ERR 6
#define PKTCCOPS_SCOMMAND_GATE_INFO 7
#define PKTCCOPS_SCOMMAND_GATE_INFO_ACK 8
#define PKTCCOPS_SCOMMAND_GATE_INFO_ERR 9
#define PKTCCOPS_SCOMMAND_GATE_DELETE 10
#define PKTCCOPS_SCOMMAND_GATE_DELETE_ACK 11
#define PKTCCOPS_SCOMMAND_GATE_DELETE_ERR 12
#define PKTCCOPS_SCOMMAND_GATE_OPEN 13
#define PKTCCOPS_SCOMMAND_GATE_CLOSE 14

AST_MUTEX_DEFINE_STATIC(pktccops_lock);
static pthread_t pktccops_thread = AST_PTHREADT_NULL;
static uint16_t cops_trid = 0;

struct pktcobj {
	uint16_t length;
	unsigned char cnum;
	unsigned char ctype;
	char *contents;
	struct pktcobj *next; 
};

struct copsmsg {
	unsigned char verflag;
	unsigned char opcode;
	uint16_t clienttype;
	uint32_t length;
	struct pktcobj *object;
	char *msg; /* != NULL if not packet cable message received */
};

struct gatespec {
	int direction; /* 0-DS, 1-US */
	int protocolid;
	int flags; /* 0x00 */
	int sessionclass; /* normal voip: 0x01, high priority voip: 0x02, unspecified: 0x00 */
	uint32_t srcip;
	uint32_t dstip;
	uint16_t srcp;
	uint16_t dstp;
	int diffserv;
	uint16_t t1;
	uint16_t t7;
	uint16_t t8;
	uint32_t r; /* Token Bucket Rate */
	uint32_t b; /* token Bucket Size */
	uint32_t p; /* Peak Data Rate */
	uint32_t m; /* Minimum Policed Size*/
	uint32_t mm; /* Maximum Policed Size */
	uint32_t rate;
	uint32_t s; /* Allowable Jitter*/
};


struct cops_cmts {
	AST_LIST_ENTRY(cops_cmts) list;
	char name[80];
	char host[80];
	char port[80];
	uint16_t t1;	
	uint16_t t7;
	uint16_t t8;
	uint32_t keepalive;

	uint32_t handle;
	int state;
	time_t contime;
	time_t katimer;
	int sfd;
	int need_delete;
};

struct cops_ippool {
	AST_LIST_ENTRY(cops_ippool) list;
	uint32_t start;
	uint32_t stop;
	struct cops_cmts *cmts;
};

static uint16_t t1 = 250;
static uint16_t t7 = 200;
static uint16_t t8 = 300;
static uint32_t keepalive = 60;
static int pktccopsdebug = 0;
static int pktcreload = 0;
static int gateinfoperiod = 60;
static int gatetimeout = 150;

AST_LIST_HEAD_STATIC(cmts_list, cops_cmts);
AST_LIST_HEAD_STATIC(ippool_list, cops_ippool);
AST_LIST_HEAD_STATIC(gate_list, cops_gate);

static int pktccops_add_ippool(struct cops_ippool *ippool);
static struct cops_gate *cops_gate_cmd(int cmd, struct cops_cmts *cmts, uint16_t trid, uint32_t mta, uint32_t actcount, float bitrate, uint32_t psize, uint32_t ssip, uint16_t ssport, struct cops_gate *gate);
static void pktccops_unregister_ippools(void);
static int load_pktccops_config(void);

static uint32_t ftoieeef(float n)
{
	uint32_t res;
	memcpy(&res, &n, 4);
	return htonl(res);
}

static uint16_t cops_constructgatespec(struct gatespec *gs, char *res)
{
	if (res == NULL) {
		return 0;
	}
	
	*res = (char) gs->direction;
	*(res + 1) = (char) gs->protocolid;
	*(res + 2) = (char) gs->flags;
	*(res + 3) = (char) gs->sessionclass;

	*((uint32_t *) (res + 4)) = gs->srcip;
	*((uint32_t *) (res + 8)) = gs->dstip;

	*((uint16_t *) (res + 12)) = gs->srcp;
	*((uint16_t *) (res + 14)) = gs->dstp;

	*(res + 16) = (char) gs->diffserv;
	*(res + 17) = 0; /* reserved */
	*(res + 18) = 0; /* reserved */
	*(res + 19) = 0; /* reserved */

	*((uint16_t *) (res + 20)) = gs->t1;
	*(res + 22) = 0; /* reserved */
	*(res + 23) = 0; /* reserved */

	*((uint16_t *) (res + 24)) = gs->t7;
	*((uint16_t *) (res + 26)) = gs->t8;

	*((uint32_t *) (res + 28)) = gs->r;
	*((uint32_t *) (res + 32)) = gs->b;
	*((uint32_t *) (res + 36)) = gs->p;
	*((uint32_t *) (res + 40)) = gs->m;
	*((uint32_t *) (res + 44)) = gs->mm;
	*((uint32_t *) (res + 48)) = gs->rate;
	*((uint32_t *) (res + 52)) = gs->s;
	return 56; /* length */
};

static uint16_t cops_construct_gate (int cmd, char *p,  uint16_t trid,
		uint32_t mtahost, uint32_t actcount, float rate, uint32_t psizegateid,
		uint32_t ssip, uint16_t ssport, uint32_t gateid, struct cops_cmts *cmts)
{
	struct gatespec gs;
	int offset = 0;
	
	ast_debug(3, "CMD: %d\n", cmd);

	/* Transaction Identifier 8 octets */
	*(p + offset++) = 0;
	*(p + offset++) = 8; /* length */
	*(p + offset++) = 1; /* snum */
	*(p + offset++) = 1; /* stype */
	*((uint16_t *) (p + offset)) = htons(trid);
	offset += 2;
	*(p + offset++) = 0;
	*(p + offset++) = (cmd == GATE_DEL) ? PKTCCOPS_SCOMMAND_GATE_DELETE : (cmd != GATE_INFO) ? PKTCCOPS_SCOMMAND_GATE_SET : PKTCCOPS_SCOMMAND_GATE_INFO; /* 4: GATE-SET, 7: GATE-INFO */

	/*Subscriper Identifier 8 octets */
	*(p + offset++) = 0;
	*(p + offset++) = 8; /* length */
	*(p + offset++) = 2; /* snum */
	*(p + offset++) = 1; /* stype */
	*((uint32_t *) (p + offset)) = htonl(mtahost);
	offset += 4;
	
	if (cmd == GATE_INFO || cmd == GATE_SET_HAVE_GATEID || cmd == GATE_DEL) {
		/* Gate ID 8 Octets */
		*(p + offset++) = 0;
		*(p + offset++) = 8; /* length */
		*(p + offset++) = 3; /* snum */
		*(p + offset++) = 1; /* stype */
		*((uint32_t *) (p + offset)) = htonl(gateid);
		offset += 4;
		if (cmd == GATE_INFO || cmd == GATE_DEL) {
			return offset;
		}
	
	}

	/* Activity Count 8 octets */
	*(p + offset++) = 0;
	*(p + offset++) = 8; /* length */
	*(p + offset++) = 4; /* snum */
	*(p + offset++) = 1; /* stype */
	*((uint32_t *) (p + offset)) = htonl(actcount);
	offset += 4;


	/* Gate Spec 2*60 Octets */
	gs.direction = 0; /* DS */
	gs.protocolid = 17; /* UDP */
	gs.flags = 0;
	gs.sessionclass = 1;
	gs.srcip = htonl(ssip);
	gs.dstip = htonl(mtahost);
	gs.srcp = htons(ssport);
	gs.dstp = 0;
/*	gs.diffserv = 0xa0;*/
	gs.diffserv = 0;
	gs.t1 = htons(cmts->t1);
	gs.t7 = htons(cmts->t7);
	gs.t8 = htons(cmts->t8);
	gs.r = ftoieeef(rate);
	gs.b = ftoieeef(psizegateid);
	gs.p = ftoieeef(rate);
	gs.m = htonl((uint32_t) psizegateid);
	gs.mm = htonl((uint32_t) psizegateid);
	gs.rate = ftoieeef(rate);
	gs.s = htonl(800);


	*(p + offset) = 0;
	offset++;
	*(p + offset) = 60; /* length */
	offset++;
	*(p + offset) = 5; /* snum */
	offset++;
	*(p + offset) = 1; /* stype */
	offset++;
	offset += cops_constructgatespec(&gs, p + offset);


	gs.direction = 1; /* US */
	gs.srcip = htonl(mtahost);
	gs.dstip = htonl(ssip);
	gs.srcp = 0;
	gs.dstp = htons(ssport);
	*(p + offset) = 0;
	offset++;
	*(p + offset) = 60; /* length */
	offset++;
	*(p + offset) = 5; /* snum */
	offset++;
	*(p + offset) = 1; /* stype */
	offset++;
	offset += cops_constructgatespec(&gs, p + offset);

	return(offset);
}

static int cops_getmsg (int sfd, struct copsmsg *recmsg)
{
	int len, lent;
	char buf[COPS_HEADER_SIZE];
	struct pktcobj *pobject = NULL;
	uint16_t *ubuf = (uint16_t *) buf;
	recmsg->msg = NULL;
	recmsg->object = NULL;
	len = recv(sfd, buf, COPS_HEADER_SIZE, MSG_DONTWAIT);
	if (len < COPS_HEADER_SIZE) {
		return len;
	}
	recmsg->verflag = *buf;
	recmsg->opcode = *(buf + 1);
	recmsg->clienttype = ntohs(*((uint16_t *) (buf + 2)));
	recmsg->length = ntohl(*((uint32_t *) (buf + 4)));
	/* Eg KA msg*/
	if (recmsg->clienttype != 0x8008 ) {
		if (!(recmsg->msg = ast_malloc(recmsg->length - COPS_HEADER_SIZE))) {
			return -1;
		}
		lent = recv(sfd, recmsg->msg, recmsg->length - COPS_HEADER_SIZE, MSG_DONTWAIT);
		if (lent < recmsg->length - COPS_HEADER_SIZE) {
			return lent;
		}
		len += len;
	} else {
		/* PacketCable Objects */
		while (len < recmsg->length) {
			if (len == COPS_HEADER_SIZE) {
				/* 1st round */
				if (!(recmsg->object = ast_malloc(sizeof(struct pktcobj)))) {
					return -1;
				}
				pobject = recmsg->object;
			} else {
		 		if (!(pobject->next = ast_malloc(sizeof(struct pktcobj)))) {
					return -1;
				}
				pobject = pobject->next;
			}
			pobject->next = NULL;
			lent = recv(sfd, buf, COPS_OBJECT_HEADER_SIZE, MSG_DONTWAIT);
			if (lent < COPS_OBJECT_HEADER_SIZE) {
				ast_debug(3, "Too short object header len: %i\n", lent);
				return lent;
			}
			len += lent;
			pobject->length = ntohs(*ubuf);
			pobject->cnum = *(buf + 2);
			pobject->ctype = *(buf + 3);
			if (!(pobject->contents = ast_malloc(pobject->length - COPS_OBJECT_HEADER_SIZE))) {
				return -1;
			}
			lent = recv(sfd, pobject->contents, pobject->length - COPS_OBJECT_HEADER_SIZE, MSG_DONTWAIT);
			if (lent < pobject->length - COPS_OBJECT_HEADER_SIZE) {
				ast_debug(3, "Too short object content len: %i\n", lent);
				return lent;
			}
			len += lent;
		}
	}
	return len;
}

static int cops_sendmsg (int sfd, struct copsmsg * sendmsg)
{
	char *buf;
	int bufpos;
	struct pktcobj *pobject;
	
	if (sfd < 0) {
		return -1;
	}

	ast_debug(3, "COPS: sending opcode: %i len: %u\n", sendmsg->opcode, sendmsg->length);
	if (sendmsg->length < COPS_HEADER_SIZE) {
		ast_log(LOG_WARNING, "COPS: invalid msg size!!!\n");
		return -1;
	}
	if (!(buf = ast_malloc((size_t) sendmsg->length))) {
		return -1;
	}
	*buf = sendmsg->verflag ;
	*(buf + 1) = sendmsg->opcode;
	*((uint16_t *)(buf + 2)) = htons(sendmsg->clienttype);
	*((uint32_t *)(buf + 4)) = htonl(sendmsg->length);

	if (sendmsg->msg != NULL) {
		memcpy(buf + COPS_HEADER_SIZE, sendmsg->msg, sendmsg->length - COPS_HEADER_SIZE);
	} else if (sendmsg->object != NULL) {
		bufpos = 8;
		pobject = sendmsg->object;
		while(pobject != NULL) {
			ast_debug(3, "COPS: Sending Object : cnum: %i ctype %i len: %i\n", pobject->cnum, pobject->ctype, pobject->length);
			if (sendmsg->length < bufpos + pobject->length) {
				ast_log(LOG_WARNING, "COPS: Invalid msg size len: %u objectlen: %i\n", sendmsg->length, pobject->length);
				ast_free(buf);
				return -1;
			}
			*(uint16_t *) (buf + bufpos) = htons(pobject->length);
			*(buf + bufpos + 2) = pobject->cnum;
			*(buf + bufpos + 3) = pobject->ctype;
			if (sendmsg->length < pobject->length + bufpos) {
				ast_log(LOG_WARNING, "COPS: Error sum of object len more the msg len %u < %i\n", sendmsg->length, pobject->length + bufpos);
				ast_free(buf);
				return -1;
			}
			memcpy((buf + bufpos + 4), pobject->contents, pobject->length - 4);
			bufpos += pobject->length;
			pobject = pobject->next;
		}
	}
	
	errno = 0;
#ifdef HAVE_MSG_NOSIGNAL
#define	SENDFLAGS	MSG_NOSIGNAL | MSG_DONTWAIT
#else
#define	SENDFLAGS	MSG_DONTWAIT
#endif
	if (send(sfd, buf, sendmsg->length, SENDFLAGS) == -1) {
		ast_log(LOG_WARNING, "COPS: Send failed errno=%i\n", errno);
		ast_free(buf);
		return -2;
	}
#undef SENDFLAGS
	ast_free(buf);
	return 0;
}

static void cops_freemsg(struct copsmsg *p)
{
	struct pktcobj *pnext;
	ast_free(p->msg);
	p->msg = NULL;
	while (p->object != NULL) {
			pnext = p->object->next;
			ast_free(p->object->contents);
			p->object->contents = NULL;
			ast_free(p->object);
			p->object = pnext;
	}
	p->object = NULL;
}

struct cops_gate * AST_OPTIONAL_API_NAME(ast_pktccops_gate_alloc)(int cmd,
		struct cops_gate *gate, uint32_t mta, uint32_t actcount, float bitrate,
		uint32_t psize, uint32_t ssip, uint16_t ssport,
		int (* const got_dq_gi) (struct cops_gate *gate),
		int (* const gate_remove) (struct cops_gate *gate))
{
	while (pktcreload) {
		sched_yield();
	}

	if (cmd == GATE_SET_HAVE_GATEID && gate) {
		ast_debug(3, "------- gate modify gateid 0x%x ssip: 0x%x\n", gate->gateid, ssip);
		/* TODO implement it */
		ast_log(LOG_WARNING, "Modify GateID not implemented\n");
	} 
	
	if ((gate = cops_gate_cmd(cmd, NULL, cops_trid++, mta, actcount, bitrate, psize, ssip, ssport, gate))) {
		ast_debug(3, "COPS: Allocating gate for mta: 0x%x\n", mta);
		gate->got_dq_gi = got_dq_gi;
		gate->gate_remove = gate_remove;
		return(gate);
	} else {
		ast_debug(3, "COPS: Couldn't allocate gate for mta: 0x%x\n", mta); 
		return NULL;
	}
}

static struct cops_gate *cops_gate_cmd(int cmd, struct cops_cmts *cmts,
		uint16_t trid, uint32_t mta, uint32_t actcount, float bitrate,
		uint32_t psize, uint32_t ssip, uint16_t ssport, struct cops_gate *gate)
{
	struct copsmsg *gateset;
	struct cops_gate *new;
	struct cops_ippool *ippool;

	if (cmd == GATE_DEL) {
		if (gate == NULL) {
			return NULL;
		} else {
			cmts = gate->cmts;
		}
	}

	if (!cmts) {
		AST_LIST_LOCK(&ippool_list);
		AST_LIST_TRAVERSE(&ippool_list, ippool, list) {
			if (mta >= ippool->start && mta <= ippool->stop) {
				cmts = ippool->cmts;
				break;
			}
		}
		AST_LIST_UNLOCK(&ippool_list);
		if (!cmts) {
			ast_log(LOG_WARNING, "COPS: couldn't find cmts for mta: 0x%x\n", mta);
			return NULL;
		}
		if (cmts->sfd < 0) {
			ast_log(LOG_WARNING, "CMTS: %s not connected\n", cmts->name);
			return NULL;
		}
	}

	if (cmd == GATE_SET) {
		new = ast_calloc(1, sizeof(*new));
		new->gateid = 0;
		new->trid = trid;
		new->mta = mta;
		new->state = GATE_ALLOC_PROGRESS;
		new->checked = time(NULL);
		new->allocated = time(NULL);
		new->cmts = cmts;
		new->got_dq_gi = NULL;
		new->gate_remove = NULL;
		new->gate_open = NULL;
		new->tech_pvt = NULL;
		new->deltimer = 0;
		AST_LIST_LOCK(&gate_list);
		AST_LIST_INSERT_HEAD(&gate_list, new, list);
		AST_LIST_UNLOCK(&gate_list);
		gate = new;
	} else {
		if (gate) {
			gate->trid = trid;
		}
	}
	
	gate->in_transaction = time(NULL);

	if (!(gateset = ast_malloc(sizeof(struct copsmsg)))) {
		ast_free(gateset);
		return NULL;
	}
	gateset->msg = NULL;
	gateset->verflag = 0x10;
	gateset->opcode = 2; /* Decision */
	gateset->clienttype = 0x8008; /* =PacketCable */
	
	/* Handle object */
	gateset->object = ast_malloc(sizeof(struct pktcobj));
	if (!gateset->object) {
		cops_freemsg(gateset);
		ast_free(gateset);
		return NULL;
	}
	gateset->object->length = COPS_OBJECT_HEADER_SIZE + 4;
	gateset->object->cnum = 1; /* Handle */
	gateset->object->ctype = 1; /* client */
	if (!(gateset->object->contents = ast_malloc(sizeof(uint32_t)))) {
		cops_freemsg(gateset);
		ast_free(gateset);
		return NULL;
	}
	*((uint32_t *) gateset->object->contents) = htonl(cmts->handle);

	/* Context Object */
	if (!(gateset->object->next = ast_malloc(sizeof(struct pktcobj)))) {
		cops_freemsg(gateset);
		ast_free(gateset);
		return NULL;
	}
	gateset->object->next->length = COPS_OBJECT_HEADER_SIZE + 4;
	gateset->object->next->cnum = 2; /* Context */
	gateset->object->next->ctype = 1; /* Context */
	if (!(gateset->object->next->contents = ast_malloc(sizeof(uint32_t)))) {
		cops_freemsg(gateset);
		ast_free(gateset);
		return NULL;
	}
	*((uint32_t *) gateset->object->next->contents) = htonl(0x00080000); /* R-Type = 8 configuration request, M-Type = 0 */

	/* Decision Object: Flags */
	if (!(gateset->object->next->next = ast_malloc(sizeof(struct pktcobj)))) {
		cops_freemsg(gateset);
		ast_free(gateset);
		return NULL;
	}
	gateset->object->next->next->length = COPS_OBJECT_HEADER_SIZE + 4;
	gateset->object->next->next->cnum = 6; /* Decision */
	gateset->object->next->next->ctype = 1; /* Flags */
	if (!(gateset->object->next->next->contents = ast_malloc(sizeof(uint32_t)))) {
		cops_freemsg(gateset);
		ast_free(gateset);
		return NULL;
	}
	*((uint32_t *) gateset->object->next->next->contents) = htonl(0x00010001); /* Install, Trigger Error */

	/* Decision Object: Data */
	if (!(gateset->object->next->next->next = ast_malloc(sizeof(struct pktcobj)))) {
		cops_freemsg(gateset);
		ast_free(gateset);
		return NULL;
	}
	gateset->object->next->next->next->length = COPS_OBJECT_HEADER_SIZE + ((cmd != GATE_INFO && cmd != GATE_DEL) ? GATE_SET_OBJ_SIZE : GATE_INFO_OBJ_SIZE) + ((cmd == GATE_SET_HAVE_GATEID) ? GATEID_OBJ_SIZE : 0);
	gateset->object->next->next->next->cnum = 6; /* Decision */
	gateset->object->next->next->next->ctype = 4; /* Decision Data */
	gateset->object->next->next->next->contents = ast_malloc(((cmd != GATE_INFO && cmd != GATE_DEL) ? GATE_SET_OBJ_SIZE : GATE_INFO_OBJ_SIZE) + ((cmd == GATE_SET_HAVE_GATEID) ? GATEID_OBJ_SIZE : 0));
	if (!gateset->object->next->next->next->contents) {
		cops_freemsg(gateset);
		ast_free(gateset);
		return NULL;
	}
	gateset->object->next->next->next->next = NULL;
	
	gateset->length = COPS_HEADER_SIZE + gateset->object->length + gateset->object->next->length + gateset->object->next->next->length + gateset->object->next->next->next->length;

	if ((cmd == GATE_INFO || cmd == GATE_SET_HAVE_GATEID || cmd == GATE_DEL) && gate) {
		ast_debug(1, "Construct gate with gateid: 0x%x\n", gate->gateid);
		cops_construct_gate(cmd, gateset->object->next->next->next->contents, trid, mta, actcount, bitrate, psize, ssip, ssport, gate->gateid, cmts);
	} else {
		ast_debug(1, "Construct new gate\n");
		cops_construct_gate(cmd, gateset->object->next->next->next->contents, trid, mta, actcount, bitrate, psize, ssip, ssport, 0, cmts);
	}
	if (pktccopsdebug) {
		ast_debug(3, "send cmd\n");
	}
	cops_sendmsg(cmts->sfd, gateset);
	cops_freemsg(gateset);
	ast_free(gateset);
	return gate;
}

static int cops_connect(char *host, char *port)
{
	int s, sfd = -1, flags;
	struct addrinfo hints;
	struct addrinfo *rp;
	struct addrinfo *result;
#ifdef HAVE_SO_NOSIGPIPE
	int trueval = 1;
#endif

	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_UNSPEC;    
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	s = getaddrinfo(host, port, &hints, &result);
	if (s != 0) {
		ast_log(LOG_WARNING, "COPS: getaddrinfo: %s\n", gai_strerror(s));
		return -1;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1) {
			ast_log(LOG_WARNING, "Failed socket\n");
		}
		flags = fcntl(sfd, F_GETFL);
		fcntl(sfd, F_SETFL, flags | O_NONBLOCK);
#ifdef HAVE_SO_NOSIGPIPE
		setsockopt(sfd, SOL_SOCKET, SO_NOSIGPIPE, &trueval, sizeof(trueval));
#endif
		connect(sfd, rp->ai_addr, rp->ai_addrlen);
		if (sfd == -1) {
			ast_log(LOG_WARNING, "Failed connect\n");
		}
	}
	freeaddrinfo(result);

	ast_debug(3, "Connecting to cmts:  %s:%s\n", host, port);
	return(sfd);
}

#define PKTCCOPS_DESTROY_CURRENT_GATE   \
		AST_LIST_REMOVE_CURRENT(list);  \
		if (gate->gate_remove) {        \
			gate->gate_remove(gate);    \
		}                               \
		ast_free(gate);

static void *do_pktccops(void *data)
{
	int res, nfds, len;
	struct copsmsg *recmsg, *sendmsg;
	struct copsmsg recmsgb, sendmsgb;
	struct pollfd *pfds = NULL, *tmp;
	struct pktcobj *pobject;
	struct cops_cmts *cmts;
	struct cops_gate *gate;
	char *sobjp;
	uint16_t snst, sobjlen, scommand, recvtrid, actcount, reason, subreason;
	uint32_t gateid, subscrid, pktcerror;
	time_t last_exec = 0;

	recmsg = &recmsgb;
	sendmsg = &sendmsgb;

	ast_debug(3, "COPS: thread started\n");

	for (;;) {
		ast_free(pfds);
		pfds = NULL;
		nfds = 0;
		AST_LIST_LOCK(&cmts_list);
		AST_LIST_TRAVERSE(&cmts_list, cmts, list) {
			if (last_exec != time(NULL)) {
				if (cmts->state == 2 && cmts->katimer + cmts->keepalive < time(NULL)) {
					ast_log(LOG_WARNING, "KA timer (%us) expired cmts: %s\n",  cmts->keepalive, cmts->name);
					cmts->state = 0;
					cmts->katimer = -1;
					close(cmts->sfd);
					cmts->sfd = -1;
				}
			}
			if (cmts->sfd > 0) {
				if (!(tmp = ast_realloc(pfds, (nfds + 1) * sizeof(*pfds)))) {
					continue;
				}
				pfds = tmp;
				pfds[nfds].fd = cmts->sfd;
				pfds[nfds].events = POLLIN;
				pfds[nfds].revents = 0;
				nfds++;
			} else {
				cmts->sfd = cops_connect(cmts->host, cmts->port);
				if (cmts->sfd > 0) {
					cmts->state = 1;
					if (cmts->sfd > 0) {
						if (!(tmp = ast_realloc(pfds, (nfds + 1) * sizeof(*pfds)))) {
							continue;
						}
						pfds = tmp;
						pfds[nfds].fd = cmts->sfd;
						pfds[nfds].events = POLLIN;
						pfds[nfds].revents = 0;
						nfds++;
					}
				}
			}
		}
		AST_LIST_UNLOCK(&cmts_list);

		if (last_exec != time(NULL)) {
			last_exec = time(NULL);
			AST_LIST_LOCK(&gate_list);
			AST_LIST_TRAVERSE_SAFE_BEGIN(&gate_list, gate, list) {
				if (gate) {
					if (gate->deltimer && gate->deltimer < time(NULL)) {
						gate->deltimer = time(NULL) + 5;
						gate->trid = cops_trid++;
						cops_gate_cmd(GATE_DEL, gate->cmts, gate->trid, 0, 0, 0, 0, 0, 0, gate);
						ast_debug(3, "COPS: requested Gate-Del: CMTS: %s gateid: 0x%x\n", (gate->cmts) ? gate->cmts->name : "null", gate->gateid);
					}
					if (time(NULL) - gate->checked > gatetimeout) {
						ast_debug(3, "COPS: remove from list GATE, CMTS: %s gateid: 0x%x\n", (gate->cmts) ? gate->cmts->name : "null", gate->gateid);
						gate->state = GATE_TIMEOUT;
						PKTCCOPS_DESTROY_CURRENT_GATE;
					} else if (time(NULL) - gate->checked > gateinfoperiod && (gate->state == GATE_ALLOCATED || gate->state == GATE_OPEN)) {
						if (gate->cmts && (!gate->in_transaction || ( gate->in_transaction + 5 ) < time(NULL))) {
							gate->trid = cops_trid++;
							ast_debug(3, "COPS: Gate-Info send to CMTS: %s gateid: 0x%x\n", gate->cmts->name, gate->gateid);
							cops_gate_cmd(GATE_INFO, gate->cmts, gate->trid, gate->mta, 0, 0, 0, 0, 0, gate);
						}
					}
				}
			}
			AST_LIST_TRAVERSE_SAFE_END;
			AST_LIST_UNLOCK(&gate_list);
		}

		if (pktcreload == 2) {
			pktcreload = 0;
		}
		if ((res = ast_poll(pfds, nfds, 1000))) {
			AST_LIST_LOCK(&cmts_list);
			AST_LIST_TRAVERSE(&cmts_list, cmts, list) {
				int idx;
				if ((idx = ast_poll_fd_index(pfds, nfds, cmts->sfd)) > -1 && (pfds[idx].revents & POLLIN)) {
					len = cops_getmsg(cmts->sfd, recmsg);
					if (len > 0) {
						ast_debug(3, "COPS: got from %s:\n Header: versflag=0x%02hhx opcode=%i clienttype=0x%04hx msglength=%u\n",
							cmts->name, recmsg->verflag,
							recmsg->opcode, recmsg->clienttype, recmsg->length);
						if (recmsg->object != NULL) {
							pobject = recmsg->object;
							while (pobject != NULL) {
								ast_debug(3, " OBJECT: length=%i cnum=%i ctype=%i\n", pobject->length, pobject->cnum, pobject->ctype);
								if (recmsg->opcode == 1 && pobject->cnum == 1 && pobject->ctype == 1 ) {
									cmts->handle = ntohl(*((uint32_t *) pobject->contents));
									ast_debug(3, "    REQ client handle: %u\n", cmts->handle);
									cmts->state = 2;
									cmts->katimer = time(NULL);
								} else if (pobject->cnum == 9 && pobject->ctype == 1) {
									sobjp = pobject->contents;
									subscrid = 0;
									recvtrid = 0;
									scommand = 0;
									pktcerror = 0;
									actcount = 0;
									gateid = 0;
									reason = 0;
									subreason = 0;
									while (sobjp < (pobject->contents + pobject->length - 4)) {
										sobjlen = ntohs(*((uint16_t *) sobjp));
										snst = ntohs(*((uint16_t *) (sobjp + 2)));
										ast_debug(3, "   S-Num S-type: 0x%.4x len: %i\n", (unsigned)snst, sobjlen);
										if (snst == 0x0101 ) {
											recvtrid = ntohs(*((uint16_t *) (sobjp + 4)));
											scommand = ntohs(*((uint16_t *) (sobjp + 6)));					
											ast_debug(3, "     Transaction Identifier command: %i trid %i\n", scommand, recvtrid);
										} else if (snst == 0x0201) {
											subscrid = ntohl(*((uint32_t *) (sobjp + 4)));
											ast_debug(3, "     Subscriber ID: 0x%.8x\n", subscrid);
										} else if (snst == 0x0301) {
											gateid = ntohl(*((uint32_t *) (sobjp + 4)));
											ast_debug(3, "      Gate ID: 0x%x 0x%.8x\n", gateid, gateid);
										} else if (snst == 0x0401) {
											actcount = ntohs(*((uint16_t *) (sobjp + 6)));
											ast_debug(3, "      Activity Count: %i\n", actcount);
										} else if (snst == 0x0901) {
											pktcerror = ntohl(*((uint32_t *) (sobjp + 4)));
											ast_debug(3, "      PKTC Error: 0x%.8x\n", pktcerror);
										} else if (snst == 0x0d01) {
											reason = ntohs(*((uint16_t *) (sobjp + 4)));
											subreason = ntohs(*((uint16_t *) (sobjp + 6)));
											ast_debug(3, "      Reason: %d Subreason: %d\n", reason, subreason);
										}
										sobjp += sobjlen;
										if (!sobjlen)
											break;
									}
									if (scommand == PKTCCOPS_SCOMMAND_GATE_CLOSE || scommand == PKTCCOPS_SCOMMAND_GATE_OPEN) {
										AST_LIST_LOCK(&gate_list);
										AST_LIST_TRAVERSE_SAFE_BEGIN(&gate_list, gate, list) {
											if (gate->cmts == cmts && gate->gateid == gateid) {
												if (scommand == PKTCCOPS_SCOMMAND_GATE_CLOSE && gate->state != GATE_CLOSED && gate->state != GATE_CLOSED_ERR ) {
													ast_debug(3, "COPS Gate Close Gate ID: 0x%x TrId: %i CMTS: %s\n", gateid, recvtrid, cmts->name);
													if (subreason) {
														gate->state = GATE_CLOSED_ERR;
														PKTCCOPS_DESTROY_CURRENT_GATE;
													} else {
														gate->state = GATE_CLOSED;
														PKTCCOPS_DESTROY_CURRENT_GATE;
													}
													break;
												} else if (scommand == PKTCCOPS_SCOMMAND_GATE_OPEN && gate->state == GATE_ALLOCATED) {
													ast_debug(3, "COPS Gate Open Gate ID: 0x%x TrId: %i CMTS: %s\n", gateid, recvtrid, cmts->name);
													gate->state = GATE_OPEN;
													if (gate->gate_open) {
														ast_debug(3, "Calling GATE-OPEN callback function\n");
														gate->gate_open(gate);
														gate->gate_open = NULL;
													}
													break;
												} 
											}
										}
										AST_LIST_TRAVERSE_SAFE_END;
										AST_LIST_UNLOCK(&gate_list);
									} else if (scommand == PKTCCOPS_SCOMMAND_GATE_SET_ACK || scommand == PKTCCOPS_SCOMMAND_GATE_SET_ERR || scommand == PKTCCOPS_SCOMMAND_GATE_INFO_ACK || scommand == PKTCCOPS_SCOMMAND_GATE_INFO_ERR || scommand == PKTCCOPS_SCOMMAND_GATE_DELETE_ACK) {
										AST_LIST_LOCK(&gate_list);
										AST_LIST_TRAVERSE_SAFE_BEGIN(&gate_list, gate, list) {
											if (gate->cmts == cmts && gate->trid == recvtrid) {
												gate->gateid = gateid;
												gate->checked = time(NULL);
												if (scommand == PKTCCOPS_SCOMMAND_GATE_SET_ACK) {
													ast_debug(3, "COPS Gate Set Ack Gate ID: 0x%x TrId: %i CMTS: %s\n", gateid, recvtrid, cmts->name);
													gate->state = GATE_ALLOCATED;
													if (gate->got_dq_gi) {
														gate->got_dq_gi(gate);
														gate->got_dq_gi = NULL;
													}
												} else if (scommand == PKTCCOPS_SCOMMAND_GATE_SET_ERR) {
													ast_debug(3, "COPS Gate Set Error TrId: %i ErrorCode: 0x%.8x CMTS: %s\n ", recvtrid, pktcerror, cmts->name);
													gate->state = GATE_ALLOC_FAILED;
													if (gate->got_dq_gi) {
														gate->got_dq_gi(gate);
														gate->got_dq_gi = NULL;
													}
													PKTCCOPS_DESTROY_CURRENT_GATE;
												} else if (scommand == PKTCCOPS_SCOMMAND_GATE_INFO_ACK) {
													ast_debug(3, "COPS Gate Info Ack Gate ID: 0x%x TrId: %i CMTS: %s\n", gateid, recvtrid, cmts->name);
												} else if (scommand == PKTCCOPS_SCOMMAND_GATE_INFO_ERR) {
													ast_debug(3, "COPS Gate Info Error Gate ID: 0x%x TrId: %i CMTS: %s\n", gateid, recvtrid, cmts->name);
													gate->state = GATE_ALLOC_FAILED;
													PKTCCOPS_DESTROY_CURRENT_GATE;
												} else if (scommand == PKTCCOPS_SCOMMAND_GATE_DELETE_ACK) {
													ast_debug(3, "COPS Gate Deleted Gate ID: 0x%x TrId: %i CMTS: %s\n", gateid, recvtrid, cmts->name);
													gate->state = GATE_DELETED;
													PKTCCOPS_DESTROY_CURRENT_GATE;
												}
												gate->in_transaction = 0;
												break;
											}
										}
										AST_LIST_TRAVERSE_SAFE_END;
										AST_LIST_UNLOCK(&gate_list);
									}
								}
								pobject = pobject->next;
							}
						}

						if (recmsg->opcode == 6 && recmsg->object && recmsg->object->cnum == 11 && recmsg->object->ctype == 1) {
							ast_debug(3, "COPS: Client open %s\n", cmts->name);
							sendmsg->msg = NULL;
							sendmsg->verflag = 0x10;
							sendmsg->opcode = 7; /* Client Accept */
							sendmsg->clienttype = 0x8008; /* =PacketCable */
							sendmsg->length = COPS_HEADER_SIZE + COPS_OBJECT_HEADER_SIZE + 4;
							sendmsg->object = ast_malloc(sizeof(struct pktcobj));
							sendmsg->object->length = 4 + COPS_OBJECT_HEADER_SIZE;
							sendmsg->object->cnum = 10; /* keppalive timer*/
							sendmsg->object->ctype = 1;
							sendmsg->object->contents = ast_malloc(sizeof(uint32_t));
							*((uint32_t *) sendmsg->object->contents) = htonl(cmts->keepalive & 0x0000ffff);
							sendmsg->object->next = NULL;
							cops_sendmsg(cmts->sfd, sendmsg);
							cops_freemsg(sendmsg);
						} else if (recmsg->opcode == 9) {
							ast_debug(3, "COPS: Keepalive Request got echoing back %s\n", cmts->name);
							cops_sendmsg(cmts->sfd, recmsg);
							cmts->state = 2;
							cmts->katimer = time(NULL);
						}
					} 
					if (len <= 0) {
						ast_debug(3, "COPS: lost connection to %s\n", cmts->name);
						close(cmts->sfd);
						cmts->sfd = -1;
						cmts->state = 0;
					}
					cops_freemsg(recmsg);
				}
			}
			AST_LIST_UNLOCK(&cmts_list);			
		}
		if (pktcreload) {
			ast_debug(3, "Reloading pktccops...\n");
			AST_LIST_LOCK(&gate_list);
			AST_LIST_LOCK(&cmts_list);
			pktccops_unregister_ippools();
			AST_LIST_TRAVERSE(&cmts_list, cmts, list) {
				cmts->need_delete = 1;
			}
			load_pktccops_config();
			AST_LIST_TRAVERSE_SAFE_BEGIN(&cmts_list, cmts, list) {
				if (cmts && cmts->need_delete) {
					AST_LIST_TRAVERSE(&gate_list, gate, list) {
						if (gate->cmts == cmts) {
							ast_debug(3, "Null gate %s\n", gate->cmts->name);
							gate->cmts = NULL;
						}
						gate->in_transaction = 0;
					}
					AST_LIST_UNLOCK(&gate_list);
					ast_debug(3, "removing cmts: %s\n", cmts->name);
					if (cmts->sfd > 0) {
						close(cmts->sfd);
					}
					AST_LIST_REMOVE_CURRENT(list);
					ast_free(cmts);
				}
			}
			AST_LIST_TRAVERSE_SAFE_END;
			AST_LIST_UNLOCK(&cmts_list);
			AST_LIST_UNLOCK(&gate_list);
			pktcreload = 2;
		}
		pthread_testcancel();
	}
	return NULL;
}

static int restart_pktc_thread(void)
{
	if (pktccops_thread == AST_PTHREADT_STOP) {
		return 0;
	}
	if (ast_mutex_lock(&pktccops_lock)) {
		ast_log(LOG_WARNING, "Unable to lock pktccops\n");
		return -1;
	}
	if (pktccops_thread == pthread_self()) {
		ast_mutex_unlock(&pktccops_lock);
		ast_log(LOG_WARNING, "Cannot kill myself\n");
		return -1;
	}
	if (pktccops_thread != AST_PTHREADT_NULL) {
		/* Wake up the thread */
		pthread_kill(pktccops_thread, SIGURG);
	} else {
		/* Start a new monitor */
		if (ast_pthread_create_background(&pktccops_thread, NULL, do_pktccops, NULL) < 0) {
			ast_mutex_unlock(&pktccops_lock);
			ast_log(LOG_ERROR, "Unable to start monitor thread.\n");
			return -1;
		}
	}
	ast_mutex_unlock(&pktccops_lock);
	return 0;
}

static int load_pktccops_config(void)
{
	static char *cfg = "res_pktccops.conf";
	struct ast_config *config;
	struct ast_variable *v;
	struct cops_cmts *cmts;
	struct cops_ippool *new_ippool;
	const char *host, *cat, *port;
	int update;
	int res = 0;
	uint16_t t1_temp, t7_temp, t8_temp;
	uint32_t keepalive_temp;
	unsigned int a,b,c,d,e,f,g,h;
	struct ast_flags config_flags = {0};

	if (!(config = ast_config_load(cfg, config_flags))) {
		ast_log(LOG_WARNING, "Unable to load config file res_pktccops.conf\n");
		return -1;
	}
	for (cat = ast_category_browse(config, NULL); cat; cat = ast_category_browse(config, cat)) {
		if (!strcmp(cat, "general")) {
			for (v = ast_variable_browse(config, cat); v; v = v->next) {
				if (!strcasecmp(v->name, "t1")) {
					t1 = atoi(v->value);
				} else if (!strcasecmp(v->name, "t7")) {
					t7 = atoi(v->value);
				} else if (!strcasecmp(v->name, "t8")) {
					t8 = atoi(v->value);
				} else if (!strcasecmp(v->name, "keepalive")) {
					keepalive = atoi(v->value);
				} else if (!strcasecmp(v->name, "gateinfoperiod")) {
					gateinfoperiod = atoi(v->value);
				} else if (!strcasecmp(v->name, "gatetimeout")) {
					gatetimeout = atoi(v->value);
				} else {
					ast_log(LOG_WARNING, "Unkown option %s in general section of res_ptkccops.conf\n", v->name);
				}
			}			
		} else {
			/* Defaults */
			host = NULL;
			port = NULL;
			t1_temp = t1;
			t7_temp = t7;
			t8_temp = t8;
			keepalive_temp = keepalive;

			for (v = ast_variable_browse(config, cat); v; v = v->next) {
				if (!strcasecmp(v->name, "host")) {
					host = v->value;				
				} else if (!strcasecmp(v->name, "port")) {
					port = v->value;
				} else if (!strcasecmp(v->name, "t1")) {
					t1_temp = atoi(v->value);
				} else if (!strcasecmp(v->name, "t7")) {
					t7_temp = atoi(v->value);
				} else if (!strcasecmp(v->name, "t8")) {
					t8_temp = atoi(v->value);
				} else if (!strcasecmp(v->name, "keepalive")) {
					keepalive_temp = atoi(v->value);
				} else if (!strcasecmp(v->name, "pool")) {
					/* we weill parse it in 2nd round */
				} else {
					ast_log(LOG_WARNING, "Unkown option %s in res_ptkccops.conf\n", v->name);
				}
			}

			update = 0;
			AST_LIST_TRAVERSE(&cmts_list, cmts, list) {
				if (!strcmp(cmts->name, cat)) {
					update = 1;
					break;
				}

			}
			if (!update) {
				cmts = ast_calloc(1, sizeof(*cmts));
				if (!cmts) {
					res = -1;
					break;
				}
				AST_LIST_INSERT_HEAD(&cmts_list, cmts, list);
			}
			if (cat) {
				ast_copy_string(cmts->name, cat, sizeof(cmts->name));
			}
			if (host) {
				ast_copy_string(cmts->host, host, sizeof(cmts->host));
			}
			if (port) {
				ast_copy_string(cmts->port, port, sizeof(cmts->port));
			} else {
				ast_copy_string(cmts->port, DEFAULT_COPS_PORT, sizeof(cmts->port));
			}

			cmts->t1 = t1_temp;
			cmts->t7 = t7_temp;
			cmts->t8 = t8_temp;
			cmts->keepalive = keepalive_temp;
			if (!update) {
				cmts->state = 0;
				cmts->sfd = -1;
			}
			cmts->need_delete = 0;
			for (v = ast_variable_browse(config, cat); v; v = v->next) {
				/* parse ipppol when we have cmts ptr */
				if (!strcasecmp(v->name, "pool")) {
					if (sscanf(v->value, "%3u.%3u.%3u.%3u %3u.%3u.%3u.%3u", &a, &b, &c, &d, &e, &f, &g, &h) == 8) {
						new_ippool = ast_calloc(1, sizeof(*new_ippool));
						if (!new_ippool) {
							res = -1;
							break;
						}
						new_ippool->start = a << 24 | b << 16 | c << 8 | d;
						new_ippool->stop = e << 24 | f << 16 | g << 8 | h;
						new_ippool->cmts = cmts;
						pktccops_add_ippool(new_ippool);
					} else {
						ast_log(LOG_WARNING, "Invalid ip pool format in res_pktccops.conf\n");
					}
				}
			}
		}
	}
	ast_config_destroy(config);
	return res;
}

static char *pktccops_show_cmtses(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct cops_cmts *cmts;
	char statedesc[16];
	int katimer;
	
	switch(cmd) {
	case CLI_INIT:
		e->command = "pktccops show cmtses";
		e->usage = 
			"Usage: pktccops show cmtses\n"
			"       List PacketCable COPS CMTSes.\n";

		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "%-16s %-24s %-12s %7s\n", "Name        ", "Host                ", "Status    ", "KA timer  ");
	ast_cli(a->fd, "%-16s %-24s %-12s %7s\n", "------------", "--------------------", "----------", "-----------");
	AST_LIST_LOCK(&cmts_list);
	AST_LIST_TRAVERSE(&cmts_list, cmts, list) {
		katimer = -1;
		if (cmts->state == 2) {
			ast_copy_string(statedesc, "Connected", sizeof(statedesc));
			katimer = (int) (time(NULL) - cmts->katimer);
		} else if (cmts->state == 1) {
			ast_copy_string(statedesc, "Connecting", sizeof(statedesc));
		} else {
			ast_copy_string(statedesc, "N/A", sizeof(statedesc));
		}
		ast_cli(a->fd, "%-16s %-15s:%-8s %-12s %-7d\n", cmts->name, cmts->host, cmts->port, statedesc, katimer);
	}
	AST_LIST_UNLOCK(&cmts_list);
	return CLI_SUCCESS;
}

static char *pktccops_show_gates(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct cops_gate *gate;
	char state_desc[16];

	switch(cmd) {
	case CLI_INIT:
		e->command = "pktccops show gates";
		e->usage = 
			"Usage: pktccops show gates\n"
			"       List PacketCable COPS GATEs.\n";

		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "%-16s %-12s %-12s %-10s %-10s %-10s\n" ,"CMTS", "Gate-Id","MTA", "Status", "AllocTime", "CheckTime");
	ast_cli(a->fd, "%-16s %-12s %-12s %-10s %-10s %-10s\n" ,"--------------" ,"----------", "----------", "--------", "--------", "--------\n");
	AST_LIST_LOCK(&cmts_list);
	AST_LIST_LOCK(&gate_list);
	AST_LIST_TRAVERSE(&gate_list, gate, list) {
		if (gate->state == GATE_ALLOC_FAILED) {
			ast_copy_string(state_desc, "Failed", sizeof(state_desc));
		} else if (gate->state == GATE_ALLOC_PROGRESS) {
			ast_copy_string(state_desc, "In Progress", sizeof(state_desc));
		} else if (gate->state == GATE_ALLOCATED) {
			ast_copy_string(state_desc, "Allocated", sizeof(state_desc));
		} else if (gate->state == GATE_CLOSED) {
			ast_copy_string(state_desc, "Closed", sizeof(state_desc));
		} else if (gate->state == GATE_CLOSED_ERR) {
			ast_copy_string(state_desc, "ClosedErr", sizeof(state_desc));
		} else if (gate->state == GATE_OPEN) {
			ast_copy_string(state_desc, "Open", sizeof(state_desc));
		} else if (gate->state == GATE_DELETED) {
			ast_copy_string(state_desc, "Deleted", sizeof(state_desc));
		} else {
			ast_copy_string(state_desc, "N/A", sizeof(state_desc));
		}
		
		ast_cli(a->fd, "%-16s 0x%.8x   0x%08x   %-10s %10i %10i %u\n", (gate->cmts) ? gate->cmts->name : "null" , gate->gateid, gate->mta, 
			state_desc, (int) (time(NULL) - gate->allocated), (gate->checked) ? (int) (time(NULL) - gate->checked) : 0, (unsigned int) gate->in_transaction);
	}
	AST_LIST_UNLOCK(&cmts_list);
	AST_LIST_UNLOCK(&gate_list);
	return CLI_SUCCESS;
}

static char *pktccops_show_pools(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct cops_ippool *ippool;
	char start[32];
	char stop[32];

	switch(cmd) {
	case CLI_INIT:
		e->command = "pktccops show pools";
		e->usage = 
			"Usage: pktccops show pools\n"
			"       List PacketCable COPS ip pools of MTAs.\n";

		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "%-16s %-18s %-7s\n", "Start     ", "Stop      ", "CMTS    ");
	ast_cli(a->fd, "%-16s %-18s %-7s\n", "----------", "----------", "--------");
	AST_LIST_LOCK(&ippool_list);
	AST_LIST_TRAVERSE(&ippool_list, ippool, list) {
		snprintf(start, sizeof(start), "%3u.%3u.%3u.%3u", ippool->start >> 24, (ippool->start >> 16) & 0x000000ff, (ippool->start >> 8) & 0x000000ff, ippool->start & 0x000000ff);

		snprintf(stop, sizeof(stop), "%3u.%3u.%3u.%3u", ippool->stop >> 24, (ippool->stop >> 16) & 0x000000ff, (ippool->stop >> 8) & 0x000000ff, ippool->stop & 0x000000ff);
		ast_cli(a->fd, "%-16s %-18s %-16s\n", start, stop, ippool->cmts->name);
	}
	AST_LIST_UNLOCK(&ippool_list);
	return CLI_SUCCESS;
}

static char *pktccops_gatedel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int found = 0;
	int trid;
	uint32_t gateid;
	struct cops_gate *gate;
	struct cops_cmts *cmts;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pktccops gatedel";
		e->usage = 
			"Usage: pktccops gatedel <cmts> <gateid>\n"
			"       Send Gate-Del to cmts.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 4)
		return CLI_SHOWUSAGE;

	AST_LIST_LOCK(&cmts_list);
	AST_LIST_TRAVERSE(&cmts_list, cmts, list) {
		if (!strcmp(cmts->name, a->argv[2])) {
			ast_cli(a->fd, "Found cmts: %s\n", cmts->name);
			found = 1;
			break;
		}
	}
	AST_LIST_UNLOCK(&cmts_list);
	
	if (!found)
		return CLI_SHOWUSAGE;

	trid = cops_trid++;
	if (!sscanf(a->argv[3], "%x", &gateid)) {
		ast_cli(a->fd, "bad gate specification (%s)\n", a->argv[3]);	
		return CLI_SHOWUSAGE;
	}

	found = 0;
	AST_LIST_LOCK(&gate_list);
	AST_LIST_TRAVERSE(&gate_list, gate, list) {
		if (gate->gateid == gateid && gate->cmts == cmts) {
			found = 1;
			break;
		}
	}
		
	if (!found) {
		ast_cli(a->fd, "gate not found: %s\n", a->argv[3]);
		return CLI_SHOWUSAGE;
	}

	AST_LIST_UNLOCK(&gate_list);
	cops_gate_cmd(GATE_DEL, cmts, trid, 0, 0, 0, 0, 0, 0, gate);
	return CLI_SUCCESS;
}

static char *pktccops_gateset(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int foundcmts = 0;
	int trid;
	unsigned int an,bn,cn,dn;
	uint32_t mta, ssip;
	struct cops_cmts *cmts;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pktccops gateset";
		e->usage = 
			"Usage: pktccops gateset <cmts> <mta> <acctcount> <bitrate> <packet size> <switch ip> <switch port>\n"
			"       Send Gate-Set to cmts.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 9)
		return CLI_SHOWUSAGE;

	if (!strcmp(a->argv[2], "null")) {
		cmts = NULL;
	} else {
		AST_LIST_LOCK(&cmts_list);
		AST_LIST_TRAVERSE(&cmts_list, cmts, list) {
			if (!strcmp(cmts->name, a->argv[2])) {
				ast_cli(a->fd, "Found cmts: %s\n", cmts->name);
				foundcmts = 1;
				break;
			}
		}
		AST_LIST_UNLOCK(&cmts_list);
		if (!foundcmts) {
			ast_cli(a->fd, "CMTS not found: %s\n", a->argv[2]);
			return CLI_SHOWUSAGE;
		}
	}

	trid = cops_trid++;
	if (sscanf(a->argv[3], "%3u.%3u.%3u.%3u", &an, &bn, &cn, &dn) != 4) {
		ast_cli(a->fd, "MTA specification (%s) does not look like an ipaddr\n", a->argv[3]);
		return CLI_SHOWUSAGE;
	}
	mta = an << 24 | bn << 16 | cn << 8 | dn;

	if (sscanf(a->argv[7], "%3u.%3u.%3u.%3u", &an, &bn, &cn, &dn) != 4) {
		ast_cli(a->fd, "SSIP specification (%s) does not look like an ipaddr\n", a->argv[7]);
		return CLI_SHOWUSAGE;
	}
	ssip = an << 24 | bn << 16 | cn << 8 | dn;

	cops_gate_cmd(GATE_SET, cmts, trid, mta, atoi(a->argv[4]), atof(a->argv[5]), atoi(a->argv[6]), ssip, atoi(a->argv[8]), NULL);
	return CLI_SUCCESS;
}

static char *pktccops_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "pktccops set debug {on|off}";
		e->usage = 
			"Usage: pktccops set debug {on|off}\n"
			"	Turn on/off debuging\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;
	if (!strncasecmp(a->argv[e->args - 1], "on", 2)) {
		pktccopsdebug = 1;
		ast_cli(a->fd, "PktcCOPS Debugging Enabled\n");
	} else if (!strncasecmp(a->argv[e->args - 1], "off", 2)) {
		pktccopsdebug = 0;
		ast_cli(a->fd, "PktcCOPS Debugging Disabled\n");
	} else {
		return CLI_SHOWUSAGE;
	}
	return CLI_SUCCESS;

}

static struct ast_cli_entry cli_pktccops[] = {
	AST_CLI_DEFINE(pktccops_show_cmtses, "List PacketCable COPS CMTSes"),
	AST_CLI_DEFINE(pktccops_show_gates, "List PacketCable COPS GATEs"),
	AST_CLI_DEFINE(pktccops_show_pools, "List PacketCable MTA pools"),
	AST_CLI_DEFINE(pktccops_gateset, "Send Gate-Set to cmts"),
	AST_CLI_DEFINE(pktccops_gatedel, "Send Gate-Det to cmts"),
	AST_CLI_DEFINE(pktccops_debug, "Enable/Disable COPS debugging")
};

static int pktccops_add_ippool(struct cops_ippool *ippool)
{
	if (ippool) {
		AST_LIST_LOCK(&ippool_list);
		AST_LIST_INSERT_HEAD(&ippool_list, ippool, list);
		AST_LIST_UNLOCK(&ippool_list);
		return 0;
	} else {
		ast_log(LOG_WARNING, "Attempted to register NULL ippool?\n");
		return -1;
	}
}

static void pktccops_unregister_cmtses(void)
{
	struct cops_cmts *cmts;
	struct cops_gate *gate;
	AST_LIST_LOCK(&cmts_list);
	while ((cmts = AST_LIST_REMOVE_HEAD(&cmts_list, list))) {
		if (cmts->sfd > 0) {
			close(cmts->sfd);
		}
		ast_free(cmts);
	}
	AST_LIST_UNLOCK(&cmts_list);

	AST_LIST_LOCK(&gate_list);
	while ((gate = AST_LIST_REMOVE_HEAD(&gate_list, list))) {
		ast_free(gate);
	}
	AST_LIST_UNLOCK(&gate_list);
}

static void pktccops_unregister_ippools(void)
{
	struct cops_ippool *ippool;
	AST_LIST_LOCK(&ippool_list);
	while ((ippool = AST_LIST_REMOVE_HEAD(&ippool_list, list))) {
		ast_free(ippool);
	}
	AST_LIST_UNLOCK(&ippool_list);
}

static int load_module(void)
{
	int res;
	AST_LIST_LOCK(&cmts_list);
	res = load_pktccops_config();
	AST_LIST_UNLOCK(&cmts_list);
	if (res == -1) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_cli_register_multiple(cli_pktccops, sizeof(cli_pktccops) / sizeof(struct ast_cli_entry));
	restart_pktc_thread();
	return 0;
}

static void unload_module(void)
{
	if (!ast_mutex_lock(&pktccops_lock)) {
		if ((pktccops_thread != AST_PTHREADT_NULL) && (pktccops_thread != AST_PTHREADT_STOP)) {
			pthread_cancel(pktccops_thread);
			pthread_kill(pktccops_thread, SIGURG);
			pthread_join(pktccops_thread, NULL);
		}
		pktccops_thread = AST_PTHREADT_STOP;
		ast_mutex_unlock(&pktccops_lock);
	} else {
		ast_log(LOG_ERROR, "Unable to lock the pktccops_thread\n");
		ast_module_block_unload(AST_MODULE_SELF);
		return;
	}

	pktccops_unregister_cmtses();
	pktccops_unregister_ippools();
	pktccops_thread = AST_PTHREADT_NULL;
}

static int reload_module(void)
{
	/* Prohibit unloading */
	if (pktcreload) {
		ast_log(LOG_NOTICE, "Previous reload in progress, please wait!\n");
		return -1;
	}
	pktcreload = 1;
	return 0;
}

AST_MODULE_INFO_RELOADABLE(ASTERISK_GPL_KEY, "PktcCOPS manager for MGCP");
