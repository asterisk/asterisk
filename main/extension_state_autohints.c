/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Sangoma Technologies Corporation
 *
 * Joshua Colp <jcolp@sangoma.com>
 *
 * See https://www.asterisk.org for more information about
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
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "asterisk/_private.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis.h"
#include "asterisk/vector.h"
#include "pbx_private.h"

/*! \brief Lock to protect the autohints vector */
AST_MUTEX_DEFINE_STATIC(extension_state_autohints_lock);

/*! \brief Contexts which have autohints enabled */
static AST_VECTOR(, struct ast_context *) extension_state_autohints;

/*! \brief Subscription to receive updates so we can create hints as needed on autohint enabled contexts */
static struct stasis_subscription *autohints_subscription;

/*! \brief The static registrar for the added dialplan hints */
static const char registrar[] = "autohints";

/*!
 * \internal
 * \brief Callback for device state updates to create hints for autohint enabled contexts.
 *
 * \param userdata The user data passed to the subscription.
 * \param sub The subscription that received the message.
 * \param msg The message received by the subscription.
 */
static void extension_state_autohints_device_state_cb(void *userdata, struct stasis_subscription *sub,
		struct stasis_message *msg)
{
	struct ast_device_state_message *device_state;
	char *virtual_device, *type, *device_name;
	int i;

	if (stasis_message_type(msg) != ast_device_state_message_type()) {
		return;
	}

	device_state = stasis_message_data(msg);

	/* We only care about the aggregate state */
	if (device_state->eid) {
		return;
	}

	type = ast_strdupa(device_state->device);
	if (ast_strlen_zero(type)) {
		return;
	}

	/* Determine if this is a virtual/custom device or a real device */
	virtual_device = strchr(type, ':');
	device_name = strchr(type, '/');
	if (virtual_device && (!device_name || (virtual_device < device_name))) {
		device_name = virtual_device;
	}

	/* Invalid device state name - not a virtual/custom device and not a real device */
	if (ast_strlen_zero(device_name)) {
		return;
	}
	*device_name++ = '\0';

	ast_wrlock_contexts();

	ast_mutex_lock(&extension_state_autohints_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&extension_state_autohints); i++) {
		struct ast_context *context = AST_VECTOR_GET(&extension_state_autohints, i);
		struct pbx_find_info q = { .stacklen = 0 };

		if (pbx_find_extension(NULL, NULL, &q, ast_get_context_name(context), device_name,
			PRIORITY_HINT, NULL, "", E_MATCH)) {
			continue;
		}

		/* The device has no hint in the context referenced by this autohint so create one */
		ast_add_extension(ast_get_context_name(context), 0, device_name, PRIORITY_HINT, NULL, NULL,
			device_state->device, ast_strdup(device_state->device), ast_free_ptr, registrar);
	}
	ast_mutex_unlock(&extension_state_autohints_lock);

	ast_unlock_contexts();
}

/*!
 * \internal
 * \brief Compare an autohint's context name to a provided context name for searching the autohints vector
 */
#define AUTOHINT_CMP_CONTEXT_NAME(elem, name) (!strcmp(ast_get_context_name(elem), name))

void pbx_extension_state_autohint_set(struct ast_context *context)
{
	ast_rdlock_contexts();

	ast_mutex_lock(&extension_state_autohints_lock);

	/* Since we store a pointer to the context we remove the old one by name and append the new one, easy */
	AST_VECTOR_REMOVE_CMP_UNORDERED(&extension_state_autohints, ast_get_context_name(context), AUTOHINT_CMP_CONTEXT_NAME,
		AST_VECTOR_ELEM_CLEANUP_NOOP);
	AST_VECTOR_APPEND(&extension_state_autohints, context);

	if (AST_VECTOR_SIZE(&extension_state_autohints) && !autohints_subscription) {
		/* We have at least one context enabled with autohints and no subscription, so subscribe */
		autohints_subscription = stasis_subscribe(ast_device_state_topic_all(), extension_state_autohints_device_state_cb, NULL);
	}
	ast_mutex_unlock(&extension_state_autohints_lock);

	ast_unlock_contexts();
}

void pbx_extension_state_autohint_remove(struct ast_context *context, unsigned int forced)
{
	int removed;

	ast_wrlock_contexts();

	ast_mutex_lock(&extension_state_autohints_lock);
	if (!forced) {
		removed = AST_VECTOR_REMOVE_CMP_UNORDERED(&extension_state_autohints, context, AST_VECTOR_ELEM_DEFAULT_CMP,
			AST_VECTOR_ELEM_CLEANUP_NOOP);
	} else {
		removed = AST_VECTOR_REMOVE_CMP_UNORDERED(&extension_state_autohints, ast_get_context_name(context), AUTOHINT_CMP_CONTEXT_NAME,
			AST_VECTOR_ELEM_CLEANUP_NOOP);
	}
	if (removed) {
		ast_context_destroy_by_name(ast_get_context_name(context), registrar);
	}
	if (!AST_VECTOR_SIZE(&extension_state_autohints) && autohints_subscription) {
		/* There's no more autohint enabled contexts, so unsubscribe */
		autohints_subscription = stasis_unsubscribe(autohints_subscription);
	}
	ast_mutex_unlock(&extension_state_autohints_lock);

	ast_unlock_contexts();
}

/*!
 * \internal
 * \brief Clean up the autohints extension state system, called at shutdown.
 */
static void extension_state_autohints_cleanup(void)
{
	ast_mutex_lock(&extension_state_autohints_lock);
	if (autohints_subscription) {
		autohints_subscription = stasis_unsubscribe(autohints_subscription);
	}
	/* The vector just contains pointers so no need to free the individual items */
	AST_VECTOR_FREE(&extension_state_autohints);
	ast_mutex_unlock(&extension_state_autohints_lock);
}

int ast_extension_state_autohints_init(void)
{
	/* Most likely there will be at most one context configured for autohints, or zero */
	if (AST_VECTOR_INIT(&extension_state_autohints, 1)) {
		return -1;
	}

	ast_register_cleanup(extension_state_autohints_cleanup);
	return 0;
}
