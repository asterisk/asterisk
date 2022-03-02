/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Eliel C. Sardanons (LU1ALY) <eliels@gmail.com>
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

#ifndef _ASTERISK_XML_H
#define _ASTERISK_XML_H

/*! \file
 * \brief Asterisk XML abstraction layer
 */

#include "asterisk/vector.h"

struct ast_xml_node;
struct ast_xml_doc;
struct ast_xml_xpath_results;
struct ast_xslt_doc;

/*!
 * \brief Initialize the XML library implementation.
 *         This function is used to setup everything needed
 *         to start working with the xml implementation.
 * \retval 0 On success.
 * \retval 1 On error.
 */
int ast_xml_init(void);

/*!
 * \brief Cleanup library allocated global data.
 * \retval 0 On success.
 * \retval 1 On error.
 */
int ast_xml_finish(void);

/*!
 * \brief Open an XML document.
 * \param filename Document path.
 * \retval NULL on error.
 * \return The ast_xml_doc reference to the open document.
 */
struct ast_xml_doc *ast_xml_open(char *filename);

/*!
 * \brief Create a XML document.
 * \retval NULL on error.
 * \retval non-NULL The allocated document structure.
 */
struct ast_xml_doc *ast_xml_new(void);

/*!
 * \brief Create a XML node.
 * \param name The name of the node to be created.
 * \retval NULL on error.
 * \retval non-NULL The allocated node structe.
 */
struct ast_xml_node *ast_xml_new_node(const char *name);

/*!
 * \brief Add a child node inside a passed parent node.
 * \param parent The pointer of the parent node.
 * \param child_name The name of the child node to add.
 * \retval NULL on error.
 * \retval non-NULL The created child node pointer.
 */
struct ast_xml_node *ast_xml_new_child(struct ast_xml_node *parent, const char *child_name);

/*!
 * \brief Add a child node, to a specified parent node.
 * \param parent Where to add the child node.
 * \param child The child node to add.
 * \retval NULL on error.
 * \retval non-NULL The add child node on success.
 */
struct ast_xml_node *ast_xml_add_child(struct ast_xml_node *parent, struct ast_xml_node *child);

/*!
 * \brief Add a list of child nodes, to a specified parent node.
 * \param parent Where to add the child node.
 * \param child The child list to add.
 * \retval NULL on error.
 * \retval non-NULL The added child list on success.
 */
struct ast_xml_node *ast_xml_add_child_list(struct ast_xml_node *parent, struct ast_xml_node *child);

/*!
 * \brief Create a copy of a n ode list.
 * \param list The list to copy.
 * \retval NULL on error.
 * \retval non-NULL The copied list.
 */
struct ast_xml_node *ast_xml_copy_node_list(struct ast_xml_node *list);

/*!
 * \brief Close an already open document and free the used
 *        structure.
 * \param doc XML Document to close
 */
void ast_xml_close(struct ast_xml_doc *doc);

/*! \brief Open an XML document that resides in memory.
 * \param buffer The address where the document is stored
 * \param size The number of bytes in the document
 * \retval NULL on error.
 * \return The ast_xml_doc reference to the open document.
 */
struct ast_xml_doc *ast_xml_read_memory(char *buffer, size_t size);

/*!
 * \brief Specify the root node of a XML document.
 * \param doc XML Document reference
 * \param node A pointer to the node we want to set as root node.
 */
void ast_xml_set_root(struct ast_xml_doc *doc, struct ast_xml_node *node);

/*!
 * \brief Get the document root node.
 * \param doc XML Document reference
 * \retval NULL on error
 * \return The root node on success.
 */
struct ast_xml_node *ast_xml_get_root(struct ast_xml_doc *doc);

/*!
 * \brief Free node
 * \param node Node to be released.
 */
void ast_xml_free_node(struct ast_xml_node *node);

/*!
 * \brief Free an attribute returned by ast_xml_get_attribute()
 * \param attribute pointer to be freed.
 */
void ast_xml_free_attr(const char *attribute);

/*!
 * \brief Get the document based on a node.
 * \param node A node that is part of the dom.
 * \return The dom pointer where this node resides.
 */
struct ast_xml_doc *ast_xml_get_doc(struct ast_xml_node *node);

/*!
 * \brief Free a content element that was returned by ast_xml_get_text()
 * \param text text to be freed.
 */
void ast_xml_free_text(const char *text);

/*!
 * \brief Get a node attribute by name
 * \param node Node where to search the attribute.
 * \param attrname Attribute name.
 * \retval NULL on error
 * \return The attribute value on success.
 */
const char *ast_xml_get_attribute(struct ast_xml_node *node, const char *attrname);

/*!
 * \brief Set an attribute to a node.
 * \param node In which node we want to insert the attribute.
 * \param name The attribute name.
 * \param value The attribute value.
 * \retval 0 on success.
 * \retval -1 on error.
 */
int ast_xml_set_attribute(struct ast_xml_node *node, const char *name, const char *value);

/*!
 * \brief Find a node element by name.
 * \param root_node This is the node starting point.
 * \param name Node name to find.
 * \param attrname attribute name to match (if NULL it won't be matched).
 * \param attrvalue attribute value to match (if NULL it won't be matched).
 * \retval NULL if not found.
 * \return The node on success.
 */
struct ast_xml_node *ast_xml_find_element(struct ast_xml_node *root_node, const char *name, const char *attrname, const char *attrvalue);
struct ast_xml_ns *ast_xml_find_namespace(struct ast_xml_doc *doc, struct ast_xml_node *node, const char *ns_name);

/*!
 * \brief Get the prefix of a namespace.
 * \param ns The namespace
 * \return The prefix of the namespace.
 */
const char *ast_xml_get_ns_prefix(struct ast_xml_ns *ns);

/*!
 * \brief Get the href of a namespace.
 * \param ns The namespace
 * \return The href of the namespace.
 */
const char *ast_xml_get_ns_href(struct ast_xml_ns *ns);

/*!
 * \brief Get an element content string.
 * \param node Node from where to get the string.
 * \retval NULL on error.
 * \return The text content of node.
 */
const char *ast_xml_get_text(struct ast_xml_node *node);

/*!
 * \brief Set an element content string.
 * \param node Node from where to set the content string.
 * \param content The text to insert in the node.
 */
void ast_xml_set_text(struct ast_xml_node *node, const char *content);

/*!
 * \brief Set or reset an element's name.
 * \param node Node whose name is to be set.
 * \param name New name.
 */
void ast_xml_set_name(struct ast_xml_node *node, const char *name);

/*!
 * \brief Get the name of a node. */
const char *ast_xml_node_get_name(struct ast_xml_node *node);

/*!
 * \brief Get the node's children. */
struct ast_xml_node *ast_xml_node_get_children(struct ast_xml_node *node);

/*!
 * \brief Get the next node in the same level. */
struct ast_xml_node *ast_xml_node_get_next(struct ast_xml_node *node);

/*!
 * \brief Get the previous node in the same leve. */
struct ast_xml_node *ast_xml_node_get_prev(struct ast_xml_node *node);

/*!
 * \brief Get the parent of a specified node. */
struct ast_xml_node *ast_xml_node_get_parent(struct ast_xml_node *node);

/*!
 * \brief Dump the specified document to a file. */
int ast_xml_doc_dump_file(FILE *output, struct ast_xml_doc *doc);

/*!
 * \brief Free the XPath results
 * \param results The XPath results object to dispose of
 *
 * \since 12
 */
void ast_xml_xpath_results_free(struct ast_xml_xpath_results *results);

/*!
 * \brief Return the number of results from an XPath query
 * \param results The XPath results object to count
 * \return The number of results in the XPath object
 *
 * \since 12
 */
int ast_xml_xpath_num_results(struct ast_xml_xpath_results *results);

/*!
 * \brief Return the first result node of an XPath query
 * \param results The XPath results object to get the first result from
 * \return The first result in the XPath object on success
 * \retval NULL on error
 *
 * \since 12
 */
struct ast_xml_node *ast_xml_xpath_get_first_result(struct ast_xml_xpath_results *results);

/*!
 * \brief Return a specific result node of an XPath query
 * \param results The XPath results object to get the result from
 * \param n The index of the result to get
 * \return The nth result in the XPath object on success
 * \retval NULL on error
 */
struct ast_xml_node *ast_xml_xpath_get_result(struct ast_xml_xpath_results *results, int n);

/*!
 * \brief Execute an XPath query on an XML document
 * \param doc XML document to query
 * \param xpath_str The XPath query string to execute on the document
 * \return An object containing the results of the XPath query on success
 * \retval NULL on failure
 *
 * \since 12
 */
struct ast_xml_xpath_results *ast_xml_query(struct ast_xml_doc *doc, const char *xpath_str);

/*!
 * \brief Namespace definition
 */
struct ast_xml_namespace_def {
	const char *prefix;
	const char *href;
};

AST_VECTOR(ast_xml_namespace_def_vector, struct ast_xml_namespace_def);

/*!
 * \brief Execute an XPath query on an XML document with namespaces
 * \param doc XML document to query
 * \param xpath_str The XPath query string to execute on the document
 * \param namespaces A vector of ast_xml_namespace structures (not pointers)
 * \return An object containing the results of the XPath query on success
 * \retval NULL on failure
 */
struct ast_xml_xpath_results *ast_xml_query_with_namespaces(struct ast_xml_doc *doc, const char *xpath_str,
	struct ast_xml_namespace_def_vector *namespaces);

#ifdef HAVE_LIBXSLT

/*! \brief Open an XSLT document that resides in memory.
 *
 * \param buffer The address where the stylesheet is stored
 * \param size   The number of bytes in the stylesheet
 *
 * \return The stylesheet document.  Must be closed with ast_xslt_close().
 */
struct ast_xslt_doc *ast_xslt_read_memory(char *buffer, size_t size);

/*!
 * \brief Open an XSLT document.
 *
 * \param filename stylesheet path.
 *
 * \return The stylesheet document.  Must be closed with ast_xslt_close().
 */
struct ast_xslt_doc *ast_xslt_open(char *filename);

/*!
 * \brief Close a stylesheet document and free its resources.
 *
 * \param xslt XSLT stylesheet to close
 */
void ast_xslt_close(struct ast_xslt_doc *xslt);

/*!
 * \brief Apply an XSLT stylesheet to an XML document
 *
 * \param xslt    XSLT stylesheet to apply.
 * \param xml     XML document the stylesheet will be applied to.
 * \param params  An array of name value pairs to pass as parameters
 *                The array must terminate with a NULL sentinel.
 *                Example:  { "name1", "value1", "name2", "value2", NULL }
 *
 * \return A pointer to the result document which must be freed with ast_xml_close()
 */
struct ast_xml_doc *ast_xslt_apply(struct ast_xslt_doc *xslt, struct ast_xml_doc *doc, const char **params);

/*!
 * \brief Save the results of applying a stylesheet to a string
 *
 * \param buffer[out]  A pointer to a char * to receive the address of the result string.
 *                     The buffer must be freed with ast_xml_free_text().
 * \param length[out]  A pointer to an int to receive the result string length.
 * \param result       The result document from ast_xslt_apply.
 * \param xslt         The stylesheet that was applied.
 *
 * \return 0 on success, any other value on failure.
 */
int ast_xslt_save_result_to_string(char **buffer, int *length, struct ast_xml_doc *result,
	struct ast_xslt_doc *xslt);

#endif /* HAVE_LIBXSLT */
#endif /* _ASTERISK_XML_H */
