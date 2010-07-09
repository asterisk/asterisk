/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
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
 * \brief sip request parsing functions and unit tests
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "include/sip.h"
#include "include/sip_utils.h"
#include "include/reqresp_parser.h"

/*! \brief * parses a URI in its components.*/
int parse_uri_full(char *uri, const char *scheme, char **user, char **pass,
		   char **domain, struct uriparams *params, char **headers,
		   char **residue)
{
	char *userinfo = NULL;
	char *parameters = NULL;
	char *endparams = NULL;
	char *c = NULL;
	int error = 0;

	/* check for valid input */
	if (ast_strlen_zero(uri)) {
		return -1;
	}

	if (scheme) {
		int l;
		char *scheme2 = ast_strdupa(scheme);
		char *cur = strsep(&scheme2, ",");
		for (; !ast_strlen_zero(cur); cur = strsep(&scheme2, ",")) {
			l = strlen(cur);
			if (!strncasecmp(uri, cur, l)) {
				uri += l;
				break;
			}
		}
		if (ast_strlen_zero(cur)) {
			ast_debug(1, "No supported scheme found in '%s' using the scheme[s] %s\n", uri, scheme);
			error = -1;
		}
	}

	if (!domain) {
		/* if we don't want to split around domain, keep everything as a
		 * userinfo - cos thats how old parse_uri operated*/
		userinfo = uri;
	} else {
		char *dom = "";
		if ((c = strchr(uri, '@'))) {
			*c++ = '\0';
			dom = c;
			userinfo = uri;
			uri = c; /* userinfo can contain ? and ; chars so step forward before looking for params and headers */
		} else {
			/* domain-only URI, according to the SIP RFC. */
			dom = uri;
			userinfo = "";
			}

		*domain = dom;
	}

	if (pass && (c = strchr(userinfo, ':'))) {	  /* user:password */
		*c++ = '\0';
		*pass = c;
	} else if (pass) {
		*pass = "";
	}

	if (user) {
		*user = userinfo;
	}

	parameters = uri;
	/* strip [?headers] from end of uri  - even if no header pointer exists*/
	if ((c = strrchr(uri, '?'))) {
			*c++ = '\0';
		uri = c;
		if (headers) {
			*headers = c;
		}
		if ((c = strrchr(uri, ';'))) {
			*c++ = '\0';
		} else {
			c = strrchr(uri, '\0');
		}
		uri = c; /* residue */


	} else if (headers) {
		*headers = "";
	}

	/* parse parameters */
	endparams = strchr(parameters,'\0');
	if ((c = strchr(parameters, ';'))) {
			*c++ = '\0';
		parameters = c;
	} else {
		parameters = endparams;
		}

	if (params) {
		char *rem = parameters; /* unparsed or unrecognised remainder */
		char *label;
		char *value;
		int lr = 0;

		params->transport = "";
		params->user = "";
		params->method = "";
		params->ttl = "";
		params->maddr = "";
		params->lr = 0;

		rem = parameters;

		while ((value = strchr(parameters, '=')) || (lr = !strncmp(parameters, "lr", 2))) {
			/* The while condition will not continue evaluation to set lr if it matches "lr=" */
			if (lr) {
				value = parameters;
			} else {
				*value++ = '\0';
			}
			label = parameters;
			if ((c = strchr(value, ';'))) {
			*c++ = '\0';
				parameters = c;
			} else {
				parameters = endparams;
		}

			if (!strcmp(label, "transport")) {
				if (params) {params->transport=value;}
				rem = parameters;
			} else if (!strcmp(label, "user")) {
				if (params) {params->user=value;}
				rem = parameters;
			} else if (!strcmp(label, "method")) {
				if (params) {params->method=value;}
				rem = parameters;
			} else if (!strcmp(label, "ttl")) {
				if (params) {params->ttl=value;}
				rem = parameters;
			} else if (!strcmp(label, "maddr")) {
				if (params) {params->maddr=value;}
				rem = parameters;
			/* Treat "lr", "lr=yes", "lr=on", "lr=1", "lr=almostanything" as lr enabled and "", "lr=no", "lr=off", "lr=0", "lr=" and "lranything" as lr disabled */
			} else if ((!strcmp(label, "lr") && strcmp(value, "no") && strcmp(value, "off") && strcmp(value, "0") && strcmp(value, "")) || ((lr) && strcmp(value, "lr"))) {
				if (params) {params->lr=1;}
				rem = parameters;
			} else {
				value--;
				*value = '=';
				if(c) {
				c--;
				*c = ';';
	}
			}
		}
		if (rem > uri) { /* no headers */
			uri = rem;
		}

	}

	if (residue) {
		*residue = uri;
	}

	return error;
}


AST_TEST_DEFINE(sip_parse_uri_fully_test)
{
	int res = AST_TEST_PASS;
	char uri[1024];
	char *user, *pass, *domain, *headers, *residue;
	struct uriparams params;

	struct testdata {
		char *desc;
		char *uri;
		char **userptr;
		char **passptr;
		char **domainptr;
		char **headersptr;
		char **residueptr;
		struct uriparams *paramsptr;
		char *user;
		char *pass;
		char *domain;
		char *headers;
		char *residue;
		struct uriparams params;
		AST_LIST_ENTRY(testdata) list;
	};


	struct testdata *testdataptr;

	static AST_LIST_HEAD_NOLOCK(testdataliststruct, testdata) testdatalist;

	struct testdata td1 = {
		.desc = "no headers",
		.uri = "sip:user:secret@host:5060;param=discard;transport=tcp;param2=residue",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "",
		.residue = "param2=residue",
		.params.transport = "tcp",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td2 = {
		.desc = "with headers",
		.uri = "sip:user:secret@host:5060;param=discard;transport=tcp;param2=discard2?header=blah&header2=blah2;param3=residue",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "header=blah&header2=blah2",
		.residue = "param3=residue",
		.params.transport = "tcp",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td3 = {
		.desc = "difficult user",
		.uri = "sip:-_.!~*'()&=+$,;?/:secret@host:5060;transport=tcp",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "-_.!~*'()&=+$,;?/",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "",
		.residue = "",
		.params.transport = "tcp",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td4 = {
		.desc = "difficult pass",
		.uri = "sip:user:-_.!~*'()&=+$,@host:5060;transport=tcp",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "user",
		.pass = "-_.!~*'()&=+$,",
		.domain = "host:5060",
		.headers = "",
		.residue = "",
		.params.transport = "tcp",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td5 = {
		.desc = "difficult host",
		.uri = "sip:user:secret@1-1.a-1.:5060;transport=tcp",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "user",
		.pass = "secret",
		.domain = "1-1.a-1.:5060",
		.headers = "",
		.residue = "",
		.params.transport = "tcp",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td6 = {
		.desc = "difficult params near transport",
		.uri = "sip:user:secret@host:5060;-_.!~*'()[]/:&+$=-_.!~*'()[]/:&+$;transport=tcp",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "",
		.residue = "",
		.params.transport = "tcp",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td7 = {
		.desc = "difficult params near headers",
		.uri = "sip:user:secret@host:5060;-_.!~*'()[]/:&+$=-_.!~*'()[]/:&+$?header=blah&header2=blah2;-_.!~*'()[]/:&+$=residue",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "header=blah&header2=blah2",
		.residue = "-_.!~*'()[]/:&+$=residue",
		.params.transport = "",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td8 = {
		.desc = "lr parameter",
		.uri = "sip:user:secret@host:5060;param=discard;lr?header=blah",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "header=blah",
		.residue = "",
		.params.transport = "",
		.params.lr = 1,
		.params.user = ""
	};

	struct testdata td9 = {
		.desc = "alternative lr parameter",
		.uri = "sip:user:secret@host:5060;param=discard;lr=yes?header=blah",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "header=blah",
		.residue = "",
		.params.transport = "",
		.params.lr = 1,
		.params.user = ""
	};

	struct testdata td10 = {
		.desc = "no lr parameter",
		.uri = "sip:user:secret@host:5060;paramlr=lr;lr=no;lr=off;lr=0;lr=;=lr;lrextra;lrparam2=lr?header=blah",
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "header=blah",
		.residue = "",
		.params.transport = "",
		.params.lr = 0,
		.params.user = ""
	};


	AST_LIST_HEAD_SET_NOLOCK(&testdatalist, &td1);
	AST_LIST_INSERT_TAIL(&testdatalist, &td2, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td3, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td4, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td5, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td6, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td7, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td8, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td9, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td10, list);


	switch (cmd) {
	case TEST_INIT:
		info->name = "sip_uri_full_parse_test";
		info->category = "channels/chan_sip/";
		info->summary = "tests sip full uri parsing";
		info->description =
			"Tests full parsing of various URIs "
			"Verifies output matches expected behavior.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	AST_LIST_TRAVERSE(&testdatalist, testdataptr, list) {
		user = pass = domain = headers = residue = NULL;
		params.transport = params.user = params.method = params.ttl = params.maddr = NULL;
		params.lr = 0;

		ast_copy_string(uri,testdataptr->uri,sizeof(uri));
		if (parse_uri_full(uri, "sip:,sips:", testdataptr->userptr,
				   testdataptr->passptr, testdataptr->domainptr,
				   testdataptr->paramsptr,
				   testdataptr->headersptr,
				   testdataptr->residueptr) ||
			((testdataptr->userptr) && strcmp(testdataptr->user, user)) ||
			((testdataptr->passptr) && strcmp(testdataptr->pass, pass)) ||
			((testdataptr->domainptr) && strcmp(testdataptr->domain, domain)) ||
			((testdataptr->headersptr) && strcmp(testdataptr->headers, headers)) ||
			((testdataptr->residueptr) && strcmp(testdataptr->residue, residue)) ||
			((testdataptr->paramsptr) && strcmp(testdataptr->params.transport,params.transport)) ||
			((testdataptr->paramsptr) && (testdataptr->params.lr != params.lr)) ||
			((testdataptr->paramsptr) && strcmp(testdataptr->params.user,params.user))
		) {
				ast_test_status_update(test, "Sub-Test: %s, failed.\n", testdataptr->desc);
				res = AST_TEST_FAIL;
		}
	}


	return res;
}


int parse_uri(char *uri, const char *scheme, char **user, char **pass,
	      char **domain, char **transport) {
	int ret;
	char *headers;
	struct uriparams params;

	headers = NULL;
	ret = parse_uri_full(uri, scheme, user, pass, domain, &params, &headers, NULL);
	if (transport) {
		*transport=params.transport;
	}
	return ret;
}

AST_TEST_DEFINE(sip_parse_uri_test)
{
	int res = AST_TEST_PASS;
	char *name, *pass, *domain, *transport;
	char uri1[] = "sip:name@host";
	char uri2[] = "sip:name@host;transport=tcp";
	char uri3[] = "sip:name:secret@host;transport=tcp";
	char uri4[] = "sip:name:secret@host:port;transport=tcp?headers=%40%40testblah&headers2=blah%20blah";
	/* test 5 is for NULL input */
	char uri6[] = "sip:name:secret@host:port;transport=tcp?headers=%40%40testblah&headers2=blah%20blah";
	char uri7[] = "sip:name:secret@host:port;transport=tcp?headers=%40%40testblah&headers2=blah%20blah";
	char uri8[] = "sip:host";
	char uri9[] = "sip:host:port;transport=tcp?headers=%40%40testblah&headers2=blah%20blah";
	char uri10[] = "host:port;transport=tcp?headers=%40%40testblah&headers2=blah%20blah";
	char uri11[] = "host";

	switch (cmd) {
	case TEST_INIT:
		info->name = "sip_uri_parse_test";
		info->category = "channels/chan_sip/";
		info->summary = "tests sip uri parsing";
		info->description =
							"Tests parsing of various URIs "
							"Verifies output matches expected behavior.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Test 1, simple URI */
	name = pass = domain = transport = NULL;
	if (parse_uri(uri1, "sip:,sips:", &name, &pass, &domain, &transport) ||
			strcmp(name, "name")        ||
			!ast_strlen_zero(pass)      ||
			strcmp(domain, "host")      ||
			!ast_strlen_zero(transport)) {
		ast_test_status_update(test, "Test 1: simple uri failed. \n");
		res = AST_TEST_FAIL;
	}

	/* Test 2, add tcp transport */
	name = pass = domain = transport = NULL;
	if (parse_uri(uri2, "sip:,sips:", &name, &pass, &domain, &transport) ||
			strcmp(name, "name")        ||
			!ast_strlen_zero(pass)      ||
			strcmp(domain, "host")    ||
			strcmp(transport, "tcp")) {
		ast_test_status_update(test, "Test 2: uri with addtion of tcp transport failed. \n");
		res = AST_TEST_FAIL;
	}

	/* Test 3, add secret */
	name = pass = domain = transport = NULL;
	if (parse_uri(uri3, "sip:,sips:", &name, &pass, &domain, &transport) ||
			strcmp(name, "name")        ||
			strcmp(pass, "secret")      ||
			strcmp(domain, "host")    ||
			strcmp(transport, "tcp")) {
		ast_test_status_update(test, "Test 3: uri with addition of secret failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 4, add port and unparsed header field*/
	name = pass = domain = transport = NULL;
	if (parse_uri(uri4, "sip:,sips:", &name, &pass, &domain, &transport) ||
			strcmp(name, "name")        ||
			strcmp(pass, "secret")      ||
			strcmp(domain, "host:port") ||
			strcmp(transport, "tcp")) {
		ast_test_status_update(test, "Test 4: add port and unparsed header field failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 5, verify parse_uri does not crash when given a NULL uri */
	name = pass = domain = transport = NULL;
	if (!parse_uri(NULL, "sip:,sips:", &name, &pass, &domain, &transport)) {
		ast_test_status_update(test, "Test 5: passing a NULL uri failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 6, verify parse_uri does not crash when given a NULL output parameters */
	name = pass = domain = transport = NULL;
	if (parse_uri(uri6, "sip:,sips:", NULL, NULL, NULL, NULL)) {
		ast_test_status_update(test, "Test 6: passing NULL output parameters failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 7, verify parse_uri returns user:secret and domain when no port or secret output parameters are supplied. */
	name = pass = domain = transport = NULL;
	if (parse_uri(uri7, "sip:,sips:", &name, NULL, &domain, NULL) ||
			strcmp(name, "name:secret")        ||
			strcmp(domain, "host:port")) {

		ast_test_status_update(test, "Test 7: providing no port and secret output parameters failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 8, verify parse_uri can handle a domain only uri */
	name = pass = domain = transport = NULL;
	if (parse_uri(uri8, "sip:,sips:", &name, &pass, &domain, &transport) ||
			strcmp(domain, "host") ||
			!ast_strlen_zero(name)) {
		ast_test_status_update(test, "Test 8: add port and unparsed header field failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 9, add port and unparsed header field with domain only uri*/
	name = pass = domain = transport = NULL;
	if (parse_uri(uri9, "sip:,sips:", &name, &pass, &domain, &transport) ||
			!ast_strlen_zero(name)        ||
			!ast_strlen_zero(pass)      ||
			strcmp(domain, "host:port")    ||
			strcmp(transport, "tcp")) {
		ast_test_status_update(test, "Test 9: domain only uri failed \n");
		res = AST_TEST_FAIL;
	}

	/* Test 10, handle invalid/missing "sip:,sips:" scheme
	 * we expect parse_uri to return an error, but still parse
	 * the results correctly here */
	name = pass = domain = transport = NULL;
	if (!parse_uri(uri10, "sip:,sips:", &name, &pass, &domain, &transport) ||
			!ast_strlen_zero(name)        ||
			!ast_strlen_zero(pass)      ||
			strcmp(domain, "host:port")    ||
			strcmp(transport, "tcp")) {
		ast_test_status_update(test, "Test 10: missing \"sip:sips:\" scheme failed\n");
		res = AST_TEST_FAIL;
	}

	/* Test 11, simple domain only URI with missing scheme
	 * we expect parse_uri to return an error, but still parse
	 * the results correctly here */
	name = pass = domain = transport = NULL;
	if (!parse_uri(uri11, "sip:,sips:", &name, &pass, &domain, &transport) ||
			!ast_strlen_zero(name)      ||
			!ast_strlen_zero(pass)      ||
			strcmp(domain, "host")      ||
			!ast_strlen_zero(transport)) {
		ast_test_status_update(test, "Test 11: simple uri with missing scheme failed. \n");
		res = AST_TEST_FAIL;
	}

	return res;
}

/*! \brief  Get caller id name from SIP headers, copy into output buffer
 *
 *  \retval input string pointer placed after display-name field if possible
 */
const char *get_calleridname(const char *input, char *output, size_t outputsize)
{
	/* From RFC3261:
	 * 
	 * From           =  ( "From" / "f" ) HCOLON from-spec
	 * from-spec      =  ( name-addr / addr-spec ) *( SEMI from-param )
	 * name-addr      =  [ display-name ] LAQUOT addr-spec RAQUOT
	 * display-name   =  *(token LWS)/ quoted-string
	 * token          =  1*(alphanum / "-" / "." / "!" / "%" / "*"
	 *                     / "_" / "+" / "`" / "'" / "~" )
	 * quoted-string  =  SWS DQUOTE *(qdtext / quoted-pair ) DQUOTE
	 * qdtext         =  LWS / %x21 / %x23-5B / %x5D-7E
	 *                     / UTF8-NONASCII
	 * quoted-pair    =  "\" (%x00-09 / %x0B-0C / %x0E-7F)
	 *
	 * HCOLON         = *WSP ":" SWS
	 * SWS            = [LWS]
	 * LWS            = *[*WSP CRLF] 1*WSP
	 * WSP            = (SP / HTAB)
	 *
	 * Deviations from it:
	 * - following CRLF's in LWS is not done (here at least)
	 * - ascii NUL is never legal as it terminates the C-string
	 * - utf8-nonascii is not checked for validity
	 */
	char *orig_output = output;
	const char *orig_input = input;

	/* clear any empty characters in the beginning */
	input = ast_skip_blanks(input);

	/* no data at all or no storage room? */
	if (!input || *input == '<' || !outputsize || !output) {
		return orig_input;
	}

	/* make sure the output buffer is initilized */
	*orig_output = '\0';

	/* make room for '\0' at the end of the output buffer */
	outputsize--;

	/* quoted-string rules */
	if (input[0] == '"') {
		input++; /* skip the first " */

		for (;((outputsize > 0) && *input); input++) {
			if (*input == '"') {  /* end of quoted-string */
				break;
			} else if (*input == 0x5c) { /* quoted-pair = "\" (%x00-09 / %x0B-0C / %x0E-7F) */
				input++;
				if (!*input || (unsigned char)*input > 0x7f || *input == 0xa || *input == 0xd) {
					continue;  /* not a valid quoted-pair, so skip it */
				}
			} else if (((*input != 0x9) && ((unsigned char) *input < 0x20)) ||
			            (*input == 0x7f)) {
				continue; /* skip this invalid character. */
			}

			*output++ = *input;
			outputsize--;
		}

		/* if this is successful, input should be at the ending quote */
		if (!input || *input != '"') {
			ast_log(LOG_WARNING, "No ending quote for display-name was found\n");
			*orig_output = '\0';
			return orig_input;
		}

		/* make sure input is past the last quote */
		input++;

		/* terminate outbuf */
		*output = '\0';
	} else {  /* either an addr-spec or tokenLWS-combo */
		for (;((outputsize > 0) && *input); input++) {
			/* token or WSP (without LWS) */
			if ((*input >= '0' && *input <= '9') || (*input >= 'A' && *input <= 'Z')
				|| (*input >= 'a' && *input <= 'z') || *input == '-' || *input == '.'
				|| *input == '!' || *input == '%' || *input == '*' || *input == '_'
				|| *input == '+' || *input == '`' || *input == '\'' || *input == '~'
				|| *input == 0x9 || *input == ' ') {
				*output++ = *input;
				outputsize -= 1;
			} else if (*input == '<') {   /* end of tokenLWS-combo */
				/* we could assert that the previous char is LWS, but we don't care */
				break;
			} else if (*input == ':') {
				/* This invalid character which indicates this is addr-spec rather than display-name. */
				*orig_output = '\0';
				return orig_input;
			} else {         /* else, invalid character we can skip. */
				continue;    /* skip this character */
			}
		}

		if (*input != '<') {   /* if we never found the start of addr-spec then this is invalid */
			*orig_output = '\0';
			return orig_input;
		}

		/* set NULL while trimming trailing whitespace */
		do {
			*output-- = '\0';
		} while (*output == 0x9 || *output == ' '); /* we won't go past orig_output as first was a non-space */
	}

	return input;
}

AST_TEST_DEFINE(get_calleridname_test)
{
	int res = AST_TEST_PASS;
	const char *in1 = "\" quoted-text internal \\\" quote \"<stuff>";
	const char *in2 = " token text with no quotes <stuff>";
	const char *overflow1 = " \"quoted-text overflow 1234567890123456789012345678901234567890\" <stuff>";
	const char *noendquote = " \"quoted-text no end <stuff>";
	const char *addrspec = " \"sip:blah@blah <stuff>";
	const char *no_quotes_no_brackets = "blah@blah";
	const char *after_dname;
	char dname[40];

	switch (cmd) {
	case TEST_INIT:
		info->name = "sip_get_calleridname_test";
		info->category = "channels/chan_sip/";
		info->summary = "decodes callerid name from sip header";
		info->description = "Decodes display-name field of sip header.  Checks for valid output and expected failure cases.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* quoted-text with backslash escaped quote */
	after_dname = get_calleridname(in1, dname, sizeof(dname));
	ast_test_status_update(test, "display-name1: %s\nafter: %s\n", dname, after_dname);
	if (strcmp(dname, " quoted-text internal \" quote ")) {
		ast_test_status_update(test, "display-name1 test failed\n");
		res = AST_TEST_FAIL;
	}

	/* token text */
	after_dname = get_calleridname(in2, dname, sizeof(dname));
	ast_test_status_update(test, "display-name2: %s\nafter: %s\n", dname, after_dname);
	if (strcmp(dname, "token text with no quotes")) {
		ast_test_status_update(test, "display-name2 test failed\n");
		res = AST_TEST_FAIL;
	}

	/* quoted-text buffer overflow */
	after_dname = get_calleridname(overflow1, dname, sizeof(dname));
	ast_test_status_update(test, "overflow display-name1: %s\nafter: %s\n", dname, after_dname);
	if (*dname != '\0' && after_dname != overflow1) {
		ast_test_status_update(test, "overflow display-name1 test failed\n");
		res = AST_TEST_FAIL;
	}

	/* quoted-text buffer with no terminating end quote */
	after_dname = get_calleridname(noendquote, dname, sizeof(dname));
	ast_test_status_update(test, "noendquote display-name1: %s\nafter: %s\n", dname, after_dname);
	if (*dname != '\0' && after_dname != noendquote) {
		ast_test_status_update(test, "no end quote for quoted-text display-name failed\n");
		res = AST_TEST_FAIL;
	}

	/* addr-spec rather than display-name. */
	after_dname = get_calleridname(addrspec, dname, sizeof(dname));
	ast_test_status_update(test, "noendquote display-name1: %s\nafter: %s\n", dname, after_dname);
	if (*dname != '\0' && after_dname != addrspec) {
		ast_test_status_update(test, "detection of addr-spec failed\n");
		res = AST_TEST_FAIL;
	}

	/* no quotes, no brackets */
	after_dname = get_calleridname(no_quotes_no_brackets, dname, sizeof(dname));
	ast_test_status_update(test, "no_quotes_no_brackets display-name1: %s\nafter: %s\n", dname, after_dname);
	if (*dname != '\0' && after_dname != no_quotes_no_brackets) {
		ast_test_status_update(test, "detection of addr-spec failed\n");
		res = AST_TEST_FAIL;
	}


	return res;
}

int get_name_and_number(const char *hdr, char **name, char **number)
{
	char header[256];
	char tmp_name[50] = { 0, };
	char *tmp_number = NULL;
	char *domain = NULL;
	char *dummy = NULL;

	if (!name || !number || ast_strlen_zero(hdr)) {
		return -1;
	}

	*number = NULL;
	*name = NULL;
	ast_copy_string(header, hdr, sizeof(header));

	/* strip the display-name portion off the beginning of the header. */
	get_calleridname(header, tmp_name, sizeof(tmp_name));

	/* get uri within < > brackets */
	tmp_number = get_in_brackets(header);

	/* parse out the number here */
	if (parse_uri(tmp_number, "sip:,sips:", &tmp_number, &dummy, &domain, NULL) || ast_strlen_zero(tmp_number)) {
		ast_log(LOG_ERROR, "can not parse name and number from sip header.\n");
		return -1;
	}

	/* number is not option, and must be present at this point */
	*number = ast_strdup(tmp_number);
	ast_uri_decode(*number);

	/* name is optional and may not be present at this point */
	if (!ast_strlen_zero(tmp_name)) {
		*name = ast_strdup(tmp_name);
	}

	return 0;
}

AST_TEST_DEFINE(get_name_and_number_test)
{
	int res = AST_TEST_PASS;
	char *name = NULL;
	char *number = NULL;
	const char *in1 = "NAME <sip:NUMBER@place>";
	const char *in2 = "\"NA><ME\" <sip:NUMBER@place>";
	const char *in3 = "NAME";
	const char *in4 = "<sip:NUMBER@place>";
	const char *in5 = "This is a screwed up string <sip:LOLCLOWNS<sip:>@place>";

	switch (cmd) {
	case TEST_INIT:
		info->name = "sip_get_name_and_number_test";
		info->category = "channels/chan_sip/";
		info->summary = "Tests getting name and number from sip header";
		info->description =
				"Runs through various test situations in which a name and "
				"and number can be retrieved from a sip header.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Test 1. get name and number */
	number = name = NULL;
	if ((get_name_and_number(in1, &name, &number)) ||
		strcmp(name, "NAME") ||
		strcmp(number, "NUMBER")) {

		ast_test_status_update(test, "Test 1, simple get name and number failed.\n");
		res = AST_TEST_FAIL;
	}
	ast_free(name);
	ast_free(number);

	/* Test 2. get quoted name and number */
	number = name = NULL;
	if ((get_name_and_number(in2, &name, &number)) ||
		strcmp(name, "NA><ME") ||
		strcmp(number, "NUMBER")) {

		ast_test_status_update(test, "Test 2, get quoted name and number failed.\n");
		res = AST_TEST_FAIL;
	}
	ast_free(name);
	ast_free(number);

	/* Test 3. name only */
	number = name = NULL;
	if (!(get_name_and_number(in3, &name, &number))) {

		ast_test_status_update(test, "Test 3, get name only was expected to fail but did not.\n");
		res = AST_TEST_FAIL;
	}
	ast_free(name);
	ast_free(number);

	/* Test 4. number only */
	number = name = NULL;
	if ((get_name_and_number(in4, &name, &number)) ||
		!ast_strlen_zero(name) ||
		strcmp(number, "NUMBER")) {

		ast_test_status_update(test, "Test 4, get number with no name present failed.\n");
		res = AST_TEST_FAIL;
	}
	ast_free(name);
	ast_free(number);

	/* Test 5. malformed string, since number can not be parsed, this should return an error.  */
	number = name = NULL;
	if (!(get_name_and_number(in5, &name, &number)) ||
		!ast_strlen_zero(name) ||
		!ast_strlen_zero(number)) {

		ast_test_status_update(test, "Test 5, processing malformed string failed.\n");
		res = AST_TEST_FAIL;
	}
	ast_free(name);
	ast_free(number);

	/* Test 6. NULL output parameters */
	number = name = NULL;
	if (!(get_name_and_number(in5, NULL, NULL))) {

		ast_test_status_update(test, "Test 6, NULL output parameters failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 7. NULL input parameter */
	number = name = NULL;
	if (!(get_name_and_number(NULL, &name, &number)) ||
		!ast_strlen_zero(name) ||
		!ast_strlen_zero(number)) {

		ast_test_status_update(test, "Test 7, NULL input parameter failed.\n");
		res = AST_TEST_FAIL;
	}
	ast_free(name);
	ast_free(number);

	return res;
}

int get_in_brackets_full(char *tmp,char **out,char **residue)
{
	const char *parse = tmp;
	char *first_bracket;
	char *second_bracket;

	if (out) {
		*out = "";
	}
	if (residue) {
		*residue = "";
	}

	if (ast_strlen_zero(tmp)) {
		return 1;
	}

	/*
	 * Skip any quoted text until we find the part in brackets.
	* On any error give up and return -1
	*/
	while ( (first_bracket = strchr(parse, '<')) ) {
		char *first_quote = strchr(parse, '"');
		first_bracket++;
		if (!first_quote || first_quote >= first_bracket) {
			break; /* no need to look at quoted part */
		}
		/* the bracket is within quotes, so ignore it */
		parse = find_closing_quote(first_quote + 1, NULL);
		if (!*parse) {
			ast_log(LOG_WARNING, "No closing quote found in '%s'\n", tmp);
			return  -1;
		}
		parse++;
	}

	/* If no first bracket then still look for a second bracket as some other parsing functions
	may overwrite first bracket with NULL when terminating a token based display-name. As this
	only affects token based display-names there is no danger of brackets being in quotes */
	if (first_bracket) {
		parse = first_bracket;
		} else {
		parse = tmp;
	}

	if ((second_bracket = strchr(parse, '>'))) {
		*second_bracket++ = '\0';
		if (out) {
			*out = first_bracket;
		}
		if (residue) {
			*residue = second_bracket;
		}
		return 0;
	}

	if ((first_bracket)) {
			ast_log(LOG_WARNING, "No closing bracket found in '%s'\n", tmp);
		return -1;
		}

	if (out) {
		*out = tmp;
	}

	return 1;
}

char *get_in_brackets(char *tmp)
{
	char *out;

	if ((get_in_brackets_full(tmp, &out, NULL))) {
		return tmp;
	}
	return out;
}

AST_TEST_DEFINE(get_in_brackets_test)
{
	int res = AST_TEST_PASS;
	char *in_brackets = "<sip:name:secret@host:port;transport=tcp?headers=testblah&headers2=blahblah>";
	char no_name[] = "<sip:name:secret@host:port;transport=tcp?headers=testblah&headers2=blahblah>";
	char quoted_string[] = "\"I'm a quote stri><ng\" <sip:name:secret@host:port;transport=tcp?headers=testblah&headers2=blahblah>";
	char missing_end_quote[] = "\"I'm a quote string <sip:name:secret@host:port;transport=tcp?headers=testblah&headers2=blahblah>";
	char name_no_quotes[] = "name not in quotes <sip:name:secret@host:port;transport=tcp?headers=testblah&headers2=blahblah>";
	char no_end_bracket[] = "name not in quotes <sip:name:secret@host:port;transport=tcp?headers=testblah&headers2=blahblah";
	char no_name_no_brackets[] = "sip:name@host";
	char *uri = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sip_get_in_brackets_test";
		info->category = "channels/chan_sip/";
		info->summary = "Tests getting a sip uri in <> brackets within a sip header.";
		info->description =
				"Runs through various test situations in which a sip uri "
				"in angle brackets needs to be retrieved";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Test 1, simple get in brackets */
	if (!(uri = get_in_brackets(no_name)) || !(strcmp(uri, in_brackets))) {

		ast_test_status_update(test, "Test 1, simple get in brackets failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 2, starts with quoted string */
	if (!(uri = get_in_brackets(quoted_string)) || !(strcmp(uri, in_brackets))) {

		ast_test_status_update(test, "Test 2, get in brackets with quoted string in front failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 3, missing end quote */
	if (!(uri = get_in_brackets(missing_end_quote)) || !(strcmp(uri, in_brackets))) {

		ast_test_status_update(test, "Test 3, missing end quote failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 4, starts with a name not in quotes */
	if (!(uri = get_in_brackets(name_no_quotes)) || !(strcmp(uri, in_brackets))) {

		ast_test_status_update(test, "Test 4, passing name not in quotes failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 5, no end bracket, should just return everything after the first '<'  */
	if (!(uri = get_in_brackets(no_end_bracket)) || !(strcmp(uri, in_brackets))) {

		ast_test_status_update(test, "Test 5, no end bracket failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 6, NULL input  */
	if ((uri = get_in_brackets(NULL))) {

		ast_test_status_update(test, "Test 6, NULL input failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 7, no name, and no brackets. */
	if (!(uri = get_in_brackets(no_name_no_brackets)) || (strcmp(uri, "sip:name@host"))) {

		ast_test_status_update(test, "Test 7 failed. %s\n", uri);
		res = AST_TEST_FAIL;
	}

	return res;
}


int parse_name_andor_addr(char *uri, const char *scheme, char **name,
			  char **user, char **pass, char **domain,
			  struct uriparams *params, char **headers,
			  char **residue)
{
	static char buf[1024];
	char **residue2=residue;
	int ret;
	if (name) {
		get_calleridname(uri,buf,sizeof(buf));
		*name = buf;
	}
	ret = get_in_brackets_full(uri,&uri,residue);
	if (ret == 0) { /* uri is in brackets so do not treat unknown trailing uri parameters as potential messageheader parameters */
		*residue = *residue + 1; /* step over the first semicolon so as per parse uri residue */
		residue2 = NULL;
	}

	return parse_uri_full(uri, scheme, user, pass, domain, params, headers,
			      residue2);
}

AST_TEST_DEFINE(parse_name_andor_addr_test)
{
	int res = AST_TEST_PASS;
	char uri[1024];
	char *name, *user, *pass, *domain, *headers, *residue;
	struct uriparams params;

	struct testdata {
		char *desc;
		char *uri;
		char **nameptr;
		char **userptr;
		char **passptr;
		char **domainptr;
		char **headersptr;
		char **residueptr;
		struct uriparams *paramsptr;
		char *name;
		char *user;
		char *pass;
		char *domain;
		char *headers;
		char *residue;
		struct uriparams params;
		AST_LIST_ENTRY(testdata) list;
	};

	struct testdata *testdataptr;

	static AST_LIST_HEAD_NOLOCK(testdataliststruct, testdata) testdatalist;

	struct testdata td1 = {
		.desc = "quotes and brackets",
		.uri = "\"name :@ \" <sip:user:secret@host:5060;param=discard;transport=tcp>;tag=tag",
		.nameptr = &name,
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.name =  "name :@ ",
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "",
		.residue = "tag=tag",
		.params.transport = "tcp",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td2 = {
		.desc = "no quotes",
		.uri = "givenname familyname <sip:user:secret@host:5060;param=discard;transport=tcp>;expires=3600",
		.nameptr = &name,
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.name = "givenname familyname",
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "",
		.residue = "expires=3600",
		.params.transport = "tcp",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td3 = {
		.desc = "no brackets",
		.uri = "sip:user:secret@host:5060;param=discard;transport=tcp;q=1",
		.nameptr = &name,
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.name = "",
		.user = "user",
		.pass = "secret",
		.domain = "host:5060",
		.headers = "",
		.residue = "q=1",
		.params.transport = "tcp",
		.params.lr = 0,
		.params.user = ""
	};

	struct testdata td4 = {
		.desc = "just host",
		.uri = "sips:host",
		.nameptr = &name,
		.userptr = &user,
		.passptr = &pass,
		.domainptr = &domain,
		.headersptr = &headers,
		.residueptr = &residue,
		.paramsptr = &params,
		.name = "",
		.user = "",
		.pass = "",
		.domain = "host",
		.headers = "",
		.residue = "",
		.params.transport = "",
		.params.lr = 0,
		.params.user = ""
	};


	AST_LIST_HEAD_SET_NOLOCK(&testdatalist, &td1);
	AST_LIST_INSERT_TAIL(&testdatalist, &td2, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td3, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td4, list);


	switch (cmd) {
	case TEST_INIT:
		info->name = "parse_name_andor_addr_test";
		info->category = "channels/chan_sip/";
		info->summary = "tests parsing of name_andor_addr abnf structure";
		info->description =
			"Tests parsing of abnf name-andor-addr = name-addr / addr-spec "
			"Verifies output matches expected behavior.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	AST_LIST_TRAVERSE(&testdatalist, testdataptr, list) {
		name = user = pass = domain = headers = residue = NULL;
		params.transport = params.user = params.method = params.ttl = params.maddr = NULL;
		params.lr = 0;
	ast_copy_string(uri,testdataptr->uri,sizeof(uri));
		if (parse_name_andor_addr(uri, "sip:,sips:",
					  testdataptr->nameptr,
					  testdataptr->userptr,
					  testdataptr->passptr,
					  testdataptr->domainptr,
					  testdataptr->paramsptr,
					  testdataptr->headersptr,
					  testdataptr->residueptr) ||
			((testdataptr->nameptr) && strcmp(testdataptr->name, name)) ||
			((testdataptr->userptr) && strcmp(testdataptr->user, user)) ||
			((testdataptr->passptr) && strcmp(testdataptr->pass, pass)) ||
			((testdataptr->domainptr) && strcmp(testdataptr->domain, domain)) ||
			((testdataptr->headersptr) && strcmp(testdataptr->headers, headers)) ||
			((testdataptr->residueptr) && strcmp(testdataptr->residue, residue)) ||
			((testdataptr->paramsptr) && strcmp(testdataptr->params.transport,params.transport)) ||
			((testdataptr->paramsptr) && strcmp(testdataptr->params.user,params.user))
		) {
				ast_test_status_update(test, "Sub-Test: %s,failed.\n", testdataptr->desc);
				res = AST_TEST_FAIL;
		}
	}


	return res;
}

int get_comma(char *in, char **out) {
	char *c;
	char *parse = in;
	if (out) {
		*out = in;
	}

	/* Skip any quoted text */
	while (*parse) {
		if ((c = strchr(parse, '"'))) {
			in = (char *)find_closing_quote((const char *)c + 1, NULL);
			if (!*in) {
				ast_log(LOG_WARNING, "No closing quote found in '%s'\n", c);
				return -1;
			} else {
				break;
			}
		} else {
			break;
		}
		parse++;
	}
	parse = in;

	/* Skip any userinfo components of a uri as they may contain commas */
	if ((c = strchr(parse,'@'))) {
		parse = c+1;
	}
	if ((out) && (c = strchr(parse,','))) {
		*c++ = '\0';
		*out = c;
		return 0;
	}
	return 1;
}

int parse_contact_header(char *contactheader, struct contactliststruct *contactlist) {
	int res;
	int last;
	char *comma;
	char *residue;
	char *param;
	char *value;
	struct contact *contact=NULL;

	if (*contactheader == '*') {
		return 1;
	}

	contact = malloc(sizeof(*contact));

	AST_LIST_HEAD_SET_NOLOCK(contactlist, contact);
	while ((last = get_comma(contactheader,&comma)) != -1) {

		res = parse_name_andor_addr(contactheader, "sip:,sips:",
					    &contact->name, &contact->user,
					    &contact->pass, &contact->domain,
					    &contact->params, &contact->headers,
					    &residue);
		if (res == -1) {
			return res;
		}

		/* parse contact params */
		contact->expires = contact->q = "";

		while ((value = strchr(residue,'='))) {
			*value++ = '\0';

			param = residue;
			if ((residue = strchr(value,';'))) {
				*residue++ = '\0';
			} else {
				residue = "";
			}

			if (!strcmp(param,"expires")) {
				contact->expires = value;
			} else if (!strcmp(param,"q")) {
				contact->q = value;
			}
		}

		if(last) {
			return 0;
		}
		contactheader = comma;

		contact = malloc(sizeof(*contact));
		AST_LIST_INSERT_TAIL(contactlist, contact, list);

	}
	return last;
}

AST_TEST_DEFINE(parse_contact_header_test)
{
	int res = AST_TEST_PASS;
	char contactheader[1024];
	int star;
	struct contactliststruct contactlist;
	struct contactliststruct *contactlistptr=&contactlist;

	struct testdata {
		char *desc;
		char *contactheader;
		int star;
		struct contactliststruct *contactlist;

		AST_LIST_ENTRY(testdata) list;
	};

	struct testdata *testdataptr;
	struct contact *tdcontactptr;
	struct contact *contactptr;

	static AST_LIST_HEAD_NOLOCK(testdataliststruct, testdata) testdatalist;
	struct contactliststruct contactlist1, contactlist2;

	struct testdata td1 = {
		.desc = "single contact",
		.contactheader = "\"name :@;?&,\" <sip:user:secret@host:5082;param=discard;transport=tcp>;expires=3600",
		.contactlist = &contactlist1,
		.star = 0
	};
	struct contact contact11 = {
		.name = "name :@;?&,",
		.user = "user",
		.pass = "secret",
		.domain = "host:5082",
		.params.transport = "tcp",
		.params.ttl = "",
		.params.lr = 0,
		.headers = "",
		.expires = "3600",
		.q = ""
	};

	struct testdata td2 = {
		.desc = "multiple contacts",
		.contactheader = "sip:,user1,:,secret1,@host1;ttl=7;q=1;expires=3600,sips:host2",
		.contactlist = &contactlist2,
		.star = 0,
	};
	struct contact contact21 = {
		.name = "",
		.user = ",user1,",
		.pass = ",secret1,",
		.domain = "host1",
		.params.transport = "",
		.params.ttl = "7",
		.params.lr = 0,
		.headers = "",
		.expires = "3600",
		.q = "1"
	};
	struct contact contact22 = {
		.name = "",
		.user = "",
		.pass = "",
		.domain = "host2",
		.params.transport = "",
		.params.ttl = "",
		.params.lr = 0,
		.headers = "",
		.expires = "",
		.q = ""
	};

	struct testdata td3 = {
		.desc = "star - all contacts",
		.contactheader = "*",
		.star = 1,
		.contactlist = NULL
	};

	AST_LIST_HEAD_SET_NOLOCK(&testdatalist, &td1);
	AST_LIST_INSERT_TAIL(&testdatalist, &td2, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &td3, list);

	AST_LIST_HEAD_SET_NOLOCK(&contactlist1, &contact11);

	AST_LIST_HEAD_SET_NOLOCK(&contactlist2, &contact21);
	AST_LIST_INSERT_TAIL(&contactlist2, &contact22, list);


	switch (cmd) {
	case TEST_INIT:
		info->name = "parse_contact_header_test";
		info->category = "channels/chan_sip/";
		info->summary = "tests parsing of sip contact header";
		info->description =
			"Tests parsing of a contact header including those with multiple contacts "
			"Verifies output matches expected behavior.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	AST_LIST_TRAVERSE(&testdatalist, testdataptr, list) {
		ast_copy_string(contactheader,testdataptr->contactheader,sizeof(contactheader));
		star = parse_contact_header(contactheader,contactlistptr);
		if (testdataptr->star) {
			/* expecting star rather than list of contacts */
			if (!star) {
				ast_test_status_update(test, "Sub-Test: %s,failed.\n", testdataptr->desc);
				res = AST_TEST_FAIL;
				break;
			}
		} else {
			contactptr = AST_LIST_FIRST(contactlistptr);
			AST_LIST_TRAVERSE(testdataptr->contactlist, tdcontactptr, list) {
				if (!contactptr ||
					strcmp(tdcontactptr->name, contactptr->name) ||
					strcmp(tdcontactptr->user, contactptr->user) ||
					strcmp(tdcontactptr->pass, contactptr->pass) ||
					strcmp(tdcontactptr->domain, contactptr->domain) ||
					strcmp(tdcontactptr->headers, contactptr->headers) ||
					strcmp(tdcontactptr->expires, contactptr->expires) ||
					strcmp(tdcontactptr->q, contactptr->q) ||
					strcmp(tdcontactptr->params.transport, contactptr->params.transport) ||
					strcmp(tdcontactptr->params.ttl, contactptr->params.ttl) ||
					(tdcontactptr->params.lr != contactptr->params.lr)
				) {
					ast_test_status_update(test, "Sub-Test: %s,failed.\n", testdataptr->desc);
					res = AST_TEST_FAIL;
					break;
				}

			contactptr = AST_LIST_NEXT(contactptr,list);
			}
		}

	}

	return res;
}

/*!
 * \brief Parse supported header in incoming packet
 *
 * \details This function parses through the options parameters and
 * builds a bit field representing all the SIP options in that field. When an
 * item is found that is not supported, it is copied to the unsupported
 * out buffer.
 *
 * \param option list
 * \param unsupported out buffer (optional)
 * \param unsupported out buffer length (optional)
 */
unsigned int parse_sip_options(const char *options, char *unsupported, size_t unsupported_len)
{
	char *next, *sep;
	char *temp;
	int i, found, supported;
	unsigned int profile = 0;

	char *out = unsupported;
	size_t outlen = unsupported_len;
	char *cur_out = out;

	if (out && (outlen > 0)) {
		memset(out, 0, outlen);
	}

	if (ast_strlen_zero(options) )
		return 0;

	temp = ast_strdupa(options);

	ast_debug(3, "Begin: parsing SIP \"Supported: %s\"\n", options);

	for (next = temp; next; next = sep) {
		found = FALSE;
		supported = FALSE;
		if ((sep = strchr(next, ',')) != NULL) {
			*sep++ = '\0';
		}

		/* trim leading and trailing whitespace */
		next = ast_strip(next);

		if (ast_strlen_zero(next)) {
			continue; /* if there is a blank argument in there just skip it */
		}

		ast_debug(3, "Found SIP option: -%s-\n", next);
		for (i = 0; i < ARRAY_LEN(sip_options); i++) {
			if (!strcasecmp(next, sip_options[i].text)) {
				profile |= sip_options[i].id;
				if (sip_options[i].supported == SUPPORTED) {
					supported = TRUE;
				}
				found = TRUE;
				ast_debug(3, "Matched SIP option: %s\n", next);
				break;
			}
		}

		/* If option is not supported, add to unsupported out buffer */
		if (!supported && out && outlen) {
			size_t copylen = strlen(next);
			size_t cur_outlen = strlen(out);
			/* Check to see if there is enough room to store this option.
			 * Copy length is string length plus 2 for the ',' and '\0' */
			if ((cur_outlen + copylen + 2) < outlen) {
				/* if this isn't the first item, add the ',' */
				if (cur_outlen) {
					*cur_out = ',';
					cur_out++;
					cur_outlen++;
				}
				ast_copy_string(cur_out, next, (outlen - cur_outlen));
				cur_out += copylen;
			}
		}

		if (!found) {
			profile |= SIP_OPT_UNKNOWN;
			if (!strncasecmp(next, "x-", 2))
				ast_debug(3, "Found private SIP option, not supported: %s\n", next);
			else
				ast_debug(3, "Found no match for SIP option: %s (Please file bug report!)\n", next);
		}
	}

	return profile;
}

AST_TEST_DEFINE(sip_parse_options_test)
{
	int res = AST_TEST_PASS;
	char unsupported[64];
	unsigned int option_profile = 0;
	struct testdata {
		char *name;
		char *input_options;
		char *expected_unsupported;
		unsigned int expected_profile;
		AST_LIST_ENTRY(testdata) list;
	};

	struct testdata *testdataptr;
	static AST_LIST_HEAD_NOLOCK(testdataliststruct, testdata) testdatalist;

	struct testdata test1 = {
		.name = "test_all_unsupported",
		.input_options = "unsupported1,,, ,unsupported2,unsupported3,unsupported4",
		.expected_unsupported = "unsupported1,unsupported2,unsupported3,unsupported4",
		.expected_profile = SIP_OPT_UNKNOWN,
	};
	struct testdata test2 = {
		.name = "test_all_unsupported_one_supported",
		.input_options = "  unsupported1, replaces,   unsupported3  , , , ,unsupported4",
		.expected_unsupported = "unsupported1,unsupported3,unsupported4",
		.expected_profile = SIP_OPT_UNKNOWN | SIP_OPT_REPLACES
	};
	struct testdata test3 = {
		.name = "test_two_supported_two_unsupported",
		.input_options = ",,  timer  ,replaces     ,unsupported3,unsupported4",
		.expected_unsupported = "unsupported3,unsupported4",
		.expected_profile = SIP_OPT_UNKNOWN | SIP_OPT_REPLACES | SIP_OPT_TIMER,
	};

	struct testdata test4 = {
		.name = "test_all_supported",
		.input_options = "timer,replaces",
		.expected_unsupported = "",
		.expected_profile = SIP_OPT_REPLACES | SIP_OPT_TIMER,
	};

	struct testdata test5 = {
		.name = "test_all_supported_redundant",
		.input_options = "timer,replaces,timer,replace,timer,replaces",
		.expected_unsupported = "",
		.expected_profile = SIP_OPT_REPLACES | SIP_OPT_TIMER,
	};
	struct testdata test6 = {
		.name = "test_buffer_overflow",
		.input_options = "unsupported1,replaces,timer,unsupported4,unsupported_huge____"
		"____________________________________,__________________________________________"
		"________________________________________________",
		.expected_unsupported = "unsupported1,unsupported4",
		.expected_profile = SIP_OPT_UNKNOWN | SIP_OPT_REPLACES | SIP_OPT_TIMER,
	};
	struct testdata test7 = {
		.name = "test_null_input",
		.input_options = NULL,
		.expected_unsupported = "",
		.expected_profile = 0,
	};
	struct testdata test8 = {
		.name = "test_whitespace_input",
		.input_options = "         ",
		.expected_unsupported = "",
		.expected_profile = 0,
	};
	struct testdata test9 = {
		.name = "test_whitespace_plus_option_input",
		.input_options = " , , ,timer , ,  , ,        ,    ",
		.expected_unsupported = "",
		.expected_profile = SIP_OPT_TIMER,
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "sip_parse_options_test";
		info->category = "channels/chan_sip/";
		info->summary = "Tests parsing of sip options";
		info->description =
							"Tests parsing of SIP options from supported and required "
							"header fields.  Verifies when unsupported options are encountered "
							"that they are appended to the unsupported out buffer and that the "
							"correct bit field representnig the option profile is returned.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	AST_LIST_HEAD_SET_NOLOCK(&testdatalist, &test1);
	AST_LIST_INSERT_TAIL(&testdatalist, &test2, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &test3, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &test4, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &test5, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &test6, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &test7, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &test8, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &test9, list);

	/* Test with unsupported char buffer */
	AST_LIST_TRAVERSE(&testdatalist, testdataptr, list) {
		option_profile = parse_sip_options(testdataptr->input_options, unsupported, ARRAY_LEN(unsupported));
		if (option_profile != testdataptr->expected_profile ||
			strcmp(unsupported, testdataptr->expected_unsupported)) {
			ast_test_status_update(test, "Test with output buffer \"%s\", expected unsupported: %s actual unsupported:"
				"%s expected bit profile: %x actual bit profile: %x\n",
				testdataptr->name,
				testdataptr->expected_unsupported,
				unsupported,
				testdataptr->expected_profile,
				option_profile);
			res = AST_TEST_FAIL;
		} else {
			ast_test_status_update(test, "\"%s\" passed got expected unsupported: %s and bit profile: %x\n",
				testdataptr->name,
				unsupported,
				option_profile);
		}

		option_profile = parse_sip_options(testdataptr->input_options, NULL, 0);
		if (option_profile != testdataptr->expected_profile) {
			ast_test_status_update(test, "NULL output test \"%s\", expected bit profile: %x actual bit profile: %x\n",
				testdataptr->name,
				testdataptr->expected_profile,
				option_profile);
			res = AST_TEST_FAIL;
		} else {
			ast_test_status_update(test, "\"%s\" with NULL output buf passed, bit profile: %x\n",
				testdataptr->name,
				option_profile);
		}
	}

	return res;
}


void sip_request_parser_register_tests(void)
{
	AST_TEST_REGISTER(get_calleridname_test);
	AST_TEST_REGISTER(sip_parse_uri_test);
	AST_TEST_REGISTER(get_in_brackets_test);
	AST_TEST_REGISTER(get_name_and_number_test);
	AST_TEST_REGISTER(sip_parse_uri_fully_test);
	AST_TEST_REGISTER(parse_name_andor_addr_test);
	AST_TEST_REGISTER(parse_contact_header_test);
	AST_TEST_REGISTER(sip_parse_options_test);
}
void sip_request_parser_unregister_tests(void)
{
	AST_TEST_UNREGISTER(sip_parse_uri_test);
	AST_TEST_UNREGISTER(get_calleridname_test);
	AST_TEST_UNREGISTER(get_in_brackets_test);
	AST_TEST_UNREGISTER(get_name_and_number_test);
	AST_TEST_UNREGISTER(sip_parse_uri_fully_test);
	AST_TEST_UNREGISTER(parse_name_andor_addr_test);
	AST_TEST_UNREGISTER(parse_contact_header_test);
	AST_TEST_UNREGISTER(sip_parse_options_test);
}
