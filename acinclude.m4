# AST_EXT_LIB([NAME], [FUNCTION], [package header], [package symbol name], [package friendly name], [additional LIB data])

AC_DEFUN([AST_EXT_LIB],
[
AC_ARG_WITH([$1], AC_HELP_STRING([--with-$1=PATH],[use $5 files in PATH]),[
case ${withval} in
     n|no)
     USE_$4=no
     ;;
     y|ye|yes)
     $4_MANDATORY="yes"
     ;;
     *)
     $4_DIR="${withval}"
     $4_MANDATORY="yes"
     ;;
esac
])

PBX_LIB$4=0

if test "${USE_$4}" != "no"; then
   pbxlibdir=""
   if test "x${$4_DIR}" != "x"; then
      pbxlibdir="-L${$1_DIR}/lib"
   fi
   AC_CHECK_LIB([$1], [$2], [AST_$4_FOUND=yes], [AST_$4_FOUND=no], ${pbxlibdir} $6)

   if test "${AST_$4_FOUND}" = "yes"; then
      $4_LIB="-l$1 $6"
      $4_HEADER_FOUND="1"
      if test "x${$4_DIR}" != "x"; then
         $4_LIB="${pbxlibdir} ${$4_LIB}"
	 $4_INCLUDE="-I${$4_DIR}/include"
	 if test "x$3" != "x" ; then
	    AC_CHECK_HEADER([${$4_DIR}/include/$3], [$4_HEADER_FOUND=1], [$4_HEADER_FOUND=0] )
	 fi
      else
	 if test "x$3" != "x" ; then
            AC_CHECK_HEADER([$3], [$4_HEADER_FOUND=1], [$4_HEADER_FOUND=0] )
	 fi
      fi
      if test "x${$4_HEADER_FOUND}" = "x0" ; then
         if test ! -z "${$4_MANDATORY}" ;
         then
            echo " ***"
            echo " *** It appears that you do not have the $1 development package installed."
            echo " *** Please install it to include $5 support, or re-run configure"
            echo " *** without explicitly specifying --with-$1"
            exit 1
         fi
         $4_LIB=""
         $4_INCLUDE=""
         PBX_LIB$4=0
      else
         PBX_LIB$4=1
         AC_DEFINE_UNQUOTED([HAVE_$4], 1, [Define to indicate the $5 library])
      fi
   elif test ! -z "${$4_MANDATORY}";
   then
      echo "***"
      echo "*** The $5 installation on this system appears to be broken."
      echo "*** Either correct the installation, or run configure"
      echo "*** without explicity specifying --with-$1"
      exit 1
   fi
fi
AC_SUBST([$4_LIB])
AC_SUBST([$4_INCLUDE])
AC_SUBST([PBX_LIB$4])
])


AC_DEFUN(
[AST_CHECK_GNU_MAKE], [AC_CACHE_CHECK(for GNU make, GNU_MAKE,
   GNU_MAKE='Not Found' ;
   for a in make gmake gnumake ; do
      if test -z "$a" ; then continue ; fi ;
      if ( sh -c "$a --version" 2> /dev/null | grep GNU  2>&1 > /dev/null ) ;  then
         GNU_MAKE=$a ;
         break;
      fi
   done ;
) ;
if test  "x$GNU_MAKE" = "xNot Found"  ; then
   echo " *** Please install GNU make.  It is required to build Asterisk!"
   exit 1
fi
AC_SUBST([GNU_MAKE])
])
