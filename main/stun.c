/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2008, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 *
 * \brief STUN Support
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \note STUN is defined in RFC 3489.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/stun.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/channel.h"

static int stundebug;			/*!< Are we debugging stun? */

/*!
 * \brief STUN support code
 *
 * This code provides some support for doing STUN transactions.
 * Eventually it should be moved elsewhere as other protocols
 * than RTP can benefit from it - e.g. SIP.
 * STUN is described in RFC3489 and it is based on the exchange
 * of UDP packets between a client and one or more servers to
 * determine the externally visible address (and port) of the client
 * once it has gone through the NAT boxes that connect it to the
 * outside.
 * The simplest request packet is just the header defined in
 * struct stun_header, and from the response we may just look at
 * one attribute, STUN_MAPPED_ADDRESS, that we find in the response.
 * By doing more transactions with different server addresses we
 * may determine more about the behaviour of the NAT boxes, of
 * course - the details are in the RFC.
 *
 * All STUN packets start with a simple header made of a type,
 * length (excluding the header) and a 16-byte random transaction id.
 * Following the header we may have zero or more attributes, each
 * structured as a type, length and a value (whose format depends
 * on the type, but often contains addresses).
 * Of course all fields are in network format.
 */

typedef struct { unsigned int id[4]; } __attribute__((packed)) stun_trans_id;

struct stun_header {
	unsigned short msgtype;
	unsigned short msglen;
	stun_trans_id  id;
	unsigned char ies[0];
} __attribute__((packed));

struct stun_attr {
	unsigned short attr;
	unsigned short len;
	unsigned char value[0];
} __attribute__((packed));

/*
 * The format normally used for addresses carried by STUN messages.
 */
struct stun_addr {
	unsigned char unused;
	unsigned char family;
	unsigned short port;
	unsigned int addr;
} __attribute__((packed));

/*! \brief STUN message types
 * 'BIND' refers to transactions used to determine the externally
 * visible addresses. 'SEC' refers to transactions used to establish
 * a session key for subsequent requests.
 * 'SEC' functionality is not supported here.
 */

#define STUN_BINDREQ	0x0001
#define STUN_BINDRESP	0x0101
#define STUN_BINDERR	0x0111
#define STUN_SECREQ	0x0002
#define STUN_SECRESP	0x0102
#define STUN_SECERR	0x0112

/*! \brief Basic attribute types in stun messages.
 * Messages can also contain custom attributes (codes above 0x7fff)
 */
#define STUN_MAPPED_ADDRESS	0x0001
#define STUN_RESPONSE_ADDRESS	0x0002
#define STUN_CHANGE_REQUEST	0x0003
#define STUN_SOURCE_ADDRESS	0x0004
#define STUN_CHANGED_ADDRESS	0x0005
#define STUN_USERNAME		0x0006
#define STUN_PASSWORD		0x0007
#define STUN_MESSAGE_INTEGRITY	0x0008
#define STUN_ERROR_CODE		0x0009
#define STUN_UNKNOWN_ATTRIBUTES	0x000a
#define STUN_REFLECTED_FROM	0x000b

/*! \brief helper function to print message names */
static const char *stun_msg2str(int msg)
{
	switch (msg) {
	case STUN_BINDREQ:
		return "Binding Request";
	case STUN_BINDRESP:
		return "Binding Response";
	case STUN_BINDERR:
		return "Binding Error Response";
	case STUN_SECREQ:
		return "Shared Secret Request";
	case STUN_SECRESP:
		return "Shared Secret Response";
	case STUN_SECERR:
		return "Shared Secret Error Response";
	}
	return "Non-RFC3489 Message";
}

/*! \brief helper function to print attribute names */
static const char *stun_attr2str(int msg)
{
	switch (msg) {
	case STUN_MAPPED_ADDRESS:
		return "Mapped Address";
	case STUN_RESPONSE_ADDRESS:
		return "Response Address";
	case STUN_CHANGE_REQUEST:
		return "Change Request";
	case STUN_SOURCE_ADDRESS:
		return "Source Address";
	case STUN_CHANGED_ADDRESS:
		return "Changed Address";
	case STUN_USERNAME:
		return "Username";
	case STUN_PASSWORD:
		return "Password";
	case STUN_MESSAGE_INTEGRITY:
		return "Message Integrity";
	case STUN_ERROR_CODE:
		return "Error Code";
	case STUN_UNKNOWN_ATTRIBUTES:
		return "Unknown Attributes";
	case STUN_REFLECTED_FROM:
		return "Reflected From";
	}
	return "Non-RFC3489 Attribute";
}

/*! \brief here we store credentials extracted from a message */
struct stun_state {
	const char *username;
	const char *password;
};

static int stun_process_attr(struct stun_state *state, struct stun_attr *attr)
{
	if (stundebug)
		ast_verbose("Found STUN Attribute %s (%04x), length %d\n",
			    stun_attr2str(ntohs(attr->attr)), (unsigned)ntohs(attr->attr), ntohs(attr->len));
	switch (ntohs(attr->attr)) {
	case STUN_USERNAME:
		state->username = (const char *) (attr->value);
		break;
	case STUN_PASSWORD:
		state->password = (const char *) (attr->value);
		break;
	default:
		if (stundebug)
			ast_verbose("Ignoring STUN attribute %s (%04x), length %d\n",
				    stun_attr2str(ntohs(attr->attr)), (unsigned)ntohs(attr->attr), ntohs(attr->len));
	}
	return 0;
}

/*! \brief append a string to an STUN message */
static void append_attr_string(struct stun_attr **attr, int attrval, const char *s, int *len, int *left)
{
	int str_length = strlen(s);
	int attr_length = str_length + ((~(str_length - 1)) & 0x3);
	int size = sizeof(**attr) + attr_length;
	if (*left > size) {
		(*attr)->attr = htons(attrval);
		(*attr)->len = htons(attr_length);
		memcpy((*attr)->value, s, str_length);
		memset((*attr)->value + str_length, 0, attr_length - str_length);
		(*attr) = (struct stun_attr *)((*attr)->value + attr_length);
		*len += size;
		*left -= size;
	}
}

/*! \brief append an address to an STUN message */
static void append_attr_address(struct stun_attr **attr, int attrval, struct sockaddr_in *sin, int *len, int *left)
{
	int size = sizeof(**attr) + 8;
	struct stun_addr *addr;
	if (*left > size) {
		(*attr)->attr = htons(attrval);
		(*attr)->len = htons(8);
		addr = (struct stun_addr *)((*attr)->value);
		addr->unused = 0;
		addr->family = 0x01;
		addr->port = sin->sin_port;
		addr->addr = sin->sin_addr.s_addr;
		(*attr) = (struct stun_attr *)((*attr)->value + 8);
		*len += size;
		*left -= size;
	}
}

/*! \brief wrapper to send an STUN message */
static int stun_send(int s, struct sockaddr_in *dst, struct stun_header *resp)
{
	return sendto(s, resp, ntohs(resp->msglen) + sizeof(*resp), 0,
		      (struct sockaddr *)dst, sizeof(*dst));
}

/*!
 * \internal
 * \brief Compare the STUN tranaction IDs.
 *
 * \param left Transaction ID.
 * \param right Transaction ID.
 *
 * \retval 0 if match.
 * \retval non-zero if not match.
 */
static int stun_id_cmp(stun_trans_id *left, stun_trans_id *right)
{
	return memcmp(left, right, sizeof(*left));
}

/*! \brief helper function to generate a random request id */
static void stun_req_id(struct stun_header *req)
{
	int x;
	for (x = 0; x < 4; x++)
		req->id.id[x] = ast_random();
}

int ast_stun_handle_packet(int s, struct sockaddr_in *src, unsigned char *data, size_t len, stun_cb_f *stun_cb, void *arg)
{
	struct stun_header *hdr = (struct stun_header *)data;
	struct stun_attr *attr;
	struct stun_state st;
	int ret = AST_STUN_IGNORE;
	int x;

	/* On entry, 'len' is the length of the udp payload. After the
	 * initial checks it becomes the size of unprocessed options,
	 * while 'data' is advanced accordingly.
	 */
	if (len < sizeof(struct stun_header)) {
		ast_debug(1, "Runt STUN packet (only %d, wanting at least %d)\n", (int) len, (int) sizeof(struct stun_header));
		return -1;
	}
	len -= sizeof(struct stun_header);
	data += sizeof(struct stun_header);
	x = ntohs(hdr->msglen);	/* len as advertised in the message */
	if (stundebug)
		ast_verbose("STUN Packet, msg %s (%04x), length: %d\n", stun_msg2str(ntohs(hdr->msgtype)), (unsigned)ntohs(hdr->msgtype), x);
	if (x > len) {
		ast_debug(1, "Scrambled STUN packet length (got %d, expecting %d)\n", x, (int)len);
	} else
		len = x;
	memset(&st, 0, sizeof(st));
	while (len) {
		if (len < sizeof(struct stun_attr)) {
			ast_debug(1, "Runt Attribute (got %d, expecting %d)\n", (int)len, (int) sizeof(struct stun_attr));
			break;
		}
		attr = (struct stun_attr *)data;
		/* compute total attribute length */
		x = ntohs(attr->len) + sizeof(struct stun_attr);
		if (x > len) {
			ast_debug(1, "Inconsistent Attribute (length %d exceeds remaining msg len %d)\n", x, (int)len);
			break;
		}
		if (stun_cb)
			stun_cb(attr, arg);
		if (stun_process_attr(&st, attr)) {
			ast_debug(1, "Failed to handle attribute %s (%04x)\n", stun_attr2str(ntohs(attr->attr)), (unsigned)ntohs(attr->attr));
			break;
		}
		/* Clear attribute id: in case previous entry was a string,
		 * this will act as the terminator for the string.
		 */
		attr->attr = 0;
		data += x;
		len -= x;
	}
	/* Null terminate any string.
	 * XXX NOTE, we write past the size of the buffer passed by the
	 * caller, so this is potentially dangerous. The only thing that
	 * saves us is that usually we read the incoming message in a
	 * much larger buffer in the struct ast_rtp
	 */
	*data = '\0';

	/* Now prepare to generate a reply, which at the moment is done
	 * only for properly formed (len == 0) STUN_BINDREQ messages.
	 */
	if (len == 0) {
		unsigned char respdata[1024];
		struct stun_header *resp = (struct stun_header *)respdata;
		int resplen = 0;	/* len excluding header */
		int respleft = sizeof(respdata) - sizeof(struct stun_header);
		char combined[33];

		resp->id = hdr->id;
		resp->msgtype = 0;
		resp->msglen = 0;
		attr = (struct stun_attr *)resp->ies;
		switch (ntohs(hdr->msgtype)) {
		case STUN_BINDREQ:
			if (stundebug)
				ast_verbose("STUN Bind Request, username: %s\n",
					    st.username ? st.username : "<none>");
			if (st.username) {
				append_attr_string(&attr, STUN_USERNAME, st.username, &resplen, &respleft);
				snprintf(combined, sizeof(combined), "%16s%16s", st.username + 16, st.username);
			}

			append_attr_address(&attr, STUN_MAPPED_ADDRESS, src, &resplen, &respleft);
			resp->msglen = htons(resplen);
			resp->msgtype = htons(STUN_BINDRESP);
			stun_send(s, src, resp);
			ast_stun_request(s, src, combined, NULL);
			ret = AST_STUN_ACCEPT;
			break;
		default:
			if (stundebug)
				ast_verbose("Dunno what to do with STUN message %04x (%s)\n", (unsigned)ntohs(hdr->msgtype), stun_msg2str(ntohs(hdr->msgtype)));
		}
	}
	return ret;
}

/*! \brief Extract the STUN_MAPPED_ADDRESS from the stun response.
 * This is used as a callback for stun_handle_response
 * when called from ast_stun_request.
 */
static int stun_get_mapped(struct stun_attr *attr, void *arg)
{
	struct stun_addr *addr = (struct stun_addr *)(attr + 1);
	struct sockaddr_in *sa = (struct sockaddr_in *)arg;

	if (ntohs(attr->attr) != STUN_MAPPED_ADDRESS || ntohs(attr->len) != 8)
		return 1;	/* not us. */
	sa->sin_port = addr->port;
	sa->sin_addr.s_addr = addr->addr;
	return 0;
}

int ast_stun_request(int s, struct sockaddr_in *dst,
	const char *username, struct sockaddr_in *answer)
{
	struct stun_header *req;
	struct stun_header *rsp;
	unsigned char req_buf[1024];
	unsigned char rsp_buf[1024];
	int reqlen, reqleft;
	struct stun_attr *attr;
	int res = -1;
	int retry;

	if (answer) {
		/* Always clear answer in case the request fails. */
		memset(answer, 0, sizeof(struct sockaddr_in));
	}

	/* Create STUN bind request */
	req = (struct stun_header *) req_buf;
	stun_req_id(req);
	reqlen = 0;
	reqleft = sizeof(req_buf) - sizeof(struct stun_header);
	req->msgtype = 0;
	req->msglen = 0;
	attr = (struct stun_attr *) req->ies;
	if (username) {
		append_attr_string(&attr, STUN_USERNAME, username, &reqlen, &reqleft);
	}
	req->msglen = htons(reqlen);
	req->msgtype = htons(STUN_BINDREQ);

	for (retry = 0; retry++ < 3;) {	/* XXX make retries configurable */
		/* send request, possibly wait for reply */
		struct sockaddr_in src;
		socklen_t srclen;

		/* Send STUN message. */
		res = stun_send(s, dst, req);
		if (res < 0) {
			ast_debug(1, "stun_send try %d failed: %s\n", retry, strerror(errno));
			break;
		}
		if (!answer) {
			/* Successful send since we don't care about any response. */
			res = 0;
			break;
		}

try_again:
		/* Wait for response. */
		{
			struct pollfd pfds = { .fd = s, .events = POLLIN };

			res = ast_poll(&pfds, 1, 3000);
			if (res < 0) {
				/* Error */
				continue;
			}
			if (!res) {
				/* No response, timeout */
				res = 1;
				continue;
			}
		}

		/* Read STUN response. */
		memset(&src, 0, sizeof(src));
		srclen = sizeof(src);
		/* XXX pass sizeof(rsp_buf) - 1 in the size, because stun_handle_packet might
		 * write past the end of the buffer.
		 */
		res = recvfrom(s, rsp_buf, sizeof(rsp_buf) - 1,
			0, (struct sockaddr *) &src, &srclen);
		if (res < 0) {
			ast_debug(1, "recvfrom try %d failed: %s\n", retry, strerror(errno));
			break;
		}

		/* Process the STUN response. */
		rsp = (struct stun_header *) rsp_buf;
		if (ast_stun_handle_packet(s, &src, rsp_buf, res, stun_get_mapped, answer)
			|| (rsp->msgtype != htons(STUN_BINDRESP)
				&& rsp->msgtype != htons(STUN_BINDERR))
			|| stun_id_cmp(&req->id, &rsp->id)) {
			/* Bad STUN packet, not right type, or transaction ID did not match. */
			memset(answer, 0, sizeof(struct sockaddr_in));

			/* Was not a resonse to our request. */
			goto try_again;
		}
		/* Success.  answer contains the external address if available. */
		res = 0;
		break;
	}
	return res;
}

static char *handle_cli_stun_set_debug(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "stun set debug {on|off}";
		e->usage =
			"Usage: stun set debug {on|off}\n"
			"       Enable/Disable STUN (Simple Traversal of UDP through NATs)\n"
			"       debugging\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (!strncasecmp(a->argv[e->args-1], "on", 2))
		stundebug = 1;
	else if (!strncasecmp(a->argv[e->args-1], "off", 3))
		stundebug = 0;
	else
		return CLI_SHOWUSAGE;

	ast_cli(a->fd, "STUN Debugging %s\n", stundebug ? "Enabled" : "Disabled");
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_stun[] = {
	AST_CLI_DEFINE(handle_cli_stun_set_debug, "Enable/Disable STUN debugging"),
};

static void stun_shutdown(void)
{
	ast_cli_unregister_multiple(cli_stun, sizeof(cli_stun) / sizeof(struct ast_cli_entry));
}

/*! \brief Initialize the STUN system in Asterisk */
void ast_stun_init(void)
{
	ast_cli_register_multiple(cli_stun, sizeof(cli_stun) / sizeof(struct ast_cli_entry));
	ast_register_cleanup(stun_shutdown);
}
