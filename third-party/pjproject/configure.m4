AC_DEFUN([_PJPROJECT_CONFIGURE],
[
	if test "${ac_mandatory_list#*PJPROJECT*}" != "$ac_mandatory_list" ; then
		AC_MSG_ERROR(--with-pjproject and --with-pjproject-bundled can't both be specified)
	fi

	ac_mandatory_list="$ac_mandatory_list PJPROJECT"
	PJPROJECT_DIR="${ac_top_build_prefix}third-party/pjproject"

	AC_MSG_CHECKING(for embedded pjproject (may have to download))
	AC_MSG_RESULT(configuring)

	if test "x${DOWNLOAD_TO_STDOUT}" = "x" ; then
		AC_MSG_ERROR(A download utility (wget, curl or fetch) is required to download bundled pjproject)
	fi
	if test "${BZIP2}" = ":" ; then
		AC_MSG_ERROR(bzip2 is required to extract the pjproject tar file)
	fi
	if test "${TAR}" = ":" ; then
		AC_MSG_ERROR(tar is required to extract the pjproject tar file)
	fi
	if test "${PATCH}" = ":" ; then
		AC_MSG_ERROR(patch is required to configure bundled pjproject)
	fi
	if test "${SED}" = ":" ; then
		AC_MSG_ERROR(sed is required to configure bundled pjproject)
	fi
	if test "${NM}" = ":" ; then
		AC_MSG_ERROR(nm is required to build bundled pjproject)
	fi

	export TAR PATCH SED NM EXTERNALS_CACHE_DIR DOWNLOAD_TO_STDOUT
	${GNU_MAKE} --quiet --no-print-directory -C ${PJPROJECT_DIR} EXTERNALS_CACHE_DIR=${EXTERNALS_CACHE_DIR} configure
	if test $? -ne 0 ; then
		AC_MSG_RESULT(failed)
		AC_MSG_NOTICE(Unable to configure ${PJPROJECT_DIR})
		AC_MSG_ERROR(Run "${GNU_MAKE} -C ${PJPROJECT_DIR} NOISY_BUILD=yes configure" to see error details.)
	fi

	AC_MSG_CHECKING(for bundled pjproject)

	PJPROJECT_INCLUDE=$(${GNU_MAKE} --quiet --no-print-directory -C ${PJPROJECT_DIR} EXTERNALS_CACHE_DIR=${EXTERNALS_CACHE_DIR} echo_cflags)
	PJPROJECT_CFLAGS="$PJPROJECT_INCLUDE"
	PBX_PJPROJECT=1

	AC_DEFINE([HAVE_PJPROJECT], 1, [Define if your system has PJPROJECT])
	AC_DEFINE([HAVE_PJPROJECT_BUNDLED], 1, [Define if your system has PJPROJECT_BUNDLED])

	AC_DEFINE([HAVE_PJSIP_DLG_CREATE_UAS_AND_INC_LOCK], 1, [Define if your system has pjsip_dlg_create_uas_and_inc_lock declared.])
	AC_DEFINE([HAVE_PJ_TRANSACTION_GRP_LOCK], 1, [Define if your system has pjsip_tsx_create_uac2 declared.])
	AC_DEFINE([HAVE_PJSIP_REPLACE_MEDIA_STREAM], 1, [Define if your system has PJSIP_REPLACE_MEDIA_STREAM declared])
	AC_DEFINE([HAVE_PJSIP_GET_DEST_INFO], 1, [Define if your system has pjsip_get_dest_info declared.])
	AC_DEFINE([HAVE_PJ_SSL_CERT_LOAD_FROM_FILES2], 1, [Define if your system has pj_ssl_cert_load_from_files2 declared.])
	AC_DEFINE([HAVE_PJSIP_EXTERNAL_RESOLVER], 1, [Define if your system has pjsip_endpt_set_ext_resolver declared.])
	AC_DEFINE([HAVE_PJSIP_TLS_TRANSPORT_PROTO], 1, [Define if your system has PJSIP_TLS_TRANSPORT_PROTO])
	AC_DEFINE([HAVE_PJSIP_EVSUB_GRP_LOCK], 1, [Define if your system has PJSIP_EVSUB_GRP_LOCK])
	AC_DEFINE([HAVE_PJSIP_INV_SESSION_REF], 1, [Define if your system has PJSIP_INV_SESSION_REF])
	AC_DEFINE([HAVE_PJSIP_AUTH_CLT_DEINIT], 1, [Define if your system has pjsip_auth_clt_deinit declared.])

	AC_SUBST([PJPROJECT_BUNDLED])
	AC_SUBST([PJPROJECT_DIR])
	AC_SUBST([PBX_PJPROJECT])
	AC_SUBST([PJPROJECT_LIB])
	AC_SUBST([PJPROJECT_INCLUDE])
	AC_MSG_RESULT(yes)
])

AC_DEFUN([PJPROJECT_CONFIGURE],
[
	if test "$PJPROJECT_BUNDLED" = "yes" ; then
		_PJPROJECT_CONFIGURE()
	fi
])
