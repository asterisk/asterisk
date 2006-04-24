/*
 * "$Id$"
 *
 * Documentation generator using Mini-XML, a small XML-like file parsing
 * library.
 *
 * Copyright 2003-2005 by Michael Sweet.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contents:
 *
 *   main()                - Main entry for test program.
 *   add_variable()        - Add a variable or argument.
 *   safe_strcpy()         - Copy a string allowing for overlapping strings.
 *   scan_file()           - Scan a source file.
 *   sort_node()           - Insert a node sorted into a tree.
 *   update_comment()      - Update a comment node.
 *   write_documentation() - Write HTML documentation.
 *   write_element()       - Write an elements text nodes.
 *   write_string()        - Write a string, quoting XHTML special chars
 *                           as needed...
 *   ws_cb()               - Whitespace callback for saving.
 */

/*
 * Include necessary headers...
 */

#include "config.h"
#include "mxml.h"


/*
 * This program scans source and header files and produces public API
 * documentation for code that conforms to the CUPS Configuration
 * Management Plan (CMP) coding standards.  Please see the following web
 * page for details:
 *
 *     http://www.cups.org/cmp.html
 *
 * Using Mini-XML, this program creates and maintains an XML representation
 * of the public API code documentation which can then be converted to HTML
 * as desired.  The following is a poor-man's schema:
 *
 * <?xml version="1.0"?>
 * <mxmldoc xmlns="http://www.easysw.com"
 *  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
 *  xsi:schemaLocation="http://www.easysw.com/~mike/mxml/mxmldoc.xsd">
 *
 *   <namespace name="">                        [optional...]
 *     <constant name="">
 *       <description>descriptive text</description>
 *     </constant>
 *  
 *     <enumeration name="">
 *       <description>descriptive text</description>
 *       <constant name="">...</constant>
 *     </enumeration>
 *  
 *     <typedef name="">
 *       <description>descriptive text</description>
 *       <type>type string</type>
 *     </typedef>
 *  
 *     <function name="" scope="">
 *       <description>descriptive text</description>
 *       <argument name="" direction="I|O|IO" default="">
 *         <description>descriptive text</description>
 *         <type>type string</type>
 *       </argument>
 *       <returnvalue>
 *         <description>descriptive text</description>
 *         <type>type string</type>
 *       </returnvalue>
 *       <seealso>function names separated by spaces</seealso>
 *     </function>
 *  
 *     <variable name="" scope="">
 *       <description>descriptive text</description>
 *       <type>type string</type>
 *     </variable>
 *  
 *     <struct name="">
 *       <description>descriptive text</description>
 *       <variable name="">...</variable>
 *       <function name="">...</function>
 *     </struct>
 *  
 *     <union name="">
 *       <description>descriptive text</description>
 *       <variable name="">...</variable>
 *     </union>
 *  
 *     <class name="" parent="">
 *       <description>descriptive text</description>
 *       <class name="">...</class>
 *       <enumeration name="">...</enumeration>
 *       <function name="">...</function>
 *       <struct name="">...</struct>
 *       <variable name="">...</variable>
 *     </class>
 *   </namespace>
 * </mxmldoc>
 */
 

/*
 * Basic states for file parser...
 */

#define STATE_NONE		0	/* No state - whitespace, etc. */
#define STATE_PREPROCESSOR	1	/* Preprocessor directive */
#define STATE_C_COMMENT		2	/* Inside a C comment */
#define STATE_CXX_COMMENT	3	/* Inside a C++ comment */
#define STATE_STRING		4	/* Inside a string constant */
#define STATE_CHARACTER		5	/* Inside a character constant */
#define STATE_IDENTIFIER	6	/* Inside a keyword/identifier */


/*
 * Local functions...
 */

static mxml_node_t	*add_variable(mxml_node_t *parent, const char *name,
			              mxml_node_t *type);
static void		safe_strcpy(char *dst, const char *src);
static int		scan_file(const char *filename, FILE *fp,
			          mxml_node_t *doc);
static void		sort_node(mxml_node_t *tree, mxml_node_t *func);
static void		update_comment(mxml_node_t *parent,
			               mxml_node_t *comment);
static void		write_documentation(mxml_node_t *doc);
static void		write_element(mxml_node_t *doc, mxml_node_t *element);
static void		write_string(const char *s);
static const char	*ws_cb(mxml_node_t *node, int where);


/*
 * 'main()' - Main entry for test program.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line args */
{
  int		i;			/* Looping var */
  FILE		*fp;			/* File to read */
  mxml_node_t	*doc;			/* XML documentation tree */
  mxml_node_t	*mxmldoc;		/* mxmldoc node */


 /*
  * Check arguments...
  */

  if (argc < 2)
  {
    fputs("Usage: mxmldoc filename.xml [source files] >filename.html\n", stderr);
    return (1);
  }

 /*
  * Read the XML documentation file, if it exists...
  */

  if ((fp = fopen(argv[1], "r")) != NULL)
  {
   /*
    * Read the existing XML file...
    */

    doc = mxmlLoadFile(NULL, fp, MXML_NO_CALLBACK);

    fclose(fp);

    if (!doc)
    {
      mxmldoc = NULL;

      fprintf(stderr, "mxmldoc: Unable to read the XML documentation file \"%s\"!\n",
              argv[1]);
    }
    else if ((mxmldoc = mxmlFindElement(doc, doc, "mxmldoc", NULL,
                                        NULL, MXML_DESCEND)) == NULL)
    {
      fprintf(stderr, "mxmldoc: XML documentation file \"%s\" is missing <mxmldoc> node!!\n",
              argv[1]);

      mxmlDelete(doc);
      doc = NULL;
    }
  }
  else
  {
    doc     = NULL;
    mxmldoc = NULL;
  }

  if (!doc)
  {
   /*
    * Create an empty XML documentation file...
    */

    doc = mxmlNewElement(NULL, "?xml version=\"1.0\"?");

    mxmldoc = mxmlNewElement(doc, "mxmldoc");

#ifdef MXML_INCLUDE_SCHEMA
   /*
    * Currently we don't include the schema/namespace stuff with the
    * XML output since some validators don't seem to like it...
    */

    mxmlElementSetAttr(mxmldoc, "xmlns", "http://www.easysw.com");
    mxmlElementSetAttr(mxmldoc, "xmlns:xsi",
                       "http://www.w3.org/2001/XMLSchema-instance");
    mxmlElementSetAttr(mxmldoc, "xsi:schemaLocation",
                       "http://www.easysw.com/~mike/mxml/mxmldoc.xsd");
#endif /* MXML_INCLUDE_SCHEMA */
  }

 /*
  * Loop through all of the source files...
  */

  for (i = 2; i < argc; i ++)
    if ((fp = fopen(argv[i], "r")) == NULL)
    {
      fprintf(stderr, "Unable to open source file \"%s\": %s\n", argv[i],
              strerror(errno));
      mxmlDelete(doc);
      return (1);
    }
    else if (scan_file(argv[i], fp, mxmldoc))
    {
      fclose(fp);
      mxmlDelete(doc);
      return (1);
    }
    else
      fclose(fp);

  if (argc > 2)
  {
   /*
    * Save the updated XML documentation file...
    */

    if ((fp = fopen(argv[1], "w")) != NULL)
    {
     /*
      * Write over the existing XML file...
      */

      if (mxmlSaveFile(doc, fp, ws_cb))
      {
	fprintf(stderr, "Unable to write the XML documentation file \"%s\": %s!\n",
        	argv[1], strerror(errno));
	fclose(fp);
	mxmlDelete(doc);
	return (1);
      }

      fclose(fp);
    }
    else
    {
      fprintf(stderr, "Unable to create the XML documentation file \"%s\": %s!\n",
              argv[1], strerror(errno));
      mxmlDelete(doc);
      return (1);
    }
  }

 /*
  * Write HTML documentation...
  */

  write_documentation(mxmldoc);

 /*
  * Delete the tree and return...
  */

  mxmlDelete(doc);

  return (0);
}


/*
 * 'add_variable()' - Add a variable or argument.
 */

static mxml_node_t *			/* O - New variable/argument */
add_variable(mxml_node_t *parent,	/* I - Parent node */
             const char  *name,		/* I - "argument" or "variable" */
             mxml_node_t *type)		/* I - Type nodes */
{
  mxml_node_t	*variable,		/* New variable */
		*node,			/* Current node */
		*next;			/* Next node */
  char		buffer[16384],		/* String buffer */
		*bufptr;		/* Pointer into buffer */


 /*
  * Range check input...
  */

  if (!type || !type->child)
    return (NULL);

 /*
  * Create the variable/argument node...
  */

  variable = mxmlNewElement(parent, name);

 /*
  * Check for a default value...
  */

  for (node = type->child; node; node = node->next)
    if (!strcmp(node->value.text.string, "="))
      break;

  if (node)
  {
   /*
    * Default value found, copy it and add as a "default" attribute...
    */

    for (bufptr = buffer; node; bufptr += strlen(bufptr))
    {
      if (node->value.text.whitespace && bufptr > buffer)
	*bufptr++ = ' ';

      strcpy(bufptr, node->value.text.string);

      next = node->next;
      mxmlDelete(node);
      node = next;
    }

    mxmlElementSetAttr(variable, "default", buffer);
  }

 /*
  * Extract the argument/variable name...
  */

  if (type->last_child->value.text.string[0] == ')')
  {
   /*
    * Handle "type (*name)(args)"...
    */

    for (node = type->child; node; node = node->next)
      if (node->value.text.string[0] == '(')
	break;

    for (bufptr = buffer; node; bufptr += strlen(bufptr))
    {
      if (node->value.text.whitespace && bufptr > buffer)
	*bufptr++ = ' ';

      strcpy(bufptr, node->value.text.string);

      next = node->next;
      mxmlDelete(node);
      node = next;
    }
  }
  else
  {
   /*
    * Handle "type name"...
    */

    strcpy(buffer, type->last_child->value.text.string);
    mxmlDelete(type->last_child);
  }

 /*
  * Set the name...
  */

  mxmlElementSetAttr(variable, "name", buffer);

 /*
  * Add the remaining type information to the variable node...
  */

  mxmlAdd(variable, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, type);

 /*
  * Add new new variable node...
  */

  return (variable);
}


/*
 * 'safe_strcpy()' - Copy a string allowing for overlapping strings.
 */

static void
safe_strcpy(char       *dst,		/* I - Destination string */
            const char *src)		/* I - Source string */
{
  while (*src)
    *dst++ = *src++;

  *dst = '\0';
}


/*
 * 'scan_file()' - Scan a source file.
 */

static int				/* O - 0 on success, -1 on error */
scan_file(const char  *filename,	/* I - Filename */
          FILE        *fp,		/* I - File to scan */
          mxml_node_t *tree)		/* I - Function tree */
{
  int		state,			/* Current parser state */
		braces,			/* Number of braces active */
		parens;			/* Number of active parenthesis */
  int		ch;			/* Current character */
  char		buffer[65536],		/* String buffer */
		*bufptr;		/* Pointer into buffer */
  const char	*scope;			/* Current variable/function scope */
  mxml_node_t	*comment,		/* <comment> node */
		*constant,		/* <constant> node */
		*enumeration,		/* <enumeration> node */
		*function,		/* <function> node */
		*fstructclass,		/* function struct/class node */
		*structclass,		/* <struct> or <class> node */
		*typedefnode,		/* <typedef> node */
		*variable,		/* <variable> or <argument> node */
		*returnvalue,		/* <returnvalue> node */
		*type,			/* <type> node */
		*description,		/* <description> node */
		*node,			/* Current node */
		*next;			/* Next node */
#if DEBUG > 1
  mxml_node_t	*temp;			/* Temporary node */
  int		oldstate,		/* Previous state */
		oldch;			/* Old character */
  static const char *states[] =		/* State strings */
		{
		  "STATE_NONE",
		  "STATE_PREPROCESSOR",
		  "STATE_C_COMMENT",
		  "STATE_CXX_COMMENT",
		  "STATE_STRING",
		  "STATE_CHARACTER",
		  "STATE_IDENTIFIER"
		};
#endif /* DEBUG > 1 */


#ifdef DEBUG
  fprintf(stderr, "scan_file(filename=\"%s\", fp=%p, tree=%p)\n", filename,
          fp, tree);
#endif // DEBUG

 /*
  * Initialize the finite state machine...
  */

  state        = STATE_NONE;
  braces       = 0;
  parens       = 0;
  bufptr       = buffer;

  comment      = mxmlNewElement(MXML_NO_PARENT, "temp");
  constant     = NULL;
  enumeration  = NULL;
  function     = NULL;
  variable     = NULL;
  returnvalue  = NULL;
  type         = NULL;
  description  = NULL;
  typedefnode  = NULL;
  structclass  = NULL;
  fstructclass = NULL;

  if (!strcmp(tree->value.element.name, "class"))
    scope = "private";
  else
    scope = NULL;

 /*
  * Read until end-of-file...
  */

  while ((ch = getc(fp)) != EOF)
  {
#if DEBUG > 1
    oldstate = state;
    oldch    = ch;
#endif /* DEBUG > 1 */

    switch (state)
    {
      case STATE_NONE :			/* No state - whitespace, etc. */
          switch (ch)
	  {
	    case '/' :			/* Possible C/C++ comment */
	        ch     = getc(fp);
		bufptr = buffer;

		if (ch == '*')
		  state = STATE_C_COMMENT;
		else if (ch == '/')
		  state = STATE_CXX_COMMENT;
		else
		{
		  ungetc(ch, fp);

		  if (type)
		  {
#ifdef DEBUG
                    fputs("Identifier: <<<< / >>>\n", stderr);
#endif /* DEBUG */
                    ch = type->last_child->value.text.string[0];
		    mxmlNewText(type, isalnum(ch) || ch == '_', "/");
		  }
		}
		break;

	    case '#' :			/* Preprocessor */
#ifdef DEBUG
	        fputs("    #preprocessor...\n", stderr);
#endif /* DEBUG */
	        state = STATE_PREPROCESSOR;
		break;

            case '\'' :			/* Character constant */
	        state = STATE_CHARACTER;
		bufptr = buffer;
		*bufptr++ = ch;
		break;

            case '\"' :			/* String constant */
	        state = STATE_STRING;
		bufptr = buffer;
		*bufptr++ = ch;
		break;

            case '{' :
#ifdef DEBUG
	        fprintf(stderr, "    open brace, function=%p, type=%p...\n",
		        function, type);
                if (type)
                  fprintf(stderr, "    type->child=\"%s\"...\n",
		          type->child->value.text.string);
#endif /* DEBUG */

	        if (function)
		{
		  if (fstructclass)
		  {
		    sort_node(fstructclass, function);
		    fstructclass = NULL;
		  }
		  else
		    sort_node(tree, function);

		  function = NULL;
		}
		else if (type && type->child &&
		         ((!strcmp(type->child->value.text.string, "typedef") &&
			   type->child->next &&
			   (!strcmp(type->child->next->value.text.string, "struct") ||
			    !strcmp(type->child->next->value.text.string, "union") ||
			    !strcmp(type->child->next->value.text.string, "class"))) ||
			  !strcmp(type->child->value.text.string, "union") ||
			  !strcmp(type->child->value.text.string, "struct") ||
			  !strcmp(type->child->value.text.string, "class")))
		{
		 /*
		  * Start of a class or structure...
		  */

		  if (!strcmp(type->child->value.text.string, "typedef"))
		  {
#ifdef DEBUG
                    fputs("    starting typedef...\n", stderr);
#endif /* DEBUG */

		    typedefnode = mxmlNewElement(MXML_NO_PARENT, "typedef");
		    mxmlDelete(type->child);
		  }
		  else
		    typedefnode = NULL;
	
		  structclass = mxmlNewElement(MXML_NO_PARENT,
		                               type->child->value.text.string);

#ifdef DEBUG
                  fprintf(stderr, "%c%s: <<<< %s >>>\n",
		          toupper(type->child->value.text.string[0]),
			  type->child->value.text.string + 1,
			  type->child->next ?
			      type->child->next->value.text.string : "(noname)");

                  fputs("    type =", stderr);
                  for (node = type->child; node; node = node->next)
		    fprintf(stderr, " \"%s\"", node->value.text.string);
		  putc('\n', stderr);

                  fprintf(stderr, "    scope = %s\n", scope ? scope : "(null)");
#endif /* DEBUG */

                  if (comment->last_child &&
		      strstr(comment->last_child->value.text.string, "@private"))
		  {
		    mxmlDelete(type);
		    type = NULL;

                    if (typedefnode)
		    {
		      mxmlDelete(typedefnode);
		      typedefnode = NULL;
		    }

		    mxmlDelete(structclass);
		    structclass = NULL;

	            braces ++;
		    function = NULL;
		    variable = NULL;
		    break;
		  }

                  if (type->child->next)
		  {
		    mxmlElementSetAttr(structclass, "name",
		                       type->child->next->value.text.string);
		    sort_node(tree, structclass);
		  }

                  if (typedefnode && type->child)
		    type->child->value.text.whitespace = 0;
                  else if (structclass && type->child &&
		           type->child->next && type->child->next->next)
		  {
		    for (bufptr = buffer, node = type->child->next->next;
		         node;
			 bufptr += strlen(bufptr))
		    {
		      if (node->value.text.whitespace && bufptr > buffer)
			*bufptr++ = ' ';

		      strcpy(bufptr, node->value.text.string);

		      next = node->next;
		      mxmlDelete(node);
		      node = next;
		    }

		    mxmlElementSetAttr(structclass, "parent", buffer);

		    mxmlDelete(type);
		    type = NULL;
		  }
		  else
		  {
		    mxmlDelete(type);
		    type = NULL;
		  }

		  if (typedefnode && comment->last_child)
		  {
		   /*
		    * Copy comment for typedef as well as class/struct/union...
		    */

		    mxmlNewText(comment, 0,
		                comment->last_child->value.text.string);
		    description = mxmlNewElement(typedefnode, "description");
#ifdef DEBUG
		    fputs("    duplicating comment for typedef...\n", stderr);
#endif /* DEBUG */
		    update_comment(typedefnode, comment->last_child);
		    mxmlAdd(description, MXML_ADD_AFTER, MXML_ADD_TO_PARENT,
		            comment->last_child);
		  }

		  description = mxmlNewElement(structclass, "description");
#ifdef DEBUG
		  fprintf(stderr, "    adding comment to %s...\n",
		          structclass->value.element.name);
#endif /* DEBUG */
		  update_comment(structclass, comment->last_child);
		  mxmlAdd(description, MXML_ADD_AFTER, MXML_ADD_TO_PARENT,
		          comment->last_child);

                  if (scan_file(filename, fp, structclass))
		  {
		    mxmlDelete(comment);
		    return (-1);
		  }

#ifdef DEBUG
                  fputs("    ended typedef...\n", stderr);
#endif /* DEBUG */
                  structclass = NULL;
                  break;
                }
		else if (type && type->child && type->child->next &&
		         (!strcmp(type->child->value.text.string, "enum") ||
			  (!strcmp(type->child->value.text.string, "typedef") &&
			   !strcmp(type->child->next->value.text.string, "enum"))))
                {
		 /*
		  * Enumeration type...
		  */

		  if (!strcmp(type->child->value.text.string, "typedef"))
		  {
#ifdef DEBUG
                    fputs("    starting typedef...\n", stderr);
#endif /* DEBUG */

		    typedefnode = mxmlNewElement(MXML_NO_PARENT, "typedef");
		    mxmlDelete(type->child);
		  }
		  else
		    typedefnode = NULL;
	
		  enumeration = mxmlNewElement(MXML_NO_PARENT, "enumeration");

#ifdef DEBUG
                  fprintf(stderr, "Enumeration: <<<< %s >>>\n",
			  type->child->next ?
			      type->child->next->value.text.string : "(noname)");
#endif /* DEBUG */

                  if (type->child->next)
		  {
		    mxmlElementSetAttr(enumeration, "name",
		                       type->child->next->value.text.string);
		    sort_node(tree, enumeration);
		  }

                  if (typedefnode && type->child)
		    type->child->value.text.whitespace = 0;
                  else
		  {
		    mxmlDelete(type);
		    type = NULL;
		  }

		  if (typedefnode && comment->last_child)
		  {
		   /*
		    * Copy comment for typedef as well as class/struct/union...
		    */

		    mxmlNewText(comment, 0,
		                comment->last_child->value.text.string);
		    description = mxmlNewElement(typedefnode, "description");
#ifdef DEBUG
		    fputs("    duplicating comment for typedef...\n", stderr);
#endif /* DEBUG */
		    update_comment(typedefnode, comment->last_child);
		    mxmlAdd(description, MXML_ADD_AFTER, MXML_ADD_TO_PARENT,
		            comment->last_child);
		  }

		  description = mxmlNewElement(enumeration, "description");
#ifdef DEBUG
		  fputs("    adding comment to enumeration...\n", stderr);
#endif /* DEBUG */
		  update_comment(enumeration, comment->last_child);
		  mxmlAdd(description, MXML_ADD_AFTER, MXML_ADD_TO_PARENT,
		          comment->last_child);
		}
		else if (type && type->child &&
		         !strcmp(type->child->value.text.string, "extern"))
                {
                  if (scan_file(filename, fp, tree))
		  {
		    mxmlDelete(comment);
		    return (-1);
		  }
                }
		else if (type)
		{
		  mxmlDelete(type);
		  type = NULL;
		}

	        braces ++;
		function = NULL;
		variable = NULL;
		break;

            case '}' :
#ifdef DEBUG
	        fputs("    close brace...\n", stderr);
#endif /* DEBUG */

                if (structclass)
		  scope = NULL;

                enumeration = NULL;
		constant    = NULL;
		structclass = NULL;

	        if (braces > 0)
		  braces --;
		else
		{
		  mxmlDelete(comment);
		  return (0);
		}
		break;

            case '(' :
		if (type)
		{
#ifdef DEBUG
                  fputs("Identifier: <<<< ( >>>\n", stderr);
#endif /* DEBUG */
		  mxmlNewText(type, 0, "(");
		}

	        parens ++;
		break;

            case ')' :
	        if (parens > 0)
		  parens --;

		if (type && parens)
		{
#ifdef DEBUG
                  fputs("Identifier: <<<< ) >>>\n", stderr);
#endif /* DEBUG */
		  mxmlNewText(type, 0, ")");
		}

                if (function && type && !parens)
		{
		  variable = add_variable(function, "argument", type);
		  type     = NULL;
		}
		break;

	    case ';' :
#ifdef DEBUG
                fputs("Identifier: <<<< ; >>>\n", stderr);
		fprintf(stderr, "    function=%p, type=%p\n", function, type);
#endif /* DEBUG */

	        if (function)
		{
		  if (!strcmp(tree->value.element.name, "class"))
		  {
#ifdef DEBUG
		    fputs("    ADDING FUNCTION TO CLASS\n", stderr);
#endif /* DEBUG */
		    sort_node(tree, function);
		  }
		  else
		    mxmlDelete(function);

		  function = NULL;
		  variable = NULL;
		}

		if (type)
		{
		  mxmlDelete(type);
		  type = NULL;
		}
		break;

	    case ':' :
		if (type)
		{
#ifdef DEBUG
                  fputs("Identifier: <<<< : >>>\n", stderr);
#endif /* DEBUG */
		  mxmlNewText(type, 1, ":");
		}
		break;

	    case '*' :
		if (type)
		{
#ifdef DEBUG
                  fputs("Identifier: <<<< * >>>\n", stderr);
#endif /* DEBUG */
                  ch = type->last_child->value.text.string[0];
		  mxmlNewText(type, isalnum(ch) || ch == '_', "*");
		}
		break;

	    case '&' :
		if (type)
		{
#ifdef DEBUG
                  fputs("Identifier: <<<< & >>>\n", stderr);
#endif /* DEBUG */
		  mxmlNewText(type, 1, "&");
		}
		break;

	    case '+' :
		if (type)
		{
#ifdef DEBUG
                  fputs("Identifier: <<<< + >>>\n", stderr);
#endif /* DEBUG */
                  ch = type->last_child->value.text.string[0];
		  mxmlNewText(type, isalnum(ch) || ch == '_', "+");
		}
		break;

	    case '-' :
		if (type)
		{
#ifdef DEBUG
                  fputs("Identifier: <<<< - >>>\n", stderr);
#endif /* DEBUG */
                  ch = type->last_child->value.text.string[0];
		  mxmlNewText(type, isalnum(ch) || ch == '_', "-");
		}
		break;

	    case '=' :
		if (type)
		{
#ifdef DEBUG
                  fputs("Identifier: <<<< = >>>\n", stderr);
#endif /* DEBUG */
                  ch = type->last_child->value.text.string[0];
		  mxmlNewText(type, isalnum(ch) || ch == '_', "=");
		}
		break;

            default :			/* Other */
	        if (isalnum(ch) || ch == '_' || ch == '.' || ch == ':' || ch == '~')
		{
		  state     = STATE_IDENTIFIER;
		  bufptr    = buffer;
		  *bufptr++ = ch;
		}
		break;
          }
          break;

      case STATE_PREPROCESSOR :		/* Preprocessor directive */
          if (ch == '\n')
	    state = STATE_NONE;
	  else if (ch == '\\')
	    getc(fp);
          break;

      case STATE_C_COMMENT :		/* Inside a C comment */
          switch (ch)
	  {
	    case '\n' :
	        while ((ch = getc(fp)) != EOF)
		  if (ch == '*')
		  {
		    ch = getc(fp);

		    if (ch == '/')
		    {
		      *bufptr = '\0';

        	      if (comment->child != comment->last_child)
		      {
#ifdef DEBUG
			fprintf(stderr, "    removing comment %p, last comment %p...\n",
				comment->child, comment->last_child);
#endif /* DEBUG */
			mxmlDelete(comment->child);
#ifdef DEBUG
			fprintf(stderr, "    new comment %p, last comment %p...\n",
				comment->child, comment->last_child);
#endif /* DEBUG */
		      }

#ifdef DEBUG
                      fprintf(stderr, "    processing comment, variable=%p, constant=%p, tree=\"%s\"\n",
		              variable, constant, tree->value.element.name);
#endif /* DEBUG */

		      if (variable)
		      {
        		description = mxmlNewElement(variable, "description");
#ifdef DEBUG
			fputs("    adding comment to variable...\n", stderr);
#endif /* DEBUG */
			update_comment(variable,
			               mxmlNewText(description, 0, buffer));
                        variable = NULL;
		      }
		      else if (constant)
		      {
        		description = mxmlNewElement(constant, "description");
#ifdef DEBUG
		        fputs("    adding comment to constant...\n", stderr);
#endif /* DEBUG */
			update_comment(constant,
			               mxmlNewText(description, 0, buffer));
                        constant = NULL;
		      }
		      else if (typedefnode)
		      {
        		description = mxmlNewElement(typedefnode, "description");
#ifdef DEBUG
			fprintf(stderr, "    adding comment to typedef %s...\n",
			        mxmlElementGetAttr(typedefnode, "name"));
#endif /* DEBUG */
			update_comment(typedefnode,
			               mxmlNewText(description, 0, buffer));
		      }
		      else if (strcmp(tree->value.element.name, "mxmldoc") &&
		               !mxmlFindElement(tree, tree, "description",
			                        NULL, NULL, MXML_DESCEND_FIRST))
                      {
        		description = mxmlNewElement(tree, "description");
#ifdef DEBUG
			fputs("    adding comment to parent...\n", stderr);
#endif /* DEBUG */
			update_comment(tree,
			               mxmlNewText(description, 0, buffer));
		      }
		      else
		      {
#ifdef DEBUG
		        fprintf(stderr, "    before adding comment, child=%p, last_child=%p\n",
			        comment->child, comment->last_child);
#endif /* DEBUG */
        		mxmlNewText(comment, 0, buffer);
#ifdef DEBUG
		        fprintf(stderr, "    after adding comment, child=%p, last_child=%p\n",
			        comment->child, comment->last_child);
#endif /* DEBUG */
                      }
#ifdef DEBUG
		      fprintf(stderr, "C comment: <<<< %s >>>\n", buffer);
#endif /* DEBUG */

		      state = STATE_NONE;
		      break;
		    }
		    else
		      ungetc(ch, fp);
		  }
		  else if (ch == '\n' && bufptr > buffer &&
		           bufptr < (buffer + sizeof(buffer) - 1))
		    *bufptr++ = ch;
		  else if (!isspace(ch))
		    break;

		if (ch != EOF)
		  ungetc(ch, fp);

                if (bufptr > buffer && bufptr < (buffer + sizeof(buffer) - 1))
		  *bufptr++ = '\n';
		break;

	    case '/' :
	        if (ch == '/' && bufptr > buffer && bufptr[-1] == '*')
		{
		  while (bufptr > buffer &&
		         (bufptr[-1] == '*' || isspace(bufptr[-1] & 255)))
		    bufptr --;
		  *bufptr = '\0';

        	  if (comment->child != comment->last_child)
		  {
#ifdef DEBUG
		    fprintf(stderr, "    removing comment %p, last comment %p...\n",
			    comment->child, comment->last_child);
#endif /* DEBUG */
		    mxmlDelete(comment->child);
#ifdef DEBUG
		    fprintf(stderr, "    new comment %p, last comment %p...\n",
			    comment->child, comment->last_child);
#endif /* DEBUG */
		  }

		  if (variable)
		  {
        	    description = mxmlNewElement(variable, "description");
#ifdef DEBUG
		    fputs("    adding comment to variable...\n", stderr);
#endif /* DEBUG */
		    update_comment(variable,
			           mxmlNewText(description, 0, buffer));
                    variable = NULL;
		  }
		  else if (constant)
		  {
        	    description = mxmlNewElement(constant, "description");
#ifdef DEBUG
		    fputs("    adding comment to constant...\n", stderr);
#endif /* DEBUG */
		    update_comment(constant,
			           mxmlNewText(description, 0, buffer));
		    constant = NULL;
		  }
		  else if (typedefnode)
		  {
        	    description = mxmlNewElement(typedefnode, "description");
#ifdef DEBUG
		    fprintf(stderr, "    adding comment to typedef %s...\n",
			    mxmlElementGetAttr(typedefnode, "name"));
#endif /* DEBUG */
		    update_comment(typedefnode,
			           mxmlNewText(description, 0, buffer));
		  }
		  else if (strcmp(tree->value.element.name, "mxmldoc") &&
		           !mxmlFindElement(tree, tree, "description",
			                    NULL, NULL, MXML_DESCEND_FIRST))
                  {
        	    description = mxmlNewElement(tree, "description");
#ifdef DEBUG
		    fputs("    adding comment to parent...\n", stderr);
#endif /* DEBUG */
		    update_comment(tree,
			           mxmlNewText(description, 0, buffer));
		  }
		  else
        	    mxmlNewText(comment, 0, buffer);

#ifdef DEBUG
		  fprintf(stderr, "C comment: <<<< %s >>>\n", buffer);
#endif /* DEBUG */

		  state = STATE_NONE;
		  break;
		}

	    default :
	        if (ch == ' ' && bufptr == buffer)
		  break;

	        if (bufptr < (buffer + sizeof(buffer) - 1))
		  *bufptr++ = ch;
		break;
          }
          break;

      case STATE_CXX_COMMENT :		/* Inside a C++ comment */
          if (ch == '\n')
	  {
	    state = STATE_NONE;
	    *bufptr = '\0';

            if (comment->child != comment->last_child)
	    {
#ifdef DEBUG
	      fprintf(stderr, "    removing comment %p, last comment %p...\n",
		      comment->child, comment->last_child);
#endif /* DEBUG */
	      mxmlDelete(comment->child);
#ifdef DEBUG
	      fprintf(stderr, "    new comment %p, last comment %p...\n",
		      comment->child, comment->last_child);
#endif /* DEBUG */
	    }

	    if (variable)
	    {
              description = mxmlNewElement(variable, "description");
#ifdef DEBUG
	      fputs("    adding comment to variable...\n", stderr);
#endif /* DEBUG */
	      update_comment(variable,
			     mxmlNewText(description, 0, buffer));
              variable = NULL;
	    }
	    else if (constant)
	    {
              description = mxmlNewElement(constant, "description");
#ifdef DEBUG
	      fputs("    adding comment to constant...\n", stderr);
#endif /* DEBUG */
	      update_comment(constant,
			     mxmlNewText(description, 0, buffer));
              constant = NULL;
	    }
	    else if (typedefnode)
	    {
              description = mxmlNewElement(typedefnode, "description");
#ifdef DEBUG
	      fprintf(stderr, "    adding comment to typedef %s...\n",
		      mxmlElementGetAttr(typedefnode, "name"));
#endif /* DEBUG */
	      update_comment(typedefnode,
			     mxmlNewText(description, 0, buffer));
	    }
	    else if (strcmp(tree->value.element.name, "mxmldoc") &&
		     !mxmlFindElement(tree, tree, "description",
			              NULL, NULL, MXML_DESCEND_FIRST))
            {
              description = mxmlNewElement(tree, "description");
#ifdef DEBUG
	      fputs("    adding comment to parent...\n", stderr);
#endif /* DEBUG */
	      update_comment(tree,
			     mxmlNewText(description, 0, buffer));
	    }
	    else
              mxmlNewText(comment, 0, buffer);

#ifdef DEBUG
	    fprintf(stderr, "C++ comment: <<<< %s >>>\n", buffer);
#endif /* DEBUG */
	  }
	  else if (ch == ' ' && bufptr == buffer)
	    break;
	  else if (bufptr < (buffer + sizeof(buffer) - 1))
	    *bufptr++ = ch;
          break;

      case STATE_STRING :		/* Inside a string constant */
	  *bufptr++ = ch;

          if (ch == '\\')
	    *bufptr++ = getc(fp);
	  else if (ch == '\"')
	  {
	    *bufptr = '\0';

	    if (type)
	      mxmlNewText(type, type->child != NULL, buffer);

	    state = STATE_NONE;
	  }
          break;

      case STATE_CHARACTER :		/* Inside a character constant */
	  *bufptr++ = ch;

          if (ch == '\\')
	    *bufptr++ = getc(fp);
	  else if (ch == '\'')
	  {
	    *bufptr = '\0';

	    if (type)
	      mxmlNewText(type, type->child != NULL, buffer);

	    state = STATE_NONE;
	  }
          break;

      case STATE_IDENTIFIER :		/* Inside a keyword or identifier */
	  if (isalnum(ch) || ch == '_' || ch == '[' || ch == ']' ||
	      (ch == ',' && parens > 1) || ch == ':' || ch == '.' || ch == '~')
	  {
	    if (bufptr < (buffer + sizeof(buffer) - 1))
	      *bufptr++ = ch;
	  }
	  else
	  {
	    ungetc(ch, fp);
	    *bufptr = '\0';
	    state   = STATE_NONE;

#ifdef DEBUG
            fprintf(stderr, "    braces=%d, type=%p, type->child=%p, buffer=\"%s\"\n",
	            braces, type, type ? type->child : NULL, buffer);
#endif /* DEBUG */

            if (!braces)
	    {
	      if (!type || !type->child)
	      {
		if (!strcmp(tree->value.element.name, "class"))
		{
		  if (!strcmp(buffer, "public") ||
	              !strcmp(buffer, "public:"))
		  {
		    scope = "public";
#ifdef DEBUG
		    fputs("    scope = public\n", stderr);
#endif /* DEBUG */
		    break;
		  }
		  else if (!strcmp(buffer, "private") ||
	                   !strcmp(buffer, "private:"))
		  {
		    scope = "private";
#ifdef DEBUG
		    fputs("    scope = private\n", stderr);
#endif /* DEBUG */
		    break;
		  }
		  else if (!strcmp(buffer, "protected") ||
	                   !strcmp(buffer, "protected:"))
		  {
		    scope = "protected";
#ifdef DEBUG
		    fputs("    scope = protected\n", stderr);
#endif /* DEBUG */
		    break;
		  }
		}
	      }

	      if (!type)
                type = mxmlNewElement(MXML_NO_PARENT, "type");

#ifdef DEBUG
              fprintf(stderr, "    function=%p (%s), type->child=%p, ch='%c', parens=%d\n",
	              function,
		      function ? mxmlElementGetAttr(function, "name") : "null",
	              type->child, ch, parens);
#endif /* DEBUG */

              if (!function && ch == '(')
	      {
	        if (type->child &&
		    !strcmp(type->child->value.text.string, "extern"))
		{
		 /*
		  * Remove external declarations...
		  */

		  mxmlDelete(type);
		  type = NULL;
		  break;
		}

	        if (type->child &&
		    !strcmp(type->child->value.text.string, "static") &&
		    !strcmp(tree->value.element.name, "mxmldoc"))
		{
		 /*
		  * Remove static functions...
		  */

		  mxmlDelete(type);
		  type = NULL;
		  break;
		}

	        function = mxmlNewElement(MXML_NO_PARENT, "function");
		if ((bufptr = strchr(buffer, ':')) != NULL && bufptr[1] == ':')
		{
		  *bufptr = '\0';
		  bufptr += 2;

		  if ((fstructclass =
		           mxmlFindElement(tree, tree, "class", "name", buffer,
		                           MXML_DESCEND_FIRST)) == NULL)
		    fstructclass =
		        mxmlFindElement(tree, tree, "struct", "name", buffer,
		                        MXML_DESCEND_FIRST);
		}
		else
		  bufptr = buffer;

		mxmlElementSetAttr(function, "name", bufptr);

		if (scope)
		  mxmlElementSetAttr(function, "scope", scope);

#ifdef DEBUG
                fprintf(stderr, "function: %s\n", buffer);
		fprintf(stderr, "    scope = %s\n", scope ? scope : "(null)");
		fprintf(stderr, "    comment = %p\n", comment);
		fprintf(stderr, "    child = (%p) %s\n",
		        comment->child,
			comment->child ?
			    comment->child->value.text.string : "(null)");
		fprintf(stderr, "    last_child = (%p) %s\n",
		        comment->last_child,
			comment->last_child ?
			    comment->last_child->value.text.string : "(null)");
#endif /* DEBUG */

                if (type->last_child &&
		    strcmp(type->last_child->value.text.string, "void"))
		{
                  returnvalue = mxmlNewElement(function, "returnvalue");

		  mxmlAdd(returnvalue, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, type);

		  description = mxmlNewElement(returnvalue, "description");
#ifdef DEBUG
		  fputs("    adding comment to returnvalue...\n", stderr);
#endif /* DEBUG */
		  update_comment(returnvalue, comment->last_child);
		  mxmlAdd(description, MXML_ADD_AFTER, MXML_ADD_TO_PARENT,
		          comment->last_child);
                }
		else
		  mxmlDelete(type);

		description = mxmlNewElement(function, "description");
#ifdef DEBUG
		  fputs("    adding comment to function...\n", stderr);
#endif /* DEBUG */
		update_comment(function, comment->last_child);
		mxmlAdd(description, MXML_ADD_AFTER, MXML_ADD_TO_PARENT,
		        comment->last_child);

		type = NULL;
	      }
	      else if (function && ((ch == ')' && parens == 1) || ch == ','))
	      {
	       /*
	        * Argument definition...
		*/

	        mxmlNewText(type, type->child != NULL &&
		                  type->last_child->value.text.string[0] != '(' &&
				  type->last_child->value.text.string[0] != '*',
			    buffer);

#ifdef DEBUG
                fprintf(stderr, "Argument: <<<< %s >>>\n", buffer);
#endif /* DEBUG */

	        variable = add_variable(function, "argument", type);
		type     = NULL;
	      }
              else if (type->child && !function && (ch == ';' || ch == ','))
	      {
#ifdef DEBUG
	        fprintf(stderr, "    got semicolon, typedefnode=%p, structclass=%p\n",
		        typedefnode, structclass);
#endif /* DEBUG */

	        if (typedefnode || structclass)
		{
#ifdef DEBUG
                  fprintf(stderr, "Typedef/struct/class: <<<< %s >>>>\n", buffer);
#endif /* DEBUG */

		  if (typedefnode)
		  {
		    mxmlElementSetAttr(typedefnode, "name", buffer);

                    sort_node(tree, typedefnode);
		  }

		  if (structclass && !mxmlElementGetAttr(structclass, "name"))
		  {
#ifdef DEBUG
		    fprintf(stderr, "setting struct/class name to %s!\n",
		            type->last_child->value.text.string);
#endif /* DEBUG */
		    mxmlElementSetAttr(structclass, "name", buffer);

		    sort_node(tree, structclass);
		    structclass = NULL;
		  }

		  if (typedefnode)
		    mxmlAdd(typedefnode, MXML_ADD_BEFORE, MXML_ADD_TO_PARENT,
		            type);
                  else
		    mxmlDelete(type);

		  type        = NULL;
		  typedefnode = NULL;
		}
		else if (type->child &&
		         !strcmp(type->child->value.text.string, "typedef"))
		{
		 /*
		  * Simple typedef...
		  */

#ifdef DEBUG
                  fprintf(stderr, "Typedef: <<<< %s >>>\n", buffer);
#endif /* DEBUG */

		  typedefnode = mxmlNewElement(MXML_NO_PARENT, "typedef");
		  mxmlElementSetAttr(typedefnode, "name", buffer);
		  mxmlDelete(type->child);

                  sort_node(tree, typedefnode);

                  if (type->child)
		    type->child->value.text.whitespace = 0;

		  mxmlAdd(typedefnode, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, type);
		  type = NULL;
		}
		else if (!parens)
		{
		 /*
	          * Variable definition...
		  */

	          if (type->child &&
		      !strcmp(type->child->value.text.string, "static") &&
		      !strcmp(tree->value.element.name, "mxmldoc"))
		  {
		   /*
		    * Remove static functions...
		    */

		    mxmlDelete(type);
		    type = NULL;
		    break;
		  }

	          mxmlNewText(type, type->child != NULL &&
		                    type->last_child->value.text.string[0] != '(' &&
				    type->last_child->value.text.string[0] != '*',
			      buffer);

#ifdef DEBUG
                  fprintf(stderr, "Variable: <<<< %s >>>>\n", buffer);
                  fprintf(stderr, "    scope = %s\n", scope ? scope : "(null)");
#endif /* DEBUG */

	          variable = add_variable(MXML_NO_PARENT, "variable", type);
		  type     = NULL;

		  sort_node(tree, variable);

		  if (scope)
		    mxmlElementSetAttr(variable, "scope", scope);
		}
              }
	      else
              {
#ifdef DEBUG
                fprintf(stderr, "Identifier: <<<< %s >>>>\n", buffer);
#endif /* DEBUG */

	        mxmlNewText(type, type->child != NULL &&
		                  type->last_child->value.text.string[0] != '(' &&
				  type->last_child->value.text.string[0] != '*',
			    buffer);
	      }
	    }
	    else if (enumeration && !isdigit(buffer[0] & 255))
	    {
#ifdef DEBUG
	      fprintf(stderr, "Constant: <<<< %s >>>\n", buffer);
#endif /* DEBUG */

	      constant = mxmlNewElement(MXML_NO_PARENT, "constant");
	      mxmlElementSetAttr(constant, "name", buffer);
	      sort_node(enumeration, constant);
	    }
	    else if (type)
	    {
	      mxmlDelete(type);
	      type = NULL;
	    }
	  }
          break;
    }

#if DEBUG > 1
    if (state != oldstate)
    {
      fprintf(stderr, "    changed states from %s to %s on receipt of character '%c'...\n",
              states[oldstate], states[state], oldch);
      fprintf(stderr, "    variable = %p\n", variable);
      if (type)
      {
        fputs("    type =", stderr);
        for (temp = type->child; temp; temp = temp->next)
	  fprintf(stderr, " \"%s\"", temp->value.text.string);
	fputs("\n", stderr);
      }
    }
#endif /* DEBUG > 1 */
  }

  mxmlDelete(comment);

 /*
  * All done, return with no errors...
  */

  return (0);
}


/*
 * 'sort_node()' - Insert a node sorted into a tree.
 */

static void
sort_node(mxml_node_t *tree,		/* I - Tree to sort into */
          mxml_node_t *node)		/* I - Node to add */
{
  mxml_node_t	*temp;			/* Current node */
  const char	*tempname,		/* Name of current node */
		*nodename,		/* Name of node */
		*scope;			/* Scope */


#if DEBUG > 1
  fprintf(stderr, "    sort_node(tree=%p, node=%p)\n", tree, node);
#endif /* DEBUG > 1 */

 /*
  * Range check input...
  */

  if (!tree || !node || node->parent == tree)
    return;

 /*
  * Get the node name...
  */

  if ((nodename = mxmlElementGetAttr(node, "name")) == NULL)
    return;

#if DEBUG > 1
  fprintf(stderr, "        nodename=%p (\"%s\")\n", nodename, nodename);
#endif /* DEBUG > 1 */

 /*
  * Delete any existing definition at this level, if one exists...
  */

  if ((temp = mxmlFindElement(tree, tree, node->value.element.name,
                              "name", nodename, MXML_DESCEND_FIRST)) != NULL)
  {
   /*
    * Copy the scope if needed...
    */

    if ((scope = mxmlElementGetAttr(temp, "scope")) != NULL &&
        mxmlElementGetAttr(node, "scope") == NULL)
    {
#ifdef DEBUG
      fprintf(stderr, "    copying scope %s for %s\n", scope, nodename);
#endif /* DEBUG */

      mxmlElementSetAttr(node, "scope", scope);
    }

    mxmlDelete(temp);
  }

 /*
  * Add the node into the tree at the proper place...
  */

  for (temp = tree->child; temp; temp = temp->next)
  {
#if DEBUG > 1
    fprintf(stderr, "        temp=%p\n", temp);
#endif /* DEBUG > 1 */

    if ((tempname = mxmlElementGetAttr(temp, "name")) == NULL)
      continue;

#if DEBUG > 1
    fprintf(stderr, "        tempname=%p (\"%s\")\n", tempname, tempname);
#endif /* DEBUG > 1 */

    if (strcmp(nodename, tempname) < 0)
      break;
  }

  if (temp)
    mxmlAdd(tree, MXML_ADD_BEFORE, temp, node);
  else
    mxmlAdd(tree, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
}


/*
 * 'update_comment()' - Update a comment node.
 */

static void
update_comment(mxml_node_t *parent,	/* I - Parent node */
               mxml_node_t *comment)	/* I - Comment node */
{
  char	*ptr;				/* Pointer into comment */


#ifdef DEBUG
  fprintf(stderr, "update_comment(parent=%p, comment=%p)\n",
          parent, comment);
#endif /* DEBUG */

 /*
  * Range check the input...
  */

  if (!parent || !comment)
    return;
 
 /*
  * Update the comment...
  */

  ptr = comment->value.text.string;

  if (*ptr == '\'')
  {
   /*
    * Convert "'name()' - description" to "description".
    */

    for (ptr ++; *ptr && *ptr != '\''; ptr ++);

    if (*ptr == '\'')
    {
      ptr ++;
      while (isspace(*ptr & 255))
        ptr ++;

      if (*ptr == '-')
        ptr ++;

      while (isspace(*ptr & 255))
        ptr ++;

      safe_strcpy(comment->value.text.string, ptr);
    }
  }
  else if (!strncmp(ptr, "I ", 2) || !strncmp(ptr, "O ", 2) ||
           !strncmp(ptr, "IO ", 3))
  {
   /*
    * 'Convert "I - description", "IO - description", or "O - description"
    * to description + directory attribute.
    */

    ptr = strchr(ptr, ' ');
    *ptr++ = '\0';

    if (!strcmp(parent->value.element.name, "argument"))
      mxmlElementSetAttr(parent, "direction", comment->value.text.string);

    while (isspace(*ptr & 255))
      ptr ++;

    if (*ptr == '-')
      ptr ++;

    while (isspace(*ptr & 255))
      ptr ++;

    safe_strcpy(comment->value.text.string, ptr);
  }

 /*
  * Eliminate leading and trailing *'s...
  */

  for (ptr = comment->value.text.string; *ptr == '*'; ptr ++);
  for (; isspace(*ptr & 255); ptr ++);
  if (ptr > comment->value.text.string)
    safe_strcpy(comment->value.text.string, ptr);

  for (ptr = comment->value.text.string + strlen(comment->value.text.string) - 1;
       ptr > comment->value.text.string && *ptr == '*';
       ptr --)
    *ptr = '\0';
  for (; ptr > comment->value.text.string && isspace(*ptr & 255); ptr --)
    *ptr = '\0';

#ifdef DEBUG
  fprintf(stderr, "    updated comment = %s\n", comment->value.text.string);
#endif /* DEBUG */
}


/*
 * 'write_documentation()' - Write HTML documentation.
 */

static void
write_documentation(mxml_node_t *doc)	/* I - XML documentation */
{
  int		i;			/* Looping var */
  mxml_node_t	*function,		/* Current function */
		*scut,			/* Struct/class/union/typedef */
		*arg,			/* Current argument */
		*description,		/* Description of function/var */
		*type;			/* Type for argument */
  const char	*name,			/* Name of function/type */
		*cname,			/* Class name */
		*defval,		/* Default value */
		*parent;		/* Parent class */
  int		inscope;		/* Variable/method scope */
  char		prefix;			/* Prefix character */
  static const char * const scopes[] =	/* Scope strings */
		{
		  "private",
		  "protected",
		  "public"
		};


 /*
  * Standard header...
  */

  puts("<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" "
       "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
       "<html xmlns='http://www.w3.org/1999/xhtml' xml:lang='en' lang='en'>\n"
       "<head>\n"
       "\t<title>Documentation</title>\n"
       "\t<meta name='creator' content='" MXML_VERSION "'/>\n"
       "\t<style><!--\n"
       "\th1, h2, h3, p { font-family: sans-serif; text-align: justify; }\n"
       "\ttt, pre a:link, pre a:visited, tt a:link, tt a:visited { font-weight: bold; color: #7f0000; }\n"
       "\tpre { font-weight: bold; color: #7f0000; margin-left: 2em; }\n"
       "\t--></style>\n"
       "</head>\n"
       "<body>");

 /*
  * Table of contents...
  */

  puts("<h2>Contents</h2>");
  puts("<ul>");
  if (mxmlFindElement(doc, doc, "class", NULL, NULL, MXML_DESCEND_FIRST))
    puts("\t<li><a href='#_classes'>Classes</a></li>");
  if (mxmlFindElement(doc, doc, "enumeration", NULL, NULL, MXML_DESCEND_FIRST))
    puts("\t<li><a href='#_enumerations'>Enumerations</a></li>");
  if (mxmlFindElement(doc, doc, "function", NULL, NULL, MXML_DESCEND_FIRST))
    puts("\t<li><a href='#_functions'>Functions</a></li>");
  if (mxmlFindElement(doc, doc, "struct", NULL, NULL, MXML_DESCEND_FIRST))
    puts("\t<li><a href='#_structures'>Structures</a></li>");
  if (mxmlFindElement(doc, doc, "typedef", NULL, NULL, MXML_DESCEND_FIRST))
    puts("\t<li><a href='#_types'>Types</a></li>");
  if (mxmlFindElement(doc, doc, "union", NULL, NULL, MXML_DESCEND_FIRST))
    puts("\t<li><a href='#_unions'>Unions</a></li>");
  if (mxmlFindElement(doc, doc, "variable", NULL, NULL, MXML_DESCEND_FIRST))
    puts("\t<li><a href='#_variables'>Variables</a></li>");
  puts("</ul>");

 /*
  * List of classes...
  */

  if (mxmlFindElement(doc, doc, "class", NULL, NULL, MXML_DESCEND_FIRST))
  {
    puts("<!-- NEW PAGE -->\n"
         "<h2><a name='_classes'>Classes</a></h2>\n"
         "<ul>");

    for (scut = mxmlFindElement(doc, doc, "class", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "class", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(scut, "name");
      printf("\t<li><a href='#%s'><tt>%s</tt></a></li>\n", name, name);
    }

    puts("</ul>");

    for (scut = mxmlFindElement(doc, doc, "class", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "class", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      cname = mxmlElementGetAttr(scut, "name");
      printf("<!-- NEW PAGE -->\n"
             "<h3><a name='%s'>%s</a></h3>\n"
             "<hr noshade/>\n", cname, cname);

      description = mxmlFindElement(scut, scut, "description", NULL,
                                    NULL, MXML_DESCEND_FIRST);
      if (description)
      {
        fputs("<h4>Description</h4>\n"
	      "<p>", stdout);
	write_element(NULL, description);
	puts("</p>");
      }

      printf("<h4>Definition</h4>\n"
             "<pre>\n"
             "class %s", cname);
      if ((parent = mxmlElementGetAttr(scut, "parent")) != NULL)
        printf(" %s", parent);
      puts("\n{");

      for (i = 0; i < 3; i ++)
      {
        inscope = 0;

	for (arg = mxmlFindElement(scut, scut, "variable", "scope", scopes[i],
                        	   MXML_DESCEND_FIRST);
	     arg;
	     arg = mxmlFindElement(arg, scut, "variable", "scope", scopes[i],
                        	   MXML_NO_DESCEND))
	{
          if (!inscope)
	  {
	    inscope = 1;
	    printf("  %s:\n", scopes[i]);
	  }

	  printf("    ");
	  write_element(doc, mxmlFindElement(arg, arg, "type", NULL,
                                             NULL, MXML_DESCEND_FIRST));
	  printf(" %s;\n", mxmlElementGetAttr(arg, "name"));
	}

	for (function = mxmlFindElement(scut, scut, "function", "scope", scopes[i],
                                	MXML_DESCEND_FIRST);
	     function;
	     function = mxmlFindElement(function, scut, "function", "scope", scopes[i],
                                	MXML_NO_DESCEND))
	{
          if (!inscope)
	  {
	    inscope = 1;
	    printf("  %s:\n", scopes[i]);
	  }

          name = mxmlElementGetAttr(function, "name");

          printf("    ");

	  arg = mxmlFindElement(function, function, "returnvalue", NULL,
                        	NULL, MXML_DESCEND_FIRST);

	  if (arg)
	  {
	    write_element(doc, mxmlFindElement(arg, arg, "type", NULL,
                                               NULL, MXML_DESCEND_FIRST));
	    putchar(' ');
	  }
	  else if (strcmp(cname, name) && strcmp(cname, name + 1))
	    fputs("void ", stdout);

	  printf("<a href='#%s.%s'>%s</a>", cname, name, name);

	  for (arg = mxmlFindElement(function, function, "argument", NULL, NULL,
                        	     MXML_DESCEND_FIRST), prefix = '(';
	       arg;
	       arg = mxmlFindElement(arg, function, "argument", NULL, NULL,
                        	     MXML_NO_DESCEND), prefix = ',')
	  {
	    type = mxmlFindElement(arg, arg, "type", NULL, NULL,
	                	   MXML_DESCEND_FIRST);

	    putchar(prefix);
	    if (prefix == ',')
	      putchar(' ');

	    if (type->child)
	    {
	      write_element(doc, type);
	      putchar(' ');
	    }
	    fputs(mxmlElementGetAttr(arg, "name"), stdout);
            if ((defval = mxmlElementGetAttr(arg, "default")) != NULL)
	      printf(" %s", defval);
	  }

	  if (prefix == '(')
	    puts("(void);");
	  else
	    puts(");");
	}
      }

      puts("};\n</pre>\n"
           "<h4>Members</h4>\n"
           "<p class='table'><table align='center' border='1' "
           "cellpadding='5' cellspacing='0' width='80%'>\n"
           "<thead><tr bgcolor='#cccccc'><th>Name</th><th>Description</th></tr></thead>\n"
           "<tbody>");

      for (arg = mxmlFindElement(scut, scut, "variable", NULL, NULL,
                        	 MXML_DESCEND_FIRST);
	   arg;
	   arg = mxmlFindElement(arg, scut, "variable", NULL, NULL,
                        	 MXML_NO_DESCEND))
      {
	printf("<tr><td><tt>%s</tt></td><td>", mxmlElementGetAttr(arg, "name"));

	write_element(NULL, mxmlFindElement(arg, arg, "description", NULL,
                                            NULL, MXML_DESCEND_FIRST));

	puts("</td></tr>");
      }

      for (function = mxmlFindElement(scut, scut, "function", NULL, NULL,
                                      MXML_DESCEND_FIRST);
	   function;
	   function = mxmlFindElement(function, scut, "function", NULL, NULL,
                                      MXML_NO_DESCEND))
      {
	name = mxmlElementGetAttr(function, "name");

	printf("<tr><td><tt><a name='%s.%s'>%s()</a></tt></td><td>",
	       cname, name, name);

	description = mxmlFindElement(function, function, "description", NULL,
                                      NULL, MXML_DESCEND_FIRST);
	if (description)
	  write_element(NULL, description);

	arg = mxmlFindElement(function, function, "returnvalue", NULL,
                              NULL, MXML_DESCEND_FIRST);

	if (arg)
	{
	  fputs("\n<i>Returns:</i> ", stdout);
	  write_element(NULL, mxmlFindElement(arg, arg, "description", NULL,
                                              NULL, MXML_DESCEND_FIRST));
	}

	puts("</td></tr>");
      }

      puts("</tbody></table></p>");
    }
  }

 /*
  * List of enumerations...
  */

  if (mxmlFindElement(doc, doc, "enumeration", NULL, NULL, MXML_DESCEND_FIRST))
  {
    puts("<!-- NEW PAGE -->\n"
         "<h2><a name='_enumerations'>Enumerations</a></h2>\n"
         "<ul>");

    for (scut = mxmlFindElement(doc, doc, "enumeration", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "enumeration", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(scut, "name");
      printf("\t<li><a href='#%s'><tt>%s</tt></a></li>\n", name, name);
    }

    puts("</ul>");

    for (scut = mxmlFindElement(doc, doc, "enumeration", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "enumeration", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(scut, "name");
      printf("<!-- NEW PAGE -->\n"
             "<h3><a name='%s'>%s</a></h3>\n"
             "<hr noshade/>\n", name, name);

      description = mxmlFindElement(scut, scut, "description", NULL,
                                    NULL, MXML_DESCEND_FIRST);
      if (description)
      {
        fputs("<h4>Description</h4>\n"
	      "<p>", stdout);
	write_element(NULL, description);
	puts("</p>");
      }

      puts("<h4>Values</h4>\n"
           "<p class='table'><table align='center' border='1' width='80%' "
           "cellpadding='5' cellspacing='0' width='80%'>\n"
           "<thead><tr bgcolor='#cccccc'><th>Name</th><th>Description</th></tr></thead>\n"
           "<tbody>");

      for (arg = mxmlFindElement(scut, scut, "constant", NULL, NULL,
                        	 MXML_DESCEND_FIRST);
	   arg;
	   arg = mxmlFindElement(arg, scut, "constant", NULL, NULL,
                        	 MXML_NO_DESCEND))
      {
	printf("<tr><td><tt>%s</tt></td><td>", mxmlElementGetAttr(arg, "name"));

	write_element(doc, mxmlFindElement(arg, arg, "description", NULL,
                                           NULL, MXML_DESCEND_FIRST));

	puts("</td></tr>");
      }

      puts("</tbody></table></p>");
    }
  }

 /*
  * List of functions...
  */

  if (mxmlFindElement(doc, doc, "function", NULL, NULL, MXML_DESCEND_FIRST))
  {
    puts("<!-- NEW PAGE -->\n"
         "<h2><a name='_functions'>Functions</a></h2>\n"
         "<ul>");

    for (function = mxmlFindElement(doc, doc, "function", NULL, NULL,
                                    MXML_DESCEND_FIRST);
	 function;
	 function = mxmlFindElement(function, doc, "function", NULL, NULL,
                                    MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(function, "name");
      printf("\t<li><a href='#%s'><tt>%s()</tt></a></li>\n", name, name);
    }

    puts("</ul>");

    for (function = mxmlFindElement(doc, doc, "function", NULL, NULL,
                                    MXML_DESCEND_FIRST);
	 function;
	 function = mxmlFindElement(function, doc, "function", NULL, NULL,
                                    MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(function, "name");
      printf("<!-- NEW PAGE -->\n"
             "<h3><a name='%s'>%s()</a></h3>\n"
             "<hr noshade/>\n", name, name);

      description = mxmlFindElement(function, function, "description", NULL,
                                    NULL, MXML_DESCEND_FIRST);
      if (description)
      {
        fputs("<h4>Description</h4>\n"
	      "<p>", stdout);
	write_element(NULL, description);
	puts("</p>");
      }

      puts("<h4>Syntax</h4>\n"
           "<pre>");

      arg = mxmlFindElement(function, function, "returnvalue", NULL,
                            NULL, MXML_DESCEND_FIRST);

      if (arg)
	write_element(doc, mxmlFindElement(arg, arg, "type", NULL,
                                           NULL, MXML_DESCEND_FIRST));
      else
	fputs("void", stdout);

      printf("\n%s", name);
      for (arg = mxmlFindElement(function, function, "argument", NULL, NULL,
                        	 MXML_DESCEND_FIRST), prefix = '(';
	   arg;
	   arg = mxmlFindElement(arg, function, "argument", NULL, NULL,
                        	 MXML_NO_DESCEND), prefix = ',')
      {
        type = mxmlFindElement(arg, arg, "type", NULL, NULL,
	                       MXML_DESCEND_FIRST);

	printf("%c\n    ", prefix);
	if (type->child)
	{
	  write_element(doc, type);
	  putchar(' ');
	}
	fputs(mxmlElementGetAttr(arg, "name"), stdout);
        if ((defval = mxmlElementGetAttr(arg, "default")) != NULL)
	  printf(" %s", defval);
      }

      if (prefix == '(')
	puts("(void);\n</pre>");
      else
	puts(");\n</pre>");

      puts("<h4>Arguments</h4>");

      if (prefix == '(')
	puts("<p>None.</p>");
      else
      {
	puts("<p class='table'><table align='center' border='1' width='80%' "
             "cellpadding='5' cellspacing='0' width='80%'>\n"
	     "<thead><tr bgcolor='#cccccc'><th>Name</th><th>Description</th></tr></thead>\n"
	     "<tbody>");

	for (arg = mxmlFindElement(function, function, "argument", NULL, NULL,
                        	   MXML_DESCEND_FIRST);
	     arg;
	     arg = mxmlFindElement(arg, function, "argument", NULL, NULL,
                        	   MXML_NO_DESCEND))
	{
	  printf("<tr><td><tt>%s</tt></td><td>", mxmlElementGetAttr(arg, "name"));

	  write_element(NULL, mxmlFindElement(arg, arg, "description", NULL,
                               		      NULL, MXML_DESCEND_FIRST));

          puts("</td></tr>");
	}

	puts("</tbody></table></p>");
      }

      puts("<h4>Returns</h4>");

      arg = mxmlFindElement(function, function, "returnvalue", NULL,
                            NULL, MXML_DESCEND_FIRST);

      if (!arg)
	puts("<p>Nothing.</p>");
      else
      {
	fputs("<p>", stdout);
	write_element(NULL, mxmlFindElement(arg, arg, "description", NULL,
                                            NULL, MXML_DESCEND_FIRST));
	puts("</p>");
      }
    }
  }

 /*
  * List of structures...
  */

  if (mxmlFindElement(doc, doc, "struct", NULL, NULL, MXML_DESCEND_FIRST))
  {
    puts("<!-- NEW PAGE -->\n"
         "<h2><a name='_structures'>Structures</a></h2>\n"
         "<ul>");

    for (scut = mxmlFindElement(doc, doc, "struct", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "struct", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(scut, "name");
      printf("\t<li><a href='#%s'><tt>%s</tt></a></li>\n", name, name);
    }

    puts("</ul>");

    for (scut = mxmlFindElement(doc, doc, "struct", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "struct", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      cname = mxmlElementGetAttr(scut, "name");
      printf("<!-- NEW PAGE -->\n"
             "<h3><a name='%s'>%s</a></h3>\n"
	     "<hr noshade/>\n", cname, cname);

      description = mxmlFindElement(scut, scut, "description", NULL,
                                    NULL, MXML_DESCEND_FIRST);
      if (description)
      {
        fputs("<h4>Description</h4>\n"
	      "<p>", stdout);
	write_element(NULL, description);
	puts("</p>");
      }

      printf("<h4>Definition</h4>\n"
             "<pre>\n"
	     "struct %s\n{\n", cname);
      for (arg = mxmlFindElement(scut, scut, "variable", NULL, NULL,
                        	 MXML_DESCEND_FIRST);
	   arg;
	   arg = mxmlFindElement(arg, scut, "variable", NULL, NULL,
                        	 MXML_NO_DESCEND))
      {
	printf("  ");
	write_element(doc, mxmlFindElement(arg, arg, "type", NULL,
                                           NULL, MXML_DESCEND_FIRST));
	printf(" %s;\n", mxmlElementGetAttr(arg, "name"));
      }

      for (function = mxmlFindElement(scut, scut, "function", NULL, NULL,
                                      MXML_DESCEND_FIRST);
	   function;
	   function = mxmlFindElement(function, scut, "function", NULL, NULL,
                                      MXML_NO_DESCEND))
      {
        name = mxmlElementGetAttr(function, "name");

        printf("  ");

	arg = mxmlFindElement(function, function, "returnvalue", NULL,
                              NULL, MXML_DESCEND_FIRST);

	if (arg)
	{
	  write_element(doc, mxmlFindElement(arg, arg, "type", NULL,
                                             NULL, MXML_DESCEND_FIRST));
	  putchar(' ');
	}
	else if (strcmp(cname, name) && strcmp(cname, name + 1))
	  fputs("void ", stdout);

	printf("<a href='#%s.%s'>%s</a>", cname, name, name);

	for (arg = mxmlFindElement(function, function, "argument", NULL, NULL,
                        	   MXML_DESCEND_FIRST), prefix = '(';
	     arg;
	     arg = mxmlFindElement(arg, function, "argument", NULL, NULL,
                        	   MXML_NO_DESCEND), prefix = ',')
	{
	  type = mxmlFindElement(arg, arg, "type", NULL, NULL,
	                	 MXML_DESCEND_FIRST);

	  putchar(prefix);
	  if (prefix == ',')
	    putchar(' ');

	  if (type->child)
	  {
	    write_element(doc, type);
	    putchar(' ');
	  }
	  fputs(mxmlElementGetAttr(arg, "name"), stdout);
          if ((defval = mxmlElementGetAttr(arg, "default")) != NULL)
	    printf(" %s", defval);
	}

	if (prefix == '(')
	  puts("(void);");
	else
	  puts(");");
      }

      puts("};\n</pre>\n"
           "<h4>Members</h4>\n"
           "<p class='table'><table align='center' border='1' width='80%' "
           "cellpadding='5' cellspacing='0' width='80%'>\n"
           "<thead><tr bgcolor='#cccccc'><th>Name</th><th>Description</th></tr></thead>\n"
           "<tbody>");

      for (arg = mxmlFindElement(scut, scut, "variable", NULL, NULL,
                        	 MXML_DESCEND_FIRST);
	   arg;
	   arg = mxmlFindElement(arg, scut, "variable", NULL, NULL,
                        	 MXML_NO_DESCEND))
      {
	printf("<tr><td><tt>%s</tt></td><td>", mxmlElementGetAttr(arg, "name"));

	write_element(NULL, mxmlFindElement(arg, arg, "description", NULL,
                                            NULL, MXML_DESCEND_FIRST));

	puts("</td></tr>");
      }

      for (function = mxmlFindElement(scut, scut, "function", NULL, NULL,
                                      MXML_DESCEND_FIRST);
	   function;
	   function = mxmlFindElement(function, scut, "function", NULL, NULL,
                                      MXML_NO_DESCEND))
      {
	name = mxmlElementGetAttr(function, "name");

	printf("<tr><td><tt><a name='%s.%s'>%s()</a></tt></td><td>",
	       cname, name, name);

	description = mxmlFindElement(function, function, "description", NULL,
                                      NULL, MXML_DESCEND_FIRST);
	if (description)
	  write_element(NULL, description);

	arg = mxmlFindElement(function, function, "returnvalue", NULL,
                              NULL, MXML_DESCEND_FIRST);

	if (arg)
	{
	  fputs("\n<i>Returns:</i> ", stdout);
	  write_element(NULL, mxmlFindElement(arg, arg, "description", NULL,
                                              NULL, MXML_DESCEND_FIRST));
	}

	puts("</td></tr>");
      }

      puts("</tbody></table></p>");
    }
  }

 /*
  * List of types...
  */

  if (mxmlFindElement(doc, doc, "typedef", NULL, NULL, MXML_DESCEND_FIRST))
  {
    puts("<!-- NEW PAGE -->\n"
         "<h2><a name='_types'>Types</a></h2>\n"
         "<ul>");

    for (scut = mxmlFindElement(doc, doc, "typedef", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "typedef", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(scut, "name");
      printf("\t<li><a href='#%s'><tt>%s</tt></a></li>\n", name, name);
    }

    puts("</ul>");

    for (scut = mxmlFindElement(doc, doc, "typedef", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "typedef", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(scut, "name");
      printf("<!-- NEW PAGE -->\n"
             "<h3><a name='%s'>%s</a></h3>\n"
	     "<hr noshade/>\n", name, name);

      description = mxmlFindElement(scut, scut, "description", NULL,
                                    NULL, MXML_DESCEND_FIRST);
      if (description)
      {
        fputs("<h4>Description</h4>\n"
	      "<p>", stdout);
	write_element(NULL, description);
	puts("</p>");
      }

      fputs("<h4>Definition</h4>\n"
            "<pre>\n"
	    "typedef ", stdout);
      write_element(doc, mxmlFindElement(scut, scut, "type", NULL,
                                         NULL, MXML_DESCEND_FIRST));
      printf(" %s;\n</pre>\n", name);
    }
  }

 /*
  * List of unions...
  */

  if (mxmlFindElement(doc, doc, "union", NULL, NULL, MXML_DESCEND_FIRST))
  {
    puts("<!-- NEW PAGE -->\n"
         "<h2><a name='_unions'>Unions</a></h2>\n"
         "<ul>");

    for (scut = mxmlFindElement(doc, doc, "union", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "union", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(scut, "name");
      printf("\t<li><a href='#%s'><tt>%s</tt></a></li>\n", name, name);
    }

    puts("</ul>");

    for (scut = mxmlFindElement(doc, doc, "union", NULL, NULL,
                        	MXML_DESCEND_FIRST);
	 scut;
	 scut = mxmlFindElement(scut, doc, "union", NULL, NULL,
                        	MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(scut, "name");
      printf("<!-- NEW PAGE -->\n"
             "<h3><a name='%s'>%s</a></h3>\n"
	     "<hr noshade/>\n", name, name);

      description = mxmlFindElement(scut, scut, "description", NULL,
                                    NULL, MXML_DESCEND_FIRST);
      if (description)
      {
        fputs("<h4>Description</h4>\n"
	      "<p>", stdout);
	write_element(NULL, description);
	puts("</p>");
      }

      printf("<h4>Definition</h4>\n"
             "<pre>\n"
	     "union %s\n{\n", name);
      for (arg = mxmlFindElement(scut, scut, "variable", NULL, NULL,
                        	 MXML_DESCEND_FIRST);
	   arg;
	   arg = mxmlFindElement(arg, scut, "variable", NULL, NULL,
                        	 MXML_NO_DESCEND))
      {
	printf("  ");
	write_element(doc, mxmlFindElement(arg, arg, "type", NULL,
                                           NULL, MXML_DESCEND_FIRST));
	printf(" %s;\n", mxmlElementGetAttr(arg, "name"));
      }

      puts("};\n</pre>\n"
           "<h4>Members</h4>\n"
           "<p class='table'><table align='center' border='1' width='80%' "
           "cellpadding='5' cellspacing='0' width='80%'>\n"
           "<thead><tr bgcolor='#cccccc'><th>Name</th><th>Description</th></tr></thead>\n"
           "<tbody>");

      for (arg = mxmlFindElement(scut, scut, "variable", NULL, NULL,
                        	 MXML_DESCEND_FIRST);
	   arg;
	   arg = mxmlFindElement(arg, scut, "variable", NULL, NULL,
                        	 MXML_NO_DESCEND))
      {
	printf("<tr><td><tt>%s</tt></td><td>", mxmlElementGetAttr(arg, "name"));

	write_element(NULL, mxmlFindElement(arg, arg, "description", NULL,
                                            NULL, MXML_DESCEND_FIRST));

	puts("</td></tr>");
      }

      puts("</tbody></table></p>");
    }
  }

 /*
  * Variables...
  */

  if (mxmlFindElement(doc, doc, "variable", NULL, NULL, MXML_DESCEND_FIRST))
  {
    puts("<!-- NEW PAGE -->\n"
         "<h2><a name='_variables'>Variables</a></h2>\n"
         "<ul>");

    for (arg = mxmlFindElement(doc, doc, "variable", NULL, NULL,
                               MXML_DESCEND_FIRST);
	 arg;
	 arg = mxmlFindElement(arg, doc, "variable", NULL, NULL,
                               MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(arg, "name");
      printf("\t<li><a href='#%s'><tt>%s</tt></a></li>\n", name, name);
    }

    puts("</ul>");

    for (arg = mxmlFindElement(doc, doc, "variable", NULL, NULL,
                               MXML_DESCEND_FIRST);
	 arg;
	 arg = mxmlFindElement(arg, doc, "variable", NULL, NULL,
                               MXML_NO_DESCEND))
    {
      name = mxmlElementGetAttr(arg, "name");
      printf("<!-- NEW PAGE -->\n"
             "<h3><a name='%s'>%s</a></h3>\n"
	     "<hr noshade/>", name, name);

      description = mxmlFindElement(arg, arg, "description", NULL,
                                    NULL, MXML_DESCEND_FIRST);
      if (description)
      {
        fputs("<h4>Description</h4>\n"
	      "<p>", stdout);
	write_element(NULL, description);
	puts("</p>");
      }

      puts("<h4>Definition</h4>\n"
           "<pre>");

      write_element(doc, mxmlFindElement(arg, arg, "type", NULL,
                                         NULL, MXML_DESCEND_FIRST));
      printf(" %s", mxmlElementGetAttr(arg, "name"));
      if ((defval = mxmlElementGetAttr(arg, "default")) != NULL)
	printf(" %s", defval);
      puts(";\n</pre>");
    }
  }

 /*
  * Standard footer...
  */

  puts("</body>\n"
       "</html>");
}


/*
 * 'write_element()' - Write an element's text nodes.
 */

static void
write_element(mxml_node_t *doc,		/* I - Document tree */
              mxml_node_t *element)	/* I - Element to write */
{
  mxml_node_t	*node;			/* Current node */


  if (!element)
    return;

  for (node = element->child;
       node;
       node = mxmlWalkNext(node, element, MXML_NO_DESCEND))
    if (node->type == MXML_TEXT)
    {
      if (node->value.text.whitespace)
	putchar(' ');

      if (mxmlFindElement(doc, doc, "class", "name", node->value.text.string,
                          MXML_DESCEND) ||
	  mxmlFindElement(doc, doc, "enumeration", "name",
	                  node->value.text.string, MXML_DESCEND) ||
	  mxmlFindElement(doc, doc, "struct", "name", node->value.text.string,
                          MXML_DESCEND) ||
	  mxmlFindElement(doc, doc, "typedef", "name", node->value.text.string,
                          MXML_DESCEND) ||
	  mxmlFindElement(doc, doc, "union", "name", node->value.text.string,
                          MXML_DESCEND))
      {
        printf("<a href='#");
        write_string(node->value.text.string);
	printf("'>");
        write_string(node->value.text.string);
	printf("</a>");
      }
      else
        write_string(node->value.text.string);
    }
}


/*
 * 'write_string()' - Write a string, quoting XHTML special chars as needed...
 */

static void
write_string(const char *s)		/* I - String to write */
{
  while (*s)
  {
    if (*s == '&')
      fputs("&amp;", stdout);
    else if (*s == '<')
      fputs("&lt;", stdout);
    else if (*s == '>')
      fputs("&gt;", stdout);
    else if (*s == '\"')
      fputs("&quot;", stdout);
    else if (*s & 128)
    {
     /*
      * Convert UTF-8 to Unicode constant...
      */

      int	ch;			/* Unicode character */


      ch = *s & 255;

      if ((ch & 0xe0) == 0xc0)
      {
        ch = ((ch & 0x1f) << 6) | (s[1] & 0x3f);
	s ++;
      }
      else if ((ch & 0xf0) == 0xe0)
      {
        ch = ((((ch * 0x0f) << 6) | (s[1] & 0x3f)) << 6) | (s[2] & 0x3f);
	s += 2;
      }

      if (ch == 0xa0)
      {
       /*
        * Handle non-breaking space as-is...
	*/

        fputs("&nbsp;", stdout);
      }
      else
        printf("&#x%x;", ch);
    }
    else
      putchar(*s);

    s ++;
  }
}


/*
 * 'ws_cb()' - Whitespace callback for saving.
 */

static const char *			/* O - Whitespace string or NULL for none */
ws_cb(mxml_node_t *node,		/* I - Element node */
      int         where)		/* I - Where value */
{
  const char *name;			/* Name of element */
  int	depth;				/* Depth of node */
  static const char *spaces = "                                        ";
					/* Whitespace (40 spaces) for indent */


  name = node->value.element.name;

  switch (where)
  {
    case MXML_WS_BEFORE_CLOSE :
        if (strcmp(name, "argument") &&
	    strcmp(name, "class") &&
	    strcmp(name, "constant") &&
	    strcmp(name, "enumeration") &&
	    strcmp(name, "function") &&
	    strcmp(name, "mxmldoc") &&
	    strcmp(name, "namespace") &&
	    strcmp(name, "returnvalue") &&
	    strcmp(name, "struct") &&
	    strcmp(name, "typedef") &&
	    strcmp(name, "union") &&
	    strcmp(name, "variable"))
	  return (NULL);

	for (depth = -4; node; node = node->parent, depth += 2);
	if (depth > 40)
	  return (spaces);
	else if (depth < 2)
	  return (NULL);
	else
	  return (spaces + 40 - depth);

    case MXML_WS_AFTER_CLOSE :
	return ("\n");

    case MXML_WS_BEFORE_OPEN :
	for (depth = -4; node; node = node->parent, depth += 2);
	if (depth > 40)
	  return (spaces);
	else if (depth < 2)
	  return (NULL);
	else
	  return (spaces + 40 - depth);

    default :
    case MXML_WS_AFTER_OPEN :
        if (strcmp(name, "argument") &&
	    strcmp(name, "class") &&
	    strcmp(name, "constant") &&
	    strcmp(name, "enumeration") &&
	    strcmp(name, "function") &&
	    strcmp(name, "mxmldoc") &&
	    strcmp(name, "namespace") &&
	    strcmp(name, "returnvalue") &&
	    strcmp(name, "struct") &&
	    strcmp(name, "typedef") &&
	    strcmp(name, "union") &&
	    strcmp(name, "variable"))
	  return (NULL);
	else
          return ("\n");
  }
}


/*
 * End of "$Id$".
 */
