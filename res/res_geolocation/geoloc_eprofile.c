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
#include "asterisk/pbx.h"
#include "asterisk/strings.h"
#include "asterisk/xml.h"
#include "geoloc_private.h"

extern const uint8_t _binary_res_geolocation_pidf_to_eprofile_xslt_start[];
extern const uint8_t _binary_res_geolocation_pidf_to_eprofile_xslt_end[];
static size_t pidf_to_eprofile_xslt_size;

extern const uint8_t _binary_res_geolocation_pidf_lo_test_xml_start[];
extern const uint8_t _binary_res_geolocation_pidf_lo_test_xml_end[];
static size_t pidf_lo_test_xml_size;

extern const uint8_t _binary_res_geolocation_eprofile_to_pidf_xslt_start[];
extern const uint8_t _binary_res_geolocation_eprofile_to_pidf_xslt_end[];
static size_t eprofile_to_pidf_xslt_size;

static struct ast_xslt_doc *eprofile_to_pidf_xslt;
static struct ast_xslt_doc *pidf_to_eprofile_xslt;

static struct ast_sorcery *geoloc_sorcery;

#define DUP_VARS(_dest, _source) \
({ \
	int _rc = 0; \
	if (_source) { \
		struct ast_variable *_vars = ast_variables_dup(_source); \
		if (!_vars) { \
			_rc = -1; \
		} else { \
			_dest = _vars; \
		} \
	} \
	(_rc); \
})

static void geoloc_eprofile_destructor(void *obj)
{
	struct ast_geoloc_eprofile *eprofile = obj;

	ast_string_field_free_memory(eprofile);
	ast_variables_destroy(eprofile->location_info);
	ast_variables_destroy(eprofile->location_refinement);
	ast_variables_destroy(eprofile->location_variables);
	ast_variables_destroy(eprofile->effective_location);
	ast_variables_destroy(eprofile->usage_rules);
	ast_variables_destroy(eprofile->confidence);
}

struct ast_geoloc_eprofile *ast_geoloc_eprofile_alloc(const char *name)
{
	struct ast_geoloc_eprofile *eprofile = ao2_alloc_options(sizeof(*eprofile),
		geoloc_eprofile_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);

	ast_string_field_init(eprofile, 256);
	ast_string_field_set(eprofile, id, name); /* SAFE string fields handle NULL */

	return eprofile;
}

int ast_geoloc_eprofile_refresh_location(struct ast_geoloc_eprofile *eprofile)
{
	struct ast_geoloc_location *loc = NULL;
	RAII_VAR(struct ast_variable *, temp_locinfo, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, temp_effloc, NULL, ast_variables_destroy);
	RAII_VAR(struct ast_variable *, temp_confidence, NULL, ast_variables_destroy);
	const char *method = NULL;
	const char *location_source = NULL;
	enum ast_geoloc_format format;
	struct ast_variable *var;
	int rc = 0;

	if (!eprofile) {
		return -1;
	}

	if (!ast_strlen_zero(eprofile->location_reference)) {
		loc = ast_sorcery_retrieve_by_id(geoloc_sorcery, "location", eprofile->location_reference);
		if (!loc) {
			ast_log(LOG_ERROR, "Profile '%s' referenced location '%s' does not exist!", eprofile->id,
				eprofile->location_reference);
			return -1;
		}

		format = loc->format;
		method = loc->method;
		location_source = loc->location_source;
		rc = DUP_VARS(temp_locinfo, loc->location_info);
		if (rc == 0) {
			rc = DUP_VARS(temp_confidence, loc->confidence);
		}
		ao2_ref(loc, -1);
		if (rc != 0) {
			return -1;
		}
	} else {
		format = eprofile->format;
		method = eprofile->method;
		location_source = eprofile->location_source;
		rc = DUP_VARS(temp_locinfo, eprofile->location_info);
		if (rc == 0) {
			rc = DUP_VARS(temp_confidence, eprofile->confidence);
		}
		if (rc != 0) {
			return -1;
		}
	}

	rc = DUP_VARS(temp_effloc, temp_locinfo);
	if (rc != 0) {
		return -1;
	}

	if (eprofile->location_refinement) {
		for (var = eprofile->location_refinement; var; var = var->next) {
			struct ast_variable *newvar = ast_variable_new(var->name, var->value, "");
			if (!newvar) {
				return -1;
			}
			if (ast_variable_list_replace(&temp_effloc, newvar)) {
				ast_variable_list_append(&temp_effloc, newvar);
			}
		}
	}

	eprofile->format = format;
	ast_string_field_set(eprofile, method, method);
	ast_string_field_set(eprofile, location_source, location_source);

	ast_variables_destroy(eprofile->location_info);
	eprofile->location_info = temp_locinfo;
	temp_locinfo = NULL;
	ast_variables_destroy(eprofile->effective_location);
	eprofile->effective_location = temp_effloc;
	temp_effloc = NULL;

	return 0;
}

struct ast_geoloc_eprofile *ast_geoloc_eprofile_dup(struct ast_geoloc_eprofile *src)
{
	struct ast_geoloc_eprofile *eprofile;
	const char *profile_id;
	int rc = 0;

	if (!src) {
		return NULL;
	}

	profile_id = ast_strdupa(src->id);

	eprofile = ast_geoloc_eprofile_alloc(profile_id);
	if (!eprofile) {
		return NULL;
	}

	eprofile->allow_routing_use = src->allow_routing_use;
	eprofile->pidf_element = src->pidf_element;
	eprofile->suppress_empty_ca_elements = src->suppress_empty_ca_elements;
	eprofile->format = src->format;
	eprofile->precedence = src->precedence;


	rc = ast_string_field_set(eprofile, location_reference, src->location_reference);
	if (rc == 0) {
		ast_string_field_set(eprofile, notes, src->notes);
	}
	if (rc == 0) {
		ast_string_field_set(eprofile, method, src->method);
	}
	if (rc == 0) {
		ast_string_field_set(eprofile, location_source, src->location_source);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->location_info, src->location_info);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->effective_location, src->effective_location);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->location_refinement, src->location_refinement);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->location_variables, src->location_variables);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->usage_rules, src->usage_rules);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->confidence, src->confidence);
	}
	if (rc != 0) {
		ao2_ref(eprofile, -1);
		return NULL;
	}


	return eprofile;
}

struct ast_geoloc_eprofile *ast_geoloc_eprofile_create_from_profile(struct ast_geoloc_profile *profile)
{
	struct ast_geoloc_eprofile *eprofile;
	const char *profile_id;
	int rc = 0;

	if (!profile) {
		return NULL;
	}

	profile_id = ast_sorcery_object_get_id(profile);

	eprofile = ast_geoloc_eprofile_alloc(profile_id);
	if (!eprofile) {
		return NULL;
	}

	ao2_lock(profile);
	eprofile->allow_routing_use = profile->allow_routing_use;
	eprofile->pidf_element = profile->pidf_element;
	eprofile->suppress_empty_ca_elements = profile->suppress_empty_ca_elements;
	eprofile->format = profile->format;


	rc = ast_string_field_set(eprofile, location_reference, profile->location_reference);
	if (rc == 0) {
		ast_string_field_set(eprofile, notes, profile->notes);
	}
	if (rc == 0) {
		ast_string_field_set(eprofile, method, profile->method);
	}
	if (rc == 0) {
		ast_string_field_set(eprofile, location_source, profile->location_source);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->location_info, profile->location_info);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->location_refinement, profile->location_refinement);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->location_variables, profile->location_variables);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->usage_rules, profile->usage_rules);
	}
	if (rc == 0) {
		rc = DUP_VARS(eprofile->confidence, profile->confidence);
	}
	if (rc != 0) {
		ao2_unlock(profile);
		ao2_ref(eprofile, -1);
		return NULL;
	}

	eprofile->precedence = profile->precedence;
	ao2_unlock(profile);

	if (ast_geoloc_eprofile_refresh_location(eprofile) != 0) {
		ao2_ref(eprofile, -1);
		return NULL;
	}

	return eprofile;
}

static int set_loc_src(struct ast_geoloc_eprofile *eprofile, const char *uri, const char *ref_str)
{
	char *local_uri = ast_strdupa(uri);
	char *loc_src = NULL;

	loc_src = strchr(local_uri, ';');
	if (loc_src) {
		*loc_src = '\0';
		loc_src++;
	}

	if (!ast_strlen_zero(loc_src)) {
		if (ast_begins_with(loc_src, "loc-src=")) {
			struct ast_sockaddr loc_source_addr;
			int rc = 0;
			loc_src += 8;
			rc = ast_sockaddr_parse(&loc_source_addr, loc_src, PARSE_PORT_FORBID);
			if (rc == 1) {
				ast_log(LOG_WARNING, "%s: URI '%s' has an invalid 'loc-src' parameter."
					" RFC8787 states that IP addresses MUST be dropped.\n",
					ref_str, uri);
				return -1;
			} else {
				ast_string_field_set(eprofile, location_source, loc_src);
			}
		}
	}
	return 0;
}

struct ast_geoloc_eprofile *ast_geoloc_eprofile_create_from_uri(const char *uri,
	const char *ref_str)
{
	struct ast_geoloc_eprofile *eprofile = NULL;
	char *ra = NULL;
	char *local_uri;

	if (ast_strlen_zero(uri)) {
		return NULL;
	}
	local_uri = ast_strdupa(uri);

	if (local_uri[0] == '<') {
		local_uri++;
	}
	ra = strchr(local_uri, '>');
	if (ra) {
		*ra = '\0';
	}

	ast_strip(local_uri);

	eprofile = ast_geoloc_eprofile_alloc(local_uri);
	if (!eprofile) {
		return NULL;
	}

	set_loc_src(eprofile, uri, ref_str);

	eprofile->format = AST_GEOLOC_FORMAT_URI;
	eprofile->location_info = ast_variable_new("URI", local_uri, "");

	return eprofile;
}

struct ast_variable *geoloc_eprofile_resolve_varlist(struct ast_variable *source,
	struct ast_variable *variables, struct ast_channel *chan)
{
	struct ast_variable *dest = NULL;
	struct ast_variable *var = NULL;
	struct varshead *vh = NULL;
	struct ast_str *buf = ast_str_alloca(256);

	if (!source || !chan) {
		return NULL;
	}

	/*
	 * ast_str_substitute_variables does only minimal recursive resolution so we need to
	 * pre-resolve each variable in the "variables" list, then use that result to
	 * do the final pass on the "source" variable list.
	 */
	if (variables) {
		var = variables;
		vh = ast_var_list_create();
		if (!vh) {
			return NULL;
		}
		for ( ; var; var = var->next) {
			ast_str_substitute_variables_full2(&buf, 0, chan, vh, var->value, NULL, 1);
			AST_VAR_LIST_INSERT_TAIL(vh, ast_var_assign(var->name, ast_str_buffer(buf)));
			ast_str_reset(buf);
		}
	}

	var = source;
	for ( ; var; var = var->next) {
		struct ast_variable *newvar = NULL;
		ast_str_substitute_variables_full2(&buf, 0, chan, vh, var->value, NULL, 1);
		newvar = ast_variable_new(var->name, ast_str_buffer(buf), "");
		if (!newvar) {
			ast_variables_destroy(dest);
			ast_var_list_destroy(vh);
			return NULL;
		}
		ast_variable_list_append(&dest, newvar);
		ast_str_reset(buf);
	}
	ast_var_list_destroy(vh);

	return dest;
}


const char *ast_geoloc_eprofile_to_uri(struct ast_geoloc_eprofile *eprofile,
	struct ast_channel *chan, struct ast_str **buf, const char *ref_str)
{
	const char *uri = NULL;
	struct ast_variable *resolved = NULL;
	char *result;
	int we_created_buf = 0;

	if (!eprofile || !buf || !chan) {
		return NULL;
	}

	if (eprofile->format != AST_GEOLOC_FORMAT_URI) {
		ast_log(LOG_ERROR, "%s: '%s' is not a URI profile.  It's '%s'\n",
			ref_str, eprofile->id, ast_geoloc_format_to_name(eprofile->format));
		return NULL;
	}

	resolved = geoloc_eprofile_resolve_varlist(eprofile->effective_location,
		eprofile->location_variables, chan);
	if (!resolved) {
		return NULL;
	}

	uri = ast_variable_find_in_list(resolved, "URI");
	result = uri ? ast_strdupa(uri) : NULL;
	ast_variables_destroy(resolved);

	if (ast_strlen_zero(result)) {
		ast_log(LOG_ERROR, "%s: '%s' is a URI profile but had no, or an empty, 'URI' entry in location_info\n",
			ref_str, eprofile->id);
		return NULL;
	}

	if (!*buf) {
		*buf = ast_str_create(256);
		if (!*buf) {
			return NULL;
		}
		we_created_buf = 1;
	}

	if (ast_str_append(buf, 0, "%s", result) <= 0) {
		if (we_created_buf) {
			ast_free(*buf);
			*buf = NULL;
			return NULL;
		}
	}

	return ast_str_buffer(*buf);
}

static struct ast_variable *var_list_from_node(struct ast_xml_node *node,
	const char *ref_str)
{
	struct ast_variable *list = NULL;
	struct ast_xml_node *container;
	struct ast_xml_node *child;
	struct ast_variable *var;
	SCOPE_ENTER(3, "%s\n", ref_str);

	container = ast_xml_node_get_children(node);
	for (child = container; child; child = ast_xml_node_get_next(child)) {
		const char *name = ast_xml_node_get_name(child);
		const char *value = ast_xml_get_text(child);
		const char *uom = ast_xml_get_attribute(child, "uom");

		if (uom) {
			/* '20 radians\0' */
			char newval[strlen(value) + 1 + strlen(uom) + 1];
			sprintf(newval, "%s %s", value, uom);
			var = ast_variable_new(name, newval, "");
		} else {
			var = ast_variable_new(name, value, "");
		}

		ast_xml_free_text(value);
		ast_xml_free_attr(uom);

		if (!var) {
			ast_variables_destroy(list);
			SCOPE_EXIT_RTN_VALUE(NULL, "%s: Allocation failure\n", ref_str);
		}
		ast_variable_list_append(&list, var);
	}

	if (TRACE_ATLEAST(5)) {
		struct ast_str *buf = NULL;
		ast_variable_list_join(list, ", ", "=", "\"", &buf);
		ast_trace(5, "%s: Result: %s\n", ref_str, ast_str_buffer(buf));
		ast_free(buf);
	}

	SCOPE_EXIT_RTN_VALUE(list, "%s: Done\n", ref_str);
}

static struct ast_variable *var_list_from_loc_info(struct ast_xml_node *locinfo,
	enum ast_geoloc_format format, const char *ref_str)
{
	struct ast_variable *list = NULL;
	struct ast_variable *locinfo_list = NULL;
	struct ast_xml_node *container;
	struct ast_variable *var = NULL;
	const char *attr;
	SCOPE_ENTER(3, "%s\n", ref_str);

	container = ast_xml_node_get_children(locinfo);
	if (format == AST_GEOLOC_FORMAT_CIVIC_ADDRESS) {
		attr = ast_xml_get_attribute(container, "lang");
		if (attr) {
			var = ast_variable_new("lang", attr, "");
			ast_xml_free_attr(attr);
			if (!var) {
				SCOPE_EXIT_RTN_VALUE(NULL, "%s: Allocation failure\n", ref_str);
			}
			ast_variable_list_append(&list, var);
		}
	} else {
		var = ast_variable_new("shape", ast_xml_node_get_name(container), "");
		if (!var) {
			SCOPE_EXIT_RTN_VALUE(NULL, "%s: Allocation failure\n", ref_str);
		}
		ast_variable_list_append(&list, var);

		attr = ast_xml_get_attribute(container, "srsName");
		var = ast_variable_new("crs", attr, "");
		ast_xml_free_attr(attr);
		if (!var) {
			ast_variables_destroy(list);
			SCOPE_EXIT_RTN_VALUE(NULL, "%s: Allocation failure\n", ref_str);
		}
		ast_variable_list_append(&list, var);
	}

	locinfo_list = var_list_from_node(container, ref_str);
	if (locinfo_list == NULL) {
		ast_log(LOG_WARNING, "%s: There were no elements in the location info\n", ref_str);
		SCOPE_EXIT_RTN_VALUE(list, "%s: There were no elements in the location info\n", ref_str);
	}
	ast_variable_list_append(&list, locinfo_list);

	if (TRACE_ATLEAST(5)) {
		struct ast_str *buf = NULL;
		ast_variable_list_join(list, ", ", "=", "\"", &buf);
		ast_trace(5, "%s: Result: %s\n", ref_str, ast_str_buffer(buf));
		ast_free(buf);
	}

	SCOPE_EXIT_RTN_VALUE(list, "%s: Done\n", ref_str);
}

static struct ast_variable *var_list_from_confidence(struct ast_xml_node *confidence,
	const char *ref_str)
{
	struct ast_variable *list = NULL;
	struct ast_variable *var;
	const char *pdf;
	const char *value;
	SCOPE_ENTER(3, "%s\n", ref_str);

	if (!confidence) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: No confidence\n", ref_str);
	}

	pdf = ast_xml_get_attribute(confidence, "pdf");
	var = ast_variable_new("pdf", S_OR(pdf, "unknown"), "");
	ast_xml_free_attr(pdf);
	if (!var) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Allocation failure\n", ref_str);
	}
	ast_variable_list_append(&list, var);

	value = ast_xml_get_text(confidence);
	var = ast_variable_new("value", S_OR(value, "95"), "");
	ast_xml_free_text(value);
	if (!var) {
		ast_variables_destroy(list);
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Allocation failure\n", ref_str);
	}
	ast_variable_list_append(&list, var);

	if (TRACE_ATLEAST(5)) {
		struct ast_str *buf = NULL;
		ast_variable_list_join(list, ", ", "=", "\"", &buf);
		ast_trace(5, "%s: Result: %s\n", ref_str, ast_str_buffer(buf));
		ast_free(buf);
	}

	SCOPE_EXIT_RTN_VALUE(list, "%s: Done\n", ref_str);
}

static struct ast_geoloc_eprofile *geoloc_eprofile_create_from_xslt_result(
	struct ast_xml_doc *result_doc, const char *ref_str)
{
	struct ast_geoloc_eprofile *eprofile;
	/*
	 * None of the ast_xml_nodes needs to be freed
	 * because they're just pointers into result_doc.
	 */
	struct ast_xml_node *presence = NULL;
	struct ast_xml_node *pidf_element = NULL;
	struct ast_xml_node *location_info = NULL;
	struct ast_xml_node *confidence = NULL;
	struct ast_xml_node *usage_rules = NULL;
	struct ast_xml_node *method = NULL;
	struct ast_xml_node *note_well = NULL;
	/*
	 * Like nodes, names of nodes are just
	 * pointers into result_doc and don't need to be freed.
	 */
	const char *pidf_element_str;
	/*
	 * Attributes and element text however are allocated on the fly
	 * so they DO need to be freed after use.
	 */
	const char *id = NULL;
	const char *format_str = NULL;
	const char *method_str = NULL;
	const char *note_well_str = NULL;

	SCOPE_ENTER(3, "%s\n", ref_str);

	if (!result_doc) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: result_doc was NULL", ref_str);
	}

	if (TRACE_ATLEAST(5)) {
		char *doc_str = NULL;
		int doc_len = 0;

		ast_xml_doc_dump_memory(result_doc, &doc_str, &doc_len);
		ast_trace(5, "xslt result doc len: %d\n%s\n", doc_len, doc_len ? doc_str : "<empty>");
		ast_xml_free_text(doc_str);
	}

	presence = ast_xml_get_root(result_doc);
	if (!presence) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Can't find 'presence' root element\n",
			ref_str);
	}

	pidf_element = ast_xml_node_get_children(presence);
	if (!pidf_element) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Can't find a device, tuple or person element\n",
			ref_str);
	}

	id = ast_xml_get_attribute(pidf_element, "id");
	if (ast_strlen_zero(id)) {
		ast_xml_free_attr(id);
		id = ast_xml_get_attribute(presence, "entity");
	}

	if (ast_strlen_zero(id)) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Unable to find 'id' attribute\n", ref_str);
	}

	eprofile = ast_geoloc_eprofile_alloc(id);
	ast_xml_free_attr(id);
	if (!eprofile) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Allocation failure\n", ref_str);
	}

	location_info = ast_xml_find_child_element(pidf_element, "location-info", NULL, NULL);
	if (!location_info) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Can't find a location-info element\n",
			ref_str);
	}

	format_str = ast_xml_get_attribute(location_info, "format");
	if (ast_strlen_zero(format_str)) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Unable to find 'format' attribute\n", ref_str);
	}

	eprofile->format = AST_GEOLOC_FORMAT_NONE;
	if (strcasecmp(format_str, "gml") == 0) {
		eprofile->format = AST_GEOLOC_FORMAT_GML;
	} else if (strcasecmp(format_str, "civicAddress") == 0) {
		eprofile->format = AST_GEOLOC_FORMAT_CIVIC_ADDRESS;
	}

	if (eprofile->format == AST_GEOLOC_FORMAT_NONE) {
		char *dup_format_str = ast_strdupa(format_str);
		ast_xml_free_attr(format_str);
		ao2_ref(eprofile, -1);
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unknown format '%s'\n", ref_str, dup_format_str);
	}
	ast_xml_free_attr(format_str);

	pidf_element_str = ast_xml_node_get_name(pidf_element);
	eprofile->pidf_element = ast_geoloc_pidf_element_str_to_enum(pidf_element_str);

	eprofile->location_info = var_list_from_loc_info(location_info, eprofile->format, ref_str);
	if (!eprofile->location_info) {
		ao2_ref(eprofile, -1);
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR,
			"%s: Unable to create location variables\n", ref_str);
	}

	/*
	 * The function calls that follow are all NULL tolerant
	 * so no need for explicit error checking.
	 */
	usage_rules = ast_xml_find_child_element(pidf_element, "usage-rules", NULL, NULL);
	eprofile->usage_rules = var_list_from_node(usage_rules, ref_str);
	confidence = ast_xml_find_child_element(location_info, "confidence", NULL, NULL);
	eprofile->confidence = var_list_from_confidence(confidence, ref_str);

	method = ast_xml_find_child_element(pidf_element, "method", NULL, NULL);
	method_str = ast_xml_get_text(method);
	ast_string_field_set(eprofile, method, method_str);
	ast_xml_free_text(method_str);

	note_well = ast_xml_find_child_element(pidf_element, "note-well", NULL, NULL);
	note_well_str = ast_xml_get_text(note_well);
	ast_string_field_set(eprofile, notes, note_well_str);
	ast_xml_free_text(note_well_str);

	SCOPE_EXIT_RTN_VALUE(eprofile, "%s: Done.\n", ref_str);
}

static int is_pidf_lo(struct ast_xml_doc *result_doc)
{
	struct ast_xml_node *presence;
	struct ast_xml_node *pidf_element;
	struct ast_xml_node *location_info;
	const char *pidf_element_name;

	if (!result_doc) {
		return 0;
	}
	presence = ast_xml_get_root(result_doc);
	if (!presence || !ast_strings_equal("presence", ast_xml_node_get_name(presence))) {
		return 0;
	}

	pidf_element = ast_xml_node_get_children(presence);
	if (!pidf_element) {
		return 0;
	}
	pidf_element_name = ast_xml_node_get_name(pidf_element);
	if (!ast_strings_equal(pidf_element_name, "device") &&
		!ast_strings_equal(pidf_element_name, "tuple") &&
		!ast_strings_equal(pidf_element_name, "person")) {
		return 0;
	}

	location_info = ast_xml_find_child_element(pidf_element, "location-info", NULL, NULL);
	if (!location_info) {
		return 0;
	}

	return 1;
}

struct ast_geoloc_eprofile *ast_geoloc_eprofile_create_from_pidf(
	struct ast_xml_doc *pidf_xmldoc, const char *geoloc_uri, const char *ref_str)
{
	struct ast_xml_doc *result_doc = NULL;
	struct ast_geoloc_eprofile *eprofile = NULL;

	SCOPE_ENTER(3, "%s\n", ref_str);

	result_doc = ast_xslt_apply(pidf_to_eprofile_xslt, pidf_xmldoc, NULL);
	if (!is_pidf_lo(result_doc)) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Not a PIDF-LO.  Skipping.\n", ref_str);
	}

	/*
	 * The document returned from the stylesheet application looks like this...
	 * <presence id="presence-entity">
	 *     <tuple id="element-id">
	 *         <location-info format="gml">shape="Ellipsoid", crs="3d", ...</location-info>
	 *         <usage-rules>retransmission-allowed="no", retention-expiry="2010-11-14T20:00:00Z"</usage-rules>
	 *         <method>Hybrid_A-GPS</method>
	 *     </tuple>
	 *  </presence>
	 *
	 * Regardless of whether the pidf-element was tuple, device or person and whether
	 * the format is gml or civicAddress, the presence, pidf-element and location-info
	 * elements should be there.
	 *
	 * The confidence, usage-rules and note-well elements are optional.
	 */

	if (TRACE_ATLEAST(5)) {
		char *doc_str = NULL;
		int doc_len = 0;

		ast_xml_doc_dump_memory(result_doc, &doc_str, &doc_len);
		ast_trace(5, "Intermediate doc len: %d\n%s\n", doc_len, doc_len ? doc_str : "<empty>");
		ast_xml_free_text(doc_str);
		doc_str = NULL;
		doc_len = 0;
	}

	eprofile = geoloc_eprofile_create_from_xslt_result(result_doc, ref_str);
	ast_xml_close(result_doc);

	if (eprofile && geoloc_uri) {
		set_loc_src(eprofile, geoloc_uri, ref_str);
	}

	SCOPE_EXIT_RTN_VALUE(eprofile, "%s: Done.\n", ref_str);
}

/*!
 * \internal
 * \brief Create an common, intermediate XML document to pass to the outgoing XSLT process
 *
 * \param element_name The name of the top-level XML element to create
 * \param eprofile     The eprofile
 * \param chan         The channel to resolve variables against
 * \param ref_string   A reference string for error messages
 * \return             An XML doc
 *
 * \note Given that the document is simple and static, it was easier to just
 * create the elements in a string buffer and call ast_xml_read_memory()
 * at the end instead of creating
 */
static struct ast_xml_node *geoloc_eprofile_to_intermediate(const char *element_name, struct ast_geoloc_eprofile *eprofile,
	struct ast_channel *chan, const char *ref_string)
{
	struct ast_variable *resolved_location = NULL;
	struct ast_variable *resolved_usage = NULL;
	struct ast_variable *var = NULL;
	RAII_VAR(struct ast_xml_node *, pidf_node, NULL, ast_xml_free_node);
	struct ast_xml_node *rtn_pidf_node;
	struct ast_xml_node *loc_node;
	struct ast_xml_node *confidence_node;
	struct ast_xml_node *info_node;
	struct ast_xml_node *rules_node;
	struct ast_xml_node *method_node;
	struct ast_xml_node *notes_node;
	struct ast_xml_node *timestamp_node;
	struct timeval tv = ast_tvnow();
	struct tm tm = { 0, };
	char timestr[32] = { 0, };
	int rc = 0;

	SCOPE_ENTER(3, "%s\n", ref_string);

	if (!eprofile || !chan) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Either or both eprofile or chan were NULL\n", ref_string);
	}

	pidf_node = ast_xml_new_node(element_name);
	if (!pidf_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create '%s' XML node\n",
			ref_string, element_name);
	}

	loc_node = ast_xml_new_child(pidf_node, "location-info");
	if (!loc_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'location-info' XML node\n",
			ref_string);
	}
	rc = ast_xml_set_attribute(loc_node, "format", ast_geoloc_format_to_name(eprofile->format));
	if (rc != 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to set 'format' XML attribute\n", ref_string);
	}

	resolved_location = geoloc_eprofile_resolve_varlist(eprofile->effective_location,
		eprofile->location_variables, chan);
	if (eprofile->format == AST_GEOLOC_FORMAT_CIVIC_ADDRESS) {
		info_node = geoloc_civicaddr_list_to_xml(resolved_location, ref_string);
	} else {
		info_node = geoloc_gml_list_to_xml(resolved_location, ref_string);
	}
	ast_variables_destroy(resolved_location);

	if (!info_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create XML from '%s' list\n",
			ref_string, ast_geoloc_format_to_name(eprofile->format));
	}
	if (!ast_xml_add_child(loc_node, info_node)) {
		ast_xml_free_node(info_node);
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable add '%s' node to XML document\n",
			ref_string, ast_geoloc_format_to_name(eprofile->format));
	}

	if (eprofile->confidence) {
		const char *value = S_OR(ast_variable_find_in_list(eprofile->confidence, "value"), "95");
		const char *pdf = S_OR(ast_variable_find_in_list(eprofile->confidence, "pdf"), "unknown");

		confidence_node = ast_xml_new_child(loc_node, "confidence");
		if (!confidence_node) {
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'confidence' XML node\n",
				ref_string);
		}
		rc = ast_xml_set_attribute(confidence_node, "pdf", pdf);
		if (rc != 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to set 'pdf' attribute on 'confidence' element\n", ref_string);
		}

		ast_xml_set_text(confidence_node, value);
	}

	rules_node = ast_xml_new_child(pidf_node, "usage-rules");
	if (!rules_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'usage-rules' XML node\n",
			ref_string);
	}
	resolved_usage = geoloc_eprofile_resolve_varlist(eprofile->usage_rules,
		eprofile->location_variables, chan);
	for (var = resolved_usage; var; var = var->next) {
		struct ast_xml_node *ur = ast_xml_new_child(rules_node, var->name);
		ast_xml_set_text(ur, var->value);
	}
	ast_variables_destroy(resolved_usage);

	if (!ast_strlen_zero(eprofile->method)) {
		method_node = ast_xml_new_child(pidf_node, "method");
		if (!method_node) {
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'method' XML node\n",
				ref_string);
		}
		ast_xml_set_text(method_node, eprofile->method);
	};

	if (!ast_strlen_zero(eprofile->notes)) {
		notes_node = ast_xml_new_child(pidf_node, "note-well");
		if (!notes_node) {
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'note-well' XML node\n",
				ref_string);
		}
		ast_xml_set_text(notes_node, eprofile->notes);
	};

	gmtime_r(&tv.tv_sec, &tm);
	strftime(timestr, sizeof(timestr), "%FT%TZ", &tm);
	timestamp_node = ast_xml_new_child(pidf_node, "timestamp");
	if (!timestamp_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create 'timestamp' XML node\n",
			ref_string);
	}
	ast_xml_set_text(timestamp_node, timestr);

	rtn_pidf_node = pidf_node;
	pidf_node = NULL;
	SCOPE_EXIT_RTN_VALUE(rtn_pidf_node, "%s: Done\n", ref_string);
}

#define CREATE_NODE_LIST(node) \
	if (!node) { \
		node = ast_xml_new_child(root_node, \
			geoloc_pidf_element_to_name(eprofile->pidf_element)); \
		if (!pidfs[eprofile->pidf_element]) { \
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create pidf '%s' XML node\n", \
				ref_string, geoloc_pidf_element_to_name(eprofile->pidf_element)); \
		} \
	}

const char *ast_geoloc_eprofiles_to_pidf(struct ast_datastore *ds,
	struct ast_channel *chan, struct ast_str **buf, const char * ref_string)
{
	RAII_VAR(struct ast_xml_doc *, intermediate, NULL, ast_xml_close);
	RAII_VAR(struct ast_xml_doc *, pidf_doc, NULL, ast_xml_close);
	struct ast_xml_node *root_node;
	struct ast_xml_node *pidfs[AST_PIDF_ELEMENT_LAST] = {NULL, };
	struct ast_geoloc_eprofile *eprofile;
	int eprofile_count = 0;
	int i;
	char *doc_str = NULL;
	int doc_len = 0;
	int rc = 0;
	SCOPE_ENTER(3, "%s\n", ref_string);

	if (!ds || !chan || !buf || !*buf || ast_strlen_zero(ref_string)) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: Either or both datastore or chan were NULL\n",
			ref_string);
	}

	intermediate = ast_xml_new();
	if (!intermediate) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create XML document\n", ref_string);
	}
	root_node = ast_xml_new_node("presence");
	if (!root_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create root XML node\n", ref_string);
	}
	ast_xml_set_root(intermediate, root_node);

	eprofile_count = ast_geoloc_datastore_size(ds);
	for (i = 0; i < eprofile_count; i++) {
		struct ast_xml_node *temp_node = NULL;
		struct ast_xml_node *curr_loc = NULL;
		struct ast_xml_node *new_loc = NULL;
		struct ast_xml_node *new_loc_child = NULL;
		struct ast_xml_node *new_loc_child_dup = NULL;
		const char *entity = NULL;
		int has_no_entity = 0;
		eprofile = ast_geoloc_datastore_get_eprofile(ds, i);
		if (eprofile->format == AST_GEOLOC_FORMAT_URI) {
			continue;
		}

		entity = ast_xml_get_attribute(root_node, "entity");
		has_no_entity = ast_strlen_zero(entity);
		ast_xml_free_attr(entity);
		if (has_no_entity) {
			rc = ast_xml_set_attribute(root_node, "entity", eprofile->id);
			if (rc != 0) {
				SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to set 'entity' XML attribute\n", ref_string);
			}
		}

		temp_node = geoloc_eprofile_to_intermediate(ast_geoloc_pidf_element_to_name(eprofile->pidf_element),
			eprofile, chan, ref_string);
		if (!temp_node) {
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create temp_node\n", ref_string);
		}

		if (!pidfs[eprofile->pidf_element]) {
			pidfs[eprofile->pidf_element] = temp_node;
			ast_xml_add_child(root_node, temp_node);
			continue;
		}

		curr_loc = ast_xml_find_child_element(pidfs[eprofile->pidf_element], "location-info", NULL, NULL);
		new_loc = ast_xml_find_child_element(temp_node, "location-info", NULL, NULL);
		new_loc_child = ast_xml_node_get_children(new_loc);
		new_loc_child_dup = ast_xml_copy_node_list(new_loc_child);
		ast_xml_add_child_list(curr_loc, new_loc_child_dup);

		ast_xml_free_node(temp_node);
	}

	if (TRACE_ATLEAST(5)) {
		ast_xml_doc_dump_memory(intermediate, &doc_str, &doc_len);
		ast_trace(5, "Intermediate doc len: %d\n%s\n", doc_len, doc_len ? doc_str : "<empty>");
		ast_xml_free_text(doc_str);
		doc_str = NULL;
		doc_len = 0;
	}

	pidf_doc = ast_xslt_apply(eprofile_to_pidf_xslt, intermediate, NULL);
	if (!pidf_doc) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create final PIDF-LO doc from intermediate docs\n",
			ref_string);
	}

	ast_xml_doc_dump_memory(pidf_doc, &doc_str, &doc_len);
	if (doc_len == 0 || !doc_str) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to dump final PIDF-LO doc to string\n",
			ref_string);
	}

	rc = ast_str_set(buf, 0, "%s", doc_str);
	ast_xml_free_text(doc_str);
	if (rc <= 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to extend buffer (%d)\n",
			ref_string, rc);
	}

	ast_trace(5, "Final doc:\n%s\n", ast_str_buffer(*buf));

	SCOPE_EXIT_RTN_VALUE(ast_str_buffer(*buf), "%s: Done\n", ref_string);
}

const char *ast_geoloc_eprofile_to_pidf(struct ast_geoloc_eprofile *eprofile,
	struct ast_channel *chan, struct ast_str **buf, const char * ref_string)
{
	RAII_VAR(struct ast_xml_doc *, intermediate, NULL, ast_xml_close);
	RAII_VAR(struct ast_xml_doc *, pidf_doc, NULL, ast_xml_close);
	struct ast_xml_node *root_node;
	char *doc_str = NULL;
	int doc_len;
	int rc = 0;
	struct ast_xml_node *temp_node = NULL;
	const char *entity = NULL;
	int has_no_entity = 0;
	const char *params[] = { "suppress_empty_ca_elements", "false()", NULL };

	SCOPE_ENTER(3, "%s\n", ref_string);

	if (!eprofile || !chan || !buf || !*buf || ast_strlen_zero(ref_string)) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: One of eprofile, chan or buf was NULL\n",
			ref_string);
	}

	if (eprofile->format == AST_GEOLOC_FORMAT_URI) {
		SCOPE_EXIT_RTN_VALUE(NULL, "%s: eprofile '%s' was a URI format\n",
			ref_string, eprofile->id);
	}

	intermediate = ast_xml_new();
	if (!intermediate) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create XML document\n", ref_string);
	}
	root_node = ast_xml_new_node("presence");
	if (!root_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create root XML node\n", ref_string);
	}
	ast_xml_set_root(intermediate, root_node);

	entity = ast_xml_get_attribute(root_node, "entity");
	has_no_entity = ast_strlen_zero(entity);
	ast_xml_free_attr(entity);
	if (has_no_entity) {
		rc = ast_xml_set_attribute(root_node, "entity", eprofile->id);
		if (rc != 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to set 'entity' XML attribute\n", ref_string);
		}
	}

	temp_node = geoloc_eprofile_to_intermediate(
		ast_geoloc_pidf_element_to_name(eprofile->pidf_element), eprofile, chan, ref_string);
	if (!temp_node) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create temp_node for eprofile '%s'\n",
			ref_string, eprofile->id);
	}

	ast_xml_add_child(root_node, temp_node);

	if (TRACE_ATLEAST(5)) {
		ast_xml_doc_dump_memory(intermediate, &doc_str, &doc_len);
		ast_trace(5, "Intermediate doc len: %d\n%s\n", doc_len, doc_len ? doc_str : "<empty>");
		ast_xml_free_text(doc_str);
		doc_str = NULL;
		doc_len = 0;
	}

	if (eprofile->suppress_empty_ca_elements) {
		params[1] = "true()";
	}
	pidf_doc = ast_xslt_apply(eprofile_to_pidf_xslt, intermediate, params);
	if (!pidf_doc) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to create final PIDF-LO doc from intermediate doc\n",
			ref_string);
	}

	ast_xml_doc_dump_memory(pidf_doc, &doc_str, &doc_len);
	if (doc_len == 0 || !doc_str) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to dump final PIDF-LO doc to string\n",
			ref_string);
	}

	rc = ast_str_set(buf, 0, "%s", doc_str);
	ast_xml_free_text(doc_str);
	if (rc <= 0) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Unable to extend buffer (%d)\n",
			ref_string, rc);
	}

	ast_trace(5, "Final doc:\n%s\n", ast_str_buffer(*buf));

	SCOPE_EXIT_RTN_VALUE(ast_str_buffer(*buf), "%s: Done\n", ref_string);
}

#ifdef TEST_FRAMEWORK
static void load_tests(void);
static void unload_tests(void);
#else
static void load_tests(void) {}
static void unload_tests(void) {}
#endif


int geoloc_eprofile_unload(void)
{
	unload_tests();
	if (pidf_to_eprofile_xslt) {
		ast_xslt_close(pidf_to_eprofile_xslt);
	}

	if (eprofile_to_pidf_xslt) {
		ast_xslt_close(eprofile_to_pidf_xslt);
	}

	if (geoloc_sorcery) {
		ast_sorcery_unref(geoloc_sorcery);
	}

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_eprofile_load(void)
{
	pidf_to_eprofile_xslt_size =
		(_binary_res_geolocation_pidf_to_eprofile_xslt_end - _binary_res_geolocation_pidf_to_eprofile_xslt_start);

	pidf_lo_test_xml_size =
		(_binary_res_geolocation_pidf_lo_test_xml_end - _binary_res_geolocation_pidf_lo_test_xml_start);

	pidf_to_eprofile_xslt = ast_xslt_read_memory(
		(char *)_binary_res_geolocation_pidf_to_eprofile_xslt_start, pidf_to_eprofile_xslt_size);
	if (!pidf_to_eprofile_xslt) {
		ast_log(LOG_ERROR, "Unable to read pidf_to_eprofile_xslt from memory\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	eprofile_to_pidf_xslt_size =
		(_binary_res_geolocation_eprofile_to_pidf_xslt_end - _binary_res_geolocation_eprofile_to_pidf_xslt_start);

	eprofile_to_pidf_xslt = ast_xslt_read_memory(
		(char *)_binary_res_geolocation_eprofile_to_pidf_xslt_start, eprofile_to_pidf_xslt_size);
	if (!eprofile_to_pidf_xslt) {
		ast_log(LOG_ERROR, "Unable to read eprofile_to_pidf_xslt from memory\n");
//		geoloc_eprofile_unload();
		return AST_MODULE_LOAD_DECLINE;
	}

	geoloc_sorcery = geoloc_get_sorcery();

	load_tests();

	return AST_MODULE_LOAD_SUCCESS;
}

int geoloc_eprofile_reload(void)
{
	return AST_MODULE_LOAD_SUCCESS;
}


#ifdef TEST_FRAMEWORK
#include "asterisk/test.h"

AST_TEST_DEFINE(test_create_from_uri)
{

	RAII_VAR(struct ast_geoloc_eprofile *, eprofile,  NULL, ao2_cleanup);
	const char *uri = NULL;
	int rc = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "create_from_uri";
		info->category = "/geoloc/";
		info->summary = "Test create from uri";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	eprofile = ast_geoloc_eprofile_create_from_uri("http://some_uri&a=b", __func__);
	ast_test_validate(test, eprofile != NULL);
	ast_test_validate(test, eprofile->format == AST_GEOLOC_FORMAT_URI);
	ast_test_validate(test, eprofile->location_info != NULL);
	uri = ast_variable_find_in_list(eprofile->location_info, "URI");
	ast_test_validate(test, uri != NULL);
	ast_test_validate(test, strcmp(uri, "http://some_uri&a=b") == 0);

	return rc;
}

static enum ast_test_result_state validate_eprofile(struct ast_test *test,
	struct ast_xml_doc * pidf_xmldoc,
	const char *path,
	const char *id,
	enum ast_geoloc_pidf_element pidf_element,
	enum ast_geoloc_format format,
	const char *method,
	const char *location,
	const char *usage
	)
{
	RAII_VAR(struct ast_str *, str, NULL, ast_free);
	RAII_VAR(struct ast_geoloc_eprofile *, eprofile,  NULL, ao2_cleanup);
	RAII_VAR(struct ast_xml_doc *, result_doc, NULL, ast_xml_close);

	if (!ast_strlen_zero(path)) {
		result_doc = ast_xslt_apply(pidf_to_eprofile_xslt, pidf_xmldoc, NULL);
		ast_test_validate(test, (result_doc && ast_xml_node_get_children((struct ast_xml_node *)result_doc)));

		eprofile = geoloc_eprofile_create_from_xslt_result(result_doc, "test_create_from_xslt");
	} else {
		eprofile = ast_geoloc_eprofile_create_from_pidf(pidf_xmldoc, NULL, "test_create_from_pidf");
	}

	ast_test_validate(test, eprofile != NULL);
	ast_test_status_update(test, "ID: '%s'  pidf_element: '%s'  format: '%s'  method: '%s'\n", eprofile->id,
		ast_geoloc_pidf_element_to_name(eprofile->pidf_element),
		ast_geoloc_format_to_name(eprofile->format),
		eprofile->method);

	ast_test_validate(test, ast_strings_equal(eprofile->id, id));
	ast_test_validate(test, eprofile->pidf_element == pidf_element);
	ast_test_validate(test, eprofile->format == format);
	ast_test_validate(test, ast_strings_equal(eprofile->method, method));

	str = ast_variable_list_join(eprofile->location_info, ",", "=", NULL, NULL);
	ast_test_validate(test, str != NULL);
	ast_test_status_update(test, "location_vars expected: %s\n", location);
	ast_test_status_update(test, "location_vars received: %s\n", ast_str_buffer(str));
	ast_test_validate(test, ast_strings_equal(ast_str_buffer(str), location));
	ast_free(str);

	str = ast_variable_list_join(eprofile->usage_rules, ",", "=", "'", NULL);
	ast_test_validate(test, str != NULL);
	ast_test_status_update(test, "usage_rules expected: %s\n", usage);
	ast_test_status_update(test, "usage_rules received: %s\n", ast_str_buffer(str));
	ast_test_validate(test, ast_strings_equal(ast_str_buffer(str), usage));

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(test_create_from_pidf)
{

	RAII_VAR(struct ast_xml_doc *, pidf_xmldoc, NULL, ast_xml_close);
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "create_from_pidf";
		info->category = "/geoloc/";
		info->summary = "Test create from pidf scenarios";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	pidf_xmldoc = ast_xml_read_memory((char *)_binary_res_geolocation_pidf_lo_test_xml_start, pidf_lo_test_xml_size);
	ast_test_validate(test, pidf_xmldoc != NULL);

	res = validate_eprofile(test, pidf_xmldoc,
		NULL,
		"point-2d",
		AST_PIDF_ELEMENT_TUPLE,
		AST_GEOLOC_FORMAT_GML,
		"Manual",
		"shape=Point,crs=2d,pos=-34.410649 150.87651",
		"retransmission-allowed='no',retention-expiry='2010-11-14T20:00:00Z'"
		);
	ast_test_validate(test, res == AST_TEST_PASS);

	return res;
}

static void load_tests(void) {
	AST_TEST_REGISTER(test_create_from_uri);
	AST_TEST_REGISTER(test_create_from_pidf);
}
static void unload_tests(void) {
	AST_TEST_UNREGISTER(test_create_from_uri);
	AST_TEST_UNREGISTER(test_create_from_pidf);
}

#endif
