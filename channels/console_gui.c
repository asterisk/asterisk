/*
 * GUI for console video.
 * The routines here are in charge of loading the keypad and handling events.
 * $Revision$
 */

/*
 * GUI layout, structure and management
 
For the GUI we use SDL to create a large surface (gui->screen)
containing tree sections: remote video on the left, local video
on the right, and the keypad with all controls and text windows
in the center.
The central section is built using an image for the skin, fonts and
other GUI elements.  Comments embedded in the image to indicate to
what function each area is mapped to.

Mouse and keyboard events are detected on the whole surface, and
handled differently according to their location:

- drag on the local video window are used to move the captured
  area (in the case of X11 grabber) or the picture-in-picture
  location (in case of camera included on the X11 grab).
- click on the keypad are mapped to the corresponding key;
- drag on some keypad areas (sliders etc.) are mapped to the
  corresponding functions;
- keystrokes are used as keypad functions, or as text input
  if we are in text-input mode.

Configuration options control the appeareance of the gui:

    keypad = /tmp/phone.jpg	; the skin
    keypad_font = /tmp/font.ttf	; the font to use for output (XXX deprecated)

 *
 */

#include "asterisk.h"
#include "console_video.h"
#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/utils.h"	/* ast_calloc and ast_realloc */
#include <math.h>		/* sqrt */

/* We use 3 'windows' in the GUI */
enum { WIN_LOCAL, WIN_REMOTE, WIN_KEYPAD, WIN_MAX };

#ifndef HAVE_SDL	/* stubs if we don't have any sdl */
static void show_frame(struct video_desc *env, int out)	{}
static void sdl_setup(struct video_desc *env)		{}
static struct gui_info *cleanup_sdl(struct gui_info *gui)	{ return NULL; }
static void eventhandler(struct video_desc *env, const char *caption)	{}
static int keypad_cfg_read(struct gui_info *gui, const char *val)	{ return 0; }

#else /* HAVE_SDL, the real rendering code */

#include <SDL/SDL.h>
#include <SDL/SDL_syswm.h>
#ifdef HAVE_SDL_IMAGE
#include <SDL/SDL_image.h>      /* for loading images */
#endif

#ifdef HAVE_X11
/* Need to hook into X for SDL_WINDOWID handling */
#include <X11/Xlib.h>
#endif

enum kp_type { KP_NONE, KP_RECT, KP_CIRCLE };
struct keypad_entry {
        int c;  /* corresponding character */
        int x0, y0, x1, y1, h;  /* arguments */
        enum kp_type type;
};

/* our representation of a displayed window. SDL can only do one main
 * window so we map everything within that one
 */
struct display_window   {   
	SDL_Overlay	*bmp;
	SDL_Rect	rect;	/* location of the window */
};

struct gui_info {
	enum kb_output		kb_output;	/* where the keyboard output goes */
	struct drag_info	drag;		/* info on the window are we dragging */
	/* support for display. */
	SDL_Surface             *screen;	/* the main window */

	int			outfd;		/* fd for output */
	SDL_Surface		*keypad;	/* the skin for the keypad */
	SDL_Rect		kp_rect;	/* portion of the skin to display - default all */
	SDL_Surface		*font;		/* font to be used */ 
	SDL_Rect		font_rects[96];	/* only printable chars */

	/* each board has two rectangles,
	 * [0] is the geometry relative to the keypad,
	 * [1] is the geometry relative to the whole screen
	 */
	SDL_Rect		kp_msg[2];		/* incoming msg, relative to kpad */
	struct board		*bd_msg;

	SDL_Rect		kp_edit[2];	/* edit user input */
	struct board		*bd_edit;

	SDL_Rect		kp_dialed[2];	/* dialed number */
	struct board		*bd_dialed;

	/* variable-size array mapping keypad regions to functions */
	int kp_size, kp_used;
	struct keypad_entry *kp;

	struct display_window   win[WIN_MAX];
};

/*! \brief free the resources in struct gui_info and the descriptor itself.
 *  Return NULL so we can assign the value back to the descriptor in case.
 */
static struct gui_info *cleanup_sdl(struct gui_info *gui)
{
	int i;

	if (gui == NULL)
		return NULL;

	/* unload font file */ 
	if (gui->font) {
		SDL_FreeSurface(gui->font);
		gui->font = NULL; 
	}

	if (gui->outfd > -1)
		close(gui->outfd);
	if (gui->keypad)
		SDL_FreeSurface(gui->keypad);
	gui->keypad = NULL;
	if (gui->kp)
		ast_free(gui->kp);

	/* uninitialize the SDL environment */
	for (i = 0; i < WIN_MAX; i++) {
		if (gui->win[i].bmp)
			SDL_FreeYUVOverlay(gui->win[i].bmp);
	}
	bzero(gui, sizeof(gui));
	ast_free(gui);
	SDL_Quit();
	return NULL;
}

/*
 * Display video frames (from local or remote stream) using the SDL library.
 * - Set the video mode to use the resolution specified by the codec context
 * - Create a YUV Overlay to copy the frame into it;
 * - After the frame is copied into the overlay, display it
 *
 * The size is taken from the configuration.
 *
 * 'out' is 0 for remote video, 1 for the local video
 */
static void show_frame(struct video_desc *env, int out)
{
	AVPicture *p_in, p_out;
	struct fbuf_t *b_in, *b_out;
	SDL_Overlay *bmp;
	struct gui_info *gui = env->gui;

	if (!gui)
		return;

	if (out == WIN_LOCAL) {	/* webcam/x11 to sdl */
		b_in = &env->enc_in;
		b_out = &env->loc_dpy;
		p_in = NULL;
	} else {
		/* copy input format from the decoding context */
		AVCodecContext *c;
		if (env->in == NULL)	/* XXX should not happen - decoder not ready */
			return;
		c = env->in->dec_ctx;
		b_in = &env->in->dec_out;
                b_in->pix_fmt = c->pix_fmt;
                b_in->w = c->width;
                b_in->h = c->height;

		b_out = &env->rem_dpy;
		p_in = (AVPicture *)env->in->d_frame;
	}
	bmp = gui->win[out].bmp;
	SDL_LockYUVOverlay(bmp);
	/* output picture info - this is sdl, YUV420P */
	bzero(&p_out, sizeof(p_out));
	p_out.data[0] = bmp->pixels[0];
	p_out.data[1] = bmp->pixels[1];
	p_out.data[2] = bmp->pixels[2];
	p_out.linesize[0] = bmp->pitches[0];
	p_out.linesize[1] = bmp->pitches[1];
	p_out.linesize[2] = bmp->pitches[2];

	my_scale(b_in, p_in, b_out, &p_out);

	/* lock to protect access to Xlib by different threads. */
	SDL_DisplayYUVOverlay(bmp, &gui->win[out].rect);
	SDL_UnlockYUVOverlay(bmp);
}

/*
 * Identifiers for regions of the main window.
 * Values between 0 and 127 correspond to ASCII characters.
 * The corresponding strings to be used in the skin comment section
 * are defined in gui_key_map.
 */
enum skin_area {
	/* answer/close functions */
	KEY_PICK_UP = 128,
	KEY_HANG_UP = 129,

	KEY_MUTE = 130,
	KEY_AUTOANSWER = 131,
	KEY_SENDVIDEO = 132,
	KEY_LOCALVIDEO = 133,
	KEY_REMOTEVIDEO = 134,
	KEY_FLASH = 136,

	/* sensitive areas for the various text windows */
	KEY_MESSAGEBOARD = 140,
	KEY_DIALEDBOARD = 141,
	KEY_EDITBOARD = 142,

	KEY_GUI_CLOSE = 199,		/* close gui */
	/* regions of the skin - displayed area, fonts, etc.
	 * XXX NOTE these are not sensitive areas.
	 */
	KEY_KEYPAD = 200,		/* the keypad - default to the whole image */
	KEY_FONT = 201,		/* the font. Maybe not really useful */
	KEY_MESSAGE = 202,	/* area for incoming messages */
	KEY_DIALED = 203,	/* area for dialed numbers */
	KEY_EDIT = 204,		/* area for editing user input */

	/* areas outside the keypad - simulated */
	KEY_OUT_OF_KEYPAD = 241,
	KEY_REM_DPY = 242,
	KEY_LOC_DPY = 243,
	KEY_RESET = 253,		/* the 'reset' keyword */
	KEY_NONE = 254,			/* invalid area */
	KEY_DIGIT_BACKGROUND = 255,	/* other areas within the keypad */
};

/*
 * Handlers for the various keypad functions
 */

/* accumulate digits, possibly call dial if in connected mode */
static void keypad_digit(struct video_desc *env, int digit)
{	
	if (env->owner) {		/* we have a call, send the digit */
		struct ast_frame f = { AST_FRAME_DTMF, 0 };

		f.subclass = digit;
		ast_queue_frame(env->owner, &f);
	} else {		/* no call, accumulate digits */
		char buf[2] = { digit, '\0' };
		if (env->gui->bd_msg) /* XXX not strictly necessary ... */
			print_message(env->gui->bd_msg, buf);
	}
}

/* function used to toggle on/off the status of some variables */
static char *keypad_toggle(struct video_desc *env, int index)
{
	ast_log(LOG_WARNING, "keypad_toggle(%i) called\n", index);

	switch (index) {
	case KEY_SENDVIDEO:
		env->out.sendvideo = !env->out.sendvideo;
		break;
#ifdef notyet
	case KEY_MUTE: {
		struct chan_oss_pvt *o = find_desc(oss_active);
		o->mute = !o->mute;
		}
		break;
	case KEY_AUTOANSWER: {
		struct chan_oss_pvt *o = find_desc(oss_active);
		o->autoanswer = !o->autoanswer;
		}
		break;
#endif
	}
	return NULL;
}

char *console_do_answer(int fd);
/*
 * Function called when the pick up button is pressed
 * perform actions according the channel status:
 *
 *  - if no one is calling us and no digits was pressed,
 *    the operation have no effects,
 *  - if someone is calling us we answer to the call.
 *  - if we have no call in progress and we pressed some
 *    digit, send the digit to the console.
 */
static void keypad_pick_up(struct video_desc *env)
{
	struct gui_info *gui = env->gui;

	ast_log(LOG_WARNING, "keypad_pick_up called\n");

	if (env->owner) { /* someone is calling us, just answer */
		ast_cli_command(gui->outfd, "console answer");
	} else { /* we have someone to call */
		char buf[160];
		const char *who = ast_skip_blanks(read_message(gui->bd_msg));
		buf[sizeof(buf) - 1] = '\0';
		snprintf(buf, sizeof(buf), "console dial %s", who);
		ast_log(LOG_WARNING, "doing <%s>\n", buf);
		print_message(gui->bd_dialed, "\n");
		print_message(gui->bd_dialed, who);
		reset_board(gui->bd_msg);
		ast_cli_command(gui->outfd, buf);
	}
}

#if 0 /* still unused */
/*
 * As an alternative to SDL_TTF, we can simply load the font from
 * an image and blit characters on the background of the GUI.
 *
 * To generate a font we can use the 'fly' command with the
 * following script (3 lines with 32 chars each)
 
size 320,64
name font.png
transparent 0,0,0
string 255,255,255,  0, 0,giant, !"#$%&'()*+,-./0123456789:;<=>?
string 255,255,255,  0,20,giant,@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_
string 255,255,255,  0,40,giant,`abcdefghijklmnopqrstuvwxyz{|}~
end

 */

/* Print given text on the gui */
static int gui_output(struct video_desc *env, const char *text)
{
	return 1;	/* error, not supported */
}
#endif 

static int video_geom(struct fbuf_t *b, const char *s);
static void sdl_setup(struct video_desc *env);
static int kp_match_area(const struct keypad_entry *e, int x, int y);

static void set_drag(struct drag_info *drag, int x, int y, enum drag_window win)
{
	drag->x_start = x;
	drag->y_start = y;
	drag->drag_window = win;
}

/*
 * Handle SDL_MOUSEBUTTONDOWN type, finding the palette
 * index value and calling the right callback.
 *
 * x, y are referred to the upper left corner of the main SDL window.
 */
static void handle_mousedown(struct video_desc *env, SDL_MouseButtonEvent button)
{
	uint8_t index = KEY_OUT_OF_KEYPAD;	/* the key or region of the display we clicked on */
	struct gui_info *gui = env->gui;

#if 0
	ast_log(LOG_WARNING, "event %d %d have %d/%d regions at %p\n",
		button.x, button.y, gui->kp_used, gui->kp_size, gui->kp);
#endif
	/* for each mousedown we end previous drag */
	gui->drag.drag_window = DRAG_NONE;

	/* define keypad boundary */
	if (button.x < env->rem_dpy.w)
		index = KEY_REM_DPY; /* click on remote video */
	else if (button.x > env->rem_dpy.w + gui->keypad->w)
		index = KEY_LOC_DPY; /* click on local video */
	else if (button.y > gui->keypad->h)
		index = KEY_OUT_OF_KEYPAD; /* click outside the keypad */
	else if (gui->kp) {
		int i;
		for (i = 0; i < gui->kp_used; i++) {
			if (kp_match_area(&gui->kp[i], button.x - env->rem_dpy.w, button.y)) {
				index = gui->kp[i].c;
				break;
			}
		}
	}

	/* exec the function */
	if (index < 128) {	/* surely clicked on the keypad, don't care which key */
		keypad_digit(env, index);
		return;
	}
	switch (index) {
	/* answer/close function */
	case KEY_PICK_UP:
		keypad_pick_up(env);
		break;
	case KEY_HANG_UP:
		ast_cli_command(gui->outfd, "console hangup");
		break;

	/* other functions */
	case KEY_MUTE:
	case KEY_AUTOANSWER:
	case KEY_SENDVIDEO:
		keypad_toggle(env, index);
		break;

	case KEY_LOCALVIDEO:
		break;
	case KEY_REMOTEVIDEO:
		break;

	case KEY_MESSAGEBOARD:
		if (button.button == SDL_BUTTON_LEFT)
			set_drag(&gui->drag, button.x, button.y, DRAG_MESSAGE);
		break;

	/* press outside the keypad. right increases size, center decreases, left drags */
	case KEY_LOC_DPY:
	case KEY_REM_DPY:
		if (button.button == SDL_BUTTON_LEFT) {
			if (index == KEY_LOC_DPY)
				set_drag(&gui->drag, button.x, button.y, DRAG_LOCAL);
			break;
		} else {
			char buf[128];
			struct fbuf_t *fb = index == KEY_LOC_DPY ? &env->loc_dpy : &env->rem_dpy;
			sprintf(buf, "%c%dx%d", button.button == SDL_BUTTON_RIGHT ? '>' : '<',
				fb->w, fb->h);
			video_geom(fb, buf);
			sdl_setup(env);
		}
		break;
	case KEY_OUT_OF_KEYPAD:
		break;

	case KEY_DIGIT_BACKGROUND:
		break;
	default:
		ast_log(LOG_WARNING, "function not yet defined %i\n", index);
	}
}

/*
 * Handle SDL_KEYDOWN type event, put the key pressed
 * in the dial buffer or in the text-message buffer,
 * depending on the text_mode variable value.
 *
 * key is the SDLKey structure corresponding to the key pressed.
 * Note that SDL returns modifiers (ctrl, shift, alt) as independent
 * information so the key itself is not enough and we need to
 * use a translation table, below - one line per entry,
 * plain, shift, ctrl, ... using the first char as key.
 */
static const char *us_kbd_map[] = {
	"`~", "1!", "2@", "3#", "4$", "5%", "6^",
	"7&", "8*", "9(", "0)", "-_", "=+", "[{",
	"]}", "\\|", ";:", "'\"", ",<", ".>", "/?",
	"jJ\n",
	NULL
};

static char map_key(SDL_keysym *ks)
{
	const char *s, **p = us_kbd_map;
	int c = ks->sym;

	if (c == '\r')	/* map cr into lf */
		c = '\n';
	if (c >= SDLK_NUMLOCK && c <= SDLK_COMPOSE)
		return 0;	/* only a modifier */
	if (ks->mod == 0)
		return c;
	while ((s = *p) && s[0] != c)
		p++;
	if (s) { /* see if we have a modifier and a chance to use it */
		int l = strlen(s), mod = 0;
		if (l > 1)
			mod |= (ks->mod & KMOD_SHIFT) ? 1 : 0;
		if (l > 2 + mod)
			mod |= (ks->mod & KMOD_CTRL) ? 2 : 0;
		if (l > 4 + mod)
			mod |= (ks->mod & KMOD_ALT) ? 4 : 0;
		c = s[mod];
	}
	if (ks->mod & (KMOD_CAPS|KMOD_SHIFT) && c >= 'a' && c <='z')
		c += 'A' - 'a';
	return c;
}

static void handle_keyboard_input(struct video_desc *env, SDL_keysym *ks)
{
	char buf[2] = { map_key(ks), '\0' };
	struct gui_info *gui = env->gui;
	if (buf[0] == 0)	/* modifier ? */
		return;
	switch (gui->kb_output) {
	default:
		break;
	case KO_INPUT:	/* to be completed */
		break;
	case KO_MESSAGE:
		if (gui->bd_msg) {
			print_message(gui->bd_msg, buf);
			if (buf[0] == '\r' || buf[0] == '\n') {
				keypad_pick_up(env);
			}
		}
		break;

	case KO_DIALED: /* to be completed */
		break;
	}

	return;
}

static void grabber_move(struct video_out_desc *, int dx, int dy);

int compute_drag(int *start, int end, int magnifier);
int compute_drag(int *start, int end, int magnifier)
{
	int delta = end - *start;
#define POLARITY -1
	/* add a small quadratic term */
	delta += delta * delta * (delta > 0 ? 1 : -1 )/100;
	delta *= POLARITY * magnifier;
#undef POLARITY
	*start = end;
	return delta;
}

/*
 * I am seeing some kind of deadlock or stall around
 * SDL_PumpEvents() while moving the window on a remote X server
 * (both xfree-4.4.0 and xorg 7.2)
 * and windowmaker. It is unclear what causes it.
 */

/*! \brief refresh the screen, and also grab a bunch of events.
 */
static void eventhandler(struct video_desc *env, const char *caption)
{
	struct gui_info *gui = env->gui;
	struct drag_info *drag;
#define N_EVENTS	32
	int i, n;
	SDL_Event ev[N_EVENTS];

	if (!gui)
		return;
	drag = &gui->drag;
	if (caption)
		SDL_WM_SetCaption(caption, NULL);

#define MY_EV (SDL_MOUSEBUTTONDOWN|SDL_KEYDOWN)
	while ( (n = SDL_PeepEvents(ev, N_EVENTS, SDL_GETEVENT, SDL_ALLEVENTS)) > 0) {
		for (i = 0; i < n; i++) {
#if 0
			ast_log(LOG_WARNING, "------ event %d at %d %d\n",
				ev[i].type,  ev[i].button.x,  ev[i].button.y);
#endif
			switch (ev[i].type) {
			default:
				ast_log(LOG_WARNING, "------ event %d at %d %d\n",
					ev[i].type,  ev[i].button.x,  ev[i].button.y);
				break;

			case SDL_ACTIVEEVENT:
				ast_log(LOG_WARNING, "------ active gain %d state 0x%x\n",
					ev[i].active.gain,  ev[i].active.state);
				if (ev[i].active.gain == 0 && ev[i].active.state & SDL_APPACTIVE) {
					ast_log(LOG_WARNING, "/* somebody has killed us ? */");
					ast_cli_command(gui->outfd, "stop now");
				}
				break;

			case SDL_KEYUP:	/* ignore, for the time being */
				break;

			case SDL_KEYDOWN:
				handle_keyboard_input(env, &ev[i].key.keysym);
				break;

			case SDL_MOUSEMOTION:
			case SDL_MOUSEBUTTONUP:
				if (drag->drag_window == DRAG_LOCAL) {
					/* move the capture source */
					int dx = compute_drag(&drag->x_start, ev[i].motion.x, 3);
					int dy = compute_drag(&drag->y_start, ev[i].motion.y, 3);
					grabber_move(&env->out, dx, dy);
				} else if (drag->drag_window == DRAG_MESSAGE) {
					/* scroll up/down the window */
					int dy = compute_drag(&drag->y_start, ev[i].motion.y, 1);
					move_message_board(gui->bd_msg, dy);
				}
				if (ev[i].type == SDL_MOUSEBUTTONUP)
					drag->drag_window = DRAG_NONE;
				break;
			case SDL_MOUSEBUTTONDOWN:
				handle_mousedown(env, ev[i].button);
				break;
			}
		}
	}
	if (1) {
		struct timeval b, a = ast_tvnow();
		int i;
		//SDL_Lock_EventThread();
		SDL_PumpEvents();
		b = ast_tvnow();
		i = ast_tvdiff_ms(b, a);
		if (i > 3)
			fprintf(stderr, "-------- SDL_PumpEvents took %dms\n", i);
		//SDL_Unlock_EventThread();
	}
}

static SDL_Surface *load_image(const char *file)
{
	SDL_Surface *temp;
 
#ifdef HAVE_SDL_IMAGE
	temp = IMG_Load(file);
#else
	temp = SDL_LoadBMP(file);
#endif
	if (temp == NULL)
		fprintf(stderr, "Unable to load image %s: %s\n",
			file, SDL_GetError());
	return temp;
}

static void keypad_setup(struct gui_info *gui, const char *kp_file);

/* TODO: consistency checks, check for bpp, widht and height */
/* Init the mask image used to grab the action. */
static struct gui_info *gui_init(const char *keypad_file, const char *font)
{
	struct gui_info *gui = ast_calloc(1, sizeof(*gui));

	if (gui == NULL)
		return NULL;
	/* initialize keypad status */
	gui->kb_output = KO_MESSAGE;	/* XXX temp */
	gui->drag.drag_window = DRAG_NONE;
	gui->outfd = -1;

	keypad_setup(gui, keypad_file);
	if (gui->keypad == NULL)	/* no keypad, we are done */
		return gui;
	/* XXX load image */
	if (!ast_strlen_zero(font)) {
		int i;
		SDL_Rect *r;

		gui->font = load_image(font);
		if (!gui->font) {
			ast_log(LOG_WARNING, "Unable to load font %s, no output available\n", font);
			goto error;
		}
		ast_log(LOG_WARNING, "Loaded font %s\n", font);
		/* XXX hardwired constants - 3 rows of 32 chars */
		r = gui->font_rects;
#define FONT_H 20
#define FONT_W 9
		for (i = 0; i < 96; r++, i++) {
                	r->x = (i % 32 ) * FONT_W;
                	r->y = (i / 32 ) * FONT_H;
                	r->w = FONT_W;
                	r->h = FONT_H;
		}
	}

	gui->outfd = open ("/dev/null", O_WRONLY);	/* discard output, temporary */
	if (gui->outfd < 0) {
		ast_log(LOG_WARNING, "Unable output fd\n");
		goto error;
	}
	return gui;

error:
	ast_free(gui);
	return NULL;
}

/* setup an sdl overlay and associated info, return 0 on success, != 0 on error */
static int set_win(SDL_Surface *screen, struct display_window *win, int fmt,
	int w, int h, int x, int y)
{
	win->bmp = SDL_CreateYUVOverlay(w, h, fmt, screen);
	if (win->bmp == NULL)
		return -1;	/* error */
	win->rect.x = x;
	win->rect.y = y;
	win->rect.w = w;
	win->rect.h = h;
	return 0;
}

static int keypad_cfg_read(struct gui_info *gui, const char *val);

static void keypad_setup(struct gui_info *gui, const char *kp_file)
{
	FILE *fd;
	char buf[1024];
	const char region[] = "region";
	int reg_len = strlen(region);
	int in_comment = 0;

	if (gui->keypad)
		return;
	gui->keypad = load_image(kp_file);
	if (!gui->keypad)
		return;
	/* now try to read the keymap from the file. */
	fd = fopen(kp_file, "r");
	if (fd == NULL) {
		ast_log(LOG_WARNING, "fail to open %s\n", kp_file);
		return;
	}
	/*
	 * If the keypad image has a comment field, try to read
	 * the button location from there. The block must start with
	 * a comment (or empty) line, and continue with entries like:
	 *	region = token shape x0 y0 x1 y1 h
	 *	...
	 * (basically, lines have the same format as config file entries).
	 * You can add it to a jpeg file using wrjpgcom
	 */
	while (fgets(buf, sizeof(buf), fd)) {
		char *s;

		if (!strstr(buf, region)) { /* no keyword yet */
			if (!in_comment)	/* still waiting for initial comment block */
				continue;
			else
				break;
		}
		if (!in_comment) {	/* first keyword, reset previous entries */
			keypad_cfg_read(gui, "reset");
			in_comment = 1;
		}
		s = ast_skip_blanks(buf);
		ast_trim_blanks(s);
		if (memcmp(s, region, reg_len))
			break;	/* keyword not found */
		s = ast_skip_blanks(s + reg_len); /* space between token and '=' */
		if (*s++ != '=')	/* missing separator */
			break;
		if (*s == '>')	/* skip '>' if present */
			s++;
		keypad_cfg_read(gui, ast_skip_blanks(s));
	}
	fclose(fd);
}

struct board *board_setup(SDL_Surface *screen, SDL_Rect *dest,
	SDL_Surface *font, SDL_Rect *font_rects);

/*! \brief initialize the boards we have in the keypad */
static void init_board(struct gui_info *gui, struct board **dst, SDL_Rect *r, int dx, int dy)
{
	if (r[0].w == 0 || r[0].h == 0)
		return;	/* not available */
	r[1] = r[0];	/* copy geometry */
	r[1].x += dx;	/* add offset of main window */
	r[1].y += dy;
	if (*dst == NULL) {	/* initial call */
		*dst = board_setup(gui->screen, &r[1], gui->font, gui->font_rects);
	} else {
		/* call a refresh */
	}
}

/*! \brief [re]set the main sdl window, useful in case of resize.
 * We can tell the first from subsequent calls from the value of
 * env->gui, which is NULL the first time.
 */
static void sdl_setup(struct video_desc *env)
{
	int dpy_fmt = SDL_IYUV_OVERLAY;	/* YV12 causes flicker in SDL */
	int depth, maxw, maxh;
	const SDL_VideoInfo *info;
	int kp_w = 0, kp_h = 0;	/* keypad width and height */
	struct gui_info *gui = env->gui;

	/*
	 * initialize the SDL environment. We have one large window
	 * with local and remote video, and a keypad.
	 * At the moment we arrange them statically, as follows:
	 * - on the left, the remote video;
	 * - on the center, the keypad
	 * - on the right, the local video
	 * We need to read in the skin for the keypad before creating the main
	 * SDL window, because the size is only known here.
	 */

	if (gui == NULL && SDL_Init(SDL_INIT_VIDEO)) {
 		ast_log(LOG_WARNING, "Could not initialize SDL - %s\n",
                        SDL_GetError());
                /* again not fatal, just we won't display anything */
		return;
	}
	info = SDL_GetVideoInfo();
	/* We want at least 16bpp to support YUV overlays.
	 * E.g with SDL_VIDEODRIVER = aalib the default is 8
	 */
	if (!info || !info->vfmt) {
 		ast_log(LOG_WARNING, "Bad SDL_GetVideoInfo - %s\n",
                        SDL_GetError());
		return;
	}
	depth = info->vfmt->BitsPerPixel;
	if (depth < 16)
		depth = 16;
	if (!gui)
		env->gui = gui = gui_init(env->keypad_file, env->keypad_font);
	if (!gui)
		goto no_sdl;

	if (gui->keypad) {
		if (gui->kp_rect.w > 0 && gui->kp_rect.h > 0) {
			kp_w = gui->kp_rect.w;
			kp_h = gui->kp_rect.h;
		} else {
			kp_w = gui->keypad->w;
			kp_h = gui->keypad->h;
		}
	}
	/* XXX same for other boards */
#define BORDER	5	/* border around our windows */
	maxw = env->rem_dpy.w + env->loc_dpy.w + kp_w;
	maxh = MAX( MAX(env->rem_dpy.h, env->loc_dpy.h), kp_h);
	maxw += 4 * BORDER;
	maxh += 2 * BORDER;
	/* XXX warning, here it might crash if SDL_WINDOWID is set badly */
	gui->screen = SDL_SetVideoMode(maxw, maxh, depth, 0);
	if (!gui->screen) {
		ast_log(LOG_ERROR, "SDL: could not set video mode - exiting\n");
		goto no_sdl;
	}

#ifdef HAVE_X11
	/*
	 * Annoying as it may be, if SDL_WINDOWID is set, SDL does
	 * not grab keyboard/mouse events or expose or other stuff,
	 * and it does not handle resize either.
	 * So we need to implement workarounds here.
	 */
    do {
	/* First, handle the event mask */
	XWindowAttributes attr;
        long want;
        SDL_SysWMinfo info;
	Display *SDL_Display;
        Window win;

	const char *e = getenv("SDL_WINDOWID");
	if (ast_strlen_zero(e))	 /* no external window, don't bother doing this */
		break;
        SDL_VERSION(&info.version); /* it is important to set the version */
        if (SDL_GetWMInfo(&info) != 1) {
                fprintf(stderr, "no wm info\n");
                break;
        }
	SDL_Display = info.info.x11.display;
	if (SDL_Display == NULL)
		break;
        win = info.info.x11.window;

	/*
	 * A list of events we want.
	 * Leave ResizeRedirectMask to the parent.
	 */
        want = KeyPressMask | KeyReleaseMask | ButtonPressMask |
                           ButtonReleaseMask | EnterWindowMask |
                           LeaveWindowMask | PointerMotionMask |
                           Button1MotionMask |
                           Button2MotionMask | Button3MotionMask |
                           Button4MotionMask | Button5MotionMask |
                           ButtonMotionMask | KeymapStateMask |
                           ExposureMask | VisibilityChangeMask |
                           StructureNotifyMask | /* ResizeRedirectMask | */
                           SubstructureNotifyMask | SubstructureRedirectMask |
                           FocusChangeMask | PropertyChangeMask |
                           ColormapChangeMask | OwnerGrabButtonMask;

        bzero(&attr, sizeof(attr));
	XGetWindowAttributes(SDL_Display, win, &attr);

	/* the following events can be delivered only to one client.
	 * So check which ones are going to someone else, and drop
	 * them from our request.
	 */
	{
	/* ev are the events for a single recipient */
	long ev = ButtonPressMask | ResizeRedirectMask |
			SubstructureRedirectMask;
        ev &= (attr.all_event_masks & ~attr.your_event_mask);
	/* now ev contains 1 for single-recipient events owned by others.
	 * We must clear those bits in 'want'
	 * and then add the bits in 'attr.your_event_mask' to 'want'
	 */
	want &= ~ev;
	want |= attr.your_event_mask;
	}
	XSelectInput(SDL_Display, win, want);

	/* Second, handle resize.
	 * We do part of the things that X11Resize does,
	 * but also generate a ConfigureNotify event so
	 * the owner of the window has a chance to do something
	 * with it.
 	 */
	XResizeWindow(SDL_Display, win, maxw, maxh);
	{
	XConfigureEvent ce = {
		.type = ConfigureNotify,
		.serial = 0,
		.send_event = 1,	/* TRUE */
		.display = SDL_Display,
		.event = win,
		.window = win,
		.x = 0,
		.y = 0,
		.width = maxw,
		.height = maxh,
		.border_width = 0,
		.above = 0,
		.override_redirect = 0 };
	XSendEvent(SDL_Display, win, 1 /* TRUE */, StructureNotifyMask, (XEvent *)&ce);
	}
    } while (0);
#endif /* HAVE_X11 */
	SDL_WM_SetCaption("Asterisk console Video Output", NULL);
	if (set_win(gui->screen, &gui->win[WIN_REMOTE], dpy_fmt,
			env->rem_dpy.w, env->rem_dpy.h, BORDER, BORDER))
		goto no_sdl;
	if (set_win(gui->screen, &gui->win[WIN_LOCAL], dpy_fmt,
			env->loc_dpy.w, env->loc_dpy.h,
			3*BORDER+env->rem_dpy.w + kp_w, BORDER))
		goto no_sdl;

	/* display the skin, but do not free it as we need it later to
	 * restore text areas and maybe sliders too.
	 */
	if (gui->keypad) {
		struct SDL_Rect *dest = &gui->win[WIN_KEYPAD].rect;
		struct SDL_Rect *src = (gui->kp_rect.w > 0 && gui->kp_rect.h > 0) ? & gui->kp_rect : NULL;
		/* set the coordinates of the keypad relative to the main screen */
		dest->x = 2*BORDER + env->rem_dpy.w;
		dest->y = BORDER;
		dest->w = kp_w;
		dest->h = kp_h;
		SDL_BlitSurface(gui->keypad, src, gui->screen, dest);
		init_board(gui, &gui->bd_msg, gui->kp_msg, dest->x, dest->y);
		init_board(gui, &gui->bd_dialed, gui->kp_dialed, dest->x, dest->y);
		SDL_UpdateRects(gui->screen, 1, dest);
	}
	return;

no_sdl:
	/* free resources in case of errors */
	env->gui = cleanup_sdl(gui);
}

/*
 * Functions to determine if a point is within a region. Return 1 if success.
 * First rotate the point, with
 *	x' =  (x - x0) * cos A + (y - y0) * sin A
 *	y' = -(x - x0) * sin A + (y - y0) * cos A
 * where cos A = (x1-x0)/l, sin A = (y1 - y0)/l, and
 *	l = sqrt( (x1-x0)^2 + (y1-y0)^2
 * Then determine inclusion by simple comparisons i.e.:
 *	rectangle: x >= 0 && x < l && y >= 0 && y < h
 *	ellipse: (x-xc)^2/l^2 + (y-yc)^2/h2 < 1
 */
static int kp_match_area(const struct keypad_entry *e, int x, int y)
{
	double xp, dx = (e->x1 - e->x0);
	double yp, dy = (e->y1 - e->y0);
	double l = sqrt(dx*dx + dy*dy);
	int ret = 0;

	if (l > 1) { /* large enough */
		xp = ((x - e->x0)*dx + (y - e->y0)*dy)/l;
		yp = (-(x - e->x0)*dy + (y - e->y0)*dx)/l;
		if (e->type == KP_RECT) {
			ret = (xp >= 0 && xp < l && yp >=0 && yp < l);
		} else if (e->type == KP_CIRCLE) {
			dx = xp*xp/(l*l) + yp*yp/(e->h*e->h);
			ret = (dx < 1);
		}
	}
#if 0
	ast_log(LOG_WARNING, "result %d [%d] for match %d,%d in type %d p0 %d,%d p1 %d,%d h %d\n",
		ret, e->c, x, y, e->type, e->x0, e->y0, e->x1, e->y1, e->h);
#endif
	return ret;
}

struct _s_k { const char *s; int k; };
static struct _s_k gui_key_map[] = {
	{"PICK_UP",	KEY_PICK_UP },
	{"PICKUP",	KEY_PICK_UP },
        {"HANG_UP",	KEY_HANG_UP },
        {"HANGUP",	KEY_HANG_UP },
        {"MUTE",	KEY_MUTE },
        {"FLASH",	KEY_FLASH },
        {"AUTOANSWER",	KEY_AUTOANSWER },
        {"SENDVIDEO",	KEY_SENDVIDEO },
        {"LOCALVIDEO",	KEY_LOCALVIDEO },
        {"REMOTEVIDEO",	KEY_REMOTEVIDEO },
        {"GUI_CLOSE",	KEY_GUI_CLOSE },
        {"MESSAGEBOARD",	KEY_MESSAGEBOARD },
        {"DIALEDBOARD",	KEY_DIALEDBOARD },
        {"EDITBOARD",	KEY_EDITBOARD },
        {"KEYPAD",	KEY_KEYPAD },	/* x0 y0 w h - active area of the keypad */
        {"MESSAGE",	KEY_MESSAGE },	/* x0 y0 w h - incoming messages */
        {"DIALED",	KEY_DIALED },	/* x0 y0 w h - dialed number */
        {"EDIT",	KEY_EDIT },	/* x0 y0 w h - edit user input */
        {"FONT",	KEY_FONT },	/* x0 yo w h rows cols - location and format of the font */
        {NULL, 0 } };

static int gui_map_token(const char *s)
{
	/* map the string into token to be returned */
	int i = atoi(s);
	struct _s_k *p;
	if (i > 0 || s[1] == '\0')	/* numbers or single characters */
		return (i > 9) ? i : s[0];
	for (p = gui_key_map; p->s; p++) {
		if (!strcasecmp(p->s, s))
			return p->k;
	}
	return KEY_NONE;	/* not found */
}

/*! \brief read a keypad entry line in the format
 *	reset
 *	token circle xc yc diameter
 *	token circle xc yc x1 y1 h	# ellipse, main diameter and height
 *	token rect x0 y0 x1 y1 h	# rectangle with main side and eight
 * token is the token to be returned, either a character or a symbol
 * as KEY_* above
 * Return 1 on success, 0 on error.
 */
static int keypad_cfg_read(struct gui_info *gui, const char *val)
{
	struct keypad_entry e;
	SDL_Rect *r = NULL;
	char s1[16], s2[16];
	int i, ret = 0; /* default, error */

	if (gui == NULL || val == NULL)
		return 0;

	s1[0] = s2[0] = '\0';
	bzero(&e, sizeof(e));
	i = sscanf(val, "%14s %14s %d %d %d %d %d",
                s1, s2, &e.x0, &e.y0, &e.x1, &e.y1, &e.h);

	e.c = gui_map_token(s1);
	if (e.c == KEY_NONE)
		return 0;	/* nothing found */
	switch (i) {
	default:
		break;
	case 1:	/* only "reset" is allowed */
		if (e.c != KEY_RESET)
			break;
		if (gui->kp)
			gui->kp_used = 0;
		break;
	case 5:
		if (e.c == KEY_KEYPAD)	/* active keypad area */
			r = &gui->kp_rect;
		else if (e.c == KEY_MESSAGE)
			r = gui->kp_msg;
		else if (e.c == KEY_DIALED)
			r = gui->kp_dialed;
		else if (e.c == KEY_EDIT)
			r = gui->kp_edit;
		if (r) {
			r->x = atoi(s2);
			r->y = e.x0;
			r->w = e.y0;
			r->h = e.x1;
			break;
		}
		if (strcasecmp(s2, "circle"))	/* invalid */
			break;
		/* token circle xc yc diameter */
		e.h = e.x1;
		e.y1 = e.y0;	/* map radius in x1 y1 */
		e.x1 = e.x0 + e.h;	/* map radius in x1 y1 */
		e.x0 = e.x0 - e.h;	/* map radius in x1 y1 */
		/* fallthrough */

	case 7:
		if (e.c == KEY_FONT) {	/* font - x0 y0 w h rows cols */
			ast_log(LOG_WARNING, "font not supported yet\n");
			break;
		}
		/* token circle|rect x0 y0 x1 y1 h */
		if (e.x1 < e.x0 || e.h <= 0) {
			ast_log(LOG_WARNING, "error in coordinates\n");
			e.type = 0;
			break;
		}
		if (!strcasecmp(s2, "circle")) {
			/* for a circle we specify the diameter but store center and radii */
			e.type = KP_CIRCLE;
			e.x0 = (e.x1 + e.x0) / 2;
			e.y0 = (e.y1 + e.y0) / 2;
			e.h = e.h / 2;
		} else if (!strcasecmp(s2, "rect")) {
			e.type = KP_RECT;
		} else
			break;
		ret = 1;
	}
	// ast_log(LOG_WARNING, "reading [%s] returns %d %d\n", val, i, ret);
	if (ret == 0)
		return 0;
	if (gui->kp_size == 0) {
		gui->kp = ast_calloc(10, sizeof(e));
		if (gui->kp == NULL) {
			ast_log(LOG_WARNING, "cannot allocate kp");
			return 0;
		}
		gui->kp_size = 10;
	}
	if (gui->kp_size == gui->kp_used) { /* must allocate */
		struct keypad_entry *a = ast_realloc(gui->kp, sizeof(e)*(gui->kp_size+10));
		if (a == NULL) {
			ast_log(LOG_WARNING, "cannot reallocate kp");
			return 0;
		}
		gui->kp = a;
		gui->kp_size += 10;
	}
	if (gui->kp_size == gui->kp_used)
		return 0;
	gui->kp[gui->kp_used++] = e;
	// ast_log(LOG_WARNING, "now %d regions\n", gui->kp_used);
	return 1;
}
#endif	/* HAVE_SDL */
