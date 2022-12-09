/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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
#include "asterisk/astobj2.h"
#include "asterisk/datastore.h"
#include "asterisk/channel.h"
#include "asterisk/res_geolocation.h"
#include "asterisk/vector.h"
#include "geoloc_private.h"

#define GEOLOC_DS_TYPE "geoloc_eprofiles"

struct ast_sorcery *geoloc_sorcery;

struct eprofiles_datastore {
	const char *id;
	AST_VECTOR(geoloc_eprofiles, struct ast_geoloc_eprofile *) eprofiles;
};

static void geoloc_datastore_free(void *obj)
{
	struct eprofiles_datastore *eds = obj;

	AST_VECTOR_RESET(&eds->eprofiles, ao2_cleanup);
	AST_VECTOR_FREE(&eds->eprofiles);
	ast_free(eds);
}

static void *geoloc_datastore_duplicate(void *obj)
{
	struct eprofiles_datastore *in_eds = obj;
	struct eprofiles_datastore *out_eds;
	int rc = 0;
	int i = 0;
	int eprofile_count = 0;

	out_eds = ast_calloc(1, sizeof(*out_eds));
	if (!out_eds) {
		return NULL;
	}

	rc = AST_VECTOR_INIT(&out_eds->eprofiles, 2);
	if (rc != 0) {
		ast_free(out_eds);
		return NULL;
	}

	eprofile_count = AST_VECTOR_SIZE(&in_eds->eprofiles);
	for (i = 0; i < eprofile_count; i++) {
		struct ast_geoloc_eprofile *ep = AST_VECTOR_GET(&in_eds->eprofiles, i);
		rc = AST_VECTOR_APPEND(&out_eds->eprofiles, ao2_bump(ep));
		if (rc != 0) {
			/* This will clean up the bumped reference to the eprofile */
			geoloc_datastore_free(out_eds);
			return NULL;
		}
	}

	return out_eds;
}

static const struct ast_datastore_info geoloc_datastore_info = {
	.type = GEOLOC_DS_TYPE,
	.destroy = geoloc_datastore_free,
	.duplicate = geoloc_datastore_duplicate,
};

#define IS_GEOLOC_DS(_ds) (_ds && _ds->data && ast_strings_equal(_ds->info->type, GEOLOC_DS_TYPE))

const char *ast_geoloc_datastore_get_id(struct ast_datastore *ds)
{
	struct eprofiles_datastore *eds = NULL;

	if (!IS_GEOLOC_DS(ds)) {
		return NULL;
	}

	eds = (struct eprofiles_datastore *)ds->data;

	return eds->id;
}

struct ast_datastore *ast_geoloc_datastore_create(const char *id)
{
	struct ast_datastore *ds = NULL;
	struct eprofiles_datastore *eds = NULL;
	int rc = 0;

	if (ast_strlen_zero(id)) {
		ast_log(LOG_ERROR, "A geoloc datastore can't be allocated with a NULL or empty id\n");
		return NULL;
	}

	ds = ast_datastore_alloc(&geoloc_datastore_info, NULL);
	if (!ds) {
		ast_log(LOG_ERROR, "Geoloc datastore '%s' couldn't be allocated\n", id);
		return NULL;
	}

	eds = ast_calloc(1, sizeof(*eds));
	if (!eds) {
		ast_datastore_free(ds);
		ast_log(LOG_ERROR, "Private structure for geoloc datastore '%s' couldn't be allocated\n", id);
		return NULL;
	}
	ds->data = eds;


	rc = AST_VECTOR_INIT(&eds->eprofiles, 2);
	if (rc != 0) {
		ast_datastore_free(ds);
		ast_log(LOG_ERROR, "Vector for geoloc datastore '%s' couldn't be initialized\n", id);
		return NULL;
	}

	return ds;
}

int ast_geoloc_datastore_add_eprofile(struct ast_datastore *ds,
	struct ast_geoloc_eprofile *eprofile)
{
	struct eprofiles_datastore *eds = NULL;
	int rc = 0;

	if (!IS_GEOLOC_DS(ds) || !eprofile) {
		return -1;
	}

	eds = ds->data;
	rc = AST_VECTOR_APPEND(&eds->eprofiles, ao2_bump(eprofile));
	if (rc != 0) {
		ao2_ref(eprofile, -1);
		ast_log(LOG_ERROR, "Couldn't add eprofile '%s' to geoloc datastore '%s'\n", eprofile->id, eds->id);
		return -1;
	}

	return AST_VECTOR_SIZE(&eds->eprofiles);
}

int ast_geoloc_datastore_insert_eprofile(struct ast_datastore *ds,
	struct ast_geoloc_eprofile *eprofile, int index)
{
	struct eprofiles_datastore *eds = NULL;
	int rc = 0;

	if (!IS_GEOLOC_DS(ds) || !eprofile) {
		return -1;
	}

	eds = ds->data;
	rc = AST_VECTOR_INSERT_AT(&eds->eprofiles, index, ao2_bump(eprofile));
	if (rc != 0) {
		ao2_ref(eprofile, -1);
		ast_log(LOG_ERROR, "Couldn't add eprofile '%s' to geoloc datastore '%s' in position '%d'\n",
			eprofile->id, eds->id, index);
		return -1;
	}

	return AST_VECTOR_SIZE(&eds->eprofiles);
}

int ast_geoloc_datastore_size(struct ast_datastore *ds)
{
	struct eprofiles_datastore *eds = NULL;

	if (!IS_GEOLOC_DS(ds)) {
		return -1;
	}

	eds = ds->data;

	return AST_VECTOR_SIZE(&eds->eprofiles);
}

int ast_geoloc_datastore_set_inheritance(struct ast_datastore *ds, int inherit)
{
	if (!IS_GEOLOC_DS(ds)) {
		return -1;
	}
	ds->inheritance = inherit ? DATASTORE_INHERIT_FOREVER : 0;
	return 0;
}

struct ast_geoloc_eprofile *ast_geoloc_datastore_get_eprofile(struct ast_datastore *ds, int ix)
{
	struct eprofiles_datastore *eds = NULL;
	struct ast_geoloc_eprofile *eprofile;

	if (!IS_GEOLOC_DS(ds)) {
		return NULL;
	}

	eds = ds->data;

	if (ix >= AST_VECTOR_SIZE(&eds->eprofiles)) {
		return NULL;
	}

	eprofile  = AST_VECTOR_GET(&eds->eprofiles, ix);
	return ao2_bump(eprofile);
}

struct ast_datastore *ast_geoloc_datastore_find(struct ast_channel *chan)
{
	return ast_channel_datastore_find(chan, &geoloc_datastore_info, NULL);
}

int ast_geoloc_datastore_delete_eprofile(struct ast_datastore *ds, int ix)
{
	struct eprofiles_datastore *eds = NULL;

	if (!IS_GEOLOC_DS(ds)) {
		return -1;
	}

	eds = ds->data;

	if (ix >= AST_VECTOR_SIZE(&eds->eprofiles)) {
		return -1;
	}

	ao2_ref(AST_VECTOR_REMOVE(&eds->eprofiles, ix, 1), -1);
	return 0;
}

struct ast_datastore *ast_geoloc_datastore_create_from_eprofile(
	struct ast_geoloc_eprofile *eprofile)
{
	struct ast_datastore *ds;
	int rc = 0;

	if (!eprofile) {
		return NULL;
	}

	ds = ast_geoloc_datastore_create(eprofile->id);
	if (!ds) {
		return NULL;
	}

	rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
	if (rc <= 0) {
		ast_datastore_free(ds);
		ds = NULL;
	}

	return ds;
}

struct ast_datastore *ast_geoloc_datastore_create_from_profile_name(const char *profile_name)
{
	struct ast_datastore *ds = NULL;
	struct ast_geoloc_eprofile *eprofile = NULL;
	struct ast_geoloc_profile *profile = NULL;
	int rc = 0;

	if (ast_strlen_zero(profile_name)) {
		return NULL;
	}

	profile = ast_sorcery_retrieve_by_id(geoloc_sorcery, "profile", profile_name);
	if (!profile) {
		ast_log(LOG_ERROR, "A profile with the name '%s' was not found\n", profile_name);
		return NULL;
	}

	ds = ast_geoloc_datastore_create(profile_name);
	if (!ds) {
		ast_log(LOG_ERROR, "A datastore couldn't be allocated for profile '%s'\n", profile_name);
		ao2_ref(profile, -1);
		return NULL;
	}

	eprofile = ast_geoloc_eprofile_create_from_profile(profile);
	ao2_ref(profile, -1);
	if (!eprofile) {
		ast_datastore_free(ds);
		ast_log(LOG_ERROR, "An effective profile with the name '%s' couldn't be allocated\n", profile_name);
		return NULL;
	}

	rc = ast_geoloc_datastore_add_eprofile(ds, eprofile);
	ao2_ref(eprofile, -1);
	if (rc <= 0) {
		ast_datastore_free(ds);
		ds = NULL;
	}

	return ds;
}

int geoloc_channel_unload(void)
{
	if (geoloc_sorcery) {
		ast_sorcery_unref(geoloc_sorcery);
	}
	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_channel_load(void)
{
	geoloc_sorcery = geoloc_get_sorcery();
	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_channel_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}
