dnl macro AST_CHECK_STRSEP_ARRAY_BOUNDS0
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
dnl When the issue is detected it will add a define to autoconfig.h which will prevent 
dnl bits/string2.h from replacing the standard implementation of strsep/strcmp with it's
dnl macro optimized version. bits/string.h checks these defines before inserting it's 
dnl replacements.
dnl
dnl When bits/string2.h get's fixed in the future, this macro should be able to
dnl detect the new behaviour, and when no warning is generated, it will use the optimize
dnl version from bits/string2.h
dnl
dnl 
dnl See 'define __strcmp_gc(s1, s2, l2) in bits/string2.h'
dnl 
dnl llvm-comment: Normally, this array-bounds warning are suppressed for macros, so that 
dnl unused paths like the one that accesses __s1[3] are not warned about.  But if you 
dnl preprocess manually, and feed the result to another instance of clang, it will warn 
dnl about all the possible forks of this particular if statement.
dnl
dnl Instead of switching of this optimization, another solution would be to run the pre-
dnl processing step with -frewrite-includes, which should preserve enough information
dnl so that clang should still be able to suppress the diagnostic at the compile step
dnl later on.
dnl
dnl See also "https://llvm.org/bugs/show_bug.cgi?id=20144"
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
				void test_strsep_strcmp (void) {
					char *haystackstr = "test1,test2";
					char *outstr;
					if (!strcmp(haystackstr, ",")) {
						printf("fail\n");
					}
					if ((outstr = strsep(&haystackstr, ","))) {
						printf("fail:%s\n", outstr);
					}
				}
				int main(int argc, char *argv[]) {
					test_strsep_strcmp();
					return 0;
				}
			])
		],[
			AC_MSG_RESULT(no)
		],[
			dnl setting this define in autoconfig.h will prevent bits/string2.h from replacing the standard implementation of strsep/strcmp
			AC_DEFINE([_HAVE_STRING_ARCH_strcmp], 1, [Prevent clang array-bounds warning by not using strcmp from bits/string2.h])
			AC_DEFINE([_HAVE_STRING_ARCH_strsep], 1, [Prevent clang array-bounds warning by not using strsep from bits/string2.h])
			AC_MSG_RESULT([prevent use of __string2_1bptr_p / strsep / strcmp from bits/string2.h])
		]
	)
	CFLAGS="$save_CFLAGS"
])
