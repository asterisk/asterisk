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
#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/paths.h"
#include "asterisk/sorcery.h"
#include "asterisk/taskprocessor.h"
#include "include/res_pjsip_private.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/statsd.h"
#include "asterisk/named_locks.h"

#include "asterisk/res_pjproject.h"

static int pj_max_hostname = PJ_MAX_HOSTNAME;
static int pjsip_max_url_size = PJSIP_MAX_URL_SIZE;

/*! \brief Destructor for AOR */
static void aor_destroy(void *obj)
{
	struct ast_sip_aor *aor = obj;

	ao2_cleanup(aor->permanent_contacts);
	ast_string_field_free_memory(aor);
	ast_free(aor->voicemail_extension);
}

/*! \brief Allocator for AOR */
static void *aor_alloc(const char *name)
{
	void *lock;
	struct ast_sip_aor *aor;

	lock = ast_named_lock_get(AST_NAMED_LOCK_TYPE_MUTEX, "aor", name);
	if (!lock) {
		return NULL;
	}

	aor = ast_sorcery_lockable_alloc(sizeof(struct ast_sip_aor), aor_destroy, lock);
	ao2_ref(lock, -1);

	if (!aor) {
		return NULL;
	}
	ast_string_field_init(aor, 128);

	return aor;
}

/*! \brief Internal callback function which destroys the specified contact */
static int destroy_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;

	ast_sip_location_delete_contact(contact);

	return CMP_MATCH;
}

static void aor_deleted_observer(const void *object)
{
	const struct ast_sip_aor *aor = object;
	const char *aor_id = ast_sorcery_object_get_id(object);
	/* Give enough space for ;@ at the end, since that is our object naming scheme */
	size_t prefix_len = strlen(aor_id) + sizeof(";@") - 1;
	char prefix[prefix_len + 1];
	struct ao2_container *contacts;

	if (aor->permanent_contacts) {
		ao2_callback(aor->permanent_contacts, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, destroy_contact, NULL);
	}

	sprintf(prefix, "%s;@", aor_id); /* Safe */
	if (!(contacts = ast_sorcery_retrieve_by_prefix(ast_sip_get_sorcery(), "contact", prefix, prefix_len))) {
		return;
	}
	/* Destroy any contacts that may still exist that were made for this AoR */
	ao2_callback(contacts, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, destroy_contact, NULL);

	ao2_ref(contacts, -1);
}

/*! \brief Observer for contacts so state can be updated on respective endpoints */
static const struct ast_sorcery_observer aor_observer = {
	.deleted = aor_deleted_observer,
};


/*! \brief Destructor for contact */
static void contact_destroy(void *obj)
{
	struct ast_sip_contact *contact = obj;

	ast_string_field_free_memory(contact);
	ao2_cleanup(contact->endpoint);
}

/*! \brief Allocator for contact */
static void *contact_alloc(const char *name)
{
	struct ast_sip_contact *contact = ast_sorcery_generic_alloc(sizeof(*contact), contact_destroy);
	char *id = ast_strdupa(name);
	char *aor = id;
	char *aor_separator = NULL;

	if (!contact) {
		return NULL;
	}

	if (ast_string_field_init(contact, 256)) {
		ao2_cleanup(contact);
		return NULL;
	}

	/* Dynamic contacts are delimited with ";@" and static ones with "@@" */
	if ((aor_separator = strstr(id, ";@")) || (aor_separator = strstr(id, "@@"))) {
		*aor_separator = '\0';
	}
	ast_assert(aor_separator != NULL);

	ast_string_field_set(contact, aor, aor);

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

	ao2_link(dest, obj);
	return 0;
}

/*! \brief Internal callback function which removes any contact which is unreachable */
static int contact_remove_unreachable(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct ast_sip_contact_status *status;
	int unreachable;

	status = ast_sip_get_contact_status(contact);
	if (!status) {
		return 0;
	}

	unreachable = (status->status == UNAVAILABLE);
	ao2_ref(status, -1);

	return unreachable ? CMP_MATCH : 0;
}

struct ast_sip_contact *ast_sip_location_retrieve_first_aor_contact(const struct ast_sip_aor *aor)
{
	return ast_sip_location_retrieve_first_aor_contact_filtered(aor, AST_SIP_CONTACT_FILTER_DEFAULT);
}

struct ast_sip_contact *ast_sip_location_retrieve_first_aor_contact_filtered(const struct ast_sip_aor *aor,
	unsigned int flags)
{
	struct ao2_container *contacts;
	struct ast_sip_contact *contact = NULL;

	contacts = ast_sip_location_retrieve_aor_contacts_filtered(aor, flags);
	if (contacts && ao2_container_count(contacts)) {
		/* Get the first AOR contact in the container. */
		contact = ao2_callback(contacts, 0, NULL, NULL);
	}
	ao2_cleanup(contacts);
	return contact;
}

struct ao2_container *ast_sip_location_retrieve_aor_contacts_nolock(const struct ast_sip_aor *aor)
{
	return ast_sip_location_retrieve_aor_contacts_nolock_filtered(aor, AST_SIP_CONTACT_FILTER_DEFAULT);
}

struct ao2_container *ast_sip_location_retrieve_aor_contacts_nolock_filtered(const struct ast_sip_aor *aor,
	unsigned int flags)
{
	/* Give enough space for ;@ at the end, since that is our object naming scheme */
	size_t prefix_len = strlen(ast_sorcery_object_get_id(aor)) + sizeof(";@") - 1;
	char prefix[prefix_len + 1];
	struct ao2_container *contacts;

	sprintf(prefix, "%s;@", ast_sorcery_object_get_id(aor)); /* Safe */
	if (!(contacts = ast_sorcery_retrieve_by_prefix(ast_sip_get_sorcery(), "contact", prefix, prefix_len))) {
		return NULL;
	}

	/* Prune any expired contacts and delete them, we do this first because static contacts can never expire */
	ao2_callback(contacts, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, contact_expire, NULL);

	/* Add any permanent contacts from the AOR */
	if (aor->permanent_contacts) {
		ao2_callback(aor->permanent_contacts, OBJ_NODATA, contact_link_static, contacts);
	}

	if (flags & AST_SIP_CONTACT_FILTER_REACHABLE) {
		ao2_callback(contacts, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, contact_remove_unreachable, NULL);
	}

	return contacts;
}

struct ao2_container *ast_sip_location_retrieve_aor_contacts(const struct ast_sip_aor *aor)
{
	return ast_sip_location_retrieve_aor_contacts_filtered(aor, AST_SIP_CONTACT_FILTER_DEFAULT);
}

struct ao2_container *ast_sip_location_retrieve_aor_contacts_filtered(const struct ast_sip_aor *aor,
	unsigned int flags)
{
	struct ao2_container *contacts;

	/* ao2_lock / ao2_unlock do not actually write aor since it has an ao2 lockobj. */
	ao2_lock((void*)aor);
	contacts = ast_sip_location_retrieve_aor_contacts_nolock_filtered(aor, flags);
	ao2_unlock((void*)aor);

	return contacts;
}


void ast_sip_location_retrieve_contact_and_aor_from_list(const char *aor_list, struct ast_sip_aor **aor,
	struct ast_sip_contact **contact)
{
	ast_sip_location_retrieve_contact_and_aor_from_list_filtered(aor_list, AST_SIP_CONTACT_FILTER_DEFAULT, aor, contact);
}

void ast_sip_location_retrieve_contact_and_aor_from_list_filtered(const char *aor_list, unsigned int flags,
	struct ast_sip_aor **aor, struct ast_sip_contact **contact)
{
	char *aor_name;
	char *rest;

	/* If the location is still empty we have nowhere to go */
	if (ast_strlen_zero(aor_list) || !(rest = ast_strdupa(aor_list))) {
		ast_log(LOG_WARNING, "Unable to determine contacts from empty aor list\n");
		return;
	}

	*aor = NULL;
	*contact = NULL;

	while ((aor_name = ast_strip(strsep(&rest, ",")))) {
		*aor = ast_sip_location_retrieve_aor(aor_name);

		if (!(*aor)) {
			continue;
		}
		*contact = ast_sip_location_retrieve_first_aor_contact_filtered(*aor, flags);
		/* If a valid contact is available use its URI for dialing */
		if (*contact) {
			break;
		}

		ao2_ref(*aor, -1);
		*aor = NULL;
	}
}

struct ast_sip_contact *ast_sip_location_retrieve_contact_from_aor_list(const char *aor_list)
{
	struct ast_sip_aor *aor;
	struct ast_sip_contact *contact;

	ast_sip_location_retrieve_contact_and_aor_from_list(aor_list, &aor, &contact);

	ao2_cleanup(aor);

	return contact;
}

static int permanent_uri_sort_fn(const void *obj_left, const void *obj_right, int flags);
static int cli_contact_populate_container(void *obj, void *arg, int flags);

static int gather_contacts_for_aor(void *obj, void *arg, int flags)
{
	struct ao2_container *aor_contacts;
	struct ast_sip_aor *aor = obj;
	struct ao2_container *container = arg;

	aor_contacts = ast_sip_location_retrieve_aor_contacts(aor);
	if (!aor_contacts) {
		return 0;
	}
	ao2_callback(aor_contacts, OBJ_MULTIPLE | OBJ_NODATA, cli_contact_populate_container,
		container);
	ao2_ref(aor_contacts, -1);
	return CMP_MATCH;
}

struct ao2_container *ast_sip_location_retrieve_contacts_from_aor_list(const char *aor_list)
{
	struct ao2_container *contacts;

	contacts = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, permanent_uri_sort_fn, NULL);
	if (!contacts) {
		return NULL;
	}

	ast_sip_for_each_aor(aor_list, gather_contacts_for_aor, contacts);

	return contacts;
}

struct ast_sip_contact *ast_sip_location_retrieve_contact(const char *contact_name)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "contact", contact_name);
}

struct ast_sip_contact *ast_sip_location_create_contact(struct ast_sip_aor *aor,
	const char *uri, struct timeval expiration_time, const char *path_info,
	const char *user_agent, const char *via_addr, int via_port, const char *call_id,
	int prune_on_boot, struct ast_sip_endpoint *endpoint)
{
	struct ast_sip_contact *contact;
	char name[MAX_OBJECT_FIELD * 2 + 3];
	char hash[33];

	ast_md5_hash(hash, uri);
	snprintf(name, sizeof(name), "%s;@%s", ast_sorcery_object_get_id(aor), hash);

	contact = ast_sorcery_alloc(ast_sip_get_sorcery(), "contact", name);
	if (!contact) {
		return NULL;
	}

	ast_string_field_set(contact, uri, uri);
	contact->expiration_time = expiration_time;
	contact->qualify_frequency = aor->qualify_frequency;
	contact->qualify_timeout = aor->qualify_timeout;
	contact->authenticate_qualify = aor->authenticate_qualify;
	if (path_info && aor->support_path) {
		ast_string_field_set(contact, path, path_info);
	}

	if (!ast_strlen_zero(aor->outbound_proxy)) {
		ast_string_field_set(contact, outbound_proxy, aor->outbound_proxy);
	}

	if (!ast_strlen_zero(user_agent)) {
		ast_string_field_set(contact, user_agent, user_agent);
	}

	if (!ast_strlen_zero(ast_config_AST_SYSTEM_NAME)) {
		ast_string_field_set(contact, reg_server, ast_config_AST_SYSTEM_NAME);
	}

	if (!ast_strlen_zero(via_addr)) {
		ast_string_field_set(contact, via_addr, via_addr);
	}
	contact->via_port = via_port;

	if (!ast_strlen_zero(call_id)) {
		ast_string_field_set(contact, call_id, call_id);
	}

	contact->endpoint = ao2_bump(endpoint);
	if (endpoint) {
		ast_string_field_set(contact, endpoint_name, ast_sorcery_object_get_id(endpoint));
	}

	contact->prune_on_boot = prune_on_boot;

	if (ast_sorcery_create(ast_sip_get_sorcery(), contact)) {
		ao2_ref(contact, -1);
		return NULL;
	}
	return contact;
}

int ast_sip_location_add_contact_nolock(struct ast_sip_aor *aor, const char *uri,
		struct timeval expiration_time, const char *path_info, const char *user_agent,
		const char *via_addr, int via_port, const char *call_id,
		struct ast_sip_endpoint *endpoint)
{
	struct ast_sip_contact *contact;

	contact = ast_sip_location_create_contact(aor, uri, expiration_time, path_info,
		user_agent, via_addr, via_port, call_id, 0, endpoint);
	ao2_cleanup(contact);
	return contact ? 0 : -1;
}

int ast_sip_location_add_contact(struct ast_sip_aor *aor, const char *uri,
		struct timeval expiration_time, const char *path_info, const char *user_agent,
		const char *via_addr, int via_port, const char *call_id,
		struct ast_sip_endpoint *endpoint)
{
	int res;

	ao2_lock(aor);
	res = ast_sip_location_add_contact_nolock(aor, uri, expiration_time, path_info, user_agent,
		via_addr, via_port, call_id,
		endpoint);
	ao2_unlock(aor);

	return res;
}

int ast_sip_location_update_contact(struct ast_sip_contact *contact)
{
	return ast_sorcery_update(ast_sip_get_sorcery(), contact);
}

int ast_sip_location_delete_contact(struct ast_sip_contact *contact)
{
	return ast_sorcery_delete(ast_sip_get_sorcery(), contact);
}

static int prune_boot_contacts_cb(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;

	if (contact->prune_on_boot
		&& !strcmp(contact->reg_server, ast_config_AST_SYSTEM_NAME ?: "")) {
		ast_verb(3, "Removed contact '%s' from AOR '%s' due to system boot\n",
			contact->uri, contact->aor);
		ast_sip_location_delete_contact(contact);
	}

	return 0;
}

void ast_sip_location_prune_boot_contacts(void)
{
	struct ao2_container *contacts;

	contacts = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "contact",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (contacts) {
		ao2_callback(contacts, 0, prune_boot_contacts_cb, NULL);
		ao2_ref(contacts, -1);
	}
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
	return (ast_asprintf(buf, "%ld", contact->expiration_time.tv_sec) < 0) ? -1 : 0;
}

static int permanent_uri_sort_fn(const void *obj_left, const void *obj_right, int flags)
{
	const struct ast_sip_contact *object_left = obj_left;
	const struct ast_sip_contact *object_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = ast_sorcery_object_get_id(object_right);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(ast_sorcery_object_get_id(object_left), right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/*
		 * We could also use a partial key struct containing a length
		 * so strlen() does not get called for every comparison instead.
		 */
		cmp = strncmp(ast_sorcery_object_get_id(object_left), right_key, strlen(right_key));
		break;
	default:
		/* Sort can only work on something with a full or partial key. */
		ast_assert(0);
		cmp = 0;
		break;
	}
	return cmp;
}

int ast_sip_validate_uri_length(const char *contact_uri)
{
	int max_length = pj_max_hostname - 1;
	char *contact = ast_strdupa(contact_uri);
	char *host;
	char *at;
	int theres_a_port = 0;

	if (strlen(contact_uri) > pjsip_max_url_size - 1) {
		return -1;
	}

	contact = ast_strip_quoted(contact, "<", ">");

	if (!strncasecmp(contact, "sip:", 4)) {
		host = contact + 4;
	} else if (!strncasecmp(contact, "sips:", 5)) {
		host = contact + 5;
	} else {
		/* Not a SIP URI */
		return -1;
	}

	at = strchr(contact, '@');
	if (at) {
		/* sip[s]:user@host */
		host = at + 1;
	}

	if (host[0] == '[') {
		/* Host is an IPv6 address. Just get up to the matching bracket */
		char *close_bracket;

		close_bracket = strchr(host, ']');
		if (!close_bracket) {
			return -1;
		}
		close_bracket++;
		if (*close_bracket == ':') {
			theres_a_port = 1;
		}
		*close_bracket = '\0';
	} else {
		/* uri parameters could contain ';' so trim them off first */
		host = strsep(&host, ";?");
		/* Host is FQDN or IPv4 address. Need to find closing delimiter */
		if (strchr(host, ':')) {
			theres_a_port = 1;
			host = strsep(&host, ":");
		}
	}

	if (!theres_a_port) {
		max_length -= strlen("_sips.tcp.");
	}

	if (strlen(host) > max_length) {
		return -1;
	}

	return 0;
}

/*! \brief Custom handler for permanent URIs */
static int permanent_uri_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_aor *aor = obj;
	const char *aor_id = ast_sorcery_object_get_id(aor);
	char *contacts;
	char *contact_uri;

	if (ast_strlen_zero(var->value)) {
		return 0;
	}

	contacts = ast_strdupa(var->value);
	while ((contact_uri = ast_strip(strsep(&contacts, ",")))) {
		struct ast_sip_contact *contact;
		struct ast_sip_contact_status *status;
		char hash[33];
		char contact_id[strlen(aor_id) + sizeof(hash) + 2];

		if (ast_strlen_zero(contact_uri)) {
			continue;
		}

		if (ast_sip_validate_uri_length(contact_uri)) {
			ast_log(LOG_ERROR, "Contact uri or hostname length exceeds pjproject limit or is not a sip(s) uri: %s\n", contact_uri);
			return -1;
		}

		if (!aor->permanent_contacts) {
			aor->permanent_contacts = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK,
				AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, permanent_uri_sort_fn, NULL);
			if (!aor->permanent_contacts) {
				return -1;
			}
		}

		ast_md5_hash(hash, contact_uri);
		snprintf(contact_id, sizeof(contact_id), "%s@@%s", aor_id, hash);
		contact = ast_sorcery_alloc(ast_sip_get_sorcery(), "contact", contact_id);
		if (!contact) {
			return -1;
		}

		ast_string_field_set(contact, uri, contact_uri);

		status = ast_res_pjsip_find_or_create_contact_status(contact);
		if (!status) {
			ao2_ref(contact, -1);
			return -1;
		}
		ao2_ref(status, -1);

		ao2_link(aor->permanent_contacts, contact);
		ao2_ref(contact, -1);
	}

	return 0;
}

static int contact_to_var_list(void *object, void *arg, int flags)
{
	struct ast_sip_contact_wrapper *wrapper = object;
	struct ast_variable **var = arg;

	ast_variable_list_append(&*var, ast_variable_new("contact", wrapper->contact->uri, ""));

	return 0;
}

static int contacts_to_var_list(const void *obj, struct ast_variable **fields)
{
	const struct ast_sip_aor *aor = obj;

	ast_sip_for_each_contact(aor, contact_to_var_list, fields);

	return 0;
}

static int voicemail_extension_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_aor *aor = obj;

	aor->voicemail_extension = ast_strdup(var->value);

	return aor->voicemail_extension ? 0 : -1;
}

static int voicemail_extension_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_aor *aor = obj;

	*buf = ast_strdup(aor->voicemail_extension);

	return 0;
}

int ast_sip_for_each_aor(const char *aors, ao2_callback_fn on_aor, void *arg)
{
	char *copy;
	char *name;
	int res;

	if (!on_aor || ast_strlen_zero(aors)) {
		return 0;
	}

	copy = ast_strdupa(aors);
	while ((name = ast_strip(strsep(&copy, ",")))) {
		struct ast_sip_aor *aor;

		aor = ast_sip_location_retrieve_aor(name);
		if (aor) {
			res = on_aor(aor, arg, 0);
			ao2_ref(aor, -1);
			if (res) {
				return -1;
			}
		}
	}
	return 0;
}

static void contact_wrapper_destroy(void *obj)
{
	struct ast_sip_contact_wrapper *wrapper = obj;

	ast_free(wrapper->aor_id);
	ast_free(wrapper->contact_id);
	ao2_cleanup(wrapper->contact);
}

int ast_sip_for_each_contact(const struct ast_sip_aor *aor,
		ao2_callback_fn on_contact, void *arg)
{
	struct ao2_container *contacts;
	struct ao2_iterator i;
	int res = 0;
	void *object = NULL;

	if (!on_contact ||
	    !(contacts = ast_sip_location_retrieve_aor_contacts(aor))) {
		return 0;
	}

	i = ao2_iterator_init(contacts, 0);
	while ((object = ao2_iterator_next(&i))) {
		RAII_VAR(struct ast_sip_contact *, contact, object, ao2_cleanup);
		RAII_VAR(struct ast_sip_contact_wrapper *, wrapper, NULL, ao2_cleanup);
		const char *aor_id = ast_sorcery_object_get_id(aor);

		wrapper = ao2_alloc_options(sizeof(struct ast_sip_contact_wrapper),
			contact_wrapper_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
		if (!wrapper) {
			res = -1;
			break;
		}
		wrapper->contact_id = ast_malloc(strlen(aor_id) + strlen(contact->uri) + 2);
		if (!wrapper->contact_id) {
			res = -1;
			break;
		}
		sprintf(wrapper->contact_id, "%s/%s", aor_id, contact->uri);
		wrapper->aor_id = ast_strdup(aor_id);
		if (!wrapper->aor_id) {
			res = -1;
			break;
		}
		wrapper->contact = contact;
		ao2_bump(wrapper->contact);

		if ((res = on_contact(wrapper, arg, 0))) {
			break;
		}
	}
	ao2_iterator_destroy(&i);
	ao2_ref(contacts, -1);
	return res;
}

int ast_sip_contact_to_str(void *object, void *arg, int flags)
{
	struct ast_sip_contact_wrapper *wrapper = object;
	struct ast_str **buf = arg;

	ast_str_append(buf, 0, "%s,", wrapper->contact_id);

	return 0;
}

static int sip_aor_to_ami(const struct ast_sip_aor *aor, struct ast_str **buf)
{
	struct ast_variable *objset;
	struct ast_variable *i;

	objset = ast_sorcery_objectset_create2(ast_sip_get_sorcery(), aor,
		AST_HANDLER_ONLY_STRING);
	if (!objset) {
		return -1;
	}

	ast_str_append(buf, 0, "ObjectType: %s\r\n",
		       ast_sorcery_object_get_type(aor));
	ast_str_append(buf, 0, "ObjectName: %s\r\n",
		       ast_sorcery_object_get_id(aor));

	for (i = objset; i; i = i->next) {
		char *camel = ast_to_camel_case(i->name);

		if (strcmp(camel, "Contact") == 0) {
			ast_free(camel);
			camel = NULL;
		}
		ast_str_append(buf, 0, "%s: %s\r\n", S_OR(camel, "Contacts"), i->value);
		ast_free(camel);
	}

	ast_variables_destroy(objset);
	return 0;
}

static int contacts_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_aor *aor = obj;
	struct ast_str *str;

	str = ast_str_create(MAX_OBJECT_FIELD);
	if (!str) {
		*buf = NULL;
		return -1;
	}

	ast_sip_for_each_contact(aor, ast_sip_contact_to_str, &str);
	ast_str_truncate(str, -1);

	*buf = ast_strdup(ast_str_buffer(str));
	ast_free(str);

	return *buf ? 0 : -1;
}

static int format_ami_aor_handler(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_sip_ami *ami = arg;
	const struct ast_sip_endpoint *endpoint = ami->arg;
	struct ast_str *buf;
	struct ao2_container *contacts;
	int total_contacts;
	int num_permanent;

	buf = ast_sip_create_ami_event("AorDetail", ami);
	if (!buf) {
		return -1;
	}
	contacts = ast_sip_location_retrieve_aor_contacts(aor);
	if (!contacts) {
		ast_free(buf);
		return -1;
	}

	sip_aor_to_ami(aor, &buf);
	total_contacts = ao2_container_count(contacts);
	num_permanent = aor->permanent_contacts ?
		ao2_container_count(aor->permanent_contacts) : 0;

	ast_str_append(&buf, 0, "TotalContacts: %d\r\n", total_contacts);
	ast_str_append(&buf, 0, "ContactsRegistered: %d\r\n",
		       total_contacts - num_permanent);
	ast_str_append(&buf, 0, "EndpointName: %s\r\n",
		       ast_sorcery_object_get_id(endpoint));

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ami->count++;

	ast_free(buf);
	ao2_ref(contacts, -1);
	return 0;
}

static int format_ami_endpoint_aor(const struct ast_sip_endpoint *endpoint,
				   struct ast_sip_ami *ami)
{
	ami->arg = (void *)endpoint;
	return ast_sip_for_each_aor(endpoint->aors,
				    format_ami_aor_handler, ami);
}

struct ast_sip_endpoint_formatter endpoint_aor_formatter = {
	.format_ami = format_ami_endpoint_aor
};

static struct ao2_container *cli_aor_get_container(const char *regex)
{
	struct ao2_container *container;
	struct ao2_container *s_container;

	container = ast_sorcery_retrieve_by_regex(ast_sip_get_sorcery(), "aor", regex);
	if (!container) {
		return NULL;
	}

	/* Create a sorted container of aors. */
	s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, ast_sorcery_object_id_compare);
	if (s_container
		&& ao2_container_dup(s_container, container, 0)) {
		ao2_ref(s_container, -1);
		s_container = NULL;
	}
	ao2_ref(container, -1);

	return s_container;
}

static int cli_contact_populate_container(void *obj, void *arg, int flags)
{
	ao2_link(arg, obj);

	return 0;
}

static int cli_aor_gather_contacts(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;

	return ast_sip_for_each_contact(aor, cli_contact_populate_container, arg);
}

static const char *cli_contact_get_id(const void *obj)
{
	const struct ast_sip_contact_wrapper *wrapper = obj;
	return wrapper->contact_id;
}

static int cli_contact_sort(const void *obj, const void *arg, int flags)
{
	const struct ast_sip_contact_wrapper *left_wrapper = obj;
	const struct ast_sip_contact_wrapper *right_wrapper = arg;
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right_wrapper->contact_id;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left_wrapper->contact_id, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left_wrapper->contact_id, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int cli_contact_compare(void *obj, void *arg, int flags)
{
	const struct ast_sip_contact_wrapper *left_wrapper = obj;
	const struct ast_sip_contact_wrapper *right_wrapper = arg;
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = right_wrapper->contact_id;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		if (strcmp(left_wrapper->contact_id, right_key) == 0) {;
			cmp = CMP_MATCH | CMP_STOP;
		}
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		if (strncmp(left_wrapper->contact_id, right_key, strlen(right_key)) == 0) {
			cmp = CMP_MATCH;
		}
		break;
	default:
		cmp = 0;
		break;
	}

	return cmp;
}

static int cli_contact_iterate(void *container, ao2_callback_fn callback, void *args)
{
	return ast_sip_for_each_contact(container, callback, args);
}

static int cli_filter_contacts(void *obj, void *arg, int flags)
{
	struct ast_sip_contact_wrapper *wrapper = obj;
	regex_t *regexbuf = arg;

	if (!regexec(regexbuf, wrapper->contact_id, 0, NULL, 0)) {
		return 0;
	}

	return CMP_MATCH;
}

static int cli_gather_contact(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	RAII_VAR(struct ast_sip_contact_wrapper *, wrapper, NULL, ao2_cleanup);

	if (strcmp(contact->reg_server, ast_config_AST_SYSTEM_NAME ?: "")) {
		return 0;
	}

	wrapper = ao2_alloc_options(sizeof(struct ast_sip_contact_wrapper),
		contact_wrapper_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!wrapper) {
		return -1;
	}

	wrapper->contact_id = ast_malloc(strlen(contact->aor) + strlen(contact->uri) + 2);
	if (!wrapper->contact_id) {
		return -1;
	}
	sprintf(wrapper->contact_id, "%s/%s", contact->aor, contact->uri);

	wrapper->aor_id = ast_strdup(contact->aor);
	if (!wrapper->aor_id) {
		return -1;
	}

	wrapper->contact = ao2_bump(contact);

	ao2_link(arg, wrapper);

	return 0;
}

static struct ao2_container *cli_contact_get_container(const char *regex)
{
	RAII_VAR(struct ao2_container *, aors, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, var_aor, NULL, ast_variables_destroy);
	struct ao2_container *contacts_container;
	regex_t regexbuf;

	if (!(var_aor = ast_variable_new("contact !=", "", ""))) {
		return NULL;
	}

	/* Retrieving all the contacts may result in finding the same contact multiple
	 * times. So that they don't get displayed multiple times we only allow a
	 * single one to be placed into the container.
	 */
	contacts_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT,
		cli_contact_sort, cli_contact_compare);
	if (!contacts_container) {
		return NULL;
	}

	contacts = ast_sorcery_retrieve_by_regex(ast_sip_get_sorcery(), "contact", regex);
	if (!contacts) {
		ao2_ref(contacts_container, -1);
		return NULL;
	}
	ao2_callback(contacts, OBJ_NODATA, cli_gather_contact, contacts_container);

	aors = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(),
		"aor", AST_RETRIEVE_FLAG_MULTIPLE, var_aor);
	if (!aors) {
		ao2_ref(contacts_container, -1);
		return NULL;
	}

	ao2_callback(aors, OBJ_NODATA, cli_aor_gather_contacts, contacts_container);

	if (!ast_strlen_zero(regex)) {
		if (regcomp(&regexbuf, regex, REG_EXTENDED | REG_NOSUB)) {
			ao2_ref(contacts_container, -1);
			return NULL;
		}
		ao2_callback(contacts_container, OBJ_UNLINK | OBJ_MULTIPLE | OBJ_NODATA, cli_filter_contacts, &regexbuf);
		regfree(&regexbuf);
	}

	return contacts_container;
}

static void *cli_contact_retrieve_by_id(const char *id)
{
	struct ao2_container *container;
	void *obj;

	container = cli_contact_get_container("");
	if (!container) {
		return NULL;
	}

	obj = ao2_find(container, id, OBJ_SEARCH_KEY);
	ao2_ref(container, -1);
	return obj;
}

static int cli_contact_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 23;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <Aor/ContactUri%*.*s> <Hash....> <Status> <RTT(ms)..>\n",
		indent, "Contact", filler, filler, CLI_HEADER_FILLER);

	return 0;
}

static int cli_contact_print_body(void *obj, void *arg, int flags)
{
	struct ast_sip_contact_wrapper *wrapper = obj;
	struct ast_sip_contact *contact = wrapper->contact;
	struct ast_sip_cli_context *context = arg;
	int indent;
	int flexwidth;
	const char *contact_id = ast_sorcery_object_get_id(contact);
	const char *hash_start = contact_id + strlen(contact->aor) + 2;
	struct ast_sip_contact_status *status;

	ast_assert(contact->uri != NULL);
	ast_assert(context->output_buffer != NULL);

	status = ast_sip_get_contact_status(contact);

	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - indent - 9 - strlen(contact->aor) + 1;

	ast_str_append(&context->output_buffer, 0, "%*s:  %s/%-*.*s %-10.10s %-7.7s %11.3f\n",
		indent,
		"Contact",
		contact->aor,
		flexwidth, flexwidth,
		contact->uri,
		hash_start,
		ast_sip_get_contact_short_status_label(status ? status->status : UNKNOWN),
		(status && (status->status == AVAILABLE)) ? ((long long) status->rtt) / 1000.0 : NAN);

	ao2_cleanup(status);
	return 0;
}

static int cli_aor_iterate(void *container, ao2_callback_fn callback, void *args)
{
	const char *aor_list = container;

	return ast_sip_for_each_aor(aor_list, callback, args);
}

static void *cli_aor_retrieve_by_id(const char *id)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "aor", id);
}

static const char *cli_aor_get_id(const void *obj)
{
	return ast_sorcery_object_get_id(obj);
}

static int cli_aor_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 7;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <Aor%*.*s>  <MaxContact>\n",
		indent, "Aor", filler, filler, CLI_HEADER_FILLER);

	if (context->recurse) {
		struct ast_sip_cli_formatter_entry *formatter_entry;

		context->indent_level++;
		formatter_entry = ast_sip_lookup_cli_formatter("contact");
		if (formatter_entry) {
			formatter_entry->print_header(NULL, context, 0);
			ao2_ref(formatter_entry, -1);
		}
		context->indent_level--;
	}

	return 0;
}

static int cli_aor_print_body(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_sip_cli_context *context = arg;
	int indent;
	int flexwidth;

	ast_assert(context->output_buffer != NULL);

//	context->current_aor = aor;

	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - indent - 12;

	ast_str_append(&context->output_buffer, 0, "%*s:  %-*.*s %12u\n",
		indent,
		"Aor",
		flexwidth, flexwidth,
		ast_sorcery_object_get_id(aor), aor->max_contacts);

	if (context->recurse) {
		struct ast_sip_cli_formatter_entry *formatter_entry;

		context->indent_level++;

		formatter_entry = ast_sip_lookup_cli_formatter("contact");
		if (formatter_entry) {
			formatter_entry->iterate(aor, formatter_entry->print_body, context);
			ao2_ref(formatter_entry, -1);
		}

		context->indent_level--;

		if (context->indent_level == 0) {
			ast_str_append(&context->output_buffer, 0, "\n");
		}
	}

	if (context->show_details || (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(aor, context, 0);
	}

	return 0;
}

static struct ao2_container *cli_get_aors(void)
{
	struct ao2_container *aors;

	aors = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "aor",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	return aors;
}

static int format_ami_aorlist_handler(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_sip_ami *ami = arg;
	struct ast_str *buf;

	buf = ast_sip_create_ami_event("AorList", ami);
	if (!buf) {
		return -1;
	}

	sip_aor_to_ami(aor, &buf);

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ami->count++;

	ast_free(buf);

	return 0;
}

static int ami_show_aors(struct mansession *s, const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"), };
	struct ao2_container *aors;

	aors = cli_get_aors();
	if (!aors) {
		astman_send_error(s, m, "Could not get AORs\n");
		return 0;
	}

	if (!ao2_container_count(aors)) {
		astman_send_error(s, m, "No AORs found\n");
		ao2_ref(aors, -1);
		return 0;
	}

	astman_send_listack(s, m, "A listing of AORs follows, presented as AorList events",
			"start");

	ao2_callback(aors, OBJ_NODATA, format_ami_aorlist_handler, &ami);

	astman_send_list_complete_start(s, m, "AorListComplete", ami.count);
	astman_send_list_complete_end(s);

	ao2_ref(aors, -1);

	return 0;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Aors",
		.command = "pjsip list aors",
		.usage = "Usage: pjsip list aors [ like <pattern> ]\n"
				"       List the configured PJSIP Aors\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Aors",
		.command = "pjsip show aors",
		.usage = "Usage: pjsip show aors [ like <pattern> ]\n"
				"       Show the configured PJSIP Aors\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Aor",
		.command = "pjsip show aor",
		.usage = "Usage: pjsip show aor <id>\n"
				 "       Show the configured PJSIP Aor\n"),

	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Contacts",
		.command = "pjsip list contacts",
		.usage = "Usage: pjsip list contacts [ like <pattern> ]\n"
				"       List the configured PJSIP contacts\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Contacts",
		.command = "pjsip show contacts",
		.usage = "Usage: pjsip show contacts [ like <pattern> ]\n"
				"       Show the configured PJSIP contacts\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Contact",
		.command = "pjsip show contact",
		.usage = "Usage: pjsip show contact\n"
				 "       Show the configured PJSIP contact\n"),
};

struct ast_sip_cli_formatter_entry *contact_formatter;
struct ast_sip_cli_formatter_entry *aor_formatter;

/*! \brief Always create a contact_status for each contact */
static int contact_apply_handler(const struct ast_sorcery *sorcery, void *object)
{
	struct ast_sip_contact_status *status;
	struct ast_sip_contact *contact = object;

	if (ast_strlen_zero(contact->uri)) {
		ast_log(LOG_ERROR, "A URI on dynamic contact '%s' is empty\n",
			ast_sorcery_object_get_id(contact));
		return -1;
	}
	status = ast_res_pjsip_find_or_create_contact_status(contact);
	ao2_cleanup(status);

	return status ? 0 : -1;
}

/*! \brief Initialize sorcery with location support */
int ast_sip_initialize_sorcery_location(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();
	int i;

	ast_pjproject_get_buildopt("PJ_MAX_HOSTNAME", "%d", &pj_max_hostname);
	/* As of pjproject 2.4.5, PJSIP_MAX_URL_SIZE isn't exposed yet but we try anyway. */
	ast_pjproject_get_buildopt("PJSIP_MAX_URL_SIZE", "%d", &pjsip_max_url_size);

	ast_sorcery_apply_default(sorcery, "contact", "astdb", "registrar");
	ast_sorcery_object_set_congestion_levels(sorcery, "contact", -1,
		3 * AST_TASKPROCESSOR_HIGH_WATER_LEVEL);
	ast_sorcery_apply_default(sorcery, "aor", "config", "pjsip.conf,criteria=type=aor");

	if (ast_sorcery_object_register(sorcery, "contact", contact_alloc, NULL, contact_apply_handler) ||
		ast_sorcery_object_register(sorcery, "aor", aor_alloc, NULL, NULL)) {
		return -1;
	}

	ast_sorcery_observer_add(sorcery, "aor", &aor_observer);

	ast_sorcery_object_field_register(sorcery, "contact", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "contact", "uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, uri));
	ast_sorcery_object_field_register(sorcery, "contact", "path", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, path));
	ast_sorcery_object_field_register_custom(sorcery, "contact", "expiration_time", "", expiration_str2struct, expiration_struct2str, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "contact", "qualify_frequency", 0, OPT_UINT_T,
		PARSE_IN_RANGE, FLDSET(struct ast_sip_contact, qualify_frequency), 0, 86400);
	ast_sorcery_object_field_register(sorcery, "contact", "qualify_timeout", "3.0", OPT_DOUBLE_T, 0, FLDSET(struct ast_sip_contact, qualify_timeout));
	ast_sorcery_object_field_register(sorcery, "contact", "authenticate_qualify", "no", OPT_YESNO_T, 1, FLDSET(struct ast_sip_contact, authenticate_qualify));
	ast_sorcery_object_field_register(sorcery, "contact", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, outbound_proxy));
	ast_sorcery_object_field_register(sorcery, "contact", "user_agent", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, user_agent));
	ast_sorcery_object_field_register(sorcery, "contact", "endpoint", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, endpoint_name));
	ast_sorcery_object_field_register(sorcery, "contact", "reg_server", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, reg_server));
	ast_sorcery_object_field_register(sorcery, "contact", "via_addr", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, via_addr));
	ast_sorcery_object_field_register(sorcery, "contact", "via_port", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_contact, via_port));
	ast_sorcery_object_field_register(sorcery, "contact", "call_id", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, call_id));
	ast_sorcery_object_field_register(sorcery, "contact", "prune_on_boot", "no", OPT_YESNO_T, 1, FLDSET(struct ast_sip_contact, prune_on_boot));

	ast_sorcery_object_field_register(sorcery, "aor", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "aor", "minimum_expiration", "60", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, minimum_expiration));
	ast_sorcery_object_field_register(sorcery, "aor", "maximum_expiration", "7200", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, maximum_expiration));
	ast_sorcery_object_field_register(sorcery, "aor", "default_expiration", "3600", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, default_expiration));
	ast_sorcery_object_field_register(sorcery, "aor", "qualify_frequency", 0, OPT_UINT_T, PARSE_IN_RANGE, FLDSET(struct ast_sip_aor, qualify_frequency), 0, 86400);
	ast_sorcery_object_field_register(sorcery, "aor", "qualify_timeout", "3.0", OPT_DOUBLE_T, 0, FLDSET(struct ast_sip_aor, qualify_timeout));
	ast_sorcery_object_field_register(sorcery, "aor", "authenticate_qualify", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_aor, authenticate_qualify));
	ast_sorcery_object_field_register(sorcery, "aor", "max_contacts", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, max_contacts));
	ast_sorcery_object_field_register(sorcery, "aor", "remove_existing", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_aor, remove_existing));
	ast_sorcery_object_field_register_custom(sorcery, "aor", "contact", "", permanent_uri_handler, contacts_to_str, contacts_to_var_list, 0, 0);
	ast_sorcery_object_field_register(sorcery, "aor", "mailboxes", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_aor, mailboxes));
	ast_sorcery_object_field_register_custom(sorcery, "aor", "voicemail_extension", "", voicemail_extension_handler, voicemail_extension_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "aor", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_aor, outbound_proxy));
	ast_sorcery_object_field_register(sorcery, "aor", "support_path", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_aor, support_path));

	ast_sip_register_endpoint_formatter(&endpoint_aor_formatter);

	contact_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!contact_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for contact_formatter\n");
		return -1;
	}
	contact_formatter->name = "contact";
	contact_formatter->print_header = cli_contact_print_header;
	contact_formatter->print_body = cli_contact_print_body;
	contact_formatter->get_container = cli_contact_get_container;
	contact_formatter->iterate = cli_contact_iterate;
	contact_formatter->get_id = cli_contact_get_id;
	contact_formatter->retrieve_by_id = cli_contact_retrieve_by_id;

	aor_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!aor_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for aor_formatter\n");
		return -1;
	}
	aor_formatter->name = "aor";
	aor_formatter->print_header = cli_aor_print_header;
	aor_formatter->print_body = cli_aor_print_body;
	aor_formatter->get_container = cli_aor_get_container;
	aor_formatter->iterate = cli_aor_iterate;
	aor_formatter->get_id = cli_aor_get_id;
	aor_formatter->retrieve_by_id = cli_aor_retrieve_by_id;

	ast_sip_register_cli_formatter(contact_formatter);
	ast_sip_register_cli_formatter(aor_formatter);
	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	if (ast_manager_register_xml("PJSIPShowAors", EVENT_FLAG_SYSTEM, ami_show_aors)) {
		return -1;
	}

	/*
	 * Reset StatsD gauges in case we didn't shut down cleanly.
	 * Note that this must done here, as contacts will create the contact_status
	 * object before PJSIP options handling is initialized.
	 */
	for (i = 0; i <= REMOVED; i++) {
		ast_statsd_log_full_va("PJSIP.contacts.states.%s", AST_STATSD_GAUGE, 0, 1.0, ast_sip_get_contact_status_label(i));
	}

	return 0;
}

int ast_sip_destroy_sorcery_location(void)
{
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "aor", &aor_observer);
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sip_unregister_cli_formatter(contact_formatter);
	ast_sip_unregister_cli_formatter(aor_formatter);
	ast_manager_unregister("PJSIPShowAors");

	ast_sip_unregister_endpoint_formatter(&endpoint_aor_formatter);

	return 0;
}
