/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Christopher L. Wade <wade.christopher@gmail.com>
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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/adsi.h"

#ifdef SKREP
#define build_stub(func_name,...) \
static int stub_ ## func_name(__VA_ARGS__) \
{ \
	if (option_debug > 4) \
	        ast_log(LOG_NOTICE, "ADSI support not loaded!\n"); \
        return -1; \
} \
\
int (*func_name)(__VA_ARGS__) = \
	stub_ ## func_name;
#endif
#define build_stub(func_name,...) \
static int stub_##func_name(__VA_ARGS__) \
{ \
	ast_debug(5, "ADSI support not loaded!\n"); \
        return -1; \
} \
\
int (*func_name)(__VA_ARGS__) = \
	stub_##func_name;

build_stub(ast_adsi_channel_init, struct ast_channel *chan)
build_stub(ast_adsi_begin_download, struct ast_channel *chan, char *service, unsigned char *fdn, unsigned char *sec, int version)
build_stub(ast_adsi_end_download, struct ast_channel *chan)
build_stub(ast_adsi_channel_restore, struct ast_channel *chan)
build_stub(ast_adsi_print, struct ast_channel *chan, char **lines, int *align, int voice)
build_stub(ast_adsi_load_session, struct ast_channel *chan, unsigned char *app, int ver, int data)
build_stub(ast_adsi_unload_session, struct ast_channel *chan)
build_stub(ast_adsi_transmit_messages, struct ast_channel *chan, unsigned char **msg, int *msglen, int *msgtype)
build_stub(ast_adsi_transmit_message, struct ast_channel *chan, unsigned char *msg, int msglen, int msgtype)
build_stub(ast_adsi_transmit_message_full, struct ast_channel *chan, unsigned char *msg, int msglen, int msgtype, int dowait)
build_stub(ast_adsi_read_encoded_dtmf, struct ast_channel *chan, unsigned char *buf, int maxlen)
build_stub(ast_adsi_connect_session, unsigned char *buf, unsigned char *fdn, int ver)
build_stub(ast_adsi_query_cpeid, unsigned char *buf)
build_stub(ast_adsi_query_cpeinfo, unsigned char *buf)
build_stub(ast_adsi_get_cpeid, struct ast_channel *chan, unsigned char *cpeid, int voice)
build_stub(ast_adsi_get_cpeinfo, struct ast_channel *chan, int *width, int *height, int *buttons, int voice)
build_stub(ast_adsi_download_connect, unsigned char *buf, char *service, unsigned char *fdn, unsigned char *sec, int ver)
build_stub(ast_adsi_disconnect_session, unsigned char *buf)
build_stub(ast_adsi_download_disconnect, unsigned char *buf)
build_stub(ast_adsi_data_mode, unsigned char *buf)
build_stub(ast_adsi_clear_soft_keys, unsigned char *buf)
build_stub(ast_adsi_clear_screen, unsigned char *buf)
build_stub(ast_adsi_voice_mode, unsigned char *buf, int when)
build_stub(ast_adsi_available, struct ast_channel *chan)
build_stub(ast_adsi_display, unsigned char *buf, int page, int line, int just, int wrap, char *col1, char *col2)
build_stub(ast_adsi_set_line, unsigned char *buf, int page, int line)
build_stub(ast_adsi_load_soft_key, unsigned char *buf, int key, const char *llabel, const char *slabel, char *ret, int data)
build_stub(ast_adsi_set_keys, unsigned char *buf, unsigned char *keys)
build_stub(ast_adsi_input_control, unsigned char *buf, int page, int line, int display, int format, int just)
build_stub(ast_adsi_input_format, unsigned char *buf, int num, int dir, int wrap, char *format1, char *format2)
