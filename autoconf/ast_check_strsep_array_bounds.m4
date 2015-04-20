dnl macro AST_CHECK_STRSEP_ARRAY_BOUNDS
dnl
dnl The optimized strcmp and strsep macro's in
dnl /usr/include/xxx-linux-gnu/bits/string2.h produce a warning (-Warray-bounds)
dnl when compiled with clang (+ -O1), when the delimiter parameter is
dnl passed in as a char *, instead of the expected char[]
dnl
dnl Instead of replacing all occurrences of strsep and strcmp, looking like:
dnl xxx_name = strsep(&rest, ",");
dnl
dnl with:
dnl char delimiters[] = ",";
dnl xxx_name = strsep(&rest, delimiters);
dnl
dnl to get around this warning, without having to suppress the warning completely.
dnl This macro detects the warning and force these 'optimizations' to be
dnl switched off (Clang already has a set of builtin optimizers which should result
dnl in good performance for these type of functions).
dnl
dnl When bits/string2.h get's fixed in the future, this macro should be able to
dnl detect the new behaviour, and when no warning is generated, it will use the optimize
dnl version from bits/string2.h
dnl
dnl See also "https://llvm.org/bugs/show_bug.cgi?id=11536"
dnl
AC_DEFUN([AST_CHECK_STRSEP_ARRAY_BOUNDS], [
	AC_MSG_CHECKING([for clang strsep/strcmp optimization])
	save_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -O1 -Werror=array-bounds"
	AC_COMPILE_IFELSE(
		[
		    	AC_LANG_SOURCE([
				#include <stdio.h>
				#include <string.h>

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
