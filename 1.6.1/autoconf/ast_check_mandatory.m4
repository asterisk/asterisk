# Check whether any of the mandatory modules are not present, and
# print error messages in case. The mandatory list is built using
# --with-* arguments when invoking configure.

AC_DEFUN([AST_CHECK_MANDATORY],
[
	AC_MSG_CHECKING([for mandatory modules: ${ac_mandatory_list}])
	err=0;
	for i in ${ac_mandatory_list}; do
		eval "a=\${PBX_$i}"
		if test "x${a}" = "x1" ; then continue; fi
		if test ${err} = "0" ; then AC_MSG_RESULT(fail) ; fi
		AC_MSG_RESULT()
		eval "a=\${${i}_OPTION}"
		AC_MSG_NOTICE([***])
		AC_MSG_NOTICE([*** The $i installation appears to be missing or broken.])
		AC_MSG_NOTICE([*** Either correct the installation, or run configure])
		AC_MSG_NOTICE([*** including --without-${a}.])
		err=1
	done
	if test $err = 1 ; then exit 1; fi
	AC_MSG_RESULT(ok)
])
