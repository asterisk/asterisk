#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>

#define OK					0
#define MODULE_NOT_FOUND			1
#define INVALID_NUMBER_ARGUMENTS	2

int main(int argc, char **argv) {
	/* Asterisk entry point */
	char* error = NULL;
	int (*ast_main)(int argc, char **argv);

	void *handle = dlopen ("asterisk.dll", RTLD_GLOBAL);
	if (handle == NULL) {
		fputs (dlerror(), stderr);
		fputs ("\r\n", stderr);
		return MODULE_NOT_FOUND;
	}
	printf("\r\nAsterisk module loaded successfully");
	ast_main = dlsym(handle, "main");
		if ((error = dlerror()) != NULL) {
			fputs("Asterisk main not found", stderr);
			fputs(error, stderr);
			exit(1);
		}
	printf("\r\nAsterisk entry point found");
	/* run asterisk main */
	(*ast_main)(argc, argv);
	dlclose(handle);
	printf("\r\nAsterisk stopped");
	return OK;
}
