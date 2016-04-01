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
#include <sys/time.h>
#include <signal.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/named_locks.h"

/*! \brief Thread keeping things alive */
static pthread_t check_thread = AST_PTHREADT_NULL;

/*! \brief The global interval at which to check for contact expiration */
static unsigned int check_interval;

/*! \brief Callback function which deletes a contact */
static int expire_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct ast_named_lock *lock;

	lock = ast_named_lock_get(AST_NAMED_LOCK_TYPE_RWLOCK, "aor", contact->aor);
	if (!lock) {
		return 0;
	}

	/*
	 * We need to check the expiration again with the aor lock held
	 * in case another thread is attempting to renew the contact.
	 */
	ao2_wrlock(lock);
	if (ast_tvdiff_ms(ast_tvnow(), contact->expiration_time) > 0) {
		ast_sip_location_delete_contact(contact);
	}
	ao2_unlock(lock);
	ast_named_lock_put(lock);

	return 0;
}

static void *check_expiration_thread(void *data)
{
	struct ao2_container *contacts;
	struct ast_variable *var;
	char *time = alloca(64);

	while (check_interval) {
		sleep(check_interval);

		sprintf(time, "%ld", ast_tvnow().tv_sec);
		var = ast_variable_new("expiration_time <=", time, "");

		ast_debug(4, "Woke up at %s  Interval: %d\n", time, check_interval);

		contacts = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "contact",
			AST_RETRIEVE_FLAG_MULTIPLE, var);

		ast_variables_destroy(var);
		if (contacts) {
			ast_debug(3, "Expiring %d contacts\n\n", ao2_container_count(contacts));
			ao2_callback(contacts, OBJ_NODATA, expire_contact, NULL);
			ao2_ref(contacts, -1);
		}
	}

	return NULL;
}

static void expiration_global_loaded(const char *object_type)
{
	check_interval = ast_sip_get_contact_expiration_check_interval();

	/* Observer calls are serialized so this is safe without it's own lock */
	if (check_interval) {
		if (check_thread == AST_PTHREADT_NULL) {
			if (ast_pthread_create_background(&check_thread, NULL, check_expiration_thread, NULL)) {
				ast_log(LOG_ERROR, "Could not create thread for checking contact expiration.\n");
				return;
			}
			ast_debug(3, "Interval = %d, starting thread\n", check_interval);
		}
	} else {
		if (check_thread != AST_PTHREADT_NULL) {
			pthread_kill(check_thread, SIGURG);
			pthread_join(check_thread, NULL);
			check_thread = AST_PTHREADT_NULL;
			ast_debug(3, "Interval = 0, shutting thread down\n");
		}
	}
}

/*! \brief Observer which is used to update our interval when the global setting changes */
static struct ast_sorcery_observer expiration_global_observer = {
	.loaded = expiration_global_loaded,
};

static int unload_module(void)
{
	if (check_thread != AST_PTHREADT_NULL) {
		check_interval = 0;
		pthread_kill(check_thread, SIGURG);
		pthread_join(check_thread, NULL);

		check_thread = AST_PTHREADT_NULL;
	}

	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "global", &expiration_global_observer);

	return 0;
}


static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &expiration_global_observer);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Contact Auto-Expiration",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
);
