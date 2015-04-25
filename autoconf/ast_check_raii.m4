dnl check RAII requirements
dnl
dnl gcc / llvm-gcc: -fnested-functions
dnl clang : -fblocks / -fblocks and -lBlocksRuntime"
AC_DEFUN([AST_CHECK_RAII], [
	AC_MSG_CHECKING([for RAII support])
	AST_C_COMPILER_FAMILY=""
	AC_LINK_IFELSE(
		[AC_LANG_PROGRAM([], [
			#if defined(__clang__)
			choke
			#endif
			])
		],[
			dnl Nested functions required for RAII implementation
			AC_MSG_CHECKING(for gcc -fnested-functions)
			AC_COMPILE_IFELSE(
				dnl Prototype needed due to http://gcc.gnu.org/bugzilla/show_bug.cgi?id=36774
				[
					AC_LANG_PROGRAM([], [auto void foo(void); void foo(void) {}])
				],[
					AST_NESTED_FUNCTIONS=""
					AC_MSG_RESULT(no)
				],[
					AST_NESTED_FUNCTIONS="-fnested-functions"
					AC_MSG_RESULT(yes)
				]
			)
			AC_SUBST(AST_NESTED_FUNCTIONS)
			AST_C_COMPILER_FAMILY="gcc"
		],[
			dnl Nested functions required for RAII implementation
			AC_MSG_CHECKING(for gcc -fnested-functions)
			AC_COMPILE_IFELSE(
				[
					AC_LANG_PROGRAM([], [auto void foo(void); void foo(void) {}])
				],[
					AST_NESTED_FUNCTIONS=""
					AC_MSG_RESULT(no)
				],[
					AST_NESTED_FUNCTIONS="-fnested-functions"
					AC_MSG_RESULT(yes)
				]
			)
			AC_SUBST(AST_NESTED_FUNCTIONS)
			AST_C_COMPILER_FAMILY="gcc"
	])
	if [ -z "${AST_C_COMPILER_FAMILY}" ]; then
		AC_MSG_ERROR([Compiler ${CC} not supported. Mminimum required gcc-4.3 / llvm-gcc-4.3 / clang-3.3 + libblocksruntime-dev])
	fi
	AC_SUBST(AST_C_COMPILER_FAMILY)
])
