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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/sched.h"

#define CONTACT_AUTOEXPIRE_BUCKETS 977

static struct ao2_container *contact_autoexpire;

/*! \brief Scheduler used for automatically expiring contacts */
static struct ast_sched_context *sched;

/*! \brief Structure used for contact auto-expiration */
struct contact_expiration {
	/*! \brief Contact that is being auto-expired */
	struct ast_sip_contact *contact;

	/*! \brief Scheduled item for performing expiration */
	int sched;
};

/*! \brief Destructor function for contact auto-expiration */
static void contact_expiration_destroy(void *obj)
{
	struct contact_expiration *expiration = obj;

	ao2_cleanup(expiration->contact);
}

/*! \brief Hashing function for contact auto-expiration */
static int contact_expiration_hash(const void *obj, const int flags)
{
	const struct contact_expiration *expiration = obj;
	const char *id = obj;

	return ast_str_hash(flags & OBJ_KEY ? id : ast_sorcery_object_get_id(expiration->contact));
}

/*! \brief Comparison function for contact auto-expiration */
static int contact_expiration_cmp(void *obj, void *arg, int flags)
{
	struct contact_expiration *expiration1 = obj, *expiration2 = arg;
	const char *id = arg;

	return !strcmp(ast_sorcery_object_get_id(expiration1->contact), flags & OBJ_KEY ? id :
		       ast_sorcery_object_get_id(expiration2->contact)) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Scheduler function which deletes a contact */
static int contact_expiration_expire(const void *data)
{
	RAII_VAR(struct contact_expiration *, expiration, (void*)data, ao2_cleanup);

	expiration->sched = -1;

	/* This will end up invoking the deleted observer callback, which will perform the unlinking and such */
	ast_sorcery_delete(ast_sip_get_sorcery(), expiration->contact);

	return 0;
}

/*! \brief Observer callback for when a contact is created */
static void contact_expiration_observer_created(const void *object)
{
	const struct ast_sip_contact *contact = object;
	RAII_VAR(struct contact_expiration *, expiration, NULL, ao2_cleanup);
	int expires = MAX(0, ast_tvdiff_ms(contact->expiration_time, ast_tvnow()));

	if (ast_tvzero(contact->expiration_time)) {
		return;
	}

	if (!(expiration = ao2_alloc_options(sizeof(*expiration), contact_expiration_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK))) {
		return;
	}

	expiration->contact = (struct ast_sip_contact*)contact;
	ao2_ref(expiration->contact, +1);

	ao2_ref(expiration, +1);
	if ((expiration->sched = ast_sched_add(sched, expires, contact_expiration_expire, expiration)) < 0) {
		ao2_cleanup(expiration);
		ast_log(LOG_ERROR, "Scheduled expiration for contact '%s' could not be performed, contact may persist past life\n",
			ast_sorcery_object_get_id(contact));
		return;
	}

	ao2_link(contact_autoexpire, expiration);
}

/*! \brief Observer callback for when a contact is updated */
static void contact_expiration_observer_updated(const void *object)
{
	const struct ast_sip_contact *contact = object;
	RAII_VAR(struct contact_expiration *, expiration, ao2_find(contact_autoexpire, ast_sorcery_object_get_id(contact), OBJ_KEY), ao2_cleanup);
	int expires = MAX(0, ast_tvdiff_ms(contact->expiration_time, ast_tvnow()));

	if (!expiration) {
		return;
	}

	AST_SCHED_REPLACE_UNREF(expiration->sched, sched, expires, contact_expiration_expire, expiration, ao2_cleanup(expiration), ao2_cleanup(expiration), ao2_ref(expiration, +1));
}

/*! \brief Observer callback for when a contact is deleted */
static void contact_expiration_observer_deleted(const void *object)
{
	RAII_VAR(struct contact_expiration *, expiration, ao2_find(contact_autoexpire, ast_sorcery_object_get_id(object), OBJ_KEY | OBJ_UNLINK), ao2_cleanup);

	if (!expiration) {
		return;
	}

	AST_SCHED_DEL_UNREF(sched, expiration->sched, ao2_cleanup(expiration));
}

/*! \brief Observer callbacks for autoexpiring contacts */
static const struct ast_sorcery_observer contact_expiration_observer = {
	.created = contact_expiration_observer_created,
	.updated = contact_expiration_observer_updated,
	.deleted = contact_expiration_observer_deleted,
};

/*! \brief Callback function which deletes a contact if it has expired or sets up auto-expiry */
static int contact_expiration_setup(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	int expires = MAX(0, ast_tvdiff_ms(contact->expiration_time, ast_tvnow()));

	if (!expires) {
		ast_sorcery_delete(ast_sip_get_sorcery(), contact);
	} else {
		contact_expiration_observer_created(contact);
	}

	return 0;
}

/*! \brief Initialize auto-expiration of any existing contacts */
static void contact_expiration_initialize_existing(void)
{
	RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);

	if (!(contacts = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "contact", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL))) {
		return;
	}

	ao2_callback(contacts, OBJ_NODATA, contact_expiration_setup, NULL);
}

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	if (!(contact_autoexpire = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, CONTACT_AUTOEXPIRE_BUCKETS,
		contact_expiration_hash, contact_expiration_cmp))) {
		ast_log(LOG_ERROR, "Could not create container for contact auto-expiration\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Could not create scheduler for contact auto-expiration\n");
		goto error;
	}

	if (ast_sched_start_thread(sched)) {
		ast_log(LOG_ERROR, "Could not start scheduler thread for contact auto-expiration\n");
		goto error;
	}

	contact_expiration_initialize_existing();

	if (ast_sorcery_observer_add(ast_sip_get_sorcery(), "contact", &contact_expiration_observer)) {
		ast_log(LOG_ERROR, "Could not add observer for notifications about contacts for contact auto-expiration\n");
		goto error;
	}

	return AST_MODULE_LOAD_SUCCESS;

error:
	if (sched) {
		ast_sched_context_destroy(sched);
	}

	ao2_cleanup(contact_autoexpire);
	return AST_MODULE_LOAD_FAILURE;
}

static int unload_module(void)
{
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "contact", &contact_expiration_observer);
	ast_sched_context_destroy(sched);
	ao2_cleanup(contact_autoexpire);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Contact Auto-Expiration",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
