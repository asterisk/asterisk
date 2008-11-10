/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Handy terminal functions for vt* terms
 */

#ifndef _ASTERISK_TERM_H
#define _ASTERISK_TERM_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define ESC 0x1b

/*! \name Terminal Attributes 
*/
/*@{ */
#define ATTR_RESET	0
#define ATTR_BRIGHT	1
#define ATTR_DIM	2
#define ATTR_UNDER	4
#define ATTR_BLINK	5
#define ATTR_REVER	7
#define ATTR_HIDDEN	8
/*@} */

/*! \name Terminal Colors
*/
/*@{ */
#define COLOR_BLACK 	30
#define COLOR_GRAY  	(30 | 128)
#define COLOR_RED	31
#define COLOR_BRRED	(31 | 128)
#define COLOR_GREEN	32
#define COLOR_BRGREEN	(32 | 128)
#define COLOR_BROWN	33
#define COLOR_YELLOW	(33 | 128)
#define COLOR_BLUE	34
#define COLOR_BRBLUE	(34 | 128)
#define COLOR_MAGENTA	35
#define COLOR_BRMAGENTA (35 | 128)
#define COLOR_CYAN      36
#define COLOR_BRCYAN    (36 | 128)
#define COLOR_WHITE     37
#define COLOR_BRWHITE   (37 | 128)
/*@} */

/*! \brief Maximum number of characters needed for a color escape sequence,
 *         plus a null char */
#define AST_TERM_MAX_ESCAPE_CHARS   23

char *term_color(char *outbuf, const char *inbuf, int fgcolor, int bgcolor, int maxout);

/*!
 * \brief Append a color sequence to an ast_str
 *
 * \param str The string to append to
 * \param fgcolor foreground color
 * \param bgcolor background color
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_term_color_code(struct ast_str **str, int fgcolor, int bgcolor);

/*!
 * \brief Write a color sequence to a string
 *
 * \param outbuf the location to write to
 * \param fgcolor foreground color
 * \param bgcolor background color
 * \param maxout maximum number of characters to write
 *
 * \return outbuf
 */
char *term_color_code(char *outbuf, int fgcolor, int bgcolor, int maxout);

char *term_strip(char *outbuf, char *inbuf, int maxout);

void term_filter_escapes(char *line);

char *term_prompt(char *outbuf, const char *inbuf, int maxout);

char *term_prep(void);

char *term_end(void);

char *term_quit(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_TERM_H */
