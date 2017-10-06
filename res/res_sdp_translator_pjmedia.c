/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
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

#include "asterisk.h"
#include "asterisk/sdp_translator.h"
#include "asterisk/sdp_options.h"
#include "asterisk/vector.h"
#include "asterisk/netsock2.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/test.h"
#include "asterisk/module.h"

#include "asterisk/sdp.h"
#ifdef HAVE_PJPROJECT
#include <pjlib.h>
#include <pjmedia.h>
#endif

/*** MODULEINFO
	<depend>pjproject</depend>
	<support_level>core</support_level>
 ***/

/*
 * XXX TODO: The memory in the pool is held onto longer than necessary.  It
 * is kept and grows for the duration of the associated chan_pjsip session.
 *
 * The translation API does not need to be so generic.  The users will know
 * at compile time what the non-Asterisk SDP format they have or need.  They
 * should simply call the specific translation functions.  However, to make
 * this a loadable module we need to be able to keep it in memory when a
 * dependent module is loaded.
 *
 * To address both issues I propose this API:
 *
 * void ast_sdp_translate_pjmedia_ref(void) - Inc this module's user ref
 * void ast_sdp_translate_pjmedia_unref(void) - Dec this module's user ref.
 *    The res_pjsip_session.c:ast_sip_session_alloc() can call the module ref
 *    and the session's destructor can call the module unref.
 *
 * struct ast_sdp *ast_sdp_translate_pjmedia_from(const pjmedia_sdp_session *pjmedia_sdp);
 *
 * pjmedia_sdp_session *ast_sdp_translate_pjmedia_to(const struct ast_sdp *sdp, pj_pool_t *pool);
 *    Passing in a memory pool allows the memory to be obtained from an
 *    rdata memory pool that will be released when the message processing
 *    is complete.  This prevents memory from accumulating for the duration
 *    of a call.
 *
 * int ast_sdp_translate_pjmedia_set_remote_sdp(struct ast_sdp_state *sdp_state, const pjmedia_sdp_session *remote);
 * const pjmedia_sdp_session *ast_sdp_translate_pjmedia_get_local_sdp(struct ast_sdp_state *sdp_state, pj_pool_t *pool);
 *    These two functions just do the bookkeeping to translate and set or get
 *    the requested SDP.
 *
 *
 * XXX TODO: This code doesn't handle allocation failures very well.  i.e.,
 *   It assumes they will never happen.
 *
 * XXX TODO: This code uses ast_alloca() inside loops.  Doing so if the number
 *   of times through the loop is unconstrained will blow the stack.
 *   See dupa_pj_str() usage.
 */

static pj_caching_pool sdp_caching_pool;


static void *pjmedia_new(void)
{
	pj_pool_t *pool;

	pool = pj_pool_create(&sdp_caching_pool.factory, "pjmedia sdp translator", 1024, 1024, NULL);

	return pool;
}

static void pjmedia_free(void *translator_priv)
{
	pj_pool_t *pool = translator_priv;

	pj_pool_release(pool);
}

#define dupa_pj_str(pjstr) \
({ \
	char *dest = ast_alloca(pjstr.slen + 1); \
	memcpy(dest, pjstr.ptr, pjstr.slen); \
	dest[pjstr.slen] = '\0'; \
	dest; \
})

static struct ast_sdp_m_line *pjmedia_copy_m_line(struct pjmedia_sdp_media *pjmedia_m_line)
{
	int i;

	struct ast_sdp_c_line *c_line = pjmedia_m_line->conn ?
		ast_sdp_c_alloc(dupa_pj_str(pjmedia_m_line->conn->addr_type),
		dupa_pj_str(pjmedia_m_line->conn->addr)) : NULL;

	struct ast_sdp_m_line *m_line = ast_sdp_m_alloc(dupa_pj_str(pjmedia_m_line->desc.media),
		pjmedia_m_line->desc.port, pjmedia_m_line->desc.port_count,
		dupa_pj_str(pjmedia_m_line->desc.transport), c_line);

	for (i = 0; i < pjmedia_m_line->desc.fmt_count; ++i) {
		ast_sdp_m_add_payload(m_line,
			ast_sdp_payload_alloc(dupa_pj_str(pjmedia_m_line->desc.fmt[i])));
	}

	for (i = 0; i < pjmedia_m_line->attr_count; ++i) {
		ast_sdp_m_add_a(m_line, ast_sdp_a_alloc(dupa_pj_str(pjmedia_m_line->attr[i]->name),
			dupa_pj_str(pjmedia_m_line->attr[i]->value)));
	}

	return m_line;
}

static void pjmedia_copy_a_lines(struct ast_sdp *new_sdp, const pjmedia_sdp_session *pjmedia_sdp)
{
	int i;

	for (i = 0; i < pjmedia_sdp->attr_count; ++i) {
		ast_sdp_add_a(new_sdp, ast_sdp_a_alloc(dupa_pj_str(pjmedia_sdp->attr[i]->name),
			dupa_pj_str(pjmedia_sdp->attr[i]->value)));
	}
}

static void pjmedia_copy_m_lines(struct ast_sdp *new_sdp,
	const struct pjmedia_sdp_session *pjmedia_sdp)
{
	int i;

	for (i = 0; i < pjmedia_sdp->media_count; ++i) {
		ast_sdp_add_m(new_sdp, pjmedia_copy_m_line(pjmedia_sdp->media[i]));
	}
}

static struct ast_sdp *pjmedia_to_sdp(const void *in, void *translator_priv)
{
	const struct pjmedia_sdp_session *pjmedia_sdp = in;

	struct ast_sdp_o_line *o_line = ast_sdp_o_alloc(dupa_pj_str(pjmedia_sdp->origin.user),
		pjmedia_sdp->origin.id, pjmedia_sdp->origin.version,
		dupa_pj_str(pjmedia_sdp->origin.addr_type), dupa_pj_str(pjmedia_sdp->origin.addr));

	struct ast_sdp_c_line *c_line = pjmedia_sdp->conn ?
		ast_sdp_c_alloc(dupa_pj_str(pjmedia_sdp->conn->addr_type),
			dupa_pj_str(pjmedia_sdp->conn->addr)) : NULL;

	struct ast_sdp_s_line *s_line = ast_sdp_s_alloc(dupa_pj_str(pjmedia_sdp->name));

	struct ast_sdp_t_line *t_line = ast_sdp_t_alloc(pjmedia_sdp->time.start,
		pjmedia_sdp->time.stop);

	struct ast_sdp *new_sdp = ast_sdp_alloc(o_line, c_line, s_line, t_line);

	pjmedia_copy_a_lines(new_sdp, pjmedia_sdp);
	pjmedia_copy_m_lines(new_sdp, pjmedia_sdp);

	return new_sdp;
}

static void copy_o_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp,
	struct ast_sdp_o_line *o_line)
{
	pjmedia_sdp->origin.id = o_line->session_id;
	pjmedia_sdp->origin.version = o_line->session_version;
	pj_strdup2(pool, &pjmedia_sdp->origin.user, o_line->username);
	pj_strdup2(pool, &pjmedia_sdp->origin.addr_type, o_line->address_type);
	pj_strdup2(pool, &pjmedia_sdp->origin.addr, o_line->address);
	pj_strdup2(pool, &pjmedia_sdp->origin.net_type, "IN");
}

static void copy_s_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp,
	struct ast_sdp_s_line *s_line)
{
	pj_strdup2(pool, &pjmedia_sdp->name, s_line->session_name);
}

static void copy_t_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp,
	struct ast_sdp_t_line *t_line)
{
	pjmedia_sdp->time.start = t_line->start_time;
	pjmedia_sdp->time.stop = t_line->stop_time;
}

static void copy_c_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_conn **conn,
	struct ast_sdp_c_line *c_line)
{
	pjmedia_sdp_conn *local_conn;
	local_conn = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_conn);
	pj_strdup2(pool, &local_conn->addr_type, c_line->address_type);
	pj_strdup2(pool, &local_conn->addr, c_line->address);
	pj_strdup2(pool, &local_conn->net_type, "IN");

	*conn = local_conn;
}

static void copy_a_lines_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp,
	const struct ast_sdp *sdp)
{
	int i;

	for (i = 0; i < ast_sdp_get_a_count(sdp); ++i) {
		pjmedia_sdp_attr *attr;
		pj_str_t value;
		struct ast_sdp_a_line *a_line;

		a_line = ast_sdp_get_a(sdp, i);
		pj_strdup2(pool, &value, a_line->value);
		attr = pjmedia_sdp_attr_create(pool, a_line->name, &value);
		pjmedia_sdp_session_add_attr(pjmedia_sdp, attr);
	}
}

static void copy_a_lines_pjmedia_media(pj_pool_t *pool, pjmedia_sdp_media *media,
	struct ast_sdp_m_line *m_line)
{
	int i;

	for (i = 0; i < ast_sdp_m_get_a_count(m_line); ++i) {
		pjmedia_sdp_attr *attr;
		pj_str_t value;
		struct ast_sdp_a_line *a_line;

		a_line = ast_sdp_m_get_a(m_line, i);
		pj_strdup2(pool, &value, a_line->value);
		attr = pjmedia_sdp_attr_create(pool, a_line->name, &value);
		pjmedia_sdp_media_add_attr(media, attr);
	}
}

static void copy_m_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_media *media,
	struct ast_sdp_m_line *m_line)
{
	int i;

	media->desc.port = m_line->port;
	media->desc.port_count = m_line->port_count;
	pj_strdup2(pool, &media->desc.transport, m_line->proto);
	pj_strdup2(pool, &media->desc.media, m_line->type);

	for (i = 0; i < ast_sdp_m_get_payload_count(m_line); ++i) {
		pj_strdup2(pool, &media->desc.fmt[i], ast_sdp_m_get_payload(m_line, i)->fmt);
		++media->desc.fmt_count;
	}
	if (m_line->c_line && m_line->c_line->address) {
		copy_c_line_pjmedia(pool, &media->conn, m_line->c_line);
	}
	copy_a_lines_pjmedia_media(pool, media, m_line);
}

static void copy_m_lines_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp,
	const struct ast_sdp *sdp)
{
	int i;

	for (i = 0; i < ast_sdp_get_m_count(sdp); ++i) {
		pjmedia_sdp_media *media;

		media = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_media);
		copy_m_line_pjmedia(pool, media, ast_sdp_get_m(sdp, i));
		pjmedia_sdp->media[pjmedia_sdp->media_count] = media;
		++pjmedia_sdp->media_count;
	}
}

static const void *sdp_to_pjmedia(const struct ast_sdp *sdp, void *translator_priv)
{
	pj_pool_t *pool = translator_priv;
	pjmedia_sdp_session *pjmedia_sdp;

	pjmedia_sdp = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_session);
	copy_o_line_pjmedia(pool, pjmedia_sdp, sdp->o_line);
	copy_s_line_pjmedia(pool, pjmedia_sdp, sdp->s_line);
	copy_t_line_pjmedia(pool, pjmedia_sdp, sdp->t_line);
	copy_c_line_pjmedia(pool, &pjmedia_sdp->conn, sdp->c_line);
	copy_a_lines_pjmedia(pool, pjmedia_sdp, sdp);
	copy_m_lines_pjmedia(pool, pjmedia_sdp, sdp);
	return pjmedia_sdp;
}

static struct ast_sdp_translator_ops pjmedia_translator = {
	.repr = AST_SDP_IMPL_PJMEDIA,
	.translator_new = pjmedia_new,
	.translator_free = pjmedia_free,
	.to_sdp = pjmedia_to_sdp,
	.from_sdp = sdp_to_pjmedia,
};

#ifdef TEST_FRAMEWORK

static int verify_s_line(struct ast_sdp_s_line *s_line, char *expected)
{
	return strcmp(s_line->session_name, expected) == 0;
}

static int verify_c_line(struct ast_sdp_c_line *c_line, char *family, char *addr)
{
	return strcmp(c_line->address_type, family) == 0 && strcmp(c_line->address, addr) == 0;
}

static int verify_t_line(struct ast_sdp_t_line *t_line, uint32_t start, uint32_t end)
{
	return t_line->start_time == start && t_line->stop_time == end;
}

static int verify_m_line(struct ast_sdp *sdp, int index, char *type, int port,
	int port_count, char *profile, ...)
{
	struct ast_sdp_m_line *m_line;
	int res;
	va_list ap;
	int i;

	m_line = ast_sdp_get_m(sdp, index);

	res = strcmp(m_line->type, type) == 0;
	res |= m_line->port == port;
	res |= m_line->port_count == port_count;
	res |= strcmp(m_line->proto, profile) == 0;

	va_start(ap, profile);
	for (i = 0; i < ast_sdp_m_get_payload_count(m_line); ++i) {
		char *payload;

		payload = va_arg(ap, char *);
		if (!payload) {
			res = -1;
			break;
		}
		res |= strcmp(ast_sdp_m_get_payload(m_line, i)->fmt, payload) == 0;
	}
	va_end(ap);
	return res;
}

static int verify_a_line(struct ast_sdp *sdp, int m_index, int a_index, char *name,
	char *value)
{
	struct ast_sdp_m_line *m_line;
	struct ast_sdp_a_line *a_line;

	m_line = ast_sdp_get_m(sdp, m_index);
	a_line = ast_sdp_m_get_a(m_line, a_index);

	return strcmp(a_line->name, name) == 0 && strcmp(a_line->value, value) == 0;
}

AST_TEST_DEFINE(pjmedia_to_sdp_test)
{
	struct ast_sdp_translator *translator;
	pj_pool_t *pool;
	char *sdp_str =
      "v=0\r\n"
      "o=alice 2890844526 2890844527 IN IP4 host.atlanta.example.com\r\n"
      "s= \r\n"
      "c=IN IP4 host.atlanta.example.com\r\n"
      "t=123 456\r\n"
      "m=audio 49170 RTP/AVP 0 8 97\r\n"
      "a=rtpmap:0 PCMU/8000\r\n"
      "a=rtpmap:8 PCMA/8000\r\n"
      "a=rtpmap:97 iLBC/8000\r\n"
	  "a=sendrecv\r\n"
      "m=video 51372 RTP/AVP 31 32\r\n"
      "a=rtpmap:31 H261/90000\r\n"
      "a=rtpmap:32 MPV/90000\r\n";
	pjmedia_sdp_session *pjmedia_sdp;
	struct ast_sdp *sdp = NULL;
	pj_status_t status;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "pjmedia_to_sdp";
		info->category = "/main/sdp/";
		info->summary = "PJMEDIA to SDP unit test";
		info->description =
			"Ensures PJMEDIA SDPs are translated correctly";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	pool = pj_pool_create(&sdp_caching_pool.factory, "pjmedia to sdp test", 1024, 1024, NULL);

	translator = ast_sdp_translator_new(AST_SDP_IMPL_PJMEDIA);
	if (!translator) {
		ast_test_status_update(test, "Failed to create SDP translator\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	status = pjmedia_sdp_parse(pool, sdp_str, strlen(sdp_str), &pjmedia_sdp);
	if (status != PJ_SUCCESS) {
		ast_test_status_update(test, "Error parsing SDP\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	sdp = ast_sdp_translator_to_sdp(translator, pjmedia_sdp);

	if (strcmp(sdp->o_line->username, "alice")) {
		ast_test_status_update(test, "Unexpected SDP user '%s'\n", sdp->o_line->username);
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (sdp->o_line->session_id != 2890844526UL) {
		ast_test_status_update(test, "Unexpected SDP id '%" PRId64 "lu'\n", sdp->o_line->session_id);
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (sdp->o_line->session_version != 2890844527UL) {
		ast_test_status_update(test, "Unexpected SDP version '%" PRId64 "'\n", sdp->o_line->session_version);
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (strcmp(sdp->o_line->address_type, "IP4")) {
		ast_test_status_update(test, "Unexpected address family '%s'\n", sdp->o_line->address_type);
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (strcmp(sdp->o_line->address, "host.atlanta.example.com")) {
		ast_test_status_update(test, "Unexpected address '%s'\n", sdp->o_line->address);
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	if (!verify_s_line(sdp->s_line, " ")) {
		ast_test_status_update(test, "Bad s line\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_c_line(sdp->c_line, "IP4", "host.atlanta.example.com")) {
		ast_test_status_update(test, "Bad c line\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_t_line(sdp->t_line, 123, 456)) {
		ast_test_status_update(test, "Bad t line\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	if (!verify_m_line(sdp, 0, "audio", 49170, 1, "RTP/AVP", "0", "8", "97", NULL)) {
		ast_test_status_update(test, "Bad m line 1\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_a_line(sdp, 0, 0, "rtpmap", "0 PCMU/8000")) {
		ast_test_status_update(test, "Bad a line 1\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_a_line(sdp, 0, 1, "rtpmap", "8 PCMA/8000")) {
		ast_test_status_update(test, "Bad a line 2\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_a_line(sdp, 0, 2, "rtpmap", "97 iLBC/8000")) {
		ast_test_status_update(test, "Bad a line 3\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_a_line(sdp, 0, 3, "sendrecv", "")) {
		ast_test_status_update(test, "Bad a line 3\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_m_line(sdp, 1, "video", 51372, 1, "RTP/AVP", "31", "32", NULL)) {
		ast_test_status_update(test, "Bad m line 2\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_a_line(sdp, 1, 0, "rtpmap", "31 H261/90000")) {
		ast_test_status_update(test, "Bad a line 4\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_a_line(sdp, 1, 1, "rtpmap", "32 MPV/90000")) {
		ast_test_status_update(test, "Bad a line 5\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

cleanup:
	ast_sdp_free(sdp);
	ast_sdp_translator_free(translator);
	pj_pool_release(pool);
	return res;
}

AST_TEST_DEFINE(sdp_to_pjmedia_test)
{
	struct ast_sdp_translator *translator;
	char *sdp_str =
      "v=0\r\n"
      "o=alice 2890844526 2890844526 IN IP4 host.atlanta.example.com\r\n"
      "s= \r\n"
      "c=IN IP4 host.atlanta.example.com\r\n"
      "t=123 456\r\n"
      "m=audio 49170 RTP/AVP 0 8 97\r\n"
      "a=rtpmap:0 PCMU/8000\r\n"
      "a=rtpmap:8 PCMA/8000\r\n"
      "a=rtpmap:97 iLBC/8000\r\n"
	  "a=sendrecv\r\n"
      "m=video 51372 RTP/AVP 31 32\r\n"
      "a=rtpmap:31 H261/90000\r\n"
      "a=rtpmap:32 MPV/90000\r\n\r\n";
	pj_pool_t *pool;
	pjmedia_sdp_session *pjmedia_sdp_orig;
	const pjmedia_sdp_session *pjmedia_sdp_dup;
	struct ast_sdp *sdp = NULL;
	pj_status_t status;
	enum ast_test_result_state res = AST_TEST_PASS;
	char buf[2048];
	char errbuf[256];

	switch (cmd) {
	case TEST_INIT:
		info->name = "sdp_to_pjmedia";
		info->category = "/main/sdp/";
		info->summary = "SDP to PJMEDIA unit test";
		info->description =
			"Ensures PJMEDIA SDPs are translated correctly";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	pool = pj_pool_create(&sdp_caching_pool.factory, "pjmedia to sdp test", 1024, 1024, NULL);

	translator = ast_sdp_translator_new(AST_SDP_IMPL_PJMEDIA);
	if (!translator) {
		ast_test_status_update(test, "Failed to create SDP translator\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	status = pjmedia_sdp_parse(pool, sdp_str, strlen(sdp_str), &pjmedia_sdp_orig);
	if (status != PJ_SUCCESS) {
		ast_test_status_update(test, "Error parsing SDP\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	sdp = ast_sdp_translator_to_sdp(translator, pjmedia_sdp_orig);
	pjmedia_sdp_dup = ast_sdp_translator_from_sdp(translator, sdp);

	if ((status = pjmedia_sdp_session_cmp(pjmedia_sdp_orig, pjmedia_sdp_dup, 0)) != PJ_SUCCESS) {
		ast_test_status_update(test, "SDPs aren't equal\n");
		pjmedia_sdp_print(pjmedia_sdp_orig, buf, sizeof(buf));
		ast_test_status_update(test, "Original SDP is %s\n", buf);
		pjmedia_sdp_print(pjmedia_sdp_dup, buf, sizeof(buf));
		ast_test_status_update(test, "New SDP is %s\n", buf);
		pjmedia_strerror(status, errbuf, sizeof(errbuf));
		ast_test_status_update(test, "PJMEDIA says %d: '%s'\n", status, errbuf);
		res = AST_TEST_FAIL;
		goto cleanup;
	}

cleanup:
	ast_sdp_free(sdp);
	ast_sdp_translator_free(translator);
	pj_pool_release(pool);
	return res;
}

#endif /* TEST_FRAMEWORK */

static int load_module(void)
{
	if (ast_sdp_register_translator(&pjmedia_translator)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	pj_caching_pool_init(&sdp_caching_pool, NULL, 1024 * 1024);
	AST_TEST_REGISTER(pjmedia_to_sdp_test);
	AST_TEST_REGISTER(sdp_to_pjmedia_test);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sdp_unregister_translator(&pjmedia_translator);
	pj_caching_pool_destroy(&sdp_caching_pool);
	AST_TEST_UNREGISTER(pjmedia_to_sdp_test);
	AST_TEST_UNREGISTER(sdp_to_pjmedia_test);
	return 0;
}

static int reload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJMEDIA SDP Translator",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
