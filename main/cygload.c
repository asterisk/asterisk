/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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
 * \brief 
 * Loader for Asterisk under Cygwin/windows.
 * Open the dll, locate main, run.
 */

#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>

typedef int (*main_f)(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	main_f ast_main = NULL;
	void *handle = dlopen("asterisk.dll", 0);
	if (handle)
		ast_main = (main_f)dlsym(handle, "main");
	if (ast_main)
		return ast_main(argc, argv);
	fprintf(stderr, "could not load Asterisk, %s\n", dlerror());
	return 1;	/* there was an error */
}
