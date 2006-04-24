/*
 * "$Id$"
 *
 * Attribute support code for Mini-XML, a small XML-like file parsing library.
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
 *   mxmlElementGetAttr() - Get an attribute.
 *   mxmlElementSetAttr() - Set an attribute.
 */

/*
 * Include necessary headers...
 */

#include "config.h"
#include "mxml.h"


/*
 * 'mxmlElementGetAttr()' - Get an attribute.
 *
 * This function returns NULL if the node is not an element or the
 * named attribute does not exist.
 */

const char *				/* O - Attribute value or NULL */
mxmlElementGetAttr(mxml_node_t *node,	/* I - Element node */
                   const char  *name)	/* I - Name of attribute */
{
  int	i;				/* Looping var */
  mxml_attr_t	*attr;			/* Cirrent attribute */


#ifdef DEBUG
  fprintf(stderr, "mxmlElementGetAttr(node=%p, name=\"%s\")\n",
          node, name ? name : "(null)");
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!node || node->type != MXML_ELEMENT || !name)
    return (NULL);

 /*
  * Look for the attribute...
  */

  for (i = node->value.element.num_attrs, attr = node->value.element.attrs;
       i > 0;
       i --, attr ++)
    if (!strcmp(attr->name, name))
      return (attr->value);

 /*
  * Didn't find attribute, so return NULL...
  */

  return (NULL);
}


/*
 * 'mxmlElementSetAttr()' - Set an attribute.
 *
 * If the named attribute already exists, the value of the attribute
 * is replaced by the new string value. The string value is copied
 * into the element node. This function does nothing if the node is
 * not an element.
 */

void
mxmlElementSetAttr(mxml_node_t *node,	/* I - Element node */
                   const char  *name,	/* I - Name of attribute */
                   const char  *value)	/* I - Attribute value */
{
  int		i;			/* Looping var */
  mxml_attr_t	*attr;			/* New attribute */


#ifdef DEBUG
  fprintf(stderr, "mxmlElementSetAttr(node=%p, name=\"%s\", value=\"%s\")\n",
          node, name ? name : "(null)", value ? value : "(null)");
#endif /* DEBUG */

 /*
  * Range check input...
  */

  if (!node || node->type != MXML_ELEMENT || !name)
    return;

 /*
  * Look for the attribute...
  */

  for (i = node->value.element.num_attrs, attr = node->value.element.attrs;
       i > 0;
       i --, attr ++)
    if (!strcmp(attr->name, name))
    {
     /*
      * Replace the attribute value and return...
      */

      if (attr->value)
        free(attr->value);

      if (value)
      {
	if ((attr->value = strdup(value)) == NULL)
	  mxml_error("Unable to allocate memory for attribute '%s' in element %s!",
                     name, node->value.element.name);
      }
      else
        attr->value = NULL;

      return;
    }

 /*
  * Attribute not found, so add a new one...
  */

  if (node->value.element.num_attrs == 0)
    attr = malloc(sizeof(mxml_attr_t));
  else
    attr = realloc(node->value.element.attrs,
                   (node->value.element.num_attrs + 1) * sizeof(mxml_attr_t));

  if (!attr)
  {
    mxml_error("Unable to allocate memory for attribute '%s' in element %s!",
               name, node->value.element.name);
    return;
  }

  node->value.element.attrs = attr;
  attr += node->value.element.num_attrs;

  attr->name = strdup(name);
  if (value)
    attr->value = strdup(value);
  else
    attr->value = NULL;

  if (!attr->name || (!attr->value && value))
  {
    if (attr->name)
      free(attr->name);

    if (attr->value)
      free(attr->value);

    mxml_error("Unable to allocate memory for attribute '%s' in element %s!",
               name, node->value.element.name);

    return;
  }
    
  node->value.element.num_attrs ++;
}


/*
 * End of "$Id$".
 */
