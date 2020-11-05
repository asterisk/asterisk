#include "asterisk/format_cache.h"      /* for ast_format_amr(wb) */
#include "asterisk/frame.h"             /* for ast_frame, etc */

static uint8_t ex_amr[] = {
	0xf0, 0x6d, 0x47, 0x8c, 0xc3, 0x0d, 0x03, 0xec,
	0xe2, 0x18, 0x3e, 0x28, 0x20, 0x80
};

static struct ast_frame *amr_sample(void)
{
	static struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.datalen = sizeof(ex_amr),
		.samples = 160,
		.mallocd = 0,
		.offset = 0,
		.src = __PRETTY_FUNCTION__,
		.data.ptr = ex_amr,
	};

	f.subclass.format = ast_format_amr;

	return &f;
}

static uint8_t ex_amrwb[] = {
	0xf1, 0x5e, 0x51, 0x98, 0xc5, 0x64, 0xc7, 0xc5,
	0x0c, 0x6c, 0x82, 0x19, 0x16, 0x03, 0xf0, 0x0a,
	0x0b, 0x57, 0x53, 0x51, 0x7f, 0x97, 0x97, 0x79,
	0x31, 0xdd, 0x73, 0x1b, 0x92, 0x54, 0xf5, 0x79,
	0x9a,
};

static struct ast_frame *amrwb_sample(void)
{
	static struct ast_frame f = {
		.frametype = AST_FRAME_VOICE,
		.datalen = sizeof(ex_amrwb),
		.samples = 320,
		.mallocd = 0,
		.offset = 0,
		.src = __PRETTY_FUNCTION__,
		.data.ptr = ex_amrwb,
	};

	f.subclass.format = ast_format_amrwb;

	return &f;
}
