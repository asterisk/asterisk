#
# Asterisk -- An open source telephony toolkit.
#
# Copyright (C) 2013, Russell Bryant
#
# Russell Bryant <russell@russellbryant.net>
#
# See http://www.asterisk.org for more information about
# the Asterisk project. Please do not directly contact
# any of the maintainers of this project for assistance;
# the project provides a web site, mailing lists and IRC
# channels for your use.
#
# This program is free software, distributed under the terms of
# the GNU General Public License Version 2. See the LICENSE file
# at the top of the source tree.
#

"""Create tables

Revision ID: 4da0c5f79a9c
Revises: None
Create Date: 2013-07-28 12:28:03.091587

"""

# revision identifiers, used by Alembic.
revision = '4da0c5f79a9c'
down_revision = None

from alembic import op
import sqlalchemy as sa
from sqlalchemy.sql import quoted_name
from sqlalchemy.dialects.postgresql import ENUM

YESNO_VALUES = ['yes', 'no']
TYPE_VALUES = ['friend', 'user', 'peer']

SIP_TRANSPORT_VALUES = ['udp', 'tcp', 'tls', 'ws', 'wss', 'udp,tcp', 'tcp,udp']
SIP_DTMFMODE_VALUES = ['rfc2833', 'info', 'shortinfo', 'inband', 'auto']
SIP_DIRECTMEDIA_VALUES = ['yes', 'no', 'nonat', 'update']
SIP_PROGRESSINBAND_VALUES = ['yes', 'no', 'never']
SIP_SESSION_TIMERS_VALUES = ['accept', 'refuse', 'originate']
SIP_SESSION_REFRESHER_VALUES = ['uac', 'uas']
SIP_CALLINGPRES_VALUES = ['allowed_not_screened', 'allowed_passed_screen',
                          'allowed_failed_screen', 'allowed',
                          'prohib_not_screened', 'prohib_passed_screen',
                          'prohib_failed_screen', 'prohib']

IAX_REQUIRECALLTOKEN_VALUES = ['yes', 'no', 'auto']
IAX_ENCRYPTION_VALUES = ['yes', 'no', 'aes128']
IAX_TRANSFER_VALUES = ['yes', 'no', 'mediaonly']

MOH_MODE_VALUES = ['custom', 'files', 'mp3nb', 'quietmp3nb', 'quietmp3']


def upgrade():
    op.create_table(
        'sippeers',
        sa.Column('id', sa.Integer, primary_key=True, nullable=False,
                  autoincrement=True),
        sa.Column('name', sa.String(40), nullable=False, unique=True),
        sa.Column('ipaddr', sa.String(45)),
        sa.Column('port', sa.Integer),
        sa.Column('regseconds', sa.Integer),
        sa.Column('defaultuser', sa.String(40)),
        sa.Column('fullcontact', sa.String(80)),
        sa.Column('regserver', sa.String(20)),
        sa.Column('useragent', sa.String(20)),
        sa.Column('lastms', sa.Integer),
        sa.Column('host', sa.String(40)),
        sa.Column('type', sa.Enum(*TYPE_VALUES, name='type_values')),
        sa.Column('context', sa.String(40)),
        sa.Column('permit', sa.String(95)),
        sa.Column('deny', sa.String(95)),
        sa.Column('secret', sa.String(40)),
        sa.Column('md5secret', sa.String(40)),
        sa.Column('remotesecret', sa.String(40)),
        sa.Column('transport', sa.Enum(*SIP_TRANSPORT_VALUES,
                  name='sip_transport_values')),
        sa.Column('dtmfmode', sa.Enum(*SIP_DTMFMODE_VALUES,
                  name='sip_dtmfmode_values')),
        sa.Column('directmedia', sa.Enum(*SIP_DIRECTMEDIA_VALUES,
                  name='sip_directmedia_values')),
        sa.Column('nat', sa.String(29)),
        sa.Column('callgroup', sa.String(40)),
        sa.Column('pickupgroup', sa.String(40)),
        sa.Column('language', sa.String(40)),
        sa.Column('disallow', sa.String(200)),
        sa.Column('allow', sa.String(200)),
        sa.Column('insecure', sa.String(40)),
        sa.Column('trustrpid', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('progressinband', sa.Enum(*SIP_PROGRESSINBAND_VALUES,
                  name='sip_progressinband_values')),
        sa.Column('promiscredir', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('useclientcode', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('accountcode', sa.String(40)),
        sa.Column('setvar', sa.String(200)),
        sa.Column('callerid', sa.String(40)),
        sa.Column('amaflags', sa.String(40)),
        sa.Column('callcounter', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('busylevel', sa.Integer),
        sa.Column('allowoverlap', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('allowsubscribe', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('videosupport', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('maxcallbitrate', sa.Integer),
        sa.Column('rfc2833compensate', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('mailbox', sa.String(40)),
        sa.Column('session-timers', sa.Enum(*SIP_SESSION_TIMERS_VALUES,
                  name='sip_session_timers_values')),
        sa.Column('session-expires', sa.Integer),
        sa.Column('session-minse', sa.Integer),
        sa.Column('session-refresher', sa.Enum(*SIP_SESSION_REFRESHER_VALUES,
                  name='sip_session_refresher_values')),
        sa.Column('t38pt_usertpsource', sa.String(40)),
        sa.Column('regexten', sa.String(40)),
        sa.Column('fromdomain', sa.String(40)),
        sa.Column('fromuser', sa.String(40)),
        sa.Column(quoted_name('qualify', True), sa.String(40)),
        sa.Column('defaultip', sa.String(45)),
        sa.Column('rtptimeout', sa.Integer),
        sa.Column('rtpholdtimeout', sa.Integer),
        sa.Column('sendrpid', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('outboundproxy', sa.String(40)),
        sa.Column('callbackextension', sa.String(40)),
        sa.Column('timert1', sa.Integer),
        sa.Column('timerb', sa.Integer),
        sa.Column('qualifyfreq', sa.Integer),
        sa.Column('constantssrc', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('contactpermit', sa.String(95)),
        sa.Column('contactdeny', sa.String(95)),
        sa.Column('usereqphone', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('textsupport', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('faxdetect', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('buggymwi', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('auth', sa.String(40)),
        sa.Column('fullname', sa.String(40)),
        sa.Column('trunkname', sa.String(40)),
        sa.Column('cid_number', sa.String(40)),
        sa.Column('callingpres', sa.Enum(*SIP_CALLINGPRES_VALUES,
                  name='sip_callingpres_values')),
        sa.Column('mohinterpret', sa.String(40)),
        sa.Column('mohsuggest', sa.String(40)),
        sa.Column('parkinglot', sa.String(40)),
        sa.Column('hasvoicemail', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('subscribemwi', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('vmexten', sa.String(40)),
        sa.Column('autoframing', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('rtpkeepalive', sa.Integer),
        sa.Column('call-limit', sa.Integer),
        sa.Column('g726nonstandard', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('ignoresdpversion', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('allowtransfer', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('dynamic', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('path', sa.String(256)),
        sa.Column('supportpath', sa.Enum(*YESNO_VALUES, name='yes_no_values'))
    )
    op.create_index('sippeers_name', 'sippeers', ['name'])
    op.create_index('sippeers_name_host', 'sippeers', ['name', 'host'])
    op.create_index('sippeers_ipaddr_port', 'sippeers', ['ipaddr', 'port'])
    op.create_index('sippeers_host_port', 'sippeers', ['host', 'port'])

    op.create_table(
        'iaxfriends',
        sa.Column('id', sa.Integer, primary_key=True, nullable=False,
                  autoincrement=True),
        sa.Column('name', sa.String(40), nullable=False, unique=True),
        sa.Column('type', sa.Enum(*TYPE_VALUES, name='type_values')),
        sa.Column('username', sa.String(40)),
        sa.Column('mailbox', sa.String(40)),
        sa.Column('secret', sa.String(40)),
        sa.Column('dbsecret', sa.String(40)),
        sa.Column('context', sa.String(40)),
        sa.Column('regcontext', sa.String(40)),
        sa.Column('host', sa.String(40)),
        sa.Column('ipaddr', sa.String(40)),
        sa.Column('port', sa.Integer),
        sa.Column('defaultip', sa.String(20)),
        sa.Column('sourceaddress', sa.String(20)),
        sa.Column('mask', sa.String(20)),
        sa.Column('regexten', sa.String(40)),
        sa.Column('regseconds', sa.Integer),
        sa.Column('accountcode', sa.String(20)),
        sa.Column('mohinterpret', sa.String(20)),
        sa.Column('mohsuggest', sa.String(20)),
        sa.Column('inkeys', sa.String(40)),
        sa.Column('outkeys', sa.String(40)),
        sa.Column('language', sa.String(10)),
        sa.Column('callerid', sa.String(100)),
        sa.Column('cid_number', sa.String(40)),
        sa.Column('sendani', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('fullname', sa.String(40)),
        sa.Column('trunk', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('auth', sa.String(20)),
        sa.Column('maxauthreq', sa.Integer),
        sa.Column('requirecalltoken', sa.Enum(*IAX_REQUIRECALLTOKEN_VALUES,
                  name='iax_requirecalltoken_values')),
        sa.Column('encryption', sa.Enum(*IAX_ENCRYPTION_VALUES,
                  name='iax_encryption_values')),
        sa.Column('transfer', sa.Enum(*IAX_TRANSFER_VALUES,
                  name='iax_transfer_values')),
        sa.Column('jitterbuffer', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('forcejitterbuffer', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('disallow', sa.String(200)),
        sa.Column('allow', sa.String(200)),
        sa.Column('codecpriority', sa.String(40)),
        sa.Column(quoted_name('qualify', True), sa.String(10)),
        sa.Column('qualifysmoothing',
                  sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('qualifyfreqok', sa.String(10)),
        sa.Column('qualifyfreqnotok', sa.String(10)),
        sa.Column('timezone', sa.String(20)),
        sa.Column('adsi', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('amaflags', sa.String(20)),
        sa.Column('setvar', sa.String(200))
    )
    op.create_index('iaxfriends_name', 'iaxfriends', ['name'])
    op.create_index('iaxfriends_name_host', 'iaxfriends', ['name', 'host'])
    op.create_index('iaxfriends_name_ipaddr_port', 'iaxfriends',
                    ['name', 'ipaddr', 'port'])
    op.create_index('iaxfriends_ipaddr_port', 'iaxfriends', ['ipaddr', 'port'])
    op.create_index('iaxfriends_host_port', 'iaxfriends', ['host', 'port'])

    op.create_table(
        'voicemail',
        sa.Column('uniqueid', sa.Integer, primary_key=True, nullable=False,
                  autoincrement=True),
        sa.Column('context', sa.String(80), nullable=False),
        sa.Column('mailbox', sa.String(80), nullable=False),
        sa.Column('password', sa.String(80), nullable=False),
        sa.Column('fullname', sa.String(80)),
        sa.Column('alias', sa.String(80)),
        sa.Column('email', sa.String(80)),
        sa.Column('pager', sa.String(80)),
        sa.Column('attach', sa.Enum(*YESNO_VALUES, name='yes_no_values')),
        sa.Column('attachfmt', sa.String(10)),
        sa.Column('serveremail', sa.String(80)),
        sa.Column('language', sa.String(20)),
        sa.Column('tz', sa.String(30)),
        sa.Column('deletevoicemail', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('saycid', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('sendvoicemail', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('review', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('tempgreetwarn', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('operator', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('envelope', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('sayduration', sa.Integer),
        sa.Column('forcename', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('forcegreetings', sa.Enum(*YESNO_VALUES,
                  name='yes_no_values')),
        sa.Column('callback', sa.String(80)),
        sa.Column('dialout', sa.String(80)),
        sa.Column('exitcontext', sa.String(80)),
        sa.Column('maxmsg', sa.Integer),
        sa.Column('volgain', sa.Numeric(precision=5, scale=2)),
        sa.Column('imapuser', sa.String(80)),
        sa.Column('imappassword', sa.String(80)),
        sa.Column('imapserver', sa.String(80)),
        sa.Column('imapport', sa.String(8)),
        sa.Column('imapflags', sa.String(80)),
        sa.Column('stamp', sa.DateTime())
    )
    op.create_index('voicemail_mailbox', 'voicemail', ['mailbox'])
    op.create_index('voicemail_context', 'voicemail', ['context'])
    op.create_index('voicemail_mailbox_context', 'voicemail',
                    ['mailbox', 'context'])
    op.create_index('voicemail_imapuser', 'voicemail', ['imapuser'])

    op.create_table(
        'meetme',
        sa.Column('bookid', sa.Integer, primary_key=True, nullable=False,
                  autoincrement=True),
        sa.Column('confno', sa.String(80), nullable=False),
        sa.Column('starttime', sa.DateTime()),
        sa.Column('endtime', sa.DateTime()),
        sa.Column('pin', sa.String(20)),
        sa.Column('adminpin', sa.String(20)),
        sa.Column('opts', sa.String(20)),
        sa.Column('adminopts', sa.String(20)),
        sa.Column('recordingfilename', sa.String(80)),
        sa.Column('recordingformat', sa.String(10)),
        sa.Column('maxusers', sa.Integer),
        sa.Column('members', sa.Integer, nullable=False, default=0)
    )
    op.create_index('meetme_confno_start_end', 'meetme',
                    ['confno', 'starttime', 'endtime'])

    op.create_table(
        'musiconhold',
        sa.Column('name', sa.String(80), primary_key=True, nullable=False),
        sa.Column('mode', sa.Enum(*MOH_MODE_VALUES, name='moh_mode_values')),
        sa.Column('directory', sa.String(255)),
        sa.Column('application', sa.String(255)),
        sa.Column('digit', sa.String(1)),
        sa.Column('sort', sa.String(10)),
        sa.Column('format', sa.String(10)),
        sa.Column('stamp', sa.DateTime())
    )


def downgrade():
    context = op.get_context()

    op.drop_table('sippeers')
    op.drop_table('iaxfriends')
    op.drop_table('voicemail')
    op.drop_table('meetme')
    op.drop_table('musiconhold')

    enums = ['type_values', 'yes_no_values',
             'sip_transport_values','sip_dtmfmode_values','sip_directmedia_values',
             'sip_progressinband_values','sip_session_timers_values','sip_session_refresher_values',
             'sip_callingpres_values','iax_requirecalltoken_values','iax_encryption_values',
             'iax_transfer_values','moh_mode_values']

    if context.bind.dialect.name == 'postgresql':
        for e in enums:
            ENUM(name=e).drop(op.get_bind(), checkfirst=False)
