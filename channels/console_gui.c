/*
 * GUI for console video.
 * The routines here are in charge of loading the keypad and handling events.
 * $Revision$
 */

/*
 * GUI layout, structure and management
 
For the GUI we use SDL to create a large surface (gui->screen) with 4 areas:
remote video on the left, local video on the right, keypad with all controls
and text windows in the center, and source device thumbnails on the top.
The top row is not displayed if no devices are specified in the config file.

     ________________________________________________________________
    |  ______   ______   ______   ______   ______   ______   ______  |
    | | tn.1 | | tn.2 | | tn.3 | | tn.4 | | tn.5 | | tn.6 | | tn.7 | |
    | |______| |______| |______| |______| |______| |______| |______| |
    |  ______   ______   ______   ______   ______   ______   ______  |
    | |______| |______| |______| |______| |______| |______| |______| |
    |  _________________    __________________    _________________  |
    | |                 |  |                  |  |                 | |
    | |                 |  |                  |  |                 | |
    | |                 |  |                  |  |                 | |
    | |   remote video  |  |                  |  |   local video   | |
    | |                 |  |                  |  |          ______ | |
    | |                 |  |      keypad      |  |         |  PIP || |
    | |                 |  |                  |  |         |______|| |
    | |_________________|  |                  |  |_________________| |
    |                      |                  |                      |
    |                      |                  |                      |
    |                      |__________________|                      |
    |________________________________________________________________|


The central section is built using an image (jpg, png, maybe gif too)
for the skin, and other GUI elements.  Comments embedded in the image
indicate to what function each area is mapped to.
Another image (png with transparency) is used for the font.

Mouse and keyboard events are detected on the whole surface, and
handled differently according to their location:
- center/right click on the local/remote window are used to resize
  the corresponding window;
- clicks on the thumbnail start/stop sources and select them as
  primary or secondary video sources;
- drag on the local video window are used to move the captured
  area (in the case of X11 grabber) or the picture-in-picture position;
- keystrokes on the keypad are mapped to the corresponding key;
  keystrokes are used as keypad functions, or as text input
  if we are in text-input mode.
- drag on some keypad areas (sliders etc.) are mapped to the
  corresponding functions (mute/unmute audio and video,
  enable/disable Picture-in-Picture, freeze the incoming video,
  dial numbers, pick up or hang up a call, ...)

Configuration options control the appeareance of the gui:

    keypad = /tmp/kpad2.jpg	; the skin
    keypad_font = /tmp/font.png	; the font to use for output

For future implementation, intresting features can be the following:
- save of the whole SDL window as a picture
- audio output device switching

The audio switching feature should allow changing the device
or switching to a recorded message for audio sent to remote party.
The selection of the device should happen clicking on a marker in the layout.
For this reason above the thumbnails row in the layout we would like a new row,
the elements composing the row could be message boards, reporting the name of the
device or the path of the message to be played.

For video input freeze and entire window capture, we define 2 new key types,
those should be activated pressing the buttons on the keypad, associated with
new regions inside the keypad pictureas comments


 *
 */

#include "asterisk.h"
#include "console_video.h"
#include "asterisk/lock.h"
#include "asterisk/frame.h"
#include "asterisk/utils.h"	/* ast_calloc and ast_realloc */
#include <math.h>		/* sqrt */

/* We use a maximum of 12 'windows' in the GUI */
enum { WIN_LOCAL, WIN_REMOTE, WIN_KEYPAD, WIN_SRC1,
	WIN_SRC2, WIN_SRC3, WIN_SRC4, WIN_SRC5,
	WIN_SRC6, WIN_SRC7, WIN_SRC8, WIN_SRC9, WIN_MAX };

#ifndef HAVE_SDL	/* stubs if we don't have any sdl */
static void show_frame(struct video_desc *env, int out)	{}
static void sdl_setup(struct video_desc *env)		{}
static struct gui_info *cleanup_sdl(struct gui_info* g, int n)	{ return NULL; }
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

#define BORDER	5		/* border around our windows */
#define SRC_MSG_BD_H 20		/* height of the message board below those windows */

enum kp_type { KP_NONE, KP_RECT, KP_CIRCLE };
struct keypad_entry {
        int c;  /* corresponding character */
        int x0, y0, x1, y1, h;  /* arguments */
        enum kp_type type;
};

/* our representation of a displayed window. SDL can only do one main
 * window so we map everything within that one
 */
struct display_window {
	SDL_Overlay	*bmp;
	SDL_Rect	rect;	/* location of the window */
};

/* each thumbnail message board has a rectangle associated for the geometry,
 * and a board structure, we include these two elements in a singole structure */
struct thumb_bd {
	SDL_Rect		rect;		/* the rect for geometry and background */
	struct board		*board;		/* the board */
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

	/* each of the following board has two rectangles,
	 * [0] is the geometry relative to the keypad,
	 * [1] is the geometry relative to the whole screen
	 * we do not use the thumb_bd for these boards because here we need
	 * 2 rectangles for geometry
	 */
	SDL_Rect		kp_msg[2];		/* incoming msg, relative to kpad */
	struct board		*bd_msg;

	SDL_Rect		kp_edit[2];	/* edit user input */
	struct board		*bd_edit;

	SDL_Rect		kp_dialed[2];	/* dialed number */
	struct board		*bd_dialed;

	/* other boards are one associated with the source windows
	 * above the keypad in the layout, we only have the geometry
	 * relative to the whole screen
	 */
	struct thumb_bd		thumb_bd_array[MAX_VIDEO_SOURCES];

	/* variable-size array mapping keypad regions to functions */
	int kp_size, kp_used;
	struct keypad_entry *kp;

	struct display_window   win[WIN_MAX];
};

/*! \brief free the resources in struct gui_info and the descriptor itself.
 *  Return NULL so we can assign the value back to the descriptor in case.
 */
static struct gui_info *cleanup_sdl(struct gui_info *gui, int device_num)
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
	memset(gui, '\0', sizeof(gui));

	/* deallocates the space allocated for the keypad message boards */
	if (gui->bd_dialed)
		delete_board(gui->bd_dialed);
	if (gui->bd_msg)
		delete_board(gui->bd_msg);

	/* deallocates the space allocated for the thumbnail message boards */
	for (i = 0; i < device_num; i++) {
		if (gui->thumb_bd_array[i].board) /* may be useless */
			delete_board(gui->thumb_bd_array[i].board);
	}
	
	ast_free(gui);
	SDL_Quit();
	return NULL;
}

/* messages to be displayed in the sources message boards
 * below the source windows
 */

/* costants defined to describe status of devices */
#define IS_PRIMARY 1
#define IS_SECONDARY 2
#define IS_ON 4

char* src_msgs[] = {
	"    OFF",
	"1   OFF",
	"  2 OFF",
	"1+2 OFF",
	"    ON",
	"1   ON",
	"  2 ON",
	"1+2 ON",
};
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
	} else if (out == WIN_REMOTE) {
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
	} else {
		int i = out-WIN_SRC1;
		b_in = env->out.devices[i].dev_buf;
		if (b_in == NULL)
			return;
		p_in = NULL;
		b_out = &env->src_dpy[i];
	}		
	bmp = gui->win[out].bmp;
	SDL_LockYUVOverlay(bmp);
	/* output picture info - this is sdl, YUV420P */
	memset(&p_out, '\0', sizeof(p_out));
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

#ifdef notyet /* XXX for future implementation */
	KEY_AUDIO_SRCS = 210,
	/*indexes between 210 and 219 (or more) have been reserved for the "keys"
	associated with the audio device markers, clicking on these markers
	will change the source device for audio output */

#endif
	/* Keys related to video sources */
	KEY_FREEZE = 220,	/* freeze the incoming video */
	KEY_CAPTURE = 221,	/* capture the whole SDL window as a picture */
	KEY_PIP = 230,
	/*indexes between 231 and 239 have been reserved for the "keys"
	associated with the device thumbnails, clicking on these pictures
	will change the source device for primary or secondary (PiP) video output*/
	KEY_SRCS_WIN = 231, /* till 239 */
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
	case KEY_SENDVIDEO: /* send or do not send video */
		env->out.sendvideo = !env->out.sendvideo;
		break;

	case KEY_PIP: /* enable or disable Picture in Picture */
		env->out.picture_in_picture = !env->out.picture_in_picture;
		break;

	case KEY_MUTE: /* send or do not send audio */
		ast_cli_command(env->gui->outfd, "console mute toggle");
		break;

	case KEY_FREEZE: /* freeze/unfreeze the incoming frames */
		env->frame_freeze = !env->frame_freeze;
		break;

#ifdef notyet
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

static int update_device_info(struct video_desc *env, int i)
{
	reset_board(env->gui->thumb_bd_array[i].board);
	print_message(env->gui->thumb_bd_array[i].board,
		src_msgs[env->out.devices[i].status_index]);
	return 0;
}

/*! \brief Changes the video output (local video) source, controlling if
 * it is already using that video device, 
 * and switching the correct fields of env->out.
 * grabbers are always open and saved in the device table.
 * The secondary or the primary device can be changed,
 * according to the "button" parameter:
 * the primary device is changed if button = SDL_BUTTON_LEFT;
 * the secondary device is changed if button = not SDL_BUTTON_LEFT;
 * 
 * the correct message boards of the sources are also updated
 * with the new status
 * 
 * \param env = pointer to the video environment descriptor
 * \param index = index of the device the caller wants to use are primary or secondary device
 * \param button = button clicked on the mouse
 *
 * returns 0 on success,
 * returns 1 on error 
 */
static int switch_video_out(struct video_desc *env, int index, Uint8 button)
{
	int *p; /* pointer to the index of the device to select */

	if (index >= env->out.device_num) {
		ast_log(LOG_WARNING, "no devices\n");
		return 1;
	}
	/* select primary or secondary */
	p = (button == SDL_BUTTON_LEFT) ? &env->out.device_primary :
		&env->out.device_secondary;
	/* controls if the device is already selected */
	if (index == *p) {
		ast_log(LOG_WARNING, "device %s already selected\n", env->out.devices[index].name);
		return 0;
	}
	ast_log(LOG_WARNING, "switching to %s...\n", env->out.devices[index].name);
	/* already open */
	if (env->out.devices[index].grabber) {
		/* we also have to update the messages in the source 
		message boards below the source windows */
		/* first we update the board of the previous source */
		if (p == &env->out.device_primary)
			env->out.devices[*p].status_index &= ~IS_PRIMARY;
		else
			env->out.devices[*p].status_index &= ~IS_SECONDARY;
		update_device_info(env, *p);
		/* update the index used as primary or secondary */
		*p = index;
		ast_log(LOG_WARNING, "done\n");
		/* then we update the board of the new primary or secondary source */
		if (p == &env->out.device_primary)
			env->out.devices[*p].status_index |= IS_PRIMARY;
		else
			env->out.devices[*p].status_index |= IS_SECONDARY;
		update_device_info(env, *p);
		return 0;
	}
	/* device is off, just do nothing */
	ast_log(LOG_WARNING, "device is down\n");
	return 1;
}

/*! \brief tries to switch the state of a device from on to off or off to on
 * we also have to update the status of the device and the correct message board
 *
 * \param index = the device that must be turned on or off
 * \param env = pointer to the video environment descriptor
 *
 * returns:
 * - 0 on falure switching from off to on
 * - 1 on success in switching from off to on
 * - 2 on success in switching from on to off
*/
static int turn_on_off(int index, struct video_desc *env)
{
	struct video_device *p = &env->out.devices[index];

	if (index >= env->out.device_num) {
		ast_log(LOG_WARNING, "no devices\n");
		return 0;
	}

	if (!p->grabber) { /* device off */
		void *g_data; /* result of grabber_open() */
		struct grab_desc *g;
		int i;

		/* see if the device can be used by one of the existing drivers */
		for (i = 0; (g = console_grabbers[i]); i++) {
			/* try open the device */
			g_data = g->open(p->name, &env->out.loc_src_geometry, env->out.fps);
			if (!g_data)	/* no luck, try the next driver */
				continue;
			p->grabber = g;
			p->grabber_data = g_data;
			/* update the status of the source */
			p->status_index |= IS_ON;
			/* print the new message in the message board */
			update_device_info(env, index);
			return 1; /* open succeded */
		}
		return 0; /* failure */
	} else {
		/* the grabber must be closed */
		p->grabber_data = p->grabber->close(p->grabber_data);
		p->grabber = NULL;
		/* dev_buf is already freed by grabber->close() */
		p->dev_buf = NULL;
		/* update the status of the source */
		p->status_index &= ~IS_ON;
		/* print the new message in the message board */
		update_device_info(env, index);
		return 2; /* closed */
	}	
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
		
	int i; /* integer variable used as iterator */

	int x; /* integer variable usable as a container */
	
	/* total width of source device thumbnails */
	int src_wins_tot_w = env->out.device_num*(SRC_WIN_W+BORDER)+BORDER;

	/* x coordinate of the center of the keypad */
	int x0 = MAX(env->rem_dpy.w+gui->keypad->w/2+2*BORDER, src_wins_tot_w/2);
	
#if 0
	ast_log(LOG_WARNING, "event %d %d have %d/%d regions at %p\n",
		button.x, button.y, gui->kp_used, gui->kp_size, gui->kp);
#endif
	/* for each mousedown we end previous drag */
	gui->drag.drag_window = DRAG_NONE;
	
	/* define keypad boundary */
	/* XXX this should be extended for clicks on different audio device markers */
	if (button.y >= (env->out.device_num ? SRC_WIN_H+2*BORDER+SRC_MSG_BD_H : 0)) {
		/* if control reaches this point this means that the clicked point is
		below the row of the additional sources windows*/
		/* adjust the y coordinate as if additional devices windows were not present */
		button.y -= (env->out.device_num ? SRC_WIN_H+2*BORDER+SRC_MSG_BD_H : 0);
		if (button.y < BORDER)
			index = KEY_OUT_OF_KEYPAD;
		else if (button.y >= MAX(MAX(env->rem_dpy.h, env->loc_dpy.h), gui->keypad->h))
			index = KEY_OUT_OF_KEYPAD;
		else if (button.x < x0 - gui->keypad->w/2 - BORDER - env->rem_dpy.w)
			index = KEY_OUT_OF_KEYPAD;
		else if (button.x < x0 - gui->keypad->w/2 - BORDER)
			index = KEY_REM_DPY;
		else if (button.x < x0 - gui->keypad->w/2)
			index = KEY_OUT_OF_KEYPAD;
		else if (button.x >= x0 + gui->keypad->w/2 + BORDER + env->loc_dpy.w)
			index = KEY_OUT_OF_KEYPAD;
		else if (button.x >= x0 + gui->keypad->w/2 + BORDER)
			index = KEY_LOC_DPY;
		else if (button.x >= x0 + gui->keypad->w/2)
			index = KEY_OUT_OF_KEYPAD;
		else if (gui->kp) {
			/* we have to calculate the first coordinate 
			inside the keypad before calling the kp_match_area*/
			int x_keypad = button.x - (x0 - gui->keypad->w/2);
			/* find the key clicked (if one was clicked) */
			for (i = 0; i < gui->kp_used; i++) {
				if (kp_match_area(&gui->kp[i],x_keypad, button.y - BORDER)) {
					index = gui->kp[i].c;
					break;
				}
			}
		}
	} else if (button.y < BORDER) {
		index = KEY_OUT_OF_KEYPAD;
	} else {  /* we are in the thumbnail area */
		x = x0 - src_wins_tot_w/2 + BORDER;
		if (button.y >= BORDER + SRC_WIN_H)
			index = KEY_OUT_OF_KEYPAD;
		else if (button.x < x)
			index = KEY_OUT_OF_KEYPAD;
		else if (button.x < x + src_wins_tot_w - BORDER) {
			/* note that the additional device windows 
			are numbered from left to right
			starting from 0, with a maximum of 8, the index associated on a click is:
			KEY_SRCS_WIN + number_of_the_window */
			for (i = 1; i <= env->out.device_num; i++) {
				if (button.x < x+i*(SRC_WIN_W+BORDER)-BORDER) {
					index = KEY_SRCS_WIN+i-1;
					break;
				} else if (button.x < x+i*(SRC_WIN_W+BORDER)) {
					index = KEY_OUT_OF_KEYPAD;
					break;
				}
			}
		} else
			index = KEY_OUT_OF_KEYPAD;
	}

	/* exec the function */
	if (index < 128) {	/* surely clicked on the keypad, don't care which key */
		keypad_digit(env, index);
		return;
	}

	else if (index >= KEY_SRCS_WIN && index < KEY_SRCS_WIN+env->out.device_num) {
		index -= KEY_SRCS_WIN; /* index of the window, equal to the device index in the table */
		/* if one of the additional device windows is clicked with
		left or right mouse button, we have to switch to that device */
		if (button.button == SDL_BUTTON_RIGHT || button.button == SDL_BUTTON_LEFT) {
			switch_video_out(env, index, button.button);
			return;
		}
		/* turn on or off the devices selectively with other mouse buttons */
		else {
			int ret = turn_on_off(index, env);
			/* print a message according to what happened */
			if (!ret)
				ast_log(LOG_WARNING, "unable to turn on device %s\n",
					env->out.devices[index].name);
			else if (ret == 1)
				ast_log(LOG_WARNING, "device %s changed state to on\n",
					env->out.devices[index].name);
			else if (ret == 2)
				ast_log(LOG_WARNING, "device %s changed state to off\n",
					env->out.devices[index].name);
			return;
		}
	}

	/* XXX for future implementation
	else if (click on audio source marker)
		change audio source device
	*/

	switch (index) {
	/* answer/close function */
	case KEY_PICK_UP:
		keypad_pick_up(env);
		break;
	case KEY_HANG_UP:
		ast_cli_command(gui->outfd, "console hangup");
		break;

	/* other functions */
	case KEY_MUTE: /* send or not send the audio */
	case KEY_AUTOANSWER:
	case KEY_SENDVIDEO: /* send or not send the video */
	case KEY_PIP: /* activate/deactivate picture in picture mode */
	case KEY_FREEZE: /* freeze/unfreeze the incoming video */
		keypad_toggle(env, index);
		break;

	case KEY_LOCALVIDEO:
		break;
	case KEY_REMOTEVIDEO:
		break;

#ifdef notyet /* XXX for future implementations */
	case KEY_CAPTURE:
		break;
#endif

	case KEY_MESSAGEBOARD:
		if (button.button == SDL_BUTTON_LEFT)
			set_drag(&gui->drag, button.x, button.y, DRAG_MESSAGE);
		break;

	/* press outside the keypad. right increases size, center decreases, left drags */
	case KEY_LOC_DPY:
	case KEY_REM_DPY:
		if (button.button == SDL_BUTTON_LEFT) {
			/* values used to find the position of the picture in picture (if present) */
			int pip_loc_x = (double)env->out.pip_x/env->enc_in.w * env->loc_dpy.w;
			int pip_loc_y = (double)env->out.pip_y/env->enc_in.h * env->loc_dpy.h;
			/* check if picture in picture is active and the click was on it */
			if (index == KEY_LOC_DPY && env->out.picture_in_picture &&
			  button.x >= x0+gui->keypad->w/2+BORDER+pip_loc_x &&
			  button.x < x0+gui->keypad->w/2+BORDER+pip_loc_x+env->loc_dpy.w/3 &&
			  button.y >= BORDER+pip_loc_y && 
			  button.y < BORDER+pip_loc_y+env->loc_dpy.h/3) {
				/* set the y cordinate to his previous value */
				button.y += (env->out.device_num ? SRC_WIN_H+2*BORDER+SRC_MSG_BD_H : 0);
				/* starts dragging the picture inside the picture */
				set_drag(&gui->drag, button.x, button.y, DRAG_PIP);
			}
			else if (index == KEY_LOC_DPY) {
				/* set the y cordinate to his previous value */
				button.y += (env->out.device_num ? SRC_WIN_H+2*BORDER+SRC_MSG_BD_H : 0);
				/* click in the local display, but not on the PiP */
				set_drag(&gui->drag, button.x, button.y, DRAG_LOCAL);
			}
			break;
		} else {
			char buf[128];
			struct fbuf_t *fb = index == KEY_LOC_DPY ? &env->loc_dpy : &env->rem_dpy;
			sprintf(buf, "%c%dx%d", button.button == SDL_BUTTON_RIGHT ? '>' : '<',
				fb->w, fb->h);
			video_geom(fb, buf);
			sdl_setup(env);
			/* writes messages in the source boards, those can be 
			modified during the execution, because of the events 
			this must be done here, otherwise the status of sources will not be
			shown after sdl_setup */
			for (i = 0; i < env->out.device_num; i++) {
				update_device_info(env, i);
			}
			/* we also have to refresh other boards, 
			to avoid messages to disappear after video resize */
			print_message(gui->bd_msg, " \b");
			print_message(gui->bd_dialed, " \b");
		}
		break;
	case KEY_OUT_OF_KEYPAD:
		ast_log(LOG_WARNING, "nothing clicked, coordinates: %d, %d\n", button.x, button.y);
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
static const char * const us_kbd_map[] = {
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

static void grabber_move(struct video_device *, int dx, int dy);

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

/*! \brief This function moves the picture in picture,
 * controlling the limits of the containing buffer
 * to avoid problems deriving from going through the limits.
 *
 * \param env = pointer to the descriptor of the video environment
 * \param dx = the variation of the x position
 * \param dy = the variation of the y position
*/
static void pip_move(struct video_desc* env, int dx, int dy) {
	int new_pip_x = env->out.pip_x+dx;
	int new_pip_y = env->out.pip_y+dy;
	/* going beyond the left borders */
	if (new_pip_x < 0)
		new_pip_x = 0;
	/* going beyond the right borders */
	else if (new_pip_x > env->enc_in.w - env->enc_in.w/3)
		new_pip_x = env->enc_in.w - env->enc_in.w/3;
	/* going beyond the top borders */
	if (new_pip_y < 0)
		new_pip_y = 0;
	/* going beyond the bottom borders */
	else if (new_pip_y > env->enc_in.h - env->enc_in.h/3)
		new_pip_y = env->enc_in.h - env->enc_in.h/3;
	env->out.pip_x = new_pip_x;
	env->out.pip_y = new_pip_y;
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
#if 0 /* do not react, we don't want to die because the window is minimized */
				if (ev[i].active.gain == 0 && ev[i].active.state & SDL_APPACTIVE) {
					ast_log(LOG_WARNING, "/* somebody has killed us ? */");
					ast_cli_command(gui->outfd, "stop now");
				}
#endif
				break;

			case SDL_KEYUP:	/* ignore, for the time being */
				break;

			case SDL_KEYDOWN:
				handle_keyboard_input(env, &ev[i].key.keysym);
				break;

			case SDL_MOUSEMOTION:
			case SDL_MOUSEBUTTONUP:
				if (drag->drag_window == DRAG_LOCAL && env->out.device_num) {
					/* move the capture source */
					int dx = compute_drag(&drag->x_start, ev[i].motion.x, 3);
					int dy = compute_drag(&drag->y_start, ev[i].motion.y, 3);
					grabber_move(&env->out.devices[env->out.device_primary], dx, dy);
				} else if (drag->drag_window == DRAG_PIP) {
					/* move the PiP image inside the frames of the enc_in buffers */
					int dx = ev[i].motion.x - drag->x_start;
					int dy = ev[i].motion.y - drag->y_start;
					/* dx and dy value are directly applied to env->out.pip_x and
					env->out.pip_y, so they must work as if the format was cif */
					dx = (double)dx*env->enc_in.w/env->loc_dpy.w;
					dy = (double)dy*env->enc_in.h/env->loc_dpy.h;
					/* sets starts to a new value */
					drag->x_start = ev[i].motion.x;
					drag->y_start = ev[i].motion.y;
					/* ast_log(LOG_WARNING, "moving: %d, %d\n", dx, dy); */
					pip_move(env, dx, dy);
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

#ifdef HAVE_X11
/*
 * SDL is not very robust on error handling, so we need to trap ourselves
 * at least the most obvious failure conditions, e.g. a bad SDL_WINDOWID.
 * As of sdl-1.2.13, SDL_SetVideoMode crashes with bad parameters, so
 * we need to do the explicit X calls to make sure the window is correct.
 * And around these calls, we must trap X errors.
 */
static int my_x_handler(Display *d, XErrorEvent *e)
{
	ast_log(LOG_WARNING, "%s error_code %d\n", __FUNCTION__, e->error_code);
	return 0;
}
#endif /* HAVE_X11 */

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
	
	/* Some helper variables used for filling the SDL window */
	int x0; /* the x coordinate of the center of the keypad */
	int x1; /* userful for calculating of the size of the parent window */
	int y0; /* y coordinate of the keypad, the remote window and the local window */
	int src_wins_tot_w; /* total width of the source windows */
	int i;
	int x; /* useful for the creation of the source windows; */
	
#ifdef HAVE_X11
	const char *e = getenv("SDL_WINDOWID");

	if (!ast_strlen_zero(e)) {
		XWindowAttributes a;
		int (*old_x_handler)(Display *d, XErrorEvent *e) = XSetErrorHandler(my_x_handler);
		Display *d = XOpenDisplay(getenv("DISPLAY"));
		long w = atol(e);
		int success = w ? XGetWindowAttributes(d, w, &a) : 0;

		XSetErrorHandler(old_x_handler);
		if (!success) {
			ast_log(LOG_WARNING, "%s error in window\n", __FUNCTION__);
			return;
		}
	}	
#endif
	/*
	 * initialize the SDL environment. We have one large window
	 * with local and remote video, and a keypad.
	 * At the moment we arrange them statically, as follows:
	 * - top row: thumbnails for local video sources;
	 * - next row: message boards for local video sources
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
	
	/* total width of the thumbnails */
	src_wins_tot_w = env->out.device_num*(SRC_WIN_W+BORDER)+BORDER;
	
	/* x coordinate of the center of the keypad */
	x0 = MAX(env->rem_dpy.w+kp_w/2+2*BORDER, src_wins_tot_w/2);
	
	/* from center of the keypad to right border */
	x1 = MAX(env->loc_dpy.w+kp_w/2+2*BORDER, src_wins_tot_w/2);
	
	/* total width of the SDL window to create */
	maxw = x0+x1;
	
	/* total height of the mother window to create */
	maxh = MAX( MAX(env->rem_dpy.h, env->loc_dpy.h), kp_h)+2*BORDER;
	maxh += env->out.device_num ? (2*BORDER+SRC_WIN_H+SRC_MSG_BD_H) : 0;
	
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

        memset(&attr, '\0', sizeof(attr));
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

	y0 = env->out.device_num ? (3*BORDER+SRC_WIN_H+SRC_MSG_BD_H) : BORDER;
	
	SDL_WM_SetCaption("Asterisk console Video Output", NULL);
	
	/* intialize the windows for local and remote video */
	if (set_win(gui->screen, &gui->win[WIN_REMOTE], dpy_fmt,
			env->rem_dpy.w, env->rem_dpy.h, x0-kp_w/2-BORDER-env->rem_dpy.w, y0))
		goto no_sdl;
	/* unfreeze incoming frames if set (to avoid showing nothing) */
	env->frame_freeze = 0;

	if (set_win(gui->screen, &gui->win[WIN_LOCAL], dpy_fmt,
			env->loc_dpy.w, env->loc_dpy.h,
			x0+kp_w/2+BORDER, y0))
		goto no_sdl;
	
	/* initialize device_num source windows (thumbnails) and boards
	(for a maximum of 9 additional windows and boards) */
	x = x0 - src_wins_tot_w/2 + BORDER;
	for (i = 0; i < env->out.device_num; i++){
		struct thumb_bd *p = &gui->thumb_bd_array[i];
		if (set_win(gui->screen, &gui->win[i+WIN_SRC1], dpy_fmt,
			SRC_WIN_W, SRC_WIN_H, x+i*(BORDER+SRC_WIN_W), BORDER))
			goto no_sdl;
		/* set geometry for the rect for the message board of the device */
		p->rect.w = SRC_WIN_W;
		p->rect.h = SRC_MSG_BD_H;
		p->rect.x = x+i*(BORDER+SRC_WIN_W);
		p->rect.y = 2*BORDER+SRC_WIN_H;
		/* the white color is used as background */
		SDL_FillRect(gui->screen, &p->rect,
			SDL_MapRGB(gui->screen->format, 255, 255, 255));
		/* if necessary, initialize boards for the sources */
		if (!p->board)
			p->board =
				board_setup(gui->screen, &p->rect,
				gui->font, gui->font_rects);
		/* update board rect */
		SDL_UpdateRect(gui->screen, p->rect.x, p->rect.y, p->rect.w, p->rect.h);
	}

	/* display the skin, but do not free it as we need it later to
	restore text areas and maybe sliders too */
	if (gui->keypad) {
		struct SDL_Rect *dest = &gui->win[WIN_KEYPAD].rect;
		struct SDL_Rect *src = (gui->kp_rect.w > 0 && gui->kp_rect.h > 0) ? & gui->kp_rect : NULL;
		/* set the coordinates of the keypad relative to the main screen */
		dest->x = x0-kp_w/2;
		dest->y = y0;
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
	env->gui = cleanup_sdl(gui, env->out.device_num);
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
			ret = (xp >= 0 && xp < l && yp >=0 && yp < e->h);
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
static const struct _s_k gui_key_map[] = {
	{"FREEZE",	KEY_FREEZE},
	{"PIP",		KEY_PIP},
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
 *	token x0 y0 w h			# horizontal rectangle (short format)
 *					# this is used e.g. for message boards
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
	memset(&e, '\0', sizeof(e));
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
			r->x = atoi(s2);	/* this becomes x0 */
			r->y = e.x0;		/* this becomes y0 */
			r->w = e.y0;		/* this becomes w  */
			r->h = e.x1;		/* this becomes h  */
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
