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
#include "asterisk/sdp_priv.h"
#include "asterisk/utils.h"

struct ast_sdp *ast_sdp_alloc(void)
{
	struct ast_sdp *new_sdp;

	new_sdp = ast_calloc(1, sizeof *new_sdp);
	return new_sdp;
}

static void free_o_line(struct ast_sdp *dead)
{
	ast_free(dead->o_line.user);
	ast_free(dead->o_line.family);
	ast_free(dead->o_line.addr);
}

static void free_s_line(struct ast_sdp *dead)
{
	ast_free(dead->s_line);
}

static void free_c_line(struct ast_sdp_c_line *c_line)
{
	ast_free(c_line->family);
	ast_free(c_line->addr);
}

static void free_t_line(struct ast_sdp_t_line *t_line)
{
	return;
}

static void free_a_line(struct ast_sdp_a_line *a_line)
{
	ast_free(a_line->name);
	ast_free(a_line->value);
}

static void free_a_lines(struct ast_sdp_a_line_vector *a_lines)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(a_lines); ++i) {
		free_a_line(AST_VECTOR_GET_ADDR(a_lines, i));
	}
	AST_VECTOR_FREE(a_lines);
}

static void free_m_line(struct ast_sdp_m_line *m_line)
{
	int i;

	ast_free(m_line->type);
	ast_free(m_line->profile);
	free_c_line(&m_line->c_line);

	for (i = 0; i < AST_VECTOR_SIZE(&m_line->payloads); ++i) {
		ast_free(AST_VECTOR_GET(&m_line->payloads, i));
	}
	AST_VECTOR_FREE(&m_line->payloads);

	free_a_lines(&m_line->a_lines);
}

static void free_m_lines(struct ast_sdp *dead)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&dead->m_lines); ++i) {
		free_m_line(AST_VECTOR_GET_ADDR(&dead->m_lines, i));
	}

	AST_VECTOR_FREE(&dead->m_lines);
}

void ast_sdp_free(struct ast_sdp *dead)
{
	if (!dead) {
		return;
	}

	free_o_line(dead);
	free_s_line(dead);
	free_c_line(&dead->c_line);
	free_t_line(&dead->t_line);
	free_a_lines(&dead->a_lines);
	free_m_lines(dead);
	ast_free(dead);
}

