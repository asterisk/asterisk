/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright 2004 - 2017,
 *
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
 *
 * \brief JSON parser Implementation for dialplan
 *
 * \author Sebastian Gutierrez Maeso <scgm11@gmail.com>
 * 
 * \ingroup applications
 */

//
/*** MODULEINFO
	<defaultenabled>yes</defaultenabled>
	<support_level>core</support_level>
 ***/

#include <asterisk.h>

#include <asterisk/module.h>
#include <asterisk/app.h>
#include <asterisk/pbx.h>
#include <asterisk/channel.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*** DOCUMENTATION
	<application name="JSON" language="en_US">
		<synopsis>
			
		</synopsis>
		<syntax>
		Example:
		exten=> _XXXX,1,Set(json={"Guid": 1 , "Screen": false , "Form": "Codigos" , "Campaign" : "Entrada" , "Callerid" : "${CALLERID(num)}"  , "ParAndValues" : "par1=val1-par2=val2-par3=val3" , "Beep" : "TRUE"})
		exten=> _XXXX,2,JSON(${json})
		exten=> _XXXX,3,NoOp(${Guid})
		exten=> _XXXX,4,NoOp(${Form})
		exten=> _XXXX,5,NoOp(${Beep})	</syntax>
		<description>
		<para>Retrieves data from JSON Strings.</para>
		</description>
	</application>
 ***/

/**
 * JSON type identifier. Basic types are:
 * 	o Object
 * 	o Array
 * 	o String
 * 	o Other primitive: number, boolean (true/false) or null
 */
typedef enum {
    JSMN_PRIMITIVE = 0,
    JSMN_OBJECT = 1,
    JSMN_ARRAY = 2,
    JSMN_STRING = 3
} jsmntype_t;

typedef enum {
    /* Not enough tokens were provided */
    JSMN_ERROR_NOMEM = -1,
    /* Invalid character inside JSON string */
    JSMN_ERROR_INVAL = -2,
    /* The string is not a full JSON packet, more bytes expected */
    JSMN_ERROR_PART = -3
} jsmnerr_t;

/**
 * JSON token description.
 * @param		type	type (object, array, string etc.)
 * @param		start	start position in JSON data string
 * @param		end		end position in JSON data string
 */
typedef struct
{
    jsmntype_t type;
    int start;
    int end;
    int size;
    int parent;
} jsmntok_t;

/**
 * JSON parser. Contains an array of token blocks available. Also stores
 * the string being parsed now and current position in that string
 */
typedef struct
{
    unsigned int pos;     /* offset in the JSON string */
    unsigned int toknext; /* next token to allocate */
    int toksuper;	 /* superior token node, e.g parent object or array */
} jsmn_parser;

/**
 * Create JSON parser over an array of tokens
 */
void jsmn_init(jsmn_parser *parser);

/**
 * Run JSON parser. It parses a JSON data string into and array of tokens, each describing
 * a single JSON object.
 */
jsmnerr_t jsmn_parse(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens);

//Asterisk App
static char *app = "JSON";

//Asterisk execution
static int execute(struct ast_channel *chan, const char *data)
{

    jsmn_parser parser;
	jsmntok_t tokens[256];
	char *substrk;
    int i = 0;

    char *parse;
    AST_DECLARE_APP_ARGS(args,
			 AST_APP_ARG(jsonstring););

    //Check for arguments
    if (ast_strlen_zero(data))
    {
	ast_log(LOG_WARNING, "JSON requires arguments (JSON String)\n");
	return -1;
    }

    parse = ast_strdupa(data);

    AST_STANDARD_APP_ARGS(args, parse);

    //Check for all needed arguments
    if (ast_strlen_zero(args.jsonstring))
    {
	ast_log(LOG_WARNING, "Missing argument to JSON (JSON String)\n");
	return -1;
    }




    jsmn_init(&parser);

    // js - pointer to JSON string
    // tokens - an array of tokens available
    // 256 - number of tokens available
    int tks = jsmn_parse(&parser, args.jsonstring, strlen(args.jsonstring), tokens, 256);

    if (tks < 0)
    {
	ast_verb(9, "ERROR Parsing JSON String");
    }

    ast_verb(9, "JSON Tokens: %d\n", tks);


    for (i = 1; i < tks; i++)
    {

		int l = tokens[i].end - tokens[i].start;

		if (i % 2 != 0)
		{

			//IS KEY

			substrk = (char *)ast_malloc(l + 1);
			strncpy(substrk, args.jsonstring + tokens[i].start, l);
			*(substrk + l) = 0;
		}
		else
		{

			//IS VALUE

			char *substrv = (char *)ast_malloc(l + 1);
			strncpy(substrv, args.jsonstring + tokens[i].start, l);
			*(substrv + l) = 0;

			pbx_builtin_setvar_helper(chan, substrk, substrv);
			ast_verb(9, "Variable: %s Value: %s\n", substrk, substrv);

			ast_free(substrk);
			ast_free(substrv);
		}
    }

    return 0;
}

/**
 * Allocates a fresh unused token from the token pull.
 */
static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser, jsmntok_t *tokens, size_t num_tokens)
{
    jsmntok_t *tok;
    if (parser->toknext >= num_tokens)
    {
	return NULL;
    }
    tok = &tokens[parser->toknext++];
    tok->start = tok->end = -1;
    tok->size = 0;
#ifdef JSMN_PARENT_LINKS
    tok->parent = -1;
#endif
    return tok;
}

/**
 * Fills token type and boundaries.
 */
static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type, int start, int end)
{
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

/**
 * Fills next available token with JSON primitive.
 */
static jsmnerr_t jsmn_parse_primitive(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, size_t num_tokens)
{
    jsmntok_t *token;
    int start;

    start = parser->pos;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++)
    {
	switch (js[parser->pos])
	{
#ifndef JSMN_STRICT
	/* In strict mode primitive must be followed by "," or "}" or "]" */
	case ':':
#endif
	case '\t':
	case '\r':
	case '\n':
	case ' ':
	case ',':
	case ']':
	case '}':
	    goto found;
	}
	if (js[parser->pos] < 32 || js[parser->pos] >= 127)
	{
	    parser->pos = start;
	    return JSMN_ERROR_INVAL;
	}
    }
#ifdef JSMN_STRICT
    /* In strict mode primitive must be followed by a comma/object/array */
    parser->pos = start;
    return JSMN_ERROR_PART;
#endif

found:
    if (tokens == NULL)
    {
	parser->pos--;
	return 0;
    }
    token = jsmn_alloc_token(parser, tokens, num_tokens);
    if (token == NULL)
    {
	parser->pos = start;
	return JSMN_ERROR_NOMEM;
    }
    jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
    token->parent = parser->toksuper;
    parser->pos--;
    return 0;
}

/**
 * Filsl next token with JSON string.
 */
static jsmnerr_t jsmn_parse_string(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, size_t num_tokens)
{
    jsmntok_t *token;

    int start = parser->pos;

    parser->pos++;

    /* Skip starting quote */
    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++)
    {
	char c = js[parser->pos];

	/* Quote: end of string */
	if (c == '\"')
	{
	    if (tokens == NULL)
	    {
		return 0;
	    }
	    token = jsmn_alloc_token(parser, tokens, num_tokens);
	    if (token == NULL)
	    {
		parser->pos = start;
		return JSMN_ERROR_NOMEM;
	    }
	    jsmn_fill_token(token, JSMN_STRING, start + 1, parser->pos);
	    token->parent = parser->toksuper;
	    return 0;
	}

	/* Backslash: Quoted symbol expected */
	if (c == '\\' && parser->pos + 1 < len)
	{
	    int i;
	    parser->pos++;
	    switch (js[parser->pos])
	    {
	    /* Allowed escaped symbols */
	    case '\"':
	    case '/':
	    case '\\':
	    case 'b':
	    case 'f':
	    case 'r':
	    case 'n':
	    case 't':
		break;
	    /* Allows escaped symbol \uXXXX */
	    case 'u':
		parser->pos++;
		for (i = 0; i < 4 && parser->pos < len && js[parser->pos] != '\0'; i++)
		{
		    /* If it isn't a hex character we have an error */
		    if (!((js[parser->pos] >= 48 && js[parser->pos] <= 57) || /* 0-9 */
			  (js[parser->pos] >= 65 && js[parser->pos] <= 70) || /* A-F */
			  (js[parser->pos] >= 97 && js[parser->pos] <= 102)))
		    { /* a-f */
			parser->pos = start;
			return JSMN_ERROR_INVAL;
		    }
		    parser->pos++;
		}
		parser->pos--;
		break;
	    /* Unexpected symbol */
	    default:
		parser->pos = start;
		return JSMN_ERROR_INVAL;
	    }
	}
    }
    parser->pos = start;
    return JSMN_ERROR_PART;
}

/**
 * Parse JSON string and fill tokens.
 */
jsmnerr_t jsmn_parse(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens)
{
    jsmnerr_t r;
    int i;
    jsmntok_t *token;
    int count = 0;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++)
    {
	char c;
	jsmntype_t type;

	c = js[parser->pos];
	switch (c)
	{
	case '{':
	case '[':
	    count++;
	    if (tokens == NULL)
	    {
		break;
	    }
	    token = jsmn_alloc_token(parser, tokens, num_tokens);
	    if (token == NULL)
		return JSMN_ERROR_NOMEM;
	    if (parser->toksuper != -1)
	    {
		tokens[parser->toksuper].size++;
		token->parent = parser->toksuper;
	    }
	    token->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
	    token->start = parser->pos;
	    parser->toksuper = parser->toknext - 1;
	    break;
	case '}':
	case ']':
	    if (tokens == NULL)
		break;
	    type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
	    if (parser->toknext < 1)
	    {
		return JSMN_ERROR_INVAL;
	    }
	    token = &tokens[parser->toknext - 1];
	    for (;;)
	    {
		if (token->start != -1 && token->end == -1)
		{
		    if (token->type != type)
		    {
			return JSMN_ERROR_INVAL;
		    }
		    token->end = parser->pos + 1;
		    parser->toksuper = token->parent;
		    break;
		}
		if (token->parent == -1)
		{
		    break;
		}
		token = &tokens[token->parent];
	    }
	    break;
	case '\"':
	    r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
	    if (r < 0)
		return r;
	    count++;
	    if (parser->toksuper != -1 && tokens != NULL)
		tokens[parser->toksuper].size++;
	    break;
	case '\t':
	case '\r':
	case '\n':
	case ' ':
	    break;
	case ':':
	    parser->toksuper = parser->toknext - 1;
	    break;
	case ',':
	    if (tokens != NULL &&
		tokens[parser->toksuper].type != JSMN_ARRAY &&
		tokens[parser->toksuper].type != JSMN_OBJECT)
	    {
		parser->toksuper = tokens[parser->toksuper].parent;
	    }
	    break;
#ifdef JSMN_STRICT
	/* In strict mode primitives are: numbers and booleans */
	case '-':
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case 't':
	case 'f':
	case 'n':
	    /* And they must not be keys of the object */
	    if (tokens != NULL)
	    {
		jsmntok_t *t = &tokens[parser->toksuper];
		if (t->type == JSMN_OBJECT ||
		    (t->type == JSMN_STRING && t->size != 0))
		{
		    return JSMN_ERROR_INVAL;
		}
	    }
#else
	/* In non-strict mode every unquoted value is a primitive */
	default:
#endif
	    r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
	    if (r < 0)
		return r;
	    count++;
	    if (parser->toksuper != -1 && tokens != NULL)
		tokens[parser->toksuper].size++;
	    break;

#ifdef JSMN_STRICT
	/* Unexpected char in strict mode */
	default:
	    return JSMN_ERROR_INVAL;
#endif
	}
    }

    for (i = parser->toknext - 1; i >= 0; i--)
    {
	/* Unmatched opened object or array */
	if (tokens[i].start != -1 && tokens[i].end == -1)
	{
	    return JSMN_ERROR_PART;
	}
    }

    return count;
}

/**
 * Creates a new parser based over a given  buffer with an array of tokens
 * available.
 */
void jsmn_init(jsmn_parser *parser)
{
    parser->pos = 0;
    parser->toknext = 0;
    parser->toksuper = -1;
}

//Asterisk Load and Unload Methids
static int unload_module(void)
{

    return ast_unregister_application(app);
}

static int load_module(void)
{

    return ast_register_application_xml(app, execute) ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "JSON");
