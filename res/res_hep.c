/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2014, Digium, Inc.
 *
 * Alexandr Dubovikov <alexandr.dubovikov@sipcapture.org>
 * Matt Jordan <mjordan@digium.com>
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

/*!
 * \file
 * \brief Routines for integration with Homer using HEPv3
 *
 * \author Alexandr Dubovikov <alexandr.dubovikov@sipcapture.org>
 * \author Matt Jordan <mjordan@digium.com>
 *
 */

/*!
 * \li \ref res_hep.c uses the configuration file \ref hep.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page hep.conf hep.conf
 * \verbinclude hep.conf.sample
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_hep" language="en_US">
		<synopsis>Resource for integration with Homer using HEPv3</synopsis>
		<configFile name="hep.conf">
			<configObject name="general">
				<synopsis>General settings.</synopsis>
				<description><para>
					The <emphasis>general</emphasis> settings section contains information
					to configure Asterisk as a Homer capture agent.
					</para>
				</description>
				<configOption name="enabled" default="yes">
					<synopsis>Enable or disable packet capturing.</synopsis>
					<description>
						<enumlist>
							<enum name="no" />
							<enum name="yes" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="capture_address" default="192.168.1.1:9061">
					<synopsis>The address and port of the Homer server to send packets to.</synopsis>
				</configOption>
				<configOption name="capture_password">
					<synopsis>If set, the authentication password to send to Homer.</synopsis>
				</configOption>
				<configOption name="capture_id" default="0">
					<synopsis>The ID for this capture agent.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/config_options.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/res_hep.h"

#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip6.h>

#define DEFAULT_HEP_SERVER ""

/*! Generic vendor ID. Used for HEPv3 standard packets */
#define GENERIC_VENDOR_ID 0x0000

/*! Asterisk vendor ID. Used for custom data to send to a capture node */
#define ASTERISK_VENDOR_ID 0x0004

/*! Chunk types from the HEPv3 Spec */
enum hepv3_chunk_types {

	/*! THE IP PROTOCOL FAMILY */
	CHUNK_TYPE_IP_PROTOCOL_FAMILY = 0X0001,

	/*! THE IP PROTOCOL ID (UDP, TCP, ETC.) */
	CHUNK_TYPE_IP_PROTOCOL_ID = 0X0002,

	/*! IF IPV4, THE SOURCE ADDRESS */
	CHUNK_TYPE_IPV4_SRC_ADDR = 0X0003,

	/*! IF IPV4, THE DESTINATION ADDRESS */
	CHUNK_TYPE_IPV4_DST_ADDR = 0X0004,

	/*! IF IPV6, THE SOURCE ADDRESS */
	CHUNK_TYPE_IPV6_SRC_ADDR = 0X0005,

	/*! IF IPV6, THE DESTINATION ADDRESS */
	CHUNK_TYPE_IPV6_DST_ADDR = 0X0006,

	/*! THE SOURCE PORT */
	CHUNK_TYPE_SRC_PORT = 0X0007,

	/*! THE DESTINATION PORT */
	CHUNK_TYPE_DST_PORT = 0X0008,

	/*! THE CAPTURE TIME (SECONDS) */
	CHUNK_TYPE_TIMESTAMP_SEC = 0X0009,

	/*! THE CAPTURE TIME (MICROSECONDS) */
	CHUNK_TYPE_TIMESTAMP_USEC = 0X000A,

	/*! THE PROTOCOL PACKET TYPE. SEE /REF HEPV3_CAPTURE_TYPE */
	CHUNK_TYPE_PROTOCOL_TYPE = 0X000B,

	/*! OUR CAPTURE AGENT ID */
	CHUNK_TYPE_CAPTURE_AGENT_ID = 0X000C,

	/*! A KEEP ALIVE TIMER */
	CHUNK_TYPE_KEEP_ALIVE_TIMER = 0X000D,

	/*! THE \REF CAPTURE_PASSWORD IF DEFINED */
	CHUNK_TYPE_AUTH_KEY = 0X000E,

	/*! THE ONE AND ONLY PAYLOAD */
	CHUNK_TYPE_PAYLOAD = 0X000F,

	/*! THE ONE AND ONLY (ZIPPED) PAYLOAD */
	CHUNK_TYPE_PAYLOAD_ZIP = 0X0010,

	/*! THE UUID FOR THIS PACKET */
	CHUNK_TYPE_UUID = 0X0011,
};

#define INITIALIZE_GENERIC_HEP_IDS(hep_chunk, type) do { \
	(hep_chunk)->vendor_id = htons(GENERIC_VENDOR_ID); \
	(hep_chunk)->type_id = htons((type)); \
	} while (0)

#define INITIALIZE_GENERIC_HEP_IDS_VAR(hep_chunk, type, len) do { \
	INITIALIZE_GENERIC_HEP_IDS((hep_chunk), (type)); \
	(hep_chunk)->length = htons(sizeof(*(hep_chunk)) + len); \
	} while (0)

#define INITIALIZE_GENERIC_HEP_CHUNK(hep_item, type) do { \
	INITIALIZE_GENERIC_HEP_IDS(&(hep_item)->chunk, (type)); \
	(hep_item)->chunk.length = htons(sizeof(*(hep_item))); \
	} while (0)

#define INITIALIZE_GENERIC_HEP_CHUNK_DATA(hep_item, type, value) do { \
	INITIALIZE_GENERIC_HEP_CHUNK((hep_item), (type)); \
	(hep_item)->data = (value); \
	} while (0)

/*
 * HEPv3 Types.
 * Note that the content in these is stored in network byte order.
 */

struct hep_chunk {
	u_int16_t vendor_id;
	u_int16_t type_id;
	u_int16_t length;
} __attribute__((packed));

struct hep_chunk_uint8 {
	struct hep_chunk chunk;
	u_int8_t data;
} __attribute__((packed));

struct hep_chunk_uint16 {
	struct hep_chunk chunk;
	u_int16_t data;
} __attribute__((packed));

struct hep_chunk_uint32 {
	struct hep_chunk chunk;
	u_int32_t data;
} __attribute__((packed));

struct hep_chunk_ip4 {
	struct hep_chunk chunk;
	struct in_addr data;
} __attribute__((packed));

struct hep_chunk_ip6 {
	struct hep_chunk chunk;
	struct in6_addr data;
} __attribute__((packed));

struct hep_ctrl {
	char id[4];
	u_int16_t length;
} __attribute__((packed));

/* HEP structures */

struct hep_generic {
	struct hep_ctrl         header;
	struct hep_chunk_uint8  ip_family;
	struct hep_chunk_uint8  ip_proto;
	struct hep_chunk_uint16 src_port;
	struct hep_chunk_uint16 dst_port;
	struct hep_chunk_uint32 time_sec;
	struct hep_chunk_uint32 time_usec;
	struct hep_chunk_uint8  proto_t;
	struct hep_chunk_uint32 capt_id;
} __attribute__((packed));

/*! \brief Global configuration for the module */
struct hepv3_global_config {
	unsigned int enabled;                    /*!< Whether or not sending is enabled */
	unsigned int capture_id;                 /*!< Capture ID for this agent */
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(capture_address);   /*!< Address to send to */
		AST_STRING_FIELD(capture_password);  /*!< Password for Homer server */
	);
};

/*! \brief The actual module config */
struct module_config {
	struct hepv3_global_config *general; /*!< The general config settings */
};

/*! \brief Run-time data derived from \ref hepv3_global_config */
struct hepv3_runtime_data {
	struct ast_sockaddr remote_addr;  /*!< The address to send to */
	int sockfd;                       /*!< The socket file descriptor */
};

static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "general",
	.item_offset = offsetof(struct module_config, general),
	.category_match = ACO_WHITELIST,
	.category = "^general$",
};

struct aco_type *global_options[] = ACO_TYPES(&global_option);

struct aco_file hepv3_conf = {
	.filename = "hep.conf",
	.types = ACO_TYPES(&global_option),
};

/*! \brief The module configuration container */
static AO2_GLOBAL_OBJ_STATIC(global_config);

/*! \brief Current module data */
static AO2_GLOBAL_OBJ_STATIC(global_data);

static struct ast_taskprocessor *hep_queue_tp;

static void *module_config_alloc(void);
static void hepv3_config_post_apply(void);

/*! \brief Register information about the configs being processed by this module */
CONFIG_INFO_STANDARD(cfg_info, global_config, module_config_alloc,
	.files = ACO_FILES(&hepv3_conf),
	.post_apply_config = hepv3_config_post_apply,
);

static void hepv3_config_dtor(void *obj)
{
	struct hepv3_global_config *config = obj;

	ast_string_field_free_memory(config);
}

/*! \brief HEPv3 configuration object allocation */
static void *hepv3_config_alloc(void)
{
	struct hepv3_global_config *config;

	config = ao2_alloc(sizeof(*config), hepv3_config_dtor);
	if (!config || ast_string_field_init(config, 32)) {
		return NULL;
	}

	return config;
}

/*! \brief Configuration object destructor */
static void module_config_dtor(void *obj)
{
	struct module_config *config = obj;

	if (config->general) {
		ao2_ref(config->general, -1);
	}
}

/*! \brief Module config constructor */
static void *module_config_alloc(void)
{
	struct module_config *config;

	config = ao2_alloc(sizeof(*config), module_config_dtor);
	if (!config) {
		return NULL;
	}

	config->general = hepv3_config_alloc();
	if (!config->general) {
		ao2_ref(config, -1);
		config = NULL;
	}

	return config;
}

/*! \brief HEPv3 run-time data destructor */
static void hepv3_data_dtor(void *obj)
{
	struct hepv3_runtime_data *data = obj;

	if (data->sockfd > -1) {
		close(data->sockfd);
		data->sockfd = -1;
	}
}

/*! \brief Allocate the HEPv3 run-time data */
static struct hepv3_runtime_data *hepv3_data_alloc(struct hepv3_global_config *config)
{
	struct hepv3_runtime_data *data;

	data = ao2_alloc(sizeof(*data), hepv3_data_dtor);
	if (!data) {
		return NULL;
	}

	if (!ast_sockaddr_parse(&data->remote_addr, config->capture_address, PARSE_PORT_REQUIRE)) {
		ast_log(AST_LOG_WARNING, "Failed to create address from %s\n", config->capture_address);
		ao2_ref(data, -1);
		return NULL;
	}

	data->sockfd = socket(ast_sockaddr_is_ipv6(&data->remote_addr) ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
	if (data->sockfd < 0) {
		ast_log(AST_LOG_WARNING, "Failed to create socket for address %s: %s\n",
				config->capture_address, strerror(errno));
		ao2_ref(data, -1);
		return NULL;
	}

	return data;
}

/*! \brief Destructor for a \ref hepv3_capture_info object */
static void capture_info_dtor(void *obj)
{
	struct hepv3_capture_info *info = obj;

	ast_free(info->uuid);
	ast_free(info->payload);
}

struct hepv3_capture_info *hepv3_create_capture_info(const void *payload, size_t len)
{
	struct hepv3_capture_info *info;

	info = ao2_alloc(sizeof(*info), capture_info_dtor);
	if (!info) {
		return NULL;
	}

	info->payload = ast_malloc(len);
	if (!info->payload) {
		ao2_ref(info, -1);
		return NULL;
	}
	memcpy(info->payload, payload, len);
	info->len = len;

	return info;
}

/*! \brief Callback function for the \ref hep_queue_tp taskprocessor */
static int hep_queue_cb(void *data)
{
	RAII_VAR(struct module_config *, config, ao2_global_obj_ref(global_config), ao2_cleanup);
	RAII_VAR(struct hepv3_runtime_data *, hepv3_data, ao2_global_obj_ref(global_data), ao2_cleanup);
	RAII_VAR(struct hepv3_capture_info *, capture_info, data, ao2_cleanup);
	struct hep_generic hg_pkt;
	unsigned int packet_len = 0, sock_buffer_len;
	struct hep_chunk_ip4 ipv4_src, ipv4_dst;
	struct hep_chunk_ip6 ipv6_src, ipv6_dst;
	struct hep_chunk auth_key, payload, uuid;
	void *sock_buffer;
	int res;

	if (!capture_info || !config || !hepv3_data) {
		return 0;
	}

	if (ast_sockaddr_is_ipv4(&capture_info->src_addr) != ast_sockaddr_is_ipv4(&capture_info->dst_addr)) {
		ast_log(AST_LOG_NOTICE, "Unable to send packet: Address Family mismatch between source/destination\n");
		return -1;
	}

	packet_len = sizeof(hg_pkt);

	/* Build HEPv3 header, capture info, and calculate the total packet size */
	memcpy(hg_pkt.header.id, "\x48\x45\x50\x33", 4);

	INITIALIZE_GENERIC_HEP_CHUNK_DATA(&hg_pkt.ip_proto, CHUNK_TYPE_IP_PROTOCOL_ID, 0x11);
	INITIALIZE_GENERIC_HEP_CHUNK_DATA(&hg_pkt.src_port, CHUNK_TYPE_SRC_PORT, htons(ast_sockaddr_port(&capture_info->src_addr)));
	INITIALIZE_GENERIC_HEP_CHUNK_DATA(&hg_pkt.dst_port, CHUNK_TYPE_DST_PORT, htons(ast_sockaddr_port(&capture_info->dst_addr)));
	INITIALIZE_GENERIC_HEP_CHUNK_DATA(&hg_pkt.time_sec, CHUNK_TYPE_TIMESTAMP_SEC, htonl(capture_info->capture_time.tv_sec));
	INITIALIZE_GENERIC_HEP_CHUNK_DATA(&hg_pkt.time_usec, CHUNK_TYPE_TIMESTAMP_USEC, htonl(capture_info->capture_time.tv_usec));
	INITIALIZE_GENERIC_HEP_CHUNK_DATA(&hg_pkt.proto_t, CHUNK_TYPE_PROTOCOL_TYPE, capture_info->capture_type);
	INITIALIZE_GENERIC_HEP_CHUNK_DATA(&hg_pkt.capt_id, CHUNK_TYPE_CAPTURE_AGENT_ID, htonl(config->general->capture_id));

	if (ast_sockaddr_is_ipv4(&capture_info->src_addr)) {
		INITIALIZE_GENERIC_HEP_CHUNK_DATA(&hg_pkt.ip_family,
			CHUNK_TYPE_IP_PROTOCOL_FAMILY, AF_INET);

		INITIALIZE_GENERIC_HEP_CHUNK(&ipv4_src, CHUNK_TYPE_IPV4_SRC_ADDR);
		inet_pton(AF_INET, ast_sockaddr_stringify_addr(&capture_info->src_addr), &ipv4_src.data);

		INITIALIZE_GENERIC_HEP_CHUNK(&ipv4_dst, CHUNK_TYPE_IPV4_DST_ADDR);
		inet_pton(AF_INET, ast_sockaddr_stringify_addr(&capture_info->dst_addr), &ipv4_dst.data);

		packet_len += (sizeof(ipv4_src) + sizeof(ipv4_dst));
	} else {
		INITIALIZE_GENERIC_HEP_CHUNK_DATA(&hg_pkt.ip_family,
			CHUNK_TYPE_IP_PROTOCOL_FAMILY, AF_INET6);

		INITIALIZE_GENERIC_HEP_CHUNK(&ipv6_src, CHUNK_TYPE_IPV6_SRC_ADDR);
		inet_pton(AF_INET6, ast_sockaddr_stringify_addr(&capture_info->src_addr), &ipv6_src.data);

		INITIALIZE_GENERIC_HEP_CHUNK(&ipv6_dst, CHUNK_TYPE_IPV6_DST_ADDR);
		inet_pton(AF_INET6, ast_sockaddr_stringify_addr(&capture_info->dst_addr), &ipv6_dst.data);

		packet_len += (sizeof(ipv6_src) + sizeof(ipv6_dst));
	}

	if (!ast_strlen_zero(config->general->capture_password))  {
		INITIALIZE_GENERIC_HEP_IDS_VAR(&auth_key, CHUNK_TYPE_AUTH_KEY, strlen(config->general->capture_password));
		packet_len += (sizeof(auth_key) + strlen(config->general->capture_password));
	}
	INITIALIZE_GENERIC_HEP_IDS_VAR(&uuid, CHUNK_TYPE_UUID, strlen(capture_info->uuid));
	packet_len += (sizeof(uuid) + strlen(capture_info->uuid));
	INITIALIZE_GENERIC_HEP_IDS_VAR(&payload,
		capture_info->zipped ? CHUNK_TYPE_PAYLOAD_ZIP : CHUNK_TYPE_PAYLOAD, capture_info->len);
	packet_len += (sizeof(payload) + capture_info->len);
	hg_pkt.header.length = htons(packet_len);

	/* Build the buffer to send */
	sock_buffer = ast_malloc(packet_len);
	if (!sock_buffer) {
		return -1;
	}

	/* Copy in the header */
	memcpy(sock_buffer, &hg_pkt, sizeof(hg_pkt));
	sock_buffer_len = sizeof(hg_pkt);

	/* Addresses */
	if (ast_sockaddr_is_ipv4(&capture_info->src_addr)) {
		memcpy(sock_buffer + sock_buffer_len, &ipv4_src, sizeof(ipv4_src));
		sock_buffer_len += sizeof(ipv4_src);
		memcpy(sock_buffer + sock_buffer_len, &ipv4_dst, sizeof(ipv4_dst));
		sock_buffer_len += sizeof(ipv4_dst);
	} else {
		memcpy(sock_buffer + sock_buffer_len, &ipv6_src, sizeof(ipv6_src));
		sock_buffer_len += sizeof(ipv6_src);
		memcpy(sock_buffer + sock_buffer_len, &ipv6_dst, sizeof(ipv6_dst));
		sock_buffer_len += sizeof(ipv6_dst);
	}

	/* Auth Key */
	if (!ast_strlen_zero(config->general->capture_password)) {
		memcpy(sock_buffer + sock_buffer_len, &auth_key, sizeof(auth_key));
		sock_buffer_len += sizeof(auth_key);
		memcpy(sock_buffer + sock_buffer_len, config->general->capture_password, strlen(config->general->capture_password));
		sock_buffer_len += strlen(config->general->capture_password);
	}

	/* UUID */
	memcpy(sock_buffer + sock_buffer_len, &uuid, sizeof(uuid));
	sock_buffer_len += sizeof(uuid);
	memcpy(sock_buffer + sock_buffer_len, capture_info->uuid, strlen(capture_info->uuid));
	sock_buffer_len += strlen(capture_info->uuid);

	/* Packet! */
	memcpy(sock_buffer + sock_buffer_len, &payload, sizeof(payload));
	sock_buffer_len += sizeof(payload);
	memcpy(sock_buffer + sock_buffer_len, capture_info->payload, capture_info->len);
	sock_buffer_len += capture_info->len;

	ast_assert(sock_buffer_len == packet_len);

	res = ast_sendto(hepv3_data->sockfd, sock_buffer, sock_buffer_len, 0, &hepv3_data->remote_addr);
	if (res < 0) {
		ast_log(AST_LOG_ERROR, "Error [%d] while sending packet to HEPv3 server: %s\n",
			errno, strerror(errno));
	} else if (res != sock_buffer_len) {
		ast_log(AST_LOG_WARNING, "Failed to send complete packet to HEPv3 server: %d of %u sent\n",
			res, sock_buffer_len);
		res = -1;
	}

	ast_free(sock_buffer);
	return res;
}

int hepv3_send_packet(struct hepv3_capture_info *capture_info)
{
	RAII_VAR(struct module_config *, config, ao2_global_obj_ref(global_config), ao2_cleanup);
	int res;

	if (!config || !config->general->enabled) {
		return 0;
	}

	res = ast_taskprocessor_push(hep_queue_tp, hep_queue_cb, capture_info);
	if (res == -1) {
		ao2_ref(capture_info, -1);
	}

	return res;
}

/*!
 * \brief Post-apply callback for the config framework.
 *
 * This will create the run-time information from the supplied
 * configuration.
*/
static void hepv3_config_post_apply(void)
{
	RAII_VAR(struct module_config *, mod_cfg, ao2_global_obj_ref(global_config), ao2_cleanup);
	struct hepv3_runtime_data *data;

	data = hepv3_data_alloc(mod_cfg->general);
	if (!data) {
		return;
	}

	ao2_global_obj_replace_unref(global_data, data);
}

/*!
 * \brief Reload the module
 */
static int reload_module(void)
{
	if (aco_process_config(&cfg_info, 1) == ACO_PROCESS_ERROR) {
		return -1;
	}
	return 0;
}

/*!
 * \brief Unload the module
 */
static int unload_module(void)
{
	hep_queue_tp = ast_taskprocessor_unreference(hep_queue_tp);

	ao2_global_obj_release(global_config);
	ao2_global_obj_release(global_data);
	aco_info_destroy(&cfg_info);

	return 0;
}

/*!
 * \brief Load the module
 */
static int load_module(void)
{
	if (aco_info_init(&cfg_info)) {
		goto error;
	}

	hep_queue_tp = ast_taskprocessor_get("hep_queue_tp", TPS_REF_DEFAULT);
	if (!hep_queue_tp) {
		goto error;
	}

	aco_option_register(&cfg_info, "enabled", ACO_EXACT, global_options, "yes", OPT_BOOL_T, 1, FLDSET(struct hepv3_global_config, enabled));
	aco_option_register(&cfg_info, "capture_address", ACO_EXACT, global_options, DEFAULT_HEP_SERVER, OPT_STRINGFIELD_T, 0, STRFLDSET(struct hepv3_global_config, capture_address));
	aco_option_register(&cfg_info, "capture_password", ACO_EXACT, global_options, "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct hepv3_global_config, capture_password));
	aco_option_register(&cfg_info, "capture_id", ACO_EXACT, global_options, "0", OPT_UINT_T, 0, STRFLDSET(struct hepv3_global_config, capture_id));

	if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
		goto error;
	}

	return AST_MODULE_LOAD_SUCCESS;

error:
	aco_info_destroy(&cfg_info);
	return AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "HEPv3 API",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	);
