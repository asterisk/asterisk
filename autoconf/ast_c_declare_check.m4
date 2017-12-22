# Check if a given symbol is declared using a certain header.
# Check whether SYMBOL (a function, variable, or constant) is declared.

# AST_C_DECLARE_CHECK([package], [symbol], [header file], [version])
AC_DEFUN([AST_C_DECLARE_CHECK],
[
    if test "x${PBX_$1}" != "x1" -a "${USE_$1}" != "no"; then
        AC_MSG_CHECKING([for $2 declared in $3])
        saved_cppflags="${CPPFLAGS}"
        if test "x${$1_DIR}" != "x"; then
            $1_INCLUDE="-I${$1_DIR}/include"
        fi
        CPPFLAGS="${CPPFLAGS} ${$1_INCLUDE}"

        AC_COMPILE_IFELSE(
            [ AC_LANG_PROGRAM( [#include <$3>],
                                [#if !defined($2)
                                    (void) $2;
                                #endif
                                ])],
            [   AC_MSG_RESULT(yes)
                PBX_$1=1
                AC_DEFINE([HAVE_$1], 1, [Define if your system has $2 declared.])
                m4_ifval([$4], [AC_DEFINE([HAVE_$1_VERSION], $4, [Define $1 headers version])])
            ],
            [   AC_MSG_RESULT(no) ]
        )

        CPPFLAGS="${saved_cppflags}"
    fi
])
