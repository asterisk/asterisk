/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Eliel C. Sardanons (LU1ALY) <eliels@gmail.com>
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
 * \brief Data retrieval API.
 *
 * \author Brett Bryant <brettbryant@gmail.com>
 * \author Eliel C. Sardanons (LU1ALY) <eliels@gmail.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

#include <regex.h>

#include "asterisk/module.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/data.h"
#include "asterisk/astobj2.h"
#include "asterisk/xml.h"
#include "asterisk/cli.h"
#include "asterisk/term.h"
#include "asterisk/manager.h"
#include "asterisk/test.h"
#include "asterisk/frame.h"

/*** DOCUMENTATION
	<manager name="DataGet" language="en_US">
		<synopsis>
			Retrieve the data api tree.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Path" required="true" />
			<parameter name="Search" />
			<parameter name="Filter" />
		</syntax>
		<description>
			<para>Retrieve the data api tree.</para>
		</description>
	</manager>
 ***/

#define NUM_DATA_NODE_BUCKETS	59
#define NUM_DATA_RESULT_BUCKETS 59
#define NUM_DATA_SEARCH_BUCKETS 59
#define NUM_DATA_FILTER_BUCKETS 59

/*! \brief The last compatible version. */
static const uint32_t latest_handler_compatible_version = 0;

/*! \brief The last compatible version. */
static const uint32_t latest_query_compatible_version = 0;

/*! \brief Current handler structure version. */
static const uint32_t current_handler_version = AST_DATA_HANDLER_VERSION;

/*! \brief Current query structure version. */
static const uint32_t current_query_version = AST_DATA_QUERY_VERSION;

/*! \brief The data tree to be returned by the callbacks and
	   managed by functions local to this file. */
struct ast_data {
	enum ast_data_type type;

	/*! \brief The node content. */
	union {
		int32_t sint;
		uint32_t uint;
		double dbl;
		unsigned int boolean;
		char *str;
		char character;
		struct in_addr ipaddr;
		void *ptr;
	} payload;

	/*! \brief The filter node that depends on the current node,
	 * this is used only when creating the result tree. */
	const struct data_filter *filter;

	/*! \brief The list of nodes inside this node. */
	struct ao2_container *children;
	/*! \brief The name of the node. */
	char name[0];
};

/*! \brief Type of comparisons allow in the search string. */
enum data_search_comparison {
	DATA_CMP_UNKNOWN,
	DATA_CMP_EQ,	/* =  */
	DATA_CMP_NEQ,	/* != */
	DATA_CMP_GT,	/* >  */
	DATA_CMP_GE,	/* >= */
	DATA_CMP_LT,	/* <  */
	DATA_CMP_LE	/* <= */
};

/*! \brief The list of nodes with their search requirement. */
struct ast_data_search {
	/*! \brief The value of the comparison. */
	char *value;
	/*! \brief The type of comparison. */
	enum data_search_comparison cmp_type;
	/*! \brief reference another node. */
	struct ao2_container *children;
	/*! \brief The name of the node we are trying to compare. */
	char name[0];
};

struct data_filter;

/*! \brief The filter node. */
struct data_filter {
	/*! \brief node childrens. */
	struct ao2_container *children;
	/*! \brief glob list */
	AST_LIST_HEAD_NOLOCK(glob_list_t, data_filter) glob_list;
	/*! \brief glob list entry */
	AST_LIST_ENTRY(data_filter) list;
	/*! \brief node name. */
	char name[0];
};

/*! \brief A data container node pointing to the registered handler. */
struct data_provider {
	/*! \brief node content handler. */
	const struct ast_data_handler *handler;
	/*! \brief Module providing this handler. */
	struct ast_module *module;
	/*! \brief children nodes. */
	struct ao2_container *children;
	/*! \brief Who registered this node. */
	const char *registrar;
	/*! \brief Node name. */
	char name[0];
};

/*! \brief This structure is used by the iterator. */
struct ast_data_iterator {
	/*! \brief The internal iterator. */
	struct ao2_iterator internal_iterator;
	/*! \brief The last returned node. */
	struct ast_data *last;
	/*! \brief The iterator pattern. */
	const char *pattern;
	/*! \brief The compiled patter. */
	regex_t regex_pattern;
	/*! \brief is a regular expression. */
	unsigned int is_pattern:1;
};

struct {
	/*! \brief The asterisk data main content structure. */
	struct ao2_container *container;
	/*! \brief asterisk data locking mechanism. */
	ast_rwlock_t lock;
} root_data;

static void __data_result_print_cli(int fd, const struct ast_data *root, uint32_t depth);

/*!
 * \internal
 * \brief Common string hash function.
 * \see ast_data_init
 */
static int data_provider_hash(const void *obj, const int flags)
{
	const struct data_provider *node = obj;
	return ast_str_case_hash(node->name);
}

/*!
 * \internal
 * \brief Compare two data_provider's.
 * \see ast_data_init
 */
static int data_provider_cmp(void *obj1, void *obj2, int flags)
{
	struct data_provider *node1 = obj1, *node2 = obj2;
	return strcasecmp(node1->name, node2->name) ? 0 : CMP_MATCH;
}

/*!
 * \internal
 * \brief Common string hash function for data nodes
 */
static int data_result_hash(const void *obj, const int flags)
{
	const struct ast_data *node = obj;
	return ast_str_hash(node->name);
}

/*!
 * \internal
 * \brief Common string comparison function
 */
static int data_result_cmp(void *obj, void *arg, int flags)
{
	struct ast_data *node1 = obj, *node2 = arg;
	return strcasecmp(node1->name, node2->name) ? 0 : CMP_MATCH;
}

/*!
 * \internal
 * \brief Lock the data registered handlers structure for writing.
 * \see data_unlock
 */
#define data_write_lock() ast_rwlock_wrlock(&root_data.lock)

/*!
 * \internal
 * \brief Lock the data registered handlers structure for reading.
 * \see data_unlock
 */
#define data_read_lock() ast_rwlock_rdlock(&root_data.lock)

/*!
 * \internal
 * \brief Unlock the data registered handlers structure.
 */
#define data_unlock() ast_rwlock_unlock(&root_data.lock)

/*!
 * \internal
 * \brief Check if a version is compatible with the current core.
 * \param[in] structure_version The current structure version.
 * \param[in] latest_compatible The latest compatible version.
 * \param[in] current The current Data API version.
 * \retval 1 If the module is compatible.
 * \retval 0 If the module is NOT compatible.
 */
static int data_structure_compatible(int structure_version, uint32_t latest_compatible,
	uint32_t current)
{
	if (structure_version >= latest_compatible && structure_version <= current) {
		return 1;
	}

	ast_log(LOG_ERROR, "A module is not compatible with the"
		"current data api version\n");

	return 0;
}

/*!
 * \internal
 * \brief Get the next node name in a path (/node1/node2)
 *        Avoid null nodes like //node1//node2/node3.
 * \param[in] path The path where we are going to search for the next node name.
 * \retval The next node name we found inside the given path.
 * \retval NULL if there are no more node names.
 */
static char *next_node_name(char **path)
{
	char *res;

	do {
		res = strsep(path, "/");
	} while (res && ast_strlen_zero(res));

	return res;
}

/*!
 * \internal
 * \brief Release the memory allocated by a call to ao2_alloc.
 */
static void data_provider_destructor(void *obj)
{
	struct data_provider *provider = obj;

	ao2_ref(provider->children, -1);
}

/*!
 * \internal
 * \brief Create a new data node.
 * \param[in] name The name of the node we are going to create.
 * \param[in] handler The handler registered for this node.
 * \param[in] registrar The name of the registrar.
 * \retval NULL on error.
 * \retval The allocated data node structure.
 */
static struct data_provider *data_provider_new(const char *name,
	const struct ast_data_handler *handler, const char *registrar)
{
	struct data_provider *node;
	size_t namelen;

	namelen = strlen(name) + 1;

	node = ao2_alloc(sizeof(*node) + namelen, data_provider_destructor);
	if (!node) {
		return NULL;
	}

	node->handler = handler;
	node->registrar = registrar;
	strcpy(node->name, name);

	/* initialize the childrens container. */
	if (!(node->children = ao2_container_alloc(NUM_DATA_NODE_BUCKETS,
			data_provider_hash, data_provider_cmp))) {
		ao2_ref(node, -1);
		return NULL;
	}

	return node;
}

/*!
 * \internal
 * \brief Add a child node named 'name' to the 'parent' node.
 * \param[in] parent Where to add the child node.
 * \param[in] name The name of the child node.
 * \param[in] handler The handler structure.
 * \param[in] registrar Who registered this node.
 * \retval NULL on error.
 * \retval A newly allocated child in parent.
 */
static struct data_provider *data_provider_add_child(struct ao2_container *parent,
	const char *name, const struct ast_data_handler *handler, const char *registrar)
{
	struct data_provider *child;

	child = data_provider_new(name, handler, registrar);
	if (!child) {
		return NULL;
	}

	ao2_link(parent, child);

	return child;
}

/*!
 * \internal
 * \brief Find a child node, based on his name.
 * \param[in] parent Where to find the node.
 * \param[in] name The node name to find.
 * \param[in] registrar Also check if the node was being used by this registrar.
 * \retval NULL if a node wasn't found.
 * \retval The node found.
 * \note Remember to decrement the ref count of the returned node after using it.
 */
static struct data_provider *data_provider_find(struct ao2_container *parent,
	const char *name, const char *registrar)
{
	struct data_provider *find_node, *found;

	/* XXX avoid allocating a new data node for searching... */
	find_node = data_provider_new(name, NULL, NULL);
	if (!find_node) {
		return NULL;
	}

	found = ao2_find(parent, find_node, OBJ_POINTER);

	/* free the created node used for searching. */
	ao2_ref(find_node, -1);

	if (found && found->registrar && registrar) {
		if (strcmp(found->registrar, registrar)) {
			/* if the name doesn't match, do not return this node. */
			ast_debug(1, "Registrar doesn't match, node was registered"
				" by '%s' and we are searching for '%s'\n",
				found->registrar, registrar);
			ao2_ref(found, -1);
			return NULL;
		}
	}

	return found;
}

/*!
 * \internal
 * \brief Release a group of nodes.
 * \param[in] parent The parent node.
 * \param[in] path The path of nodes to release.
 * \param[in] registrar Who registered this node.
 * \retval <0 on error.
 * \retval 0 on success.
 * \see data_provider_create
 */
static int data_provider_release(struct ao2_container *parent, const char *path,
	const char *registrar)
{
	char *node_name, *rpath;
	struct data_provider *child;
	int ret = 0;

	rpath = ast_strdupa(path);

	node_name = next_node_name(&rpath);
	if (!node_name) {
		return -1;
	}

	child = data_provider_find(parent, node_name, registrar);
	if (!child) {
		return -1;
	}

	/* if this is not a terminal node. */
	if (!child->handler && rpath) {
		ret = data_provider_release(child->children, rpath, registrar);
	}

	/* if this node is empty, unlink it. */
	if (!ret && !ao2_container_count(child->children)) {
		ao2_unlink(parent, child);
	}

	ao2_ref(child, -1);

	return ret;
}

/*!
 * \internal
 * \brief Release every node registered by 'registrar'.
 * \param[in] parent The parent node.
 * \param[in] registrar
 * \see __ast_data_unregister
 */
static void data_provider_release_all(struct ao2_container *parent,
	const char *registrar)
{
	struct ao2_iterator i;
	struct data_provider *node;

	i = ao2_iterator_init(parent, 0);
	while ((node = ao2_iterator_next(&i))) {
		if (!node->handler) {
			/* this is a non-terminal node, go inside it. */
			data_provider_release_all(node->children, registrar);
			if (!ao2_container_count(node->children)) {
				/* if this node was left empty, unlink it. */
				ao2_unlink(parent, node);
			}
		} else {
			if (!strcmp(node->registrar, registrar)) {
				/* if the registrars match, release it! */
				ao2_unlink(parent, node);
			}
		}
		ao2_ref(node, -1);
	}
	ao2_iterator_destroy(&i);

}

/*!
 * \internal
 * \brief Create the middle nodes for the specified path (asterisk/testnode1/childnode)
 * \param[in] parent Where to add the middle nodes structure.
 * \param[in] path The path of nodes to add.
 * \param[in] registrar Who is trying to create this node provider.
 * \retval NULL on error.
 * \retval The created node.
 * \see data_provider_release
 */
static struct data_provider *data_provider_create(struct ao2_container *parent,
	const char *path, const char *registrar)
{
	char *rpath, *node_name;
	struct data_provider *child, *ret = NULL;

	rpath = ast_strdupa(path);

	node_name = next_node_name(&rpath);
	if (!node_name) {
		/* no more nodes to create. */
		return NULL;
	}

	child = data_provider_find(parent, node_name, NULL);

	if (!child) {
		/* nodes without handler are non-terminal nodes. */
		child = data_provider_add_child(parent, node_name, NULL, registrar);
	}

	if (rpath) {
		ret = data_provider_create(child->children, rpath, registrar);
		if (ret) {
			ao2_ref(child, -1);
		}
	}

	return ret ? ret : child;
}

int __ast_data_register(const char *path, const struct ast_data_handler *handler,
	const char *registrar, struct ast_module *mod)
{
	struct data_provider *node;

	if (!path) {
		return -1;
	}

	/* check if the handler structure is compatible. */
	if (!data_structure_compatible(handler->version,
		latest_handler_compatible_version,
		current_handler_version)) {
		return -1;
	}

	/* create the node structure for the registered handler. */
	data_write_lock();

	node = data_provider_create(root_data.container, path, registrar);
	if (!node) {
		ast_log(LOG_ERROR, "Unable to create the specified path (%s) "
			"for '%s'.\n", path, registrar);
		data_unlock();
		return -1;
	}

	if (ao2_container_count(node->children) || node->handler) {
		ast_log(LOG_ERROR, "The node '%s' was already registered. "
			"We were unable to register '%s' for registrar '%s'.\n",
			node->name, path, registrar);
		ao2_ref(node, -1);
		data_unlock();
		return -1;
	}

	/* add handler to that node. */
	node->handler = handler;
	node->module = mod;

	ao2_ref(node, -1);

	data_unlock();

	return 0;
}

int __ast_data_register_multiple(const struct ast_data_entry *data_entries,
	size_t entries, const char *registrar, struct ast_module *mod)
{
	int i, res;

	for (i = 0; i < entries; i++) {
		res = __ast_data_register(data_entries[i].path, data_entries[i].handler,
				registrar, mod);
		if (res) {
			/* unregister all the already registered nodes, and make
			 * this an atomic action. */
			while ((--i) >= 0) {
				__ast_data_unregister(data_entries[i].path, registrar);
			}
			return -1;
		}
	}

	return 0;
}

int __ast_data_unregister(const char *path, const char *registrar)
{
	int ret = 0;

	data_write_lock();
	if (path) {
		ret = data_provider_release(root_data.container, path, registrar);
	} else {
		data_provider_release_all(root_data.container, registrar);
	}
	data_unlock();

	if (path && ret) {
		ast_log(LOG_ERROR, "Unable to unregister '%s' for '%s'\n",
			path, registrar);
	}

	return ret;
}

/*!
 * \internal
 * \brief Is a char used to specify a comparison?
 * \param[in] a Character to evaluate.
 * \retval 1 It is a char used to specify a comparison.
 * \retval 0 It is NOT a char used to specify a comparison.
 */
static int data_search_comparison_char(char a)
{
	switch (a) {
	case '!':
	case '=':
	case '<':
	case '>':
		return 1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Get the type of comparison.
 */
static enum data_search_comparison data_search_comparison_type(const char *comparison)
{
	if (!strcmp(comparison, "=")) {
		return DATA_CMP_EQ;
	} else if (!strcmp(comparison, "!=")) {
		return DATA_CMP_NEQ;
	} else if (!strcmp(comparison, "<")) {
		return DATA_CMP_LT;
	} else if (!strcmp(comparison, ">")) {
		return DATA_CMP_GT;
	} else if (!strcmp(comparison, "<=")) {
		return DATA_CMP_LE;
	} else if (!strcmp(comparison, ">=")) {
		return DATA_CMP_GE;
	}

	return DATA_CMP_UNKNOWN;
}

/*!
 * \internal
 * \brief Common string hash function for data nodes
 */
static int data_search_hash(const void *obj, const int flags)
{
	const struct ast_data_search *node = obj;
	return ast_str_hash(node->name);
}

/*!
 * \internal
 * \brief Common string comparison function
 */
static int data_search_cmp(void *obj, void *arg, int flags)
{
	struct ast_data_search *node1 = obj, *node2 = arg;
	return strcasecmp(node1->name, node2->name) ? 0 : CMP_MATCH;
}

/*!
 * \internal
 * \brief Destroy the ao2 search node.
 */
static void data_search_destructor(void *obj)
{
	struct ast_data_search *node = obj;

	if (node->value) {
		ast_free(node->value);
	}

	ao2_ref(node->children, -1);
}

/*!
 * \internal
 * \brief Allocate a search node.
 * \retval NULL on error.
 * \retval non-NULL The allocated search node structure.
 */
static struct ast_data_search *data_search_alloc(const char *name)
{
	struct ast_data_search *res;
	size_t name_len = strlen(name) + 1;

	res = ao2_alloc(sizeof(*res) + name_len, data_search_destructor);
	if (!res) {
		return NULL;
	}

	res->children = ao2_container_alloc(NUM_DATA_SEARCH_BUCKETS, data_search_hash,
		data_search_cmp);

	if (!res->children) {
		ao2_ref(res, -1);
		return NULL;
	}

	strcpy(res->name, name);

	return res;
}

/*!
 * \internal
 * \brief Find a child node, based on his name.
 * \param[in] parent Where to find the node.
 * \param[in] name The node name to find.
 * \retval NULL if a node wasn't found.
 * \retval The node found.
 * \note Remember to decrement the ref count of the returned node after using it.
 */
static struct ast_data_search *data_search_find(struct ao2_container *parent,
	const char *name)
{
	struct ast_data_search *find_node, *found;

	find_node = data_search_alloc(name);
	if (!find_node) {
		return NULL;
	}

	found = ao2_find(parent, find_node, OBJ_POINTER);

	/* free the created node used for searching. */
	ao2_ref(find_node, -1);

	return found;
}

/*!
 * \internal
 * \brief Add a child node named 'name' to the 'parent' node.
 * \param[in] parent Where to add the child node.
 * \param[in] name The name of the child node.
 * \retval NULL on error.
 * \retval A newly allocated child in parent.
 */
static struct ast_data_search *data_search_add_child(struct ao2_container *parent,
	const char *name)
{
	struct ast_data_search *child;

	child = data_search_alloc(name);
	if (!child) {
		return NULL;
	}

	ao2_link(parent, child);

	return child;
}

/*!
 * \internal
 * \brief Create the middle nodes for the specified path (asterisk/testnode1/childnode)
 * \param[in] parent Where to add the middle nodes structure.
 * \param[in] path The path of nodes to add.
 * \retval NULL on error.
 * \retval The created node.
 */
static struct ast_data_search *data_search_create(struct ao2_container *parent,
	const char *path)
{
	char *rpath, *node_name;
	struct ast_data_search *child = NULL;
	struct ao2_container *current = parent;

	rpath = ast_strdupa(path);

	node_name = next_node_name(&rpath);
	while (node_name) {
		child = data_search_find(current, node_name);
		if (!child) {
			child = data_search_add_child(current, node_name);
		}
		ao2_ref(child, -1);
		current = child->children;
		node_name = next_node_name(&rpath);
	}

	return child;
}

/*!
 * \internal
 * \brief Allocate a tree with the search string parsed.
 * \param[in] search_string The search string.
 * \retval NULL on error.
 * \retval non-NULL A dynamically allocated search tree.
 */
static struct ast_data_search *data_search_generate(const char *search_string)
{
	struct ast_str *name, *value, *comparison;
	char *elements, *search_string_dup, *saveptr;
	int i;
	struct ast_data_search *root, *child;
	enum data_search_comparison cmp_type;
	size_t search_string_len;

	if (!search_string) {
		ast_log(LOG_ERROR, "You must pass a valid search string.\n");
		return NULL;
	}

	search_string_len = strlen(search_string);

	name = ast_str_create(search_string_len);
	if (!name) {
		return NULL;
	}
	value = ast_str_create(search_string_len);
	if (!value) {
		ast_free(name);
		return NULL;
	}
	comparison = ast_str_create(search_string_len);
	if (!comparison) {
		ast_free(name);
		ast_free(value);
		return NULL;
	}

	search_string_dup = ast_strdupa(search_string);

	/* Create the root node (just used as a container) */
	root = data_search_alloc("/");
	if (!root) {
		ast_free(name);
		ast_free(value);
		ast_free(comparison);
		return NULL;
	}

	for (elements = strtok_r(search_string_dup, ",", &saveptr); elements;
		elements = strtok_r(NULL, ",", &saveptr)) {
		/* Parse the name */
		ast_str_reset(name);
		for (i = 0; !data_search_comparison_char(elements[i]) &&
			elements[i]; i++) {
			ast_str_append(&name, 0, "%c", elements[i]);
		}

		/* check if the syntax is ok. */
		if (!data_search_comparison_char(elements[i])) {
			/* if this is the end of the string, then this is
			 * an error! */
			ast_log(LOG_ERROR, "Invalid search string!\n");
			continue;
		}

		/* parse the comparison string. */
		ast_str_reset(comparison);
		for (; data_search_comparison_char(elements[i]) && elements[i]; i++) {
			ast_str_append(&comparison, 0, "%c", elements[i]);
		}

		/* parse the value string. */
		ast_str_reset(value);
		for (; elements[i]; i++) {
			ast_str_append(&value, 0, "%c", elements[i]);
		}

		cmp_type = data_search_comparison_type(ast_str_buffer(comparison));
		if (cmp_type == DATA_CMP_UNKNOWN) {
			ast_log(LOG_ERROR, "Invalid comparison '%s'\n",
				ast_str_buffer(comparison));
			continue;
		}

		/* add this node to the tree. */
		child = data_search_create(root->children, ast_str_buffer(name));
		if (child) {
			child->cmp_type = cmp_type;
			child->value = ast_strdup(ast_str_buffer(value));
		}
	}

	ast_free(name);
	ast_free(value);
	ast_free(comparison);

	return root;
}

/*!
 * \internal
 * \brief Release the allocated memory for the search tree.
 * \param[in] search The search tree root node.
 */
static void data_search_release(struct ast_data_search *search)
{
	ao2_ref(search, -1);
}

/*!
 * \internal
 * \brief Based on the kind of comparison and the result in cmpval, return
 *        if it matches.
 * \param[in] cmpval A result returned by a strcmp() for example.
 * \param[in] comparison_type The kind of comparison (<,>,=,!=,...)
 * \retval 1 If the comparison doesn't match.
 * \retval 0 If the comparison matches.
 */
static inline int data_search_comparison_result(int cmpval,
	enum data_search_comparison comparison_type)
{
	switch (comparison_type) {
	case DATA_CMP_GE:
		if (cmpval >= 0) {
			return 0;
		}
		break;
	case DATA_CMP_LE:
		if (cmpval <= 0) {
			return 0;
		}
		break;
	case DATA_CMP_EQ:
		if (cmpval == 0) {
			return 0;
		}
		break;
	case DATA_CMP_NEQ:
		if (cmpval != 0) {
			return 0;
		}
		break;
	case DATA_CMP_LT:
		if (cmpval < 0) {
			return 0;
		}
		break;
	case DATA_CMP_GT:
		if (cmpval > 0) {
			return 0;
		}
		break;
	case DATA_CMP_UNKNOWN:
		break;
	}
	return 1;
}

/*!
 * \internal
 * \brief Get an internal node, from the search tree.
 * \param[in] node A node container.
 * \param[in] path The path to the needed internal node.
 * \retval NULL if the internal node is not found.
 * \retval non-NULL the internal node with path 'path'.
 */
static struct ast_data_search *data_search_get_node(const struct ast_data_search *node,
	const char *path)
{
	char *savepath, *node_name;
	struct ast_data_search *child, *current = (struct ast_data_search *) node;

	if (!node) {
		return NULL;
	}

	savepath = ast_strdupa(path);
	node_name = next_node_name(&savepath);

	while (node_name) {
		child = data_search_find(current->children, node_name);
		if (current != node) {
			ao2_ref(current, -1);
		}
		if (!child) {
			return NULL;
		};
		current = child;
		node_name = next_node_name(&savepath);
	}

	return current;
}

/*!
 * \internal
 * \brief Based on a search tree, evaluate the specified 'name' inside the tree with the
 *        current string value.
 *        .search = "somename=somestring"
 *        name = "somename"
 *        value is the current value of something and will be evaluated against "somestring".
 * \param[in] root The root node pointer of the search tree.
 * \param[in] name The name of the specific.
 * \param[in] value The value to compare.
 * \returns The strcmp return value.
 */
static int data_search_cmp_string(const struct ast_data_search *root, const char *name,
	char *value)
{
	struct ast_data_search *child;
	enum data_search_comparison cmp_type;
	int ret;

	child = data_search_get_node(root, name);
	if (!child) {
		return 0;
	}

	ret = strcmp(value, child->value);
	cmp_type = child->cmp_type;

	ao2_ref(child, -1);

	return data_search_comparison_result(ret, cmp_type);
}

/*!
 * \internal
 * \brief Based on a search tree, evaluate the specified 'name' inside the tree with the
 *        current pointer address value.
 *        .search = "something=0x32323232"
 *        name = "something"
 *        value is the current value of something and will be evaluated against "0x32323232".
 * \param[in] root The root node pointer of the search tree.
 * \param[in] name The name of the specific.
 * \param[in] ptr The pointer address to compare.
 * \returns The (value - current_value) result.
 */
static int data_search_cmp_ptr(const struct ast_data_search *root, const char *name,
	void *ptr)
{
	struct ast_data_search *child;
	enum data_search_comparison cmp_type;
	void *node_ptr;

	child = data_search_get_node(root, name);
	if (!child) {
		return 0;
	}

	cmp_type = child->cmp_type;

	if (sscanf(child->value, "%p", &node_ptr) <= 0) {
		ao2_ref(child, -1);
		return 1;
	}

	ao2_ref(child, -1);

	return data_search_comparison_result((node_ptr - ptr), cmp_type);
}

/*!
 * \internal
 * \brief Based on a search tree, evaluate the specified 'name' inside the tree with the
 *        current ipv4 address value.
 *        .search = "something=192.168.2.2"
 *        name = "something"
 *        value is the current value of something and will be evaluated against "192.168.2.2".
 * \param[in] root The root node pointer of the search tree.
 * \param[in] name The name of the specific.
 * \param[in] addr The ipv4 address value to compare.
 * \returns The (value - current_value) result.
 */
static int data_search_cmp_ipaddr(const struct ast_data_search *root, const char *name,
	struct in_addr addr)
{
	struct ast_data_search *child;
	enum data_search_comparison cmp_type;
	struct in_addr node_addr;

	child = data_search_get_node(root, name);
	if (!child) {
		return 0;
	}
	cmp_type = child->cmp_type;

	inet_aton(child->value, &node_addr);

	ao2_ref(child, -1);

	return data_search_comparison_result((node_addr.s_addr - addr.s_addr), cmp_type);
}

/*!
 * \internal
 * \brief Based on a search tree, evaluate the specified 'name' inside the tree with the
 *        current boolean value.
 *        .search = "something=true"
 *        name = "something"
 *        value is the current value of something and will be evaluated against "true".
 * \param[in] root The root node pointer of the search tree.
 * \param[in] name The name of the specific.
 * \param[in] value The boolean value to compare.
 * \returns The (value - current_value) result.
 */
static int data_search_cmp_bool(const struct ast_data_search *root, const char *name,
	unsigned int value)
{
	struct ast_data_search *child;
	unsigned int node_value;
	enum data_search_comparison cmp_type;

	child = data_search_get_node(root, name);
	if (!child) {
		return 0;
	}

	node_value = abs(ast_true(child->value));
	cmp_type = child->cmp_type;

	ao2_ref(child, -1);

	return data_search_comparison_result(value - node_value, cmp_type);
}

/*!
 * \internal
 * \brief Based on a search tree, evaluate the specified 'name' inside the tree with the
 *        current double value.
 *        .search = "something=222"
 *        name = "something"
 *        value is the current value of something and will be evaluated against "222".
 * \param[in] root The root node pointer of the search tree.
 * \param[in] name The name of the specific.
 * \param[in] value The double value to compare.
 * \returns The (value - current_value) result.
 */
static int data_search_cmp_dbl(const struct ast_data_search *root, const char *name,
	double value)
{
	struct ast_data_search *child;
	double node_value;
	enum data_search_comparison cmp_type;

	child = data_search_get_node(root, name);
	if (!child) {
		return 0;
	}

	node_value = strtod(child->value, NULL);
	cmp_type = child->cmp_type;

	ao2_ref(child, -1);

	return data_search_comparison_result(value - node_value, cmp_type);
}

/*!
 * \internal
 * \brief Based on a search tree, evaluate the specified 'name' inside the tree with the
 *        current unsigned integer value.
 *        .search = "something=10"
 *        name = "something"
 *        value is the current value of something and will be evaluated against "10".
 * \param[in] root The root node pointer of the search tree.
 * \param[in] name The name of the specific.
 * \param[in] value The unsigned value to compare.
 * \returns The strcmp return value.
 */
static int data_search_cmp_uint(const struct ast_data_search *root, const char *name,
	unsigned int value)
{
	struct ast_data_search *child;
	unsigned int node_value;
	enum data_search_comparison cmp_type;

	child = data_search_get_node(root, name);
	if (!child) {
		return 0;
	}

	node_value = atoi(child->value);
	cmp_type = child->cmp_type;

	ao2_ref(child, -1);

	return data_search_comparison_result(value - node_value, cmp_type);
}

/*!
 * \internal
 * \brief Based on a search tree, evaluate the specified 'name' inside the tree with the
 *        current signed integer value.
 *        .search = "something=10"
 *        name = "something"
 *        value is the current value of something and will be evaluated against "10".
 * \param[in] root The root node pointer of the search tree.
 * \param[in] name The name of the specific.
 * \param[in] value The value to compare.
 * \returns The strcmp return value.
 */
static int data_search_cmp_int(const struct ast_data_search *root, const char *name,
	int value)
{
	struct ast_data_search *child;
	int node_value;
	enum data_search_comparison cmp_type;

	child = data_search_get_node(root, name);
	if (!child) {
		return 0;
	}

	node_value = atoi(child->value);
	cmp_type = child->cmp_type;

	ao2_ref(child, -1);

	return data_search_comparison_result(value - node_value, cmp_type);
}

/*!
 * \internal
 * \brief Based on a search tree, evaluate the specified 'name' inside the tree with the
 *        current character value.
 *        .search = "something=c"
 *        name = "something"
 *        value is the current value of something and will be evaluated against "c".
 * \param[in] root The root node pointer of the search tree.
 * \param[in] name The name of the specific.
 * \param[in] value The boolean value to compare.
 * \returns The (value - current_value) result.
 */
static int data_search_cmp_char(const struct ast_data_search *root, const char *name,
	char value)
{
	struct ast_data_search *child;
	char node_value;
	enum data_search_comparison cmp_type;

	child = data_search_get_node(root, name);
	if (!child) {
		return 0;
	}

	node_value = *(child->value);
	cmp_type = child->cmp_type;

	ao2_ref(child, -1);

	return data_search_comparison_result(value - node_value, cmp_type);
}

/*!
 * \internal
 * \brief Get the member pointer, from a mapping structure, based on its name.
 * \XXX We will need to improve performance here!!.
 * \retval <0 if the member was not found.
 * \retval >=0 The member position in the mapping structure.
 */
static inline int data_search_mapping_find(const struct ast_data_mapping_structure *map,
	size_t mapping_len,
	const char *member_name)
{
	int i;

	for (i = 0; i < mapping_len; i++) {
		if (!strcmp(map[i].name, member_name)) {
			return i;
		}
	}

	return -1;
}

int __ast_data_search_cmp_structure(const struct ast_data_search *search,
	const struct ast_data_mapping_structure *mapping, size_t mapping_len,
	void *structure, const char *structure_name)
{
	struct ao2_iterator i;
	struct ast_data_search *node, *struct_children;
	int member, notmatch = 0;

	if (!search) {
		return 0;
	}

	struct_children = data_search_get_node(search, structure_name);
	if (!struct_children) {
		return 0;
	}

	i = ao2_iterator_init(struct_children->children, 0);
	while ((node = ao2_iterator_next(&i))) {
		member = data_search_mapping_find(mapping, mapping_len, node->name);
		if (member < 0) {
			/* the structure member name doesn't match! */
			ao2_ref(node, -1);
			ao2_ref(struct_children, -1);
			ao2_iterator_destroy(&i);
			return 0;
		}

		notmatch = 0;
		switch (mapping[member].type) {
		case AST_DATA_PASSWORD:
			notmatch = data_search_cmp_string(struct_children,
				node->name,
				mapping[member].get.AST_DATA_PASSWORD(structure));
			break;
		case AST_DATA_TIMESTAMP:
			notmatch = data_search_cmp_uint(struct_children,
				node->name,
				mapping[member].get.AST_DATA_TIMESTAMP(structure));
			break;
		case AST_DATA_SECONDS:
			notmatch = data_search_cmp_uint(struct_children,
				node->name,
				mapping[member].get.AST_DATA_SECONDS(structure));
			break;
		case AST_DATA_MILLISECONDS:
			notmatch = data_search_cmp_uint(struct_children,
				node->name,
				mapping[member].get.AST_DATA_MILLISECONDS(structure));
			break;
		case AST_DATA_STRING:
			notmatch = data_search_cmp_string(struct_children,
				node->name,
				mapping[member].get.AST_DATA_STRING(structure));
			break;
		case AST_DATA_CHARACTER:
			notmatch = data_search_cmp_char(struct_children,
				node->name,
				mapping[member].get.AST_DATA_CHARACTER(structure));
			break;
		case AST_DATA_INTEGER:
			notmatch = data_search_cmp_int(struct_children,
				node->name,
				mapping[member].get.AST_DATA_INTEGER(structure));
			break;
		case AST_DATA_BOOLEAN:
			notmatch = data_search_cmp_bool(struct_children,
				node->name,
				mapping[member].get.AST_DATA_BOOLEAN(structure));
			break;
		case AST_DATA_UNSIGNED_INTEGER:
			notmatch = data_search_cmp_uint(struct_children,
				node->name,
				mapping[member].get.AST_DATA_UNSIGNED_INTEGER(structure));
			break;
		case AST_DATA_DOUBLE:
			notmatch = data_search_cmp_dbl(struct_children,
				node->name,
				mapping[member].get.AST_DATA_DOUBLE(structure));
			break;
		case AST_DATA_IPADDR:
			notmatch = data_search_cmp_ipaddr(struct_children,
				node->name,
				mapping[member].get.AST_DATA_IPADDR(structure));
			break;
		case AST_DATA_POINTER:
			notmatch = data_search_cmp_ptr(struct_children,
				node->name,
				mapping[member].get.AST_DATA_POINTER(structure));
			break;
		case AST_DATA_CONTAINER:
			break;
		}

		ao2_ref(node, -1);
	}
	ao2_iterator_destroy(&i);

	ao2_ref(struct_children, -1);

	return notmatch;
}

/*!
 * \internal
 * \brief Release the memory allocated by a call to ao2_alloc.
 */
static void data_result_destructor(void *obj)
{
	struct ast_data *root = obj;

	switch (root->type) {
	case AST_DATA_PASSWORD:
	case AST_DATA_STRING:
		ast_free(root->payload.str);
		ao2_ref(root->children, -1);
		break;
	case AST_DATA_POINTER:
	case AST_DATA_CHARACTER:
	case AST_DATA_CONTAINER:
	case AST_DATA_INTEGER:
	case AST_DATA_TIMESTAMP:
	case AST_DATA_SECONDS:
	case AST_DATA_MILLISECONDS:
	case AST_DATA_UNSIGNED_INTEGER:
	case AST_DATA_DOUBLE:
	case AST_DATA_BOOLEAN:
	case AST_DATA_IPADDR:
		ao2_ref(root->children, -1);
		break;
	}
}

static struct ast_data *data_result_create(const char *name)
{
	struct ast_data *res;
	size_t namelen;

	namelen = ast_strlen_zero(name) ? 1 : strlen(name) + 1;

	res = ao2_alloc(sizeof(*res) + namelen, data_result_destructor);
	if (!res) {
		return NULL;
	}

	strcpy(res->name, namelen ? name : "");

	/* initialize the children container */
	res->children = ao2_container_alloc(NUM_DATA_RESULT_BUCKETS, data_result_hash,
		data_result_cmp);
	if (!res->children) {
		ao2_ref(res, -1);
		return NULL;
	}

	/* set this node as a container. */
	res->type = AST_DATA_CONTAINER;

	return res;
}

/*!
 * \internal
 * \brief Find a child node, based on its name.
 * \param[in] root The starting point.
 * \param[in] name The child name.
 * \retval NULL if the node wasn't found.
 * \retval non-NULL the node we were looking for.
 */
static struct ast_data *data_result_find_child(struct ast_data *root, const char *name)
{
	struct ast_data *found, *find_node;

	find_node = data_result_create(name);
	if (!find_node) {
		return NULL;
	}

	found = ao2_find(root->children, find_node, OBJ_POINTER);

	/* release the temporary created node used for searching. */
	ao2_ref(find_node, -1);

	return found;
}

int ast_data_search_match(const struct ast_data_search *search, struct ast_data *data)
{
	struct ao2_iterator i, ii;
	struct ast_data_search *s, *s_child;
	struct ast_data *d_child;
	int notmatch = 1;

	if (!search) {
		return 1;
	}

	s_child = data_search_find(search->children, data->name);
	if (!s_child) {
		/* nothing to compare */
		ao2_ref(s_child, -1);
		return 1;
	}

	i = ao2_iterator_init(s_child->children, 0);
	while ((s = ao2_iterator_next(&i))) {
		if (!ao2_container_count(s->children)) {
			/* compare this search node with every data node */
			d_child = data_result_find_child(data, s->name);
			if (!d_child) {
				ao2_ref(s, -1);
				notmatch = 1;
				continue;
			}

			switch (d_child->type) {
			case AST_DATA_PASSWORD:
			case AST_DATA_STRING:
				notmatch = data_search_cmp_string(s_child, d_child->name,
					d_child->payload.str);
				break;
			case AST_DATA_CHARACTER:
				notmatch = data_search_cmp_char(s_child, d_child->name,
					d_child->payload.character);
				break;
			case AST_DATA_INTEGER:
				notmatch = data_search_cmp_int(s_child, d_child->name,
					d_child->payload.sint);
				break;
			case AST_DATA_BOOLEAN:
				notmatch = data_search_cmp_bool(s_child, d_child->name,
					d_child->payload.boolean);
				break;
			case AST_DATA_UNSIGNED_INTEGER:
				notmatch = data_search_cmp_uint(s_child, d_child->name,
					d_child->payload.uint);
				break;
			case AST_DATA_TIMESTAMP:
			case AST_DATA_SECONDS:
			case AST_DATA_MILLISECONDS:
			case AST_DATA_DOUBLE:
				notmatch = data_search_cmp_uint(s_child, d_child->name,
					d_child->payload.dbl);
				break;
			case AST_DATA_IPADDR:
				notmatch = data_search_cmp_ipaddr(s_child, d_child->name,
					d_child->payload.ipaddr);
				break;
			case AST_DATA_POINTER:
				notmatch = data_search_cmp_ptr(s_child, d_child->name,
					d_child->payload.ptr);
				break;
			case AST_DATA_CONTAINER:
				break;
			}
			ao2_ref(d_child, -1);
		} else {
			ii = ao2_iterator_init(data->children, 0);
			while ((d_child = ao2_iterator_next(&ii))) {
				if (strcmp(d_child->name, s->name)) {
					ao2_ref(d_child, -1);
					continue;
				}
				if (!(notmatch = !ast_data_search_match(s_child, d_child))) {
					/* do not continue if we have a match. */
					ao2_ref(d_child, -1);
					break;
				}
				ao2_ref(d_child, -1);
			}
			ao2_iterator_destroy(&ii);
		}
		ao2_ref(s, -1);
		if (notmatch) {
			/* do not continue if we don't have a match. */
			break;
		}
	}
	ao2_iterator_destroy(&i);

	ao2_ref(s_child, -1);

	return !notmatch;
}

/*!
 * \internal
 * \brief Get an internal node, from the result set.
 * \param[in] node A node container.
 * \param[in] path The path to the needed internal node.
 * \retval NULL if the internal node is not found.
 * \retval non-NULL the internal node with path 'path'.
 */
static struct ast_data *data_result_get_node(struct ast_data *node,
	const char *path)
{
	char *savepath, *node_name;
	struct ast_data *child, *current = node;

	savepath = ast_strdupa(path);
	node_name = next_node_name(&savepath);

	while (node_name) {
		child = data_result_find_child(current, node_name);
		if (current != node) {
			ao2_ref(current, -1);
		}
		if (!child) {
			return NULL;
		}
		current = child;
		node_name = next_node_name(&savepath);
	}

	/* do not increment the refcount of the returned object. */
	if (current != node) {
		ao2_ref(current, -1);
	}

	return current;
}

/*!
 * \internal
 * \brief Add a child to the specified root node.
 * \param[in] root The root node pointer.
 * \param[in] child The child to add to the root node.
 */
static void data_result_add_child(struct ast_data *root, struct ast_data *child)
{
	ao2_link(root->children, child);
}

/*!
 * \internal
 * \brief Common string hash function for data nodes
 */
static int data_filter_hash(const void *obj, const int flags)
{
	const struct data_filter *node = obj;
	return ast_str_hash(node->name);
}

/*!
 * \internal
 * \brief Common string comparison function
 */
static int data_filter_cmp(void *obj, void *arg, int flags)
{
	struct data_filter *node1 = obj, *node2 = arg;
	return strcasecmp(node1->name, node2->name) ? 0 : CMP_MATCH;
}

/*!
 * \internal
 * \brief Destroy a data filter tree.
 * \param[in] obj Data filter list to be destroyed.
 */
static void data_filter_destructor(void *obj)
{
	struct data_filter *filter = obj, *globres;

	while ((globres = AST_LIST_REMOVE_HEAD(&(filter->glob_list), list))) {
		ao2_ref(globres, -1);
	}

	ao2_ref(filter->children, -1);
}

/*!
 * \internal
 * \brief Allocate a filter node.
 * \retval NULL on error.
 * \retval non-NULL The allocated search node structure.
 */
static struct data_filter *data_filter_alloc(const char *name)
{
	char *globname, *token;
	struct data_filter *res, *globfilter;
	size_t name_len = strlen(name) + 1;

	res = ao2_alloc(sizeof(*res) + name_len, data_filter_destructor);
	if (!res) {
		return NULL;
	}

	res->children = ao2_container_alloc(NUM_DATA_FILTER_BUCKETS, data_filter_hash,
		data_filter_cmp);

	if (!res->children) {
		ao2_ref(res, -1);
		return NULL;
	}

	strcpy(res->name, name);

	if (strchr(res->name, '*')) {
		globname = ast_strdupa(res->name);

		while ((token = strsep(&globname, "*"))) {
			globfilter = data_filter_alloc(token);
			AST_LIST_INSERT_TAIL(&(res->glob_list), globfilter, list);
		}
	}

	return res;
}

/*!
 * \internal
 * \brief Release a filter tree.
 * \param[in] filter The filter tree root node.
 */
static void data_filter_release(struct data_filter *filter)
{
	ao2_ref(filter, -1);
}

/*!
 * \internal
 * \brief Find a child node, based on his name.
 * \param[in] parent Where to find the node.
 * \param[in] name The node name to find.
 * \retval NULL if a node wasn't found.
 * \retval The node found.
 * \note Remember to decrement the ref count of the returned node after using it.
 */
static struct data_filter *data_filter_find(struct ao2_container *parent,
	const char *name)
{
	int i, olend, orend, globfound;
	size_t name_len = strlen(name), glob_len;
	struct ao2_iterator iter;
	struct data_filter *find_node, *found, *globres;

	find_node = data_filter_alloc(name);
	if (!find_node) {
		return NULL;
	}

	found = ao2_find(parent, find_node, OBJ_POINTER);

	/* free the created node used for searching. */
	ao2_ref(find_node, -1);

	if (found) {
		return found;
	}

	iter = ao2_iterator_init(parent, 0);
	while ((found = ao2_iterator_next(&iter))) {
		if (!AST_LIST_EMPTY(&(found->glob_list))) {
			i = 0;
			globfound = 1;

			olend = ast_strlen_zero(AST_LIST_FIRST(&(found->glob_list))->name);
			orend = ast_strlen_zero(AST_LIST_LAST(&(found->glob_list))->name);

			AST_LIST_TRAVERSE(&(found->glob_list), globres, list) {
				if (!*globres->name) {
					continue;
				}

				glob_len = strlen(globres->name);

				if (!i && !olend) {
					if (strncasecmp(name, globres->name, glob_len)) {
						globfound = 0;
						break;
					}

					i += glob_len;
					continue;
				}

				for (globfound = 0; name_len - i >= glob_len; ++i) {
					if (!strncasecmp(name + i, globres->name, glob_len)) {
						globfound = 1;
						i += glob_len;
						break;
					}
				}

				if (!globfound) {
					break;
				}
			}

			if (globfound && (i == name_len || orend)) {
				ao2_iterator_destroy(&iter);
				return found;
			}
		}

		ao2_ref(found, -1);
	}
	ao2_iterator_destroy(&iter);

	return NULL;
}

/*!
 * \internal
 * \brief Add a child to the specified node.
 * \param[in] root The root node where to add the child.
 * \param[in] name The name of the node to add.
 * \note Remember to decrement the ref count after using the returned node.
 */
static struct data_filter *data_filter_add_child(struct ao2_container *root,
	char *name)
{
	struct data_filter *node;

	node = data_filter_find(root, name);
	if (node) {
		return node;
	}

	node = data_filter_alloc(name);
	if (!node) {
		return NULL;
	}

	ao2_link(root, node);

	return node;
}

/*!
 * \internal
 * \brief Add a node to a filter list from a path
 * \param[in] Filter list to add the path onto.
 * \param[in] The path to add into the filter list.
 * \retval NULL on error.
 * \retval non-NULL A tree with the wanted nodes.
 */
static int data_filter_add_nodes(struct ao2_container *root, char *path)
{
	struct data_filter *node;
	char *savepath, *saveptr, *token, *node_name;
	int ret = 0;

	if (!path) {
		return 0;
	}

	savepath = ast_strdupa(path);

	node_name = next_node_name(&savepath);

	if (!node_name) {
		return 0;
	}

	for (token = strtok_r(node_name, "|", &saveptr);
			token; token = strtok_r(NULL, "|", &saveptr)) {
		node = data_filter_add_child(root, token);
		if (!node) {
			continue;
		}
		data_filter_add_nodes(node->children, savepath);
		ret = 1;
		ao2_ref(node, -1);
	}

	return ret;
}

/*!
 * \internal
 * \brief Generate a filter list based on a filter string provided by the API user.
 * \param[in] A filter string to create a filter from.
 */
static struct data_filter *data_filter_generate(const char *constfilter)
{
	struct data_filter *filter = NULL;
	char *strfilter, *token, *saveptr;
	int node_added = 0;

	if (!constfilter) {
		return NULL;
	}

	strfilter = ast_strdupa(constfilter);

	filter = data_filter_alloc("/");
	if (!filter) {
		return NULL;
	}

	for (token = strtok_r(strfilter, ",", &saveptr); token;
			token = strtok_r(NULL, ",", &saveptr)) {
		node_added = data_filter_add_nodes(filter->children, token);
	}

	if (!node_added) {
		ao2_ref(filter, -1);
		return NULL;
	}

	return filter;
}

/*!
 * \internal
 * \brief Generate all the tree from a specified provider.
 * \param[in] query The query executed.
 * \param[in] root_provider The provider specified in the path of the query.
 * \param[in] parent_node_name The root node name.
 * \retval NULL on error.
 * \retval non-NULL The generated result tree.
 */
static struct ast_data *data_result_generate_node(const struct ast_data_query *query,
	const struct data_provider *root_provider,
	const char *parent_node_name,
	const struct ast_data_search *search,
	const struct data_filter *filter)
{
	struct ast_data *generated, *node;
	struct ao2_iterator i;
	struct data_provider *provider;
	struct ast_data_search *search_child = NULL;
	struct data_filter *filter_child;

	node = data_result_create(parent_node_name);
	if (!node) {
		ast_log(LOG_ERROR, "Unable to allocate '%s' node\n", parent_node_name);
		return NULL;
	}

	if (root_provider->module) {
		ast_module_ref(root_provider->module);
	}

	/* if this is a terminal node, just run the callback function. */
	if (root_provider->handler && root_provider->handler->get) {
		node->filter = filter;
		root_provider->handler->get(search, node);
		if (root_provider->module) {
			ast_module_unref(root_provider->module);
		}
		return node;
	}

	if (root_provider->module) {
		ast_module_unref(root_provider->module);
	}

	/* if this is not a terminal node, generate every child node. */
	i = ao2_iterator_init(root_provider->children, 0);
	while ((provider = ao2_iterator_next(&i))) {
		filter_child = NULL;
		generated = NULL;

		/* get the internal search node. */
		if (search) {
			search_child = data_search_find(search->children, provider->name);
		}
		/* get the internal filter node. */
		if (filter) {
			filter_child = data_filter_find(filter->children, provider->name);
		}

		if (!filter || filter_child) {
			/* only generate the internal node, if we have something to
			 * generate based on the filtering string. */
			generated = data_result_generate_node(query, provider,
				provider->name,
				search_child, filter_child);
		}

		/* decrement the refcount of the internal search node. */
		if (search_child) {
			ao2_ref(search_child, -1);
		}

		/* decrement the refcount of the internal filter node. */
		if (filter_child) {
			ao2_ref(filter_child, -1);
		}

		if (generated) {
			data_result_add_child(node, generated);
			ao2_ref(generated, -1);
		}

		ao2_ref(provider, -1);
	}
	ao2_iterator_destroy(&i);

	return node;
}

/*!
 * \internal
 * \brief Generate a result tree based on a query.
 * \param[in] query The complete query structure.
 * \param[in] search_path The path to retrieve.
 * \retval NULL on error.
 * \retval non-NULL The generated data result.
 */
static struct ast_data *data_result_generate(const struct ast_data_query *query,
	const char *search_path)
{
	char *node_name, *tmp_path;
	struct data_provider *provider_child, *tmp_provider_child;
	struct ast_data *result, *result_filtered;
	struct ast_data_search *search = NULL, *search_child = NULL;
	struct data_filter *filter = NULL, *filter_child = NULL;

	if (!search_path) {
		/* generate all the trees?. */
		return NULL;
	}

	tmp_path = ast_strdupa(search_path);

	/* start searching the root node name */
	node_name = next_node_name(&tmp_path);
	if (!node_name) {
		return NULL;
	}
	provider_child = data_provider_find(root_data.container, node_name, NULL);

	/* continue with the rest of the path. */
	while (provider_child) {
		node_name = next_node_name(&tmp_path);
		if (!node_name) {
			break;
		}

		tmp_provider_child = data_provider_find(provider_child->children,
				node_name, NULL);

		/* release the reference from this child */
		ao2_ref(provider_child, -1);

		provider_child = tmp_provider_child;
	}

	if (!provider_child) {
		ast_log(LOG_ERROR, "Invalid path '%s', '%s' not found.\n",
				tmp_path, node_name);
		return NULL;
	}

	/* generate the search tree. */
	if (query->search) {
		search = data_search_generate(query->search);
		if (search) {
			search_child = data_search_find(search->children,
				provider_child->name);
		}
	}

	/* generate the filter tree. */
	if (query->filter) {
		filter = data_filter_generate(query->filter);
		if (filter) {
			filter_child = data_filter_find(filter->children,
				provider_child->name);
		}
	}

	result = data_result_generate_node(query, provider_child, provider_child->name,
			search_child, filter_child);

	/* release the requested provider. */
	ao2_ref(provider_child, -1);

	/* release the generated search tree. */
	if (search_child) {
		ao2_ref(search_child, -1);
	}

	if (filter_child) {
		ao2_ref(filter_child, -1);
	}

	if (search) {
		data_search_release(search);
	}

	result_filtered = result;

	/* release the generated filter tree. */
	if (filter) {
		data_filter_release(filter);
	}

	return result_filtered;
}

struct ast_data *ast_data_get(const struct ast_data_query *query)
{
	struct ast_data *res;

	/* check compatibility */
	if (!data_structure_compatible(query->version, latest_query_compatible_version,
		current_query_version)) {
		return NULL;
	}

	data_read_lock();
	res = data_result_generate(query, query->path);
	data_unlock();

	if (!res) {
		ast_log(LOG_ERROR, "Unable to get data from %s\n", query->path);
		return NULL;
	}

	return res;
}

#ifdef HAVE_LIBXML2
/*!
 * \internal
 * \brief Helper function to move an ast_data tree to xml.
 * \param[in] parent_data The initial ast_data node to be passed to xml.
 * \param[out] parent_xml The root node to insert the xml.
 */
static void data_get_xml_add_child(struct ast_data *parent_data,
	struct ast_xml_node *parent_xml)
{
	struct ao2_iterator i;
	struct ast_data *node;
	struct ast_xml_node *child_xml;
	char node_content[256];

	i = ao2_iterator_init(parent_data->children, 0);
	while ((node = ao2_iterator_next(&i))) {
		child_xml = ast_xml_new_node(node->name);
		if (!child_xml) {
			ao2_ref(node, -1);
			continue;
		}

		switch (node->type) {
		case AST_DATA_CONTAINER:
			data_get_xml_add_child(node, child_xml);
			break;
		case AST_DATA_PASSWORD:
			ast_xml_set_text(child_xml, node->payload.str);
			break;
		case AST_DATA_TIMESTAMP:
			snprintf(node_content, sizeof(node_content), "%u",
				node->payload.uint);
			ast_xml_set_text(child_xml, node_content);
			break;
		case AST_DATA_SECONDS:
			snprintf(node_content, sizeof(node_content), "%u",
				node->payload.uint);
			ast_xml_set_text(child_xml, node_content);
			break;
		case AST_DATA_MILLISECONDS:
			snprintf(node_content, sizeof(node_content), "%u",
				node->payload.uint);
			ast_xml_set_text(child_xml, node_content);
			break;
		case AST_DATA_STRING:
			ast_xml_set_text(child_xml, node->payload.str);
			break;
		case AST_DATA_CHARACTER:
			snprintf(node_content, sizeof(node_content), "%c",
				node->payload.character);
			ast_xml_set_text(child_xml, node_content);
			break;
		case AST_DATA_INTEGER:
			snprintf(node_content, sizeof(node_content), "%d",
				node->payload.sint);
			ast_xml_set_text(child_xml, node_content);
			break;
		case AST_DATA_UNSIGNED_INTEGER:
			snprintf(node_content, sizeof(node_content), "%u",
				node->payload.uint);
			ast_xml_set_text(child_xml, node_content);
			break;
		case AST_DATA_DOUBLE:
			snprintf(node_content, sizeof(node_content), "%f",
				node->payload.dbl);
			ast_xml_set_text(child_xml, node_content);
			break;
		case AST_DATA_BOOLEAN:
			if (node->payload.boolean) {
				ast_xml_set_text(child_xml, "true");
			} else {
				ast_xml_set_text(child_xml, "false");
			}
			break;
		case AST_DATA_POINTER:
			snprintf(node_content, sizeof(node_content), "%p",
				node->payload.ptr);
			ast_xml_set_text(child_xml, node_content);
			break;
		case AST_DATA_IPADDR:
			snprintf(node_content, sizeof(node_content), "%s",
				ast_inet_ntoa(node->payload.ipaddr));
			ast_xml_set_text(child_xml, node_content);
			break;
		}
		ast_xml_add_child(parent_xml, child_xml);

		ao2_ref(node, -1);
	}
	ao2_iterator_destroy(&i);

}

struct ast_xml_doc *ast_data_get_xml(const struct ast_data_query *query)
{
	struct ast_xml_doc *doc;
	struct ast_xml_node *root;
	struct ast_data *res;

	res = ast_data_get(query);
	if (!res) {
		return NULL;
	}

	doc = ast_xml_new();
	if (!doc) {
		ast_data_free(res);
		return NULL;
	}

	root = ast_xml_new_node(res->name);
	if (!root) {
		ast_xml_close(doc);
	}

	ast_xml_set_root(doc, root);

	data_get_xml_add_child(res, root);

	ast_data_free(res);

	return doc;
}
#endif

enum ast_data_type ast_data_retrieve_type(struct ast_data *node, const char *path)
{
	struct ast_data *internal;

	internal = data_result_get_node(node, path);
	if (!internal) {
		return -1;
	}

	return internal->type;
}

char *ast_data_retrieve_name(struct ast_data *node)
{
	return node->name;
}

/*!
 * \internal
 * \brief Insert a child node inside a passed parent node.
 * \param root Where we are going to insert the child node.
 * \param name The name of the child node to add.
 * \param type The type of content inside the child node.
 * \param ptr The actual content of the child node.
 * \retval NULL on error.
 * \retval non-NULL The added child node pointer.
 */
static struct ast_data *__ast_data_add(struct ast_data *root, const char *name,
	enum ast_data_type type, void *ptr)
{
	struct ast_data *node;
	struct data_filter *filter, *filter_child = NULL;

	if (!root || !root->children) {
		/* invalid data result node. */
		return NULL;
	}

	/* check if we need to add this node, based on the filter. */
	if (root->filter) {
		filter = data_filter_find(root->filter->children, name);
		if (!filter) {
			return NULL;
		}
		ao2_ref(filter, -1);
	}

	node = data_result_create(name);
	if (!node) {
		return NULL;
	}

	node->type = type;

	switch (type) {
	case AST_DATA_BOOLEAN:
		node->payload.boolean = *(unsigned int *) ptr;
		break;
	case AST_DATA_INTEGER:
		node->payload.sint = *(int *) ptr;
		break;
	case AST_DATA_TIMESTAMP:
	case AST_DATA_SECONDS:
	case AST_DATA_MILLISECONDS:
	case AST_DATA_UNSIGNED_INTEGER:
		node->payload.uint = *(unsigned int *) ptr;
		break;
	case AST_DATA_DOUBLE:
		node->payload.dbl = *(double *) ptr;
		break;
	case AST_DATA_PASSWORD:
	case AST_DATA_STRING:
		node->payload.str = (char *) ptr;
		break;
	case AST_DATA_CHARACTER:
		node->payload.character = *(char *) ptr;
		break;
	case AST_DATA_POINTER:
		node->payload.ptr = ptr;
		break;
	case AST_DATA_IPADDR:
		node->payload.ipaddr = *(struct in_addr *) ptr;
		break;
	case AST_DATA_CONTAINER:
		if (root->filter) {
			filter_child = data_filter_find(root->filter->children, name);
			if (filter_child) {
				/* do not increment the refcount because it is not neccesary. */
				ao2_ref(filter_child, -1);
			}
		}
		node->filter = filter_child;
		break;
	default:
		break;
	}

	data_result_add_child(root, node);

	ao2_ref(node, -1);

	return node;
}

struct ast_data *ast_data_add_node(struct ast_data *root, const char *name)
{
	return __ast_data_add(root, name, AST_DATA_CONTAINER, NULL);
}

struct ast_data *ast_data_add_int(struct ast_data *root, const char *name, int value)
{
	return __ast_data_add(root, name, AST_DATA_INTEGER, &value);
}

struct ast_data *ast_data_add_char(struct ast_data *root, const char *name, char value)
{
	return __ast_data_add(root, name, AST_DATA_CHARACTER, &value);
}

struct ast_data *ast_data_add_uint(struct ast_data *root, const char *name,
	unsigned int value)
{
	return __ast_data_add(root, name, AST_DATA_UNSIGNED_INTEGER, &value);
}

struct ast_data *ast_data_add_dbl(struct ast_data *root, const char *childname,
	double dbl)
{
	return __ast_data_add(root, childname, AST_DATA_DOUBLE, &dbl);
}

struct ast_data *ast_data_add_bool(struct ast_data *root, const char *childname,
	unsigned int boolean)
{
	return __ast_data_add(root, childname, AST_DATA_BOOLEAN, &boolean);
}

struct ast_data *ast_data_add_ipaddr(struct ast_data *root, const char *childname,
	struct in_addr addr)
{
	return __ast_data_add(root, childname, AST_DATA_IPADDR, &addr);
}

struct ast_data *ast_data_add_ptr(struct ast_data *root, const char *childname,
	void *ptr)
{
	return __ast_data_add(root, childname, AST_DATA_POINTER, ptr);
}

struct ast_data *ast_data_add_timestamp(struct ast_data *root, const char *childname,
	unsigned int timestamp)
{
	return __ast_data_add(root, childname, AST_DATA_TIMESTAMP, &timestamp);
}

struct ast_data *ast_data_add_seconds(struct ast_data *root, const char *childname,
	unsigned int seconds)
{
	return __ast_data_add(root, childname, AST_DATA_SECONDS, &seconds);
}

struct ast_data *ast_data_add_milliseconds(struct ast_data *root, const char *childname,
	unsigned int milliseconds)
{
	return __ast_data_add(root, childname, AST_DATA_MILLISECONDS, &milliseconds);
}

struct ast_data *ast_data_add_password(struct ast_data *root, const char *childname,
	const char *value)
{
	char *name;
	size_t namelen = 1 + (ast_strlen_zero(value) ? 0 : strlen(value));
	struct ast_data *res;

	if (!(name = ast_malloc(namelen))) {
		return NULL;
	}

	strcpy(name, (ast_strlen_zero(value) ? "" : value));

	res = __ast_data_add(root, childname, AST_DATA_PASSWORD, name);
	if (!res) {
		ast_free(name);
	}

	return res;
}

struct ast_data *ast_data_add_str(struct ast_data *root, const char *childname,
	const char *value)
{
	char *name;
	size_t namelen = 1 + (ast_strlen_zero(value) ? 0 : strlen(value));
	struct ast_data *res;

	if (!(name = ast_malloc(namelen))) {
		return NULL;
	}

	strcpy(name, (ast_strlen_zero(value) ? "" : value));

	res = __ast_data_add(root, childname, AST_DATA_STRING, name);
	if (!res) {
		ast_free(name);
	}

	return res;
}

int __ast_data_add_structure(struct ast_data *root,
	const struct ast_data_mapping_structure *mapping, size_t mapping_len,
	void *structure)
{
	int i;

	for (i = 0; i < mapping_len; i++) {
		switch (mapping[i].type) {
		case AST_DATA_INTEGER:
			ast_data_add_int(root, mapping[i].name,
				mapping[i].get.AST_DATA_INTEGER(structure));
			break;
		case AST_DATA_UNSIGNED_INTEGER:
			ast_data_add_uint(root, mapping[i].name,
				mapping[i].get.AST_DATA_UNSIGNED_INTEGER(structure));
			break;
		case AST_DATA_DOUBLE:
			ast_data_add_dbl(root, mapping[i].name,
				mapping[i].get.AST_DATA_DOUBLE(structure));
			break;
		case AST_DATA_BOOLEAN:
			ast_data_add_bool(root, mapping[i].name,
				mapping[i].get.AST_DATA_BOOLEAN(structure));
			break;
		case AST_DATA_PASSWORD:
			ast_data_add_password(root, mapping[i].name,
				mapping[i].get.AST_DATA_PASSWORD(structure));
			break;
		case AST_DATA_TIMESTAMP:
			ast_data_add_timestamp(root, mapping[i].name,
				mapping[i].get.AST_DATA_TIMESTAMP(structure));
			break;
		case AST_DATA_SECONDS:
			ast_data_add_seconds(root, mapping[i].name,
				mapping[i].get.AST_DATA_SECONDS(structure));
			break;
		case AST_DATA_MILLISECONDS:
			ast_data_add_milliseconds(root, mapping[i].name,
				mapping[i].get.AST_DATA_MILLISECONDS(structure));
			break;
		case AST_DATA_STRING:
			ast_data_add_str(root, mapping[i].name,
				mapping[i].get.AST_DATA_STRING(structure));
			break;
		case AST_DATA_CHARACTER:
			ast_data_add_char(root, mapping[i].name,
				mapping[i].get.AST_DATA_CHARACTER(structure));
			break;
		case AST_DATA_CONTAINER:
			break;
		case AST_DATA_IPADDR:
			ast_data_add_ipaddr(root, mapping[i].name,
				mapping[i].get.AST_DATA_IPADDR(structure));
			break;
		case AST_DATA_POINTER:
			ast_data_add_ptr(root, mapping[i].name,
				mapping[i].get.AST_DATA_POINTER(structure));
			break;
		}
	}

	return 0;
}

void ast_data_remove_node(struct ast_data *root, struct ast_data *child)
{
	ao2_unlink(root->children, child);
}

void ast_data_free(struct ast_data *root)
{
	/* destroy it, this will destroy all the internal nodes. */
	ao2_ref(root, -1);
}

struct ast_data_iterator *ast_data_iterator_init(struct ast_data *tree,
	const char *elements)
{
	struct ast_data_iterator *iterator;
	struct ao2_iterator i;
	struct ast_data *internal = tree;
	char *path, *ptr = NULL;

	if (!elements) {
		return NULL;
	}

	/* tree is the node we want to use to iterate? or we are going
	 * to iterate thow an internal node? */
	path = ast_strdupa(elements);

	ptr = strrchr(path, '/');
	if (ptr) {
		*ptr = '\0';
		internal = data_result_get_node(tree, path);
		if (!internal) {
			return NULL;
		}
	}

	iterator = ast_calloc(1, sizeof(*iterator));
	if (!iterator) {
		return NULL;
	}

	i = ao2_iterator_init(internal->children, 0);

	iterator->pattern = (ptr ? strrchr(elements, '/') + 1 : elements);

	/* is the last node a regular expression?, compile it! */
	if (!regcomp(&(iterator->regex_pattern), iterator->pattern,
			REG_EXTENDED | REG_NOSUB | REG_ICASE)) {
		iterator->is_pattern = 1;
	}

	iterator->internal_iterator = i;

	return iterator;
}

void ast_data_iterator_end(struct ast_data_iterator *iterator)
{
	/* decrement the reference counter. */
	if (iterator->last) {
		ao2_ref(iterator->last, -1);
	}

	/* release the generated pattern. */
	if (iterator->is_pattern) {
		regfree(&(iterator->regex_pattern));
	}

	ao2_iterator_destroy(&(iterator->internal_iterator));

	ast_free(iterator);
	iterator = NULL;
}

struct ast_data *ast_data_iterator_next(struct ast_data_iterator *iterator)
{
	struct ast_data *res;

	if (iterator->last) {
		/* release the last retrieved node reference. */
		ao2_ref(iterator->last, -1);
	}

	while ((res = ao2_iterator_next(&iterator->internal_iterator))) {
		/* if there is no node name pattern specified, return
		 * the next node. */
		if (!iterator->pattern) {
			break;
		}

		/* if the pattern is a regular expression, check if this node
		 * matches. */
		if (iterator->is_pattern && !regexec(&(iterator->regex_pattern),
			res->name, 0, NULL, 0)) {
			break;
		}

		/* if there is a pattern specified, check if this node matches
		 * the wanted node names. */
		if (!iterator->is_pattern && (iterator->pattern &&
				!strcasecmp(res->name, iterator->pattern))) {
			break;
		}

		ao2_ref(res, -1);
	}

	iterator->last = res;

	return res;
}

int ast_data_retrieve(struct ast_data *tree, const char *path,
	struct ast_data_retrieve *content)
{
	struct ast_data *node;

	if (!content) {
		return -1;
	}

	node = data_result_get_node(tree, path);
	if (!node) {
		ast_log(LOG_ERROR, "Invalid internal node %s\n", path);
		return -1;
	}

	content->type = node->type;
	switch (node->type) {
	case AST_DATA_STRING:
		content->value.AST_DATA_STRING = node->payload.str;
		break;
	case AST_DATA_PASSWORD:
		content->value.AST_DATA_PASSWORD = node->payload.str;
		break;
	case AST_DATA_TIMESTAMP:
		content->value.AST_DATA_TIMESTAMP = node->payload.uint;
		break;
	case AST_DATA_SECONDS:
		content->value.AST_DATA_SECONDS = node->payload.uint;
		break;
	case AST_DATA_MILLISECONDS:
		content->value.AST_DATA_MILLISECONDS = node->payload.uint;
		break;
	case AST_DATA_CHARACTER:
		content->value.AST_DATA_CHARACTER = node->payload.character;
		break;
	case AST_DATA_INTEGER:
		content->value.AST_DATA_INTEGER = node->payload.sint;
		break;
	case AST_DATA_UNSIGNED_INTEGER:
		content->value.AST_DATA_UNSIGNED_INTEGER = node->payload.uint;
		break;
	case AST_DATA_BOOLEAN:
		content->value.AST_DATA_BOOLEAN = node->payload.boolean;
		break;
	case AST_DATA_IPADDR:
		content->value.AST_DATA_IPADDR = node->payload.ipaddr;
		break;
	case AST_DATA_DOUBLE:
		content->value.AST_DATA_DOUBLE = node->payload.dbl;
		break;
	case AST_DATA_CONTAINER:
		break;
	case AST_DATA_POINTER:
		content->value.AST_DATA_POINTER = node->payload.ptr;
		break;
	}

	return 0;
}

/*!
 * \internal
 * \brief One color for each node type.
 */
static const struct {
	enum ast_data_type type;
	int color;
} data_result_color[] = {
	{ AST_DATA_STRING, COLOR_BLUE },
	{ AST_DATA_PASSWORD, COLOR_BRBLUE },
	{ AST_DATA_TIMESTAMP, COLOR_CYAN },
	{ AST_DATA_SECONDS, COLOR_MAGENTA },
	{ AST_DATA_MILLISECONDS, COLOR_BRMAGENTA },
	{ AST_DATA_CHARACTER, COLOR_GRAY },
	{ AST_DATA_INTEGER, COLOR_RED },
	{ AST_DATA_UNSIGNED_INTEGER, COLOR_RED },
	{ AST_DATA_DOUBLE, COLOR_RED },
	{ AST_DATA_BOOLEAN, COLOR_BRRED },
	{ AST_DATA_CONTAINER, COLOR_GREEN },
	{ AST_DATA_IPADDR, COLOR_BROWN },
	{ AST_DATA_POINTER, COLOR_YELLOW },
};

/*!
 * \internal
 * \brief Get the color configured for a specific node type.
 * \param[in] type The node type.
 * \returns The color specified for the passed type.
 */
static int data_result_get_color(enum ast_data_type type)
{
	int i;
	for (i = 0; i < ARRAY_LEN(data_result_color); i++) {
		if (data_result_color[i].type == type) {
			return data_result_color[i].color;
		}
	}

	return COLOR_BLUE;
}

/*!
 * \internal
 * \brief Print a node to the CLI.
 * \param[in] fd The CLI file descriptor.
 * \param[in] node The node to print.
 * \param[in] depth The actual node depth in the tree.
 */
static void data_result_print_cli_node(int fd, const struct ast_data *node, uint32_t depth)
{
	int i;
	struct ast_str *tabs, *output;

	tabs = ast_str_create(depth * 10 + 1);
	if (!tabs) {
		return;
	}
	ast_str_reset(tabs);
	for (i = 0; i < depth; i++) {
		ast_str_append(&tabs, 0, "  ");
	}

	output = ast_str_create(20);
	if (!output) {
		ast_free(tabs);
		return;
	}

	ast_str_reset(output);
	ast_term_color_code(&output, data_result_get_color(node->type), 0);

	switch (node->type) {
	case AST_DATA_POINTER:
		ast_str_append(&output, 0, "%s%s: %p\n", ast_str_buffer(tabs),
				node->name, node->payload.ptr);
		break;
	case AST_DATA_PASSWORD:
		ast_str_append(&output, 0, "%s%s: \"%s\"\n",
				ast_str_buffer(tabs),
				node->name,
				node->payload.str);
		break;
	case AST_DATA_STRING:
		ast_str_append(&output, 0, "%s%s: \"%s\"\n",
				ast_str_buffer(tabs),
				node->name,
				node->payload.str);
		break;
	case AST_DATA_CHARACTER:
		ast_str_append(&output, 0, "%s%s: \'%c\'\n",
				ast_str_buffer(tabs),
				node->name,
				node->payload.character);
		break;
	case AST_DATA_CONTAINER:
		ast_str_append(&output, 0, "%s%s\n", ast_str_buffer(tabs),
				node->name);
		break;
	case AST_DATA_TIMESTAMP:
		ast_str_append(&output, 0, "%s%s: %u\n", ast_str_buffer(tabs),
				node->name,
				node->payload.uint);
		break;
	case AST_DATA_SECONDS:
		ast_str_append(&output, 0, "%s%s: %u\n", ast_str_buffer(tabs),
				node->name,
				node->payload.uint);
		break;
	case AST_DATA_MILLISECONDS:
		ast_str_append(&output, 0, "%s%s: %u\n", ast_str_buffer(tabs),
				node->name,
				node->payload.uint);
		break;
	case AST_DATA_INTEGER:
		ast_str_append(&output, 0, "%s%s: %d\n", ast_str_buffer(tabs),
				node->name,
				node->payload.sint);
		break;
	case AST_DATA_UNSIGNED_INTEGER:
		ast_str_append(&output, 0, "%s%s: %u\n", ast_str_buffer(tabs),
				node->name,
				node->payload.uint);
		break;
	case AST_DATA_DOUBLE:
		ast_str_append(&output, 0, "%s%s: %lf\n", ast_str_buffer(tabs),
				node->name,
				node->payload.dbl);
		break;
	case AST_DATA_BOOLEAN:
		ast_str_append(&output, 0, "%s%s: %s\n", ast_str_buffer(tabs),
				node->name,
				((node->payload.boolean) ? "True" : "False"));
		break;
	case AST_DATA_IPADDR:
		ast_str_append(&output, 0, "%s%s: %s\n", ast_str_buffer(tabs),
				node->name,
				ast_inet_ntoa(node->payload.ipaddr));
		break;
	}

	ast_free(tabs);

	ast_term_color_code(&output, COLOR_WHITE, 0);

	ast_cli(fd, "%s", ast_str_buffer(output));

	ast_free(output);

	if (node->type == AST_DATA_CONTAINER) {
		__data_result_print_cli(fd, node, depth + 1);
	}
}

/*!
 * \internal
 * \brief Print out an ast_data tree to the CLI.
 * \param[in] fd The CLI file descriptor.
 * \param[in] root The root node of the tree.
 * \param[in] depth Actual depth.
 */

static void __data_result_print_cli(int fd, const struct ast_data *root, uint32_t depth)
{
	struct ao2_iterator iter;
	struct ast_data *node;

	if (root->type == AST_DATA_CONTAINER) {
		iter = ao2_iterator_init(root->children, 0);
		while ((node = ao2_iterator_next(&iter))) {
			data_result_print_cli_node(fd, node, depth + 1);
			ao2_ref(node, -1);
		}
		ao2_iterator_destroy(&iter);
	} else {
		data_result_print_cli_node(fd, root, depth);
	}
}

/*!
 * \internal
 * \brief
 * \param[in] fd The CLI file descriptor.
 * \param[in] root The root node of the tree.
 */
static void data_result_print_cli(int fd, const struct ast_data *root)
{
	struct ast_str *output;

	/* print the initial node. */
	output = ast_str_create(30);
	if (!output) {
		return;
	}

	ast_term_color_code(&output, data_result_get_color(root->type), 0);
	ast_str_append(&output, 0, "%s\n", root->name);
	ast_term_color_code(&output, COLOR_WHITE, 0);
	ast_cli(fd, "%s", ast_str_buffer(output));
	ast_free(output);

	__data_result_print_cli(fd, root, 0);

	ast_cli(fd, "\n");
}

/*!
 * \internal
 * \brief Handle the CLI command "data get".
 */
static char *handle_cli_data_get(struct ast_cli_entry *e, int cmd,
		struct ast_cli_args *a)
{
	struct ast_data_query query = {
		.version = AST_DATA_QUERY_VERSION
	};
	struct ast_data *tree;

	switch (cmd) {
	case CLI_INIT:
		e->command = "data get";
		e->usage = ""
			"Usage: data get <path> [<search> [<filter>]]\n"
			"       Get the tree based on a path.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	query.path = (char *) a->argv[e->args];

	if (a->argc > e->args + 1) {
		query.search = (char *) a->argv[e->args + 1];
	}

	if (a->argc > e->args + 2) {
		query.filter = (char *) a->argv[e->args + 2];
	}

	tree = ast_data_get(&query);
	if (!tree) {
		return CLI_FAILURE;
	}

	data_result_print_cli(a->fd, tree);

	ast_data_free(tree);

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief Print the list of data providers.
 * \param[in] fd The CLI file descriptor.
 * \param[in] name The last node visited name.
 * \param[in] container The childrens of the last node.
 * \param[in] path The path to the current node.
 */
static void data_provider_print_cli(int fd, const char *name,
	struct ao2_container *container, struct ast_str *path)
{
	struct ao2_iterator i;
	struct ast_str *current_path;
	struct data_provider *provider;

	current_path = ast_str_create(60);
	if (!current_path) {
		return;
	}

	ast_str_reset(current_path);
	if (path) {
		ast_str_set(&current_path, 0, "%s/%s", ast_str_buffer(path), name);
	} else {
		ast_str_set(&current_path, 0, "%s", name);
	}

	i = ao2_iterator_init(container, 0);
	while ((provider = ao2_iterator_next(&i))) {
		if (provider->handler) {
			/* terminal node, print it. */
			ast_cli(fd, "%s/%s (", ast_str_buffer(current_path),
				provider->name);
			if (provider->handler->get) {
				ast_cli(fd, "get");
			}
			ast_cli(fd, ") [%s]\n", provider->registrar);
		}
		data_provider_print_cli(fd, provider->name, provider->children,
			current_path);
		ao2_ref(provider, -1);
	}
	ao2_iterator_destroy(&i);

	ast_free(current_path);
}

/*!
 * \internal
 * \brief Handle CLI command "data show providers"
 */
static char *handle_cli_data_show_providers(struct ast_cli_entry *e, int cmd,
		struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "data show providers";
		e->usage = ""
			"Usage: data show providers\n"
			"       Show the list of registered providers\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	data_read_lock();
	data_provider_print_cli(a->fd, "", root_data.container, NULL);
	data_unlock();

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief Data API CLI commands.
 */
static struct ast_cli_entry cli_data[] = {
	AST_CLI_DEFINE(handle_cli_data_get, "Data API get"),
	AST_CLI_DEFINE(handle_cli_data_show_providers, "Show data providers")
};

/*!
 * \internal
 * \brief Output a tree to the AMI.
 * \param[in] s AMI session.
 * \param[in] name The root node name.
 * \param[in] container The root container.
 * \param[in] path The current path.
 */
static void data_result_manager_output(struct mansession *s, const char *name,
	struct ao2_container *container, struct ast_str *path, int id)
{
	struct ao2_iterator i;
	struct ast_str *current_path;
	struct ast_data *node;
	int current_id = id;

	current_path = ast_str_create(60);
	if (!current_path) {
		return;
	}

	ast_str_reset(current_path);
	if (path) {
		ast_str_set(&current_path, 0, "%s.%s", ast_str_buffer(path), name);
	} else {
		ast_str_set(&current_path, 0, "%s", name);
	}

	i = ao2_iterator_init(container, 0);
	while ((node = ao2_iterator_next(&i))) {
		/* terminal node, print it. */
		if (node->type != AST_DATA_CONTAINER) {
			astman_append(s, "%d-%s.%s", id, ast_str_buffer(current_path),
					node->name);
		}
		switch (node->type) {
		case AST_DATA_CONTAINER:
			data_result_manager_output(s, node->name, node->children, current_path, ++current_id);
			break;
		case AST_DATA_INTEGER:
			astman_append(s, ": %d\r\n", node->payload.sint);
			break;
		case AST_DATA_TIMESTAMP:
		case AST_DATA_SECONDS:
		case AST_DATA_MILLISECONDS:
		case AST_DATA_UNSIGNED_INTEGER:
			astman_append(s, ": %u\r\n", node->payload.uint);
			break;
		case AST_DATA_PASSWORD:
			astman_append(s, ": %s\r\n", node->payload.str);
			break;
		case AST_DATA_STRING:
			astman_append(s, ": %s\r\n", node->payload.str);
			break;
		case AST_DATA_CHARACTER:
			astman_append(s, ": %c\r\n", node->payload.character);
			break;
		case AST_DATA_IPADDR:
			astman_append(s, ": %s\r\n", ast_inet_ntoa(node->payload.ipaddr));
			break;
		case AST_DATA_POINTER:
			break;
		case AST_DATA_DOUBLE:
			astman_append(s, ": %f\r\n", node->payload.dbl);
			break;
		case AST_DATA_BOOLEAN:
			astman_append(s, ": %s\r\n",
				(node->payload.boolean ? "True" : "False"));
			break;
		}

		ao2_ref(node, -1);
	}
	ao2_iterator_destroy(&i);

	ast_free(current_path);
}

/*!
 * \internal
 * \brief Implements the manager action: "DataGet".
 */
static int manager_data_get(struct mansession *s, const struct message *m)
{
	const char *path = astman_get_header(m, "Path");
	const char *search = astman_get_header(m, "Search");
	const char *filter = astman_get_header(m, "Filter");
	const char *id = astman_get_header(m, "ActionID");
	struct ast_data *res;
	struct ast_data_query query = {
		.version = AST_DATA_QUERY_VERSION,
		.path = (char *) path,
		.search = (char *) search,
		.filter = (char *) filter,
	};

	if (ast_strlen_zero(path)) {
		astman_send_error(s, m, "'Path' parameter not specified");
		return 0;
	}

	res = ast_data_get(&query);
	if (!res) {
		astman_send_error(s, m, "No data returned");
		return 0;
	}

	astman_append(s, "Event: DataGet Tree\r\n");
	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}
	data_result_manager_output(s, res->name, res->children, NULL, 0);
	astman_append(s, "\r\n");

	ast_data_free(res);

	return RESULT_SUCCESS;
}

int ast_data_add_codec(struct ast_data *root, const char *node_name, struct ast_format *format)
{
	struct ast_data *codecs, *codec;
	size_t fmlist_size;
	const struct ast_format_list *fmlist;
	int x;

	codecs = ast_data_add_node(root, node_name);
	if (!codecs) {
		return -1;
	}
	fmlist = ast_format_list_get(&fmlist_size);
	for (x = 0; x < fmlist_size; x++) {
		if (ast_format_cmp(&fmlist[x].format, format) == AST_FORMAT_CMP_EQUAL) {
			codec = ast_data_add_node(codecs, "codec");
			if (!codec) {
				ast_format_list_destroy(fmlist);
				return -1;
			}
			ast_data_add_str(codec, "name", fmlist[x].name);
			ast_data_add_int(codec, "samplespersecond", fmlist[x].samplespersecond);
			ast_data_add_str(codec, "description", fmlist[x].desc);
			ast_data_add_int(codec, "frame_length", fmlist[x].fr_len);
		}
	}
	ast_format_list_destroy(fmlist);

	return 0;
}

int ast_data_add_codecs(struct ast_data *root, const char *node_name, struct ast_format_cap *cap)
{
	struct ast_data *codecs, *codec;
	size_t fmlist_size;
	const struct ast_format_list *fmlist;
	int x;

	codecs = ast_data_add_node(root, node_name);
	if (!codecs) {
		return -1;
	}
	fmlist = ast_format_list_get(&fmlist_size);
	for (x = 0; x < fmlist_size; x++) {
		if (ast_format_cap_iscompatible(cap, &fmlist[x].format)) {
			codec = ast_data_add_node(codecs, "codec");
			if (!codec) {
				ast_format_list_destroy(fmlist);
				return -1;
			}
			ast_data_add_str(codec, "name", fmlist[x].name);
			ast_data_add_int(codec, "samplespersecond", fmlist[x].samplespersecond);
			ast_data_add_str(codec, "description", fmlist[x].desc);
			ast_data_add_int(codec, "frame_length", fmlist[x].fr_len);
		}
	}
	ast_format_list_destroy(fmlist);

	return 0;
}

#ifdef TEST_FRAMEWORK

/*!
 * \internal
 * \brief Structure used to test how to add a complete structure,
 *        and how to compare it.
 */
struct test_structure {
	int a_int;
	unsigned int b_bool:1;
	char *c_str;
	unsigned int a_uint;
};

/*!
 * \internal
 * \brief test_structure mapping.
 */
#define DATA_EXPORT_TEST_STRUCTURE(MEMBER)                              \
	MEMBER(test_structure, a_int, AST_DATA_INTEGER)                 \
	MEMBER(test_structure, b_bool, AST_DATA_BOOLEAN)                \
	MEMBER(test_structure, c_str, AST_DATA_STRING)                  \
	MEMBER(test_structure, a_uint, AST_DATA_UNSIGNED_INTEGER)

AST_DATA_STRUCTURE(test_structure, DATA_EXPORT_TEST_STRUCTURE);

/*!
 * \internal
 * \brief Callback implementation.
 */
static int test_data_full_provider(const struct ast_data_search *search,
		struct ast_data *root)
{
	struct ast_data *test_structure;
	struct test_structure local_test_structure = {
		.a_int = 10,
		.b_bool = 1,
		.c_str = "test string",
		.a_uint = 20
	};

	test_structure = ast_data_add_node(root, "test_structure");
	if (!test_structure) {
		ast_debug(1, "Internal data api error\n");
		return 0;
	}

	/* add the complete structure. */
	ast_data_add_structure(test_structure, test_structure, &local_test_structure);

	if (!ast_data_search_match(search, test_structure)) {
		ast_data_remove_node(root, test_structure);
	}

	return 0;
}

/*!
 * \internal
 * \brief Handler definition for the full provider.
 */
static const struct ast_data_handler full_provider = {
	.version = AST_DATA_HANDLER_VERSION,
	.get = test_data_full_provider
};

/*!
 * \internal
 * \brief Structure used to define multiple providers at once.
 */
static const struct ast_data_entry test_providers[] = {
	AST_DATA_ENTRY("test/node1/node11/node111", &full_provider)
};

AST_TEST_DEFINE(test_data_get)
{
	struct ast_data *res, *node;
	struct ast_data_iterator *i;
	struct ast_data_query query = {
		.version = AST_DATA_QUERY_VERSION,
		.path = "test/node1/node11/node111",
		.search = "node111/test_structure/a_int=10",
		.filter = "node111/test_structure/a*int"
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "data_test";
		info->category = "/main/data/";
		info->summary = "Data API unit test";
		info->description =
			"Tests whether data API get implementation works as expected.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_data_register_multiple_core(test_providers, ARRAY_LEN(test_providers));

	res = ast_data_get(&query);
	if (!res) {
		ast_test_status_update(test, "Unable to get tree.");
		ast_data_unregister("test/node1/node11/node111");
		return AST_TEST_FAIL;
	}

	/* initiate the iterator and check for errors. */
	i = ast_data_iterator_init(res, "test_structure/");
	if (!i) {
		ast_test_status_update(test, "Unable to initiate the iterator.");
		ast_data_free(res);
		ast_data_unregister("test/node1/node11/node111");
		return AST_TEST_FAIL;
	}

	/* walk the returned nodes. */
	while ((node = ast_data_iterator_next(i))) {
		if (!strcmp(ast_data_retrieve_name(node), "a_int")) {
			if (ast_data_retrieve_int(node, "/") != 10) {
				ast_data_iterator_end(i);
				ast_data_free(res);
				ast_data_unregister("test/node1/node11/node111");
				return AST_TEST_FAIL;
			}
		} else if (!strcmp(ast_data_retrieve_name(node), "a_uint")) {
			if (ast_data_retrieve_uint(node, "/") != 20) {
				ast_data_iterator_end(i);
				ast_data_free(res);
				ast_data_unregister("test/node1/node11/node111");
				return AST_TEST_FAIL;
			}
		}
	}

	/* finish the iterator. */
	ast_data_iterator_end(i);

	ast_data_free(res);

	ast_data_unregister("test/node1/node11/node111");

	return AST_TEST_PASS;
}

#endif

/*! \internal \brief Clean up resources on Asterisk shutdown */
static void data_shutdown(void)
{
	ast_manager_unregister("DataGet");
	ast_cli_unregister_multiple(cli_data, ARRAY_LEN(cli_data));
	ao2_t_ref(root_data.container, -1, "Unref root_data.container in data_shutdown");
	root_data.container = NULL;
	ast_rwlock_destroy(&root_data.lock);
	AST_TEST_UNREGISTER(test_data_get);
}

int ast_data_init(void)
{
	int res = 0;

	ast_rwlock_init(&root_data.lock);

	if (!(root_data.container = ao2_container_alloc(NUM_DATA_NODE_BUCKETS,
		data_provider_hash, data_provider_cmp))) {
		return -1;
	}

	res |= ast_cli_register_multiple(cli_data, ARRAY_LEN(cli_data));

	res |= ast_manager_register_xml_core("DataGet", 0, manager_data_get);

	AST_TEST_REGISTER(test_data_get);

	ast_register_cleanup(data_shutdown);

	return res;
}
