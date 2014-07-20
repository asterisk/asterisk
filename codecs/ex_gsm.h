/*! \file
 * \brief 8-bit raw data
 *
 * Copyright (C) 2008, Digium, Inc.
 *
 * Distributed under the terms of the GNU General Public License
 *
 */

static uint8_t ex_gsm[] = {
	0xda, 0xa6, 0xac, 0x2d, 0xa3, 0x50, 0x00, 0x49, 0x24, 0x92,
	0x49, 0x24, 0x50, 0x40, 0x49, 0x24, 0x92, 0x37, 0x24, 0x52,
	0x00, 0x49, 0x24, 0x92, 0x47, 0x24, 0x50, 0x80, 0x46, 0xe3,
	0x6d, 0xb8, 0xdc,
};

static struct ast_frame *gsm_sample(void)
{
	static struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.datalen = sizeof(ex_gsm),
		/* All frames are 20 ms long */
		.samples = GSM_SAMPLES,
		.mallocd = 0,
		.offset = 0,
		.src = __PRETTY_FUNCTION__,
		.data.ptr = ex_gsm,
	};

	f.subclass.format = ast_format_gsm;

	return &f;
}
