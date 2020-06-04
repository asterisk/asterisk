/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<defaultenabled>yes</defaultenabled>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <netinet/in.h>	/* For IPPROTO_UDP and in6_addr */

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/netsock2.h"
#include "asterisk/acl.h"

/*! \brief PCAP Header */
struct pcap_header {
	uint32_t magic_number; 	/*! \brief PCAP file format magic number */
	uint16_t version_major;	/*! \brief Major version number of the file format */
	uint16_t version_minor;	/*! \brief Minor version number of the file format */
	int32_t thiszone;	/*! \brief GMT to local correction */
	uint32_t sigfigs;	/*! \brief Accuracy of timestamps */
	uint32_t snaplen;	/*! \brief The maximum size that can be recorded in the file */
	uint32_t network;	/*! \brief Type of packets held within the file */
};

/*! \brief PCAP Packet Record Header */
struct pcap_record_header {
	uint32_t ts_sec;	/*! \brief When the record was created */
	uint32_t ts_usec;	/*! \brief When the record was created */
	uint32_t incl_len;	/*! \brief Length of packet as saved in the file */
	uint32_t orig_len;	/*! \brief Length of packet as sent over network */
};

/*! \brief PCAP Ethernet Header */
struct pcap_ethernet_header {
	uint8_t dst[6];	/*! \brief Destination MAC address */
	uint8_t src[6];	/*! \brief Source MAD address */
	uint16_t type;	/*! \brief The type of packet contained within */
} __attribute__((__packed__));

/*! \brief PCAP IPv4 Header */
struct pcap_ipv4_header {
	uint8_t ver_ihl;	/*! \brief IP header version and other bits */
	uint8_t ip_tos;		/*! \brief Type of service details */
	uint16_t ip_len;	/*! \brief Total length of the packet (including IPv4 header) */
	uint16_t ip_id;		/*! \brief Identification value */
	uint16_t ip_off;	/*! \brief Fragment offset */
	uint8_t ip_ttl;		/*! \brief Time to live for the packet */
	uint8_t ip_protocol;	/*! \brief Protocol of the data held within the packet (always UDP) */
	uint16_t ip_sum;	/*! \brief Checksum (not calculated for our purposes */
	uint32_t ip_src;	/*! \brief Source IP address */
	uint32_t ip_dst;	/*! \brief Destination IP address */
};

/*! \brief PCAP IPv6 Header */
struct pcap_ipv6_header {
   union {
      struct ip6_hdrctl {
         uint32_t ip6_un1_flow; /*! \brief Version, traffic class, flow label */
         uint16_t ip6_un1_plen; /*! \brief Length of the packet (not including IPv6 header) */
         uint8_t ip6_un1_nxt; 	/*! \brief Next header field */
         uint8_t ip6_un1_hlim;	/*! \brief Hop Limit */
      } ip6_un1;
      uint8_t ip6_un2_vfc;	/*! \brief Version, traffic class */
   } ip6_ctlun;
   struct in6_addr ip6_src; /*! \brief Source IP address */
   struct in6_addr ip6_dst; /*! \brief Destination IP address */
};

/*! \brief PCAP UDP Header */
struct pcap_udp_header {
	uint16_t src;		/*! \brief Source IP port */
	uint16_t dst;		/*! \brief Destination IP port */
	uint16_t length;	/*! \brief Length of the UDP header plus UDP packet */
	uint16_t checksum;	/*! \brief Packet checksum, left uncalculated for our purposes */
};

/*! \brief PJSIP Logging Session */
struct pjsip_logger_session {
	/*! \brief Explicit addresses or ranges being logged */
	struct ast_ha *matches;
	/*! \brief Filename used for the pcap file */
	char pcap_filename[PATH_MAX];
	/*! \brief The pcap file itself */
	FILE *pcap_file;
	/*! \brief Whether the session is enabled or not */
	unsigned int enabled:1;
	/*! \brief Whether the session is logging all traffic or not */
	unsigned int log_all_traffic:1;
	/*! \brief Whether to log to verbose or not */
	unsigned int log_to_verbose:1;
	/*! \brief Whether to log to pcap or not */
	unsigned int log_to_pcap:1;
};

/*! \brief The default logger session */
static struct pjsip_logger_session *default_logger;

/*! \brief Destructor for logger session */
static void pjsip_logger_session_destroy(void *obj)
{
	struct pjsip_logger_session *session = obj;

	if (session->pcap_file) {
		fclose(session->pcap_file);
	}

	ast_free_ha(session->matches);
}

/*! \brief Allocator for logger session */
static struct pjsip_logger_session *pjsip_logger_session_alloc(void)
{
	struct pjsip_logger_session *session;

	session = ao2_alloc_options(sizeof(struct pjsip_logger_session), pjsip_logger_session_destroy,
		AO2_ALLOC_OPT_LOCK_RWLOCK);
	if (!session) {
		return NULL;
	}

	session->log_to_verbose = 1;

	return session;
}

/*! \brief See if we pass debug IP filter */
static inline int pjsip_log_test_addr(const struct pjsip_logger_session *session, const char *address, int port)
{
	struct ast_sockaddr test_addr;

	if (!session->enabled) {
		return 0;
	}

	if (session->log_all_traffic) {
		return 1;
	}

	/* A null address was passed in or no explicit matches. Just reject it. */
	if (ast_strlen_zero(address) || !session->matches) {
		return 0;
	}

	ast_sockaddr_parse(&test_addr, address, PARSE_PORT_IGNORE);
	ast_sockaddr_set_port(&test_addr, port);

	/* Compare the address against the matches */
	if (ast_apply_ha(session->matches, &test_addr) != AST_SENSE_ALLOW) {
		return 1;
	} else {
		return 0;
	}
}

static void pjsip_logger_write_to_pcap(struct pjsip_logger_session *session, const char *msg, size_t msg_len,
	pj_sockaddr *source, pj_sockaddr *destination)
{
	struct timeval now = ast_tvnow();
	struct pcap_record_header pcap_record_header = {
		.ts_sec = now.tv_sec,
		.ts_usec = now.tv_usec,
	};
	struct pcap_ethernet_header pcap_ethernet_header = {
		.type = 0,
	};
	struct pcap_ipv4_header pcap_ipv4_header = {
		.ver_ihl = 0x45, /* IPv4 + 20 bytes of header */
		.ip_ttl = 128, /* We always put a TTL of 128 to keep Wireshark less blue */
	};
	struct pcap_ipv6_header pcap_ipv6_header = {
		.ip6_ctlun.ip6_un2_vfc = 0x60,
	};
	void *pcap_ip_header;
	size_t pcap_ip_header_len;
	struct pcap_udp_header pcap_udp_header;

	/* Packets are always stored as UDP to simplify this logic */
	if (source) {
		pcap_udp_header.src = ntohs(pj_sockaddr_get_port(source));
	} else {
		pcap_udp_header.src = ntohs(0);
	}
	if (destination) {
		pcap_udp_header.dst = ntohs(pj_sockaddr_get_port(destination));
	} else {
		pcap_udp_header.dst = ntohs(0);
	}
	pcap_udp_header.length = ntohs(sizeof(struct pcap_udp_header) + msg_len);

	/* Construct the appropriate IP header */
	if ((source && source->addr.sa_family == pj_AF_INET()) ||
		(destination && destination->addr.sa_family == pj_AF_INET())) {
		pcap_ethernet_header.type = htons(0x0800); /* We are providing an IPv4 packet */
		pcap_ip_header = &pcap_ipv4_header;
		pcap_ip_header_len = sizeof(struct pcap_ipv4_header);
		if (source) {
			memcpy(&pcap_ipv4_header.ip_src, pj_sockaddr_get_addr(source), pj_sockaddr_get_addr_len(source));
		}
		if (destination) {
			memcpy(&pcap_ipv4_header.ip_dst, pj_sockaddr_get_addr(destination), pj_sockaddr_get_addr_len(destination));
		}
		pcap_ipv4_header.ip_len = htons(sizeof(struct pcap_udp_header) + sizeof(struct pcap_ipv4_header) + msg_len);
		pcap_ipv4_header.ip_protocol = IPPROTO_UDP; /* We always provide UDP */
	} else {
		pcap_ethernet_header.type = htons(0x86DD); /* We are providing an IPv6 packet */
		pcap_ip_header = &pcap_ipv6_header;
		pcap_ip_header_len = sizeof(struct pcap_ipv6_header);
		if (source) {
			memcpy(&pcap_ipv6_header.ip6_src, pj_sockaddr_get_addr(source), pj_sockaddr_get_addr_len(source));
		}
		if (destination) {
			memcpy(&pcap_ipv6_header.ip6_dst, pj_sockaddr_get_addr(destination), pj_sockaddr_get_addr_len(destination));
		}
		pcap_ipv6_header.ip6_ctlun.ip6_un1.ip6_un1_plen = htons(sizeof(struct pcap_udp_header) + msg_len);
		pcap_ipv6_header.ip6_ctlun.ip6_un1.ip6_un1_nxt = IPPROTO_UDP;
	}

	/* Add up all the sizes for this record */
	pcap_record_header.incl_len = pcap_record_header.orig_len = sizeof(pcap_ethernet_header) + pcap_ip_header_len + sizeof(pcap_udp_header) + msg_len;

	/* We lock the logger session since we're writing these out in parts */
	ao2_wrlock(session);
	if (session->pcap_file) {
		if (fwrite(&pcap_record_header, sizeof(struct pcap_record_header), 1, session->pcap_file) != 1) {
			ast_log(LOG_WARNING, "Writing PCAP header failed: %s\n", strerror(errno));
		}
		if (fwrite(&pcap_ethernet_header, sizeof(struct pcap_ethernet_header), 1, session->pcap_file) != 1) {
			ast_log(LOG_WARNING, "Writing ethernet header to pcap failed: %s\n", strerror(errno));
		}
		if (fwrite(pcap_ip_header, pcap_ip_header_len, 1, session->pcap_file) != 1) {
			ast_log(LOG_WARNING, "Writing IP header to pcap failed: %s\n", strerror(errno));
		}
		if (fwrite(&pcap_udp_header, sizeof(struct pcap_udp_header), 1, session->pcap_file) != 1) {
			ast_log(LOG_WARNING, "Writing UDP header to pcap failed: %s\n", strerror(errno));
		}
		if (fwrite(msg, msg_len, 1, session->pcap_file) != 1) {
			ast_log(LOG_WARNING, "Writing UDP payload to pcap failed: %s\n", strerror(errno));
		}
	}
	ao2_unlock(session);
}

static pj_status_t logging_on_tx_msg(pjsip_tx_data *tdata)
{
	char buffer[AST_SOCKADDR_BUFLEN];

	ao2_rdlock(default_logger);
	if (!pjsip_log_test_addr(default_logger, tdata->tp_info.dst_name, tdata->tp_info.dst_port)) {
		ao2_unlock(default_logger);
		return PJ_SUCCESS;
	}
	ao2_unlock(default_logger);

	if (default_logger->log_to_verbose) {
		ast_verbose("<--- Transmitting SIP %s (%d bytes) to %s:%s --->\n%.*s\n",
			tdata->msg->type == PJSIP_REQUEST_MSG ? "request" : "response",
			(int) (tdata->buf.cur - tdata->buf.start),
			tdata->tp_info.transport->type_name,
			pj_sockaddr_print(&tdata->tp_info.dst_addr, buffer, sizeof(buffer), 3),
			(int) (tdata->buf.end - tdata->buf.start), tdata->buf.start);
	}

	if (default_logger->log_to_pcap) {
		pjsip_logger_write_to_pcap(default_logger, tdata->buf.start, (int) (tdata->buf.cur - tdata->buf.start),
			NULL, &tdata->tp_info.dst_addr);
	}

	return PJ_SUCCESS;
}

static pj_bool_t logging_on_rx_msg(pjsip_rx_data *rdata)
{
	char buffer[AST_SOCKADDR_BUFLEN];

	if (!rdata->msg_info.msg) {
		return PJ_FALSE;
	}

	ao2_rdlock(default_logger);
	if (!pjsip_log_test_addr(default_logger, rdata->pkt_info.src_name, rdata->pkt_info.src_port)) {
		ao2_unlock(default_logger);
		return PJ_FALSE;
	}
	ao2_unlock(default_logger);

	if (default_logger->log_to_verbose) {
		ast_verbose("<--- Received SIP %s (%d bytes) from %s:%s --->\n%s\n",
			rdata->msg_info.msg->type == PJSIP_REQUEST_MSG ? "request" : "response",
			rdata->msg_info.len,
			rdata->tp_info.transport->type_name,
			pj_sockaddr_print(&rdata->pkt_info.src_addr, buffer, sizeof(buffer), 3),
			rdata->pkt_info.packet);
	}

	if (default_logger->log_to_pcap) {
		pjsip_logger_write_to_pcap(default_logger, rdata->pkt_info.packet, rdata->msg_info.len,
			&rdata->pkt_info.src_addr, NULL);
	}

	return PJ_FALSE;
}

static pjsip_module logging_module = {
	.name = { "Logging Module", 14 },
	.priority = 0,
	.on_rx_request = logging_on_rx_msg,
	.on_rx_response = logging_on_rx_msg,
	.on_tx_request = logging_on_tx_msg,
	.on_tx_response = logging_on_tx_msg,
};

static char *pjsip_enable_logger_all(int fd)
{
	ao2_wrlock(default_logger);
	default_logger->enabled = 1;
	default_logger->log_all_traffic = 1;
	ao2_unlock(default_logger);

	if (fd >= 0) {
		ast_cli(fd, "PJSIP Logging enabled\n");
	}

	return CLI_SUCCESS;
}

static char *pjsip_enable_logger_host(int fd, const char *arg, unsigned int add_host)
{
	const char *host = arg;
	char *mask;
	struct ast_sockaddr address;
	int error = 0;

	ao2_wrlock(default_logger);
	default_logger->enabled = 1;

	if (!add_host) {
		/* If this is not adding an additional host or subnet then we have to
		 * remove what already exists.
		 */
		ast_free_ha(default_logger->matches);
		default_logger->matches = NULL;
	}

	mask = strrchr(host, '/');
	if (!mask && !ast_sockaddr_parse(&address, arg, 0)) {
		if (ast_sockaddr_resolve_first_af(&address, arg, 0, AST_AF_UNSPEC)) {
			ao2_unlock(default_logger);
			return CLI_SHOWUSAGE;
		}
		host = ast_sockaddr_stringify(&address);
	}

	default_logger->matches = ast_append_ha_with_port("d", host, default_logger->matches, &error);
	if (!default_logger->matches || error) {
		if (fd >= 0) {
			ast_cli(fd, "Failed to add address '%s' for logging\n", host);
		}
		ao2_unlock(default_logger);
		return CLI_SUCCESS;
	}

	ao2_unlock(default_logger);

	if (fd >= 0) {
		ast_cli(fd, "PJSIP Logging Enabled for host: %s\n", ast_sockaddr_stringify_addr(&address));
	}

	return CLI_SUCCESS;
}

static char *pjsip_disable_logger(int fd)
{
	ao2_wrlock(default_logger);

	/* Default the settings back to the way they were */
	default_logger->enabled = 0;
	default_logger->log_all_traffic = 0;
	default_logger->pcap_filename[0] = '\0';
	default_logger->log_to_verbose = 1;
	default_logger->log_to_pcap = 0;

	/* Stop logging to the PCAP file if active */
	if (default_logger->pcap_file) {
		fclose(default_logger->pcap_file);
		default_logger->pcap_file = NULL;
	}

	ast_free_ha(default_logger->matches);
	default_logger->matches = NULL;

	ao2_unlock(default_logger);

	if (fd >= 0) {
		ast_cli(fd, "PJSIP Logging disabled\n");
	}

	return CLI_SUCCESS;
}

static char *pjsip_set_logger_verbose(int fd, const char *arg)
{
	ao2_wrlock(default_logger);
	default_logger->log_to_verbose = ast_true(arg);
	ao2_unlock(default_logger);

	ast_cli(fd, "PJSIP Logging to verbose has been %s\n", ast_true(arg) ? "enabled" : "disabled");

	return CLI_SUCCESS;
}

static char *pjsip_set_logger_pcap(int fd, const char *arg)
{
	struct pcap_header pcap_header = {
		.magic_number = 0xa1b2c3d4,
		.version_major = 2,
		.version_minor = 4,
		.snaplen = 65535,
		.network = 1, /* We always use ethernet so we can combine IPv4 and IPv6 in same pcap */
	};

	ao2_wrlock(default_logger);
	ast_copy_string(default_logger->pcap_filename, arg, sizeof(default_logger->pcap_filename));

	if (default_logger->pcap_file) {
		fclose(default_logger->pcap_file);
		default_logger->pcap_file = NULL;
	}

	default_logger->pcap_file = fopen(arg, "wb");
	if (!default_logger->pcap_file) {
		ao2_unlock(default_logger);
		ast_cli(fd, "Failed to open file '%s' for pcap writing\n", arg);
		return CLI_SUCCESS;
	}
	fwrite(&pcap_header, 1, sizeof(struct pcap_header), default_logger->pcap_file);

	default_logger->log_to_pcap = 1;
	ao2_unlock(default_logger);

	ast_cli(fd, "PJSIP logging to pcap file '%s'\n", arg);

	return CLI_SUCCESS;
}

static char *pjsip_set_logger(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *what;

	if (cmd == CLI_INIT) {
		e->command = "pjsip set logger {on|off|host|add|verbose|pcap}";
		e->usage =
			"Usage: pjsip set logger {on|off|host <name/subnet>|add <name/subnet>|verbose <on/off>|pcap <filename>}\n"
			"       Enables or disabling logging of SIP packets\n"
			"       read on ports bound to PJSIP transports either\n"
			"       globally or enables logging for an individual\n"
			"       host.\n";
		return NULL;
	} else if (cmd == CLI_GENERATE) {
		return NULL;
	}

	what = a->argv[e->args - 1];     /* Guaranteed to exist */

	if (a->argc == e->args) {        /* on/off */
		if (!strcasecmp(what, "on")) {
			return pjsip_enable_logger_all(a->fd);
		} else if (!strcasecmp(what, "off")) {
			return pjsip_disable_logger(a->fd);
		}
	} else if (a->argc == e->args + 1) {
		if (!strcasecmp(what, "host")) {
			return pjsip_enable_logger_host(a->fd, a->argv[e->args], 0);
		} else if (!strcasecmp(what, "add")) {
			return pjsip_enable_logger_host(a->fd, a->argv[e->args], 1);
		} else if (!strcasecmp(what, "verbose")) {
			return pjsip_set_logger_verbose(a->fd, a->argv[e->args]);
		} else if (!strcasecmp(what, "pcap")) {
			return pjsip_set_logger_pcap(a->fd, a->argv[e->args]);
		}
	}

	return CLI_SHOWUSAGE;
}

static struct ast_cli_entry cli_pjsip[] = {
	AST_CLI_DEFINE(pjsip_set_logger, "Enable/Disable PJSIP Logger Output")
};

static void check_debug(void)
{
	RAII_VAR(char *, debug, ast_sip_get_debug(), ast_free);

	if (ast_false(debug)) {
		pjsip_disable_logger(-1);
		return;
	}

	if (ast_true(debug)) {
		pjsip_enable_logger_all(-1);
		return;
	}

	if (pjsip_enable_logger_host(-1, debug, 0) != CLI_SUCCESS) {
		ast_log(LOG_WARNING, "Could not resolve host %s for debug "
			"logging\n", debug);
	}
}

static void global_reloaded(const char *object_type)
{
	check_debug();
}

static const struct ast_sorcery_observer global_observer = {
	.loaded = global_reloaded
};

static int load_module(void)
{
	if (ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &global_observer)) {
		ast_log(LOG_WARNING, "Unable to add global observer\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	default_logger = pjsip_logger_session_alloc();
	if (!default_logger) {
		ast_sorcery_observer_remove(
			ast_sip_get_sorcery(), "global", &global_observer);
		ast_log(LOG_WARNING, "Unable to create default logger\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	check_debug();

	ast_sip_register_service(&logging_module);
	ast_cli_register_multiple(cli_pjsip, ARRAY_LEN(cli_pjsip));

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_pjsip, ARRAY_LEN(cli_pjsip));
	ast_sip_unregister_service(&logging_module);

	ast_sorcery_observer_remove(
		ast_sip_get_sorcery(), "global", &global_observer);

	ao2_cleanup(default_logger);
	default_logger = NULL;

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Packet Logger",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip",
);
