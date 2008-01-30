/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Andreas 'MacBrody' Brodmann
 *
 * Andreas 'MacBrody' Brodmann <andreas.brodmann@gmail.com>
 *
 * Information on how multicast paging works with linksys 
 * phones was used from FreeSWITCH's mod_esf with permission
 * from Brian West.
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
 * \brief Application to stream a channel's input to a specified uni-/multicast address
 *
 * \author Andreas 'MacBrody' Brodmann <andreas.brodmann@gmail.com>
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>yes</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/config.h"
#include "asterisk/acl.h"

#define RTP_PT_ULAW    0
#define RTP_PT_GSM     3
#define RTP_PT_ALAW    8
#define RTP_PT_G729   18

/*! \brief Multicast Group Receiver Type object */
enum grouptype {
	MGT_BASIC = 1,    /*!< simple multicast enabled client/receiver like snom, barix */
	MGT_LINKSYS = 2,  /*!< linksys ipphones; they need a start/stop packet */
	MGT_CISCO = 3     /*!< cisco phones; they need a http request to their internal web server // NOT YET IMPLEMENTED */
};

/*! \brief Multicast Group object */
struct mcast_group {
	char name[32];                        /*!< name of the group */
	enum grouptype type;                  /*!< type, see grouptype */
	int socket;                           /*!< socket used for streaming to this group (each group has got its own socket */
	int ttl;                              /*!< timetolive to be set on this socket */
	struct sockaddr_in rtp_address;       /*!< address/port pair where the traffic is sent to */
	struct sockaddr_in control_address;   /*!< address/port for linksys phones to send the start/stop packet to */
	AST_LIST_ENTRY(mcast_group) list;     /*!< next element int group list */
};

/*! \brief RTP header object */
struct rtp_header {
	uint16_t flags;
	uint16_t seqno;
	uint32_t timestamp;
	uint32_t ssrc;
};

/*! \brief Control Packet object as used for linksys phones for start/stop packets */
struct control_packet {
	uint32_t unique_id;                    /*!< unique id per command start or stop - not the same for both commands */
	uint32_t command;                      /*!< the command: 6=start, 7=stop */
	uint32_t ip;                           /*!< multicast address in network byte order */
	uint32_t port;                         /*!< udp port to send the data to */
};

/*! \brief List to hold all the multicast groups defined in the config file */
static AST_LIST_HEAD_STATIC(groups, mcast_group);

static char *app = "RTPPage";
static char *synopsis = "RTPPage Application";
static char *descrip = "  RTPPage(direct|multicast, ip:port[&ip:port]|group[&group2[&group3...]][,codec]): Sends the channel's input to the\n"
"specified group(s) defined in the config file rtppage.conf.\n"
"The optional codec may be one of the following:\n"
"   ulaw - default\n"
"   alaw\n"
"   gsm\n"
"   g729\n"
"as long as asterisk does not have to translate or respective translators are\n"
"installed with your asterisk installation. If none or any other codec is\n"
"specified the application will fall back to ulaw.\n";

static const char config[] = "rtppage.conf";
static int default_ttl = -1;
static unsigned int tos = -1;

/*! \brief Read input from channel and send it to the specified group(s) as rtp traffic */
static int rtppage_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_module_user *u =  NULL;
	struct ast_frame *f = NULL;
	char *parse = NULL;
	char *rest = NULL, *cur = NULL;
	char *rest2 = NULL;
	char *ip = NULL, *port = NULL;
	int ms = -1;
	unsigned char *databuf = NULL;
	struct sockaddr_in destaddr;
	struct mcast_group *group;
	struct control_packet cpk;
	struct rtp_header *rtph = NULL;
	uint8_t rtp_pt = RTP_PT_ULAW;
	int chan_format = AST_FORMAT_ULAW;
	uint16_t rtpflags = 0;
	int ttl = 0;
	int pagetype = 0;
	AST_LIST_HEAD(, mcast_group) activegroups;

	/* init active groups */
	activegroups.first = NULL;
	activegroups.last = NULL;
	activegroups.lock = AST_MUTEX_INIT_VALUE;

	/* you can specify three arguments:
	 * 1) pagetype (0 = direct, 1 = multicast)
	 * 2) groups, e.g. NameOfGroup or Name1&Name2 etc) / or ip:port in case of direct
	 * 3) optional: codec, if specified and valid
	 *    this codec will be used for streaming
	 */
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(pagetype);
		AST_APP_ARG(groups);
		AST_APP_ARG(codec);
	);

	/* make sure there is at least one parameter */
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires argument (group(s)[,codec])\n", app);
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	/* pagetype is a mandatory parameter */
	if (!args.pagetype) {
		ast_log(LOG_WARNING, "%s requires arguments (pagetype, group(s) | ip:port[,codec])\n", app);
		return(-1);
	}
	if (!strcasecmp(args.pagetype, "direct")) {
		pagetype = 0;
	} else if (!strcasecmp(args.pagetype, "multicast")) {
		pagetype = 1;
	} else {
		ast_log(LOG_ERROR, "%s is an invalid grouptype! valid types are: direct, multicast.\n", args.pagetype);
		return(-1);
	}

	/* group is a mandatory parameter */
	if (!args.groups) {
		ast_log(LOG_WARNING, "%s requires arguments (pagetype, group(s) | ip:port[,codec])\n", app);
		return(-1);
	}

	/* setup variables for the desired codec */
	if (args.codec) {
		if (!strcasecmp(args.codec, "ulaw")) {
			/* use default settings */
		} else if (!strcasecmp(args.codec, "alaw")) {
			rtp_pt = RTP_PT_ALAW;
			chan_format = AST_FORMAT_ALAW;
		} else if (!strcasecmp(args.codec, "gsm")) {
			rtp_pt = RTP_PT_GSM;
			chan_format = AST_FORMAT_GSM;
		} else if (!strcasecmp(args.codec, "g729")) {
			rtp_pt = RTP_PT_G729;
			chan_format = AST_FORMAT_G729A;
		} else {
			/* use ulaw as fallback */
			rtp_pt = RTP_PT_ULAW;
			chan_format = AST_FORMAT_ULAW;
		}
	}

	u = ast_module_user_add(chan);

	/* Check if the channel is answered, if not
	 * do answer it */
	if (chan->_state != AST_STATE_UP) {
		res = ast_answer(chan);
		if (res) {
			ast_log(LOG_WARNING, "Could not answer channel '%s'\n", chan->name);
			goto end;
		}
	}

	/* allocate memory for the rtp send buffer */
	if ((databuf = ast_calloc(1, 172)) == NULL) {
		ast_log(LOG_WARNING, "Failed to allocate memory for the data buffer, give up\n");
		goto end;
	}

	/* initialize rtp buffer header
	 * with rtp version and
	 * payload type
	 */
	rtph = (struct rtp_header *)databuf;
	rtpflags  = (0x02 << 14); /* rtp v2 */
	rtpflags  = (rtpflags & 0xFF80) |  rtp_pt;  
	rtph->flags = htons(rtpflags);
	rtph->ssrc =  htonl((u_long)time(NULL));
	
	/* first create a temporary table for this page session
	 * containing all groups which will be used
	 */
	AST_LIST_LOCK(&groups);
	rest = ast_strdup(args.groups);
	if (pagetype == 0) {
		/* a direct page call. this can actually be used
		 * for multicast paging too by passing the ip:port as
		 * argument 2 
		 */
		while ((cur = strsep(&rest, "&"))) {
			struct mcast_group *agroup = ast_calloc(1, sizeof(*agroup));
			rest2 = ast_strdup(cur);
			ip = strsep(&rest2, ":");
			port = strsep(&rest2, ":");
			if (ip == NULL || port == NULL) {
				ast_log(LOG_WARNING, "invalid ip:port pair in call to RTPPage (%s)!\n", cur);
				free(agroup);
				continue;
			}
			agroup->rtp_address.sin_family = AF_INET;
			agroup->rtp_address.sin_port = htons(atoi(port));
			if (inet_pton(AF_INET, ip, &agroup->rtp_address.sin_addr) <= 0) {
				ast_log(LOG_WARNING, "invalid ip in call to RTPPage (%s)!\n", cur);
				free(agroup);
				continue;
			}
			agroup->type = MGT_BASIC;
			agroup->socket = -1;
			agroup->ttl = -1;
			AST_LIST_INSERT_TAIL(&activegroups, agroup, list);
		}
	} else if (pagetype == 1) {
		/* a multicast page call */
		while ((cur = strsep(&rest, "&"))) {
			AST_LIST_TRAVERSE(&groups, group, list) {
				if (!strcasecmp(group->name, cur)) {
					struct mcast_group *agroup = ast_calloc(1, sizeof(*agroup));
					memcpy(agroup->name, group->name, 32);
					agroup->type = group->type;
					agroup->socket = group->socket;
					agroup->ttl = group->ttl;
					memcpy(&agroup->rtp_address, &group->rtp_address, sizeof(agroup->rtp_address));
					memcpy(&agroup->control_address, &group->control_address, sizeof(agroup->control_address));
					AST_LIST_INSERT_TAIL(&activegroups, agroup, list);
				}
			}
		}
	}
	AST_LIST_UNLOCK(&groups);

	/* now initialize these groups, e.g. create a udp socket for each,
	 * set ttl and tos if requested by config, and
	 * in case of linksys type groups send the multicast start signal
	 */
	AST_LIST_TRAVERSE(&activegroups, group, list) {
		group->socket = socket(AF_INET, SOCK_DGRAM, 0);
		/* set ttl if configured
		 * ttl can be configured either globally in the
		 * category 'general' or locally within
		 * the respective groups
		 */
		if (group->ttl >= 0 || default_ttl >= 0) {
			ttl = default_ttl;
			if (group->ttl >= 0) {
				ttl = group->ttl;
			}
			if (setsockopt(group->socket, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
				ast_log(LOG_WARNING, "Failed to set ttl on socket for group %s!\n", group->name);
			}
		}
		/* set tos if requested 
		 * tos can only be configured globally ('general')
		 */
		if (tos >= 0) {
			if (setsockopt(group->socket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
				ast_log(LOG_WARNING, "Failed to set tos field on socket for group %s!\n", group->name);
			}
		}
		/* for linksys device groups send multicast start command */
		if (group->type == MGT_LINKSYS) {
			cpk.unique_id = htonl((u_long)time(NULL));
			cpk.command = htonl(6);  /* multicast start command */
			memcpy(&cpk.ip, &group->rtp_address.sin_addr, sizeof(cpk.ip));
			cpk.port = htonl(ntohs(group->rtp_address.sin_port));
			memcpy(&destaddr, &group->control_address, sizeof(destaddr));
			sendto(group->socket, &cpk, sizeof(cpk), 0, (struct sockaddr *)&destaddr, sizeof(destaddr));
			sendto(group->socket, &cpk, sizeof(cpk), 0, (struct sockaddr *)&destaddr, sizeof(destaddr));
		}
	}

	/* Set read format as configured - this codec will be used for streaming */
	res = ast_set_read_format(chan, chan_format);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to set channel read mode, giving up\n");
		res = -1;
		goto end;
	}

	/* Play a beep to let the caller know he can start talking */
	res = ast_streamfile(chan, "beep", chan->language);
	if (!res) {
		res = ast_waitstream(chan, "");
	} else {
		ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", chan->name);
	}
	ast_stopstream(chan);

	/* main loop: 
	 * read frames from the input channel and, if they are voice frames,
	 * send them to all requested multi-/unicast listeners.
	 */
	for (;;) {
		ms = ast_waitfor(chan, 1000);
		if (ms < 0) {
			ast_log(LOG_DEBUG, "Hangup detected\n");
			goto end;
		}
		f = ast_read(chan);
		if (!f)
			break;

		/* if the speaker pressed '#', then quit */
		if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '#')) {
			res = 0;
			ast_frfree(f);
			ast_log(LOG_DEBUG, "Received DTMF key: %d\n", f->subclass);
			goto end;
		}

		if (f->frametype == AST_FRAME_VOICE) {
			/* update the rtp header */
			rtph = (struct rtp_header *)databuf;
			rtph->seqno = htons(f->seqno);
			rtph->timestamp = htonl(f->ts * 8);
			memcpy(databuf+12, f->data, f->datalen);

			/* now send that frame to the destination groups */
			AST_LIST_TRAVERSE(&activegroups, group, list) {
				memcpy(&destaddr, &group->rtp_address, sizeof(destaddr));
				if (sendto(group->socket, databuf, f->datalen+12, 0, (struct sockaddr *)&destaddr, sizeof(destaddr)) <= 0) {
					ast_log(LOG_DEBUG, "sendto() failed!\n");
				}
			}
		}
		ast_frfree(f);
		f = NULL;
	}

end:

	/* send a stop multicast signal to all linksys devices */
	AST_LIST_TRAVERSE(&activegroups, group, list) {
		if (group->socket > 0) {
			if (group->type == MGT_LINKSYS) {
				cpk.unique_id = htonl((u_long)time(NULL));
				cpk.command = htonl(7);  /* multicast stop command */
				memcpy(&cpk.ip, &group->rtp_address.sin_addr, sizeof(cpk.ip));
				cpk.port = htonl(ntohs(group->rtp_address.sin_port));
				memcpy(&destaddr, &group->control_address, sizeof(destaddr));
				sendto(group->socket, &cpk, 8, 0, (struct sockaddr *)&destaddr, sizeof(destaddr));
				sendto(group->socket, &cpk, 8, 0, (struct sockaddr *)&destaddr, sizeof(destaddr));
			}
			close(group->socket);
		}
	}

	/* free activegroups list */
	while ((group = AST_LIST_REMOVE_HEAD(&activegroups, list))) {
		free(group);
	}

	/* free the rtp data buffer */
	if (databuf != NULL) {
		free(databuf);
	}

	ast_module_user_remove(u);
	ast_log(LOG_DEBUG, "Exit RTPPage(%s)\n", args.groups);

	return res;
}

static int load_config(int reload) {

	int res = 0;
	const char *cat = NULL;
	struct ast_config *cfg = NULL;
	struct mcast_group *group = NULL;
	const char *var = NULL;
	struct ast_flags config_flags = { 0 };

	AST_LIST_LOCK(&groups);
	if (reload) {
		/* if this is a reload, then free the config structure before
		 * filling it again 
		 */
		while ((group = AST_LIST_REMOVE_HEAD(&groups, list))) {
			free(group);
		}

		/* reset default_ttl & tos */
		default_ttl = -1; /* means not set */
		tos = -1;
	}

	/* load config file */
	if (!(cfg = ast_config_load(config, config_flags))) {
		ast_log(LOG_NOTICE, "Failed to load config!\n");
		AST_LIST_UNLOCK(&groups);
		return(-1);
	}

	while ((cat = ast_category_browse(cfg, cat)) != NULL) {
		/* 'general' is reserved for generic options */
		if (!strcasecmp(cat, "general")) {
			var = ast_variable_retrieve(cfg, cat, "ttl");
			if (var) {
				default_ttl = atoi(var);
			}
			var = ast_variable_retrieve(cfg, cat, "tos");
			if (var) {
				ast_str2tos(var, &tos);
			}
			continue;
		}

		group = ast_calloc(1, sizeof(*group));
		var = ast_variable_retrieve(cfg, cat, "type");
		if (!strcasecmp(var, "basic")) {
			ast_copy_string(group->name, cat, sizeof(group->name));
			group->type = MGT_BASIC;
			group->socket = -1;
			group->ttl = -1;
			if (ast_variable_retrieve(cfg, cat, "ttl") != NULL) {
				group->ttl = atoi(ast_variable_retrieve(cfg, cat, "ttl"));
			}
			memset(&group->rtp_address, 0, sizeof(group->rtp_address));
			group->rtp_address.sin_family = AF_INET;
			group->rtp_address.sin_port = htons(atoi(ast_variable_retrieve(cfg, cat, "rtp_port")));
			if (inet_pton(AF_INET, ast_variable_retrieve(cfg, cat, "rtp_address"), &group->rtp_address.sin_addr) <= 0) {
				ast_log(LOG_NOTICE, "Invalid ip address in group %s!\n", cat);
				ast_free(group);
				group = NULL;
				continue;
			}
		} else if (!strcasecmp(var, "linksys")) {
			ast_copy_string(group->name, cat, sizeof(group->name));
			group->type = MGT_LINKSYS;
			group->socket = -1;
			group->ttl = -1;
			if (ast_variable_retrieve(cfg, cat, "ttl") != NULL) {
				group->ttl = atoi(ast_variable_retrieve(cfg, cat, "ttl"));
			}
			memset(&group->rtp_address, 0, sizeof(group->rtp_address));
			group->rtp_address.sin_family = AF_INET;
			group->rtp_address.sin_port = htons(atoi(ast_variable_retrieve(cfg, cat, "rtp_port")));
			if (inet_pton(AF_INET, ast_variable_retrieve(cfg, cat, "rtp_address"), &group->rtp_address.sin_addr) <= 0) {
				ast_log(LOG_NOTICE, "Invalid ip address in group %s!\n", cat);
				ast_free(group);
				group = NULL;
				continue;
			}
			memset(&group->control_address, 0, sizeof(group->control_address));
			group->control_address.sin_family = AF_INET;
			group->control_address.sin_port = htons(atoi(ast_variable_retrieve(cfg, cat, "control_port")));
			if (inet_pton(AF_INET, ast_variable_retrieve(cfg, cat, "control_address"), &group->control_address.sin_addr) <= 0) {
				ast_log(LOG_NOTICE, "Invalid ip address in group %s!\n", cat);
				ast_free(group);
				group = NULL;
				continue;
			}
		} else {
			group->type = -1;
			group->socket = -1;
			group->ttl = -1;
			ast_log(LOG_NOTICE, "Invalid mcast group %s!\n", cat);
			continue;
		}

		/* now add it to the linked list */
		AST_LIST_INSERT_TAIL(&groups, group, list);
		ast_log(LOG_NOTICE, "loaded category %s\n", group->name);
		group = NULL;
		var = NULL;
	}

	AST_LIST_UNLOCK(&groups);

	ast_config_destroy(cfg);

	return(res);
}

static int unload_module(void)
{
	int res;
	res = ast_unregister_application(app);
	ast_module_user_hangup_all();
	return res;	
}

static int load_module(void)
{

	load_config(0);
	return ast_register_application(app, rtppage_exec, synopsis, descrip);
}

static int reload(void)
{
	return load_config(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "RTPPage Application",
	.load = load_module,
	.unload = unload_module,
	.reload = reload,
);


