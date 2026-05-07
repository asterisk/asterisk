/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Joshua Elson
 *
 * Joshua Elson <joshelson@gmail.com>
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
 * \brief res_odbc unit tests
 *
 * \author Joshua Elson <joshelson@gmail.com>
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_odbc</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/res_odbc.h"
#include "asterisk/test.h"

#define CATEGORY "/res/res_odbc/"

static struct ast_category *append_category(struct ast_config *cfg, const char *name)
{
	struct ast_category *cat;

	cat = ast_category_new(name, "", 0);
	if (!cat) {
		return NULL;
	}
	ast_category_append(cfg, cat);
	return cat;
}

static int append_var(struct ast_category *cat, const char *name, const char *value)
{
	struct ast_variable *var;

	var = ast_variable_new(name, value, "");
	if (!var) {
		return -1;
	}
	ast_variable_append(cat, var);
	return 0;
}

static struct ast_config *config_with_class(struct ast_test *test, const char *class_name,
	struct ast_category **class_cat)
{
	struct ast_config *cfg;

	cfg = ast_config_new();
	if (!cfg) {
		ast_test_status_update(test, "Failed to allocate ast_config\n");
		return NULL;
	}

	*class_cat = append_category(cfg, class_name);
	if (!*class_cat) {
		ast_test_status_update(test, "Failed to allocate category '%s'\n", class_name);
		ast_config_destroy(cfg);
		return NULL;
	}

	return cfg;
}

static int parse_class_config(struct ast_test *test, struct ast_config *cfg,
	const char *class_name, struct ast_odbc_test_class_config *out)
{
	int res;

	res = ast_odbc_test_parse_ast_config(cfg, class_name, out);
	if (res) {
		ast_test_status_update(test, "Parsing class '%s' returned %d\n", class_name, res);
		return -1;
	}

	return 0;
}

struct iso_text2enum_case {
	const char *input;
	int expected;
	const char *note;
};

AST_TEST_DEFINE(text2isolation_canonical_values)
{
	static const struct iso_text2enum_case cases[] = {
		{ "read_committed",   SQL_TXN_READ_COMMITTED,   "exact lower" },
		{ "read_uncommitted", SQL_TXN_READ_UNCOMMITTED, "exact lower" },
		{ "serializable",     SQL_TXN_SERIALIZABLE,     "exact lower" },
		{ "repeatable_read",  SQL_TXN_REPEATABLE_READ,  "exact lower" },
		{ "READ_COMMITTED",   SQL_TXN_READ_COMMITTED,   "uppercase"   },
		{ "Read_Committed",   SQL_TXN_READ_COMMITTED,   "mixed case"  },
	};
	size_t i;
	int failed = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "ast_odbc_text2isolation: canonical values";
		info->description =
			"Verify ast_odbc_text2isolation maps the four documented "
			"isolation level names to the matching SQL_TXN_* constants, "
			"and that matching is case-insensitive.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(cases); i++) {
		int got = ast_odbc_text2isolation(cases[i].input);
		if (got != cases[i].expected) {
			ast_test_status_update(test,
				"input '%s' (%s): got %d, expected %d\n",
				cases[i].input, cases[i].note, got, cases[i].expected);
			failed = 1;
		}
	}

	return failed ? AST_TEST_FAIL : AST_TEST_PASS;
}

AST_TEST_DEFINE(text2isolation_loose_prefix_matching)
{
	/*
	 * The legacy parser does loose-prefix matching:
	 *   "read_" + 'c'-anything → READ_COMMITTED
	 *   "read_" + 'u'-anything → READ_UNCOMMITTED
	 *   "ser*"                 → SERIALIZABLE
	 *   "rep*"                 → REPEATABLE_READ
	 * This is part of the published contract since at least Asterisk 1.6
	 * and is preserved deliberately. If the migration tightens it, this
	 * test should be updated alongside an UpgradeNote.
	 */
	static const struct iso_text2enum_case cases[] = {
		{ "read_c",            SQL_TXN_READ_COMMITTED,   "loose 'read_c'"   },
		{ "read_u",            SQL_TXN_READ_UNCOMMITTED, "loose 'read_u'"   },
		{ "ser",               SQL_TXN_SERIALIZABLE,     "minimal 'ser'"    },
		{ "rep",               SQL_TXN_REPEATABLE_READ,  "minimal 'rep'"    },
		{ "serpent",           SQL_TXN_SERIALIZABLE,     "loose 'serpent'"  },
		{ "reptile",           SQL_TXN_REPEATABLE_READ,  "loose 'reptile'"  },
		{ "READ_C",            SQL_TXN_READ_COMMITTED,   "uppercase loose"  },
		{ "READ_U",            SQL_TXN_READ_UNCOMMITTED, "uppercase loose"  },
	};
	size_t i;
	int failed = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "ast_odbc_text2isolation: loose-prefix matching";
		info->description =
			"Pin down the loose-prefix matching contract documented in "
			"res_odbc.c:152. 'reptile' resolving to REPEATABLE_READ and "
			"'serpent' resolving to SERIALIZABLE is intentional, not a "
			"bug. The migration to config_options must consciously "
			"preserve these mappings.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(cases); i++) {
		int got = ast_odbc_text2isolation(cases[i].input);
		if (got != cases[i].expected) {
			ast_test_status_update(test,
				"input '%s' (%s): got %d, expected %d\n",
				cases[i].input, cases[i].note, got, cases[i].expected);
			failed = 1;
		}
	}

	return failed ? AST_TEST_FAIL : AST_TEST_PASS;
}

AST_TEST_DEFINE(text2isolation_unrecognized_returns_zero)
{
	static const char *const inputs[] = {
		"",
		"totally_bogus",
		"read_zebra",   /* "read_" + non-c/non-u */
		"committed",    /* missing "read_" prefix */
		"uncommitted",
		"snapshot",     /* not a recognized prefix */
	};
	size_t i;
	int failed = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "ast_odbc_text2isolation: unrecognized inputs return 0";
		info->description =
			"Inputs that don't match any prefix branch must return 0. "
			"The caller in load_odbc_config treats 0 as 'unrecognized "
			"value' and falls back to READ_COMMITTED with an ERROR log "
			"line, so a regression here would silently change error-"
			"handling behavior.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(inputs); i++) {
		int got = ast_odbc_text2isolation(inputs[i]);
		if (got != 0) {
			ast_test_status_update(test,
				"input '%s': got %d, expected 0\n", inputs[i], got);
			failed = 1;
		}
	}

	return failed ? AST_TEST_FAIL : AST_TEST_PASS;
}

AST_TEST_DEFINE(isolation2text_canonical_values)
{
	struct {
		int input;
		const char *expected;
	} cases[] = {
		{ SQL_TXN_READ_COMMITTED,   "read_committed"   },
		{ SQL_TXN_READ_UNCOMMITTED, "read_uncommitted" },
		{ SQL_TXN_SERIALIZABLE,     "serializable"     },
		{ SQL_TXN_REPEATABLE_READ,  "repeatable_read"  },
		{ 0,                        "unknown"          },
		{ 0x7FFFFFFF,               "unknown"          },
	};
	size_t i;
	int failed = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "ast_odbc_isolation2text: canonical values + unknown";
		info->description =
			"Round-trip mapping from SQL_TXN_* constants to lowercase "
			"text labels. Unrecognized integer values must return the "
			"literal string \"unknown\" — used by `odbc show` and any "
			"diagnostic output, so a regression here is user-visible.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(cases); i++) {
		const char *got = ast_odbc_isolation2text(cases[i].input);
		if (strcmp(got, cases[i].expected) != 0) {
			ast_test_status_update(test,
				"input %d: got '%s', expected '%s'\n",
				cases[i].input, got, cases[i].expected);
			failed = 1;
		}
	}

	return failed ? AST_TEST_FAIL : AST_TEST_PASS;
}

AST_TEST_DEFINE(isolation_round_trip)
{
	static const int values[] = {
		SQL_TXN_READ_COMMITTED,
		SQL_TXN_READ_UNCOMMITTED,
		SQL_TXN_SERIALIZABLE,
		SQL_TXN_REPEATABLE_READ,
	};
	size_t i;
	int failed = 0;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "isolation enum round-trips through 2text→2isolation";
		info->description =
			"For every documented SQL_TXN_* value, isolation2text(v) "
			"must produce a string that text2isolation parses back to v. "
			"Without this invariant, the `odbc show` output would be "
			"unparseable as input to a future re-load.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; i < ARRAY_LEN(values); i++) {
		const char *as_text = ast_odbc_isolation2text(values[i]);
		int back = ast_odbc_text2isolation(as_text);
		if (back != values[i]) {
			ast_test_status_update(test,
				"value %d → '%s' → %d (round-trip broken)\n",
				values[i], as_text, back);
			failed = 1;
		}
	}

	return failed ? AST_TEST_FAIL : AST_TEST_PASS;
}

AST_TEST_DEFINE(config_options_explicit_values)
{
	RAII_VAR(struct ast_config *, cfg, NULL, ast_config_destroy);
	struct ast_category *cat;
	struct ast_odbc_test_class_config parsed;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "res_odbc ACO parser: explicit values";
		info->description =
			"Verify the config_options conversion maps explicit res_odbc.conf "
			"options into the parsed class snapshot.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cfg = config_with_class(test, "primary", &cat);
	ast_test_validate(test, cfg != NULL);
	ast_test_validate(test, append_var(cat, "enabled", "yes") == 0);
	ast_test_validate(test, append_var(cat, "dsn", "pg-main") == 0);
	ast_test_validate(test, append_var(cat, "username", "asterisk") == 0);
	ast_test_validate(test, append_var(cat, "password", "secret") == 0);
	ast_test_validate(test, append_var(cat, "sanitysql", "select 42") == 0);
	ast_test_validate(test, append_var(cat, "pre-connect", "yes") == 0);
	ast_test_validate(test, append_var(cat, "backslash_is_escape", "no") == 0);
	ast_test_validate(test, append_var(cat, "forcecommit", "yes") == 0);
	ast_test_validate(test, append_var(cat, "logging", "yes") == 0);
	ast_test_validate(test, append_var(cat, "connect_timeout", "7") == 0);
	ast_test_validate(test, append_var(cat, "max_connections", "12") == 0);
	ast_test_validate(test, append_var(cat, "slow_query_limit", "42") == 0);
	ast_test_validate(test, append_var(cat, "isolation", "repeatable_read") == 0);
	ast_test_validate(test, append_var(cat, "cache_type", "rr") == 0);
	ast_test_validate(test, append_var(cat, "cache_size", "3") == 0);
	ast_test_validate(test, append_var(cat, "negative_connection_cache", "2.5") == 0);
	ast_test_validate(test, parse_class_config(test, cfg, "primary", &parsed) == 0);

	ast_test_validate(test, !strcmp(parsed.name, "primary"));
	ast_test_validate(test, !strcmp(parsed.dsn, "pg-main"));
	ast_test_validate(test, !strcmp(parsed.username, "asterisk"));
	ast_test_validate(test, !strcmp(parsed.password, "secret"));
	ast_test_validate(test, !strcmp(parsed.sanitysql, "select 42"));
	ast_test_validate(test, parsed.enabled != 0);
	ast_test_validate(test, parsed.preconnect != 0);
	ast_test_validate(test, parsed.backslash_is_escape == 0);
	ast_test_validate(test, parsed.forcecommit != 0);
	ast_test_validate(test, parsed.logging != 0);
	ast_test_validate(test, parsed.conntimeout == 7);
	ast_test_validate(test, parsed.maxconnections == 12);
	ast_test_validate(test, parsed.slowquerylimit == 42);
	ast_test_validate(test, parsed.isolation == SQL_TXN_REPEATABLE_READ);
	ast_test_validate(test, parsed.cache_is_queue == 1);
	ast_test_validate(test, parsed.max_cache_size == 3);
	ast_test_validate(test, parsed.negative_connection_cache_sec == 2);
	ast_test_validate(test, parsed.negative_connection_cache_usec == 500000);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(config_options_defaults_and_legacy_unknowns)
{
	RAII_VAR(struct ast_config *, cfg, NULL, ast_config_destroy);
	struct ast_category *cat;
	struct ast_odbc_test_class_config parsed;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "res_odbc ACO parser: defaults and unknown-option tolerance";
		info->description =
			"Verify parser defaults and that unknown option names — including "
			"the four obsolete pool-related names — do not fail the parse. "
			"Unknown names are warned about (so operator typos are visible) "
			"and discarded so the rest of the class still registers.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cfg = config_with_class(test, "defaults", &cat);
	ast_test_validate(test, cfg != NULL);
	ast_test_validate(test, append_var(cat, "dsn", "pg-default") == 0);
	ast_test_validate(test, append_var(cat, "site_local_option", "ignored") == 0);
	ast_test_validate(test, append_var(cat, "pooling", "yes") == 0);
	ast_test_validate(test, append_var(cat, "shared_connections", "yes") == 0);
	ast_test_validate(test, append_var(cat, "limit", "4") == 0);
	ast_test_validate(test, append_var(cat, "idlecheck", "30") == 0);
	ast_test_validate(test, parse_class_config(test, cfg, "defaults", &parsed) == 0);

	ast_test_validate(test, !strcmp(parsed.dsn, "pg-default"));
	ast_test_validate(test, parsed.enabled != 0);
	ast_test_validate(test, parsed.preconnect == 0);
	ast_test_validate(test, parsed.backslash_is_escape != 0);
	ast_test_validate(test, parsed.forcecommit == 0);
	ast_test_validate(test, parsed.logging == 0);
	ast_test_validate(test, parsed.conntimeout == 10);
	ast_test_validate(test, parsed.maxconnections == 1);
	ast_test_validate(test, parsed.slowquerylimit == 5000);
	ast_test_validate(test, parsed.isolation == SQL_TXN_READ_COMMITTED);
	ast_test_validate(test, parsed.cache_is_queue == 0);
	ast_test_validate(test, parsed.max_cache_size == UINT_MAX);
	ast_test_validate(test, parsed.negative_connection_cache_sec == 0);
	ast_test_validate(test, parsed.negative_connection_cache_usec == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(config_options_invalid_values_fall_back)
{
	RAII_VAR(struct ast_config *, cfg, NULL, ast_config_destroy);
	struct ast_category *cat;
	struct ast_odbc_test_class_config parsed;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "res_odbc ACO parser: invalid values fall back";
		info->description =
			"Verify invalid non-fatal option values retain the legacy "
			"fallback behavior instead of rejecting the whole class.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	cfg = config_with_class(test, "bad", &cat);
	ast_test_validate(test, cfg != NULL);
	ast_test_validate(test, append_var(cat, "dsn", "pg-bad") == 0);
	ast_test_validate(test, append_var(cat, "connect_timeout", "0") == 0);
	ast_test_validate(test, append_var(cat, "max_connections", "0") == 0);
	ast_test_validate(test, append_var(cat, "slow_query_limit", "not-a-number") == 0);
	ast_test_validate(test, append_var(cat, "isolation", "snapshot") == 0);
	ast_test_validate(test, append_var(cat, "cache_type", "stack") == 0);
	ast_test_validate(test, append_var(cat, "cache_size", "not-a-number") == 0);
	ast_test_validate(test, append_var(cat, "negative_connection_cache", "-1") == 0);
	ast_test_validate(test, parse_class_config(test, cfg, "bad", &parsed) == 0);

	ast_test_validate(test, parsed.conntimeout == 10);
	ast_test_validate(test, parsed.maxconnections == 1);
	ast_test_validate(test, parsed.slowquerylimit == 5000);
	ast_test_validate(test, parsed.isolation == SQL_TXN_READ_COMMITTED);
	ast_test_validate(test, parsed.cache_is_queue == 0);
	ast_test_validate(test, parsed.max_cache_size == UINT_MAX);
	ast_test_validate(test, parsed.negative_connection_cache_sec == 300);
	ast_test_validate(test, parsed.negative_connection_cache_usec == 0);

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(config_options_env_sections)
{
	RAII_VAR(struct ast_config *, cfg, NULL, ast_config_destroy);
	struct ast_category *cat;
	struct ast_odbc_test_class_config parsed;
	const char *env_one;
	const char *env_two;

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "res_odbc ACO parser: repeated ENV sections";
		info->description =
			"Verify the manual [ENV] handling keeps the legacy behavior of "
			"processing each repeated ENV section.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	unsetenv("AST_ODBC_TEST_ENV_ONE");
	unsetenv("AST_ODBC_TEST_ENV_TWO");

	cfg = ast_config_new();
	ast_test_validate(test, cfg != NULL);
	cat = append_category(cfg, "ENV");
	ast_test_validate(test, cat != NULL);
	ast_test_validate(test, append_var(cat, "AST_ODBC_TEST_ENV_ONE", "alpha") == 0);
	cat = append_category(cfg, "class");
	ast_test_validate(test, cat != NULL);
	ast_test_validate(test, append_var(cat, "dsn", "pg-env") == 0);
	cat = append_category(cfg, "env");
	ast_test_validate(test, cat != NULL);
	ast_test_validate(test, append_var(cat, "AST_ODBC_TEST_ENV_TWO", "beta") == 0);
	ast_test_validate(test, parse_class_config(test, cfg, "class", &parsed) == 0);

	env_one = getenv("AST_ODBC_TEST_ENV_ONE");
	env_two = getenv("AST_ODBC_TEST_ENV_TWO");
	ast_test_validate(test, env_one && !strcmp(env_one, "alpha"));
	ast_test_validate(test, env_two && !strcmp(env_two, "beta"));
	ast_test_validate(test, !strcmp(parsed.dsn, "pg-env"));

	unsetenv("AST_ODBC_TEST_ENV_ONE");
	unsetenv("AST_ODBC_TEST_ENV_TWO");

	return AST_TEST_PASS;
}

static int load_module(void)
{
	AST_TEST_REGISTER(text2isolation_canonical_values);
	AST_TEST_REGISTER(text2isolation_loose_prefix_matching);
	AST_TEST_REGISTER(text2isolation_unrecognized_returns_zero);
	AST_TEST_REGISTER(isolation2text_canonical_values);
	AST_TEST_REGISTER(isolation_round_trip);
	AST_TEST_REGISTER(config_options_explicit_values);
	AST_TEST_REGISTER(config_options_defaults_and_legacy_unknowns);
	AST_TEST_REGISTER(config_options_invalid_values_fall_back);
	AST_TEST_REGISTER(config_options_env_sections);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(config_options_env_sections);
	AST_TEST_UNREGISTER(config_options_invalid_values_fall_back);
	AST_TEST_UNREGISTER(config_options_defaults_and_legacy_unknowns);
	AST_TEST_UNREGISTER(config_options_explicit_values);
	AST_TEST_UNREGISTER(isolation_round_trip);
	AST_TEST_UNREGISTER(isolation2text_canonical_values);
	AST_TEST_UNREGISTER(text2isolation_unrecognized_returns_zero);
	AST_TEST_UNREGISTER(text2isolation_loose_prefix_matching);
	AST_TEST_UNREGISTER(text2isolation_canonical_values);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "res_odbc test module",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_odbc",
);
