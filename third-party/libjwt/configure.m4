#
# If this file is changed, be sure to run ASTTOPDIR/bootstrap.sh
# before committing.
#

AC_DEFUN([_LIBJWT_CONFIGURE],
[
	if test "${ac_mandatory_list#*LIBJWT*}" != "$ac_mandatory_list" ; then
		AC_MSG_ERROR(--with-libjwt and --with-libjwt-bundled can't both be specified)
	fi

	ac_mandatory_list="$ac_mandatory_list LIBJWT"
	LIBJWT_DIR="${ac_pwd}/third-party/libjwt"

	AC_MSG_CHECKING(for embedded libjwt (may have to download))
	AC_MSG_RESULT(configuring)

	if test "x${DOWNLOAD_TO_STDOUT}" = "x" ; then
		AC_MSG_ERROR(A download utility (wget, curl, or fetch) is required to download bundled libjwt)
	fi
	if test "${GZIP}" = ":" ; then
		AC_MSG_ERROR(gzip is required to extract the libjwt tar file)
	fi
	if test "${TAR}" = ":" ; then
		AC_MSG_ERROR(tar is required to extract the libjwt tar file)
	fi
	if test "${PATCH}" = ":" ; then
		AC_MSG_ERROR(patch is required to configure bundled libjwt)
	fi
	if test "${SED}" = ":" ; then
		AC_MSG_ERROR(sed is required to configure bundled libjwt)
	fi
	if test "${NM}" = ":" ; then
		AC_MSG_ERROR(nm is required to build bundled libjwt)
	fi
	if test "${MD5}" = ":" ; then
		AC_MSG_ERROR(md5sum is required to build bundled libjwt)
	fi
	if test "${CAT}" = ":" ; then
		AC_MSG_ERROR(cat is required to build bundled libjwt)
	fi
	if test "${CUT}" = ":" ; then
		AC_MSG_ERROR(cut is required to build bundled libjwt)
	fi
	if test "${GREP}" = ":" ; then
		AC_MSG_ERROR(grep is required to build bundled libjwt)
	fi

	AC_ARG_VAR([LIBJWT_CONFIGURE_OPTS],[Additional configure options to pass to bundled libjwt])
	this_host=$(./config.sub $(./config.guess))
	if test "$build" != "$this_host" ; then
		LIBJWT_CONFIGURE_OPTS+=" --build=$build_alias"
	fi
	if test "$host" != "$this_host" ; then
		LIBJWT_CONFIGURE_OPTS+=" --host=$host_alias"
	fi

	export TAR PATCH SED NM EXTERNALS_CACHE_DIR AST_DOWNLOAD_CACHE DOWNLOAD_TO_STDOUT DOWNLOAD_TIMEOUT DOWNLOAD MD5 CAT CUT GREP
	export NOISY_BUILD
	export JANSSON_CFLAGS
	export JANSSON_LIBS="${JANSSON_LIB}"
	${GNU_MAKE} --quiet --no-print-directory -C ${LIBJWT_DIR} \
		LIBJWT_CONFIGURE_OPTS="$LIBJWT_CONFIGURE_OPTS" \
		EXTERNALS_CACHE_DIR="${EXTERNALS_CACHE_DIR:-${AST_DOWNLOAD_CACHE}}" \
		configure
	if test $? -ne 0 ; then
		AC_MSG_RESULT(failed)
		AC_MSG_NOTICE(Unable to configure ${LIBJWT_DIR})
		AC_MSG_ERROR(Re-run the ./configure command with 'NOISY_BUILD=yes' appended to see error details.)
	fi

	AC_MSG_CHECKING(for bundled libjwt)

	LIBJWT_INCLUDE=-I${LIBJWT_DIR}/dist/usr/include
	LIBJWT_CFLAGS="$LIBJWT_INCLUDE"
	LIBJWT_LIB="-L${LIBJWT_DIR}/dist/usr/lib -ljwt"
	PBX_LIBJWT=1

	# We haven't run install yet

	AC_SUBST([LIBJWT_BUNDLED])
	AC_SUBST([PBX_LIBJWT])
	AC_SUBST([LIBJWT_LIB])
	AC_SUBST([LIBJWT_INCLUDE])
	AC_MSG_RESULT(yes)
	AC_DEFINE([HAVE_LIBJWT_BUNDLED], 1, [Define if your system has LIBJWT_BUNDLED])
])

AC_DEFUN([LIBJWT_CONFIGURE],
[
	if test "$LIBJWT_BUNDLED" = "yes" ; then
		_LIBJWT_CONFIGURE()
	fi
])

