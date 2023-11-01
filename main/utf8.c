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

/*!
 * \warning A UTF-8 sequence could be 1, 2, 3 or 4 bytes long depending
 * on the first byte in the sequence. Don't try to modify this function
 * without understanding how UTF-8 works.
 */

/*
 * The official unicode replacement character is U+FFFD
 * which is actually the 3 following bytes:
 */
#define REPL_SEQ "\xEF\xBF\xBD"
#define REPL_SEQ_LEN 3

enum ast_utf8_replace_result
ast_utf8_replace_invalid_chars(char *dst, size_t *dst_size, const char *src,
	size_t src_len)
{
	enum ast_utf8_replace_result res = AST_UTF8_REPLACE_VALID;
	size_t src_pos = 0;
	size_t dst_pos = 0;
	uint32_t prev_state = UTF8_ACCEPT;
	uint32_t curr_state = UTF8_ACCEPT;
	/*
	* UTF-8 sequences can be 1 - 4 bytes in length so we
	* have to keep track of where we are.
	*/
	int seq_len = 0;

	if (dst) {
		memset(dst, 0, *dst_size);
	} else {
		*dst_size = 0;
	}

	if (!src || src_len == 0) {
		return AST_UTF8_REPLACE_VALID;
	}

	for (prev_state = 0, curr_state = 0; src_pos < src_len; prev_state = curr_state, src_pos++) {
		uint32_t rc;

		rc = decode(&curr_state, (uint8_t) src[src_pos]);

		if (dst && dst_pos >= *dst_size - 1) {
			if (prev_state > UTF8_REJECT) {
				/*
				 * We ran out of space in the middle of a possible
				 * multi-byte sequence so we have to back up and
				 * overwrite the start of the sequence with the
				 * NULL terminator.
				 */
				dst_pos -= (seq_len - (prev_state / 36));
			}
			dst[dst_pos] = '\0';

			return AST_UTF8_REPLACE_OVERRUN;
		}

		if (rc == UTF8_ACCEPT) {
			if (dst) {
				dst[dst_pos] = src[src_pos];
			}
			dst_pos++;
			seq_len = 0;
		}

		if (rc > UTF8_REJECT) {
			/*
			 * We're possibly at the start of, or in the middle of,
			 * a multi-byte sequence. The curr_state will tell us how many
			 * bytes _should_ be remaining in the sequence.
			 */
			if (prev_state == UTF8_ACCEPT) {
				/* If the previous state was a good character then
				 * this can only be the start of s sequence
				 * which is all we care about.
				 */
				seq_len = curr_state / 36 + 1;
			}

			if (dst) {
				dst[dst_pos] = src[src_pos];
			}
			dst_pos++;
		}

		if (rc == UTF8_REJECT) {
			/* We got at least 1 rejection so the string is invalid */
			res = AST_UTF8_REPLACE_INVALID;

			if (prev_state != UTF8_ACCEPT) {
				/*
				 * If we were in a multi-byte sequence and this
				 * byte isn't valid at this time, we'll back
				 * the destination pointer back to the start
				 * of the now-invalid sequence and write the
				 * replacement bytes there.  Then we'll
				 * process the current byte again in the next
				 * loop iteration.  It may be quite valid later.
				 */
				dst_pos -= (seq_len - (prev_state / 36));
				src_pos--;
			}
			if (dst) {
				/*
				 * If we're not just calculating the needed destination
				 * buffer space, and we don't have enough room to write
				 * the replacement sequence, terminate the output
				 * and return.
				 */
				if (dst_pos > *dst_size - 4) {
					dst[dst_pos] = '\0';
					return AST_UTF8_REPLACE_OVERRUN;
				}
				memcpy(&dst[dst_pos], REPL_SEQ, REPL_SEQ_LEN);
			}
			dst_pos += REPL_SEQ_LEN;
			/* Reset the state machine */
			curr_state = UTF8_ACCEPT;
		}
	}

	if (curr_state != UTF8_ACCEPT) {
		/*
		 * We were probably in the middle of a
		 * sequence and ran out of space.
		 */
		res = AST_UTF8_INVALID;
		dst_pos -= (seq_len - (prev_state / 36));
		if (dst) {
			if (dst_pos > *dst_size - 4) {
				dst[dst_pos] = '\0';
				return AST_UTF8_REPLACE_OVERRUN;
			}
			memcpy(&dst[dst_pos], REPL_SEQ, REPL_SEQ_LEN);
		}
		dst_pos += REPL_SEQ_LEN;
	}

	if (dst) {
		dst[dst_pos] = '\0';
	} else {
		*dst_size = dst_pos + 1;
	}

	return res;
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

#include "asterisk/json.h"

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

/*
 * Let the replace function determine how much
 * buffer space is required for the destination.
 */
#define SIZE_REQUIRED 0
/*
 * Set the destination buffer size to the size
 * we expect it to be.  0xDead has no meaning
 * other than it's larger than any test needs
 * a buffer to be.
 */
#define SIZE_EXPECTED 0xDead

static int tracs(int run, const char *src, const char *cmp,
	size_t dst_size, enum ast_utf8_replace_result exp_result)
{
	char *dst = NULL;
	struct ast_json *blob;
	enum ast_utf8_replace_result result;

	if (dst_size == SIZE_REQUIRED) {
		ast_utf8_replace_invalid_chars(dst, &dst_size, src, src ? strlen(src) : 0);
	} else if (dst_size == SIZE_EXPECTED) {
		dst_size = strlen(cmp) + 1;
	}

	dst = (char *)ast_alloca(dst_size);
	result = ast_utf8_replace_invalid_chars(dst, &dst_size, src, src ? strlen(src) : 0);
	if (result != exp_result || strcmp(dst, cmp) != 0) {
		ast_log(LOG_ERROR, "Run: %2d Invalid result. Src: '%s', Dst: '%s', ExpDst: '%s'  Result: %d  ExpResult: %d\n",
			run, src, dst, cmp, result, exp_result);
		return 0;
	}

	/*
	 * The ultimate test: Does jansson accept the result as valid UTF-8?
	 */
	blob = ast_json_pack("{s: s, s: s}",
		"variable", "doesntmatter",
		"value", dst);
	ast_json_unref(blob);

	return blob != NULL;
}

#define ATV(t, v) ast_test_validate(t, v)

AST_TEST_DEFINE(test_utf8_replace_invalid_chars)
{
	const char *src;
	size_t dst_size;
	enum ast_utf8_replace_result result;
	int k = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = "replace_invalid";
		info->category = "/main/utf8/";
		info->summary = "Test ast_utf8_replace_invalid_chars";
		info->description =
			"Tests UTF-8 string copying/replacing code.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

/*
		Table 3-7. Well-Formed UTF-8 Byte Sequences
		Code Points			First	Second	Third	Fourth
							Byte	Byte	Byte	Byte
		U+0000..U+007F		00..7F
		U+0080..U+07FF		C2..DF	80..BF
		U+0800..U+0FFF		E0		A0..BF	80..BF
		U+1000..U+CFFF		E1..EC	80..BF	80..BF
		U+D000..U+D7FF		ED		80..9F	80..BF
		U+E000..U+FFFF		EE..EF	80..BF	80..BF
		U+10000..U+3FFFF	F0		90..BF	80..BF	80..BF
		U+40000..U+FFFFF	F1..F3	80..BF	80..BF	80..BF
		U+100000..U+10FFFF	F4		80..8F	80..BF	80..BF

		Older compilers don't support using the \uXXXX or \UXXXXXXXX
		universal character notation so we have to manually specify
		the byte sequences even for valid UTF-8 sequences.

		These are the ones used for the tests below:

		\u00B0 = \xC2\xB0
		\u0800 = \xE0\xA0\x80
		\uE000 = \xEE\x80\x80
		\U00040000 = \xF1\x80\x80\x80
*/

	/*
	 * Check that NULL destination with a valid source string gives us a
	 * valid result code and buffer size = the length of the input string
	 * plus room for the NULL terminator.
	 */
	src = "ABC\xC2\xB0xyz";
	result = ast_utf8_replace_invalid_chars(NULL, &dst_size, src, src ? strlen(src) : 0);
	ATV(test, result == AST_UTF8_REPLACE_VALID && dst_size == strlen(src) + 1);

	/*
	 * Check that NULL destination with an invalid source string gives us an
	 * invalid result code and buffer size = the length of the input string
	 * plus room for the NULL terminator plus the 2 extra bytes needed for
	 * the one replacement character.
	 */
	src = "ABC\xFFxyz";
	result = ast_utf8_replace_invalid_chars(NULL, &dst_size, src, src ? strlen(src) : 0);
	ATV(test, result == AST_UTF8_REPLACE_INVALID && dst_size == strlen(src) + 3);

	/*
	 * NULL or empty input
	 */
	ATV(test, tracs(__LINE__, NULL, "", 80, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "", "", 80, AST_UTF8_REPLACE_VALID));


	/* Let the replace function calculate the space needed for result */
	k = SIZE_REQUIRED;

	/*
	 * Basic ASCII string
	 */
	ATV(test, tracs(__LINE__, "ABC xyzA", "ABC xyzA", k, AST_UTF8_REPLACE_VALID));

	/*
	 * Mid string.
	 */
	/* good single sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0xyz", "ABC\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "ABC\xE0\xA0\x80xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xF1\x80\x80\x80xyz", "ABC\xF1\x80\x80\x80xyz", k, AST_UTF8_REPLACE_VALID));
	/* good multiple adjacent sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0\xC2\xB0xyz", "ABC\xC2\xB0\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80\xC2\xB0xyz", "ABC\xE0\xA0\x80\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xF1\x80\x80\x80\xC2\xB0xyz", "ABC\xF1\x80\x80\x80\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	/* Bad sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "ABC\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xC2\xC2xyz", "ABC\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xB0xyz", "ABC\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\xC2xyz", "ABC\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\xF5xyz", "ABC\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0xyz", "ABC\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));

	/*
	 * Beginning of string.
	 */
	/* good single sequences */
	ATV(test, tracs(__LINE__, "\xC2\xB0xyz", "\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0\x80xyz", "\xE0\xA0\x80xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "\xF1\x80\x80\x80xyz", "\xF1\x80\x80\x80xyz", k, AST_UTF8_REPLACE_VALID));
	/* good multiple adjacent sequences */
	ATV(test, tracs(__LINE__, "\xC2\xB0\xC2\xB0xyz", "\xC2\xB0\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0\x80\xC2\xB0xyz", "\xE0\xA0\x80\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "\xF1\x80\x80\x80\xC2\xB0xyz", "\xF1\x80\x80\x80\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	/* Bad sequences */
	ATV(test, tracs(__LINE__, "\xC2xyz", "\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xC2\xC2xyz", "\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xB0xyz", "\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0\xC2xyz", "\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0\xF5xyz", "\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0xyz", "\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));

	/*
	 * End of string.
	 */
	/* good single sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0", "ABC\xC2\xB0", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80", "ABC\xE0\xA0\x80", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xF1\x80\x80\x80", "ABC\xF1\x80\x80\x80", k, AST_UTF8_REPLACE_VALID));
	/* good multiple adjacent sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0\xC2\xB0", "ABC\xC2\xB0\xC2\xB0", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80\xC2\xB0", "ABC\xE0\xA0\x80\xC2\xB0", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xF1\x80\x80\x80\xC2\xB0", "ABC\xF1\x80\x80\x80\xC2\xB0", k, AST_UTF8_REPLACE_VALID));
	/* Bad sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2", "ABC\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xC2\xC2", "ABC\xEF\xBF\xBD\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xB0", "ABC\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\xC2", "ABC\xEF\xBF\xBD\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\xF5", "ABC\xEF\xBF\xBD\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0", "ABC\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));


	/* Force destination buffer to be only large enough to hold the expected result */
	k = SIZE_EXPECTED;

	/*
	 * Mid string.
	 */
	/* good single sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0xyz", "ABC\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "ABC\xE0\xA0\x80xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xF1\x80\x80\x80xyz", "ABC\xF1\x80\x80\x80xyz", k, AST_UTF8_REPLACE_VALID));
	/* good multiple adjacent sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0\xC2\xB0xyz", "ABC\xC2\xB0\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80\xC2\xB0xyz", "ABC\xE0\xA0\x80\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xF1\x80\x80\x80\xC2\xB0xyz", "ABC\xF1\x80\x80\x80\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	/* Bad sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "ABC\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xC2\xC2xyz", "ABC\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xB0xyz", "ABC\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\xC2xyz", "ABC\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\xF5xyz", "ABC\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0xyz", "ABC\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));

	/*
	 * Beginning of string.
	 */
	/* good single sequences */
	ATV(test, tracs(__LINE__, "\xC2\xB0xyz", "\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0\x80xyz", "\xE0\xA0\x80xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "\xF1\x80\x80\x80xyz", "\xF1\x80\x80\x80xyz", k, AST_UTF8_REPLACE_VALID));
	/* good multiple adjacent sequences */
	ATV(test, tracs(__LINE__, "\xC2\xB0\xC2\xB0xyz", "\xC2\xB0\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0\x80\xC2\xB0xyz", "\xE0\xA0\x80\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "\xF1\x80\x80\x80\xC2\xB0xyz", "\xF1\x80\x80\x80\xC2\xB0xyz", k, AST_UTF8_REPLACE_VALID));
	/* Bad sequences */
	ATV(test, tracs(__LINE__, "\xC2xyz", "\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xC2\xC2xyz", "\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xB0xyz", "\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0\xC2xyz", "\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0\xF5xyz", "\xEF\xBF\xBD\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "\xE0\xA0xyz", "\xEF\xBF\xBDxyz", k, AST_UTF8_REPLACE_INVALID));

	/*
	 * End of string.
	 */
	/* good single sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0", "ABC\xC2\xB0", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80", "ABC\xE0\xA0\x80", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xF1\x80\x80\x80", "ABC\xF1\x80\x80\x80", k, AST_UTF8_REPLACE_VALID));
	/* good multiple adjacent sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0\xC2\xB0", "ABC\xC2\xB0\xC2\xB0", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80\xC2\xB0", "ABC\xE0\xA0\x80\xC2\xB0", k, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xF1\x80\x80\x80\xC2\xB0", "ABC\xF1\x80\x80\x80\xC2\xB0", k, AST_UTF8_REPLACE_VALID));
	/* Bad sequences */
	ATV(test, tracs(__LINE__, "ABC\xC2", "ABC\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xC2\xC2", "ABC\xEF\xBF\xBD\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xB0", "ABC\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\xC2", "ABC\xEF\xBF\xBD\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\xF5", "ABC\xEF\xBF\xBD\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0", "ABC\xEF\xBF\xBD", k, AST_UTF8_REPLACE_INVALID));


	/*
	 * Overrun Prevention
	 */

	/* No frills. */
	k = 9;
	ATV(test, tracs(__LINE__, "ABC xyzA", "ABC xyzA", k--, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC xyzA", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyzA", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));

	/* good single sequences */
	k = 9;  /* \xC2\xB0 needs 2 bytes */
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0xyz", "ABC\xC2\xB0xyz", k--, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0xyz", "ABC\xC2\xB0xy", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0xyz", "ABC\xC2\xB0x", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0xyz", "ABC\xC2\xB0", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0xyz", "ABC", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0xyz", "ABC", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2\xB0xyz", "AB", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 10; /* \xE0\xA0\x80 needs 3 bytes */
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "ABC\xE0\xA0\x80xyz", k--, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "ABC\xE0\xA0\x80xy", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "ABC\xE0\xA0\x80x", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "ABC\xE0\xA0\x80", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "ABC", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "ABC", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "ABC", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xE0\xA0\x80xyz", "AB", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 10; /* \xEF\xBF\xBD  needs 3 bytes */
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "ABC\xEF\xBF\xBDxyz", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "ABC\xEF\xBF\xBDxy", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "ABC\xEF\xBF\xBDx", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "ABC\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "ABC", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "ABC", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "ABC", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC\xC2xyz", "AB", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 14; /* Each \xEF\xBF\xBD needs 3 bytes */
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xFF", "ABC xyz\xEF\xBF\xBD\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xFF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xFF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xFF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xFF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xFF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xFF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xFF", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xFF", "ABC x", k--, AST_UTF8_REPLACE_OVERRUN));

	/*
	 * The following tests are classed as "Everything including the kitchen sink".
	 * Some tests may be redundant.
	 */
	k = 11;
	ATV(test, tracs(__LINE__, "ABC xyz\xFF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xFF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xFF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xFF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xFF", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 11;
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xB0", "ABC xyz\xC2\xB0", k--, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xB0", "ABC xyz\xC2\xB0", k--, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xB0", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xB0", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2\xB0", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 11;
	ATV(test, tracs(__LINE__, "ABC xyz\xC2", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xC2", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 12;
	ATV(test, tracs(__LINE__, "ABC xyz\xEE\x80\x80", "ABC xyz\xEE\x80\x80", k--, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xEE\x80\x80", "ABC xyz\xEE\x80\x80", k--, AST_UTF8_REPLACE_VALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xEE\x80\x80", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xEE\x80\x80", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xEE\x80\x80", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xEE\x80\x80", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 11;
	ATV(test, tracs(__LINE__, "ABC xyz\xED", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xED", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 14;
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xBF", "ABC xyz\xEF\xBF\xBD\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xBF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xBF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xBF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xBF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xBF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xBF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xBF", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xBF", "ABC x", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 14;
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xFF", "ABC xyz\xEF\xBF\xBD\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xFF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xFF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xFF", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xFF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xFF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xFF", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xFF", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xFF", "ABC x", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 14;
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2", "ABC xyz\xEF\xBF\xBD\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2", "ABC x", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 14;
	ATV(test, tracs(__LINE__, "ABC xyz\xED\x80\xC0", "ABC xyz\xEF\xBF\xBD\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\x80\xC0", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\x80\xC0", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\x80\xC0", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\x80\xC0", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\x80\xC0", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\x80\xC0", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\x80\xC0", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\x80\xC0", "ABC x", k--, AST_UTF8_REPLACE_OVERRUN));

	k = 13;
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2\xB0", "ABC xyz\xEF\xBF\xBD\xC2\xB0", k--, AST_UTF8_REPLACE_INVALID));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2\xB0", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2\xB0", "ABC xyz\xEF\xBF\xBD", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2\xB0", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2\xB0", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2\xB0", "ABC xyz", k--, AST_UTF8_REPLACE_OVERRUN));
	ATV(test, tracs(__LINE__, "ABC xyz\xED\xC2\xB0", "ABC xy", k--, AST_UTF8_REPLACE_OVERRUN));

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
	AST_TEST_UNREGISTER(test_utf8_replace_invalid_chars);
}

int ast_utf8_init(void)
{
	AST_TEST_REGISTER(test_utf8_is_valid);
	AST_TEST_REGISTER(test_utf8_copy_string);
	AST_TEST_REGISTER(test_utf8_validator);
	AST_TEST_REGISTER(test_utf8_replace_invalid_chars);

	ast_register_cleanup(test_utf8_shutdown);

	return 0;
}

#else /* !TEST_FRAMEWORK */

int ast_utf8_init(void)
{
	return 0;
}

#endif
