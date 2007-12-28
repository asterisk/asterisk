/*
 * GUI for console video.
 * The routines here are in charge of loading the keypad and handling events.
 * $Revision$
 */


#include "asterisk.h"
#include "console_video.h"
#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/utils.h"	/* ast_calloc and ast_realloc */
#include <math.h>		/* sqrt */

/* we support 3 regions in the GUI */
enum { WIN_LOCAL, WIN_REMOTE, WIN_KEYPAD, WIN_MAX };

#ifndef HAVE_SDL
/* stubs if we don't have any sdl */
static void show_frame(struct video_desc *env, int out)	{}
static void sdl_setup(struct video_desc *env)		{}
static struct gui_info *cleanup_sdl(struct gui_info *gui)	{ return NULL; }
static void eventhandler(struct video_desc *env, const char *caption)	{}
static int keypad_cfg_read(struct gui_info *gui, const char *val)	{ return 0; }

#else /* HAVE_SDL, the real rendering code */

#include <SDL/SDL.h>
#ifdef HAVE_SDL_IMAGE
#include <SDL/SDL_image.h>      /* for loading images */
#endif
#ifdef HAVE_SDL_TTF
#include <SDL/SDL_ttf.h>        /* render text on sdl surfaces */
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
#define GUI_BUFFER_LEN 256			/* buffer lenght used for input buffers */

struct keypad_entry;	/* defined in console_gui.c */

/*! \brief info related to the gui: button status, mouse coords, etc. */
struct gui_info {
	char			inbuf[GUI_BUFFER_LEN];	/* buffer for to-dial buffer */
	int			inbuf_pos;	/* next free position in inbuf */
	char			msgbuf[GUI_BUFFER_LEN];	/* buffer for text-message buffer */
	int			msgbuf_pos;	/* next free position in msgbuf */
	int			text_mode;	/* switch to-dial and text-message mode */
	int			drag_mode;	/* switch phone and drag-source mode */
	int			x_drag;		/* x coordinate where the drag starts */
	int			y_drag;		/* y coordinate where the drag starts */
#ifdef HAVE_SDL_TTF
	TTF_Font                *font;          /* font to be used */ 
#endif
	/* support for display. */
	SDL_Surface             *screen;	/* the main window */

	int			outfd;		/* fd for output */
	SDL_Surface		*keypad;	/* the pixmap for the keypad */
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

#ifdef HAVE_SDL_TTF
	/* unload font file */ 
	if (gui->font) {
		TTF_CloseFont(gui->font);
		gui->font = NULL; 
	}

	/* uninitialize SDL_ttf library */
	if ( TTF_WasInit() )
		TTF_Quit();
#endif
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
 * GUI layout, structure and management
 *

For the GUI we use SDL to create a large surface (gui->screen)
containing tree sections: remote video on the left, local video
on the right, and the keypad with all controls and text windows
in the center.
The central section is built using two images: one is the skin,
the other one is a mask where the sensitive areas of the skin
are colored in different grayscale levels according to their
functions. The mapping between colors and function is defined
in the 'enum pixel_value' below.

Mouse and keyboard events are detected on the whole surface, and
handled differently according to their location, as follows:

- drag on the local video window are used to move the captured
  area (in the case of X11 grabber) or the picture-in-picture
  location (in case of camera included on the X11 grab).
- click on the keypad are mapped to the corresponding key;
- drag on some keypad areas (sliders etc.) are mapped to the
  corresponding functions;
- keystrokes are used as keypad functions, or as text input
  if we are in text-input mode.

To manage these behavior we use two status variables,
that defines if keyboard events should be redirect to dialing functions
or to write message functions, and if mouse events should be used
to implement keypad functionalities or to drag the capture device.

Configuration options control the appeareance of the gui:

    keypad = /tmp/phone.jpg		; the keypad on the screen
    keypad_font = /tmp/font.ttf		; the font to use for output

 *
 */

/* enumerate for the pixel value. 0..127 correspond to ascii chars */
enum pixel_value {
	/* answer/close functions */
	KEY_PICK_UP = 128,
	KEY_HANG_UP = 129,

	/* other functions */
	KEY_MUTE = 130,
	KEY_AUTOANSWER = 131,
	KEY_SENDVIDEO = 132,
	KEY_LOCALVIDEO = 133,
	KEY_REMOTEVIDEO = 134,
	KEY_WRITEMESSAGE = 135,
	KEY_GUI_CLOSE = 136,		/* close gui */

	/* other areas within the keypad */
	KEY_DIGIT_BACKGROUND = 255,

	/* areas outside the keypad - simulated */
	KEY_OUT_OF_KEYPAD = 251,
	KEY_REM_DPY = 252,
	KEY_LOC_DPY = 253,
};

/*
 * Handlers for the various keypad functions
 */

/*! \brief append a character, or reset if '\0' */
static void append_char(char *str, int *str_pos, const char c)
{
	int i = *str_pos;
	if (c == '\0')
		i = 0;
	else if (i < GUI_BUFFER_LEN - 1)
		str[i++] = c;
	else
		i = GUI_BUFFER_LEN - 1; /* unnecessary, i think */
	str = '\0';
	*str_pos = i;
}

/* accumulate digits, possibly call dial if in connected mode */
static void keypad_digit(struct video_desc *env, int digit)
{	
	if (env->owner) {		/* we have a call, send the digit */
		struct ast_frame f = { AST_FRAME_DTMF, 0 };

		f.subclass = digit;
		ast_queue_frame(env->owner, &f);
	} else {		/* no call, accumulate digits */
		append_char(env->gui->inbuf, &env->gui->inbuf_pos, digit);
	}
}

/* this is a wrapper for actions that are available through the cli */
/* TODO append arg to command and send the resulting string as cli command */
static void keypad_send_command(struct video_desc *env, char *command)
{	
	ast_log(LOG_WARNING, "keypad_send_command(%s) called\n", command);
	ast_cli_command(env->gui->outfd, command);
	return;
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
		console_do_answer(-1);
	} else if (gui->inbuf_pos) { /* we have someone to call */
		ast_cli_command(gui->outfd, gui->inbuf);
	}

	append_char(gui->inbuf, &gui->inbuf_pos, '\0'); /* clear buffer */
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
#ifndef HAVE_SDL_TTF
	return 1;	/* error, not supported */
#else
	int x = 30, y = 20;	/* XXX change */
	SDL_Surface *output = NULL;
	SDL_Color color = {0, 0, 0};	/* text color */
	struct gui_info *gui = env->gui;
	SDL_Rect dest = {gui->win[WIN_KEYPAD].rect.x + x, y};

	/* clean surface each rewrite */
	SDL_BlitSurface(gui->keypad, NULL, gui->screen, &gui->win[WIN_KEYPAD].rect);

	output = TTF_RenderText_Solid(gui->font, text, color);
	if (output == NULL) {
		ast_log(LOG_WARNING, "Cannot render text on gui - %s\n", TTF_GetError());
		return 1;
	}

	SDL_BlitSurface(output, NULL, gui->screen, &dest);
	
	SDL_UpdateRects(gui->keypad, 1, &gui->win[WIN_KEYPAD].rect);
	SDL_FreeSurface(output);
	return 0;	/* success */
#endif
}
#endif 

static int video_geom(struct fbuf_t *b, const char *s);
static void sdl_setup(struct video_desc *env);
static int kp_match_area(const struct keypad_entry *e, int x, int y);

/*
 * Handle SDL_MOUSEBUTTONDOWN type, finding the palette
 * index value and calling the right callback.
 *
 * x, y are referred to the upper left corner of the main SDL window.
 */
static void handle_button_event(struct video_desc *env, SDL_MouseButtonEvent button)
{
	uint8_t index = KEY_OUT_OF_KEYPAD;	/* the key or region of the display we clicked on */
	struct gui_info *gui = env->gui;

#if 0
	ast_log(LOG_WARNING, "event %d %d have %d/%d regions at %p\n",
		button.x, button.y, gui->kp_used, gui->kp_size, gui->kp);
#endif
	/* for each click we come back in normal mode */
	gui->text_mode = 0;

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
		keypad_send_command(env, "console hangup");
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
	case KEY_WRITEMESSAGE:
		/* goes in text-mode */
		env->gui->text_mode = 1;
		break;


	/* press outside the keypad. right increases size, center decreases, left drags */
	case KEY_LOC_DPY:
	case KEY_REM_DPY:
		if (button.button == SDL_BUTTON_LEFT) {
			if (index == KEY_LOC_DPY) {
				/* store points where the drag start
				* and switch in drag mode */
				env->gui->x_drag = button.x;
				env->gui->y_drag = button.y;
				env->gui->drag_mode = 1;
			}
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
 */
static void handle_keyboard_input(struct video_desc *env, SDLKey key)
{
	struct gui_info *gui = env->gui;
	if (gui->text_mode) {
		/* append in the text-message buffer */
		if (key == SDLK_RETURN) {
			/* send the text message and return in normal mode */
			gui->text_mode = 0;
			keypad_send_command(env, "send text");
		} else {
			/* accumulate the key in the message buffer */
			append_char(gui->msgbuf, &gui->msgbuf_pos, key);
		}
	}
	else {
		/* append in the dial buffer */
		append_char(gui->inbuf, &gui->inbuf_pos, key);
	}

	return;
}

/*
 * Check if the grab point is inside the X screen.
 *
 * x represent the new grab value
 * limit represent the upper value to use
 */
static int boundary_checks(int x, int limit)
{
	return (x <= 0) ? 0 : (x > limit ? limit : x);
}

/* implement superlinear acceleration on the movement */
static int move_accel(int delta)
{
	int d1 = delta*delta / 100;
	return (delta > 0) ? delta + d1 : delta - d1;
}

/*
 * Move the source of the captured video.
 *
 * x_final_drag and y_final_drag are the coordinates where the drag ends,
 * start coordinares are in the gui_info structure.
 */
static void move_capture_source(struct video_desc *env, int x_final_drag, int y_final_drag)
{
	int new_x, new_y;		/* new coordinates for grabbing local video */
	int x = env->out.loc_src.x;	/* old value */
	int y = env->out.loc_src.y;	/* old value */

	/* move the origin */
#define POLARITY -1		/* +1 or -1 depending on the desired direction */
	new_x = x + POLARITY*move_accel(x_final_drag - env->gui->x_drag) * 3;
	new_y = y + POLARITY*move_accel(y_final_drag - env->gui->y_drag) * 3;
#undef POLARITY
	env->gui->x_drag = x_final_drag;	/* update origin */
	env->gui->y_drag = y_final_drag;

	/* check boundary and let the source to grab from the new points */
	env->out.loc_src.x = boundary_checks(new_x, env->out.screen_width - env->out.loc_src.w);
	env->out.loc_src.y = boundary_checks(new_y, env->out.screen_height - env->out.loc_src.h);
	return;
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
#define N_EVENTS	32
	int i, n;
	SDL_Event ev[N_EVENTS];

	if (!gui)
		return;
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
			case SDL_KEYDOWN:
				handle_keyboard_input(env, ev[i].key.keysym.sym);
				break;
			case SDL_MOUSEMOTION:
				if (gui->drag_mode != 0)
					move_capture_source(env, ev[i].motion.x, ev[i].motion.y);
				break;
			case SDL_MOUSEBUTTONDOWN:
				handle_button_event(env, ev[i].button);
				break;
			case SDL_MOUSEBUTTONUP:
				if (gui->drag_mode != 0) {
					move_capture_source(env, ev[i].button.x, ev[i].button.y);
					gui->drag_mode = 0;
				}
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

static SDL_Surface *get_keypad(const char *file)
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
static struct gui_info *gui_init(const char *keypad_file)
{
	struct gui_info *gui = ast_calloc(1, sizeof(*gui));

	if (gui == NULL)
		return NULL;
	/* initialize keypad status */
	gui->text_mode = 0;
	gui->drag_mode = 0;
	gui->outfd = -1;

	/* initialize keyboard buffer */
	append_char(gui->inbuf, &gui->inbuf_pos, '\0');
	append_char(gui->msgbuf, &gui->msgbuf_pos, '\0');

	keypad_setup(gui, keypad_file);
	if (gui->keypad == NULL)	/* no keypad, we are done */
		return gui;
#ifdef HAVE_SDL_TTF
	/* Initialize SDL_ttf library and load font */
	if (TTF_Init() == -1) {
		ast_log(LOG_WARNING, "Unable to init SDL_ttf, no output available\n");
		goto error;
	}

#define GUI_FONTSIZE 28
	gui->font = TTF_OpenFont( env->keypad_font, GUI_FONTSIZE);
	if (!gui->font) {
		ast_log(LOG_WARNING, "Unable to load font %s, no output available\n", env->keypad_font);
		goto error;
	}
	ast_log(LOG_WARNING, "Loaded font %s\n", env->keypad_font);
#endif

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
	gui->keypad = get_keypad(kp_file);
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
	depth = info->vfmt->BitsPerPixel;
	if (depth < 16)
		depth = 16;
	if (!gui)
		env->gui = gui = gui_init(env->keypad_file);
	if (!gui)
		goto no_sdl;

	if (gui->keypad) {
		kp_w = gui->keypad->w;
		kp_h = gui->keypad->h;
	}
#define BORDER	5	/* border around our windows */
	maxw = env->rem_dpy.w + env->loc_dpy.w + kp_w;
	maxh = MAX( MAX(env->rem_dpy.h, env->loc_dpy.h), kp_h);
	maxw += 4 * BORDER;
	maxh += 2 * BORDER;
	gui->screen = SDL_SetVideoMode(maxw, maxh, depth, 0);
	if (!gui->screen) {
		ast_log(LOG_ERROR, "SDL: could not set video mode - exiting\n");
		goto no_sdl;
	}

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
		dest->x = 2*BORDER + env->rem_dpy.w;
		dest->y = BORDER;
		dest->w = gui->keypad->w;
		dest->h = gui->keypad->h;
		SDL_BlitSurface(gui->keypad, NULL, gui->screen, dest);
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
        {"AUTOANSWER",	KEY_AUTOANSWER },
        {"SENDVIDEO",	KEY_SENDVIDEO },
        {"LOCALVIDEO",	KEY_LOCALVIDEO },
        {"REMOTEVIDEO",	KEY_REMOTEVIDEO },
        {"WRITEMESSAGE", KEY_WRITEMESSAGE },
        {"GUI_CLOSE",	KEY_GUI_CLOSE },
        {NULL, 0 } };

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
	char s1[16], s2[16];
	int i, ret = 0;

	if (gui == NULL || val == NULL)
		return 0;

	bzero(&e, sizeof(e));
	i = sscanf(val, "%14s %14s %d %d %d %d %d",
                s1, s2, &e.x0, &e.y0, &e.x1, &e.y1, &e.h);

	switch (i) {
	default:
		break;
	case 1:	/* only "reset" is allowed */
		if (strcasecmp(s1, "reset"))	/* invalid */
			break;
		if (gui->kp) {
			gui->kp_used = 0;
		}
		break;
	case 5: /* token circle xc yc diameter */
		if (strcasecmp(s2, "circle"))	/* invalid */
			break;
		e.h = e.x1;
		e.y1 = e.y0;	/* map radius in x1 y1 */
		e.x1 = e.x0 + e.h;	/* map radius in x1 y1 */
		e.x0 = e.x0 - e.h;	/* map radius in x1 y1 */
		/* fallthrough */

	case 7: /* token circle|rect x0 y0 x1 y1 h */
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
	/* map the string into token to be returned */
	i = atoi(s1);
	if (i > 0 || s1[1] == '\0')	/* numbers or single characters */
		e.c = (i > 9) ? i : s1[0];
	else {
		struct _s_k *p;
		for (p = gui_key_map; p->s; p++) {
			if (!strcasecmp(p->s, s1)) {
				e.c = p->k;
				break;
			}
		}
	}
	if (e.c == 0) {
		ast_log(LOG_WARNING, "missing token\n");
		return 0;
	}
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
