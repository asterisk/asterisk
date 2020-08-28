/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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

#include "asterisk.h"

#include "asterisk/cli.h"
#include "asterisk/conversions.h"
#include "asterisk/logger_category.h"
#include "asterisk/vector.h"

struct category_t {
	int sublevel;
	uintmax_t id;
	char name[0];
};

AST_VECTOR_RW(categories_t, struct category_t *);

struct categories_level_t {
	int type;
	int sublevel;
	uintmax_t id_pool;
	uintmax_t state;
	struct categories_t categories;
};

/*! \brief Retrieve the next available id.
 *
 * Ids must be a power of 2. This allows for fast lookup, and "or'ing" of ids
 * in order to permit multiple categories in a log statement.
 */
static uintmax_t get_next_id(struct categories_level_t *level)
{
	if (level->id_pool == 0) {
		level->id_pool = 1;
	} else if (level->id_pool >= (UINTMAX_MAX / 2)) {
		/* No more ids left*/
		return 0;
	} else {
		level->id_pool <<= 1;
	}

	return level->id_pool;
}

static int cmp_by_name(const struct category_t *category, const char *name)
{
	return !strcmp(category->name, name);
}

static uintmax_t category_register(struct categories_level_t *level, const char *name)
{
	int i;
	struct category_t *category;

	AST_VECTOR_RW_WRLOCK(&level->categories);

	i = AST_VECTOR_GET_INDEX(&level->categories, name, cmp_by_name);
	if (i >= 0) {
		AST_VECTOR_RW_UNLOCK(&level->categories);
		ast_log(LOG_ERROR, "Cannot register logger category '%s'. "
				"Name already used for type.\n", name);
		return 0;
	}

	category = ast_calloc(1, sizeof(*category) + strlen(name) + 1);
	if (!category) {
		AST_VECTOR_RW_UNLOCK(&level->categories);
		return 0;
	}

	category->id = get_next_id(level);
	category->sublevel = AST_LOG_CATEGORY_DISABLED;
	strcpy(category->name, name); /* Safe */

	if (AST_VECTOR_APPEND(&level->categories, category)) {
		AST_VECTOR_RW_UNLOCK(&level->categories);
		ast_log(LOG_ERROR, "Cannot register logger category '%s'. "
				"Unable to append.\n", name);
		return 0;
	}

	AST_VECTOR_RW_UNLOCK(&level->categories);
	return category->id;
}

static int category_unregister(struct categories_level_t *level, const char *name)
{
	int res;

	AST_VECTOR_RW_WRLOCK(&level->categories);
	res = AST_VECTOR_REMOVE_CMP_UNORDERED(&level->categories, name, cmp_by_name, ast_free);
	AST_VECTOR_RW_UNLOCK(&level->categories);

	return res;
}

static int casecmp_by_name(const struct category_t *category, const char *name)
{
	return !strcasecmp(category->name, name);
}

static int category_set_sublevel(struct category_t *category, struct categories_level_t *level,
	const char *name, int sublevel)
{
	int locked = 0;

	if (!category) {
		struct category_t **obj;

		if (!name) {
			return -1;
		}

		locked = !AST_VECTOR_RW_WRLOCK(&level->categories);
		if (!locked) {
			return -1;
		}

		obj = AST_VECTOR_GET_CMP(&level->categories, name, casecmp_by_name);
		if (!obj) {
			AST_VECTOR_RW_UNLOCK(&level->categories);
			return -1;
		}

		category = *obj;
	}

	category->sublevel = sublevel;

	if (category->sublevel == AST_LOG_CATEGORY_DISABLED) {
		level->state &= ~category->id;
	} else {
		level->state |= category->id;
	}

	if (locked) {
		AST_VECTOR_RW_UNLOCK(&level->categories);
	}

	return 0;
}

static int category_set_sublevels(struct categories_level_t *level,
	const char * const *names, size_t size, int default_sublevel)
{
	int i;

	if (!names || !size) {
		level->state = default_sublevel;
		AST_VECTOR_RW_WRLOCK(&level->categories);
		AST_VECTOR_CALLBACK_VOID(&level->categories, category_set_sublevel,
			level, NULL, default_sublevel);
		AST_VECTOR_RW_UNLOCK(&level->categories);
		return 0;
	}

	for (i = 0; i < size; ++i) {
		const char *sublevel;
		int num = default_sublevel;

		sublevel = strchr(names[i], ':');
		if (sublevel) {
			size_t len = ++sublevel - names[i];
			char name[len];

			if (*sublevel && ast_str_to_int(sublevel, &num)) {
				continue;
			}

			ast_copy_string(name, names[i], len);
			category_set_sublevel(NULL, level, name, num);
		} else {
			category_set_sublevel(NULL, level, names[i], num);
		}
	}

	return 0;
}

static char *category_complete(struct categories_level_t *level, const char * const *argv,
	int argc, const char *word, int state)
{
	int wordlen = strlen(word);
	int which = 0;
	int i, j;

	AST_VECTOR_RW_RDLOCK(&level->categories);

	if (argc == AST_VECTOR_SIZE(&level->categories)) {
		AST_VECTOR_RW_UNLOCK(&level->categories);
		return NULL;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&level->categories); ++i) {
		struct category_t *category = AST_VECTOR_GET(&level->categories, i);

		if (!strncasecmp(word, category->name, wordlen) && (++which > state)) {
			/* Check to see if one is already been included */
			for (j = 0; j < argc; ++j) {
				if (!strncasecmp(category->name, argv[j], strlen(category->name))) {
					break;
				}
			}

			if (j != argc) {
				continue;
			}

			if (state != -1) {
				AST_VECTOR_RW_UNLOCK(&level->categories);
				return ast_strdup(category->name);
			}

			if (ast_cli_completion_add(ast_strdup(category->name))) {
				AST_VECTOR_RW_UNLOCK(&level->categories);
				return NULL;
			}
		}
	}

	AST_VECTOR_RW_UNLOCK(&level->categories);
	return NULL;
}

static int category_is_allowed(int sublevel, struct categories_level_t *level, uintmax_t ids)
{
	/* Check level, and potentially allow but only if there is a matching state enabled */
	if (ids & level->state) {
		int i;

		if (sublevel == AST_LOG_CATEGORY_ENABLED || sublevel == 0) {
			/* If at least one id is enabled then always allow these sublevels */
			return 1;
		}

		AST_VECTOR_RW_RDLOCK(&level->categories);
		for (i = 0; i < AST_VECTOR_SIZE(&level->categories); ++i) {
			struct category_t *category = AST_VECTOR_GET(&level->categories, i);

			/*
			 * If there is at least one matching category, and that category is enabled
			 * or its sub-level is at or above the given sub-level then allow.
			 */
			if ((category->id & ids) && category->sublevel != AST_LOG_CATEGORY_DISABLED &&
				(category->sublevel == AST_LOG_CATEGORY_ENABLED || category->sublevel >= sublevel)) {
				AST_VECTOR_RW_UNLOCK(&level->categories);
				return 1;
			}
		}
		AST_VECTOR_RW_UNLOCK(&level->categories);
	}

	return 0;
}

static struct categories_level_t debug_categories = {
	.type = __LOG_DEBUG,
	.sublevel = 0,
	.id_pool = 0,
	.state = 0,
};

uintmax_t ast_debug_category_register(const char *name)
{
	return category_register(&debug_categories, name);
}

int ast_debug_category_unregister(const char *name)
{
	return category_unregister(&debug_categories, name);
}

int ast_debug_category_set_sublevel(const char *name, int sublevel)
{
	return category_set_sublevel(NULL, &debug_categories, name, sublevel);
}

int ast_debug_category_set_sublevels(const char * const *names,
	size_t size, int default_sublevel)
{
	return category_set_sublevels(&debug_categories, names, size, default_sublevel);
}

char *ast_debug_category_complete(const char * const *argv, int argc,
	const char *word, int state)
{
	return category_complete(&debug_categories, argv, argc, word, state);
}

int ast_debug_category_is_allowed(int sublevel, uintmax_t ids)
{
	return category_is_allowed(sublevel, &debug_categories, ids);
}

int ast_logger_category_unload(void)
{
	AST_VECTOR_RW_FREE(&debug_categories.categories);
	return 0;
}

int ast_logger_category_load(void)
{
	if (AST_VECTOR_RW_INIT(&debug_categories.categories, 10)) {
		return -1;
	}

	return 0;
}
