AC_DEFUN([AST_CHECK_GNU_MAKE], [AC_CACHE_CHECK([for GNU make], [ac_cv_GNU_MAKE],
   ac_cv_GNU_MAKE='Not Found' ;
   ac_cv_GNU_MAKE_VERSION_MAJOR=0 ;
   ac_cv_GNU_MAKE_VERSION_MINOR=0 ;
   for a in make gmake gnumake ; do
      if test -z "$a" ; then continue ; fi ;
      if ( sh -c "$a --version" 2> /dev/null | grep GNU  2>&1 > /dev/null ) ;  then
         ac_cv_GNU_MAKE=$a ;
         ac_cv_GNU_MAKE_VERSION_MAJOR=`$ac_cv_GNU_MAKE --version | grep "GNU Make" | cut -f3 -d' ' | cut -f1 -d'.'`
         ac_cv_GNU_MAKE_VERSION_MINOR=`$ac_cv_GNU_MAKE --version | grep "GNU Make" | cut -f2 -d'.' | cut -c1-2`
         break;
      fi
   done ;
) ;
if test  "x$ac_cv_GNU_MAKE" = "xNot Found"  ; then
   AC_MSG_ERROR( *** Please install GNU make.  It is required to build Asterisk!)
   exit 1
fi
AC_SUBST([GNU_MAKE], [$ac_cv_GNU_MAKE])
])
