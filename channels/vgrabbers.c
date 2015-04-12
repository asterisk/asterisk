/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright 2007, Luigi Rizzo 
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

/*
 * Video grabbers used in console_video.
 *
 * $Revision$
 *
 * Each grabber is implemented through open/read/close calls,
 * plus an additional move() function used e.g. to change origin
 * for the X grabber (this may be extended in the future to support
 * more controls e.g. resolution changes etc.).
 *
 * open() should try to open and initialize the grabber, returning NULL on error.
 * On success it allocates a descriptor for its private data (including
 * a buffer for the video) and returns a pointer to the descriptor.
 * read() will return NULL on failure, or a pointer to a buffer with data
 * on success.
 * close() should release resources.
 * move() is optional.
 * For more details look at the X11 grabber below.
 *
 * NOTE: at the moment we expect uncompressed video frames in YUV format,
 * because this is what current sources supply and also is a convenient
 * format for display. It is conceivable that one might want to support
 * an already compressed stream, in which case we should redesign the
 * pipeline used for the local source, which at the moment is
 *
 *                        .->--[loc_dpy]
 *   [src]-->--[enc_in]--+
 *                        `->--[enc_out]
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"
ASTERISK_REGISTER_FILE(__FILE__)
#include <sys/ioctl.h>
#include "asterisk/file.h"
#include "asterisk/utils.h"	/* ast_calloc */

#include "console_video.h"

#if defined(HAVE_VIDEO_CONSOLE)

#ifdef HAVE_X11

/* A simple X11 grabber, supporting only truecolor formats */

#include <X11/Xlib.h>

/*! \brief internal info used by the X11 grabber */
struct grab_x11_desc {
	Display		*dpy;
	XImage		*image;
	int		screen_width;	/* width of X screen */
	int		screen_height;	/* height of X screen */
	struct fbuf_t	b;		/* geometry and pointer into the XImage */
};

static void *grab_x11_close(void *desc);	/* forward declaration */

/*! \brief open the grabber.
 * We use the special name 'X11' to indicate this grabber.
 */
static void *grab_x11_open(const char *name, struct fbuf_t *geom, int fps)
{
	XImage *im;
	int screen_num;
	struct grab_x11_desc *v;
	struct fbuf_t *b;

	/* all names starting with X11 identify this grabber */
	if (strncasecmp(name, "X11", 3))
		return NULL;	/* not us */
	v = ast_calloc(1, sizeof(*v));
	if (v == NULL)
		return NULL;	/* no memory */

	/* init the connection with the X server */
	v->dpy = XOpenDisplay(NULL);
	if (v->dpy == NULL) {
		ast_log(LOG_WARNING, "error opening display\n");
		goto error;
	}

	v->b = *geom;	/* copy geometry */
	b = &v->b;	/* shorthand */
	/* find width and height of the screen */
	screen_num = DefaultScreen(v->dpy);
	v->screen_width = DisplayWidth(v->dpy, screen_num);
	v->screen_height = DisplayHeight(v->dpy, screen_num);

	v->image = im = XGetImage(v->dpy,
		RootWindow(v->dpy, DefaultScreen(v->dpy)),
		b->x, b->y, b->w, b->h, AllPlanes, ZPixmap);
	if (v->image == NULL) {
		ast_log(LOG_WARNING, "error creating Ximage\n");
		goto error;
	}
	switch (im->bits_per_pixel) {
	case 32:
		b->pix_fmt = PIX_FMT_RGBA32;
		break;
	case 16:
		b->pix_fmt = (im->green_mask == 0x7e0) ? PIX_FMT_RGB565 : PIX_FMT_RGB555;
		break;
	}

	ast_log(LOG_NOTICE, "image: data %p %d bpp fmt %d, mask 0x%lx 0x%lx 0x%lx\n",
		im->data,
		im->bits_per_pixel,
		b->pix_fmt,
		im->red_mask, im->green_mask, im->blue_mask);

	/* set the pointer but not the size as this is not malloc'ed */
	b->data = (uint8_t *)im->data;
	return v;

error:
	return grab_x11_close(v);
}

static struct fbuf_t *grab_x11_read(void *desc)
{
	/* read frame from X11 */
	struct grab_x11_desc *v = desc;
	struct fbuf_t *b = &v->b;

	XGetSubImage(v->dpy,
		RootWindow(v->dpy, DefaultScreen(v->dpy)),
			b->x, b->y, b->w, b->h, AllPlanes, ZPixmap, v->image, 0, 0);

	b->data = (uint8_t *)v->image->data;
	return b;
}

static int boundary_checks(int x, int limit)
{
        return (x <= 0) ? 0 : (x > limit ? limit : x);
}

/*! \brief move the origin for the grabbed area, making sure we do not
 * overflow the screen.
 */
static void grab_x11_move(void *desc, int dx, int dy)
{
	struct grab_x11_desc *v = desc;

        v->b.x = boundary_checks(v->b.x + dx, v->screen_width - v->b.w);
        v->b.y = boundary_checks(v->b.y + dy, v->screen_height - v->b.h);
}

/*! \brief disconnect from the server and release memory */
static void *grab_x11_close(void *desc)
{
	struct grab_x11_desc *v = desc;

	if (v->dpy)
		XCloseDisplay(v->dpy);
	v->dpy = NULL;
	v->image = NULL;
	ast_free(v);
	return NULL;
}

static struct grab_desc grab_x11_desc = {
	.name = "X11",
	.open = grab_x11_open,
	.read = grab_x11_read,
	.move = grab_x11_move,
	.close = grab_x11_close,
};
#endif	/* HAVE_X11 */

#ifdef HAVE_VIDEODEV_H
#include <linux/videodev.h>	/* Video4Linux stuff is only used in grab_v4l1_open() */

struct grab_v4l1_desc {
	int fd;			/* device handle */
	struct fbuf_t	b;	/* buffer (allocated) with grabbed image */
};

/*! \brief
 * Open the local video source and allocate a buffer
 * for storing the image.
 */
static void *grab_v4l1_open(const char *dev, struct fbuf_t *geom, int fps)
{
	struct video_window vw = { 0 };	/* camera attributes */
	struct video_picture vp;
	int fd, i;
	struct grab_v4l1_desc *v;
	struct fbuf_t *b;

	/* name should be something under /dev/ */
	if (strncmp(dev, "/dev/", 5)) 
		return NULL;
	fd = open(dev, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		ast_log(LOG_WARNING, "error opening camera %s\n", dev);
		return NULL;
	}

	v = ast_calloc(1, sizeof(*v));
	if (v == NULL) {
		ast_log(LOG_WARNING, "no memory for camera %s\n", dev);
		close(fd);
		return NULL;	/* no memory */
	}
	v->fd = fd;
	v->b = *geom;
	b = &v->b;	/* shorthand */

	i = fcntl(fd, F_GETFL);
	if (-1 == fcntl(fd, F_SETFL, i | O_NONBLOCK)) {
		/* non fatal, just emit a warning */
		ast_log(LOG_WARNING, "error F_SETFL for %s [%s]\n",
			dev, strerror(errno));
	}
	/* set format for the camera.
	 * In principle we could retry with a different format if the
	 * one we are asking for is not supported.
	 */
	vw.width = b->w;
	vw.height = b->h;
	vw.flags = fps << 16;
	if (ioctl(fd, VIDIOCSWIN, &vw) == -1) {
		ast_log(LOG_WARNING, "error setting format for %s [%s]\n",
			dev, strerror(errno));
		goto error;
	}
	if (ioctl(fd, VIDIOCGPICT, &vp) == -1) {
		ast_log(LOG_WARNING, "error reading picture info\n");
		goto error;
	}
	ast_log(LOG_WARNING,
		"contrast %d bright %d colour %d hue %d white %d palette %d\n",
		vp.contrast, vp.brightness,
		vp.colour, vp.hue,
		vp.whiteness, vp.palette);
	/* set the video format. Here again, we don't necessary have to
	 * fail if the required format is not supported, but try to use
	 * what the camera gives us.
	 */
	b->pix_fmt = vp.palette;
	vp.palette = VIDEO_PALETTE_YUV420P;
	if (ioctl(v->fd, VIDIOCSPICT, &vp) == -1) {
		ast_log(LOG_WARNING, "error setting palette, using %d\n",
			b->pix_fmt);
	} else
		b->pix_fmt = vp.palette;
	/* allocate the source buffer.
	 * XXX, the code here only handles yuv411, for other formats
	 * we need to look at pix_fmt and set size accordingly
	 */
	b->size = (b->w * b->h * 3)/2;	/* yuv411 */
	ast_log(LOG_WARNING, "videodev %s opened, size %dx%d %d\n",
		dev, b->w, b->h, b->size);
	b->data = ast_calloc(1, b->size);
	if (!b->data) {
		ast_log(LOG_WARNING, "error allocating buffer %d bytes\n",
			b->size);
		goto error;
	}
	ast_log(LOG_WARNING, "success opening camera\n");
	return v;

error:
	close(v->fd);
	fbuf_free(b);
	ast_free(v);
	return NULL;
}

/*! \brief read until error, no data or buffer full.
 * This might be blocking but no big deal since we are in the
 * display thread.
 */
static struct fbuf_t *grab_v4l1_read(void *desc)
{
	struct grab_v4l1_desc *v = desc;
	struct fbuf_t *b = &v->b;
	for (;;) {
		int r, l = b->size - b->used;
		r = read(v->fd, b->data + b->used, l);
		// ast_log(LOG_WARNING, "read %d of %d bytes from webcam\n", r, l);
		if (r < 0)	/* read error */
			break;
		if (r == 0)	/* no data */
			break;
		b->used += r;
		if (r == l) {
			b->used = 0; /* prepare for next frame */
			return b;
		}
	}
	return NULL;
}

static void *grab_v4l1_close(void *desc)
{
	struct grab_v4l1_desc *v = desc;

	close(v->fd);
	v->fd = -1;
	fbuf_free(&v->b);
	ast_free(v);
	return NULL;
}

/*! \brief our descriptor. We don't have .move */
static struct grab_desc grab_v4l1_desc = {
	.name = "v4l1",
	.open = grab_v4l1_open,
	.read = grab_v4l1_read,
	.close = grab_v4l1_close,
};
#endif /* HAVE_VIDEODEV_H */

/*
 * Here you can add more grabbers, e.g. V4L2, firewire,
 * a file, a still image...
 */

/*! \brief The list of grabbers supported, with a NULL at the end */
struct grab_desc *console_grabbers[] = {
#ifdef HAVE_X11
	&grab_x11_desc,
#endif
#ifdef HAVE_VIDEODEV_H
	&grab_v4l1_desc,
#endif
	NULL
};

#endif /* HAVE_VIDEO_CONSOLE */
