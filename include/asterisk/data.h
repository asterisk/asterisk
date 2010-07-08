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

/*!
 * \file
 * \brief Data retrieval API.
 * \author Brett Bryant <brettbryant@gmail.com>
 * \author Eliel C. Sardanons (LU1ALY) <eliels@gmail.com>
 * \arg \ref AstDataRetrieval
 */

#ifndef ASTERISK_DATA_H
#define ASTERISK_DATA_H

#include "asterisk/frame.h"

/*!
 * \page AstDataRetrieval The Asterisk DATA retrieval API.
 *
 * This module implements an abstraction for retrieving asterisk data and
 * export it.
 *
 * \section USAGE
 *
 * \subsection Provider
 *
 * \b Register
 *
 * To register a callback use:
 *
 * \code
 *	static const struct ast_data_handler callback_handler = {
 *		.get = callback_handler_get_function,
 *	};
 *
 *	ast_data_register("/node/path", &callback_handler);
 * \endcode
 *
 * If you instead want to register multiple nodes at once use:
 * \code
 *	static const struct ast_data_handler handler_struct1 = {
 *		.get = handler_callback_read,
 *	};
 *	... other handlers ...
 *
 *	static const struct ast_data_entry list_providers[] = {
 *		AST_DATA_ENTRY("/path1/node1", &handler_struct1),
 *		AST_DATA_ENTRY("/path2/node2", &handler_struct2),
 *		AST_DATA_ENTRY("/path3/node3", &handler_struct3),
 *	};
 *
 *      ...
 *
 *	ast_data_register_multiple(list_providers, ARRAY_LEN(list_providers));
 * \endcode
 *
 * \b Unregister
 *
 * To unregister a callback function already registered you can just call:
 *
 * \code
 *	ast_data_unregister(NULL);
 * \endcode
 * And every node registered by the current module (file) will be unregistered.
 * If you want to unregister a specific node use:
 *
 * \code
 *	ast_data_unregister("/node/path");
 * \endcode
 *
 * \b Implementation
 *
 * A simple callback function implementation:
 *
 * \code
 *	#include <data.h>
 *
 *	struct test_structure {
 *		int a;
 *		double b;
 *	};
 *
 *	DATA_EXPORT_TEST_STRUCTURE(MEMBER)			\
 *		MEMBER(test_structure, a, AST_DATA_INTEGER)	\
 *		MEMBER(test_structure, b, AST_DATA_DOUBLE)
 *
 *	AST_DATA_STRUCTURE(test_structure, DATA_EXPORT_TEST_STRUCTURE)
 *
 *	static int my_callback_function(struct ast_data_search *search,
 *		struct ast_data *root_node)
 *	{
 *		struct ast_data *internal_node;
 *		struct test_structure ts = {
 *			.a = 10,
 *			.b = 20
 *		};
 *
 *		internal_node = ast_data_add_node(root_node, "test_node");
 *		if (!internal_node) {
 *			return -1;
 *		}
 *
 *		ast_data_add_structure(test_structure, internal_node, ts);
 *
 *		if (!ast_data_search_match(search, internal_node)) {
 *			ast_data_remove_node(root_node, internal_node);
 *		}
 *
 *		return 0;
 *	}
 *
 * \endcode
 *
 * \subsection Get
 *
 * \b Getting \b the \b tree
 *
 * To get the tree you need to create a query, a query is based on three parameters
 * a \b path to the provider, a \b search condition and a \b filter condition.
 * \code
 *	struct ast_data *result;
 *	struct ast_data_query query = {
 *		.path = "/asterisk/application/app_queue/queues",
 *		.search = "/queues/queue/name=queue1",
 *		.filter = "/queues/queue/name|wrapuptime|members/member/interface"
 *	};
 *
 *	result = ast_data_get(&query);
 * \endcode
 *
 * After using it you need to release the allocated memory of the returned tree:
 * \code
 *	ast_data_free(result);
 * \endcode
 *
 * \b Iterate
 *
 * To retrieve nodes from the tree, it is possible to iterate through the returned
 * nodes of the tree using:
 * \code
 *	struct ast_data_iterator *i;
 *	struct ast_data *internal_node;
 *
 *	i = ast_data_iterator_init(result_tree, "path/node_name");
 *	while ((internal_node = ast_data_iterator_next(i))) {
 *		... do something with node ...
 *	}
 *	ast_data_iterator_end(i);
 * \endcode
 * node_name is the name of the nodes to retrieve and path is the path to the internal
 * nodes to retrieve (if needed).
 *
 * \b Retrieving
 *
 * After getting the node you where searching for, you will need to retrieve its value,
 * to do that you may use one of the ast_data_retrieve_##type functions:
 * \code
 *	int a = ast_data_retrieve_int(tree, "path/to/the/node");
 *	double b = ast_data_retrieve_dbl(tree, "path/to/the/node");
 *	unsigned int c = ast_data_retrieve_bool(tree, "path/to/the/node");
 *	char *d = ast_data_retrieve_string(tree, "path/to/the/node");
 *	struct sockaddr_in e = ast_data_retrieve_ipaddr(tree, "path/to/the/node");
 *	unsigned int f = ast_data_retrieve_uint(tree, "path/to/the/node");
 *	void *g = ast_data_retrieve_ptr(tree, "path/to/the/node");
 * \endcode
 *
 */

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief The data type of the data node. */
enum ast_data_type {
	AST_DATA_CONTAINER,
	AST_DATA_INTEGER,
	AST_DATA_UNSIGNED_INTEGER,
	AST_DATA_DOUBLE,
	AST_DATA_BOOLEAN,
	AST_DATA_STRING,
	AST_DATA_CHARACTER,
	AST_DATA_PASSWORD,
	AST_DATA_IPADDR,
	AST_DATA_TIMESTAMP,
	AST_DATA_SECONDS,
	AST_DATA_MILLISECONDS,
	AST_DATA_POINTER
};

/*! \brief The Data API structures version. */
#define AST_DATA_HANDLER_VERSION 1
#define AST_DATA_QUERY_VERSION	 1

/*! \brief opaque definition of an ast_data handler, a tree node. */
struct ast_data;

/*! \brief opaque definition of an ast_data_iterator handler. */
struct ast_data_iterator;

/*! \brief opaque definition of an ast_data_search structure. */
struct ast_data_search;

/*! \brief structure retrieved from a node, with the nodes content. */
struct ast_data_retrieve {
	/*! \brief The type of the node retrieved. */
	enum ast_data_type type;

	union {
		char AST_DATA_CHARACTER;
		char *AST_DATA_STRING;
		char *AST_DATA_PASSWORD;
		int AST_DATA_INTEGER;
		unsigned int AST_DATA_TIMESTAMP;
		unsigned int AST_DATA_SECONDS;
		unsigned int AST_DATA_MILLISECONDS;
		double AST_DATA_DOUBLE;
		unsigned int AST_DATA_UNSIGNED_INTEGER;
		unsigned int AST_DATA_BOOLEAN;
		void *AST_DATA_POINTER;
		struct in_addr AST_DATA_IPADDR;
		void *AST_DATA_CONTAINER;
	} value;
};

/*!
 * \brief The get callback definition.
 */
typedef int (*ast_data_get_cb)(const struct ast_data_search *search,
	struct ast_data *root);

/*! \brief The structure of the node handler. */
struct ast_data_handler {
	/*! \brief Structure version. */
	uint32_t version;
	/*! \brief Data get callback implementation. */
	ast_data_get_cb get;
};

/*! \brief This entries are for multiple registers. */
struct ast_data_entry {
	/*! \brief Path of the node to register. */
	const char *path;
	/*! \brief Data handler structure. */
	const struct ast_data_handler *handler;
};

#define AST_DATA_ENTRY(__path, __handler) { .path = __path, .handler = __handler }

/*! \brief A query to the data API is specified in this structure. */
struct ast_data_query {
	/*! \brief Data query version. */
	uint32_t version;
	/*! \brief Path to the node to retrieve. */
	char *path;
	/*! \brief Filter string, return the internal nodes specified here.
	 *         Setting it to NULL will return every internal node. */
	char *filter;
	/*! \brief Search condition. */
	char *search;
};

/*! \brief Map the members of a structure. */
struct ast_data_mapping_structure {
	/*! \brief structure member name. */
	const char *name;
	/*! \brief structure member type. */
	enum ast_data_type type;
	/*! \brief member getter. */
	union {
		char (*AST_DATA_CHARACTER)(void *ptr);
		char *(*AST_DATA_STRING)(void *ptr);
		char *(*AST_DATA_PASSWORD)(void *ptr);
		int (*AST_DATA_INTEGER)(void *ptr);
		int (*AST_DATA_TIMESTAMP)(void *ptr);
		int (*AST_DATA_SECONDS)(void *ptr);
		int (*AST_DATA_MILLISECONDS)(void *ptr);
		double (*AST_DATA_DOUBLE)(void *ptr);
		unsigned int (*AST_DATA_UNSIGNED_INTEGER)(void *ptr);
		unsigned int (*AST_DATA_BOOLEAN)(void *ptr);
		void *(*AST_DATA_POINTER)(void *ptr);
		struct in_addr (*AST_DATA_IPADDR)(void *ptr);
		void *(*AST_DATA_CONTAINER)(void *ptr);
	} get;
};

/* Generate the structure and the functions to access the members of a structure. */
#define AST_DATA_STRUCTURE(__struct, __name)								\
	__name(__AST_DATA_MAPPING_FUNCTION);								\
	static const struct ast_data_mapping_structure __data_mapping_structure_##__struct[] = {	\
		__name(__AST_DATA_MAPPING_STRUCTURE)							\
	}

/* Generate the structure to access the members and setup the pointer of the getter. */
#define __AST_DATA_MAPPING_STRUCTURE(__structure, __member, __type)				\
	{ .name = #__member, .get.__type = data_mapping_structure_get_##__structure##__member,	\
	.type = __type },

/* based on the data type, specifify the type of return value for the getter function. */
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_PASSWORD(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_PASSWORD, char *)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_STRING(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_STRING, char *)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_CHARACTER(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_CHARACTER, char)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_INTEGER(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_INTEGER, int)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_TIMESTAMP(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_INTEGER, int)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_SECONDS(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_INTEGER, int)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_MILLISECONDS(__structure, __member)			\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_INTEGER, int)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_UNSIGNED_INTEGER(__structure, __member)			\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_UNSIGNED_INTEGER, unsigned int)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_BOOLEAN(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_BOOLEAN, unsigned int)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_POINTER(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_POINTER, void *)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_IPADDR(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_IPADDR, struct in_addr)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_DOUBLE(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_DBL, double)
#define __AST_DATA_MAPPING_FUNCTION_AST_DATA_CONTAINER(__structure, __member)				\
	__AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, AST_DATA_CONTAINER, void *)

#define __AST_DATA_MAPPING_FUNCTION(__structure, __member, __type)		\
	__AST_DATA_MAPPING_FUNCTION_##__type(__structure, __member)

/* Create the function to retrieve a member of the structure. */
#define __AST_DATA_MAPPING_FUNCTION_TYPE(__structure, __member, __type, __real_type)		\
	static __real_type data_mapping_structure_get_##__structure##__member(void *ptr) {	\
		struct __structure *struct_##__member = (struct __structure *) ptr;		\
		return (__real_type) struct_##__member->__member;				\
	}

/*!
 * \brief Register a data provider.
 * \param[in] path The path of the node to register.
 * \param[in] handler The structure defining this node handler.
 * \param[in] registrar Who is registering this node.
 * \param[in] mod The module registering this handler.
 * \see ast_data_unregister
 * \retval <0 on error.
 * \retval 0 on success.
 * \see __ast_data_unregister, __ast_data_register_multiple
 */
int __ast_data_register(const char *path, const struct ast_data_handler *handler,
	const char *registrar, struct ast_module *mod);
#define ast_data_register(path, handler) __ast_data_register(path, handler, __FILE__, ast_module_info->self)
#define ast_data_register_core(path, handler) __ast_data_register(path, handler, __FILE__, NULL)

/*!
 * \brief Register multiple data providers at once.
 * \param[in] data_entries An array of data_entries structures.
 * \param[in] entries The number of entries in the data_entries array.
 * \param[in] registrar Who is registering this nodes.
 * \param[in] mod The module registering this handlers.
 * \retval <0 on error (none of the nodes are being registered on error).
 * \retval 0 on success.
 * \see __ast_data_register, __ast_data_unregister
 */
int __ast_data_register_multiple(const struct ast_data_entry *data_entries,
	size_t entries, const char *registrar, struct ast_module *mod);
#define ast_data_register_multiple(data_entries, entries) \
	__ast_data_register_multiple(data_entries, entries, __FILE__, ast_module_info->self)
#define ast_data_register_multiple_core(data_entries, entries) \
	__ast_data_register_multiple(data_entries, entries, __FILE__, NULL)

/*!
 * \brief Unregister a data provider.
 * \param[in] path Which node to unregister, if path is NULL unregister every node
 *                 registered by the passed 'registrar'.
 * \param[in] registrar Who is trying to unregister this node, only the owner (the
 *                      one who registered the node) will be able to unregister it.
 * \see ast_data_register
 * \retval <0 on error.
 * \retval 0 on success.
 * \see __ast_data_register, __ast_data_register_multiple
 */
int __ast_data_unregister(const char *path, const char *registrar);
#define ast_data_unregister(path) __ast_data_unregister(path, __FILE__)

/*!
 * \brief Check the current generated node to know if it matches the search
 *        condition.
 * \param[in] search The search condition.
 * \param[in] data The AstData node generated.
 * \return 1 If the "data" node matches the search condition.
 * \return 0 If the "data" node does not matches the search condition.
 * \see ast_data_remove_node
 */
int ast_data_search_match(const struct ast_data_search *search, struct ast_data *data);

/*!
 * \brief Based on a search tree, evaluate every member of a structure against it.
 * \param[in] search The search tree.
 * \param[in] mapping The structure mapping.
 * \param[in] mapping_len The lenght of the structure mapping.
 * \param[in] structure The structure pointer.
 * \param[in] structure_name The name of the structure to compare.
 * \retval 0 If the structure matches.
 * \retval 1 If the structure doesn't match.
 */
int __ast_data_search_cmp_structure(const struct ast_data_search *search,
	const struct ast_data_mapping_structure *mapping, size_t mapping_len,
	void *structure, const char *structure_name);
#define ast_data_search_cmp_structure(search, structure_name, structure, structure_name_cmp)		\
	__ast_data_search_cmp_structure(search, __data_mapping_structure_##structure_name,		\
	ARRAY_LEN(__data_mapping_structure_##structure_name), structure, structure_name_cmp)

/*!
 * \brief Retrieve a subtree from the asterisk data API.
 * \param[in] query The query structure specifying what nodes to retrieve.
 * \retval NULL on error.
 * \retval non-NULL The dynamically allocated requested sub-tree (it needs to be
 *         released using ast_data_free.
 * \see ast_data_free, ast_data_get_xml
 */
struct ast_data *ast_data_get(const struct ast_data_query *query);

#ifdef HAVE_LIBXML2
/*!
 * \brief Retrieve a subtree from the asterisk data API in XML format..
 * \param[in] query The query structure specifying what nodes to retrieve.
 * \retval NULL on error.
 * \retval non-NULL The dynamically allocated requested sub-tree (it needs to be
 *         released using ast_data_free.
 * \see ast_data_free, ast_data_get
 */
struct ast_xml_doc *ast_data_get_xml(const struct ast_data_query *query);
#endif

/*!
 * \brief Release the allocated memory of a tree.
 * \param[in] root The sub-tree pointer returned by a call to ast_data_get.
 * \see ast_data_get
 */
void ast_data_free(struct ast_data *root);

/*!
 * \brief Get a node type.
 * \param[in] res A pointer to the ast_data result set.
 * \param[in] path A path to the node to get the type.
 * \return The type of the requested node type.
 */
enum ast_data_type ast_data_retrieve_type(struct ast_data *res, const char *path);

/*!
 * \brief Get the node name.
 * \param[in] node The node pointer.
 * \returns The node name.
 */
char *ast_data_retrieve_name(struct ast_data *node);

/*!
 * \brief Add a container child.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_node(struct ast_data *root, const char *childname);

/*!
 * \brief Add an integer node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] value The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_int(struct ast_data *root, const char *childname,
	int value);

/*!
 * \brief Add a char node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] value The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_char(struct ast_data *root, const char *childname,
	char value);

/*!
 * \brief Add an unsigned integer node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] value The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_uint(struct ast_data *root, const char *childname,
	unsigned int value);

/*!
 * \brief Add a floating point node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] dbl The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_dbl(struct ast_data *root, const char *childname,
	double dbl);
/*!
 * \brief Add a ipv4 address type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] addr The ipv4 address value.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_ipaddr(struct ast_data *root, const char *childname,
	struct in_addr addr);

/*!
 * \brief Add a ptr node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] ptr The pointer value to add.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_ptr(struct ast_data *root, const char *childname,
	void *ptr);

/*!
 * \brief Add a password node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] string The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_password(struct ast_data *root, const char *childname,
	const char *string);

/*!
 * \brief Add a timestamp node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] timestamp The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_timestamp(struct ast_data *root, const char *childname,
	unsigned int timestamp);

/*!
 * \brief Add a seconds node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] seconds The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_seconds(struct ast_data *root, const char *childname,
	unsigned int seconds);

/*!
 * \brief Add a milliseconds node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] milliseconds The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_milliseconds(struct ast_data *root, const char *childname,
	unsigned int milliseconds);

/*!
 * \brief Add a string node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] string The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_str(struct ast_data *root, const char *childname,
	const char *string);

/*!
 * \brief Add a boolean node type.
 * \param[in] root The root of the ast_data to insert into.
 * \param[in] childname The name of the child element to be added.
 * \param[in] boolean The value for the new node.
 * \retval NULL on error (memory exhaustion only).
 * \retval non-NULL a newly allocated node.
 */
struct ast_data *ast_data_add_bool(struct ast_data *root, const char *childname,
	unsigned int boolean);

/*!
 * \brief Add a complete structure to a node.
 * \param[in] root Where to add the structure.
 * \param[in] mapping The structure mapping array.
 * \param[in] mapping_len The lenght of the mapping array.
 * \param[in] structure The structure pointer.
 * \retval 0 on success.
 * \retval 1 on error.
 */
int __ast_data_add_structure(struct ast_data *root,
	const struct ast_data_mapping_structure *mapping,
	size_t mapping_len, void *structure);
#define ast_data_add_structure(structure_name, root, structure)				\
	__ast_data_add_structure(root, __data_mapping_structure_##structure_name,	\
		ARRAY_LEN(__data_mapping_structure_##structure_name), structure)

/*!
 * \brief Remove a node that was added using ast_data_add_
 * \param[in] root The root node of the node to be removed.
 * \param[in] child The node pointer to remove.
 */
void ast_data_remove_node(struct ast_data *root, struct ast_data *child);

/*!
 * \brief Initialize an iterator.
 * \param[in] tree The returned tree by a call to ast_data_get.
 * \param[in] elements Which elements to iterate through.
 * \retval NULL on error.
 * \retval non-NULL A dinamically allocated iterator structure.
 */
struct ast_data_iterator *ast_data_iterator_init(struct ast_data *tree,
	const char *elements);

/*!
 * \brief Release (stop using) an iterator.
 * \param[in] iterator The iterator created by ast_data_iterator_start.
 * \see ast_data_iterator_start
 */
void ast_data_iterator_end(struct ast_data_iterator *iterator);

/*!
 * \brief Get the next node of the tree.
 * \param[in] iterator The iterator structure returned by ast_data_iterator_start.
 * \retval NULL when no more nodes to return.
 * \retval non-NULL A node of the ast_data tree.
 * \see ast_data_iterator_start, ast_data_iterator_stop
 */
struct ast_data *ast_data_iterator_next(struct ast_data_iterator *iterator);

/*!
 * \brief Retrieve a value from a node in the tree.
 * \param[in] tree The structure returned by a call to ast_data_get.
 * \param[in] path The path to the node.
 * \param[out] content The node content.
 * \retval 0 on success.
 * \retval <0 on error.
 */
int ast_data_retrieve(struct ast_data *tree, const char *path, struct ast_data_retrieve *content);

/*!
 * \brief Retrieve the integer value of a node.
 * \param[in] tree The tree from where to get the value.
 * \param[in] path The node name or path.
 * \returns The value of the node.
 */
static inline int ast_data_retrieve_int(struct ast_data *tree, const char *path)
{
	struct ast_data_retrieve ret;

	ast_data_retrieve(tree, path, &ret);

	return ret.value.AST_DATA_INTEGER;
}

/*!
 * \brief Retrieve the character value of a node.
 * \param[in] tree The tree from where to get the value.
 * \param[in] path The node name or path.
 * \returns The value of the node.
 */
static inline char ast_data_retrieve_char(struct ast_data *tree, const char *path)
{
	struct ast_data_retrieve ret;

	ast_data_retrieve(tree, path, &ret);

	return ret.value.AST_DATA_CHARACTER;
}

/*!
 * \brief Retrieve the boolean value of a node.
 * \param[in] tree The tree from where to get the value.
 * \param[in] path The node name or path.
 * \returns The value of the node.
 */
static inline unsigned int ast_data_retrieve_bool(struct ast_data *tree, const char *path)
{
	struct ast_data_retrieve ret;

	ast_data_retrieve(tree, path, &ret);

	return ret.value.AST_DATA_BOOLEAN;
}

/*!
 * \brief Retrieve the unsigned integer value of a node.
 * \param[in] tree The tree from where to get the value.
 * \param[in] path The node name or path.
 * \returns The value of the node.
 */
static inline unsigned int ast_data_retrieve_uint(struct ast_data *tree, const char *path)
{
	struct ast_data_retrieve ret;

	ast_data_retrieve(tree, path, &ret);

	return ret.value.AST_DATA_UNSIGNED_INTEGER;
}

/*!
 * \brief Retrieve the password value of a node.
 * \param[in] tree The tree from where to get the value.
 * \param[in] path The node name or path.
 * \returns The value of the node.
 */
static inline const char *ast_data_retrieve_password(struct ast_data *tree, const char *path)
{
	struct ast_data_retrieve ret;

	ast_data_retrieve(tree, path, &ret);

	return ret.value.AST_DATA_PASSWORD;
}

/*!
 * \brief Retrieve the string value of a node.
 * \param[in] tree The tree from where to get the value.
 * \param[in] path The node name or path.
 * \returns The value of the node.
 */
static inline const char *ast_data_retrieve_string(struct ast_data *tree, const char *path)
{
	struct ast_data_retrieve ret;

	ast_data_retrieve(tree, path, &ret);

	return ret.value.AST_DATA_STRING;
}

/*!
 * \brief Retrieve the ptr value of a node.
 * \param[in] tree The tree from where to get the value.
 * \param[in] path The node name or path.
 * \returns The value of the node.
 */
static inline void *ast_data_retrieve_ptr(struct ast_data *tree, const char *path)
{
	struct ast_data_retrieve ret;

	ast_data_retrieve(tree, path, &ret);

	return ret.value.AST_DATA_POINTER;
}

/*!
 * \brief Retrieve the double value of a node.
 * \param[in] tree The tree from where to get the value.
 * \param[in] path The node name or path.
 * \returns The value of the node.
 */
static inline double ast_data_retrieve_dbl(struct ast_data *tree, const char *path)
{
	struct ast_data_retrieve ret;

	ast_data_retrieve(tree, path, &ret);

	return ret.value.AST_DATA_DOUBLE;
}

/*!
 * \brief Retrieve the ipv4 address value of a node.
 * \param[in] tree The tree from where to get the value.
 * \param[in] path The node name or path.
 * \returns The value of the node.
 */
static inline struct in_addr ast_data_retrieve_ipaddr(struct ast_data *tree, const char *path)
{
	struct ast_data_retrieve ret;

	ast_data_retrieve(tree, path, &ret);

	return ret.value.AST_DATA_IPADDR;
}

/*!
 * \brief Add the list of codecs in the root node based on the capability parameter.
 * \param[in] root The astdata root node where to add the codecs node.
 * \param[in] node_name The name of the node where we are going to add the list of
 *                      codecs.
 * \param[in] capability The codecs allowed.
 * \return < 0 on error.
 * \return 0 on success.
 */
int ast_data_add_codecs(struct ast_data *root, const char *node_name, format_t capability);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* ASTERISK_DATA_H */
