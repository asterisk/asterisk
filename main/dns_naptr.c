/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief DNS NAPTR Record Support
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <arpa/nameser.h>
#include <netinet/in.h>
#include <resolv.h>
#include <regex.h>

#include "asterisk/dns_core.h"
#include "asterisk/dns_naptr.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dns_internal.h"
#include "asterisk/utils.h"

/*!
 * \brief Result of analyzing NAPTR flags on a record
 */
enum flags_result {
	/*! Terminal record, meaning the DDDS algorithm can be stopped */
	FLAGS_TERMINAL,
	/*! No flags provided, likely meaning another NAPTR lookup */
	FLAGS_EMPTY,
	/*! Unrecognized but valid flags. We cannot conclude what they mean */
	FLAGS_UNKNOWN,
	/*! Non-alphanumeric or invalid combination of flags */
	FLAGS_INVALID,
};

/*!
 * \brief Analyze and interpret NAPTR flags as per RFC 3404
 *
 * \note The flags string passed into this function is NOT NULL-terminated
 *
 * \param flags The flags string from a NAPTR record
 * \flags_size The size of the flags string in bytes
 * \return flag result
 */
static enum flags_result interpret_flags(const char *flags, uint8_t flags_size)
{
	int i;
	char known_flag_found = 0;

	if (flags_size == 0) {
		return FLAGS_EMPTY;
	}

	/* Take care of the most common (and easy) case, one character */
	if (flags_size == 1) {
		if (*flags == 's' || *flags == 'S' ||
				*flags == 'a' || *flags == 'A' ||
				*flags == 'u' || *flags == 'U') {
			return FLAGS_TERMINAL;
		} else if (!isalnum(*flags)) {
			return FLAGS_INVALID;
		} else {
			return FLAGS_UNKNOWN;
		}
	}

	/*
	 * Multiple flags are allowed, but you cannot mix the
	 * S, A, U, and P flags together.
	 */
	for (i = 0; i < flags_size; ++i) {
		if (!isalnum(flags[i])) {
			return FLAGS_INVALID;
		} else if (flags[i] == 's' || flags[i] == 'S') {
			if (known_flag_found && known_flag_found != 's') {
				return FLAGS_INVALID;
			}
			known_flag_found = 's';
		} else if (flags[i] == 'u' || flags[i] == 'U') {
			if (known_flag_found && known_flag_found != 'u') {
				return FLAGS_INVALID;
			}
			known_flag_found = 'u';
		} else if (flags[i] == 'a' || flags[i] == 'A') {
			if (known_flag_found && known_flag_found != 'a') {
				return FLAGS_INVALID;
			}
			known_flag_found = 'a';
		} else if (flags[i] == 'p' || flags[i] == 'P') {
			if (known_flag_found && known_flag_found != 'p') {
				return FLAGS_INVALID;
			}
			known_flag_found = 'p';
		}
	}

	return (!known_flag_found || known_flag_found == 'p') ? FLAGS_UNKNOWN : FLAGS_TERMINAL;
}

/*!
 * \brief Analyze NAPTR services for validity as defined by RFC 3404
 *
 * \note The services string passed to this function is NOT NULL-terminated
 * \param services The services string parsed from a NAPTR record
 * \param services_size The size of the services string
 * \retval 0 Services are valid
 * \retval -1 Services are invalid
 */
static int services_invalid(const char *services, uint8_t services_size)
{
	const char *current_pos = services;
	const char *end_of_services = services + services_size;

	if (services_size == 0) {
		return 0;
	}

	/* Services are broken into sections divided by a + sign. Each section
	 * must start with an alphabetic character, and then can only contain
	 * alphanumeric characters. The size of any section is limited to
	 * 32 characters
	 */
	while (1) {
		char *plus_pos = memchr(current_pos, '+', end_of_services - current_pos);
		uint8_t current_size = plus_pos ? plus_pos - current_pos : end_of_services - current_pos;
		int i;

		if (!isalpha(current_pos[0])) {
			return -1;
		}

		if (current_size > 32) {
			return -1;
		}

		for (i = 1; i < current_size; ++i) {
			if (!isalnum(current_pos[i])) {
				return -1;
			}
		}

		if (!plus_pos) {
			break;
		}
		current_pos = plus_pos + 1;
	}

	return 0;
}

/*!
 * \brief Determine if flags in the regexp are invalid
 *
 * A NAPTR regexp is structured like so
 * /pattern/repl/FLAGS
 *
 * This ensures that the flags on the regexp are valid. Regexp
 * flags can either be zero or one character long. If the flags
 * are one character long, that character must be "i" to indicate
 * the regex evaluation is case-insensitive.
 *
 * \note The flags string passed to this function is not NULL-terminated
 * \param flags The regexp flags from the NAPTR record
 * \param end A pointer to the end of the flags string
 * \retval 0 Flags are valid
 * \retval -1 Flags are invalid
 */
static int regexp_flags_invalid(const char *flags, const char *end)
{
	if (flags >= end) {
		return 0;
	}

	if (end - flags > 1) {
		return -1;
	}

	if (*flags != 'i') {
		return -1;
	}

	return 0;
}

/*!
 * \brief Determine if the replacement in the regexp is invalid
 *
 * A NAPTR regexp is structured like so
 * /pattern/REPL/flags
 *
 * This ensures that the replacement on the regexp is valid. The regexp
 * replacement is free to use any character it wants, plus backreferences
 * and an escaped regexp delimiter.
 *
 * This function does not attempt to ensure that the backreferences refer
 * to valid portions of the regexp's regex pattern.
 *
 * \note The repl string passed to this function is NOT NULL-terminated
 *
 * \param repl The regexp replacement string
 * \param end Pointer to the end of the replacement string
 * \param delim The delimiter character for the regexp
 *
 * \retval 0 Replacement is valid
 * \retval -1 Replacement is invalid
 */
static int regexp_repl_invalid(const char *repl, const char *end, char delim)
{
	const char *ptr = repl;

	if (repl == end) {
		/* Kind of weird, but this is fine */
		return 0;
	}

	while (1) {
		char *backslash_pos = memchr(ptr, '\\', end - ptr);
		if (!backslash_pos) {
			break;
		}

		ast_assert(backslash_pos < end - 1);

		/* XXX RFC 3402 is unclear about whether other backslash-escaped characters
		 * (such as a backslash-escaped backslash) are legal
		 */
		if (!strchr("12345689", backslash_pos[1]) && backslash_pos[1] != delim) {
			return -1;
		}

		ptr = backslash_pos + 1;
	}

	return 0;
}

/*!
 * \brief Determine if the pattern in a regexp is invalid
 *
 * A NAPTR regexp is structured like so
 * /PATTERN/repl/flags
 *
 * This ensures that the pattern on the regexp is valid. The pattern is
 * passed to a regex compiler to determine its validity.
 *
 * \note The pattern string passed to this function is NOT NULL-terminated
 *
 * \param pattern The pattern from the NAPTR record
 * \param end A pointer to the end of the pattern
 *
 * \retval 0 Pattern is valid
 * \retval non-zero Pattern is invalid
 */
static int regexp_pattern_invalid(const char *pattern, const char *end)
{
	int pattern_size = end - pattern;
	char pattern_str[pattern_size + 1];
	regex_t reg;
	int res;

	/* regcomp requires a NULL-terminated string */
	memcpy(pattern_str, pattern, pattern_size);
	pattern_str[pattern_size] = '\0';

	res = regcomp(&reg, pattern_str, REG_EXTENDED);

	regfree(&reg);

	return res;
}

/*!
 * \brief Determine if the regexp in a NAPTR record is invalid
 *
 * The goal of this function is to divide the regexp into its
 * constituent parts and then let validation subroutines determine
 * if each part is valid. If all parts are valid, then the entire
 * regexp is valid.
 *
 * \note The regexp string passed to this function is NOT NULL-terminated
 *
 * \param regexp The regexp from the NAPTR record
 * \param regexp_size The size of the regexp string
 *
 * \retval 0 regexp is valid
 * \retval non-zero regexp is invalid
 */
static int regexp_invalid(const char *regexp, uint8_t regexp_size)
{
	char delim;
	const char *delim2_pos;
	const char *delim3_pos;
	const char *ptr = regexp;
	const char *end_of_regexp = regexp + regexp_size;
	const char *regex_pos;
	const char *repl_pos;
	const char *flags_pos;

	if (regexp_size == 0) {
		return 0;
	}

	/* The delimiter will be a ! or / in most cases, but the rules allow
	 * for the delimiter to be nearly any character. It cannot be 'i' because
	 * the delimiter cannot be the same as regexp flags. The delimiter cannot
	 * be 1-9 because the delimiter cannot be a backreference number. RFC
	 * 2915 specified that backslash was also not allowed as a delimiter, but
	 * RFC 3402 does not say this. We've gone ahead and made the character
	 * illegal for our purposes.
	 */
	delim = *ptr;
	if (strchr("123456789\\i", delim)) {
		return -1;
	}
	++ptr;
	regex_pos = ptr;

	/* Find the other two delimiters. If the delim is escaped with a backslash, it doesn't count */
	while (1) {
		delim2_pos = memchr(ptr, delim, end_of_regexp - ptr);
		if (!delim2_pos) {
			return -1;
		}
		ptr = delim2_pos + 1;
		if (delim2_pos[-1] != '\\') {
			break;
		}
	}

	if (ptr >= end_of_regexp) {
		return -1;
	}

	repl_pos = ptr;

	while (1) {
		delim3_pos = memchr(ptr, delim, end_of_regexp - ptr);
		if (!delim3_pos) {
			return -1;
		}
		ptr = delim3_pos + 1;
		if (delim3_pos[-1] != '\\') {
			break;
		}
	}
	flags_pos = ptr;

	if (regexp_flags_invalid(flags_pos, end_of_regexp) ||
			regexp_repl_invalid(repl_pos, delim3_pos, delim) ||
			regexp_pattern_invalid(regex_pos, delim2_pos)) {
		return -1;
	}

	return 0;
}

#define PAST_END_OF_RECORD ptr >= end_of_record

struct ast_dns_record *dns_naptr_alloc(struct ast_dns_query *query, const char *data, const size_t size)
{
	struct ast_dns_naptr_record *naptr;
	char *ptr = NULL;
	uint16_t order;
	uint16_t preference;
	uint8_t flags_size;
	char *flags;
	uint8_t services_size;
	char *services;
	uint8_t regexp_size;
	char *regexp;
	char replacement[256] = "";
	int replacement_size;
	const char *end_of_record;
	enum flags_result flags_res;
	size_t naptr_len;

	ptr = dns_find_record(data, size, query->result->answer, query->result->answer_size);
	ast_assert(ptr != NULL);

	end_of_record = ptr + size;

	/* ORDER */
	/* This assignment takes a big-endian 16-bit value and stores it in the
	 * machine's native byte order. Using this method allows us to avoid potential
	 * alignment issues in case the order is not on a short-addressable boundary.
	 * See http://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html for
	 * more information
	 */
	ptr += dns_parse_short((unsigned char *) ptr, &order);
	if (PAST_END_OF_RECORD) {
		return NULL;
	}

	/* PREFERENCE */
	ptr += dns_parse_short((unsigned char *) ptr, &preference);
	if (PAST_END_OF_RECORD) {
		return NULL;
	}

	/* FLAGS */
	ptr += dns_parse_string(ptr, &flags_size, &flags);
	if (PAST_END_OF_RECORD) {
		return NULL;
	}

	/* SERVICES */
	ptr += dns_parse_string(ptr, &services_size, &services);
	if (PAST_END_OF_RECORD) {
		return NULL;
	}

	/* REGEXP */
	ptr += dns_parse_string(ptr, &regexp_size, &regexp);
	if (PAST_END_OF_RECORD) {
		return NULL;
	}

	/*
	 * The return value from dn_expand represents the size of the replacement
	 * in the buffer which MAY be compressed.  Since the expanded replacement
	 * is NULL terminated, you can use strlen() to get the expanded size.
	 */
	replacement_size = dn_expand((unsigned char *)query->result->answer,
		(unsigned char *) end_of_record, (unsigned char *) ptr,
		replacement, sizeof(replacement) - 1);
	if (replacement_size < 0) {
		ast_log(LOG_ERROR, "Failed to expand domain name: %s\n", strerror(errno));
		return NULL;
	}

	ptr += replacement_size;

	if (ptr != end_of_record) {
		ast_log(LOG_ERROR, "NAPTR record gave undersized string indications.\n");
		return NULL;
	}

	/* We've validated the size of the NAPTR record. Now we can validate
	 * the individual parts
	 */
	flags_res = interpret_flags(flags, flags_size);
	if (flags_res == FLAGS_INVALID) {
		ast_log(LOG_ERROR, "NAPTR Record contained invalid flags %.*s\n", flags_size, flags);
		return NULL;
	}

	if (services_invalid(services, services_size)) {
		ast_log(LOG_ERROR, "NAPTR record contained invalid services %.*s\n", services_size, services);
		return NULL;
	}

	if (regexp_invalid(regexp, regexp_size)) {
		ast_log(LOG_ERROR, "NAPTR record contained invalid regexp %.*s\n", regexp_size, regexp);
		return NULL;
	}

	/* replacement_size takes into account the NULL label, so a NAPTR record with no replacement
	 * will have a replacement_size of 1.
	 */
	if (regexp_size && replacement_size > 1) {
		ast_log(LOG_ERROR, "NAPTR record contained both a regexp and replacement\n");
		return NULL;
	}

	naptr_len = sizeof(*naptr) + size + flags_size + 1 + services_size + 1
		+ regexp_size + 1 + strlen(replacement) + 1;
	naptr = ast_calloc(1, naptr_len);
	if (!naptr) {
		return NULL;
	}

	naptr->order = order;
	naptr->preference = preference;

	ptr = naptr->data;
	ptr += size;

	strncpy(ptr, flags, flags_size);
	ptr[flags_size] = '\0';
	naptr->flags = ptr;
	ptr += flags_size + 1;

	strncpy(ptr, services, services_size);
	ptr[services_size] = '\0';
	naptr->service = ptr;
	ptr += services_size + 1;

	strncpy(ptr, regexp, regexp_size);
	ptr[regexp_size] = '\0';
	naptr->regexp = ptr;
	ptr += regexp_size + 1;

	strcpy(ptr, replacement);
	naptr->replacement = ptr;

	naptr->generic.data_ptr = naptr->data;

	return (struct ast_dns_record *)naptr;
}


static int compare_order(const void *record1, const void *record2)
{
	const struct ast_dns_naptr_record **left = (const struct ast_dns_naptr_record **)record1;
	const struct ast_dns_naptr_record **right = (const struct ast_dns_naptr_record **)record2;

	if ((*left)->order < (*right)->order) {
		return -1;
	} else if ((*left)->order > (*right)->order) {
		return 1;
	} else {
		return 0;
	}
}

static int compare_preference(const void *record1, const void *record2)
{
	const struct ast_dns_naptr_record **left = (const struct ast_dns_naptr_record **)record1;
	const struct ast_dns_naptr_record **right = (const struct ast_dns_naptr_record **)record2;

	if ((*left)->preference < (*right)->preference) {
		return -1;
	} else if ((*left)->preference > (*right)->preference) {
		return 1;
	} else {
		return 0;
	}
}

void dns_naptr_sort(struct ast_dns_result *result)
{
	struct ast_dns_record *current;
	size_t num_records = 0;
	struct ast_dns_naptr_record **records;
	int i = 0;
	int j = 0;
	int cur_order;

	/* Determine the number of records */
	AST_LIST_TRAVERSE(&result->records, current, list) {
		++num_records;
	}

	/* No point in continuing if there are no records */
	if (num_records == 0) {
		return;
	}

	/* Allocate an array with that number of records */
	records = ast_alloca(num_records * sizeof(*records));

	/* Move records from the list to the array */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&result->records, current, list) {
		records[i++] = (struct ast_dns_naptr_record *) current;
		AST_LIST_REMOVE_CURRENT(list);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* Sort the array by order */
	qsort(records, num_records, sizeof(*records), compare_order);

	/* Sort subarrays by preference */
	for (i = 0; i < num_records; i = j) {
		cur_order = records[i]->order;
		for (j = i + 1; j < num_records; ++j) {
			if (records[j]->order != cur_order) {
				break;
			}
		}
		qsort(&records[i], j - i, sizeof(*records), compare_preference);
	}

	/* Place sorted records back into the original list */
	for (i = 0; i < num_records; ++i) {
		AST_LIST_INSERT_TAIL(&result->records, (struct ast_dns_record *)(records[i]), list);
	}
}

const char *ast_dns_naptr_get_flags(const struct ast_dns_record *record)
{
	struct ast_dns_naptr_record *naptr = (struct ast_dns_naptr_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == T_NAPTR);
	return naptr->flags;
}

const char *ast_dns_naptr_get_service(const struct ast_dns_record *record)
{
	struct ast_dns_naptr_record *naptr = (struct ast_dns_naptr_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == T_NAPTR);
	return naptr->service;
}

const char *ast_dns_naptr_get_regexp(const struct ast_dns_record *record)
{
	struct ast_dns_naptr_record *naptr = (struct ast_dns_naptr_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == T_NAPTR);
	return naptr->regexp;
}

const char *ast_dns_naptr_get_replacement(const struct ast_dns_record *record)
{
	struct ast_dns_naptr_record *naptr = (struct ast_dns_naptr_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == T_NAPTR);
	return naptr->replacement;
}

unsigned short ast_dns_naptr_get_order(const struct ast_dns_record *record)
{
	struct ast_dns_naptr_record *naptr = (struct ast_dns_naptr_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == T_NAPTR);
	return naptr->order;
}

unsigned short ast_dns_naptr_get_preference(const struct ast_dns_record *record)
{
	struct ast_dns_naptr_record *naptr = (struct ast_dns_naptr_record *) record;

	ast_assert(ast_dns_record_get_rr_type(record) == T_NAPTR);
	return naptr->preference;
}
