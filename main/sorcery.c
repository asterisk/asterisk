/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
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

/*! \file
 *
 * \brief Sorcery Data Access Layer API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/logger.h"
#include "asterisk/sorcery.h"
#include "asterisk/astobj2.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/strings.h"
#include "asterisk/config_options.h"
#include "asterisk/netsock2.h"
#include "asterisk/module.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/threadpool.h"
#include "asterisk/json.h"
#include "asterisk/vector.h"

/* To prevent DEBUG_FD_LEAKS from interfering with things we undef open and close */
#undef open
#undef close

/*! \brief Number of buckets for wizards (should be prime for performance reasons) */
#define WIZARD_BUCKETS 7

/*! \brief Number of buckets for types (should be prime for performance reasons) */
#define TYPE_BUCKETS 53

/*! \brief Number of buckets for instances (should be prime for performance reasons) */
#define INSTANCE_BUCKETS 17

/*! \brief Number of buckets for object fields (should be prime for performance reasons) */
#define OBJECT_FIELD_BUCKETS 29

#define NOTIFY_GENERIC_OBSERVERS(container, type, callback, ...) ({ \
	struct ao2_iterator i = ao2_iterator_init(container, 0); \
	struct type *observer; \
	ao2_rdlock(container); \
	while ((observer = ao2_iterator_next(&i))) { \
		if (observer->callbacks->callback) { \
			observer->callbacks->callback(__VA_ARGS__); \
		} \
		ao2_cleanup(observer); \
	} \
	ao2_unlock(container); \
	ao2_iterator_cleanup(&i); \
})

#define NOTIFY_GLOBAL_OBSERVERS(container, callback, ...) \
	NOTIFY_GENERIC_OBSERVERS(container, sorcery_global_observer, callback, __VA_ARGS__)

#define NOTIFY_INSTANCE_OBSERVERS(container, callback, ...) \
	NOTIFY_GENERIC_OBSERVERS(container, sorcery_instance_observer, callback, __VA_ARGS__)

#define NOTIFY_WIZARD_OBSERVERS(container, callback, ...) \
	NOTIFY_GENERIC_OBSERVERS(container, sorcery_wizard_observer, callback, __VA_ARGS__)

/*! \brief Thread pool for observers */
static struct ast_threadpool *threadpool;

/*! \brief Structure for an internal wizard instance */
struct ast_sorcery_internal_wizard {
	/*! \brief Wizard interface itself */
	struct ast_sorcery_wizard callbacks;

	/*! \brief Observers */
	struct ao2_container *observers;
};

/*! \brief Structure for a wizard instance which operates on objects */
struct ast_sorcery_object_wizard {
	/*! \brief Wizard interface itself */
	struct ast_sorcery_internal_wizard *wizard;

	/*! \brief Unique data for the wizard */
	void *data;

	/*! \brief Wizard is acting as an object cache */
	unsigned int caching:1;
};

/*! \brief Structure for internal sorcery object information */
struct ast_sorcery_object {
	/*! \brief Unique identifier of this object */
	char *id;

	/*! \brief Type of object */
	char type[MAX_OBJECT_TYPE];

	/*! \brief Optional object destructor */
	ao2_destructor_fn destructor;

	/*! \brief Extended object fields */
	struct ast_variable *extended;
};

/*! \brief Structure for registered object type */
struct ast_sorcery_object_type {
	/*! \brief Unique name of the object type */
	char name[MAX_OBJECT_TYPE];

	/*! \brief Optional transformation callback */
	sorcery_transform_handler transform;

	/*! \brief Optional object set apply callback */
	sorcery_apply_handler apply;

	/*! \brief Optional object copy callback */
	sorcery_copy_handler copy;

	/*! \brief Optional object diff callback */
	sorcery_diff_handler diff;

	/*! \brief Wizard instances */
	struct ast_sorcery_object_wizards wizards;

	/*! \brief Object fields */
	struct ao2_container *fields;

	/*! \brief Configuration framework general information */
	struct aco_info *info;

	/*! \brief Configuration framework file information */
	struct aco_file *file;

	/*! \brief Type details */
	struct aco_type type;

	/*! \brief Observers */
	struct ao2_container *observers;

	/*! \brief Serializer for observers */
	struct ast_taskprocessor *serializer;

	/*! \brief Specifies if object type is reloadable or not */
	unsigned int reloadable:1;
};

/*! \brief Structure for registered object type observer */
struct ast_sorcery_object_type_observer {
	/*! \brief Pointer to the observer implementation */
	const struct ast_sorcery_observer *callbacks;
};

/*! \brief Structure used for observer invocations */
struct sorcery_observer_invocation {
	/*! \brief Pointer to the object type */
	struct ast_sorcery_object_type *object_type;

	/*! \brief Pointer to the object */
	void *object;
};

/*! \brief Structure for registered object field */
struct ast_sorcery_object_field {
	/*! \brief Name of the field */
	char name[MAX_OBJECT_FIELD];

	/*! \brief The compiled name regex if name is a regex */
	regex_t *name_regex;

	/*! \brief Callback function for translation of a single value */
	sorcery_field_handler handler;

	/*! \brief Callback function for translation of multiple values */
	sorcery_fields_handler multiple_handler;

	/*! \brief Position of the field */
	intptr_t args[];
};

/*! \brief Full structure for sorcery */
struct ast_sorcery {
	/*! \brief Container for known object types */
	struct ao2_container *types;

	/*! \brief Observers */
	struct ao2_container *observers;

	/*! \brief The name of the module owning this sorcery instance */
	char module_name[0];
};

/*! \brief Structure for passing load/reload details */
struct sorcery_load_details {
	/*! \brief Sorcery structure in use */
	const struct ast_sorcery *sorcery;

	/*! \brief Type of object being loaded */
	const char *type;

	/*! \brief Whether this is a reload or not */
	unsigned int reload:1;
};

/*! \brief Registered sorcery wizards */
static struct ao2_container *wizards;

/* The following 3 observer wrappers must name their
 * external observer 'callbacks' and it must be
 * the first member of the structure.  Common macros
 * and container callbacks depend on it.
 */

/*! \brief A global observer wrapper */
struct sorcery_global_observer {
	const struct ast_sorcery_global_observer *callbacks;
};

/*! \brief An instance observer wrapper */
struct sorcery_instance_observer {
	const struct ast_sorcery_instance_observer *callbacks;
};

/*! \brief A wizard observer wrapper */
struct sorcery_wizard_observer {
	const struct ast_sorcery_wizard_observer *callbacks;
};

/*! \brief Registered global observers */
struct ao2_container *observers;

/*! \brief Registered sorcery instances */
static struct ao2_container *instances;

static int int_handler_fn(const void *obj, const intptr_t *args, char **buf)
{
	int *field = (int *)(obj + args[0]);
	return (ast_asprintf(buf, "%d", *field) < 0) ? -1 : 0;
}

static int uint_handler_fn(const void *obj, const intptr_t *args, char **buf)
{
	unsigned int *field = (unsigned int *)(obj + args[0]);
	return (ast_asprintf(buf, "%u", *field) < 0) ? -1 : 0;
}

static int double_handler_fn(const void *obj, const intptr_t *args, char **buf)
{
	double *field = (double *)(obj + args[0]);
	return (ast_asprintf(buf, "%f", *field) < 0) ? -1 : 0;
}

static int stringfield_handler_fn(const void *obj, const intptr_t *args, char **buf)
{
	ast_string_field *field = (const char **)(obj + args[0]);
	return !(*buf = ast_strdup(*field)) ? -1 : 0;
}

static int bool_handler_fn(const void *obj, const intptr_t *args, char **buf)
{
	unsigned int *field = (unsigned int *)(obj + args[0]);
	return !(*buf = ast_strdup(*field ? "true" : "false")) ? -1 : 0;
}

static int sockaddr_handler_fn(const void *obj, const intptr_t *args, char **buf)
{
	struct ast_sockaddr *field = (struct ast_sockaddr *)(obj + args[0]);
	return !(*buf = ast_strdup(ast_sockaddr_stringify(field))) ? -1 : 0;
}

static int chararray_handler_fn(const void *obj, const intptr_t *args, char **buf)
{
	char *field = (char *)(obj + args[0]);
	return !(*buf = ast_strdup(field)) ? -1 : 0;
}

static int codec_handler_fn(const void *obj, const intptr_t *args, char **buf)
{
	struct ast_str *codec_buf = ast_str_alloca(64);
	struct ast_format_cap **cap = (struct ast_format_cap **)(obj + args[0]);
	return !(*buf = ast_strdup(ast_format_cap_get_names(*cap, &codec_buf)));
}

static sorcery_field_handler sorcery_field_default_handler(enum aco_option_type type)
{
	switch(type) {
	case OPT_BOOL_T: return bool_handler_fn;
	case OPT_CHAR_ARRAY_T: return chararray_handler_fn;
	case OPT_CODEC_T: return codec_handler_fn;
	case OPT_DOUBLE_T: return double_handler_fn;
	case OPT_INT_T: return int_handler_fn;
	case OPT_SOCKADDR_T: return sockaddr_handler_fn;
	case OPT_STRINGFIELD_T: return stringfield_handler_fn;
	case OPT_UINT_T: return uint_handler_fn;

	default:
	case OPT_CUSTOM_T: return NULL;
	}

	return NULL;
}

/*! \brief Hashing function for sorcery wizards */
static int sorcery_wizard_hash(const void *obj, const int flags)
{
	const struct ast_sorcery_internal_wizard *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->callbacks.name;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief Comparator function for sorcery wizards */
static int sorcery_wizard_cmp(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_internal_wizard *object_left = obj;
	const struct ast_sorcery_internal_wizard *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->callbacks.name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->callbacks.name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(object_left->callbacks.name, right_key, strlen(right_key));
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

/*! \brief Hashing function for sorcery wizards */
static int object_type_field_hash(const void *obj, const int flags)
{
	const struct ast_sorcery_object_field *object_field;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object_field = obj;
		key = object_field->name;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

static int object_type_field_cmp(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_field *field_left = obj;
	const struct ast_sorcery_object_field *field_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = field_right->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(field_left->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(field_left->name, right_key, strlen(right_key));
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

/*! \brief Cleanup function for graceful shutdowns */
static void sorcery_cleanup(void)
{
	ast_threadpool_shutdown(threadpool);
	threadpool = NULL;
	ao2_cleanup(wizards);
	wizards = NULL;
	ao2_cleanup(observers);
	observers = NULL;
	ao2_cleanup(instances);
	instances = NULL;
}

/*! \brief Compare function for sorcery instances */
static int sorcery_instance_cmp(void *obj, void *arg, int flags)
{
	const struct ast_sorcery *object_left = obj;
	const struct ast_sorcery *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->module_name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->module_name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(object_left->module_name, right_key, strlen(right_key));
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

/*! \brief Hashing function for sorcery instances */
static int sorcery_instance_hash(const void *obj, const int flags)
{
	const struct ast_sorcery *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->module_name;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

int ast_sorcery_init(void)
{
	struct ast_threadpool_options options = {
		.version = AST_THREADPOOL_OPTIONS_VERSION,
		.auto_increment = 1,
		.max_size = 0,
		.idle_timeout = 60,
		.initial_size = 0,
	};
	ast_assert(wizards == NULL);

	if (!(threadpool = ast_threadpool_create("Sorcery", NULL, &options))) {
		threadpool = NULL;
		return -1;
	}

	if (!(wizards = ao2_container_alloc(WIZARD_BUCKETS, sorcery_wizard_hash, sorcery_wizard_cmp))) {
		ast_threadpool_shutdown(threadpool);
		return -1;
	}

	observers = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_RWLOCK, 0, NULL, NULL);
	if (!observers) {
		sorcery_cleanup();
		return -1;
	}

	instances = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_RWLOCK, INSTANCE_BUCKETS,
		sorcery_instance_hash, sorcery_instance_cmp);
	if (!instances) {
		sorcery_cleanup();
		return -1;
	}

	ast_register_cleanup(sorcery_cleanup);

	return 0;
}

static void sorcery_internal_wizard_destructor(void *obj)
{
	struct ast_sorcery_internal_wizard *wizard = obj;

	ao2_cleanup(wizard->observers);
}

int __ast_sorcery_wizard_register(const struct ast_sorcery_wizard *interface, struct ast_module *module)
{
	struct ast_sorcery_internal_wizard *wizard;
	int res = -1;

	ast_assert(!ast_strlen_zero(interface->name));

	ao2_lock(wizards);

	if ((wizard = ao2_find(wizards, interface->name, OBJ_KEY | OBJ_NOLOCK))) {
		ast_log(LOG_WARNING, "Attempted to register sorcery wizard '%s' twice\n",
			interface->name);
		goto done;
	}

	if (!(wizard = ao2_alloc(sizeof(*wizard), sorcery_internal_wizard_destructor))) {
		goto done;
	}

	wizard->callbacks = *interface;
	wizard->callbacks.module = module;

	wizard->observers = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_RWLOCK, 0, NULL, NULL);
	if (!wizard->observers) {
		goto done;
	}

	ao2_link_flags(wizards, wizard, OBJ_NOLOCK);
	res = 0;

	ast_verb(2, "Sorcery registered wizard '%s'\n", interface->name);

	NOTIFY_GLOBAL_OBSERVERS(observers, wizard_registered,
		interface->name, interface);

done:
	ao2_cleanup(wizard);
	ao2_unlock(wizards);

	return res;
}

int ast_sorcery_wizard_unregister(const struct ast_sorcery_wizard *interface)
{
	struct ast_sorcery_internal_wizard *wizard =
		interface ? ao2_find(wizards, interface->name, OBJ_SEARCH_KEY) : NULL;

	if (wizard) {
		NOTIFY_GLOBAL_OBSERVERS(observers, wizard_unregistering, wizard->callbacks.name, &wizard->callbacks);
		ao2_unlink(wizards, wizard);
		ao2_ref(wizard, -1);
		ast_verb(2, "Sorcery unregistered wizard '%s'\n", interface->name);
		return 0;
	} else {
		return -1;
	}
}

/*! \brief Internal callback function for removing a generic observer */
static int sorcery_generic_observer_remove(void *obj, void *arg, int flags)
{
	const struct sorcery_global_observer *observer = obj;

	return (observer->callbacks == arg) ? CMP_MATCH | CMP_STOP : 0;
}

int ast_sorcery_global_observer_add(const struct ast_sorcery_global_observer *callbacks)
{
	struct sorcery_global_observer *cb;

	cb = ao2_alloc(sizeof(*cb), NULL);
	if (!cb) {
		return -1;
	}

	cb->callbacks = callbacks;
	ao2_link(observers, cb);
	ao2_ref(cb, -1);

	return 0;
}

void ast_sorcery_global_observer_remove(
	const struct ast_sorcery_global_observer *callbacks)
{
	ao2_callback(observers, OBJ_NODATA | OBJ_UNLINK, sorcery_generic_observer_remove, (void *)callbacks);
}

int ast_sorcery_instance_observer_add(struct ast_sorcery *sorcery,
	const struct ast_sorcery_instance_observer *callbacks)
{
	struct sorcery_instance_observer *cb;

	cb = ao2_alloc(sizeof(*cb), NULL);
	if (!cb) {
		return -1;
	}

	cb->callbacks = callbacks;
	ao2_link(sorcery->observers, cb);
	ao2_ref(cb, -1);

	return 0;
}

void ast_sorcery_instance_observer_remove(struct ast_sorcery *sorcery,
	const struct ast_sorcery_instance_observer *callbacks)
{
	ao2_callback(sorcery->observers, OBJ_NODATA | OBJ_UNLINK, sorcery_generic_observer_remove, (void *)callbacks);
}

int ast_sorcery_wizard_observer_add(struct ast_sorcery_wizard *interface,
	const struct ast_sorcery_wizard_observer *callbacks)
{
	RAII_VAR(struct ast_sorcery_internal_wizard *, wizard,
		interface ? ao2_find(wizards, interface->name, OBJ_SEARCH_KEY) : NULL,
			ao2_cleanup);

	if (wizard) {
		struct sorcery_wizard_observer *cb;

		cb = ao2_alloc(sizeof(*cb), NULL);
		if (!cb) {
			return -1;
		}

		cb->callbacks = callbacks;
		ao2_link(wizard->observers, cb);
		ao2_ref(cb, -1);

		return 0;
	}

	return -1;
}

void ast_sorcery_wizard_observer_remove(struct ast_sorcery_wizard *interface,
	const struct ast_sorcery_wizard_observer *callbacks)
{
	RAII_VAR(struct ast_sorcery_internal_wizard *, wizard,
		interface ? ao2_find(wizards, interface->name, OBJ_SEARCH_KEY) : NULL,
			ao2_cleanup);

	if (wizard) {
		ao2_callback(wizard->observers, OBJ_NODATA | OBJ_UNLINK, sorcery_generic_observer_remove, (void *)callbacks);
	}
}

/*! \brief Destructor called when sorcery structure is destroyed */
static void sorcery_destructor(void *obj)
{
	struct ast_sorcery *sorcery = obj;

	if (sorcery->observers) {
		NOTIFY_GLOBAL_OBSERVERS(observers, instance_destroying, sorcery->module_name, sorcery);
	}
	ao2_cleanup(sorcery->observers);
	ao2_cleanup(sorcery->types);
}

/*! \brief Hashing function for sorcery types */
static int sorcery_type_hash(const void *obj, const int flags)
{
	const struct ast_sorcery_object_type *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->name;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief Comparator function for sorcery types */
static int sorcery_type_cmp(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_type *object_left = obj;
	const struct ast_sorcery_object_type *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(object_left->name, right_key, strlen(right_key));
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

struct ast_sorcery *__ast_sorcery_open(const char *module_name)
{
	struct ast_sorcery *sorcery;

	ast_assert(module_name != NULL);

	ao2_wrlock(instances);
	if ((sorcery = ao2_find(instances, module_name, OBJ_SEARCH_KEY | OBJ_NOLOCK))) {
		goto done;
	}

	if (!(sorcery = ao2_alloc(sizeof(*sorcery) + strlen(module_name) + 1, sorcery_destructor))) {
		goto done;
	}

	if (!(sorcery->types = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_RWLOCK, TYPE_BUCKETS, sorcery_type_hash, sorcery_type_cmp))) {
		ao2_ref(sorcery, -1);
		sorcery = NULL;
		goto done;
	}

	if (!(sorcery->observers = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_RWLOCK, 0, NULL, NULL))) {
		ao2_ref(sorcery, -1);
		sorcery = NULL;
		goto done;
	}

	strcpy(sorcery->module_name, module_name); /* Safe */

	if (__ast_sorcery_apply_config(sorcery, module_name, module_name) == AST_SORCERY_APPLY_FAIL) {
		ast_log(LOG_ERROR, "Error attempting to apply configuration %s to sorcery.\n", module_name);
		ao2_cleanup(sorcery);
		sorcery = NULL;
		goto done;
	}

	ao2_link_flags(instances, sorcery, OBJ_NOLOCK);

	NOTIFY_GLOBAL_OBSERVERS(observers, instance_created, module_name, sorcery);

done:
	ao2_unlock(instances);
	return sorcery;
}

/*! \brief Search function for sorcery instances */
struct ast_sorcery *ast_sorcery_retrieve_by_module_name(const char *module_name)
{
	return ao2_find(instances, module_name, OBJ_SEARCH_KEY);
}

/*! \brief Destructor function for object types */
static void sorcery_object_type_destructor(void *obj)
{
	struct ast_sorcery_object_type *object_type = obj;

	AST_VECTOR_RW_WRLOCK(&object_type->wizards);
	AST_VECTOR_CALLBACK_VOID(&object_type->wizards, ao2_cleanup);
	AST_VECTOR_RW_UNLOCK(&object_type->wizards);
	AST_VECTOR_RW_FREE(&object_type->wizards);
	ao2_cleanup(object_type->fields);
	ao2_cleanup(object_type->observers);

	if (object_type->info) {
		aco_info_destroy(object_type->info);
		ast_free(object_type->info);
	}

	ast_free(object_type->file);

	ast_taskprocessor_unreference(object_type->serializer);
}

/*! \brief Internal function which allocates an object type structure */
static struct ast_sorcery_object_type *sorcery_object_type_alloc(const char *type, const char *module)
{
	struct ast_sorcery_object_type *object_type;
	char uuid[AST_UUID_STR_LEN];

	if (!(object_type = ao2_alloc(sizeof(*object_type), sorcery_object_type_destructor))) {
		return NULL;
	}

	/* Order matters for object wizards */
	if (AST_VECTOR_RW_INIT(&object_type->wizards, 5) != 0) {
		ao2_ref(object_type, -1);
		return NULL;
	}

	if (!(object_type->fields = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, OBJECT_FIELD_BUCKETS,
					object_type_field_hash, object_type_field_cmp))) {
		ao2_ref(object_type, -1);
		return NULL;
	}

	if (!(object_type->observers = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_RWLOCK, 1, NULL, NULL))) {
		ao2_ref(object_type, -1);
		return NULL;
	}

	if (!(object_type->info = ast_calloc(1, sizeof(*object_type->info) + 2 * sizeof(object_type->info->files[0])))) {
		ao2_ref(object_type, -1);
		return NULL;
	}

	if (!(object_type->file = ast_calloc(1, sizeof(*object_type->file) + 2 * sizeof(object_type->file->types[0])))) {
		ao2_ref(object_type, -1);
		return NULL;
	}

	if (!ast_uuid_generate_str(uuid, sizeof(uuid))) {
		ao2_ref(object_type, -1);
		return NULL;
	}

	if (!(object_type->serializer = ast_threadpool_serializer(uuid, threadpool))) {
		ao2_ref(object_type, -1);
		return NULL;
	}

	object_type->info->files[0] = object_type->file;
	object_type->info->files[1] = NULL;
	object_type->info->module = module;

	ast_copy_string(object_type->name, type, sizeof(object_type->name));

	return object_type;
}

/*! \brief Object wizard destructor */
static void sorcery_object_wizard_destructor(void *obj)
{
	struct ast_sorcery_object_wizard *object_wizard = obj;

	if (object_wizard->data) {
		object_wizard->wizard->callbacks.close(object_wizard->data);
	}

	if (object_wizard->wizard) {
		ast_module_unref(object_wizard->wizard->callbacks.module);
	}

	ao2_cleanup(object_wizard->wizard);
}

/*! \brief Return a vector with the object_type's wizards */
struct ast_sorcery_object_wizards *ast_sorcery_get_wizard_mappings(struct ast_sorcery *sorcery, const char *type)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	struct ast_sorcery_object_wizards *result;

	AST_VECTOR_RW_RDLOCK(&object_type->wizards);
	result = AST_VECTOR_CALLBACK_MULTIPLE(&object_type->wizards, AST_VECTOR_MATCH_ALL);
	AST_VECTOR_RW_UNLOCK(&object_type->wizards);

	return result;
}

/*! \brief Internal function which creates an object type and inserts a wizard mapping */
enum ast_sorcery_apply_result __ast_sorcery_insert_wizard_mapping(struct ast_sorcery *sorcery,
		const char *type, const char *module, const char *name, const char *data,
		unsigned int caching, int position)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	RAII_VAR(struct ast_sorcery_internal_wizard *, wizard, ao2_find(wizards, name, OBJ_KEY), ao2_cleanup);
	RAII_VAR(struct ast_sorcery_object_wizard *, object_wizard, ao2_alloc(sizeof(*object_wizard), sorcery_object_wizard_destructor), ao2_cleanup);
	int created = 0;

	if (!wizard) {
		ast_log(LOG_ERROR, "Wizard '%s' could not be applied to object type '%s' as it was not found\n",
			name, type);
		return AST_SORCERY_APPLY_FAIL;
	} else if (!object_wizard) {
		return AST_SORCERY_APPLY_FAIL;
	}

	if (!object_type) {
		if (!(object_type = sorcery_object_type_alloc(type, module))) {
			return AST_SORCERY_APPLY_FAIL;
		}
		created = 1;
	}

	AST_VECTOR_RW_WRLOCK(&object_type->wizards);
	if (!created) {
		struct ast_sorcery_object_wizard *found;

#define WIZARD_COMPARE(a, b) ((a)->wizard == (b))
		found = AST_VECTOR_GET_CMP(&object_type->wizards, wizard, WIZARD_COMPARE);
#undef WIZARD_COMPARE
		if (found) {
			ast_debug(1, "Wizard %s already applied to object type %s\n",
					wizard->callbacks.name, object_type->name);
			AST_VECTOR_RW_UNLOCK(&object_type->wizards);
			return AST_SORCERY_APPLY_DUPLICATE;
		}
	}

	if (wizard->callbacks.open && !(object_wizard->data = wizard->callbacks.open(data))) {
		AST_VECTOR_RW_UNLOCK(&object_type->wizards);
		return AST_SORCERY_APPLY_FAIL;
	}

	ast_module_ref(wizard->callbacks.module);

	object_wizard->wizard = ao2_bump(wizard);
	object_wizard->caching = caching;

	if (position == ast_sorcery_wizard_position_last) {
		position = AST_VECTOR_SIZE(&object_type->wizards);
	}

	if (AST_VECTOR_INSERT_AT(&object_type->wizards, position, object_wizard) != 0) {
		AST_VECTOR_RW_UNLOCK(&object_type->wizards);
		return AST_SORCERY_APPLY_FAIL;
	}
	AST_VECTOR_RW_UNLOCK(&object_type->wizards);
	ao2_bump(object_wizard);

	if (created) {
		ao2_link(sorcery->types, object_type);
	}

	NOTIFY_INSTANCE_OBSERVERS(sorcery->observers, wizard_mapped,
		sorcery->module_name, sorcery, type, &wizard->callbacks, data, object_wizard->data);

	return AST_SORCERY_APPLY_SUCCESS;
}

/*! \brief Internal function which creates an object type and adds a wizard mapping */
enum ast_sorcery_apply_result __ast_sorcery_apply_wizard_mapping(struct ast_sorcery *sorcery,
		const char *type, const char *module, const char *name, const char *data, unsigned int caching)
{
	return __ast_sorcery_insert_wizard_mapping(sorcery, type, module, name, data,
		caching, ast_sorcery_wizard_position_last);
}

enum ast_sorcery_apply_result  __ast_sorcery_apply_config(struct ast_sorcery *sorcery, const char *name, const char *module)
{
	struct ast_flags flags = { 0 };
	struct ast_config *config = ast_config_load2("sorcery.conf", "sorcery", flags);
	struct ast_variable *mapping;
	int res = AST_SORCERY_APPLY_SUCCESS;

	if (!config) {
		return AST_SORCERY_APPLY_NO_CONFIGURATION;
	}

	if (config == CONFIG_STATUS_FILEINVALID) {
		return AST_SORCERY_APPLY_FAIL;
	}

	for (mapping = ast_variable_browse(config, name); mapping; mapping = mapping->next) {
		RAII_VAR(char *, mapping_name, ast_strdup(mapping->name), ast_free);
		RAII_VAR(char *, mapping_value, ast_strdup(mapping->value), ast_free);
		char *options = mapping_name;
		char *type = strsep(&options, "/");
		char *data = mapping_value;
		char *wizard = strsep(&data, ",");
		unsigned int caching = 0;

		/* If no object type or wizard exists just skip, nothing we can do */
		if (ast_strlen_zero(type) || ast_strlen_zero(wizard)) {
			continue;
		}

		/* If the wizard is configured as a cache treat it as such */
		if (!ast_strlen_zero(options) && strstr(options, "cache")) {
			caching = 1;
		}

		/* Any error immediately causes us to stop */
		if (__ast_sorcery_apply_wizard_mapping(sorcery, type, module, wizard, data, caching) == AST_SORCERY_APPLY_FAIL) {
			res = AST_SORCERY_APPLY_FAIL;
			break;
		}
	}

	ast_config_destroy(config);

	return res;
}

enum ast_sorcery_apply_result __ast_sorcery_apply_default(struct ast_sorcery *sorcery, const char *type, const char *module, const char *name, const char *data)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);

	/* Defaults can not be added if any existing mapping exists */
	if (object_type) {
		return AST_SORCERY_APPLY_DEFAULT_UNNECESSARY;
	}

	return __ast_sorcery_apply_wizard_mapping(sorcery, type, module, name, data, 0);
}

static int sorcery_extended_config_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	return ast_sorcery_object_set_extended(obj, var->name, var->value);
}

static int sorcery_extended_fields_handler(const void *obj, struct ast_variable **fields)
{
	const struct ast_sorcery_object_details *details = obj;

	if (details->object->extended) {
		*fields = ast_variables_dup(details->object->extended);
	} else {
		*fields = NULL;
	}

	return 0;
}

int __ast_sorcery_object_register(struct ast_sorcery *sorcery, const char *type, unsigned int hidden, unsigned int reloadable, aco_type_item_alloc alloc, sorcery_transform_handler transform, sorcery_apply_handler apply)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);

	if (!object_type || object_type->type.item_alloc) {
		return -1;
	}

	object_type->type.name = object_type->name;
	object_type->type.type = ACO_ITEM;
	object_type->type.category = ".?";
	object_type->type.item_alloc = alloc;
	object_type->type.hidden = hidden;

	object_type->reloadable = reloadable;
	object_type->transform = transform;
	object_type->apply = apply;
	object_type->file->types[0] = &object_type->type;
	object_type->file->types[1] = NULL;

	if (aco_info_init(object_type->info)) {
		return -1;
	}

	if (ast_sorcery_object_fields_register(sorcery, type, "^@", sorcery_extended_config_handler, sorcery_extended_fields_handler)) {
		return -1;
	}

	NOTIFY_INSTANCE_OBSERVERS(sorcery->observers, object_type_registered,
		sorcery->module_name, sorcery, type);

	return 0;
}

void ast_sorcery_object_set_copy_handler(struct ast_sorcery *sorcery, const char *type, sorcery_copy_handler copy)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);

	if (!object_type) {
		return;
	}

	object_type->copy = copy;
}

void ast_sorcery_object_set_diff_handler(struct ast_sorcery *sorcery, const char *type, sorcery_diff_handler diff)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);

	if (!object_type) {
		return;
	}

	object_type->diff = diff;
}

static void sorcery_object_field_destructor(void *obj)
{
	struct ast_sorcery_object_field *object_field = obj;

	if (object_field->name_regex) {
		regfree(object_field->name_regex);
		ast_free(object_field->name_regex);
	}
}

int ast_sorcery_object_fields_register(struct ast_sorcery *sorcery, const char *type, const char *regex, aco_option_handler config_handler, sorcery_fields_handler sorcery_handler)
{
#define MAX_REGEX_ERROR_LEN 128
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	RAII_VAR(struct ast_sorcery_object_field *, object_field, NULL, ao2_cleanup);
	int rc;

	if (!object_type || !object_type->type.item_alloc || !config_handler
		|| !(object_field = ao2_alloc(sizeof(*object_field), sorcery_object_field_destructor))) {
		return -1;
	}

	ast_copy_string(object_field->name, regex, sizeof(object_field->name));
	object_field->multiple_handler = sorcery_handler;

	if (!(object_field->name_regex = ast_calloc(1, sizeof(regex_t)))) {
		return -1;
	}

	if ((rc = regcomp(object_field->name_regex, regex, REG_EXTENDED | REG_NOSUB))) {
		char *regerr = ast_alloca(MAX_REGEX_ERROR_LEN);
		regerror(rc, object_field->name_regex, regerr, MAX_REGEX_ERROR_LEN);
		ast_log(LOG_ERROR, "Regular expression '%s' failed to compile: %s\n", regex, regerr);
		return -1;
	}

	ao2_link(object_type->fields, object_field);
	__aco_option_register(object_type->info, regex, ACO_REGEX, object_type->file->types, "", OPT_CUSTOM_T, config_handler, 0, 1, 0);

	return 0;
}

int __ast_sorcery_object_field_register(struct ast_sorcery *sorcery, const char *type, const char *name, const char *default_val, enum aco_option_type opt_type,
					aco_option_handler config_handler, sorcery_field_handler sorcery_handler, sorcery_fields_handler multiple_handler, unsigned int flags, unsigned int no_doc, unsigned int alias, size_t argc, ...)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	RAII_VAR(struct ast_sorcery_object_field *, object_field, NULL, ao2_cleanup);
	int pos;
	va_list args;

	if (!strcmp(type, "id") || !object_type || !object_type->type.item_alloc) {
		return -1;
	}

	if (!sorcery_handler) {
		sorcery_handler = sorcery_field_default_handler(opt_type);
	}

	if (!(object_field = ao2_alloc(sizeof(*object_field) + argc * sizeof(object_field->args[0]), NULL))) {
		return -1;
	}

	ast_copy_string(object_field->name, name, sizeof(object_field->name));
	object_field->handler = sorcery_handler;
	object_field->multiple_handler = multiple_handler;

	va_start(args, argc);
	for (pos = 0; pos < argc; pos++) {
		object_field->args[pos] = va_arg(args, size_t);
	}
	va_end(args);

	if (!alias) {
		ao2_link(object_type->fields, object_field);
	}

	/* TODO: Improve this hack */
	if (!argc) {
		__aco_option_register(object_type->info, name, ACO_EXACT, object_type->file->types, default_val, opt_type, config_handler, flags, no_doc, argc);
	} else if (argc == 1) {
		__aco_option_register(object_type->info, name, ACO_EXACT, object_type->file->types, default_val, opt_type, config_handler, flags, no_doc, argc,
			object_field->args[0]);
	} else if (argc == 2) {
		__aco_option_register(object_type->info, name, ACO_EXACT, object_type->file->types, default_val, opt_type, config_handler, flags, no_doc, argc,
			object_field->args[0], object_field->args[1]);
	} else if (argc == 3) {
		__aco_option_register(object_type->info, name, ACO_EXACT, object_type->file->types, default_val, opt_type, config_handler, flags, no_doc, argc,
			object_field->args[0], object_field->args[1], object_field->args[2]);
	} else {
		ast_assert(0); /* The hack... she does us no good for this */
	}

	return 0;
}

/*! \brief Retrieves whether or not the type is reloadable */
static int sorcery_reloadable(const struct ast_sorcery *sorcery, const char *type)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type,
		 ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	return object_type && object_type->reloadable;
}

static int sorcery_wizard_load(void *obj, void *arg, int flags)
{
	struct ast_sorcery_object_wizard *wizard = obj;
	struct sorcery_load_details *details = arg;
	void (*load)(void *data, const struct ast_sorcery *sorcery, const char *type);

	load = !details->reload ? wizard->wizard->callbacks.load : wizard->wizard->callbacks.reload;

	if (load) {
		NOTIFY_WIZARD_OBSERVERS(wizard->wizard->observers, wizard_loading,
			wizard->wizard->callbacks.name, &wizard->wizard->callbacks, details->type, details->reload);

		load(wizard->data, details->sorcery, details->type);

		NOTIFY_WIZARD_OBSERVERS(wizard->wizard->observers, wizard_loaded,
			wizard->wizard->callbacks.name, &wizard->wizard->callbacks, details->type, details->reload);
	}

	return 0;
}

/*! \brief Destructor for observer invocation */
static void sorcery_observer_invocation_destroy(void *obj)
{
	struct sorcery_observer_invocation *invocation = obj;

	ao2_cleanup(invocation->object_type);
	ao2_cleanup(invocation->object);
}

/*! \brief Allocator function for observer invocation */
static struct sorcery_observer_invocation *sorcery_observer_invocation_alloc(struct ast_sorcery_object_type *object_type, void *object)
{
	struct sorcery_observer_invocation *invocation = ao2_alloc(sizeof(*invocation), sorcery_observer_invocation_destroy);

	if (!invocation) {
		return NULL;
	}

	ao2_ref(object_type, +1);
	invocation->object_type = object_type;

	if (object) {
		ao2_ref(object, +1);
		invocation->object = object;
	}

	return invocation;
}

/*! \brief Internal callback function which notifies an individual observer that an object type has been loaded */
static int sorcery_observer_notify_loaded(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_type_observer *observer = obj;

	if (observer->callbacks->loaded) {
		observer->callbacks->loaded(arg);
	}

	return 0;
}

/*! \brief Internal callback function which notifies observers that an object type has been loaded */
static int sorcery_observers_notify_loaded(void *data)
{
	struct sorcery_observer_invocation *invocation = data;

	ao2_callback(invocation->object_type->observers, OBJ_NODATA, sorcery_observer_notify_loaded, invocation->object_type->name);
	ao2_cleanup(invocation);

	return 0;
}

static int sorcery_object_load(void *obj, void *arg, int flags)
{
	struct ast_sorcery_object_type *type = obj;
	struct sorcery_load_details *details = arg;

	if (!type->type.item_alloc) {
		return 0;
	}

	details->type = type->name;

	if (details->reload && !sorcery_reloadable(details->sorcery, details->type)) {
		ast_log(LOG_NOTICE, "Type '%s' is not reloadable, maintaining previous values\n",
			details->type);
		return 0;
	}

	NOTIFY_INSTANCE_OBSERVERS(details->sorcery->observers, object_type_loading,
		details->sorcery->module_name, details->sorcery, type->name, details->reload);

	AST_VECTOR_RW_RDLOCK(&type->wizards);
	AST_VECTOR_CALLBACK(&type->wizards, sorcery_wizard_load, NULL, details, 0);
	AST_VECTOR_RW_UNLOCK(&type->wizards);

	NOTIFY_INSTANCE_OBSERVERS(details->sorcery->observers, object_type_loaded,
		details->sorcery->module_name, details->sorcery, type->name, details->reload);

	if (ao2_container_count(type->observers)) {
		struct sorcery_observer_invocation *invocation;

		invocation = sorcery_observer_invocation_alloc(type, NULL);
		if (invocation
			&& ast_taskprocessor_push(type->serializer, sorcery_observers_notify_loaded,
				invocation)) {
			ao2_cleanup(invocation);
		}
	}

	return 0;
}

void ast_sorcery_load(const struct ast_sorcery *sorcery)
{
	struct sorcery_load_details details = {
		.sorcery = sorcery,
		.reload = 0,
	};

	NOTIFY_INSTANCE_OBSERVERS(sorcery->observers, instance_loading,
		sorcery->module_name, sorcery, 0);

	ao2_callback(sorcery->types, OBJ_NODATA, sorcery_object_load, &details);

	NOTIFY_INSTANCE_OBSERVERS(sorcery->observers, instance_loaded,
		sorcery->module_name, sorcery, 0);
}

void ast_sorcery_load_object(const struct ast_sorcery *sorcery, const char *type)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	struct sorcery_load_details details = {
		.sorcery = sorcery,
		.reload = 0,
	};

	if (!object_type) {
		return;
	}

	sorcery_object_load(object_type, &details, 0);
}

void ast_sorcery_reload(const struct ast_sorcery *sorcery)
{
	struct sorcery_load_details details = {
		.sorcery = sorcery,
		.reload = 1,
	};

	NOTIFY_INSTANCE_OBSERVERS(sorcery->observers, instance_loading,
		sorcery->module_name, sorcery, 1);

	ao2_callback(sorcery->types, OBJ_NODATA, sorcery_object_load, &details);

	NOTIFY_INSTANCE_OBSERVERS(sorcery->observers, instance_loaded,
		sorcery->module_name, sorcery, 1);

}

void ast_sorcery_reload_object(const struct ast_sorcery *sorcery, const char *type)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	struct sorcery_load_details details = {
		.sorcery = sorcery,
		.reload = 1,
	};

	if (!object_type) {
		return;
	}

	sorcery_object_load(object_type, &details, 0);
}

void ast_sorcery_ref(struct ast_sorcery *sorcery)
{
	ao2_ref(sorcery, +1);
}

static struct ast_variable *get_single_field_as_var_list(const void *object, struct ast_sorcery_object_field *object_field)
{
	struct ast_variable *tmp = NULL;
	char *buf = NULL;

	if (!object_field->handler) {
		return NULL;
	}

	if (!(object_field->handler(object, object_field->args, &buf))) {
		tmp = ast_variable_new(object_field->name, S_OR(buf, ""), "");
	}
	ast_free(buf);

	return tmp;
}

static struct ast_variable *get_multiple_fields_as_var_list(const void *object, struct ast_sorcery_object_field *object_field)
{
	struct ast_variable *tmp = NULL;

	if (!object_field->multiple_handler) {
		return NULL;
	}

	if (object_field->multiple_handler(object, &tmp)) {
		ast_variables_destroy(tmp);
		tmp = NULL;
	}

	return tmp;
}

struct ast_variable *ast_sorcery_objectset_create2(const struct ast_sorcery *sorcery,
	const void *object,	enum ast_sorcery_field_handler_flags flags)
{
	const struct ast_sorcery_object_details *details = object;
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, details->object->type, OBJ_KEY), ao2_cleanup);
	struct ao2_iterator i;
	struct ast_sorcery_object_field *object_field;
	struct ast_variable *head = NULL;
	struct ast_variable *tail = NULL;

	if (!object_type) {
		return NULL;
	}

	i = ao2_iterator_init(object_type->fields, 0);

	for (; (object_field = ao2_iterator_next(&i)); ao2_ref(object_field, -1)) {
		struct ast_variable *tmp;

		switch (flags) {
		case AST_HANDLER_PREFER_LIST:
			if ((tmp = get_multiple_fields_as_var_list(object, object_field)) ||
				(tmp = get_single_field_as_var_list(object, object_field))) {
				break;
			}
			continue;
		case AST_HANDLER_PREFER_STRING:
			if ((tmp = get_single_field_as_var_list(object, object_field)) ||
				(tmp = get_multiple_fields_as_var_list(object, object_field))) {
				break;
			}
			continue;
		case AST_HANDLER_ONLY_LIST:
			if ((tmp = get_multiple_fields_as_var_list(object, object_field))) {
				break;
			}
			continue;
		case AST_HANDLER_ONLY_STRING:
			if ((tmp = get_single_field_as_var_list(object, object_field))) {
				break;
			}
			continue;
		default:
			continue;
		}

		tail = ast_variable_list_append_hint(&head, tail, tmp);
	}

	ao2_iterator_destroy(&i);

	return head;
}

struct ast_json *ast_sorcery_objectset_json_create(const struct ast_sorcery *sorcery, const void *object)
{
	const struct ast_sorcery_object_details *details = object;
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, details->object->type, OBJ_KEY), ao2_cleanup);
	struct ao2_iterator i;
	struct ast_sorcery_object_field *object_field;
	struct ast_json *json = ast_json_object_create();
	int res = 0;

	if (!object_type || !json) {
		return NULL;
	}

	i = ao2_iterator_init(object_type->fields, 0);

	for (; !res && (object_field = ao2_iterator_next(&i)); ao2_ref(object_field, -1)) {
		if (object_field->multiple_handler) {
			struct ast_variable *tmp = NULL;
			struct ast_variable *field;

			if ((res = object_field->multiple_handler(object, &tmp))) {
				ast_variables_destroy(tmp);
				ao2_ref(object_field, -1);
				break;
			}

			for (field = tmp; field; field = field->next) {
				struct ast_json *value = ast_json_string_create(field->value);

				if (!value || ast_json_object_set(json, field->name, value)) {
					res = -1;
					break;
				}
			}

			ast_variables_destroy(tmp);
		} else if (object_field->handler) {
			char *buf = NULL;
			struct ast_json *value = NULL;

			if ((res = object_field->handler(object, object_field->args, &buf))
				|| !(value = ast_json_string_create(buf))
				|| ast_json_object_set(json, object_field->name, value)) {
				res = -1;
			}

			ast_free(buf);
		} else {
			continue;
		}
	}

	ao2_iterator_destroy(&i);

	/* If any error occurs we destroy the JSON object so a partial objectset is not returned */
	if (res) {
		ast_json_unref(json);
		json = NULL;
	}

	return json;
}

int ast_sorcery_objectset_apply(const struct ast_sorcery *sorcery, void *object, struct ast_variable *objectset)
{
	const struct ast_sorcery_object_details *details = object;
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, details->object->type, OBJ_KEY), ao2_cleanup);
	RAII_VAR(struct ast_variable *, transformed, NULL, ast_variables_destroy);
	struct ast_variable *field;
	int res = 0;

	if (!object_type) {
		return -1;
	}

	if (object_type->transform && (transformed = object_type->transform(objectset))) {
		field = transformed;
	} else {
		field = objectset;
	}

	for (; field; field = field->next) {
		if ((res = aco_process_var(&object_type->type, details->object->id, field, object))) {
			break;
		}
	}

	if (!res && object_type->apply) {
		res = object_type->apply(sorcery, object);
	}

	return res;
}

int ast_sorcery_changeset_create(const struct ast_variable *original, const struct ast_variable *modified, struct ast_variable **changes)
{
	const struct ast_variable *field;
	int res = 0;

	*changes = NULL;

	/* Unless the ast_variable list changes when examined... it can't differ from itself */
	if (original == modified) {
		return 0;
	}

	for (field = modified; field; field = field->next) {
		const char *old_value = ast_variable_find_in_list(original, field->name);

		if (!old_value || strcmp(old_value, field->value)) {
			struct ast_variable *tmp;

			if (!(tmp = ast_variable_new(field->name, field->value, ""))) {
				res = -1;
				break;
			}

			tmp->next = *changes;
			*changes = tmp;
		}
	}

	/* If an error occurred do not return a partial changeset */
	if (res) {
		ast_variables_destroy(*changes);
		*changes = NULL;
	}

	return res;
}

static void sorcery_object_destructor(void *object)
{
	struct ast_sorcery_object_details *details = object;

	if (details->object->destructor) {
		details->object->destructor(object);
	}

	ast_variables_destroy(details->object->extended);
	ast_free(details->object->id);
}

void *ast_sorcery_generic_alloc(size_t size, ao2_destructor_fn destructor)
{
	void *object = ao2_alloc_options(size + sizeof(struct ast_sorcery_object), sorcery_object_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	struct ast_sorcery_object_details *details = object;

	if (!object) {
		return NULL;
	}

	details->object = object + size;
	details->object->destructor = destructor;

	return object;
}

void *ast_sorcery_alloc(const struct ast_sorcery *sorcery, const char *type, const char *id)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	struct ast_sorcery_object_details *details;

	if (!object_type || !object_type->type.item_alloc ||
		!(details = object_type->type.item_alloc(id))) {
		return NULL;
	}

	if (ast_strlen_zero(id)) {
		char uuid[AST_UUID_STR_LEN];

		ast_uuid_generate_str(uuid, sizeof(uuid));
		details->object->id = ast_strdup(uuid);
	} else {
		details->object->id = ast_strdup(id);
	}

	ast_copy_string(details->object->type, type, sizeof(details->object->type));

	if (aco_set_defaults(&object_type->type, id, details)) {
		ao2_ref(details, -1);
		return NULL;
	}

	return details;
}

void *ast_sorcery_copy(const struct ast_sorcery *sorcery, const void *object)
{
	const struct ast_sorcery_object_details *details = object;
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, details->object->type, OBJ_KEY), ao2_cleanup);
	struct ast_sorcery_object_details *copy = ast_sorcery_alloc(sorcery, details->object->type, details->object->id);
	RAII_VAR(struct ast_variable *, objectset, NULL, ast_variables_destroy);
	int res = 0;

	if (!copy) {
		return NULL;
	} else if (object_type->copy) {
		res = object_type->copy(object, copy);
	} else if ((objectset = ast_sorcery_objectset_create(sorcery, object))) {
		res = ast_sorcery_objectset_apply(sorcery, copy, objectset);
	} else {
		/* No native copy available and could not create an objectset, this copy has failed */
		res = -1;
	}

	if (res) {
		ao2_cleanup(copy);
		copy = NULL;
	}

	return copy;
}

int ast_sorcery_diff(const struct ast_sorcery *sorcery, const void *original, const void *modified, struct ast_variable **changes)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, ast_sorcery_object_get_type(original), OBJ_KEY), ao2_cleanup);

	*changes = NULL;

	if (strcmp(ast_sorcery_object_get_type(original), ast_sorcery_object_get_type(modified))) {
		return -1;
	}

	if (original == modified) {
		return 0;
	} else if (!object_type->diff) {
		RAII_VAR(struct ast_variable *, objectset1, NULL, ast_variables_destroy);
		RAII_VAR(struct ast_variable *, objectset2, NULL, ast_variables_destroy);

		objectset1 = ast_sorcery_objectset_create(sorcery, original);
		objectset2 = ast_sorcery_objectset_create(sorcery, modified);

		return ast_sorcery_changeset_create(objectset1, objectset2, changes);
	} else {
		return object_type->diff(original, modified, changes);
	}
}

/*! \brief Structure used when calling create, update, or delete */
struct sorcery_details {
	/*! \brief Pointer to the sorcery instance */
	const struct ast_sorcery *sorcery;
	/*! \brief Pointer to the object itself */
	void *obj;
};

/*! \brief Internal function used to create an object in caching wizards */
static int sorcery_cache_create(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_wizard *object_wizard = obj;
	const struct sorcery_details *details = arg;

	if (!object_wizard->caching || !object_wizard->wizard->callbacks.create) {
		return 0;
	}

	object_wizard->wizard->callbacks.create(details->sorcery, object_wizard->data, details->obj);

	return 0;
}

void *ast_sorcery_retrieve_by_id(const struct ast_sorcery *sorcery, const char *type, const char *id)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	void *object = NULL;
	int i;
	struct ast_sorcery_object_wizard *wizard;
	unsigned int cached = 0;

	if (!object_type || ast_strlen_zero(id)) {
		return NULL;
	}

	AST_VECTOR_RW_RDLOCK(&object_type->wizards);
	for (i = 0; i < AST_VECTOR_SIZE(&object_type->wizards); i++) {
		wizard = AST_VECTOR_GET(&object_type->wizards, i);

		if (wizard->wizard->callbacks.retrieve_id &&
			!(object = wizard->wizard->callbacks.retrieve_id(sorcery, wizard->data, object_type->name, id))) {
			continue;
		}

		cached = wizard->caching;
		break;
	}

	if (!cached && object) {
		AST_VECTOR_CALLBACK(&object_type->wizards, sorcery_cache_create, NULL, object, 0);
	}
	AST_VECTOR_RW_UNLOCK(&object_type->wizards);

	return object;
}

void *ast_sorcery_retrieve_by_fields(const struct ast_sorcery *sorcery, const char *type, unsigned int flags, struct ast_variable *fields)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	void *object = NULL;
	int i;
	struct ast_sorcery_object_wizard *wizard;
	unsigned int cached = 0;

	if (!object_type) {
		return NULL;
	}

	/* If returning multiple objects create a container to store them in */
	if ((flags & AST_RETRIEVE_FLAG_MULTIPLE)) {
		if (!(object = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 1, NULL, NULL))) {
			return NULL;
		}
	}

	AST_VECTOR_RW_RDLOCK(&object_type->wizards);
	for (i = 0; i < AST_VECTOR_SIZE(&object_type->wizards); i++) {
		wizard = AST_VECTOR_GET(&object_type->wizards, i);

		if ((flags & AST_RETRIEVE_FLAG_MULTIPLE)) {
			if (wizard->wizard->callbacks.retrieve_multiple) {
				wizard->wizard->callbacks.retrieve_multiple(sorcery, wizard->data, object_type->name, object, fields);
			}
		} else if (fields && wizard->wizard->callbacks.retrieve_fields) {
			if (wizard->wizard->callbacks.retrieve_fields) {
				object = wizard->wizard->callbacks.retrieve_fields(sorcery, wizard->data, object_type->name, fields);
			}
		}

		if ((flags & AST_RETRIEVE_FLAG_MULTIPLE) || !object) {
			continue;
		}

		cached = wizard->caching;

		break;
	}

	/* If we are returning a single object and it came from a non-cache source create it in any caches */
	if (!(flags & AST_RETRIEVE_FLAG_MULTIPLE) && !cached && object) {
		AST_VECTOR_CALLBACK(&object_type->wizards, sorcery_cache_create, NULL, object, 0);
	}
	AST_VECTOR_RW_UNLOCK(&object_type->wizards);

	return object;
}

struct ao2_container *ast_sorcery_retrieve_by_regex(const struct ast_sorcery *sorcery, const char *type, const char *regex)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	struct ao2_container *objects;
	int i;
	struct ast_sorcery_object_wizard *wizard;

	if (!object_type || !(objects = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 1, NULL, NULL))) {
		return NULL;
	}

	AST_VECTOR_RW_RDLOCK(&object_type->wizards);
	for (i = 0; i < AST_VECTOR_SIZE(&object_type->wizards); i++) {
		wizard = AST_VECTOR_GET(&object_type->wizards, i);
		if (!wizard->wizard->callbacks.retrieve_regex) {
			continue;
		}

		wizard->wizard->callbacks.retrieve_regex(sorcery, wizard->data, object_type->name, objects, regex);
	}
	AST_VECTOR_RW_UNLOCK(&object_type->wizards);

	return objects;
}

/*! \brief Internal function which returns if the wizard has created the object */
static int sorcery_wizard_create(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_wizard *object_wizard = obj;
	const struct sorcery_details *details = arg;

	if (!object_wizard->wizard->callbacks.create) {
		ast_assert(0);
		ast_log(LOG_ERROR, "Sorcery wizard '%s' doesn't contain a 'create' virtual function.\n",
			object_wizard->wizard->callbacks.name);
		return 0;
	}
	return (!object_wizard->caching && !object_wizard->wizard->callbacks.create(details->sorcery, object_wizard->data, details->obj)) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Internal callback function which notifies an individual observer that an object has been created */
static int sorcery_observer_notify_create(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_type_observer *observer = obj;

	if (observer->callbacks->created) {
		observer->callbacks->created(arg);
	}

	return 0;
}

/*! \brief Internal callback function which notifies observers that an object has been created */
static int sorcery_observers_notify_create(void *data)
{
	struct sorcery_observer_invocation *invocation = data;

	ao2_callback(invocation->object_type->observers, OBJ_NODATA, sorcery_observer_notify_create, invocation->object);
	ao2_cleanup(invocation);

	return 0;
}

int ast_sorcery_create(const struct ast_sorcery *sorcery, void *object)
{
	const struct ast_sorcery_object_details *details = object;
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, details->object->type, OBJ_KEY), ao2_cleanup);
	struct ast_sorcery_object_wizard *object_wizard = NULL;
	struct ast_sorcery_object_wizard *found_wizard;
	int i;
	struct sorcery_details sdetails = {
		.sorcery = sorcery,
		.obj = object,
	};

	if (!object_type) {
		return -1;
	}

	AST_VECTOR_RW_RDLOCK(&object_type->wizards);
	for (i = 0; i < AST_VECTOR_SIZE(&object_type->wizards); i++) {
		found_wizard = AST_VECTOR_GET(&object_type->wizards, i);
		if (sorcery_wizard_create(found_wizard, &sdetails, 0) == (CMP_MATCH | CMP_STOP)) {
			object_wizard = found_wizard;
			if(ao2_container_count(object_type->observers)) {
				struct sorcery_observer_invocation *invocation = sorcery_observer_invocation_alloc(object_type, object);

				if (invocation && ast_taskprocessor_push(object_type->serializer, sorcery_observers_notify_create, invocation)) {
					ao2_cleanup(invocation);
				}
			}
		}
	}
	AST_VECTOR_RW_UNLOCK(&object_type->wizards);

	return object_wizard ? 0 : -1;
}

/*! \brief Internal callback function which notifies an individual observer that an object has been updated */
static int sorcery_observer_notify_update(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_type_observer *observer = obj;

	if (observer->callbacks->updated) {
		observer->callbacks->updated(arg);
	}

	return 0;
}

/*! \brief Internal callback function which notifies observers that an object has been updated */
static int sorcery_observers_notify_update(void *data)
{
	struct sorcery_observer_invocation *invocation = data;

	ao2_callback(invocation->object_type->observers, OBJ_NODATA, sorcery_observer_notify_update, invocation->object);
	ao2_cleanup(invocation);

	return 0;
}

/*! \brief Internal function which returns if a wizard has updated the object */
static int sorcery_wizard_update(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_wizard *object_wizard = obj;
	const struct sorcery_details *details = arg;

	return (object_wizard->wizard->callbacks.update && !object_wizard->wizard->callbacks.update(details->sorcery, object_wizard->data, details->obj) &&
		!object_wizard->caching) ? CMP_MATCH | CMP_STOP : 0;
}

int ast_sorcery_update(const struct ast_sorcery *sorcery, void *object)
{
	const struct ast_sorcery_object_details *details = object;
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, details->object->type, OBJ_KEY), ao2_cleanup);
	struct ast_sorcery_object_wizard *object_wizard = NULL;
	struct ast_sorcery_object_wizard *found_wizard;
	int i;
	struct sorcery_details sdetails = {
		.sorcery = sorcery,
		.obj = object,
	};

	if (!object_type) {
		return -1;
	}

	AST_VECTOR_RW_RDLOCK(&object_type->wizards);
	for (i = 0; i < AST_VECTOR_SIZE(&object_type->wizards); i++) {
		found_wizard = AST_VECTOR_GET(&object_type->wizards, i);
		if (sorcery_wizard_update(found_wizard, &sdetails, 0) == (CMP_MATCH | CMP_STOP)) {
			object_wizard = found_wizard;
			if (ao2_container_count(object_type->observers)) {
				struct sorcery_observer_invocation *invocation = sorcery_observer_invocation_alloc(object_type, object);

				if (invocation && ast_taskprocessor_push(object_type->serializer, sorcery_observers_notify_update, invocation)) {
					ao2_cleanup(invocation);
				}
			}
		}
	}
	AST_VECTOR_RW_UNLOCK(&object_type->wizards);

	return object_wizard ? 0 : -1;
}

/*! \brief Internal callback function which notifies an individual observer that an object has been deleted */
static int sorcery_observer_notify_delete(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_type_observer *observer = obj;

	if (observer->callbacks->deleted) {
		observer->callbacks->deleted(arg);
	}

	return 0;
}

/*! \brief Internal callback function which notifies observers that an object has been deleted */
static int sorcery_observers_notify_delete(void *data)
{
	struct sorcery_observer_invocation *invocation = data;

	ao2_callback(invocation->object_type->observers, OBJ_NODATA, sorcery_observer_notify_delete, invocation->object);
	ao2_cleanup(invocation);

	return 0;
}

/*! \brief Internal function which returns if a wizard has deleted the object */
static int sorcery_wizard_delete(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_wizard *object_wizard = obj;
	const struct sorcery_details *details = arg;

	return (object_wizard->wizard->callbacks.delete && !object_wizard->wizard->callbacks.delete(details->sorcery, object_wizard->data, details->obj) &&
		!object_wizard->caching) ? CMP_MATCH | CMP_STOP : 0;
}

int ast_sorcery_delete(const struct ast_sorcery *sorcery, void *object)
{
	const struct ast_sorcery_object_details *details = object;
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, details->object->type, OBJ_KEY), ao2_cleanup);
	struct ast_sorcery_object_wizard *object_wizard = NULL;
	struct ast_sorcery_object_wizard *found_wizard;
	int i;
	struct sorcery_details sdetails = {
		.sorcery = sorcery,
		.obj = object,
	};

	if (!object_type) {
		return -1;
	}

	AST_VECTOR_RW_RDLOCK(&object_type->wizards);
	for (i = 0; i < AST_VECTOR_SIZE(&object_type->wizards); i++) {
		found_wizard = AST_VECTOR_GET(&object_type->wizards, i);
		if (sorcery_wizard_delete(found_wizard, &sdetails, 0) == (CMP_MATCH | CMP_STOP)) {
			object_wizard = found_wizard;
			if (ao2_container_count(object_type->observers)) {
				struct sorcery_observer_invocation *invocation = sorcery_observer_invocation_alloc(object_type, object);

				if (invocation && ast_taskprocessor_push(object_type->serializer, sorcery_observers_notify_delete, invocation)) {
					ao2_cleanup(invocation);
				}
			}
		}
	}
	AST_VECTOR_RW_UNLOCK(&object_type->wizards);

	return object_wizard ? 0 : -1;
}

void ast_sorcery_unref(struct ast_sorcery *sorcery)
{
	if (sorcery) {
		/* One ref for what we just released, the other for the instances container. */
		ao2_wrlock(instances);
		if (ao2_ref(sorcery, -1) == 2) {
			ao2_unlink_flags(instances, sorcery, OBJ_NOLOCK);
		}
		ao2_unlock(instances);
	}
}

const char *ast_sorcery_object_get_id(const void *object)
{
	const struct ast_sorcery_object_details *details = object;
	return details->object->id;
}

const char *ast_sorcery_object_get_type(const void *object)
{
	const struct ast_sorcery_object_details *details = object;
	return details->object->type;
}

const char *ast_sorcery_object_get_extended(const void *object, const char *name)
{
	const struct ast_sorcery_object_details *details = object;
	struct ast_variable *field;

	for (field = details->object->extended; field; field = field->next) {
		if (!strcmp(field->name + 1, name)) {
			return field->value;
		}
	}

	return NULL;
}

int ast_sorcery_object_set_extended(const void *object, const char *name, const char *value)
{
	RAII_VAR(struct ast_variable *, field, NULL, ast_variables_destroy);
	struct ast_variable *extended = ast_variable_new(name, value, ""), *previous = NULL;
	const struct ast_sorcery_object_details *details = object;

	if (!extended) {
		return -1;
	}

	for (field = details->object->extended; field; previous = field, field = field->next) {
		if (!strcmp(field->name, name)) {
			if (previous) {
				previous->next = field->next;
			} else {
				details->object->extended = field->next;
			}
			field->next = NULL;
			break;
		}
	}

	extended->next = details->object->extended;
	details->object->extended = extended;

	return 0;
}

int ast_sorcery_observer_add(const struct ast_sorcery *sorcery, const char *type, const struct ast_sorcery_observer *callbacks)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, ao2_find(sorcery->types, type, OBJ_KEY), ao2_cleanup);
	struct ast_sorcery_object_type_observer *observer;
	int res;

	if (!object_type || !callbacks) {
		return -1;
	}

	if (!(observer = ao2_alloc(sizeof(*observer), NULL))) {
		return -1;
	}

	observer->callbacks = callbacks;
	res = 0;
	if (!ao2_link(object_type->observers, observer)) {
		res = -1;
	}
	ao2_ref(observer, -1);

	return res;
}

/*! \brief Internal callback function for removing an observer */
static int sorcery_observer_remove(void *obj, void *arg, int flags)
{
	const struct ast_sorcery_object_type_observer *observer = obj;

	return (observer->callbacks == arg) ? CMP_MATCH | CMP_STOP : 0;
}

void ast_sorcery_observer_remove(const struct ast_sorcery *sorcery, const char *type, const struct ast_sorcery_observer *callbacks)
{
	RAII_VAR(struct ast_sorcery_object_type *, object_type, NULL, ao2_cleanup);
	struct ast_sorcery_observer *cbs = (struct ast_sorcery_observer *) callbacks;/* Remove const for traversal. */

	if (!sorcery) {
		return;
	}
	object_type = ao2_find(sorcery->types, type, OBJ_KEY);
	if (!object_type) {
		return;
	}

	ao2_callback(object_type->observers, OBJ_NODATA | OBJ_UNLINK,
		sorcery_observer_remove, cbs);
}

int ast_sorcery_object_id_sort(const void *obj, const void *arg, int flags)
{
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = ast_sorcery_object_get_id(arg);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(ast_sorcery_object_get_id(obj), right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(ast_sorcery_object_get_id(obj), right_key, strlen(right_key));
		break;
	default:
		cmp = 0;
		break;
	}
	return cmp;
}

int ast_sorcery_object_id_compare(void *obj, void *arg, int flags)
{
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = ast_sorcery_object_get_id(arg);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		if (strcmp(ast_sorcery_object_get_id(obj), right_key) == 0) {
			cmp = CMP_MATCH | CMP_STOP;
		}
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		if (strncmp(ast_sorcery_object_get_id(obj), right_key, strlen(right_key)) == 0) {
			cmp = CMP_MATCH;
		}
		break;
	default:
		cmp = 0;
		break;
	}
	return cmp;
}

int ast_sorcery_object_id_hash(const void *obj, int flags) {
	if (flags & OBJ_SEARCH_OBJECT) {
		return ast_str_hash(ast_sorcery_object_get_id(obj));
	} else if (flags & OBJ_SEARCH_KEY) {
		return ast_str_hash(obj);
	}
	return -1;
}

struct ast_sorcery_object_type *ast_sorcery_get_object_type(const struct ast_sorcery *sorcery,
		const char *type)
{
	return ao2_find(sorcery->types, type, OBJ_SEARCH_KEY);
}

static int is_registered_cb(void *obj, void *arg, int flags)
{
	struct ast_sorcery_object_field *object_field = obj;
	char *name = arg;
	int rc = 0;

	if (object_field->name_regex
		&& !regexec(object_field->name_regex, name, 0, NULL, 0)) {
		rc = CMP_MATCH | CMP_STOP;
	}

	return rc;
}

int ast_sorcery_is_object_field_registered(const struct ast_sorcery_object_type *object_type,
		const char *field_name)
{
	struct ast_sorcery_object_field *object_field;
	int res = 1;

	ast_assert(object_type != NULL);

	object_field = ao2_find(object_type->fields, field_name, OBJ_SEARCH_KEY);

	if (!object_field) {
		object_field = ao2_callback(object_type->fields, 0, is_registered_cb, (char *)field_name);
	}

	if (!object_field) {
		res = 0;
	}

	ao2_cleanup(object_field);
	return res;
}
