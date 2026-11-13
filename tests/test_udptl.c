/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Arcadiy Ivanov
 *
 * Arcadiy Ivanov <arcadiy@ivanov.biz>
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
 * \brief UDPTL Unit Tests
 *
 * \author Arcadiy Ivanov <arcadiy@ivanov.biz>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/udptl.h"
#include "asterisk/frame.h"
#include "asterisk/netsock2.h"
#include "asterisk/config.h" /* PARSE_PORT_* */
#include "asterisk/poll-compat.h"

#include <unistd.h>

/*!
 * \internal
 * \brief Bind a UDP socket to \a address with an ephemeral port
 *
 * \return The socket, or -1 on failure
 */
static int test_udp_socket(const char *address, struct ast_sockaddr *bound)
{
	struct ast_sockaddr addr;
	int fd;

	ast_sockaddr_parse(&addr, address, PARSE_PORT_FORBID);
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return -1;
	}
	if (ast_bind(fd, &addr) || ast_getsockname(fd, bound)) {
		close(fd);
		return -1;
	}
	return fd;
}

/*!
 * \internal
 * \brief Receive one datagram within a second and report its source
 *
 * \retval 0 received
 * \retval -1 nothing received
 */
static int test_udp_recv(int fd, struct ast_sockaddr *from)
{
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	char buf[1500];

	if (ast_poll(&pfd, 1, 1000) != 1) {
		return -1;
	}
	return ast_recvfrom(fd, buf, sizeof(buf), 0, from) < 0 ? -1 : 0;
}

AST_TEST_DEFINE(source_address)
{
	/*
	 * The UDPTL session is bound to a wildcard, as res_pjsip_t38 does, or to
	 * a specific address, as with t38_bind_udptl_to_media_address, which it
	 * must keep sending from.
	 */
	static const char *binds[] = { "0.0.0.0", "::", "127.0.0.1" };
	static const struct {
		/*! Source address to set, or NULL to clear it */
		const char *source;
		/*! Source address the receiver must see, for each of binds */
		const char *expected[3];
	} steps[] = {
		{ "127.0.0.2", { "127.0.0.2", "127.0.0.2", "127.0.0.1" } },
		{ NULL, { "127.0.0.1", "127.0.0.1", "127.0.0.1" } },
	};
	unsigned char ifp[] = { 0x00, 0x01, 0x02, 0x03 };
	struct ast_frame frame = {
		.frametype = AST_FRAME_MODEM,
		.subclass.integer = AST_MODEM_T38,
		.data.ptr = ifp,
		.datalen = sizeof(ifp),
	};
	struct ast_sockaddr peer, us, from, source, expected;
	enum ast_test_result_state res = AST_TEST_PASS;
	int i, j, fd;

	switch (cmd) {
	case TEST_INIT:
		info->name = "source_address";
		info->category = "/main/udptl/";
		info->summary = "UDPTL source address unit test";
		info->description =
			"Tests that UDPTL packets from a wildcard-bound session leave from\n"
			"the source address set on the session, from the session's port,\n"
			"and that a session bound to a specific address keeps sending from\n"
			"that address.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* 127.0.0.2 is a second local address on Linux */
	fd = test_udp_socket("127.0.0.2", &us);
	if (fd < 0) {
		ast_test_status_update(test, "127.0.0.2 is not a local address on this host\n");
		return AST_TEST_NOT_RUN;
	}
	close(fd);

	fd = test_udp_socket("127.0.0.1", &peer);
	if (fd < 0) {
		ast_test_status_update(test, "Unable to bind receiver to 127.0.0.1\n");
		return AST_TEST_FAIL;
	}

	for (i = 0; i < ARRAY_LEN(binds); i++) {
		struct ast_sockaddr bind_addr;
		struct ast_udptl *udptl;

		ast_sockaddr_parse(&bind_addr, binds[i], PARSE_PORT_FORBID);
		if (ast_sockaddr_is_ipv6(&bind_addr) && !ast_check_ipv6()) {
			ast_test_status_update(test, "Skipping bind %s: IPv6 is not available\n", binds[i]);
			continue;
		}
		udptl = ast_udptl_new_with_bindaddr(NULL, NULL, 0, &bind_addr);
		if (!udptl) {
			ast_test_status_update(test, "Bind %s: unable to create UDPTL session\n", binds[i]);
			res = AST_TEST_FAIL;
			continue;
		}
		ast_udptl_set_far_max_datagram(udptl, 400);
		ast_udptl_set_peer(udptl, &peer);
		ast_udptl_get_us(udptl, &us);

		for (j = 0; j < ARRAY_LEN(steps); j++) {
			if (steps[j].source) {
				ast_sockaddr_parse(&source, steps[j].source, PARSE_PORT_FORBID);
			}
			ast_udptl_set_source_address(udptl, steps[j].source ? &source : NULL);
			ast_sockaddr_parse(&expected, steps[j].expected[i], PARSE_PORT_FORBID);

			ast_udptl_write(udptl, &frame);
			if (test_udp_recv(fd, &from)) {
				ast_test_status_update(test, "Bind %s, source %s: no UDPTL received\n",
					binds[i], S_OR(steps[j].source, "(none)"));
				res = AST_TEST_FAIL;
			} else if (ast_sockaddr_cmp_addr(&from, &expected)
				|| ast_sockaddr_port(&from) != ast_sockaddr_port(&us)) {
				ast_test_status_update(test, "Bind %s, source %s: UDPTL from %s, expected %s:%d\n",
					binds[i], S_OR(steps[j].source, "(none)"), ast_sockaddr_stringify(&from),
					steps[j].expected[i], ast_sockaddr_port(&us));
				res = AST_TEST_FAIL;
			}
		}

		ast_udptl_destroy(udptl);
	}

	close(fd);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(source_address);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(source_address);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "UDPTL test module");
