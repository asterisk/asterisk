/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * Terry Wilson <twilson@digium.com>
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
 * \brief Netsock2 Unit Tests
 *
 * \author Terry Wilson <twilson@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/netsock2.h"
#include "asterisk/logger.h"
#include "asterisk/config.h" /* PARSE_PORT_* */
#include "asterisk/poll-compat.h"
#include "asterisk/time.h"

#include <unistd.h>

struct parse_test {
	const char *address;
	int expected_result;
};

AST_TEST_DEFINE(parsing)
{
	int res = AST_TEST_PASS;
	struct parse_test test_vals[] = {
		{ "192.168.1.0", 1 },
		{ "10.255.255.254", 1 },
		{ "172.18.5.4", 1 },
		{ "8.8.4.4", 1 },
		{ "0.0.0.0", 1 },
		{ "127.0.0.1", 1 },
		{ "1.256.3.4", 0 },
		{ "256.0.0.1", 0 },
		{ "1.2.3.4:5060", 1 },
		{ "::ffff:5.6.7.8", 1 },
		{ "fdf8:f53b:82e4::53", 1 },
		{ "fe80::200:5aee:feaa:20a2", 1 },
		{ "2001::1", 1 },
		{ "2001:0000:4136:e378:8000:63bf:3fff:fdd2", 1 },
		{ "2001:0002:6c::430", 1 },
		{ "2001:10:240:ab::a", 1 },
		{ "2002:cb0a:3cdd:1::1", 1 },
		{ "2001:db8:8:4::2", 1 }, /* Documentation only, should never be used */
		{ "ff01:0:0:0:0:0:0:2", 1 }, /* Multicast */
		{ "[fdf8:f53b:82e4::53]", 1 },
		{ "[fe80::200:5aee:feaa:20a2]", 1 },
		{ "[2001::1]", 1 },
		{ "[2001:0000:4136:e378:8000:63bf:3fff:fdd2]:5060", 1 },
		{ "2001:0000:4136:e378:8000:63bf:3fff:fdd2:5060", 0 }, /* port, but no brackets */
		{ "fe80::200::abcd", 0 }, /* multiple zero expansions */
	};

	size_t x;
	struct ast_sockaddr addr;
	int parse_result;

	switch (cmd) {
	case TEST_INIT:
		info->name = "parsing";
		info->category = "/main/netsock2/";
		info->summary = "netsock2 parsing unit test";
		info->description =
			"Test parsing of IPv4 and IPv6 network addresses";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (x = 0; x < ARRAY_LEN(test_vals); x++) {
		memset(&addr, 0, sizeof(addr));
		if ((parse_result = ast_sockaddr_parse(&addr, test_vals[x].address, 0)) != test_vals[x].expected_result) {
			ast_test_status_update(test, "On '%s' expected %d but got %d\n", test_vals[x].address, test_vals[x].expected_result, parse_result);
			res = AST_TEST_FAIL;
		}
		if (parse_result) {
			struct ast_sockaddr tmp_addr;
			const char *tmp;

			tmp = ast_sockaddr_stringify(&addr);
			memset(&tmp_addr, 0, sizeof(tmp_addr));
			ast_sockaddr_parse(&tmp_addr, tmp, 0);
			if (ast_sockaddr_cmp_addr(&addr, &tmp_addr)) {
				char buf[64];
				ast_copy_string(buf, ast_sockaddr_stringify(&addr), sizeof(buf));
				ast_test_status_update(test, "Re-parsed stringification of '%s' did not match: '%s' vs '%s'\n", test_vals[x].address, buf, ast_sockaddr_stringify(&tmp_addr));
				res = AST_TEST_FAIL;
			}
		}
	}

	return res;
}

AST_TEST_DEFINE(split_hostport)
{
	int res = AST_TEST_PASS;
	char *host, *port, buf[128];

	switch (cmd) {
	case TEST_INIT:
		info->name = "split_hostport";
		info->category = "/main/netsock2/";
		info->summary = "netsock2 ast_sockaddr_split_hostport() unit test";
		info->description =
			"Test splitting of IPv4 and IPv6 host:port strings";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

/* Assumes res, host, and port variables */
#define TEST_SPLIT_HOSTPORT(str, flags, expected_host, expected_port, expected_result) { \
	int __res; \
	ast_copy_string(buf, str, sizeof(buf)); \
	if ((__res = ast_sockaddr_split_hostport(buf, &host, &port, flags)) != expected_result || ( \
			expected_result && ( \
				strcmp(host, expected_host) || ( \
					ast_strlen_zero(expected_port) ? !ast_strlen_zero(port) : (!ast_strlen_zero(port) && strcmp(port, expected_port)) \
				) \
			) \
		) \
	) { \
		res = AST_TEST_FAIL; \
		if (__res != expected_result) { \
			ast_test_status_update(test, "Expected %d, got %d\n", expected_result, __res); \
		} else { \
			ast_test_status_update(test, "Failed parsing '%s' into expected host '%s' (got '%s') and port '%s' (got '%s')\n", \
				str, S_OR(expected_host, ""), host, expected_port, S_OR(port, "")); \
		} \
	} \
}

	/* Test various situations with flags = 0 */
	TEST_SPLIT_HOSTPORT("192.168.1.1", 0, "192.168.1.1", "", 1);
	TEST_SPLIT_HOSTPORT("192.168.1.1:5060", 0, "192.168.1.1", "5060", 1);
	TEST_SPLIT_HOSTPORT("::ffff:5.6.7.8", 0, "::ffff:5.6.7.8", "", 1);
	TEST_SPLIT_HOSTPORT("[::ffff:5.6.7.8]:5060", 0, "::ffff:5.6.7.8", "5060", 1);
	TEST_SPLIT_HOSTPORT("fdf8:f53b:82e4::53", 0, "fdf8:f53b:82e4::53", "", 1);
	TEST_SPLIT_HOSTPORT("fe80::200:5aee:feaa:20a2", 0, "fe80::200:5aee:feaa:20a2", "", 1);
	TEST_SPLIT_HOSTPORT("[fdf8:f53b:82e4::53]", 0, "fdf8:f53b:82e4::53", "", 1);
	TEST_SPLIT_HOSTPORT("[fe80::200:5aee:feaa:20a2]:5060", 0, "fe80::200:5aee:feaa:20a2", "5060", 1);
	TEST_SPLIT_HOSTPORT("host:port", 0, "host", "port", 1);
	TEST_SPLIT_HOSTPORT("host", 0, "host", "", 1);

	/* Make sure that flag conditions work when they should */
	TEST_SPLIT_HOSTPORT("192.168.1.1:5060", PARSE_PORT_IGNORE, "192.168.1.1", "", 1);
	TEST_SPLIT_HOSTPORT("192.168.1.1:5060", PARSE_PORT_REQUIRE, "192.168.1.1", "5060", 1);
	TEST_SPLIT_HOSTPORT("192.168.1.1", PARSE_PORT_FORBID, "192.168.1.1", "", 1);
	TEST_SPLIT_HOSTPORT("[::ffff:5.6.7.8]:5060", PARSE_PORT_IGNORE, "::ffff:5.6.7.8", "", 1);
	TEST_SPLIT_HOSTPORT("[::ffff:5.6.7.8]:5060", PARSE_PORT_REQUIRE, "::ffff:5.6.7.8", "5060", 1);
	TEST_SPLIT_HOSTPORT("::ffff:5.6.7.8", PARSE_PORT_FORBID, "::ffff:5.6.7.8", "", 1);

	/* Make sure it fails when flag requirements are not met */
	TEST_SPLIT_HOSTPORT("192.168.1.1", PARSE_PORT_REQUIRE, "<undefined>", "<undefined>", 0);
	TEST_SPLIT_HOSTPORT("192.168.1.1:5060", PARSE_PORT_FORBID, "<undefined>", "<undefined>", 0);
	TEST_SPLIT_HOSTPORT("::ffff:5.6.7.8", PARSE_PORT_REQUIRE, "<undefined>", "<undefined>", 0);
	TEST_SPLIT_HOSTPORT("[::ffff:5.6.7.8]:5060", PARSE_PORT_FORBID, "<undefined>", "<undefined>", 0);

	return res;
}

/*!
 * \internal
 * \brief Open a UDP socket bound to \a address with an ephemeral port
 *
 * A wildcard IPv6 address gives a dual-stack socket, as the RTP stack uses.
 *
 * \return The socket, or -1 if the address cannot be bound on this host
 */
static int sendto_from_socket(const char *address, struct ast_sockaddr *bound)
{
	struct ast_sockaddr addr;
	int fd;

	if (!ast_sockaddr_parse(&addr, address, PARSE_PORT_FORBID)) {
		return -1;
	}
	fd = socket(ast_sockaddr_is_ipv6(&addr) ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return -1;
	}
#ifdef HAVE_SOCK_IPV6_V6ONLY
	if (ast_sockaddr_is_ipv6(&addr) && ast_sockaddr_is_any(&addr)) {
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &(int){0}, sizeof(int));
	}
#endif
	if (ast_bind(fd, &addr) || ast_getsockname(fd, bound)) {
		close(fd);
		return -1;
	}
	return fd;
}

AST_TEST_DEFINE(sendto_from)
{
	static const struct {
		/*! Address the sending socket is bound to */
		const char *bind;
		/*! Destination address (the port is the receiver's) */
		const char *dest;
		/*! Source address to send from, or NULL */
		const char *source;
		/*! Source address the receiver must see */
		const char *expected;
	} tests[] = {
		/* No source requested: the kernel picks 127.0.0.1 for 127.0.0.1 */
		{ "0.0.0.0", "127.0.0.1", NULL, "127.0.0.1" },
		{ "0.0.0.0", "127.0.0.1", "127.0.0.2", "127.0.0.2" },
		/* Wildcard, non-local and wrong-family sources are not applied */
		{ "0.0.0.0", "127.0.0.1", "0.0.0.0", "127.0.0.1" },
		{ "0.0.0.0", "127.0.0.1", "192.0.2.1", "127.0.0.1" },
		{ "0.0.0.0", "127.0.0.1", "::1", "127.0.0.1" },
		/* Dual-stack socket to IPv4 peers, plain and v4-mapped */
		{ "::", "127.0.0.1", NULL, "127.0.0.1" },
		{ "::", "127.0.0.1", "127.0.0.2", "127.0.0.2" },
		{ "::", "::ffff:127.0.0.1", "127.0.0.2", "127.0.0.2" },
		{ "::", "127.0.0.1", "::ffff:127.0.0.2", "127.0.0.2" },
		{ "::", "127.0.0.1", "192.0.2.1", "127.0.0.1" },
		/* Dual-stack socket to an IPv6 peer */
		{ "::", "::1", "::1", "::1" },
		{ "::", "::1", "127.0.0.2", "::1" },
		{ "::", "::1", "2001:db8::1", "::1" },
	};
	struct ast_sockaddr recv4_addr, recv6_addr, probe_addr;
	int recv4 = -1, recv6 = -1, probe, have_lo2;
	enum ast_test_result_state res = AST_TEST_PASS;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sendto_from";
		info->category = "/main/netsock2/";
		info->summary = "Test sending a datagram from a specific source address";
		info->description =
			"Sends from wildcard-bound sockets with and without a requested\n"
			"source address and checks the source the receiver sees.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* 127.0.0.2 is a second local address on Linux; skip its cases elsewhere */
	probe = sendto_from_socket("127.0.0.2", &probe_addr);
	have_lo2 = probe >= 0;
	if (have_lo2) {
		close(probe);
	}

	recv4 = sendto_from_socket("127.0.0.1", &recv4_addr);
	if (recv4 < 0) {
		ast_test_status_update(test, "Unable to bind receiver to 127.0.0.1\n");
		return AST_TEST_FAIL;
	}
	recv6 = sendto_from_socket("::1", &recv6_addr);

	for (i = 0; i < ARRAY_LEN(tests); i++) {
		struct ast_sockaddr bound, dest, address, expected, from;
		struct ast_sendto_source source;
		struct pollfd pfd = { .events = POLLIN };
		char buf[16], label[96];
		int sender, dest6;
		ssize_t sent;

		snprintf(label, sizeof(label), "%s -> %s from %s",
			tests[i].bind, tests[i].dest, S_OR(tests[i].source, "(none)"));
		ast_sockaddr_parse(&dest, tests[i].dest, PARSE_PORT_FORBID);
		ast_sockaddr_parse(&expected, tests[i].expected, PARSE_PORT_FORBID);
		/* A fresh source for each case, so an earlier rejection does not suspend it */
		memset(&source, 0, sizeof(source));
		if (tests[i].source) {
			ast_sockaddr_parse(&address, tests[i].source, PARSE_PORT_FORBID);
			ast_sendto_source_set(&source, &address);
		}
		/* Native IPv6 destinations go to the ::1 receiver, all others to 127.0.0.1 */
		dest6 = ast_sockaddr_is_ipv6(&dest) && !ast_sockaddr_is_ipv4_mapped(&dest);

		if ((dest6 && recv6 < 0)
			|| (strchr(tests[i].bind, ':') && !ast_check_ipv6())
			|| (!have_lo2 && tests[i].source && strstr(tests[i].source, "127.0.0.2"))) {
			ast_test_status_update(test, "Skipping %s: address not available\n", label);
			continue;
		}
		sender = sendto_from_socket(tests[i].bind, &bound);
		if (sender < 0) {
			ast_test_status_update(test, "%s: cannot bind %s\n", label, tests[i].bind);
			res = AST_TEST_FAIL;
			continue;
		}

		pfd.fd = recv4;
		ast_sockaddr_set_port(&dest, ast_sockaddr_port(&recv4_addr));
		if (dest6) {
			pfd.fd = recv6;
			ast_sockaddr_set_port(&dest, ast_sockaddr_port(&recv6_addr));
		}

		sent = ast_sendto_from_source(sender, "test", 4, 0, &dest, &source);
		if (sent != 4) {
			ast_test_status_update(test, "%s: send failed: %s\n", label, strerror(errno));
			res = AST_TEST_FAIL;
		} else if (ast_poll(&pfd, 1, 1000) != 1
			|| ast_recvfrom(pfd.fd, buf, sizeof(buf), 0, &from) != 4) {
			ast_test_status_update(test, "%s: nothing received\n", label);
			res = AST_TEST_FAIL;
		} else if (ast_sockaddr_cmp_addr(&from, &expected)
			|| ast_sockaddr_port(&from) != ast_sockaddr_port(&bound)) {
			ast_test_status_update(test, "%s: received from %s, expected %s port %d\n", label,
				ast_sockaddr_stringify(&from), tests[i].expected, ast_sockaddr_port(&bound));
			res = AST_TEST_FAIL;
		}
		close(sender);
	}

	close(recv4);
	if (recv6 >= 0) {
		close(recv6);
	}
	return res;
}

/*!
 * \internal
 * \brief Send from \a source and check the source the receiver sees and
 *        whether the source address ends up suspended
 *
 * \retval 0 as expected
 * \retval -1 not as expected
 */
static int sendto_from_source_check(struct ast_test *test, const char *step, int sender,
	int receiver, const struct ast_sockaddr *dest, struct ast_sendto_source *source,
	const char *expected, int suspended)
{
	struct pollfd pfd = { .fd = receiver, .events = POLLIN };
	struct ast_sockaddr expected_addr, from;
	char buf[16];

	ast_sockaddr_parse(&expected_addr, expected, PARSE_PORT_FORBID);

	if (ast_sendto_from_source(sender, "test", 4, 0, dest, source) != 4) {
		ast_test_status_update(test, "%s: send failed: %s\n", step, strerror(errno));
		return -1;
	}
	if (ast_poll(&pfd, 1, 1000) != 1 || ast_recvfrom(receiver, buf, sizeof(buf), 0, &from) != 4) {
		ast_test_status_update(test, "%s: nothing received\n", step);
		return -1;
	}
	if (ast_sockaddr_cmp_addr(&from, &expected_addr)) {
		ast_test_status_update(test, "%s: received from %s, expected %s\n",
			step, ast_sockaddr_stringify_addr(&from), expected);
		return -1;
	}
	if (ast_tvzero(source->retry) == suspended) {
		ast_test_status_update(test, "%s: the source address is %s, expected %s\n", step,
			suspended ? "in use" : "suspended", suspended ? "suspended" : "in use");
		return -1;
	}
	return 0;
}

AST_TEST_DEFINE(sendto_from_source)
{
	struct ast_sendto_source source;
	struct ast_sockaddr recv_addr, recv6_addr, bound, rejected, usable;
	int receiver, sender = -1, receiver6 = -1, sender6 = -1;
	enum ast_test_result_state res = AST_TEST_FAIL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sendto_from_source";
		info->category = "/main/netsock2/";
		info->summary = "Test that a rejected source address is suspended and tried again";
		info->description =
			"Sends from a source address the kernel rejects and checks that\n"
			"datagrams are sent without it until it is tried again, that it\n"
			"is used again once it is accepted, that a datagram it cannot be\n"
			"used for does not end the suspension, and that setting the clock\n"
			"back does not prolong it.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

#if !defined(IP_PKTINFO) && !defined(IP_SENDSRCADDR)
	ast_test_status_update(test, "Skipping: IPv4 source addresses cannot be set on this platform\n");
	return AST_TEST_NOT_RUN;
#endif

	/* 127.0.0.2 is a second local address on Linux */
	receiver = sendto_from_socket("127.0.0.2", &bound);
	if (receiver < 0) {
		ast_test_status_update(test, "Skipping: 127.0.0.2 is not a local address\n");
		return AST_TEST_NOT_RUN;
	}
	close(receiver);

	receiver = sendto_from_socket("127.0.0.1", &recv_addr);
	if (receiver < 0) {
		ast_test_status_update(test, "Unable to bind receiver to 127.0.0.1\n");
		return AST_TEST_FAIL;
	}
	sender = sendto_from_socket("0.0.0.0", &bound);
	if (sender < 0) {
		ast_test_status_update(test, "Unable to bind sender to 0.0.0.0\n");
		goto cleanup;
	}
	ast_sockaddr_parse(&rejected, "192.0.2.1", PARSE_PORT_FORBID);
	ast_sockaddr_parse(&usable, "127.0.0.2", PARSE_PORT_FORBID);
	memset(&source, 0, sizeof(source));

	ast_sendto_source_set(&source, &rejected);
	if (sendto_from_source_check(test, "Rejected address", sender, receiver, &recv_addr,
			&source, "127.0.0.1", 1)) {
		goto cleanup;
	}

	ast_sendto_source_set(&source, &rejected);
	if (ast_tvzero(source.retry)) {
		ast_test_status_update(test, "Setting the same address ended its suspension\n");
		goto cleanup;
	}

	/*
	 * The address becomes usable, but is not tried until it is due (set here
	 * as the rejection did, so that a slow run does not make it due already)
	 */
	ast_sockaddr_copy(&source.address, &usable);
	source.retry = ast_tvadd(ast_tvnow(), ast_tv(1, 0));
	if (sendto_from_source_check(test, "Suspended address", sender, receiver, &recv_addr,
			&source, "127.0.0.1", 1)) {
		goto cleanup;
	}

	source.retry = ast_tvsub(ast_tvnow(), ast_tv(1, 0));
	if (sendto_from_source_check(test, "Address tried again", sender, receiver, &recv_addr,
			&source, "127.0.0.2", 0)) {
		goto cleanup;
	}

	ast_sendto_source_set(&source, &rejected);
	if (sendto_from_source_check(test, "Address rejected again", sender, receiver, &recv_addr,
			&source, "127.0.0.1", 1)) {
		goto cleanup;
	}

	/* A datagram the address cannot be used for does not end the suspension */
	source.retry = ast_tvsub(ast_tvnow(), ast_tv(1, 0));
	sender6 = ast_check_ipv6() ? sendto_from_socket("::", &bound) : -1;
	receiver6 = sender6 >= 0 ? sendto_from_socket("::1", &recv6_addr) : -1;
	if (receiver6 < 0) {
		ast_test_status_update(test, "Skipping the IPv6 destination: ::1 is not available\n");
	} else if (sendto_from_source_check(test, "IPv6 destination", sender6, receiver6, &recv6_addr,
			&source, "::1", 1)) {
		goto cleanup;
	}

	/* The clock was set back, so the retry time is further away than it can be */
	ast_sockaddr_copy(&source.address, &usable);
	source.retry = ast_tvadd(ast_tvnow(), ast_tv(3600, 0));
	if (sendto_from_source_check(test, "Clock set back", sender, receiver, &recv_addr,
			&source, "127.0.0.2", 0)) {
		goto cleanup;
	}

	ast_sendto_source_set(&source, &usable);
	if (sendto_from_source_check(test, "Different address", sender, receiver, &recv_addr,
			&source, "127.0.0.2", 0)) {
		goto cleanup;
	}

	ast_sendto_source_set(&source, NULL);
	if (sendto_from_source_check(test, "No address", sender, receiver, &recv_addr,
			&source, "127.0.0.1", 0)) {
		goto cleanup;
	}

	res = AST_TEST_PASS;

cleanup:
	if (sender >= 0) {
		close(sender);
	}
	if (sender6 >= 0) {
		close(sender6);
	}
	if (receiver6 >= 0) {
		close(receiver6);
	}
	close(receiver);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(parsing);
	AST_TEST_UNREGISTER(split_hostport);
	AST_TEST_UNREGISTER(sendto_from);
	AST_TEST_UNREGISTER(sendto_from_source);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(parsing);
	AST_TEST_REGISTER(split_hostport);
	AST_TEST_REGISTER(sendto_from);
	AST_TEST_REGISTER(sendto_from_source);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Netsock2 test module");
