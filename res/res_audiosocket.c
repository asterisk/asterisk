/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, CyCore Systems, Inc
 *
 * Seán C McCord <scm@cycoresys.com
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
 * \brief AudioSocket support for Asterisk
 *
 * \author Seán C McCord <scm@cycoresys.com>
 *
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"
#include "errno.h"
#include <uuid/uuid.h>
#include <arpa/inet.h>  /* For byte-order conversion. */

#include "asterisk/file.h"
#include "asterisk/res_audiosocket.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/uuid.h"
#include "asterisk/format_cache.h"

#define	MODULE_DESCRIPTION	"AudioSocket support functions for Asterisk"

#define MAX_CONNECT_TIMEOUT_MSEC 2000

/*!
 * \internal
 * \brief Attempt to complete the audiosocket connection.
 *
 * \param server Url that we are trying to connect to.
 * \param addr Address that host was resolved to.
 * \param netsockfd File descriptor of socket.
 *
 * \retval 0 when connection is succesful.
 * \retval 1 when there is an error.
 */
static int handle_audiosocket_connection(const char *server,
	const struct ast_sockaddr addr, const int netsockfd)
{
	struct pollfd pfds[1];
	int res, conresult;
	socklen_t reslen;

	reslen = sizeof(conresult);

	pfds[0].fd = netsockfd;
	pfds[0].events = POLLOUT;

	while ((res = ast_poll(pfds, 1, MAX_CONNECT_TIMEOUT_MSEC)) != 1) {
		if (errno != EINTR) {
			if (!res) {
				ast_log(LOG_WARNING, "AudioSocket connection to '%s' timed"
					"out after MAX_CONNECT_TIMEOUT_MSEC (%d) milliseconds.\n",
					server, MAX_CONNECT_TIMEOUT_MSEC);
			} else {
				ast_log(LOG_WARNING, "Connect to '%s' failed: %s\n", server,
					strerror(errno));
			}

			return -1;
		}
	}

	if (getsockopt(pfds[0].fd, SOL_SOCKET, SO_ERROR, &conresult, &reslen) < 0) {
		ast_log(LOG_WARNING, "Connection to '%s' failed with error: %s\n",
			ast_sockaddr_stringify(&addr), strerror(errno));
		return -1;
	}

	if (conresult) {
		ast_log(LOG_WARNING, "Connecting to '%s' failed for url '%s': %s\n",
			ast_sockaddr_stringify(&addr), server, strerror(conresult));
		return -1;
	}

	return 0;
}

const int ast_audiosocket_connect(const char *server, struct ast_channel *chan)
{
	int s = -1;
	struct ast_sockaddr *addrs = NULL;
	int num_addrs = 0, i = 0;

	if (chan && ast_autoservice_start(chan) < 0) {
		ast_log(LOG_WARNING, "Failed to start autoservice for channel "
			"'%s'\n", ast_channel_name(chan));
		goto end;
	}

	if (ast_strlen_zero(server)) {
		ast_log(LOG_ERROR, "No AudioSocket server provided\n");
		goto end;
	}

	if (!(num_addrs = ast_sockaddr_resolve(&addrs, server, PARSE_PORT_REQUIRE,
		AST_AF_UNSPEC))) {
		ast_log(LOG_ERROR, "Failed to resolve AudioSocket service using '%s' - "
			"requires a valid hostname and port\n", server);
		goto end;
	}

	/* Connect to AudioSocket service */
	for (i = 0; i < num_addrs; i++) {

		if (!ast_sockaddr_port(&addrs[i])) {
			/* If there's no port, other addresses should have the
			 * same problem. Stop here.
			 */
			ast_log(LOG_ERROR, "No port provided for '%s'\n",
				ast_sockaddr_stringify(&addrs[i]));
			s = -1;
			goto end;
		}

		if ((s = ast_socket_nonblock(addrs[i].ss.ss_family, SOCK_STREAM,
			IPPROTO_TCP)) < 0) {
			ast_log(LOG_WARNING, "Unable to create socket: '%s'\n", strerror(errno));
			continue;
		}

		/*
		 * Disable Nagle's algorithm by setting the TCP_NODELAY socket option.
		 * This reduces latency by preventing delays caused by packet buffering.
		 */
		if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &(int){1}, sizeof(int)) < 0) {
			ast_log(LOG_ERROR, "Failed to set TCP_NODELAY on AudioSocket: %s\n", strerror(errno));
		}

		if (ast_connect(s, &addrs[i]) && errno == EINPROGRESS) {

			if (handle_audiosocket_connection(server, addrs[i], s)) {
				close(s);
				continue;
			}

		} else {
			ast_log(LOG_ERROR, "Connection to '%s' failed with unexpected error: %s\n",
				ast_sockaddr_stringify(&addrs[i]), strerror(errno));
			close(s);
			s = -1;
		}

		break;
	}

end:
	if (addrs) {
		ast_free(addrs);
	}

	if (chan && ast_autoservice_stop(chan) < 0) {
		ast_log(LOG_WARNING, "Failed to stop autoservice for channel '%s'\n",
		ast_channel_name(chan));
		close(s);
		return -1;
	}

	if (i == num_addrs) {
		ast_log(LOG_ERROR, "Failed to connect to AudioSocket service\n");
		close(s);
		return -1;
	}

	return s;
}

const int ast_audiosocket_init(const int svc, const char *id)
{
	uuid_t uu;
	int ret = 0;
	uint8_t buf[3 + 16];

	if (ast_strlen_zero(id)) {
		ast_log(LOG_ERROR, "No UUID for AudioSocket\n");
		return -1;
	}

	if (uuid_parse(id, uu)) {
		ast_log(LOG_ERROR, "Failed to parse UUID '%s'\n", id);
		return -1;
	}

	buf[0] = AST_AUDIOSOCKET_KIND_UUID;
	buf[1] = 0x00;
	buf[2] = 0x10;
	memcpy(buf + 3, uu, 16);

	if (write(svc, buf, 3 + 16) != 3 + 16) {
		ast_log(LOG_WARNING, "Failed to write data to AudioSocket because: %s\n", strerror(errno));
		ret = -1;
	}

	return ret;
}

const int ast_audiosocket_send_frame(const int svc, const struct ast_frame *f)
{
	int datalen = f->datalen;
	if (f->frametype == AST_FRAME_DTMF) {
		datalen = 1;
	}

	{
		uint8_t buf[3 + datalen];
		uint16_t *length = (uint16_t *) &buf[1];

		/* Audio format is 16-bit, 8kHz signed linear mono for dialplan app,
			depends on agreed upon audio codec for channel driver interface. */
		switch (f->frametype) {
			case AST_FRAME_VOICE:
				if (ast_format_cmp(f->subclass.format, ast_format_slin) == AST_FORMAT_CMP_EQUAL) {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO;
				} else if (ast_format_cmp(f->subclass.format, ast_format_slin12) == AST_FORMAT_CMP_EQUAL) {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO_SLIN12;
				} else if (ast_format_cmp(f->subclass.format, ast_format_slin16) == AST_FORMAT_CMP_EQUAL) {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO_SLIN16;
				} else if (ast_format_cmp(f->subclass.format, ast_format_slin24) == AST_FORMAT_CMP_EQUAL) {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO_SLIN24;
				} else if (ast_format_cmp(f->subclass.format, ast_format_slin32) == AST_FORMAT_CMP_EQUAL) {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO_SLIN32;
				} else if (ast_format_cmp(f->subclass.format, ast_format_slin44) == AST_FORMAT_CMP_EQUAL) {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO_SLIN44;
				} else if (ast_format_cmp(f->subclass.format, ast_format_slin48) == AST_FORMAT_CMP_EQUAL) {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO_SLIN48;
				} else if (ast_format_cmp(f->subclass.format, ast_format_slin96) == AST_FORMAT_CMP_EQUAL) {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO_SLIN96;
				} else if (ast_format_cmp(f->subclass.format, ast_format_slin192) == AST_FORMAT_CMP_EQUAL) {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO_SLIN192;
				} else {
					buf[0] = AST_AUDIOSOCKET_KIND_AUDIO;
				}

				*length = htons(datalen);
				memcpy(&buf[3], f->data.ptr, datalen);
				break;
			case AST_FRAME_DTMF:
				buf[0] = AST_AUDIOSOCKET_KIND_DTMF;
				buf[3] = (uint8_t) f->subclass.integer;
				*length = htons(1);
				break;
			default:
				ast_log(LOG_ERROR, "Unsupported frame type %d for AudioSocket\n", f->frametype);
				return -1;
		}

		if (write(svc, buf, 3 + datalen) != 3 + datalen) {
			ast_log(LOG_WARNING, "Failed to write data to AudioSocket because: %s\n", strerror(errno));
			return -1;
		}
	}

	return 0;
}

struct ast_frame *ast_audiosocket_receive_frame(const int svc)
{
	return ast_audiosocket_receive_frame_with_hangup(svc, NULL);
}

struct ast_frame *ast_audiosocket_receive_frame_with_hangup(const int svc,
	int *const hangup)
{
	int i = 0, n = 0, ret = 0;
	struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.src = "AudioSocket",
		.mallocd = AST_MALLOCD_DATA,
	};
	uint8_t header[3];
	uint8_t *kind = &header[0];
	uint16_t *length = (uint16_t *) &header[1];
	uint8_t *data;

	if (hangup) {
		*hangup = 0;
	}

	while (i < 3) {
		n = read(svc, header, 3);
		if (n == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				int poll_result = ast_wait_for_input(svc, 5);

				if (poll_result == 1) {
					continue;
				} else if (poll_result == 0) {
					ast_debug(1, "Poll timed out while waiting for header data\n");
					continue;
				} else {
					ast_log(LOG_WARNING, "Poll error: %s\n", strerror(errno));
				}
			}

			ast_log(LOG_ERROR, "Failed to read header from AudioSocket because: %s\n", strerror(errno));
			return NULL;
		}
		if (n == 0) {
			break;
		}
		i += n;
	}

	if (n == 0 || *kind == AST_AUDIOSOCKET_KIND_HANGUP) {
		/* Socket closure or requested hangup. */
		if (hangup) {
			*hangup = 1;
		}
		return NULL;
	}

	switch (*kind) {
		case AST_AUDIOSOCKET_KIND_AUDIO:
			f.subclass.format = ast_format_slin;
			break;
		case AST_AUDIOSOCKET_KIND_AUDIO_SLIN12:
			f.subclass.format = ast_format_slin12;
			break;
		case AST_AUDIOSOCKET_KIND_AUDIO_SLIN16:
			f.subclass.format = ast_format_slin16;
			break;
		case AST_AUDIOSOCKET_KIND_AUDIO_SLIN24:
			f.subclass.format = ast_format_slin24;
			break;
		case AST_AUDIOSOCKET_KIND_AUDIO_SLIN32:
			f.subclass.format = ast_format_slin32;
			break;
		case AST_AUDIOSOCKET_KIND_AUDIO_SLIN44:
			f.subclass.format = ast_format_slin44;
			break;
		case AST_AUDIOSOCKET_KIND_AUDIO_SLIN48:
			f.subclass.format = ast_format_slin48;
			break;
		case AST_AUDIOSOCKET_KIND_AUDIO_SLIN96:
			f.subclass.format = ast_format_slin96;
			break;
		case AST_AUDIOSOCKET_KIND_AUDIO_SLIN192:
			f.subclass.format = ast_format_slin192;
			break;
		default:
			ast_log(LOG_ERROR, "Received AudioSocket message other than hangup or audio, refer to protocol specification for valid message types\n");
			return NULL;
	}

	/* Swap endianess of length if needed. */
	*length = ntohs(*length);
	if (*length < 1) {
		ast_log(LOG_ERROR, "Invalid message length received from AudioSocket server. \n");
		return NULL;
	}

	data = ast_malloc(*length);
	if (!data) {
		ast_log(LOG_ERROR, "Failed to allocate for data from AudioSocket\n");
		return NULL;
	}

	ret = 0;
	n = 0;
	i = 0;
	while (i < *length) {
		n = read(svc, data + i, *length - i);
		if (n == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				int poll_result = ast_wait_for_input(svc, 5);

				if (poll_result == 1) {
					continue;
				} else if (poll_result == 0) {
					ast_log(LOG_WARNING, "Poll timed out while waiting for data\n");
				} else {
					ast_log(LOG_WARNING, "Poll error: %s\n", strerror(errno));
				}
			}

			ast_log(LOG_ERROR, "Failed to read payload from AudioSocket: %s\n", strerror(errno));
			ret = -1;
			break;
		}
		if (n == 0) {
			ast_log(LOG_ERROR, "Insufficient payload read from AudioSocket\n");
			ret = -1;
			break;
		}
		i += n;
	}

	if (ret != 0) {
		ast_free(data);
		return NULL;
	}

	f.data.ptr = data;
	f.datalen = *length;
	f.samples = *length / 2;

	/* The frame steals data, so it doesn't need to be freed here */
	return ast_frisolate(&f);
}

static int load_module(void)
{
	ast_verb(5, "Loading AudioSocket Support module\n");
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_verb(5, "Unloading AudioSocket Support module\n");
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "AudioSocket support",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
