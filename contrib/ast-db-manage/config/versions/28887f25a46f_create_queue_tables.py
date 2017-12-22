#
# Asterisk -- An open source telephony toolkit.
#
# Copyright (C) 2014, Richard Mudgett
#
# Richard Mudgett <rmudgett@digium.com>
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

"""Create queue tables

Revision ID: 28887f25a46f
Revises: 21e526ad3040
Create Date: 2014-03-03 12:26:25.261640

"""

# revision identifiers, used by Alembic.
revision = '28887f25a46f'
down_revision = '21e526ad3040'

from alembic import op
from alembic import context
import sqlalchemy as sa
from sqlalchemy.dialects.postgresql import ENUM


YESNO_NAME = 'yesno_values'
YESNO_VALUES = ['yes', 'no']

QUEUE_STRATEGY_NAME = 'queue_strategy_values'
QUEUE_STRATEGY_VALUES = [ 'ringall', 'leastrecent', 'fewestcalls', 'random', 'rrmemory',
    'linear', 'wrandom', 'rrordered', ]

QUEUE_AUTOPAUSE_NAME = 'queue_autopause_values'
QUEUE_AUTOPAUSE_VALUES = ['yes', 'no', 'all']


def upgrade():
    ############################# Enums ##############################

    # yesno_values have already been created, so use postgres enum object
    # type to get around "already created" issue - works okay with mysql
    yesno_values = ENUM(*YESNO_VALUES, name=YESNO_NAME, create_type=False)

    queue_strategy_values = sa.Enum(*QUEUE_STRATEGY_VALUES, name=QUEUE_STRATEGY_NAME)
    queue_autopause_values = sa.Enum(*QUEUE_AUTOPAUSE_VALUES, name=QUEUE_AUTOPAUSE_NAME)

    ######################### create tables ##########################

    op.create_table(
        'queues',
        sa.Column('name', sa.String(128), primary_key=True, nullable=False),
        sa.Column('musiconhold', sa.String(128)),
        sa.Column('announce', sa.String(128)),
        sa.Column('context', sa.String(128)),
        sa.Column('timeout', sa.Integer),
        sa.Column('ringinuse', yesno_values),
        sa.Column('setinterfacevar', yesno_values),
        sa.Column('setqueuevar', yesno_values),
        sa.Column('setqueueentryvar', yesno_values),
        sa.Column('monitor_format', sa.String(8)),
        sa.Column('membermacro', sa.String(512)),
        sa.Column('membergosub', sa.String(512)),
        sa.Column('queue_youarenext', sa.String(128)),
        sa.Column('queue_thereare', sa.String(128)),
        sa.Column('queue_callswaiting', sa.String(128)),
        sa.Column('queue_quantity1', sa.String(128)),
        sa.Column('queue_quantity2', sa.String(128)),
        sa.Column('queue_holdtime', sa.String(128)),
        sa.Column('queue_minutes', sa.String(128)),
        sa.Column('queue_minute', sa.String(128)),
        sa.Column('queue_seconds', sa.String(128)),
        sa.Column('queue_thankyou', sa.String(128)),
        sa.Column('queue_callerannounce', sa.String(128)),
        sa.Column('queue_reporthold', sa.String(128)),
        sa.Column('announce_frequency', sa.Integer),
        sa.Column('announce_to_first_user', yesno_values),
        sa.Column('min_announce_frequency', sa.Integer),
        sa.Column('announce_round_seconds', sa.Integer),
        sa.Column('announce_holdtime', sa.String(128)),
        sa.Column('announce_position', sa.String(128)),
        sa.Column('announce_position_limit', sa.Integer),
        sa.Column('periodic_announce', sa.String(50)),
        sa.Column('periodic_announce_frequency', sa.Integer),
        sa.Column('relative_periodic_announce', yesno_values),
        sa.Column('random_periodic_announce', yesno_values),
        sa.Column('retry', sa.Integer),
        sa.Column('wrapuptime', sa.Integer),
        sa.Column('penaltymemberslimit', sa.Integer),
        sa.Column('autofill', yesno_values),
        sa.Column('monitor_type', sa.String(128)),
        sa.Column('autopause', queue_autopause_values),
        sa.Column('autopausedelay', sa.Integer),
        sa.Column('autopausebusy', yesno_values),
        sa.Column('autopauseunavail', yesno_values),
        sa.Column('maxlen', sa.Integer),
        sa.Column('servicelevel', sa.Integer),
        sa.Column('strategy', queue_strategy_values),
        sa.Column('joinempty', sa.String(128)),
        sa.Column('leavewhenempty', sa.String(128)),
        sa.Column('reportholdtime', yesno_values),
        sa.Column('memberdelay', sa.Integer),
        sa.Column('weight', sa.Integer),
        sa.Column('timeoutrestart', yesno_values),
        sa.Column('defaultrule', sa.String(128)),
        sa.Column('timeoutpriority', sa.String(128))
    )

    op.create_table(
        'queue_members',
        sa.Column('queue_name', sa.String(80), primary_key=True, nullable=False),
        sa.Column('interface', sa.String(80), primary_key=True, nullable=False),
        sa.Column('uniqueid', sa.String(80), nullable=False),
        sa.Column('membername', sa.String(80)),
        sa.Column('state_interface', sa.String(80)),
        sa.Column('penalty', sa.Integer),
        sa.Column('paused', sa.Integer)
    )


def downgrade():
    ########################## drop tables ###########################

    op.drop_table('queues')
    op.drop_table('queue_members')

    ########################## drop enums ############################

    sa.Enum(name=QUEUE_STRATEGY_NAME).drop(op.get_bind(), checkfirst=False)
    sa.Enum(name=QUEUE_AUTOPAUSE_NAME).drop(op.get_bind(), checkfirst=False)
