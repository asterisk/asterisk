/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Options provided by main asterisk program
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_OPTIONS_H
#define _ASTERISK_OPTIONS_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#define AST_CACHE_DIR_LEN 512
#define AST_FILENAME_MAX	80

extern int option_verbose;
extern int option_debug;
extern int option_nofork;
extern int option_quiet;
extern int option_console;
extern int option_initcrypto;
extern int option_nocolor;
extern int fully_booted;
extern int option_exec_includes;
extern int option_cache_record_files;
extern int option_timestamp;
extern int option_transcode_slin;
extern char defaultlanguage[];
extern time_t ast_startuptime;
extern time_t ast_lastreloadtime;
extern int ast_mainpid;
extern char record_cache_dir[AST_CACHE_DIR_LEN];
extern char debug_filename[AST_FILENAME_MAX];

#define VERBOSE_PREFIX_1 " "
#define VERBOSE_PREFIX_2 "  == "
#define VERBOSE_PREFIX_3 "    -- "
#define VERBOSE_PREFIX_4 "       > "  

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif



#endif
