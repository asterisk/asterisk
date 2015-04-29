/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Matthew Nicholson <mnichiolson@digium.com>
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
 * \brief Test ast_format_str_reduce
 *
 * \author Matthew Nicholson <mnichiolson@digium.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<use type="module">format_g723</use>
	<use type="module">format_g726</use>
	<use type="module">format_g729</use>
	<use type="module">format_gsm</use>
	<use type="module">format_ogg_vorbis</use>
	<use type="module">format_pcm</use>
	<use type="module">format_siren14</use>
	<use type="module">format_siren7</use>
	<use type="module">format_sln</use>
	<use type="module">format_wav</use>
	<use type="module">format_wav_gsm</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/file.h"
#include "asterisk/test.h"

/* this is an array containing a list of strings to test and the expected
 * result for each test string.  The list should be terminated by an entry
 * containing NULL for both elements {NULL, NULL}) */
static char *test_strings[][2] = {
	{"wav",                       "wav"},
	{"wav|ulaw",                  "wav|ulaw"},
	{"pcm|wav",                   "pcm|wav"},
	{"pcm|wav|ulaw",              "pcm|wav"},
	{"wav|ulaw|pcm",              "wav|ulaw"},
	{"wav|ulaw|pcm|alaw",         "wav|ulaw|alaw"},
	{"pcm|ulaw|ul|mu|ulw",        "pcm"},
	{"wav|ulaw|pcm|alaw|sln|raw", "wav|ulaw|alaw|sln"},
	{"wav|gsm|wav49",             "wav|gsm|wav49"},
	{"WAV|gsm|wav49",             "WAV|gsm"},
	{"wav|invalid|gsm",           "wav|gsm"},
	{"invalid|gsm",               "gsm"},
	{"ulaw|gsm|invalid",          "ulaw|gsm"},
	{"g723|g726-40|g729|gsm|ilbc|ogg|wav|WAV|siren7|siren14|sln", "g723|g726-40|g729|gsm|ilbc|ogg|wav|WAV|siren7|siren14"},
	{NULL, NULL},
};

/* this is a NULL terminated array containing a list of strings that should
 * cause ast_format_str_reduce() to fail */
static char *fail_strings[] = {
	"this will fail",            /* format does not exist */
	"this one|should|fail also", /* format does not exist */
	NULL,
};

AST_TEST_DEFINE(ast_format_str_reduce_test_1)
{
	int i;
	char *c;

	switch (cmd) {
	case TEST_INIT:
		info->name = "ast_format_str_reduce_test_1";
		info->category = "/main/file/";
		info->summary = "reduce format strings";
		info->description = "Reduce some format strings and make sure the results match what we expect.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; test_strings[i][0]; i++) {
		c = ast_strdupa(test_strings[i][0]);
		if (!(c = ast_format_str_reduce(c))) {
			ast_test_status_update(test, "Error running ast_format_str_reduce() on string '%s'\n",
					test_strings[i][0]);
			return AST_TEST_FAIL;
		}

		if (strcmp(test_strings[i][1], c)) {
			ast_test_status_update(test, "Format string '%s' reduced to '%s'.  Expected '%s'\n",
					test_strings[i][0], c, test_strings[i][1]);
			return AST_TEST_FAIL;
		}
	}

	for (i = 0; fail_strings[i]; i++) {
		c = ast_strdupa(fail_strings[i]);
		if ((c = ast_format_str_reduce(c))) {
			ast_test_status_update(test, "ast_format_str_reduce() succeded on string '%s' "
					"with result '%s', but we expected it to fail\n",
					fail_strings[i], c);
			return AST_TEST_FAIL;
		}
	}

	return AST_TEST_PASS;
}

static int load_module(void)
{

	AST_TEST_REGISTER(ast_format_str_reduce_test_1);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_AUTOCLEAN(ASTERISK_GPL_KEY, "ast_format_str_reduce() test module");
