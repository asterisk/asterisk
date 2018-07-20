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

/*! \file
 *
 * \brief XML Documentation API
 *
 * \author Eliel C. Sardanons (LU1ALY) <eliels@gmail.com>
 *
 * libxml2 http://www.xmlsoft.org/
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"
#include "asterisk/paths.h"
#include "asterisk/linkedlists.h"
#include "asterisk/config.h"
#include "asterisk/term.h"
#include "asterisk/astobj2.h"
#include "asterisk/xmldoc.h"
#include "asterisk/cli.h"

#ifdef AST_XML_DOCS

/*! \brief Default documentation language. */
static const char default_documentation_language[] = "en_US";

/*! \brief Number of columns to print when showing the XML documentation with a
 *         'core show application/function *' CLI command. Used in text wrapping.*/
static const int xmldoc_text_columns = 79;

/*! \brief XML documentation language. */
static char documentation_language[6];

/*! \brief XML documentation tree */
struct documentation_tree {
	char *filename;					/*!< XML document filename. */
	struct ast_xml_doc *doc;			/*!< Open document pointer. */
	AST_RWLIST_ENTRY(documentation_tree) entry;
};

static char *xmldoc_get_syntax_cmd(struct ast_xml_node *fixnode, const char *name, int printname);
static int xmldoc_parse_enumlist(struct ast_xml_node *fixnode, const char *tabs, struct ast_str **buffer);
static void xmldoc_parse_parameter(struct ast_xml_node *fixnode, const char *tabs, struct ast_str **buffer);
static int xmldoc_parse_info(struct ast_xml_node *node, const char *tabs, const char *posttabs, struct ast_str **buffer);
static int xmldoc_parse_para(struct ast_xml_node *node, const char *tabs, const char *posttabs, struct ast_str **buffer);
static int xmldoc_parse_specialtags(struct ast_xml_node *fixnode, const char *tabs, const char *posttabs, struct ast_str **buffer);


/*!
 * \brief Container of documentation trees
 *
 * \note A RWLIST is a sufficient container type to use here for now.
 *       However, some changes will need to be made to implement ref counting
 *       if reload support is added in the future.
 */
static AST_RWLIST_HEAD_STATIC(xmldoc_tree, documentation_tree);

static const struct strcolorized_tags {
	const char *init;      /*!< Replace initial tag with this string. */
	const char *end;       /*!< Replace end tag with this string. */
	const int colorfg;     /*!< Foreground color. */
	const char *inittag;   /*!< Initial tag description. */
	const char *endtag;    /*!< Ending tag description. */
} colorized_tags[] = {
	{ "<",  ">",  COLOR_GREEN,  "<replaceable>", "</replaceable>" },
	{ "\'", "\'", COLOR_BLUE,   "<literal>",     "</literal>" },
	{ "*",  "*",  COLOR_RED,    "<emphasis>",    "</emphasis>" },
	{ "\"", "\"", COLOR_YELLOW, "<filename>",    "</filename>" },
	{ "\"", "\"", COLOR_CYAN,   "<directory>",   "</directory>" },
	{ "${", "}",  COLOR_GREEN,  "<variable>",    "</variable>" },
	{ "",   "",   COLOR_BLUE,   "<value>",       "</value>" },
	{ "",   "",   COLOR_BLUE,   "<enum>",        "</enum>" },
	{ "\'", "\'", COLOR_GRAY,   "<astcli>",      "</astcli>" },

	/* Special tags */
	{ "", "", COLOR_YELLOW, "<note>",   "</note>" },
	{ "", "", COLOR_RED,   "<warning>", "</warning>" },
	{ "", "", COLOR_WHITE, "<example>", "</example>" },
	{ "", "", COLOR_GRAY, "<exampletext>", "</exampletext>"},
};

static const struct strspecial_tags {
	const char *tagname;		/*!< Special tag name. */
	const char *init;		/*!< Print this at the beginning. */
	const char *end;		/*!< Print this at the end. */
} special_tags[] = {
	{ "note",    "<note>NOTE:</note> ",             "" },
	{ "warning", "<warning>WARNING!!!:</warning> ", "" },
	{ "example", "<example>Example:</example> ", "" },
};

/*!
 * \internal
 * \brief Calculate the space in bytes used by a format string
 *        that will be passed to a sprintf function.
 *
 * \param postbr The format string to use to calculate the length.
 *
 * \retval The postbr length.
 */
static int xmldoc_postbrlen(const char *postbr)
{
	int postbrreallen = 0, i;
	size_t postbrlen;

	if (!postbr) {
		return 0;
	}
	postbrlen = strlen(postbr);
	for (i = 0; i < postbrlen; i++) {
		if (postbr[i] == '\t') {
			postbrreallen += 8 - (postbrreallen % 8);
		} else {
			postbrreallen++;
		}
	}
	return postbrreallen;
}

/*!
 * \internal
 * \brief Setup postbr to be used while wrapping the text.
 *        Add to postbr array all the spaces and tabs at the beginning of text.
 *
 * \param postbr output array.
 * \param len text array length.
 * \param text Text with format string before the actual string.
 */
static void xmldoc_setpostbr(char *postbr, size_t len, const char *text)
{
	int c, postbrlen = 0;

	if (!text) {
		return;
	}

	for (c = 0; c < len; c++) {
		if (text[c] == '\t' || text[c] == ' ') {
			postbr[postbrlen++] = text[c];
		} else {
			break;
		}
	}
	postbr[postbrlen] = '\0';
}

/*!
 * \internal
 * \brief Justify a text to a number of columns.
 *
 * \param text Input text to be justified.
 * \param columns Number of columns to preserve in the text.
 *
 * \retval NULL on error.
 * \retval The wrapped text.
 */
static char *xmldoc_string_wrap(const char *text, int columns)
{
	struct ast_str *tmp;
	char *ret, postbr[160];
	int count, i, textlen, postbrlen, lastbreak;

	/* sanity check */
	if (!text || columns <= 0) {
		ast_log(LOG_WARNING, "Passing wrong arguments while trying to wrap the text\n");
		return NULL;
	}

	tmp = ast_str_create(strlen(text) * 3);

	if (!tmp) {
		return NULL;
	}

	/* Check for blanks and tabs and put them in postbr. */
	xmldoc_setpostbr(postbr, sizeof(postbr), text);
	postbrlen = xmldoc_postbrlen(postbr);

	count = 0;
	lastbreak = 0;

	textlen = strlen(text);
	for (i = 0; i < textlen; i++) {
		if (text[i] == '\n') {
			xmldoc_setpostbr(postbr, sizeof(postbr), &text[i] + 1);
			postbrlen = xmldoc_postbrlen(postbr);
			count = 0;
			lastbreak = 0;
		} else if (text[i] == ESC) {
			/* Walk over escape sequences without counting them. */
			do {
				ast_str_append(&tmp, 0, "%c", text[i]);
				i++;
			} while (i < textlen && text[i] != 'm');
		} else {
			if (text[i] == ' ') {
				lastbreak = i;
			}
			count++;
		}

		if (count > columns) {
			/* Seek backwards if it was at most 30 characters ago. */
			int back = i - lastbreak;
			if (lastbreak && back > 0 && back < 30) {
				ast_str_truncate(tmp, -back);
				i = lastbreak; /* go back a bit */
			}
			ast_str_append(&tmp, 0, "\n%s", postbr);
			count = postbrlen;
			lastbreak = 0;
		} else {
			ast_str_append(&tmp, 0, "%c", text[i]);
		}
	}

	ret = ast_strdup(ast_str_buffer(tmp));
	ast_free(tmp);

	return ret;
}

char *ast_xmldoc_printable(const char *bwinput, int withcolors)
{
	struct ast_str *colorized;
	char *wrapped = NULL;
	int i, c, len, colorsection;
	char *tmp;
	size_t bwinputlen;
	static const int base_fg = COLOR_CYAN;

	if (!bwinput) {
		return NULL;
	}

	bwinputlen = strlen(bwinput);

	if (!(colorized = ast_str_create(256))) {
		return NULL;
	}

	if (withcolors) {
		ast_term_color_code(&colorized, base_fg, 0);
		if (!colorized) {
			return NULL;
		}
	}

	for (i = 0; i < bwinputlen; i++) {
		colorsection = 0;
		/* Check if we are at the beginning of a tag to be colorized. */
		for (c = 0; c < ARRAY_LEN(colorized_tags); c++) {
			if (strncasecmp(bwinput + i, colorized_tags[c].inittag, strlen(colorized_tags[c].inittag))) {
				continue;
			}

			if (!(tmp = strcasestr(bwinput + i + strlen(colorized_tags[c].inittag), colorized_tags[c].endtag))) {
				continue;
			}

			len = tmp - (bwinput + i + strlen(colorized_tags[c].inittag));

			/* Setup color */
			if (withcolors) {
				if (ast_opt_light_background) {
					/* Turn off *bright* colors */
					ast_term_color_code(&colorized, colorized_tags[c].colorfg & 0x7f, 0);
				} else {
					/* Turn on *bright* colors */
					ast_term_color_code(&colorized, colorized_tags[c].colorfg | 0x80, 0);
				}
				if (!colorized) {
					return NULL;
				}
			}

			/* copy initial string replace */
			ast_str_append(&colorized, 0, "%s", colorized_tags[c].init);
			if (!colorized) {
				return NULL;
			}
			{
				char buf[len + 1];
				ast_copy_string(buf, bwinput + i + strlen(colorized_tags[c].inittag), sizeof(buf));
				ast_str_append(&colorized, 0, "%s", buf);
			}
			if (!colorized) {
				return NULL;
			}

			/* copy the ending string replace */
			ast_str_append(&colorized, 0, "%s", colorized_tags[c].end);
			if (!colorized) {
				return NULL;
			}

			/* Continue with the last color. */
			if (withcolors) {
				ast_term_color_code(&colorized, base_fg, 0);
				if (!colorized) {
					return NULL;
				}
			}

			i += len + strlen(colorized_tags[c].endtag) + strlen(colorized_tags[c].inittag) - 1;
			colorsection = 1;
			break;
		}

		if (!colorsection) {
			ast_str_append(&colorized, 0, "%c", bwinput[i]);
			if (!colorized) {
				return NULL;
			}
		}
	}

	if (withcolors) {
		ast_str_append(&colorized, 0, "%s", ast_term_reset());
		if (!colorized) {
			return NULL;
		}
	}

	/* Wrap the text, notice that string wrap will avoid cutting an ESC sequence. */
	wrapped = xmldoc_string_wrap(ast_str_buffer(colorized), xmldoc_text_columns);

	ast_free(colorized);

	return wrapped;
}

/*!
 * \internal
 * \brief Cleanup spaces and tabs after a \n
 *
 * \param text String to be cleaned up.
 * \param output buffer (not already allocated).
 * \param lastspaces Remove last spaces in the string.
 * \param maintain_newlines Preserve new line characters (\n \r) discovered in the string
 */
static void xmldoc_string_cleanup(const char *text, struct ast_str **output, int lastspaces, int maintain_newlines)
{
	int i;
	size_t textlen;

	if (!text) {
		*output = NULL;
		return;
	}

	textlen = strlen(text);

	*output = ast_str_create(textlen);
	if (!(*output)) {
		ast_log(LOG_ERROR, "Problem allocating output buffer\n");
		return;
	}

	for (i = 0; i < textlen; i++) {
		if (text[i] == '\n' || text[i] == '\r') {
			if (maintain_newlines) {
				ast_str_append(output, 0, "%c", text[i]);
			}
			/* remove spaces/tabs/\n after a \n. */
			while (text[i + 1] == '\t' || text[i + 1] == '\r' || text[i + 1] == '\n') {
				i++;
			}
			ast_str_append(output, 0, " ");
			continue;
		} else {
			ast_str_append(output, 0, "%c", text[i]);
		}
	}

	/* remove last spaces (we don't want always to remove the trailing spaces). */
	if (lastspaces) {
		ast_str_trim_blanks(*output);
	}
}

/*!
 * \internal
 * \brief Check if the given attribute on the given node matches the given value.
 *
 * \param node the node to match
 * \param attr the name of the attribute
 * \param value the expected value of the attribute
 *
 * \retval true if the given attribute contains the given value
 * \retval false if the given attribute does not exist or does not contain the given value
 */
static int xmldoc_attribute_match(struct ast_xml_node *node, const char *attr, const char *value)
{
	const char *attr_value = ast_xml_get_attribute(node, attr);
	int match = attr_value && !strcmp(attr_value, value);
	ast_xml_free_attr(attr_value);
	return match;
}

/*!
 * \internal
 * \brief Get the application/function node for 'name' application/function with language 'language'
 *        and module 'module' if we don't find any, get the first application
 *        with 'name' no matter which language or module.
 *
 * \param type 'application', 'function', ...
 * \param name Application or Function name.
 * \param module Module item is in.
 * \param language Try to get this language (if not found try with en_US)
 *
 * \retval NULL on error.
 * \retval A node of type ast_xml_node.
 */
static struct ast_xml_node *xmldoc_get_node(const char *type, const char *name, const char *module, const char *language)
{
	struct ast_xml_node *node = NULL;
	struct ast_xml_node *first_match = NULL;
	struct ast_xml_node *lang_match = NULL;
	struct documentation_tree *doctree;

	AST_RWLIST_RDLOCK(&xmldoc_tree);
	AST_LIST_TRAVERSE(&xmldoc_tree, doctree, entry) {
		/* the core xml documents have priority over thirdparty document. */
		node = ast_xml_get_root(doctree->doc);
		if (!node) {
			break;
		}

		node = ast_xml_node_get_children(node);
		while ((node = ast_xml_find_element(node, type, "name", name))) {
			if (!ast_xml_node_get_children(node)) {
				/* ignore empty nodes */
				node = ast_xml_node_get_next(node);
				continue;
			}

			if (!first_match) {
				first_match = node;
			}

			/* Check language */
			if (xmldoc_attribute_match(node, "language", language)) {
				if (!lang_match) {
					lang_match = node;
				}

				/* if module is empty we have a match */
				if (ast_strlen_zero(module)) {
					break;
				}

				/* Check module */
				if (xmldoc_attribute_match(node, "module", module)) {
					break;
				}
			}

			node = ast_xml_node_get_next(node);
		}

		/* if we matched lang and module return this match */
		if (node) {
			break;
		}

		/* we didn't match lang and module, just return the first
		 * result with a matching language if we have one */
		if (lang_match) {
			node = lang_match;
			break;
		}

		/* we didn't match with only the language, just return the
		 * first match */
		if (first_match) {
			node = first_match;
			break;
		}
	}
	AST_RWLIST_UNLOCK(&xmldoc_tree);

	return node;
}

/*!
 * \internal
 * \brief Helper function used to build the syntax, it allocates the needed buffer (or reallocates it),
 *        and based on the reverse value it makes use of fmt to print the parameter list inside the
 *        realloced buffer (syntax).
 *
 * \param reverse We are going backwards while generating the syntax?
 * \param len Current length of 'syntax' buffer.
 * \param syntax Output buffer for the concatenated values.
 * \param fmt A format string that will be used in a sprintf call.
 */
static void __attribute__((format(printf, 4, 5))) xmldoc_reverse_helper(int reverse, int *len, char **syntax, const char *fmt, ...)
{
	int totlen;
	int tmpfmtlen;
	char *tmpfmt;
	char *new_syntax;
	char tmp;
	va_list ap;

	va_start(ap, fmt);
	if (ast_vasprintf(&tmpfmt, fmt, ap) < 0) {
		va_end(ap);
		return;
	}
	va_end(ap);

	tmpfmtlen = strlen(tmpfmt);
	totlen = *len + tmpfmtlen + 1;

	new_syntax = ast_realloc(*syntax, totlen);
	if (!new_syntax) {
		ast_free(tmpfmt);
		return;
	}
	*syntax = new_syntax;

	if (reverse) {
		memmove(*syntax + tmpfmtlen, *syntax, *len);
		/* Save this char, it will be overwritten by the \0 of strcpy. */
		tmp = (*syntax)[0];
		strcpy(*syntax, tmpfmt);
		/* Restore the already saved char. */
		(*syntax)[tmpfmtlen] = tmp;
		(*syntax)[totlen - 1] = '\0';
	} else {
		strcpy(*syntax + *len, tmpfmt);
	}

	*len = totlen - 1;
	ast_free(tmpfmt);
}

/*!
 * \internal
 * \brief Check if the passed node has 'what' tags inside it.
 *
 * \param node Root node to search 'what' elements.
 * \param what node name to search inside node.
 *
 * \retval 1 If a 'what' element is found inside 'node'.
 * \retval 0 If no 'what' is found inside 'node'.
 */
static int xmldoc_has_inside(struct ast_xml_node *fixnode, const char *what)
{
	struct ast_xml_node *node = fixnode;

	for (node = ast_xml_node_get_children(fixnode); node; node = ast_xml_node_get_next(node)) {
		if (!strcasecmp(ast_xml_node_get_name(node), what)) {
			return 1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Check if the passed node has at least one node inside it.
 *
 * \param node Root node to search node elements.
 *
 * \retval 1 If a node element is found inside 'node'.
 * \retval 0 If no node is found inside 'node'.
 */
static int xmldoc_has_nodes(struct ast_xml_node *fixnode)
{
	struct ast_xml_node *node = fixnode;

	for (node = ast_xml_node_get_children(fixnode); node; node = ast_xml_node_get_next(node)) {
		if (strcasecmp(ast_xml_node_get_name(node), "text")) {
			return 1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Check if the passed node has at least one specialtag.
 *
 * \param node Root node to search "specialtags" elements.
 *
 * \retval 1 If a "specialtag" element is found inside 'node'.
 * \retval 0 If no "specialtag" is found inside 'node'.
 */
static int xmldoc_has_specialtags(struct ast_xml_node *fixnode)
{
	struct ast_xml_node *node = fixnode;
	int i;

	for (node = ast_xml_node_get_children(fixnode); node; node = ast_xml_node_get_next(node)) {
		for (i = 0; i < ARRAY_LEN(special_tags); i++) {
			if (!strcasecmp(ast_xml_node_get_name(node), special_tags[i].tagname)) {
				return 1;
			}
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Build the syntax for a specified starting node.
 *
 * \param rootnode A pointer to the ast_xml root node.
 * \param rootname Name of the application, function, option, etc. to build the syntax.
 * \param childname The name of each parameter node.
 * \param printparenthesis Boolean if we must print parenthesis if not parameters are found in the rootnode.
 * \param printrootname Boolean if we must print the rootname before the syntax and parenthesis at the begining/end.
 *
 * \retval NULL on error.
 * \retval An ast_malloc'ed string with the syntax generated.
 */
static char *xmldoc_get_syntax_fun(struct ast_xml_node *rootnode, const char *rootname, const char *childname, int printparenthesis, int printrootname)
{
#define GOTONEXT(__rev, __a) (__rev ? ast_xml_node_get_prev(__a) : ast_xml_node_get_next(__a))
#define ISLAST(__rev, __a)  (__rev == 1 ? (ast_xml_node_get_prev(__a) ? 0 : 1) : (ast_xml_node_get_next(__a) ? 0 : 1))
#define MP(__a) ((multiple ? __a : ""))
	struct ast_xml_node *node = NULL, *firstparam = NULL, *lastparam = NULL;
	const char *paramtype, *multipletype, *paramnameattr, *attrargsep, *parenthesis, *argname;
	int reverse, required, paramcount = 0, openbrackets = 0, len = 0, hasparams=0;
	int reqfinode = 0, reqlanode = 0, optmidnode = 0, prnparenthesis, multiple;
	char *syntax = NULL, *argsep, *paramname;

	if (ast_strlen_zero(rootname) || ast_strlen_zero(childname)) {
		ast_log(LOG_WARNING, "Tried to look in XML tree with faulty rootname or childname while creating a syntax.\n");
		return NULL;
	}

	if (!rootnode || !ast_xml_node_get_children(rootnode)) {
		/* If the rootnode field is not found, at least print name. */
		if (ast_asprintf(&syntax, "%s%s", (printrootname ? rootname : ""), (printparenthesis ? "()" : "")) < 0) {
			syntax = NULL;
		}
		return syntax;
	}

	/* Get the argument separator from the root node attribute name 'argsep', if not found
	defaults to ','. */
	attrargsep = ast_xml_get_attribute(rootnode, "argsep");
	if (attrargsep) {
		argsep = ast_strdupa(attrargsep);
		ast_xml_free_attr(attrargsep);
	} else {
		argsep = ast_strdupa(",");
	}

	/* Get order of evaluation. */
	for (node = ast_xml_node_get_children(rootnode); node; node = ast_xml_node_get_next(node)) {
		if (strcasecmp(ast_xml_node_get_name(node), childname)) {
			continue;
		}
		required = 0;
		hasparams = 1;
		if ((paramtype = ast_xml_get_attribute(node, "required"))) {
			if (ast_true(paramtype)) {
				required = 1;
			}
			ast_xml_free_attr(paramtype);
		}

		lastparam = node;
		reqlanode = required;

		if (!firstparam) {
			/* first parameter node */
			firstparam = node;
			reqfinode = required;
		}
	}

	if (!hasparams) {
		/* This application, function, option, etc, doesn't have any params. */
		if (ast_asprintf(&syntax, "%s%s", (printrootname ? rootname : ""), (printparenthesis ? "()" : "")) < 0) {
			syntax = NULL;
		}
		return syntax;
	}

	if (reqfinode && reqlanode) {
		/* check midnode */
		for (node = ast_xml_node_get_children(rootnode); node; node = ast_xml_node_get_next(node)) {
			if (strcasecmp(ast_xml_node_get_name(node), childname)) {
				continue;
			}
			if (node != firstparam && node != lastparam) {
				if ((paramtype = ast_xml_get_attribute(node, "required"))) {
					if (!ast_true(paramtype)) {
						optmidnode = 1;
						ast_xml_free_attr(paramtype);
						break;
					}
					ast_xml_free_attr(paramtype);
				}
			}
		}
	}

	if ((!reqfinode && reqlanode) || (reqfinode && reqlanode && optmidnode)) {
		reverse = 1;
		node = lastparam;
	} else {
		reverse = 0;
		node = firstparam;
	}

	/* init syntax string. */
	if (reverse) {
		xmldoc_reverse_helper(reverse, &len, &syntax,
			(printrootname ? (printrootname == 2 ? ")]" : ")"): ""));
	} else {
		xmldoc_reverse_helper(reverse, &len, &syntax, "%s%s", (printrootname ? rootname : ""),
			(printrootname ? (printrootname == 2 ? "[(" : "(") : ""));
	}

	for (; node; node = GOTONEXT(reverse, node)) {
		if (strcasecmp(ast_xml_node_get_name(node), childname)) {
			continue;
		}

		/* Get the argument name, if it is not the leaf, go inside that parameter. */
		if (xmldoc_has_inside(node, "argument")) {
			parenthesis = ast_xml_get_attribute(node, "hasparams");
			prnparenthesis = 0;
			if (parenthesis) {
				prnparenthesis = ast_true(parenthesis);
				if (!strcasecmp(parenthesis, "optional")) {
					prnparenthesis = 2;
				}
				ast_xml_free_attr(parenthesis);
			}
			argname = ast_xml_get_attribute(node, "name");
			if (argname) {
				paramname = xmldoc_get_syntax_fun(node, argname, "argument", prnparenthesis, prnparenthesis);
				ast_xml_free_attr(argname);
			} else {
				/* Malformed XML, print **UNKOWN** */
				paramname = ast_strdup("**unknown**");
			}
		} else {
			paramnameattr = ast_xml_get_attribute(node, "name");
			if (!paramnameattr) {
				ast_log(LOG_WARNING, "Malformed XML %s: no %s name\n", rootname, childname);
				if (syntax) {
					/* Free already allocated syntax */
					ast_free(syntax);
				}
				/* to give up is ok? */
				if (ast_asprintf(&syntax, "%s%s", (printrootname ? rootname : ""), (printparenthesis ? "()" : "")) < 0) {
					syntax = NULL;
				}
				return syntax;
			}
			paramname = ast_strdup(paramnameattr);
			ast_xml_free_attr(paramnameattr);
		}

		if (!paramname) {
			return NULL;
		}

		/* Defaults to 'false'. */
		multiple = 0;
		if ((multipletype = ast_xml_get_attribute(node, "multiple"))) {
			if (ast_true(multipletype)) {
				multiple = 1;
			}
			ast_xml_free_attr(multipletype);
		}

		required = 0;	/* Defaults to 'false'. */
		if ((paramtype = ast_xml_get_attribute(node, "required"))) {
			if (ast_true(paramtype)) {
				required = 1;
			}
			ast_xml_free_attr(paramtype);
		}

		/* build syntax core. */

		if (required) {
			/* First parameter */
			if (!paramcount) {
				xmldoc_reverse_helper(reverse, &len, &syntax, "%s%s%s%s", paramname, MP("["), MP(argsep), MP("...]"));
			} else {
				/* Time to close open brackets. */
				while (openbrackets > 0) {
					xmldoc_reverse_helper(reverse, &len, &syntax, (reverse ? "[" : "]"));
					openbrackets--;
				}
				if (reverse) {
					xmldoc_reverse_helper(reverse, &len, &syntax, "%s%s", paramname, argsep);
				} else {
					xmldoc_reverse_helper(reverse, &len, &syntax, "%s%s", argsep, paramname);
				}
				xmldoc_reverse_helper(reverse, &len, &syntax, "%s%s%s", MP("["), MP(argsep), MP("...]"));
			}
		} else {
			/* First parameter */
			if (!paramcount) {
				xmldoc_reverse_helper(reverse, &len, &syntax, "[%s%s%s%s]", paramname, MP("["), MP(argsep), MP("...]"));
			} else {
				if (ISLAST(reverse, node)) {
					/* This is the last parameter. */
					if (reverse) {
						xmldoc_reverse_helper(reverse, &len, &syntax, "[%s%s%s%s]%s", paramname,
									MP("["), MP(argsep), MP("...]"), argsep);
					} else {
						xmldoc_reverse_helper(reverse, &len, &syntax, "%s[%s%s%s%s]", argsep, paramname,
									MP("["), MP(argsep), MP("...]"));
					}
				} else {
					if (reverse) {
						xmldoc_reverse_helper(reverse, &len, &syntax, "%s%s%s%s%s]", paramname, argsep,
									MP("["), MP(argsep), MP("...]"));
					} else {
						xmldoc_reverse_helper(reverse, &len, &syntax, "[%s%s%s%s%s", argsep, paramname,
									MP("["), MP(argsep), MP("...]"));
					}
					openbrackets++;
				}
			}
		}
		ast_free(paramname);

		paramcount++;
	}

	/* Time to close open brackets. */
	while (openbrackets > 0) {
		xmldoc_reverse_helper(reverse, &len, &syntax, (reverse ? "[" : "]"));
		openbrackets--;
	}

	/* close syntax string. */
	if (reverse) {
		xmldoc_reverse_helper(reverse, &len, &syntax, "%s%s", (printrootname ? rootname : ""),
			(printrootname ? (printrootname == 2 ? "[(" : "(") : ""));
	} else {
		xmldoc_reverse_helper(reverse, &len, &syntax, (printrootname ? (printrootname == 2 ? ")]" : ")") : ""));
	}

	return syntax;
#undef ISLAST
#undef GOTONEXT
#undef MP
}

/*!
 * \internal
 * \brief Parse an enumlist inside a <parameter> to generate a COMMAND syntax.
 *
 * \param fixnode A pointer to the <enumlist> node.
 *
 * \retval {<unknown>} on error.
 * \retval A string inside brackets {} with the enum's separated by pipes |.
 */
static char *xmldoc_parse_cmd_enumlist(struct ast_xml_node *fixnode)
{
	struct ast_xml_node *node = fixnode;
	struct ast_str *paramname;
	char *enumname, *ret;
	int first = 1;

	paramname = ast_str_create(128);
	if (!paramname) {
		return ast_strdup("{<unkown>}");
	}

	ast_str_append(&paramname, 0, "{");

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (strcasecmp(ast_xml_node_get_name(node), "enum")) {
			continue;
		}

		enumname = xmldoc_get_syntax_cmd(node, "", 0);
		if (!enumname) {
			continue;
		}
		if (!first) {
			ast_str_append(&paramname, 0, "|");
		}
		ast_str_append(&paramname, 0, "%s", enumname);
		first = 0;
		ast_free(enumname);
	}

	ast_str_append(&paramname, 0, "}");

	ret = ast_strdup(ast_str_buffer(paramname));
	ast_free(paramname);

	return ret;
}

/*!
 * \internal
 * \brief Generate a syntax of COMMAND type.
 *
 * \param fixnode The <syntax> node pointer.
 * \param name The name of the 'command'.
 * \param printname Print the name of the command before the paramters?
 *
 * \retval On error, return just 'name'.
 * \retval On success return the generated syntax.
 */
static char *xmldoc_get_syntax_cmd(struct ast_xml_node *fixnode, const char *name, int printname)
{
	struct ast_str *syntax;
	struct ast_xml_node *tmpnode, *node = fixnode;
	char *ret, *paramname;
	const char *paramtype, *attrname, *literal;
	int required, isenum, first = 1, isliteral;

	if (!fixnode) {
		return NULL;
	}

	syntax = ast_str_create(128);
	if (!syntax) {
		/* at least try to return something... */
		return ast_strdup(name);
	}

	/* append name to output string. */
	if (printname) {
		ast_str_append(&syntax, 0, "%s", name);
		first = 0;
	}

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (strcasecmp(ast_xml_node_get_name(node), "parameter")) {
			continue;
		}

		if (xmldoc_has_inside(node, "parameter")) {
			/* is this a recursive parameter. */
			paramname = xmldoc_get_syntax_cmd(node, "", 0);
			isenum = 1;
		} else {
			for (tmpnode = ast_xml_node_get_children(node); tmpnode; tmpnode = ast_xml_node_get_next(tmpnode)) {
				if (!strcasecmp(ast_xml_node_get_name(tmpnode), "enumlist")) {
					break;
				}
			}
			if (tmpnode) {
				/* parse enumlist (note that this is a special enumlist
				that is used to describe a syntax like {<param1>|<param2>|...} */
				paramname = xmldoc_parse_cmd_enumlist(tmpnode);
				isenum = 1;
			} else {
				/* this is a simple parameter. */
				attrname = ast_xml_get_attribute(node, "name");
				if (!attrname) {
					/* ignore this bogus parameter and continue. */
					continue;
				}
				paramname = ast_strdup(attrname);
				ast_xml_free_attr(attrname);
				isenum = 0;
			}
		}

		/* Is this parameter required? */
		required = 0;
		paramtype = ast_xml_get_attribute(node, "required");
		if (paramtype) {
			required = ast_true(paramtype);
			ast_xml_free_attr(paramtype);
		}

		/* Is this a replaceable value or a fixed parameter value? */
		isliteral = 0;
		literal = ast_xml_get_attribute(node, "literal");
		if (literal) {
			isliteral = ast_true(literal);
			ast_xml_free_attr(literal);
		}

		/* if required="false" print with [...].
		 * if literal="true" or is enum print without <..>.
		 * if not first print a space at the beginning.
		 */
		ast_str_append(&syntax, 0, "%s%s%s%s%s%s",
				(first ? "" : " "),
				(required ? "" : "["),
				(isenum || isliteral ? "" : "<"),
				paramname,
				(isenum || isliteral ? "" : ">"),
				(required ? "" : "]"));
		first = 0;
		ast_free(paramname);
	}

	/* return a common string. */
	ret = ast_strdup(ast_str_buffer(syntax));
	ast_free(syntax);

	return ret;
}

/*!
 * \internal
 * \brief Generate an AMI action/event syntax.
 *
 * \param fixnode The manager action/event node pointer.
 * \param name The name of the manager action/event.
 * \param manager_type "Action" or "Event"
 *
 * \retval The generated syntax.
 * \retval NULL on error.
 */
static char *xmldoc_get_syntax_manager(struct ast_xml_node *fixnode, const char *name, const char *manager_type)
{
	struct ast_str *syntax;
	struct ast_xml_node *node = fixnode;
	const char *paramtype, *attrname;
	int required;
	char *ret;

	if (!fixnode) {
		return NULL;
	}

	syntax = ast_str_create(128);
	if (!syntax) {
		return ast_strdup(name);
	}

	ast_str_append(&syntax, 0, "%s: %s", manager_type, name);

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (strcasecmp(ast_xml_node_get_name(node), "parameter")) {
			continue;
		}

		/* Is this parameter required? */
		required = !strcasecmp(manager_type, "event") ? 1 : 0;
		paramtype = ast_xml_get_attribute(node, "required");
		if (paramtype) {
			required = ast_true(paramtype);
			ast_xml_free_attr(paramtype);
		}

		attrname = ast_xml_get_attribute(node, "name");
		if (!attrname) {
			/* ignore this bogus parameter and continue. */
			continue;
		}

		ast_str_append(&syntax, 0, "\n%s%s:%s <value>",
			(required ? "" : "["),
			attrname,
			(required ? "" : "]"));
		ast_xml_free_attr(attrname);
	}

	/* return a common string. */
	ret = ast_strdup(ast_str_buffer(syntax));
	ast_free(syntax);

	return ret;
}

static char *xmldoc_get_syntax_config_object(struct ast_xml_node *fixnode, const char *name)
{
	struct ast_xml_node *matchinfo, *tmp;
	int match;
	const char *attr_value;
	const char *text;
	RAII_VAR(struct ast_str *, syntax, ast_str_create(128), ast_free);

	if (!syntax || !fixnode) {
		return NULL;
	}
	if (!(matchinfo = ast_xml_find_element(ast_xml_node_get_children(fixnode), "matchInfo", NULL, NULL))) {
		return NULL;
	}
	if (!(tmp  = ast_xml_find_element(ast_xml_node_get_children(matchinfo), "category", NULL, NULL))) {
		return NULL;
	}
	attr_value = ast_xml_get_attribute(tmp, "match");
	if (attr_value) {
		match = ast_true(attr_value);
		text = ast_xml_get_text(tmp);
		ast_str_set(&syntax, 0, "category %s /%s/", match ? "=~" : "!~", text);
		ast_xml_free_attr(attr_value);
		ast_xml_free_text(text);
	}

	if ((tmp = ast_xml_find_element(ast_xml_node_get_children(matchinfo), "field", NULL, NULL))) {
		text = ast_xml_get_text(tmp);
		attr_value = ast_xml_get_attribute(tmp, "name");
		ast_str_append(&syntax, 0, " matchfield: %s = %s", S_OR(attr_value, "Unknown"), text);
		ast_xml_free_attr(attr_value);
		ast_xml_free_text(text);
	}
	return ast_strdup(ast_str_buffer(syntax));
}

static char *xmldoc_get_syntax_config_option(struct ast_xml_node *fixnode, const char *name)
{
	const char *type;
	const char *default_value;
	const char *regex;
	RAII_VAR(struct ast_str *, syntax, ast_str_create(128), ast_free);

	if (!syntax || !fixnode) {
		return NULL;
	}
	type = ast_xml_get_attribute(fixnode, "type");
	default_value = ast_xml_get_attribute(fixnode, "default");

	regex = ast_xml_get_attribute(fixnode, "regex");
	ast_str_set(&syntax, 0, "%s = [%s] (Default: %s) (Regex: %s)\n",
		name,
		type ?: "",
		default_value ?: "n/a",
		regex ?: "False");

	ast_xml_free_attr(type);
	ast_xml_free_attr(default_value);
	ast_xml_free_attr(regex);

	return ast_strdup(ast_str_buffer(syntax));
}

/*! \brief Types of syntax that we are able to generate. */
enum syntaxtype {
	FUNCTION_SYNTAX,
	MANAGER_SYNTAX,
	MANAGER_EVENT_SYNTAX,
	CONFIG_INFO_SYNTAX,
	CONFIG_FILE_SYNTAX,
	CONFIG_OPTION_SYNTAX,
	CONFIG_OBJECT_SYNTAX,
	COMMAND_SYNTAX
};

/*! \brief Mapping between type of node and type of syntax to generate. */
static struct strsyntaxtype {
	const char *type;
	enum syntaxtype stxtype;
} stxtype[] = {
    { "function",     FUNCTION_SYNTAX      },
    { "application",  FUNCTION_SYNTAX      },
    { "manager",      MANAGER_SYNTAX       },
    { "managerEvent", MANAGER_EVENT_SYNTAX },
    { "configInfo",   CONFIG_INFO_SYNTAX   },
    { "configFile",   CONFIG_FILE_SYNTAX   },
    { "configOption", CONFIG_OPTION_SYNTAX },
    { "configObject", CONFIG_OBJECT_SYNTAX },
    { "agi",          COMMAND_SYNTAX       },
};

/*!
 * \internal
 * \brief Get syntax type based on type of node.
 *
 * \param type Type of node.
 *
 * \retval The type of syntax to generate based on the type of node.
 */
static enum syntaxtype xmldoc_get_syntax_type(const char *type)
{
	int i;
	for (i=0; i < ARRAY_LEN(stxtype); i++) {
		if (!strcasecmp(stxtype[i].type, type)) {
			return stxtype[i].stxtype;
		}
	}

	return FUNCTION_SYNTAX;
}

/*!
 * \internal
 * \brief Build syntax information for an item
 * \param node	The syntax node to parse
 * \param type	The source type
 * \param name	The name of the item that the syntax describes
 *
 * \note This method exists for when you already have the node.  This
 * prevents having to lock the documentation tree twice
 *
 * \retval A malloc'd character pointer to the syntax of the item
 * \retval NULL on failure
 *
 * \since 11
 */
static char *_ast_xmldoc_build_syntax(struct ast_xml_node *root_node, const char *type, const char *name)
{
	char *syntax = NULL;
	struct ast_xml_node *node = root_node;

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (!strcasecmp(ast_xml_node_get_name(node), "syntax")) {
			break;
		}
	}

	switch (xmldoc_get_syntax_type(type)) {
	case FUNCTION_SYNTAX:
		syntax = xmldoc_get_syntax_fun(node, name, "parameter", 1, 1);
		break;
	case COMMAND_SYNTAX:
		syntax = xmldoc_get_syntax_cmd(node, name, 1);
		break;
	case MANAGER_SYNTAX:
		syntax = xmldoc_get_syntax_manager(node, name, "Action");
		break;
	case MANAGER_EVENT_SYNTAX:
		syntax = xmldoc_get_syntax_manager(node, name, "Event");
		break;
	case CONFIG_OPTION_SYNTAX:
		syntax = xmldoc_get_syntax_config_option(root_node, name);
		break;
	case CONFIG_OBJECT_SYNTAX:
		syntax = xmldoc_get_syntax_config_object(node, name);
		break;
	default:
		syntax = xmldoc_get_syntax_fun(node, name, "parameter", 1, 1);
	}

	return syntax;
}

char *ast_xmldoc_build_syntax(const char *type, const char *name, const char *module)
{
	struct ast_xml_node *node;

	node = xmldoc_get_node(type, name, module, documentation_language);
	if (!node) {
		return NULL;
	}

	return _ast_xmldoc_build_syntax(node, type, name);
}

/*!
 * \internal
 * \brief Parse common internal elements.  This includes paragraphs, special
 *        tags, and information nodes.
 *
 * \param node The element to parse
 * \param tabs Add this string before the content of the parsed element.
 * \param posttabs Add this string after the content of the parsed element.
 * \param buffer This must be an already allocated ast_str. It will be used to
 *               store the result (if something has already been placed in the
 *               buffer, the parsed elements will be appended)
 *
 * \retval 1 if any data was appended to the buffer
 * \retval 2 if the data appended to the buffer contained a text paragraph
 * \retval 0 if no data was appended to the buffer
 */
static int xmldoc_parse_common_elements(struct ast_xml_node *node, const char *tabs, const char *posttabs, struct ast_str **buffer)
{
	return (xmldoc_parse_para(node, tabs, posttabs, buffer)
		|| xmldoc_parse_specialtags(node, tabs, posttabs, buffer)
		|| xmldoc_parse_info(node, tabs, posttabs, buffer));
}

/*!
 * \internal
 * \brief Parse a <para> element.
 *
 * \param node The <para> element pointer.
 * \param tabs Added this string before the content of the <para> element.
 * \param posttabs Added this string after the content of the <para> element.
 * \param buffer This must be an already allocated ast_str. It will be used
 *        to store the result (if already has something it will be appended to the current
 *        string).
 *
 * \retval 1 If 'node' is a named 'para'.
 * \retval 2 If data is appended in buffer.
 * \retval 0 on error.
 */
static int xmldoc_parse_para(struct ast_xml_node *node, const char *tabs, const char *posttabs, struct ast_str **buffer)
{
	const char *tmptext;
	struct ast_xml_node *tmp;
	int ret = 0;
	struct ast_str *tmpstr;

	if (!node || !ast_xml_node_get_children(node)) {
		return ret;
	}

	if (strcasecmp(ast_xml_node_get_name(node), "para")) {
		return ret;
	}

	ast_str_append(buffer, 0, "%s", tabs);

	ret = 1;

	for (tmp = ast_xml_node_get_children(node); tmp; tmp = ast_xml_node_get_next(tmp)) {
		/* Get the text inside the <para> element and append it to buffer. */
		tmptext = ast_xml_get_text(tmp);
		if (tmptext) {
			/* Strip \n etc. */
			xmldoc_string_cleanup(tmptext, &tmpstr, 0, 0);
			ast_xml_free_text(tmptext);
			if (tmpstr) {
				if (strcasecmp(ast_xml_node_get_name(tmp), "text")) {
					ast_str_append(buffer, 0, "<%s>%s</%s>", ast_xml_node_get_name(tmp),
							ast_str_buffer(tmpstr), ast_xml_node_get_name(tmp));
				} else {
					ast_str_append(buffer, 0, "%s", ast_str_buffer(tmpstr));
				}
				ast_free(tmpstr);
				ret = 2;
			}
		}
	}

	ast_str_append(buffer, 0, "%s", posttabs);

	return ret;
}

/*!
 * \internal
 * \brief Parse an <example> node.
 * \since 13.0.0
 *
 * \param fixnode An ast xml pointer to the <example> node.
 * \param buffer The output buffer.
 *
 * \retval 0 if no example node is parsed.
 * \retval 1 if an example node is parsed.
 */
static int xmldoc_parse_example(struct ast_xml_node *fixnode, struct ast_str **buffer)
{
	struct ast_xml_node *node = fixnode;
	const char *tmptext;
	const char *title;
	struct ast_str *stripped_text;
	int ret = 0;

	if (!node || !ast_xml_node_get_children(node)) {
		return ret;
	}

	if (strcasecmp(ast_xml_node_get_name(node), "example")) {
		return ret;
	}

	ret = 1;

	title = ast_xml_get_attribute(node, "title");
	if (title) {
		ast_str_append(buffer, 0, "%s", title);
		ast_xml_free_attr(title);
	}
	ast_str_append(buffer, 0, "\n");

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		tmptext = ast_xml_get_text(node);
		if (tmptext) {
			xmldoc_string_cleanup(tmptext, &stripped_text, 0, 1);
			if (stripped_text) {
				ast_str_append(buffer, 0, "<exampletext>%s</exampletext>\n", ast_str_buffer(stripped_text));
				ast_xml_free_text(tmptext);
				ast_free(stripped_text);
			}
		}
	}

	return ret;
}

/*!
 * \internal
 * \brief Parse special elements defined in 'struct special_tags' special elements must have a <para> element inside them.
 *
 * \param fixnode special tag node pointer.
 * \param tabs put tabs before printing the node content.
 * \param posttabs put posttabs after printing node content.
 * \param buffer Output buffer, the special tags will be appended here.
 *
 * \retval 0 if no special element is parsed.
 * \retval 1 if a special element is parsed (data is appended to buffer).
 * \retval 2 if a special element is parsed and also a <para> element is parsed inside the specialtag.
 */
static int xmldoc_parse_specialtags(struct ast_xml_node *fixnode, const char *tabs, const char *posttabs, struct ast_str **buffer)
{
	struct ast_xml_node *node = fixnode;
	int ret = 0, i;

	if (!node || !ast_xml_node_get_children(node)) {
		return ret;
	}

	for (i = 0; i < ARRAY_LEN(special_tags); i++) {
		if (strcasecmp(ast_xml_node_get_name(node), special_tags[i].tagname)) {
			continue;
		}

		ret = 1;
		/* This is a special tag. */

		/* concat data */
		if (!ast_strlen_zero(special_tags[i].init)) {
			ast_str_append(buffer, 0, "%s%s", tabs, special_tags[i].init);
		}

		if (xmldoc_parse_example(node, buffer)) {
			ret = 1;
			break;
		}

		/* parse <para> elements inside special tags. */
		for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
			/* first <para> just print it without tabs at the begining. */
			if ((xmldoc_parse_para(node, "", posttabs, buffer) == 2)
				|| (xmldoc_parse_info(node, "", posttabs, buffer) == 2)) {
				ret = 2;
			}
		}

		if (!ast_strlen_zero(special_tags[i].end)) {
			ast_str_append(buffer, 0, "%s%s", special_tags[i].end, posttabs);
		}

		break;
	}

	return ret;
}

/*!
 * \internal
 * \brief Parse an <argument> element from the xml documentation.
 *
 * \param fixnode Pointer to the 'argument' xml node.
 * \param insideparameter If we are parsing an <argument> inside a <parameter>.
 * \param paramtabs pre tabs if we are inside a parameter element.
 * \param tabs What to be printed before the argument name.
 * \param buffer Output buffer to put values found inside the <argument> element.
 *
 * \retval 1 If there is content inside the argument.
 * \retval 0 If the argument element is not parsed, or there is no content inside it.
 */
static int xmldoc_parse_argument(struct ast_xml_node *fixnode, int insideparameter, const char *paramtabs, const char *tabs, struct ast_str **buffer)
{
	struct ast_xml_node *node = fixnode;
	const char *argname;
	int count = 0, ret = 0;

	if (!node || !ast_xml_node_get_children(node)) {
		return ret;
	}

	/* Print the argument names */
	argname = ast_xml_get_attribute(node, "name");
	if (!argname) {
		return 0;
	}
	if (xmldoc_has_inside(node, "para") || xmldoc_has_inside(node, "info") || xmldoc_has_specialtags(node)) {
		ast_str_append(buffer, 0, "%s%s%s", tabs, argname, (insideparameter ? "\n" : ""));
		ast_xml_free_attr(argname);
	} else {
		ast_xml_free_attr(argname);
		return 0;
	}

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (xmldoc_parse_common_elements(node, (insideparameter ? paramtabs : (!count ? " - " : tabs)), "\n", buffer) == 2) {
			count++;
			ret = 1;
		}
	}

	return ret;
}

/*!
 * \internal
 * \brief Parse a <variable> node inside a <variablelist> node.
 *
 * \param node The variable node to parse.
 * \param tabs A string to be appended at the begining of the output that will be stored
 *        in buffer.
 * \param buffer This must be an already created ast_str. It will be used
 *        to store the result (if already has something it will be appended to the current
 *        string).
 *
 * \retval 0 if no data is appended.
 * \retval 1 if data is appended.
 */
static int xmldoc_parse_variable(struct ast_xml_node *node, const char *tabs, struct ast_str **buffer)
{
	struct ast_xml_node *tmp;
	const char *valname;
	const char *tmptext;
	struct ast_str *cleanstr;
	int ret = 0, printedpara=0;

	for (tmp = ast_xml_node_get_children(node); tmp; tmp = ast_xml_node_get_next(tmp)) {
		if (xmldoc_parse_common_elements(tmp, (ret ? tabs : ""), "\n", buffer)) {
			printedpara = 1;
			continue;
		}

		if (strcasecmp(ast_xml_node_get_name(tmp), "value")) {
			continue;
		}

		/* Parse a <value> tag only. */
		if (!printedpara) {
			ast_str_append(buffer, 0, "\n");
			printedpara = 1;
		}
		/* Parse each <value name='valuename'>desciption</value> */
		valname = ast_xml_get_attribute(tmp, "name");
		if (valname) {
			ret = 1;
			ast_str_append(buffer, 0, "%s<value>%s</value>", tabs, valname);
			ast_xml_free_attr(valname);
		}
		tmptext = ast_xml_get_text(tmp);
		/* Check inside this node for any explanation about its meaning. */
		if (tmptext) {
			/* Cleanup text. */
			xmldoc_string_cleanup(tmptext, &cleanstr, 1, 0);
			ast_xml_free_text(tmptext);
			if (cleanstr && ast_str_strlen(cleanstr) > 0) {
				ast_str_append(buffer, 0, ":%s", ast_str_buffer(cleanstr));
			}
			ast_free(cleanstr);
		}
		ast_str_append(buffer, 0, "\n");
	}

	return ret;
}

/*!
 * \internal
 * \brief Parse a <variablelist> node and put all the output inside 'buffer'.
 *
 * \param node The variablelist node pointer.
 * \param tabs A string to be appended at the begining of the output that will be stored
 *        in buffer.
 * \param buffer This must be an already created ast_str. It will be used
 *        to store the result (if already has something it will be appended to the current
 *        string).
 *
 * \retval 1 If a <variablelist> element is parsed.
 * \retval 0 On error.
 */
static int xmldoc_parse_variablelist(struct ast_xml_node *node, const char *tabs, struct ast_str **buffer)
{
	struct ast_xml_node *tmp;
	const char *varname;
	char *vartabs;
	int ret = 0;

	if (!node || !ast_xml_node_get_children(node)) {
		return ret;
	}

	if (strcasecmp(ast_xml_node_get_name(node), "variablelist")) {
		return ret;
	}

	/* use this spacing (add 4 spaces) inside a variablelist node. */
	if (ast_asprintf(&vartabs, "%s    ", tabs) < 0) {
		return ret;
	}
	for (tmp = ast_xml_node_get_children(node); tmp; tmp = ast_xml_node_get_next(tmp)) {
		/* We can have a <para> element inside the variable list */
		if (xmldoc_parse_common_elements(tmp, (ret ? tabs : ""), "\n", buffer)) {
			ret = 1;
			continue;
		}

		if (!strcasecmp(ast_xml_node_get_name(tmp), "variable")) {
			/* Store the variable name in buffer. */
			varname = ast_xml_get_attribute(tmp, "name");
			if (varname) {
				ast_str_append(buffer, 0, "%s<variable>%s</variable>: ", tabs, varname);
				ast_xml_free_attr(varname);
				/* Parse the <variable> possible values. */
				xmldoc_parse_variable(tmp, vartabs, buffer);
				ret = 1;
			}
		}
	}

	ast_free(vartabs);

	return ret;
}

/*!
 * \internal
 * \brief Build seealso information for an item
 *
 * \param node	The seealso node to parse
 *
 * \note This method exists for when you already have the node.  This
 * prevents having to lock the documentation tree twice
 *
 * \retval A malloc'd character pointer to the seealso information of the item
 * \retval NULL on failure
 *
 * \since 11
 */
static char *_ast_xmldoc_build_seealso(struct ast_xml_node *node)
{
	char *output;
	struct ast_str *outputstr;
	const char *typename;
	const char *content;
	int first = 1;

	/* Find the <see-also> node. */
	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (!strcasecmp(ast_xml_node_get_name(node), "see-also")) {
			break;
		}
	}

	if (!node || !ast_xml_node_get_children(node)) {
		/* we couldnt find a <see-also> node. */
		return NULL;
	}

	/* prepare the output string. */
	outputstr = ast_str_create(128);
	if (!outputstr) {
		return NULL;
	}

	/* get into the <see-also> node. */
	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (strcasecmp(ast_xml_node_get_name(node), "ref")) {
			continue;
		}

		/* parse the <ref> node. 'type' attribute is required. */
		typename = ast_xml_get_attribute(node, "type");
		if (!typename) {
			continue;
		}
		content = ast_xml_get_text(node);
		if (!content) {
			ast_xml_free_attr(typename);
			continue;
		}
		if (!strcasecmp(typename, "application")) {
			ast_str_append(&outputstr, 0, "%s%s()",	(first ? "" : ", "), content);
		} else if (!strcasecmp(typename, "function")) {
			ast_str_append(&outputstr, 0, "%s%s", (first ? "" : ", "), content);
		} else if (!strcasecmp(typename, "astcli")) {
			ast_str_append(&outputstr, 0, "%s<astcli>%s</astcli>", (first ? "" : ", "), content);
		} else {
			ast_str_append(&outputstr, 0, "%s%s", (first ? "" : ", "), content);
		}
		first = 0;
		ast_xml_free_text(content);
		ast_xml_free_attr(typename);
	}

	output = ast_strdup(ast_str_buffer(outputstr));
	ast_free(outputstr);

	return output;
}

char *ast_xmldoc_build_seealso(const char *type, const char *name, const char *module)
{
	char *output;
	struct ast_xml_node *node;

	if (ast_strlen_zero(type) || ast_strlen_zero(name)) {
		return NULL;
	}

	/* get the application/function root node. */
	node = xmldoc_get_node(type, name, module, documentation_language);
	if (!node || !ast_xml_node_get_children(node)) {
		return NULL;
	}

	output = _ast_xmldoc_build_seealso(node);

	return output;
}

/*!
 * \internal
 * \brief Parse a <enum> node.
 *
 * \param fixnode An ast_xml_node pointer to the <enum> node.
 * \param buffer The output buffer.
 *
 * \retval 0 if content is not found inside the enum element (data is not appended to buffer).
 * \retval 1 if content is found and data is appended to buffer.
 */
static int xmldoc_parse_enum(struct ast_xml_node *fixnode, const char *tabs, struct ast_str **buffer)
{
	struct ast_xml_node *node = fixnode;
	int ret = 0;
	char *optiontabs;

	if (ast_asprintf(&optiontabs, "%s    ", tabs) < 0) {
		return ret;
	}

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (xmldoc_parse_common_elements(node, (ret ? tabs : " - "), "\n", buffer)) {
			ret = 1;
		}

		xmldoc_parse_enumlist(node, optiontabs, buffer);
		xmldoc_parse_parameter(node, optiontabs, buffer);
	}

	ast_free(optiontabs);

	return ret;
}

/*!
 * \internal
 * \brief Parse a <enumlist> node.
 *
 * \param fixnode As ast_xml pointer to the <enumlist> node.
 * \param buffer The ast_str output buffer.
 *
 * \retval 0 if no <enumlist> node was parsed.
 * \retval 1 if a <enumlist> node was parsed.
 */
static int xmldoc_parse_enumlist(struct ast_xml_node *fixnode, const char *tabs, struct ast_str **buffer)
{
	struct ast_xml_node *node = fixnode;
	const char *enumname;
	int ret = 0;

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (strcasecmp(ast_xml_node_get_name(node), "enum")) {
			continue;
		}

		enumname = ast_xml_get_attribute(node, "name");
		if (enumname) {
			ast_str_append(buffer, 0, "%s<enum>%s</enum>", tabs, enumname);
			ast_xml_free_attr(enumname);

			/* parse only enum elements inside a enumlist node. */
			if ((xmldoc_parse_enum(node, tabs, buffer))) {
				ret = 1;
			} else {
				ast_str_append(buffer, 0, "\n");
			}
		}
	}
	return ret;
}

/*!
 * \internal
 * \brief Parse an <option> node.
 *
 * \param fixnode An ast_xml pointer to the <option> node.
 * \param tabs A string to be appended at the begining of each line being added to the
 *             buffer string.
 * \param buffer The output buffer.
 *
 * \retval 0 if no option node is parsed.
 * \retval 1 if an option node is parsed.
 */
static int xmldoc_parse_option(struct ast_xml_node *fixnode, const char *tabs, struct ast_str **buffer)
{
	struct ast_xml_node *node;
	int ret = 0;
	char *optiontabs;

	if (ast_asprintf(&optiontabs, "%s    ", tabs) < 0) {
		return ret;
	}
	for (node = ast_xml_node_get_children(fixnode); node; node = ast_xml_node_get_next(node)) {
		if (!strcasecmp(ast_xml_node_get_name(node), "argument")) {
			/* if this is the first data appended to buffer, print a \n*/
			if (!ret && ast_xml_node_get_children(node)) {
				/* print \n */
				ast_str_append(buffer, 0, "\n");
			}
			if (xmldoc_parse_argument(node, 0, NULL, optiontabs, buffer)) {
				ret = 1;
			}
			continue;
		}

		if (xmldoc_parse_common_elements(node, (ret ? tabs :  ""), "\n", buffer)) {
			ret = 1;
		}

		xmldoc_parse_variablelist(node, optiontabs, buffer);

		xmldoc_parse_enumlist(node, optiontabs, buffer);
	}
	ast_free(optiontabs);

	return ret;
}

/*!
 * \internal
 * \brief Parse an <optionlist> element from the xml documentation.
 *
 * \param fixnode Pointer to the optionlist xml node.
 * \param tabs A string to be appended at the begining of each line being added to the
 *             buffer string.
 * \param buffer Output buffer to put what is inside the optionlist tag.
 */
static void xmldoc_parse_optionlist(struct ast_xml_node *fixnode, const char *tabs, struct ast_str **buffer)
{
	struct ast_xml_node *node;
	const char *optname, *hasparams;
	char *optionsyntax;
	int optparams;

	for (node = ast_xml_node_get_children(fixnode); node; node = ast_xml_node_get_next(node)) {
		/* Start appending every option tag. */
		if (strcasecmp(ast_xml_node_get_name(node), "option")) {
			continue;
		}

		/* Get the option name. */
		optname = ast_xml_get_attribute(node, "name");
		if (!optname) {
			continue;
		}

		optparams = 1;
		hasparams = ast_xml_get_attribute(node, "hasparams");
		if (hasparams && !strcasecmp(hasparams, "optional")) {
			optparams = 2;
		}

		optionsyntax = xmldoc_get_syntax_fun(node, optname, "argument", 0, optparams);
		if (!optionsyntax) {
			ast_xml_free_attr(optname);
			ast_xml_free_attr(hasparams);
			continue;
		}

		ast_str_append(buffer, 0, "%s%s: ", tabs, optionsyntax);

		if (!xmldoc_parse_option(node, tabs, buffer)) {
			ast_str_append(buffer, 0, "\n");
		}
		ast_str_append(buffer, 0, "\n");
		ast_xml_free_attr(optname);
		ast_xml_free_attr(hasparams);
		ast_free(optionsyntax);
	}
}

/*!
 * \internal
 * \brief Parse a 'parameter' tag inside a syntax element.
 *
 * \param fixnode A pointer to the 'parameter' xml node.
 * \param tabs A string to be appended at the beginning of each line being printed inside
 *             'buffer'.
 * \param buffer String buffer to put values found inside the parameter element.
 */
static void xmldoc_parse_parameter(struct ast_xml_node *fixnode, const char *tabs, struct ast_str **buffer)
{
	const char *paramname;
	struct ast_xml_node *node = fixnode;
	int hasarguments, printed = 0;
	char *internaltabs;

	if (strcasecmp(ast_xml_node_get_name(node), "parameter")) {
		return;
	}

	hasarguments = xmldoc_has_inside(node, "argument");
	if (!(paramname = ast_xml_get_attribute(node, "name"))) {
		/* parameter MUST have an attribute name. */
		return;
	}

	if (ast_asprintf(&internaltabs, "%s    ", tabs) < 0) {
		ast_xml_free_attr(paramname);
		return;
	}

	if (!hasarguments && xmldoc_has_nodes(node)) {
		ast_str_append(buffer, 0, "%s\n", paramname);
		ast_xml_free_attr(paramname);
		printed = 1;
	}

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (!strcasecmp(ast_xml_node_get_name(node), "optionlist")) {
			xmldoc_parse_optionlist(node, internaltabs, buffer);
		} else if (!strcasecmp(ast_xml_node_get_name(node), "enumlist")) {
			xmldoc_parse_enumlist(node, internaltabs, buffer);
		} else if (!strcasecmp(ast_xml_node_get_name(node), "argument")) {
			xmldoc_parse_argument(node, 1, internaltabs, (!hasarguments ? "        " : ""), buffer);
		} else if (!strcasecmp(ast_xml_node_get_name(node), "para")) {
			if (!printed) {
				ast_str_append(buffer, 0, "%s\n", paramname);
				ast_xml_free_attr(paramname);
				printed = 1;
			}
			if (xmldoc_parse_para(node, internaltabs, "\n", buffer)) {
				/* If anything ever goes in below this condition before the continue below,
				 * we should probably continue immediately. */
				continue;
			}
			continue;
		} else if (!strcasecmp(ast_xml_node_get_name(node), "info")) {
			if (!printed) {
				ast_str_append(buffer, 0, "%s\n", paramname);
				ast_xml_free_attr(paramname);
				printed = 1;
			}
			if (xmldoc_parse_info(node, internaltabs, "\n", buffer)) {
				/* If anything ever goes in below this condition before the continue below,
				 * we should probably continue immediately. */
				continue;
			}
			continue;
		} else if ((xmldoc_parse_specialtags(node, internaltabs, "\n", buffer))) {
			continue;
		}
	}
	if (!printed) {
		ast_xml_free_attr(paramname);
	}
	ast_free(internaltabs);
}

/*!
 * \internal
 * \brief Parse an 'info' tag inside an element.
 *
 * \param node A pointer to the 'info' xml node.
 * \param tabs A string to be appended at the beginning of each line being printed
 *             inside 'buffer'
 * \param posttabs Add this string after the content of the <para> element, if one exists
 * \param String buffer to put values found inide the info element.
 *
 * \retval 2 if the information contained a para element, and it returned a value of 2
 * \retval 1 if information was put into the buffer
 * \retval 0 if no information was put into the buffer or error
 */
static int xmldoc_parse_info(struct ast_xml_node *node, const char *tabs, const char *posttabs, struct ast_str **buffer)
{
	const char *tech;
	char *internaltabs;
	int internal_ret;
	int ret = 0;

	if (strcasecmp(ast_xml_node_get_name(node), "info")) {
		return ret;
	}

	ast_asprintf(&internaltabs, "%s    ", tabs);
	if (!internaltabs) {
		return ret;
	}

	tech = ast_xml_get_attribute(node, "tech");
	if (tech) {
		ast_str_append(buffer, 0, "%s<note>Technology: %s</note>\n", internaltabs, tech);
		ast_xml_free_attr(tech);
	}

	ret = 1;

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (!strcasecmp(ast_xml_node_get_name(node), "enumlist")) {
			xmldoc_parse_enumlist(node, internaltabs, buffer);
		} else if (!strcasecmp(ast_xml_node_get_name(node), "parameter")) {
			xmldoc_parse_parameter(node, internaltabs, buffer);
		} else if ((internal_ret = xmldoc_parse_common_elements(node, internaltabs, posttabs, buffer))) {
			if (internal_ret > ret) {
				ret = internal_ret;
			}
		}
	}
	ast_free(internaltabs);

	return ret;
}

/*!
 * \internal
 * \brief Build the arguments for an item
 *
 * \param node	The arguments node to parse
 *
 * \note This method exists for when you already have the node.  This
 * prevents having to lock the documentation tree twice
 *
 * \retval A malloc'd character pointer to the arguments for the item
 * \retval NULL on failure
 *
 * \since 11
 */
static char *_ast_xmldoc_build_arguments(struct ast_xml_node *node)
{
	char *retstr = NULL;
	struct ast_str *ret;

	ret = ast_str_create(128);
	if (!ret) {
		return NULL;
	}

	/* Find the syntax field. */
	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		if (!strcasecmp(ast_xml_node_get_name(node), "syntax")) {
			break;
		}
	}

	if (!node || !ast_xml_node_get_children(node)) {
		/* We couldn't find the syntax node. */
		ast_free(ret);
		return NULL;
	}

	for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
		xmldoc_parse_parameter(node, "", &ret);
	}

	if (ast_str_strlen(ret) > 0) {
		/* remove last '\n' */
		char *buf = ast_str_buffer(ret);
		if (buf[ast_str_strlen(ret) - 1] == '\n') {
			ast_str_truncate(ret, -1);
		}
		retstr = ast_strdup(ast_str_buffer(ret));
	}
	ast_free(ret);

	return retstr;
}

char *ast_xmldoc_build_arguments(const char *type, const char *name, const char *module)
{
	struct ast_xml_node *node;

	if (ast_strlen_zero(type) || ast_strlen_zero(name)) {
		return NULL;
	}

	node = xmldoc_get_node(type, name, module, documentation_language);

	if (!node || !ast_xml_node_get_children(node)) {
		return NULL;
	}

	return _ast_xmldoc_build_arguments(node);
}

/*!
 * \internal
 * \brief Return the string within a node formatted with <para> and <variablelist> elements.
 *
 * \param node Parent node where content resides.
 * \param raw If set, return the node's content without further processing.
 * \param raw_wrap Wrap raw text.
 *
 * \retval NULL on error
 * \retval Node content on success.
 */
static struct ast_str *xmldoc_get_formatted(struct ast_xml_node *node, int raw_output, int raw_wrap)
{
	struct ast_xml_node *tmp;
	const char *notcleanret, *tmpstr;
	struct ast_str *ret;

	if (raw_output) {
		/* xmldoc_string_cleanup will allocate the ret object */
		notcleanret = ast_xml_get_text(node);
		tmpstr = notcleanret;
		xmldoc_string_cleanup(ast_skip_blanks(notcleanret), &ret, 0, 0);
		ast_xml_free_text(tmpstr);
	} else {
		ret = ast_str_create(128);
		if (!ret) {
			return NULL;
		}
		for (tmp = ast_xml_node_get_children(node); tmp; tmp = ast_xml_node_get_next(tmp)) {
			/* if found, parse children elements. */
			if (xmldoc_parse_common_elements(tmp, "", "\n", &ret)) {
				continue;
			}
			if (xmldoc_parse_variablelist(tmp, "", &ret)) {
				continue;
			}
			if (xmldoc_parse_enumlist(tmp, "    ", &ret)) {
				continue;
			}
			if (xmldoc_parse_specialtags(tmp, "", "", &ret)) {
				continue;
			}
		}
		/* remove last '\n' */
		/* XXX Don't modify ast_str internals manually */
		tmpstr = ast_str_buffer(ret);
		if (tmpstr[ast_str_strlen(ret) - 1] == '\n') {
			ast_str_truncate(ret, -1);
		}
	}
	return ret;
}

/*!
 * \internal
 * \brief Get the content of a field (synopsis, description, etc) from an asterisk document tree node
 *
 * \param node The node to obtain the information from
 * \param var Name of field to return (synopsis, description, etc).
 * \param raw Field only contains text, no other elements inside it.
 *
 * \retval NULL On error.
 * \retval Field text content on success.
 * \since 11
 */
static char *_xmldoc_build_field(struct ast_xml_node *node, const char *var, int raw)
{
	char *ret = NULL;
	struct ast_str *formatted;

	node = ast_xml_find_element(ast_xml_node_get_children(node), var, NULL, NULL);

	if (!node || !ast_xml_node_get_children(node)) {
		return ret;
	}

	formatted = xmldoc_get_formatted(node, raw, raw);
	if (formatted && ast_str_strlen(formatted) > 0) {
		ret = ast_strdup(ast_str_buffer(formatted));
	}
	ast_free(formatted);

	return ret;
}

/*!
 * \internal
 * \brief Get the content of a field (synopsis, description, etc) from an asterisk document tree
 *
 * \param type Type of element (application, function, ...).
 * \param name Name of element (Dial, Echo, Playback, ...).
 * \param var Name of field to return (synopsis, description, etc).
 * \param module
 * \param raw Field only contains text, no other elements inside it.
 *
 * \retval NULL On error.
 * \retval Field text content on success.
 */
static char *xmldoc_build_field(const char *type, const char *name, const char *module, const char *var, int raw)
{
	struct ast_xml_node *node;

	if (ast_strlen_zero(type) || ast_strlen_zero(name)) {
		ast_log(LOG_ERROR, "Tried to look in XML tree with faulty values.\n");
		return NULL;
	}

	node = xmldoc_get_node(type, name, module, documentation_language);

	if (!node) {
		ast_log(LOG_WARNING, "Couldn't find %s %s in XML documentation\n", type, name);
		return NULL;
	}

	return _xmldoc_build_field(node, var, raw);
}

/*!
 * \internal
 * \brief Build the synopsis for an item
 *
 * \param node The synopsis node
 *
 * \note This method exists for when you already have the node.  This
 * prevents having to lock the documentation tree twice
 *
 * \retval A malloc'd character pointer to the synopsis information
 * \retval NULL on failure
 * \since 11
 */
static char *_ast_xmldoc_build_synopsis(struct ast_xml_node *node)
{
	return _xmldoc_build_field(node, "synopsis", 1);
}

char *ast_xmldoc_build_synopsis(const char *type, const char *name, const char *module)
{
	return xmldoc_build_field(type, name, module, "synopsis", 1);
}

/*!
 * \internal
 * \brief Build the descripton for an item
 *
 * \param node	The description node to parse
 *
 * \note This method exists for when you already have the node.  This
 * prevents having to lock the documentation tree twice
 *
 * \retval A malloc'd character pointer to the arguments for the item
 * \retval NULL on failure
 * \since 11
 */
static char *_ast_xmldoc_build_description(struct ast_xml_node *node)
{
	return _xmldoc_build_field(node, "description", 0);
}

char *ast_xmldoc_build_description(const char *type, const char *name, const char *module)
{
	return xmldoc_build_field(type, name, module, "description", 0);
}

/*!
 * \internal
 * \brief ast_xml_doc_item ao2 destructor
 * \since 11
 */
static void ast_xml_doc_item_destructor(void *obj)
{
	struct ast_xml_doc_item *doc = obj;

	if (!doc) {
		return;
	}

	ast_free(doc->syntax);
	ast_free(doc->seealso);
	ast_free(doc->arguments);
	ast_free(doc->synopsis);
	ast_free(doc->description);
	ast_string_field_free_memory(doc);

	if (AST_LIST_NEXT(doc, next)) {
		ao2_ref(AST_LIST_NEXT(doc, next), -1);
		AST_LIST_NEXT(doc, next) = NULL;
	}
}

/*!
 * \internal
 * \brief Create an ao2 ref counted ast_xml_doc_item
 *
 * \param name The name of the item
 * \param type The item's source type
 * \since 11
 */
static struct ast_xml_doc_item *ast_xml_doc_item_alloc(const char *name, const char *type)
{
	struct ast_xml_doc_item *item;

	if (!(item = ao2_alloc(sizeof(*item), ast_xml_doc_item_destructor))) {
		ast_log(AST_LOG_ERROR, "Failed to allocate memory for ast_xml_doc_item instance\n");
		return NULL;
	}

	if (   !(item->syntax = ast_str_create(128))
		|| !(item->seealso = ast_str_create(128))
		|| !(item->arguments = ast_str_create(128))
		|| !(item->synopsis = ast_str_create(128))
		|| !(item->description = ast_str_create(128))) {
		ast_log(AST_LOG_ERROR, "Failed to allocate strings for ast_xml_doc_item instance\n");
		goto ast_xml_doc_item_failure;
	}

	if (ast_string_field_init(item, 64)) {
		ast_log(AST_LOG_ERROR, "Failed to initialize string field for ast_xml_doc_item instance\n");
		goto ast_xml_doc_item_failure;
	}
	ast_string_field_set(item, name, name);
	ast_string_field_set(item, type, type);

	return item;

ast_xml_doc_item_failure:
	ao2_ref(item, -1);
	return NULL;
}

/*!
 * \internal
 * \brief ao2 item hash function for ast_xml_doc_item
 * \since 11
 */
static int ast_xml_doc_item_hash(const void *obj, const int flags)
{
	const struct ast_xml_doc_item *item = obj;
	const char *name = (flags & OBJ_KEY) ? obj : item->name;
	return ast_str_case_hash(name);
}

/*!
 * \internal
 * \brief ao2 item comparison function for ast_xml_doc_item
 * \since 11
 */
static int ast_xml_doc_item_cmp(void *obj, void *arg, int flags)
{
	struct ast_xml_doc_item *left = obj;
	struct ast_xml_doc_item *right = arg;
	const char *match = (flags & OBJ_KEY) ? arg : right->name;
	return strcasecmp(left->name, match) ? 0 : (CMP_MATCH | CMP_STOP);
}

/*!
 * \internal
 * \brief Build an XML documentation item
 *
 * \param node The root node for the item
 * \param name The name of the item
 * \param type The item's source type
 *
 * \retval NULL on failure
 * \retval An ao2 ref counted object
 * \since 11
 */
static struct ast_xml_doc_item *xmldoc_build_documentation_item(struct ast_xml_node *node, const char *name, const char *type)
{
	struct ast_xml_doc_item *item;
	char *syntax;
	char *seealso;
	char *arguments;
	char *synopsis;
	char *description;

	if (!(item = ast_xml_doc_item_alloc(name, type))) {
		return NULL;
	}
	item->node = node;

	syntax = _ast_xmldoc_build_syntax(node, type, name);
	seealso = _ast_xmldoc_build_seealso(node);
	arguments = _ast_xmldoc_build_arguments(node);
	synopsis = _ast_xmldoc_build_synopsis(node);
	description = _ast_xmldoc_build_description(node);

	if (syntax) {
		ast_str_set(&item->syntax, 0, "%s", syntax);
	}
	if (seealso) {
		ast_str_set(&item->seealso, 0, "%s", seealso);
	}
	if (arguments) {
		ast_str_set(&item->arguments, 0, "%s", arguments);
	}
	if (synopsis) {
		ast_str_set(&item->synopsis, 0, "%s", synopsis);
	}
	if (description) {
		ast_str_set(&item->description, 0, "%s", description);
	}

	ast_free(syntax);
	ast_free(seealso);
	ast_free(arguments);
	ast_free(synopsis);
	ast_free(description);

	return item;
}

/*!
 * \internal
 * \brief Build the list responses for an item
 *
 * \param manager_action The action node to parse
 *
 * \note This method exists for when you already have the node.  This
 * prevents having to lock the documentation tree twice
 *
 * \retval A list of ast_xml_doc_items
 * \retval NULL on failure
 *
 * \since 13.0.0
 */
static struct ast_xml_doc_item *xmldoc_build_list_responses(struct ast_xml_node *manager_action)
{
	struct ast_xml_node *event;
	struct ast_xml_node *responses;
	struct ast_xml_node *list_elements;
	struct ast_xml_doc_item_list root;

	AST_LIST_HEAD_INIT(&root);

	responses = ast_xml_find_element(ast_xml_node_get_children(manager_action), "responses", NULL, NULL);
	if (!responses) {
		return NULL;
	}

	list_elements = ast_xml_find_element(ast_xml_node_get_children(responses), "list-elements", NULL, NULL);
	if (!list_elements) {
		return NULL;
	}

	/* Iterate over managerEvent nodes */
	for (event = ast_xml_node_get_children(list_elements); event; event = ast_xml_node_get_next(event)) {
		struct ast_xml_node *event_instance;
		RAII_VAR(const char *, name, ast_xml_get_attribute(event, "name"),
			ast_xml_free_attr);
		struct ast_xml_doc_item *new_item;

		if (!name || strcmp(ast_xml_node_get_name(event), "managerEvent")) {
			continue;
		}

		event_instance = ast_xml_find_element(ast_xml_node_get_children(event),
			"managerEventInstance", NULL, NULL);
		new_item = xmldoc_build_documentation_item(event_instance, name, "managerEvent");
		if (!new_item) {
			ao2_cleanup(AST_LIST_FIRST(&root));
			return NULL;
		}

		AST_LIST_INSERT_TAIL(&root, new_item, next);
	}

	return AST_LIST_FIRST(&root);
}

struct ast_xml_doc_item *ast_xmldoc_build_list_responses(const char *type, const char *name, const char *module)
{
	struct ast_xml_node *node;

	if (ast_strlen_zero(type) || ast_strlen_zero(name)) {
		return NULL;
	}

	node = xmldoc_get_node(type, name, module, documentation_language);

	if (!node || !ast_xml_node_get_children(node)) {
		return NULL;
	}

	return xmldoc_build_list_responses(node);
}

/*!
 * \internal
 * \brief Build the final response for an item
 *
 * \param manager_action The action node to parse
 *
 * \note This method exists for when you already have the node.  This
 * prevents having to lock the documentation tree twice
 *
 * \retval An ast_xml_doc_item
 * \retval NULL on failure
 *
 * \since 13.0.0
 */
static struct ast_xml_doc_item *xmldoc_build_final_response(struct ast_xml_node *manager_action)
{
	struct ast_xml_node *responses;
	struct ast_xml_node *final_response_event;
	struct ast_xml_node *event_instance;

	responses = ast_xml_find_element(ast_xml_node_get_children(manager_action),
		"responses", NULL, NULL);
	if (!responses) {
		return NULL;
	}

	final_response_event = ast_xml_find_element(ast_xml_node_get_children(responses),
		"managerEvent", NULL, NULL);
	if (!final_response_event) {
		return NULL;
	}

	event_instance = ast_xml_find_element(ast_xml_node_get_children(final_response_event),
		"managerEventInstance", NULL, NULL);
	if (!event_instance) {
		return NULL;
	} else {
		const char *name;
		struct ast_xml_doc_item *res;

		name = ast_xml_get_attribute(final_response_event, "name");
		res = xmldoc_build_documentation_item(event_instance, name, "managerEvent");
		ast_xml_free_attr(name);
		return res;
	}

}

struct ast_xml_doc_item *ast_xmldoc_build_final_response(const char *type, const char *name, const char *module)
{
	struct ast_xml_node *node;

	if (ast_strlen_zero(type) || ast_strlen_zero(name)) {
		return NULL;
	}

	node = xmldoc_get_node(type, name, module, documentation_language);

	if (!node || !ast_xml_node_get_children(node)) {
		return NULL;
	}

	return xmldoc_build_final_response(node);
}

struct ast_xml_xpath_results *__attribute__((format(printf, 1, 2))) ast_xmldoc_query(const char *fmt, ...)
{
	struct ast_xml_xpath_results *results = NULL;
	struct documentation_tree *doctree;
	RAII_VAR(struct ast_str *, xpath_str, ast_str_create(128), ast_free);
	va_list ap;
	int res;

	if (!xpath_str) {
		return NULL;
	}

	va_start(ap, fmt);
	res = ast_str_set_va(&xpath_str, 0, fmt, ap);
	va_end(ap);
	if (res == AST_DYNSTR_BUILD_FAILED) {
		return NULL;
	}

	AST_RWLIST_RDLOCK(&xmldoc_tree);
	AST_LIST_TRAVERSE(&xmldoc_tree, doctree, entry) {
		if (!(results = ast_xml_query(doctree->doc, ast_str_buffer(xpath_str)))) {
			continue;
		}
		break;
	}
	AST_RWLIST_UNLOCK(&xmldoc_tree);

	return results;
}

static void build_config_docs(struct ast_xml_node *cur, struct ast_xml_doc_item_list *root)
{
	struct ast_xml_node *iter;
	struct ast_xml_doc_item *item;

	for (iter = ast_xml_node_get_children(cur); iter; iter = ast_xml_node_get_next(iter)) {
		const char *iter_name;
		if (strncasecmp(ast_xml_node_get_name(iter), "config", 6)) {
			continue;
		}
		iter_name = ast_xml_get_attribute(iter, "name");
		/* Now add all of the child config-related items to the list */
		if (!(item = xmldoc_build_documentation_item(iter, iter_name, ast_xml_node_get_name(iter)))) {
			ast_log(LOG_ERROR, "Could not build documentation for '%s:%s'\n", ast_xml_node_get_name(iter), iter_name);
			ast_xml_free_attr(iter_name);
			break;
		}
		ast_xml_free_attr(iter_name);
		if (!strcasecmp(ast_xml_node_get_name(iter), "configOption")) {
			const char *name = ast_xml_get_attribute(cur, "name");
			ast_string_field_set(item, ref, name);
			ast_xml_free_attr(name);
		}
		AST_LIST_INSERT_TAIL(root, item, next);
		build_config_docs(iter, root);
	}
}

int ast_xmldoc_regenerate_doc_item(struct ast_xml_doc_item *item)
{
	const char *name;
	char *syntax;
	char *seealso;
	char *arguments;
	char *synopsis;
	char *description;

	if (!item || !item->node) {
		return -1;
	}

	name = ast_xml_get_attribute(item->node, "name");
	if (!name) {
		return -1;
	}

	syntax = _ast_xmldoc_build_syntax(item->node, item->type, name);
	seealso = _ast_xmldoc_build_seealso(item->node);
	arguments = _ast_xmldoc_build_arguments(item->node);
	synopsis = _ast_xmldoc_build_synopsis(item->node);
	description = _ast_xmldoc_build_description(item->node);

	if (syntax) {
		ast_str_set(&item->syntax, 0, "%s", syntax);
	}
	if (seealso) {
		ast_str_set(&item->seealso, 0, "%s", seealso);
	}
	if (arguments) {
		ast_str_set(&item->arguments, 0, "%s", arguments);
	}
	if (synopsis) {
		ast_str_set(&item->synopsis, 0, "%s", synopsis);
	}
	if (description) {
		ast_str_set(&item->description, 0, "%s", description);
	}

	ast_free(syntax);
	ast_free(seealso);
	ast_free(arguments);
	ast_free(synopsis);
	ast_free(description);
	ast_xml_free_attr(name);
	return 0;
}

struct ao2_container *ast_xmldoc_build_documentation(const char *type)
{
	struct ao2_container *docs;
	struct ast_xml_node *node = NULL, *instance = NULL;
	struct documentation_tree *doctree;
	const char *name;

	if (!(docs = ao2_container_alloc(127, ast_xml_doc_item_hash, ast_xml_doc_item_cmp))) {
		ast_log(AST_LOG_ERROR, "Failed to create container for xml document item instances\n");
		return NULL;
	}

	AST_RWLIST_RDLOCK(&xmldoc_tree);
	AST_LIST_TRAVERSE(&xmldoc_tree, doctree, entry) {
		/* the core xml documents have priority over thirdparty document. */
		node = ast_xml_get_root(doctree->doc);
		if (!node) {
			break;
		}

		for (node = ast_xml_node_get_children(node); node; node = ast_xml_node_get_next(node)) {
			struct ast_xml_doc_item *item = NULL;

			/* Ignore empty nodes or nodes that aren't of the type requested */
			if (!ast_xml_node_get_children(node) || strcasecmp(ast_xml_node_get_name(node), type)) {
				continue;
			}
			name = ast_xml_get_attribute(node, "name");
			if (!name) {
				continue;
			}

			switch (xmldoc_get_syntax_type(type)) {
			case MANAGER_EVENT_SYNTAX:
			{
				struct ast_xml_doc_item_list root;

				AST_LIST_HEAD_INIT(&root);
				for (instance = ast_xml_node_get_children(node); instance; instance = ast_xml_node_get_next(instance)) {
					struct ast_xml_doc_item *temp;
					if (!ast_xml_node_get_children(instance) || strcasecmp(ast_xml_node_get_name(instance), "managerEventInstance")) {
						continue;
					}
					temp = xmldoc_build_documentation_item(instance, name, type);
					if (!temp) {
						break;
					}
					AST_LIST_INSERT_TAIL(&root, temp, next);
				}
				item = AST_LIST_FIRST(&root);
				break;
			}
			case CONFIG_INFO_SYNTAX:
			{
				RAII_VAR(const char *, name, ast_xml_get_attribute(node, "name"), ast_xml_free_attr);

				if (!ast_xml_node_get_children(node) || strcasecmp(ast_xml_node_get_name(node), "configInfo")) {
					break;
				}

				item = xmldoc_build_documentation_item(node, name, "configInfo");
				if (item) {
					struct ast_xml_doc_item_list root;

					AST_LIST_HEAD_INIT(&root);
					AST_LIST_INSERT_TAIL(&root, item, next);
					build_config_docs(node, &root);
				}
				break;
			}
			default:
				item = xmldoc_build_documentation_item(node, name, type);
			}
			ast_xml_free_attr(name);

			if (item) {
				ao2_link(docs, item);
				ao2_t_ref(item, -1, "Dispose of creation ref");
			}
		}
	}
	AST_RWLIST_UNLOCK(&xmldoc_tree);

	return docs;
}

int ast_xmldoc_regenerate_doc_item(struct ast_xml_doc_item *item);


#if !defined(HAVE_GLOB_NOMAGIC) || !defined(HAVE_GLOB_BRACE) || defined(DEBUG_NONGNU)
static int xml_pathmatch(char *xmlpattern, int xmlpattern_maxlen, glob_t *globbuf)
{
	int globret;

	snprintf(xmlpattern, xmlpattern_maxlen, "%s/documentation/thirdparty/*-%s.xml",
		ast_config_AST_DATA_DIR, documentation_language);
	if((globret = glob(xmlpattern, GLOB_NOCHECK, NULL, globbuf))) {
		return globret;
	}

	snprintf(xmlpattern, xmlpattern_maxlen, "%s/documentation/thirdparty/*-%.2s_??.xml",
		ast_config_AST_DATA_DIR, documentation_language);
	if((globret = glob(xmlpattern, GLOB_APPEND | GLOB_NOCHECK, NULL, globbuf))) {
		return globret;
	}

	snprintf(xmlpattern, xmlpattern_maxlen, "%s/documentation/thirdparty/*-%s.xml",
		ast_config_AST_DATA_DIR, default_documentation_language);
	if((globret = glob(xmlpattern, GLOB_APPEND | GLOB_NOCHECK, NULL, globbuf))) {
		return globret;
	}

	snprintf(xmlpattern, xmlpattern_maxlen, "%s/documentation/*-%s.xml",
		ast_config_AST_DATA_DIR, documentation_language);
	if((globret = glob(xmlpattern, GLOB_APPEND | GLOB_NOCHECK, NULL, globbuf))) {
		return globret;
	}

	snprintf(xmlpattern, xmlpattern_maxlen, "%s/documentation/*-%.2s_??.xml",
		ast_config_AST_DATA_DIR, documentation_language);
	if((globret = glob(xmlpattern, GLOB_APPEND | GLOB_NOCHECK, NULL, globbuf))) {
		return globret;
	}

	snprintf(xmlpattern, xmlpattern_maxlen, "%s/documentation/*-%s.xml",
		ast_config_AST_DATA_DIR, default_documentation_language);
	globret = glob(xmlpattern, GLOB_APPEND | GLOB_NOCHECK, NULL, globbuf);

	return globret;
}
#endif

static char *handle_dump_docs(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct documentation_tree *doctree;
	struct ast_xml_doc *dumpdoc;
	struct ast_xml_node *dumproot;
	FILE *f;

	switch (cmd) {
	case CLI_INIT:
		e->command = "xmldoc dump";
		e->usage =
			"Usage: xmldoc dump <filename>\n"
			"  Dump XML documentation to a file\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	dumpdoc = ast_xml_new();
	if (!dumpdoc) {
		ast_log(LOG_ERROR, "Could not create new XML document\n");
		return CLI_FAILURE;
	}

	dumproot = ast_xml_new_node("docs");
	if (!dumproot) {
		ast_xml_close(dumpdoc);
		ast_log(LOG_ERROR, "Could not create new XML root node\n");
		return CLI_FAILURE;
	}

	ast_xml_set_root(dumpdoc, dumproot);

	AST_RWLIST_RDLOCK(&xmldoc_tree);
	AST_LIST_TRAVERSE(&xmldoc_tree, doctree, entry) {
		struct ast_xml_node *root_node = ast_xml_get_root(doctree->doc);
		struct ast_xml_node *kids = ast_xml_node_get_children(root_node);
		struct ast_xml_node *kids_copy;

		/* If there are no kids someone screwed up, but we check anyway. */
		if (!kids) {
			continue;
		}

		kids_copy = ast_xml_copy_node_list(kids);
		if (!kids_copy) {
			ast_xml_close(dumpdoc);
			ast_log(LOG_ERROR, "Could not create copy of XML node list\n");
			return CLI_FAILURE;
		}

		ast_xml_add_child_list(dumproot, kids_copy);
	}
	AST_RWLIST_UNLOCK(&xmldoc_tree);

	if (!(f = fopen(a->argv[2], "w"))) {
		ast_xml_close(dumpdoc);
		ast_log(LOG_ERROR, "Could not open file '%s': %s\n", a->argv[2], strerror(errno));
		return CLI_FAILURE;
	}

	ast_xml_doc_dump_file(f, dumpdoc);
	ast_xml_close(dumpdoc);

	fclose(f);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_dump_xmldocs = AST_CLI_DEFINE(handle_dump_docs, "Dump the XML docs to the specified file");

/*! \brief Close and unload XML documentation. */
static void xmldoc_unload_documentation(void)
{
	struct documentation_tree *doctree;

	ast_cli_unregister(&cli_dump_xmldocs);

	AST_RWLIST_WRLOCK(&xmldoc_tree);
	while ((doctree = AST_RWLIST_REMOVE_HEAD(&xmldoc_tree, entry))) {
		ast_free(doctree->filename);
		ast_xml_close(doctree->doc);
		ast_free(doctree);
	}
	AST_RWLIST_UNLOCK(&xmldoc_tree);

	ast_xml_finish();
}

int ast_xmldoc_load_documentation(void)
{
	struct ast_xml_node *root_node;
	struct ast_xml_doc *tmpdoc;
	struct documentation_tree *doc_tree;
	char *xmlpattern;
	struct ast_config *cfg = NULL;
	struct ast_variable *var = NULL;
	struct ast_flags cnfflags = { 0 };
	int globret, i, dup, duplicate;
	glob_t globbuf;
#if !defined(HAVE_GLOB_NOMAGIC) || !defined(HAVE_GLOB_BRACE) || defined(DEBUG_NONGNU)
	int xmlpattern_maxlen;
#endif

	/* setup default XML documentation language */
	snprintf(documentation_language, sizeof(documentation_language), default_documentation_language);

	if ((cfg = ast_config_load2("asterisk.conf", "" /* core can't reload */, cnfflags)) && cfg != CONFIG_STATUS_FILEINVALID) {
		for (var = ast_variable_browse(cfg, "options"); var; var = var->next) {
			if (!strcasecmp(var->name, "documentation_language")) {
				if (!ast_strlen_zero(var->value)) {
					snprintf(documentation_language, sizeof(documentation_language), "%s", var->value);
				}
			}
		}
		ast_config_destroy(cfg);
	}

	/* initialize the XML library. */
	ast_xml_init();

	ast_cli_register(&cli_dump_xmldocs);
	/* register function to be run when asterisk finish. */
	ast_register_cleanup(xmldoc_unload_documentation);

	globbuf.gl_offs = 0;    /* slots to reserve in gl_pathv */

#if !defined(HAVE_GLOB_NOMAGIC) || !defined(HAVE_GLOB_BRACE) || defined(DEBUG_NONGNU)
	xmlpattern_maxlen = strlen(ast_config_AST_DATA_DIR) + strlen("/documentation/thirdparty") + strlen("/*-??_??.xml") + 1;
	xmlpattern = ast_malloc(xmlpattern_maxlen);
	globret = xml_pathmatch(xmlpattern, xmlpattern_maxlen, &globbuf);
#else
	/* Get every *-LANG.xml file inside $(ASTDATADIR)/documentation */
	if (ast_asprintf(&xmlpattern, "%s/documentation{/thirdparty/,/}*-{%s,%.2s_??,%s}.xml", ast_config_AST_DATA_DIR,
		documentation_language, documentation_language, default_documentation_language) < 0) {
		return 1;
	}
	globret = glob(xmlpattern, MY_GLOB_FLAGS, NULL, &globbuf);
#endif

	ast_debug(3, "gl_pathc %zu\n", (size_t)globbuf.gl_pathc);
	if (globret == GLOB_NOSPACE) {
		ast_log(LOG_WARNING, "XML load failure, glob expansion of pattern '%s' failed: Not enough memory\n", xmlpattern);
		ast_free(xmlpattern);
		return 1;
	} else if (globret  == GLOB_ABORTED) {
		ast_log(LOG_WARNING, "XML load failure, glob expansion of pattern '%s' failed: Read error\n", xmlpattern);
		ast_free(xmlpattern);
		return 1;
	}
	ast_free(xmlpattern);

	AST_RWLIST_WRLOCK(&xmldoc_tree);
	/* loop over expanded files */
	for (i = 0; i < globbuf.gl_pathc; i++) {
		/* check for duplicates (if we already [try to] open the same file. */
		duplicate = 0;
		for (dup = 0; dup < i; dup++) {
			if (!strcmp(globbuf.gl_pathv[i], globbuf.gl_pathv[dup])) {
				duplicate = 1;
				break;
			}
		}
		if (duplicate || strchr(globbuf.gl_pathv[i], '*')) {
		/* skip duplicates as well as pathnames not found
		 * (due to use of GLOB_NOCHECK in xml_pathmatch) */
			continue;
		}
		tmpdoc = NULL;
		tmpdoc = ast_xml_open(globbuf.gl_pathv[i]);
		if (!tmpdoc) {
			ast_log(LOG_ERROR, "Could not open XML documentation at '%s'\n", globbuf.gl_pathv[i]);
			continue;
		}
		/* Get doc root node and check if it starts with '<docs>' */
		root_node = ast_xml_get_root(tmpdoc);
		if (!root_node) {
			ast_log(LOG_ERROR, "Error getting documentation root node\n");
			ast_xml_close(tmpdoc);
			continue;
		}
		/* Check root node name for malformed xmls. */
		if (strcmp(ast_xml_node_get_name(root_node), "docs")) {
			ast_log(LOG_ERROR, "Documentation file is not well formed!\n");
			ast_xml_close(tmpdoc);
			continue;
		}
		doc_tree = ast_calloc(1, sizeof(*doc_tree));
		if (!doc_tree) {
			ast_log(LOG_ERROR, "Unable to allocate documentation_tree structure!\n");
			ast_xml_close(tmpdoc);
			continue;
		}
		doc_tree->doc = tmpdoc;
		doc_tree->filename = ast_strdup(globbuf.gl_pathv[i]);
		AST_RWLIST_INSERT_TAIL(&xmldoc_tree, doc_tree, entry);
	}
	AST_RWLIST_UNLOCK(&xmldoc_tree);

	globfree(&globbuf);

	return 0;
}

#endif /* AST_XML_DOCS */
