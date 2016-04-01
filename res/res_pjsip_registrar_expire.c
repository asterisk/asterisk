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
#include "asterisk/named_locks.h"

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
	const struct contact_expiration *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = ast_sorcery_object_get_id(object->contact);
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief Comparison function for contact auto-expiration */
static int contact_expiration_cmp(void *obj, void *arg, int flags)
{
	const struct contact_expiration *object_left = obj;
	const struct contact_expiration *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = ast_sorcery_object_get_id(object_right->contact);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(ast_sorcery_object_get_id(object_left->contact), right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/*
		 * We could also use a partial key struct containing a length
		 * so strlen() does not get called for every comparison instead.
		 */
		cmp = strncmp(ast_sorcery_object_get_id(object_left->contact), right_key,
			strlen(right_key));
		break;
	default:
		/*
		 * What arg points to is specific to this traversal callback
		 * and has no special meaning to astobj2.
		 */
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	/*
	 * At this point the traversal callback is identical to a sorted
	 * container.
	 */
	return CMP_MATCH;
}

/*! \brief Scheduler function which deletes a contact */
static int contact_expiration_expire(const void *data)
{
	struct contact_expiration *expiration = (void *) data;
	struct ast_sip_contact *fresh_contact;
	struct ast_named_lock *lock;
	int reschedule_time = 0;

	lock = ast_named_lock_get(AST_NAMED_LOCK_TYPE_RWLOCK, "aor", expiration->contact->aor);
	if (!lock) {
		/* Uh Oh.  Just expire the contact and don't be nice about it. */
		expiration->sched = -1;
		ast_sip_location_delete_contact(expiration->contact);
		ao2_ref(expiration, -1);
		return 0;
	}

	/*
	 * We need to check the expiration again with the aor lock held
	 * in case another thread is attempting to renew the contact.
	 */
	ao2_wrlock(lock);

	fresh_contact = ast_sip_location_retrieve_contact(
		ast_sorcery_object_get_id(expiration->contact));
	if (fresh_contact) {
		int expires;

		expires = ast_tvdiff_ms(fresh_contact->expiration_time, ast_tvnow());
		if (0 < expires) {
			/* We need to reschedule for the new expiration time. */
			reschedule_time = expires;
		} else {
			/*
			 * Contact is expired.
			 *
			 * This will end up invoking the deleted observer callback,
			 * which will perform the unlinking and such.
			 */
			expiration->sched = -1;
			ast_sip_location_delete_contact(fresh_contact);
			ao2_ref(expiration, -1);
		}
		ao2_ref(fresh_contact, -1);
	} else {
		/*
		 * The object no longer exists in sorcery since we
		 * could not get a fresh copy.
		 */
		expiration->sched = -1;
		ast_sip_location_delete_contact(expiration->contact);
		ao2_ref(expiration, -1);
	}

	ao2_unlock(lock);
	ast_named_lock_put(lock);

	return reschedule_time;
}

/*! \brief Observer callback for when a contact is created */
static void contact_expiration_observer_created(const void *object)
{
	const struct ast_sip_contact *contact = object;
	struct contact_expiration *expiration;
	int expires = MAX(0, ast_tvdiff_ms(contact->expiration_time, ast_tvnow()));

	if (ast_tvzero(contact->expiration_time)) {
		return;
	}

	expiration = ao2_alloc_options(sizeof(*expiration), contact_expiration_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!expiration) {
		return;
	}

	expiration->contact = (struct ast_sip_contact*)contact;
	ao2_ref(expiration->contact, +1);

	ao2_ref(expiration, +1);
	expiration->sched = ast_sched_add_variable(sched, expires, contact_expiration_expire,
		expiration, 1);
	if (expiration->sched < 0) {
		ao2_ref(expiration, -1);
		ast_log(LOG_ERROR, "Scheduled expiration for contact '%s' could not be performed, contact may persist past life\n",
			ast_sorcery_object_get_id(contact));
	} else {
		ao2_link(contact_autoexpire, expiration);
	}
	ao2_ref(expiration, -1);
}

/*! \brief Observer callback for when a contact is updated */
static void contact_expiration_observer_updated(const void *object)
{
	const struct ast_sip_contact *contact = object;
	struct contact_expiration *expiration;
	int expires = MAX(0, ast_tvdiff_ms(contact->expiration_time, ast_tvnow()));

	expiration = ao2_find(contact_autoexpire, ast_sorcery_object_get_id(contact),
		OBJ_SEARCH_KEY);
	if (!expiration) {
		return;
	}

	AST_SCHED_REPLACE_VARIABLE_UNREF(expiration->sched, sched, expires,
		contact_expiration_expire, expiration, 1,
		ao2_cleanup(expiration), ao2_cleanup(expiration), ao2_ref(expiration, +1));
	ao2_ref(expiration, -1);
}

/*! \brief Observer callback for when a contact is deleted */
static void contact_expiration_observer_deleted(const void *object)
{
	struct contact_expiration *expiration;

	expiration = ao2_find(contact_autoexpire, ast_sorcery_object_get_id(object),
		OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (!expiration) {
		return;
	}

	AST_SCHED_DEL_UNREF(sched, expiration->sched, ao2_cleanup(expiration));
	ao2_ref(expiration, -1);
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
	struct ao2_container *contacts;

	contacts = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "contact",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!contacts) {
		return;
	}

	ao2_callback(contacts, OBJ_NODATA, contact_expiration_setup, NULL);
	ao2_ref(contacts, -1);
}

static int unload_observer_delete(void *obj, void *arg, int flags)
{
	struct contact_expiration *expiration = obj;

	AST_SCHED_DEL_UNREF(sched, expiration->sched, ao2_cleanup(expiration));
	return CMP_MATCH;
}

static int unload_module(void)
{
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "contact", &contact_expiration_observer);
	if (sched) {
		ao2_callback(contact_autoexpire, OBJ_MULTIPLE | OBJ_NODATA | OBJ_UNLINK,
			unload_observer_delete, NULL);
		ast_sched_context_destroy(sched);
		sched = NULL;
	}
	ao2_cleanup(contact_autoexpire);
	contact_autoexpire = NULL;

	return 0;
}

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	contact_autoexpire = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK,
		CONTACT_AUTOEXPIRE_BUCKETS, contact_expiration_hash, contact_expiration_cmp);
	if (!contact_autoexpire) {
		ast_log(LOG_ERROR, "Could not create container for contact auto-expiration\n");
		return AST_MODULE_LOAD_FAILURE;
	}

	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Could not create scheduler for contact auto-expiration\n");
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	if (ast_sched_start_thread(sched)) {
		ast_log(LOG_ERROR, "Could not start scheduler thread for contact auto-expiration\n");
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	contact_expiration_initialize_existing();

	if (ast_sorcery_observer_add(ast_sip_get_sorcery(), "contact", &contact_expiration_observer)) {
		ast_log(LOG_ERROR, "Could not add observer for notifications about contacts for contact auto-expiration\n");
		unload_module();
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Contact Auto-Expiration",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	);
