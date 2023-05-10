/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Commend International
 *
 * Maximilian Fridrich <m.fridrich@commend.com>
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

/*! \file
 *
 * \brief Out-of-call refer support
 *
 * \author Maximilian Fridrich <m.fridrich@commend.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"

#include "asterisk/module.h"
#include "asterisk/datastore.h"
#include "asterisk/pbx.h"
#include "asterisk/manager.h"
#include "asterisk/strings.h"
#include "asterisk/astobj2.h"
#include "asterisk/vector.h"
#include "asterisk/app.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/refer.h"

struct refer_data {
	/* Stored in stuff[] at struct end */
	char *name;
	/* Stored separately */
	char *value;
	/* Holds name */
	char stuff[0];
};

/*!
 * \brief A refer.
 */
struct ast_refer {
	AST_DECLARE_STRING_FIELDS(
		/*! Where the refer is going */
		AST_STRING_FIELD(to);
		/*! Where we "say" the refer came from */
		AST_STRING_FIELD(from);
		/*! Where to refer to */
		AST_STRING_FIELD(refer_to);
		/*! An endpoint associated with this refer */
		AST_STRING_FIELD(endpoint);
		/*! The technology of the endpoint associated with this refer */
		AST_STRING_FIELD(tech);
	);
	/* Whether to refer to Asterisk itself, if refer_to is an Asterisk endpoint. */
	int to_self;
	/*! Technology/dialplan specific variables associated with the refer */
	struct ao2_container *vars;
};

/*! \brief Lock for \c refer_techs vector */
static ast_rwlock_t refer_techs_lock;

/*! \brief Vector of refer technologies */
AST_VECTOR(, const struct ast_refer_tech *) refer_techs;

static int refer_data_cmp_fn(void *obj, void *arg, int flags)
{
	const struct refer_data *object_left = obj;
	const struct refer_data *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->name;
	case OBJ_SEARCH_KEY:
		cmp = strcasecmp(object_left->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncasecmp(object_left->name, right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	return CMP_MATCH;
}

static void refer_data_destructor(void *obj)
{
	struct refer_data *data = obj;
	ast_free(data->value);
	ast_free(data);
}

static void refer_destructor(void *obj)
{
	struct ast_refer *refer = obj;

	ast_string_field_free_memory(refer);
	ao2_cleanup(refer->vars);
}

struct ast_refer *ast_refer_alloc(void)
{
	struct ast_refer *refer;

	if (!(refer = ao2_alloc_options(sizeof(*refer), refer_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK))) {
		return NULL;
	}

	if (ast_string_field_init(refer, 128)) {
		ao2_ref(refer, -1);
		return NULL;
	}

	refer->vars = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		NULL, refer_data_cmp_fn);
	if (!refer->vars) {
		ao2_ref(refer, -1);
		return NULL;
	}
	refer->to_self = 0;

	return refer;
}

struct ast_refer *ast_refer_ref(struct ast_refer *refer)
{
	ao2_ref(refer, 1);
	return refer;
}

struct ast_refer *ast_refer_destroy(struct ast_refer *refer)
{
	ao2_ref(refer, -1);
	return NULL;
}

int ast_refer_set_to(struct ast_refer *refer, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(refer, to, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_refer_set_from(struct ast_refer *refer, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(refer, from, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_refer_set_refer_to(struct ast_refer *refer, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(refer, refer_to, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_refer_set_to_self(struct ast_refer *refer, int val)
{
	refer->to_self = val;
	return 0;
}

int ast_refer_set_tech(struct ast_refer *refer, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(refer, tech, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_refer_set_endpoint(struct ast_refer *refer, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(refer, endpoint, fmt, ap);
	va_end(ap);

	return 0;
}

const char *ast_refer_get_refer_to(const struct ast_refer *refer)
{
	return refer->refer_to;
}

const char *ast_refer_get_from(const struct ast_refer *refer)
{
	return refer->from;
}

const char *ast_refer_get_to(const struct ast_refer *refer)
{
	return refer->to;
}

int ast_refer_get_to_self(const struct ast_refer *refer)
{
	return refer->to_self;
}

const char *ast_refer_get_tech(const struct ast_refer *refer)
{
	return refer->tech;
}

const char *ast_refer_get_endpoint(const struct ast_refer *refer)
{
	return refer->endpoint;
}

static struct refer_data *refer_data_new(const char *name)
{
	struct refer_data *data;
	int name_len = strlen(name) + 1;

	if ((data = ao2_alloc_options(name_len + sizeof(*data), refer_data_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK))) {
		data->name = data->stuff;
		strcpy(data->name, name);
	}

	return data;
}

static struct refer_data *refer_data_find(struct ao2_container *vars, const char *name)
{
	return ao2_find(vars, name, OBJ_SEARCH_KEY);
}

char *ast_refer_get_var_and_unlink(struct ast_refer *refer, const char *name)
{
	struct refer_data *data;
	char *val = NULL;

	if (!(data = ao2_find(refer->vars, name, OBJ_SEARCH_KEY | OBJ_UNLINK))) {
		return NULL;
	}

	val = ast_strdup(data->value);
	ao2_ref(data, -1);

	return val;
}

static int refer_set_var_full(struct ast_refer *refer, const char *name, const char *value)
{
	struct refer_data *data;

	if (!(data = refer_data_find(refer->vars, name))) {
		if (ast_strlen_zero(value)) {
			return 0;
		}
		if (!(data = refer_data_new(name))) {
			return -1;
		};
		data->value = ast_strdup(value);

		ao2_link(refer->vars, data);
	} else {
		if (ast_strlen_zero(value)) {
			ao2_unlink(refer->vars, data);
		} else {
			data->value = ast_strdup(value);
		}
	}

	ao2_ref(data, -1);

	return 0;
}

int ast_refer_set_var_outbound(struct ast_refer *refer, const char *name, const char *value)
{
	return refer_set_var_full(refer, name, value);
}

const char *ast_refer_get_var(struct ast_refer *refer, const char *name)
{
	struct refer_data *data;
	const char *val = NULL;

	if (!(data = refer_data_find(refer->vars, name))) {
		return NULL;
	}

	val = data->value;
	ao2_ref(data, -1);

	return val;
}

struct ast_refer_var_iterator {
	struct ao2_iterator iter;
	struct refer_data *current_used;
};

struct ast_refer_var_iterator *ast_refer_var_iterator_init(const struct ast_refer *refer)
{
	struct ast_refer_var_iterator *iter;

	iter = ast_calloc(1, sizeof(*iter));
	if (!iter) {
		return NULL;
	}

	iter->iter = ao2_iterator_init(refer->vars, 0);

	return iter;
}

int ast_refer_var_iterator_next(struct ast_refer_var_iterator *iter, const char **name, const char **value)
{
	struct refer_data *data;

	if (!iter) {
		return 0;
	}

	data = ao2_iterator_next(&iter->iter);
	if (!data) {
		return 0;
	}

	*name = data->name;
	*value = data->value;

	iter->current_used = data;

	return 1;
}

void ast_refer_var_unref_current(struct ast_refer_var_iterator *iter)
{
	ao2_cleanup(iter->current_used);
	iter->current_used = NULL;
}

void ast_refer_var_iterator_destroy(struct ast_refer_var_iterator *iter)
{
	if (iter) {
		ao2_iterator_destroy(&iter->iter);
		ast_refer_var_unref_current(iter);
		ast_free(iter);
	}
}

/*!
 * \internal \brief Find a \c ast_refer_tech by its technology name
 *
 * \param tech_name The name of the refer technology
 *
 * \note \c refer_techs should be locked via \c refer_techs_lock prior to
 *       calling this function
 *
 * \retval NULL if no \ref ast_refer_tech has been registered
 * \return \ref ast_refer_tech if registered
 */
static const struct ast_refer_tech *refer_find_by_tech_name(const char *tech_name)
{
	const struct ast_refer_tech *current;
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&refer_techs); i++) {
		current = AST_VECTOR_GET(&refer_techs, i);
		if (!strcmp(current->name, tech_name)) {
			return current;
		}
	}

	return NULL;
}

int ast_refer_send(struct ast_refer *refer)
{
	char *tech_name = NULL;
	const struct ast_refer_tech *refer_tech;
	int res = -1;

	if (ast_strlen_zero(refer->to)) {
		ao2_ref(refer, -1);
		return -1;
	}

	tech_name = ast_strdupa(refer->to);
	tech_name = strsep(&tech_name, ":");

	ast_rwlock_rdlock(&refer_techs_lock);
	refer_tech = refer_find_by_tech_name(tech_name);

	if (!refer_tech) {
		ast_log(LOG_ERROR, "Unknown refer tech: %s\n", tech_name);
		ast_rwlock_unlock(&refer_techs_lock);
		ao2_ref(refer, -1);
		return -1;
	}

	ao2_lock(refer);
	res = refer_tech->refer_send(refer);
	ao2_unlock(refer);

	ast_rwlock_unlock(&refer_techs_lock);

	ao2_ref(refer, -1);

	return res;
}

int ast_refer_tech_register(const struct ast_refer_tech *tech)
{
	const struct ast_refer_tech *match;

	ast_rwlock_wrlock(&refer_techs_lock);

	match = refer_find_by_tech_name(tech->name);
	if (match) {
		ast_log(LOG_ERROR, "Refer technology already registered for '%s'\n",
		        tech->name);
		ast_rwlock_unlock(&refer_techs_lock);
		return -1;
	}

	if (AST_VECTOR_APPEND(&refer_techs, tech)) {
		ast_log(LOG_ERROR, "Failed to register refer technology for '%s'\n",
		        tech->name);
		ast_rwlock_unlock(&refer_techs_lock);
		return -1;
	}
	ast_verb(3, "Refer technology '%s' registered.\n", tech->name);

	ast_rwlock_unlock(&refer_techs_lock);

	return 0;
}

/*!
 * \brief Comparison callback for \c ast_refer_tech vector removal
 *
 * \param vec_elem The element in the vector being compared
 * \param srch The element being looked up
 *
 * \retval non-zero The items are equal
 * \retval 0 The items are not equal
 */
static int refer_tech_cmp(const struct ast_refer_tech *vec_elem, const struct ast_refer_tech *srch)
{
	if (!vec_elem->name || !srch->name) {
		return (vec_elem->name == srch->name) ? 1 : 0;
	}
	return !strcmp(vec_elem->name, srch->name);
}

int ast_refer_tech_unregister(const struct ast_refer_tech *tech)
{
	int match;

	ast_rwlock_wrlock(&refer_techs_lock);
	match = AST_VECTOR_REMOVE_CMP_UNORDERED(&refer_techs, tech, refer_tech_cmp,
	                                        AST_VECTOR_ELEM_CLEANUP_NOOP);
	ast_rwlock_unlock(&refer_techs_lock);

	if (match) {
		ast_log(LOG_ERROR, "No '%s' refer technology found.\n", tech->name);
		return -1;
	}

	ast_verb(2, "Refer technology '%s' unregistered.\n", tech->name);

	return 0;
}

/*!
 * \internal
 * \brief Clean up other resources on Asterisk shutdown
 */
static void refer_shutdown(void)
{
	AST_VECTOR_FREE(&refer_techs);
	ast_rwlock_destroy(&refer_techs_lock);
}

/*!
 * \internal
 * \brief Initialize stuff during Asterisk startup.
 *
 * Cleanup isn't a big deal in this function.  If we return non-zero,
 * Asterisk is going to exit.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_refer_init(void)
{
	ast_rwlock_init(&refer_techs_lock);
	if (AST_VECTOR_INIT(&refer_techs, 8)) {
		return -1;
	}
	ast_register_cleanup(refer_shutdown);
	return 0;
}
