/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Flat, binary, ADPCM vox file format.
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */
 
#include <asterisk/lock.h>
#include <asterisk/channel.h>
#include <asterisk/file.h>
#include <asterisk/logger.h>
#include <asterisk/sched.h>
#include <asterisk/module.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#ifdef __linux__
#include <endian.h>
#else
#include <machine/endian.h>
#endif

#define BUF_SIZE 80		/* 160 samples */

struct ast_filestream {
	void *reserved[AST_RESERVED_POINTERS];
	/* This is what a filestream means to us */
	int fd; /* Descriptor */
	struct ast_frame fr;				/* Frame information */
	char waste[AST_FRIENDLY_OFFSET];	/* Buffer for sending frames, etc */
	char empty;							/* Empty character */
	unsigned char buf[BUF_SIZE + 3];				/* Output Buffer */
	int lasttimeout;
	struct timeval last;
	short signal;						/* Signal level (file side) */
	short ssindex;						/* Signal ssindex (file side) */
};


static ast_mutex_t vox_lock = AST_MUTEX_INITIALIZER;
static int glistcnt = 0;

static char *name = "vox";
static char *desc = "Dialogic VOX (ADPCM) File Format";
static char *exts = "vox";

/*
 * Step size index shift table 
 */

static short indsft[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

/*
 * Step size table, where stpsz[i]=floor[16*(11/10)^i]
 */

static short stpsz[49] = {
  16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
  80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279,
  307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
  1060, 1166, 1282, 1411, 1552
};

/* 
 * Nibble to bit map
 */

static short nbl2bit[16][4] = {
  {1, 0, 0, 0}, {1, 0, 0, 1}, {1, 0, 1, 0}, {1, 0, 1, 1},
  {1, 1, 0, 0}, {1, 1, 0, 1}, {1, 1, 1, 0}, {1, 1, 1, 1},
  {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
  {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
};

/*
 * Decode(encoded)
 *  Decodes the encoded nibble from the adpcm file.
 *
 * Results:
 *  Returns the encoded difference.
 *
 * Side effects:
 *  Sets the index to the step size table for the next encode.
 */

static inline short
decode (unsigned char encoded, short *ssindex)
{
  short diff, step;
  step = stpsz[*ssindex];
  diff = nbl2bit[encoded][0] * (step * nbl2bit[encoded][1] +
				(step >> 1) * nbl2bit[encoded][2] +
				(step >> 2) * nbl2bit[encoded][3] +
				(step >> 3));
  *ssindex = *ssindex + indsft[(encoded & 7)];
  if (*ssindex < 0)
    *ssindex = 0;
  else if (*ssindex > 48)
    *ssindex = 48;
  return (diff);
}

/*
 * Adpcm
 *  Takes a signed linear signal and encodes it as ADPCM
 *  For more information see http://support.dialogic.com/appnotes/adpcm.pdf
 *
 * Results:
 *  Foo.
 *
 * Side effects:
 *  signal gets updated with each pass.
 */

static inline unsigned char
adpcm (short csig, short *ssindex, short *signal)
{
  short diff, step;
  unsigned char encoded;
  step = stpsz[*ssindex];
  /* 
   * Clip csig if too large or too small
   */
   
  csig >>= 4;

  diff = csig - *signal;
  
  if (diff < 0)
    {
      encoded = 8;
      diff = -diff;
    }
  else
    encoded = 0;
  if (diff >= step)
    {
      encoded |= 4;
      diff -= step;
    }
  step >>= 1;
  if (diff >= step)
    {
      encoded |= 2;
      diff -= step;
    }
  step >>= 1;
  if (diff >= step)
    encoded |= 1;
    
  *signal += decode (encoded, ssindex);
  return (encoded);
}

static struct ast_filestream *vox_open(int fd)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&vox_lock)) {
			ast_log(LOG_WARNING, "Unable to lock vox list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		tmp->fr.data = tmp->buf;
		tmp->fr.frametype = AST_FRAME_VOICE;
		tmp->fr.subclass = AST_FORMAT_ADPCM;
		/* datalen will vary for each frame */
		tmp->fr.src = name;
		tmp->fr.mallocd = 0;
		tmp->lasttimeout = -1;
		glistcnt++;
		ast_mutex_unlock(&vox_lock);
		ast_update_use_count();
	}
	return tmp;
}

static struct ast_filestream *vox_rewrite(int fd, char *comment)
{
	/* We don't have any header to read or anything really, but
	   if we did, it would go here.  We also might want to check
	   and be sure it's a valid file.  */
	struct ast_filestream *tmp;
	if ((tmp = malloc(sizeof(struct ast_filestream)))) {
		memset(tmp, 0, sizeof(struct ast_filestream));
		if (ast_mutex_lock(&vox_lock)) {
			ast_log(LOG_WARNING, "Unable to lock vox list\n");
			free(tmp);
			return NULL;
		}
		tmp->fd = fd;
		glistcnt++;
		ast_mutex_unlock(&vox_lock);
		ast_update_use_count();
	} else
		ast_log(LOG_WARNING, "Out of memory\n");
	return tmp;
}

static void vox_close(struct ast_filestream *s)
{
	if (ast_mutex_lock(&vox_lock)) {
		ast_log(LOG_WARNING, "Unable to lock vox list\n");
		return;
	}
	glistcnt--;
	ast_mutex_unlock(&vox_lock);
	ast_update_use_count();
	close(s->fd);
	free(s);
	s = NULL;
}

static struct ast_frame *vox_read(struct ast_filestream *s, int *whennext)
{
	int res;
	int x;
	/* Send a frame from the file to the appropriate channel */

	s->fr.frametype = AST_FRAME_VOICE;
	s->fr.subclass = AST_FORMAT_ADPCM;
	s->fr.offset = AST_FRIENDLY_OFFSET;
	s->fr.mallocd = 0;
	s->fr.data = s->buf;
	if ((res = read(s->fd, s->buf + 3, BUF_SIZE)) < 1) {
		if (res)
			ast_log(LOG_WARNING, "Short read (%d) (%s)!\n", res, strerror(errno));
		return NULL;
	}
	/* Store index, then signal */
	s->buf[0] = s->ssindex & 0xff;
	s->buf[1] = (s->signal >> 8) & 0xff;
	s->buf[2] = s->signal & 0xff;
	/* Do the decoder to be sure we get the right stuff in the signal and index fields. */
	for (x=3;x<res+3;x++) {
		s->signal += decode(s->buf[x] >> 4, &s->ssindex);
		s->signal += decode(s->buf[x] & 0xf, &s->ssindex);
	}
	s->fr.samples = res * 2;
	s->fr.datalen = res + 3;
	*whennext = s->fr.samples;
	return &s->fr;
}

static int vox_write(struct ast_filestream *fs, struct ast_frame *f)
{
	int res;
	if (f->frametype != AST_FRAME_VOICE) {
		ast_log(LOG_WARNING, "Asked to write non-voice frame!\n");
		return -1;
	}
	if (f->subclass != AST_FORMAT_ADPCM) {
		ast_log(LOG_WARNING, "Asked to write non-ADPCM frame (%d)!\n", f->subclass);
		return -1;
	}
	if (f->datalen < 3) {
		ast_log(LOG_WARNING, "Invalid frame of data (< 3 bytes long) from %s\n", f->src);
		return -1;
	}
	if ((res = write(fs->fd, f->data + 3, f->datalen)) != f->datalen) {
			ast_log(LOG_WARNING, "Bad write (%d/%d): %s\n", res, f->datalen, strerror(errno));
			return -1;
	}
	return 0;
}

static char *vox_getcomment(struct ast_filestream *s)
{
	return NULL;
}

static int vox_seek(struct ast_filestream *fs, long sample_offset, int whence)
{
	return -1;
}

static int vox_trunc(struct ast_filestream *fs)
{
	return -1;
}

static long vox_tell(struct ast_filestream *fs)
{
	return -1;
}

int load_module()
{
	return ast_format_register(name, exts, AST_FORMAT_ADPCM,
								vox_open,
								vox_rewrite,
								vox_write,
								vox_seek,
								vox_trunc,
								vox_tell,
								vox_read,
								vox_close,
								vox_getcomment);
								
								
}

int unload_module()
{
	return ast_format_unregister(name);
}	

int usecount()
{
	int res;
	if (ast_mutex_lock(&vox_lock)) {
		ast_log(LOG_WARNING, "Unable to lock vox list\n");
		return -1;
	}
	res = glistcnt;
	ast_mutex_unlock(&vox_lock);
	return res;
}

char *description()
{
	return desc;
}


char *key()
{
	return ASTERISK_GPL_KEY;
}
