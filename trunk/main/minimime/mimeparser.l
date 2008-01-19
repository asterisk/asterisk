%{
/*
 * Copyright (c) 2004 Jann Fischer. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * This is a lexer file for parsing MIME compatible messages. It is intended
 * to satisfy at least RFC 2045 (Format of Internet Message Bodies). It still
 * has quite a few problems:
 *
 *	- The parsing could probably be done in a more elegant way
 *	- I don't know what performance impact REJECT has on the parser
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "mimeparser.h"
#include "mimeparser.tab.h"

#define NAMEOF(v) #v
/* BC() is a debug wrapper for lex' BEGIN() macro */
#define BC(x) do { \
	struct lexer_state *lstate = yyget_extra(yyscanner); \
	BEGIN(x); \
	lstate->condition = x; \
} while(0);

#define ZERO(x) memset(x, '\0', sizeof(x))

#define PREALLOC_BUFFER	100000
#undef YY_BUF_SIZE
#define YY_BUF_SIZE 65536

enum header_states
{
	STATE_MAIL = 0,
	STATE_CTYPE,
	STATE_CDISP,
	STATE_CENC,
	STATE_MIME
};



%}

%option reentrant
%option yylineno
%option bison-bridge

%s headers
%s header
%s headervalue
%s tspecialvalue
%s comment
%s body
%s postamble
%s preamble
%s boundary
%s endboundary
%s endoffile

STRING	[a-zA-Z0-9\-\.\_]
TSPECIAL [a-zA-Z0-9)(<>@,;:/\-.=_\+'? ]
TSPECIAL_LITE [a-zA-Z0-9)(<>@,-._+'?\[\]]

%%

<INITIAL,headers>^[a-zA-Z]+[a-zA-Z0-9\-\_]* {
	struct lexer_state *lstate = yyget_extra(yyscanner);

	yylval_param->string=strdup(yytext); 
	lstate->current_pos += yyleng;
	BC(header);

	/* Depending on what header we are processing, we enter a different
	 * state and return a different value.
	 */
	if (!strcasecmp(yytext, "Content-Type")) {
		lstate->header_state = STATE_CTYPE;
		return CONTENTTYPE_HEADER;
	} else if (!strcasecmp(yytext, "Content-Transfer-Encoding")) {
		lstate->header_state = STATE_CENC;
		return CONTENTENCODING_HEADER;
	} else if (!strcasecmp(yytext, "Content-Disposition")) {
		lstate->header_state = STATE_CDISP;
		return CONTENTDISPOSITION_HEADER;
	} else if (!strcasecmp(yytext, "MIME-Version")) {
		lstate->header_state = STATE_MAIL;
		return MIMEVERSION_HEADER;
	} else {
		lstate->header_state = STATE_MAIL;
		return MAIL_HEADER;
	}
}

<INITIAL,headers>. {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	/* dprintf2("Unknown header char: %c\n", *yytext); */
	lstate->current_pos += yyleng;
	return ANY;
}

<headers>^(\r\n|\n) {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->lineno++; 

	lstate->current_pos += yyleng;

	/* This marks the end of headers. Depending on whether we are in the
	 * envelope currently we need to parse either a body or the preamble
	 * now.
	 */
	if (lstate->is_envelope == 0 || lstate->boundary_string == NULL) {
		BC(body);
		lstate->body_start = lstate->current_pos;
	} else {
		lstate->is_envelope = 0;
		lstate->preamble_start = lstate->current_pos;
		BC(preamble);
	}	

	return ENDOFHEADERS;
}

<header>\: {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	BC(headervalue); 
	lstate->current_pos += yyleng;
	return COLON;
}	

<header>(\r\n|\n) {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	BC(headers);
	/* dprintf2("Invalid header, returning EOL\n"); */
	lstate->current_pos += yyleng;
	return EOL;
}	

<headervalue>(\n|\r\n)[\ \t]+	{
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->current_pos += yyleng;
}

<headervalue>.+|(.+(\n|\r\n)[\ \t]+.+)+ {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	if (lstate->header_state != STATE_MAIL && lstate->header_state != STATE_CENC) {
		REJECT;
	}
	lstate->current_pos += yyleng;
	while (*yytext && isspace(*yytext)) yytext++;
	/* Do we actually have a header value? */
	if (*yytext == '\0') {
		yylval_param->string = strdup("");
	} else {
		yylval_param->string=strdup(yytext); 
		lstate->lineno += count_lines(yytext);
	}	
	return WORD;
}

<headervalue,tspecialvalue>(\r\n|\n) {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	/* marks the end of one header line */
	lstate->lineno++;
	BC(headers);
	lstate->current_pos += yyleng;
	return EOL;
}

<headervalue>;|;(\r\n|\n)[\ \t]+ {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->lineno += count_lines(yytext);
	lstate->current_pos += yyleng;
	return SEMICOLON;
}

<headervalue>\= {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->current_pos += yyleng;
	return EQUAL;
}

<headervalue>\" {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	BC(tspecialvalue);
	lstate->current_pos += yyleng;
	return *yytext;
}

<headervalue>{STRING}+|{TSPECIAL_LITE}+ {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	yylval_param->string=strdup(yytext);
	lstate->lineno += count_lines(yytext);
	lstate->current_pos += yyleng;
	return WORD;
}

<headervalue>[\ |\t]+	{
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->current_pos += yyleng;
}	

<tspecialvalue>{TSPECIAL}+ {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->lineno += count_lines(yytext);
	yylval_param->string=strdup(yytext);
	lstate->current_pos += yyleng;
	return TSPECIAL;
}

<tspecialvalue>\" {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	BC(headervalue);
	lstate->current_pos += yyleng;
	return *yytext;
}

<body>^\-\-{TSPECIAL}+\-\- {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	/**
	 * Make sure we only catch matching boundaries, and not other lines
	 * that begin and end with two dashes. If we have catched a valid
	 * end boundary, which actually ends a body, we save the current
	 * position, put the token back on the input stream and let the
	 * endboundary condition parse the actual token.
	 */
	if (lstate->endboundary_string != NULL) {
		if (strcmp(lstate->endboundary_string, yytext)) {
			/* dprintf2("YYTEXT != end_boundary: '%s'\n", yytext); */
			REJECT;
		} else {
			lstate->current_pos += yyleng;
			/* dprintf2("YYTEXT == lstate->end_boundary: '%s'\n", yytext); */
			if (lstate->body_start) {
				yylval_param->position.opaque_start = 
				    lstate->body_opaque_start;
				yylval_param->position.start = lstate->body_start;
				yylval_param->position.end = lstate->current_pos - yyleng;
				lstate->body_opaque_start = 0;
				lstate->body_start = 0;
				lstate->body_end = 0;
				yyless(0);
				BC(endboundary);
				return BODY;
			}	
		}
	} else {
	}	

	REJECT;
}

<body,preamble>^\-\-{TSPECIAL}+ {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	/**
	 * Make sure we only catch matching boundaries, and not other lines
	 * that begin with two dashes.
	 */
	if (lstate->boundary_string != NULL) {
		if (strcmp(lstate->boundary_string, yytext)) {
			/* dprintf2("YYTEXT != boundary: '%s'\n", yytext);*/
			REJECT;
		} else {
			/* dprintf2("YYTEXT == boundary: '%s'\n", yytext);*/
			if (lstate->body_start) {
				yylval_param->position.opaque_start = lstate->body_opaque_start;
				yylval_param->position.start = lstate->body_start;
				yylval_param->position.end = lstate->current_pos;
				lstate->body_opaque_start = 0;
				lstate->body_start = 0;
				lstate->body_end = 0;
				yyless(0);
				BC(boundary);
				return BODY;
			} else if (lstate->preamble_start) {
				yylval_param->position.start = lstate->preamble_start;
				yylval_param->position.end = lstate->current_pos;
				lstate->preamble_start = lstate->preamble_end = 0;
				yyless(0);
				BC(boundary);
				return PREAMBLE;
			} else {
				BC(boundary);
				yylval_param->string = strdup(yytext);
				lstate->current_pos += yyleng;
				return(BOUNDARY);
			}
		}
	} else {
	}	

	REJECT;
}

<body>(\r\n|\n) {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->current_pos += yyleng;
	lstate->lineno++;
}

<body>\r {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->current_pos += yyleng;
	/* dprintf2("stray CR in body...\n"); */
}

<body>[^\r\n]+ {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->current_pos += yyleng;
}

<body><<EOF>> {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	if (lstate->boundary_string == NULL && lstate->body_start) {
		yylval_param->position.opaque_start = 0;
		yylval_param->position.start = lstate->body_start;
		yylval_param->position.end = lstate->current_pos;
		lstate->body_start = 0;
		return BODY;
	} else if (lstate->body_start) {
		return POSTAMBLE;
	}	
	yyterminate();
}	

<preamble,postamble>(\r\n|\n) {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	/* dprintf2("Preamble CR/LF at line %d\n", lineno); */
	lstate->lineno++; 
	lstate->current_pos += yyleng;
}	

<boundary>[^\r\n]+ {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	yylval_param->string = strdup(yytext);
	lstate->current_pos += yyleng;
	return BOUNDARY;
}

<endboundary>[^\r\n]+ {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	yylval_param->string = strdup(yytext);
	lstate->current_pos += yyleng;
	return ENDBOUNDARY;
}

<boundary>(\r\n|\n) {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	BC(headers);
	lstate->lineno++;
	lstate->current_pos += yyleng;
	lstate->body_opaque_start = lstate->current_pos;
	return EOL;
}

<endboundary>(\r\n|\n) {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	BC(postamble);
	lstate->lineno++;
	lstate->current_pos += yyleng;
}

<preamble>. {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->current_pos += yyleng;
}


<postamble>. {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->current_pos += yyleng;
}

(\r\n|\n) {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->lineno++;
	lstate->current_pos += yyleng;
	return EOL;
}

. {
	struct lexer_state *lstate = yyget_extra(yyscanner);
	lstate->current_pos += yyleng;
	return((int)*yytext);
}


%%

void reset_lexer_state(void *yyscanner, struct parser_state *pstate)
{
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	struct lexer_state *lstate = &(pstate->lstate);

	yyset_extra((void*)lstate, yyscanner);
	BEGIN(0);
	lstate->header_state = STATE_MAIL;
	lstate->lineno = 0;
	lstate->current_pos = 1;
	lstate->condition = 0;

	lstate->is_envelope = 1;

	lstate->message_len = 0;
	lstate->buffer_length = 0;

	/* temporary marker variables */
	lstate->body_opaque_start = 0;
	lstate->body_start = 0;
	lstate->body_end = 0;
	lstate->preamble_start = 0;
	lstate->preamble_end = 0;
	lstate->postamble_start = 0;
	lstate->postamble_end = 0;
}

void
PARSER_setbuffer(const char *string, yyscan_t scanner)
{
	struct lexer_state *lstate = yyget_extra(scanner);
	lstate->message_buffer = string;
	yy_scan_string(string, scanner);
}

void
PARSER_setfp(FILE *fp, yyscan_t scanner)
{
	/* looks like a bug in bison 2.2a -- the wrong code is generated for yyset_in !! */
	struct yyguts_t * yyg = (struct yyguts_t*) scanner;
	yyg->yyin_r = fp;
	
	if (0) {
		/* This is just to make a compiler warning go away */
		yyunput(0, NULL, scanner);
	}
}

/**
 * Counts how many lines a given string represents in the message (in case of
 * folded header values, for example, or a message body).
 */
int
count_lines(char *txt)
{
	char *o;
	int line;

	line = 0;

	for (o = txt; *o != '\0'; o++)	
		if (*o == '\n')
			line++;

	return line;
}
