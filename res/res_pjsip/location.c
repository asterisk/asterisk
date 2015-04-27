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

#include "asterisk/res_pjsip.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/sorcery.h"
#include "include/res_pjsip_private.h"
#include "asterisk/res_pjsip_cli.h"

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
	struct ast_sip_aor *aor = ast_sorcery_generic_alloc(sizeof(struct ast_sip_aor), aor_destroy);
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
	struct ast_sip_contact *contact = ast_sorcery_generic_alloc(sizeof(*contact), contact_destroy);

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

	ao2_link(dest, obj);
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

	contact = ao2_callback(contacts, 0, contact_find_first, NULL);
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
	ao2_callback(contacts, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK, contact_expire, NULL);

	/* Add any permanent contacts from the AOR */
	if (aor->permanent_contacts) {
		ao2_callback(aor->permanent_contacts, OBJ_NODATA, contact_link_static, contacts);
	}

	return contacts;
}

void ast_sip_location_retrieve_contact_and_aor_from_list(const char *aor_list, struct ast_sip_aor **aor,
	struct ast_sip_contact **contact)
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

	while ((aor_name = strsep(&rest, ","))) {
		*aor = ast_sip_location_retrieve_aor(aor_name);

		if (!(*aor)) {
			continue;
		}
		*contact = ast_sip_location_retrieve_first_aor_contact(*aor);
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

int ast_sip_location_add_contact(struct ast_sip_aor *aor, const char *uri,
		struct timeval expiration_time, const char *path_info, const char *user_agent)
{
	char name[MAX_OBJECT_FIELD * 2 + 3];
	RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);

	snprintf(name, sizeof(name), "%s;@%s", ast_sorcery_object_get_id(aor), uri);

	if (!(contact = ast_sorcery_alloc(ast_sip_get_sorcery(), "contact", name))) {
		return -1;
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
	return (ast_asprintf(buf, "%ld", contact->expiration_time.tv_sec) < 0) ? -1 : 0;
}

/*! \brief Helper function which validates a permanent contact */
static int permanent_contact_validate(void *data)
{
	const char *value = data;
	pj_pool_t *pool;
	pj_str_t contact_uri;
	static const pj_str_t HCONTACT = { "Contact", 7 };
	pjsip_contact_hdr *contact_hdr;
	int rc = 0;

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Permanent Contact Validation", 256, 256);
	if (!pool) {
		return -1;
	}

	pj_strdup2_with_null(pool, &contact_uri, value);
	if (!(contact_hdr = pjsip_parse_hdr(pool, &HCONTACT, contact_uri.ptr, contact_uri.slen, NULL))
		|| !(PJSIP_URI_SCHEME_IS_SIP(contact_hdr->uri)
			|| PJSIP_URI_SCHEME_IS_SIPS(contact_hdr->uri))) {
		rc = -1;
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
	return rc;
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
	while ((contact_uri = strsep(&contacts, ","))) {
		struct ast_sip_contact *contact;
		char contact_id[strlen(aor_id) + strlen(contact_uri) + 2 + 1];

		if (ast_sip_push_task_synchronous(NULL, permanent_contact_validate, contact_uri)) {
			ast_log(LOG_ERROR, "Permanent URI on aor '%s' with contact '%s' failed to parse\n",
				ast_sorcery_object_get_id(aor), contact_uri);
			return -1;
		}

		if (!aor->permanent_contacts) {
			aor->permanent_contacts = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK,
				AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, permanent_uri_sort_fn, NULL);
			if (!aor->permanent_contacts) {
				return -1;
			}
		}

		snprintf(contact_id, sizeof(contact_id), "%s@@%s", aor_id, contact_uri);
		contact = ast_sorcery_alloc(ast_sip_get_sorcery(), "contact", contact_id);
		if (!contact) {
			return -1;
		}

		if (!ast_res_pjsip_find_or_create_contact_status(contact)) {
			ao2_ref(contact, -1);
			return -1;
		}

		ast_string_field_set(contact, uri, contact_uri);
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

int ast_sip_for_each_aor(const char *aors, ao2_callback_fn on_aor, void *arg)
{
	char *copy, *name;

	if (!on_aor || ast_strlen_zero(aors)) {
		return 0;
	}

	copy = ast_strdupa(aors);
	while ((name = strsep(&copy, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor,
			 ast_sip_location_retrieve_aor(name), ao2_cleanup);

		if (!aor) {
			continue;
		}

		if (on_aor(aor, arg, 0)) {
			return -1;
		}
	}
	ast_free(copy);
	return 0;
}

static void contact_wrapper_destroy(void *obj)
{
	struct ast_sip_contact_wrapper *wrapper = obj;
	ast_free(wrapper->aor_id);
	ast_free(wrapper->contact_id);
	ao2_ref(wrapper->contact, -1);
}

int ast_sip_for_each_contact(const struct ast_sip_aor *aor,
		ao2_callback_fn on_contact, void *arg)
{
	RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
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

		wrapper = ao2_alloc(sizeof(struct ast_sip_contact_wrapper), contact_wrapper_destroy);
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
	RAII_VAR(struct ast_variable *, objset, ast_sorcery_objectset_create2(
			 ast_sip_get_sorcery(), aor, AST_HANDLER_ONLY_STRING), ast_variables_destroy);
	struct ast_variable *i;

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

	return 0;
}

static int contacts_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_aor *aor = obj;
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);

	ast_sip_for_each_contact(aor, ast_sip_contact_to_str, &str);
	ast_str_truncate(str, -1);

	*buf = ast_strdup(ast_str_buffer(str));
	if (!*buf) {
		return -1;
	}

	return 0;
}

static int format_ami_aor_handler(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_sip_ami *ami = arg;
	const struct ast_sip_endpoint *endpoint = ami->arg;
	RAII_VAR(struct ast_str *, buf,
		 ast_sip_create_ami_event("AorDetail", ami), ast_free);

	int total_contacts;
	int num_permanent;
	RAII_VAR(struct ao2_container *, contacts,
		 ast_sip_location_retrieve_aor_contacts(aor), ao2_cleanup);

	if (!buf) {
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

static struct ao2_container *cli_aor_get_container(void)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	struct ao2_container *s_container;

	container = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "aor",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!container) {
		return NULL;
	}

	s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, ast_sorcery_object_id_compare);
	if (!s_container) {
		return NULL;
	}

	if (ao2_container_dup(s_container, container, 0)) {
		ao2_ref(s_container, -1);
		return NULL;
	}

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

static struct ao2_container *cli_contact_get_container(void)
{
	RAII_VAR(struct ao2_container *, parent_container, NULL, ao2_cleanup);
	struct ao2_container *child_container;

	parent_container =  cli_aor_get_container();
	if (!parent_container) {
		return NULL;
	}

	child_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		cli_contact_sort, cli_contact_compare);
	if (!child_container) {
		return NULL;
	}

	ao2_callback(parent_container, OBJ_NODATA, cli_aor_gather_contacts, child_container);

	return child_container;
}

static void *cli_contact_retrieve_by_id(const char *id)
{
	return ao2_find(cli_contact_get_container(), id, OBJ_KEY | OBJ_NOLOCK);
}

static int cli_contact_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 18;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <Aor/ContactUri%*.*s>  <Status....>  <RTT(ms)..>\n",
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

	RAII_VAR(struct ast_sip_contact_status *, status,
		ast_sorcery_retrieve_by_id( ast_sip_get_sorcery(), CONTACT_STATUS, ast_sorcery_object_get_id(contact)),
		ao2_cleanup);

	ast_assert(contact->uri != NULL);
	ast_assert(context->output_buffer != NULL);

	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - indent - 2;

	ast_str_append(&context->output_buffer, 0, "%*s:  %-*.*s  %-12.12s  %11.3f\n",
		indent,
		"Contact",
		flexwidth, flexwidth,
		wrapper->contact_id,
		ast_sip_get_contact_short_status_label(status ? status->status : UNKNOWN),
		(status && (status->status != UNKNOWN) ? ((long long) status->rtt) / 1000.0 : NAN));

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
	RAII_VAR(struct ast_sip_cli_formatter_entry *, formatter_entry, NULL, ao2_cleanup);

	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 7;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <Aor%*.*s>  <MaxContact>\n",
		indent, "Aor", filler, filler, CLI_HEADER_FILLER);

	if (context->recurse) {
		context->indent_level++;
		formatter_entry = ast_sip_lookup_cli_formatter("contact");
		if (formatter_entry) {
			formatter_entry->print_header(NULL, context, 0);
		}
		context->indent_level--;
	}

	return 0;
}

static int cli_aor_print_body(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_sip_cli_context *context = arg;
	RAII_VAR(struct ast_sip_cli_formatter_entry *, formatter_entry, NULL, ao2_cleanup);
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
		context->indent_level++;

		formatter_entry = ast_sip_lookup_cli_formatter("contact");
		if (formatter_entry) {
			formatter_entry->iterate(aor, formatter_entry->print_body, context);
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

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Aors",
		.command = "pjsip list aors",
		.usage = "Usage: pjsip list aors\n"
				 "       List the configured PJSIP Aors\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Aors",
		.command = "pjsip show aors",
		.usage = "Usage: pjsip show aors\n"
				 "       Show the configured PJSIP Aors\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Aor",
		.command = "pjsip show aor",
		.usage = "Usage: pjsip show aor <id>\n"
				 "       Show the configured PJSIP Aor\n"),

	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Contacts",
		.command = "pjsip list contacts",
		.usage = "Usage: pjsip list contacts\n"
				 "       List the configured PJSIP contacts\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Contacts",
		.command = "pjsip show contacts",
		.usage = "Usage: pjsip show contacts\n"
				 "       Show the configured PJSIP contacts\n"),
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

	status = ast_res_pjsip_find_or_create_contact_status(contact);

	return status ? 0 : -1;
}

/*! \brief Initialize sorcery with location support */
int ast_sip_initialize_sorcery_location(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();
	ast_sorcery_apply_default(sorcery, "contact", "astdb", "registrar");
	ast_sorcery_apply_default(sorcery, "aor", "config", "pjsip.conf,criteria=type=aor");

	if (ast_sorcery_object_register(sorcery, "contact", contact_alloc, NULL, contact_apply_handler) ||
		ast_sorcery_object_register(sorcery, "aor", aor_alloc, NULL, NULL)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, "contact", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "contact", "uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, uri));
	ast_sorcery_object_field_register(sorcery, "contact", "path", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, path));
	ast_sorcery_object_field_register_custom(sorcery, "contact", "expiration_time", "", expiration_str2struct, expiration_struct2str, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "contact", "qualify_frequency", 0, OPT_UINT_T,
		PARSE_IN_RANGE, FLDSET(struct ast_sip_contact, qualify_frequency), 0, 86400);
	ast_sorcery_object_field_register(sorcery, "contact", "qualify_timeout", "3.0", OPT_DOUBLE_T, 0, FLDSET(struct ast_sip_contact, qualify_timeout));
	ast_sorcery_object_field_register(sorcery, "contact", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, outbound_proxy));
	ast_sorcery_object_field_register(sorcery, "contact", "user_agent", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, user_agent));

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
	ast_sorcery_object_field_register(sorcery, "aor", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_aor, outbound_proxy));
	ast_sorcery_object_field_register(sorcery, "aor", "support_path", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_aor, support_path));

	internal_sip_register_endpoint_formatter(&endpoint_aor_formatter);

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

	return 0;
}

int ast_sip_destroy_sorcery_location(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sip_unregister_cli_formatter(contact_formatter);
	ast_sip_unregister_cli_formatter(aor_formatter);

	internal_sip_unregister_endpoint_formatter(&endpoint_aor_formatter);

	return 0;
}

