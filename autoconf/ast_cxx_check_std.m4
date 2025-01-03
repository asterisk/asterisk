# AST_CXX_CHECK_STD([standard], [force latest std?])
# Check if the C++ compiler supprts a specific standard.
# If the second argument is "yes", forse the compiler to
# use the latest standard it supports by keeping the last
# -std=gnu++=XX option that worked.
AC_DEFUN([AST_CXX_CHECK_STD],
[
    PBX_CXX$1=0
    if test "$2" != "yes" ; then
        ast_cxx_check_std_save_CXX="${CXX}"
        ast_cxx_check_std_save_CXXCPP="${CXXCPP}"
    fi
    AX_CXX_COMPILE_STDCXX($1, , optional)
    if test "$HAVE_CXX$1" = "1";
    then
       PBX_CXX$1=1
    fi
    AC_SUBST(PBX_CXX$1)
    if test "$2" != "yes" ; then
        CXX="${ast_cxx_check_std_save_CXX}"
        CXXCPP="${ast_cxx_check_std_save_CXXCPP}"
    fi
])
