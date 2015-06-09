# Helper function to check for gcc attributes.
# AST_GCC_ATTRIBUTE([attribute name], [attribute syntax], [attribute scope], [makeopts flag])

AC_DEFUN([AST_GCC_ATTRIBUTE],
[
AC_MSG_CHECKING(for compiler 'attribute $1' support)
saved_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Wall -Wno-unused -Werror"
m4_ifval([$4],$4=0)

if test "x$2" = "x"
then
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([$3 void __attribute__(($1)) *test(void *muffin, ...) {return (void *) 0;}],
			[])],
	AC_MSG_RESULT(yes)
	m4_ifval([$4],$4=1)
	AC_DEFINE_UNQUOTED([HAVE_ATTRIBUTE_$1], 1, [Define to 1 if your GCC C compiler supports the '$1' attribute.]),
	AC_MSG_RESULT(no)
)
else
AC_COMPILE_IFELSE(
	[AC_LANG_PROGRAM([$3 void __attribute__(($2)) *test(void *muffin, ...) ;],
			[])],
	AC_MSG_RESULT(yes)
	m4_ifval([$4],$4=1)
	AC_DEFINE_UNQUOTED([HAVE_ATTRIBUTE_$1], 1, [Define to 1 if your GCC C compiler supports the '$1' attribute.]),
	AC_MSG_RESULT(no)
)
fi

m4_ifval([$4],[AC_SUBST($4)])
CFLAGS="$saved_CFLAGS"
]
)
