#
# If this file is changed, be sure to run ASTTOPDIR/bootstrap.sh
# before committing.
#

AC_DEFUN([_JANSSON_CONFIGURE],
[
	if test "${ac_mandatory_list#*JANSSON*}" != "$ac_mandatory_list" ; then
		AC_MSG_ERROR(--with-jansson and --with-jansson-bundled can't both be specified)
	fi

	ac_mandatory_list="$ac_mandatory_list JANSSON"
	JANSSON_DIR="${ac_pwd}/third-party/jansson"

	AC_MSG_CHECKING(for embedded jansson (may have to download))
	AC_MSG_RESULT(configuring)

	if test "x${DOWNLOAD_TO_STDOUT}" = "x" ; then
		AC_MSG_ERROR(A download utility (wget, curl, or fetch) is required to download bundled jansson)
	fi
	if test "${BZIP2}" = ":" ; then
		AC_MSG_ERROR(bzip2 is required to extract the jansson tar file)
	fi
	if test "${TAR}" = ":" ; then
		AC_MSG_ERROR(tar is required to extract the jansson tar file)
	fi
	if test "${PATCH}" = ":" ; then
		AC_MSG_ERROR(patch is required to configure bundled jansson)
	fi
	if test "${SED}" = ":" ; then
		AC_MSG_ERROR(sed is required to configure bundled jansson)
	fi
	if test "${NM}" = ":" ; then
		AC_MSG_ERROR(nm is required to build bundled jansson)
	fi
	if test "${MD5}" = ":" ; then
		AC_MSG_ERROR(md5sum is required to build bundled jansson)
	fi
	if test "${CAT}" = ":" ; then
		AC_MSG_ERROR(cat is required to build bundled jansson)
	fi
	if test "${CUT}" = ":" ; then
		AC_MSG_ERROR(cut is required to build bundled jansson)
	fi
	if test "${GREP}" = ":" ; then
		AC_MSG_ERROR(grep is required to build bundled jansson)
	fi

	AC_ARG_VAR([JANSSON_CONFIGURE_OPTS],[Additional configure options to pass to bundled jansson])
	this_host=$(./config.sub $(./config.guess))
	if test "$build" != "$this_host" ; then
		JANSSON_CONFIGURE_OPTS+=" --build=$build"
	fi
	if test "$host" != "$this_host" ; then
		JANSSON_CONFIGURE_OPTS+=" --host=$host"
	fi

	export TAR PATCH SED NM EXTERNALS_CACHE_DIR AST_DOWNLOAD_CACHE DOWNLOAD_TO_STDOUT DOWNLOAD_TIMEOUT DOWNLOAD MD5 CAT CUT GREP
	export NOISY_BUILD
	${GNU_MAKE} --quiet --no-print-directory -C ${JANSSON_DIR} \
		JANSSON_CONFIGURE_OPTS="$JANSSON_CONFIGURE_OPTS" \
		EXTERNALS_CACHE_DIR="${EXTERNALS_CACHE_DIR:-${AST_DOWNLOAD_CACHE}}" \
		configure
	if test $? -ne 0 ; then
		AC_MSG_RESULT(failed)
		AC_MSG_NOTICE(Unable to configure ${JANSSON_DIR})
		AC_MSG_ERROR(Re-run the ./configure command with 'NOISY_BUILD=yes' appended to see error details.)
	fi

	AC_MSG_CHECKING(for bundled jansson)

	JANSSON_INCLUDE=-I${JANSSON_DIR}/dest/include
	JANSSON_CFLAGS="$JANSSON_INCLUDE"
	JANSSON_LIB="-L${JANSSON_DIR}/dest/lib -ljansson"
	PBX_JANSSON=1

	AC_SUBST([JANSSON_BUNDLED])
	AC_SUBST([PBX_JANSSON])
	AC_SUBST([JANSSON_LIB])
	AC_SUBST([JANSSON_INCLUDE])
	AC_MSG_RESULT(yes)
])

AC_DEFUN([JANSSON_CONFIGURE],
[
	if test "$JANSSON_BUNDLED" = "yes" ; then
		_JANSSON_CONFIGURE()
	fi
])
