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

/*! \brief From where the documentation come from, this structure is useful for
 *  use it inside application/functions/manager actions structure. */
enum ast_doc_src {
	AST_XML_DOC,            /*!< From XML documentation */
	AST_STATIC_DOC          /*!< From application/function registration */
};

#ifdef AST_XML_DOCS

/*!
 *  \brief Get the syntax for a specified application or function.
 *  \param type Application, Function or AGI ?
 *  \param name Name of the application or function.
 *  \retval NULL on error.
 *  \retval The generated syntax in a ast_malloc'ed string.
 */
char *ast_xmldoc_build_syntax(const char *type, const char *name);

/*!
 *  \brief Parse the <see-also> node content.
 *  \param type 'application', 'function' or 'agi'.
 *  \param name Application or functions name.
 *  \retval NULL on error.
 *  \retval Content of the see-also node.
 */
char *ast_xmldoc_build_seealso(const char *type, const char *name);

/*!
 *  \brief Generate the [arguments] tag based on type of node ('application',
 *         'function' or 'agi') and name.
 *  \param type 'application', 'function' or 'agi' ?
 *  \param name Name of the application or function to build the 'arguments' tag.
 *  \retval NULL on error.
 *  \retval Output buffer with the [arguments] tag content.
 */
char *ast_xmldoc_build_arguments(const char *type, const char *name);

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
 *  \retval NULL on error.
 *  \retval A malloc'ed string with the synopsis.
 */
char *ast_xmldoc_build_synopsis(const char *type, const char *name);

/*!
 *  \brief Generate description documentation from XML.
 *  \param type The source of documentation (application, function, etc).
 *  \param name The name of the application, function, etc.
 *  \retval NULL on error.
 *  \retval A malloc'ed string with the formatted description.
 */
char *ast_xmldoc_build_description(const char *type, const char *name);

#endif /* AST_XML_DOCS */

#endif /* _ASTERISK_XMLDOC_H */
