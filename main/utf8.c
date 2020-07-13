/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sean Bright
 *
 * Sean Bright <sean.bright@gmail.com>
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
 * \brief UTF-8 information and validation functions
 */

/*** MODULEINFO
	 <support_level>core</support_level>
***/

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/utf8.h"
#include "asterisk/test.h"

/*
 * BEGIN THIRD PARTY CODE
 *
 * Copyright (c) 2008-2010 Björn Höhrmann <bjoern@hoehrmann.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
 */

#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

static const uint8_t utf8d[] = {
	/* The first part of the table maps bytes to character classes that
	 * to reduce the size of the transition table and create bitmasks. */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

	/* The second part is a transition table that maps a combination
	 * of a state of the automaton and a character class to a state. */
	0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
	12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
	12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
	12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
	12,36,12,12,12,12,12,12,12,12,12,12,
};

#if 0
/* We can bring this back if we need the codepoint? */
static uint32_t inline decode(uint32_t *state, uint32_t *codep, uint32_t byte) {
	uint32_t type = utf8d[byte];

	*codep = (*state != UTF8_ACCEPT) ?
		(byte & 0x3fu) | (*codep << 6) :
		(0xff >> type) & (byte);

	*state = utf8d[256 + *state + type];
	return *state;
}
#endif

static uint32_t inline decode(uint32_t *state, uint32_t byte) {
	uint32_t type = utf8d[byte];
	*state = utf8d[256 + *state + type];
	return *state;
}

/*
 * END THIRD PARTY CODE
 *
 * See copyright notice above.
 */

int ast_utf8_is_valid(const char *src)
{
	uint32_t state = UTF8_ACCEPT;

	while (*src) {
		decode(&state, (uint8_t) *src++);
	}

	return state == UTF8_ACCEPT;
}

int ast_utf8_is_validn(const char *src, size_t size)
{
	uint32_t state = UTF8_ACCEPT;

	while (size && *src) {
		decode(&state, (uint8_t) *src++);
		size--;
	}

	return state == UTF8_ACCEPT;
}

void ast_utf8_copy_string(char *dst, const char *src, size_t size)
{
	uint32_t state = UTF8_ACCEPT;
	char *last_good = dst;

	ast_assert(size > 0);

	while (size && *src) {
		if (decode(&state, (uint8_t) *src) == UTF8_REJECT) {
			/* We _could_ replace with U+FFFD and try to recover, but for now
			 * we treat this the same as if we had run out of space */
			break;
		}

		*dst++ = *src++;
		size--;

		if (size && state == UTF8_ACCEPT) {
			/* last_good is where we will ultimately write the 0 byte */
			last_good = dst;
		}
	}

	*last_good = '\0';
}

struct ast_utf8_validator {
	uint32_t state;
};

int ast_utf8_validator_new(struct ast_utf8_validator **validator)
{
	struct ast_utf8_validator *tmp = ast_malloc(sizeof(*tmp));

	if (!tmp) {
		return 1;
	}

	tmp->state = UTF8_ACCEPT;
	*validator = tmp;
	return 0;
}

enum ast_utf8_validation_result ast_utf8_validator_state(
	struct ast_utf8_validator *validator)
{
	switch (validator->state) {
	case UTF8_ACCEPT:
		return AST_UTF8_VALID;
	case UTF8_REJECT:
		return AST_UTF8_INVALID;
	default:
		return AST_UTF8_UNKNOWN;
	}
}

enum ast_utf8_validation_result ast_utf8_validator_feed(
	struct ast_utf8_validator *validator, const char *data)
{
	while (*data) {
		decode(&validator->state, (uint8_t) *data++);
	}

	return ast_utf8_validator_state(validator);
}

enum ast_utf8_validation_result ast_utf8_validator_feedn(
	struct ast_utf8_validator *validator, const char *data, size_t size)
{
	while (size && *data) {
		decode(&validator->state, (uint8_t) *data++);
		size--;
	}

	return ast_utf8_validator_state(validator);
}

void ast_utf8_validator_reset(struct ast_utf8_validator *validator)
{
	validator->state = UTF8_ACCEPT;
}

void ast_utf8_validator_destroy(struct ast_utf8_validator *validator)
{
	ast_free(validator);
}

#ifdef TEST_FRAMEWORK

AST_TEST_DEFINE(test_utf8_is_valid)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "is_valid";
		info->category = "/main/utf8/";
		info->summary = "Test ast_utf8_is_valid and ast_utf8_is_validn";
		info->description =
			"Tests UTF-8 string validation code.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	/* Valid UTF-8 */
	ast_test_validate(test, ast_utf8_is_valid("Asterisk"));
	ast_test_validate(test, ast_utf8_is_valid("\xce\xbb"));
	ast_test_validate(test, ast_utf8_is_valid("\xe2\x8a\x9b"));
	ast_test_validate(test, ast_utf8_is_valid("\xf0\x9f\x93\x9e"));

	/* Valid with leading */
	ast_test_validate(test, ast_utf8_is_valid("aaa Asterisk"));
	ast_test_validate(test, ast_utf8_is_valid("aaa \xce\xbb"));
	ast_test_validate(test, ast_utf8_is_valid("aaa \xe2\x8a\x9b"));
	ast_test_validate(test, ast_utf8_is_valid("aaa \xf0\x9f\x93\x9e"));

	/* Valid with trailing */
	ast_test_validate(test, ast_utf8_is_valid("Asterisk aaa"));
	ast_test_validate(test, ast_utf8_is_valid("\xce\xbb aaa"));
	ast_test_validate(test, ast_utf8_is_valid("\xe2\x8a\x9b aaa"));
	ast_test_validate(test, ast_utf8_is_valid("\xf0\x9f\x93\x9e aaa"));

	/* Valid with leading and trailing */
	ast_test_validate(test, ast_utf8_is_valid("aaa Asterisk aaa"));
	ast_test_validate(test, ast_utf8_is_valid("aaa \xce\xbb aaa"));
	ast_test_validate(test, ast_utf8_is_valid("aaa \xe2\x8a\x9b aaa"));
	ast_test_validate(test, ast_utf8_is_valid("aaa \xf0\x9f\x93\x9e aaa"));

	/* Valid if limited by number of bytes */
	ast_test_validate(test, ast_utf8_is_validn("Asterisk" "\xff", strlen("Asterisk")));
	ast_test_validate(test, ast_utf8_is_validn("\xce\xbb" "\xff", strlen("\xce\xbb")));
	ast_test_validate(test, ast_utf8_is_validn("\xe2\x8a\x9b" "\xff", strlen("\xe2\x8a\x9b")));
	ast_test_validate(test, ast_utf8_is_validn("\xf0\x9f\x93\x9e" "\xff", strlen("\xf0\x9f\x93\x9e")));

	/* Invalid */
	ast_test_validate(test, !ast_utf8_is_valid("\xc0\x8a")); /* Overlong */
	ast_test_validate(test, !ast_utf8_is_valid("98.6\xa7")); /* 'High ASCII' */
	ast_test_validate(test, !ast_utf8_is_valid("\xc3\x28"));
	ast_test_validate(test, !ast_utf8_is_valid("\xa0\xa1"));
	ast_test_validate(test, !ast_utf8_is_valid("\xe2\x28\xa1"));
	ast_test_validate(test, !ast_utf8_is_valid("\xe2\x82\x28"));
	ast_test_validate(test, !ast_utf8_is_valid("\xf0\x28\x8c\xbc"));
	ast_test_validate(test, !ast_utf8_is_valid("\xf0\x90\x28\xbc"));
	ast_test_validate(test, !ast_utf8_is_valid("\xf0\x28\x8c\x28"));

	return AST_TEST_PASS;
}

static int test_copy_and_compare(const char *src, size_t dst_len, const char *cmp)
{
	char dst[dst_len];
	ast_utf8_copy_string(dst, src, dst_len);
	return strcmp(dst, cmp) == 0;
}

AST_TEST_DEFINE(test_utf8_copy_string)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "copy_string";
		info->category = "/main/utf8/";
		info->summary = "Test ast_utf8_copy_string";
		info->description =
			"Tests UTF-8 string copying code.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, test_copy_and_compare("Asterisk",           6, "Aster"));
	ast_test_validate(test, test_copy_and_compare("Asterisk \xc2\xae", 11, "Asterisk "));
	ast_test_validate(test, test_copy_and_compare("Asterisk \xc2\xae", 12, "Asterisk \xc2\xae"));
	ast_test_validate(test, test_copy_and_compare("Asterisk \xc0\x8a", 12, "Asterisk "));
	ast_test_validate(test, test_copy_and_compare("\xce\xbb xyz", 1, ""));
	ast_test_validate(test, test_copy_and_compare("\xce\xbb xyz", 2, ""));
	ast_test_validate(test, test_copy_and_compare("\xce\xbb xyz", 3, "\xce\xbb"));
	ast_test_validate(test, test_copy_and_compare("\xce\xbb xyz", 4, "\xce\xbb "));
	ast_test_validate(test, test_copy_and_compare("\xce\xbb xyz", 5, "\xce\xbb x"));
	ast_test_validate(test, test_copy_and_compare("\xce\xbb xyz", 6, "\xce\xbb xy"));
	ast_test_validate(test, test_copy_and_compare("\xce\xbb xyz", 7, "\xce\xbb xyz"));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_utf8_validator)
{
	struct ast_utf8_validator *validator;

	switch (cmd) {
	case TEST_INIT:
		info->name = "utf8_validator";
		info->category = "/main/utf8/";
		info->summary = "Test ast_utf8_validator";
		info->description =
			"Tests UTF-8 progressive validator code.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_utf8_validator_new(&validator)) {
		return AST_TEST_FAIL;
	}

	ast_test_validate(test, ast_utf8_validator_feed(validator, "Asterisk") == AST_UTF8_VALID);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "\xc2")     == AST_UTF8_UNKNOWN);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "\xae")     == AST_UTF8_VALID);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "Private")  == AST_UTF8_VALID);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "Branch")   == AST_UTF8_VALID);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "Exchange") == AST_UTF8_VALID);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "\xe2")     == AST_UTF8_UNKNOWN);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "\x84")     == AST_UTF8_UNKNOWN);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "\xbb")     == AST_UTF8_VALID);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "\xc0\x8a") == AST_UTF8_INVALID);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "valid")    == AST_UTF8_INVALID);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "valid")    == AST_UTF8_INVALID);
	ast_test_validate(test, ast_utf8_validator_feed(validator, "valid")    == AST_UTF8_INVALID);

	ast_utf8_validator_destroy(validator);

	return AST_TEST_PASS;
}

static void test_utf8_shutdown(void)
{
	AST_TEST_UNREGISTER(test_utf8_is_valid);
	AST_TEST_UNREGISTER(test_utf8_copy_string);
	AST_TEST_UNREGISTER(test_utf8_validator);
}

int ast_utf8_init(void)
{
	AST_TEST_REGISTER(test_utf8_is_valid);
	AST_TEST_REGISTER(test_utf8_copy_string);
	AST_TEST_REGISTER(test_utf8_validator);

	ast_register_cleanup(test_utf8_shutdown);

	return 0;
}

#else /* !TEST_FRAMEWORK */

int ast_utf8_init(void)
{
	return 0;
}

#endif
