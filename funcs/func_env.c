/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Environment related dialplan functions
 * 
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

static int env_read(struct ast_channel *chan, const char *cmd, char *data,
		    char *buf, size_t len)
{
	char *ret = NULL;

	*buf = '\0';

	if (data)
		ret = getenv(data);

	if (ret)
		ast_copy_string(buf, ret, len);

	return 0;
}

static int env_write(struct ast_channel *chan, const char *cmd, char *data,
		     const char *value)
{
	if (!ast_strlen_zero(data)) {
		if (!ast_strlen_zero(value)) {
			setenv(data, value, 1);
		} else {
			unsetenv(data);
		}
	}

	return 0;
}

static int stat_read(struct ast_channel *chan, const char *cmd, char *data,
		     char *buf, size_t len)
{
	char *action;
	struct stat s;

	ast_copy_string(buf, "0", len);

	action = strsep(&data, ",");
	if (stat(data, &s)) {
		return 0;
	} else {
		switch (*action) {
		case 'e':
			strcpy(buf, "1");
			break;
		case 's':
			snprintf(buf, len, "%d", (unsigned int) s.st_size);
			break;
		case 'f':
			snprintf(buf, len, "%d", S_ISREG(s.st_mode) ? 1 : 0);
			break;
		case 'd':
			snprintf(buf, len, "%d", S_ISDIR(s.st_mode) ? 1 : 0);
			break;
		case 'M':
			snprintf(buf, len, "%d", (int) s.st_mtime);
			break;
		case 'A':
			snprintf(buf, len, "%d", (int) s.st_mtime);
			break;
		case 'C':
			snprintf(buf, len, "%d", (int) s.st_ctime);
			break;
		case 'm':
			snprintf(buf, len, "%o", (int) s.st_mode);
			break;
		}
	}

	return 0;
}

static int file_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(offset);
		AST_APP_ARG(length);
	);
	int offset = 0, length;
	char *contents;

	AST_STANDARD_APP_ARGS(args, data);
	if (args.argc > 1)
		offset = atoi(args.offset);

	if (args.argc > 2) {
		if ((length = atoi(args.length)) < 1) {
			ast_log(LOG_WARNING, "Invalid length '%s'.  Returning the max (%d)\n", args.length, (int)len);
			length = len;
		} else if (length > len) {
			ast_log(LOG_WARNING, "Length %d is greater than the max (%d).  Truncating output.\n", length, (int)len);
			length = len;
		}
	} else
		length = len;

	if (!(contents = ast_read_textfile(args.filename)))
		return -1;

	if (offset >= 0)
		ast_copy_string(buf, &contents[offset], length);
	else {
		size_t tmp = strlen(contents);
		if (offset * -1 > tmp) {
			ast_log(LOG_WARNING, "Offset is larger than the file size.\n");
			offset = tmp * -1;
		}
		ast_copy_string(buf, &contents[tmp + offset], length);
	}
	ast_free(contents);

	return 0;
}

static struct ast_custom_function env_function = {
	.name = "ENV",
	.synopsis = "Gets or sets the environment variable specified",
	.syntax = "ENV(<envname>)",
	.read = env_read,
	.write = env_write,
};

static struct ast_custom_function stat_function = {
	.name = "STAT",
	.synopsis = "Does a check on the specified file",
	.syntax = "STAT(<flag>,<filename>)",
	.read = stat_read,
	.desc =
		"flag may be one of the following:\n"
		"  d - Checks if the file is a directory\n"
		"  e - Checks if the file exists\n"
		"  f - Checks if the file is a regular file\n"
		"  m - Returns the file mode (in octal)\n"
		"  s - Returns the size (in bytes) of the file\n"
		"  A - Returns the epoch at which the file was last accessed\n"
		"  C - Returns the epoch at which the inode was last changed\n"
		"  M - Returns the epoch at which the file was last modified\n",
};

static struct ast_custom_function file_function = {
	.name = "FILE",
	.synopsis = "Obtains the contents of a file",
	.syntax = "FILE(<filename>,<offset>,<length>)",
	.read = file_read,
	/*
	 * Some enterprising programmer could probably add write functionality
	 * to FILE(), although I'm not sure how useful it would be.  Hence why
	 * it's called FILE and not READFILE (like the app was).
	 */
	.desc =
"<offset> may be specified as any number.  If negative, <offset> specifies\n"
"    the number of bytes back from the end of the file.\n"
"<length>, if specified, will limit the length of the data read to that size.\n",
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&env_function);
	res |= ast_custom_function_unregister(&stat_function);
	res |= ast_custom_function_unregister(&file_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&env_function);
	res |= ast_custom_function_register(&stat_function);
	res |= ast_custom_function_register(&file_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Environment/filesystem dialplan functions");
