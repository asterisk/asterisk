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
#include "asterisk/sdp_priv.h"
#include "asterisk/vector.h"
#include "asterisk/netsock2.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/test.h"
#include "asterisk/module.h"
#ifdef HAVE_PJPROJECT
#include <pjlib.h>
#include <pjmedia.h>
#endif

/*** MODULEINFO
	<depend>pjproject</depend>
	<support_level>core</support_level>
 ***/

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

static void copy_pj_str(char *dest, const pj_str_t *src, size_t size)
{
	memcpy(dest, pj_strbuf(src), size);
	dest[size] = '\0';
}

static void dup_pj_str(char **dest, const pj_str_t *src)
{
	*dest = ast_malloc(pj_strlen(src) + 1);
	copy_pj_str(*dest, src, pj_strlen(src));
}

static void pjmedia_copy_o_line(struct ast_sdp *new_sdp, struct pjmedia_sdp_session * pjmedia_sdp)
{
	dup_pj_str(&new_sdp->o_line.user, &pjmedia_sdp->origin.user);
	new_sdp->o_line.id = pjmedia_sdp->origin.id;
	new_sdp->o_line.version = pjmedia_sdp->origin.version;
	dup_pj_str(&new_sdp->o_line.family, &pjmedia_sdp->origin.addr_type);
	dup_pj_str(&new_sdp->o_line.addr, &pjmedia_sdp->origin.addr);
}

static void pjmedia_copy_s_line(struct ast_sdp *new_sdp, struct pjmedia_sdp_session *pjmedia_sdp)
{
	dup_pj_str(&new_sdp->s_line, &pjmedia_sdp->name);
}

static void pjmedia_copy_t_line(struct ast_sdp_t_line *new_t_line, struct pjmedia_sdp_session *pjmedia_sdp)
{
	new_t_line->start = pjmedia_sdp->time.start;
	new_t_line->end = pjmedia_sdp->time.stop;
}

static void pjmedia_copy_c_line(struct ast_sdp_c_line *new_c_line, struct pjmedia_sdp_conn *conn)
{
	/* It's perfectly reasonable for a c line not to be present, especially within a media description */
	if (!conn) {
		return;
	}

	dup_pj_str(&new_c_line->family, &conn->addr_type);
	dup_pj_str(&new_c_line->addr, &conn->addr);
}

static void pjmedia_copy_m_line(struct ast_sdp_m_line *new_m_line, struct pjmedia_sdp_media *pjmedia_m_line)
{
	int i;

	dup_pj_str(&new_m_line->type, &pjmedia_m_line->desc.media);
	new_m_line->port = pjmedia_m_line->desc.port;
	new_m_line->port_count = pjmedia_m_line->desc.port_count;
	dup_pj_str(&new_m_line->profile, &pjmedia_m_line->desc.transport);
	pjmedia_copy_c_line(&new_m_line->c_line, pjmedia_m_line->conn);

	AST_VECTOR_INIT(&new_m_line->payloads, pjmedia_m_line->desc.fmt_count);
	for (i = 0; i < pjmedia_m_line->desc.fmt_count; ++i) {
		++new_m_line->payloads.current;
		dup_pj_str(AST_VECTOR_GET_ADDR(&new_m_line->payloads, i), &pjmedia_m_line->desc.fmt[i]);
	}
}

static void pjmedia_copy_a_lines(struct ast_sdp_a_line_vector *new_a_lines, pjmedia_sdp_attr **attr, unsigned int attr_count)
{
	int i;

	AST_VECTOR_INIT(new_a_lines, attr_count);

	for (i = 0; i < attr_count; ++i) {
		struct ast_sdp_a_line *a_line;

		++new_a_lines->current;
		a_line = AST_VECTOR_GET_ADDR(new_a_lines, i);
		dup_pj_str(&a_line->name, &attr[i]->name);
		dup_pj_str(&a_line->value, &attr[i]->value);
	}
}

static void pjmedia_copy_m_lines(struct ast_sdp *new_sdp, struct pjmedia_sdp_session *pjmedia_sdp)
{
	int i;

	AST_VECTOR_INIT(&new_sdp->m_lines, pjmedia_sdp->media_count);

	for (i = 0; i < pjmedia_sdp->media_count; ++i) {
		++new_sdp->m_lines.current;

		pjmedia_copy_m_line(AST_VECTOR_GET_ADDR(&new_sdp->m_lines, i), pjmedia_sdp->media[i]);
		pjmedia_copy_a_lines(&AST_VECTOR_GET_ADDR(&new_sdp->m_lines, i)->a_lines, pjmedia_sdp->media[i]->attr, pjmedia_sdp->media[i]->attr_count);
	}
}

static struct ast_sdp *pjmedia_to_sdp(void *in, void *translator_priv)
{
	struct pjmedia_sdp_session *pjmedia_sdp = in;

	struct ast_sdp *new_sdp = ast_sdp_alloc();

	pjmedia_copy_o_line(new_sdp, pjmedia_sdp);
	pjmedia_copy_s_line(new_sdp, pjmedia_sdp);
	pjmedia_copy_t_line(&new_sdp->t_line, pjmedia_sdp);
	pjmedia_copy_c_line(&new_sdp->c_line, pjmedia_sdp->conn);
	pjmedia_copy_a_lines(&new_sdp->a_lines, pjmedia_sdp->attr, pjmedia_sdp->attr_count);
	pjmedia_copy_m_lines(new_sdp, pjmedia_sdp);

	return new_sdp;
}

static void copy_o_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp, struct ast_sdp *sdp)
{
	pjmedia_sdp->origin.id = sdp->o_line.id;
	pjmedia_sdp->origin.version = sdp->o_line.version;
	pj_strdup2(pool, &pjmedia_sdp->origin.user, sdp->o_line.user);
	pj_strdup2(pool, &pjmedia_sdp->origin.addr_type, sdp->o_line.family);
	pj_strdup2(pool, &pjmedia_sdp->origin.addr, sdp->o_line.addr);
	pj_strdup2(pool, &pjmedia_sdp->origin.net_type, "IN");
}

static void copy_s_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp, struct ast_sdp *sdp)
{
	pj_strdup2(pool, &pjmedia_sdp->name, sdp->s_line);
}

static void copy_t_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp, struct ast_sdp_t_line *t_line)
{
	pjmedia_sdp->time.start = t_line->start;
	pjmedia_sdp->time.stop = t_line->end;
}

static void copy_c_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_conn **conn, struct ast_sdp_c_line *c_line)
{
	pjmedia_sdp_conn *local_conn;
	local_conn = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_conn);
	pj_strdup2(pool, &local_conn->addr_type, c_line->family);
	pj_strdup2(pool, &local_conn->addr, c_line->addr);
	pj_strdup2(pool, &local_conn->net_type, "IN");

	*conn = local_conn;
}

static void copy_a_lines_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp, struct ast_sdp_a_line_vector *a_lines)
{
	int i;
	
	for (i = 0; i < AST_VECTOR_SIZE(a_lines); ++i) {
		pjmedia_sdp_attr *attr;
		pj_str_t value;

		pj_strdup2(pool, &value, AST_VECTOR_GET(a_lines, i).value);
		attr = pjmedia_sdp_attr_create(pool, AST_VECTOR_GET(a_lines, i).name, &value);
		pjmedia_sdp_session_add_attr(pjmedia_sdp, attr);
	}
}

static void copy_a_lines_pjmedia_media(pj_pool_t *pool, pjmedia_sdp_media *media, struct ast_sdp_a_line_vector *a_lines)
{
	int i;
	
	for (i = 0; i < AST_VECTOR_SIZE(a_lines); ++i) {
		pjmedia_sdp_attr *attr;
		pj_str_t value;

		pj_strdup2(pool, &value, AST_VECTOR_GET(a_lines, i).value);
		attr = pjmedia_sdp_attr_create(pool, AST_VECTOR_GET(a_lines, i).name, &value);
		pjmedia_sdp_media_add_attr(media, attr);
	}
}

static void copy_m_line_pjmedia(pj_pool_t *pool, pjmedia_sdp_media *media, struct ast_sdp_m_line *m_line)
{
	int i;

	media->desc.port = m_line->port;
	media->desc.port_count = m_line->port_count;
	pj_strdup2(pool, &media->desc.transport, m_line->profile);
	pj_strdup2(pool, &media->desc.media, m_line->type);

	for (i = 0; i < AST_VECTOR_SIZE(&m_line->payloads); ++i) {
		pj_strdup2(pool, &media->desc.fmt[i], AST_VECTOR_GET(&m_line->payloads, i));
		++media->desc.fmt_count;
	}
	if (m_line->c_line.addr) {
		copy_c_line_pjmedia(pool, &media->conn, &m_line->c_line);
	}
	copy_a_lines_pjmedia_media(pool, media, &m_line->a_lines);
}

static void copy_m_lines_pjmedia(pj_pool_t *pool, pjmedia_sdp_session *pjmedia_sdp, struct ast_sdp *sdp)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&sdp->m_lines); ++i) {
		pjmedia_sdp_media *media;

		media = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_media);
		copy_m_line_pjmedia(pool, media, AST_VECTOR_GET_ADDR(&sdp->m_lines, i));
		pjmedia_sdp->media[pjmedia_sdp->media_count] = media;
		++pjmedia_sdp->media_count;
	}
}

static void *sdp_to_pjmedia(struct ast_sdp *sdp, void *translator_priv)
{
	pj_pool_t *pool = translator_priv;
	pjmedia_sdp_session *pjmedia_sdp;

	pjmedia_sdp = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_session);
	copy_o_line_pjmedia(pool, pjmedia_sdp, sdp);
	copy_s_line_pjmedia(pool, pjmedia_sdp, sdp);
	copy_t_line_pjmedia(pool, pjmedia_sdp, &sdp->t_line);
	copy_c_line_pjmedia(pool, &pjmedia_sdp->conn, &sdp->c_line);
	copy_a_lines_pjmedia(pool, pjmedia_sdp, &sdp->a_lines);
	copy_m_lines_pjmedia(pool, pjmedia_sdp, sdp);
	return pjmedia_sdp;
}

static struct ast_sdp_translator_ops pjmedia_translator = {
	.repr = AST_SDP_REPR_PJMEDIA,
	.translator_new = pjmedia_new,
	.translator_free = pjmedia_free,
	.to_sdp = pjmedia_to_sdp,
	.from_sdp = sdp_to_pjmedia,
};

#ifdef TEST_FRAMEWORK

static int verify_s_line(char *s_line, char *expected)
{
	return strcmp(s_line, expected) == 0;
}

static int verify_c_line(struct ast_sdp_c_line *c_line, char *family, char *addr)
{
	return strcmp(c_line->family, family) == 0 && strcmp(c_line->addr, addr) == 0;
}

static int verify_t_line(struct ast_sdp_t_line *t_line, uint32_t start, uint32_t end)
{
	return t_line->start == start && t_line->end == end;
}

static int verify_m_line(struct ast_sdp *sdp, int index, char *type, int port, int port_count, char *profile, ...)
{
	struct ast_sdp_m_line *m_line;
	int res;
	va_list ap;
	int i;
	
	m_line = AST_VECTOR_GET_ADDR(&sdp->m_lines, index);

	res = strcmp(m_line->type, type) == 0;
	res |= m_line->port == port;
	res |= m_line->port_count == port_count;
	res |= strcmp(m_line->profile, profile) == 0;

	va_start(ap, profile);
	for (i = 0; i < AST_VECTOR_SIZE(&m_line->payloads); ++i) {
		char *payload;

		payload = va_arg(ap, char *);
		if (!payload) {
			res = -1;
			break;
		}
		res |= strcmp(AST_VECTOR_GET(&m_line->payloads, i), payload) == 0;
	}
	va_end(ap);
	return res;
}

static int verify_a_line(struct ast_sdp *sdp, int m_index, int a_index, char *name, char *value)
{
	struct ast_sdp_m_line *m_line;
	struct ast_sdp_a_line *a_line;

	m_line = AST_VECTOR_GET_ADDR(&sdp->m_lines, m_index);
	a_line = AST_VECTOR_GET_ADDR(&m_line->a_lines, a_index);

	return strcmp(a_line->name, name) == 0 && strcmp(a_line->value, value) == 0;
}

AST_TEST_DEFINE(pjmedia_to_sdp_test)
{
	struct ast_sdp_translator *translator;
	pj_pool_t *pool;
	char *sdp_str = 
      "v=0\r\n"
      "o=alice 2890844526 2890844526 IN IP4 host.atlanta.example.com\r\n"
      "s= \r\n"
      "c=IN IP4 host.atlanta.example.com\r\n"
      "t=0 0\r\n"
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

	translator = ast_sdp_translator_new(AST_SDP_REPR_PJMEDIA);
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
	
	if (strcmp(sdp->o_line.user, "alice")) {
		ast_test_status_update(test, "Unexpected SDP user '%s'\n", sdp->o_line.user);
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (sdp->o_line.id != 2890844526) {
		ast_test_status_update(test, "Unexpected SDP id '%u'\n", sdp->o_line.id);
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (sdp->o_line.version != 2890844526) {
		ast_test_status_update(test, "Unexpected SDP version '%u'\n", sdp->o_line.version);
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (strcmp(sdp->o_line.family, "IP4")) {
		ast_test_status_update(test, "Unexpected address family '%s'\n", sdp->o_line.family);
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (strcmp(sdp->o_line.addr, "host.atlanta.example.com")) {
		ast_test_status_update(test, "Unexpected address '%s'\n", sdp->o_line.addr);
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	if (!verify_s_line(sdp->s_line, " ")) {
		ast_test_status_update(test, "Bad s line\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_c_line(&sdp->c_line, "IP4", "host.atlanta.example.com")) {
		ast_test_status_update(test, "Bad c line\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	} else if (!verify_t_line(&sdp->t_line, 0, 0)) {
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
      "t=0 0\r\n"
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
	pjmedia_sdp_session *pjmedia_sdp_dup;
	struct ast_sdp *sdp = NULL;
	pj_status_t status;
	enum ast_test_result_state res = AST_TEST_PASS;

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

	translator = ast_sdp_translator_new(AST_SDP_REPR_PJMEDIA);
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
		char buf[2048];
		char errbuf[256];
		ast_test_status_update(test, "SDPs aren't equal\n");
		pjmedia_sdp_print(pjmedia_sdp_orig, buf, sizeof(buf));
		ast_log(LOG_NOTICE, "Original SDP is %s\n", buf);
		pjmedia_sdp_print(pjmedia_sdp_dup, buf, sizeof(buf));
		ast_log(LOG_NOTICE, "New SDP is %s\n", buf);
		pjmedia_strerror(status, errbuf, sizeof(errbuf));
		ast_log(LOG_NOTICE, "PJMEDIA says %d: '%s'\n", status, errbuf);
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
	AST_TEST_REGISTER(sdp_to_pjmedia_test);
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
