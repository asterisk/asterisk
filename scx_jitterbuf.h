/*
 * scx_jitterbuf: jitterbuffering algorithm
 *
 * Copyright (C) 2005, Attractel OOD
 *
 * Contributors:
 * Slav Klenov <slav@securax.org>
 *
 * Copyright on this file is disclaimed to Digium for inclusion in Asterisk
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
 * 
 * \brief Jitterbuffering algorithm.
 * 
 */

#ifndef _SCX_JITTERBUF_H_
#define _SCX_JITTERBUF_H_

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif


/* return codes */
#define SCX_JB_OK		0
#define SCX_JB_DROP		1
#define SCX_JB_INTERP	2
#define SCX_JB_NOFRAME	3


/* defaults */
#define SCX_JB_SIZE_DEFAULT 200
#define SCX_JB_RESYNCH_THRESHOLD_DEFAULT 1000


/* jb configuration properties */
struct scx_jb_conf
{
	long jbsize;
 	long resync_threshold;
};


struct scx_jb_frame
{
	void *data;
	long ts;
	long ms;
	long delivery;
	struct scx_jb_frame *next;
	struct scx_jb_frame *prev;
};


struct scx_jb;


/* jb interface */

struct scx_jb * scx_jb_new(struct scx_jb_conf *conf);

void scx_jb_destroy(struct scx_jb *jb);

int scx_jb_put_first(struct scx_jb *jb, void *data, long ms, long ts, long now);

int scx_jb_put(struct scx_jb *jb, void *data, long ms, long ts, long now);

int scx_jb_get(struct scx_jb *jb, struct scx_jb_frame *frame, long now, long interpl);

long scx_jb_next(struct scx_jb *jb);

int scx_jb_remove(struct scx_jb *jb, struct scx_jb_frame *frameout);

void scx_jb_set_force_resynch(struct scx_jb *jb);


#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _SCX_JITTERBUF_H_ */
