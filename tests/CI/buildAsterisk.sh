#!/usr/bin/env bash

CIDIR=$(dirname $(readlink -fn $0))
COVERAGE=0
REF_DEBUG=0
DISABLE_BINARY_MODULES=0
NO_CONFIGURE=0
NO_MENUSELECT=0
NO_MAKE=0
NO_ALEMBIC=0
NO_DEV_MODE=0
source $CIDIR/ci.functions

set -e

if [ -z $BRANCH_NAME ]; then
	BRANCH_NAME=$(git config -f .gitreview --get gerrit.defaultbranch)
fi

gen_cats() {
	set +x
	action=$1
	shift
	cats=$@

	for x in $cats ; do
		echo " --${action}-category ${x}"
	done
}

gen_mods() {
	set +x
	action=$1
	shift
	mods=$@

	for x in $mods ; do
		echo " --${action} ${x}"
	done
}

run_alembic() {
	pushd contrib/ast-db-manage >/dev/null
	runner alembic $@
	RC=$?
	popd > /dev/null
	return $RC
}

[ x"$OUTPUT_DIR" != x ] && mkdir -p "$OUTPUT_DIR" 2> /dev/null

if [ -z $TESTED_ONLY ]; then
	# Skip building untested modules by default if coverage is enabled.
	TESTED_ONLY=$COVERAGE
fi

if [ -z $LCOV_DIR ]; then
	LCOV_DIR="${OUTPUT_DIR:+${OUTPUT_DIR}/}lcov"
fi

if [ x"$CACHE_DIR" != x ] ; then
	mkdir -p $CACHE_DIR/sounds $CACHE_DIR/externals 2> /dev/null
fi

if [ ${CCACHE_DISABLE:-0} -ne 1 ] ; then
	if [ x"$CACHE_DIR" != x ] ; then
		mkdir -p $CACHE_DIR/ccache
		export CCACHE_UMASK=002
		export CCACHE_DIR=$CACHE_DIR/ccache
	fi
	case ":${PATH:-}:" in
		*:/usr/lib*/ccache:*)
			echo "Enabling ccache at $CCACHE_DIR"
		 ;;
		*)
			if [ -d /usr/lib64/ccache ] ; then
				echo "Enabling ccache at /usr/lib64/ccache with $CCACHE_DIR"
				export PATH="/usr/lib64/ccache${PATH:+:$PATH}"
			elif [ -d /usr/lib/ccache ] ; then
				echo "Enabling ccache at /usr/lib/ccache with $CCACHE_DIR"
				export PATH="/usr/lib/ccache${PATH:+:$PATH}"
			fi
		;;
	esac
fi

runner ccache -s
runner ulimit -a

MAKE=`which make`
PKGCONFIG=`which pkg-config`
_libdir=`${CIDIR}/findLibdir.sh`

common_config_args="--prefix=/usr ${_libdir:+--libdir=${_libdir}} --sysconfdir=/etc --with-pjproject-bundled"
$PKGCONFIG 'jansson >= 2.11' || common_config_args+=" --with-jansson-bundled"
common_config_args+=" ${CACHE_DIR:+--with-sounds-cache=${CACHE_DIR}/sounds --with-externals-cache=${CACHE_DIR}/externals}"
if [ $NO_DEV_MODE -eq 0 ] ; then
	common_config_args+=" --enable-dev-mode"
fi
if [ $COVERAGE -eq 1 ] ; then
	common_config_args+=" --enable-coverage"
fi
if [ "$BRANCH_NAME" == "master" -o $DISABLE_BINARY_MODULES -eq 1 ] ; then
	common_config_args+=" --disable-binary-modules"
fi

export WGET_EXTRA_ARGS="--quiet"

if [ $NO_CONFIGURE -eq 0 ] ; then
	runner ./configure ${common_config_args} > ${OUTPUT_DIR:+${OUTPUT_DIR}/}configure.txt
fi

if [ $NO_MENUSELECT -eq 0 ] ; then
	runner ${MAKE} menuselect.makeopts

	runner menuselect/menuselect `gen_mods enable DONT_OPTIMIZE BETTER_BACKTRACES` menuselect.makeopts
	if [ $NO_DEV_MODE -eq 0 ] ; then
		runner menuselect/menuselect `gen_mods enable MALLOC_DEBUG DO_CRASH TEST_FRAMEWORK` menuselect.makeopts
	fi
	runner menuselect/menuselect `gen_mods disable COMPILE_DOUBLE BUILD_NATIVE` menuselect.makeopts
	if [ $REF_DEBUG -eq 1 ] ; then
		runner menuselect/menuselect --enable REF_DEBUG menuselect.makeopts
	fi

	cat_enables=""

	if [[ ! "${BRANCH_NAME}" =~ ^certified ]] ; then
		cat_enables+=" MENUSELECT_BRIDGES MENUSELECT_CEL MENUSELECT_CDR"
		cat_enables+=" MENUSELECT_CHANNELS MENUSELECT_CODECS MENUSELECT_FORMATS MENUSELECT_FUNCS"
		cat_enables+=" MENUSELECT_PBX MENUSELECT_RES MENUSELECT_UTILS"
	fi

	if [ $NO_DEV_MODE -eq 0 ] ; then
		cat_enables+=" MENUSELECT_TESTS"
	fi
	runner menuselect/menuselect `gen_cats enable $cat_enables` menuselect.makeopts

	mod_disables="res_digium_phone chan_vpb"
	if [ $TESTED_ONLY -eq 1 ] ; then
		# These modules are not tested at all.  They are loaded but nothing is ever done
		# with them, no testsuite tests depend on them.
		mod_disables+=" app_adsiprog app_alarmreceiver app_celgenuserevent app_db app_dictate"
		mod_disables+=" app_dumpchan app_externalivr app_festival app_getcpeid app_ices app_image"
		mod_disables+=" app_jack app_milliwatt app_minivm app_morsecode app_mp3 app_nbscat app_privacy"
		mod_disables+=" app_readexten app_sms app_speech_utils app_test app_url app_waitforring"
		mod_disables+=" app_waitforsilence app_waituntil app_zapateller"
		mod_disables+=" cdr_adaptive_odbc cdr_custom cdr_manager cdr_odbc cdr_pgsql cdr_radius"
		mod_disables+=" cdr_syslog cdr_tds"
		mod_disables+=" cel_odbc cel_pgsql cel_radius cel_sqlite3_custom cel_tds"
		mod_disables+=" chan_alsa chan_console chan_mgcp chan_motif chan_oss chan_rtp chan_skinny chan_unistim"
		mod_disables+=" func_frame_trace func_pitchshift func_speex func_volume func_dialgroup"
		mod_disables+=" func_periodic_hook func_sprintf func_enum func_extstate func_sysinfo func_iconv"
		mod_disables+=" func_callcompletion func_version func_rand func_sha1 func_module func_md5"
		mod_disables+=" pbx_dundi pbx_loopback"
		mod_disables+=" res_ael_share res_calendar res_config_ldap res_config_pgsql res_corosync"
		mod_disables+=" res_http_post res_pktccops res_rtp_multicast res_snmp res_xmpp"
	fi

	runner menuselect/menuselect `gen_mods disable $mod_disables` menuselect.makeopts

	mod_enables="app_voicemail app_directory FILE_STORAGE"
	mod_enables+=" res_mwi_external res_ari_mailboxes res_mwi_external_ami res_stasis_mailbox"
	mod_enables+=" CORE-SOUNDS-EN-GSM MOH-OPSOUND-GSM EXTRA-SOUNDS-EN-GSM"
	runner menuselect/menuselect `gen_mods enable $mod_enables` menuselect.makeopts
fi

if [ $NO_MAKE -eq 0 ] ; then
runner ${MAKE} -j8 full || runner ${MAKE} -j1 NOISY_BUILD=yes full
fi

runner rm -f ${LCOV_DIR}/*.info
if [ $COVERAGE -eq 1 ] ; then
	runner mkdir -p ${LCOV_DIR}

	# Zero counter data
	runner lcov --quiet --directory . --zerocounters

	# Branch coverage is not supported by --initial.  Disable to suppresses a notice
	# printed if it was enabled in lcovrc.
	# This initial capture ensures any module which was built but never loaded is
	# reported with 0% coverage for all sources.
	runner lcov --quiet --directory . --no-external --capture --initial --rc lcov_branch_coverage=0 \
		--output-file ${LCOV_DIR}/initial.info
fi

if [ $NO_ALEMBIC -eq 0 ] ; then
	ALEMBIC=$(which alembic 2>/dev/null || : )
	if [ x"$ALEMBIC" = x ] ; then
		>&2 echo "Alembic not installed"
		exit 1
	fi

	find contrib/ast-db-manage -name *.pyc -delete
	out=$(run_alembic -c config.ini.sample branches)
	if [ "x$out" != "x" ] ; then
		>&2 echo "Alembic branches were found for config"
		>&2 echo $out
		exit 1
	fi
	run_alembic -c config.ini.sample upgrade head --sql > "${OUTPUT_DIR:+${OUTPUT_DIR}/}alembic-config.sql" || exit 1
	echo "Alembic for 'config' OK"

	out=$(run_alembic -c cdr.ini.sample branches)
	if [ "x$out" != "x" ] ; then
		>&2 echo "Alembic branches were found for cdr"
		>&2 echo $out
		exit 1
	fi
	run_alembic -c cdr.ini.sample upgrade head --sql > "${OUTPUT_DIR:+${OUTPUT_DIR}/}alembic-cdr.sql" || exit 1
	echo "Alembic for 'cdr' OK"

	out=$(run_alembic -c voicemail.ini.sample branches)
	if [ "x$out" != "x" ] ; then
		>&2 echo "Alembic branches were found for voicemail"
		>&2 echo $out
		exit 1
	fi
	run_alembic -c voicemail.ini.sample upgrade head --sql > "${OUTPUT_DIR:+${OUTPUT_DIR}/}alembic-voicemail.sql" || exit 1
	echo "Alembic for 'voicemail' OK"
fi

if [ -f "doc/core-en_US.xml" ] ; then
	runner ${MAKE} validate-docs || ${MAKE} NOISY_BUILD=yes validate-docs
fi
