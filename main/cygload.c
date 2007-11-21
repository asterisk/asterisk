/*
 * Loader for asterisk under windows.
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
	fprintf(stderr, "could not load asterisk, %s\n", dlerror());
	return 1;	/* there was an error */
}
