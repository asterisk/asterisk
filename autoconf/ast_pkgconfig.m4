# Check for pkg-config component $2:
# AST_PKG_CONFIG_CHECK([package], [component])
AC_DEFUN([AST_PKG_CONFIG_CHECK],
[
   if test "x${PBX_$1}" != "x1" -a "${USE_$1}" != "no"; then
      PKG_CHECK_MODULES($1, $2, [
            PBX_$1=1
            $1_INCLUDE="$$1_CFLAGS"
            $1_LIB="$$1_LIBS"
            AC_DEFINE([HAVE_$1], 1, [Define if your system has the $1 libraries.])
         ], [
            PBX_$1=0
         ]
      )
   fi
])

