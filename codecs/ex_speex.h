/*! \file
 * \brief Random Data
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Distributed under the terms of the GNU General Public License
 *
 */

static uint8_t ex_speex[] = {
	0x2e, 0x8e, 0x0f, 0x9a, 0x20, 0000, 0x01, 0x7f, 0xff, 0xff, 
	0xff, 0xff, 0xff, 0x91, 0000, 0xbf, 0xff, 0xff, 0xff, 0xff, 
	0xff, 0xdc, 0x80, 0x5f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
	0x98, 0x7f, 0xff, 0xff, 0xff, 0xe8, 0xff, 0xf7, 0x80,
};

static struct ast_frame *speex_sample(void)
{
	static struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.subclass = AST_FORMAT_SPEEX,
		.datalen = sizeof(ex_speex),
		/* All frames are 20 ms long */
		.samples = SPEEX_SAMPLES,
		.mallocd = 0,
		.offset = 0,
		.src = __PRETTY_FUNCTION__,
		.data.ptr = ex_speex,
	};

	return &f;
}
