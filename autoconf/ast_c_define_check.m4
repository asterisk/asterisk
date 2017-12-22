# Check if a given macro is defined in a certain header.

# AST_C_DEFINE_CHECK([package], [macro name], [header file], [version])
AC_DEFUN([AST_C_DEFINE_CHECK],
[
    if test "x${PBX_$1}" != "x1"; then
	AC_MSG_CHECKING([for $2 in $3])
	saved_cppflags="${CPPFLAGS}"
	if test "x${$1_DIR}" != "x"; then
	    $1_INCLUDE="-I${$1_DIR}/include"
	fi
	CPPFLAGS="${CPPFLAGS} ${$1_INCLUDE}"

	AC_COMPILE_IFELSE(
	    [ AC_LANG_PROGRAM( [#include <$3>],
			       [#if defined($2)
				int foo = 0;
			        #else
			        int foo = bar;
			        #endif
				0
			       ])],
	    [   AC_MSG_RESULT(yes)
		PBX_$1=1
		AC_DEFINE([HAVE_$1], 1, [Define if your system has the $1 headers.])
		m4_ifval([$4], [AC_DEFINE([HAVE_$1_VERSION], $4, [Define $1 headers version])])
	    ],
	    [   AC_MSG_RESULT(no) ]
	)
	CPPFLAGS="${saved_cppflags}"
    fi
    AC_SUBST(PBX_$1)
])
