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
 * These are the grammatic definitions in yacc syntax to parse MIME conform
 * messages.
 *
 * TODO:
 *	- honour parse flags passed to us (partly done)
 *	- parse Content-Disposition header (partly done)
 *	- parse Content-Encoding header
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "mimeparser.h"
#include "mm.h"
#include "mm_internal.h"

int set_boundary(char *,struct parser_state *);
int mimeparser_yywrap(void);
void reset_environ(struct parser_state *pstate);
int PARSER_initialize(struct parser_state *pstate, void *yyscanner);

static char *PARSE_readmessagepart(size_t, size_t, size_t, size_t *,yyscan_t, struct parser_state *);
FILE *mimeparser_yyget_in (yyscan_t yyscanner );

%}

%pure-parser
%parse-param {struct parser_state *pstate}
%parse-param {void *yyscanner}
%lex-param {void *yyscanner}

%union
{
	int number;
	char *string;
	struct s_position position;
}

%token ANY
%token COLON 
%token DASH
%token DQUOTE
%token ENDOFHEADERS
%token EOL
%token EOM
%token EQUAL
%token MIMEVERSION_HEADER
%token SEMICOLON

%token <string> CONTENTDISPOSITION_HEADER
%token <string> CONTENTENCODING_HEADER
%token <string> CONTENTTYPE_HEADER
%token <string> MAIL_HEADER
%token <string> HEADERVALUE
%token <string> BOUNDARY
%token <string> ENDBOUNDARY
%token <string> CONTENTTYPE_VALUE 
%token <string> TSPECIAL
%token <string> WORD

%token <position> BODY
%token <position> PREAMBLE
%token <position> POSTAMBLE

%type  <string> content_disposition
%type  <string> contenttype_parameter_value
%type  <string> mimetype
%type  <string> body

%start message

%%

/* This is a parser for a MIME-conform message, which is in either single
 * part or multi part format.
 */
message : 
	multipart_message
	|
	singlepart_message
	;

multipart_message:
	headers preamble 
	{ 
		mm_context_attachpart(pstate->ctx, pstate->current_mimepart);
		pstate->current_mimepart = mm_mimepart_new();
		pstate->have_contenttype = 0;
	}
	mimeparts endboundary postamble
	{
		dprintf2(pstate,"This was a multipart message\n");
	}
	;

singlepart_message:	
	headers body
	{
		dprintf2(pstate,"This was a single part message\n");
		mm_context_attachpart(pstate->ctx, pstate->current_mimepart);
	}
	;
	
headers :
	header headers
	|
	end_headers
	{
		/* If we did not find a Content-Type header for the current
		 * MIME part (or envelope), we create one and attach it.
		 * According to the RFC, a type of "text/plain" and a
		 * charset of "us-ascii" can be assumed.
		 */
		struct mm_content *ct;
		struct mm_param *param;

		if (!pstate->have_contenttype) {
			ct = mm_content_new();
			mm_content_settype(ct, "text/plain");
			
			param = mm_param_new();
			param->name = xstrdup("charset");
			param->value = xstrdup("us-ascii");

			mm_content_attachtypeparam(ct, param);
			mm_mimepart_attachcontenttype(pstate->current_mimepart, ct);
		}	
		pstate->have_contenttype = 0;
	}
	|
	header
	;

preamble:
	PREAMBLE
	{
		char *preamble;
		size_t offset;
		
		if ($1.start != $1.end) {
			preamble = PARSE_readmessagepart(0, $1.start, $1.end,
			    &offset,yyscanner,pstate);
			if (preamble == NULL) {
				return(-1);
			}
			pstate->ctx->preamble = preamble;
			dprintf2(pstate,"PREAMBLE:\n%s\n", preamble);
		}
	}
	|
	;

postamble:
	POSTAMBLE
	{
	}
	|
	;

mimeparts:
	mimeparts mimepart
	|
	mimepart
	;

mimepart:
	boundary headers body
	{

		if (mm_context_attachpart(pstate->ctx, pstate->current_mimepart) == -1) {
			mm_errno = MM_ERROR_ERRNO;
			return(-1);
		}	

		pstate->temppart = mm_mimepart_new();
		pstate->current_mimepart = pstate->temppart;
		pstate->mime_parts++;
	}
	;
	
header	:
	mail_header
	|
	contenttype_header
	{
		pstate->have_contenttype = 1;
		if (mm_content_iscomposite(pstate->envelope->type)) {
			pstate->ctx->messagetype = MM_MSGTYPE_MULTIPART;
		} else {
			pstate->ctx->messagetype = MM_MSGTYPE_FLAT;
		}	
	}
	|
	contentdisposition_header
	|
	contentencoding_header
	|
	mimeversion_header
	|
	invalid_header
	{
		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("invalid header encountered");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}
	}
	;

mail_header:
	MAIL_HEADER COLON WORD EOL
	{
		struct mm_mimeheader *hdr;
		hdr = mm_mimeheader_generate($1, $3);
		mm_mimepart_attachheader(pstate->current_mimepart, hdr);
	}
	|
	MAIL_HEADER COLON EOL
	{
		struct mm_mimeheader *hdr;

		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_MIME;
			mm_error_setmsg("invalid header encountered");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}	
		
		hdr = mm_mimeheader_generate($1, xstrdup(""));
		mm_mimepart_attachheader(pstate->current_mimepart, hdr);
	}
	;

contenttype_header:
	CONTENTTYPE_HEADER COLON mimetype EOL
	{
		mm_content_settype(pstate->ctype, "%s", $3);
		mm_mimepart_attachcontenttype(pstate->current_mimepart, pstate->ctype);
		dprintf2(pstate,"Content-Type -> %s\n", $3);
		pstate->ctype = mm_content_new();
	}
	|
	CONTENTTYPE_HEADER COLON mimetype contenttype_parameters EOL
	{
		mm_content_settype(pstate->ctype, "%s", $3);
		mm_mimepart_attachcontenttype(pstate->current_mimepart, pstate->ctype);
		dprintf2(pstate,"Content-Type (P) -> %s\n", $3);
		pstate->ctype = mm_content_new();
	}
	;

contentdisposition_header:
	CONTENTDISPOSITION_HEADER COLON content_disposition EOL
	{
		dprintf2(pstate,"Content-Disposition -> %s\n", $3);
		pstate->ctype->disposition_type = xstrdup($3);
	}
	|
	CONTENTDISPOSITION_HEADER COLON content_disposition content_disposition_parameters EOL
	{
		dprintf2(pstate,"Content-Disposition (P) -> %s; params\n", $3);
		pstate->ctype->disposition_type = xstrdup($3);
	}
	;

content_disposition:
	WORD
	{
		/*
		 * According to RFC 2183, the content disposition value may
		 * only be "inline", "attachment" or an extension token. We
		 * catch invalid values here if we are not in loose parsing
		 * mode.
		 */
		if (strcasecmp($1, "inline") && strcasecmp($1, "attachment")
		    && strncasecmp($1, "X-", 2)) {
			if (pstate->parsemode != MM_PARSE_LOOSE) {
				mm_errno = MM_ERROR_MIME;
				mm_error_setmsg("invalid content-disposition");
				return(-1);
			}	
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}	
		$$ = $1;
	}
	;

contentencoding_header:
	CONTENTENCODING_HEADER COLON WORD EOL
	{
		dprintf2(pstate,"Content-Transfer-Encoding -> %s\n", $3);
	}
	;

mimeversion_header:
	MIMEVERSION_HEADER COLON WORD EOL
	{
		dprintf2(pstate,"MIME-Version -> '%s'\n", $3);
	}
	;

invalid_header:
	any EOL
	;

any:
	any ANY
	|
	ANY
	;
	
mimetype:
	WORD '/' WORD
	{
		char type[255];
		snprintf(type, sizeof(type), "%s/%s", $1, $3);
		$$ = type;
	}	
	;

contenttype_parameters: 
	SEMICOLON contenttype_parameter contenttype_parameters
	|
	SEMICOLON contenttype_parameter
	|
	SEMICOLON
	{
		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_MIME;
			mm_error_setmsg("invalid Content-Type header");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}	
	}
	;

content_disposition_parameters:
	SEMICOLON content_disposition_parameter content_disposition_parameters
	|
	SEMICOLON content_disposition_parameter
	|
	SEMICOLON
	{	
		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_MIME;
			mm_error_setmsg("invalid Content-Disposition header");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}
	}	
	;

contenttype_parameter:	
	WORD EQUAL contenttype_parameter_value
	{
		struct mm_param *param;
		param = mm_param_new();
		
		dprintf2(pstate,"Param: '%s', Value: '%s'\n", $1, $3);
		
		/* Catch an eventual boundary identifier */
		if (!strcasecmp($1, "boundary")) {
			if (pstate->lstate.boundary_string == NULL) {
				set_boundary($3,pstate);
			} else {
				if (pstate->parsemode != MM_PARSE_LOOSE) {
					mm_errno = MM_ERROR_MIME;
					mm_error_setmsg("duplicate boundary "
					    "found");
					return -1;
				} else {
					/* TODO: attach MM_WARNING_DUPPARAM */
				}
			}
		}

		param->name = xstrdup($1);
		param->value = xstrdup($3);

		mm_content_attachtypeparam(pstate->ctype, param);
	}
	;

content_disposition_parameter:
	WORD EQUAL contenttype_parameter_value
	{
		struct mm_param *param;
		param = mm_param_new();
		
		param->name = xstrdup($1);
		param->value = xstrdup($3);

		mm_content_attachdispositionparam(pstate->ctype, param);

	}
	;

contenttype_parameter_value:
	WORD
	{
		dprintf2(pstate,"contenttype_param_val: WORD=%s\n", $1);
		$$ = $1;
	}
	|
	TSPECIAL
	{
		dprintf2(pstate,"contenttype_param_val: TSPECIAL\n");
		/* For broken MIME implementation */
		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_MIME;
			mm_error_setmsg("tspecial without quotes");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVAL */
		}	
		$$ = $1;
	}
	|
	'"' TSPECIAL '"'
	{
		dprintf2(pstate,"contenttype_param_val: \"TSPECIAL\"\n" );
		$$ = $2;
	}
	;
	
end_headers	:
	ENDOFHEADERS
	{
		dprintf2(pstate,"End of headers at line %d\n", pstate->lstate.lineno);
	}
	;

boundary	:
	BOUNDARY EOL
	{
		if (pstate->lstate.boundary_string == NULL) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("internal incosistency");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		}
		if (strcmp(pstate->lstate.boundary_string, $1)) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("invalid boundary: '%s' (%d)", $1, strlen($1));
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		}
		dprintf2(pstate,"New MIME part... (%s)\n", $1);
	}
	;

endboundary	:
	ENDBOUNDARY
	{
		if (pstate->lstate.endboundary_string == NULL) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("internal incosistency");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		}
		if (strcmp(pstate->lstate.endboundary_string, $1)) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("invalid end boundary: %s", $1);
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		}
		dprintf2(pstate,"End of MIME message\n");
	}
	;

body:
	BODY
	{
		char *body;
		size_t offset;

		dprintf2(pstate,"BODY (%d/%d), SIZE %d\n", $1.start, $1.end, $1.end - $1.start);

		body = PARSE_readmessagepart($1.opaque_start, $1.start, $1.end,
		    &offset,yyscanner,pstate);

		if (body == NULL) {
			return(-1);
		}
		pstate->current_mimepart->opaque_body = body;
		pstate->current_mimepart->body = body + offset;
		pstate->current_mimepart->opaque_length = $1.end - $1.start - 2 + offset;
		pstate->current_mimepart->length = pstate->current_mimepart->opaque_length - offset;
	}
	;

%%

/*
 * This function gets the specified part from the currently parsed message.
 */
static char *
PARSE_readmessagepart(size_t opaque_start, size_t real_start, size_t end, 
    size_t *offset, yyscan_t yyscanner, struct parser_state *pstate)
{
	size_t body_size;
	size_t current;
	size_t start;
	char *body;

	/* calculate start and offset markers for the opaque and
	 * header stripped body message.
	 */
	if (opaque_start > 0) {
		/* Multipart message */
		if (real_start) {
			if (real_start < opaque_start) {
				mm_errno = MM_ERROR_PARSE;
				mm_error_setmsg("internal incosistency (S:%d/O:%d)",
				    real_start,
				    opaque_start);
				return(NULL);
			}
			start = opaque_start;
			*offset = real_start - start;
		/* Flat message */	
		} else {	
			start = opaque_start;
			*offset = 0;
		}	
	} else {
		start = real_start;
		*offset = 0;
	}

	/* The next three cases should NOT happen anytime */
	if (end <= start) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("internal incosistency,2");
		mm_error_setlineno(pstate->lstate.lineno);
		return(NULL);
	}
	if (start < *offset) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("internal incosistency, S:%d,O:%d,L:%d", start, offset, pstate->lstate.lineno);
		mm_error_setlineno(pstate->lstate.lineno);
		return(NULL);
	}	
	if (start < 0 || end < 0) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("internal incosistency,4");
		mm_error_setlineno(pstate->lstate.lineno);
		return(NULL);
	}	

	/* XXX: do we want to enforce a maximum body size? make it a
	 * parser option? */

	/* Read in the body message */
	body_size = end - start;

	if (body_size < 1) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("size of body cannot be < 1");
		mm_error_setlineno(pstate->lstate.lineno);
		return(NULL);
	}	
	
	body = (char *)malloc(body_size + 1);
	if (body == NULL) {
		mm_errno = MM_ERROR_ERRNO;
		return(NULL);
	}	
		
	/* Get the message body either from a stream or a memory
	 * buffer.
	 */
	if (mimeparser_yyget_in(yyscanner) != NULL) {
		FILE *x = mimeparser_yyget_in(yyscanner);
		current = ftell(x);
		fseek(x, start - 1, SEEK_SET);
		fread(body, body_size - 1, 1, x);
		fseek(x, current, SEEK_SET);
	} else if (pstate->lstate.message_buffer != NULL) {
		strlcpy(body, pstate->lstate.message_buffer + start - 1, body_size);
	} 
	
	return(body);

}

int
yyerror(struct parser_state *pstate, void *yyscanner, const char *str)
{
	mm_errno = MM_ERROR_PARSE;
	mm_error_setmsg("%s", str);
	mm_error_setlineno(pstate->lstate.lineno);
	return -1;
}

int 
mimeparser_yywrap(void)
{
	return 1;
}

/**
 * Sets the boundary value for the current message
 */
int 
set_boundary(char *str, struct parser_state *pstate)
{
	size_t blen;

	blen = strlen(str);

	pstate->lstate.boundary_string = (char *)malloc(blen + 3);
	pstate->lstate.endboundary_string = (char *)malloc(blen + 5);

	if (pstate->lstate.boundary_string == NULL || pstate->lstate.endboundary_string == NULL) {
		if (pstate->lstate.boundary_string != NULL) {
			free(pstate->lstate.boundary_string);
		}
		if (pstate->lstate.endboundary_string != NULL) {
			free(pstate->lstate.endboundary_string);
		}	
		return -1;
	}
	
	pstate->ctx->boundary = xstrdup(str);

	snprintf(pstate->lstate.boundary_string, blen + 3, "--%s", str);
	snprintf(pstate->lstate.endboundary_string, blen + 5, "--%s--", str);

	return 0;
}

/**
 * Debug printf()
 */
int
dprintf2(struct parser_state *pstate, const char *fmt, ...)
{
	va_list ap;
	char *msg;
	if (pstate->debug == 0) return 1;

	va_start(ap, fmt);
	vasprintf(&msg, fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s", msg);
	free(msg);

	return 0;
	
}

void reset_environ(struct parser_state *pstate)
{
	pstate->lstate.lineno = 0;
	pstate->lstate.boundary_string = NULL;
	pstate->lstate.endboundary_string = NULL;
	pstate->lstate.message_buffer = NULL;
	pstate->mime_parts = 0;
	pstate->debug = 0;
	pstate->envelope = NULL;
	pstate->temppart = NULL;
	pstate->ctype = NULL;
	pstate->current_mimepart = NULL;

	pstate->have_contenttype = 0;
}
/**
 * Initializes the parser engine.
 */
int
PARSER_initialize(struct parser_state *pstate, void *yyscanner)
{
	void reset_lexer_state(void *yyscanner, struct parser_state *);
#if 0
	if (pstate->ctx != NULL) {
		xfree(pstate->ctx);
		pstate->ctx = NULL;
	}
	if (pstate->envelope != NULL) {
		xfree(pstate->envelope);
		pstate->envelope = NULL;
	}	
	if (pstate->ctype != NULL) {
		xfree(pstate->ctype);
		pstate->ctype = NULL;
	}	
#endif
	/* yydebug = 1; */
	reset_environ(pstate);
	reset_lexer_state(yyscanner,pstate);

	pstate->envelope = mm_mimepart_new();
	pstate->current_mimepart = pstate->envelope;
	pstate->ctype = mm_content_new();

	pstate->have_contenttype = 0;

	return 1;
}


