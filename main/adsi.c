/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \brief ADSI Support (built upon Caller*ID)
 */

#include "asterisk.h"

#include "asterisk/adsi.h"
#include "asterisk/lock.h"

static const struct adsi_funcs *installed_funcs;
static const int current_adsi_version = AST_ADSI_VERSION;
AST_RWLOCK_DEFINE_STATIC(func_lock);

int ast_adsi_begin_download(struct ast_channel *chan, char *service, unsigned char *fdn, unsigned char *sec, int version)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->begin_download) {
		res = installed_funcs->begin_download(chan, service, fdn, sec, version);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_end_download(struct ast_channel *chan)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->end_download) {
		res = installed_funcs->end_download(chan);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_channel_restore(struct ast_channel *chan)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->channel_restore) {
		res = installed_funcs->channel_restore(chan);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_print(struct ast_channel *chan, char **lines, int *align, int voice)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->print) {
		res = installed_funcs->print(chan, lines, align, voice);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_load_session(struct ast_channel *chan, unsigned char *app, int ver, int data)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->load_session) {
		res = installed_funcs->load_session(chan, app, ver, data);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_unload_session(struct ast_channel *chan)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->unload_session) {
		res = installed_funcs->unload_session(chan);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_transmit_message(struct ast_channel *chan, unsigned char *msg, int msglen, int msgtype)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->transmit_message) {
		res = installed_funcs->transmit_message(chan, msg, msglen, msgtype);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_transmit_message_full(struct ast_channel *chan, unsigned char *msg, int msglen, int msgtype, int dowait)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->transmit_message_full) {
		res = installed_funcs->transmit_message_full(chan, msg, msglen, msgtype, dowait);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_read_encoded_dtmf(struct ast_channel *chan, unsigned char *buf, int maxlen)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->read_encoded_dtmf) {
		res = installed_funcs->read_encoded_dtmf(chan, buf, maxlen);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_connect_session(unsigned char *buf, unsigned char *fdn, int ver)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->connect_session) {
		res = installed_funcs->connect_session(buf, fdn, ver);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_query_cpeid(unsigned char *buf)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->query_cpeid) {
		res = installed_funcs->query_cpeid(buf);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_query_cpeinfo(unsigned char *buf)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->query_cpeinfo) {
		res = installed_funcs->query_cpeinfo(buf);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_get_cpeid(struct ast_channel *chan, unsigned char *cpeid, int voice)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->get_cpeid) {
		res = installed_funcs->get_cpeid(chan, cpeid, voice);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_get_cpeinfo(struct ast_channel *chan, int *width, int *height, int *buttons, int voice)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->get_cpeinfo) {
		res = installed_funcs->get_cpeinfo(chan, width, height, buttons, voice);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_download_connect(unsigned char *buf, char *service, unsigned char *fdn, unsigned char *sec, int ver)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->download_connect) {
		res = installed_funcs->download_connect(buf, service, fdn, sec, ver);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_disconnect_session(unsigned char *buf)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->disconnect_session) {
		res = installed_funcs->disconnect_session(buf);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_download_disconnect(unsigned char *buf)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->download_disconnect) {
		res = installed_funcs->download_disconnect(buf);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_data_mode(unsigned char *buf)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->data_mode) {
		res = installed_funcs->data_mode(buf);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_clear_soft_keys(unsigned char *buf)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->clear_soft_keys) {
		res = installed_funcs->clear_soft_keys(buf);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_clear_screen(unsigned char *buf)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->clear_screen) {
		res = installed_funcs->clear_screen(buf);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_voice_mode(unsigned char *buf, int when)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->voice_mode) {
		res = installed_funcs->voice_mode(buf, when);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_available(struct ast_channel *chan)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->available) {
		res = installed_funcs->available(chan);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_display(unsigned char *buf, int page, int line, int just, int wrap, char *col1, char *col2)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->display) {
		res = installed_funcs->display(buf, page, line, just, wrap, col1, col2);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_set_line(unsigned char *buf, int page, int line)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->set_line) {
		res = installed_funcs->set_line(buf, page, line);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_load_soft_key(unsigned char *buf, int key, const char *llabel, const char *slabel, char *ret, int data)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->load_soft_key) {
		res = installed_funcs->load_soft_key(buf, key, llabel, slabel, ret, data);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_set_keys(unsigned char *buf, unsigned char *keys)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->set_keys) {
		res = installed_funcs->set_keys(buf, keys);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_input_control(unsigned char *buf, int page, int line, int display, int format, int just)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->input_control) {
		res = installed_funcs->input_control(buf, page, line, display, format, just);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

int ast_adsi_input_format(unsigned char *buf, int num, int dir, int wrap, char *format1, char *format2)
{
	int res = 0;
	ast_rwlock_rdlock(&func_lock);
	if (installed_funcs && installed_funcs->input_format) {
		res = installed_funcs->input_format(buf, num, dir, wrap, format1, format2);
	}
	ast_rwlock_unlock(&func_lock);
	return res;
}

void ast_adsi_install_funcs(const struct adsi_funcs *funcs)
{
	if (funcs && funcs->version < current_adsi_version) {
		ast_log(LOG_WARNING, "Cannot install ADSI function pointers due to version mismatch."
				"Ours: %d, Theirs: %u\n", current_adsi_version, funcs->version);
		return;
	}

	ast_rwlock_wrlock(&func_lock);
	installed_funcs = funcs;
	ast_rwlock_unlock(&func_lock);
}
