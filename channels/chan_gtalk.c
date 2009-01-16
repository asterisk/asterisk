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
 * \brief Gtalk Channel Driver, until google/libjingle works with jingle spec
 * 
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>iksemel</depend>
	<depend>res_jabber</depend>
	<use>gnutls</use>
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
#include <pthread.h>

#ifdef HAVE_GNUTLS
#include <gcrypt.h>
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif /* HAVE_GNUTLS */

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

#define GOOGLE_CONFIG "gtalk.conf"

#define GOOGLE_NS "http://www.google.com/session"


/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf =
{
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = ""
};
static struct ast_jb_conf global_jbconf;

enum gtalk_protocol {
	AJI_PROTOCOL_UDP = 1,
	AJI_PROTOCOL_SSLTCP = 2,
};

enum gtalk_connect_type {
	AJI_CONNECT_STUN = 1,
	AJI_CONNECT_LOCAL = 2,
	AJI_CONNECT_RELAY = 3,
};

struct gtalk_pvt {
	ast_mutex_t lock;                /*!< Channel private lock */
	time_t laststun;
	struct gtalk *parent;	         /*!< Parent client */
	char sid[100];
	char us[AJI_MAX_JIDLEN];
	char them[AJI_MAX_JIDLEN];
	char ring[10];                   /*!< Message ID of ring */
	iksrule *ringrule;               /*!< Rule for matching RING request */
	int initiator;                   /*!< If we're the initiator */
	int alreadygone;
	int capability;
	struct ast_codec_pref prefs;
	struct gtalk_candidate *theircandidates;
	struct gtalk_candidate *ourcandidates;
	char cid_num[80];                /*!< Caller ID num */
	char cid_name[80];               /*!< Caller ID name */
	char exten[80];                  /*!< Called extension */
	struct ast_channel *owner;       /*!< Master Channel */
	struct ast_rtp *rtp;             /*!< RTP audio session */
	struct ast_rtp *vrtp;            /*!< RTP video session */
	int jointcapability;             /*!< Supported capability at both ends (codecs ) */
	int peercapability;
	struct gtalk_pvt *next;	/* Next entity */
};

struct gtalk_candidate {
	char name[100];
	enum gtalk_protocol protocol;
	double preference;
	char username[100];
	char password[100];
	enum gtalk_connect_type type;
	char network[6];
	int generation;
	char ip[16];
	int port;
	int receipt;
	struct gtalk_candidate *next;
};

struct gtalk {
	ASTOBJ_COMPONENTS(struct gtalk);
	struct aji_client *connection;
	struct aji_buddy *buddy;
	struct gtalk_pvt *p;
	struct ast_codec_pref prefs;
	int amaflags;			/*!< AMA Flags */
	char user[AJI_MAX_JIDLEN];
	char context[AST_MAX_CONTEXT];
	char accountcode[AST_MAX_ACCOUNT_CODE];	/*!< Account code */
	int capability;
	ast_group_t callgroup;	/*!< Call group */
	ast_group_t pickupgroup;	/*!< Pickup group */
	int callingpres;		/*!< Calling presentation */
	int allowguest;
	char language[MAX_LANGUAGE];	/*!<  Default language for prompts */
	char musicclass[MAX_MUSICCLASS];	/*!<  Music on Hold class */
};

struct gtalk_container {
	ASTOBJ_CONTAINER_COMPONENTS(struct gtalk);
};

static const char desc[] = "Gtalk Channel";

static int global_capability = AST_FORMAT_ULAW | AST_FORMAT_ALAW | AST_FORMAT_GSM | AST_FORMAT_H263;

AST_MUTEX_DEFINE_STATIC(gtalklock); /*!< Protect the interface list (of gtalk_pvt's) */

/* Forward declarations */
static struct ast_channel *gtalk_request(const char *type, int format, void *data, int *cause);
static int gtalk_digit(struct ast_channel *ast, char digit, unsigned int duration);
static int gtalk_digit_begin(struct ast_channel *ast, char digit);
static int gtalk_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int gtalk_call(struct ast_channel *ast, char *dest, int timeout);
static int gtalk_hangup(struct ast_channel *ast);
static int gtalk_answer(struct ast_channel *ast);
static int gtalk_action(struct gtalk *client, struct gtalk_pvt *p, const char *action);
static void gtalk_free_pvt(struct gtalk *client, struct gtalk_pvt *p);
static int gtalk_newcall(struct gtalk *client, ikspak *pak);
static struct ast_frame *gtalk_read(struct ast_channel *ast);
static int gtalk_write(struct ast_channel *ast, struct ast_frame *f);
static int gtalk_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int gtalk_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int gtalk_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen);
static struct gtalk_pvt *gtalk_alloc(struct gtalk *client, const char *us, const char *them, const char *sid);
static int gtalk_do_reload(int fd, int argc, char **argv);
static int gtalk_show_channels(int fd, int argc, char **argv);
/*----- RTP interface functions */
static int gtalk_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp,
							   struct ast_rtp *vrtp, int codecs, int nat_active);
static enum ast_rtp_get_result gtalk_get_rtp_peer(struct ast_channel *chan, struct ast_rtp **rtp);
static int gtalk_get_codec(struct ast_channel *chan);

/*! \brief PBX interface structure for channel registration */
static const struct ast_channel_tech gtalk_tech = {
	.type = "Gtalk",
	.description = "Gtalk Channel Driver",
	.capabilities = ((AST_FORMAT_MAX_AUDIO << 1) - 1),
	.requester = gtalk_request,
	.send_digit_begin = gtalk_digit_begin,
	.send_digit_end = gtalk_digit_end,
	.bridge = ast_rtp_bridge,
	.call = gtalk_call,
	.hangup = gtalk_hangup,
	.answer = gtalk_answer,
	.read = gtalk_read,
	.write = gtalk_write,
	.exception = gtalk_read,
	.indicate = gtalk_indicate,
	.fixup = gtalk_fixup,
	.send_html = gtalk_sendhtml,
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER
};

static struct sockaddr_in bindaddr = { 0, };	/*!< The address we bind to */

static struct sched_context *sched;	/*!< The scheduling context */
static struct io_context *io;	/*!< The IO context */
static struct in_addr __ourip;


/*! \brief RTP driver interface */
static struct ast_rtp_protocol gtalk_rtp = {
	type: "Gtalk",
	get_rtp_info: gtalk_get_rtp_peer,
	set_rtp_peer: gtalk_set_rtp_peer,
	get_codec: gtalk_get_codec,
};

static char show_channels_usage[] = 
"Usage: gtalk show channels\n" 
"       Shows current state of the Gtalk channels.\n";

static char reload_usage[] = 
"Usage: gtalk reload\n" 
"       Reload gtalk channel driver.\n";


static struct ast_cli_entry gtalk_cli[] = {
	{{ "gtalk", "reload", NULL}, gtalk_do_reload, "Reload GoogleTalk configuration", reload_usage },
	{{ "gtalk", "show", "channels", NULL}, gtalk_show_channels, "Show GoogleTalk channels", show_channels_usage },
 };



static char externip[16];

static struct gtalk_container gtalk_list;

static void gtalk_member_destroy(struct gtalk *obj)
{
	free(obj);
}

static struct gtalk *find_gtalk(char *name, char *connection)
{
	struct gtalk *gtalk = NULL;
	char *domain = NULL , *s = NULL;

	if(strchr(connection, '@')) {
		s = ast_strdupa(connection);
		domain = strsep(&s, "@");
		ast_verbose("OOOOH domain = %s\n", domain);
	}
	gtalk = ASTOBJ_CONTAINER_FIND(&gtalk_list, name);
	if (!gtalk && strchr(name, '@'))
		gtalk = ASTOBJ_CONTAINER_FIND_FULL(&gtalk_list, name, user,,, strcasecmp);

	if (!gtalk) {
		/* guest call */
		ASTOBJ_CONTAINER_TRAVERSE(&gtalk_list, 1, {
			ASTOBJ_RDLOCK(iterator);
			if (!strcasecmp(iterator->name, "guest")) {
				gtalk = iterator;
			}
			ASTOBJ_UNLOCK(iterator);

			if (gtalk)
				break;
		});

	}
	return gtalk;
}


static int add_codec_to_answer(const struct gtalk_pvt *p, int codec, iks *dcodecs)
{
	int res = 0;
	char *format = ast_getformatname(codec);

	if (!strcasecmp("ulaw", format)) {
		iks *payload_eg711u, *payload_pcmu;
		payload_pcmu = iks_new("payload-type");
		payload_eg711u = iks_new("payload-type");
	
		if(!payload_eg711u || !payload_pcmu) {
			if(payload_pcmu)
				iks_delete(payload_pcmu);
			if(payload_eg711u)
				iks_delete(payload_eg711u);
			ast_log(LOG_WARNING,"Failed to allocate iks node");
			return -1;
		}
		iks_insert_attrib(payload_pcmu, "id", "0");
		iks_insert_attrib(payload_pcmu, "name", "PCMU");
		iks_insert_attrib(payload_pcmu, "clockrate","8000");
		iks_insert_attrib(payload_pcmu, "bitrate","64000");
		iks_insert_attrib(payload_eg711u, "id", "100");
		iks_insert_attrib(payload_eg711u, "name", "EG711U");
		iks_insert_attrib(payload_eg711u, "clockrate","8000");
		iks_insert_attrib(payload_eg711u, "bitrate","64000");
		iks_insert_node(dcodecs, payload_pcmu);
		iks_insert_node(dcodecs, payload_eg711u);
		res ++;
	}
	if (!strcasecmp("alaw", format)) {
		iks *payload_eg711a, *payload_pcma;
		payload_pcma = iks_new("payload-type");
		payload_eg711a = iks_new("payload-type");
		if(!payload_eg711a || !payload_pcma) {
			if(payload_eg711a)
				iks_delete(payload_eg711a);
			if(payload_pcma)
				iks_delete(payload_pcma);
			ast_log(LOG_WARNING,"Failed to allocate iks node");
			return -1;
		}
		iks_insert_attrib(payload_pcma, "id", "8");
		iks_insert_attrib(payload_pcma, "name", "PCMA");
		iks_insert_attrib(payload_pcma, "clockrate","8000");
		iks_insert_attrib(payload_pcma, "bitrate","64000");
		payload_eg711a = iks_new("payload-type");
		iks_insert_attrib(payload_eg711a, "id", "101");
		iks_insert_attrib(payload_eg711a, "name", "EG711A");
		iks_insert_attrib(payload_eg711a, "clockrate","8000");
		iks_insert_attrib(payload_eg711a, "bitrate","64000");
		iks_insert_node(dcodecs, payload_pcma);
		iks_insert_node(dcodecs, payload_eg711a);
		res ++;
	}
	if (!strcasecmp("ilbc", format)) {
		iks *payload_ilbc = iks_new("payload-type");
		if(!payload_ilbc) {
			ast_log(LOG_WARNING,"Failed to allocate iks node");
			return -1;
		}
		iks_insert_attrib(payload_ilbc, "id", "97");
		iks_insert_attrib(payload_ilbc, "name", "iLBC");
		iks_insert_attrib(payload_ilbc, "clockrate","8000");
		iks_insert_attrib(payload_ilbc, "bitrate","13300");
		iks_insert_node(dcodecs, payload_ilbc);
		res ++;
	}
	if (!strcasecmp("g723", format)) {
		iks *payload_g723 = iks_new("payload-type");
		if(!payload_g723) {
			ast_log(LOG_WARNING,"Failed to allocate iks node");
			return -1;
		}
		iks_insert_attrib(payload_g723, "id", "4");
		iks_insert_attrib(payload_g723, "name", "G723");
		iks_insert_attrib(payload_g723, "clockrate","8000");
		iks_insert_attrib(payload_g723, "bitrate","6300");
		iks_insert_node(dcodecs, payload_g723);
		res ++;
	}
	if (!strcasecmp("speex", format)) {
		iks *payload_speex = iks_new("payload-type");
		if(!payload_speex) {
			ast_log(LOG_WARNING,"Failed to allocate iks node");
			return -1;
		}
		iks_insert_attrib(payload_speex, "id", "110");
		iks_insert_attrib(payload_speex, "name", "speex");
		iks_insert_attrib(payload_speex, "clockrate","8000");
		iks_insert_attrib(payload_speex, "bitrate","11000");
		iks_insert_node(dcodecs, payload_speex);
		res++;
	}
	if (!strcasecmp("gsm", format)) {
		iks *payload_gsm = iks_new("payload-type");
		if(!payload_gsm) {
			ast_log(LOG_WARNING,"Failed to allocate iks node");
			return -1;
		}
		iks_insert_attrib(payload_gsm, "id", "103");
		iks_insert_attrib(payload_gsm, "name", "gsm");
		iks_insert_node(dcodecs, payload_gsm);
		res++;
	}
	ast_rtp_lookup_code(p->rtp, 1, codec);
	return res;
}

static int gtalk_invite(struct gtalk_pvt *p, char *to, char *from, char *sid, int initiator)
{
	struct gtalk *client = p->parent;
	iks *iq, *gtalk, *dcodecs, *payload_telephone, *transport;
	int x;
	int pref_codec = 0;
	int alreadysent = 0;
	int codecs_num = 0;

	iq = iks_new("iq");
	gtalk = iks_new("session");
	dcodecs = iks_new("description");
	transport = iks_new("transport");
	payload_telephone = iks_new("payload-type");
	if (!(iq && gtalk && dcodecs && transport && payload_telephone)){
		if(iq)
			iks_delete(iq);
		if(gtalk)
			iks_delete(gtalk);
		if(dcodecs)
			iks_delete(dcodecs);
		if(transport)
			iks_delete(transport);
		if(payload_telephone)
			iks_delete(payload_telephone);

		ast_log(LOG_ERROR, "Could not allocate iksemel nodes\n");
		return 0;
	}
	iks_insert_attrib(dcodecs, "xmlns", "http://www.google.com/session/phone");
	iks_insert_attrib(dcodecs, "xml:lang", "en");

	for (x = 0; x < 32; x++) {
		if (!(pref_codec = ast_codec_pref_index(&client->prefs, x)))
			break;
		if (!(client->capability & pref_codec))
			continue;
		if (alreadysent & pref_codec)
			continue;
		codecs_num = add_codec_to_answer(p, pref_codec, dcodecs);
		alreadysent |= pref_codec;
	}
	
	if (codecs_num) {
		/* only propose DTMF within an audio session */
		iks_insert_attrib(payload_telephone, "id", "106");
		iks_insert_attrib(payload_telephone, "name", "telephone-event");
		iks_insert_attrib(payload_telephone, "clockrate", "8000");
	}
	iks_insert_attrib(transport,"xmlns","http://www.google.com/transport/p2p");
	
	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(iq, "to", to);
	iks_insert_attrib(iq, "from", from);
	iks_insert_attrib(iq, "id", client->connection->mid);
	ast_aji_increment_mid(client->connection->mid);

	iks_insert_attrib(gtalk, "xmlns", "http://www.google.com/session");
	iks_insert_attrib(gtalk, "type",initiator ? "initiate": "accept");
	iks_insert_attrib(gtalk, "initiator", initiator ? from : to);
	iks_insert_attrib(gtalk, "id", sid);
	iks_insert_node(iq, gtalk);
	iks_insert_node(gtalk, dcodecs);
	iks_insert_node(gtalk, transport);
	iks_insert_node(dcodecs, payload_telephone);

	iks_send(client->connection->p, iq);
	iks_delete(payload_telephone);
	iks_delete(transport);
	iks_delete(dcodecs);
	iks_delete(gtalk);
	iks_delete(iq);
	return 1;
}

static int gtalk_invite_response(struct gtalk_pvt *p, char *to , char *from, char *sid, int initiator)
{
	iks *iq, *session, *transport;
	iq = iks_new("iq");
	session = iks_new("session");
	transport = iks_new("transport");
	if(!(iq && session && transport)) {
		if(iq)
			iks_delete(iq);
		if(session)
			iks_delete(session);
		if(transport)
			iks_delete(transport);
		ast_log(LOG_ERROR, " Unable to allocate IKS node\n");
		return -1;
	}
	iks_insert_attrib(iq, "from", from);
	iks_insert_attrib(iq, "to", to);
	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(iq, "id",p->parent->connection->mid);
	ast_aji_increment_mid(p->parent->connection->mid);
	iks_insert_attrib(session, "type", "transport-accept");
	iks_insert_attrib(session, "id", sid);
	iks_insert_attrib(session, "initiator", initiator ? from : to);
	iks_insert_attrib(session, "xmlns", "http://www.google.com/session");
	iks_insert_attrib(transport, "xmlns", "http://www.google.com/transport/p2p");
	iks_insert_node(iq,session);
	iks_insert_node(session,transport);
	iks_send(p->parent->connection->p, iq);
	iks_delete(transport);
	iks_delete(session);
	iks_delete(iq);
	return 1;

}

static int gtalk_ringing_ack(void *data, ikspak *pak)
{
	struct gtalk_pvt *p = data;

	if (p->ringrule)
		iks_filter_remove_rule(p->parent->connection->f, p->ringrule);
	p->ringrule = NULL;
	if (p->owner)
		ast_queue_control(p->owner, AST_CONTROL_RINGING);
	return IKS_FILTER_EAT;
}

static int gtalk_answer(struct ast_channel *ast)
{
	struct gtalk_pvt *p = ast->tech_pvt;
	int res = 0;
	
	if (option_debug)
		ast_log(LOG_DEBUG, "Answer!\n");
	ast_mutex_lock(&p->lock);
	gtalk_invite(p, p->them, p->us,p->sid, 0);
	ast_mutex_unlock(&p->lock);
	return res;
}

static enum ast_rtp_get_result gtalk_get_rtp_peer(struct ast_channel *chan, struct ast_rtp **rtp)
{
	struct gtalk_pvt *p = chan->tech_pvt;
	enum ast_rtp_get_result res = AST_RTP_GET_FAILED;

	if (!p)
		return res;

	ast_mutex_lock(&p->lock);
	if (p->rtp){
		*rtp = p->rtp;
		res = AST_RTP_TRY_PARTIAL;
	}
	ast_mutex_unlock(&p->lock);

	return res;
}

static int gtalk_get_codec(struct ast_channel *chan)
{
	struct gtalk_pvt *p = chan->tech_pvt;
	return p->peercapability;
}

static int gtalk_set_rtp_peer(struct ast_channel *chan, struct ast_rtp *rtp, struct ast_rtp *vrtp, int codecs, int nat_active)
{
	struct gtalk_pvt *p;

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

static int gtalk_response(struct gtalk *client, char *from, ikspak *pak, const char *reasonstr, const char *reasonstr2)
{
	iks *response = NULL, *error = NULL, *reason = NULL;
	int res = -1;

	response = iks_new("iq");
	if (response) {
		iks_insert_attrib(response, "type", "result");
		iks_insert_attrib(response, "from", from);
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

static int gtalk_is_answered(struct gtalk *client, ikspak *pak)
{
	struct gtalk_pvt *tmp;
	char *from;
	iks *codec;
	char s1[BUFSIZ], s2[BUFSIZ], s3[BUFSIZ];
	int peernoncodeccapability;

	ast_log(LOG_DEBUG, "The client is %s\n", client->name);
	/* Make sure our new call doesn't exist yet */
	for (tmp = client->p; tmp; tmp = tmp->next) {
		if (iks_find_with_attrib(pak->x, "session", "id", tmp->sid))
			break;
	}

	/* codec points to the first <payload-type/> tag */
	codec = iks_child(iks_child(iks_child(pak->x)));
	while (codec) {
		ast_rtp_set_m_type(tmp->rtp, atoi(iks_find_attrib(codec, "id")));
		ast_rtp_set_rtpmap_type(tmp->rtp, atoi(iks_find_attrib(codec, "id")), "audio", iks_find_attrib(codec, "name"), 0);
		codec = iks_next(codec);
	}
	
	/* Now gather all of the codecs that we are asked for */
	ast_rtp_get_current_formats(tmp->rtp, &tmp->peercapability, &peernoncodeccapability);
	
	/* at this point, we received an awser from the remote Gtalk client,
	   which allows us to compare capabilities */
	tmp->jointcapability = tmp->capability & tmp->peercapability;
	if (!tmp->jointcapability) {
		ast_log(LOG_WARNING, "Capabilities don't match : us - %s, peer - %s, combined - %s \n", ast_getformatname_multiple(s1, BUFSIZ, tmp->capability),
			ast_getformatname_multiple(s2, BUFSIZ, tmp->peercapability),
			ast_getformatname_multiple(s3, BUFSIZ, tmp->jointcapability));
		/* close session if capabilities don't match */
		ast_queue_hangup(tmp->owner);

		return -1;

	}	
	
	from = iks_find_attrib(pak->x, "to");
	if(!from)
		from = client->connection->jid->full;

	if (tmp) {
		if (tmp->owner)
			ast_queue_control(tmp->owner, AST_CONTROL_ANSWER);
	} else
		ast_log(LOG_NOTICE, "Whoa, didn't find call!\n");
	gtalk_response(client, from, pak, NULL, NULL);
	return 1;
}

static int gtalk_is_accepted(struct gtalk *client, ikspak *pak)
{
	struct gtalk_pvt *tmp;
	char *from;

	ast_log(LOG_DEBUG, "The client is %s\n", client->name);
	/* find corresponding call */
	for (tmp = client->p; tmp; tmp = tmp->next) {
		if (iks_find_with_attrib(pak->x, "session", "id", tmp->sid))
			break;
	}

	from = iks_find_attrib(pak->x, "to");
	if(!from)
		from = client->connection->jid->full;

	if (!tmp)
		ast_log(LOG_NOTICE, "Whoa, didn't find call!\n");

	/* answer 'iq' packet to let the remote peer know that we're alive */
	gtalk_response(client, from, pak, NULL, NULL);
	return 1;
}

static int gtalk_handle_dtmf(struct gtalk *client, ikspak *pak)
{
	struct gtalk_pvt *tmp;
	iks *dtmfnode = NULL, *dtmfchild = NULL;
	char *dtmf;
	char *from;
	/* Make sure our new call doesn't exist yet */
	for (tmp = client->p; tmp; tmp = tmp->next) {
		if (iks_find_with_attrib(pak->x, "session", "id", tmp->sid) || iks_find_with_attrib(pak->x, "gtalk", "sid", tmp->sid))
			break;
	}
	from = iks_find_attrib(pak->x, "to");
	if(!from)
		from = client->connection->jid->full;


	if (tmp) {
		if(iks_find_with_attrib(pak->x, "dtmf-method", "method", "rtp")) {
			gtalk_response(client, from, pak,
					"feature-not-implemented xmlns='urn:ietf:params:xml:ns:xmpp-stanzas'",
					"unsupported-dtmf-method xmlns='http://jabber.org/protocol/gtalk/info/dtmf#errors'");
			return -1;
		}
		if ((dtmfnode = iks_find(pak->x, "dtmf"))) {
			if((dtmf = iks_find_attrib(dtmfnode, "code"))) {
				if(iks_find_with_attrib(pak->x, "dtmf", "action", "button-up")) {
					struct ast_frame f = {AST_FRAME_DTMF_BEGIN, };
					f.subclass = dtmf[0];
					ast_queue_frame(tmp->owner, &f);
					ast_verbose("GOOGLE! DTMF-relay event received: %c\n", f.subclass);
				} else if(iks_find_with_attrib(pak->x, "dtmf", "action", "button-down")) {
					struct ast_frame f = {AST_FRAME_DTMF_END, };
					f.subclass = dtmf[0];
					ast_queue_frame(tmp->owner, &f);
					ast_verbose("GOOGLE! DTMF-relay event received: %c\n", f.subclass);
				} else if(iks_find_attrib(pak->x, "dtmf")) { /* 250 millasecond default */
					struct ast_frame f = {AST_FRAME_DTMF, };
					f.subclass = dtmf[0];
					ast_queue_frame(tmp->owner, &f);
					ast_verbose("GOOGLE! DTMF-relay event received: %c\n", f.subclass);
				}
			}
		} else if ((dtmfnode = iks_find_with_attrib(pak->x, "gtalk", "action", "session-info"))) {
			if((dtmfchild = iks_find(dtmfnode, "dtmf"))) {
				if((dtmf = iks_find_attrib(dtmfchild, "code"))) {
					if(iks_find_with_attrib(dtmfnode, "dtmf", "action", "button-up")) {
						struct ast_frame f = {AST_FRAME_DTMF_END, };
						f.subclass = dtmf[0];
						ast_queue_frame(tmp->owner, &f);
						ast_verbose("GOOGLE! DTMF-relay event received: %c\n", f.subclass);
					} else if(iks_find_with_attrib(dtmfnode, "dtmf", "action", "button-down")) {
						struct ast_frame f = {AST_FRAME_DTMF_BEGIN, };
						f.subclass = dtmf[0];
						ast_queue_frame(tmp->owner, &f);
						ast_verbose("GOOGLE! DTMF-relay event received: %c\n", f.subclass);
					}
				}
			}
		}
		gtalk_response(client, from, pak, NULL, NULL);
		return 1;
	} else
		ast_log(LOG_NOTICE, "Whoa, didn't find call!\n");

	gtalk_response(client, from, pak, NULL, NULL);
	return 1;
}

static int gtalk_hangup_farend(struct gtalk *client, ikspak *pak)
{
	struct gtalk_pvt *tmp;
	char *from;

	ast_log(LOG_DEBUG, "The client is %s\n", client->name);
	/* Make sure our new call doesn't exist yet */
	for (tmp = client->p; tmp; tmp = tmp->next) {
		if (iks_find_with_attrib(pak->x, "session", "id", tmp->sid))
			break;
	}
	from = iks_find_attrib(pak->x, "to");
	if(!from)
		from = client->connection->jid->full;

	if (tmp) {
		tmp->alreadygone = 1;
		if (tmp->owner)
			ast_queue_hangup(tmp->owner);
	} else
		ast_log(LOG_NOTICE, "Whoa, didn't find call!\n");
	gtalk_response(client, from, pak, NULL, NULL);
	return 1;
}

static int gtalk_create_candidates(struct gtalk *client, struct gtalk_pvt *p, char *sid, char *from, char *to)
{
	struct gtalk_candidate *tmp;
	struct aji_client *c = client->connection;
	struct gtalk_candidate *ours1 = NULL, *ours2 = NULL;
	struct sockaddr_in sin;
	struct sockaddr_in dest;
	struct in_addr us;
	iks *iq, *gtalk, *candidate, *transport;
	char user[17], pass[17], preference[5], port[7];


	iq = iks_new("iq");
	gtalk = iks_new("session");
	candidate = iks_new("candidate");
	transport = iks_new("transport");
	if (!iq || !gtalk || !candidate || !transport) {
		ast_log(LOG_ERROR, "Memory allocation error\n");
		goto safeout;
	}
	ours1 = ast_calloc(1, sizeof(*ours1));
	ours2 = ast_calloc(1, sizeof(*ours2));
	if (!ours1 || !ours2)
		goto safeout;

	iks_insert_attrib(transport, "xmlns","http://www.google.com/transport/p2p");
	iks_insert_node(iq, gtalk);
	iks_insert_node(gtalk,transport);
	iks_insert_node(transport, candidate);

	for (; p; p = p->next) {
		if (!strcasecmp(p->sid, sid))
			break;
	}

	if (!p) {
		ast_log(LOG_NOTICE, "No matching gtalk session - SID %s!\n", sid);
		goto safeout;
	}

	ast_rtp_get_us(p->rtp, &sin);
	ast_find_ourip(&us, bindaddr);

	/* Setup our gtalk candidates */
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
		iks_insert_attrib(iq, "from", to);
		iks_insert_attrib(iq, "to", from);
		iks_insert_attrib(iq, "type", "set");
		iks_insert_attrib(iq, "id", c->mid);
		ast_aji_increment_mid(c->mid);
		iks_insert_attrib(gtalk, "type", "transport-info");
		iks_insert_attrib(gtalk, "id", sid);
		iks_insert_attrib(gtalk, "initiator", (p->initiator) ? to : from);
		iks_insert_attrib(gtalk, "xmlns", GOOGLE_NS);
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
	if (gtalk)
		iks_delete(gtalk);
	if (candidate)
		iks_delete(candidate);
	if(transport)
		iks_delete(transport);
	return 1;
}

static struct gtalk_pvt *gtalk_alloc(struct gtalk *client, const char *us, const char *them, const char *sid)
{
	struct gtalk_pvt *tmp = NULL;
	struct aji_resource *resources = NULL;
	struct aji_buddy *buddy;
	char idroster[200];
	char *data, *exten = NULL;

	if (option_debug)
		ast_log(LOG_DEBUG, "The client is %s for alloc\n", client->name);
	if (!sid && !strchr(them, '/')) {	/* I started call! */
		if (!strcasecmp(client->name, "guest")) {
			buddy = ASTOBJ_CONTAINER_FIND(&client->connection->buddies, them);
			if (buddy)
				resources = buddy->resources;
		} else if (client->buddy)
			resources = client->buddy->resources;
		while (resources) {
			if (resources->cap->jingle) {
				break;
			}
			resources = resources->next;
		}
		if (resources)
			snprintf(idroster, sizeof(idroster), "%s/%s", them, resources->resource);
		else {
			ast_log(LOG_ERROR, "no gtalk capable clients to talk to.\n");
			return NULL;
		}
	}
	if (!(tmp = ast_calloc(1, sizeof(*tmp)))) {
		return NULL;
	}
	if (sid) {
		ast_copy_string(tmp->sid, sid, sizeof(tmp->sid));
		ast_copy_string(tmp->them, them, sizeof(tmp->them));
		ast_copy_string(tmp->us, us, sizeof(tmp->us));
	} else {
		snprintf(tmp->sid, sizeof(tmp->sid), "%08lx%08lx", ast_random(), ast_random());
		ast_copy_string(tmp->them, idroster, sizeof(tmp->them));
		ast_copy_string(tmp->us, us, sizeof(tmp->us));
		tmp->initiator = 1;
	}
	/* clear codecs */
	tmp->rtp = ast_rtp_new_with_bindaddr(sched, io, 1, 0, bindaddr.sin_addr);
	ast_rtp_pt_clear(tmp->rtp);

	/* add user configured codec capabilites */
	if (client->capability)
		tmp->capability = client->capability;
	else if (global_capability)
		tmp->capability = global_capability;

	tmp->parent = client;
	if (!tmp->rtp) {
		ast_log(LOG_WARNING, "Out of RTP sessions?\n");
		free(tmp);
		return NULL;
	}

	/* Set CALLERID(name) to the full JID of the remote peer */
	ast_copy_string(tmp->cid_name, tmp->them, sizeof(tmp->cid_name));

	if(strchr(tmp->us, '/')) {
		data = ast_strdupa(tmp->us);
		exten = strsep(&data, "/");
	} else
		exten = tmp->us;
	ast_copy_string(tmp->exten,  exten, sizeof(tmp->exten));
	ast_mutex_init(&tmp->lock);
	ast_mutex_lock(&gtalklock);
	tmp->next = client->p;
	client->p = tmp;
	ast_mutex_unlock(&gtalklock);
	return tmp;
}

/*! \brief Start new gtalk channel */
static struct ast_channel *gtalk_new(struct gtalk *client, struct gtalk_pvt *i, int state, const char *title)
{
	struct ast_channel *tmp;
	int fmt;
	int what;
	const char *n2;

	if (title)
		n2 = title;
	else
		n2 = i->us;
	tmp = ast_channel_alloc(1, state, i->cid_num, i->cid_name, client->accountcode, i->exten, client->context, client->amaflags, "Gtalk/%s-%04lx", n2, ast_random() & 0xffff);
	if (!tmp) {
		ast_log(LOG_WARNING, "Unable to allocate Gtalk channel structure!\n");
		return NULL;
	}
	tmp->tech = &gtalk_tech;

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

	if (i->rtp) {
		ast_rtp_setstun(i->rtp, 1);
		tmp->fds[0] = ast_rtp_fd(i->rtp);
		tmp->fds[1] = ast_rtcp_fd(i->rtp);
	}
	if (i->vrtp) {
		ast_rtp_setstun(i->rtp, 1);
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
	ast_module_ref(ast_module_info->self);
	ast_copy_string(tmp->context, client->context, sizeof(tmp->context));
	ast_copy_string(tmp->exten, i->exten, sizeof(tmp->exten));

	if (!ast_strlen_zero(i->exten) && strcmp(i->exten, "s"))
		tmp->cid.cid_dnid = ast_strdup(i->exten);
	tmp->priority = 1;
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

static int gtalk_action(struct gtalk *client, struct gtalk_pvt *p, const char *action)
{
	iks *request, *session = NULL;
	int res = -1;

	request = iks_new("iq");
	if (request) {
		iks_insert_attrib(request, "type", "set");
		iks_insert_attrib(request, "from", p->us);
		iks_insert_attrib(request, "to", p->them);
		iks_insert_attrib(request, "id", client->connection->mid);
		ast_aji_increment_mid(client->connection->mid);
		session = iks_new("session");
		if (session) {
			iks_insert_attrib(session, "type", action);
			iks_insert_attrib(session, "id", p->sid);
			iks_insert_attrib(session, "initiator", p->initiator ? p->us : p->them);
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

static void gtalk_free_candidates(struct gtalk_candidate *candidate)
{
	struct gtalk_candidate *last;
	while (candidate) {
		last = candidate;
		candidate = candidate->next;
		free(last);
	}
}

static void gtalk_free_pvt(struct gtalk *client, struct gtalk_pvt *p)
{
	struct gtalk_pvt *cur, *prev = NULL;
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
	gtalk_free_candidates(p->theircandidates);
	free(p);
}


static int gtalk_newcall(struct gtalk *client, ikspak *pak)
{
	struct gtalk_pvt *p, *tmp = client->p;
	struct ast_channel *chan;
	int res;
	iks *codec;
	char *from = NULL;
	char s1[BUFSIZ], s2[BUFSIZ], s3[BUFSIZ];
	int peernoncodeccapability;

	/* Make sure our new call doesn't exist yet */
	from = iks_find_attrib(pak->x,"to");
	if(!from)
		from = client->connection->jid->full;
	
	while (tmp) {
		if (iks_find_with_attrib(pak->x, "session", "id", tmp->sid)) {
			ast_log(LOG_NOTICE, "Ignoring duplicate call setup on SID %s\n", tmp->sid);
			gtalk_response(client, from, pak, "out-of-order", NULL);
			return -1;
		}
		tmp = tmp->next;
	}

 	if (!strcasecmp(client->name, "guest")){
 		/* the guest account is not tied to any configured XMPP client,
 		   let's set it now */
 		client->connection = ast_aji_get_client(from);
 		if (!client->connection) {
 			ast_log(LOG_ERROR, "No XMPP client to talk to, us (partial JID) : %s\n", from);
 			return -1;
 		}
 	}

	p = gtalk_alloc(client, from, pak->from->full, iks_find_attrib(pak->query, "id"));
	if (!p) {
		ast_log(LOG_WARNING, "Unable to allocate gtalk structure!\n");
		return -1;
	}

	chan = gtalk_new(client, p, AST_STATE_DOWN, pak->from->user);
	if (!chan) {
		gtalk_free_pvt(client, p);
		return -1;
	}

	ast_mutex_lock(&p->lock);
	ast_copy_string(p->them, pak->from->full, sizeof(p->them));
	if (iks_find_attrib(pak->query, "id")) {
		ast_copy_string(p->sid, iks_find_attrib(pak->query, "id"),
				sizeof(p->sid));
	}

	/* codec points to the first <payload-type/> tag */	
	codec = iks_child(iks_child(iks_child(pak->x)));
	
	while (codec) {
		ast_rtp_set_m_type(p->rtp, atoi(iks_find_attrib(codec, "id")));
		ast_rtp_set_rtpmap_type(p->rtp, atoi(iks_find_attrib(codec, "id")), "audio", iks_find_attrib(codec, "name"), 0);
		codec = iks_next(codec);
	}
	
	/* Now gather all of the codecs that we are asked for */
	ast_rtp_get_current_formats(p->rtp, &p->peercapability, &peernoncodeccapability);
	p->jointcapability = p->capability & p->peercapability;
	ast_mutex_unlock(&p->lock);
		
	ast_setstate(chan, AST_STATE_RING);
	if (!p->jointcapability) {
		ast_log(LOG_WARNING, "Capabilities don't match : us - %s, peer - %s, combined - %s \n", ast_getformatname_multiple(s1, BUFSIZ, p->capability),
			ast_getformatname_multiple(s2, BUFSIZ, p->peercapability),
			ast_getformatname_multiple(s3, BUFSIZ, p->jointcapability));
		/* close session if capabilities don't match */
		gtalk_action(client, p, "reject");
		p->alreadygone = 1;
		gtalk_hangup(chan);
		ast_channel_free(chan);
		return -1;
	}	

	res = ast_pbx_start(chan);
	
	switch (res) {
	case AST_PBX_FAILED:
		ast_log(LOG_WARNING, "Failed to start PBX :(\n");
		gtalk_response(client, from, pak, "service-unavailable", NULL);
		break;
	case AST_PBX_CALL_LIMIT:
		ast_log(LOG_WARNING, "Failed to start PBX (call limit reached) \n");
		gtalk_response(client, from, pak, "service-unavailable", NULL);
		break;
	case AST_PBX_SUCCESS:
		gtalk_response(client, from, pak, NULL, NULL);
		gtalk_invite_response(p, p->them, p->us,p->sid, 0);
		gtalk_create_candidates(client, p, p->sid, p->them, p->us);
		/* nothing to do */
		break;
	}

	return 1;
}

static int gtalk_update_stun(struct gtalk *client, struct gtalk_pvt *p)
{
	struct gtalk_candidate *tmp;
	struct hostent *hp;
	struct ast_hostent ahp;
	struct sockaddr_in sin;
	struct sockaddr_in aux;

	if (time(NULL) == p->laststun)
		return 0;

	tmp = p->theircandidates;
	p->laststun = time(NULL);
	while (tmp) {
		char username[256];

		/* Find the IP address of the host */
		hp = ast_gethostbyname(tmp->ip, &ahp);
		sin.sin_family = AF_INET;
		memcpy(&sin.sin_addr, hp->h_addr, sizeof(sin.sin_addr));
		sin.sin_port = htons(tmp->port);
		snprintf(username, sizeof(username), "%s%s", tmp->username,
			 p->ourcandidates->username);
		
		/* Find out the result of the STUN */
		ast_rtp_get_peer(p->rtp, &aux);

		/* If the STUN result is different from the IP of the hostname,
			lock on the stun IP of the hostname advertised by the
			remote client */
		if (aux.sin_addr.s_addr && 
		    aux.sin_addr.s_addr != sin.sin_addr.s_addr)
			ast_rtp_stun_request(p->rtp, &aux, username);
		else 
			ast_rtp_stun_request(p->rtp, &sin, username);
		
		if (aux.sin_addr.s_addr && option_debug > 3) {
			ast_log(LOG_DEBUG, "Receiving RTP traffic from IP %s, matches with remote candidate's IP %s\n", ast_inet_ntoa(aux.sin_addr), tmp->ip);
			ast_log(LOG_DEBUG, "Sending STUN request to %s\n", tmp->ip);
		}

		tmp = tmp->next;
	}
	return 1;
}

static int gtalk_add_candidate(struct gtalk *client, ikspak *pak)
{
	struct gtalk_pvt *p = NULL, *tmp = NULL;
	struct aji_client *c = client->connection;
	struct gtalk_candidate *newcandidate = NULL;
	iks *traversenodes = NULL, *receipt = NULL;
	char *from;

	from = iks_find_attrib(pak->x,"to");
	if(!from)
		from = c->jid->full;

	for (tmp = client->p; tmp; tmp = tmp->next) {
		if (iks_find_with_attrib(pak->x, "session", "id", tmp->sid)) {
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
		if(!strcasecmp(iks_name(traversenodes), "transport")) {
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
			gtalk_update_stun(p->parent, p);
			newcandidate = NULL;
		}
		traversenodes = iks_next(traversenodes);
	}
	
	receipt = iks_new("iq");
	iks_insert_attrib(receipt, "type", "result");
	iks_insert_attrib(receipt, "from", from);
	iks_insert_attrib(receipt, "to", iks_find_attrib(pak->x, "from"));
	iks_insert_attrib(receipt, "id", iks_find_attrib(pak->x, "id"));
	iks_send(c->p, receipt);
	iks_delete(receipt);

	return 1;
}

static struct ast_frame *gtalk_rtp_read(struct ast_channel *ast, struct gtalk_pvt *p)
{
	struct ast_frame *f;

	if (!p->rtp)
		return &ast_null_frame;
	f = ast_rtp_read(p->rtp);
	gtalk_update_stun(p->parent, p);
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

static struct ast_frame *gtalk_read(struct ast_channel *ast)
{
	struct ast_frame *fr;
	struct gtalk_pvt *p = ast->tech_pvt;

	ast_mutex_lock(&p->lock);
	fr = gtalk_rtp_read(ast, p);
	ast_mutex_unlock(&p->lock);
	return fr;
}

/*! \brief Send frame to media channel (rtp) */
static int gtalk_write(struct ast_channel *ast, struct ast_frame *frame)
{
	struct gtalk_pvt *p = ast->tech_pvt;
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
		ast_log(LOG_WARNING, "Can't send %d type frames with Gtalk write\n",
				frame->frametype);
		return 0;
	}

	return res;
}

static int gtalk_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct gtalk_pvt *p = newchan->tech_pvt;
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

static int gtalk_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
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

static int gtalk_digit_begin(struct ast_channel *chan, char digit)
{
	return gtalk_digit(chan, digit, 0);
}

static int gtalk_digit_end(struct ast_channel *chan, char digit, unsigned int duration)
{
	return gtalk_digit(chan, digit, duration);
}

static int gtalk_digit(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct gtalk_pvt *p = ast->tech_pvt;
	struct gtalk *client = p->parent;
	iks *iq, *gtalk, *dtmf;
	char buffer[2] = {digit, '\0'};
	iq = iks_new("iq");
	gtalk = iks_new("gtalk");
	dtmf = iks_new("dtmf");
	if(!iq || !gtalk || !dtmf) {
		if(iq)
			iks_delete(iq);
		if(gtalk)
			iks_delete(gtalk);
		if(dtmf)
			iks_delete(dtmf);
		ast_log(LOG_ERROR, "Did not send dtmf do to memory issue\n");
		return -1;
	}

	iks_insert_attrib(iq, "type", "set");
	iks_insert_attrib(iq, "to", p->them);
	iks_insert_attrib(iq, "from", p->us);
	iks_insert_attrib(iq, "id", client->connection->mid);
	ast_aji_increment_mid(client->connection->mid);
	iks_insert_attrib(gtalk, "xmlns", "http://jabber.org/protocol/gtalk");
	iks_insert_attrib(gtalk, "action", "session-info");
	iks_insert_attrib(gtalk, "initiator", p->initiator ? p->us: p->them);
	iks_insert_attrib(gtalk, "sid", p->sid);
	iks_insert_attrib(dtmf, "xmlns", "http://jabber.org/protocol/gtalk/info/dtmf");
	iks_insert_attrib(dtmf, "code", buffer);
	iks_insert_node(iq, gtalk);
	iks_insert_node(gtalk, dtmf);

	ast_mutex_lock(&p->lock);
	if (ast->dtmff.frametype == AST_FRAME_DTMF_BEGIN || duration == 0) {
		iks_insert_attrib(dtmf, "action", "button-down");
	} else if (ast->dtmff.frametype == AST_FRAME_DTMF_END || duration != 0) {
		iks_insert_attrib(dtmf, "action", "button-up");
	}
	iks_send(client->connection->p, iq);
	iks_delete(iq);
	iks_delete(gtalk);
	iks_delete(dtmf);
	ast_mutex_unlock(&p->lock);
	return 0;
}

static int gtalk_sendhtml(struct ast_channel *ast, int subclass, const char *data, int datalen)
{
	ast_log(LOG_NOTICE, "XXX Implement gtalk sendhtml XXX\n");

	return -1;
}

/* Not in use right now.
static int gtalk_auto_congest(void *nothing)
{
	struct gtalk_pvt *p = nothing;

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
static int gtalk_call(struct ast_channel *ast, char *dest, int timeout)
{
	struct gtalk_pvt *p = ast->tech_pvt;

	if ((ast->_state != AST_STATE_DOWN) && (ast->_state != AST_STATE_RESERVED)) {
		ast_log(LOG_WARNING, "gtalk_call called on %s, neither down nor reserved\n", ast->name);
		return -1;
	}

	ast_setstate(ast, AST_STATE_RING);
	if (!p->ringrule) {
		ast_copy_string(p->ring, p->parent->connection->mid, sizeof(p->ring));
		p->ringrule = iks_filter_add_rule(p->parent->connection->f, gtalk_ringing_ack, p,
							IKS_RULE_ID, p->ring, IKS_RULE_DONE);
	} else
		ast_log(LOG_WARNING, "Whoa, already have a ring rule!\n");

	gtalk_invite(p, p->them, p->us, p->sid, 1);
	gtalk_create_candidates(p->parent, p, p->sid, p->them, p->us);

	return 0;
}

/*! \brief Hangup a call through the gtalk proxy channel */
static int gtalk_hangup(struct ast_channel *ast)
{
	struct gtalk_pvt *p = ast->tech_pvt;
	struct gtalk *client;

	ast_mutex_lock(&p->lock);
	client = p->parent;
	p->owner = NULL;
	ast->tech_pvt = NULL;
	if (!p->alreadygone)
		gtalk_action(client, p, "terminate");
	ast_mutex_unlock(&p->lock);

	gtalk_free_pvt(client, p);
	ast_module_unref(ast_module_info->self);

	return 0;
}

/*! \brief Part of PBX interface */
static struct ast_channel *gtalk_request(const char *type, int format, void *data, int *cause)
{
	struct gtalk_pvt *p = NULL;
	struct gtalk *client = NULL;
	char *sender = NULL, *to = NULL, *s = NULL;
	struct ast_channel *chan = NULL;

	if (data) {
		s = ast_strdupa(data);
		if (s) {
			sender = strsep(&s, "/");
			if (sender && (sender[0] != '\0'))
				to = strsep(&s, "/");
			if (!to) {
				ast_log(LOG_ERROR, "Bad arguments in Gtalk Dialstring: %s\n", (char*) data);
				return NULL;
			}
		}
	}

	client = find_gtalk(to, sender);
	if (!client) {
		ast_log(LOG_WARNING, "Could not find recipient.\n");
		return NULL;
	}
	if (!strcasecmp(client->name, "guest")){
		/* the guest account is not tied to any configured XMPP client,
		   let's set it now */
		client->connection = ast_aji_get_client(sender);
		if (!client->connection) {
			ast_log(LOG_ERROR, "No XMPP client to talk to, us (partial JID) : %s\n", sender);
			ASTOBJ_UNREF(client, gtalk_member_destroy);
			return NULL;
		}
	}

	ASTOBJ_WRLOCK(client);
	p = gtalk_alloc(client, strchr(sender, '@') ? sender : client->connection->jid->full, strchr(to, '@') ? to : client->user, NULL);
	if (p)
		chan = gtalk_new(client, p, AST_STATE_DOWN, to);

	ASTOBJ_UNLOCK(client);
	return chan;
}

/*! \brief CLI command "gtalk show channels" */
static int gtalk_show_channels(int fd, int argc, char **argv)
{
#define FORMAT  "%-30.30s  %-30.30s  %-15.15s  %-5.5s %-5.5s \n"
	struct gtalk_pvt *p;
	struct ast_channel *chan;
	int numchans = 0;
	char them[AJI_MAX_JIDLEN];
	char *jid = NULL;
	char *resource = NULL;

	if (argc != 3)
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(&gtalklock);
	ast_cli(fd, FORMAT, "Channel", "Jabber ID", "Resource", "Read", "Write");
	ASTOBJ_CONTAINER_TRAVERSE(&gtalk_list, 1, {
		ASTOBJ_WRLOCK(iterator);
		p = iterator->p;
		while(p) {
			chan = p->owner;
			ast_copy_string(them, p->them, sizeof(them));
			jid = them;
			resource = strchr(them, '/');
			if (!resource)
				resource = "None";
			else {
				*resource = '\0';
				resource ++;
			}
			if (chan)
				ast_cli(fd, FORMAT, 
					chan->name,
					jid,
					resource,
					ast_getformatname(chan->readformat),
					ast_getformatname(chan->writeformat)					
					);
			else 
				ast_log(LOG_WARNING, "No available channel\n");
			numchans ++;
			p = p->next;
		}
		ASTOBJ_UNLOCK(iterator);
	});

	ast_mutex_unlock(&gtalklock);

	ast_cli(fd, "%d active gtalk channel%s\n", numchans, (numchans != 1) ? "s" : "");
	return RESULT_SUCCESS;
#undef FORMAT
}

/*! \brief CLI command "gtalk show channels" */
static int gtalk_do_reload(int fd, int argc, char **argv)
{
	ast_verbose("IT DOES WORK!\n");
	return RESULT_SUCCESS;
}

static int gtalk_parser(void *data, ikspak *pak)
{
	struct gtalk *client = ASTOBJ_REF((struct gtalk *) data);

	if (iks_find_with_attrib(pak->x, "session", "type", "initiate")) {
		/* New call */
		gtalk_newcall(client, pak);
	} else if (iks_find_with_attrib(pak->x, "session", "type", "candidates") || iks_find_with_attrib(pak->x, "session", "type", "transport-info")) {
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "About to add candidate!\n");
		gtalk_add_candidate(client, pak);
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Candidate Added!\n");
	} else if (iks_find_with_attrib(pak->x, "session", "type", "accept")) {
		gtalk_is_answered(client, pak);
	} else if (iks_find_with_attrib(pak->x, "session", "type", "transport-accept")) {
		gtalk_is_accepted(client, pak);
	} else if (iks_find_with_attrib(pak->x, "session", "type", "content-info") || iks_find_with_attrib(pak->x, "gtalk", "action", "session-info")) {
		gtalk_handle_dtmf(client, pak);
	} else if (iks_find_with_attrib(pak->x, "session", "type", "terminate")) {
		gtalk_hangup_farend(client, pak);
	} else if (iks_find_with_attrib(pak->x, "session", "type", "reject")) {
		gtalk_hangup_farend(client, pak);
	}
	ASTOBJ_UNREF(client, gtalk_member_destroy);
	return IKS_FILTER_EAT;
}

/* Not using this anymore probably take out soon 
static struct gtalk_candidate *gtalk_create_candidate(char *args)
{
	char *name, *type, *preference, *protocol;
	struct gtalk_candidate *res;
	res = malloc(sizeof(struct gtalk_candidate));
	memset(res, 0, sizeof(struct gtalk_candidate));
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

static int gtalk_create_member(char *label, struct ast_variable *var, int allowguest,
								struct ast_codec_pref prefs, char *context,
								struct gtalk *member)
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
		struct gtalk_candidate *candidate = NULL;
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
			candidate = gtalk_create_candidate(var->value);
			if (candidate) {
				candidate->next = member->ourcandidates;
				member->ourcandidates = candidate;
			}
		}
#endif
		else if (!strcasecmp(var->name, "connection")) {
			if ((client = ast_aji_get_client(var->value))) {
				member->connection = client;
				iks_filter_add_rule(client->f, gtalk_parser, member, 
						    IKS_RULE_TYPE, IKS_PAK_IQ, 
						    IKS_RULE_FROM_PARTIAL, member->user,
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

static int gtalk_load_config(void)
{
	char *cat = NULL;
	struct ast_config *cfg = NULL;
	char context[AST_MAX_CONTEXT];
	int allowguest = 1;
	struct ast_variable *var;
	struct gtalk *member;
	struct ast_codec_pref prefs;
	struct aji_client_container *clients;
	struct gtalk_candidate *global_candidates = NULL;
	struct hostent *hp;
	struct ast_hostent ahp;

	cfg = ast_config_load(GOOGLE_CONFIG);
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
		else if (!strcasecmp(var->name, "bindaddr")) {
			if (!(hp = ast_gethostbyname(var->value, &ahp))) {
				ast_log(LOG_WARNING, "Invalid address: %s\n", var->value);
			} else {
				memcpy(&bindaddr.sin_addr, hp->h_addr, sizeof(bindaddr.sin_addr));
			}
		}
/*  Idea to allow for custom candidates  */
/*
		else if (!strcasecmp(var->name, "candidate")) {
			candidate = gtalk_create_candidate(var->value);
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
			member = (struct gtalk *) malloc(sizeof(struct gtalk));
			memset(member, 0, sizeof(struct gtalk));
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
						candidate = gtalk_create_candidate(var->value);
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
						member->connection = NULL;
						iks_filter_add_rule(iterator->f, gtalk_parser, member, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_NS, "http://www.google.com/session", IKS_RULE_DONE);
						iks_filter_add_rule(iterator->f, gtalk_parser, member, IKS_RULE_TYPE, IKS_PAK_IQ, IKS_RULE_NS, "http://jabber.org/protocol/gtalk", IKS_RULE_DONE);
						ASTOBJ_UNLOCK(member);
						ASTOBJ_UNLOCK(iterator);
					});
					ASTOBJ_CONTAINER_LINK(&gtalk_list, member);
					ASTOBJ_UNREF(member, gtalk_member_destroy);
				} else {
					ASTOBJ_UNLOCK(member);
					ASTOBJ_UNREF(member, gtalk_member_destroy);
				}
			} else {
				ASTOBJ_UNLOCK(member);
				if (gtalk_create_member(cat, var, allowguest, prefs, context, member))
					ASTOBJ_CONTAINER_LINK(&gtalk_list, member);
				ASTOBJ_UNREF(member, gtalk_member_destroy);
			}
		}
		cat = ast_category_browse(cfg, cat);
	}
	gtalk_free_candidates(global_candidates);
	return 1;
}

/*! \brief Load module into PBX, register channel */
static int load_module(void)
{
	char *jabber_loaded = ast_module_helper("", "res_jabber.so", 0, 0, 0, 0);
	free(jabber_loaded);
	if (!jabber_loaded) {
		/* If embedded, check for a different module name */
		jabber_loaded = ast_module_helper("", "res_jabber", 0, 0, 0, 0);
		free(jabber_loaded);
		if (!jabber_loaded) {
			ast_log(LOG_ERROR, "chan_gtalk.so depends upon res_jabber.so\n");
			return AST_MODULE_LOAD_DECLINE;
		}
	}

#ifdef HAVE_GNUTLS	
        gcry_control (GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
#endif /* HAVE_GNUTLS */

	ASTOBJ_CONTAINER_INIT(&gtalk_list);
	if (!gtalk_load_config()) {
		ast_log(LOG_ERROR, "Unable to read config file %s. Not loading module.\n", GOOGLE_CONFIG);
		return 0;
	}

	sched = sched_context_create();
	if (!sched) 
		ast_log(LOG_WARNING, "Unable to create schedule context\n");

	io = io_context_create();
	if (!io) 
		ast_log(LOG_WARNING, "Unable to create I/O context\n");

	if (ast_find_ourip(&__ourip, bindaddr)) {
		ast_log(LOG_WARNING, "Unable to get own IP address, Gtalk disabled\n");
		return 0;
	}

	ast_rtp_proto_register(&gtalk_rtp);
	ast_cli_register_multiple(gtalk_cli, sizeof(gtalk_cli) / sizeof(gtalk_cli[0]));

	/* Make sure we can register our channel type */
	if (ast_channel_register(&gtalk_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", gtalk_tech.type);
		return -1;
	}
	return 0;
}

/*! \brief Reload module */
static int reload(void)
{
	return 0;
}

/*! \brief Unload the gtalk channel from Asterisk */
static int unload_module(void)
{
	struct gtalk_pvt *privates = NULL;
	ast_cli_unregister_multiple(gtalk_cli, sizeof(gtalk_cli) / sizeof(gtalk_cli[0]));
	/* First, take us out of the channel loop */
	ast_channel_unregister(&gtalk_tech);
	ast_rtp_proto_unregister(&gtalk_rtp);

	if (!ast_mutex_lock(&gtalklock)) {
		/* Hangup all interfaces if they have an owner */
		ASTOBJ_CONTAINER_TRAVERSE(&gtalk_list, 1, {
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
		ast_mutex_unlock(&gtalklock);
	} else {
		ast_log(LOG_WARNING, "Unable to lock the monitor\n");
		return -1;
	}
	ASTOBJ_CONTAINER_DESTROYALL(&gtalk_list, gtalk_member_destroy);
	ASTOBJ_CONTAINER_DESTROY(&gtalk_list);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Gtalk Channel Driver",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
