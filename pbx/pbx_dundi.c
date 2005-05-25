/*
 * Distributed Universal Number Discovery (DUNDi)
 *
 * Copyright (C) 2004 - 2005, Digium Inc.
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * This program is Free Software distributed under the terms of
 * of the GNU General Public License.
 */

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/md5.h"
#include "asterisk/dundi.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/utils.h"
#include "asterisk/crypto.h"
#include "asterisk/astdb.h"
#include "asterisk/acl.h"
#include "asterisk/aes.h"

#include "dundi-parser.h"
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(SOLARIS) || defined(__Darwin__)
#include <sys/types.h>
#include <netinet/in_systm.h>
#endif
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__Darwin__)
#include <net/if_dl.h>
#include <ifaddrs.h>
#endif
#include <zlib.h>

#define MAX_RESULTS	64

#define MAX_PACKET_SIZE 8192

extern char ast_config_AST_KEY_DIR[];

static char *tdesc = "Distributed Universal Number Discovery (DUNDi)";

static char *app = "DUNDiLookup";
static char *synopsis = "Look up a number with DUNDi";
static char *descrip = 
"DUNDiLookup(number[|context[|options]])\n"
"      Looks up a given number in the global context specified or in\n"
"the reserved 'e164' context if not specified.  Returns -1 if the channel\n"
"is hungup during the lookup or 0 otherwise.  On completion, the variable\n"
"${DUNDTECH} and ${DUNDDEST} will contain the technology and destination\n"
"of the appropriate technology and destination to access the number. If no\n"
"answer was found, and the priority n + 101 exists, execution will continue\n"
"at that location.\n";

#define DUNDI_MODEL_INBOUND		(1 << 0)
#define DUNDI_MODEL_OUTBOUND	(1 << 1)
#define DUNDI_MODEL_SYMMETRIC	(DUNDI_MODEL_INBOUND | DUNDI_MODEL_OUTBOUND)

/* Keep times of last 10 lookups */
#define DUNDI_TIMING_HISTORY	10

#define FLAG_ISREG		(1 << 0)		/* Transaction is register request */
#define FLAG_DEAD		(1 << 1)		/* Transaction is dead */
#define FLAG_FINAL		(1 << 2)		/* Transaction has final message sent */
#define FLAG_ISQUAL		(1 << 3)		/* Transaction is a qualification */
#define FLAG_ENCRYPT	(1 << 4)		/* Transaction is encrypted wiht ECX/DCX */
#define FLAG_SENDFULLKEY	(1 << 5)		/* Send full key on transaction */
#define FLAG_STOREHIST	(1 << 6)		/* Record historic performance */

#define DUNDI_FLAG_INTERNAL_NOPARTIAL (1 << 17)

#if 0
#define DUNDI_SECRET_TIME 15	/* Testing only */
#else
#define DUNDI_SECRET_TIME DUNDI_DEFAULT_CACHE_TIME
#endif

#define KEY_OUT			0
#define KEY_IN			1

static struct io_context *io;
static struct sched_context *sched;
static int netsocket = -1;
static pthread_t netthreadid = AST_PTHREADT_NULL;
static pthread_t precachethreadid = AST_PTHREADT_NULL;
static int tos = 0;
static int dundidebug = 0;
static int authdebug = 0;
static int dundi_ttl = DUNDI_DEFAULT_TTL;
static int dundi_key_ttl = DUNDI_DEFAULT_KEY_EXPIRE;
static int global_autokilltimeout = 0;
static dundi_eid global_eid;
static int default_expiration = 60;
static int global_storehistory = 0;
static char dept[80];
static char org[80];
static char locality[80];
static char stateprov[80];
static char country[80];
static char email[80];
static char phone[80];
static char secretpath[80];
static char cursecret[80];
static char ipaddr[80];
static time_t rotatetime;
static dundi_eid empty_eid = { { 0, 0, 0, 0, 0, 0 } };
struct permission {
	struct permission *next;
	int allow;
	char name[0];
};

struct dundi_packet {
	struct dundi_hdr *h;
	struct dundi_packet *next;
	int datalen;
	struct dundi_transaction *parent;
	int retransid;
	int retrans;
	unsigned char data[0];
};

struct dundi_hint_metadata {
	unsigned short flags;
	char exten[AST_MAX_EXTENSION];
};

struct dundi_precache_queue {
	struct dundi_precache_queue *next;
	char *context;
	time_t expiration;
	char number[0];
};

struct dundi_request;

struct dundi_transaction {
	struct sockaddr_in addr;	/* Other end of transaction */
	struct timeval start;		/* When this transaction was created */
	dundi_eid eids[DUNDI_MAX_STACK + 1];
	int eidcount;				/* Number of eids in eids */
	dundi_eid us_eid;			/* Our EID, to them */
	dundi_eid them_eid;			/* Their EID, to us */
	aes_encrypt_ctx	ecx;		/* AES 128 Encryption context */
	aes_decrypt_ctx	dcx;		/* AES 128 Decryption context */
	unsigned int flags;				/* Has final packet been sent */
	int ttl;					/* Remaining TTL for queries on this one */
	int thread;					/* We have a calling thread */
	int retranstimer;			/* How long to wait before retransmissions */
	int autokillid;				/* ID to kill connection if answer doesn't come back fast enough */
	int autokilltimeout;		/* Recommended timeout for autokill */
	unsigned short strans;		/* Our transaction identifier */
	unsigned short dtrans;		/* Their transaction identifer */
	unsigned char iseqno;		/* Next expected received seqno */
	unsigned char oiseqno;		/* Last received incoming seqno */
	unsigned char oseqno;		/* Next transmitted seqno */
	unsigned char aseqno;		/* Last acknowledge seqno */
	struct dundi_packet *packets;	/* Packets to be retransmitted */
	struct dundi_packet *lasttrans;	/* Last transmitted / ACK'd packet */
	struct dundi_transaction *next;	/* Next with respect to the parent */
	struct dundi_request *parent;	/* Parent request (if there is one) */
	struct dundi_transaction *allnext; /* Next with respect to all DUNDi transactions */
} *alltrans;

struct dundi_request {
	char dcontext[AST_MAX_EXTENSION];
	char number[AST_MAX_EXTENSION];
	dundi_eid query_eid;
	dundi_eid root_eid;
	struct dundi_result *dr;
	struct dundi_entity_info *dei;
	struct dundi_hint_metadata *hmd;
	int maxcount;
	int respcount;
	int expiration;
	int cbypass;
	int pfds[2];
	unsigned long crc32;							/* CRC-32 of all but root EID's in avoid list */
	struct dundi_transaction *trans;	/* Transactions */
	struct dundi_request *next;
} *requests;

static struct dundi_mapping {
	char dcontext[AST_MAX_EXTENSION];
	char lcontext[AST_MAX_EXTENSION];
	int weight;
	int options;
	int tech;
	int dead;
	char dest[AST_MAX_EXTENSION];
	struct dundi_mapping *next;
} *mappings = NULL;

static struct dundi_peer {
	dundi_eid eid;
	struct sockaddr_in addr;	/* Address of DUNDi peer */
	struct permission *permit;
	struct permission *include;
	struct permission *precachesend;
	struct permission *precachereceive;
	dundi_eid us_eid;
	char inkey[80];
	char outkey[80];
	int dead;
	int registerid;
	int qualifyid;
	int sentfullkey;
	int order;
	unsigned char txenckey[256]; /* Transmitted encrypted key + sig */
	unsigned char rxenckey[256]; /* Cache received encrypted key + sig */
	unsigned long us_keycrc32;	/* CRC-32 of our key */
	aes_encrypt_ctx	us_ecx;		/* Cached AES 128 Encryption context */
	aes_decrypt_ctx	us_dcx;		/* Cached AES 128 Decryption context */
	unsigned long them_keycrc32;/* CRC-32 of our key */
	aes_encrypt_ctx	them_ecx;	/* Cached AES 128 Encryption context */
	aes_decrypt_ctx	them_dcx;	/* Cached AES 128 Decryption context */
	time_t keyexpire;			/* When to expire/recreate key */
	int registerexpire;
	int lookuptimes[DUNDI_TIMING_HISTORY];
	char *lookups[DUNDI_TIMING_HISTORY];
	int avgms;
	struct dundi_transaction *regtrans;	/* Registration transaction */
	struct dundi_transaction *qualtrans;	/* Qualify transaction */
	struct dundi_transaction *keypending;
	int model;					/* Pull model */
	int pcmodel;				/* Push/precache model */
	int dynamic;				/* Are we dynamic? */
	int lastms;					/* Last measured latency */
	int maxms;					/* Max permissible latency */
	struct timeval qualtx;		/* Time of transmit */
	struct dundi_peer *next;
} *peers;

static struct dundi_precache_queue *pcq;

AST_MUTEX_DEFINE_STATIC(peerlock);
AST_MUTEX_DEFINE_STATIC(pclock);

static int dundi_xmit(struct dundi_packet *pack);

static void dundi_debug_output(const char *data)
{
	if (dundidebug)
		ast_verbose("%s", data);
}

static void dundi_error_output(const char *data)
{
	ast_log(LOG_WARNING, "%s", data);
}

static int has_permission(struct permission *ps, char *cont)
{
	int res=0;
	while(ps) {
		if (!strcasecmp(ps->name, "all") || !strcasecmp(ps->name, cont))
			res = ps->allow;
		ps = ps->next;
	}
	return res;
}

static char *tech2str(int tech)
{
	switch(tech) {
	case DUNDI_PROTO_NONE:
		return "None";
	case DUNDI_PROTO_IAX:
		return "IAX2";
	case DUNDI_PROTO_SIP:
		return "SIP";
	case DUNDI_PROTO_H323:
		return "H323";
	default:
		return "Unknown";
	}
}

static int str2tech(char *str)
{
	if (!strcasecmp(str, "IAX") || !strcasecmp(str, "IAX2")) 
		return DUNDI_PROTO_IAX;
	else if (!strcasecmp(str, "SIP"))
		return DUNDI_PROTO_SIP;
	else if (!strcasecmp(str, "H323"))
		return DUNDI_PROTO_H323;
	else
		return -1;
}

static int dundi_lookup_internal(struct dundi_result *result, int maxret, struct ast_channel *chan, const char *dcontext, const char *number, int ttl, int blockempty, struct dundi_hint_metadata *md, int *expiration, int cybpass, int modeselect, dundi_eid *skip, dundi_eid *avoid[], int direct[]);
static int dundi_precache_internal(const char *context, const char *number, int ttl, dundi_eid *avoids[]);
static struct dundi_transaction *create_transaction(struct dundi_peer *p);
static struct dundi_transaction *find_transaction(struct dundi_hdr *hdr, struct sockaddr_in *sin)
{
	/* Look for an exact match first */
	struct dundi_transaction *trans;
	trans = alltrans;
	while(trans) {
		if (!inaddrcmp(&trans->addr, sin) && 
		     ((trans->strans == (ntohs(hdr->dtrans) & 32767)) /* Matches our destination */ ||
			  ((trans->dtrans == (ntohs(hdr->strans) & 32767)) && (!hdr->dtrans))) /* We match their destination */) {
			  if (hdr->strans)
				  trans->dtrans = ntohs(hdr->strans) & 32767;
			  break;
		}
		trans = trans->allnext;
	}
	if (!trans) {
		switch(hdr->cmdresp & 0x7f) {
		case DUNDI_COMMAND_DPDISCOVER:
		case DUNDI_COMMAND_EIDQUERY:
		case DUNDI_COMMAND_PRECACHERQ:
		case DUNDI_COMMAND_REGREQ:
		case DUNDI_COMMAND_NULL:
		case DUNDI_COMMAND_ENCRYPT:
			if (hdr->strans) {	
				/* Create new transaction */
				trans = create_transaction(NULL);
				if (trans) {
					memcpy(&trans->addr, sin, sizeof(trans->addr));
					trans->dtrans = ntohs(hdr->strans) & 32767;
				} else
					ast_log(LOG_WARNING, "Out of memory!\n");
			}
			break;
		default:
			break;
		}
	}
	return trans;
}

static int dundi_send(struct dundi_transaction *trans, int cmdresp, int flags, int final, struct dundi_ie_data *ied);

static int dundi_ack(struct dundi_transaction *trans, int final)
{
	return dundi_send(trans, DUNDI_COMMAND_ACK, 0, final, NULL);
}
static void dundi_reject(struct dundi_hdr *h, struct sockaddr_in *sin)
{
	struct {
		struct dundi_packet pack;
		struct dundi_hdr hdr;
	} tmp;
	struct dundi_transaction trans;
	/* Never respond to an INVALID with another INVALID */
	if (h->cmdresp == DUNDI_COMMAND_INVALID)
		return;
	memset(&tmp, 0, sizeof(tmp));
	memset(&trans, 0, sizeof(trans));
	memcpy(&trans.addr, sin, sizeof(trans.addr));
	tmp.hdr.strans = h->dtrans;
	tmp.hdr.dtrans = h->strans;
	tmp.hdr.iseqno = h->oseqno;
	tmp.hdr.oseqno = h->iseqno;
	tmp.hdr.cmdresp = DUNDI_COMMAND_INVALID;
	tmp.hdr.cmdflags = 0;
	tmp.pack.h = (struct dundi_hdr *)tmp.pack.data;
	tmp.pack.datalen = sizeof(struct dundi_hdr);
	tmp.pack.parent = &trans;
	dundi_xmit(&tmp.pack);
}

static void reset_global_eid(void)
{
#if defined(SIOCGIFHWADDR)
	int x,s;
	char eid_str[20];
	struct ifreq ifr;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s > 0) {
		x = 0;
		for(x=0;x<10;x++) {
			memset(&ifr, 0, sizeof(ifr));
			snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "eth%d", x);
			if (!ioctl(s, SIOCGIFHWADDR, &ifr)) {
				memcpy(&global_eid, ((unsigned char *)&ifr.ifr_hwaddr) + 2, sizeof(global_eid));
				ast_log(LOG_DEBUG, "Seeding global EID '%s' from '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &global_eid), ifr.ifr_name);
				close(s);
				return;
			}
        }
		close(s);
	}
#else
#if defined(ifa_broadaddr) && !defined(SOLARIS)
	char eid_str[20];
	struct ifaddrs *ifap;
	
	if (getifaddrs(&ifap) == 0) {
		struct ifaddrs *p;
		for (p = ifap; p; p = p->ifa_next) {
			if (p->ifa_addr->sa_family == AF_LINK) {
				struct sockaddr_dl* sdp = (struct sockaddr_dl*) p->ifa_addr;
				memcpy(
					&(global_eid.eid),
					sdp->sdl_data + sdp->sdl_nlen, 6);
				ast_log(LOG_DEBUG, "Seeding global EID '%s' from '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &global_eid), ifap->ifa_name);
				freeifaddrs(ifap);
				return;
			}
		}
		freeifaddrs(ifap);
	}
#endif
#endif
	ast_log(LOG_NOTICE, "No ethernet interface found for seeding global EID  You will have to set it manually.\n");
}

static int get_trans_id(void)
{
	struct dundi_transaction *t;
	int stid = (rand() % 32766) + 1;
	int tid = stid;
	do {
		t = alltrans;
		while(t) {
			if (t->strans == tid) 
				break;
			t = t->allnext;
		}
		if (!t)
			return tid;
		tid = (tid % 32766) + 1;
	} while (tid != stid);
	return 0;
}

static int reset_transaction(struct dundi_transaction *trans)
{
	int tid;
	tid = get_trans_id();
	if (tid < 1)
		return -1;
	trans->strans = tid;
	trans->dtrans = 0;
	trans->iseqno = 0;
	trans->oiseqno = 0;
	trans->oseqno = 0;
	trans->aseqno = 0;
	ast_clear_flag(trans, FLAG_FINAL);	
	return 0;
}

static struct dundi_peer *find_peer(dundi_eid *eid)
{
	struct dundi_peer *cur;
	if (!eid)
		eid = &empty_eid;
	cur = peers;
	while(cur) {
		if (!dundi_eid_cmp(&cur->eid,eid))
			return cur;
		cur = cur->next;
	}
	return NULL;
}

static void build_iv(unsigned char *iv)
{
	/* XXX Would be nice to be more random XXX */
	unsigned int *fluffy;
	int x;
	fluffy = (unsigned int *)(iv);
	for (x=0;x<4;x++)
		fluffy[x] = rand();
}

struct dundi_query_state {
	dundi_eid *eids[DUNDI_MAX_STACK + 1]; 
	int directs[DUNDI_MAX_STACK + 1]; 
	dundi_eid reqeid;
	char called_context[AST_MAX_EXTENSION];
	char called_number[AST_MAX_EXTENSION];
	struct dundi_mapping *maps;
	int nummaps;
	int nocache;
	struct dundi_transaction *trans;
	void *chal;
	int challen;
	int ttl;
	char fluffy[0];
};

static int dundi_lookup_local(struct dundi_result *dr, struct dundi_mapping *map, char *called_number, dundi_eid *us_eid, int anscnt, struct dundi_hint_metadata *hmd)
{
	struct ast_flags flags = {0};
	int x;
	if (!ast_strlen_zero(map->lcontext)) {
		if (ast_exists_extension(NULL, map->lcontext, called_number, 1, NULL))
			ast_set_flag(&flags, DUNDI_FLAG_EXISTS);
		if (ast_canmatch_extension(NULL, map->lcontext, called_number, 1, NULL))
			ast_set_flag(&flags, DUNDI_FLAG_CANMATCH);
		if (ast_matchmore_extension(NULL, map->lcontext, called_number, 1, NULL))
			ast_set_flag(&flags, DUNDI_FLAG_MATCHMORE);
		if (ast_ignore_pattern(map->lcontext, called_number))
			ast_set_flag(&flags, DUNDI_FLAG_IGNOREPAT);

		/* Clearly we can't say 'don't ask' anymore if we found anything... */
		if (ast_test_flag(&flags, AST_FLAGS_ALL)) 
			ast_clear_flag_nonstd(hmd, DUNDI_HINT_DONT_ASK);

		if (map->options & DUNDI_FLAG_INTERNAL_NOPARTIAL) {
			/* Skip partial answers */
			ast_clear_flag(&flags, DUNDI_FLAG_MATCHMORE|DUNDI_FLAG_CANMATCH);
		}
		if (ast_test_flag(&flags, AST_FLAGS_ALL)) {
			struct varshead headp;
			struct ast_var_t *newvariable;
			ast_set_flag(&flags, map->options & 0xffff);
			ast_copy_flags(dr + anscnt, &flags, AST_FLAGS_ALL);
			dr[anscnt].techint = map->tech;
			dr[anscnt].weight = map->weight;
			dr[anscnt].expiration = DUNDI_DEFAULT_CACHE_TIME;
			strncpy(dr[anscnt].tech, tech2str(map->tech), sizeof(dr[anscnt].tech));
			dr[anscnt].eid = *us_eid;
			dundi_eid_to_str(dr[anscnt].eid_str, sizeof(dr[anscnt].eid_str), &dr[anscnt].eid);
			if (ast_test_flag(&flags, DUNDI_FLAG_EXISTS)) {
				AST_LIST_HEAD_INIT(&headp);
				newvariable = ast_var_assign("NUMBER", called_number);
				AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
				newvariable = ast_var_assign("EID", dr[anscnt].eid_str);
				AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
				newvariable = ast_var_assign("SECRET", cursecret);
				AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
				newvariable = ast_var_assign("IPADDR", ipaddr);
				AST_LIST_INSERT_HEAD(&headp, newvariable, entries);
				pbx_substitute_variables_varshead(&headp, map->dest, dr[anscnt].dest, sizeof(dr[anscnt].dest));
				while (!AST_LIST_EMPTY(&headp)) {           /* List Deletion. */
					newvariable = AST_LIST_REMOVE_HEAD(&headp, entries);
					ast_var_delete(newvariable);
				}
			} else
				dr[anscnt].dest[0] = '\0';
			anscnt++;
		} else {
			/* No answers...  Find the fewest number of digits from the
			   number for which we have no answer. */
			char tmp[AST_MAX_EXTENSION]="";
			for (x=0;x<AST_MAX_EXTENSION;x++) {
				tmp[x] = called_number[x];
				if (!tmp[x])
					break;
				if (!ast_canmatch_extension(NULL, map->lcontext, tmp, 1, NULL)) {
					/* Oops found something we can't match.  If this is longer
					   than the running hint, we have to consider it */
					if (strlen(tmp) > strlen(hmd->exten)) {
						strncpy(hmd->exten, tmp, sizeof(hmd->exten) - 1);
					}
					break;
				}
			}
		}
	}
	return anscnt;
}

static void destroy_trans(struct dundi_transaction *trans, int fromtimeout);

static void *dundi_lookup_thread(void *data)
{
	struct dundi_query_state *st = data;
	struct dundi_result dr[MAX_RESULTS];
	struct dundi_ie_data ied;
	struct dundi_hint_metadata hmd;
	char eid_str[20];
	int res, x;
	int ouranswers=0;
	int max = 999999;
	int expiration = DUNDI_DEFAULT_CACHE_TIME;

	ast_log(LOG_DEBUG, "Whee, looking up '%s@%s' for '%s'\n", st->called_number, st->called_context, 
		st->eids[0] ? dundi_eid_to_str(eid_str, sizeof(eid_str), st->eids[0]) :  "ourselves");
	memset(&ied, 0, sizeof(ied));
	memset(&dr, 0, sizeof(dr));
	memset(&hmd, 0, sizeof(hmd));
	/* Assume 'don't ask for anything' and 'unaffected', no TTL expired */
	hmd.flags = DUNDI_HINT_DONT_ASK | DUNDI_HINT_UNAFFECTED;
	for (x=0;x<st->nummaps;x++)
		ouranswers = dundi_lookup_local(dr, st->maps + x, st->called_number, &st->trans->us_eid, ouranswers, &hmd);
	if (ouranswers < 0)
		ouranswers = 0;
	for (x=0;x<ouranswers;x++) {
		if (dr[x].weight < max)
			max = dr[x].weight;
	}
		
	if (max) {
		/* If we do not have a canonical result, keep looking */
		res = dundi_lookup_internal(dr + ouranswers, MAX_RESULTS - ouranswers, NULL, st->called_context, st->called_number, st->ttl, 1, &hmd, &expiration, st->nocache, 0, NULL, st->eids, st->directs);
		if (res > 0) {
			/* Append answer in result */
			ouranswers += res;
		} else {
			if ((res < -1) && (!ouranswers))
				dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_DUPLICATE, "Duplicate Request Pending");
		}
	}
	ast_mutex_lock(&peerlock);
	/* Truncate if "don't ask" isn't present */
	if (!ast_test_flag_nonstd(&hmd, DUNDI_HINT_DONT_ASK))
		hmd.exten[0] = '\0';
	if (ast_test_flag(st->trans, FLAG_DEAD)) {
		ast_log(LOG_DEBUG, "Our transaction went away!\n");
		st->trans->thread = 0;
		destroy_trans(st->trans, 0);
	} else {
		for (x=0;x<ouranswers;x++) {
			/* Add answers */
			if (dr[x].expiration && (expiration > dr[x].expiration))
				expiration = dr[x].expiration;
			dundi_ie_append_answer(&ied, DUNDI_IE_ANSWER, &dr[x].eid, dr[x].techint, dr[x].flags, dr[x].weight, dr[x].dest);
		}
		dundi_ie_append_hint(&ied, DUNDI_IE_HINT, hmd.flags, hmd.exten);
		dundi_ie_append_short(&ied, DUNDI_IE_EXPIRATION, expiration);
		dundi_send(st->trans, DUNDI_COMMAND_DPRESPONSE, 0, 1, &ied);
		st->trans->thread = 0;
	}
	ast_mutex_unlock(&peerlock);
	free(st);
	return NULL;	
}

static void *dundi_precache_thread(void *data)
{
	struct dundi_query_state *st = data;
	struct dundi_ie_data ied;
	struct dundi_hint_metadata hmd;
	char eid_str[20];

	ast_log(LOG_DEBUG, "Whee, precaching '%s@%s' for '%s'\n", st->called_number, st->called_context, 
		st->eids[0] ? dundi_eid_to_str(eid_str, sizeof(eid_str), st->eids[0]) :  "ourselves");
	memset(&ied, 0, sizeof(ied));

	/* Now produce precache */
	dundi_precache_internal(st->called_context, st->called_number, st->ttl, st->eids);

	ast_mutex_lock(&peerlock);
	/* Truncate if "don't ask" isn't present */
	if (!ast_test_flag_nonstd(&hmd, DUNDI_HINT_DONT_ASK))
		hmd.exten[0] = '\0';
	if (ast_test_flag(st->trans, FLAG_DEAD)) {
		ast_log(LOG_DEBUG, "Our transaction went away!\n");
		st->trans->thread = 0;
		destroy_trans(st->trans, 0);
	} else {
		dundi_send(st->trans, DUNDI_COMMAND_PRECACHERP, 0, 1, &ied);
		st->trans->thread = 0;
	}
	ast_mutex_unlock(&peerlock);
	free(st);
	return NULL;	
}

static inline int calc_ms(struct timeval *start)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return ((tv.tv_sec - start->tv_sec) * 1000 + (tv.tv_usec - start->tv_usec) / 1000);
}

static int dundi_query_eid_internal(struct dundi_entity_info *dei, const char *dcontext, dundi_eid *eid, struct dundi_hint_metadata *hmd, int ttl, int blockempty, dundi_eid *avoid[]);

static void *dundi_query_thread(void *data)
{
	struct dundi_query_state *st = data;
	struct dundi_entity_info dei;
	struct dundi_ie_data ied;
	struct dundi_hint_metadata hmd;
	char eid_str[20];
	int res;
	ast_log(LOG_DEBUG, "Whee, looking up '%s@%s' for '%s'\n", st->called_number, st->called_context, 
		st->eids[0] ? dundi_eid_to_str(eid_str, sizeof(eid_str), st->eids[0]) :  "ourselves");
	memset(&ied, 0, sizeof(ied));
	memset(&dei, 0, sizeof(dei));
	memset(&hmd, 0, sizeof(hmd));
	if (!dundi_eid_cmp(&st->trans->us_eid, &st->reqeid)) {
		/* Ooh, it's us! */
		ast_log(LOG_DEBUG, "Neat, someone look for us!\n");
		strncpy(dei.orgunit, dept, sizeof(dei.orgunit));
		strncpy(dei.org, org, sizeof(dei.org));
		strncpy(dei.locality, locality, sizeof(dei.locality));
		strncpy(dei.stateprov, stateprov, sizeof(dei.stateprov));
		strncpy(dei.country, country, sizeof(dei.country));
		strncpy(dei.email, email, sizeof(dei.email));
		strncpy(dei.phone, phone, sizeof(dei.phone));
		res = 1;
	} else {
		/* If we do not have a canonical result, keep looking */
		res = dundi_query_eid_internal(&dei, st->called_context, &st->reqeid, &hmd, st->ttl, 1, st->eids);
	}
	ast_mutex_lock(&peerlock);
	if (ast_test_flag(st->trans, FLAG_DEAD)) {
		ast_log(LOG_DEBUG, "Our transaction went away!\n");
		st->trans->thread = 0;
		destroy_trans(st->trans, 0);
	} else {
		if (res) {
			dundi_ie_append_str(&ied, DUNDI_IE_DEPARTMENT, dei.orgunit);
			dundi_ie_append_str(&ied, DUNDI_IE_ORGANIZATION, dei.org);
			dundi_ie_append_str(&ied, DUNDI_IE_LOCALITY, dei.locality);
			dundi_ie_append_str(&ied, DUNDI_IE_STATE_PROV, dei.stateprov);
			dundi_ie_append_str(&ied, DUNDI_IE_COUNTRY, dei.country);
			dundi_ie_append_str(&ied, DUNDI_IE_EMAIL, dei.email);
			dundi_ie_append_str(&ied, DUNDI_IE_PHONE, dei.phone);
			if (!ast_strlen_zero(dei.ipaddr))
				dundi_ie_append_str(&ied, DUNDI_IE_IPADDR, dei.ipaddr);
		}
		dundi_ie_append_hint(&ied, DUNDI_IE_HINT, hmd.flags, hmd.exten);
		dundi_send(st->trans, DUNDI_COMMAND_EIDRESPONSE, 0, 1, &ied);
		st->trans->thread = 0;
	}
	ast_mutex_unlock(&peerlock);
	free(st);
	return NULL;	
}

static int dundi_answer_entity(struct dundi_transaction *trans, struct dundi_ies *ies, char *ccontext)
{
	struct dundi_query_state *st;
	int totallen;
	int x;
	int skipfirst=0;
	struct dundi_ie_data ied;
	char eid_str[20];
	char *s;
	pthread_t lookupthread;
	pthread_attr_t attr;
	if (ies->eidcount > 1) {
		/* Since it is a requirement that the first EID is the authenticating host
		   and the last EID is the root, it is permissible that the first and last EID
		   could be the same.  In that case, we should go ahead copy only the "root" section
		   since we will not need it for authentication. */
		if (!dundi_eid_cmp(ies->eids[0], ies->eids[ies->eidcount - 1]))
			skipfirst = 1;
	}
	totallen = sizeof(struct dundi_query_state);
	totallen += (ies->eidcount - skipfirst) * sizeof(dundi_eid);
	st = malloc(totallen);
	if (st) {
		memset(st, 0, totallen);
		strncpy(st->called_context, ies->called_context, sizeof(st->called_context) - 1);
		memcpy(&st->reqeid, ies->reqeid, sizeof(st->reqeid));
		st->trans = trans;
		st->ttl = ies->ttl - 1;
		if (st->ttl < 0)
			st->ttl = 0;
		s = st->fluffy;
		for (x=skipfirst;ies->eids[x];x++) {
			st->eids[x-skipfirst] = (dundi_eid *)s;
			*st->eids[x-skipfirst] = *ies->eids[x];
			s += sizeof(dundi_eid);
		}
		ast_log(LOG_DEBUG, "Answering EID query for '%s@%s'!\n", dundi_eid_to_str(eid_str, sizeof(eid_str), ies->reqeid), ies->called_context);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		trans->thread = 1;
		if (ast_pthread_create(&lookupthread, &attr, dundi_query_thread, st)) {
			trans->thread = 0;
			ast_log(LOG_WARNING, "Unable to create thread!\n");
			free(st);
			memset(&ied, 0, sizeof(ied));
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of threads");
			dundi_send(trans, DUNDI_COMMAND_EIDRESPONSE, 0, 1, &ied);
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "Out of memory!\n");
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of memory");
		dundi_send(trans, DUNDI_COMMAND_EIDRESPONSE, 0, 1, &ied);
		return -1;
	}
	return 0;
}

static int cache_save_hint(dundi_eid *eidpeer, struct dundi_request *req, struct dundi_hint *hint, int expiration)
{
	int unaffected;
	char key1[256];
	char key2[256];
	char eidpeer_str[20];
	char eidroot_str[20];
	char data[80]="";
	time_t timeout;

	if (expiration < 0)
		expiration = DUNDI_DEFAULT_CACHE_TIME;

	/* Only cache hint if "don't ask" is there... */
	if (!ast_test_flag_nonstd(hint, htons(DUNDI_HINT_DONT_ASK)))	
		return 0;

	unaffected = ast_test_flag_nonstd(hint, htons(DUNDI_HINT_UNAFFECTED));

	dundi_eid_to_str_short(eidpeer_str, sizeof(eidpeer_str), eidpeer);
	dundi_eid_to_str_short(eidroot_str, sizeof(eidroot_str), &req->root_eid);
	snprintf(key1, sizeof(key1), "hint/%s/%s/%s/e%08lx", eidpeer_str, hint->data, req->dcontext, unaffected ? 0 : req->crc32);
	snprintf(key2, sizeof(key2), "hint/%s/%s/%s/r%s", eidpeer_str, hint->data, req->dcontext, eidroot_str);

	time(&timeout);
	timeout += expiration;
	snprintf(data, sizeof(data), "%ld|", (long)(timeout));
	
	ast_db_put("dundi/cache", key1, data);
	ast_log(LOG_DEBUG, "Caching hint at '%s'\n", key1);
	ast_db_put("dundi/cache", key2, data);
	ast_log(LOG_DEBUG, "Caching hint at '%s'\n", key2);
	return 0;
}

static int cache_save(dundi_eid *eidpeer, struct dundi_request *req, int start, int unaffected, int expiration, int push)
{
	int x;
	char key1[256];
	char key2[256];
	char data[1024]="";
	char eidpeer_str[20];
	char eidroot_str[20];
	time_t timeout;

	if (expiration < 1)	
		expiration = DUNDI_DEFAULT_CACHE_TIME;

	/* Keep pushes a little longer, cut pulls a little short */
	if (push)
		expiration += 10;
	else
		expiration -= 10;
	if (expiration < 1)
		expiration = 1;
	dundi_eid_to_str_short(eidpeer_str, sizeof(eidpeer_str), eidpeer);
	dundi_eid_to_str_short(eidroot_str, sizeof(eidroot_str), &req->root_eid);
	snprintf(key1, sizeof(key1), "%s/%s/%s/e%08lx", eidpeer_str, req->number, req->dcontext, unaffected ? 0 : req->crc32);
	snprintf(key2, sizeof(key2), "%s/%s/%s/r%s", eidpeer_str, req->number, req->dcontext, eidroot_str);
	/* Build request string */
	time(&timeout);
	timeout += expiration;
	snprintf(data, sizeof(data), "%ld|", (long)(timeout));
	for (x=start;x<req->respcount;x++) {
		/* Skip anything with an illegal pipe in it */
		if (strchr(req->dr[x].dest, '|'))
			continue;
		snprintf(data + strlen(data), sizeof(data) - strlen(data), "%d/%d/%d/%s/%s|", 
			req->dr[x].flags, req->dr[x].weight, req->dr[x].techint, req->dr[x].dest, 
			dundi_eid_to_str_short(eidpeer_str, sizeof(eidpeer_str), &req->dr[x].eid));
	}
	ast_db_put("dundi/cache", key1, data);
	ast_db_put("dundi/cache", key2, data);
	return 0;
}

static int dundi_prop_precache(struct dundi_transaction *trans, struct dundi_ies *ies, char *ccontext)
{
	struct dundi_query_state *st;
	int totallen;
	int x,z;
	struct dundi_ie_data ied;
	char *s;
	struct dundi_result dr2[MAX_RESULTS];
	struct dundi_request dr;
	struct dundi_hint_metadata hmd;

	struct dundi_mapping *cur;
	int mapcount;
	int skipfirst = 0;
	
	pthread_t lookupthread;
	pthread_attr_t attr;

	memset(&dr2, 0, sizeof(dr2));
	memset(&dr, 0, sizeof(dr));
	memset(&hmd, 0, sizeof(hmd));
	
	/* Forge request structure to hold answers for cache */
	hmd.flags = DUNDI_HINT_DONT_ASK | DUNDI_HINT_UNAFFECTED;
	dr.dr = dr2;
	dr.maxcount = MAX_RESULTS;
	dr.expiration = DUNDI_DEFAULT_CACHE_TIME;
	dr.hmd = &hmd;
	dr.pfds[0] = dr.pfds[1] = -1;
	trans->parent = &dr;
	strncpy(dr.dcontext, ies->called_context ? ies->called_context : "e164", sizeof(dr.dcontext));
	strncpy(dr.number, ies->called_number, sizeof(dr.number) - 1);
	
	for (x=0;x<ies->anscount;x++) {
		if (trans->parent->respcount < trans->parent->maxcount) {
			/* Make sure it's not already there */
			for (z=0;z<trans->parent->respcount;z++) {
				if ((trans->parent->dr[z].techint == ies->answers[x]->protocol) &&
				    !strcmp(trans->parent->dr[z].dest, ies->answers[x]->data)) 
						break;
			}
			if (z == trans->parent->respcount) {
				/* Copy into parent responses */
				trans->parent->dr[trans->parent->respcount].flags = ntohs(ies->answers[x]->flags);
				trans->parent->dr[trans->parent->respcount].techint = ies->answers[x]->protocol;
				trans->parent->dr[trans->parent->respcount].weight = ntohs(ies->answers[x]->weight);
				trans->parent->dr[trans->parent->respcount].eid = ies->answers[x]->eid;
				if (ies->expiration > 0)
					trans->parent->dr[trans->parent->respcount].expiration = ies->expiration;
				else
					trans->parent->dr[trans->parent->respcount].expiration = DUNDI_DEFAULT_CACHE_TIME;
				dundi_eid_to_str(trans->parent->dr[trans->parent->respcount].eid_str, 
					sizeof(trans->parent->dr[trans->parent->respcount].eid_str),
					&ies->answers[x]->eid);
				strncpy(trans->parent->dr[trans->parent->respcount].dest, ies->answers[x]->data,
					sizeof(trans->parent->dr[trans->parent->respcount].dest));
					strncpy(trans->parent->dr[trans->parent->respcount].tech, tech2str(ies->answers[x]->protocol),
					sizeof(trans->parent->dr[trans->parent->respcount].tech));
				trans->parent->respcount++;
				ast_clear_flag_nonstd(trans->parent->hmd, DUNDI_HINT_DONT_ASK);	
			} else if (trans->parent->dr[z].weight > ies->answers[x]->weight) {
				/* Update weight if appropriate */
				trans->parent->dr[z].weight = ies->answers[x]->weight;
			}
		} else
			ast_log(LOG_NOTICE, "Dropping excessive answers in precache for %s@%s\n",
				trans->parent->number, trans->parent->dcontext);

	}
	/* Save all the results (if any) we had.  Even if no results, still cache lookup. */
	cache_save(&trans->them_eid, trans->parent, 0, 0, ies->expiration, 1);
	if (ies->hint)
		cache_save_hint(&trans->them_eid, trans->parent, ies->hint, ies->expiration);

	totallen = sizeof(struct dundi_query_state);
	/* Count matching map entries */
	mapcount = 0;
	cur = mappings;
	while(cur) {
		if (!strcasecmp(cur->dcontext, ccontext))
			mapcount++;
		cur = cur->next;
	}
	
	/* If no maps, return -1 immediately */
	if (!mapcount)
		return -1;

	if (ies->eidcount > 1) {
		/* Since it is a requirement that the first EID is the authenticating host
		   and the last EID is the root, it is permissible that the first and last EID
		   could be the same.  In that case, we should go ahead copy only the "root" section
		   since we will not need it for authentication. */
		if (!dundi_eid_cmp(ies->eids[0], ies->eids[ies->eidcount - 1]))
			skipfirst = 1;
	}

	/* Prepare to run a query and then propagate that as necessary */
	totallen += mapcount * sizeof(struct dundi_mapping);
	totallen += (ies->eidcount - skipfirst) * sizeof(dundi_eid);
	st = malloc(totallen);
	if (st) {
		memset(st, 0, totallen);
		strncpy(st->called_context, ies->called_context, sizeof(st->called_context) - 1);
		strncpy(st->called_number, ies->called_number, sizeof(st->called_number) - 1);
		st->trans = trans;
		st->ttl = ies->ttl - 1;
		st->nocache = ies->cbypass;
		if (st->ttl < 0)
			st->ttl = 0;
		s = st->fluffy;
		for (x=skipfirst;ies->eids[x];x++) {
			st->eids[x-skipfirst] = (dundi_eid *)s;
			*st->eids[x-skipfirst] = *ies->eids[x];
			st->directs[x-skipfirst] = ies->eid_direct[x];
			s += sizeof(dundi_eid);
		}
		/* Append mappings */
		x = 0;
		st->maps = (struct dundi_mapping *)s;
		cur = mappings;
		while(cur) {
			if (!strcasecmp(cur->dcontext, ccontext)) {
				if (x < mapcount) {
					st->maps[x] = *cur;
					st->maps[x].next = NULL;
					x++;
				}
			}
			cur = cur->next;
		}
		st->nummaps = mapcount;
		ast_log(LOG_DEBUG, "Forwarding precache for '%s@%s'!\n", ies->called_number, ies->called_context);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		trans->thread = 1;
		if (ast_pthread_create(&lookupthread, &attr, dundi_precache_thread, st)) {
			trans->thread = 0;
			ast_log(LOG_WARNING, "Unable to create thread!\n");
			free(st);
			memset(&ied, 0, sizeof(ied));
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of threads");
			dundi_send(trans, DUNDI_COMMAND_PRECACHERP, 0, 1, &ied);
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "Out of memory!\n");
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of memory");
		dundi_send(trans, DUNDI_COMMAND_PRECACHERP, 0, 1, &ied);
		return -1;
	}
	return 0;
}

static int dundi_answer_query(struct dundi_transaction *trans, struct dundi_ies *ies, char *ccontext)
{
	struct dundi_query_state *st;
	int totallen;
	int x;
	struct dundi_ie_data ied;
	char *s;
	struct dundi_mapping *cur;
	int mapcount;
	int skipfirst = 0;
	
	pthread_t lookupthread;
	pthread_attr_t attr;
	totallen = sizeof(struct dundi_query_state);
	/* Count matching map entries */
	mapcount = 0;
	cur = mappings;
	while(cur) {
		if (!strcasecmp(cur->dcontext, ccontext))
			mapcount++;
		cur = cur->next;
	}
	/* If no maps, return -1 immediately */
	if (!mapcount)
		return -1;

	if (ies->eidcount > 1) {
		/* Since it is a requirement that the first EID is the authenticating host
		   and the last EID is the root, it is permissible that the first and last EID
		   could be the same.  In that case, we should go ahead copy only the "root" section
		   since we will not need it for authentication. */
		if (!dundi_eid_cmp(ies->eids[0], ies->eids[ies->eidcount - 1]))
			skipfirst = 1;
	}

	totallen += mapcount * sizeof(struct dundi_mapping);
	totallen += (ies->eidcount - skipfirst) * sizeof(dundi_eid);
	st = malloc(totallen);
	if (st) {
		memset(st, 0, totallen);
		strncpy(st->called_context, ies->called_context, sizeof(st->called_context) - 1);
		strncpy(st->called_number, ies->called_number, sizeof(st->called_number) - 1);
		st->trans = trans;
		st->ttl = ies->ttl - 1;
		st->nocache = ies->cbypass;
		if (st->ttl < 0)
			st->ttl = 0;
		s = st->fluffy;
		for (x=skipfirst;ies->eids[x];x++) {
			st->eids[x-skipfirst] = (dundi_eid *)s;
			*st->eids[x-skipfirst] = *ies->eids[x];
			st->directs[x-skipfirst] = ies->eid_direct[x];
			s += sizeof(dundi_eid);
		}
		/* Append mappings */
		x = 0;
		st->maps = (struct dundi_mapping *)s;
		cur = mappings;
		while(cur) {
			if (!strcasecmp(cur->dcontext, ccontext)) {
				if (x < mapcount) {
					st->maps[x] = *cur;
					st->maps[x].next = NULL;
					x++;
				}
			}
			cur = cur->next;
		}
		st->nummaps = mapcount;
		ast_log(LOG_DEBUG, "Answering query for '%s@%s'!\n", ies->called_number, ies->called_context);
		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		trans->thread = 1;
		if (ast_pthread_create(&lookupthread, &attr, dundi_lookup_thread, st)) {
			trans->thread = 0;
			ast_log(LOG_WARNING, "Unable to create thread!\n");
			free(st);
			memset(&ied, 0, sizeof(ied));
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of threads");
			dundi_send(trans, DUNDI_COMMAND_DPRESPONSE, 0, 1, &ied);
			return -1;
		}
	} else {
		ast_log(LOG_WARNING, "Out of memory!\n");
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Out of memory");
		dundi_send(trans, DUNDI_COMMAND_DPRESPONSE, 0, 1, &ied);
		return -1;
	}
	return 0;
}

static int cache_lookup_internal(time_t now, struct dundi_request *req, char *key, char *eid_str_full, int *lowexpiration)
{
	char data[1024]="";
	char *ptr, *term, *src;
	int tech;
	struct ast_flags flags;
	int weight;
	int length;
	int z;
	int expiration;
	char fs[256];
	time_t timeout;
	/* Build request string */
	if (!ast_db_get("dundi/cache", key, data, sizeof(data))) {
		ptr = data;
		if (sscanf(ptr, "%ld|%n", &timeout, &length) == 1) {
			expiration = timeout - now;
			if (expiration > 0) {
				ast_log(LOG_DEBUG, "Found cache expiring in %d seconds!\n", (int)(timeout - now));
				ptr += length;
				while((sscanf(ptr, "%d/%d/%d/%n", &(flags.flags), &weight, &tech, &length) == 3)) {
					ptr += length;
					term = strchr(ptr, '|');
					if (term) {
						*term = '\0';
						src = strrchr(ptr, '/');
						if (src) {
							*src = '\0';
							src++;
						} else
							src = "";
						ast_log(LOG_DEBUG, "Found cached answer '%s/%s' originally from '%s' with flags '%s' on behalf of '%s'\n", 
							tech2str(tech), ptr, src, dundi_flags2str(fs, sizeof(fs), flags.flags), eid_str_full);
						/* Make sure it's not already there */
						for (z=0;z<req->respcount;z++) {
							if ((req->dr[z].techint == tech) &&
							    !strcmp(req->dr[z].dest, ptr)) 
									break;
						}
						if (z == req->respcount) {
							/* Copy into parent responses */
							ast_copy_flags(&(req->dr[req->respcount]), &flags, AST_FLAGS_ALL);	
							req->dr[req->respcount].weight = weight;
							req->dr[req->respcount].techint = tech;
							req->dr[req->respcount].expiration = expiration;
							dundi_str_short_to_eid(&req->dr[req->respcount].eid, src);
							dundi_eid_to_str(req->dr[req->respcount].eid_str, 
								sizeof(req->dr[req->respcount].eid_str), &req->dr[req->respcount].eid);
							strncpy(req->dr[req->respcount].dest, ptr,
								sizeof(req->dr[req->respcount].dest));
							strncpy(req->dr[req->respcount].tech, tech2str(tech),
								sizeof(req->dr[req->respcount].tech));
							req->respcount++;
							ast_clear_flag_nonstd(req->hmd, DUNDI_HINT_DONT_ASK);	
						} else if (req->dr[z].weight > weight)
							req->dr[z].weight = weight;
						ptr = term + 1;
					}
				}
				/* We found *something* cached */
				if (expiration < *lowexpiration)
					*lowexpiration = expiration;
				return 1;
			} else 
				ast_db_del("dundi/cache", key);
		} else 
			ast_db_del("dundi/cache", key);
	}
		
	return 0;
}

static int cache_lookup(struct dundi_request *req, dundi_eid *peer_eid, unsigned long crc32, int *lowexpiration)
{
	char key[256];
	char eid_str[20];
	char eidroot_str[20];
	time_t now;
	int res=0;
	int res2=0;
	char eid_str_full[20];
	char tmp[256]="";
	int x;

	time(&now);
	dundi_eid_to_str_short(eid_str, sizeof(eid_str), peer_eid);
	dundi_eid_to_str_short(eidroot_str, sizeof(eidroot_str), &req->root_eid);
	dundi_eid_to_str(eid_str_full, sizeof(eid_str_full), peer_eid);
	snprintf(key, sizeof(key), "%s/%s/%s/e%08lx", eid_str, req->number, req->dcontext, crc32);
	res |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
	snprintf(key, sizeof(key), "%s/%s/%s/e%08lx", eid_str, req->number, req->dcontext, 0L);
	res |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
	snprintf(key, sizeof(key), "%s/%s/%s/r%s", eid_str, req->number, req->dcontext, eidroot_str);
	res |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
	x = 0;
	if (!req->respcount) {
		while(!res2) {
			/* Look and see if we have a hint that would preclude us from looking at this
			   peer for this number. */
			if (!(tmp[x] = req->number[x])) 
				break;
			x++;
			/* Check for hints */
			snprintf(key, sizeof(key), "hint/%s/%s/%s/e%08lx", eid_str, tmp, req->dcontext, crc32);
			res2 |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
			snprintf(key, sizeof(key), "hint/%s/%s/%s/e%08lx", eid_str, tmp, req->dcontext, 0L);
			res2 |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
			snprintf(key, sizeof(key), "hint/%s/%s/%s/r%s", eid_str, tmp, req->dcontext, eidroot_str);
			res2 |= cache_lookup_internal(now, req, key, eid_str_full, lowexpiration);
			if (res2) {
				if (strlen(tmp) > strlen(req->hmd->exten)) {
					/* Update meta data if appropriate */
					strncpy(req->hmd->exten, tmp, sizeof(req->hmd->exten) - 1);
				}
			}
		}
		res |= res2;
	}

	return res;
}

static void qualify_peer(struct dundi_peer *peer, int schedonly);

static void apply_peer(struct dundi_transaction *trans, struct dundi_peer *p)
{
	if (!trans->addr.sin_addr.s_addr)
		memcpy(&trans->addr, &p->addr, sizeof(trans->addr));
	trans->us_eid = p->us_eid;
	trans->them_eid = p->eid;
	/* Enable encryption if appropriate */
	if (!ast_strlen_zero(p->inkey))
		ast_set_flag(trans, FLAG_ENCRYPT);	
	if (p->maxms) {
		trans->autokilltimeout = p->maxms;
		trans->retranstimer = DUNDI_DEFAULT_RETRANS_TIMER;
		if (p->lastms > 1) {
			trans->retranstimer = p->lastms * 2;
			/* Keep it from being silly */
			if (trans->retranstimer < 150)
				trans->retranstimer = 150;
		}
		if (trans->retranstimer > DUNDI_DEFAULT_RETRANS_TIMER)
			trans->retranstimer = DUNDI_DEFAULT_RETRANS_TIMER;
	} else
		trans->autokilltimeout = global_autokilltimeout;
}

static int do_register_expire(void *data)
{
	struct dundi_peer *peer = data;
	char eid_str[20];
	/* Called with peerlock already held */
	ast_log(LOG_DEBUG, "Register expired for '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
	peer->registerexpire = -1;
	peer->lastms = 0;
	memset(&peer->addr, 0, sizeof(peer->addr));
	return 0;
}

static int update_key(struct dundi_peer *peer)
{
	unsigned char key[16];
	struct ast_key *ekey, *skey;
	char eid_str[20];
	int res;
	if (!peer->keyexpire || (peer->keyexpire < time(NULL))) {
		build_iv(key);
		aes_encrypt_key128(key, &peer->us_ecx);
		aes_decrypt_key128(key, &peer->us_dcx);
		ekey = ast_key_get(peer->inkey, AST_KEY_PUBLIC);
		if (!ekey) {
			ast_log(LOG_NOTICE, "No such key '%s' for creating RSA encrypted shared key for '%s'!\n",
				peer->inkey, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			return -1;
		}
		skey = ast_key_get(peer->outkey, AST_KEY_PRIVATE);
		if (!skey) {
			ast_log(LOG_NOTICE, "No such key '%s' for signing RSA encrypted shared key for '%s'!\n",
				peer->outkey, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			return -1;
		}
		if ((res = ast_encrypt_bin(peer->txenckey, key, sizeof(key), ekey)) != 128) {
			ast_log(LOG_NOTICE, "Whoa, got a weird encrypt size (%d != %d)!\n", res, 128);
			return -1;
		}
		if ((res = ast_sign_bin(skey, peer->txenckey, 128, peer->txenckey + 128))) {
			ast_log(LOG_NOTICE, "Failed to sign key (%d)!\n", res);
			return -1;
		}
		peer->us_keycrc32 = crc32(0L, peer->txenckey, 128);
		peer->sentfullkey = 0;
		/* Looks good */
		time(&peer->keyexpire);
		peer->keyexpire += dundi_key_ttl;
	}
	return 0;
}

static int encrypt_memcpy(unsigned char *dst, unsigned char *src, int len, unsigned char *iv, aes_encrypt_ctx *ecx) 
{
	unsigned char curblock[16];
	int x;
	memcpy(curblock, iv, sizeof(curblock));
	while(len > 0) {
		for (x=0;x<16;x++)
			curblock[x] ^= src[x];
		aes_encrypt(curblock, dst, ecx);
		memcpy(curblock, dst, sizeof(curblock)); 
		dst += 16;
		src += 16;
		len -= 16;
	}
	return 0;
}
static int decrypt_memcpy(unsigned char *dst, unsigned char *src, int len, unsigned char *iv, aes_decrypt_ctx *dcx) 
{
	unsigned char lastblock[16];
	int x;
	memcpy(lastblock, iv, sizeof(lastblock));
	while(len > 0) {
		aes_decrypt(src, dst, dcx);
		for (x=0;x<16;x++)
			dst[x] ^= lastblock[x];
		memcpy(lastblock, src, sizeof(lastblock));
		dst += 16;
		src += 16;
		len -= 16;
	}
	return 0;
}

static struct dundi_hdr *dundi_decrypt(struct dundi_transaction *trans, unsigned char *dst, int *dstlen, struct dundi_hdr *ohdr, struct dundi_encblock *src, int srclen)
{
	int space = *dstlen;
	unsigned long bytes;
	struct dundi_hdr *h;
	unsigned char *decrypt_space;
	decrypt_space = alloca(srclen);
	if (!decrypt_space)
		return NULL;
	decrypt_memcpy(decrypt_space, src->encdata, srclen, src->iv, &trans->dcx);
	/* Setup header */
	h = (struct dundi_hdr *)dst;
	*h = *ohdr;
	bytes = space - 6;
	if (uncompress(dst + 6, &bytes, decrypt_space, srclen) != Z_OK) {
		ast_log(LOG_DEBUG, "Ouch, uncompress failed :(\n");
		return NULL;
	}
	/* Update length */
	*dstlen = bytes + 6;
	/* Return new header */
	return h;
}

static int dundi_encrypt(struct dundi_transaction *trans, struct dundi_packet *pack)
{
	unsigned char *compress_space;
	int len;
	int res;
	unsigned long bytes;
	struct dundi_ie_data ied;
	struct dundi_peer *peer;
	unsigned char iv[16];
	len = pack->datalen + pack->datalen / 100 + 42;
	compress_space = alloca(len);
	if (compress_space) {
		memset(compress_space, 0, len);
		/* We care about everthing save the first 6 bytes of header */
		bytes = len;
		res = compress(compress_space, &bytes, pack->data + 6, pack->datalen - 6);
		if (res != Z_OK) {
			ast_log(LOG_DEBUG, "Ouch, compression failed!\n");
			return -1;
		}
		memset(&ied, 0, sizeof(ied));
		/* Say who we are */
		if (!pack->h->iseqno && !pack->h->oseqno) {
			/* Need the key in the first copy */
			if (!(peer = find_peer(&trans->them_eid))) 
				return -1;
			if (update_key(peer))
				return -1;
			if (!peer->sentfullkey)
				ast_set_flag(trans, FLAG_SENDFULLKEY);	
			/* Append key data */
			dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->us_eid);
			if (ast_test_flag(trans, FLAG_SENDFULLKEY)) {
				dundi_ie_append_raw(&ied, DUNDI_IE_SHAREDKEY, peer->txenckey, 128);
				dundi_ie_append_raw(&ied, DUNDI_IE_SIGNATURE, peer->txenckey + 128, 128);
			} else {
				dundi_ie_append_int(&ied, DUNDI_IE_KEYCRC32, peer->us_keycrc32);
			}
			/* Setup contexts */
			trans->ecx = peer->us_ecx;
			trans->dcx = peer->us_dcx;

			/* We've sent the full key */
			peer->sentfullkey = 1;
		}
		/* Build initialization vector */
		build_iv(iv);
		/* Add the field, rounded up to 16 bytes */
		dundi_ie_append_encdata(&ied, DUNDI_IE_ENCDATA, iv, NULL, ((bytes + 15) / 16) * 16);
		/* Copy the data */
		if ((ied.pos + bytes) >= sizeof(ied.buf)) {
			ast_log(LOG_NOTICE, "Final packet too large!\n");
			return -1;
		}
		encrypt_memcpy(ied.buf + ied.pos, compress_space, bytes, iv, &trans->ecx);
		ied.pos += ((bytes + 15) / 16) * 16;
		/* Reconstruct header */
		pack->datalen = sizeof(struct dundi_hdr);
		pack->h->cmdresp = DUNDI_COMMAND_ENCRYPT;
		pack->h->cmdflags = 0;
		memcpy(pack->h->ies, ied.buf, ied.pos);
		pack->datalen += ied.pos;
		return 0;
	}
	return -1;
}

static int check_key(struct dundi_peer *peer, unsigned char *newkey, unsigned char *newsig, unsigned long keycrc32)
{
	unsigned char dst[128];
	int res;
	struct ast_key *key, *skey;
	char eid_str[20];
	if (option_debug)
		ast_log(LOG_DEBUG, "Expected '%08lx' got '%08lx'\n", peer->them_keycrc32, keycrc32);
	if (peer->them_keycrc32 && (peer->them_keycrc32 == keycrc32)) {
		/* A match */
		return 1;
	} else if (!newkey || !newsig)
		return 0;
	if (!memcmp(peer->rxenckey, newkey, 128) &&
	    !memcmp(peer->rxenckey + 128, newsig, 128)) {
		/* By definition, a match */
		return 1;
	}
	/* Decrypt key */
	key = ast_key_get(peer->outkey, AST_KEY_PRIVATE);
	if (!key) {
		ast_log(LOG_NOTICE, "Unable to find key '%s' to decode shared key from '%s'\n",
			peer->outkey, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
		return -1;
	}

	skey = ast_key_get(peer->inkey, AST_KEY_PUBLIC);
	if (!skey) {
		ast_log(LOG_NOTICE, "Unable to find key '%s' to verify shared key from '%s'\n",
			peer->inkey, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
		return -1;
	}

	/* First check signature */
	res = ast_check_signature_bin(skey, newkey, 128, newsig);
	if (res) 
		return 0;

	res = ast_decrypt_bin(dst, newkey, sizeof(dst), key);
	if (res != 16) {
		if (res >= 0)
			ast_log(LOG_NOTICE, "Weird, key decoded to the wrong size (%d)\n", res);
		return 0;
	}
	/* Decrypted, passes signature */
	ast_log(LOG_DEBUG, "Wow, new key combo passed signature and decrypt!\n");
	memcpy(peer->rxenckey, newkey, 128);
	memcpy(peer->rxenckey + 128, newsig, 128);
	peer->them_keycrc32 = crc32(0L, peer->rxenckey, 128);
	aes_decrypt_key128(dst, &peer->them_dcx);
	aes_encrypt_key128(dst, &peer->them_ecx);
	return 1;
}

static int handle_command_response(struct dundi_transaction *trans, struct dundi_hdr *hdr, int datalen, int encrypted)
{
	/* Handle canonical command / response */
	int final = hdr->cmdresp & 0x80;
	int cmd = hdr->cmdresp & 0x7f;
	int x,y,z;
	int resp;
	int res;
	int authpass=0;
	unsigned char *bufcpy;
	struct dundi_ie_data ied;
	struct dundi_ies ies;
	struct dundi_peer *peer;
	char eid_str[20];
	char eid_str2[20];
	memset(&ied, 0, sizeof(ied));
	memset(&ies, 0, sizeof(ies));
	if (datalen) {
		bufcpy = alloca(datalen);
		if (!bufcpy)
			return -1;
		/* Make a copy for parsing */
		memcpy(bufcpy, hdr->ies, datalen);
		ast_log(LOG_DEBUG, "Got canonical message %d (%d), %d bytes data%s\n", cmd, hdr->oseqno, datalen, final ? " (Final)" : "");
		if (dundi_parse_ies(&ies, bufcpy, datalen) < 0) {
			ast_log(LOG_WARNING, "Failed to parse DUNDI information elements!\n");
			return -1;
		}
	}
	switch(cmd) {
	case DUNDI_COMMAND_DPDISCOVER:
	case DUNDI_COMMAND_EIDQUERY:
	case DUNDI_COMMAND_PRECACHERQ:
		if (cmd == DUNDI_COMMAND_EIDQUERY)
			resp = DUNDI_COMMAND_EIDRESPONSE;
		else if (cmd == DUNDI_COMMAND_PRECACHERQ)
			resp = DUNDI_COMMAND_PRECACHERP;
		else
			resp = DUNDI_COMMAND_DPRESPONSE;
		/* A dialplan or entity discover -- qualify by highest level entity */
		peer = find_peer(ies.eids[0]);
		if (!peer) {
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, NULL);
			dundi_send(trans, resp, 0, 1, &ied);
		} else {
			int hasauth = 0;
			trans->us_eid = peer->us_eid;
			if (strlen(peer->inkey)) {
				hasauth = encrypted;
			} else 
				hasauth = 1;
			if (hasauth) {
				/* Okay we're authentiated and all, now we check if they're authorized */
				if (!ies.called_context)
					ies.called_context = "e164";
				if (cmd == DUNDI_COMMAND_EIDQUERY) {
					res = dundi_answer_entity(trans, &ies, ies.called_context);
				} else {
					if (!ies.called_number || ast_strlen_zero(ies.called_number)) {
						/* They're not permitted to access that context */
						dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_GENERAL, "Invalid or missing number/entity");
						dundi_send(trans, resp, 0, 1, &ied);
					} else if ((cmd == DUNDI_COMMAND_DPDISCOVER) && 
					           (peer->model & DUNDI_MODEL_INBOUND) && 
							   has_permission(peer->permit, ies.called_context)) {
						res = dundi_answer_query(trans, &ies, ies.called_context);
						if (res < 0) {
							/* There is no such dundi context */
							dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Unsupported DUNDI Context");
							dundi_send(trans, resp, 0, 1, &ied);
						}
					} else if ((cmd = DUNDI_COMMAND_PRECACHERQ) && 
					           (peer->pcmodel & DUNDI_MODEL_INBOUND) && 
							   has_permission(peer->include, ies.called_context)) {
						res = dundi_prop_precache(trans, &ies, ies.called_context);
						if (res < 0) {
							/* There is no such dundi context */
							dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Unsupported DUNDI Context");
							dundi_send(trans, resp, 0, 1, &ied);
						}
					} else {
						/* They're not permitted to access that context */
						dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Permission to context denied");
						dundi_send(trans, resp, 0, 1, &ied);
					}
				}
			} else {
				/* They're not permitted to access that context */
				dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Unencrypted responses not permitted");
				dundi_send(trans, resp, 0, 1, &ied);
			}
		}
		break;
	case DUNDI_COMMAND_REGREQ:
		/* A register request -- should only have one entity */
		peer = find_peer(ies.eids[0]);
		if (!peer || !peer->dynamic) {
			dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, NULL);
			dundi_send(trans, DUNDI_COMMAND_REGRESPONSE, 0, 1, &ied);
		} else {
			int hasauth = 0;
			trans->us_eid = peer->us_eid;
			if (!ast_strlen_zero(peer->inkey)) {
				hasauth = encrypted;
			} else
				hasauth = 1;
			if (hasauth) {
				int expire = default_expiration;
				char iabuf[INET_ADDRSTRLEN];
				char data[256];
				int needqual = 0;
				if (peer->registerexpire > -1)
					ast_sched_del(sched, peer->registerexpire);
				peer->registerexpire = ast_sched_add(sched, (expire + 10) * 1000, do_register_expire, peer);
				ast_inet_ntoa(iabuf, sizeof(iabuf), trans->addr.sin_addr);
				snprintf(data, sizeof(data), "%s:%d:%d", iabuf, ntohs(trans->addr.sin_port), expire);
				ast_db_put("dundi/dpeers", dundi_eid_to_str_short(eid_str, sizeof(eid_str), &peer->eid), data);
				if (inaddrcmp(&peer->addr, &trans->addr)) {
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Registered DUNDi peer '%s' at '%s:%d'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), iabuf, ntohs(trans->addr.sin_port));
					needqual = 1;
				}
					
				memcpy(&peer->addr, &trans->addr, sizeof(peer->addr));
				dundi_ie_append_short(&ied, DUNDI_IE_EXPIRATION, default_expiration);
				dundi_send(trans, DUNDI_COMMAND_REGRESPONSE, 0, 1, &ied);
				if (needqual)
					qualify_peer(peer, 1);
			}
		}
		break;
	case DUNDI_COMMAND_DPRESPONSE:
		/* A dialplan response, lets see what we got... */
		if (ies.cause < 1) {
			/* Success of some sort */
			ast_log(LOG_DEBUG, "Looks like success of some sort (%d), %d answers\n", ies.cause, ies.anscount);
			if (ast_test_flag(trans, FLAG_ENCRYPT)) {
				authpass = encrypted;
			} else 
				authpass = 1;
			if (authpass) {
				/* Pass back up answers */
				if (trans->parent && trans->parent->dr) {
					y = trans->parent->respcount;
					for (x=0;x<ies.anscount;x++) {
						if (trans->parent->respcount < trans->parent->maxcount) {
							/* Make sure it's not already there */
							for (z=0;z<trans->parent->respcount;z++) {
								if ((trans->parent->dr[z].techint == ies.answers[x]->protocol) &&
								    !strcmp(trans->parent->dr[z].dest, ies.answers[x]->data)) 
										break;
							}
							if (z == trans->parent->respcount) {
								/* Copy into parent responses */
								trans->parent->dr[trans->parent->respcount].flags = ntohs(ies.answers[x]->flags);
								trans->parent->dr[trans->parent->respcount].techint = ies.answers[x]->protocol;
								trans->parent->dr[trans->parent->respcount].weight = ntohs(ies.answers[x]->weight);
								trans->parent->dr[trans->parent->respcount].eid = ies.answers[x]->eid;
								if (ies.expiration > 0)
									trans->parent->dr[trans->parent->respcount].expiration = ies.expiration;
								else
									trans->parent->dr[trans->parent->respcount].expiration = DUNDI_DEFAULT_CACHE_TIME;
								dundi_eid_to_str(trans->parent->dr[trans->parent->respcount].eid_str, 
									sizeof(trans->parent->dr[trans->parent->respcount].eid_str),
									&ies.answers[x]->eid);
								strncpy(trans->parent->dr[trans->parent->respcount].dest, ies.answers[x]->data,
									sizeof(trans->parent->dr[trans->parent->respcount].dest));
								strncpy(trans->parent->dr[trans->parent->respcount].tech, tech2str(ies.answers[x]->protocol),
									sizeof(trans->parent->dr[trans->parent->respcount].tech));
								trans->parent->respcount++;
								ast_clear_flag_nonstd(trans->parent->hmd, DUNDI_HINT_DONT_ASK);
							} else if (trans->parent->dr[z].weight > ies.answers[x]->weight) {
								/* Update weight if appropriate */
								trans->parent->dr[z].weight = ies.answers[x]->weight;
							}
						} else
							ast_log(LOG_NOTICE, "Dropping excessive answers to request for %s@%s\n",
								trans->parent->number, trans->parent->dcontext);
					}
					/* Save all the results (if any) we had.  Even if no results, still cache lookup.  Let
					   the cache know if this request was unaffected by our entity list. */
					cache_save(&trans->them_eid, trans->parent, y, 
							ies.hint ? ast_test_flag_nonstd(ies.hint, htons(DUNDI_HINT_UNAFFECTED)) : 0, ies.expiration, 0);
					if (ies.hint) {
						cache_save_hint(&trans->them_eid, trans->parent, ies.hint, ies.expiration);
						if (ast_test_flag_nonstd(ies.hint, htons(DUNDI_HINT_TTL_EXPIRED)))
							ast_set_flag_nonstd(trans->parent->hmd, DUNDI_HINT_TTL_EXPIRED);
						if (ast_test_flag_nonstd(ies.hint, htons(DUNDI_HINT_DONT_ASK))) { 
							if (strlen(ies.hint->data) > strlen(trans->parent->hmd->exten)) {
								strncpy(trans->parent->hmd->exten, ies.hint->data, 
									sizeof(trans->parent->hmd->exten) - 1);
							}
						} else {
							ast_clear_flag_nonstd(trans->parent->hmd, DUNDI_HINT_DONT_ASK);
						}
					}
					if (ies.expiration > 0) {
						if (trans->parent->expiration > ies.expiration) {
							trans->parent->expiration = ies.expiration;
						}
					}
				}
				/* Close connection if not final */
				if (!final) 
					dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
			
		} else {
			/* Auth failure, check for data */
			if (!final) {
				/* Cancel if they didn't already */
				dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
		}
		break;
	case DUNDI_COMMAND_EIDRESPONSE:
		/* A dialplan response, lets see what we got... */
		if (ies.cause < 1) {
			/* Success of some sort */
			ast_log(LOG_DEBUG, "Looks like success of some sort (%d)\n", ies.cause);
			if (ast_test_flag(trans, FLAG_ENCRYPT)) {
				authpass = encrypted;
			} else 
				authpass = 1;
			if (authpass) {
				/* Pass back up answers */
				if (trans->parent && trans->parent->dei && ies.q_org) {
					if (!trans->parent->respcount) {
						trans->parent->respcount++;
						if (ies.q_dept)
							strncpy(trans->parent->dei->orgunit, ies.q_dept, sizeof(trans->parent->dei->orgunit) - 1);
						if (ies.q_org)
							strncpy(trans->parent->dei->org, ies.q_org, sizeof(trans->parent->dei->org) - 1);
						if (ies.q_locality)
							strncpy(trans->parent->dei->locality, ies.q_locality, sizeof(trans->parent->dei->locality) - 1);
						if (ies.q_stateprov)
							strncpy(trans->parent->dei->stateprov, ies.q_stateprov, sizeof(trans->parent->dei->stateprov) - 1);
						if (ies.q_country)
							strncpy(trans->parent->dei->country, ies.q_country, sizeof(trans->parent->dei->country) - 1);
						if (ies.q_email)
							strncpy(trans->parent->dei->email, ies.q_email, sizeof(trans->parent->dei->email) - 1);
						if (ies.q_phone)
							strncpy(trans->parent->dei->phone, ies.q_phone, sizeof(trans->parent->dei->phone) - 1);
						if (ies.q_ipaddr)
							strncpy(trans->parent->dei->ipaddr, ies.q_ipaddr, sizeof(trans->parent->dei->ipaddr) - 1);
						if (!dundi_eid_cmp(&trans->them_eid, &trans->parent->query_eid)) {
							/* If it's them, update our address */
							ast_inet_ntoa(trans->parent->dei->ipaddr, sizeof(trans->parent->dei->ipaddr),
								trans->addr.sin_addr);
						}
					}
					if (ies.hint) {
						if (ast_test_flag_nonstd(ies.hint, htons(DUNDI_HINT_TTL_EXPIRED)))
							ast_set_flag_nonstd(trans->parent->hmd, DUNDI_HINT_TTL_EXPIRED);
					}
				}
				/* Close connection if not final */
				if (!final) 
					dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
			
		} else {
			/* Auth failure, check for data */
			if (!final) {
				/* Cancel if they didn't already */
				dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
		}
		break;
	case DUNDI_COMMAND_REGRESPONSE:
		/* A dialplan response, lets see what we got... */
		if (ies.cause < 1) {
			int hasauth;
			/* Success of some sort */
			if (ast_test_flag(trans, FLAG_ENCRYPT)) {
				hasauth = encrypted;
			} else 
				hasauth = 1;
			
			if (!hasauth) {
				ast_log(LOG_NOTICE, "Reponse to register not authorized!\n");
				if (!final) {
					dundi_ie_append_cause(&ied, DUNDI_IE_CAUSE, DUNDI_CAUSE_NOAUTH, "Improper signature in answer");
					dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, &ied);
				}
			} else {
				ast_log(LOG_DEBUG, "Yay, we've registered as '%s' to '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &trans->us_eid),
							dundi_eid_to_str(eid_str2, sizeof(eid_str2), &trans->them_eid));
				/* Close connection if not final */
				if (!final) 
					dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
		} else {
			/* Auth failure, cancel if they didn't for some reason */
			if (!final) {
				dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
			}
		}
		break;
	case DUNDI_COMMAND_INVALID:
	case DUNDI_COMMAND_NULL:
	case DUNDI_COMMAND_PRECACHERP:
		/* Do nothing special */
		if (!final) 
			dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
		break;
	case DUNDI_COMMAND_ENCREJ:
		if ((ast_test_flag(trans, FLAG_SENDFULLKEY)) || !trans->lasttrans || !(peer = find_peer(&trans->them_eid))) {
			/* No really, it's over at this point */
			if (!final) 
				dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
		} else {
			/* Send with full key */
			ast_set_flag(trans, FLAG_SENDFULLKEY);
			if (final) {
				/* Ooops, we got a final message, start by sending ACK... */
				dundi_ack(trans, hdr->cmdresp & 0x80);
				trans->aseqno = trans->iseqno;
				/* Now, we gotta create a new transaction */
				if (!reset_transaction(trans)) {
					/* Make sure handle_frame doesn't destroy us */
					hdr->cmdresp &= 0x7f;
					/* Parse the message we transmitted */
					memset(&ies, 0, sizeof(ies));
					dundi_parse_ies(&ies, trans->lasttrans->h->ies, trans->lasttrans->datalen - sizeof(struct dundi_hdr));
					/* Reconstruct outgoing encrypted packet */
					memset(&ied, 0, sizeof(ied));
					dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->us_eid);
					dundi_ie_append_raw(&ied, DUNDI_IE_SHAREDKEY, peer->txenckey, 128);
					dundi_ie_append_raw(&ied, DUNDI_IE_SIGNATURE, peer->txenckey + 128, 128);
					if (ies.encblock) 
						dundi_ie_append_encdata(&ied, DUNDI_IE_ENCDATA, ies.encblock->iv, ies.encblock->encdata, ies.enclen);
					dundi_send(trans, DUNDI_COMMAND_ENCRYPT, 0, trans->lasttrans->h->cmdresp & 0x80, &ied);
					peer->sentfullkey = 1;
				}
			}
		}
		break;
	case DUNDI_COMMAND_ENCRYPT:
		if (!encrypted) {
			/* No nested encryption! */
			if ((trans->iseqno == 1) && !trans->oseqno) {
				if (!ies.eids[0] || !(peer = find_peer(ies.eids[0])) || 
					((!ies.encsharedkey || !ies.encsig) && !ies.keycrc32) || 
					(check_key(peer, ies.encsharedkey, ies.encsig, ies.keycrc32) < 1)) {
					if (!final) {
						dundi_send(trans, DUNDI_COMMAND_ENCREJ, 0, 1, NULL);
					}
					break;
				}
				apply_peer(trans, peer);
				/* Key passed, use new contexts for this session */
				trans->ecx = peer->them_ecx;
				trans->dcx = peer->them_dcx;
			}
			if (ast_test_flag(trans, FLAG_ENCRYPT) && ies.encblock && ies.enclen) {
				struct dundi_hdr *dhdr;
				unsigned char decoded[MAX_PACKET_SIZE];
				int ddatalen;
				ddatalen = sizeof(decoded);
				dhdr = dundi_decrypt(trans, decoded, &ddatalen, hdr, ies.encblock, ies.enclen);
				if (dhdr) {
					/* Handle decrypted response */
					if (dundidebug)
						dundi_showframe(dhdr, 3, &trans->addr, ddatalen - sizeof(struct dundi_hdr));
					handle_command_response(trans, dhdr, ddatalen - sizeof(struct dundi_hdr), 1);
					/* Carry back final flag */
					hdr->cmdresp |= dhdr->cmdresp & 0x80;
					break;
				} else
					ast_log(LOG_DEBUG, "Ouch, decrypt failed :(\n");
			}
		}
		if (!final) {
			/* Turn off encryption */
			ast_clear_flag(trans, FLAG_ENCRYPT);
			dundi_send(trans, DUNDI_COMMAND_ENCREJ, 0, 1, NULL);
		}
		break;
	default:
		/* Send unknown command if we don't know it, with final flag IFF it's the
		   first command in the dialog and only if we haven't recieved final notification */
		if (!final) {
			dundi_ie_append_byte(&ied, DUNDI_IE_UNKNOWN, cmd);
			dundi_send(trans, DUNDI_COMMAND_UNKNOWN, 0, !hdr->oseqno, &ied);
		}
	}
	return 0;
}

static void destroy_packet(struct dundi_packet *pack, int needfree);
static void destroy_packets(struct dundi_packet *p)
{
	struct dundi_packet *prev;
	while(p) {
		prev = p;
		p = p->next;
		if (prev->retransid > -1)
			ast_sched_del(sched, prev->retransid);
		free(prev);
	}
}


static int ack_trans(struct dundi_transaction *trans, int iseqno)
{
	/* Ack transmitted packet corresponding to iseqno */
	struct dundi_packet *pack;
	pack = trans->packets;
	while(pack) {
		if ((pack->h->oseqno + 1) % 255 == iseqno) {
			destroy_packet(pack, 0);
			if (trans->lasttrans) {
				ast_log(LOG_WARNING, "Whoa, there was still a last trans?\n");
				destroy_packets(trans->lasttrans);
			}
			trans->lasttrans = pack;
			if (trans->autokillid > -1)
				ast_sched_del(sched, trans->autokillid);
			trans->autokillid = -1;
			return 1;
		}
		pack = pack->next;
	}
	return 0;
}

static int handle_frame(struct dundi_hdr *h, struct sockaddr_in *sin, int datalen)
{
	struct dundi_transaction *trans;
	trans = find_transaction(h, sin);
	if (!trans) {
		dundi_reject(h, sin);
		return 0;
	}
	/* Got a transaction, see where this header fits in */
	if (h->oseqno == trans->iseqno) {
		/* Just what we were looking for...  Anything but ack increments iseqno */
		if (ack_trans(trans, h->iseqno) && ast_test_flag(trans, FLAG_FINAL)) {
			/* If final, we're done */
			destroy_trans(trans, 0);
			return 0;
		}
		if (h->cmdresp != DUNDI_COMMAND_ACK) {
			trans->oiseqno = trans->iseqno;
			trans->iseqno++;
			handle_command_response(trans, h, datalen, 0);
		}
		if (trans->aseqno != trans->iseqno) {
			dundi_ack(trans, h->cmdresp & 0x80);
			trans->aseqno = trans->iseqno;
		}
		/* Delete any saved last transmissions */
		destroy_packets(trans->lasttrans);
		trans->lasttrans = NULL;
		if (h->cmdresp & 0x80) {
			/* Final -- destroy now */
			destroy_trans(trans, 0);
		}
	} else if (h->oseqno == trans->oiseqno) {
		/* Last incoming sequence number -- send ACK without processing */
		dundi_ack(trans, 0);
	} else {
		/* Out of window -- simply drop */
		ast_log(LOG_DEBUG, "Dropping packet out of window!\n");
	}
	return 0;
}

static int socket_read(int *id, int fd, short events, void *cbdata)
{
	struct sockaddr_in sin;
	int res;
	struct dundi_hdr *h;
	unsigned char buf[MAX_PACKET_SIZE];
	int len;
	len = sizeof(sin);
	res = recvfrom(netsocket, buf, sizeof(buf) - 1, 0,(struct sockaddr *) &sin, &len);
	if (res < 0) {
		if (errno != ECONNREFUSED)
			ast_log(LOG_WARNING, "Error: %s\n", strerror(errno));
		return 1;
	}
	if (res < sizeof(struct dundi_hdr)) {
		ast_log(LOG_WARNING, "midget packet received (%d of %d min)\n", res, (int)sizeof(struct dundi_hdr));
		return 1;
	}
	buf[res] = '\0';
	h = (struct dundi_hdr *)buf;
	if (dundidebug)
		dundi_showframe(h, 1, &sin, res - sizeof(struct dundi_hdr));
	ast_mutex_lock(&peerlock);
	handle_frame(h, &sin, res - sizeof(struct dundi_hdr));
	ast_mutex_unlock(&peerlock);
	return 1;
}

static void build_secret(char *secret, int seclen)
{
	char tmp[16];
	char *s;
	build_iv(tmp);
	secret[0] = '\0';
	ast_base64encode(secret ,tmp, sizeof(tmp), seclen);
	/* Eliminate potential bad characters */
	while((s = strchr(secret, ';'))) *s = '+';
	while((s = strchr(secret, '/'))) *s = '+';
	while((s = strchr(secret, ':'))) *s = '+';
	while((s = strchr(secret, '@'))) *s = '+';
}


static void save_secret(const char *newkey, const char *oldkey)
{
	char tmp[256];
	if (oldkey)
		snprintf(tmp, sizeof(tmp), "%s;%s", oldkey, newkey);
	else
		snprintf(tmp, sizeof(tmp), "%s", newkey);
	rotatetime = time(NULL) + DUNDI_SECRET_TIME;
	ast_db_put(secretpath, "secret", tmp);
	snprintf(tmp, sizeof(tmp), "%ld", rotatetime);
	ast_db_put(secretpath, "secretexpiry", tmp);
}

static void load_password(void)
{
	char *current=NULL;
	char *last=NULL;
	char tmp[256];
	time_t expired;
	
	ast_db_get(secretpath, "secretexpiry", tmp, sizeof(tmp));
	if (sscanf(tmp, "%ld", &expired) == 1) {
		ast_db_get(secretpath, "secret", tmp, sizeof(tmp));
		current = strchr(tmp, ';');
		if (!current)
			current = tmp;
		else {
			*current = '\0';
			current++;
		};
		if ((time(NULL) - expired) < 0) {
			if ((expired - time(NULL)) > DUNDI_SECRET_TIME)
				expired = time(NULL) + DUNDI_SECRET_TIME;
		} else if ((time(NULL) - (expired + DUNDI_SECRET_TIME)) < 0) {
			last = current;
			current = NULL;
		} else {
			last = NULL;
			current = NULL;
		}
	}
	if (current) {
		/* Current key is still valid, just setup rotatation properly */
		strncpy(cursecret, current, sizeof(cursecret) - 1);
		rotatetime = expired;
	} else {
		/* Current key is out of date, rotate or eliminate all together */
		build_secret(cursecret, sizeof(cursecret));
		save_secret(cursecret, last);
	}
}

static void check_password(void)
{
	char oldsecret[80];
	time_t now;
	
	time(&now);	
#if 0
	printf("%ld/%ld\n", now, rotatetime);
#endif
	if ((now - rotatetime) >= 0) {
		/* Time to rotate keys */
		strncpy(oldsecret, cursecret, sizeof(oldsecret) - 1);
		build_secret(cursecret, sizeof(cursecret));
		save_secret(cursecret, oldsecret);
	}
}

static void *network_thread(void *ignore)
{
	/* Our job is simple: Send queued messages, retrying if necessary.  Read frames 
	   from the network, and queue them for delivery to the channels */
	int res;
	/* Establish I/O callback for socket read */
	ast_io_add(io, netsocket, socket_read, AST_IO_IN, NULL);
	for(;;) {
		res = ast_sched_wait(sched);
		if ((res > 1000) || (res < 0))
			res = 1000;
		res = ast_io_wait(io, res);
		if (res >= 0) {
			ast_mutex_lock(&peerlock);
			ast_sched_runq(sched);
			ast_mutex_unlock(&peerlock);
		}
		check_password();
	}
	return NULL;
}

static void *process_precache(void *ign)
{
	struct dundi_precache_queue *qe;
	time_t now;
	char context[256]="";
	char number[256]="";
	int run;
	for (;;) {
		time(&now);
		run = 0;
		ast_mutex_lock(&pclock);
		if (pcq) {
			if (!pcq->expiration) {
				/* Gone...  Remove... */
				qe = pcq;
				pcq = pcq->next;
				free(qe);
			} else if (pcq->expiration < now) {
				/* Process this entry */
				pcq->expiration = 0;
				strncpy(context, pcq->context, sizeof(context) - 1);
				strncpy(number, pcq->number, sizeof(number) - 1);
				run = 1;
			}
		}
		ast_mutex_unlock(&pclock);
		if (run) {
			dundi_precache(context, number);
		} else
			sleep(1);
	}
	return NULL;
}

static int start_network_thread(void)
{
	ast_pthread_create(&netthreadid, NULL, network_thread, NULL);
	ast_pthread_create(&precachethreadid, NULL, process_precache, NULL);
	return 0;
}

static int dundi_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	dundidebug = 1;
	ast_cli(fd, "DUNDi Debugging Enabled\n");
	return RESULT_SUCCESS;
}

static int dundi_do_store_history(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	global_storehistory = 1;
	ast_cli(fd, "DUNDi History Storage Enabled\n");
	return RESULT_SUCCESS;
}

static int dundi_flush(int fd, int argc, char *argv[])
{
	int stats=0;
	if ((argc < 2) || (argc > 3))
		return RESULT_SHOWUSAGE;
	if (argc > 2) {
		if (!strcasecmp(argv[2], "stats"))
			stats = 1;
		else
			return RESULT_SHOWUSAGE;
	}
	if (stats) {
		/* Flush statistics */
		struct dundi_peer *p;
		int x;
		ast_mutex_lock(&peerlock);
		p = peers;
		while(p) {
			for (x=0;x<DUNDI_TIMING_HISTORY;x++) {
				if (p->lookups[x])
					free(p->lookups[x]);
				p->lookups[x] = NULL;
				p->lookuptimes[x] = 0;
			}
			p->avgms = 0;
			p = p->next;
		}
		ast_mutex_unlock(&peerlock);
	} else {
		ast_db_deltree("dundi/cache", NULL);
		ast_cli(fd, "DUNDi Cache Flushed\n");
	}
	return RESULT_SUCCESS;
}

static int dundi_no_debug(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	dundidebug = 0;
	ast_cli(fd, "DUNDi Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static int dundi_no_store_history(int fd, int argc, char *argv[])
{
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	global_storehistory = 0;
	ast_cli(fd, "DUNDi History Storage Disabled\n");
	return RESULT_SUCCESS;
}

static char *model2str(int model)
{
	switch(model) {
	case DUNDI_MODEL_INBOUND:
		return "Inbound";
	case DUNDI_MODEL_OUTBOUND:
		return "Outbound";
	case DUNDI_MODEL_SYMMETRIC:
		return "Symmetric";
	default:
		return "Unknown";
	}
}

static char *complete_peer_helper(char *line, char *word, int pos, int state, int rpos)
{
	int which=0;
	char *ret;
	struct dundi_peer *p;
	char eid_str[20];
	if (pos != rpos)
		return NULL;
	ast_mutex_lock(&peerlock);
	p = peers;
	while(p) {
		if (!strncasecmp(word, dundi_eid_to_str(eid_str, sizeof(eid_str), &p->eid), strlen(word))) {
			if (++which > state)
				break;
		}
		p = p->next;
	}
	if (p) {
		ret = strdup(dundi_eid_to_str(eid_str, sizeof(eid_str), &p->eid));
	} else
		ret = NULL;
	ast_mutex_unlock(&peerlock);
	return ret;
}

static char *complete_peer_4(char *line, char *word, int pos, int state)
{
	return complete_peer_helper(line, word, pos, state, 3);
}

static int rescomp(const void *a, const void *b)
{
	const struct dundi_result *resa, *resb;
	resa = a;
	resb = b;
	if (resa->weight < resb->weight)
		return -1;
	if (resa->weight > resb->weight)
		return 1;
	return 0;
}

static void sort_results(struct dundi_result *results, int count)
{
	qsort(results, count, sizeof(results[0]), rescomp);
}

static int dundi_do_lookup(int fd, int argc, char *argv[])
{
	int res;
	char tmp[256] = "";
	char fs[80] = "";
	char *context;
	int x;
	int bypass = 0;
	struct dundi_result dr[MAX_RESULTS];
	struct timeval start;
	if ((argc < 3) || (argc > 4))
		return RESULT_SHOWUSAGE;
	if (argc > 3) {
		if (!strcasecmp(argv[3], "bypass"))
			bypass=1;
		else
			return RESULT_SHOWUSAGE;
	}
	strncpy(tmp, argv[2], sizeof(tmp) - 1);
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	}
	gettimeofday(&start, NULL);
	res = dundi_lookup(dr, MAX_RESULTS, NULL, context, tmp, bypass);
	
	if (res < 0) 
		ast_cli(fd, "DUNDi lookup returned error.\n");
	else if (!res) 
		ast_cli(fd, "DUNDi lookup returned no results.\n");
	else
		sort_results(dr, res);
	for (x=0;x<res;x++) {
		ast_cli(fd, "%3d. %5d %s/%s (%s)\n", x + 1, dr[x].weight, dr[x].tech, dr[x].dest, dundi_flags2str(fs, sizeof(fs), dr[x].flags));
		ast_cli(fd, "     from %s, expires in %d s\n", dr[x].eid_str, dr[x].expiration);
	}
	ast_cli(fd, "DUNDi lookup completed in %d ms\n", calc_ms(&start));
	return RESULT_SUCCESS;
}

static int dundi_do_precache(int fd, int argc, char *argv[])
{
	int res;
	char tmp[256] = "";
	char *context;
	struct timeval start;
	if ((argc < 3) || (argc > 3))
		return RESULT_SHOWUSAGE;
	strncpy(tmp, argv[2], sizeof(tmp) - 1);
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	}
	gettimeofday(&start, NULL);
	res = dundi_precache(context, tmp);
	
	if (res < 0) 
		ast_cli(fd, "DUNDi precache returned error.\n");
	else if (!res) 
		ast_cli(fd, "DUNDi precache returned no error.\n");
	ast_cli(fd, "DUNDi lookup completed in %d ms\n", calc_ms(&start));
	return RESULT_SUCCESS;
}

static int dundi_do_query(int fd, int argc, char *argv[])
{
	int res;
	char tmp[256] = "";
	char *context;
	dundi_eid eid;
	struct dundi_entity_info dei;
	if ((argc < 3) || (argc > 3))
		return RESULT_SHOWUSAGE;
	if (dundi_str_to_eid(&eid, argv[2])) {
		ast_cli(fd, "'%s' is not a valid EID!\n", argv[2]);
		return RESULT_SHOWUSAGE;
	}
	strncpy(tmp, argv[2], sizeof(tmp) - 1);
	context = strchr(tmp, '@');
	if (context) {
		*context = '\0';
		context++;
	}
	res = dundi_query_eid(&dei, context, eid);
	if (res < 0) 
		ast_cli(fd, "DUNDi Query EID returned error.\n");
	else if (!res) 
		ast_cli(fd, "DUNDi Query EID returned no results.\n");
	else {
		ast_cli(fd, "DUNDi Query EID succeeded:\n");
		ast_cli(fd, "Department:      %s\n", dei.orgunit);
		ast_cli(fd, "Organization:    %s\n", dei.org);
		ast_cli(fd, "City/Locality:   %s\n", dei.locality);
		ast_cli(fd, "State/Province:  %s\n", dei.stateprov);
		ast_cli(fd, "Country:         %s\n", dei.country);
		ast_cli(fd, "E-mail:          %s\n", dei.email);
		ast_cli(fd, "Phone:           %s\n", dei.phone);
		ast_cli(fd, "IP Address:      %s\n", dei.ipaddr);
	}
	return RESULT_SUCCESS;
}

static int dundi_show_peer(int fd, int argc, char *argv[])
{
	struct dundi_peer *peer;
	struct permission *p;
	char *order;
	char iabuf[INET_ADDRSTRLEN];
	char eid_str[20];
	int x, cnt;
	
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&peerlock);
	peer = peers;
	while(peer) {
		if (!strcasecmp(dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), argv[3]))
			break;
		peer = peer->next;
	}
	if (peer) {
		switch(peer->order) {
		case 0:
			order = "Primary";
			break;
		case 1:
			order = "Secondary";
			break;
		case 2:
			order = "Tertiary";
			break;
		case 3:
			order = "Quartiary";
			break;
		default:
			order = "Unknown";
		}
		ast_cli(fd, "Peer:    %s\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
		ast_cli(fd, "Model:   %s\n", model2str(peer->model));
		ast_cli(fd, "Host:    %s\n", peer->addr.sin_addr.s_addr ? ast_inet_ntoa(iabuf, sizeof(iabuf), peer->addr.sin_addr) : "<Unspecified>");
		ast_cli(fd, "Dynamic: %s\n", peer->dynamic ? "yes" : "no");
		ast_cli(fd, "KeyPend: %s\n", peer->keypending ? "yes" : "no");
		ast_cli(fd, "Reg:     %s\n", peer->registerid < 0 ? "No" : "Yes");
		ast_cli(fd, "In Key:  %s\n", ast_strlen_zero(peer->inkey) ? "<None>" : peer->inkey);
		ast_cli(fd, "Out Key: %s\n", ast_strlen_zero(peer->outkey) ? "<None>" : peer->outkey);
		if (peer->include) {
			ast_cli(fd, "Include logic%s:\n", peer->model & DUNDI_MODEL_OUTBOUND ? "" : " (IGNORED)");
		}
		p = peer->include;
		while(p) {
			ast_cli(fd, "-- %s %s\n", p->allow ? "include" : "do not include", p->name);
			p = p->next;
		}
		if (peer->permit) {
			ast_cli(fd, "Query logic%s:\n", peer->model & DUNDI_MODEL_INBOUND ? "" : " (IGNORED)");
		}
		p = peer->permit;
		while(p) {
			ast_cli(fd, "-- %s %s\n", p->allow ? "permit" : "deny", p->name);
			p = p->next;
		}
		cnt = 0;
		for (x=0;x<DUNDI_TIMING_HISTORY;x++) {
			if (peer->lookups[x]) {
				if (!cnt)
					ast_cli(fd, "Last few query times:\n");
				ast_cli(fd, "-- %d. %s (%d ms)\n", x + 1, peer->lookups[x], peer->lookuptimes[x]);
				cnt++;
			}
		}
		if (cnt)
			ast_cli(fd, "Average query time: %d ms\n", peer->avgms);
	} else
		ast_cli(fd, "No such peer '%s'\n", argv[3]);
	ast_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
}

static int dundi_show_peers(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-20.20s %-15.15s     %-10.10s %-8.8s %-15.15s\n"
#define FORMAT "%-20.20s %-15.15s %s %-10.10s %-8.8s %-15.15s\n"
	struct dundi_peer *peer;
	char iabuf[INET_ADDRSTRLEN];
	int registeredonly=0;
	char avgms[20];
	char eid_str[20];
	int online_peers = 0;
	int offline_peers = 0;
	int unmonitored_peers = 0;
	int total_peers = 0;

	if ((argc != 3) && (argc != 4) && (argc != 5))
		return RESULT_SHOWUSAGE;
	if ((argc == 4)) {
 		if (!strcasecmp(argv[3], "registered")) {
			registeredonly = 1;
		} else
			return RESULT_SHOWUSAGE;
 	}
	ast_mutex_lock(&peerlock);
	ast_cli(fd, FORMAT2, "EID", "Host", "Model", "AvgTime", "Status");
	for (peer = peers;peer;peer = peer->next) {
		char status[20] = "";
        int print_line = -1;
		char srch[2000] = "";
		total_peers++;
		if (registeredonly && !peer->addr.sin_addr.s_addr)
			continue;
		if (peer->maxms) {
			if (peer->lastms < 0) {
				strncpy(status, "UNREACHABLE", sizeof(status) - 1);
				offline_peers++;
			}
			else if (peer->lastms > peer->maxms) {
				snprintf(status, sizeof(status), "LAGGED (%d ms)", peer->lastms);
				offline_peers++;
			}
			else if (peer->lastms) {
				snprintf(status, sizeof(status), "OK (%d ms)", peer->lastms);
				online_peers++;
			}
			else {
				strncpy(status, "UNKNOWN", sizeof(status) - 1);
				offline_peers++;
			}
		} else {
			strncpy(status, "Unmonitored", sizeof(status) - 1);
			unmonitored_peers++;
		}
		if (peer->avgms) 
			snprintf(avgms, sizeof(avgms), "%d ms", peer->avgms);
		else
			strcpy(avgms, "Unavail");
		snprintf(srch, sizeof(srch), FORMAT, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), 
					peer->addr.sin_addr.s_addr ? ast_inet_ntoa(iabuf, sizeof(iabuf), peer->addr.sin_addr) : "(Unspecified)",
					peer->dynamic ? "(D)" : "(S)", model2str(peer->model), avgms, status);

                if (argc == 5) {
                  if (!strcasecmp(argv[3],"include") && strstr(srch,argv[4])) {
                        print_line = -1;
                   } else if (!strcasecmp(argv[3],"exclude") && !strstr(srch,argv[4])) {
                        print_line = 1;
                   } else if (!strcasecmp(argv[3],"begin") && !strncasecmp(srch,argv[4],strlen(argv[4]))) {
                        print_line = -1;
                   } else {
                        print_line = 0;
                  }
                }
		
        if (print_line) {
			ast_cli(fd, FORMAT, dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), 
					peer->addr.sin_addr.s_addr ? ast_inet_ntoa(iabuf, sizeof(iabuf), peer->addr.sin_addr) : "(Unspecified)",
					peer->dynamic ? "(D)" : "(S)", model2str(peer->model), avgms, status);
		}
	}
	ast_cli(fd, "%d dundi peers [%d online, %d offline, %d unmonitored]\n", total_peers, online_peers, offline_peers, unmonitored_peers);
	ast_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int dundi_show_trans(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-22.22s %-5.5s %-5.5s %-3.3s %-3.3s %-3.3s\n"
#define FORMAT "%-16.16s:%5d %-5.5d %-5.5d %-3.3d %-3.3d %-3.3d\n"
	struct dundi_transaction *trans;
	char iabuf[INET_ADDRSTRLEN];
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&peerlock);
	ast_cli(fd, FORMAT2, "Remote", "Src", "Dst", "Tx", "Rx", "Ack");
	for (trans = alltrans;trans;trans = trans->allnext) {
			ast_cli(fd, FORMAT, ast_inet_ntoa(iabuf, sizeof(iabuf), trans->addr.sin_addr), 
					ntohs(trans->addr.sin_port), trans->strans, trans->dtrans, trans->oseqno, trans->iseqno, trans->aseqno);
	}
	ast_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int dundi_show_entityid(int fd, int argc, char *argv[])
{
	char eid_str[20];
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&peerlock);
	dundi_eid_to_str(eid_str, sizeof(eid_str), &global_eid);
	ast_mutex_unlock(&peerlock);
	ast_cli(fd, "Global EID for this system is '%s'\n", eid_str);
	return RESULT_SUCCESS;
}

static int dundi_show_requests(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-15s %-15s %-15s %-3.3s %-3.3s\n"
#define FORMAT "%-15s %-15s %-15s %-3.3d %-3.3d\n"
	struct dundi_request *req;
	char eidstr[20];
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&peerlock);
	ast_cli(fd, FORMAT2, "Number", "Context", "Root", "Max", "Rsp");
	for (req = requests;req;req = req->next) {
			ast_cli(fd, FORMAT, req->number, req->dcontext,
						dundi_eid_zero(&req->root_eid) ? "<unspecified>" : dundi_eid_to_str(eidstr, sizeof(eidstr), &req->root_eid), req->maxcount, req->respcount);
	}
	ast_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

/* Grok-a-dial DUNDi */

static int dundi_show_mappings(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-12.12s %-7.7s %-12.12s %-10.10s %-5.5s %-25.25s\n"
#define FORMAT "%-12.12s %-7d %-12.12s %-10.10s %-5.5s %-25.25s\n"
	struct dundi_mapping *map;
	char fs[256];
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&peerlock);
	ast_cli(fd, FORMAT2, "DUNDi Cntxt", "Weight", "Local Cntxt", "Options", "Tech", "Destination");
	for (map = mappings;map;map = map->next) {
			ast_cli(fd, FORMAT, map->dcontext, map->weight, 
			                    ast_strlen_zero(map->lcontext) ? "<none>" : map->lcontext, 
								dundi_flags2str(fs, sizeof(fs), map->options), tech2str(map->tech), map->dest);
	}
	ast_mutex_unlock(&peerlock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static int dundi_show_precache(int fd, int argc, char *argv[])
{
#define FORMAT2 "%-12.12s %-12.12s %-10.10s\n"
#define FORMAT "%-12.12s %-12.12s %02d:%02d:%02d\n"
	struct dundi_precache_queue *qe;
	int h,m,s;
	time_t now;
	
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	time(&now);
	ast_mutex_lock(&pclock);
	ast_cli(fd, FORMAT2, "Number", "Context", "Expiration");
	for (qe = pcq;qe;qe = qe->next) {
		s = qe->expiration - now;
		h = s / 3600;
		s = s % 3600;
		m = s / 60;
		s = s % 60;
		ast_cli(fd, FORMAT, qe->number, qe->context, h,m,s);
	}
	ast_mutex_unlock(&pclock);
	return RESULT_SUCCESS;
#undef FORMAT
#undef FORMAT2
}

static char debug_usage[] = 
"Usage: dundi debug\n"
"       Enables dumping of DUNDi packets for debugging purposes\n";

static char no_debug_usage[] = 
"Usage: dundi no debug\n"
"       Disables dumping of DUNDi packets for debugging purposes\n";

static char store_history_usage[] = 
"Usage: dundi store history\n"
"       Enables storing of DUNDi requests and times for debugging\n"
"purposes\n";

static char no_store_history_usage[] = 
"Usage: dundi no store history\n"
"       Disables storing of DUNDi requests and times for debugging\n"
"purposes\n";

static char show_peers_usage[] = 
"Usage: dundi show peers\n"
"       Lists all known DUNDi peers.\n";

static char show_trans_usage[] = 
"Usage: dundi show trans\n"
"       Lists all known DUNDi transactions.\n";

static char show_mappings_usage[] = 
"Usage: dundi show mappings\n"
"       Lists all known DUNDi mappings.\n";

static char show_precache_usage[] = 
"Usage: dundi show precache\n"
"       Lists all known DUNDi scheduled precache updates.\n";

static char show_entityid_usage[] = 
"Usage: dundi show entityid\n"
"       Displays the global entityid for this host.\n";

static char show_peer_usage[] = 
"Usage: dundi show peer [peer]\n"
"       Provide a detailed description of a specifid DUNDi peer.\n";

static char show_requests_usage[] = 
"Usage: dundi show requests\n"
"       Lists all known pending DUNDi requests.\n";

static char lookup_usage[] =
"Usage: dundi lookup <number>[@context] [bypass]\n"
"       Lookup the given number within the given DUNDi context\n"
"(or e164 if none is specified).  Bypasses cache if 'bypass'\n"
"keyword is specified.\n";

static char precache_usage[] =
"Usage: dundi precache <number>[@context]\n"
"       Lookup the given number within the given DUNDi context\n"
"(or e164 if none is specified) and precaches the results to any\n"
"upstream DUNDi push servers.\n";

static char query_usage[] =
"Usage: dundi query <entity>[@context]\n"
"       Attempts to retrieve contact information for a specific\n"
"DUNDi entity identifier (EID) within a given DUNDi context (or\n"
"e164 if none is specified).\n";

static char flush_usage[] =
"Usage: dundi flush [stats]\n"
"       Flushes DUNDi answer cache, used primarily for debug.  If\n"
"'stats' is present, clears timer statistics instead of normal\n"
"operation.\n";

static struct ast_cli_entry  cli_debug =
	{ { "dundi", "debug", NULL }, dundi_do_debug, "Enable DUNDi debugging", debug_usage };

static struct ast_cli_entry  cli_store_history =
	{ { "dundi", "store", "history", NULL }, dundi_do_store_history, "Enable DUNDi historic records", store_history_usage };

static struct ast_cli_entry  cli_no_store_history =
	{ { "dundi", "no", "store", "history", NULL }, dundi_no_store_history, "Disable DUNDi historic records", no_store_history_usage };

static struct ast_cli_entry  cli_flush =
	{ { "dundi", "flush", NULL }, dundi_flush, "Flush DUNDi cache", flush_usage };

static struct ast_cli_entry  cli_no_debug =
	{ { "dundi", "no", "debug", NULL }, dundi_no_debug, "Disable DUNDi debugging", no_debug_usage };

static struct ast_cli_entry  cli_show_peers =
	{ { "dundi", "show", "peers", NULL }, dundi_show_peers, "Show defined DUNDi peers", show_peers_usage };

static struct ast_cli_entry  cli_show_trans =
	{ { "dundi", "show", "trans", NULL }, dundi_show_trans, "Show active DUNDi transactions", show_trans_usage };

static struct ast_cli_entry  cli_show_entityid =
	{ { "dundi", "show", "entityid", NULL }, dundi_show_entityid, "Display Global Entity ID", show_entityid_usage };

static struct ast_cli_entry  cli_show_mappings =
	{ { "dundi", "show", "mappings", NULL }, dundi_show_mappings, "Show DUNDi mappings", show_mappings_usage };

static struct ast_cli_entry  cli_show_precache =
	{ { "dundi", "show", "precache", NULL }, dundi_show_precache, "Show DUNDi precache", show_precache_usage };

static struct ast_cli_entry  cli_show_requests =
	{ { "dundi", "show", "requests", NULL }, dundi_show_requests, "Show DUNDi requests", show_requests_usage };

static struct ast_cli_entry  cli_show_peer =
	{ { "dundi", "show", "peer", NULL }, dundi_show_peer, "Show info on a specific DUNDi peer", show_peer_usage, complete_peer_4 };

static struct ast_cli_entry  cli_lookup =
	{ { "dundi", "lookup", NULL }, dundi_do_lookup, "Lookup a number in DUNDi", lookup_usage };

static struct ast_cli_entry  cli_precache =
	{ { "dundi", "precache", NULL }, dundi_do_precache, "Precache a number in DUNDi", precache_usage };

static struct ast_cli_entry  cli_queryeid =
	{ { "dundi", "query", NULL }, dundi_do_query, "Query a DUNDi EID", query_usage };

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static struct dundi_transaction *create_transaction(struct dundi_peer *p)
{
	struct dundi_transaction *trans;
	int tid;
	
	/* Don't allow creation of transactions to non-registered peers */
	if (p && !p->addr.sin_addr.s_addr)
		return NULL;
	tid = get_trans_id();
	if (tid < 1)
		return NULL;
	trans = malloc(sizeof(struct dundi_transaction));
	if (trans) {
		memset(trans, 0, sizeof(struct dundi_transaction));
		if (global_storehistory) {
			gettimeofday(&trans->start, NULL);
			ast_set_flag(trans, FLAG_STOREHIST);
		}
		trans->retranstimer = DUNDI_DEFAULT_RETRANS_TIMER;
		trans->autokillid = -1;
		if (p) {
			apply_peer(trans, p);
			if (!p->sentfullkey)
				ast_set_flag(trans, FLAG_SENDFULLKEY);
		}
		trans->strans = tid;
		trans->allnext = alltrans;
		alltrans = trans;
	}
	return trans;
}

static int dundi_xmit(struct dundi_packet *pack)
{
	int res;
	char iabuf[INET_ADDRSTRLEN];
	if (dundidebug)
		dundi_showframe(pack->h, 0, &pack->parent->addr, pack->datalen - sizeof(struct dundi_hdr));
	res = sendto(netsocket, pack->data, pack->datalen, 0, (struct sockaddr *)&pack->parent->addr, sizeof(pack->parent->addr));
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to transmit to '%s:%d': %s\n", 
			ast_inet_ntoa(iabuf, sizeof(iabuf), pack->parent->addr.sin_addr),
			ntohs(pack->parent->addr.sin_port), strerror(errno));
	}
	if (res > 0)
		res = 0;
	return res;
}

static void destroy_packet(struct dundi_packet *pack, int needfree)
{
	struct dundi_packet *prev, *cur;
	if (pack->parent) {
		prev = NULL;
		cur = pack->parent->packets;
		while(cur) {
			if (cur == pack) {
				if (prev)
					prev->next = cur->next;
				else
					pack->parent->packets = cur->next;
				break;
			}
			prev = cur;
			cur = cur->next;
		}
	}
	if (pack->retransid > -1)
		ast_sched_del(sched, pack->retransid);
	if (needfree)
		free(pack);
	else {
		pack->retransid = -1;
		pack->next = NULL;
	}
}

static void destroy_trans(struct dundi_transaction *trans, int fromtimeout)
{
	struct dundi_transaction *cur, *prev;
	struct dundi_peer *peer;
	struct timeval tv;
	int ms;
	int x;
	int cnt;
	char eid_str[20];
	if (ast_test_flag(trans, FLAG_ISREG | FLAG_ISQUAL | FLAG_STOREHIST)) {
		peer = peers;
		while (peer) {
			if (peer->regtrans == trans)
				peer->regtrans = NULL;
			if (peer->keypending == trans)
				peer->keypending = NULL;
			if (peer->qualtrans == trans) {
				if (fromtimeout) {
					if (peer->lastms > -1)
						ast_log(LOG_NOTICE, "Peer '%s' has become UNREACHABLE!\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
					peer->lastms = -1;
				} else {
					gettimeofday(&tv, NULL);
					ms = (tv.tv_sec - peer->qualtx.tv_sec) * 1000 + 
							(tv.tv_usec - peer->qualtx.tv_usec) / 1000;
					if (ms < 1)
						ms = 1;
					if (ms < peer->maxms) {
						if ((peer->lastms >= peer->maxms) || (peer->lastms < 0))
							ast_log(LOG_NOTICE, "Peer '%s' has become REACHABLE!\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
					} else if (peer->lastms < peer->maxms) {
						ast_log(LOG_NOTICE, "Peer '%s' has become TOO LAGGED (%d ms)\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), ms);
					}
					peer->lastms = ms;
				}
				peer->qualtrans = NULL;
			}
			if (ast_test_flag(trans, FLAG_STOREHIST)) {
				if (trans->parent && !ast_strlen_zero(trans->parent->number)) {
					if (!dundi_eid_cmp(&trans->them_eid, &peer->eid)) {
						peer->avgms = 0;
						cnt = 0;
						if (peer->lookups[DUNDI_TIMING_HISTORY-1])
							free(peer->lookups[DUNDI_TIMING_HISTORY-1]);
						for (x=DUNDI_TIMING_HISTORY-1;x>0;x--) {
							peer->lookuptimes[x] = peer->lookuptimes[x-1];
							peer->lookups[x] = peer->lookups[x-1];
							if (peer->lookups[x]) {
								peer->avgms += peer->lookuptimes[x];
								cnt++;
							}
						}
						peer->lookuptimes[0] = calc_ms(&trans->start);
						peer->lookups[0] = malloc(strlen(trans->parent->number) + strlen(trans->parent->dcontext) + 2);
						if (peer->lookups[0]) {
							sprintf(peer->lookups[0], "%s@%s", trans->parent->number, trans->parent->dcontext);
							peer->avgms += peer->lookuptimes[0];
							cnt++;
						}
						if (cnt)
							peer->avgms /= cnt;
					}
				}
			}
			peer = peer->next;
		}
	}
	if (trans->parent) {
		/* Unlink from parent if appropriate */
		prev = NULL;
		cur = trans->parent->trans;
		while(cur) {
			if (cur == trans) {
				if (prev)
					prev->next = trans->next;
				else
					trans->parent->trans = trans->next;
				break;
			}
			prev = cur;
			cur = cur->next;
		}
		if (!trans->parent->trans) {
			/* Wake up sleeper */
			if (trans->parent->pfds[1] > -1) {
				write(trans->parent->pfds[1], "killa!", 6);
			}
		}
	}
	/* Unlink from all trans */
	prev = NULL;
	cur = alltrans;
	while(cur) {
		if (cur == trans) {
			if (prev)
				prev->allnext = trans->allnext;
			else
				alltrans = trans->allnext;
			break;
		}
		prev = cur;
		cur = cur->allnext;
	}
	destroy_packets(trans->packets);
	destroy_packets(trans->lasttrans);
	trans->packets = NULL;
	trans->lasttrans = NULL;
	if (trans->autokillid > -1)
		ast_sched_del(sched, trans->autokillid);
	trans->autokillid = -1;
	if (trans->thread) {
		/* If used by a thread, mark as dead and be done */
		ast_set_flag(trans, FLAG_DEAD);
	} else
		free(trans);
}

static int dundi_rexmit(void *data)
{
	struct dundi_packet *pack;
	char iabuf[INET_ADDRSTRLEN];
	int res;
	ast_mutex_lock(&peerlock);
	pack = data;
	if (pack->retrans < 1) {
		pack->retransid = -1;
		if (!ast_test_flag(pack->parent, FLAG_ISQUAL))
			ast_log(LOG_NOTICE, "Max retries exceeded to host '%s:%d' msg %d on call %d\n", 
				ast_inet_ntoa(iabuf, sizeof(iabuf), pack->parent->addr.sin_addr), 
				ntohs(pack->parent->addr.sin_port), pack->h->oseqno, ntohs(pack->h->strans));
		destroy_trans(pack->parent, 1);
		res = 0;
	} else {
		/* Decrement retransmission, try again */
		pack->retrans--;
		dundi_xmit(pack);
		res = 1;
	}
	ast_mutex_unlock(&peerlock);
	return res;
}

static int dundi_send(struct dundi_transaction *trans, int cmdresp, int flags, int final, struct dundi_ie_data *ied)
{
	struct dundi_packet *pack;
	int res;
	int len;
	char eid_str[20];
	len = sizeof(struct dundi_packet) + sizeof(struct dundi_hdr) + (ied ? ied->pos : 0);
	/* Reserve enough space for encryption */
	if (ast_test_flag(trans, FLAG_ENCRYPT))
		len += 384;
	pack = malloc(len);
	if (pack) {
		memset(pack, 0, len);
		pack->h = (struct dundi_hdr *)(pack->data);
		if (cmdresp != DUNDI_COMMAND_ACK) {
			pack->retransid = ast_sched_add(sched, trans->retranstimer, dundi_rexmit, pack);
			pack->retrans = DUNDI_DEFAULT_RETRANS - 1;
			pack->next = trans->packets;
			trans->packets = pack;
		}
		pack->parent = trans;
		pack->h->strans = htons(trans->strans);
		pack->h->dtrans = htons(trans->dtrans);
		pack->h->iseqno = trans->iseqno;
		pack->h->oseqno = trans->oseqno;
		pack->h->cmdresp = cmdresp;
		pack->datalen = sizeof(struct dundi_hdr);
		if (ied) {
			memcpy(pack->h->ies, ied->buf, ied->pos);
			pack->datalen += ied->pos;
		} 
		if (final) {
			pack->h->cmdresp |= DUNDI_COMMAND_FINAL;
			ast_set_flag(trans, FLAG_FINAL);
		}
		pack->h->cmdflags = flags;
		if (cmdresp != DUNDI_COMMAND_ACK) {
			trans->oseqno++;
			trans->oseqno = trans->oseqno % 256;
		}
		trans->aseqno = trans->iseqno;
		/* If we have their public key, encrypt */
		if (ast_test_flag(trans, FLAG_ENCRYPT)) {
			switch(cmdresp) {
			case DUNDI_COMMAND_REGREQ:
			case DUNDI_COMMAND_REGRESPONSE:
			case DUNDI_COMMAND_DPDISCOVER:
			case DUNDI_COMMAND_DPRESPONSE:
			case DUNDI_COMMAND_EIDQUERY:
			case DUNDI_COMMAND_EIDRESPONSE:
			case DUNDI_COMMAND_PRECACHERQ:
			case DUNDI_COMMAND_PRECACHERP:
				if (dundidebug)
					dundi_showframe(pack->h, 2, &trans->addr, pack->datalen - sizeof(struct dundi_hdr));
				res = dundi_encrypt(trans, pack);
				break;
			default:
				res = 0;
			}
		} else 
			res = 0;
		if (!res) 
			res = dundi_xmit(pack);
		if (res)
			ast_log(LOG_NOTICE, "Failed to send packet to '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &trans->them_eid));
				
		if (cmdresp == DUNDI_COMMAND_ACK)
			free(pack);
		return res;
	}
	return -1;
}

static int do_autokill(void *data)
{
	struct dundi_transaction *trans = data;
	char eid_str[20];
	ast_log(LOG_NOTICE, "Transaction to '%s' took too long to ACK, destroying\n", 
		dundi_eid_to_str(eid_str, sizeof(eid_str), &trans->them_eid));
	trans->autokillid = -1;
	destroy_trans(trans, 0); /* We could actually set it to 1 instead of 0, but we won't ;-) */
	return 0;
}

static void dundi_ie_append_eid_appropriately(struct dundi_ie_data *ied, char *context, dundi_eid *eid, dundi_eid *us)
{
	struct dundi_peer *p;
	if (!dundi_eid_cmp(eid, us)) {
		dundi_ie_append_eid(ied, DUNDI_IE_EID_DIRECT, eid);
		return;
	}
	ast_mutex_lock(&peerlock);
	p = peers;
	while(p) {
		if (!dundi_eid_cmp(&p->eid, eid)) {
			if (has_permission(p->include, context))
				dundi_ie_append_eid(ied, DUNDI_IE_EID_DIRECT, eid);
			else
				dundi_ie_append_eid(ied, DUNDI_IE_EID, eid);
			break;
		}
		p = p->next;
	}
	if (!p)
		dundi_ie_append_eid(ied, DUNDI_IE_EID, eid);
	ast_mutex_unlock(&peerlock);
}

static int dundi_discover(struct dundi_transaction *trans)
{
	struct dundi_ie_data ied;
	int x;
	if (!trans->parent) {
		ast_log(LOG_WARNING, "Tried to discover a transaction with no parent?!?\n");
		return -1;
	}
	memset(&ied, 0, sizeof(ied));
	dundi_ie_append_short(&ied, DUNDI_IE_VERSION, DUNDI_DEFAULT_VERSION);
	if (!dundi_eid_zero(&trans->us_eid))
		dundi_ie_append_eid(&ied, DUNDI_IE_EID_DIRECT, &trans->us_eid);
	for (x=0;x<trans->eidcount;x++)
		dundi_ie_append_eid_appropriately(&ied, trans->parent->dcontext, &trans->eids[x], &trans->us_eid);
	dundi_ie_append_str(&ied, DUNDI_IE_CALLED_NUMBER, trans->parent->number);
	dundi_ie_append_str(&ied, DUNDI_IE_CALLED_CONTEXT, trans->parent->dcontext);
	dundi_ie_append_short(&ied, DUNDI_IE_TTL, trans->ttl);
	if (trans->parent->cbypass)
		dundi_ie_append(&ied, DUNDI_IE_CACHEBYPASS);
	if (trans->autokilltimeout)
		trans->autokillid = ast_sched_add(sched, trans->autokilltimeout, do_autokill, trans);
	return dundi_send(trans, DUNDI_COMMAND_DPDISCOVER, 0, 0, &ied);
}

static int precache_trans(struct dundi_transaction *trans, struct dundi_mapping *maps, int mapcount, int *minexp, int *foundanswers)
{
	struct dundi_ie_data ied;
	int x, res;
	int max = 999999;
	int expiration = DUNDI_DEFAULT_CACHE_TIME;
	int ouranswers=0;
	dundi_eid *avoid[1] = { NULL, };
	int direct[1] = { 0, };
	struct dundi_result dr[MAX_RESULTS];
	struct dundi_hint_metadata hmd;
	if (!trans->parent) {
		ast_log(LOG_WARNING, "Tried to discover a transaction with no parent?!?\n");
		return -1;
	}
	memset(&hmd, 0, sizeof(hmd));
	memset(&dr, 0, sizeof(dr));
	/* Look up the answers we're going to include */
	for (x=0;x<mapcount;x++)
		ouranswers = dundi_lookup_local(dr, maps + x, trans->parent->number, &trans->us_eid, ouranswers, &hmd);
	if (ouranswers < 0)
		ouranswers = 0;
	for (x=0;x<ouranswers;x++) {
		if (dr[x].weight < max)
			max = dr[x].weight;
	}
	if (max) {
		/* If we do not have a canonical result, keep looking */
		res = dundi_lookup_internal(dr + ouranswers, MAX_RESULTS - ouranswers, NULL, trans->parent->dcontext, trans->parent->number, trans->ttl, 1, &hmd, &expiration, 0, 1, &trans->them_eid, avoid, direct);
		if (res > 0) {
			/* Append answer in result */
			ouranswers += res;
		}
	}
	
	if (ouranswers > 0) {
		*foundanswers += ouranswers;
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_short(&ied, DUNDI_IE_VERSION, DUNDI_DEFAULT_VERSION);
		if (!dundi_eid_zero(&trans->us_eid))
			dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->us_eid);
		for (x=0;x<trans->eidcount;x++)
			dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->eids[x]);
		dundi_ie_append_str(&ied, DUNDI_IE_CALLED_NUMBER, trans->parent->number);
		dundi_ie_append_str(&ied, DUNDI_IE_CALLED_CONTEXT, trans->parent->dcontext);
		dundi_ie_append_short(&ied, DUNDI_IE_TTL, trans->ttl);
		for (x=0;x<ouranswers;x++) {
			/* Add answers */
			if (dr[x].expiration && (expiration > dr[x].expiration))
				expiration = dr[x].expiration;
			dundi_ie_append_answer(&ied, DUNDI_IE_ANSWER, &dr[x].eid, dr[x].techint, dr[x].flags, dr[x].weight, dr[x].dest);
		}
		dundi_ie_append_hint(&ied, DUNDI_IE_HINT, hmd.flags, hmd.exten);
		dundi_ie_append_short(&ied, DUNDI_IE_EXPIRATION, expiration);
		if (trans->autokilltimeout)
			trans->autokillid = ast_sched_add(sched, trans->autokilltimeout, do_autokill, trans);
		if (expiration < *minexp)
			*minexp = expiration;
		return dundi_send(trans, DUNDI_COMMAND_PRECACHERQ, 0, 0, &ied);
	} else {
		/* Oops, nothing to send... */
		destroy_trans(trans, 0);
		return 0;
	}
}

static int dundi_query(struct dundi_transaction *trans)
{
	struct dundi_ie_data ied;
	int x;
	if (!trans->parent) {
		ast_log(LOG_WARNING, "Tried to query a transaction with no parent?!?\n");
		return -1;
	}
	memset(&ied, 0, sizeof(ied));
	dundi_ie_append_short(&ied, DUNDI_IE_VERSION, DUNDI_DEFAULT_VERSION);
	if (!dundi_eid_zero(&trans->us_eid))
		dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->us_eid);
	for (x=0;x<trans->eidcount;x++)
		dundi_ie_append_eid(&ied, DUNDI_IE_EID, &trans->eids[x]);
	dundi_ie_append_eid(&ied, DUNDI_IE_REQEID, &trans->parent->query_eid);
	dundi_ie_append_str(&ied, DUNDI_IE_CALLED_CONTEXT, trans->parent->dcontext);
	dundi_ie_append_short(&ied, DUNDI_IE_TTL, trans->ttl);
	if (trans->autokilltimeout)
		trans->autokillid = ast_sched_add(sched, trans->autokilltimeout, do_autokill, trans);
	return dundi_send(trans, DUNDI_COMMAND_EIDQUERY, 0, 0, &ied);
}

static int discover_transactions(struct dundi_request *dr)
{
	struct dundi_transaction *trans;
	ast_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		dundi_discover(trans);
		trans = trans->next;
	}
	ast_mutex_unlock(&peerlock);
	return 0;
}

static int precache_transactions(struct dundi_request *dr, struct dundi_mapping *maps, int mapcount, int *expiration, int *foundanswers)
{
	struct dundi_transaction *trans, *transn;
	/* Mark all as "in thread" so they don't disappear */
	ast_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		if (trans->thread)
			ast_log(LOG_WARNING, "This shouldn't happen, really...\n");
		trans->thread = 1;
		trans = trans->next;
	}
	ast_mutex_unlock(&peerlock);

	trans = dr->trans;
	while(trans) {
		if (!ast_test_flag(trans, FLAG_DEAD))
			precache_trans(trans, maps, mapcount, expiration, foundanswers);
		trans = trans->next;
	}

	/* Cleanup any that got destroyed in the mean time */
	ast_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		transn = trans->next;
		trans->thread = 0;
		if (ast_test_flag(trans, FLAG_DEAD)) {
			ast_log(LOG_DEBUG, "Our transaction went away!\n");
			destroy_trans(trans, 0);
		}
		trans = transn;
	}
	ast_mutex_unlock(&peerlock);
	return 0;
}

static int query_transactions(struct dundi_request *dr)
{
	struct dundi_transaction *trans;
	ast_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		dundi_query(trans);
		trans = trans->next;
	}
	ast_mutex_unlock(&peerlock);
	return 0;
}

static int optimize_transactions(struct dundi_request *dr, int order)
{
	/* Minimize the message propagation through DUNDi by
	   alerting the network to hops which should be not be considered */
	struct dundi_transaction *trans;
	struct dundi_peer *peer;
	dundi_eid tmp;
	int x;
	int needpush;
	ast_mutex_lock(&peerlock);
	trans = dr->trans;
	while(trans) {
		/* Pop off the true root */
		if (trans->eidcount) {
			tmp = trans->eids[--trans->eidcount];
			needpush = 1;
		} else {
			tmp = trans->us_eid;
			needpush = 0;
		}

		peer = peers;
		while(peer) {
			if (has_permission(peer->include, dr->dcontext) && 
			    dundi_eid_cmp(&peer->eid, &trans->them_eid) &&
				(peer->order <= order)) {
				/* For each other transaction, make sure we don't
				   ask this EID about the others if they're not
				   already in the list */
				if (!dundi_eid_cmp(&tmp, &peer->eid)) 
					x = -1;
				else {
					for (x=0;x<trans->eidcount;x++) {
						if (!dundi_eid_cmp(&trans->eids[x], &peer->eid))
							break;
					}
				}
				if (x == trans->eidcount) {
					/* Nope not in the list, if needed, add us at the end since we're the source */
					if (trans->eidcount < DUNDI_MAX_STACK - needpush) {
						trans->eids[trans->eidcount++] = peer->eid;
						/* Need to insert the real root (or us) at the bottom now as
						   a requirement now.  */
						needpush = 1;
					}
				}
			}
			peer = peer->next;
		}
		/* If necessary, push the true root back on the end */
		if (needpush)
			trans->eids[trans->eidcount++] = tmp;
		trans = trans->next;
	}
	ast_mutex_unlock(&peerlock);
	return 0;
}

static int append_transaction(struct dundi_request *dr, struct dundi_peer *p, int ttl, dundi_eid *avoid[])
{
	struct dundi_transaction *trans;
	int x;
	char eid_str[20];
	char eid_str2[20];
	/* Ignore if not registered */
	if (!p->addr.sin_addr.s_addr)
		return 0;
	if (p->maxms && ((p->lastms < 0) || (p->lastms >= p->maxms)))
		return 0;
	if (ast_strlen_zero(dr->number))
		ast_log(LOG_DEBUG, "Will query peer '%s' for '%s' (context '%s')\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &p->eid), dundi_eid_to_str(eid_str2, sizeof(eid_str2), &dr->query_eid), dr->dcontext);
	else
		ast_log(LOG_DEBUG, "Will query peer '%s' for '%s@%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &p->eid), dr->number, dr->dcontext);
	trans = create_transaction(p);
	if (!trans)
		return -1;
	trans->next = dr->trans;
	trans->parent = dr;
	trans->ttl = ttl;
	for (x=0;avoid[x] && (x <DUNDI_MAX_STACK);x++)
		trans->eids[x] = *avoid[x];
	trans->eidcount = x;
	dr->trans = trans;
	return 0;
}

static void cancel_request(struct dundi_request *dr)
{
	struct dundi_transaction *trans, *next;

	ast_mutex_lock(&peerlock);
	trans = dr->trans;
	
	while(trans) {
		next = trans->next;
		/* Orphan transaction from request */
		trans->parent = NULL;
		trans->next = NULL;
		/* Send final cancel */
		dundi_send(trans, DUNDI_COMMAND_CANCEL, 0, 1, NULL);
		trans = next;
	}
	ast_mutex_unlock(&peerlock);
}

static void abort_request(struct dundi_request *dr)
{
	ast_mutex_lock(&peerlock);
	while(dr->trans) 
		destroy_trans(dr->trans, 0);
	ast_mutex_unlock(&peerlock);
}

static void build_transactions(struct dundi_request *dr, int ttl, int order, int *foundcache, int *skipped, int blockempty, int nocache, int modeselect, dundi_eid *skip, dundi_eid *avoid[], int directs[])
{
	struct dundi_peer *p;
	int x;
	int res;
	int pass;
	int allowconnect;
	char eid_str[20];
	ast_mutex_lock(&peerlock);
	p = peers;
	while(p) {
		if (modeselect == 1) {
			/* Send the precache to push upstreams only! */
			pass = has_permission(p->permit, dr->dcontext) && (p->pcmodel & DUNDI_MODEL_OUTBOUND);
			allowconnect = 1;
		} else {
			/* Normal lookup / EID query */
			pass = has_permission(p->include, dr->dcontext);
			allowconnect = p->model & DUNDI_MODEL_OUTBOUND;
		}
		if (skip) {
			if (!dundi_eid_cmp(skip, &p->eid))
				pass = 0;
		}
		if (pass) {
			if (p->order <= order) {
				/* Check order first, then check cache, regardless of
				   omissions, this gets us more likely to not have an
				   affected answer. */
				if((nocache || !(res = cache_lookup(dr, &p->eid, dr->crc32, &dr->expiration)))) {
					res = 0;
					/* Make sure we haven't already seen it and that it won't
					   affect our answer */
					for (x=0;avoid[x];x++) {
						if (!dundi_eid_cmp(avoid[x], &p->eid) || !dundi_eid_cmp(avoid[x], &p->us_eid)) {
							/* If not a direct connection, it affects our answer */
							if (directs && !directs[x]) 
								ast_clear_flag_nonstd(dr->hmd, DUNDI_HINT_UNAFFECTED);
							break;
						}
					}
					/* Make sure we can ask */
					if (allowconnect) {
						if (!avoid[x] && (!blockempty || !dundi_eid_zero(&p->us_eid))) {
							/* Check for a matching or 0 cache entry */
							append_transaction(dr, p, ttl, avoid);
						} else
							ast_log(LOG_DEBUG, "Avoiding '%s' in transaction\n", dundi_eid_to_str(eid_str, sizeof(eid_str), avoid[x]));
					}
				}
				*foundcache |= res;
			} else if (!*skipped || (p->order < *skipped))
				*skipped = p->order;
		}
		p = p->next;
	}
	ast_mutex_unlock(&peerlock);
}

static int register_request(struct dundi_request *dr, struct dundi_request **pending)
{
	struct dundi_request *cur;
	int res=0;
	char eid_str[20];
	ast_mutex_lock(&peerlock);
	cur = requests;
	while(cur) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Checking '%s@%s' vs '%s@%s'\n", cur->dcontext, cur->number,
				dr->dcontext, dr->number);
		if (!strcasecmp(cur->dcontext, dr->dcontext) &&
		    !strcasecmp(cur->number, dr->number) &&
			(!dundi_eid_cmp(&cur->root_eid, &dr->root_eid) || (cur->crc32 == dr->crc32))) {
				ast_log(LOG_DEBUG, "Found existing query for '%s@%s' for '%s' crc '%08lx'\n", 
					cur->dcontext, cur->number, dundi_eid_to_str(eid_str, sizeof(eid_str), &cur->root_eid), cur->crc32);
				*pending = cur;
			res = 1;
			break;
		}
		cur = cur->next;
	}
	if (!res) {
		ast_log(LOG_DEBUG, "Registering request for '%s@%s' on behalf of '%s' crc '%08lx'\n", 
				dr->number, dr->dcontext, dundi_eid_to_str(eid_str, sizeof(eid_str), &dr->root_eid), dr->crc32);
		/* Go ahead and link us in since nobody else is searching for this */
		dr->next = requests;
		requests = dr;
		*pending = NULL;
	}
	ast_mutex_unlock(&peerlock);
	return res;
}

static void unregister_request(struct dundi_request *dr)
{
	struct dundi_request *cur, *prev;
	ast_mutex_lock(&peerlock);
	prev = NULL;
	cur = requests;
	while(cur) {
		if (cur == dr) {
			if (prev)
				prev->next = cur->next;
			else
				requests = cur->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	ast_mutex_unlock(&peerlock);
}

static int check_request(struct dundi_request *dr)
{
	struct dundi_request *cur;
	int res = 0;
	ast_mutex_lock(&peerlock);
	cur = requests;
	while(cur) {
		if (cur == dr) {
			res = 1;
			break;
		}
		cur = cur->next;
	}
	ast_mutex_unlock(&peerlock);
	return res;
}

static unsigned long avoid_crc32(dundi_eid *avoid[])
{
	/* Idea is that we're calculating a checksum which is independent of
	   the order that the EID's are listed in */
	unsigned long acrc32 = 0;
	int x;
	for (x=0;avoid[x];x++) {
		/* Order doesn't matter */
		if (avoid[x+1]) {
			acrc32 ^= crc32(0L, (unsigned char *)avoid[x], sizeof(dundi_eid));
		}
	}
	return acrc32;
}

static int dundi_lookup_internal(struct dundi_result *result, int maxret, struct ast_channel *chan, const char *dcontext, const char *number, int ttl, int blockempty, struct dundi_hint_metadata *hmd, int *expiration, int cbypass, int modeselect, dundi_eid *skip, dundi_eid *avoid[], int direct[])
{
	int res;
	struct dundi_request dr, *pending;
	dundi_eid *rooteid=NULL;
	int x;
	int ttlms;
	int ms;
	int foundcache;
	int skipped=0;
	int order=0;
	char eid_str[20];
	struct timeval start;
	
	/* Don't do anthing for a hungup channel */
	if (chan && chan->_softhangup)
		return 0;

	ttlms = DUNDI_FLUFF_TIME + ttl * DUNDI_TTL_TIME;

	for (x=0;avoid[x];x++)
		rooteid = avoid[x];
	/* Now perform real check */
	memset(&dr, 0, sizeof(dr));
	if (pipe(dr.pfds)) {
		ast_log(LOG_WARNING, "pipe failed: %s\n" , strerror(errno));
		return -1;
	}
	dr.dr = result;
	dr.hmd = hmd;
	dr.maxcount = maxret;
	dr.expiration = *expiration;
	dr.cbypass = cbypass;
	dr.crc32 = avoid_crc32(avoid);
	strncpy(dr.dcontext, dcontext ? dcontext : "e164", sizeof(dr.dcontext) - 1);
	strncpy(dr.number, number, sizeof(dr.number) - 1);
	if (rooteid)
		dr.root_eid = *rooteid;
	res = register_request(&dr, &pending);
	if (res) {
		/* Already a request */
		if (rooteid && !dundi_eid_cmp(&dr.root_eid, &pending->root_eid)) {
			/* This is on behalf of someone else.  Go ahead and close this out since
			   they'll get their answer anyway. */
			ast_log(LOG_DEBUG, "Oooh, duplicate request for '%s@%s' for '%s'\n",
				dr.number,dr.dcontext,dundi_eid_to_str(eid_str, sizeof(eid_str), &dr.root_eid));
			close(dr.pfds[0]);
			close(dr.pfds[1]);
			return -2;
		} else {
			/* Wait for the cache to populate */
			ast_log(LOG_DEBUG, "Waiting for similar request for '%s@%s' for '%s'\n",
				dr.number,dr.dcontext,dundi_eid_to_str(eid_str, sizeof(eid_str), &pending->root_eid));
			gettimeofday(&start, NULL);
			while(check_request(pending) && (calc_ms(&start) < ttlms) && (!chan || !chan->_softhangup)) {
				/* XXX Would be nice to have a way to poll/select here XXX */
				usleep(1);
			}
			/* Continue on as normal, our cache should kick in */
		}
	}
	/* Create transactions */
	do {
		order = skipped;
		skipped = 0;
		foundcache = 0;
		build_transactions(&dr, ttl, order, &foundcache, &skipped, blockempty, cbypass, modeselect, skip, avoid, direct);
	} while (skipped && !foundcache && !dr.trans);
	/* If no TTL, abort and return 0 now after setting TTL expired hint.  Couldn't
	   do this earlier because we didn't know if we were going to have transactions
	   or not. */
	if (!ttl) {
		ast_set_flag_nonstd(hmd, DUNDI_HINT_TTL_EXPIRED);
		abort_request(&dr);
		unregister_request(&dr);
		close(dr.pfds[0]);
		close(dr.pfds[1]);
		return 0;
	}
		
	/* Optimize transactions */
	optimize_transactions(&dr, order);
	/* Actually perform transactions */
	discover_transactions(&dr);
	/* Wait for transaction to come back */
	gettimeofday(&start, NULL);
	while(dr.trans && (calc_ms(&start) < ttlms) && (!chan || !chan->_softhangup)) {
		ms = 100;
		ast_waitfor_n_fd(dr.pfds, 1, &ms, NULL);
	}
	if (chan && chan->_softhangup)
		ast_log(LOG_DEBUG, "Hrm, '%s' hungup before their query for %s@%s finished\n", chan->name, dr.number, dr.dcontext);
	cancel_request(&dr);
	unregister_request(&dr);
	res = dr.respcount;
	*expiration = dr.expiration;
	close(dr.pfds[0]);
	close(dr.pfds[1]);
	return res;
}

int dundi_lookup(struct dundi_result *result, int maxret, struct ast_channel *chan, const char *dcontext, const char *number, int cbypass)
{
	struct dundi_hint_metadata hmd;
	dundi_eid *avoid[1] = { NULL, };
	int direct[1] = { 0, };
	int expiration = DUNDI_DEFAULT_CACHE_TIME;
	memset(&hmd, 0, sizeof(hmd));
	hmd.flags = DUNDI_HINT_DONT_ASK | DUNDI_HINT_UNAFFECTED;
	return dundi_lookup_internal(result, maxret, chan, dcontext, number, dundi_ttl, 0, &hmd, &expiration, cbypass, 0, NULL, avoid, direct);
}

static void reschedule_precache(const char *number, const char *context, int expiration)
{
	int len;
	struct dundi_precache_queue *qe, *prev=NULL;
	ast_mutex_lock(&pclock);
	qe = pcq;
	while(qe) {
		if (!strcmp(number, qe->number) && !strcasecmp(context, qe->context)) {
			if (prev)
				prev->next = qe->next;
			else
				pcq = qe->next;
			qe->next = NULL;
			break;
		}
		prev = qe;
		qe = qe->next;
	};
	if (!qe) {
		len = sizeof(struct dundi_precache_queue);
		len += strlen(number) + 1;
		len += strlen(context) + 1;
		qe = malloc(len);
		if (qe) {
			memset(qe, 0, len);
			strcpy(qe->number, number);
			qe->context = qe->number + strlen(number) + 1;
			strcpy(qe->context, context);
		}
	}
	time(&qe->expiration);
	qe->expiration += expiration;
	prev = pcq;
	if (prev) {
		while(prev->next && (prev->next->expiration <= qe->expiration))
			prev = prev->next;
		qe->next = prev->next;
		prev->next = qe;
	} else
		pcq = qe;
	ast_mutex_unlock(&pclock);
	
}

static void dundi_precache_full(void)
{
	struct dundi_mapping *cur;
	struct ast_context *con;
	struct ast_exten *e;
	cur = mappings;
	while(cur) {
		ast_log(LOG_NOTICE, "Should precache context '%s'\n", cur->dcontext);
		ast_lock_contexts();
		con = ast_walk_contexts(NULL);
		while(con) {
			if (!strcasecmp(cur->lcontext, ast_get_context_name(con))) {
				/* Found the match, now queue them all up */
				ast_lock_context(con);
				e = ast_walk_context_extensions(con, NULL);
				while(e) {
					reschedule_precache(ast_get_extension_name(e), cur->dcontext, 0);
					e = ast_walk_context_extensions(con, e);
				}
				ast_unlock_context(con);
			}
			con = ast_walk_contexts(con);
		}
		ast_unlock_contexts();
		cur = cur->next;
	}
}

static int dundi_precache_internal(const char *context, const char *number, int ttl, dundi_eid *avoids[])
{
	struct dundi_request dr;
	struct dundi_hint_metadata hmd;
	struct dundi_result dr2[MAX_RESULTS];
	struct timeval start;
	struct dundi_mapping *maps=NULL, *cur;
	int nummaps;
	int foundanswers;
	int foundcache, skipped, ttlms, ms;
	if (!context)
		context = "e164";
	ast_log(LOG_DEBUG, "Precache internal (%s@%s)!\n", number, context);

	ast_mutex_lock(&peerlock);
	nummaps = 0;
	cur = mappings;
	while(cur) {
		if (!strcasecmp(cur->dcontext, context))
			nummaps++;
		cur = cur->next;
	}
	if (nummaps) {
		maps = alloca(nummaps * sizeof(struct dundi_mapping));
		nummaps = 0;
		if (maps) {
			cur = mappings;
			while(cur) {
				if (!strcasecmp(cur->dcontext, context))
					maps[nummaps++] = *cur;
				cur = cur->next;
			}
		}
	}
	ast_mutex_unlock(&peerlock);
	if (!nummaps || !maps)
		return -1;
	ttlms = DUNDI_FLUFF_TIME + ttl * DUNDI_TTL_TIME;
	memset(&dr2, 0, sizeof(dr2));
	memset(&dr, 0, sizeof(dr));
	memset(&hmd, 0, sizeof(hmd));
	dr.dr = dr2;
	strncpy(dr.number, number, sizeof(dr.number) - 1);
	strncpy(dr.dcontext, context ? context : "e164", sizeof(dr.dcontext) - 1);
	dr.maxcount = MAX_RESULTS;
	dr.expiration = DUNDI_DEFAULT_CACHE_TIME;
	dr.hmd = &hmd;
	dr.pfds[0] = dr.pfds[1] = -1;
	pipe(dr.pfds);
	build_transactions(&dr, ttl, 0, &foundcache, &skipped, 0, 1, 1, NULL, avoids, NULL);
	optimize_transactions(&dr, 0);
	foundanswers = 0;
	precache_transactions(&dr, maps, nummaps, &dr.expiration, &foundanswers);
	if (foundanswers) {
		if (dr.expiration > 0) 
			reschedule_precache(dr.number, dr.dcontext, dr.expiration);
		else
			ast_log(LOG_NOTICE, "Weird, expiration = %d, but need to precache for %s@%s?!\n", dr.expiration, dr.number, dr.dcontext);
	}
	gettimeofday(&start, NULL);
	while(dr.trans && (calc_ms(&start) < ttlms)) {
		if (dr.pfds[0] > -1) {
			ms = 100;
			ast_waitfor_n_fd(dr.pfds, 1, &ms, NULL);
		} else
			usleep(1);
	}
	cancel_request(&dr);
	if (dr.pfds[0] > -1) {
		close(dr.pfds[0]);
		close(dr.pfds[1]);
	}
	return 0;
}

int dundi_precache(const char *context, const char *number)
{
	dundi_eid *avoid[1] = { NULL, };
	return dundi_precache_internal(context, number, dundi_ttl, avoid);
}

static int dundi_query_eid_internal(struct dundi_entity_info *dei, const char *dcontext, dundi_eid *eid, struct dundi_hint_metadata *hmd, int ttl, int blockempty, dundi_eid *avoid[])
{
	int res;
	struct dundi_request dr;
	dundi_eid *rooteid=NULL;
	int x;
	int ttlms;
	int skipped=0;
	int foundcache=0;
	struct timeval start;
	
	ttlms = DUNDI_FLUFF_TIME + ttl * DUNDI_TTL_TIME;

	for (x=0;avoid[x];x++)
		rooteid = avoid[x];
	/* Now perform real check */
	memset(&dr, 0, sizeof(dr));
	dr.hmd = hmd;
	dr.dei = dei;
	dr.pfds[0] = dr.pfds[1] = -1;
	strncpy(dr.dcontext, dcontext ? dcontext : "e164", sizeof(dr.dcontext) - 1);
	memcpy(&dr.query_eid, eid, sizeof(dr.query_eid));
	if (rooteid)
		dr.root_eid = *rooteid;
	/* Create transactions */
	build_transactions(&dr, ttl, 9999, &foundcache, &skipped, blockempty, 0, 0, NULL, avoid, NULL);

	/* If no TTL, abort and return 0 now after setting TTL expired hint.  Couldn't
	   do this earlier because we didn't know if we were going to have transactions
	   or not. */
	if (!ttl) {
		ast_set_flag_nonstd(hmd, DUNDI_HINT_TTL_EXPIRED);
		return 0;
	}
		
	/* Optimize transactions */
	optimize_transactions(&dr, 9999);
	/* Actually perform transactions */
	query_transactions(&dr);
	/* Wait for transaction to come back */
	gettimeofday(&start, NULL);
	while(dr.trans && (calc_ms(&start) < ttlms))
		usleep(1);
	res = dr.respcount;
	return res;
}

int dundi_query_eid(struct dundi_entity_info *dei, const char *dcontext, dundi_eid eid)
{
	dundi_eid *avoid[1] = { NULL, };
	struct dundi_hint_metadata hmd;
	memset(&hmd, 0, sizeof(hmd));
	return dundi_query_eid_internal(dei, dcontext, &eid, &hmd, dundi_ttl, 0, avoid);
}

static int dundi_lookup_exec(struct ast_channel *chan, void *data)
{
	char *tmp;
	char *context = NULL;
	char *opts;
	int res = 0;
	int results = 0;
	int x;
	int bypass = 0;
	struct localuser *u;
	struct dundi_result dr[MAX_RESULTS];

	if (!data || !strlen(data)) {
		ast_log(LOG_WARNING, "DUNDiLookup requires an argument (number)\n");
		return 0;
	}
	LOCAL_USER_ADD(u);
	tmp = ast_strdupa(data);
	if (tmp) {
		context = strchr(tmp, '|');
		if (context) {
			*context = '\0';
			context++;
			opts = strchr(context, '|');
			if (opts) {
				*opts = '\0';
				opts++;
			}
		} else
			opts = NULL;
		if (!context || !strlen(context))
			context = "e164";
		if (!opts)
			opts = "";
		
	}
	results = dundi_lookup(dr, MAX_RESULTS, NULL, context, tmp, bypass);
	if (results > 0) {
        sort_results(dr, results);
        for (x=0;x<results;x++) {
			if (ast_test_flag(dr + x, DUNDI_FLAG_EXISTS)) {
				pbx_builtin_setvar_helper(chan, "DUNDTECH", dr[x].tech);
				pbx_builtin_setvar_helper(chan, "DUNDDEST", dr[x].dest);
				break;
            }
        }
    } else {
		if (ast_exists_extension(chan, chan->context, chan->exten, chan->priority + 101, chan->cid.cid_num))
            chan->priority += 100;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}


static void mark_peers(void)
{
	struct dundi_peer *peer;
	ast_mutex_lock(&peerlock);
	peer = peers;
	while(peer) {
		peer->dead = 1;
		peer = peer->next;
	}
	ast_mutex_unlock(&peerlock);
}

static void mark_mappings(void)
{
	struct dundi_mapping *map;
	ast_mutex_lock(&peerlock);
	map = mappings;
	while(map) {
		map->dead = 1;
		map = map->next;
	}
	ast_mutex_unlock(&peerlock);
}

static void destroy_permissions(struct permission *p)
{
	struct permission *prev;
	while(p) {
		prev = p;
		p = p->next;
		free(prev);
	}
}

static void destroy_peer(struct dundi_peer *peer)
{
	if (peer->registerid > -1)
		ast_sched_del(sched, peer->registerid);
	if (peer->regtrans)
		destroy_trans(peer->regtrans, 0);
	if (peer->keypending)
		destroy_trans(peer->keypending, 0);
	if (peer->qualifyid > -1)
		ast_sched_del(sched, peer->qualifyid);
	destroy_permissions(peer->permit);
	destroy_permissions(peer->include);
	free(peer);
}

static void destroy_map(struct dundi_mapping *map)
{
	free(map);
}

static void prune_peers(void)
{
	struct dundi_peer *peer, *prev, *next;
	ast_mutex_lock(&peerlock);
	peer = peers;
	prev = NULL;
	while(peer) {
		next = peer->next;
		if (peer->dead) {
			if (prev)
				prev->next = peer->next;
			else
				peers = peer->next;
			destroy_peer(peer);
		} else
			prev = peer;
		peer = next;
	}
	ast_mutex_unlock(&peerlock);
}

static void prune_mappings(void)
{
	struct dundi_mapping *map, *prev, *next;
	ast_mutex_lock(&peerlock);
	map = mappings;
	prev = NULL;
	while(map) {
		next = map->next;
		if (map->dead) {
			if (prev)
				prev->next = map->next;
			else
				mappings = map->next;
			destroy_map(map);
		} else
			prev = map;
		map = next;
	}
	ast_mutex_unlock(&peerlock);
}

static struct permission *append_permission(struct permission *p, char *s, int allow)
{
	struct permission *start;
	start = p;
	if (p) {
		while(p->next)
			p = p->next;
	}
	if (p) {
		p->next = malloc(sizeof(struct permission) + strlen(s) + 1);
		p = p->next;
	} else {
		p = malloc(sizeof(struct permission) + strlen(s) + 1);
	}
	if (p) {
		memset(p, 0, sizeof(struct permission));
		memcpy(p->name, s, strlen(s) + 1);
		p->allow = allow;
	}
	return start ? start : p;
}

#define MAX_OPTS 128

static void build_mapping(char *name, char *value)
{
	char *t, *fields[MAX_OPTS];
	struct dundi_mapping *map;
	int x;
	int y;
	t = ast_strdupa(value);
	if (t) {
		map = mappings;
		while(map) {
			/* Find a double match */
			if (!strcasecmp(map->dcontext, name) && 
				(!strncasecmp(map->lcontext, value, strlen(map->lcontext)) && 
				  (!value[strlen(map->lcontext)] || 
				   (value[strlen(map->lcontext)] == ','))))
				break;
			map = map->next;
		}
		if (!map) {
			map = malloc(sizeof(struct dundi_mapping));
			if (map) {
				memset(map, 0, sizeof(struct dundi_mapping));
				map->next = mappings;
				mappings = map;
				map->dead = 1;
			}
		}
		if (map) {
			map->options = 0;
			memset(fields, 0, sizeof(fields));
			x = 0;
			while(t && x < MAX_OPTS) {
				fields[x++] = t;
				t = strchr(t, ',');
				if (t) {
					*t = '\0';
					t++;
				}
			} /* Russell was here, arrrr! */
			if ((x == 1) && ast_strlen_zero(fields[0])) {
				/* Placeholder mapping */
				strncpy(map->dcontext, name, sizeof(map->dcontext) - 1);
				map->dead = 0;
			} else if (x >= 4) {
				strncpy(map->dcontext, name, sizeof(map->dcontext) - 1);
				strncpy(map->lcontext, fields[0], sizeof(map->lcontext) - 1);
				if ((sscanf(fields[1], "%d", &map->weight) == 1) && (map->weight >= 0) && (map->weight < 60000)) {
					strncpy(map->dest, fields[3], sizeof(map->dest) - 1);
					if ((map->tech = str2tech(fields[2]))) {
						map->dead = 0;
					}
				} else {
					ast_log(LOG_WARNING, "Invalid weight '%s' specified, deleting entry '%s/%s'\n", fields[1], map->dcontext, map->lcontext);
				}
				for (y=4;y<x;y++) {
					if (!strcasecmp(fields[y], "nounsolicited"))
						map->options |= DUNDI_FLAG_NOUNSOLICITED;
					else if (!strcasecmp(fields[y], "nocomunsolicit"))
						map->options |= DUNDI_FLAG_NOCOMUNSOLICIT;
					else if (!strcasecmp(fields[y], "residential"))
						map->options |= DUNDI_FLAG_RESIDENTIAL;
					else if (!strcasecmp(fields[y], "commercial"))
						map->options |= DUNDI_FLAG_COMMERCIAL;
					else if (!strcasecmp(fields[y], "mobile"))
						map->options |= DUNDI_FLAG_MOBILE;
					else if (!strcasecmp(fields[y], "nopartial"))
						map->options |= DUNDI_FLAG_INTERNAL_NOPARTIAL;
					else
						ast_log(LOG_WARNING, "Don't know anything about option '%s'\n", fields[y]);
				}
			} else 
				ast_log(LOG_WARNING, "Expected at least %d arguments in map, but got only %d\n", 4, x);
		}
	}
}

static int do_register(void *data)
{
	struct dundi_ie_data ied;
	struct dundi_peer *peer = data;
	char eid_str[20];
	char eid_str2[20];
	/* Called with peerlock already held */
	ast_log(LOG_DEBUG, "Register us as '%s' to '%s'\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->us_eid), dundi_eid_to_str(eid_str2, sizeof(eid_str2), &peer->eid));
	peer->registerid = ast_sched_add(sched, default_expiration * 1000, do_register, data);
	/* Destroy old transaction if there is one */
	if (peer->regtrans)
		destroy_trans(peer->regtrans, 0);
	peer->regtrans = create_transaction(peer);
	if (peer->regtrans) {
		ast_set_flag(peer->regtrans, FLAG_ISREG);
		memset(&ied, 0, sizeof(ied));
		dundi_ie_append_short(&ied, DUNDI_IE_VERSION, DUNDI_DEFAULT_VERSION);
		dundi_ie_append_eid(&ied, DUNDI_IE_EID, &peer->regtrans->us_eid);
		dundi_ie_append_short(&ied, DUNDI_IE_EXPIRATION, default_expiration);
		dundi_send(peer->regtrans, DUNDI_COMMAND_REGREQ, 0, 0, &ied);
		
	} else
		ast_log(LOG_NOTICE, "Unable to create new transaction for registering to '%s'!\n", dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));

	return 0;
}

static int do_qualify(void *data)
{
	struct dundi_peer *peer;
	peer = data;
	peer->qualifyid = -1;
	qualify_peer(peer, 0);
	return 0;
}

static void qualify_peer(struct dundi_peer *peer, int schedonly)
{
	int when;
	if (peer->qualifyid > -1)
		ast_sched_del(sched, peer->qualifyid);
	peer->qualifyid = -1;
	if (peer->qualtrans)
		destroy_trans(peer->qualtrans, 0);
	peer->qualtrans = NULL;
	if (peer->maxms > 0) {
		when = 60000;
		if (peer->lastms < 0)
			when = 10000;
		if (schedonly)
			when = 5000;
		peer->qualifyid = ast_sched_add(sched, when, do_qualify, peer);
		if (!schedonly)
			peer->qualtrans = create_transaction(peer);
		if (peer->qualtrans) {
			gettimeofday(&peer->qualtx, NULL);
			ast_set_flag(peer->qualtrans, FLAG_ISQUAL);
			dundi_send(peer->qualtrans, DUNDI_COMMAND_NULL, 0, 1, NULL);
		}
	}
}
static void populate_addr(struct dundi_peer *peer, dundi_eid *eid)
{
	char data[256];
	char *c;
	int port, expire;
	char eid_str[20];
	dundi_eid_to_str(eid_str, sizeof(eid_str), eid);
	if (!ast_db_get("dundi/dpeers", eid_str, data, sizeof(data))) {
		c = strchr(data, ':');
		if (c) {
			*c = '\0';
			c++;
			if (sscanf(c, "%d:%d", &port, &expire) == 2) {
				/* Got it! */
				inet_aton(data, &peer->addr.sin_addr);
				peer->addr.sin_family = AF_INET;
				peer->addr.sin_port = htons(port);
				peer->registerexpire = ast_sched_add(sched, (expire + 10) * 1000, do_register_expire, peer);
			}
		}
	}
}


static void build_peer(dundi_eid *eid, struct ast_variable *v, int *globalpcmode)
{
	struct dundi_peer *peer;
	struct ast_hostent he;
	struct hostent *hp;
	dundi_eid testeid;
	int needregister=0;
	char eid_str[20];

	ast_mutex_lock(&peerlock);
	peer = peers;
	while(peer) {
		if (!dundi_eid_cmp(&peer->eid, eid)) {	
			break;
		}
		peer = peer->next;
	}
	if (!peer) {
		/* Add us into the list */
		peer = malloc(sizeof(struct dundi_peer));
		if (peer) {
			memset(peer, 0, sizeof(struct dundi_peer));
			peer->registerid = -1;
			peer->registerexpire = -1;
			peer->qualifyid = -1;
			peer->addr.sin_family = AF_INET;
			peer->addr.sin_port = htons(DUNDI_PORT);
			populate_addr(peer, eid);
			peer->next = peers;
			peers = peer;
		}
	}
	if (peer) {
		peer->dead = 0;
		peer->eid = *eid;
		peer->us_eid = global_eid;
		destroy_permissions(peer->permit);
		destroy_permissions(peer->include);
		peer->permit = NULL;
		peer->include = NULL;
		if (peer->registerid > -1)
			ast_sched_del(sched, peer->registerid);
		peer->registerid = -1;
		while(v) {
			if (!strcasecmp(v->name, "inkey")) {
				strncpy(peer->inkey, v->value, sizeof(peer->inkey) - 1);
			} else if (!strcasecmp(v->name, "outkey")) {
				strncpy(peer->outkey, v->value, sizeof(peer->outkey) - 1);
			} else if (!strcasecmp(v->name, "host")) {
				if (!strcasecmp(v->value, "dynamic")) {
					peer->dynamic = 1;
				} else {
					hp = ast_gethostbyname(v->value, &he);
					if (hp) {
						memcpy(&peer->addr.sin_addr, hp->h_addr, sizeof(peer->addr.sin_addr));
						peer->dynamic = 0;
					} else {
						ast_log(LOG_WARNING, "Unable to find host '%s' at line %d\n", v->value, v->lineno);
						peer->dead = 1;
					}
				}
			} else if (!strcasecmp(v->name, "ustothem")) {
				if (!dundi_str_to_eid(&testeid, v->value))
					peer->us_eid = testeid;
				else
					ast_log(LOG_WARNING, "'%s' is not a valid DUNDi Entity Identifier at line %d\n", v->value, v->lineno);
			} else if (!strcasecmp(v->name, "include")) {
				peer->include = append_permission(peer->include, v->value, 1);
			} else if (!strcasecmp(v->name, "permit")) {
				peer->permit = append_permission(peer->permit, v->value, 1);
			} else if (!strcasecmp(v->name, "noinclude")) {
				peer->include = append_permission(peer->include, v->value, 0);
			} else if (!strcasecmp(v->name, "deny")) {
				peer->permit = append_permission(peer->permit, v->value, 0);
			} else if (!strcasecmp(v->name, "register")) {
				needregister = ast_true(v->value);
			} else if (!strcasecmp(v->name, "order")) {
				if (!strcasecmp(v->value, "primary"))
					peer->order = 0;
				else if (!strcasecmp(v->value, "secondary"))
					peer->order = 1;
				else if (!strcasecmp(v->value, "tertiary"))
					peer->order = 2;
				else if (!strcasecmp(v->value, "quartiary"))
					peer->order = 3;
				else {
					ast_log(LOG_WARNING, "'%s' is not a valid order, should be primary, secondary, tertiary or quartiary at line %d\n", v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "qualify")) {
				if (!strcasecmp(v->value, "no")) {
					peer->maxms = 0;
				} else if (!strcasecmp(v->value, "yes")) {
					peer->maxms = DEFAULT_MAXMS;
				} else if (sscanf(v->value, "%d", &peer->maxms) != 1) {
					ast_log(LOG_WARNING, "Qualification of peer '%s' should be 'yes', 'no', or a number of milliseconds at line %d of dundi.conf\n", 
						dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid), v->lineno);
					peer->maxms = 0;
				}
			} else if (!strcasecmp(v->name, "model")) {
				if (!strcasecmp(v->value, "inbound"))
					peer->model = DUNDI_MODEL_INBOUND;
				else if (!strcasecmp(v->value, "outbound")) 
					peer->model = DUNDI_MODEL_OUTBOUND;
				else if (!strcasecmp(v->value, "symmetric"))
					peer->model = DUNDI_MODEL_SYMMETRIC;
				else if (!strcasecmp(v->value, "none"))
					peer->model = 0;
				else {
					ast_log(LOG_WARNING, "Unknown model '%s', should be 'none', 'outbound', 'inbound', or 'symmetric' at line %d\n", 
						v->value, v->lineno);
				}
			} else if (!strcasecmp(v->name, "precache")) {
				if (!strcasecmp(v->value, "inbound"))
					peer->pcmodel = DUNDI_MODEL_INBOUND;
				else if (!strcasecmp(v->value, "outbound")) 
					peer->pcmodel = DUNDI_MODEL_OUTBOUND;
				else if (!strcasecmp(v->value, "symmetric"))
					peer->pcmodel = DUNDI_MODEL_SYMMETRIC;
				else if (!strcasecmp(v->value, "none"))
					peer->pcmodel = 0;
				else {
					ast_log(LOG_WARNING, "Unknown pcmodel '%s', should be 'none', 'outbound', 'inbound', or 'symmetric' at line %d\n", 
						v->value, v->lineno);
				}
			}
			v = v->next;
		}
		(*globalpcmode) |= peer->pcmodel;
		if (!peer->model && !peer->pcmodel) {
			ast_log(LOG_WARNING, "Peer '%s' lacks a model or pcmodel, discarding!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			peer->dead = 1;
		} else if ((peer->model & DUNDI_MODEL_INBOUND) && (peer->pcmodel & DUNDI_MODEL_OUTBOUND)) {
			ast_log(LOG_WARNING, "Peer '%s' may not be both inbound/symmetric model and outbound/symmetric precache model, discarding!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			peer->dead = 1;
		} else if ((peer->model & DUNDI_MODEL_OUTBOUND) && (peer->pcmodel & DUNDI_MODEL_INBOUND)) {
			ast_log(LOG_WARNING, "Peer '%s' may not be both outbound/symmetric model and inbound/symmetric precache model, discarding!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
			peer->dead = 1;
		} else if (peer->include && !(peer->model & DUNDI_MODEL_OUTBOUND) && !(peer->pcmodel & DUNDI_MODEL_INBOUND)) {
			ast_log(LOG_WARNING, "Peer '%s' is supposed to be included in outbound searches but isn't an outbound peer or inbound precache!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
		} else if (peer->permit && !(peer->model & DUNDI_MODEL_INBOUND) && !(peer->pcmodel & DUNDI_MODEL_OUTBOUND)) {
			ast_log(LOG_WARNING, "Peer '%s' is supposed to have permission for some inbound searches but isn't an inbound peer or outbound precache!\n", 
				dundi_eid_to_str(eid_str, sizeof(eid_str), &peer->eid));
		} else { 
			if (needregister) {
				peer->registerid = ast_sched_add(sched, 2000, do_register, peer);
			}
			qualify_peer(peer, 1);
		}
	}
	ast_mutex_unlock(&peerlock);
}

static int dundi_helper(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *data, int flag)
{
	struct dundi_result results[MAX_RESULTS];
	int res;
	int x;
	int found = 0;
	if (!strncasecmp(context, "macro-", 6)) {
		if (!chan) {	
			ast_log(LOG_NOTICE, "Can't use macro mode without a channel!\n");
			return -1;
		}
		/* If done as a macro, use macro extension */
		if (!strcasecmp(exten, "s")) {
			exten = pbx_builtin_getvar_helper(chan, "ARG1");
			if (!exten || ast_strlen_zero(exten))
				exten = chan->macroexten;
			if (!exten || ast_strlen_zero(exten))
				exten = chan->exten;
			if (!exten || ast_strlen_zero(exten)) {	
				ast_log(LOG_WARNING, "Called in Macro mode with no ARG1 or MACRO_EXTEN?\n");
				return -1;
			}
		}
		if (!data || ast_strlen_zero(data))
			data = "e164";
	} else {
		if (!data || ast_strlen_zero(data))
			data = context;
	}
	res = dundi_lookup(results, MAX_RESULTS, chan, data, exten, 0);
	for (x=0;x<res;x++) {
		if (ast_test_flag(results + x, flag))
			found++;
	}
	if (found >= priority)
		return 1;
	return 0;
}

static int dundi_exists(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	return dundi_helper(chan, context, exten, priority, data, DUNDI_FLAG_EXISTS);
}

static int dundi_canmatch(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	return dundi_helper(chan, context, exten, priority, data, DUNDI_FLAG_CANMATCH);
}

static int dundi_exec(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, int newstack, const char *data)
{
	struct dundi_result results[MAX_RESULTS];
	int res;
	int x=0;
	char req[1024];
	struct ast_app *dial;
	
	if (!strncasecmp(context, "macro-", 6)) {
		if (!chan) {	
			ast_log(LOG_NOTICE, "Can't use macro mode without a channel!\n");
			return -1;
		}
		/* If done as a macro, use macro extension */
		if (!strcasecmp(exten, "s")) {
			exten = pbx_builtin_getvar_helper(chan, "ARG1");
			if (!exten || ast_strlen_zero(exten))
				exten = chan->macroexten;
			if (!exten || ast_strlen_zero(exten))
				exten = chan->exten;
			if (!exten || ast_strlen_zero(exten)) {	
				ast_log(LOG_WARNING, "Called in Macro mode with no ARG1 or MACRO_EXTEN?\n");
				return -1;
			}
		}
		if (!data || ast_strlen_zero(data))
			data = "e164";
	} else {
		if (!data || ast_strlen_zero(data))
			data = context;
	}
	res = dundi_lookup(results, MAX_RESULTS, chan, data, exten, 0);
	if (res > 0) {
		sort_results(results, res);
		for (x=0;x<res;x++) {
			if (ast_test_flag(results + x, DUNDI_FLAG_EXISTS)) {
				if (!--priority)
					break;
			}
		}
	}
	if (x < res) {
		/* Got a hit! */
		snprintf(req, sizeof(req), "%s/%s", results[x].tech, results[x].dest);
		dial = pbx_findapp("Dial");
		if (dial)
			res = pbx_exec(chan, dial, req, newstack);
	} else
		res = -1;
	return res;
}

static int dundi_matchmore(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	return dundi_helper(chan, context, exten, priority, data, DUNDI_FLAG_MATCHMORE);
}

static struct ast_switch dundi_switch =
{
        name:                   "DUNDi",
        description:    		"DUNDi Discovered Dialplan Switch",
        exists:                 dundi_exists,
        canmatch:               dundi_canmatch,
        exec:                   dundi_exec,
        matchmore:              dundi_matchmore,
};

static int set_config(char *config_file, struct sockaddr_in* sin)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	char *cat;
	int format;
	int x;
	char hn[MAXHOSTNAMELEN]="";
	struct ast_hostent he;
	struct hostent *hp;
	struct sockaddr_in sin2;
	static int last_port = 0;
	int globalpcmodel = 0;
	dundi_eid testeid;

	dundi_ttl = DUNDI_DEFAULT_TTL;
	cfg = ast_config_load(config_file);
	
	
	if (!cfg) {
		ast_log(LOG_ERROR, "Unable to load config %s\n", config_file);
		return -1;
	}
	ipaddr[0] = '\0';
	if (!gethostname(hn, sizeof(hn)-1)) {
		hp = ast_gethostbyname(hn, &he);
		if (hp) {
			memcpy(&sin2.sin_addr, hp->h_addr, sizeof(sin2.sin_addr));
			ast_inet_ntoa(ipaddr, sizeof(ipaddr), sin2.sin_addr);
		} else
			ast_log(LOG_WARNING, "Unable to look up host '%s'\n", hn);
	} else
		ast_log(LOG_WARNING, "Unable to get host name!\n");
	ast_mutex_lock(&peerlock);
	reset_global_eid();
	global_storehistory = 0;
	strncpy(secretpath, "dundi", sizeof(secretpath) - 1);
	v = ast_variable_browse(cfg, "general");
	while(v) {
		if (!strcasecmp(v->name, "port")){ 
			sin->sin_port = ntohs(atoi(v->value));
			if(last_port==0){
				last_port=sin->sin_port;
			} else if(sin->sin_port != last_port)
				ast_log(LOG_WARNING, "change to port ignored until next asterisk re-start\n");
		} else if (!strcasecmp(v->name, "bindaddr")) {
			struct hostent *hp;
			struct ast_hostent he;
			hp = ast_gethostbyname(v->value, &he);
			if (hp) {
				memcpy(&sin->sin_addr, hp->h_addr, sizeof(sin->sin_addr));
			} else
				ast_log(LOG_WARNING, "Invalid host/IP '%s'\n", v->value);
		} else if (!strcasecmp(v->name, "authdebug")) {
			authdebug = ast_true(v->value);
		} else if (!strcasecmp(v->name, "ttl")) {
			if ((sscanf(v->value, "%d", &x) == 1) && (x > 0) && (x < DUNDI_DEFAULT_TTL)) {
				dundi_ttl = x;
			} else {
				ast_log(LOG_WARNING, "'%s' is not a valid TTL at line %d, must be number from 1 to %d\n",
					v->value, v->lineno, DUNDI_DEFAULT_TTL);
			}
		} else if (!strcasecmp(v->name, "autokill")) {
			if (sscanf(v->value, "%d", &x) == 1) {
				if (x >= 0)
					global_autokilltimeout = x;
				else
					ast_log(LOG_NOTICE, "Nice try, but autokill has to be >0 or 'yes' or 'no' at line %d\n", v->lineno);
			} else if (ast_true(v->value)) {
				global_autokilltimeout = DEFAULT_MAXMS;
			} else {
				global_autokilltimeout = 0;
			}
		} else if (!strcasecmp(v->name, "entityid")) {
			if (!dundi_str_to_eid(&testeid, v->value))
				global_eid = testeid;
			else
				ast_log(LOG_WARNING, "Invalid global endpoint identifier '%s' at line %d\n", v->value, v->lineno);
		} else if (!strcasecmp(v->name, "tos")) {
			if (sscanf(v->value, "%d", &format) == 1)
				tos = format & 0xff;
			else if (!strcasecmp(v->value, "lowdelay"))
				tos = IPTOS_LOWDELAY;
			else if (!strcasecmp(v->value, "throughput"))
				tos = IPTOS_THROUGHPUT;
			else if (!strcasecmp(v->value, "reliability"))
				tos = IPTOS_RELIABILITY;
#if !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(SOLARIS)
			else if (!strcasecmp(v->value, "mincost"))
				tos = IPTOS_MINCOST;
#endif
			else if (!strcasecmp(v->value, "none"))
				tos = 0;
			else
#if !defined(__NetBSD__) && !defined(__OpenBSD__) && !defined(SOLARIS)
				ast_log(LOG_WARNING, "Invalid tos value at line %d, should be 'lowdelay', 'throughput', 'reliability', 'mincost', or 'none'\n", v->lineno);
#else
				ast_log(LOG_WARNING, "Invalid tos value at line %d, should be 'lowdelay', 'throughput', 'reliability', or 'none'\n", v->lineno);
#endif
		} else if (!strcasecmp(v->name, "department")) {
			strncpy(dept, v->value, sizeof(dept) - 1);
		} else if (!strcasecmp(v->name, "organization")) {
			strncpy(org, v->value, sizeof(org) - 1);
		} else if (!strcasecmp(v->name, "locality")) {
			strncpy(locality, v->value, sizeof(locality) - 1);
		} else if (!strcasecmp(v->name, "stateprov")) {
			strncpy(stateprov, v->value, sizeof(stateprov) - 1);
		} else if (!strcasecmp(v->name, "country")) {
			strncpy(country, v->value, sizeof(country) - 1);
		} else if (!strcasecmp(v->name, "email")) {
			strncpy(email, v->value, sizeof(email) - 1);
		} else if (!strcasecmp(v->name, "phone")) {
			strncpy(phone, v->value, sizeof(phone) - 1);
		} else if (!strcasecmp(v->name, "storehistory")) {
			global_storehistory = ast_true(v->value);
		}
		v = v->next;
	}
	ast_mutex_unlock(&peerlock);
	mark_mappings();
	v = ast_variable_browse(cfg, "mappings");
	while(v) {
		build_mapping(v->name, v->value);
		v = v->next;
	}
	prune_mappings();
	mark_peers();
	cat = ast_category_browse(cfg, NULL);
	while(cat) {
		if (strcasecmp(cat, "general") && strcasecmp(cat, "mappings")) {
			/* Entries */
			if (!dundi_str_to_eid(&testeid, cat))
				build_peer(&testeid, ast_variable_browse(cfg, cat), &globalpcmodel);
			else
				ast_log(LOG_NOTICE, "Ignoring invalid EID entry '%s'\n", cat);
		}
		cat = ast_category_browse(cfg, cat);
	}
	prune_peers();
	ast_config_destroy(cfg);
	load_password();
	if (globalpcmodel & DUNDI_MODEL_OUTBOUND)
		dundi_precache_full();
	return 0;
}

int unload_module(void)
{
	int res;
	STANDARD_HANGUP_LOCALUSERS;
	ast_cli_unregister(&cli_debug);
	ast_cli_unregister(&cli_store_history);
	ast_cli_unregister(&cli_flush);
	ast_cli_unregister(&cli_no_debug);
	ast_cli_unregister(&cli_no_store_history);
	ast_cli_unregister(&cli_show_peers);
	ast_cli_unregister(&cli_show_entityid);
	ast_cli_unregister(&cli_show_trans);
	ast_cli_unregister(&cli_show_requests);
	ast_cli_unregister(&cli_show_mappings);
	ast_cli_unregister(&cli_show_precache);
	ast_cli_unregister(&cli_show_peer);
	ast_cli_unregister(&cli_lookup);
	ast_cli_unregister(&cli_precache);
	ast_cli_unregister(&cli_queryeid);
	ast_unregister_switch(&dundi_switch);
	res = ast_unregister_application(app);
	return res;
}

int reload(void)
{
	struct sockaddr_in sin;
	set_config("dundi.conf",&sin);
	return 0;
}

int load_module(void)
{
	int res=0;
	struct sockaddr_in sin;
	char iabuf[INET_ADDRSTRLEN];
	
	dundi_set_output(dundi_debug_output);
	dundi_set_error(dundi_error_output);
	
	sin.sin_family = AF_INET;
	sin.sin_port = ntohs(DUNDI_PORT);
	sin.sin_addr.s_addr = INADDR_ANY;

	/* Make a UDP socket */
	io = io_context_create();
	sched = sched_context_create();
	
	if (!io || !sched) {
		ast_log(LOG_ERROR, "Out of memory\n");
		return -1;
	}

	ast_cli_register(&cli_debug);
	ast_cli_register(&cli_store_history);
	ast_cli_register(&cli_flush);
	ast_cli_register(&cli_no_debug);
	ast_cli_register(&cli_no_store_history);
	ast_cli_register(&cli_show_peers);
	ast_cli_register(&cli_show_entityid);
	ast_cli_register(&cli_show_trans);
	ast_cli_register(&cli_show_requests);
	ast_cli_register(&cli_show_mappings);
	ast_cli_register(&cli_show_precache);
	ast_cli_register(&cli_show_peer);
	ast_cli_register(&cli_lookup);
	ast_cli_register(&cli_precache);
	ast_cli_register(&cli_queryeid);
	if (ast_register_switch(&dundi_switch))
		ast_log(LOG_ERROR, "Unable to register DUNDi switch\n");

	set_config("dundi.conf",&sin);

	netsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	
	if (netsocket < 0) {
		ast_log(LOG_ERROR, "Unable to create network socket: %s\n", strerror(errno));
		return -1;
	}
	if (bind(netsocket,(struct sockaddr *)&sin, sizeof(sin))) {
		ast_log(LOG_ERROR, "Unable to bind to %s port %d: %s\n", ast_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port), strerror(errno));
		return -1;
	}

	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Using TOS bits %d\n", tos);

	if (setsockopt(netsocket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos))) 
		ast_log(LOG_WARNING, "Unable to set TOS to %d\n", tos);
	
	if (!res) {
		res = start_network_thread();
		if (option_verbose > 1) 
			ast_verbose(VERBOSE_PREFIX_2 "DUNDi Ready and Listening on %s port %d\n", ast_inet_ntoa(iabuf, sizeof(iabuf), sin.sin_addr), ntohs(sin.sin_port));
	} else {
		ast_log(LOG_ERROR, "Unable to start network thread\n");
		close(netsocket);
	}
	res = ast_register_application(app, dundi_lookup_exec, synopsis, descrip);
	return 0;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	/* XXX DUNDi cannot be unloaded XXX */
	return 1;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
