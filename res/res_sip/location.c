/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

#include "asterisk.h"
#include "pjsip.h"
#include "pjlib.h"

#include "asterisk/res_sip.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/sorcery.h"

/*! \brief Destructor for AOR */
static void aor_destroy(void *obj)
{
	struct ast_sip_aor *aor = obj;

	ao2_cleanup(aor->permanent_contacts);
	ast_string_field_free_memory(aor);
}

/*! \brief Allocator for AOR */
static void *aor_alloc(const char *name)
{
	struct ast_sip_aor *aor = ao2_alloc_options(sizeof(struct ast_sip_aor), aor_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!aor) {
		return NULL;
	}
	ast_string_field_init(aor, 128);
	return aor;
}

/*! \brief Destructor for contact */
static void contact_destroy(void *obj)
{
	struct ast_sip_contact *contact = obj;

	ast_string_field_free_memory(contact);
}

/*! \brief Allocator for contact */
static void *contact_alloc(const char *name)
{
	struct ast_sip_contact *contact = ao2_alloc_options(sizeof(*contact), contact_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);

	if (!contact) {
		return NULL;
	}

	if (ast_string_field_init(contact, 256)) {
		ao2_cleanup(contact);
		return NULL;
	}

	return contact;
}

struct ast_sip_aor *ast_sip_location_retrieve_aor(const char *aor_name)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "aor", aor_name);
}

/*! \brief Internal callback function which deletes and unlinks any expired contacts */
static int contact_expire(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;

	/* If the contact has not yet expired it is valid */
	if (ast_tvdiff_ms(contact->expiration_time, ast_tvnow()) > 0) {
		return 0;
	}

	ast_sip_location_delete_contact(contact);

	return CMP_MATCH;
}

/*! \brief Internal callback function which links static contacts into another container */
static int contact_link_static(void *obj, void *arg, int flags)
{
	struct ao2_container *dest = arg;

	ao2_link_flags(dest, obj, OBJ_NOLOCK);
	return 0;
}

/*! \brief Simple callback function which returns immediately, used to grab the first contact of an AOR */
static int contact_find_first(void *obj, void *arg, int flags)
{
	return CMP_MATCH | CMP_STOP;
}

struct ast_sip_contact *ast_sip_location_retrieve_first_aor_contact(const struct ast_sip_aor *aor)
{
	RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
	struct ast_sip_contact *contact;

	contacts = ast_sip_location_retrieve_aor_contacts(aor);
	if (!contacts || (ao2_container_count(contacts) == 0)) {
		return NULL;
	}

	contact = ao2_callback(contacts, OBJ_NOLOCK, contact_find_first, NULL);
	return contact;
}

struct ao2_container *ast_sip_location_retrieve_aor_contacts(const struct ast_sip_aor *aor)
{
	/* Give enough space for ^ at the beginning and ;@ at the end, since that is our object naming scheme */
	char regex[strlen(ast_sorcery_object_get_id(aor)) + 4];
	struct ao2_container *contacts;

	snprintf(regex, sizeof(regex), "^%s;@", ast_sorcery_object_get_id(aor));

	if (!(contacts = ast_sorcery_retrieve_by_regex(ast_sip_get_sorcery(), "contact", regex))) {
		return NULL;
	}

	/* Prune any expired contacts and delete them, we do this first because static contacts can never expire */
	ao2_callback(contacts, OBJ_NOLOCK | OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, contact_expire, NULL);

	/* Add any permanent contacts from the AOR */
	if (aor->permanent_contacts) {
		ao2_callback(aor->permanent_contacts, OBJ_NOLOCK | OBJ_NODATA, contact_link_static, contacts);
	}

	return contacts;
}

struct ast_sip_contact *ast_sip_location_retrieve_contact_from_aor_list(const char *aor_list)
{
	char *aor_name;
	char *rest;
	struct ast_sip_contact *contact = NULL;

	/* If the location is still empty we have nowhere to go */
	if (ast_strlen_zero(aor_list) || !(rest = ast_strdupa(aor_list))) {
		ast_log(LOG_WARNING, "Unable to determine contacts from empty aor list\n");
		return NULL;
	}

	while ((aor_name = strsep(&rest, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor, ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);

		if (!aor) {
			continue;
		}
		contact = ast_sip_location_retrieve_first_aor_contact(aor);
		/* If a valid contact is available use its URI for dialing */
		if (contact) {
			break;
		}
	}

	return contact;
}

struct ast_sip_contact *ast_sip_location_retrieve_contact(const char *contact_name)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "contact", contact_name);
}

int ast_sip_location_add_contact(struct ast_sip_aor *aor, const char *uri, struct timeval expiration_time)
{
	char name[AST_UUID_STR_LEN];
	RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);

	snprintf(name, sizeof(name), "%s;@%s", ast_sorcery_object_get_id(aor), uri);

	if (!(contact = ast_sorcery_alloc(ast_sip_get_sorcery(), "contact", name))) {
		return -1;
	}

	ast_string_field_set(contact, uri, uri);
	contact->expiration_time = expiration_time;

	return ast_sorcery_create(ast_sip_get_sorcery(), contact);
}

int ast_sip_location_update_contact(struct ast_sip_contact *contact)
{
	return ast_sorcery_update(ast_sip_get_sorcery(), contact);
}

int ast_sip_location_delete_contact(struct ast_sip_contact *contact)
{
	return ast_sorcery_delete(ast_sip_get_sorcery(), contact);
}

/*! \brief Custom handler for translating from a string timeval to actual structure */
static int expiration_str2struct(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_contact *contact = obj;
	return ast_get_timeval(var->value, &contact->expiration_time, ast_tv(0, 0), NULL);
}

/*! \brief Custom handler for translating from an actual structure timeval to string */
static int expiration_struct2str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_contact *contact = obj;
	return (ast_asprintf(buf, "%lu", contact->expiration_time.tv_sec) < 0) ? -1 : 0;
}

/*! \brief Custom handler for permanent URIs */
static int permanent_uri_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_aor *aor = obj;
	RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);

	if ((!aor->permanent_contacts && !(aor->permanent_contacts = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 1, NULL, NULL))) ||
		!(contact = ast_sorcery_alloc(ast_sip_get_sorcery(), "contact", NULL))) {
		return -1;
	}

	ast_string_field_set(contact, uri, var->value);
	ao2_link_flags(aor->permanent_contacts, contact, OBJ_NOLOCK);

	return 0;
}

/*! \brief Initialize sorcery with location support */
int ast_sip_initialize_sorcery_location(struct ast_sorcery *sorcery)
{
	ast_sorcery_apply_default(sorcery, "contact", "astdb", "registrar");
	ast_sorcery_apply_default(sorcery, "aor", "config", "res_sip.conf,criteria=type=aor");

	if (ast_sorcery_object_register(sorcery, "contact", contact_alloc, NULL, NULL) ||
		ast_sorcery_object_register(sorcery, "aor", aor_alloc, NULL, NULL)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, "contact", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "contact", "uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, uri));
	ast_sorcery_object_field_register_custom(sorcery, "contact", "expiration_time", "", expiration_str2struct, expiration_struct2str, 0, 0);

	ast_sorcery_object_field_register(sorcery, "aor", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "aor", "minimum_expiration", "60", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, minimum_expiration));
	ast_sorcery_object_field_register(sorcery, "aor", "maximum_expiration", "7200", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, maximum_expiration));
	ast_sorcery_object_field_register(sorcery, "aor", "default_expiration", "3600", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, default_expiration));
	ast_sorcery_object_field_register(sorcery, "aor", "max_contacts", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, max_contacts));
	ast_sorcery_object_field_register(sorcery, "aor", "remove_existing", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_aor, remove_existing));
	ast_sorcery_object_field_register_custom(sorcery, "aor", "contact", "", permanent_uri_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "aor", "mailboxes", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_aor, mailboxes));

	return 0;
}
