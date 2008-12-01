# Helper function to check for gcc attributes.
# AST_GCC_ATTRIBUTE([attribute name])

AC_DEFUN([AST_GCC_ATTRIBUTE],
[
AC_MSG_CHECKING(for compiler 'attribute $1' support)
saved_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Werror"
AC_COMPILE_IFELSE(
	AC_LANG_PROGRAM([void __attribute__(($1)) *test(void *muffin, ...) {}],
			[]),
	AC_MSG_RESULT(yes)
	AC_DEFINE_UNQUOTED([HAVE_ATTRIBUTE_$1], 1, [Define to 1 if your GCC C compiler supports the '$1' attribute.]),
	AC_MSG_RESULT(no))
]
CFLAGS="$saved_CFLAGS"
)
