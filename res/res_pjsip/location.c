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

int ast_sip_location_add_contact(struct ast_sip_aor *aor, const char *uri, struct timeval expiration_time, const char *path_info)
{
	char name[AST_UUID_STR_LEN];
	RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);

	snprintf(name, sizeof(name), "%s;@%s", ast_sorcery_object_get_id(aor), uri);

	if (!(contact = ast_sorcery_alloc(ast_sip_get_sorcery(), "contact", name))) {
		return -1;
	}

	ast_string_field_set(contact, uri, uri);
	contact->expiration_time = expiration_time;
	contact->qualify_frequency = aor->qualify_frequency;
	contact->authenticate_qualify = aor->authenticate_qualify;
	if (path_info && aor->support_path) {
		ast_string_field_set(contact, path, path_info);
	}

	if (!ast_strlen_zero(aor->outbound_proxy)) {
		ast_string_field_set(contact, outbound_proxy, aor->outbound_proxy);
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
	return (ast_asprintf(buf, "%lu", contact->expiration_time.tv_sec) < 0) ? -1 : 0;
}

/*! \brief Helper function which validates a permanent contact */
static int permanent_contact_validate(void *data)
{
	const char *value = data;
	pj_pool_t *pool;
	pj_str_t contact_uri;
	static const pj_str_t HCONTACT = { "Contact", 7 };

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Permanent Contact Validation", 256, 256);
	if (!pool) {
		return -1;
	}

	pj_strdup2_with_null(pool, &contact_uri, value);
	if (!pjsip_parse_hdr(pool, &HCONTACT, contact_uri.ptr, contact_uri.slen, NULL)) {
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
	return 0;
}

/*! \brief Custom handler for permanent URIs */
static int permanent_uri_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_aor *aor = obj;
	RAII_VAR(struct ast_sip_contact *, contact, NULL, ao2_cleanup);

	if (ast_sip_push_task_synchronous(NULL, permanent_contact_validate, (char*)var->value)) {
		ast_log(LOG_ERROR, "Permanent URI on aor '%s' with contact '%s' failed to parse\n",
			ast_sorcery_object_get_id(aor), var->value);
		return -1;
	}

	if ((!aor->permanent_contacts && !(aor->permanent_contacts = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 1, NULL, NULL))) ||
		!(contact = ast_sorcery_alloc(ast_sip_get_sorcery(), "contact", NULL))) {
		return -1;
	}

	ast_string_field_set(contact, uri, var->value);
	ao2_link_flags(aor->permanent_contacts, contact, OBJ_NOLOCK);

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

static void destroy_contact_pair(void *obj)
{
	struct ast_sip_aor_contact_pair *pair = obj;
	ao2_cleanup(pair->aor);
	ao2_cleanup(pair->contact);
}

static struct ast_sip_aor_contact_pair *create_contact_pair(
	struct ast_sip_aor *aor, struct ast_sip_contact *contact)
{
	struct ast_sip_aor_contact_pair *pair = ao2_alloc(
		sizeof(*pair), destroy_contact_pair);

	if (!pair) {
		return NULL;
	}

	pair->aor = aor;
	pair->contact = contact;

	ao2_ref(pair->aor, +1);
	ao2_ref(pair->contact, +1);

	return pair;
}

int ast_sip_for_each_contact(struct ast_sip_aor *aor,
		ao2_callback_fn on_contact, void *arg)
{
	RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
	struct ast_sip_contact *contact;
	struct ao2_iterator i;

	if (!on_contact ||
	    !(contacts = ast_sip_location_retrieve_aor_contacts(aor))) {
		return 0;
	}

	i = ao2_iterator_init(contacts, 0);
	while ((contact = ao2_iterator_next(&i))) {
		int res;
		RAII_VAR(struct ast_sip_aor_contact_pair *,
			 acp, create_contact_pair(aor, contact), ao2_cleanup);

		if (!acp || (res = on_contact(acp, arg, 0))) {
			ao2_iterator_destroy(&i);
			return -1;
		}
	}
	ao2_iterator_destroy(&i);
	return 0;
}

int ast_sip_contact_to_str(void *object, void *arg, int flags)
{
	struct ast_sip_aor_contact_pair *acp = object;
	struct ast_str **buf = arg;

	ast_str_append(buf, 0, "%s/%s,",
		       ast_sorcery_object_get_id(acp->aor), acp->contact->uri);

	return 0;
}

static int sip_aor_to_ami(const struct ast_sip_aor *aor, struct ast_str **buf)
{
	return ast_sip_sorcery_object_to_ami(aor, buf);
}

static int format_ami_aor_handler(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_sip_ami *ami = arg;
	const struct ast_sip_endpoint *endpoint = ami->arg;
	RAII_VAR(struct ast_str *, buf,
		 ast_sip_create_ami_event("AorDetail", ami), ast_free);

	int num;
	RAII_VAR(struct ao2_container *, contacts,
		 ast_sip_location_retrieve_aor_contacts(aor), ao2_cleanup);

	if (!buf) {
		return -1;
	}

	sip_aor_to_ami(aor, &buf);
	ast_str_append(&buf, 0, "Contacts: ");
	ast_sip_for_each_contact(aor, ast_sip_contact_to_str, &buf);
	ast_str_truncate(buf, -1);
	ast_str_append(&buf, 0, "\r\n");

	num = ao2_container_count(contacts);
	ast_str_append(&buf, 0, "TotalContacts: %d\r\n", num);
	ast_str_append(&buf, 0, "ContactsRegistered: %d\r\n",
		       num - ao2_container_count(aor->permanent_contacts));
	ast_str_append(&buf, 0, "EndpointName: %s\r\n",
		       ast_sorcery_object_get_id(endpoint));

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
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

static int populate_contact_container(void *obj, void *arg, int flags)
{
	struct ast_sip_aor_contact_pair *acp = obj;
	struct ao2_container *container = arg;
	ao2_link_flags(container, acp, OBJ_NOLOCK);
	return 0;
}

static int gather_aor_channels(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ao2_container *container = arg;
	ast_sip_for_each_contact(aor, populate_contact_container, container);
	return 0;
}

static struct ao2_container *cli_get_contact_container(struct ast_sorcery *sip_sorcery)
{
	RAII_VAR(struct ao2_container *, parent_container, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, s_parent_container, NULL, ao2_cleanup);
	struct ao2_container *child_container;

	parent_container = ast_sorcery_retrieve_by_fields(sip_sorcery, "aor",
			AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!parent_container) {
		return NULL;
	}

	s_parent_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, &ast_sorcery_object_id_compare, NULL);
	if (!s_parent_container) {
		return NULL;
	}

	ao2_container_dup(s_parent_container, parent_container, OBJ_ORDER_ASCENDING);

	child_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0, NULL, NULL);
	if (!child_container) {
		return NULL;
	}

	ao2_callback(s_parent_container, OBJ_NODATA, gather_aor_channels, child_container);

	return child_container;
}


static int cli_print_contact_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 18;

	if (!context->output_buffer) {
		return -1;
	}
	ast_str_append(&context->output_buffer, 0,
		"%*s:  <Aor/ContactUri%*.*s>  <Status....>  <RTT(ms)..>\n",
		indent, "Contact", filler, filler, CLI_HEADER_FILLER);

	return 0;
}

static int cli_print_contact_body(void *obj, void *arg, int flags)
{
	struct ast_sip_aor_contact_pair *acp = obj;
	struct ast_sip_cli_context *context = arg;
	char *print_name = NULL;
	int print_name_len;
	int indent;
	int flexwidth;

	RAII_VAR(struct ast_sip_contact_status *, status,
		ast_sorcery_retrieve_by_id( ast_sip_get_sorcery(), CONTACT_STATUS, ast_sorcery_object_get_id(acp->contact)),
		ao2_cleanup);

	if (!context->output_buffer) {
		return -1;
	}

	print_name_len = strlen(ast_sorcery_object_get_id(acp->aor))
		+ strlen(acp->contact->uri) + 2;
	if (!(print_name = alloca(print_name_len))) {
		return -1;
	}
	snprintf(print_name, print_name_len, "%s/%s",
		ast_sorcery_object_get_id(acp->aor), acp->contact->uri);

	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - indent - 2;

	ast_str_append(&context->output_buffer, 0, "%*s:  %-*.*s  %-12.12s  %11.3f\n",
		indent,
		"Contact",
		flexwidth, flexwidth,
		print_name,
		(status ? (status->status == AVAILABLE ? "Avail" : "Unavail") : "Unknown"),
		(status ? ((long long) status->rtt) / 1000.0 : NAN));

	return 0;
}

static struct ao2_container *cli_get_aor_container(struct ast_sorcery *sip_sorcery)
{
	return ast_sorcery_retrieve_by_fields(sip_sorcery, "aor",
				AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

static int cli_print_aor_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	struct ast_sip_cli_formatter_entry *formatter_entry;

	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_LAST_TABSTOP - indent - 7;

	if (!context->output_buffer) {
		return -1;
	}
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

static int cli_print_aor_body(void *obj, void *arg, int flags)
{
	struct ast_sip_aor *aor = obj;
	struct ast_sip_cli_context *context = arg;
	struct ast_sip_cli_formatter_entry *formatter_entry;
	int indent;
	int flexwidth;

	if (!context->output_buffer) {
		return -1;
	}

	context->current_aor = aor;

	indent = CLI_INDENT_TO_SPACES(context->indent_level);
	flexwidth = CLI_LAST_TABSTOP - indent - 12;

	ast_str_append(&context->output_buffer, 0, "%*s:  %-*.*s %12d\n",
		indent,
		"Aor",
		flexwidth, flexwidth,
		ast_sorcery_object_get_id(aor), aor->max_contacts);

	if (context->recurse) {
		context->indent_level++;
		formatter_entry = ast_sip_lookup_cli_formatter("contact");
		if (formatter_entry) {
			ast_sip_for_each_contact(aor, formatter_entry->print_body, context);
		}
		context->indent_level--;
	}

	if (context->show_details || (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(aor, context, 0);
	}

	return 0;
}

static struct ast_sip_cli_formatter_entry cli_contact_formatter = {
	.name = "contact",
	.print_header = cli_print_contact_header,
	.print_body = cli_print_contact_body,
	.get_container = cli_get_contact_container,
};

static struct ast_sip_cli_formatter_entry cli_aor_formatter = {
	.name = "aor",
	.print_header = cli_print_aor_header,
	.print_body = cli_print_aor_body,
	.get_container = cli_get_aor_container,
};

/*! \brief Initialize sorcery with location support */
int ast_sip_initialize_sorcery_location(struct ast_sorcery *sorcery)
{
	ast_sorcery_apply_default(sorcery, "contact", "astdb", "registrar");
	ast_sorcery_apply_default(sorcery, "aor", "config", "pjsip.conf,criteria=type=aor");

	if (ast_sorcery_object_register(sorcery, "contact", contact_alloc, NULL, NULL) ||
		ast_sorcery_object_register(sorcery, "aor", aor_alloc, NULL, NULL)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, "contact", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "contact", "uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, uri));
	ast_sorcery_object_field_register(sorcery, "contact", "path", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, path));
	ast_sorcery_object_field_register_custom(sorcery, "contact", "expiration_time", "", expiration_str2struct, expiration_struct2str, 0, 0);
	ast_sorcery_object_field_register(sorcery, "contact", "qualify_frequency", 0, OPT_UINT_T,
					  PARSE_IN_RANGE, FLDSET(struct ast_sip_contact, qualify_frequency), 0, 86400);
	ast_sorcery_object_field_register(sorcery, "contact", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_contact, outbound_proxy));

	ast_sorcery_object_field_register(sorcery, "aor", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "aor", "minimum_expiration", "60", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, minimum_expiration));
	ast_sorcery_object_field_register(sorcery, "aor", "maximum_expiration", "7200", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, maximum_expiration));
	ast_sorcery_object_field_register(sorcery, "aor", "default_expiration", "3600", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, default_expiration));
	ast_sorcery_object_field_register(sorcery, "aor", "qualify_frequency", 0, OPT_UINT_T, PARSE_IN_RANGE, FLDSET(struct ast_sip_aor, qualify_frequency), 0, 86400);
	ast_sorcery_object_field_register(sorcery, "aor", "authenticate_qualify", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_aor, authenticate_qualify));
	ast_sorcery_object_field_register(sorcery, "aor", "max_contacts", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_aor, max_contacts));
	ast_sorcery_object_field_register(sorcery, "aor", "remove_existing", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_aor, remove_existing));
	ast_sorcery_object_field_register_custom(sorcery, "aor", "contact", "", permanent_uri_handler, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "aor", "mailboxes", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_aor, mailboxes));
	ast_sorcery_object_field_register(sorcery, "aor", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_aor, outbound_proxy));
	ast_sorcery_object_field_register(sorcery, "aor", "support_path", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_aor, support_path));

	ast_sip_register_endpoint_formatter(&endpoint_aor_formatter);
	ast_sip_register_cli_formatter(&cli_contact_formatter);
	ast_sip_register_cli_formatter(&cli_aor_formatter);
	return 0;
}

