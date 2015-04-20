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
			AC_MSG_CHECKING(for clang -fblocks)
			if test "`echo "int main(){return ^{return 42;}();}" | ${CC} -o /dev/null -fblocks -x c - 2>&1`" = ""; then
				AST_CLANG_BLOCKS_LIBS=""
				AST_CLANG_BLOCKS="-Wno-unknown-warning-option -fblocks"
				AC_MSG_RESULT(yes)
			elif test "`echo "int main(){return ^{return 42;}();}" | ${CC} -o /dev/null -fblocks -x c -lBlocksRuntime - 2>&1`" = ""; then
				AST_CLANG_BLOCKS_LIBS="-lBlocksRuntime"
				AST_CLANG_BLOCKS="-fblocks"
				AC_MSG_RESULT(yes)
			else
				AC_MSG_ERROR([BlocksRuntime is required for clang, please install libblocksruntime])
			fi
			AC_SUBST(AST_CLANG_BLOCKS_LIBS)
			AC_SUBST(AST_CLANG_BLOCKS)
			AST_C_COMPILER_FAMILY="clang"
		]
	)
	if test -z "${AST_C_COMPILER_FAMILY}"; then
		AC_MSG_ERROR([Compiler ${CC} not supported. Mminimum required gcc-4.3 / llvm-gcc-4.3 / clang-3.3 + libblocksruntime-dev])
	fi
	AC_SUBST(AST_C_COMPILER_FAMILY)
])
