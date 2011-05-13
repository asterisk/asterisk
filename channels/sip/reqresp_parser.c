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

#ifdef HAVE_XLOCALE_H
locale_t c_locale;
#endif

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
				if (c) {
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
		info->category = "/channels/chan_sip/";
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
		info->category = "/channels/chan_sip/";
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
		info->category = "/channels/chan_sip/";
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
	ast_uri_decode(*number, ast_uri_sip_user);

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
		info->category = "/channels/chan_sip/";
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
		info->category = "/channels/chan_sip/";
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
	char buf[1024];
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
		info->category = "/channels/chan_sip/";
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
		info->category = "/channels/chan_sip/";
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
		info->category = "/channels/chan_sip/";
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

/*! \brief helper routine for sip_uri_cmp to compare URI parameters
 *
 * This takes the parameters from two SIP URIs and determines
 * if the URIs match. The rules for parameters *suck*. Here's a breakdown
 * 1. If a parameter appears in both URIs, then they must have the same value
 *    in order for the URIs to match
 * 2. If one URI has a user, maddr, ttl, or method parameter, then the other
 *    URI must also have that parameter and must have the same value
 *    in order for the URIs to match
 * 3. All other headers appearing in only one URI are not considered when
 *    determining if URIs match
 *
 * \param input1 Parameters from URI 1
 * \param input2 Parameters from URI 2
 * \retval 0 URIs' parameters match
 * \retval nonzero URIs' parameters do not match
 */
static int sip_uri_params_cmp(const char *input1, const char *input2)
{
	char *params1 = NULL;
	char *params2 = NULL;
	char *pos1;
	char *pos2;
	int zerolength1 = 0;
	int zerolength2 = 0;
	int maddrmatch = 0;
	int ttlmatch = 0;
	int usermatch = 0;
	int methodmatch = 0;

	if (ast_strlen_zero(input1)) {
		zerolength1 = 1;
	} else {
		params1 = ast_strdupa(input1);
	}
	if (ast_strlen_zero(input2)) {
		zerolength2 = 1;
	} else {
		params2 = ast_strdupa(input2);
	}

	/* Quick optimization. If both params are zero-length, then
	 * they match
	 */
	if (zerolength1 && zerolength2) {
		return 0;
	}

	for (pos1 = strsep(&params1, ";"); pos1; pos1 = strsep(&params1, ";")) {
		char *value1 = pos1;
		char *name1 = strsep(&value1, "=");
		char *params2dup = NULL;
		int matched = 0;
		if (!value1) {
			value1 = "";
		}
		/* Checkpoint reached. We have the name and value parsed for param1
		 * We have to duplicate params2 each time through this loop
		 * or else the inner loop below will not work properly.
		 */
		if (!zerolength2) {
			params2dup = ast_strdupa(params2);
		}
		for (pos2 = strsep(&params2dup, ";"); pos2; pos2 = strsep(&params2dup, ";")) {
			char *name2 = pos2;
			char *value2 = strchr(pos2, '=');
			if (!value2) {
				value2 = "";
			} else {
				*value2++ = '\0';
			}
			if (!strcasecmp(name1, name2)) {
				if (strcasecmp(value1, value2)) {
					goto fail;
				} else {
					matched = 1;
					break;
				}
			}
		}
		/* Check to see if the parameter is one of the 'must-match' parameters */
		if (!strcasecmp(name1, "maddr")) {
			if (matched) {
				maddrmatch = 1;
			} else {
				goto fail;
			}
		} else if (!strcasecmp(name1, "ttl")) {
			if (matched) {
				ttlmatch = 1;
			} else {
				goto fail;
			}
		} else if (!strcasecmp(name1, "user")) {
			if (matched) {
				usermatch = 1;
			} else {
				goto fail;
			}
		} else if (!strcasecmp(name1, "method")) {
			if (matched) {
				methodmatch = 1;
			} else {
				goto fail;
			}
		}
	}

	/* We've made it out of that horrible O(m*n) construct and there are no
	 * failures yet. We're not done yet, though, because params2 could have
	 * an maddr, ttl, user, or method header and params1 did not.
	 */
	for (pos2 = strsep(&params2, ";"); pos2; pos2 = strsep(&params2, ";")) {
		char *value2 = pos2;
		char *name2 = strsep(&value2, "=");
		if (!value2) {
			value2 = "";
		}
		if ((!strcasecmp(name2, "maddr") && !maddrmatch) ||
				(!strcasecmp(name2, "ttl") && !ttlmatch) ||
				(!strcasecmp(name2, "user") && !usermatch) ||
				(!strcasecmp(name2, "method") && !methodmatch)) {
			goto fail;
		}
	}
	return 0;

fail:
	return 1;
}

/*! \brief helper routine for sip_uri_cmp to compare URI headers
 *
 * This takes the headers from two SIP URIs and determines
 * if the URIs match. The rules for headers is simple. If a header
 * appears in one URI, then it must also appear in the other URI. The
 * order in which the headers appear does not matter.
 *
 * \param input1 Headers from URI 1
 * \param input2 Headers from URI 2
 * \retval 0 URI headers match
 * \retval nonzero URI headers do not match
 */
static int sip_uri_headers_cmp(const char *input1, const char *input2)
{
	char *headers1 = NULL;
	char *headers2 = NULL;
	int zerolength1 = 0;
	int zerolength2 = 0;
	int different = 0;
	char *header1;

	if (ast_strlen_zero(input1)) {
		zerolength1 = 1;
	} else {
		headers1 = ast_strdupa(input1);
	}

	if (ast_strlen_zero(input2)) {
		zerolength2 = 1;
	} else {
		headers2 = ast_strdupa(input2);
	}

	/* If one URI contains no headers and the other
	 * does, then they cannot possibly match
	 */
	if (zerolength1 != zerolength2) {
		return 1;
	}

	if (zerolength1 && zerolength2)
		return 0;

	/* At this point, we can definitively state that both inputs are
	 * not zero-length. First, one more optimization. If the length
	 * of the headers is not equal, then we definitely have no match
	 */
	if (strlen(headers1) != strlen(headers2)) {
		return 1;
	}

	for (header1 = strsep(&headers1, "&"); header1; header1 = strsep(&headers1, "&")) {
		if (!strcasestr(headers2, header1)) {
			different = 1;
			break;
		}
	}

	return different;
}

/*!
 * \brief Compare domain sections of SIP URIs
 *
 * For hostnames, a case insensitive string comparison is
 * used. For IP addresses, a binary comparison is used. This
 * is mainly because IPv6 addresses have many ways of writing
 * the same address.
 *
 * For specifics about IP address comparison, see the following
 * document: http://tools.ietf.org/html/draft-ietf-sip-ipv6-abnf-fix-05
 *
 * \param host1 The domain from the first URI
 * \param host2 THe domain from the second URI
 * \retval 0 The domains match
 * \retval nonzero The domains do not match
 */
static int sip_uri_domain_cmp(const char *host1, const char *host2)
{
	struct ast_sockaddr addr1;
	struct ast_sockaddr addr2;
	int addr1_parsed;
	int addr2_parsed;

	addr1_parsed = ast_sockaddr_parse(&addr1, host1, 0);
	addr2_parsed = ast_sockaddr_parse(&addr2, host2, 0);

	if (addr1_parsed != addr2_parsed) {
		/* One domain was an IP address and the other had
		 * a host name. FAIL!
		 */
		return 1;
	}

	/* Both are host names. A string comparison will work
	 * perfectly here. Specifying the "C" locale ensures that
	 * The LC_CTYPE conventions use those defined in ANSI C,
	 * i.e. ASCII.
	 */
	if (!addr1_parsed) {
#ifdef HAVE_XLOCALE_H
		if(!c_locale) {
			return strcasecmp(host1, host2);
		} else {
			return strcasecmp_l(host1, host2, c_locale);
		}
#else
		return strcasecmp(host1, host2);
#endif
	}

	/* Both contain IP addresses */
	return ast_sockaddr_cmp(&addr1, &addr2);
}

int sip_uri_cmp(const char *input1, const char *input2)
{
	char *uri1;
	char *uri2;
	char *uri_scheme1;
	char *uri_scheme2;
	char *host1;
	char *host2;
	char *params1;
	char *params2;
	char *headers1;
	char *headers2;

	/* XXX It would be really nice if we could just use parse_uri_full() here
	 * to separate the components of the URI, but unfortunately it is written
	 * in a way that can cause URI parameters to be discarded.
	 */

	if (!input1 || !input2) {
		return 1;
	}

	uri1 = ast_strdupa(input1);
	uri2 = ast_strdupa(input2);

	ast_uri_decode(uri1, ast_uri_sip_user);
	ast_uri_decode(uri2, ast_uri_sip_user);

	uri_scheme1 = strsep(&uri1, ":");
	uri_scheme2 = strsep(&uri2, ":");

	if (strcmp(uri_scheme1, uri_scheme2)) {
		return 1;
	}

	/* This function is tailored for SIP and SIPS URIs. There's no
	 * need to check uri_scheme2 since we have determined uri_scheme1
	 * and uri_scheme2 are equivalent already.
	 */
	if (strcmp(uri_scheme1, "sip") && strcmp(uri_scheme1, "sips")) {
		return 1;
	}

	if (ast_strlen_zero(uri1) || ast_strlen_zero(uri2)) {
		return 1;
	}

	if ((host1 = strchr(uri1, '@'))) {
		*host1++ = '\0';
	}
	if ((host2 = strchr(uri2, '@'))) {
		*host2++ = '\0';
	}

	/* Check for mismatched username and passwords. This is the
	 * only case-sensitive comparison of a SIP URI
	 */
	if ((host1 && !host2) ||
			(host2 && !host1) ||
			(host1 && host2 && strcmp(uri1, uri2))) {
		return 1;
	}

	if (!host1) {
		host1 = uri1;
	}
	if (!host2) {
		host2 = uri2;
	}

	/* Strip off the parameters and headers so we can compare
	 * host and port
	 */

	if ((params1 = strchr(host1, ';'))) {
		*params1++ = '\0';
	}
	if ((params2 = strchr(host2, ';'))) {
		*params2++ = '\0';
	}

	/* Headers come after parameters, but there may be headers without
	 * parameters, thus the S_OR
	 */
	if ((headers1 = strchr(S_OR(params1, host1), '?'))) {
		*headers1++ = '\0';
	}
	if ((headers2 = strchr(S_OR(params2, host2), '?'))) {
		*headers2++ = '\0';
	}

	if (sip_uri_domain_cmp(host1, host2)) {
		return 1;
	}

	/* Headers have easier rules to follow, so do those first */
	if (sip_uri_headers_cmp(headers1, headers2)) {
		return 1;
	}

	/* And now the parameters. Ugh */
	return sip_uri_params_cmp(params1, params2);
}

#define URI_CMP_MATCH 0
#define URI_CMP_NOMATCH 1

AST_TEST_DEFINE(sip_uri_cmp_test)
{
	static const struct {
		const char *uri1;
		const char *uri2;
		int expected_result;
	} uri_cmp_tests [] = {
		/* These are identical, so they match */
		{ "sip:bob@example.com", "sip:bob@example.com", URI_CMP_MATCH },
		/* Different usernames. No match */
		{ "sip:alice@example.com", "sip:bob@example.com", URI_CMP_NOMATCH },
		/* Different hosts. No match */
		{ "sip:bob@example.com", "sip:bob@examplez.com", URI_CMP_NOMATCH },
		/* Now start using IP addresses. Identical, so they match */
		{ "sip:bob@1.2.3.4", "sip:bob@1.2.3.4", URI_CMP_MATCH },
		/* Two identical IPv4 addresses represented differently. Match */
		{ "sip:bob@1.2.3.4", "sip:bob@001.002.003.004", URI_CMP_MATCH },
		/* Logically equivalent IPv4 Address and hostname. No Match */
		{ "sip:bob@127.0.0.1", "sip:bob@localhost", URI_CMP_NOMATCH },
		/* Logically equivalent IPv6 address and hostname. No Match */
		{ "sip:bob@[::1]", "sip:bob@localhost", URI_CMP_NOMATCH },
		/* Try an IPv6 one as well */
		{ "sip:bob@[2001:db8::1234]", "sip:bob@[2001:db8::1234]", URI_CMP_MATCH },
		/* Two identical IPv6 addresses represented differently. Match */
		{ "sip:bob@[2001:db8::1234]", "sip:bob@[2001:0db8::1234]", URI_CMP_MATCH },
		/* Different ports. No match */
		{ "sip:bob@1.2.3.4:5060", "sip:bob@1.2.3.4:5061", URI_CMP_NOMATCH },
		/* Same port logically, but only one address specifies it. No match */
		{ "sip:bob@1.2.3.4:5060", "sip:bob@1.2.3.4", URI_CMP_NOMATCH },
		/* And for safety, try with IPv6 */
		{ "sip:bob@[2001:db8:1234]:5060", "sip:bob@[2001:db8:1234]", URI_CMP_NOMATCH },
		/* User comparison is case sensitive. No match */
		{ "sip:bob@example.com", "sip:BOB@example.com", URI_CMP_NOMATCH },
		/* Host comparison is case insensitive. Match */
		{ "sip:bob@example.com", "sip:bob@EXAMPLE.COM", URI_CMP_MATCH },
		/* Add headers to the URI. Identical, so they match */
		{ "sip:bob@example.com?header1=value1&header2=value2", "sip:bob@example.com?header1=value1&header2=value2", URI_CMP_MATCH },
		/* Headers in URI 1 are not in URI 2. No Match */
		{ "sip:bob@example.com?header1=value1&header2=value2", "sip:bob@example.com", URI_CMP_NOMATCH },
		/* Header present in both URIs does not have matching values. No match */
		{ "sip:bob@example.com?header1=value1&header2=value2", "sip:bob@example.com?header1=value1&header2=value3", URI_CMP_NOMATCH },
		/* Add parameters to the URI. Identical so they match */
		{ "sip:bob@example.com;param1=value1;param2=value2", "sip:bob@example.com;param1=value1;param2=value2", URI_CMP_MATCH },
		/* Same parameters in both URIs but appear in different order. Match */
		{ "sip:bob@example.com;param2=value2;param1=value1", "sip:bob@example.com;param1=value1;param2=value2", URI_CMP_MATCH },
		/* params in URI 1 are not in URI 2. Match */
		{ "sip:bob@example.com;param1=value1;param2=value2", "sip:bob@example.com", URI_CMP_MATCH },
		/* param present in both URIs does not have matching values. No match */
		{ "sip:bob@example.com;param1=value1;param2=value2", "sip:bob@example.com;param1=value1;param2=value3", URI_CMP_NOMATCH },
		/* URI 1 has a maddr param but URI 2 does not. No match */
		{ "sip:bob@example.com;param1=value1;maddr=192.168.0.1", "sip:bob@example.com;param1=value1", URI_CMP_NOMATCH },
		/* URI 1 and URI 2 both have identical maddr params. Match */
		{ "sip:bob@example.com;param1=value1;maddr=192.168.0.1", "sip:bob@example.com;param1=value1;maddr=192.168.0.1", URI_CMP_MATCH },
		/* URI 1 is a SIPS URI and URI 2 is a SIP URI. No Match */
		{ "sips:bob@example.com", "sip:bob@example.com", URI_CMP_NOMATCH },
		/* No URI schemes. No match */
		{ "bob@example.com", "bob@example.com", URI_CMP_NOMATCH },
		/* Crashiness tests. Just an address scheme. No match */
		{ "sip", "sips", URI_CMP_NOMATCH },
		/* Still just an address scheme. Even though they're the same, No match */
		{ "sip", "sip", URI_CMP_NOMATCH },
		/* Empty strings. No match */
		{ "", "", URI_CMP_NOMATCH },
		/* An empty string and a NULL. No match */
		{ "", NULL, URI_CMP_NOMATCH },
	};
	int i;
	int test_res = AST_TEST_PASS;
	switch (cmd) {
	case TEST_INIT:
		info->name = "sip_uri_cmp_test";
		info->category = "/channels/chan_sip/";
		info->summary = "Tests comparison of SIP URIs";
		info->description = "Several would-be tricky URI comparisons are performed";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(uri_cmp_tests); ++i) {
		int cmp_res1;
		int cmp_res2;
		if ((cmp_res1 = sip_uri_cmp(uri_cmp_tests[i].uri1, uri_cmp_tests[i].uri2))) {
			/* URI comparison may return -1 or +1 depending on the failure. Standardize
			 * the return value to be URI_CMP_NOMATCH on any failure
			 */
			cmp_res1 = URI_CMP_NOMATCH;
		}
		if (cmp_res1 != uri_cmp_tests[i].expected_result) {
			ast_test_status_update(test, "Unexpected comparison result for URIs %s and %s. "
					"Expected %s but got %s\n", uri_cmp_tests[i].uri1, uri_cmp_tests[i].uri2,
					uri_cmp_tests[i].expected_result == URI_CMP_MATCH ? "Match" : "No Match",
					cmp_res1 == URI_CMP_MATCH ? "Match" : "No Match");
			test_res = AST_TEST_FAIL;
		}

		/* All URI comparisons are commutative, so for the sake of being thorough, we'll
		 * rerun the comparison with the parameters reversed
		 */
		if ((cmp_res2 = sip_uri_cmp(uri_cmp_tests[i].uri2, uri_cmp_tests[i].uri1))) {
			/* URI comparison may return -1 or +1 depending on the failure. Standardize
			 * the return value to be URI_CMP_NOMATCH on any failure
			 */
			cmp_res2 = URI_CMP_NOMATCH;
		}
		if (cmp_res2 != uri_cmp_tests[i].expected_result) {
			ast_test_status_update(test, "Unexpected comparison result for URIs %s and %s. "
					"Expected %s but got %s\n", uri_cmp_tests[i].uri2, uri_cmp_tests[i].uri1,
					uri_cmp_tests[i].expected_result == URI_CMP_MATCH ? "Match" : "No Match",
					cmp_res2 == URI_CMP_MATCH ? "Match" : "No Match");
			test_res = AST_TEST_FAIL;
		}
	}

	return test_res;
}

void free_via(struct sip_via *v)
{
	if (!v) {
		return;
	}

	ast_free(v->via);
	ast_free(v);
}

struct sip_via *parse_via(const char *header)
{
	struct sip_via *v = ast_calloc(1, sizeof(*v));
	char *via, *parm;

	if (!v) {
		return NULL;
	}

	v->via = ast_strdup(header);
	v->ttl = 1;

	via = v->via;

	if (ast_strlen_zero(via)) {
		ast_log(LOG_ERROR, "received request without a Via header\n");
		free_via(v);
		return NULL;
	}

	/* seperate the first via-parm */
	via = strsep(&via, ",");

	/* chop off sent-protocol */
	v->protocol = strsep(&via, " \t\r\n");
	if (ast_strlen_zero(v->protocol)) {
		ast_log(LOG_ERROR, "missing sent-protocol in Via header\n");
		free_via(v);
		return NULL;
	}
	v->protocol = ast_skip_blanks(v->protocol);

	if (via) {
		via = ast_skip_blanks(via);
	}

	/* chop off sent-by */
	v->sent_by = strsep(&via, "; \t\r\n");
	if (ast_strlen_zero(v->sent_by)) {
		ast_log(LOG_ERROR, "missing sent-by in Via header\n");
		free_via(v);
		return NULL;
	}
	v->sent_by = ast_skip_blanks(v->sent_by);

	/* store the port, we have to handle ipv6 addresses containing ':'
	 * characters gracefully */
	if (((parm = strchr(v->sent_by, ']')) && *(++parm) == ':') || (parm = strchr(v->sent_by, ':'))) {
		char *endptr;

		v->port = strtol(++parm, &endptr, 10);
	}

	/* evaluate any via-parms */
	while ((parm = strsep(&via, "; \t\r\n"))) {
		char *c;
		if ((c = strstr(parm, "maddr="))) {
			v->maddr = ast_skip_blanks(c + sizeof("maddr=") - 1);
		} else if ((c = strstr(parm, "branch="))) {
			v->branch = ast_skip_blanks(c + sizeof("branch=") - 1);
		} else if ((c = strstr(parm, "ttl="))) {
			char *endptr;
			c = ast_skip_blanks(c + sizeof("ttl=") - 1);
			v->ttl = strtol(c, &endptr, 10);

			/* make sure we got a valid ttl value */
			if (c == endptr) {
				v->ttl = 1;
			}
		}
	}

	return v;
}

AST_TEST_DEFINE(parse_via_test)
{
	int res = AST_TEST_PASS;
	int i = 1;
	struct sip_via *via;
	struct testdata {
		char *in;
		char *expected_protocol;
		char *expected_branch;
		char *expected_sent_by;
		char *expected_maddr;
		unsigned int expected_port;
		unsigned char expected_ttl;
		int expected_null;
		AST_LIST_ENTRY(testdata) list;
	};
	struct testdata *testdataptr;
	static AST_LIST_HEAD_NOLOCK(testdataliststruct, testdata) testdatalist;
	struct testdata t1 = {
		.in = "SIP/2.0/UDP host:port;branch=thebranch",
		.expected_protocol = "SIP/2.0/UDP",
		.expected_sent_by = "host:port",
		.expected_branch = "thebranch",
	};
	struct testdata t2 = {
		.in = "SIP/2.0/UDP host:port",
		.expected_protocol = "SIP/2.0/UDP",
		.expected_sent_by = "host:port",
		.expected_branch = "",
	};
	struct testdata t3 = {
		.in = "SIP/2.0/UDP",
		.expected_null = 1,
	};
	struct testdata t4 = {
		.in = "BLAH/BLAH/BLAH host:port;branch=",
		.expected_protocol = "BLAH/BLAH/BLAH",
		.expected_sent_by = "host:port",
		.expected_branch = "",
	};
	struct testdata t5 = {
		.in = "SIP/2.0/UDP host:5060;branch=thebranch;maddr=224.0.0.1;ttl=1",
		.expected_protocol = "SIP/2.0/UDP",
		.expected_sent_by = "host:5060",
		.expected_port = 5060,
		.expected_branch = "thebranch",
		.expected_maddr = "224.0.0.1",
		.expected_ttl = 1,
	};
	struct testdata t6 = {
		.in = "SIP/2.0/UDP      host:5060;\n   branch=thebranch;\r\n  maddr=224.0.0.1;   ttl=1",
		.expected_protocol = "SIP/2.0/UDP",
		.expected_sent_by = "host:5060",
		.expected_port = 5060,
		.expected_branch = "thebranch",
		.expected_maddr = "224.0.0.1",
		.expected_ttl = 1,
	};
	struct testdata t7 = {
		.in = "SIP/2.0/UDP [::1]:5060",
		.expected_protocol = "SIP/2.0/UDP",
		.expected_sent_by = "[::1]:5060",
		.expected_port = 5060,
		.expected_branch = "",
	};
	switch (cmd) {
	case TEST_INIT:
		info->name = "parse_via_test";
		info->category = "/channels/chan_sip/";
		info->summary = "Tests parsing the Via header";
		info->description =
				"Runs through various test situations in which various "
				" parameters parameter must be extracted from a VIA header";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	AST_LIST_HEAD_SET_NOLOCK(&testdatalist, &t1);
	AST_LIST_INSERT_TAIL(&testdatalist, &t2, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &t3, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &t4, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &t5, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &t6, list);
	AST_LIST_INSERT_TAIL(&testdatalist, &t7, list);


	AST_LIST_TRAVERSE(&testdatalist, testdataptr, list) {
		via = parse_via(testdataptr->in);
		if (!via) {
		        if (!testdataptr->expected_null) {
				ast_test_status_update(test, "TEST#%d FAILED: VIA = \"%s\"\n"
					"failed to parse header\n",
				i, testdataptr->in);
				res = AST_TEST_FAIL;
			}
			i++;
			continue;
		}

		if (testdataptr->expected_null) {
			ast_test_status_update(test, "TEST#%d FAILED: VIA = \"%s\"\n"
				"successfully parased invalid via header\n",
			i, testdataptr->in);
			res = AST_TEST_FAIL;
			free_via(via);
			i++;
			continue;
		}

		if ((ast_strlen_zero(via->protocol) && !ast_strlen_zero(testdataptr->expected_protocol))
			|| (!ast_strlen_zero(via->protocol) && strcmp(via->protocol, testdataptr->expected_protocol))) {

			ast_test_status_update(test, "TEST#%d FAILED: VIA = \"%s\"\n"
				"parsed protocol = \"%s\"\n"
				"expected = \"%s\"\n"
				"failed to parse protocol\n",
			i, testdataptr->in, via->protocol, testdataptr->expected_protocol);
			res = AST_TEST_FAIL;
		}

		if ((ast_strlen_zero(via->sent_by) && !ast_strlen_zero(testdataptr->expected_sent_by))
			|| (!ast_strlen_zero(via->sent_by) && strcmp(via->sent_by, testdataptr->expected_sent_by))) {

			ast_test_status_update(test, "TEST#%d FAILED: VIA = \"%s\"\n"
				"parsed sent_by = \"%s\"\n"
				"expected = \"%s\"\n"
				"failed to parse sent-by\n",
			i, testdataptr->in, via->sent_by, testdataptr->expected_sent_by);
			res = AST_TEST_FAIL;
		}

		if (testdataptr->expected_port && testdataptr->expected_port != via->port) {
			ast_test_status_update(test, "TEST#%d FAILED: VIA = \"%s\"\n"
				"parsed port = \"%d\"\n"
				"expected = \"%d\"\n"
				"failed to parse port\n",
			i, testdataptr->in, via->port, testdataptr->expected_port);
			res = AST_TEST_FAIL;
		}

		if ((ast_strlen_zero(via->branch) && !ast_strlen_zero(testdataptr->expected_branch))
			|| (!ast_strlen_zero(via->branch) && strcmp(via->branch, testdataptr->expected_branch))) {

			ast_test_status_update(test, "TEST#%d FAILED: VIA = \"%s\"\n"
				"parsed branch = \"%s\"\n"
				"expected = \"%s\"\n"
				"failed to parse branch\n",
			i, testdataptr->in, via->branch, testdataptr->expected_branch);
			res = AST_TEST_FAIL;
		}

		if ((ast_strlen_zero(via->maddr) && !ast_strlen_zero(testdataptr->expected_maddr))
			|| (!ast_strlen_zero(via->maddr) && strcmp(via->maddr, testdataptr->expected_maddr))) {

			ast_test_status_update(test, "TEST#%d FAILED: VIA = \"%s\"\n"
				"parsed maddr = \"%s\"\n"
				"expected = \"%s\"\n"
				"failed to parse maddr\n",
			i, testdataptr->in, via->maddr, testdataptr->expected_maddr);
			res = AST_TEST_FAIL;
		}

		if (testdataptr->expected_ttl && testdataptr->expected_ttl != via->ttl) {
			ast_test_status_update(test, "TEST#%d FAILED: VIA = \"%s\"\n"
				"parsed ttl = \"%d\"\n"
				"expected = \"%d\"\n"
				"failed to parse ttl\n",
			i, testdataptr->in, via->ttl, testdataptr->expected_ttl);
			res = AST_TEST_FAIL;
		}

		free_via(via);
		i++;
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
	AST_TEST_REGISTER(sip_uri_cmp_test);
	AST_TEST_REGISTER(parse_via_test);
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
	AST_TEST_UNREGISTER(sip_uri_cmp_test);
	AST_TEST_UNREGISTER(parse_via_test);
}

int sip_reqresp_parser_init(void)
{
#ifdef HAVE_XLOCALE_H
	c_locale = newlocale(LC_CTYPE_MASK, "C", NULL);
	if (!c_locale) {
		return -1;
	}
#endif
	return 0;
}

void sip_reqresp_parser_exit(void)
{
#ifdef HAVE_XLOCALE_H
	if (c_locale) {
		freelocale(c_locale);
		c_locale = NULL;
	}
#endif
}
