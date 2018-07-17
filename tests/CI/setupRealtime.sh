#!/usr/bin/env bash
CIDIR=$(dirname $(readlink -fn $0))
source $CIDIR/ci.functions

set -e

cp test-config.yaml test-config.orig.yaml

cat >test-config.yaml <<-EOF
	global-settings:
	    test-configuration: config-realtime

	    condition-definitions:
	        -
	            name: 'threads'
	            pre:
	                typename: 'thread_test_condition.ThreadPreTestCondition'
	            post:
	                typename: 'thread_test_condition.ThreadPostTestCondition'
	                related-type: 'thread_test_condition.ThreadPreTestCondition'
	        -
	            name: 'sip-dialogs'
	            pre:
	                typename: 'sip_dialog_test_condition.SipDialogPreTestCondition'
	            post:
	                typename: 'sip_dialog_test_condition.SipDialogPostTestCondition'
	        -
	            name: 'locks'
	            pre:
	                typename: 'lock_test_condition.LockTestCondition'
	            post:
	                typename: 'lock_test_condition.LockTestCondition'
	        -
	            name: 'file-descriptors'
	            pre:
	                typename: 'fd_test_condition.FdPreTestCondition'
	            post:
	                typename: 'fd_test_condition.FdPostTestCondition'
	                related-type: 'fd_test_condition.FdPreTestCondition'
	        -
	            name: 'channels'
	            pre:
	                typename: 'channel_test_condition.ChannelTestCondition'
	            post:
	                typename: 'channel_test_condition.ChannelTestCondition'
	        -
	            name: 'sip-channels'
	            pre:
	                typename: 'sip_channel_test_condition.SipChannelTestCondition'
	            post:
	                typename: 'sip_channel_test_condition.SipChannelTestCondition'
	        -
	            name: 'memory'
	            pre:
	                typename: 'memory_test_condition.MemoryPreTestCondition'
	            post:
	                typename: 'memory_test_condition.MemoryPostTestCondition'
	                related-type: 'memory_test_condition.MemoryPreTestCondition'

	config-realtime:
	    test-modules:
	        modules:
	            -
	                typename: realtime_converter.RealtimeConverter
	                config-section: realtime-config
		
	    realtime-config:
	        username: "asterisk"
	        host: "localhost"
	        db: "asterisk"
	        dsn: "asterisk-connector"
EOF

ASTTOP=$(readlink -fn $CIDIR/../../)

cat >/tmp/config.ini <<-EOF
	[alembic]
	script_location = config
	sqlalchemy.url = postgresql://asterisk@localhost/asterisk
		
	[loggers]
	keys = root,sqlalchemy,alembic
		
	[handlers]
	keys = console
		
	[formatters]
	keys = generic
		
	[logger_root]
	level = WARN
	handlers = console
	qualname =
		
	[logger_sqlalchemy]
	level = WARN
	handlers =
	qualname = sqlalchemy.engine
		
	[logger_alembic]
	level = INFO
	handlers =
	qualname = alembic
		
	[handler_console]
	class = StreamHandler
	args = (sys.stderr,)
	level = NOTSET
	formatter = generic
		
	[formatter_generic]
	format = %(levelname)-5.5s [%(name)s] %(message)s
	datefmt = %H:%M:%S
EOF

pushd $ASTTOP/contrib/ast-db-manage
if [ -x /usr/local/bin/postgresql-start ] ; then
	/usr/local/bin/postgresql-start
fi
psql --username=asterisk --host=localhost --db=asterisk --command='DROP OWNED BY asterisk CASCADE'
alembic -c /tmp/config.ini upgrade head
rm -rf /tmp/config.ini || :
popd
