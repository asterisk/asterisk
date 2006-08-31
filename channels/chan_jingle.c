/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Matt O'Gorman <mogorman@digium.com>
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

/*! \file
 *
 * \author Matt O'Gorman <mogorman@digium.com>
 *
 * \brief Jingle Channel Driver
 * 
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>iksemel</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include <iksemel.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/lock.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/rtp.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/manager.h"
#include "asterisk/stringfields.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/astobj.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/jabber.h"
#include "asterisk/jingle.h"

#define JINGLE_CONFIG "jingle.conf"

/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = ""
};
static struct ast_jb_conf global_jbconf;

enum jingle_protocol {
	AJI_PROTOCOL_UDP = 1,
	AJI_PROTOCOL_SSLTCP = 2,
};

enum jingle_connect_type {
	AJI_CONNECT_STUN = 1,
	AJI_CONNECT_LOCAL = 2,
	AJI_CONNECT_RELAY = 3,
};

struct jingle_pvt {
	ast_mutex_t lock;                /*!< Channel private lock */
	time_t laststun;
	struct jingle *parent;	         /*!< Parent client */
	char sid[100];
	char from[100];
	char ring[10];                   /*!< Message ID of ring */
	iksrule *ringrule;               /*!< Rule for matching RING request */
	int initiator;                   /*!< If we're the initiator */
	int alreadygone;
	int capability;
	struct ast_codec_pref prefs;
	struct jingle_candidate *theircandidates;
	struct jingle_candidate *ourcandidates;
	char cid_num[80];                /*!< Caller ID num */
	char cid_name[80];               /*!< Caller ID name */
	char exten[80];                  /*!< Called extension */
	struct ast_channel *owner;       /*!< Master Channel */
	struct ast_rtp *rtp;             /*!< RTP audio session */
	struct ast_rtp *vrtp;            /*!< RTP video session */
	int jointcapability;             /*!< Supported capability at both ends (codecs ) */
	int peercapability;
	struct jingle_pvt *next;	/* Next entity */
};

struct jingle_candidate {
	char name[100];
	enum jingle_protocol protocol;
	double preference;
	char username[100];
	char password[100];
	enum jingle_connect_type type;
	char network[6];
	int generation;
	char ip[16];
	int port;
	int receipt;
	struct jingle_candidate *next;
};

struct jingle {
	ASTOBJ_COMPONENTS(struct jingle);
	struct aji_client *connection;
	struct aji_buddy *buddy;
	struct jingle_pvt *p;
	struct ast_codec_pref prefs;
	int amaflags;			/*!< AMA Flags */
	char user[100];
	char context[100];
	char accountcode[AST_MAX_ACCOUNT_CODE];	/*!< Account code */
	int capability;
	ast_group_t callgroup;	/*!< Call group */
	ast_group_t pickupgroup;	/*!< Pickup group */
	int callingpres;		/*!< Calling presentation */
	int allowguest;
	char language[MAX_LANGUAGE];	/*!<  Default language for prompts */
	char musicclass[MAX_MUSICCLASS];	/*!<  Music on Hold class */
};

struct jingle_container {
        ASTOBJ_CONTAINER_COMPONENTS(struct jingle);
};

static const char desc[] = "Jingle Channel";
static const char type[] = "Jingle";

static int usecnt = 0;
AST_MUTEX_DEFINE_STATIC(usecnt_lock);

static int global_capability = AST_FORMAT_ULAW | AST_FORMAT_ALAW | AST_FORMAT_GSM | AST_FORMAT_H263;

AST_MUTEX_DEFINE_STATIC(jinglelock); /*!< Protect the interface list (of jingle_pvt's) */

/* Forward declarations */
static struct ast_channel *jingle_request(const char *type, int format, void *data, int *cause);
static int jingle_digit_begin(struct ast_channel *ast, char digit);
static int jingle_digit_end(struct ast_channel *ast, char digit);
static int jingle_call(struct ast_channel *ast, char *dest, int timeout);
static int jingle_hangup(struct ast_channel *ast);
static int jingle_answer(struct ast_channel *ast);
static int jingle_newcall(struct jingle *client, ikspak *pak);
static struct ast_frame *jingle_read(struct ast_channel *ast);
static int jingle_write(struct ast_channel *ast, struct ast_frame *f);
static int jingle_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int jingle_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int jingle_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen);
static struct jingle_pvt *jingle_alloc(struct jingle *client, const char *from, const char *sid);
/*----- RTP interface functions */
static int jingle_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp,
							   struct ast_rtp *vrtp, int codecs, int nat_active);
static enum ast_rtp_get_result jingle_get_rtp_peer(struct ast_channel *chan, struct ast_rtp **rtp);
static int jingle_get_codec(struct ast_channel *chan);

/*! \brief PBX interface structure for channel registration */
static const struct ast_channel_tech jingle_tech = {
	.type = type,
	.description = "Jingle Channel Driver",
	.capabilities = ((AST_FORMAT_MAX_AUDIO << 1) - 1),
	.requester = jingle_request,
	.send_digit_begin = jingle_digit_begin,
	.send_digit_end = jingle_digit_end,
	.bridge = ast_rtp_bridge,
	.call = jingle_call,
	.hangup = jingle_hangup,
	.answer = jingle_answer,
	.read = jingle_read,
	.write = jingle_write,
	.exception = jingle_read,
	.indicate = jingle_indicate,
	.fixup = jingle_fixup,
	.send_html = jingle_sendhtml,
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER
};

static struct sockaddr_in bindaddr = { 0, };	/*!< The address we bind to */

static struct sched_context *sched;	/*!< The scheduling context */
static struct io_context *io;	/*!< The IO context */
static struct in_addr __ourip;


/*! \brief RTP driver interface */
static struct ast_rtp_protocol jingle_rtp = {
	type: "jingle",
	get_rtp_info: jingle_get_rtp_peer,
	set_rtp_peer: jingle_set_rtp_peer,
	get_codec: jingle_get_codec,
};

static char externip[16];

static struct jingle_container jingles;

static void jingle_member_destroy(struct jingle *obj)
{
	free(obj);
}

static struct jingle *find_jingle(char *name, char *connection)
{
	struct jingle *jingle = NULL;

	jingle = ASTOBJ_CONTAINER_FIND(&jingles, name);
	if (!jingle && strchr(name, '@'))
		jingle = ASTOBJ_CONTAINER_FIND_FULL(&jingles, name, user,,, strcasecmp);

	if (!jingle) {				/* guest call */
		ASTOBJ_CONTAINER_TRAVERSE(&jingles, 1, {
			ASTOBJ_WRLOCK(iterator);
			if (!strcasecmp(iterator->name, "guest")) {
				if (!strcasecmp(iterator->connection->jid->partial, connection)) {
					jingle = iterator;
					break;
				} else if (!strcasecmp(iterator->connection->name, connection)) {
					jingle = iterator;
					break;
				}
			}
			ASTOBJ_UNLOCK(iterator);
		});

	}
	return jingle;
}


static void add_codec_to_answer(const struct jingle_pvt *p, int codec, iks *dcodecs)
{
	char *format = ast_getformatname(codec);

	if (!strcasecmp("ulaw", format)) {
		iks *payload_eg711u, *payload_pcmu;
		payload_pcmu = iks_new("payload-type");
		iks_insert_attrib(payload_pcmu, "id", "0");
		iks_insert_attrib(payload_pcmu, "name", "PCMU");
		iks_insert_attrib(payload_pcmu, "xmlns", "http://www.google.com/session/phone");
		payload_eg711u = iks_new("payload-type");
		iks_insert_attrib(payload_eg711u, "id", "100");
		iks_insert_attrib(payload_eg711u, "name", "EG711U");
		iks_insert_attrib(payload_eg711u, "xmlns", "http://www.google.com/session/phone");
		iks_insert_node(dcodecs, payload_pcmu);
		iks_insert_node(dcodecs, payload_eg711u);
	}
	if (!strcasecmp("alaw", format)) {
		iks *payload_eg711a;
		iks *payload_pcma = iks_new("payload-type");
		iks_insert_attrib(payload_pcma, "id", "8");
		iks_insert_attrib(payload_pcma, "name", "PCMA");
		iks_insert_attrib(payload_pcma, "xmlns", "http://www.google.com/session/phone");
		payload_eg711a = iks_new("payload-type");
		iks_insert_attrib(payload_eg711a, "id", "101");
		iks_insert_attrib(payload_eg711a, "name", "EG711A");
		iks_insert_attrib(payload_eg711a, "xmlns", "http://www.google.com/session/phone");
		iks_insert_node(dcodecs, payload_pcma);
		iks_insert_node(dcodecs, payload_eg711a);
	}
	if (!strcasecmp("ilbc", format)) {
		iks *payload_ilbc = iks_new("payload-type");
		iks_insert_attrib(payload_ilbc, "id", "102");
		iks_insert_attrib(payload_ilbc, "name", "iLBC");
		iks_insert_attrib(payload_ilbc, "xmlns", "http://www.google.com/session/phone");
		iks_insert_node(dcodecs, payload_ilbc);
	}
	if (!strcasecmp("g723", format)) {
		iks *payload_g723 = iks_new("payload-type");
		iks_insert_attrib(payload_g723, "id", "4");
		iks_insert_attrib(payload_g723, "name", "G723");
		iks_insert_attrib(payload_g723, "xmlns", "http://www.google.com/session/phone");
		iks_insert_node(dcodecs, payload_g723);
	}
	ast_rtp_lookup_code(p->rtp, 1, codec);
}

static int jingle_accept_call(struct jingle *client, struct jingle_pvt *p)
{
	struct jingle_pvt *tmp = client->p;
	struct aji_client *c = client->connection;
	iks *iq, *jingle, *dcodecs, *payload_red, *payload_audio, *payload_cn;
	int x;
	int pref_codec = 0;
	int alreadysent = 0;

	if (p->initiator)
		return 1;

	iq = iks_new("iq");
	jingle = iks_new(GOOGLE_NODE);
	dcodecs = iks_new("description");
	if (iq && jingle && dcodecs) {
		iks_insert_attrib(dcodecs, "xmlns", "http://www.google.com/session/phone");

		for (x = 0; x < 32; x++) {
			if (!(pref_codec = ast_codec_pref_index(&client->prefs, x)))
				break;
			if (!(client->capability & pref_codec))
				continue;
			if (alreadysent & pref_codec)
				continue;
			if (pref_codec <= AST_FORMAT_MAX_AUDIO)
				add_codec_to_answer(p, pref_codec, dcodecs);
			else
				add_codec_to_answer(p, pref_codec, dcodecs);
			alreadysent |= pref_codec;
		}
		payload_red = iks_new("payload-type");
		iks_insert_attrib(payload_red, "id", "117");
		iks_insert_attrib(payload_red, "name", "red");
		iks_insert_attrib(payload_red, "xmlns", "http://www.google.com/session/phone");
		payload_audio = iks_new("payload-type");
		iks_insert_attrib(payload_audio, "id", "106");
		iks_insert_attrib(payload_audio, "name", "audio/telephone-event");
		iks_insert_attrib(payload_audio, "xmlns", "http://www.google.com/session/phone");
		payload_cn = iks_new("payload-type");
		iks_insert_attrib(payload_cn, "id", "13");
		iks_insert_attrib(payload_cn, "name", "CN");
		iks_insert_attrib(payload_cn, "xmlns", "http://www.google.com/session/phone");


		iks_insert_attrib(iq, "type", "set");
		iks_insert_attrib(iq, "to", (p->from) ? p->from : client->user);
		iks_insert_attrib(iq, "id", client->connection->mid);
		ast_aji_increment_mid(client->connection->mid);

		iks_insert_attrib(jingle, "xmlns", "http://www.google.com/session");
		iks_insert_attrib(jingle, "type", JINGLE_ACCEPT);
		iks_insert_attrib(jingle, "initiator",
						  p->initiator ? client->connection->jid->full : p->from);
		iks_insert_attrib(jingle, GOOGLE_SID, tmp->sid);
		iks_insert_node(iq, jingle);
		iks_insert_node(jingle, dcodecs);
		iks_insert_node(dcodecs, payload_red);
		iks_insert_node(dcodecs, payload_audio);
		iks_insert_node(dcodecs, payload_cn);

		iks_send(c->p, iq);
		iks_delete(payload_red);
		iks_delete(payload_audio);
		iks_delete(payload_cn);
		iks_delete(dcodecs);
		iks_delete(jingle);
		iks_delete(iq);
	}
	return 1;
}

static int jingle_ringing_ack(void *data, ikspak *pak)
{
	struct jingle_pvt *p = data;

	if (p->ringrule)
		iks_filter_remove_rule(p->parent->connection->f, p->ringrule);
	p->ringrule = NULL;
	if (p->owner)
		ast_queue_control(p->owner, AST_CONTROL_RINGING);
	return IKS_FILTER_EAT;
}

static int jingle_answer(struct ast_channel *ast)
{
	struct jingle_pvt *p = ast->tech_pvt;
	struct jingle *client = p->parent;
	int res = 0;

	if (option_debug)
		ast_log(LOG_DEBUG, "Answer!\n");
	ast_mutex_lock(&p->lock);
	jingle_accept_call(client, p);
	ast_mutex_unlock(&p->lock);
	return res;
}

static enum ast_rtp_get_result jingle_get_rtp_peer(struct ast_channel *chan, struct ast_rtp **rtp)
{
	struct jingle_pvt *p = chan->tech_pvt;
	enum ast_rtp_get_result res = AST_RTP_GET_FAILED;

	if (!p)
		return res;

	ast_mutex_lock(&p->lock);
	if (p->rtp) {
		*rtp = p->rtp;
		res = AST_RTP_TRY_NATIVE;
	}
	ast_mutex_unlock(&p->lock);

	return res;
}

static int jingle_get_codec(struct ast_channel *chan)
{
	struct jingle_pvt *p = chan->tech_pvt;
	return p->peercapability;
}

static int jingle_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp, struct ast_rtp *vrtp, int codecs, int nat_active)
{
	struct jingle_pvt *p;

	p = chan->tech_pvt;
	if (!p)
		return -1;
	ast_mutex_lock(&p->lock);

/*	if (rtp)
		ast_rtp_get_peer(rtp, &p->redirip);
	else
		memset(&p->redirip, 0, sizeof(p->redirip));
	p->redircodecs = codecs; */

	/* Reset lastrtprx timer */
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int jingle_response(struct jingle *client, ikspak *pak, const char *reasonstr, const char *reasonstr2)
{
	iks *response = NULL, *error = NULL, *reason = NULL;
	int res = -1;

	response = iks_new("iq");
	if (response) {
		iks_insert_attrib(response, "type", "result");
		iks_insert_attrib(response, "from", client->connection->jid->full);
		iks_insert_attrib(response, "to", iks_find_attrib(pak->x, "from"));
		iks_insert_attrib(response, "id", iks_find_attrib(pak->x, "id"));
		if (reasonstr) {
			error = iks_new("error");
			if (error) {
				iks_insert_attrib(error, "type", "cancel");
				reason = iks_new(reasonstr);
				if (reason)
					iks_insert_node(error, reason);
				iks_insert_node(response, error);
			}
		}
		iks_send(client->connection->p, response);
		if (reason)
			iks_delete(reason);
		if (error)
			iks_delete(error);
		iks_delete(response);
		res = 0;
	}
	return res;
}

static int jingle_is_answered(struct jingle *client, ikspak *pak)
{
	struct jingle_pvt *tmp;

	ast_log(LOG_DEBUG, "The client is %s\n", client->name);
	/* Make sure our new call doesn't exist yet */
	for (tmp = client->p; tmp; tmp = tmp->next) {
		if (iks_find_with_attrib(pak->x, GOOGLE_NODE, GOOGLE_SID, tmp->sid))
			break;
	}

	if (tmp) {
		if (tmp->owner)
			ast_queue_control(tmp->owner, AST_CONTROL_ANSWER);
	} else
		ast_log(LOG_NOTICE, "Whoa, didn't find call!\n");
	jingle_response(client, pak, NULL, NULL);
	return 1;
}

static int jingle_handle_dtmf(struct jingle *client, ikspak *pak)
{
	struct jingle_pvt *tmp;
	iks *dtmfnode = NULL;
	char *dtmf;
	/* Make sure our new call doesn't exist yet */
	for (tmp = client->p; tmp; tmp = tmp->next) {
		if (iks_find_with_attrib(pak->x, GOOGLE_NODE, GOOGLE_SID, tmp->sid))
			break;
	}

	if (tmp) {
		if(iks_find_with_attrib(pak->x, "dtmf-method", "method", "rtp")) {
			jingle_response(client,pak,
					"feature-not-implemented xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'",
					"unsupported-dtmf-method xmlns='http://jabber.org/protocol/jingle/info/dtmf#errors'");
			return -1;
		}
		if ((dtmfnode  = iks_find(pak->x, "dtmf"))) {
			if((dtmf = iks_find_attrib(dtmfnode, "code"))) {
				if(iks_find_with_attrib(pak->x, "dtmf", "action", "button-up")) {
					struct ast_frame f = {AST_FRAME_DTMF_BEGIN, };
					f.subclass = dtmf[0];
					ast_queue_frame(tmp->owner, &f);
					ast_verbose("JINGLE! DTMF-relay event received: %c\n", f.subclass);
				} else if(iks_find_with_attrib(pak->x, "dtmf", "action", "button-down")) {
					struct ast_frame f = {AST_FRAME_DTMF_END, };
					f.subclass = dtmf[0];
					ast_queue_frame(tmp->owner, &f);
					ast_verbose("JINGLE! DTMF-relay event received: %c\n", f.subclass);
				} else if(iks_find_attrib(pak->x, "dtmf")) { /* 250 millasecond default */
					struct ast_frame f = {AST_FRAME_DTMF, };
					f.subclass = dtmf[0];
					ast_queue_frame(tmp->owner, &f);
					ast_verbose("JINGLE! DTMF-relay event received: %c\n", f.subclass);
				}
			}
		}
		jingle_response(client, pak, NULL, NULL);
		return 1;
	} else
		ast_log(LOG_NOTICE, "Whoa, didn't find call!\n");

	jingle_response(client, pak, NULL, NULL);
	return 1;
}


static int jingle_hangup_farend(struct jingle *client, ikspak *pak)
{
	struct jingle_pvt *tmp;

	ast_log(LOG_DEBUG, "The client is %s\n", client->name);
	/* Make sure our new call doesn't exist yet */
	for (tmp = client->p; tmp; tmp = tmp->next) {
		if (iks_find_with_attrib(pak->x, GOOGLE_NODE, GOOGLE_SID, tmp->sid))
			break;
	}

	if (tmp) {
		tmp->alreadygone = 1;
		ast_queue_hangup(tmp->owner);
	} else
		ast_log(LOG_NOTICE, "Whoa, didn't find call!\n");
	jingle_response(client, pak, NULL, NULL);
	return 1;
}

static int jingle_create_candidates(struct jingle *client, struct jingle_pvt *p, char *sid, char *from)
{
	struct jingle_candidate *tmp;
	struct aji_client *c = client->connection;
	struct jingle_candidate *ours1 = NULL, *ours2 = NULL;
	struct sockaddr_in sin;
	struct sockaddr_in dest;
	struct in_addr us;
	iks *iq, *jingle, *candidate;
	char user[17], pass[17], preference[5], port[7];


	iq = iks_new("iq");
	jingle = iks_new(GOOGLE_NODE);
	candidate = iks_new("candidate");
	if (!iq || !jingle || !candidate) {
		ast_log(LOG_ERROR, "Memory allocation error\n");
		goto safeout;
	}
	ours1 = ast_calloc(1, sizeof(*ours1));
	ours2 = ast_calloc(1, sizeof(*ours2));
	if (!ours1 || !ours2)
		goto safeout;
	iks_insert_node(iq, jingle);
	iks_insert_node(jingle, candidate);

	for (; p; p = p->next) {
		if (!strcasecmp(p->sid, sid))
			break;
	}

	if (!p) {
		ast_log(LOG_NOTICE, "No matching jingle session - SID %s!\n", sid);
		goto safeout;
	}

	ast_rtp_get_us(p->rtp, &sin);
	ast_find_ourip(&us, bindaddr);

	/* Setup our jingle candidates */
	ast_copy_string(ours1->name, "rtp", sizeof(ours1->name));
	ours1->port = ntohs(sin.sin_port);
	ours1->preference = 1;
	snprintf(user, sizeof(user), "%08lx%08lx", ast_random(), ast_random());
	snprintf(pass, sizeof(pass), "%08lx%08lx", ast_random(), ast_random());
	ast_copy_string(ours1->username, user, sizeof(ours1->username));
	ast_copy_string(ours1->password, pass, sizeof(ours1->password));
	ast_copy_string(ours1->ip, ast_inet_ntoa(us), sizeof(ours1->ip));
	ours1->protocol = AJI_PROTOCOL_UDP;
	ours1->type = AJI_CONNECT_LOCAL;
	ours1->generation = 0;
	p->ourcandidates = ours1;

	if (!ast_strlen_zero(externip)) {
		/* XXX We should really stun for this one not just go with externip XXX */
		snprintf(user, sizeof(user), "%08lx%08lx", ast_random(), ast_random());
		snprintf(pass, sizeof(pass), "%08lx%08lx", ast_random(), ast_random());
		ast_copy_string(ours2->username, user, sizeof(ours2->username));
		ast_copy_string(ours2->password, pass, sizeof(ours2->password));
		ast_copy_string(ours2->ip, externip, sizeof(ours2->ip));
		ast_copy_string(ours2->name, "rtp", sizeof(ours1->name));
		ours2->port = ntohs(sin.sin_port);
		ours2->preference = 0.9;
		ours2->protocol = AJI_PROTOCOL_UDP;
		ours2->type = AJI_CONNECT_STUN;
		ours2->generation = 0;
		ours1->next = ours2;
		ours2 = NULL;
	}
	ours1 = NULL;
	dest.sin_addr = __ourip;
	dest.sin_port = sin.sin_port;


	for (tmp = p->ourcandidates; tmp; tmp = tmp->next) {
		snprintf(port, sizeof(port), "%d", tmp->port);
		snprintf(preference, sizeof(preference), "%.2f", tmp->preference);
		iks_insert_attrib(iq, "from", c->jid->full);
		iks_insert_attrib(iq, "to", from);
		iks_insert_attrib(iq, "type", "set");
		iks_insert_attrib(iq, "id", c->mid);
		ast_aji_increment_mid(c->mid);
		iks_insert_attrib(jingle, "type", "candidates");
		iks_insert_attrib(jingle, "id", sid);
		iks_insert_attrib(jingle, "initiator", (p->initiator) ? c->jid->full : from);
		iks_insert_attrib(jingle, "xmlns", GOOGLE_NS);
		iks_insert_attrib(candidate, "name", tmp->name);
		iks_insert_attrib(candidate, "address", tmp->ip);
		iks_insert_attrib(candidate, "port", port);
		iks_insert_attrib(candidate, "username", tmp->username);
		iks_insert_attrib(candidate, "password", tmp->password);
		iks_insert_attrib(candidate, "preference", preference);
		if (tmp->protocol == AJI_PROTOCOL_UDP)
			iks_insert_attrib(candidate, "protocol", "udp");
		if (tmp->protocol == AJI_PROTOCOL_SSLTCP)
			iks_insert_attrib(candidate, "protocol", "ssltcp");
		if (tmp->type == AJI_CONNECT_STUN)
			iks_insert_attrib(candidate, "type", "stun");
		if (tmp->type == AJI_CONNECT_LOCAL)
			iks_insert_attrib(candidate, "type", "local");
		if (tmp->type == AJI_CONNECT_RELAY)
			iks_insert_attrib(candidate, "type", "relay");
		iks_insert_attrib(candidate, "network", "0");
		iks_insert_attrib(candidate, "generation", "0");
		iks_send(c->p, iq);
	}
	p->laststun = 0;

safeout:
	if (ours1)
		free(ours1);
	if (ours2)
		free(ours2);
	if (iq)
		iks_delete(iq);
	if (jingle)
		iks_delete(jingle);
	if (candidate)
		iks_delete(candidate);
	return 1;
}

static struct jingle_pvt *jingle_alloc(struct jingle *client, const char *from, const char *sid)
{
	struct jingle_pvt *tmp = NULL;
	struct aji_resource *resources = NULL;
	struct aji_buddy *buddy;
	char idroster[200];

	if (option_debug)
		ast_log(LOG_DEBUG, "The client is %s for alloc\n", client->name);
	if (!sid && !strchr(from, '/')) {	/* I started call! */
		if (!strcasecmp(client->name, "guest")) {
			buddy = ASTOBJ_CONTAINER_FIND(&client->connection->buddies, from);
			if (buddy)
				resources = buddy->resources;
		} else 
			resources = client->buddy->resources;
		while (resources) {
			if (resources->cap->jingle) {
				break;
			}
			resources = resources->next;
		}
		if (resources)
			snprintf(idroster, sizeof(idroster), "%s/%s", from, resources->resource);
		else {
			ast_log(LOG_ERROR, "no jingle capable clients to talk to.\n");
			return NULL;
		}
	}
	if (!(tmp = ast_calloc(1, sizeof(*tmp)))) {
		return NULL;
	}
	if (sid) {
		ast_copy_string(tmp->sid, sid, sizeof(tmp->sid));
		ast_copy_string(tmp->from, from, sizeof(tmp->from));
	} else {
		snprintf(tmp->sid, sizeof(tmp->sid), "%08lx%08lx", ast_random(), ast_random());
		ast_copy_string(tmp->from, idroster, sizeof(tmp->from));
		tmp->initiator = 1;
	}
	tmp->rtp = ast_rtp_new_with_bindaddr(sched, io, 1, 0, bindaddr.sin_addr);
	tmp->parent = client;
	if (!tmp->rtp) {
		ast_log(LOG_WARNING, "Out of RTP sessions?\n");
		free(tmp);
		return NULL;
	}
	ast_copy_string(tmp->exten, "s", sizeof(tmp->exten));
	ast_mutex_init(&tmp->lock);
	ast_mutex_lock(&jinglelock);
	tmp->next = client->p;
	client->p = tmp;
	ast_mutex_unlock(&jinglelock);
	return tmp;
}

/*! \brief Start new jingle channel */
static struct ast_channel *jingle_new(struct jingle *client, struct jingle_pvt *i, int state, const char *title)
{
	struct ast_channel *tmp;
	int fmt;
	int what;

	tmp = ast_channel_alloc(1);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate Jingle channel structure!\n");
		return NULL;
	}
	tmp->tech = &jingle_tech;

	/* Select our native format based on codec preference until we receive
	   something from another device to the contrary. */
	if (i->jointcapability)
		what = i->jointcapability;
	else if (i->capability)
		what = i->capability;
	else
		what = global_capability;
	tmp->nativeformats = ast_codec_choose(&i->prefs, what, 1) | (i->jointcapability & AST_FORMAT_VIDEO_MASK);
	fmt = ast_best_codec(tmp->nativeformats);

	if (title)
		ast_string_field_build(tmp, name, "Jingle/%s-%04lx", title, ast_random() & 0xffff);
	else
		ast_string_field_build(tmp, name, "Jingle/%s-%04lx", i->from, ast_random() & 0xffff);

	if (i->rtp) {
		tmp->fds[0] = ast_rtp_fd(i->rtp);
		tmp->fds[1] = ast_rtcp_fd(i->rtp);
	}
	if (i->vrtp) {
		tmp->fds[2] = ast_rtp_fd(i->vrtp);
		tmp->fds[3] = ast_rtcp_fd(i->vrtp);
	}
	if (state == AST_STATE_RING)
		tmp->rings = 1;
	tmp->adsicpe = AST_ADSI_UNAVAILABLE;
	tmp->writeformat = fmt;
	tmp->rawwriteformat = fmt;
	tmp->readformat = fmt;
	tmp->rawreadformat = fmt;
	tmp->tech_pvt = i;

	tmp->callgroup = client->callgroup;
	tmp->pickupgroup = client->pickupgroup;
	tmp->cid.cid_pres = client->callingpres;
	if (!ast_strlen_zero(client->accountcode))
		ast_string_field_set(tmp, accountcode, client->accountcode);
	if (client->amaflags)
		tmp->amaflags = client->amaflags;
	if (!ast_strlen_zero(client->language))
		ast_string_field_set(tmp, language, client->language);
	if (!ast_strlen_zero(client->musicclass))
		ast_string_field_set(tmp, musicclass, client->musicclass);
	i->owner = tmp;
	ast_mutex_lock(&usecnt_lock);
	usecnt++;
	ast_mutex_unlock(&usecnt_lock);
	ast_copy_string(tmp->context, client->context, sizeof(tmp->context));
	ast_copy_string(tmp->exten, i->exten, sizeof(tmp->exten));
	ast_set_callerid(tmp, i->cid_num, i->cid_name, i->cid_num);
	if (!ast_strlen_zero(i->exten) && strcmp(i->exten, "s"))
		tmp->cid.cid_dnid = ast_strdup(i->exten);
	tmp->priority = 1;
	ast_setstate(tmp, state);
	if (i->rtp)
		ast_jb_configure(tmp, &global_jbconf);
	if (state != AST_STATE_DOWN && ast_pbx_start(tmp)) {
		ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
		tmp->hangupcause = AST_CAUSE_SWITCH_CONGESTION;
		ast_hangup(tmp);
		tmp = NULL;
	}

	return tmp;
}

static int jingle_action(struct jingle *client, struct jingle_pvt *p, const char *action)
{
	iks *request, *session = NULL;
	int res = -1;

	request = iks_new("iq");
	if (request) {
		iks_insert_attrib(request, "type", "set");
		iks_insert_attrib(request, "from", client->connection->jid->full);
		iks_insert_attrib(request, "to", p->from);
		iks_insert_attrib(request, "id", client->connection->mid);
		ast_aji_increment_mid(client->connection->mid);
		session = iks_new("session");
		if (session) {
			iks_insert_attrib(session, "type", action);
			iks_insert_attrib(session, "id", p->sid);
			iks_insert_attrib(session, "initiator",
							  p->initiator ? client->connection->jid->full : p->from);
			iks_insert_attrib(session, "xmlns", "http://www.google.com/session");
			iks_insert_node(request, session);
			iks_send(client->connection->p, request);
			iks_delete(session);
			res = 0;
		}
		iks_delete(request);
	}
	return res;
}

static void jingle_free_candidates(struct jingle_candidate *candidate)
{
	struct jingle_candidate *last;
	while (candidate) {
		last = candidate;
		candidate = candidate->next;
		free(last);
	}
}

static void jingle_free_pvt(struct jingle *client, struct jingle_pvt *p)
{
	struct jingle_pvt *cur, *prev = NULL;
	cur = client->p;
	while (cur) {
		if (cur == p) {
			if (prev)
				prev->next = p->next;
			else
				client->p = p->next;
			break;
		}
		prev = cur;
		cur = cur->next;
	}
	if (p->ringrule)
		iks_filter_remove_rule(p->parent->connection->f, p->ringrule);
	if (p->owner)
		ast_log(LOG_WARNING, "Uh oh, there's an owner, this is going to be messy.\n");
	if (p->rtp)
		ast_rtp_destroy(p->rtp);
	if (p->vrtp)
		ast_rtp_destroy(p->vrtp);
	jingle_free_candidates(p->theircandidates);
	free(p);
}


static int jingle_newcall(struct jingle *client, ikspak *pak)
{
	struct jingle_pvt *p, *tmp = client->p;
	struct ast_channel *chan;
	int res;
	iks *codec;

	/* Make sure our new call doesn't exist yet */
	while (tmp) {
		if (iks_find_with_attrib(pak->x, GOOGLE_NODE, GOOGLE_SID, tmp->sid)) {
			ast_log(LOG_NOTICE, "Ignoring duplicate call setup on SID %s\n", tmp->sid);
			jingle_response(client, pak, "out-of-order", NULL);
			return -1;
		}
		tmp = tmp->next;
	}

	p = jingle_alloc(client, pak->from->partial, iks_find_attrib(pak->query, GOOGLE_SID));
	if (!p) {
		ast_log(LOG_WARNING, "Unable to allocate jingle structure!\n");
		return -1;
	}
	chan = jingle_new(client, p, AST_STATE_DOWN, pak->from->user);
	if (chan) {
		ast_mutex_lock(&p->lock);
		ast_copy_string(p->from, pak->from->full, sizeof(p->from));
		if (iks_find_attrib(pak->query, GOOGLE_SID)) {
			ast_copy_string(p->sid, iks_find_attrib(pak->query, GOOGLE_SID),
							sizeof(p->sid));
		}

		codec = iks_child(iks_child(iks_child(pak->x)));
		while (codec) {
			ast_rtp_set_m_type(p->rtp, atoi(iks_find_attrib(codec, "id")));
			ast_rtp_set_rtpmap_type(p->rtp, atoi(iks_find_attrib(codec, "id")), "audio",
						iks_find_attrib(codec, "name"), 0);
			codec = iks_next(codec);
		}
		
		ast_mutex_unlock(&p->lock);
		ast_setstate(chan, AST_STATE_RING);
		res = ast_pbx_start(chan);

		switch (res) {
		case AST_PBX_FAILED:
			ast_log(LOG_WARNING, "Failed to start PBX :(\n");
			jingle_response(client, pak, "service-unavailable", NULL);
			break;
		case AST_PBX_CALL_LIMIT:
			ast_log(LOG_WARNING, "Failed to start PBX (call limit reached) \n");
			jingle_response(client, pak, "service-unavailable", NULL);
			break;
		case AST_PBX_SUCCESS:
			jingle_response(client, pak, NULL, NULL);
			jingle_create_candidates(client, p,
					iks_find_attrib(pak->query, GOOGLE_SID),
					iks_find_attrib(pak->x, "from"));
			/* nothing to do */
			break;
		}
	} else {
		jingle_free_pvt(client, p);
	}
	return 1;
}

static int jingle_update_stun(struct jingle *client, struct jingle_pvt *p)
{
	struct jingle_candidate *tmp;
	struct hostent *hp;
	struct ast_hostent ahp;
	struct sockaddr_in sin;

	if (time(NULL) == p->laststun)
		return 0;

	tmp = p->theircandidates;
	p->laststun = time(NULL);
	while (tmp) {
		char username[256];
		hp = ast_gethostbyname(tmp->ip, &ahp);
		sin.sin_family = AF_INET;
		memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
		sin.sin_port = htons(tmp->port);
		snprintf(username, sizeof(username), "%s%s", tmp->username,
				 p->ourcandidates->username);

		ast_rtp_stun_request(p->rtp, &sin, username);
		tmp = tmp->next;
	}
	return 1;
}

static int jingle_add_candidate(struct jingle *client, ikspak *pak)
{
	struct jingle_pvt *p = NULL, *tmp = NULL;
	struct aji_client *c = client->connection;
	struct jingle_candidate *newcandidate = NULL;
	iks  *traversenodes = NULL, *receipt = NULL;
	newcandidate = ast_calloc(1, sizeof(*newcandidate));
	if (!newcandidate)
		return 0;
	for (tmp = client->p; tmp; tmp = tmp->next) {
		if (iks_find_with_attrib(pak->x, GOOGLE_NODE, GOOGLE_SID, tmp->sid)) {
			p = tmp;
			break;
		}
	}

	if (!p)
		return -1;

	traversenodes = pak->query;
	while(traversenodes) {
		if(!strcasecmp(iks_name(traversenodes), "session")) {
			traversenodes = iks_child(traversenodes);
			continue;
		}
		if(!strcasecmp(iks_name(traversenodes), "candidate")) {
			newcandidate = ast_calloc(1, sizeof(*newcandidate));
			if (!newcandidate)
				return 0;
			ast_copy_string(newcandidate->name, iks_find_attrib(traversenodes, "name"),
							sizeof(newcandidate->name));
			ast_copy_string(newcandidate->ip, iks_find_attrib(traversenodes, "address"),
							sizeof(newcandidate->ip));
			newcandidate->port = atoi(iks_find_attrib(traversenodes, "port"));
			ast_copy_string(newcandidate->username, iks_find_attrib(traversenodes, "username"),
							sizeof(newcandidate->username));
			ast_copy_string(newcandidate->password, iks_find_attrib(traversenodes, "password"),
							sizeof(newcandidate->password));
			newcandidate->preference = atof(iks_find_attrib(traversenodes, "preference"));
			if (!strcasecmp(iks_find_attrib(traversenodes, "protocol"), "udp"))
				newcandidate->protocol = AJI_PROTOCOL_UDP;
			if (!strcasecmp(iks_find_attrib(traversenodes, "protocol"), "ssltcp"))
				newcandidate->protocol = AJI_PROTOCOL_SSLTCP;
		
			if (!strcasecmp(iks_find_attrib(traversenodes, "type"), "stun"))
				newcandidate->type = AJI_CONNECT_STUN;
			if (!strcasecmp(iks_find_attrib(traversenodes, "type"), "local"))
				newcandidate->type = AJI_CONNECT_LOCAL;
			if (!strcasecmp(iks_find_attrib(traversenodes, "type"), "relay"))
				newcandidate->type = AJI_CONNECT_RELAY;
			ast_copy_string(newcandidate->network, iks_find_attrib(traversenodes, "network"),
							sizeof(newcandidate->network));
			newcandidate->generation = atoi(iks_find_attrib(traversenodes, "generation"));
			newcandidate->next = NULL;
		
			newcandidate->next = p->theircandidates;
			p->theircandidates = newcandidate;
			p->laststun = 0;
			jingle_update_stun(p->parent, p);
			newcandidate = NULL;
		}
		traversenodes = iks_next(traversenodes);
	}
	
	receipt = iks_new("iq");
	iks_insert_attrib(receipt, "type", "result");
	iks_insert_attrib(receipt, "from", c->jid->full);
	iks_insert_attrib(receipt, "to", iks_find_attrib(pak->x, "from"));
	iks_insert_attrib(receipt, "id", iks_find_attrib(pak->x, "id"));
	iks_send(c->p, receipt);
	iks_delete(receipt);

	return 1;
}

static struct ast_frame *jingle_rtp_read(struct ast_channel *ast, struct jingle_pvt *p)
{
	struct ast_frame *f;

	if (!p->rtp)
		return &ast_null_frame;
	f = ast_rtp_read(p->rtp);
	jingle_update_stun(p->parent, p);
	if (p->owner) {
		/* We already hold the channel lock */
		if (f->frametype == AST_FRAME_VOICE) {
			if (f->subclass != (p->owner->nativeformats & AST_FORMAT_AUDIO_MASK)) {
				if (option_debug)
					ast_log(LOG_DEBUG, "Oooh, format changed to %d\n", f->subclass);
				p->owner->nativeformats =
					(p->owner->nativeformats & AST_FORMAT_VIDEO_MASK) | f->subclass;
				ast_set_read_format(p->owner, p->owner->readformat);
				ast_set_write_format(p->owner, p->owner->writeformat);
			}
/*			if ((ast_test_flag(p, SIP_DTMF) == SIP_DTMF_INBAND) && p->vad) {
				f = ast_dsp_process(p->owner, p->vad, f);
				if (option_debug && f && (f->frametype == AST_FRAME_DTMF))
					ast_log(LOG_DEBUG, "* Detected inband DTMF '%c'\n", f->subclass);
		        } */
		}
	}
	return f;
}

static struct ast_frame *jingle_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	struct jingle_pvt *p = ast->tech_pvt;

	ast_mutex_lock(&p->lock);
	fr = jingle_rtp_read(ast, p);
	ast_mutex_unlock(&p->lock);
	return fr;
}

/*! \brief Send frame to media channel (rtp) */
static int jingle_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct jingle_pvt *p = ast->tech_pvt;
	int res = 0;

	switch (frame->frametype) {
	case AST_FRAME_VOICE:
		if (!(frame->subclass & ast->nativeformats)) {
			ast_log(LOG_WARNING,
					"Asked to transmit frame type %d, while native formats is %d (read/write = %d/%d)\n",
					frame->subclass, ast->nativeformats, ast->readformat,
					ast->writeformat);
			return 0;
		}
		if (p) {
			ast_mutex_lock(&p->lock);
			if (p->rtp) {
				res = ast_rtp_write(p->rtp, frame);
			}
			ast_mutex_unlock(&p->lock);
		}
		break;
	case AST_FRAME_VIDEO:
		if (p) {
			ast_mutex_lock(&p->lock);
			if (p->vrtp) {
				res = ast_rtp_write(p->vrtp, frame);
			}
			ast_mutex_unlock(&p->lock);
		}
		break;
	case AST_FRAME_IMAGE:
		return 0;
		break;
	default:
		ast_log(LOG_WARNING, "Can't send %d type frames with Jingle write\n",
				frame->frametype);
		return 0;
	}

	return res;
}

static int jingle_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct jingle_pvt *p = newchan->tech_pvt;
	ast_mutex_lock(&p->lock);

	if ((p->owner != oldchan)) {
		ast_mutex_unlock(&p->lock);
		return -1;
	}
	if (p->owner == oldchan)
		p->owner = newchan;
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int jingle_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	int res = 0;

	switch (condition) {
	case AST_CONTROL_HOLD:
		ast_moh_start(ast, data, NULL);
		break;
	case AST_CONTROL_UNHOLD:
		ast_moh_stop(ast);
		break;
	default:
		ast_log(LOG_NOTICE, "Don't know how to indicate condition '%d'\n", condition);
		res = -1;
	}

	return res;
}

static int jingle_digit_begin(struct ast_channel *chan, char digit)
{
	/* XXX Does jingle have a concept of the length of a dtmf digit ? */
	return 0;
}

static int jingle_digit_end(struct ast_channel *ast, char digit)
{
	struct jingle_pvt *p = ast->tech_pvt;
	struct jingle *client = p->parent;
	iks *iq, *jingle, *dtmf;
	char buffer[2] = {digit, '\0'};
	iq = iks_new("iq");
	jingle = iks_new("jingle");
	dtmf = iks_new("dtmf");
	if(!iq || !jingle || !dtmf) {
		if(iq)
			iks_delete(iq);
		if(jingle)
			iks_delete(jingle);
		if(dtmf)
			iks_delete(dtmf);
		ast_log(LOG_ERROR, "Did not send dtmf do to memory issue\n");
		return -1;
	}

	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(iq, "to", p->from);
	iks_insert_attrib(iq, "from", client->connection->jid->full);
	iks_insert_attrib(iq, "id", client->connection->mid);
	ast_aji_increment_mid(client->connection->mid);
	iks_insert_attrib(jingle, "xmlns", "http://jabber.org/protocol/jingle");
	iks_insert_attrib(jingle, "action", "content-info");
	iks_insert_attrib(jingle, "initiator", p->initiator ? client->connection->jid->full : p->from);
	iks_insert_attrib(jingle, "sid", p->sid);
	iks_insert_attrib(dtmf, "xmlns", "http://jabber.org/protocol/jingle/info/dtmf");
	iks_insert_attrib(dtmf, "code", buffer);
	iks_insert_node(iq, jingle);
	iks_insert_node(jingle, dtmf);

	ast_mutex_lock(&p->lock);
	if(ast->dtmff.frametype == AST_FRAME_DTMF) {
		ast_verbose("Sending 250ms dtmf!\n");
	} else if (ast->dtmff.frametype == AST_FRAME_DTMF_BEGIN) {
		iks_insert_attrib(dtmf, "action", "button-down");
	} else if (ast->dtmff.frametype == AST_FRAME_DTMF_END) {
		iks_insert_attrib(dtmf, "action", "button-up");
	}
	iks_send(client->connection->p, iq);
	iks_delete(iq);
	iks_delete(jingle);
	iks_delete(dtmf);
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int jingle_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen)
{
	ast_log(LOG_NOTICE, "XXX Implement jingle sendhtml XXX\n");

	return -1;
}
static int jingle_transmit_invite(struct jingle_pvt *p)
{
	struct jingle *jingle = NULL;
	struct aji_client *client = NULL;
	iks *iq, *desc, *session;
	iks *payload_eg711u, *payload_pcmu;

	jingle = p->parent;
	client = jingle->connection;
	iq = iks_new("iq");
	desc = iks_new("description");
	session = iks_new("session");
	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(iq, "to", p->from);
	iks_insert_attrib(iq, "from", client->jid->full);
	iks_insert_attrib(iq, "id", client->mid);
	ast_aji_increment_mid(client->mid);
	iks_insert_attrib(session, "type", "initiate");
	iks_insert_attrib(session, "id", p->sid);
	iks_insert_attrib(session, "initiator", client->jid->full);
	iks_insert_attrib(session, "xmlns", "http://www.google.com/session");
	iks_insert_attrib(desc, "xmlns", "http://www.google.com/session/phone");
	payload_pcmu = iks_new("payload-type");
	iks_insert_attrib(payload_pcmu, "id", "0");
	iks_insert_attrib(payload_pcmu, "name", "PCMU");
	payload_eg711u = iks_new("payload-type");
	iks_insert_attrib(payload_eg711u, "id", "100");
	iks_insert_attrib(payload_eg711u, "name", "EG711U");
	iks_insert_node(desc, payload_pcmu);
	iks_insert_node(desc, payload_eg711u);
	iks_insert_node(iq, session);
	iks_insert_node(session, desc);
	iks_send(client->p, iq);
	iks_delete(iq);
	iks_delete(desc);
	iks_delete(session);
	iks_delete(payload_eg711u);
	iks_delete(payload_pcmu);
	return 0;
}

/* Not in use right now.
static int jingle_auto_congest(void *nothing)
{
	struct jingle_pvt *p = nothing;

	ast_mutex_lock(&p->lock);
	if (p->owner) {
		if (!ast_channel_trylock(p->owner)) {
			ast_log(LOG_NOTICE, "Auto-congesting %s\n", p->owner->name);
			ast_queue_control(p->owner, AST_CONTROL_CONGESTION);
			ast_channel_unlock(p->owner);
		}
	}
	ast_mutex_unlock(&p->lock);
	return 0;
}
*/

/*! \brief Initiate new call, part of PBX interface 
 * 	dest is the dial string */
static int jingle_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct jingle_pvt *p = ast->tech_pvt;

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "jingle_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	ast_setstate(ast, AST_STATE_RING);
	p->jointcapability = p->capability;
	if (!p->ringrule) {
		ast_copy_string(p->ring, p->parent->connection->mid, sizeof(p->ring));
		p->ringrule = iks_filter_add_rule(p->parent->connection->f, jingle_ringing_ack, p,
							IKS_RULE_ID, p->ring, IKS_RULE_DONE);
	} else
		ast_log(LOG_WARNING, "Whoa, already have a ring rule!\n");

	jingle_transmit_invite(p);
	jingle_create_candidates(p->parent, p, p->sid, p->from);

	return 0;
}

/*! \brief Hangup a call through the jingle proxy channel */
static int jingle_hangup(struct ast_channel *ast)
{
	struct jingle_pvt *p = ast->tech_pvt;
	struct jingle *client;

	ast_mutex_lock(&p->lock);
	client = p->parent;
	p->owner = NULL;
	ast->tech_pvt = NULL;
	if (!p->alreadygone)
		jingle_action(client, p, "terminate");
	ast_mutex_unlock(&p->lock);

	jingle_free_pvt(client, p);
	ast_mutex_lock(&usecnt_lock);
	usecnt--;
	ast_mutex_unlock(&usecnt_lock);

	return 0;
}

/*! \brief Part of PBX interface */
static struct ast_channel *jingle_request(const char *type, int format, void *data, int *cause)
{
	struct jingle_pvt *p = NULL;
	struct jingle *client = NULL;
	char *sender = NULL, *to = NULL, *s = NULL;
	struct ast_channel *chan = NULL;

	if (data) {
		s = ast_strdupa((char *) data);
		if (s) {
			sender = strsep(&s, "/");
			if (sender && (sender[0] != '\0'))
				to = strsep(&s, "/");
			if (!to) {
				ast_log(LOG_ERROR, "Bad arguments in Jingle Dialstring: %s\n", (char*) data);
				if (s)
					free(s);
				return NULL;
			}
		}
	}
	client = find_jingle(to, sender);
	if (!client) {
		ast_log(LOG_WARNING, "Could not find recipient.\n");
		if (s)
			free(s);
		return NULL;
	}
	p = jingle_alloc(client, to, NULL);
	if (p)
		chan = jingle_new(client, p, AST_STATE_DOWN, to);

	return chan;
}

#if 0
/*! \brief CLI command "jingle show channels" */
static int jingle_show(int fd, int argc, char **argv)
{
	struct jingle_pvt *p = NULL;
	struct jingle *peer = NULL;
	struct jingle_candidate *tmp;
	struct jingle *client = NULL;
	client = ast_aji_get_client("asterisk");
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&jinglelock);
	if (client)
		p = jingles->p;
	while (p) {
		ast_mutex_lock(&p->lock);
		ast_cli(fd, "SID = %s\n", p->sid);
		tmp = p->candidates;
		while (tmp) {
			ast_verbose("port %d\n", tmp->port);
			tmp = tmp->next;
		}
		ast_mutex_unlock(&p->lock);
		p = p->next;
	}
	if (!jingles->p)
		ast_cli(fd, "No jingle channels in use\n");
	ast_mutex_unlock(&jinglelock);
	return RESULT_SUCCESS;
}
#endif

static int jingle_parser(void *data, ikspak *pak)
{
	struct jingle *client = ASTOBJ_REF((struct jingle *) data);

	if (iks_find_with_attrib(pak->x, GOOGLE_NODE, "type", JINGLE_INITIATE)) {
		/* New call */
		jingle_newcall(client, pak);
	} else if (iks_find_with_attrib(pak->x, GOOGLE_NODE, "type", GOOGLE_NEGOTIATE)) {
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "About to add candidate!\n");
		jingle_add_candidate(client, pak);
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Candidate Added!\n");
	} else if (iks_find_with_attrib(pak->x, GOOGLE_NODE, "type", GOOGLE_ACCEPT)) {
		jingle_is_answered(client, pak);
	} else if (iks_find_with_attrib(pak->x, GOOGLE_NODE, "type", "content-info")) {
		jingle_handle_dtmf(client, pak);
	} else if (iks_find_with_attrib(pak->x, GOOGLE_NODE, "type", "terminate")) {
		jingle_hangup_farend(client, pak);
	} else if (iks_find_with_attrib(pak->x, GOOGLE_NODE, "type", "reject")) {
		jingle_hangup_farend(client, pak);
	}
	ASTOBJ_UNREF(client, jingle_member_destroy);
	return IKS_FILTER_EAT;
}
/* Not using this anymore probably take out soon 
static struct jingle_candidate *jingle_create_candidate(char *args)
{
	char *name, *type, *preference, *protocol;
	struct jingle_candidate *res;
	res = malloc(sizeof(struct jingle_candidate));
	memset(res, 0, sizeof(struct jingle_candidate));
	if (args)
		name = args;
	if ((args = strchr(args, ','))) {
		*args = '\0';
		args++;
		preference = args;
	}
	if ((args = strchr(args, ','))) {
		*args = '\0';
		args++;
		protocol = args;
	}
	if ((args = strchr(args, ','))) {
		*args = '\0';
		args++;
		type = args;
	}
	if (name)
		ast_copy_string(res->name, name, sizeof(res->name));
	if (preference) {
		res->preference = atof(preference);
	}
	if (protocol) {
		if (!strcasecmp("udp", protocol))
			res->protocol = AJI_PROTOCOL_UDP;
		if (!strcasecmp("ssltcp", protocol))
			res->protocol = AJI_PROTOCOL_SSLTCP;
	}
	if (type) {
		if (!strcasecmp("stun", type))
			res->type = AJI_CONNECT_STUN;
		if (!strcasecmp("local", type))
			res->type = AJI_CONNECT_LOCAL;
		if (!strcasecmp("relay", type))
			res->type = AJI_CONNECT_RELAY;
	}

	return res;
}
*/

static int jingle_create_member(char *label, struct ast_variable *var, int allowguest,
								struct ast_codec_pref prefs, char *context,
								struct jingle *member)
{
	struct aji_client *client;

	if (!member)
		ast_log(LOG_WARNING, "Out of memory.\n");

	ast_copy_string(member->name, label, sizeof(member->name));
	ast_copy_string(member->user, label, sizeof(member->user));
	ast_copy_string(member->context, context, sizeof(member->context));
	member->allowguest = allowguest;
	member->prefs = prefs;
	while (var) {
#if 0
		struct jingle_candidate *candidate = NULL;
#endif
		if (!strcasecmp(var->name, "username"))
			ast_copy_string(member->user, var->value, sizeof(member->user));
		else if (!strcasecmp(var->name, "disallow"))
			ast_parse_allow_disallow(&member->prefs, &member->capability, var->value, 0);
		else if (!strcasecmp(var->name, "allow"))
			ast_parse_allow_disallow(&member->prefs, &member->capability, var->value, 1);
		else if (!strcasecmp(var->name, "context"))
			ast_copy_string(member->context, var->value, sizeof(member->context));
#if 0
		else if (!strcasecmp(var->name, "candidate")) {
			candidate = jingle_create_candidate(var->value);
			if (candidate) {
				candidate->next = member->ourcandidates;
				member->ourcandidates = candidate;
			}
		}
#endif
		else if (!strcasecmp(var->name, "connection")) {
			if ((client = ast_aji_get_client(var->value))) {
				member->connection = client;
				iks_filter_add_rule(client->f, jingle_parser, member, IKS_RULE_TYPE,
									IKS_PAK_IQ, IKS_RULE_FROM_PARTIAL, member->user,
									IKS_RULE_NS, "http://www.google.com/session",
									IKS_RULE_DONE);
			} else {
				ast_log(LOG_ERROR, "connection referenced not found!\n");
				return 0;
			}
		}
		var = var->next;
	}
	if (member->connection && member->user)
		member->buddy = ASTOBJ_CONTAINER_FIND(&member->connection->buddies, member->user);
	else {
		ast_log(LOG_ERROR, "No Connection or Username!\n");
	}
	return 1;
}

static int jingle_load_config(void)
{
	char *cat = NULL;
	struct ast_config *cfg = NULL;
	char context[100];
	int allowguest = 1;
	struct ast_variable *var;
	struct jingle *member;
	struct ast_codec_pref prefs;
	struct aji_client_container *clients;
	struct jingle_candidate *global_candidates = NULL;

	cfg = ast_config_load(JINGLE_CONFIG);
	if (!cfg)
		return 0;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	cat = ast_category_browse(cfg, NULL);
	for (var = ast_variable_browse(cfg, "general"); var; var = var->next) {
		/* handle jb conf */
		if (!ast_jb_read_conf(&global_jbconf, var->name, var->value))
			continue;

		if (!strcasecmp(var->name, "allowguest"))
			allowguest =
				(ast_true(ast_variable_retrieve(cfg, "general", "allowguest"))) ? 1 : 0;
		else if (!strcasecmp(var->name, "disallow"))
			ast_parse_allow_disallow(&prefs, &global_capability, var->value, 0);
		else if (!strcasecmp(var->name, "allow"))
			ast_parse_allow_disallow(&prefs, &global_capability, var->value, 1);
		else if (!strcasecmp(var->name, "context"))
			ast_copy_string(context, var->value, sizeof(context));
		else if (!strcasecmp(var->name, "externip"))
			ast_copy_string(externip, var->value, sizeof(externip));
/*  Idea to allow for custom candidates  */
/*
		else if (!strcasecmp(var->name, "candidate")) {
			candidate = jingle_create_candidate(var->value);
			if (candidate) {
				candidate->next = global_candidates;
				global_candidates = candidate;
			}
		}
*/
	}
	while (cat) {
		if (strcasecmp(cat, "general")) {
			var = ast_variable_browse(cfg, cat);
			member = (struct jingle *) malloc(sizeof(struct jingle));
			memset(member, 0, sizeof(struct jingle));
			ASTOBJ_INIT(member);
			ASTOBJ_WRLOCK(member);
			if (!strcasecmp(cat, "guest")) {
				ast_copy_string(member->name, "guest", sizeof(member->name));
				ast_copy_string(member->user, "guest", sizeof(member->user));
				ast_copy_string(member->context, context, sizeof(member->context));
				member->allowguest = allowguest;
				member->prefs = prefs;
				while (var) {
					if (!strcasecmp(var->name, "disallow"))
						ast_parse_allow_disallow(&member->prefs, &member->capability,
												 var->value, 0);
					else if (!strcasecmp(var->name, "allow"))
						ast_parse_allow_disallow(&member->prefs, &member->capability,
												 var->value, 1);
					else if (!strcasecmp(var->name, "context"))
						ast_copy_string(member->context, var->value,
										sizeof(member->context));
/*  Idea to allow for custom candidates  */
/*
					else if (!strcasecmp(var->name, "candidate")) {
						candidate = jingle_create_candidate(var->value);
						if (candidate) {
							candidate->next = member->ourcandidates;
							member->ourcandidates = candidate;
						}
					}
*/
					var = var->next;
				}
				ASTOBJ_UNLOCK(member);
				clients = ast_aji_get_clients();
				if (clients) {
					ASTOBJ_CONTAINER_TRAVERSE(clients, 1, {
						ASTOBJ_WRLOCK(iterator);
						ASTOBJ_WRLOCK(member);
						member->connection = iterator;
						iks_filter_add_rule(iterator->f, jingle_parser, member, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_NS,
										"http://www.google.com/session", IKS_RULE_DONE);
						ASTOBJ_UNLOCK(member);
						ASTOBJ_CONTAINER_LINK(&jingles, member);
						ASTOBJ_UNLOCK(iterator);
					});
				} else {
					ASTOBJ_UNLOCK(member);
					ASTOBJ_UNREF(member, jingle_member_destroy);
				}
			} else {
				ASTOBJ_UNLOCK(member);
				if (jingle_create_member(cat, var, allowguest, prefs, context, member))
					ASTOBJ_CONTAINER_LINK(&jingles, member);
				ASTOBJ_UNREF(member, jingle_member_destroy);
			}
		}
		cat = ast_category_browse(cfg, cat);
	}
	jingle_free_candidates(global_candidates);
	return 1;
}

/*! \brief Load module into PBX, register channel */
static int load_module(void)
{
	ASTOBJ_CONTAINER_INIT(&jingles);
	if (!jingle_load_config()) {
		ast_log(LOG_ERROR, "Unable to read config file %s. Not loading module.\n", JINGLE_CONFIG);
		return 0;
	}

	sched = sched_context_create();
	if (!sched) 
		ast_log(LOG_WARNING, "Unable to create schedule context\n");

	io = io_context_create();
	if (!io) 
		ast_log(LOG_WARNING, "Unable to create I/O context\n");

	if (ast_find_ourip(&__ourip, bindaddr)) {
		ast_log(LOG_WARNING, "Unable to get own IP address, Jingle disabled\n");
		return 0;
	}

	ast_rtp_proto_register(&jingle_rtp);

	/* Make sure we can register our channel type */
	if (ast_channel_register(&jingle_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", type);
		return -1;
	}
	return 0;
}

/*! \brief Reload module */
static int reload(void)
{
	return 0;
}

/*! \brief Unload the jingle channel from Asterisk */
static int unload_module(void)
{
	struct jingle_pvt *privates = NULL;

	/* First, take us out of the channel loop */
	ast_channel_unregister(&jingle_tech);
	ast_rtp_proto_unregister(&jingle_rtp);

	if (!ast_mutex_lock(&jinglelock)) {
		/* Hangup all interfaces if they have an owner */
		ASTOBJ_CONTAINER_TRAVERSE(&jingles, 1, {
			ASTOBJ_WRLOCK(iterator);
			privates = iterator->p;
			while(privates) {
			    if (privates->owner)
			        ast_softhangup(privates->owner, AST_SOFTHANGUP_APPUNLOAD);
			    privates = privates->next;
			}
			iterator->p = NULL;
			ASTOBJ_UNLOCK(iterator);
		});
		ast_mutex_unlock(&jinglelock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	ASTOBJ_CONTAINER_DESTROYALL(&jingles, jingle_member_destroy);
	ASTOBJ_CONTAINER_DESTROY(&jingles);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Jingle Channel Driver",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
