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

#ifndef _ASTERISK_XMLDOC_H
#define _ASTERISK_XMLDOC_H

/*! \file
 *  \brief Asterisk XML Documentation API
 */

#include "asterisk/xml.h"
#include "asterisk/stringfields.h"
#include "asterisk/strings.h"

/*! \brief From where the documentation come from, this structure is useful for
 *  use it inside application/functions/manager actions structure. */
enum ast_doc_src {
	AST_XML_DOC,            /*!< From XML documentation */
	AST_STATIC_DOC          /*!< From application/function registration */
};

#ifdef AST_XML_DOCS

struct ao2_container;

/*! \brief Struct that contains the XML documentation for a particular item.  Note
 * that this is an ao2 ref counted object.
 *
 * \note
 * Each of the ast_str objects are built from the corresponding ast_xmldoc_build_*
 * calls
 *
 * \since 11
 */
struct ast_xml_doc_item {
	/*! The syntax of the item */
	struct ast_str *syntax;
	/*! Seealso tagged information, if it exists */
	struct ast_str *seealso;
	/*! The arguments to the item */
	struct ast_str *arguments;
	/*! A synopsis of the item */
	struct ast_str *synopsis;
	/*! A description of the item */
	struct ast_str *description;
	AST_DECLARE_STRING_FIELDS(
		/*! The name of the item */
		AST_STRING_FIELD(name);
		/*! The type of the item */
		AST_STRING_FIELD(type);
	);
	/*! The next XML documentation item that matches the same name/item type */
	struct ast_xml_doc_item *next;
};

/*!
 *  \brief Get the syntax for a specified application or function.
 *  \param type Application, Function or AGI ?
 *  \param name Name of the application or function.
 *  \param module The module the item is in (optional, can be NULL)
 *  \retval NULL on error.
 *  \retval The generated syntax in a ast_malloc'ed string.
 */
char *ast_xmldoc_build_syntax(const char *type, const char *name, const char *module);

/*!
 *  \brief Parse the <see-also> node content.
 *  \param type 'application', 'function' or 'agi'.
 *  \param name Application or functions name.
 *  \param module The module the item is in (optional, can be NULL)
 *  \retval NULL on error.
 *  \retval Content of the see-also node.
 */
char *ast_xmldoc_build_seealso(const char *type, const char *name, const char *module);

/*!
 *  \brief Generate the [arguments] tag based on type of node ('application',
 *         'function' or 'agi') and name.
 *  \param type 'application', 'function' or 'agi' ?
 *  \param name Name of the application or function to build the 'arguments' tag.
 *  \param module The module the item is in (optional, can be NULL)
 *  \retval NULL on error.
 *  \retval Output buffer with the [arguments] tag content.
 */
char *ast_xmldoc_build_arguments(const char *type, const char *name, const char *module);

/*!
 *  \brief Colorize and put delimiters (instead of tags) to the xmldoc output.
 *  \param bwinput Not colorized input with tags.
 *  \param withcolors Result output with colors.
 *  \retval NULL on error.
 *  \retval New malloced buffer colorized and with delimiters.
 */
char *ast_xmldoc_printable(const char *bwinput, int withcolors);

/*!
 *  \brief Generate synopsis documentation from XML.
 *  \param type The source of documentation (application, function, etc).
 *  \param name The name of the application, function, etc.
 *  \param module The module the item is in (optional, can be NULL)
 *  \retval NULL on error.
 *  \retval A malloc'ed string with the synopsis.
 */
char *ast_xmldoc_build_synopsis(const char *type, const char *name, const char *module);

/*!
 *  \brief Generate description documentation from XML.
 *  \param type The source of documentation (application, function, etc).
 *  \param name The name of the application, function, etc.
 *  \param module The module the item is in (optional, can be NULL)
 *  \retval NULL on error.
 *  \retval A malloc'ed string with the formatted description.
 */
char *ast_xmldoc_build_description(const char *type, const char *name, const char *module);

/*!
 *  \brief Build the documentation for a particular source type
 *  \param type The source of the documentation items (application, function, etc.)
 *
 *  \retval NULL on error
 *  \retval An ao2_container populated with ast_xml_doc instances for each item
 *  that exists for the specified source type
 *
 *  \since 11
 */
struct ao2_container *ast_xmldoc_build_documentation(const char *type);

#endif /* AST_XML_DOCS */

#endif /* _ASTERISK_XMLDOC_H */
