dnl "https://llvm.org/bugs/show_bug.cgi?id=11536" / "https://reviewboard.asterisk.org/r/4546/"
AC_DEFUN([AST_CHECK_STRSEP_ARRAY_BOUNDS], [
	AC_MSG_CHECKING([for clang strsep/strcmp optimization])
	save_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -O1 -Werror=array-bounds"
	AC_COMPILE_IFELSE(
		[
		    	AC_LANG_SOURCE([
				#include <stdio.h>
				#include <string.h>

				/* works with gcc and clang : used to illustrate that the optimized function is not completely broken */
				/*
				void test1 (void) {
					const char delimiter[] = ",";
					char *teststr1 = "test,test";
					char *outstr;
					if ((outstr = strsep(&teststr1, delimiter))) {
						printf("ok:%s\n", outstr);
					}
					const char *teststr2 = "test,test";
					if (!strcmp(teststr2, delimiter)) {
						printf("strcmp\n");
					}
				}
				*/
				/* fails with clang and -O1 */
				void test_strsep (void) {
					char *teststr1 = "test,test";
					char *outstr;
					if ((outstr = strsep(&teststr1, ","))) {
						printf("fail:%s\n", outstr);
					}
					const char *teststr2 = "test,test";
					if (!strcmp(teststr2, ",")) {
						printf("fail\n");
					}
				}
				int main(int argc, char *argv[]) {
					test_strsep();
					return 0;
				}
			])
		],[
			AC_MSG_RESULT(no)
		],[
			AC_DEFINE([_HAVE_STRING_ARCH_strcmp], 1, [Prevent clang array-bounds warning when using of bits/string2.h version of strcmp])
			AC_DEFINE([_HAVE_STRING_ARCH_strsep], 1, [Prevent clang array-bounds warning when using of bits/string2.h version of strsep])
			AC_MSG_RESULT([prevent use of __string2_1bptr_p / strsep from bits/string2.h])
		]
	)
	CFLAGS="$save_CFLAGS"
])
