/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Anthony Minessale II.
 *
 * Anthony Minessale <anthmct@yahoo.com>
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
 * \brief A machine to gather up arbitrary frames and convert them
 * to raw slinear on demand.
 *
 * \author Anthony Minessale <anthmct@yahoo.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/frame.h"
#include "asterisk/slinfactory.h"
#include "asterisk/translate.h"

void ast_slinfactory_init(struct ast_slinfactory *sf) 
{
	memset(sf, 0, sizeof(*sf));
	sf->offset = sf->hold;
	sf->output_format = AST_FORMAT_SLINEAR;
}

int ast_slinfactory_init_rate(struct ast_slinfactory *sf, unsigned int sample_rate) 
{
	memset(sf, 0, sizeof(*sf));
	sf->offset = sf->hold;
	switch (sample_rate) {
	case 8000:
		sf->output_format = AST_FORMAT_SLINEAR;
		break;
	case 16000:
		sf->output_format = AST_FORMAT_SLINEAR16;
		break;
	default:
		return -1;
	}

	return 0;
}

void ast_slinfactory_destroy(struct ast_slinfactory *sf) 
{
	struct ast_frame *f;

	if (sf->trans) {
		ast_translator_free_path(sf->trans);
		sf->trans = NULL;
	}

	while ((f = AST_LIST_REMOVE_HEAD(&sf->queue, frame_list)))
		ast_frfree(f);
}

int ast_slinfactory_feed(struct ast_slinfactory *sf, struct ast_frame *f)
{
	struct ast_frame *begin_frame = f, *duped_frame = NULL, *frame_ptr;
	unsigned int x = 0;

	/* In some cases, we can be passed a frame which has no data in it, but
	 * which has a positive number of samples defined. Once such situation is
	 * when a jitter buffer is in use and the jitter buffer interpolates a frame.
	 * The frame it produces has data set to NULL, datalen set to 0, and samples
	 * set to either 160 or 240.
	 */
	if (!f->data.ptr) {
		return 0;
	}

	if (f->subclass != sf->output_format) {
		if (sf->trans && f->subclass != sf->format) {
			ast_translator_free_path(sf->trans);
			sf->trans = NULL;
		}

		if (!sf->trans) {
			if (!(sf->trans = ast_translator_build_path(sf->output_format, f->subclass))) {
				ast_log(LOG_WARNING, "Cannot build a path from %s to %s\n", ast_getformatname(f->subclass),
					ast_getformatname(sf->output_format));
				return 0;
			}
			sf->format = f->subclass;
		}

		if (!(begin_frame = ast_translate(sf->trans, f, 0))) {
			return 0;
		}
		
		if (!(duped_frame = ast_frisolate(begin_frame))) {
			return 0;
		}

		if (duped_frame != begin_frame) {
			ast_frfree(begin_frame);
		}
	} else {
		if (sf->trans) {
			ast_translator_free_path(sf->trans);
			sf->trans = NULL;
		}
		if (!(duped_frame = ast_frdup(f)))
			return 0;
	}

	AST_LIST_TRAVERSE(&sf->queue, frame_ptr, frame_list) {
		x++;
	}

	/* if the frame was translated, the translator may have returned multiple
	   frames, so process each of them
	*/
	for (begin_frame = duped_frame; begin_frame; begin_frame = AST_LIST_NEXT(begin_frame, frame_list)) {
		AST_LIST_INSERT_TAIL(&sf->queue, begin_frame, frame_list);
		sf->size += begin_frame->samples;
	}

	return x;
}

int ast_slinfactory_read(struct ast_slinfactory *sf, short *buf, size_t samples) 
{
	struct ast_frame *frame_ptr;
	unsigned int sofar = 0, ineed, remain;
	short *frame_data, *offset = buf;

	while (sofar < samples) {
		ineed = samples - sofar;

		if (sf->holdlen) {
			if (sf->holdlen <= ineed) {
				memcpy(offset, sf->hold, sf->holdlen * sizeof(*offset));
				sofar += sf->holdlen;
				offset += sf->holdlen;
				sf->holdlen = 0;
				sf->offset = sf->hold;
			} else {
				remain = sf->holdlen - ineed;
				memcpy(offset, sf->offset, ineed * sizeof(*offset));
				sofar += ineed;
				sf->offset += ineed;
				sf->holdlen = remain;
			}
			continue;
		}
		
		if ((frame_ptr = AST_LIST_REMOVE_HEAD(&sf->queue, frame_list))) {
			frame_data = frame_ptr->data.ptr;
			
			if (frame_ptr->samples <= ineed) {
				memcpy(offset, frame_data, frame_ptr->samples * sizeof(*offset));
				sofar += frame_ptr->samples;
				offset += frame_ptr->samples;
			} else {
				remain = frame_ptr->samples - ineed;
				memcpy(offset, frame_data, ineed * sizeof(*offset));
				sofar += ineed;
				frame_data += ineed;
				if (remain > (AST_SLINFACTORY_MAX_HOLD - sf->holdlen)) {
					remain = AST_SLINFACTORY_MAX_HOLD - sf->holdlen;
				}
				memcpy(sf->hold, frame_data, remain * sizeof(*offset));
				sf->holdlen = remain;
			}
			ast_frfree(frame_ptr);
		} else {
			break;
		}
	}

	sf->size -= sofar;
	return sofar;
}

unsigned int ast_slinfactory_available(const struct ast_slinfactory *sf)
{
	return sf->size;
}

void ast_slinfactory_flush(struct ast_slinfactory *sf)
{
	struct ast_frame *fr = NULL;

	if (sf->trans) {
		ast_translator_free_path(sf->trans);
		sf->trans = NULL;
	}

	while ((fr = AST_LIST_REMOVE_HEAD(&sf->queue, frame_list)))
		ast_frfree(fr);

	sf->size = sf->holdlen = 0;
	sf->offset = sf->hold;

	return;
}
