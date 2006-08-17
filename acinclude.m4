# AST_EXT_LIB_SETUP([package symbol name], [package friendly name], [package option name], [additional help text])

AC_DEFUN([AST_EXT_LIB_SETUP],
[
$1_DESCRIP="$2"
$1_OPTION="$3"
AC_ARG_WITH([$3], AC_HELP_STRING([--with-$3=PATH],[use $2 files in PATH $4]),[
case ${withval} in
     n|no)
     USE_$1=no
     ;;
     y|ye|yes)
     $1_MANDATORY="yes"
     ;;
     *)
     $1_DIR="${withval}"
     $1_MANDATORY="yes"
     ;;
esac
])
PBX_$1=0
AC_SUBST([$1_LIB])
AC_SUBST([$1_INCLUDE])
AC_SUBST([PBX_$1])
])

# AST_EXT_LIB_CHECK([package symbol name], [package library name], [function to check], [package header], [additional LIB data])

AC_DEFUN([AST_EXT_LIB_CHECK],
[
if test "${USE_$1}" != "no"; then
   pbxlibdir=""
   if test "x${$1_DIR}" != "x"; then
      if test -d ${$1_DIR}/lib; then
      	 pbxlibdir="-L${$1_DIR}/lib"
      else
      	 pbxlibdir="-L${$1_DIR}"
      fi
   fi
   AC_CHECK_LIB([$2], [$3], [AST_$1_FOUND=yes], [AST_$1_FOUND=no], ${pbxlibdir} $5)

   if test "${AST_$1_FOUND}" = "yes"; then
      $1_LIB="-l$2 $5"
      $1_HEADER_FOUND="1"
      if test "x${$1_DIR}" != "x"; then
         $1_LIB="${pbxlibdir} ${$1_LIB}"
	 $1_INCLUDE="-I${$1_DIR}/include"
	 if test "x$4" != "x" ; then
	    AC_CHECK_HEADER([${$1_DIR}/include/$4], [$1_HEADER_FOUND=1], [$1_HEADER_FOUND=0] )
	 fi
      else
	 if test "x$4" != "x" ; then
            AC_CHECK_HEADER([$4], [$1_HEADER_FOUND=1], [$1_HEADER_FOUND=0] )
	 fi
      fi
      if test "x${$1_HEADER_FOUND}" = "x0" ; then
         if test ! -z "${$1_MANDATORY}" ;
         then
            AC_MSG_NOTICE( ***)
            AC_MSG_NOTICE( *** It appears that you do not have the $2 development package installed.)
            AC_MSG_NOTICE( *** Please install it to include ${$1_DESCRIP} support, or re-run configure)
            AC_MSG_NOTICE( *** without explicitly specifying --with-${$1_OPTION})
            exit 1
         fi
         $1_LIB=""
         $1_INCLUDE=""
         PBX_$1=0
      else
         PBX_$1=1
         AC_DEFINE_UNQUOTED([HAVE_$1], 1, [Define to indicate the ${$1_DESCRIP} library])
      fi
   elif test ! -z "${$1_MANDATORY}";
   then
      AC_MSG_NOTICE(***)
      AC_MSG_NOTICE(*** The ${$1_DESCRIP} installation on this system appears to be broken.)
      AC_MSG_NOTICE(*** Either correct the installation, or run configure)
      AC_MSG_NOTICE(*** without explicitly specifying --with-${$1_OPTION})
      exit 1
   fi
fi
])


AC_DEFUN(
[AST_CHECK_GNU_MAKE], [AC_CACHE_CHECK(for GNU make, GNU_MAKE,
   GNU_MAKE='Not Found' ;
   GNU_MAKE_VERSION_MAJOR=0 ;
   GNU_MAKE_VERSION_MINOR=0 ;
   for a in make gmake gnumake ; do
      if test -z "$a" ; then continue ; fi ;
      if ( sh -c "$a --version" 2> /dev/null | grep GNU  2>&1 > /dev/null ) ;  then
         GNU_MAKE=$a ;
	 GNU_MAKE_VERSION_MAJOR=`$GNU_MAKE --version | grep "GNU Make" | cut -f3 -d' ' | cut -f1 -d'.'`
	 GNU_MAKE_VERSION_MINOR=`$GNU_MAKE --version | grep "GNU Make" | cut -f2 -d'.'`
         break;
      fi
   done ;
) ;
if test  "x$GNU_MAKE" = "xNot Found"  ; then
   AC_MSG_ERROR( *** Please install GNU make.  It is required to build Asterisk!)
   exit 1
fi
AC_SUBST([GNU_MAKE])
])
