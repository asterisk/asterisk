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
		ast_log(LOG_WARNING, "Connection to %s failed with error: %s\n",
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
			"%s\n", ast_channel_name(chan));
		goto end;
	}

	if (ast_strlen_zero(server)) {
		ast_log(LOG_ERROR, "No AudioSocket server provided\n");
		goto end;
	}

	if (!(num_addrs = ast_sockaddr_resolve(&addrs, server, PARSE_PORT_REQUIRE,
		AST_AF_UNSPEC))) {
		ast_log(LOG_ERROR, "Failed to resolve AudioSocket service using %s - "
			"requires a valid hostname and port\n", server);
		goto end;
	}

	/* Connect to AudioSocket service */
	for (i = 0; i < num_addrs; i++) {

		if (!ast_sockaddr_port(&addrs[i])) {
			/* If there's no port, other addresses should have the
			 * same problem. Stop here.
			 */
			ast_log(LOG_ERROR, "No port provided for %s\n",
				ast_sockaddr_stringify(&addrs[i]));
			s = -1;
			goto end;
		}

		if ((s = ast_socket_nonblock(addrs[i].ss.ss_family, SOCK_STREAM,
			IPPROTO_TCP)) < 0) {
			ast_log(LOG_WARNING, "Unable to create socket: %s\n", strerror(errno));
			continue;
		}

		if (ast_connect(s, &addrs[i]) && errno == EINPROGRESS) {

			if (handle_audiosocket_connection(server, addrs[i], s)) {
				close(s);
				continue;
			}

		} else {
			ast_log(LOG_ERROR, "Connection to %s failed with unexpected error: %s\n",
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
		ast_log(LOG_WARNING, "Failed to stop autoservice for channel %s\n",
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

	buf[0] = 0x01;
	buf[1] = 0x00;
	buf[2] = 0x10;
	memcpy(buf + 3, uu, 16);

	if (write(svc, buf, 3 + 16) != 3 + 16) {
		ast_log(LOG_WARNING, "Failed to write data to AudioSocket\n");
		ret = -1;
	}

	return ret;
}

const int ast_audiosocket_send_frame(const int svc, const struct ast_frame *f)
{
	int ret = 0;
	uint8_t kind = 0x10;	/* always 16-bit, 8kHz signed linear mono, for now */
	uint8_t *p;
	uint8_t buf[3 + f->datalen];

	p = buf;

	*(p++) = kind;
	*(p++) = f->datalen >> 8;
	*(p++) = f->datalen & 0xff;
	memcpy(p, f->data.ptr, f->datalen);

	if (write(svc, buf, 3 + f->datalen) != 3 + f->datalen) {
		ast_log(LOG_WARNING, "Failed to write data to AudioSocket\n");
		ret = -1;
	}

	return ret;
}

struct ast_frame *ast_audiosocket_receive_frame(const int svc)
{

	int i = 0, n = 0, ret = 0, not_audio = 0;
	struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.subclass.format = ast_format_slin,
		.src = "AudioSocket",
		.mallocd = AST_MALLOCD_DATA,
	};
	uint8_t kind;
	uint8_t len_high;
	uint8_t len_low;
	uint16_t len = 0;
	uint8_t *data;

	n = read(svc, &kind, 1);
	if (n < 0 && errno == EAGAIN) {
		return &ast_null_frame;
	}
	if (n == 0) {
		return &ast_null_frame;
	}
	if (n != 1) {
		ast_log(LOG_WARNING, "Failed to read type header from AudioSocket\n");
		return NULL;
	}
	if (kind == 0x00) {
		/* AudioSocket ended by remote */
		return NULL;
	}
	if (kind != 0x10) {
		/* read but ignore non-audio message */
		ast_log(LOG_WARNING, "Received non-audio AudioSocket message\n");
		not_audio = 1;
	}

	n = read(svc, &len_high, 1);
	if (n != 1) {
		ast_log(LOG_WARNING, "Failed to read data length from AudioSocket\n");
		return NULL;
	}
	len += len_high * 256;
	n = read(svc, &len_low, 1);
	if (n != 1) {
		ast_log(LOG_WARNING, "Failed to read data length from AudioSocket\n");
		return NULL;
	}
	len += len_low;

	if (len < 1) {
		return &ast_null_frame;
	}

	data = ast_malloc(len);
	if (!data) {
		ast_log(LOG_ERROR, "Failed to allocate for data from AudioSocket\n");
		return NULL;
	}

	ret = 0;
	n = 0;
	i = 0;
	while (i < len) {
		n = read(svc, data + i, len - i);
		if (n < 0) {
			ast_log(LOG_ERROR, "Failed to read data from AudioSocket\n");
			ret = n;
			break;
		}
		if (n == 0) {
			ast_log(LOG_ERROR, "Insufficient data read from AudioSocket\n");
			ret = -1;
			break;
		}
		i += n;
	}

	if (ret != 0) {
		ast_free(data);
		return NULL;
	}

	if (not_audio) {
		ast_free(data);
		return &ast_null_frame;
	}

	f.data.ptr = data;
	f.datalen = len;
	f.samples = len / 2;

	/* The frame steals data, so it doesn't need to be freed here */
	return ast_frisolate(&f);
}

static int load_module(void)
{
	ast_verb(1, "Loading AudioSocket Support module\n");
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_verb(1, "Unloading AudioSocket Support module\n");
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "AudioSocket support",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
