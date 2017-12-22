# Check if a given expression will compile using a certain header.

# AST_C_COMPILE_CHECK([package], [expression], [header file], [version], [description])
AC_DEFUN([AST_C_COMPILE_CHECK],
[
    if test "x${PBX_$1}" != "x1" -a "${USE_$1}" != "no"; then
	AC_MSG_CHECKING(m4_ifval([$5],[for $5],[if "$2" compiles using $3]))
	saved_cppflags="${CPPFLAGS}"
	if test "x${$1_DIR}" != "x"; then
	    $1_INCLUDE="-I${$1_DIR}/include"
	fi
	CPPFLAGS="${CPPFLAGS} ${$1_INCLUDE}"

	AC_COMPILE_IFELSE(
	    [ AC_LANG_PROGRAM( [#include <$3>],
			       [ $2; ]
			       )],
	    [   AC_MSG_RESULT(yes)
		PBX_$1=1
		AC_DEFINE([HAVE_$1], 1, [Define if your system has the $1 headers.])
		m4_ifval([$4], [AC_DEFINE([HAVE_$1_VERSION], $4, [Define $1 headers version])])
	    ],
	    [       AC_MSG_RESULT(no) ]
	)
	CPPFLAGS="${saved_cppflags}"
    fi
])
