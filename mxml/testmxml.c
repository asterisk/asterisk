/*
 * "$Id$"
 *
 * Test program for Mini-XML, a small XML-like file parsing library.
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
 *   main()          - Main entry for test program.
 *   type_cb()       - XML data type callback for mxmlLoadFile()...
 *   whitespace_cb() - Let the mxmlSaveFile() function know when to insert
 *                     newlines and tabs...
 */

/*
 * Include necessary headers...
 */

#include "config.h"
#include "mxml.h"
#ifdef WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif /* WIN32 */
#include <fcntl.h>
#ifndef O_BINARY
#  define O_BINARY 0
#endif /* !O_BINARY */


/*
 * Local functions...
 */

mxml_type_t	type_cb(mxml_node_t *node);
const char	*whitespace_cb(mxml_node_t *node, int where);


/*
 * 'main()' - Main entry for test program.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line args */
{
  int			i;		/* Looping var */
  FILE			*fp;		/* File to read */
  int			fd;		/* File descriptor */
  mxml_node_t		*tree,		/* XML tree */
			*node;		/* Node which should be in test.xml */
  mxml_index_t		*ind;		/* XML index */
  char			buffer[16384];	/* Save string */
  static const char	*types[] =	/* Strings for node types */
			{
			  "MXML_ELEMENT",
			  "MXML_INTEGER",
			  "MXML_OPAQUE",
			  "MXML_REAL",
			  "MXML_TEXT"
			};


 /*
  * Check arguments...
  */

  if (argc != 2)
  {
    fputs("Usage: testmxml filename.xml\n", stderr);
    return (1);
  }

 /*
  * Test the basic functionality...
  */

  tree = mxmlNewElement(MXML_NO_PARENT, "element");

  if (!tree)
  {
    fputs("ERROR: No parent node in basic test!\n", stderr);
    return (1);
  }

  if (tree->type != MXML_ELEMENT)
  {
    fprintf(stderr, "ERROR: Parent has type %s (%d), expected MXML_ELEMENT!\n",
            tree->type < MXML_ELEMENT || tree->type > MXML_TEXT ?
	        "UNKNOWN" : types[tree->type], tree->type);
    mxmlDelete(tree);
    return (1);
  }

  if (strcmp(tree->value.element.name, "element"))
  {
    fprintf(stderr, "ERROR: Parent value is \"%s\", expected \"element\"!\n",
            tree->value.element.name);
    mxmlDelete(tree);
    return (1);
  }

  mxmlNewInteger(tree, 123);
  mxmlNewOpaque(tree, "opaque");
  mxmlNewReal(tree, 123.4f);
  mxmlNewText(tree, 1, "text");

  mxmlLoadString(tree, "<group type='string'>string string string</group>",
                 MXML_NO_CALLBACK);
  mxmlLoadString(tree, "<group type='integer'>1 2 3</group>",
                 MXML_INTEGER_CALLBACK);
  mxmlLoadString(tree, "<group type='real'>1.0 2.0 3.0</group>",
                 MXML_REAL_CALLBACK);
  mxmlLoadString(tree, "<group>opaque opaque opaque</group>",
                 MXML_OPAQUE_CALLBACK);

  node = tree->child;

  if (!node)
  {
    fputs("ERROR: No first child node in basic test!\n", stderr);
    mxmlDelete(tree);
    return (1);
  }

  if (node->type != MXML_INTEGER)
  {
    fprintf(stderr, "ERROR: First child has type %s (%d), expected MXML_INTEGER!\n",
            node->type < MXML_ELEMENT || node->type > MXML_TEXT ?
	        "UNKNOWN" : types[node->type], node->type);
    mxmlDelete(tree);
    return (1);
  }

  if (node->value.integer != 123)
  {
    fprintf(stderr, "ERROR: First child value is %d, expected 123!\n",
            node->value.integer);
    mxmlDelete(tree);
    return (1);
  }

  node = node->next;

  if (!node)
  {
    fputs("ERROR: No second child node in basic test!\n", stderr);
    mxmlDelete(tree);
    return (1);
  }

  if (node->type != MXML_OPAQUE)
  {
    fprintf(stderr, "ERROR: Second child has type %s (%d), expected MXML_OPAQUE!\n",
            node->type < MXML_ELEMENT || node->type > MXML_TEXT ?
	        "UNKNOWN" : types[node->type], node->type);
    mxmlDelete(tree);
    return (1);
  }

  if (!node->value.opaque || strcmp(node->value.opaque, "opaque"))
  {
    fprintf(stderr, "ERROR: Second child value is \"%s\", expected \"opaque\"!\n",
            node->value.opaque ? node->value.opaque : "(null)");
    mxmlDelete(tree);
    return (1);
  }

  node = node->next;

  if (!node)
  {
    fputs("ERROR: No third child node in basic test!\n", stderr);
    mxmlDelete(tree);
    return (1);
  }

  if (node->type != MXML_REAL)
  {
    fprintf(stderr, "ERROR: Third child has type %s (%d), expected MXML_REAL!\n",
            node->type < MXML_ELEMENT || node->type > MXML_TEXT ?
	        "UNKNOWN" : types[node->type], node->type);
    mxmlDelete(tree);
    return (1);
  }

  if (node->value.real != 123.4f)
  {
    fprintf(stderr, "ERROR: Third child value is %f, expected 123.4!\n",
            node->value.real);
    mxmlDelete(tree);
    return (1);
  }

  node = node->next;

  if (!node)
  {
    fputs("ERROR: No fourth child node in basic test!\n", stderr);
    mxmlDelete(tree);
    return (1);
  }

  if (node->type != MXML_TEXT)
  {
    fprintf(stderr, "ERROR: Fourth child has type %s (%d), expected MXML_TEXT!\n",
            node->type < MXML_ELEMENT || node->type > MXML_TEXT ?
	        "UNKNOWN" : types[node->type], node->type);
    mxmlDelete(tree);
    return (1);
  }

  if (!node->value.text.whitespace ||
      !node->value.text.string || strcmp(node->value.text.string, "text"))
  {
    fprintf(stderr, "ERROR: Fourth child value is %d,\"%s\", expected 1,\"text\"!\n",
            node->value.text.whitespace,
	    node->value.text.string ? node->value.text.string : "(null)");
    mxmlDelete(tree);
    return (1);
  }

  for (i = 0; i < 4; i ++)
  {
    node = node->next;

    if (!node)
    {
      fprintf(stderr, "ERROR: No group #%d child node in basic test!\n", i + 1);
      mxmlDelete(tree);
      return (1);
    }

    if (node->type != MXML_ELEMENT)
    {
      fprintf(stderr, "ERROR: Group child #%d has type %s (%d), expected MXML_ELEMENT!\n",
              i + 1, node->type < MXML_ELEMENT || node->type > MXML_TEXT ?
	                 "UNKNOWN" : types[node->type], node->type);
      mxmlDelete(tree);
      return (1);
    }
  }

 /*
  * Test indices...
  */

  ind = mxmlIndexNew(tree, NULL, NULL);
  if (!ind)
  {
    fputs("ERROR: Unable to create index of all nodes!\n", stderr);
    mxmlDelete(tree);
    return (1);
  }

  if (ind->num_nodes != 5)
  {
    fprintf(stderr, "ERROR: Index of all nodes contains %d "
                    "nodes; expected 5!\n", ind->num_nodes);
    mxmlIndexDelete(ind);
    mxmlDelete(tree);
    return (1);
  }

  mxmlIndexReset(ind);
  if (!mxmlIndexFind(ind, "group", NULL))
  {
    fputs("ERROR: mxmlIndexFind for \"group\" failed!\n", stderr);
    mxmlIndexDelete(ind);
    mxmlDelete(tree);
    return (1);
  }

  mxmlIndexDelete(ind);

  ind = mxmlIndexNew(tree, "group", NULL);
  if (!ind)
  {
    fputs("ERROR: Unable to create index of groups!\n", stderr);
    mxmlDelete(tree);
    return (1);
  }

  if (ind->num_nodes != 4)
  {
    fprintf(stderr, "ERROR: Index of groups contains %d "
                    "nodes; expected 4!\n", ind->num_nodes);
    mxmlIndexDelete(ind);
    mxmlDelete(tree);
    return (1);
  }

  mxmlIndexReset(ind);
  if (!mxmlIndexEnum(ind))
  {
    fputs("ERROR: mxmlIndexEnum failed!\n", stderr);
    mxmlIndexDelete(ind);
    mxmlDelete(tree);
    return (1);
  }

  mxmlIndexDelete(ind);

  ind = mxmlIndexNew(tree, NULL, "type");
  if (!ind)
  {
    fputs("ERROR: Unable to create index of type attributes!\n", stderr);
    mxmlDelete(tree);
    return (1);
  }

  if (ind->num_nodes != 3)
  {
    fprintf(stderr, "ERROR: Index of type attributes contains %d "
                    "nodes; expected 3!\n", ind->num_nodes);
    mxmlIndexDelete(ind);
    mxmlDelete(tree);
    return (1);
  }

  mxmlIndexReset(ind);
  if (!mxmlIndexFind(ind, NULL, "string"))
  {
    fputs("ERROR: mxmlIndexFind for \"string\" failed!\n", stderr);
    mxmlIndexDelete(ind);
    mxmlDelete(tree);
    return (1);
  }

  mxmlIndexDelete(ind);

  ind = mxmlIndexNew(tree, "group", "type");
  if (!ind)
  {
    fputs("ERROR: Unable to create index of elements and attributes!\n", stderr);
    mxmlDelete(tree);
    return (1);
  }

  if (ind->num_nodes != 3)
  {
    fprintf(stderr, "ERROR: Index of elements and attributes contains %d "
                    "nodes; expected 3!\n", ind->num_nodes);
    mxmlIndexDelete(ind);
    mxmlDelete(tree);
    return (1);
  }

  mxmlIndexReset(ind);
  if (!mxmlIndexFind(ind, "group", "string"))
  {
    fputs("ERROR: mxmlIndexFind for \"string\" failed!\n", stderr);
    mxmlIndexDelete(ind);
    mxmlDelete(tree);
    return (1);
  }

  mxmlIndexDelete(ind);

 /*
  * Check the mxmlDelete() works properly...
  */

  for (i = 0; i < 8; i ++)
  {
    if (tree->child)
      mxmlDelete(tree->child);
    else
    {
      fprintf(stderr, "ERROR: Child pointer prematurely NULL on child #%d\n",
              i + 1);
      mxmlDelete(tree);
      return (1);
    }
  }

  if (tree->child)
  {
    fputs("ERROR: Child pointer not NULL after deleting all children!\n", stderr);
    return (1);
  }

  if (tree->last_child)
  {
    fputs("ERROR: Last child pointer not NULL after deleting all children!\n", stderr);
    return (1);
  }

  mxmlDelete(tree);

 /*
  * Open the file...
  */

  if (argv[1][0] == '<')
    tree = mxmlLoadString(NULL, argv[1], type_cb);
  else if ((fp = fopen(argv[1], "rb")) == NULL)
  {
    perror(argv[1]);
    return (1);
  }
  else
  {
   /*
    * Read the file...
    */

    tree = mxmlLoadFile(NULL, fp, type_cb);

    fclose(fp);
  }

  if (!tree)
  {
    fputs("Unable to read XML file!\n", stderr);
    return (1);
  }

  if (!strcmp(argv[1], "test.xml"))
  {
   /*
    * Verify that mxmlFindElement() and indirectly mxmlWalkNext() work
    * properly...
    */

    if ((node = mxmlFindElement(tree, tree, "choice", NULL, NULL,
                                MXML_DESCEND)) == NULL)
    {
      fputs("Unable to find first <choice> element in XML tree!\n", stderr);
      mxmlDelete(tree);
      return (1);
    }

    if ((node = mxmlFindElement(node, tree, "choice", NULL, NULL,
                                MXML_NO_DESCEND)) == NULL)
    {
      fputs("Unable to find second <choice> element in XML tree!\n", stderr);
      mxmlDelete(tree);
      return (1);
    }
  }

 /*
  * Print the XML tree...
  */

  mxmlSaveFile(tree, stdout, whitespace_cb);

 /*
  * Save the XML tree to a string and print it...
  */

  if (mxmlSaveString(tree, buffer, sizeof(buffer), whitespace_cb) > 0)
    fputs(buffer, stderr);

 /*
  * Delete the tree...
  */

  mxmlDelete(tree);

 /*
  * Read from/write to file descriptors...
  */

  if (argv[1][0] != '<')
  {
   /*
    * Open the file again...
    */

    if ((fd = open(argv[1], O_RDONLY | O_BINARY)) < 0)
    {
      perror(argv[1]);
      return (1);
    }

   /*
    * Read the file...
    */

    tree = mxmlLoadFd(NULL, fd, type_cb);

    close(fd);

   /*
    * Create filename.xmlfd...
    */

    snprintf(buffer, sizeof(buffer), "%sfd", argv[1]);

    if ((fd = open(buffer, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666)) < 0)
    {
      perror(buffer);
      mxmlDelete(tree);
      return (1);
    }

   /*
    * Write the file...
    */

    mxmlSaveFd(tree, fd, whitespace_cb);

    close(fd);

   /*
    * Delete the tree...
    */

    mxmlDelete(tree);
  }

 /*
  * Return...
  */

  return (0);
}


/*
 * 'type_cb()' - XML data type callback for mxmlLoadFile()...
 */

mxml_type_t				/* O - Data type */
type_cb(mxml_node_t *node)		/* I - Element node */
{
  const char	*type;			/* Type string */


 /*
  * You can lookup attributes and/or use the element name, hierarchy, etc...
  */

  if ((type = mxmlElementGetAttr(node, "type")) == NULL)
    type = node->value.element.name;

  if (!strcmp(type, "integer"))
    return (MXML_INTEGER);
  else if (!strcmp(type, "opaque") || !strcmp(type, "pre"))
    return (MXML_OPAQUE);
  else if (!strcmp(type, "real"))
    return (MXML_REAL);
  else
    return (MXML_TEXT);
}


/*
 * 'whitespace_cb()' - Let the mxmlSaveFile() function know when to insert
 *                     newlines and tabs...
 */

const char *				/* O - Whitespace string or NULL */
whitespace_cb(mxml_node_t *node,	/* I - Element node */
              int         where)	/* I - Open or close tag? */
{
  mxml_node_t	*parent;		/* Parent node */
  int		level;			/* Indentation level */
  const char	*name;			/* Name of element */
  static const char *tabs = "\t\t\t\t\t\t\t\t";
					/* Tabs for indentation */


 /*
  * We can conditionally break to a new line before or after any element.
  * These are just common HTML elements...
  */

  name = node->value.element.name;

  if (!strcmp(name, "html") || !strcmp(name, "head") || !strcmp(name, "body") ||
      !strcmp(name, "pre") || !strcmp(name, "p") ||
      !strcmp(name, "h1") || !strcmp(name, "h2") || !strcmp(name, "h3") ||
      !strcmp(name, "h4") || !strcmp(name, "h5") || !strcmp(name, "h6"))
  {
   /*
    * Newlines before open and after close...
    */

    if (where == MXML_WS_BEFORE_OPEN || where == MXML_WS_AFTER_CLOSE)
      return ("\n");
  }
  else if (!strcmp(name, "dl") || !strcmp(name, "ol") || !strcmp(name, "ul"))
  {
   /*
    * Put a newline before and after list elements...
    */

    return ("\n");
  }
  else if (!strcmp(name, "dd") || !strcmp(name, "dt") || !strcmp(name, "li"))
  {
   /*
    * Put a tab before <li>'s, <dd>'s, and <dt>'s, and a newline after them...
    */

    if (where == MXML_WS_BEFORE_OPEN)
      return ("\t");
    else if (where == MXML_WS_AFTER_CLOSE)
      return ("\n");
  }
  else if (!strcmp(name, "?xml"))
  {
    return (NULL);
  }
  else if (where == MXML_WS_BEFORE_OPEN ||
           ((!strcmp(name, "choice") || !strcmp(name, "option")) &&
	    where == MXML_WS_BEFORE_CLOSE))
  {
    for (level = -1, parent = node->parent;
         parent;
	 level ++, parent = parent->parent);

    if (level > 8)
      level = 8;
    else if (level < 0)
      level = 0;

    return (tabs + 8 - level);
  }
  else if (where == MXML_WS_AFTER_CLOSE ||
           ((!strcmp(name, "group") || !strcmp(name, "option") ||
	     !strcmp(name, "choice")) &&
            where == MXML_WS_AFTER_OPEN))
    return ("\n");
  else if (where == MXML_WS_AFTER_OPEN && !node->child)
    return ("\n");

 /*
  * Return NULL for no added whitespace...
  */

  return (NULL);
}


/*
 * End of "$Id$".
 */
