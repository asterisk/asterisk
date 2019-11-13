/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2010, Digium, Inc.
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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <sys/stat.h>   /* stat(2) */

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/file.h"

/*** DOCUMENTATION
	<function name="ENV" language="en_US">
		<synopsis>
			Gets or sets the environment variable specified.
		</synopsis>
		<syntax>
			<parameter name="varname" required="true">
				<para>Environment variable name</para>
			</parameter>
		</syntax>
		<description>
			<para>Variables starting with <literal>AST_</literal> are reserved to the system and may not be set.</para>
		</description>
	</function>
	<function name="STAT" language="en_US">
		<synopsis>
			Does a check on the specified file.
		</synopsis>
		<syntax>
			<parameter name="flag" required="true">
				<para>Flag may be one of the following:</para>
				<para>d - Checks if the file is a directory.</para>
				<para>e - Checks if the file exists.</para>
				<para>f - Checks if the file is a regular file.</para>
				<para>m - Returns the file mode (in octal)</para>
				<para>s - Returns the size (in bytes) of the file</para>
				<para>A - Returns the epoch at which the file was last accessed.</para>
				<para>C - Returns the epoch at which the inode was last changed.</para>
				<para>M - Returns the epoch at which the file was last modified.</para>
			</parameter>
			<parameter name="filename" required="true" />
		</syntax>
		<description>
			<note>
				<para>If <literal>live_dangerously</literal> in <literal>asterisk.conf</literal>
				is set to <literal>no</literal>, this function can only be executed from the
				dialplan, and not directly from external protocols.</para>
			</note>
		</description>
	</function>
	<function name="FILE" language="en_US">
		<synopsis>
			Read or write text file.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true" />
			<parameter name="offset">
				<para>Maybe specified as any number. If negative, <replaceable>offset</replaceable> specifies the number
				of bytes back from the end of the file.</para>
			</parameter>
			<parameter name="length">
				<para>If specified, will limit the length of the data read to that size. If negative,
				trims <replaceable>length</replaceable> bytes from the end of the file.</para>
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="l">
						<para>Line mode:  offset and length are assumed to be
						measured in lines, instead of byte offsets.</para>
					</option>
					<option name="a">
						<para>In write mode only, the append option is used to
						append to the end of the file, instead of overwriting
						the existing file.</para>
					</option>
					<option name="d">
						<para>In write mode and line mode only, this option does
						not automatically append a newline string to the end of
						a value.  This is useful for deleting lines, instead of
						setting them to blank.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="format">
				<para>The <replaceable>format</replaceable> parameter may be
				used to delimit the type of line terminators in line mode.</para>
				<optionlist>
					<option name="u">
						<para>Unix newline format.</para>
					</option>
					<option name="d">
						<para>DOS newline format.</para>
					</option>
					<option name="m">
						<para>Macintosh newline format.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Read and write text file in character and line mode.</para>
			<para>Examples:</para>
			<para/>
			<para>Read mode (byte):</para>
			<para>    ;reads the entire content of the file.</para>
			<para>    Set(foo=${FILE(/tmp/test.txt)})</para>
			<para>    ;reads from the 11th byte to the end of the file (i.e. skips the first 10).</para>
			<para>    Set(foo=${FILE(/tmp/test.txt,10)})</para>
			<para>    ;reads from the 11th to 20th byte in the file (i.e. skip the first 10, then read 10 bytes).</para>
			<para>    Set(foo=${FILE(/tmp/test.txt,10,10)})</para>
			<para/>
			<para>Read mode (line):</para>
			<para>    ; reads the 3rd line of the file.</para>
			<para>    Set(foo=${FILE(/tmp/test.txt,3,1,l)})</para>
			<para>    ; reads the 3rd and 4th lines of the file.</para>
			<para>    Set(foo=${FILE(/tmp/test.txt,3,2,l)})</para>
			<para>    ; reads from the third line to the end of the file.</para>
			<para>    Set(foo=${FILE(/tmp/test.txt,3,,l)})</para>
			<para>    ; reads the last three lines of the file.</para>
			<para>    Set(foo=${FILE(/tmp/test.txt,-3,,l)})</para>
			<para>    ; reads the 3rd line of a DOS-formatted file.</para>
			<para>    Set(foo=${FILE(/tmp/test.txt,3,1,l,d)})</para>
			<para/>
			<para>Write mode (byte):</para>
			<para>    ; truncate the file and write "bar"</para>
			<para>    Set(FILE(/tmp/test.txt)=bar)</para>
			<para>    ; Append "bar"</para>
			<para>    Set(FILE(/tmp/test.txt,,,a)=bar)</para>
			<para>    ; Replace the first byte with "bar" (replaces 1 character with 3)</para>
			<para>    Set(FILE(/tmp/test.txt,0,1)=bar)</para>
			<para>    ; Replace 10 bytes beginning at the 21st byte of the file with "bar"</para>
			<para>    Set(FILE(/tmp/test.txt,20,10)=bar)</para>
			<para>    ; Replace all bytes from the 21st with "bar"</para>
			<para>    Set(FILE(/tmp/test.txt,20)=bar)</para>
			<para>    ; Insert "bar" after the 4th character</para>
			<para>    Set(FILE(/tmp/test.txt,4,0)=bar)</para>
			<para/>
			<para>Write mode (line):</para>
			<para>    ; Replace the first line of the file with "bar"</para>
			<para>    Set(FILE(/tmp/foo.txt,0,1,l)=bar)</para>
			<para>    ; Replace the last line of the file with "bar"</para>
			<para>    Set(FILE(/tmp/foo.txt,-1,,l)=bar)</para>
			<para>    ; Append "bar" to the file with a newline</para>
			<para>    Set(FILE(/tmp/foo.txt,,,al)=bar)</para>
			<note>
				<para>If <literal>live_dangerously</literal> in <literal>asterisk.conf</literal>
				is set to <literal>no</literal>, this function can only be executed from the
				dialplan, and not directly from external protocols.</para>
			</note>
		</description>
		<see-also>
			<ref type="function">FILE_COUNT_LINE</ref>
			<ref type="function">FILE_FORMAT</ref>
		</see-also>
	</function>
	<function name="FILE_COUNT_LINE" language="en_US">
		<synopsis>
			Obtains the number of lines of a text file.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true" />
			<parameter name="format">
				<para>Format may be one of the following:</para>
				<optionlist>
					<option name="u">
						<para>Unix newline format.</para>
					</option>
					<option name="d">
						<para>DOS newline format.</para>
					</option>
					<option name="m">
						<para>Macintosh newline format.</para>
					</option>
				</optionlist>
				<note><para>If not specified, an attempt will be made to determine the newline format type.</para></note>
			</parameter>
		</syntax>
		<description>
			<para>Returns the number of lines, or <literal>-1</literal> on error.</para>
			<note>
				<para>If <literal>live_dangerously</literal> in <literal>asterisk.conf</literal>
				is set to <literal>no</literal>, this function can only be executed from the
				dialplan, and not directly from external protocols.</para>
			</note>
		</description>
		<see-also>
			<ref type="function">FILE</ref>
			<ref type="function">FILE_FORMAT</ref>
		</see-also>
	</function>
	<function name="FILE_FORMAT" language="en_US">
		<synopsis>
			Return the newline format of a text file.
		</synopsis>
		<syntax>
			<parameter name="filename" required="true" />
		</syntax>
		<description>
			<para>Return the line terminator type:</para>
			<para>'u' - Unix "\n" format</para>
			<para>'d' - DOS "\r\n" format</para>
			<para>'m' - Macintosh "\r" format</para>
			<para>'x' - Cannot be determined</para>
			<note>
				<para>If <literal>live_dangerously</literal> in <literal>asterisk.conf</literal>
				is set to <literal>no</literal>, this function can only be executed from the
				dialplan, and not directly from external protocols.</para>
			</note>
		</description>
		<see-also>
			<ref type="function">FILE</ref>
			<ref type="function">FILE_COUNT_LINE</ref>
		</see-also>
	</function>
 ***/

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
	if (!ast_strlen_zero(data) && strncmp(data, "AST_", 4)) {
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
			snprintf(buf, len, "%u", (unsigned int) s.st_size);
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
			snprintf(buf, len, "%o", (unsigned int) s.st_mode);
			break;
		}
	}

	return 0;
}

enum file_format {
	FF_UNKNOWN = -1,
	FF_UNIX,
	FF_DOS,
	FF_MAC,
};

static int64_t count_lines(const char *filename, enum file_format newline_format)
{
	int count = 0;
	char fbuf[4096];
	FILE *ff;

	if (!(ff = fopen(filename, "r"))) {
		ast_log(LOG_ERROR, "Unable to open '%s': %s\n", filename, strerror(errno));
		return -1;
	}

	while (fgets(fbuf, sizeof(fbuf), ff)) {
		char *next = fbuf, *first_cr = NULL, *first_nl = NULL;

		/* Must do it this way, because if the fileformat is FF_MAC, then Unix
		 * assumptions about line-format will not come into play. */
		while (next) {
			if (newline_format == FF_DOS || newline_format == FF_MAC || newline_format == FF_UNKNOWN) {
				first_cr = strchr(next, '\r');
			}
			if (newline_format == FF_UNIX || newline_format == FF_UNKNOWN) {
				first_nl = strchr(next, '\n');
			}

			/* No terminators found in buffer */
			if (!first_cr && !first_nl) {
				break;
			}

			if (newline_format == FF_UNKNOWN) {
				if ((first_cr && !first_nl) || (first_cr && first_cr < first_nl)) {
					if (first_nl && first_nl == first_cr + 1) {
						newline_format = FF_DOS;
					} else if (first_cr && first_cr == &fbuf[sizeof(fbuf) - 2]) {
						/* Get it on the next pass */
						fseek(ff, -1, SEEK_CUR);
						break;
					} else {
						newline_format = FF_MAC;
						first_nl = NULL;
					}
				} else {
					newline_format = FF_UNIX;
					first_cr = NULL;
				}
				/* Jump down into next section */
			}

			if (newline_format == FF_DOS) {
				if (first_nl && first_cr && first_nl == first_cr + 1) {
					next = first_nl + 1;
					count++;
				} else if (first_cr == &fbuf[sizeof(fbuf) - 2]) {
					/* Get it on the next pass */
					fseek(ff, -1, SEEK_CUR);
					break;
				}
			} else if (newline_format == FF_MAC) {
				if (first_cr) {
					next = first_cr + 1;
					count++;
				}
			} else if (newline_format == FF_UNIX) {
				if (first_nl) {
					next = first_nl + 1;
					count++;
				}
			}
		}
	}
	fclose(ff);

	return count;
}

static int file_count_line(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	enum file_format newline_format = FF_UNKNOWN;
	int64_t count;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(format);
	);

	AST_STANDARD_APP_ARGS(args, data);
	if (args.argc > 1) {
		if (tolower(args.format[0]) == 'd') {
			newline_format = FF_DOS;
		} else if (tolower(args.format[0]) == 'm') {
			newline_format = FF_MAC;
		} else if (tolower(args.format[0]) == 'u') {
			newline_format = FF_UNIX;
		}
	}

	count = count_lines(args.filename, newline_format);
	ast_str_set(buf, len, "%" PRId64, count);
	return 0;
}

#define LINE_COUNTER(cptr, term, counter) \
	if (*cptr == '\n' && term == FF_UNIX) { \
		counter++; \
	} else if (*cptr == '\n' && term == FF_DOS && dos_state == 0) { \
		dos_state = 1; \
	} else if (*cptr == '\r' && term == FF_DOS && dos_state == 1) { \
		dos_state = 0; \
		counter++; \
	} else if (*cptr == '\r' && term == FF_MAC) { \
		counter++; \
	} else if (term == FF_DOS) { \
		dos_state = 0; \
	}

static enum file_format file2format(const char *filename)
{
	FILE *ff;
	char fbuf[4096];
	char *first_cr, *first_nl;
	enum file_format newline_format = FF_UNKNOWN;

	if (!(ff = fopen(filename, "r"))) {
		ast_log(LOG_ERROR, "Cannot open '%s': %s\n", filename, strerror(errno));
		return -1;
	}

	while (fgets(fbuf, sizeof(fbuf), ff)) {
		first_cr = strchr(fbuf, '\r');
		first_nl = strchr(fbuf, '\n');

		if (!first_cr && !first_nl) {
			continue;
		}

		if ((first_cr && !first_nl) || (first_cr && first_cr < first_nl)) {

			if (first_nl && first_nl == first_cr + 1) {
				newline_format = FF_DOS;
			} else if (first_cr && first_cr == &fbuf[sizeof(fbuf) - 2]) {
				/* Edge case: get it on the next pass */
				fseek(ff, -1, SEEK_CUR);
				continue;
			} else {
				newline_format = FF_MAC;
			}
		} else {
			newline_format = FF_UNIX;
		}
		break;
	}
	fclose(ff);
	return newline_format;
}

static int file_format(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	enum file_format newline_format = file2format(data);
	ast_str_set(buf, len, "%c", newline_format == FF_UNIX ? 'u' : newline_format == FF_DOS ? 'd' : newline_format == FF_MAC ? 'm' : 'x');
	return 0;
}

static int file_read(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	FILE *ff;
	int64_t offset = 0, length = LLONG_MAX;
	enum file_format format = FF_UNKNOWN;
	char fbuf[4096];
	int64_t flength, i; /* iterator needs to be signed, so it can go negative and terminate the loop */
	int64_t offset_offset = -1, length_offset = -1;
	char dos_state = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(offset);
		AST_APP_ARG(length);
		AST_APP_ARG(options);
		AST_APP_ARG(fileformat);
	);

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc > 1) {
		sscanf(args.offset, "%" SCNd64, &offset);
	}
	if (args.argc > 2) {
		sscanf(args.length, "%" SCNd64, &length);
	}

	if (args.argc < 4 || !strchr(args.options, 'l')) {
		/* Character-based mode */
		off_t off_i;

		if (!(ff = fopen(args.filename, "r"))) {
			ast_log(LOG_WARNING, "Cannot open file '%s' for reading: %s\n", args.filename, strerror(errno));
			return 0;
		}

		if (fseeko(ff, 0, SEEK_END) < 0) {
			ast_log(LOG_ERROR, "Cannot seek to end of '%s': %s\n", args.filename, strerror(errno));
			fclose(ff);
			return -1;
		}
		flength = ftello(ff);

		if (offset < 0) {
			fseeko(ff, offset, SEEK_END);
			if ((offset = ftello(ff)) < 0) {
				ast_log(AST_LOG_ERROR, "Cannot determine offset position of '%s': %s\n", args.filename, strerror(errno));
				fclose(ff);
				return -1;
			}
		}
		if (length < 0) {
			fseeko(ff, length, SEEK_END);
			if ((length = ftello(ff)) - offset < 0) {
				/* Eliminates all results */
				fclose(ff);
				return -1;
			}
		} else if (length == LLONG_MAX) {
			fseeko(ff, 0, SEEK_END);
			length = ftello(ff);
		}

		ast_str_reset(*buf);

		fseeko(ff, offset, SEEK_SET);
		for (off_i = ftello(ff); off_i < flength && off_i < offset + length; off_i += sizeof(fbuf)) {
			/* Calculate if we need to retrieve just a portion of the file in memory */
			size_t toappend = sizeof(fbuf);

			if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf) && !feof(ff)) {
				ast_log(LOG_ERROR, "Short read?!!\n");
				break;
			}

			/* Don't go past the length requested */
			if (off_i + toappend > offset + length) {
				toappend = MIN(offset + length - off_i, flength - off_i);
			}

			ast_str_append_substr(buf, len, fbuf, toappend);
		}
		fclose(ff);
		return 0;
	}

	/* Line-based read */
	if (args.argc == 5) {
		if (tolower(args.fileformat[0]) == 'd') {
			format = FF_DOS;
		} else if (tolower(args.fileformat[0]) == 'm') {
			format = FF_MAC;
		} else if (tolower(args.fileformat[0]) == 'u') {
			format = FF_UNIX;
		}
	}

	if (format == FF_UNKNOWN) {
		if ((format = file2format(args.filename)) == FF_UNKNOWN) {
			ast_log(LOG_WARNING, "'%s' is not a line-based file\n", args.filename);
			return -1;
		}
	}

	if (offset < 0 && length <= offset) {
		/* Length eliminates all content */
		return -1;
	} else if (offset == 0) {
		offset_offset = 0;
	}

	if (!(ff = fopen(args.filename, "r"))) {
		ast_log(LOG_ERROR, "Cannot open '%s': %s\n", args.filename, strerror(errno));
		return -1;
	}

	if (fseek(ff, 0, SEEK_END)) {
		ast_log(LOG_ERROR, "Cannot seek to end of file '%s': %s\n", args.filename, strerror(errno));
		fclose(ff);
		return -1;
	}

	flength = ftello(ff);

	if (length == LLONG_MAX) {
		length_offset = flength;
	}

	/* For negative offset and/or negative length */
	if (offset < 0 || length < 0) {
		int64_t count = 0;
		/* Start with an even multiple of fbuf, so at the end of reading with a
		 * 0 offset, we don't try to go past the beginning of the file. */
		for (i = (flength / sizeof(fbuf)) * sizeof(fbuf); i >= 0; i -= sizeof(fbuf)) {
			size_t end;
			char *pos;
			if (fseeko(ff, i, SEEK_SET)) {
				ast_log(LOG_ERROR, "Cannot seek to offset %" PRId64 ": %s\n", i, strerror(errno));
			}
			end = fread(fbuf, 1, sizeof(fbuf), ff);
			for (pos = (end < sizeof(fbuf) ? fbuf + end - 1 : fbuf + sizeof(fbuf) - 1); pos >= fbuf; pos--) {
				LINE_COUNTER(pos, format, count);

				if (length < 0 && count * -1 == length) {
					length_offset = i + (pos - fbuf);
				} else if (offset < 0 && count * -1 == (offset - 1)) {
					/* Found our initial offset.  We're done with reverse motion! */
					if (format == FF_DOS) {
						offset_offset = i + (pos - fbuf) + 2;
					} else {
						offset_offset = i + (pos - fbuf) + 1;
					}
					break;
				}
			}
			if ((offset < 0 && offset_offset >= 0) || (offset >= 0 && length_offset >= 0)) {
				break;
			}
		}
		/* We're at the beginning, and the negative offset indicates the exact number of lines in the file */
		if (offset < 0 && offset_offset < 0 && offset == count * -1) {
			offset_offset = 0;
		}
	}

	/* Positve line offset */
	if (offset > 0) {
		int64_t count = 0;
		fseek(ff, 0, SEEK_SET);
		for (i = 0; i < flength; i += sizeof(fbuf)) {
			char *pos;
			if (i + sizeof(fbuf) <= flength) {
				/* Don't let previous values influence current counts, due to short reads */
				memset(fbuf, 0, sizeof(fbuf));
			}
			if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf) && !feof(ff)) {
				ast_log(LOG_ERROR, "Short read?!!\n");
				fclose(ff);
				return -1;
			}
			for (pos = fbuf; pos < fbuf + sizeof(fbuf); pos++) {
				LINE_COUNTER(pos, format, count);

				if (count == offset) {
					offset_offset = i + (pos - fbuf) + 1;
					break;
				}
			}
			if (offset_offset >= 0) {
				break;
			}
		}
	}

	if (offset_offset < 0) {
		ast_log(LOG_ERROR, "Offset '%s' refers to before the beginning of the file!\n", args.offset);
		fclose(ff);
		return -1;
	}

	ast_str_reset(*buf);
	if (fseeko(ff, offset_offset, SEEK_SET)) {
		ast_log(LOG_ERROR, "fseeko failed: %s\n", strerror(errno));
	}

	/* If we have both offset_offset and length_offset, then grabbing the
	 * buffer is simply a matter of just retrieving the file and adding it
	 * to buf.  Otherwise, we need to run byte-by-byte forward until the
	 * length is complete. */
	if (length_offset >= 0) {
		ast_debug(3, "offset=%" PRId64 ", length=%" PRId64 ", offset_offset=%" PRId64 ", length_offset=%" PRId64 "\n", offset, length, offset_offset, length_offset);
		for (i = offset_offset; i < length_offset; i += sizeof(fbuf)) {
			if (fread(fbuf, 1, i + sizeof(fbuf) > flength ? flength - i : sizeof(fbuf), ff) < (i + sizeof(fbuf) > flength ? flength - i : sizeof(fbuf))) {
				ast_log(LOG_ERROR, "Short read?!!\n");
			}
			ast_debug(3, "Appending first %" PRId64" bytes of fbuf=%s\n", (int64_t)(i + sizeof(fbuf) > length_offset ? length_offset - i : sizeof(fbuf)), fbuf);
			ast_str_append_substr(buf, len, fbuf, i + sizeof(fbuf) > length_offset ? length_offset - i : sizeof(fbuf));
		}
	} else if (length == 0) {
		/* Nothing to do */
	} else {
		/* Positive line offset */
		int64_t current_length = 0;
		char dos_state = 0;
		ast_debug(3, "offset=%" PRId64 ", length=%" PRId64 ", offset_offset=%" PRId64 ", length_offset=%" PRId64 "\n", offset, length, offset_offset, length_offset);
		for (i = offset_offset; i < flength; i += sizeof(fbuf)) {
			char *pos;
			size_t bytes_read;
			if ((bytes_read = fread(fbuf, 1, sizeof(fbuf), ff)) < sizeof(fbuf) && !feof(ff)) {
				ast_log(LOG_ERROR, "Short read?!!\n");
				fclose(ff);
				return -1;
			}
			for (pos = fbuf; pos < fbuf + bytes_read; pos++) {
				LINE_COUNTER(pos, format, current_length);

				if (current_length == length) {
					length_offset = i + (pos - fbuf) + 1;
					break;
				}
			}
			ast_debug(3, "length_offset=%" PRId64 ", length_offset - i=%" PRId64 "\n", length_offset, length_offset - i);
			ast_str_append_substr(buf, len, fbuf, (length_offset >= 0) ? length_offset - i : (flength > i + sizeof(fbuf)) ? sizeof(fbuf) : flength - i);

			if (length_offset >= 0) {
				break;
			}
		}
	}

	fclose(ff);
	return 0;
}

const char *format2term(enum file_format f) __attribute__((const));
const char *format2term(enum file_format f)
{
	const char *term[] = { "", "\n", "\r\n", "\r" };
	return term[f + 1];
}

static int file_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(filename);
		AST_APP_ARG(offset);
		AST_APP_ARG(length);
		AST_APP_ARG(options);
		AST_APP_ARG(format);
	);
	int64_t offset = 0, length = LLONG_MAX;
	off_t flength, vlength;
	size_t foplen = 0;
	FILE *ff;

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc > 1) {
		sscanf(args.offset, "%" SCNd64, &offset);
	}
	if (args.argc > 2) {
		sscanf(args.length, "%" SCNd64, &length);
	}

	vlength = strlen(value);

	if (args.argc < 4 || !strchr(args.options, 'l')) {
		/* Character-based mode */

		if (args.argc > 3 && strchr(args.options, 'a')) {
			/* Append mode */
			if (!(ff = fopen(args.filename, "a"))) {
				ast_log(LOG_WARNING, "Cannot open file '%s' for appending: %s\n", args.filename, strerror(errno));
				return 0;
			}
			if (fwrite(value, 1, vlength, ff) < vlength) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			}
			fclose(ff);
			return 0;
		} else if (offset == 0 && length == LLONG_MAX) {
			if (!(ff = fopen(args.filename, "w"))) {
				ast_log(LOG_WARNING, "Cannot open file '%s' for writing: %s\n", args.filename, strerror(errno));
				return 0;
			}
			if (fwrite(value, 1, vlength, ff) < vlength) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			}
			fclose(ff);
			return 0;
		}

		if (!(ff = fopen(args.filename, "r+"))) {
			ast_log(LOG_WARNING, "Cannot open file '%s' for modification: %s\n", args.filename, strerror(errno));
			return 0;
		}
		fseeko(ff, 0, SEEK_END);
		flength = ftello(ff);

		if (offset < 0) {
			if (fseeko(ff, offset, SEEK_END)) {
				ast_log(LOG_ERROR, "Cannot seek to offset of '%s': %s\n", args.filename, strerror(errno));
				fclose(ff);
				return -1;
			}
			if ((offset = ftello(ff)) < 0) {
				ast_log(AST_LOG_ERROR, "Cannot determine offset position of '%s': %s\n", args.filename, strerror(errno));
				fclose(ff);
				return -1;
			}
		}

		if (length < 0) {
			length = flength - offset + length;
			if (length < 0) {
				ast_log(LOG_ERROR, "Length '%s' exceeds the file length.  No data will be written.\n", args.length);
				fclose(ff);
				return -1;
			}
		}

		fseeko(ff, offset, SEEK_SET);

		ast_debug(3, "offset=%s/%" PRId64 ", length=%s/%" PRId64 ", vlength=%" PRId64 ", flength=%" PRId64 "\n",
			S_OR(args.offset, "(null)"), offset, S_OR(args.length, "(null)"), length, vlength, flength);

		if (length == vlength) {
			/* Simplest case, a straight replace */
			if (fwrite(value, 1, vlength, ff) < vlength) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			}
			fclose(ff);
		} else if (length == LLONG_MAX) {
			/* Simple truncation */
			if (fwrite(value, 1, vlength, ff) < vlength) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			}
			fclose(ff);
			if (truncate(args.filename, offset + vlength)) {
				ast_log(LOG_ERROR, "Unable to truncate the file: %s\n", strerror(errno));
			}
		} else if (length > vlength) {
			/* More complex -- need to close a gap */
			char fbuf[4096];
			off_t cur;
			if (fwrite(value, 1, vlength, ff) < vlength) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			}
			fseeko(ff, length - vlength, SEEK_CUR);
			while ((cur = ftello(ff)) < flength) {
				if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf) && !feof(ff)) {
					ast_log(LOG_ERROR, "Short read?!!\n");
				}
				fseeko(ff, cur + vlength - length, SEEK_SET);
				if (fwrite(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf)) {
					ast_log(LOG_ERROR, "Short write?!!\n");
				}
				/* Seek to where we stopped reading */
				if (fseeko(ff, cur + sizeof(fbuf), SEEK_SET) < 0) {
					/* Only reason for seek to fail is EOF */
					break;
				}
			}
			fclose(ff);
			if (truncate(args.filename, flength - (length - vlength))) {
				ast_log(LOG_ERROR, "Unable to truncate the file: %s\n", strerror(errno));
			}
		} else {
			/* Most complex -- need to open a gap */
			char fbuf[4096];
			off_t lastwritten = flength + vlength - length;

			/* Start reading exactly the buffer size back from the end. */
			fseeko(ff, flength - sizeof(fbuf), SEEK_SET);
			while (offset < ftello(ff)) {
				if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf)) {
					ast_log(LOG_ERROR, "Short read?!!\n");
					fclose(ff);
					return -1;
				}
				/* Since the read moved our file ptr forward, we reverse, but
				 * seek an offset equal to the amount we want to extend the
				 * file by */
				fseeko(ff, vlength - length - sizeof(fbuf), SEEK_CUR);

				/* Note the location of this buffer -- we must not overwrite this position. */
				lastwritten = ftello(ff);

				if (fwrite(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf)) {
					ast_log(LOG_ERROR, "Short write?!!\n");
					fclose(ff);
					return -1;
				}

				if (lastwritten < offset + sizeof(fbuf)) {
					break;
				}
				/* Our file pointer is now either pointing to the end of the
				 * file (new position) or a multiple of the fbuf size back from
				 * that point.  Move back to where we want to start reading
				 * again.  We never actually try to read beyond the end of the
				 * file, so we don't have do deal with short reads, as we would
				 * when we're shortening the file. */
				fseeko(ff, 2 * sizeof(fbuf) + vlength - length, SEEK_CUR);
			}

			/* Last part of the file that we need to preserve */
			if (fseeko(ff, offset + length, SEEK_SET)) {
				ast_log(LOG_WARNING, "Unable to seek to %" PRId64 " + %" PRId64 " != %" PRId64 "?)\n", offset, length, ftello(ff));
			}

			/* Doesn't matter how much we read -- just need to restrict the write */
			ast_debug(1, "Reading at %" PRId64 "\n", ftello(ff));
			if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf) && !feof(ff)) {
				ast_log(LOG_ERROR, "Short read?!!\n");
			}
			fseek(ff, offset, SEEK_SET);
			/* Write out the value, then write just up until where we last moved some data */
			if (fwrite(value, 1, vlength, ff) < vlength) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			} else {
				off_t curpos = ftello(ff);
				foplen = lastwritten - curpos;
				if (fwrite(fbuf, 1, foplen, ff) < foplen) {
					ast_log(LOG_ERROR, "Short write?!!\n");
				}
			}
			fclose(ff);
		}
	} else {
		enum file_format newline_format = FF_UNKNOWN;

		/* Line mode */
		if (args.argc == 5) {
			if (tolower(args.format[0]) == 'u') {
				newline_format = FF_UNIX;
			} else if (tolower(args.format[0]) == 'm') {
				newline_format = FF_MAC;
			} else if (tolower(args.format[0]) == 'd') {
				newline_format = FF_DOS;
			}
		}
		if (newline_format == FF_UNKNOWN && (newline_format = file2format(args.filename)) == FF_UNKNOWN) {
			ast_log(LOG_ERROR, "File '%s' not in line format\n", args.filename);
			return -1;
		}

		if (strchr(args.options, 'a')) {
			/* Append to file */
			if (!(ff = fopen(args.filename, "a"))) {
				ast_log(LOG_ERROR, "Unable to open '%s' for appending: %s\n", args.filename, strerror(errno));
				return -1;
			}
			if (fwrite(value, 1, vlength, ff) < vlength) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			} else if (!strchr(args.options, 'd') && fwrite(format2term(newline_format), 1, strlen(format2term(newline_format)), ff) < strlen(format2term(newline_format))) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			}
			fclose(ff);
		} else if (offset == 0 && length == LLONG_MAX) {
			/* Overwrite file */
			off_t truncsize;
			if (!(ff = fopen(args.filename, "w"))) {
				ast_log(LOG_ERROR, "Unable to open '%s' for writing: %s\n", args.filename, strerror(errno));
				return -1;
			}
			if (fwrite(value, 1, vlength, ff) < vlength) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			} else if (!strchr(args.options, 'd') && fwrite(format2term(newline_format), 1, strlen(format2term(newline_format)), ff) < strlen(format2term(newline_format))) {
				ast_log(LOG_ERROR, "Short write?!!\n");
			}
			if ((truncsize = ftello(ff)) < 0) {
				ast_log(AST_LOG_ERROR, "Unable to determine truncate position of '%s': %s\n", args.filename, strerror(errno));
			}
			fclose(ff);
			if (truncsize >= 0 && truncate(args.filename, truncsize)) {
				ast_log(LOG_ERROR, "Unable to truncate file '%s': %s\n", args.filename, strerror(errno));
				return -1;
			}
		} else {
			int64_t offset_offset = (offset == 0 ? 0 : -1), length_offset = -1, flength, i, current_length = 0;
			char dos_state = 0, fbuf[4096];

			if (offset < 0 && length < offset) {
				/* Nonsense! */
				ast_log(LOG_ERROR, "Length cannot specify a position prior to the offset\n");
				return -1;
			}

			if (!(ff = fopen(args.filename, "r+"))) {
				ast_log(LOG_ERROR, "Cannot open '%s' for modification: %s\n", args.filename, strerror(errno));
				return -1;
			}

			if (fseek(ff, 0, SEEK_END)) {
				ast_log(LOG_ERROR, "Cannot seek to end of file '%s': %s\n", args.filename, strerror(errno));
				fclose(ff);
				return -1;
			}
			if ((flength = ftello(ff)) < 0) {
				ast_log(AST_LOG_ERROR, "Cannot determine end position of file '%s': %s\n", args.filename, strerror(errno));
				fclose(ff);
				return -1;
			}

			/* For negative offset and/or negative length */
			if (offset < 0 || length < 0) {
				int64_t count = 0;
				for (i = (flength / sizeof(fbuf)) * sizeof(fbuf); i >= 0; i -= sizeof(fbuf)) {
					char *pos;
					if (fseeko(ff, i, SEEK_SET)) {
						ast_log(LOG_ERROR, "Cannot seek to offset %" PRId64 ": %s\n", i, strerror(errno));
					}
					if (i + sizeof(fbuf) >= flength) {
						memset(fbuf, 0, sizeof(fbuf));
					}
					if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf) && !feof(ff)) {
						ast_log(LOG_ERROR, "Short read: %s\n", strerror(errno));
						fclose(ff);
						return -1;
					}
					for (pos = fbuf + sizeof(fbuf) - 1; pos >= fbuf; pos--) {
						LINE_COUNTER(pos, newline_format, count);

						if (length < 0 && count * -1 == length) {
							length_offset = i + (pos - fbuf);
						} else if (offset < 0 && count * -1 == (offset - 1)) {
							/* Found our initial offset.  We're done with reverse motion! */
							if (newline_format == FF_DOS) {
								offset_offset = i + (pos - fbuf) + 2;
							} else {
								offset_offset = i + (pos - fbuf) + 1;
							}
							break;
						}
					}
					if ((offset < 0 && offset_offset >= 0) || (offset >= 0 && length_offset >= 0)) {
						break;
					}
				}
				/* We're at the beginning, and the negative offset indicates the exact number of lines in the file */
				if (offset < 0 && offset_offset < 0 && offset == count * -1) {
					offset_offset = 0;
				}
			}

			/* Positve line offset */
			if (offset > 0) {
				int64_t count = 0;
				fseek(ff, 0, SEEK_SET);
				for (i = 0; i < flength; i += sizeof(fbuf)) {
					char *pos;
					if (i + sizeof(fbuf) >= flength) {
						memset(fbuf, 0, sizeof(fbuf));
					}
					if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf) && !feof(ff)) {
						ast_log(LOG_ERROR, "Short read?!!\n");
						fclose(ff);
						return -1;
					}
					for (pos = fbuf; pos < fbuf + sizeof(fbuf); pos++) {
						LINE_COUNTER(pos, newline_format, count);

						if (count == offset) {
							offset_offset = i + (pos - fbuf) + 1;
							break;
						}
					}
					if (offset_offset >= 0) {
						break;
					}
				}
			}

			if (offset_offset < 0) {
				ast_log(LOG_ERROR, "Offset '%s' refers to before the beginning of the file!\n", args.offset);
				fclose(ff);
				return -1;
			}

			if (length == 0) {
				length_offset = offset_offset;
			} else if (length == LLONG_MAX) {
				length_offset = flength;
			}

			/* Positive line length */
			if (length_offset < 0) {
				fseeko(ff, offset_offset, SEEK_SET);
				for (i = offset_offset; i < flength; i += sizeof(fbuf)) {
					char *pos;
					if (i + sizeof(fbuf) >= flength) {
						memset(fbuf, 0, sizeof(fbuf));
					}
					if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf) && !feof(ff)) {
						ast_log(LOG_ERROR, "Short read?!!\n");
						fclose(ff);
						return -1;
					}
					for (pos = fbuf; pos < fbuf + sizeof(fbuf); pos++) {
						LINE_COUNTER(pos, newline_format, current_length);

						if (current_length == length) {
							length_offset = i + (pos - fbuf) + 1;
							break;
						}
					}
					if (length_offset >= 0) {
						break;
					}
				}
				if (length_offset < 0) {
					/* Exceeds length of file */
					ast_debug(3, "Exceeds length of file? length=%" PRId64 ", count=%" PRId64 ", flength=%" PRId64 "\n", length, current_length, flength);
					length_offset = flength;
				}
			}

			/* Have offset_offset and length_offset now */
			if (length_offset - offset_offset == vlength + (strchr(args.options, 'd') ? 0 : strlen(format2term(newline_format)))) {
				/* Simple case - replacement of text inline */
				fseeko(ff, offset_offset, SEEK_SET);
				if (fwrite(value, 1, vlength, ff) < vlength) {
					ast_log(LOG_ERROR, "Short write?!!\n");
				} else if (!strchr(args.options, 'd') && fwrite(format2term(newline_format), 1, strlen(format2term(newline_format)), ff) < strlen(format2term(newline_format))) {
					ast_log(LOG_ERROR, "Short write?!!\n");
				}
				fclose(ff);
			} else if (length_offset - offset_offset > vlength + (strchr(args.options, 'd') ? 0 : strlen(format2term(newline_format)))) {
				/* More complex case - need to shorten file */
				off_t cur;
				int64_t length_length = length_offset - offset_offset;
				size_t vlen = vlength + (strchr(args.options, 'd') ? 0 : strlen(format2term(newline_format)));

				ast_debug(3, "offset=%s/%" PRId64 ", length=%s/%" PRId64 " (%" PRId64 "), vlength=%" PRId64 ", flength=%" PRId64 "\n",
					args.offset, offset_offset, args.length, length_offset, length_length, vlength, flength);

				fseeko(ff, offset_offset, SEEK_SET);
				if (fwrite(value, 1, vlength, ff) < vlength) {
					ast_log(LOG_ERROR, "Short write?!!\n");
					fclose(ff);
					return -1;
				} else if (!strchr(args.options, 'd') && fwrite(format2term(newline_format), 1, vlen - vlength, ff) < vlen - vlength) {
					ast_log(LOG_ERROR, "Short write?!!\n");
					fclose(ff);
					return -1;
				}
				while ((cur = ftello(ff)) < flength) {
					if (cur < 0) {
						ast_log(AST_LOG_ERROR, "Unable to determine last write position for '%s': %s\n", args.filename, strerror(errno));
						fclose(ff);
						return -1;
					}
					fseeko(ff, length_length - vlen, SEEK_CUR);
					if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf) && !feof(ff)) {
						ast_log(LOG_ERROR, "Short read?!!\n");
						fclose(ff);
						return -1;
					}
					/* Seek to where we last stopped writing */
					fseeko(ff, cur, SEEK_SET);
					if (fwrite(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf)) {
						ast_log(LOG_ERROR, "Short write?!!\n");
						fclose(ff);
						return -1;
					}
				}
				fclose(ff);
				if (truncate(args.filename, flength - (length_length - vlen))) {
					ast_log(LOG_ERROR, "Truncation of file failed: %s\n", strerror(errno));
				}
			} else {
				/* Most complex case - need to lengthen file */
				size_t vlen = vlength + (strchr(args.options, 'd') ? 0 : strlen(format2term(newline_format)));
				int64_t origlen = length_offset - offset_offset;
				off_t lastwritten = flength + vlen - origlen;

				ast_debug(3, "offset=%s/%" PRId64 ", length=%s/%" PRId64 ", vlength=%" PRId64 ", flength=%" PRId64 "\n",
					args.offset, offset_offset, args.length, length_offset, vlength, flength);

				fseeko(ff, flength - sizeof(fbuf), SEEK_SET);
				while (offset_offset + sizeof(fbuf) < ftello(ff)) {
					if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf)) {
						ast_log(LOG_ERROR, "Short read?!!\n");
						fclose(ff);
						return -1;
					}
					fseeko(ff, sizeof(fbuf) - vlen - origlen, SEEK_CUR);
					if (fwrite(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf)) {
						ast_log(LOG_ERROR, "Short write?!!\n");
						fclose(ff);
						return -1;
					}
					if ((lastwritten = ftello(ff) - sizeof(fbuf)) < offset_offset + sizeof(fbuf)) {
						break;
					}
					fseeko(ff, 2 * sizeof(fbuf) + vlen - origlen, SEEK_CUR);
				}
				fseek(ff, length_offset, SEEK_SET);
				if (fread(fbuf, 1, sizeof(fbuf), ff) < sizeof(fbuf) && !feof(ff)) {
					ast_log(LOG_ERROR, "Short read?!!\n");
					fclose(ff);
					return -1;
				}
				fseek(ff, offset_offset, SEEK_SET);
				if (fwrite(value, 1, vlength, ff) < vlength) {
					ast_log(LOG_ERROR, "Short write?!!\n");
					fclose(ff);
					return -1;
				} else if (!strchr(args.options, 'd') && fwrite(format2term(newline_format), 1, strlen(format2term(newline_format)), ff) < strlen(format2term(newline_format))) {
					ast_log(LOG_ERROR, "Short write?!!\n");
					fclose(ff);
					return -1;
				} else {
					off_t curpos = ftello(ff);
					foplen = lastwritten - curpos;
					if (fwrite(fbuf, 1, foplen, ff) < foplen) {
						ast_log(LOG_ERROR, "Short write?!!\n");
					}
				}
				fclose(ff);
			}
		}
	}

	return 0;
}

static struct ast_custom_function env_function = {
	.name = "ENV",
	.read = env_read,
	.write = env_write
};

static struct ast_custom_function stat_function = {
	.name = "STAT",
	.read = stat_read,
	.read_max = 12,
};

static struct ast_custom_function file_function = {
	.name = "FILE",
	.read2 = file_read,
	.write = file_write,
};

static struct ast_custom_function file_count_line_function = {
	.name = "FILE_COUNT_LINE",
	.read2 = file_count_line,
	.read_max = 12,
};

static struct ast_custom_function file_format_function = {
	.name = "FILE_FORMAT",
	.read2 = file_format,
	.read_max = 2,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&env_function);
	res |= ast_custom_function_unregister(&stat_function);
	res |= ast_custom_function_unregister(&file_function);
	res |= ast_custom_function_unregister(&file_count_line_function);
	res |= ast_custom_function_unregister(&file_format_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&env_function);
	res |= ast_custom_function_register_escalating(&stat_function, AST_CFE_READ);
	res |= ast_custom_function_register_escalating(&file_function, AST_CFE_BOTH);
	res |= ast_custom_function_register_escalating(&file_count_line_function, AST_CFE_READ);
	res |= ast_custom_function_register_escalating(&file_format_function, AST_CFE_READ);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Environment/filesystem dialplan functions");
