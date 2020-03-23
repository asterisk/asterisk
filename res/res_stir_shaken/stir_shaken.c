/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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
 * \brief Internal stir/shaken utilities
 */

#include "asterisk.h"

#include "asterisk/cli.h"
#include "asterisk/sorcery.h"

#include "stir_shaken.h"
#include "asterisk/res_stir_shaken.h"

int stir_shaken_cli_show(void *obj, void *arg, int flags)
{
	struct ast_cli_args *a = arg;
	struct ast_variable *options;
	struct ast_variable *i;

	if (!obj) {
		ast_cli(a->fd, "No stir/shaken configuration found\n");
		return 0;
	}

	options = ast_variable_list_sort(ast_sorcery_objectset_create2(
		ast_stir_shaken_sorcery(), obj, AST_HANDLER_ONLY_STRING));
	if (!options) {
		return 0;
	}

	ast_cli(a->fd, "%s: %s\n", ast_sorcery_object_get_type(obj),
		ast_sorcery_object_get_id(obj));

	for (i = options; i; i = i->next) {
		ast_cli(a->fd, "\t%s: %s\n", i->name, i->value);
	}

	ast_cli(a->fd, "\n");

	ast_variables_destroy(options);

	return 0;
}

char *stir_shaken_tab_complete_name(const char *word, struct ao2_container *container)
{
	void *obj;
	struct ao2_iterator it;
	int wordlen = strlen(word);
	int ret;

	it = ao2_iterator_init(container, 0);
	while ((obj = ao2_iterator_next(&it))) {
		if (!strncasecmp(word, ast_sorcery_object_get_id(obj), wordlen)) {
			ret = ast_cli_completion_add(ast_strdup(ast_sorcery_object_get_id(obj)));
			if (ret) {
				ao2_ref(obj, -1);
				break;
			}
		}
		ao2_ref(obj, -1);
	}
	ao2_iterator_destroy(&it);

	return NULL;
}

EVP_PKEY *read_private_key(const char *path)
{
	EVP_PKEY *private_key = NULL;
	FILE *fp;

	fp = fopen(path, "r");
	if (!fp) {
		ast_log(LOG_ERROR, "Failed to read private key file '%s'\n", path);
		return NULL;
	}

	if (!PEM_read_PrivateKey(fp, &private_key, NULL, NULL)) {
		ast_log(LOG_ERROR, "Failed to read private key from file '%s'\n", path);
		fclose(fp);
		return NULL;
	}

	if (EVP_PKEY_id(private_key) != EVP_PKEY_EC) {
		ast_log(LOG_ERROR, "Private key from '%s' must be of type EVP_PKEY_EC\n", path);
		fclose(fp);
		EVP_PKEY_free(private_key);
		return NULL;
	}

	fclose(fp);

	return private_key;
}
