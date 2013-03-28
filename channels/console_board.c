/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright 2007-2008, Marta Carbone, Luigi Rizzo
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
 *
 * $Revision$
 */

/* 
 * Message board implementation.
 *
 * A message board is a region of the SDL screen where
 * messages can be printed, like on a terminal window.
 *
 * At the moment we support fix-size font.
 *
 * The text is stored in a buffer
 * of fixed size (rows and cols). A portion of the buffer is
 * visible on the screen, and the visible window can be moved up and
 * down by dragging (not yet!)
 * 
 * TODO: font dynamic allocation
 *
 * The region where the text is displayed on the screen is defined
 * as keypad element, (the name is defined in the `region' variable
 * so the board geometry can be read from the skin or from the
 * configuration file).
 */

/*** MODULEINFO
	<support_level>extended</support_level>
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"	/* ast_strdupa */
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")
#include "asterisk/utils.h"	/* ast_strdupa */
#include "console_video.h"	/* ast_strdupa */

#ifdef HAVE_SDL	/* we only use this code if SDL is available */
#include <SDL/SDL.h>

/* Fonts characterization. XXX should be read from the file */
#define FONT_H 20			/* char height, pixels */
#define FONT_W 9			/* char width, pixels */

struct board {
	int		kb_output;	/* identity of the board */
	/* pointer to the destination surface (on the keypad window) */
	SDL_Surface	*screen;	/* the main screen */
	SDL_Rect	*p_rect;	/* where to write on the main screen */
	SDL_Surface	*blank;		/* original content of the window */

	int	v_h;	/* virtual text height, in lines */
	int	v_w;	/* virtual text width, in lines (probably same as p_w) */
	int	p_h;	/* physical (displayed) text height, in lines
			 * XXX p_h * FONT_H = pixel_height */
	int	p_w;	/* physical (displayed) text width, in characters
			 * XXX p_w * FONT_W = pixel_width */

	int	cur_col; /* print position (free character) on the last line */
	int	cur_line;	/* first (or last ?) virtual line displayed,
					 * 0 is the line at the bottom, 1 is the one above,...
					 */

	SDL_Surface     *font;		/* points to a surface in the gui structure */
	SDL_Rect	*font_rects;	/* pointer to the font rects */
	char		*text;
				/* text buffer, v_h * v_w char.
				 * We make sure the buffer is always full,
				 * print on some position on the last line,
				 * and scroll up when appending new text
				 */
};

/*! \brief Initialize the board.
 * return 0 on success, 1 on error
 * TODO, if this is done at reload time,
 * free resources before allocate new ones
 * TODO: resource deallocation in case of error.
 * TODO: move the font load at gui_initialization
 * TODO: deallocation of the message history
 */
struct board *board_setup(SDL_Surface *screen, SDL_Rect *dest,
	SDL_Surface *font, SDL_Rect *font_rects);
struct board *board_setup(SDL_Surface *screen, SDL_Rect *dest,
	SDL_Surface *font, SDL_Rect *font_rects)
{
	struct board *b = ast_calloc(1, sizeof (*b));
	SDL_Rect br;

	if (b == NULL)
		return NULL;
	/* font, points to the gui structure */
	b->font = font;
	b->font_rects = font_rects;

	/* Destination rectangle on the screen - reference is the whole screen */
	b->p_rect = dest;
	b->screen = screen;

	/* compute physical sizes */
	b->p_h = b->p_rect->h/FONT_H;
	b->p_w = b->p_rect->w/FONT_W;

	/* virtual sizes */
	b->v_h = b->p_h * 10; /* XXX 10 times larger */
	b->v_w = b->p_w;	/* same width */

	/* the rectangle we actually use */
	br.h = b->p_h * FONT_H;	/* pixel sizes of the background */
	br.w = b->p_w * FONT_W;
	br.x = br.y = 0;

	/* allocate a buffer for the text */
	b->text = ast_calloc(b->v_w*b->v_h + 1, 1);
	if (b->text == NULL) {
		ast_log(LOG_WARNING, "Unable to allocate board history memory.\n");
		ast_free(b);
		return NULL;
	}
	memset(b->text, ' ', b->v_w * b->v_h);	/* fill with spaces */

	/* make a copy of the original rectangle, for cleaning up */
	b->blank = SDL_CreateRGBSurface(screen->flags, br.w, br.h,
		screen->format->BitsPerPixel,
		screen->format->Rmask, screen->format->Gmask,
		screen->format->Bmask, screen->format->Amask);

	if (b->blank == NULL) { 
		ast_log(LOG_WARNING, "Unable to allocate board virtual screen: %s\n",
				SDL_GetError());
		ast_free(b->text);
		ast_free(b);
		return NULL;
	}
	SDL_BlitSurface(screen, b->p_rect, b->blank, &br);

	/* Set color key, if not alpha channel present */
	//colorkey = SDL_MapRGB(b->board_surface->format, 0, 0, 0);
	//SDL_SetColorKey(b->board_surface, SDL_SRCCOLORKEY, colorkey);

	b->cur_col = 0;		/* current print column */
	b->cur_line = 0;	/* last line displayed */

	if (0) ast_log(LOG_WARNING, "Message board %dx%d@%d,%d successfully initialized\n",
		b->p_rect->w, b->p_rect->h,
		b->p_rect->x, b->p_rect->y);
	return b;
}

/* Render the text on the board surface.
 * The first line to render is the one at v_h - p_h - cur_line,
 * the size is p_h * p_w.
 * XXX we assume here that p_w = v_w.
 */
static void render_board(struct board *b)
{
	int first_row = b->v_h - b->p_h - b->cur_line;
	int first_char = b->v_w * first_row;
	int last_char = first_char + b->p_h * b->v_w;
	int i, col;
	SDL_Rect dst;

	/* top left char on the physical surface */
	dst.w = FONT_W;
	dst.h = FONT_H;
	dst.x = b->p_rect->x;
	dst.y = b->p_rect->y;


	/* clean the surface board */
	SDL_BlitSurface(b->blank, NULL, b->screen, b->p_rect);

	/* blit all characters */
	for (i = first_char, col = 0; i <  last_char; i++) {
		int c = b->text[i] - 32;	/* XXX first 32 chars are not printable */
		if (c < 0) /* buffer terminator or anything else is a blank */
			c = 0;
		SDL_BlitSurface(b->font, &b->font_rects[c], b->screen, &dst);
		/* point dst to next char position */
		dst.x += dst.w;
		col++;
		if (col >= b->v_w) { /* next row */
			dst.x = b->p_rect->x;
			dst.y += dst.h;
			col = 0;
		}
	}
	SDL_UpdateRects(b->screen, 1, b->p_rect);	/* Update the screen */
}

void move_message_board(struct board *b, int dy)
{
	int cur = b->cur_line + dy;
	if (cur < 0)
		cur = 0;
	else if (cur >= b->v_h - b->p_h)
		cur = b->v_h - b->p_h - 1;
	b->cur_line = cur;
	render_board(b);
}

/* return the content of a board */
const char *read_message(const struct board *b)
{
	return b->text;
}

int reset_board(struct board *b)
{
	memset(b->text, ' ', b->v_w * b->v_h);	/* fill with spaces */
	b->cur_col = 0;
	b->cur_line = 0;
	render_board(b);
	return 0;
}
/* Store the message on the history board
 * and blit on screen if required.
 * XXX now easy. only regular chars
 */
int print_message(struct board *b, const char *s)
{
	int i, l, row, col;
	char *dst;

	if (ast_strlen_zero(s))
		return 0;

	l = strlen(s);
	row = 0;
	col = b->cur_col;
	/* First, only check how much space we need.
	 * Starting from the current print position, we move
	 * it forward and down (if necessary) according to input
	 * characters (including newlines, tabs, backspaces...).
	 * At the end, row tells us how many rows to scroll, and
	 * col (ignored) is the final print position.
	 */
	for (i = 0; i < l; i++) {
		switch (s[i]) {
		case '\r':
			col = 0;
			break;
		case '\n':
			col = 0;
			row++;
			break;
		case '\b':
			if (col > 0)
				col--;
			break;
		default:
			if (s[i] < 32) /* signed, so take up to 127 */
				break;
			col++;
			if (col >= b->v_w) {
				col -= b->v_w;
				row++;
			}
			break;
		}
	}
	/* scroll the text window */
	if (row > 0) { /* need to scroll by 'row' rows */
		memcpy(b->text, b->text + row * b->v_w, b->v_w * (b->v_h - row));
		/* clean the destination area */
		dst = b->text + b->v_w * (b->v_h - row - 1) + b->cur_col;
		memset(dst, ' ', b->v_w - b->cur_col + b->v_w * row);
	}
	/* now do the actual printing. The print position is 'row' lines up
	 * from the bottom of the buffer, start at the same 'cur_col' as before.
	 * dst points to the beginning of the current line.
	 */
	dst = b->text + b->v_w * (b->v_h - row - 1); /* start of current line */
	col = b->cur_col;
	for (i = 0; i < l; i++) {
		switch (s[i]) {
		case '\r':
			col = 0;
			break;
		case '\n':	/* move to beginning of next line */
			dst[col] = '\0'; /* mark the rest of the line as empty */
			col = 0;
			dst += b->v_w;
			break;
		case '\b':	/* one char back */
			if (col > 0)
				col--;
			dst[col] = ' '; /* delete current char */
			break;
		default:
			if (s[i] < 32) /* signed, so take up to 127 */
				break;	/* non printable */
			dst[col] = s[i];	/* store character */
			col++;
			if (col >= b->v_w) {
				col -= b->v_w;
				dst += b->v_w;
			}
			break;
		}
	}
	dst[col] = '\0'; /* the current position is empty */
	b->cur_col = col;
	/* everything is printed now, must do the rendering */
	render_board(b);
	return 1;
}

/* deletes a board.
 * we make the free operation on any fields of the board structure allocated
 * in dynamic memory
 */
void delete_board(struct board *b)
{
	if (b) {
		/* deletes the text */
		if (b->text)
			ast_free (b->text);
		/* deallocates the blank surface */
		SDL_FreeSurface(b->blank);
		/* deallocates the board */
		ast_free(b);
	}
}

#if 0
/*! \brief refresh the screen, and also grab a bunch of events.
 */
static int scroll_message(...)
{
if moving up, scroll text up;
    if (gui->message_board.screen_cur > 0)
	gui->message_board.screen_cur--;
otherwise scroll text down.
    if ((b->screen_cur + b->p_line) < b->board_next) {
	gui->message_board.screen_cur++;
#endif /* notyet */

#endif /* HAVE_SDL */
