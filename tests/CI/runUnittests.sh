#!/usr/bin/env bash
CIDIR=$(dirname $(readlink -fn $0))
NO_EXPECT=0
source $CIDIR/ci.functions
ASTETCDIR=$DESTDIR/etc/asterisk

asterisk_corefile_glob() {
	local pattern=$(/sbin/sysctl -n kernel.core_pattern)

	# If core_pattern is a pipe there isn't much we can do
	if [[ ${pattern:0:1} == "|" ]] ; then
		echo "core*"
	else
		echo "${pattern%%%*}*"
	fi
}

run_tests_expect() {
$EXPECT <<-EOF
	spawn sudo $ASTERISK ${USER_GROUP:+-U ${USER_GROUP%%:*} -G ${USER_GROUP##*:}} -fcng -C $CONFFILE
	match_max 512
	set timeout 600
	expect -notransfer "Asterisk Ready."
	send "core show settings\r"
	expect -notransfer "CLI>"
	send "${UNITTEST_COMMAND:-test execute all}\r"
	expect -notransfer -ex "Test(s) Executed"
	expect -notransfer "CLI>"
	send "test show results failed\r"
	expect -notransfer "CLI>"
	send "test generate results xml ${OUTPUTFILE}\r"
	expect -notransfer "CLI>"
	send "core stop now\r"
	expect -notransfer "Executing last minute cleanups"
	wait
EOF
}

run_tests_socket() {
	sudo $ASTERISK ${USER_GROUP:+-U ${USER_GROUP%%:*} -G ${USER_GROUP##*:}} -gn -C $CONFFILE
	for n in {1..5} ; do
		sleep 3
		$ASTERISK -rx "core waitfullybooted" -C $CONFFILE && break
	done
	sleep 1
	$ASTERISK -rx "core show settings" -C $CONFFILE
	$ASTERISK -rx "${UNITTEST_COMMAND:-test execute all}" -C $CONFFILE
	$ASTERISK -rx "test show results failed" -C $CONFFILE
	$ASTERISK -rx "test generate results xml $OUTPUTFILE" -C $CONFFILE
	$ASTERISK -rx "core stop now" -C $CONFFILE
}

# If DESTDIR is used to install and run asterisk from non standard locations,
# the directory entries in asterisk.conf need to be munged to prepend DESTDIR.
ALTERED=$(head -10 ../tmp/DESTDIR/etc/asterisk/asterisk.conf | grep -q "DESTDIR" && echo yes)
if [ x"$ALTERED" = x ] ; then
	# In the section that starts with [directories and ends with a blank line,
	# replace "=> " with "=> ${DESTDIR}"
	sed -i -r -e "/^\[directories/,/^$/ s@=>\s+@=> ${DESTDIR}@" "$ASTETCDIR/asterisk.conf"
fi

cat <<-EOF > "$ASTETCDIR/logger.conf"
	[logfiles]
	full => notice,warning,error,debug,verbose
	console => notice,warning,error
EOF

echo "[default]" > "$ASTETCDIR/extensions.conf"

cat <<-EOF > "$ASTETCDIR/manager.conf"
	[general]
	enabled=yes
	bindaddr=127.0.0.1
	port=5038

	[test]
	secret=test
	read = system,call,log,verbose,agent,user,config,dtmf,reporting,cdr,dialplan
	write = system,call,agent,user,config,command,reporting,originate
EOF

cat <<-EOF > "$ASTETCDIR/http.conf"
	[general]
	enabled=yes
	bindaddr=127.0.0.1
	bindport=8088
EOF

cat <<-EOF > "$ASTETCDIR/modules.conf"
	[modules]
	autoload=yes
	noload=res_mwi_external.so
	noload=res_mwi_external_ami.so
	noload=res_ari_mailboxes.so
	noload=res_stasis_mailbox.so
EOF

cat <<-EOF >> "$ASTETCDIR/sorcery.conf"
	[res_pjsip_pubsub]
	resource_list=memory
EOF

ASTERISK="$DESTDIR/usr/sbin/asterisk"
CONFFILE=$ASTETCDIR/asterisk.conf
OUTPUTDIR=${OUTPUT_DIR:-tests/CI/output/}
OUTPUTFILE=${OUTPUT_XML:-${OUTPUTDIR}/unittests-results.xml}
EXPECT="$(which expect 2>/dev/null || : )"

[ ! -d ${OUTPUTDIR} ] && mkdir -p $OUTPUTDIR
[ x"$USER_GROUP" != x ] && sudo chown -R $USER_GROUP $OUTPUTDIR

rm -rf $ASTETCDIR/extensions.{ael,lua} || :

set -x
if [ x"$EXPECT" != x -a $NO_EXPECT -eq 0 ] ; then
	run_tests_expect
else
	run_tests_socket
fi

# Cleanup "just in case"
sudo killall -qe -ABRT $ASTERISK 

runner rsync -vaH $DESTDIR/var/log/asterisk/. $OUTPUTDIR
set +x

[ x"$USER_GROUP" != x ] && sudo chown -R $USER_GROUP $OUTPUTDIR

for core in $(asterisk_corefile_glob)
do
	if [ -f $core ]
	then
		echo "*** Found a core file ($core) after running unit tests ***"
		set -x
		sudo OUTPUTDIR=$OUTPUTDIR $DESTDIR/var/lib/asterisk/scripts/ast_coredumper --no-default-search $core
	fi
done

exit 0
