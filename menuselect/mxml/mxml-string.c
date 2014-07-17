/*
 * "$Id$"
 *
 * String functions for Mini-XML, a small XML-like file parsing library.
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
 *   mxml_strdup()    - Duplicate a string.
 *   mxml_strdupf()   - Format and duplicate a string.
 *   mxml_vsnprintf() - Format a string into a fixed size buffer.
 */

/*
 * Include necessary headers...
 */

#include "config.h"


/*
 * 'mxml_strdup()' - Duplicate a string.
 */

#ifndef HAVE_STRDUP
char 	*				/* O - New string pointer */
mxml_strdup(const char *s)		/* I - String to duplicate */
{
  char	*t;				/* New string pointer */


  if (s == NULL)
    return (NULL);

  if ((t = malloc(strlen(s) + 1)) == NULL)
    return (NULL);

  return (strcpy(t, s));
}
#endif /* !HAVE_STRDUP */


/*
 * 'mxml_strdupf()' - Format and duplicate a string.
 */

char *					/* O - New string pointer */
mxml_strdupf(const char *format,	/* I - Printf-style format string */
             va_list    ap)		/* I - Pointer to additional arguments */
{
  int	bytes;				/* Number of bytes required */
  char	*buffer,			/* String buffer */
	temp[256];			/* Small buffer for first vsnprintf */


 /*
  * First format with a tiny buffer; this will tell us how many bytes are
  * needed...
  */

  bytes = vsnprintf(temp, sizeof(temp), format, ap);

  if (bytes < sizeof(temp))
  {
   /*
    * Hey, the formatted string fits in the tiny buffer, so just dup that...
    */

    return (strdup(temp));
  }

 /*
  * Allocate memory for the whole thing and reformat to the new, larger
  * buffer...
  */

  if ((buffer = calloc(1, bytes + 1)) != NULL)
    vsnprintf(buffer, bytes + 1, format, ap);

 /*
  * Return the new string...
  */

  return (buffer);
}


#ifndef HAVE_VSNPRINTF
/*
 * 'mxml_vsnprintf()' - Format a string into a fixed size buffer.
 */

int					/* O - Number of bytes formatted */
mxml_vsnprintf(char       *buffer,	/* O - Output buffer */
               size_t     bufsize,	/* O - Size of output buffer */
	       const char *format,	/* I - Printf-style format string */
	       va_list    ap)		/* I - Pointer to additional arguments */
{
  char		*bufptr,		/* Pointer to position in buffer */
		*bufend,		/* Pointer to end of buffer */
		sign,			/* Sign of format width */
		size,			/* Size character (h, l, L) */
		type;			/* Format type character */
  const char	*bufformat;		/* Start of format */
  int		width,			/* Width of field */
		prec;			/* Number of characters of precision */
  char		tformat[100],		/* Temporary format string for sprintf() */
		temp[1024];		/* Buffer for formatted numbers */
  char		*s;			/* Pointer to string */
  int		slen;			/* Length of string */
  int		bytes;			/* Total number of bytes needed */


 /*
  * Loop through the format string, formatting as needed...
  */

  bufptr = buffer;
  bufend = buffer + bufsize - 1;
  bytes  = 0;

  while (*format)
  {
    if (*format == '%')
    {
      bufformat = format;
      format ++;

      if (*format == '%')
      {
        *bufptr++ = *format++;
	continue;
      }
      else if (strchr(" -+#\'", *format))
        sign = *format++;
      else
        sign = 0;

      width = 0;
      while (isdigit(*format))
        width = width * 10 + *format++ - '0';

      if (*format == '.')
      {
        format ++;
	prec = 0;

	while (isdigit(*format))
          prec = prec * 10 + *format++ - '0';
      }
      else
        prec = -1;

      if (*format == 'l' && format[1] == 'l')
      {
        size = 'L';
	format += 2;
      }
      else if (*format == 'h' || *format == 'l' || *format == 'L')
        size = *format++;

      if (!*format)
        break;

      type = *format++;

      switch (type)
      {
	case 'E' : /* Floating point formats */
	case 'G' :
	case 'e' :
	case 'f' :
	case 'g' :
	    if ((format - bufformat + 1) > sizeof(tformat) ||
	        (width + 2) > sizeof(temp))
	      break;

	    strncpy(tformat, bufformat, format - bufformat);
	    tformat[format - bufformat] = '\0';

	    sprintf(temp, tformat, va_arg(ap, double));

            bytes += strlen(temp);

            if (bufptr)
	    {
	      if ((bufptr + strlen(temp)) > bufend)
	      {
		strncpy(bufptr, temp, bufend - bufptr);
		bufptr = bufend;
		break;
	      }
	      else
	      {
		strcpy(bufptr, temp);
		bufptr += strlen(temp);
	      }
	    }
	    break;

        case 'B' : /* Integer formats */
	case 'X' :
	case 'b' :
        case 'd' :
	case 'i' :
	case 'o' :
	case 'u' :
	case 'x' :
	    if ((format - bufformat + 1) > sizeof(tformat) ||
	        (width + 2) > sizeof(temp))
	      break;

	    strncpy(tformat, bufformat, format - bufformat);
	    tformat[format - bufformat] = '\0';

	    sprintf(temp, tformat, va_arg(ap, int));

            bytes += strlen(temp);

	    if (bufptr)
	    {
	      if ((bufptr + strlen(temp)) > bufend)
	      {
		strncpy(bufptr, temp, bufend - bufptr);
		bufptr = bufend;
		break;
	      }
	      else
	      {
		strcpy(bufptr, temp);
		bufptr += strlen(temp);
	      }
	    }
	    break;
	    
	case 'p' : /* Pointer value */
	    if ((format - bufformat + 1) > sizeof(tformat) ||
	        (width + 2) > sizeof(temp))
	      break;

	    strncpy(tformat, bufformat, format - bufformat);
	    tformat[format - bufformat] = '\0';

	    sprintf(temp, tformat, va_arg(ap, void *));

            bytes += strlen(temp);

	    if (bufptr)
	    {
	      if ((bufptr + strlen(temp)) > bufend)
	      {
		strncpy(bufptr, temp, bufend - bufptr);
		bufptr = bufend;
		break;
	      }
	      else
	      {
		strcpy(bufptr, temp);
		bufptr += strlen(temp);
	      }
	    }
	    break;

        case 'c' : /* Character or character array */
	    bytes += width;

	    if (bufptr)
	    {
	      if (width <= 1)
		*bufptr++ = va_arg(ap, int);
	      else
	      {
		if ((bufptr + width) > bufend)
	          width = bufend - bufptr;

		memcpy(bufptr, va_arg(ap, char *), width);
		bufptr += width;
	      }
	    }
	    break;

	case 's' : /* String */
	    if ((s = va_arg(ap, char *)) == NULL)
	      s = "(null)";

	    slen = strlen(s);
	    if (slen > width && prec != width)
	      width = slen;

            bytes += width;

	    if (bufptr)
	    {
	      if ((bufptr + width) > bufend)
		width = bufend - bufptr;

              if (slen > width)
		slen = width;

	      if (sign == '-')
	      {
		strncpy(bufptr, s, slen);
		memset(bufptr + slen, ' ', width - slen);
	      }
	      else
	      {
		memset(bufptr, ' ', width - slen);
		strncpy(bufptr + width - slen, s, slen);
	      }

	      bufptr += width;
	    }
	    break;

	case 'n' : /* Output number of chars so far */
	    if ((format - bufformat + 1) > sizeof(tformat) ||
	        (width + 2) > sizeof(temp))
	      break;

	    strncpy(tformat, bufformat, format - bufformat);
	    tformat[format - bufformat] = '\0';

	    sprintf(temp, tformat, va_arg(ap, int));

            bytes += strlen(temp);

	    if (bufptr)
	    {
	      if ((bufptr + strlen(temp)) > bufend)
	      {
		strncpy(bufptr, temp, bufend - bufptr);
		bufptr = bufend;
		break;
	      }
	      else
	      {
		strcpy(bufptr, temp);
		bufptr += strlen(temp);
	      }
	    }
	    break;
      }
    }
    else
    {
      bytes ++;

      if (bufptr && bufptr < bufend)
	*bufptr++ = *format++;
    }
  }

 /*
  * Nul-terminate the string and return the number of characters needed.
  */

  *bufptr = '\0';

  return (bytes);
}
#endif /* !HAVE_VSNPRINTF */


/*
 * End of "$Id$".
 */
