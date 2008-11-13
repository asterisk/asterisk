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
 *  \brief Asterisk XML abstraction layer
 */

struct ast_xml_node;
struct ast_xml_doc;

/*! \brief Initialize the XML library implementation.
 *         This function is used to setup everything needed
 *         to start working with the xml implementation.
 *  \retval 0 On success.
 *  \retval 1 On error.
 */
int ast_xml_init(void);

/*! \brief Cleanup library allocated global data.
 *  \retval 0 On success.
 *  \retval 1 On error.
 */
int ast_xml_finish(void);

/*! \brief Open an XML document.
 *  \param filename Document path.
 *  \retval NULL on error.
 *  \retval The ast_xml_doc reference to the open document.
 */
struct ast_xml_doc *ast_xml_open(char *filename);

/*! \brief Close an already open document and free the used
 *        structure.
 *  \retval doc The document reference.
 */
void ast_xml_close(struct ast_xml_doc *doc);

/*! \brief Get the document root node.
 *  \param doc Document reference
 *  \retval NULL on error
 *  \retval The root node on success.
 */
struct ast_xml_node *ast_xml_get_root(struct ast_xml_doc *doc);

/*! \brief Free node
 *  \param node Node to be released.
 */
void ast_xml_free_node(struct ast_xml_node *node);

/*! \brief Free an attribute returned by ast_xml_get_attribute()
 *  \param data pointer to be freed.
 */
void ast_xml_free_attr(const char *attribute);

/*! \brief Free a content element that was returned by ast_xml_get_text()
 *  \param text text to be freed.
 */
void ast_xml_free_text(const char *text);

/*! \brief Get a node attribute by name
 *  \param node Node where to search the attribute.
 *  \param attrname Attribute name.
 *  \retval NULL on error
 *  \retval The attribute value on success.
 */
const char *ast_xml_get_attribute(struct ast_xml_node *node, const char *attrname);

/*! \brief Find a node element by name.
 *  \param node This is the node starting point.
 *  \param name Node name to find.
 *  \param attrname attribute name to match (if NULL it won't be matched).
 *  \param attrvalue attribute value to match (if NULL it won't be matched).
 *  \retval NULL if not found
 *  \retval The node on success.
 */
struct ast_xml_node *ast_xml_find_element(struct ast_xml_node *root_node, const char *name, const char *attrname, const char *attrvalue);

/*! \brief Get an element content string.
 *  \param node Node from where to get the string.
 *  \retval NULL on error.
 *  \retval The text content of node.
 */
const char *ast_xml_get_text(struct ast_xml_node *node);

/*! \brief Get the name of a node. */
const char *ast_xml_node_get_name(struct ast_xml_node *node);

/*! \brief Get the node's children. */
struct ast_xml_node *ast_xml_node_get_children(struct ast_xml_node *node);

/*! \brief Get the next node in the same level. */
struct ast_xml_node *ast_xml_node_get_next(struct ast_xml_node *node);

/*! \brief Get the previous node in the same leve. */
struct ast_xml_node *ast_xml_node_get_prev(struct ast_xml_node *node);

/*! \brief Get the parent of a specified node. */
struct ast_xml_node *ast_xml_node_get_parent(struct ast_xml_node *node);

/* Features using ast_xml_ */
#ifdef HAVE_LIBXML2
#define AST_XML_DOCS
#endif

#endif /* _ASTERISK_XML_H */

