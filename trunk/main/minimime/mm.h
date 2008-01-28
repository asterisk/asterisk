/*
 * $Id$
 *
 * MiniMIME - a library for handling MIME messages
 *
 * Copyright (C) 2003 Jann Fischer <rezine@mistrust.net>
 * All rights reserved.
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
 * 3. Neither the name of the author nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JANN FISCHER AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL JANN FISCHER OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MM_H_INCLUDED
#define _MM_H_INCLUDED

#include "asterisk.h"
#include "mm_queue.h"
#include "mm_mem.h"

#define MM_MIME_LINELEN 998
#define MM_BASE64_LINELEN 76

TAILQ_HEAD(mm_mimeheaders, mm_mimeheader);
TAILQ_HEAD(mm_mimeparts, mm_mimepart);
TAILQ_HEAD(mm_params, mm_param);
SLIST_HEAD(mm_codecs, mm_codec);
SLIST_HEAD(mm_warnings, mm_warning);

/*
 * Parser modes
 */
enum mm_parsemodes
{
	/** Parse loosely, accept some MIME quirks */
	MM_PARSE_LOOSE = 0,
	/** Parse as strict as possible */
	MM_PARSE_STRICT
};

/*
 * Available parser flags
 */
enum mm_parseflags
{
	MM_PARSE_NONE = (1L << 0),
	MM_PARSE_STRIPCOMMENTS = (1L << 1)
};

/*
 * Enumeration of MIME encodings
 */
enum mm_encoding
{
	MM_ENCODING_NONE = 0,
	MM_ENCODING_BASE64,
	MM_ENCODING_QUOTEDPRINTABLE,
	MM_ENCODING_UNKNOWN
};

/*
 * Message type
 */
enum mm_messagetype
{
	/** Flat message */
	MM_MSGTYPE_FLAT = 0,
	/** Composite message */
	MM_MSGTYPE_MULTIPART
};

/*
 * Enumeration of error categories
 */
enum mm_errors
{
	MM_ERROR_NONE = 0,
	MM_ERROR_UNDEF,
	MM_ERROR_ERRNO,	
	MM_ERROR_PARSE,		
	MM_ERROR_MIME,
	MM_ERROR_CODEC,
	MM_ERROR_PROGRAM
};

enum mm_warning_ids
{
	MM_WARN_NONE = 0,
	MM_WARN_PARSE,
	MM_WARN_MIME,
	MM_WARN_CODEC
};

enum mm_addressfields {
	MM_ADDR_TO = 0,
	MM_ADDR_CC,
	MM_ADDR_BCC,
	MM_ADDR_FROM,
	MM_ADDR_SENDER,
	MM_ADDR_REPLY_TO
};

enum mm_flatten_flags {
	MM_FLATTEN_NONE = 0,
	MM_FLATTEN_SKIPENVELOPE = (1L << 1),
	MM_FLATTEN_OPAQUE = (1L << 2),
	MM_FLATTEN_NOPREAMBLE = (1L << 3)
};	

/*
 * More information about an error
 */
struct mm_error_data
{
	int error_id;
	int error_where;
	int lineno;
	char error_msg[128];
};

extern int mm_errno;
extern struct mm_error_data mm_error;

enum mm_warning_code
{
	MM_WARNING_NONE = 0,
	MM_WARNING_INVHDR,
};

/*
 * A parser warning
 */
struct mm_warning
{
	enum mm_warning_code warning;
	uint32_t lineno;
	SLIST_ENTRY(mm_warning) next;
};

/*
 * Representation of a MiniMIME codec object
 */
struct mm_codec
{
	enum mm_encoding id;
	char *encoding;

	char *(*encoder)(char *, uint32_t);
	char *(*decoder)(char *);

	SLIST_ENTRY(mm_codec) next;
};

/*
 * Representation of a MIME Content-Type parameter
 */
struct mm_param
{
	char *name; 
	char *value; 

	TAILQ_ENTRY(mm_param) next;
};

/*
 * Representation of a mail or MIME header field
 */
struct mm_mimeheader
{
	char *name; 
	char *value;

	struct mm_params params;

	TAILQ_ENTRY(mm_mimeheader) next;
};

/*
 * Representation of a MIME Content-Type object
 */
struct mm_content
{
	char *maintype;
	char *subtype;
	char *disposition_type;

	struct mm_params type_params;
	struct mm_params disposition_params;

	char *encstring;
	enum mm_encoding encoding;
};

/*
 * Representation of a MIME part 
 */
struct mm_mimepart
{
	struct mm_mimeheaders headers;
	
	size_t opaque_length;
	char *opaque_body;

	size_t length;
	char *body;

	struct mm_content *type;

	TAILQ_ENTRY(mm_mimepart) next;
};

/*
 * Represantation of a MiniMIME context
 */
struct mm_context
{
	struct mm_mimeparts parts;
	enum mm_messagetype messagetype;
	struct mm_warnings warnings;
	struct mm_codecs codecs;
	char *boundary;
	char *preamble;
	size_t max_message_size;
};

typedef struct mm_context MM_CTX;
typedef struct mm_context mm_ctx_t;

char *mm_unquote(const char *);
char *mm_uncomment(const char *);
char *mm_stripchars(char *, char *);
char *mm_addchars(char *, char *, uint16_t);
int mm_gendate(char **);
void mm_striptrailing(char **, const char *);
int mm_mimeutil_genboundary(char *, size_t, char **);

int mm_library_init(void);
int mm_library_isinitialized(void);

int mm_parse_mem(MM_CTX *, const char *, int, int);
int mm_parse_file(MM_CTX *, const char *, int, int);
int mm_parse_fileptr(MM_CTX *, FILE *, int, int);

MM_CTX *mm_context_new(void);
void mm_context_free(MM_CTX *);
int mm_context_attachpart(MM_CTX *, struct mm_mimepart *);
int mm_context_deletepart(MM_CTX *, int, int);
int mm_context_countparts(MM_CTX *);
struct mm_mimepart *mm_context_getpart(MM_CTX *, int);
int mm_context_iscomposite(MM_CTX *);
int mm_context_haswarnings(MM_CTX *);
int mm_context_flatten(MM_CTX *, char **, size_t *, int);

int mm_envelope_getheaders(MM_CTX *, char **, size_t *);
int mm_envelope_setheader(MM_CTX *, const char *, const char *, ...);

struct mm_mimeheader *mm_mimeheader_new(void);
void mm_mimeheader_free(struct mm_mimeheader *);
struct mm_mimeheader *mm_mimeheader_generate(const char *, const char *);
int mm_mimeheader_uncomment(struct mm_mimeheader *);
int mm_mimeheader_uncommentbyname(struct mm_mimepart *, const char *);
int mm_mimeheader_uncommentall(struct mm_mimepart *);
int mm_mimeheader_tostring(struct mm_mimeheader *);
char *mm_mimeheader_getparambyname(struct mm_mimeheader *hdr, const char *name);
int mm_mimeheader_attachparam(struct mm_mimeheader *hdr, struct mm_param *param);

struct mm_mimepart *mm_mimepart_new(void);
void mm_mimepart_free(struct mm_mimepart *);
int mm_mimepart_attachheader(struct mm_mimepart *, struct mm_mimeheader *);
int mm_mimepart_countheaders(struct mm_mimepart *part);
int mm_mimepart_countheaderbyname(struct mm_mimepart *, const char *);
struct mm_mimeheader *mm_mimepart_getheaderbyname(struct mm_mimepart *, const char *, int);
const char *mm_mimepart_getheadervalue(struct mm_mimepart *, const char *, int);
int mm_mimepart_headers_start(struct mm_mimepart *, struct mm_mimeheader **);
struct mm_mimeheader *mm_mimepart_headers_next(struct mm_mimepart *, struct mm_mimeheader **);
char *mm_mimepart_decode(struct mm_mimepart *);
struct mm_content *mm_mimepart_getcontent(struct mm_mimepart *);
size_t mm_mimepart_getlength(struct mm_mimepart *);
char *mm_mimepart_getbody(struct mm_mimepart *, int);
void mm_mimepart_attachcontenttype(struct mm_mimepart *, struct mm_content *);
int mm_mimepart_setdefaultcontenttype(struct mm_mimepart *, int);
int mm_mimepart_flatten(struct mm_mimepart *, char **, size_t *, int);
struct mm_mimepart *mm_mimepart_fromfile(const char *);

struct mm_content *mm_content_new(void);
void mm_content_free(struct mm_content *);
int mm_content_attachtypeparam(struct mm_content *, struct mm_param *);
int mm_content_attachdispositionparam(struct mm_content *, struct mm_param *);
struct mm_content *mm_content_parse(const char *, int);
char *mm_content_gettypeparambyname(struct mm_content *, const char *);
char *mm_content_getdispositionparambyname(struct mm_content *, const char *);
struct mm_param *mm_content_gettypeparamobjbyname(struct mm_content *, const char *);
struct mm_param *mm_content_getdispositionparamobjbyname(struct mm_content *, const char *);
int mm_content_setmaintype(struct mm_content *, char *, int);
int mm_content_setsubtype(struct mm_content *, char *, int);
int mm_content_settype(struct mm_content *, const char *, ...);
int mm_content_setdispositiontype(struct mm_content *ct, char *value, int copy);
char *mm_content_getmaintype(struct mm_content *);
char *mm_content_getsubtype(struct mm_content *);
char *mm_content_gettype(struct mm_content *);
char *mm_content_getdispositiontype(struct mm_content *ct);
int mm_content_iscomposite(struct mm_content *);
int mm_content_isvalidencoding(const char *);
int mm_content_setencoding(struct mm_content *, const char *);
char *mm_content_typeparamstostring(struct mm_content *);
char *mm_content_dispositionparamstostring(struct mm_content *);
char *mm_content_tostring(struct mm_content *);

struct mm_param *mm_param_new(void);
void mm_param_free(struct mm_param *);

char *mm_flatten_mimepart(struct mm_mimepart *);
char *mm_flatten_context(MM_CTX *);

int mm_codec_isregistered(const char *);
int mm_codec_hasdecoder(const char *);
int mm_codec_hasencoder(const char *);
int mm_codec_register(const char *, char *(*encoder)(char *, uint32_t), char *(*decoder)(char *));
int mm_codec_unregister(const char *);
int mm_codec_unregisterall(void);
void mm_codec_registerdefaultcodecs(void);

char *mm_base64_decode(char *);
char *mm_base64_encode(char *, uint32_t);

void mm_error_init(void);
void mm_error_setmsg(const char *, ...);
void mm_error_setlineno(int lineno);
char *mm_error_string(void);
int mm_error_lineno(void);

void mm_warning_add(MM_CTX *, int, const char *, ...);
struct mm_warning *mm_warning_next(MM_CTX *, struct mm_warning **);

#ifndef HAVE_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif /* ! HAVE_STRLCPY */
#ifndef HAVE_STRLCAT
size_t strlcat(char *, const char *, size_t);
#endif /* ! HAVE_STRLCAT */

#define MM_ISINIT() do { \
	assert(mm_library_isinitialized() == 1); \
} while (0);

#endif /* ! _MM_H_INCLUDED */
