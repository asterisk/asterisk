/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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
 * \brief Test security event generation
 *
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/security_events.h"
#include "asterisk/netsock2.h"

static void evt_gen_failed_acl(void);
static void evt_gen_inval_acct_id(void);
static void evt_gen_session_limit(void);
static void evt_gen_mem_limit(void);
static void evt_gen_load_avg(void);
static void evt_gen_req_no_support(void);
static void evt_gen_req_not_allowed(void);
static void evt_gen_auth_method_not_allowed(void);
static void evt_gen_req_bad_format(void);
static void evt_gen_successful_auth(void);
static void evt_gen_unexpected_addr(void);
static void evt_gen_chal_resp_failed(void);
static void evt_gen_inval_password(void);
static void evt_gen_chal_sent(void);
static void evt_gen_inval_transport(void);

typedef void (*evt_generator)(void);
static const evt_generator evt_generators[AST_SECURITY_EVENT_NUM_TYPES] = {
	[AST_SECURITY_EVENT_FAILED_ACL]              = evt_gen_failed_acl,
	[AST_SECURITY_EVENT_INVAL_ACCT_ID]           = evt_gen_inval_acct_id,
	[AST_SECURITY_EVENT_SESSION_LIMIT]           = evt_gen_session_limit,
	[AST_SECURITY_EVENT_MEM_LIMIT]               = evt_gen_mem_limit,
	[AST_SECURITY_EVENT_LOAD_AVG]                = evt_gen_load_avg,
	[AST_SECURITY_EVENT_REQ_NO_SUPPORT]          = evt_gen_req_no_support,
	[AST_SECURITY_EVENT_REQ_NOT_ALLOWED]         = evt_gen_req_not_allowed,
	[AST_SECURITY_EVENT_AUTH_METHOD_NOT_ALLOWED] = evt_gen_auth_method_not_allowed,
	[AST_SECURITY_EVENT_REQ_BAD_FORMAT]          = evt_gen_req_bad_format,
	[AST_SECURITY_EVENT_SUCCESSFUL_AUTH]         = evt_gen_successful_auth,
	[AST_SECURITY_EVENT_UNEXPECTED_ADDR]         = evt_gen_unexpected_addr,
	[AST_SECURITY_EVENT_CHAL_RESP_FAILED]        = evt_gen_chal_resp_failed,
	[AST_SECURITY_EVENT_INVAL_PASSWORD]          = evt_gen_inval_password,
	[AST_SECURITY_EVENT_CHAL_SENT]               = evt_gen_chal_sent,
	[AST_SECURITY_EVENT_INVAL_TRANSPORT]         = evt_gen_inval_transport,
};

static void evt_gen_failed_acl(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_failed_acl failed_acl_event = {
		.common.event_type = AST_SECURITY_EVENT_FAILED_ACL,
		.common.version    = AST_SECURITY_EVENT_FAILED_ACL_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "Username",
		.common.session_id = "Session123",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_UDP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_UDP,
		},

		.acl_name   = "TEST_ACL",
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "192.168.1.1:12121", sizeof(localaddr));
	ast_copy_string(remoteaddr, "192.168.1.2:12345", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&failed_acl_event));
}

static void evt_gen_inval_acct_id(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_inval_acct_id inval_acct_id = {
		.common.event_type = AST_SECURITY_EVENT_INVAL_ACCT_ID,
		.common.version    = AST_SECURITY_EVENT_INVAL_ACCT_ID_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "FakeUser",
		.common.session_id = "Session456",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_TCP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_TCP,
		},
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.1.2.3:4321", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.1.2.4:123", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&inval_acct_id));
}

static void evt_gen_session_limit(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_session_limit session_limit = {
		.common.event_type = AST_SECURITY_EVENT_SESSION_LIMIT,
		.common.version    = AST_SECURITY_EVENT_SESSION_LIMIT_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "Jenny",
		.common.session_id = "8675309",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_TLS,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_TLS,
		},
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.5.4.3:4444", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.5.4.2:3333", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&session_limit));
}

static void evt_gen_mem_limit(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_mem_limit mem_limit = {
		.common.event_type = AST_SECURITY_EVENT_MEM_LIMIT,
		.common.version    = AST_SECURITY_EVENT_MEM_LIMIT_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "Felix",
		.common.session_id = "Session2604",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_UDP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_UDP,
		},
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.10.10.10:555", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.10.10.12:5656", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&mem_limit));
}

static void evt_gen_load_avg(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_load_avg load_avg = {
		.common.event_type = AST_SECURITY_EVENT_LOAD_AVG,
		.common.version    = AST_SECURITY_EVENT_LOAD_AVG_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "GuestAccount",
		.common.session_id = "XYZ123",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_UDP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_UDP,
		},
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.11.12.13:9876", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.12.11.10:9825", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&load_avg));
}

static void evt_gen_req_no_support(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_req_no_support req_no_support = {
		.common.event_type = AST_SECURITY_EVENT_REQ_NO_SUPPORT,
		.common.version    = AST_SECURITY_EVENT_REQ_NO_SUPPORT_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "George",
		.common.session_id = "asdkl23478289lasdkf",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_UDP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_UDP,
		},

		.request_type = "MakeMeDinner",
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.110.120.130:9888", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.120.110.100:9777", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&req_no_support));
}

static void evt_gen_req_not_allowed(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_req_not_allowed req_not_allowed = {
		.common.event_type = AST_SECURITY_EVENT_REQ_NOT_ALLOWED,
		.common.version    = AST_SECURITY_EVENT_REQ_NOT_ALLOWED_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "George",
		.common.session_id = "alksdjf023423h4lka0df",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_UDP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_UDP,
		},

		.request_type = "MakeMeBreakfast",
		.request_params = "BACONNNN!",
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.110.120.130:9888", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.120.110.100:9777", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&req_not_allowed));
}

static void evt_gen_auth_method_not_allowed(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_auth_method_not_allowed auth_method_not_allowed = {
		.common.event_type = AST_SECURITY_EVENT_AUTH_METHOD_NOT_ALLOWED,
		.common.version    = AST_SECURITY_EVENT_AUTH_METHOD_NOT_ALLOWED_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "Bob",
		.common.session_id = "010101010101",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_TCP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_TCP,
		},

		.auth_method = "PlainText"
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.110.120.135:8754", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.120.110.105:8745", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&auth_method_not_allowed));
}

static void evt_gen_req_bad_format(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_req_bad_format req_bad_format = {
		.common.event_type = AST_SECURITY_EVENT_REQ_BAD_FORMAT,
		.common.version    = AST_SECURITY_EVENT_REQ_BAD_FORMAT_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "Larry",
		.common.session_id = "838383fhfhf83hf8h3f8h",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_TCP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_TCP,
		},

		.request_type = "CheeseBurger",
		.request_params = "Onions,Swiss,MotorOil",
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.110.220.230:1212", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.120.210.200:2121", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&req_bad_format));
}

static void evt_gen_successful_auth(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_successful_auth successful_auth = {
		.common.event_type = AST_SECURITY_EVENT_SUCCESSFUL_AUTH,
		.common.version    = AST_SECURITY_EVENT_SUCCESSFUL_AUTH_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "ValidUser",
		.common.session_id = "Session456",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_TCP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_TCP,
		},
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.1.2.3:4321", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.1.2.4:1234", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&successful_auth));
}

static void evt_gen_unexpected_addr(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };
	struct ast_sockaddr addr_expected = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_unexpected_addr unexpected_addr = {
		.common.event_type = AST_SECURITY_EVENT_UNEXPECTED_ADDR,
		.common.version    = AST_SECURITY_EVENT_UNEXPECTED_ADDR_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "CoolUser",
		.common.session_id = "Session789",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_UDP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_UDP,
		},

		.expected_addr = {
			.addr = &addr_expected,
			.transport  = AST_TRANSPORT_UDP,
		},
	};

	char localaddr[53];
	char remoteaddr[53];
	char expectedaddr[53];

	ast_copy_string(localaddr, "10.1.2.3:4321", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.1.2.4:1234", sizeof(remoteaddr));
	ast_copy_string(expectedaddr, "10.1.2.5:2343", sizeof(expectedaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);
	ast_sockaddr_parse(&addr_expected, expectedaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&unexpected_addr));
}

static void evt_gen_chal_resp_failed(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_chal_resp_failed chal_resp_failed = {
		.common.event_type = AST_SECURITY_EVENT_CHAL_RESP_FAILED,
		.common.version    = AST_SECURITY_EVENT_CHAL_RESP_FAILED_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "SuperDuperUser",
		.common.session_id = "Session1231231231",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_TCP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_TCP,
		},

		.challenge         = "8adf8a9sd8fas9df23ljk4",
		.response          = "9u3jlaksdjflakjsdfoi23",
		.expected_response = "oiafaljhadf9834luahk3k",
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.1.2.3:4321", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.1.2.4:1234", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&chal_resp_failed));
}

static void evt_gen_inval_password(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_inval_password inval_password = {
		.common.event_type = AST_SECURITY_EVENT_INVAL_PASSWORD,
		.common.version    = AST_SECURITY_EVENT_INVAL_PASSWORD_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "AccountIDGoesHere",
		.common.session_id = "SessionIDGoesHere",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_TCP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_TCP,
		},
		.challenge          = "GoOdChAlLeNgE",
		.received_challenge = "BaDcHaLlEnGe",
		.received_hash      = "3ad9023adf309",
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.200.100.30:4321", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.200.100.40:1234", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&inval_password));
}

static void evt_gen_chal_sent(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_chal_sent chal_sent = {
		.common.event_type = AST_SECURITY_EVENT_CHAL_SENT,
		.common.version    = AST_SECURITY_EVENT_CHAL_SENT_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "AccountIDGoesHere",
		.common.session_id = "SessionIDGoesHere",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_TCP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_TCP,
		},
		.challenge         = "IcHaLlEnGeYoU",
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.200.10.30:5392", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.200.10.31:1443", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&chal_sent));
}

static void evt_gen_inval_transport(void)
{
	struct ast_sockaddr addr_local = { {0,} };
	struct ast_sockaddr addr_remote = { {0,} };

	struct timeval session_tv = ast_tvnow();
	struct ast_security_event_inval_transport inval_transport = {
		.common.event_type = AST_SECURITY_EVENT_INVAL_TRANSPORT,
		.common.version    = AST_SECURITY_EVENT_INVAL_TRANSPORT_VERSION,
		.common.service    = "TEST",
		.common.module     = AST_MODULE,
		.common.account_id = "AccountIDGoesHere",
		.common.session_id = "SessionIDGoesHere",
		.common.session_tv = &session_tv,
		.common.local_addr = {
			.addr  = &addr_local,
			.transport  = AST_TRANSPORT_TCP,
		},
		.common.remote_addr = {
			.addr = &addr_remote,
			.transport  = AST_TRANSPORT_TCP,
		},
		.transport          = "UDP",
	};

	char localaddr[53];
	char remoteaddr[53];

	ast_copy_string(localaddr, "10.200.103.45:8223", sizeof(localaddr));
	ast_copy_string(remoteaddr, "10.200.103.44:1039", sizeof(remoteaddr));

	ast_sockaddr_parse(&addr_local, localaddr, 0);
	ast_sockaddr_parse(&addr_remote, remoteaddr, 0);

	ast_security_event_report(AST_SEC_EVT(&inval_transport));
}

static void gen_events(struct ast_cli_args *a)
{
	unsigned int i;

	ast_cli(a->fd, "Generating some security events ...\n");

	for (i = 0; i < ARRAY_LEN(evt_generators); i++) {
		const char *event_type = ast_security_event_get_name(i);

		if (!evt_generators[i]) {
			ast_cli(a->fd, "*** No event generator for event type '%s' ***\n",
					event_type);
			continue;
		}

		ast_cli(a->fd, "Generating a '%s' security event ...\n", event_type);

		evt_generators[i]();
	}

	ast_cli(a->fd, "Security event generation complete.\n");
}

static char *handle_cli_sec_evt_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "securityevents test generation";
		e->usage = ""
			"Usage: securityevents test generation"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	case CLI_HANDLER:
		gen_events(a);
		return CLI_SUCCESS;
	}

	return CLI_FAILURE;
}

static struct ast_cli_entry cli_sec_evt[] = {
	AST_CLI_DEFINE(handle_cli_sec_evt_test, "Test security event generation"),
};

static int unload_module(void)
{
	return ast_cli_unregister_multiple(cli_sec_evt, ARRAY_LEN(cli_sec_evt));
}

static int load_module(void)
{
	int res;

	res = ast_cli_register_multiple(cli_sec_evt, ARRAY_LEN(cli_sec_evt));

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Test Security Event Generation");
