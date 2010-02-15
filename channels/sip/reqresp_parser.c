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
int parse_uri(char *uri, const char *scheme, char **ret_name, char **pass, char **domain, char **port, char **transport)
{
	char *name = NULL;
	char *tmp; /* used as temporary place holder */
	int error = 0;

	/* check for valid input */
	if (ast_strlen_zero(uri)) {
		return -1;
	}

	/* strip [?headers] from end of uri */
	if ((tmp = strrchr(uri, '?'))) {
		*tmp = '\0';
	}

	/* init field as required */
	if (pass)
		*pass = "";
	if (port)
		*port = "";
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
	if (transport) {
		char *t, *type = "";
		*transport = "";
		if ((t = strstr(uri, "transport="))) {
			strsep(&t, "=");
			if ((type = strsep(&t, ";"))) {
				*transport = type;
			}
		}
	}

	if (!domain) {
		/* if we don't want to split around domain, keep everything as a name,
		 * so we need to do nothing here, except remember why.
		 */
	} else {
		/* store the result in a temp. variable to avoid it being
		 * overwritten if arguments point to the same place.
		 */
		char *c, *dom = "";

		if ((c = strchr(uri, '@')) == NULL) {
			/* domain-only URI, according to the SIP RFC. */
			dom = uri;
			name = "";
		} else {
			*c++ = '\0';
			dom = c;
			name = uri;
		}

		/* Remove parameters in domain and name */
		dom = strsep(&dom, ";");
		name = strsep(&name, ";");

		if (port && (c = strchr(dom, ':'))) { /* Remove :port */
			*c++ = '\0';
			*port = c;
		}
		if (pass && (c = strchr(name, ':'))) {	/* user:password */
			*c++ = '\0';
			*pass = c;
		}
		*domain = dom;
	}
	if (ret_name)	/* same as for domain, store the result only at the end */
		*ret_name = name;

	return error;
}

AST_TEST_DEFINE(sip_parse_uri_test)
{
	int res = AST_TEST_PASS;
	char *name, *pass, *domain, *port, *transport;
	char uri1[] = "sip:name@host";
	char uri2[] = "sip:name@host;transport=tcp";
	char uri3[] = "sip:name:secret@host;transport=tcp";
	char uri4[] = "sip:name:secret@host:port;transport=tcp?headers=%40%40testblah&headers2=blah%20blah";
	/* test 5 is for NULL input */
	char uri6[] = "sip:name:secret@host:port;transport=tcp?headers=%40%40testblah&headers2=blah%20blah";
	char uri7[] = "sip:name:secret@host:port;transport=tcp?headers=%40%40testblah&headers2=blah%20blah";

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
	name = pass = domain = port = transport = NULL;
	if (parse_uri(uri1, "sip:,sips:", &name, &pass, &domain, &port, &transport) ||
			strcmp(name, "name")        ||
			!ast_strlen_zero(pass)      ||
			strcmp(domain, "host")      ||
			!ast_strlen_zero(port)      ||
			!ast_strlen_zero(transport)) {
		ast_test_status_update(test, "Test 1: simple uri failed. \n");
		res = AST_TEST_FAIL;
	}

	/* Test 2, add tcp transport */
	name = pass = domain = port = transport = NULL;
	if (parse_uri(uri2, "sip:,sips:", &name, &pass, &domain, &port, &transport) ||
			strcmp(name, "name")        ||
			!ast_strlen_zero(pass)      ||
			strcmp(domain, "host")    ||
			!ast_strlen_zero(port)      ||
			strcmp(transport, "tcp")) {
		ast_test_status_update(test, "Test 2: uri with addtion of tcp transport failed. \n");
		res = AST_TEST_FAIL;
	}

	/* Test 3, add secret */
	name = pass = domain = port = transport = NULL;
	if (parse_uri(uri3, "sip:,sips:", &name, &pass, &domain, &port, &transport) ||
			strcmp(name, "name")        ||
			strcmp(pass, "secret")      ||
			strcmp(domain, "host")    ||
			!ast_strlen_zero(port)      ||
			strcmp(transport, "tcp")) {
		ast_test_status_update(test, "Test 3: uri with addition of secret failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 4, add port and unparsed header field*/
	name = pass = domain = port = transport = NULL;
	if (parse_uri(uri4, "sip:,sips:", &name, &pass, &domain, &port, &transport) ||
			strcmp(name, "name")        ||
			strcmp(pass, "secret")      ||
			strcmp(domain, "host")    ||
			strcmp(port, "port")      ||
			strcmp(transport, "tcp")) {
		ast_test_status_update(test, "Test 4: add port and unparsed header field failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 5, verify parse_uri does not crash when given a NULL uri */
	name = pass = domain = port = transport = NULL;
	if (!parse_uri(NULL, "sip:,sips:", &name, &pass, &domain, &port, &transport)) {
		ast_test_status_update(test, "Test 5: passing a NULL uri failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 6, verify parse_uri does not crash when given a NULL output parameters */
	name = pass = domain = port = transport = NULL;
	if (parse_uri(uri6, "sip:,sips:", NULL, NULL, NULL, NULL, NULL)) {
		ast_test_status_update(test, "Test 6: passing NULL output parameters failed.\n");
		res = AST_TEST_FAIL;
	}

	/* Test 7, verify parse_uri returns user:secret and domain:port when no port or secret output parameters are supplied. */
	name = pass = domain = port = transport = NULL;
	if (parse_uri(uri7, "sip:,sips:", &name, NULL, &domain, NULL, NULL) ||
			strcmp(name, "name:secret")        ||
			strcmp(domain, "host:port")) {

		ast_test_status_update(test, "Test 7: providing no port and secret output parameters failed.\n");
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
	if (parse_uri(tmp_number, "sip:,sips:", &tmp_number, &dummy, &domain, &dummy, NULL) || ast_strlen_zero(tmp_number)) {
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

char *get_in_brackets(char *tmp)
{
	const char *parse = tmp;
	char *first_bracket;


	if (ast_strlen_zero(tmp)) {
		return tmp;
	}

	/*
	 * Skip any quoted text until we find the part in brackets.
	* On any error give up and return the full string.
	*/
	while ( (first_bracket = strchr(parse, '<')) ) {
		char *first_quote = strchr(parse, '"');

		if (!first_quote || first_quote > first_bracket)
			break; /* no need to look at quoted part */
		/* the bracket is within quotes, so ignore it */
		parse = find_closing_quote(first_quote + 1, NULL);
		if (!*parse) {
			ast_log(LOG_WARNING, "No closing quote found in '%s'\n", tmp);
			break;
		}
		parse++;
	}
	if (first_bracket) {
		char *second_bracket = strchr(first_bracket + 1, '>');
		if (second_bracket) {
			*second_bracket = '\0';
			tmp = first_bracket + 1;
		} else {
			ast_log(LOG_WARNING, "No closing bracket found in '%s'\n", tmp);
		}
	}

	return tmp;
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


	return res;
}

void sip_request_parser_register_tests(void)
{
	AST_TEST_REGISTER(get_calleridname_test);
	AST_TEST_REGISTER(sip_parse_uri_test);
	AST_TEST_REGISTER(get_in_brackets_test);
	AST_TEST_REGISTER(get_name_and_number_test);
}
void sip_request_parser_unregister_tests(void)
{
	AST_TEST_UNREGISTER(sip_parse_uri_test);
	AST_TEST_UNREGISTER(get_calleridname_test);
	AST_TEST_UNREGISTER(get_in_brackets_test);
	AST_TEST_UNREGISTER(get_name_and_number_test);
}
